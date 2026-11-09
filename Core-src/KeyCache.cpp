#include "KeyCache.h"
#include <math.h>
#include <string.h>
#include <algorithm>

void Mixer_RenderClips(const std::vector<MixClip> &clips, bool xfade, float *dst, int n, double t0, double tstep);

namespace Key {

// ------------------------------------------------------------------ подпись

static inline void Mix64(uint64_t &h, uint64_t v)
{
	h ^= v;
	h *= 1099511628211ULL;
}

static inline uint64_t Bits(double d) { uint64_t u; memcpy(&u, &d, sizeof(u)); return u; }
static inline uint64_t Bits(float d) { uint32_t u; memcpy(&u, &d, sizeof(u)); return u; }

uint64_t Signature(const std::vector<MixClip> &clips, bool xfade)
{
	uint64_t h = 14695981039346656037ULL;
	Mix64(h, xfade ? 1 : 0);
	Mix64(h, (uint64_t)clips.size());
	for (size_t i = 0; i < clips.size(); i++) {
		const MixClip &c = clips[i];
		Mix64(h, Bits(c.pos));
		Mix64(h, (uint64_t)c.start);
		Mix64(h, (uint64_t)c.len);
		if (!c.src) { Mix64(h, 0); continue; }
		Mix64(h, (uint64_t)c.src->frames);
		Mix64(h, (uint64_t)c.src->channels);
		Mix64(h, Bits(c.src->rate));
		// выборка отсчётов из используемой части (правка звука без сдвига клипа тоже меняет подпись)
		const int ch = c.src->channels;
		const int64_t total = c.len * ch;
		const int samples = 128;
		for (int k = 0; k < samples && total > 0; k++) {
			const int64_t idx = c.start * ch + (int64_t)((double)k * (double)(total - 1) / (samples - 1));
			Mix64(h, Bits(c.src->data[idx]));
		}
	}
	return h;
}

// ------------------------------------------------------------------ построение

namespace {

struct Biquad {
	double b0, b1, b2, a1, a2, z1, z2;
	Biquad() : b0(1), b1(0), b2(0), a1(0), a2(0), z1(0), z2(0) {}
	void LowPass(double f, double q, double rate)
	{
		if (f > rate * 0.45) f = rate * 0.45;
		const double w = 2.0 * M_PI * f / rate, c = cos(w), s = sin(w), alpha = s / (2.0 * q);
		const double a0 = 1.0 + alpha;
		b0 = (1.0 - c) * 0.5 / a0; b1 = (1.0 - c) / a0; b2 = b0;
		a1 = -2.0 * c / a0; a2 = (1.0 - alpha) / a0;
		z1 = z2 = 0;
	}
	inline float Run(float xf)
	{
		const double x = xf, y = b0 * x + z1;
		z1 = b1 * x - a1 * y + z2;
		z2 = b2 * x - a2 * y;
		return (float)y;
	}
};

}	// namespace

bool BuildEnvelope(const std::vector<MixClip> &clips, bool xfade, int range, float cutoffHz,
	Envelope &out, const std::function<bool()> &cancelled)
{
	const double rate = 44100.0;
	double end = 0;
	for (size_t i = 0; i < clips.size(); i++) {
		if (!clips[i].src || clips[i].len <= 0) continue;
		if (clips[i].End() > end) end = clips[i].End();
	}
	out.peak.assign((size_t)ceil(end * kEnvRate) + 2, 0.0f);
	out.signature = Signature(clips, xfade);
	if (end <= 0) return true;

	Biquad lp1, lp2;
	if (range == RANGE_LOW) {
		const double f = cutoffHz < 20 ? 20 : cutoffHz;
		lp1.LowPass(f, 0.5412, rate);			// Баттерворт 4-го порядка: две секции
		lp2.LowPass(f, 1.3066, rate);
	}
	const int kBlock = 4096;
	std::vector<float> blk((size_t)kBlock * 2);
	const int64_t total = (int64_t)(end * rate) + 1;
	for (int64_t from = 0; from < total; from += kBlock) {
		if (cancelled && cancelled()) return false;
		const int n = (int)(total - from < kBlock ? total - from : kBlock);
		std::fill(blk.begin(), blk.begin() + (size_t)n * 2, 0.0f);
		Mixer_RenderClips(clips, xfade, &blk[0], n, (double)from / rate, 1.0 / rate);
		for (int k = 0; k < n; k++) {
			const size_t b = (size_t)(((double)(from + k) / rate) * kEnvRate);
			if (b >= out.peak.size()) break;
			float v;
			if (range == RANGE_LOW) v = fabsf(lp2.Run(lp1.Run(0.5f * (blk[2 * k] + blk[2 * k + 1]))));
			else v = fmaxf(fabsf(blk[2 * k]), fabsf(blk[2 * k + 1]));
			if (v > out.peak[b]) out.peak[b] = v;
		}
	}
	return true;
}

// ------------------------------------------------------------------ кэш

Cache::Cache() : fGen(0), fQuit(false) {}

Cache::~Cache()
{
	fQuit = true;
	JoinAll();
}

Cache &Cache::Global()
{
	static Cache c;
	return c;
}

void Cache::JoinAll()
{
	std::vector<std::thread> t;
	{
		std::lock_guard<std::mutex> g(fMutex);
		t.swap(fThreads);
	}
	for (size_t i = 0; i < t.size(); i++) if (t[i].joinable()) t[i].join();
}

int64_t Cache::Key(int lane, int range, float cutoffHz)
{
	const int c = range == RANGE_LOW ? (int)(cutoffHz + 0.5f) : 0;
	return ((int64_t)(lane + 1) * 4 + range) * 100000 + c;
}

void Cache::SetLaneSource(const LaneSource &src)
{
	std::lock_guard<std::mutex> g(fMutex);
	fSource = src;
}

void Cache::Clear()
{
	fQuit = true;
	JoinAll();
	fQuit = false;
	std::lock_guard<std::mutex> g(fMutex);
	fEntries.clear();
	fGen++;
}

Status Cache::Peek(int lane, int range, float cutoffHz) const
{
	std::lock_guard<std::mutex> g(fMutex);
	std::map<int64_t, Entry>::const_iterator it = fEntries.find(Key(lane, range, cutoffHz));
	return it == fEntries.end() ? ST_NONE : it->second.st;
}

Status Cache::Check(int lane, int range, float cutoffHz)
{
	LaneSource src;
	{
		std::lock_guard<std::mutex> g(fMutex);
		src = fSource;
	}
	std::vector<MixClip> clips; bool xf = false;
	uint64_t sig = 0; bool have = false;
	if (src && src(lane, clips, xf)) { sig = Signature(clips, xf); have = true; }
	std::lock_guard<std::mutex> g(fMutex);
	std::map<int64_t, Entry>::iterator it = fEntries.find(Key(lane, range, cutoffHz));
	if (it == fEntries.end()) return ST_NONE;
	if (it->second.st == ST_READY && have && it->second.env && it->second.env->signature != sig)
		it->second.st = ST_STALE;
	return it->second.st;
}

void Cache::Finish(int64_t key, int gen, std::shared_ptr<Envelope> env, bool ok)
{
	std::lock_guard<std::mutex> g(fMutex);
	std::map<int64_t, Entry>::iterator it = fEntries.find(key);
	if (it == fEntries.end() || it->second.gen != gen) return;		// пока строили, запросили новую сборку
	if (ok) { it->second.env = env; it->second.st = ST_READY; }
	else it->second.st = it->second.env ? ST_STALE : ST_NONE;
}

void Cache::Request(int lane, int range, float cutoffHz, bool force)
{
	LaneSource src;
	{
		std::lock_guard<std::mutex> g(fMutex);
		src = fSource;
	}
	std::vector<MixClip> clips; bool xf = false;
	if (!src || !src(lane, clips, xf)) return;
	const uint64_t sig = Signature(clips, xf);
	const int64_t key = Key(lane, range, cutoffHz);
	int gen;
	{
		std::lock_guard<std::mutex> g(fMutex);
		Entry &e = fEntries[key];
		if (!force && (e.st == ST_BUILDING || (e.st == ST_READY && e.env && e.env->signature == sig))) return;
		e.st = ST_BUILDING;
		gen = ++e.gen;
	}
	std::thread th([this, key, gen, clips, xf, range, cutoffHz]() {
		std::shared_ptr<Envelope> env(new Envelope());
		const bool ok = BuildEnvelope(clips, xf, range, cutoffHz, *env, [this]() { return fQuit.load(); });
		Finish(key, gen, env, ok);
	});
	std::lock_guard<std::mutex> g(fMutex);
	fThreads.push_back(std::move(th));
}

bool Cache::BuildNow(int lane, int range, float cutoffHz)
{
	LaneSource src;
	{
		std::lock_guard<std::mutex> g(fMutex);
		src = fSource;
	}
	std::vector<MixClip> clips; bool xf = false;
	if (!src || !src(lane, clips, xf)) return false;
	const int64_t key = Key(lane, range, cutoffHz);
	int gen;
	{
		std::lock_guard<std::mutex> g(fMutex);
		Entry &e = fEntries[key];
		if (e.st == ST_READY && e.env && e.env->signature == Signature(clips, xf)) return true;
		e.st = ST_BUILDING;
		gen = ++e.gen;
	}
	std::shared_ptr<Envelope> env(new Envelope());
	const bool ok = BuildEnvelope(clips, xf, range, cutoffHz, *env);
	Finish(key, gen, env, ok);
	return ok;
}

std::shared_ptr<const Envelope> Cache::Get(int lane, int range, float cutoffHz) const
{
	std::unique_lock<std::mutex> g(fMutex, std::try_to_lock);
	if (!g.owns_lock()) return std::shared_ptr<const Envelope>();
	std::map<int64_t, Entry>::const_iterator it = fEntries.find(Key(lane, range, cutoffHz));
	if (it == fEntries.end() || (it->second.st != ST_READY && it->second.st != ST_STALE) || !it->second.env)
		return std::shared_ptr<const Envelope>();
	return it->second.env;				// устаревшая огибающая всё ещё работает (предупреждает окно плагина)
}

// ------------------------------------------------------------------ контекст

static thread_local const Context *sCurrent = 0;
static std::atomic<uint64_t> sCtxId(0);

const Context *CurrentContext() { return sCurrent; }

Scope::Scope(double t0, double step, const Cache *cache) : fPrev(sCurrent)
{
	fCtx.cache = cache; fCtx.t0 = t0; fCtx.step = step; fCtx.id = ++sCtxId;
	sCurrent = &fCtx;
}

Scope::~Scope() { sCurrent = fPrev; }

}	// namespace Key
