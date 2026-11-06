/*
 * Sadp - см. Sadp.h
 */
#include "Sadp.h"

#include <math.h>
#include <string.h>

namespace Sadp {

static const float kPi = 3.14159265358979f;
static const int kBands = 5;			// 0 НЧ, 1 средние, 2..4 высокие
static const int kControlStep = 16;		// пересчёт порогов раз в столько отсчётов

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
};

// фильтр Линквица-Райли 4 порядка (два фильтра Баттерворта подряд)
struct LR4 {
	Biquad s[2];
	inline float Run(float x) { return s[1].Run(s[0].Run(x)); }
	void Design(bool hp, float f, float fs) { s[0].Design(hp, f, fs); s[1].Design(hp, f, fs); }
};

struct Split {			// разделитель на НЧ и ВЧ часть; сумма частей = фазовый (всепропускающий) фильтр
	LR4 lp, hp;
	void Design(float f, float fs) { lp.Design(false, f, fs); hp.Design(true, f, fs); }
	inline float Ap(float x) { return lp.Run(x) + hp.Run(x); }
};

struct Chan {
	Split x[4];				// основные разделители на частотах f1..f4
	Split ap[6];			// компенсация фазы нижних полос (НЧ: 3 штуки, средняя: 2, первая высокая: 1)
	float band[kBands];
	void Design(const float f[4], float fs)
	{
		for (int i = 0; i < 4; i++) x[i].Design(f[i], fs);
		ap[0].Design(f[1], fs); ap[1].Design(f[2], fs); ap[2].Design(f[3], fs);	// НЧ
		ap[3].Design(f[2], fs); ap[4].Design(f[3], fs);							// средняя
		ap[5].Design(f[3], fs);													// высокая 1
	}
	void Split5(float in)
	{
		float lo = x[0].lp.Run(in), r = x[0].hp.Run(in);
		float mid = x[1].lp.Run(r); r = x[1].hp.Run(r);
		float h1 = x[2].lp.Run(r); r = x[2].hp.Run(r);
		float h2 = x[3].lp.Run(r), h3 = x[3].hp.Run(r);
		lo = ap[2].Ap(ap[1].Ap(ap[0].Ap(lo)));
		mid = ap[4].Ap(ap[3].Ap(mid));
		h1 = ap[5].Ap(h1);
		band[0] = lo; band[1] = mid; band[2] = h1; band[3] = h2; band[4] = h3;
	}
};

struct Instance {
	float	rate;
	Chan	ch[2];
	float	env[kBands];		// мощность в полосе (сглаженная)
	float	gain[kBands];
	float	target[kBands];
	float	lastEdges[2];		// последние границы (для пересчёта фильтров)
	int		counter;
	float	*port[P_COUNT];
	Instance() : rate(44100), counter(0)
	{
		for (int i = 0; i < kBands; i++) { env[i] = 0; gain[i] = 1; target[i] = 1; }
		lastEdges[0] = lastEdges[1] = -1;
		for (int i = 0; i < P_COUNT; i++) port[i] = 0;
	}
};

static float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float Flush(float x) { return fabsf(x) < 1e-18f ? 0.0f : x; }

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
	for (int c = 0; c < 2; c++) {
		Chan &ch = s->ch[c];
		for (int i = 0; i < 4; i++) { Split *p = &ch.x[i]; for (int k = 0; k < 2; k++) { p->lp.s[k].z1 = p->lp.s[k].z2 = p->hp.s[k].z1 = p->hp.s[k].z2 = 0; } }
		for (int i = 0; i < 6; i++) { Split *p = &ch.ap[i]; for (int k = 0; k < 2; k++) { p->lp.s[k].z1 = p->lp.s[k].z2 = p->hp.s[k].z1 = p->hp.s[k].z2 = 0; } }
	}
	for (int i = 0; i < kBands; i++) { s->env[i] = 0; s->gain[i] = 1; s->target[i] = 1; }
	s->counter = 0;
}

