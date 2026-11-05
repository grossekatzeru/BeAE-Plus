/*
 * NoiseReducer - см. NoiseReducer.h
 */
#include "NoiseReducer.h"

#include <math.h>
#include <string.h>
#include <vector>

namespace NoiseReducer {

static const int	N = kFrame;
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
	std::vector<float> smoothP, fastP, gain, noise, profile, subMin, hist;
	int		inPos, fill, outPos;
	int		subCount;
	int		subIdx;
	int		learnFrames;
	bool	warm;					// набрано достаточно данных для оценки
	int		frames;
	Chan() { Reset(true); }
	void Reset(bool all)
	{
		in.assign(N, 0.0f); acc.assign(N, 0.0f);
		outq.assign(HOP, 0.0f); outDry.assign(HOP, 0.0f);
		smoothP.assign(BINS, 0.0f); fastP.assign(BINS, 0.0f); gain.assign(BINS, 1.0f); noise.assign(BINS, 0.0f);
		subMin.assign(BINS, 1e30f); hist.assign((size_t)SUBS * BINS, 1e30f);
		if (all) profile.assign(BINS, 0.0f);
		inPos = 0; fill = 0; outPos = 0; subCount = 0; subIdx = 0; frames = 0; warm = false;
		if (all) learnFrames = 0;
	}
};

struct Instance {
	float	rate;
	Chan	ch[2];
	float	*port[P_COUNT];
	std::vector<float> win, re, im, pw, g, g2;	// окно и рабочие буферы
	int		subLen;					// кадров в подокне (~0,25 с)
	bool	wasLearning;
	Instance() : rate(44100), subLen(43), wasLearning(false) { for (int i = 0; i < P_COUNT; i++) port[i] = 0; }
};

static float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void *Instantiate(const Ladspa::Descriptor *, unsigned long rate)
{
	Instance *s = new Instance;
	s->rate = rate > 0 ? (float)rate : 44100.0f;
	s->win.resize(N); s->re.resize(N); s->im.resize(N); s->pw.resize(BINS); s->g.resize(BINS); s->g2.resize(BINS);
	for (int i = 0; i < N; i++) s->win[i] = sqrtf(0.5f - 0.5f * cosf(2 * kPi * (i + 0.5f) / N));	// корень из Ханна
	s->subLen = (int)(0.25f * s->rate / HOP + 0.5f);
	if (s->subLen < 4) s->subLen = 4;
	return s;
}

static void Connect(void *h, unsigned long port, float *loc)
{
	Instance *s = (Instance *)h;
	if (port < (unsigned long)P_COUNT) s->port[port] = loc;
}

static void Activate(void *h)
{
	Instance *s = (Instance *)h;
	s->ch[0].Reset(false);			// профиль при сбросе (перемотке) сохраняем
	s->ch[1].Reset(false);
	if (s->port[P_LATENCY]) *s->port[P_LATENCY] = (float)N;
}

// обработка одного кадра одного канала
static void Frame(Instance *s, Chan &c, float reduction, float sens, float smooth, bool useProfile, bool learn)
{
	std::vector<float> &re = s->re, &im = s->im;
	const Fft &fft = SharedFft();
	for (int i = 0; i < N; i++) {
		re[i] = c.in[(c.inPos + i) % N] * s->win[i];
		im[i] = 0.0f;
	}
	fft.Run(&re[0], &im[0], false);

	const float floorG = powf(10.0f, -reduction / 20.0f);
	std::vector<float> &P = c.smoothP;
	std::vector<float> &pw = s->pw;
	for (int k = 0; k < BINS; k++) {
		pw[k] = re[k] * re[k] + im[k] * im[k];
		P[k] = (c.frames == 0) ? pw[k] : 0.85f * P[k] + 0.15f * pw[k];
		c.fastP[k] = (c.frames == 0) ? pw[k] : 0.65f * c.fastP[k] + 0.35f * pw[k];	// для коэффициентов: быстрее, но без выбросов
	}

	// --- запоминание профиля: среднее мощности ---
	if (learn) {
		const float a = 1.0f / (float)(c.learnFrames + 1);
		for (int k = 0; k < BINS; k++) c.profile[k] += a * (pw[k] - c.profile[k]);
		c.learnFrames++;
	}

	// --- слежение за минимумом (оценка шума без профиля) ---
	for (int k = 0; k < BINS; k++)
		if (P[k] < c.subMin[k]) c.subMin[k] = P[k];
	if (++c.subCount >= s->subLen) {
		for (int k = 0; k < BINS; k++) { c.hist[(size_t)c.subIdx * BINS + k] = c.subMin[k]; c.subMin[k] = 1e30f; }
		c.subIdx = (c.subIdx + 1) % SUBS;
		c.subCount = 0;
		c.warm = true;
	}
	const bool haveProfile = useProfile && c.learnFrames > 0;
	for (int k = 0; k < BINS; k++) {
		float m = c.subMin[k];
		for (int u = 0; u < SUBS; u++) { float v = c.hist[(size_t)u * BINS + k]; if (v < m) m = v; }
		if (m > 1e29f) m = P[k];
		c.noise[k] = haveProfile ? c.profile[k] : m * 2.5f;	// минимум занижает среднее: поправка
	}

	// --- коэффициенты усиления ---
	const float rel = 0.05f + 0.55f * (1.0f - smooth);
	const float atk = 0.7f;
	std::vector<float> &g = s->g;
	for (int k = 0; k < BINS; k++) {
		float p = c.fastP[k] > 1e-20f ? c.fastP[k] : 1e-20f;
		float x = 1.0f - sens * c.noise[k] / p;
		float gr = x > floorG * floorG ? x : floorG * floorG;
		gr = sqrtf(gr);
		float &gs = c.gain[k];
		gs += (gr > gs ? atk : rel) * (gr - gs);
		g[k] = gs;
	}
	// сглаживание по частоте (убирает "музыкальный шум")
	const float fs = 0.5f * smooth;
	std::vector<float> &g2 = s->g2;
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
	for (int i = 0; i < N; i++) c.acc[i] += re[i] * s->win[i] * scale;
	for (int i = 0; i < HOP; i++) { c.outq[i] = c.acc[i]; c.outDry[i] = c.in[(c.inPos + i) % N]; }
	memmove(&c.acc[0], &c.acc[HOP], sizeof(float) * (N - HOP));
	memset(&c.acc[N - HOP], 0, sizeof(float) * HOP);
	c.frames++;
}

