// Проверка сайдчейна BeFX: огибающая ключа, кэш, расчёт приседания, встроенный плагин и микшер.
#include "KeyCache.h"
#include "Ducker.h"
#include "BeFxSidechain.h"
#include "Mixer.h"
#include <stdio.h>
#include <math.h>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
static const float RATE = 44100;

static double Db(double x) { return 20 * log10(x + 1e-30); }

// «бочка»: пачка синуса 60 Гц длиной 10 мс каждые 0.5 с, амплитуда 0.8; моно
static SourceRef Kick(double sec)
{
	std::vector<float> d((size_t)(sec * RATE), 0.0f);
	for (double t = 0.5; t + 0.01 < sec; t += 0.5)
		for (size_t i = (size_t)(t * RATE); i < (size_t)((t + 0.01) * RATE); i++) d[i] = 0.8f * sinf(2 * M_PI * 60 * (float)i / RATE);
	return MakeSource(&d[0], (int64_t)d.size(), 1, RATE);
}
static SourceRef Tone(double sec, double f, float amp)
{
	std::vector<float> d((size_t)(sec * RATE));
	for (size_t i = 0; i < d.size(); i++) d[i] = amp * sinf((float)(2 * M_PI * f * (double)i / RATE));
	return MakeSource(&d[0], (int64_t)d.size(), 1, RATE);
}
static MixClip ClipOf(const SourceRef &s, double pos)
{
	MixClip c; c.src = s; c.start = 0; c.len = s->frames; c.pos = pos; return c;
}
static double Goertzel(const std::vector<float> &b, int ch, double t0, double t1, double f)
{
	size_t i0 = (size_t)(t0 * RATE), i1 = (size_t)(t1 * RATE);
	double c = 2 * cos(2 * M_PI * f / RATE), s1 = 0, s2 = 0;
	for (size_t i = i0; i < i1; i++) { double s = b[i * 2 + ch] + c * s1 - s2; s2 = s1; s1 = s; }
	return sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) * 2 / (i1 - i0);
}
static double Rms(const std::vector<float> &b, double t0, double t1)
{
	double e = 0; size_t n = 0;
	for (size_t i = (size_t)(t0 * RATE); i < (size_t)(t1 * RATE) && i < b.size() / 2; i++) { e += (double)b[i * 2] * b[i * 2]; n++; }
	return sqrt(e / (n ? n : 1));
}

static void TestEnvelope()
{
	std::vector<MixClip> clips; clips.push_back(ClipOf(Kick(4.0), 0));
	Key::Envelope env;
	CHECK(Key::BuildEnvelope(clips, false, Key::RANGE_ALL, 0, env), "build failed");
	CHECK(env.peak.size() >= 4000 && env.peak.size() < 4010, "env length %zu", env.peak.size());
	CHECK(fabs(env.At(0.505) - 0.8) < 0.07, "kick peak %.3f", env.At(0.505));
	CHECK(env.At(0.3) < 0.01 && env.At(0.7) < 0.01, "silence not silent");
	CHECK(env.At(100.0) == 0.0f && env.At(-1.0) == 0.0f, "outside range");
	printf("envelope: kick peak %.3f, silence %.4f, bins %zu\n", env.At(0.505), env.At(0.3), env.peak.size());

	// диапазон «низ»: 100 Гц проходит, 5 кГц подавлен
	for (int k = 0; k < 2; k++) {
		std::vector<MixClip> c; c.push_back(ClipOf(Tone(1.0, k ? 5000 : 100, 0.5f), 0));
		Key::Envelope e;
		Key::BuildEnvelope(c, false, Key::RANGE_LOW, 150, e);
		const float v = e.At(0.6);
		printf("low range, %d Hz tone: %.4f\n", k ? 5000 : 100, v);
		if (k == 0) CHECK(v > 0.35f && v < 0.55f, "100 Hz through low range: %.3f", v);
		else CHECK(v < 0.002f, "5 kHz in low range: %.4f", v);
	}
	// стерео: пик из двух каналов
	{
		std::vector<float> st(2 * 44100, 0.0f);
		for (size_t i = 0; i < 44100; i++) st[i * 2 + 1] = 0.6f * sinf(2 * M_PI * 440 * (float)i / RATE);
		std::vector<MixClip> c; SourceRef s = MakeSource(&st[0], 44100, 2, RATE); c.push_back(ClipOf(s, 0));
		Key::Envelope e; Key::BuildEnvelope(c, false, Key::RANGE_ALL, 0, e);
		CHECK(fabs(e.At(0.5) - 0.6) < 0.03, "right-only peak %.3f", e.At(0.5));
	}
	// клип не с нуля и пустой список
	{
		std::vector<MixClip> c; c.push_back(ClipOf(Kick(2.0), 1.0));
		Key::Envelope e; Key::BuildEnvelope(c, false, Key::RANGE_ALL, 0, e);
		CHECK(fabs(e.At(1.505) - 0.8) < 0.07 && e.At(0.505) < 0.01, "clip offset: %.3f / %.3f", e.At(1.505), e.At(0.505));
		std::vector<MixClip> none; Key::Envelope z;
		CHECK(Key::BuildEnvelope(none, false, Key::RANGE_ALL, 0, z) && z.peak.size() <= 2, "empty lane");
	}
}