static inline float OnePole(float ms, float rate) { return 1.0f - expf(-1.0f / (ms * 0.001f * rate)); }

static void Run(void *h, unsigned long n)
{
	Instance *s = (Instance *)h;
	for (int i = 0; i < P_COUNT; i++)
		if (!s->port[i]) return;

	const float fs = s->rate;
	const float thrAbs = Clamp(*s->port[P_THRESHOLD], -80.0f, -30.0f);
	const float conHF = Clamp(*s->port[P_CONTRAST_HF], 20.0f, 60.0f);
	const float conLF = Clamp(*s->port[P_CONTRAST_LF], 20.0f, 60.0f);
	const float depth = Clamp(*s->port[P_DEPTH], 0.0f, 30.0f);
	const float attackMs = Clamp(*s->port[P_ATTACK], 1.0f, 20.0f);
	const float releaseMs = Clamp(*s->port[P_RELEASE], 20.0f, 1000.0f);
	const float lfEdge = Clamp(*s->port[P_LF_EDGE], 50.0f, 400.0f);
	float hfEdge = Clamp(*s->port[P_HF_EDGE], 2000.0f, 8000.0f);
	const bool listen = *s->port[P_LISTEN] > 0.5f;

	// границы полос; верхние ограничены частотой дискретизации
	const float nyq = fs * 0.45f;
	float f[4] = { lfEdge, hfEdge, hfEdge * 2.0f, hfEdge * 3.5f };
	for (int i = 1; i < 4; i++) { if (f[i] > nyq) f[i] = nyq; if (f[i] <= f[i - 1]) f[i] = f[i - 1] * 1.05f; }
	if (f[3] > nyq * 1.1f) f[3] = nyq * 1.1f;
	if (lfEdge != s->lastEdges[0] || hfEdge != s->lastEdges[1]) {
		s->ch[0].Design(f, fs); s->ch[1].Design(f, fs);
		s->lastEdges[0] = lfEdge; s->lastEdges[1] = hfEdge;
	}

	const float envA = OnePole(1.0f, fs), envR = OnePole(40.0f, fs);			// детектор полосы
	const float midA = OnePole(5.0f, fs), midR = OnePole(100.0f, fs);			// детектор основного сигнала
	const float gOpen = OnePole(attackMs, fs), gClose = OnePole(releaseMs, fs);

	for (unsigned long i = 0; i < n; i++) {
		float in2[2] = { s->port[P_IN_L][i], s->port[P_IN_R][i] };
		for (int c = 0; c < 2; c++) s->ch[c].Split5(in2[c]);

		for (int k = 0; k < kBands; k++) {
			float p = 0.5f * (s->ch[0].band[k] * s->ch[0].band[k] + s->ch[1].band[k] * s->ch[1].band[k]);
			float a = (k == 1) ? midA : envA, r = (k == 1) ? midR : envR;
			s->env[k] += (p > s->env[k] ? a : r) * (p - s->env[k]);
			s->env[k] = Flush(s->env[k]);
		}

		if (s->counter-- <= 0) {					// пересчёт целей редко: логарифмы дорогие
			s->counter = kControlStep - 1;
			const float midDb = 10.0f * log10f(s->env[1] + 1e-12f);
			for (int k = 0; k < kBands; k++) {
				if (k == 1) { s->target[k] = 1.0f; continue; }
				const float con = (k == 0) ? conLF : conHF;
				const float thr = fmaxf(thrAbs, midDb - con);
				const float db = 10.0f * log10f(s->env[k] + 1e-12f);
				const float red = depth * Clamp((thr - db) / 6.0f, 0.0f, 1.0f);
				s->target[k] = powf(10.0f, -red / 20.0f);
			}
		}
		for (int k = 0; k < kBands; k++) {
			s->gain[k] += (s->target[k] > s->gain[k] ? gOpen : gClose) * (s->target[k] - s->gain[k]);
			s->gain[k] = Flush(s->gain[k]);
		}

		for (int c = 0; c < 2; c++) {
			float y = 0;
			for (int k = 0; k < kBands; k++) y += s->ch[c].band[k] * s->gain[k];
			s->port[c == 0 ? P_OUT_L : P_OUT_R][i] = listen ? (in2[c] - y) : y;
		}
	}
}