static void Run(void *h, unsigned long n)
{
	Instance *s = (Instance *)h;
	for (int i = 0; i < P_COUNT; i++)
		if (!s->port[i]) return;
	const float reduction = Clamp(*s->port[P_REDUCTION], 0.0f, 40.0f);
	const float sens = Clamp(*s->port[P_SENSITIVITY], 1.0f, 3.0f);
	const float smooth = Clamp(*s->port[P_SMOOTH], 0.0f, 1.0f);
	const bool useProfile = *s->port[P_USEPROFILE] > 0.5f;
	const bool learn = *s->port[P_LEARN] > 0.5f;
	const bool listen = *s->port[P_LISTEN] > 0.5f;
	*s->port[P_LATENCY] = (float)N;
	if (learn && !s->wasLearning) {			// новая запись профиля начинается с нуля
		for (int c = 0; c < 2; c++) { s->ch[c].learnFrames = 0; std::fill(s->ch[c].profile.begin(), s->ch[c].profile.end(), 0.0f); }
	}
	s->wasLearning = learn;

	for (int c = 0; c < 2; c++) {
		Chan &ch = s->ch[c];
		const float *in = s->port[c == 0 ? P_IN_L : P_IN_R];
		float *out = s->port[c == 0 ? P_OUT_L : P_OUT_R];
		for (unsigned long i = 0; i < n; i++) {
			const float x = in[i];
			// выход: готовый кусок предыдущего кадра
			float w = ch.outq[ch.outPos], d = ch.outDry[ch.outPos];
			out[i] = listen ? (d - w) : w;
			ch.outPos++;
			// вход
			ch.in[ch.inPos] = x;
			ch.inPos = (ch.inPos + 1) % N;
			if (++ch.fill >= HOP) {
				ch.fill = 0;
				ch.outPos = 0;
				Frame(s, ch, reduction, sens, smooth, useProfile, learn);
			}
		}
	}
}

static void Cleanup(void *h) { delete (Instance *)h; }

const Ladspa::Descriptor *Descriptor()
{
	static const char *names[P_COUNT] = {
		"Подавление, дБ", "Чувствительность", "Сглаживание",
		"По профилю шума", "Запомнить шум", "Слушать удаляемое",
		"latency", "Вход L", "Вход R", "Выход L", "Выход R"
	};
	static int desc[P_COUNT];
	static Ladspa::PortRangeHint hints[P_COUNT];
	static bool init = false;
	if (!init) {
		const int ctl = Ladspa::PORT_INPUT | Ladspa::PORT_CONTROL;
		const int B = Ladspa::HINT_BOUNDED_BELOW | Ladspa::HINT_BOUNDED_ABOVE;
		for (int i = 0; i < P_COUNT; i++) { desc[i] = ctl; hints[i] = { 0, 0, 0 }; }
		hints[P_REDUCTION]	 = { B | Ladspa::HINT_DEFAULT_LOW, 0.0f, 40.0f };		// 10 дБ
		hints[P_SENSITIVITY] = { B | Ladspa::HINT_DEFAULT_MIDDLE, 1.0f, 3.0f };	// 2
		hints[P_SMOOTH]		 = { B | Ladspa::HINT_DEFAULT_MIDDLE, 0.0f, 1.0f };	// 0.5
		hints[P_USEPROFILE]	 = { Ladspa::HINT_TOGGLED | Ladspa::HINT_DEFAULT_0, 0.0f, 1.0f };
		hints[P_LEARN]		 = { Ladspa::HINT_TOGGLED | Ladspa::HINT_DEFAULT_0, 0.0f, 1.0f };
		hints[P_LISTEN]		 = { Ladspa::HINT_TOGGLED | Ladspa::HINT_DEFAULT_0, 0.0f, 1.0f };
		desc[P_LATENCY] = Ladspa::PORT_OUTPUT | Ladspa::PORT_CONTROL;
		desc[P_IN_L] = desc[P_IN_R] = Ladspa::PORT_INPUT | Ladspa::PORT_AUDIO;
		desc[P_OUT_L] = desc[P_OUT_R] = Ladspa::PORT_OUTPUT | Ladspa::PORT_AUDIO;
		init = true;
	}
	static Ladspa::Descriptor d = {
		902, "beae_noise_reducer", 0, "Шумодав", "BeAE-Plus", "BSD",
		P_COUNT, desc, names, hints, 0,
		Instantiate, Connect, Activate, Run, 0, 0, 0, Cleanup
	};
	return &d;
}

}	// namespace NoiseReducer

// точка входа LADSPA
extern "C" const Ladspa::Descriptor *ladspa_descriptor(unsigned long index)
{
	return index == 0 ? NoiseReducer::Descriptor() : 0;
}