static void TestSignature()
{
	SourceRef s = Kick(2.0);
	std::vector<MixClip> a; a.push_back(ClipOf(s, 0));
	std::vector<MixClip> b = a;
	CHECK(Key::Signature(a, false) == Key::Signature(b, false), "same signature differs");
	b[0].pos = 0.1;
	CHECK(Key::Signature(a, false) != Key::Signature(b, false), "pos not in signature");
	b = a; b[0].len = s->frames / 2;
	CHECK(Key::Signature(a, false) != Key::Signature(b, false), "len not in signature");
	CHECK(Key::Signature(a, false) != Key::Signature(a, true), "xfade not in signature");
	// другой звук той же длины
	std::vector<MixClip> c; c.push_back(ClipOf(Tone(2.0, 300, 0.5f), 0));
	CHECK(Key::Signature(a, false) != Key::Signature(c, false), "content not in signature");
	// копия данных (как снимок Pool) даёт ту же подпись
	std::vector<MixClip> d; d.push_back(ClipOf(MakeSource(s->data, s->frames, s->channels, s->rate), 0));
	CHECK(Key::Signature(a, false) == Key::Signature(d, false), "copy of data changes signature");
}

static bool WaitReady(Key::Cache &c, int lane, int range, float cut)
{
	for (int i = 0; i < 500; i++) {
		if (c.Peek(lane, range, cut) == Key::ST_READY) return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	return false;
}

static void TestCache()
{
	Key::Cache cache;
	std::vector<MixClip> lane0; lane0.push_back(ClipOf(Kick(4.0), 0));
	bool xf = false;
	cache.SetLaneSource([&](int lane, std::vector<MixClip> &clips, bool &x) {
		if (lane != 0) return false;
		clips = lane0; x = xf; return true;
	});
	CHECK(cache.Check(0, Key::RANGE_ALL, 0) == Key::ST_NONE, "initial status");
	CHECK(!cache.Get(0, Key::RANGE_ALL, 0), "Get before build");
	cache.Request(0, Key::RANGE_ALL, 0);
	CHECK(WaitReady(cache, 0, Key::RANGE_ALL, 0), "background build did not finish");
	std::shared_ptr<const Key::Envelope> e = cache.Get(0, Key::RANGE_ALL, 0);
	CHECK(e && fabs(e->At(0.505) - 0.8) < 0.07, "envelope from cache");
	CHECK(cache.Check(0, Key::RANGE_ALL, 0) == Key::ST_READY, "status ready");
	// дорожка изменилась: устарело, но старая огибающая ещё отдаётся
	lane0[0].pos = 0.25;
	CHECK(cache.Check(0, Key::RANGE_ALL, 0) == Key::ST_STALE, "status stale");
	CHECK(cache.Get(0, Key::RANGE_ALL, 0) != nullptr, "stale envelope not served");
	cache.Request(0, Key::RANGE_ALL, 0);
	CHECK(WaitReady(cache, 0, Key::RANGE_ALL, 0), "rebuild did not finish");
	e = cache.Get(0, Key::RANGE_ALL, 0);
	CHECK(e && fabs(e->At(0.755) - 0.8) < 0.07 && e->At(0.505) < 0.01, "rebuilt envelope: %.3f / %.3f", e ? e->At(0.755) : -1, e ? e->At(0.505) : -1);
	CHECK(cache.Check(0, Key::RANGE_ALL, 0) == Key::ST_READY, "ready after rebuild");
	// синхронная сборка, другой диапазон, несуществующая дорожка
	CHECK(cache.BuildNow(0, Key::RANGE_LOW, 150), "BuildNow");
	CHECK(cache.Get(0, Key::RANGE_LOW, 150) != nullptr, "low envelope missing");
	CHECK(!cache.BuildNow(5, Key::RANGE_ALL, 0), "BuildNow for missing lane");
	cache.Clear();
	CHECK(cache.Peek(0, Key::RANGE_ALL, 0) == Key::ST_NONE, "Clear");
	printf("cache: build, stale, rebuild, low range, clear ok\n");
}

static void TestDucker()
{
	Ducker d; d.Init(RATE);
	DuckParams p = { -24.0f, 12.0f, 5.0f, 0.0f, 100.0f, 0.0f, false };
	d.Set(p);
	// атака: за одну постоянную времени (5 мс) около 63 % глубины, за 25 мс - почти вся
	float g = 1;
	for (int i = 0; i < (int)(0.005 * RATE); i++) g = d.Next(0.5f);
	const double r1 = d.Reduction() / 12.0;
	for (int i = 0; i < (int)(0.020 * RATE); i++) g = d.Next(0.5f);
	const double r2 = d.Reduction() / 12.0;
	CHECK(fabs(r1 - 0.632) < 0.04 && r2 > 0.98, "attack %.3f %.3f", r1, r2);
	CHECK(fabs(Db(g) + d.Reduction()) < 0.05, "gain vs reduction");
	// возврат: за 100 мс до 37 %
	for (int i = 0; i < (int)(0.100 * RATE); i++) d.Next(0.0f);
	const double r3 = d.Reduction() / 12.0;
	CHECK(fabs(r3 - 0.368) < 0.04, "release %.3f", r3);
	printf("ducker: attack %.3f/%.3f, release %.3f of depth\n", r1, r2, r3);
	// удержание 50 мс, потом возврат
	d.Reset(); p.holdMs = 50; d.Set(p);
	for (int i = 0; i < (int)(0.05 * RATE); i++) d.Next(0.5f);
	for (int i = 0; i < (int)(0.045 * RATE); i++) d.Next(0.0f);
	CHECK(d.Reduction() > 11.5f, "hold: %.2f", d.Reduction());
	for (int i = 0; i < (int)(0.100 * RATE); i++) d.Next(0.0f);
	CHECK(d.Reduction() < 11.0f, "no release after hold: %.2f", d.Reduction());
	// линейный возврат: после удержания 0 мс половина времени возврата - половина глубины
	d.Reset(); p.holdMs = 0; p.linearRelease = true; d.Set(p);
	for (int i = 0; i < (int)(0.05 * RATE); i++) d.Next(0.5f);
	for (int i = 0; i < (int)(0.050 * RATE); i++) d.Next(0.0f);
	CHECK(fabs(d.Reduction() - 6.0f) < 0.5f, "linear release at half: %.2f", d.Reduction());
	// колено 12 дБ: на пороге половина глубины, на -6 дБ от порога ничего, на +6 дБ вся
	p.linearRelease = false; p.kneeDb = 12; p.attackMs = 1; d.Set(p);
	const float levels[3] = { -30.0f, -24.0f, -18.0f }, want[3] = { 0.0f, 6.0f, 12.0f };
	for (int k = 0; k < 3; k++) {
		d.Reset();
		for (int i = 0; i < (int)(0.1 * RATE); i++) d.Next(powf(10.0f, levels[k] / 20.0f));
		CHECK(fabs(d.Reduction() - want[k]) < 0.3f, "knee level %.0f: %.2f want %.1f", levels[k], d.Reduction(), want[k]);
	}
	// ниже порога сжатия нет
	d.Reset(); p.kneeDb = 0; d.Set(p);
	for (int i = 0; i < 4410; i++) d.Next(powf(10.0f, -40.0f / 20.0f));
	CHECK(d.Reduction() == 0.0f, "below threshold: %.3f", d.Reduction());
}

static Ladspa::Plugin FindPlugin()
{
	BeFx::RegisterAll();
	std::vector<Ladspa::Plugin> all = Ladspa::Scan(std::vector<std::string>());
	for (size_t i = 0; i < all.size(); i++) if (all[i].label == BeFx::kSidechainLabel) return all[i];
	printf("FAIL: plugin not found\n"); fails++;
	return Ladspa::Plugin();
}

static void SetDefaults(Ladspa::Runner &r, int key)
{
	r.SetControl(BeFx::SC_KEY, (float)key); r.SetControl(BeFx::SC_RANGE, 0); r.SetControl(BeFx::SC_CUTOFF, 150);
	r.SetControl(BeFx::SC_THRESHOLD, -24); r.SetControl(BeFx::SC_DEPTH, 12); r.SetControl(BeFx::SC_ATTACK, 2);
	r.SetControl(BeFx::SC_HOLD, 0); r.SetControl(BeFx::SC_RELEASE, 100); r.SetControl(BeFx::SC_KNEE, 0);
	r.SetControl(BeFx::SC_CURVE, 0); r.SetControl(BeFx::SC_MIX, 100); r.SetControl(BeFx::SC_OUTPUT, 0);
}

static void TestPlugin()
{
	Ladspa::Plugin pl = FindPlugin();
	if (pl.label.empty()) return;
	CHECK(pl.group == "BeFX" && Ladspa::IsBuiltin(pl), "group %s", pl.group.c_str());
	CHECK(pl.mode == Ladspa::MODE_STEREO_STEREO, "port layout");
	// значения по умолчанию
	{
		Ladspa::Runner r; std::string err; r.Start(pl, RATE, 2, &err);
		CHECK(r.Control(BeFx::SC_THRESHOLD) == -24.0f && r.Control(BeFx::SC_DEPTH) == 9.0f && r.Control(BeFx::SC_CUTOFF) == 150.0f, "defaults");
	}
	Key::Cache &cache = Key::Cache::Global();
	std::vector<MixClip> kick; kick.push_back(ClipOf(Kick(3.0), 0));
	cache.SetLaneSource([&](int lane, std::vector<MixClip> &clips, bool &x) {
		if (lane != 0) return false;
		clips = kick; x = false; return true;
	});
	cache.Clear();
	CHECK(cache.BuildNow(0, Key::RANGE_ALL, 0), "build key");

	const int frames = (int)(3.0 * RATE);
	std::vector<float> pad(frames * 2);
	for (int i = 0; i < frames; i++) pad[i * 2] = pad[i * 2 + 1] = 0.5f * sinf((float)(2 * M_PI * 440.0 * i / RATE));

	// 1) с ключом: на ударе приседание ~12 дБ, между ударами сигнал не тронут; блоки 4096 (плагин получает по 2048)
	{
		Ladspa::Runner r; std::string err; r.Start(pl, RATE, 2, &err); SetDefaults(r, 1);
		std::vector<float> out = pad;
		for (int pos = 0; pos < frames; pos += 4096) {
			const int n = frames - pos < 4096 ? frames - pos : 4096;
			Key::Scope sc((double)pos / RATE, 1.0 / RATE);
			r.Process(&out[pos * 2], n);
		}
		const double on = Db(Rms(out, 0.512, 0.540)) - Db(Rms(pad, 0.512, 0.540));
		const double off = Db(Rms(out, 0.90, 0.95)) - Db(Rms(pad, 0.90, 0.95));
		const double beforeFirst = Db(Rms(out, 0.1, 0.4)) - Db(Rms(pad, 0.1, 0.4));
		CHECK(on < -8.0 && on > -12.5, "ducking after kick %.2f dB", on);
		CHECK(fabs(off) < 0.3 && fabs(beforeFirst) < 0.01, "pad touched away from kicks: %.2f / %.2f", off, beforeFirst);
		printf("plugin: after kick %.2f dB, between kicks %.2f dB\n", on, off);
		// выход «Сжатие»: после последнего блока (сигнал ниже порога) почти 0
		CHECK(r.Control(BeFx::SC_REDUCTION) < 0.5f, "reduction meter %.2f", r.Control(BeFx::SC_REDUCTION));
	}
	// 2) без контекста, без выбранного ключа и с несуществующей дорожкой: сигнал не меняется
	{
		for (int mode = 0; mode < 3; mode++) {
			Ladspa::Runner r; std::string err; r.Start(pl, RATE, 2, &err); SetDefaults(r, mode == 1 ? 0 : (mode == 2 ? 7 : 1));
			std::vector<float> out = pad;
			for (int pos = 0; pos < frames; pos += 4096) {
				const int n = frames - pos < 4096 ? frames - pos : 4096;
				if (mode == 0) r.Process(&out[pos * 2], n);			// вне Scope
				else { Key::Scope sc((double)pos / RATE, 1.0 / RATE); r.Process(&out[pos * 2], n); }
			}
			double d = 0; for (size_t i = 0; i < out.size(); i++) d = fmax(d, fabs(out[i] - pad[i]));
			CHECK(d < 1e-6, "passthrough mode %d diff %g", mode, d);
		}
	}
	// 3) параллельно 0 % и выход +6 дБ
	{
		Ladspa::Runner r; std::string err; r.Start(pl, RATE, 2, &err); SetDefaults(r, 1);
		r.SetControl(BeFx::SC_MIX, 0); r.SetControl(BeFx::SC_OUTPUT, 6);
		std::vector<float> out = pad;
		for (int pos = 0; pos < frames; pos += 4096) {
			const int n = frames - pos < 4096 ? frames - pos : 4096;
			Key::Scope sc((double)pos / RATE, 1.0 / RATE); r.Process(&out[pos * 2], n);
		}
		const double d = Db(Rms(out, 0.5, 0.6)) - Db(Rms(pad, 0.5, 0.6));
		CHECK(fabs(d - 6.0) < 0.1, "mix 0 + output 6 dB: %.2f", d);
	}
	// 4) реверс: блок идёт назад внутри удара - приседание включается
	{
		Ladspa::Runner r; std::string err; r.Start(pl, RATE, 2, &err); SetDefaults(r, 1);
		std::vector<float> blk(2 * 400, 0.5f);
		{ Key::Scope sc(0.509, -1.0 / RATE); r.Process(&blk[0], 400); }
		CHECK(r.Control(BeFx::SC_REDUCTION) > 6.0f, "reverse reduction %.2f", r.Control(BeFx::SC_REDUCTION));
	}
	cache.Clear();
}

static void TestMixer()
{
	Ladspa::Plugin pl = FindPlugin();
	if (pl.label.empty()) return;
	SourceRef pad = Tone(3.0, 440, 0.5f), kick = Kick(3.0);
	Ladspa::Values v;
	v[BeFx::SC_KEY] = 2; v[BeFx::SC_RANGE] = 0; v[BeFx::SC_CUTOFF] = 150;
	v[BeFx::SC_THRESHOLD] = -24; v[BeFx::SC_DEPTH] = 12; v[BeFx::SC_ATTACK] = 2; v[BeFx::SC_HOLD] = 0;
	v[BeFx::SC_RELEASE] = 100; v[BeFx::SC_KNEE] = 0; v[BeFx::SC_CURVE] = 0; v[BeFx::SC_MIX] = 100; v[BeFx::SC_OUTPUT] = 0;
	const int frames = (int)(2.0 * RATE);
	// источник клипов - дорожки микшера (как по умолчанию в приложении)
	Key::Cache::Global().SetLaneSource([](int lane, std::vector<MixClip> &clips, bool &x) {
		const MixTrack *t = Mix.Track(lane);
		if (!t) return false;
		clips = t->clips; x = t->xfade; return true;
	});
	std::vector<float> base((size_t)frames * 2), out((size_t)frames * 2);
	// ResetAll стирает дорожки, поэтому строим сцену заново в каждом прогоне
	auto scene = [&](bool buildKey, std::vector<float> &dst) {
		Key::Cache::Global().Clear();
		Mix.ResetAll();
		Mix.AddClip(0, pad, 0, pad->frames, 0.0);
		const int kl = Mix.AddTrack("kick", kick->data, kick->frames, 1, RATE);
		Ladspa::Values vv = v; vv[BeFx::SC_KEY] = (float)(kl + 1);
		std::string e2;
		if (!Mix.Fx(0).Add(pl, RATE, &e2, &vv)) { CHECK(false, "add plugin 2: %s", e2.c_str()); return; }
		if (buildKey) CHECK(Key::Cache::Global().BuildNow(kl, Key::RANGE_ALL, 0), "build key (mixer)");
		for (int pos = 0; pos < frames; pos += 1024) {
			const int n = frames - pos < 1024 ? frames - pos : 1024;
			std::vector<float> b((size_t)n * 2, 0.0f);
			Mix.Process(&b[0], n, (double)pos, RATE, RATE, true);
			for (int i = 0; i < n * 2; i++) dst[(size_t)pos * 2 + i] = b[i];
		}
	};
	scene(false, base);
	scene(true, out);
	// частота пэда (440 Гц) отделяет его от бочки (60 Гц)
	const double aBase = Goertzel(base, 0, 0.52, 0.545, 440), aOut = Goertzel(out, 0, 0.52, 0.545, 440);
	const double aBase2 = Goertzel(base, 0, 0.90, 0.95, 440), aOut2 = Goertzel(out, 0, 0.90, 0.95, 440);
	const double dKick = Db(aOut) - Db(aBase), dAway = Db(aOut2) - Db(aBase2);
	CHECK(dKick < -8.0 && dKick > -12.5, "mixer: pad after kick %.2f dB", dKick);
	CHECK(fabs(dAway) < 0.4, "mixer: pad between kicks %.2f dB", dAway);
	printf("mixer: pad after kick %.2f dB, between kicks %.2f dB \n", dKick, dAway);
	Key::Cache::Global().Clear();
}

int main()
{
	TestEnvelope();
	TestSignature();
	TestCache();
	TestDucker();
	TestPlugin();
	TestMixer();
	printf(fails ? "FAILED %d\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}