static void Cleanup(void *h) { delete (Instance *)h; }

const Ladspa::Descriptor *Descriptor()
{
	static const char *names[P_COUNT] = {
		"Порог шума, дБ", "Контраст ВЧ, дБ", "Контраст НЧ, дБ", "Подавление, дБ",
		"Атака (открытие), мс", "Спад (закрытие), мс", "Граница НЧ, Гц", "Начало ВЧ, Гц",
		"Слушать удаляемое", "Вход L", "Вход R", "Выход L", "Выход R"
	};
	static int desc[P_COUNT];
	static Ladspa::PortRangeHint hints[P_COUNT];
	static bool init = false;
	if (!init) {
		const int B = Ladspa::HINT_BOUNDED_BELOW | Ladspa::HINT_BOUNDED_ABOVE;
		for (int i = 0; i < P_COUNT; i++) { desc[i] = Ladspa::PORT_INPUT | Ladspa::PORT_CONTROL; hints[i] = { 0, 0, 0 }; }
		hints[P_THRESHOLD]	  = { B | Ladspa::HINT_DEFAULT_MIDDLE, -80.0f, -30.0f };				// -55
		hints[P_CONTRAST_HF]  = { B | Ladspa::HINT_DEFAULT_MIDDLE, 20.0f, 60.0f };					// 40
		hints[P_CONTRAST_LF]  = { B | Ladspa::HINT_DEFAULT_MIDDLE, 20.0f, 60.0f };					// 40
		hints[P_DEPTH]		  = { B | Ladspa::HINT_DEFAULT_MIDDLE, 0.0f, 30.0f };					// 15
		hints[P_ATTACK]		  = { B | Ladspa::HINT_DEFAULT_LOW, 1.0f, 20.0f };						// около 5,8
		hints[P_RELEASE]	  = { B | Ladspa::HINT_LOGARITHMIC | Ladspa::HINT_DEFAULT_MIDDLE, 20.0f, 1000.0f };		// около 141
		hints[P_LF_EDGE]	  = { B | Ladspa::HINT_LOGARITHMIC | Ladspa::HINT_DEFAULT_MIDDLE, 50.0f, 400.0f };		// около 141
		hints[P_HF_EDGE]	  = { B | Ladspa::HINT_LOGARITHMIC | Ladspa::HINT_DEFAULT_MIDDLE, 2000.0f, 8000.0f };	// 4000
		hints[P_LISTEN]		  = { Ladspa::HINT_TOGGLED | Ladspa::HINT_DEFAULT_0, 0.0f, 1.0f };
		desc[P_IN_L] = desc[P_IN_R] = Ladspa::PORT_INPUT | Ladspa::PORT_AUDIO;
		desc[P_OUT_L] = desc[P_OUT_R] = Ladspa::PORT_OUTPUT | Ladspa::PORT_AUDIO;
		init = true;
	}
	static Ladspa::Descriptor d = {
		903, "beae_sadp", 0, "САДП (адаптивное шумоподавление)", "BeAE-Plus", "BSD",
		P_COUNT, desc, names, hints, 0,
		Instantiate, Connect, Activate, Run, 0, 0, 0, Cleanup
	};
	return &d;
}

}	// namespace Sadp

// точка входа LADSPA
extern "C" const Ladspa::Descriptor *ladspa_descriptor(unsigned long index)
{
	return index == 0 ? Sadp::Descriptor() : 0;
}
