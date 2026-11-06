/*
 * NoiseCore - см. NoiseCore.h
 */
#include "NoiseCore.h"

#include <math.h>
#include <string.h>
#include <algorithm>

static const int	N = NoiseCore::kFrame;
static const int	HOP = N / 4;
static const int	BINS = N / 2 + 1;
static const int	SUBS = 6;				// число подокон для минимума
static const float	kPi = 3.14159265358979f;

// ---- БПФ (по основанию 2, на месте) ----
struct Fft {
	std::vector<float> cs, sn;
	std::vector<int> rev;
	Fft() : cs(N / 2), sn(N / 2), rev(N)
	{
		for (int i = 0; i < N / 2; i++) { cs[i] = cosf(2 * kPi * i / N); sn[i] = -sinf(2 * kPi * i / N); }
		int bits = 0; while ((1 << bits) < N) bits++;
		for (int i = 0; i < N; i++) {
			int r = 0;
			for (int b = 0; b < bits; b++) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
			rev[i] = r;
		}
	}
	// inverse=true: обратное БПФ без деления на N
	void Run(float *re, float *im, bool inverse) const
	{
		for (int i = 0; i < N; i++) {
			int j = rev[i];
			if (j > i) { float t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
		}
		for (int len = 2; len <= N; len <<= 1) {
			const int half = len / 2, step = N / len;
			for (int i = 0; i < N; i += len)
				for (int k = 0; k < half; k++) {
					float wr = cs[k * step], wi = inverse ? -sn[k * step] : sn[k * step];
					int a = i + k, b = a + half;
					float tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
					re[b] = re[a] - tr; im[b] = im[a] - ti;
					re[a] += tr; im[a] += ti;
				}
		}
	}
};

static const Fft &SharedFft() { static Fft f; return f; }

struct Chan {
	std::vector<float> in;			// последние N входных отсчётов (кольцо)
	std::vector<float> acc;			// накопитель перекрытия, N
	std::vector<float> outq;		// готовые выходные отсчёты, HOP
	std::vector<float> outDry;		// то же для сухого сигнала
	std::vector<float> smoothP, fastP, gain, noise, subMin, hist, learn;
	int		inPos, fill, outPos, subCount, subIdx, frames, learnFrames;
	Chan() { Reset(); learn.assign(BINS, 0.0f); learnFrames = 0; }
	void Reset()
	{
		in.assign(N, 0.0f); acc.assign(N, 0.0f);
		outq.assign(HOP, 0.0f); outDry.assign(HOP, 0.0f);
		smoothP.assign(BINS, 0.0f); fastP.assign(BINS, 0.0f); gain.assign(BINS, 1.0f); noise.assign(BINS, 0.0f);
		subMin.assign(BINS, 1e30f); hist.assign((size_t)SUBS * BINS, 1e30f);
		inPos = 0; fill = 0; outPos = 0; subCount = 0; subIdx = 0; frames = 0;
	}
};

struct NoiseCore::Impl {
	float	rate;
	Chan	ch[2];
	std::vector<float> win, re, im, pw, g, g2;
	int		subLen;
	float	reduction, sens, smooth;
	bool	useProfile, listen, learning;
	std::shared_ptr<const NoiseProfile> profile;
	Impl() : rate(44100), subLen(43), reduction(10), sens(2), smooth(0.5f), useProfile(false), listen(false), learning(false) {}
};

NoiseCore::NoiseCore(float rate) : fImpl(new Impl)
{
	Impl &s = *fImpl;
	s.rate = rate > 0 ? rate : 44100.0f;
	s.win.resize(N); s.re.resize(N); s.im.resize(N); s.pw.resize(BINS); s.g.resize(BINS); s.g2.resize(BINS);
	for (int i = 0; i < N; i++) s.win[i] = sqrtf(0.5f - 0.5f * cosf(2 * kPi * (i + 0.5f) / N));	// корень из Ханна
	s.subLen = (int)(0.25f * s.rate / HOP + 0.5f);
	if (s.subLen < 4) s.subLen = 4;
}

NoiseCore::~NoiseCore() { delete fImpl; }

static float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

void NoiseCore::SetParams(float reductionDb, float sensitivity, float smooth, bool useProfile, bool listen)
{
	Impl &s = *fImpl;
	s.reduction = Clamp(reductionDb, 0.0f, 40.0f);
	s.sens = Clamp(sensitivity, 1.0f, 3.0f);
	s.smooth = Clamp(smooth, 0.0f, 1.0f);
	s.useProfile = useProfile;
	s.listen = listen;
}

void NoiseCore::SetProfile(std::shared_ptr<const NoiseProfile> p)
{
	std::atomic_store(&fImpl->profile, p);
}

void NoiseCore::Reset()
{
	fImpl->ch[0].Reset();
	fImpl->ch[1].Reset();
}

// обработка одного кадра одного канала
static void Frame(NoiseCore::Impl &s, Chan &c, int chIndex, const NoiseProfile *prof)
{
	const Fft &fft = SharedFft();
	std::vector<float> &re = s.re, &im = s.im, &pw = s.pw, &g = s.g, &g2 = s.g2;
	for (int i = 0; i < N; i++) {
		re[i] = c.in[(c.inPos + i) % N] * s.win[i];
		im[i] = 0.0f;
	}
	fft.Run(&re[0], &im[0], false);

	const float floorG = powf(10.0f, -s.reduction / 20.0f);
	std::vector<float> &P = c.smoothP;
	for (int k = 0; k < BINS; k++) {
		pw[k] = re[k] * re[k] + im[k] * im[k];
		P[k] = (c.frames == 0) ? pw[k] : 0.85f * P[k] + 0.15f * pw[k];
		c.fastP[k] = (c.frames == 0) ? pw[k] : 0.65f * c.fastP[k] + 0.35f * pw[k];	// для коэффициентов: быстрее, но без выбросов
	}

	if (s.learning) {					// запоминание профиля: среднее мощности
		const float a = 1.0f / (float)(c.learnFrames + 1);
		for (int k = 0; k < BINS; k++) c.learn[k] += a * (pw[k] - c.learn[k]);
		c.learnFrames++;
	}

	// слежение за минимумом (оценка шума без профиля)
	for (int k = 0; k < BINS; k++)
		if (P[k] < c.subMin[k]) c.subMin[k] = P[k];
	if (++c.subCount >= s.subLen) {
		for (int k = 0; k < BINS; k++) { c.hist[(size_t)c.subIdx * BINS + k] = c.subMin[k]; c.subMin[k] = 1e30f; }
		c.subIdx = (c.subIdx + 1) % SUBS;
		c.subCount = 0;
	}
	const std::vector<float> *pb = 0;
	if (s.useProfile && prof) {
		const std::vector<float> &v = prof->bins[chIndex < 2 && !prof->bins[chIndex].empty() ? chIndex : 0];
		if ((int)v.size() == BINS) pb = &v;
	}
	for (int k = 0; k < BINS; k++) {
		float m = c.subMin[k];
		for (int u = 0; u < SUBS; u++) { float v = c.hist[(size_t)u * BINS + k]; if (v < m) m = v; }
		if (m > 1e29f) m = P[k];
		c.noise[k] = pb ? (*pb)[k] : m * 2.5f;		// минимум занижает среднее: поправка
	}

	// коэффициенты усиления
	const float rel = 0.05f + 0.55f * (1.0f - s.smooth);
	const float atk = 0.7f;
	for (int k = 0; k < BINS; k++) {
		float p = c.fastP[k] > 1e-20f ? c.fastP[k] : 1e-20f;
		float x = 1.0f - s.sens * c.noise[k] / p;
		float gr = x > floorG * floorG ? x : floorG * floorG;
		gr = sqrtf(gr);
		float &gs = c.gain[k];
		gs += (gr > gs ? atk : rel) * (gr - gs);
		g[k] = gs;
	}
	// сглаживание по частоте (убирает "музыкальный шум")
	const float fs = 0.5f * s.smooth;
	for (int k = 0; k < BINS; k++) {
		float a = g[k > 0 ? k - 1 : 0], b = g[k < BINS - 1 ? k + 1 : BINS - 1];
		g2[k] = g[k] * (1.0f - fs) + 0.5f * (a + b) * fs;
	}
	for (int k = 0; k < BINS; k++) {
		re[k] *= g2[k]; im[k] *= g2[k];
		if (k > 0 && k < N / 2) { re[N - k] *= g2[k]; im[N - k] *= g2[k]; }
	}
	fft.Run(&re[0], &im[0], true);

	// наложение с перекрытием; готовы первые HOP отсчётов накопителя
	const float scale = 1.0f / (float)N * 0.5f;		// 1/N обратного БПФ; сумма окон Ханна при 75% равна 2
	for (int i = 0; i < N; i++) c.acc[i] += re[i] * s.win[i] * scale;
	for (int i = 0; i < HOP; i++) { c.outq[i] = c.acc[i]; c.outDry[i] = c.in[(c.inPos + i) % N]; }
	memmove(&c.acc[0], &c.acc[HOP], sizeof(float) * (N - HOP));
	memset(&c.acc[N - HOP], 0, sizeof(float) * HOP);
	c.frames++;
}

void NoiseCore::Process(float *buf, int frames, int channels)
{
	Impl &s = *fImpl;
	if (channels < 1) return;
	if (channels > 2) channels = 2;
	std::shared_ptr<const NoiseProfile> prof = std::atomic_load(&s.profile);
	for (int c = 0; c < channels; c++) {
		Chan &ch = s.ch[c];
		for (int i = 0; i < frames; i++) {
			float &sample = buf[(size_t)i * channels + c];
			const float x = sample;
			const float w = ch.outq[ch.outPos], d = ch.outDry[ch.outPos];
			sample = s.listen ? (d - w) : w;
			ch.outPos++;
			ch.in[ch.inPos] = x;
			ch.inPos = (ch.inPos + 1) % N;
			if (++ch.fill >= HOP) {
				ch.fill = 0;
				ch.outPos = 0;
				Frame(s, ch, c, prof.get());
			}
		}
	}
}

std::shared_ptr<const NoiseProfile> NoiseCore::LearnFrom(const float *buf, int frames, int channels, float rate)
{
	NoiseCore tmp(rate);
	tmp.fImpl->learning = true;
	std::vector<float> copy(buf, buf + (size_t)frames * channels);
	// блоками, чтобы не держать огромный буфер на выходе
	const int block = 4096;
	for (int pos = 0; pos < frames; pos += block) {
		int n = frames - pos < block ? frames - pos : block;
		tmp.Process(&copy[(size_t)pos * channels], n, channels);
	}
	std::shared_ptr<NoiseProfile> p(new NoiseProfile);
	for (int c = 0; c < channels && c < 2; c++)
		if (tmp.fImpl->ch[c].learnFrames > 0) p->bins[c] = tmp.fImpl->ch[c].learn;
	if (p->bins[1].empty()) p->bins[1] = p->bins[0];
	p->seconds = frames / (double)tmp.fImpl->rate;
	if (p->bins[0].empty()) return std::shared_ptr<const NoiseProfile>();
	return p;
}
