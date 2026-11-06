/*
 * Multiband - см. Multiband.h
 */
#include "Multiband.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace Multiband {

static const float kPi = 3.14159265358979f;
static const int kControlStep = 8;		// пересчёт цели раз в столько отсчётов

struct Biquad {
	float b0, b1, b2, a1, a2, z1, z2;
	Biquad() : b0(1), b1(0), b2(0), a1(0), a2(0), z1(0), z2(0) {}
	inline float Run(float x) { float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
	void Design(bool hp, float f, float fs)
	{
		const float w = 2 * kPi * f / fs, c = cosf(w), s = sinf(w);
		const float alpha = s / (2 * 0.70710678f);
		const float a0 = 1 + alpha;
		a1 = -2 * c / a0; a2 = (1 - alpha) / a0;
		if (hp) { b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; b2 = b0; }
		else	{ b0 = (1 - c) / 2 / a0; b1 = (1 - c) / a0; b2 = b0; }
	}
	void Reset() { z1 = z2 = 0; }
};

struct LR4 {
	Biquad s[2];
	inline float Run(float x) { return s[1].Run(s[0].Run(x)); }
	void Design(bool hp, float f, float fs) { s[0].Design(hp, f, fs); s[1].Design(hp, f, fs); }
	void Reset() { s[0].Reset(); s[1].Reset(); }
};

struct Split {			// разделитель на НЧ и ВЧ часть; сумма частей = фазовый фильтр
	LR4 lp, hp;
	void Design(float f, float fs) { lp.Design(false, f, fs); hp.Design(true, f, fs); }
	inline float Ap(float x) { return lp.Run(x) + hp.Run(x); }
	void Reset() { lp.Reset(); hp.Reset(); }
};

struct Chan {
	Split x[3];				// разделители на границах f0..f2
	Split ap[3];			// компенсация фазы: низы (f1, f2), полоса 1 (f2)
	float band[kBands];
	void Design(const float f[3], float fs)
	{
		for (int i = 0; i < 3; i++) x[i].Design(f[i], fs);
		ap[0].Design(f[1], fs); ap[1].Design(f[2], fs);		// низы
		ap[2].Design(f[2], fs);								// полоса 1
	}
	void Reset() { for (int i = 0; i < 3; i++) { x[i].Reset(); ap[i].Reset(); } }
	void Split4(float in)
	{
		float b0 = x[0].lp.Run(in), r = x[0].hp.Run(in);
		float b1 = x[1].lp.Run(r); r = x[1].hp.Run(r);
		float b2 = x[2].lp.Run(r), b3 = x[2].hp.Run(r);
		b0 = ap[1].Ap(ap[0].Ap(b0));
		b1 = ap[2].Ap(b1);
		band[0] = b0; band[1] = b1; band[2] = b2; band[3] = b3;
	}
};

struct Instance {
	float	rate;
	Chan	ch[2];
	float	env[kBands];		// мощность в полосе (сглаженная)
	float	grDb[kBands];		// текущее подавление (<= 0 дБ)
	float	targetDb[kBands];
	float	lastEdges[3];
	int		counter;
	float	*port[P_COUNT];
	Instance() : rate(44100), counter(0)
	{
		for (int i = 0; i < kBands; i++) { env[i] = 0; grDb[i] = 0; targetDb[i] = 0; }
		for (int i = 0; i < 3; i++) lastEdges[i] = -1;
		for (int i = 0; i < P_COUNT; i++) port[i] = 0;
	}
};

static float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float Flush(float x) { return fabsf(x) < 1e-18f ? 0.0f : x; }
static inline float OnePole(float ms, float rate) { return 1.0f - expf(-1.0f / (ms * 0.001f * rate)); }

static void *Instantiate(const Ladspa::Descriptor *, unsigned long rate)
{
	Instance *s = new Instance;
	s->rate = rate > 0 ? (float)rate : 44100.0f;
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
	for (int c = 0; c < 2; c++) s->ch[c].Reset();
	for (int i = 0; i < kBands; i++) { s->env[i] = 0; s->grDb[i] = 0; s->targetDb[i] = 0; }
	s->counter = 0;
}

// подавление в дБ (<= 0) для уровня, дБ; колено width
static float GainComputer(float levelDb, float thr, float ratio, float width)
{
	const float slope = 1.0f / ratio - 1.0f;		// <= 0
	const float over = levelDb - thr;
	if (2 * over < -width) return 0.0f;
	if (width > 0 && 2 * fabsf(over) <= width) { const float t = over + width * 0.5f; return slope * t * t / (2 * width); }
	return slope * over;
}

static void Run(void *h, unsigned long n)
{
	Instance *s = (Instance *)h;
	for (int i = 0; i < P_COUNT; i++)
		if (!s->port[i]) return;

	const float fs = s->rate;
	float thr[kBands], ratio[kBands], gainLin[kBands], aA[kBands], aR[kBands];
	for (int k = 0; k < kBands; k++) {
		thr[k]	 = Clamp(*s->port[P_BAND0 + k * BP_COUNT + BP_THRESHOLD], -60.0f, 0.0f);
		ratio[k] = Clamp(*s->port[P_BAND0 + k * BP_COUNT + BP_RATIO], 1.0f, 20.0f);
		aA[k]	 = OnePole(Clamp(*s->port[P_BAND0 + k * BP_COUNT + BP_ATTACK], 1.0f, 100.0f), fs / kControlStep);
		aR[k]	 = OnePole(Clamp(*s->port[P_BAND0 + k * BP_COUNT + BP_RELEASE], 20.0f, 1000.0f), fs / kControlStep);
		gainLin[k] = powf(10.0f, Clamp(*s->port[P_BAND0 + k * BP_COUNT + BP_GAIN], -12.0f, 12.0f) / 20.0f);
	}
	const float knee = Clamp(*s->port[P_KNEE], 0.0f, 12.0f);
	const float outLin = powf(10.0f, Clamp(*s->port[P_OUTPUT], -12.0f, 12.0f) / 20.0f);
	const int solo = (int)(Clamp(*s->port[P_SOLO], 0.0f, (float)kBands) + 0.5f);

	// границы полос: по возрастанию, не выше 0,45 частоты дискретизации
	const float nyq = fs * 0.45f;
	float f[3] = { Clamp(*s->port[P_X1], 60.0f, 500.0f), Clamp(*s->port[P_X2], 300.0f, 4000.0f), Clamp(*s->port[P_X3], 2000.0f, 16000.0f) };
	for (int i = 0; i < 3; i++) {
		if (f[i] > nyq) f[i] = nyq;
		if (i > 0 && f[i] < f[i - 1]) f[i] = f[i - 1];
	}
	if (f[0] != s->lastEdges[0] || f[1] != s->lastEdges[1] || f[2] != s->lastEdges[2]) {
		s->ch[0].Design(f, fs); s->ch[1].Design(f, fs);
		for (int i = 0; i < 3; i++) s->lastEdges[i] = f[i];
	}

	const float envC = OnePole(5.0f, fs);			// детектор уровня (среднеквадратичный, 5 мс)

	for (unsigned long i = 0; i < n; i++) {
		float in2[2] = { s->port[P_IN_L][i], s->port[P_IN_R][i] };
		for (int c = 0; c < 2; c++) s->ch[c].Split4(in2[c]);

		for (int k = 0; k < kBands; k++) {
			float p = 0.5f * (s->ch[0].band[k] * s->ch[0].band[k] + s->ch[1].band[k] * s->ch[1].band[k]);
			s->env[k] = Flush(s->env[k] + envC * (p - s->env[k]));
		}

		if (s->counter-- <= 0) {
			s->counter = kControlStep - 1;
			for (int k = 0; k < kBands; k++) {
				const float db = 10.0f * log10f(s->env[k] + 1e-12f) + 3.0103f;	// синус амплитуды 1 = 0 дБ
				const float target = GainComputer(db, thr[k], ratio[k], knee);
				s->targetDb[k] = target;
				s->grDb[k] += (target < s->grDb[k] ? aA[k] : aR[k]) * (target - s->grDb[k]);
			}
		}

		for (int c = 0; c < 2; c++) {
			float y = 0;
			for (int k = 0; k < kBands; k++) {
				if (solo && solo - 1 != k) continue;
				y += s->ch[c].band[k] * gainLin[k] * expf(s->grDb[k] * 0.11512925f);
			}
			s->port[c == 0 ? P_OUT_L : P_OUT_R][i] = y * outLin;
		}
	}
}

static void Cleanup(void *h) { delete (Instance *)h; }

const Ladspa::Descriptor *Descriptor()
{
	static char nameBuf[P_COUNT][48];
	static const char *names[P_COUNT];
	static int desc[P_COUNT];
	static Ladspa::PortRangeHint hints[P_COUNT];
	static bool init = false;
	if (!init) {
		const char *bandName[kBands] = { "Низы", "Низкая середина", "Высокая середина", "Верха" };
		const char *parName[BP_COUNT] = { "порог, дБ", "степень", "атака, мс", "спад, мс", "усиление, дБ" };
		const int B = Ladspa::HINT_BOUNDED_BELOW | Ladspa::HINT_BOUNDED_ABOVE;
		const int L = Ladspa::HINT_LOGARITHMIC;
		for (int i = 0; i < P_COUNT; i++) { desc[i] = Ladspa::PORT_INPUT | Ladspa::PORT_CONTROL; hints[i] = { 0, 0, 0 }; }
		for (int k = 0; k < kBands; k++) {
			const int b = P_BAND0 + k * BP_COUNT;
			hints[b + BP_THRESHOLD] = { B | Ladspa::HINT_DEFAULT_HIGH, -60.0f, 0.0f };					// -15
			hints[b + BP_RATIO]		= { B | L | Ladspa::HINT_DEFAULT_LOW, 1.0f, 20.0f };				// около 2,1
			hints[b + BP_ATTACK]	= { B | L | Ladspa::HINT_DEFAULT_MIDDLE, 1.0f, 100.0f };			// 10
			hints[b + BP_RELEASE]	= { B | L | Ladspa::HINT_DEFAULT_MIDDLE, 20.0f, 1000.0f };			// около 141
			hints[b + BP_GAIN]		= { B | Ladspa::HINT_DEFAULT_MIDDLE, -12.0f, 12.0f };				// 0
			for (int p = 0; p < BP_COUNT; p++) snprintf(nameBuf[b + p], sizeof(nameBuf[0]), "%s: %s", bandName[k], parName[p]);
		}
		hints[P_X1]	   = { B | L | Ladspa::HINT_DEFAULT_MIDDLE, 60.0f, 500.0f };		// около 173
		hints[P_X2]	   = { B | L | Ladspa::HINT_DEFAULT_MIDDLE, 300.0f, 4000.0f };		// около 1095
		hints[P_X3]	   = { B | L | Ladspa::HINT_DEFAULT_MIDDLE, 2000.0f, 16000.0f };	// около 5657
		hints[P_KNEE]  = { B | Ladspa::HINT_DEFAULT_MIDDLE, 0.0f, 12.0f };				// 6
		hints[P_OUTPUT]= { B | Ladspa::HINT_DEFAULT_MIDDLE, -12.0f, 12.0f };			// 0
		hints[P_SOLO]  = { B | Ladspa::HINT_INTEGER | Ladspa::HINT_DEFAULT_0, 0.0f, (float)kBands };
		snprintf(nameBuf[P_X1], sizeof(nameBuf[0]), "Граница 1, Гц");
		snprintf(nameBuf[P_X2], sizeof(nameBuf[0]), "Граница 2, Гц");
		snprintf(nameBuf[P_X3], sizeof(nameBuf[0]), "Граница 3, Гц");
		snprintf(nameBuf[P_KNEE], sizeof(nameBuf[0]), "Мягкое колено, дБ");
		snprintf(nameBuf[P_OUTPUT], sizeof(nameBuf[0]), "Выход, дБ");
		snprintf(nameBuf[P_SOLO], sizeof(nameBuf[0]), "Слушать полосу (0 = все)");
		snprintf(nameBuf[P_IN_L], sizeof(nameBuf[0]), "Вход L");
		snprintf(nameBuf[P_IN_R], sizeof(nameBuf[0]), "Вход R");
		snprintf(nameBuf[P_OUT_L], sizeof(nameBuf[0]), "Выход L");
		snprintf(nameBuf[P_OUT_R], sizeof(nameBuf[0]), "Выход R");
		for (int i = 0; i < P_COUNT; i++) names[i] = nameBuf[i];
		desc[P_IN_L] = desc[P_IN_R] = Ladspa::PORT_INPUT | Ladspa::PORT_AUDIO;
		desc[P_OUT_L] = desc[P_OUT_R] = Ladspa::PORT_OUTPUT | Ladspa::PORT_AUDIO;
		init = true;
	}
	static Ladspa::Descriptor d = {
		911, "beae_multiband", 0, "Многополосный компрессор", "BeAE-Plus", "BSD",
		P_COUNT, desc, names, hints, 0,
		Instantiate, Connect, Activate, Run, 0, 0, 0, Cleanup
	};
	return &d;
}

}	// namespace Multiband

extern "C" const Ladspa::Descriptor *ladspa_descriptor(unsigned long index)
{
	return index == 0 ? Multiband::Descriptor() : 0;
}
