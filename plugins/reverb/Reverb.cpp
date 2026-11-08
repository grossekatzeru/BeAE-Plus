/*
 * Реверберация BeAE-Plus: плагин LADSPA (стерео), алгоритмический реверб по схеме Шрёдера-Мурера:
 * 8 гребенчатых фильтров с демпфированием и 4 всепропускающих на канал, правый канал с небольшим сдвигом длин.
 * «Смесь» работает как посыл: сухой сигнал остаётся как есть, к нему добавляется эффект.
 * Плагин годится для любой дорожки; BeAE при импорте MIDI подставляет в него значение контроллера «реверберация» (CC91).
 */
#include "../common/ladspa_abi.h"
#include <math.h>
#include <string.h>
#include <algorithm>
#include <vector>

namespace {

enum Port { P_SIZE = 0, P_DAMP, P_MIX, P_PREDELAY, P_WIDTH, P_IN_L, P_IN_R, P_OUT_L, P_OUT_R, P_COUNT };

const int kCombs = 8, kAllpass = 4;
const int kCombTune[kCombs] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
const int kAllTune[kAllpass] = { 556, 441, 341, 225 };
const int kSpread = 23;			// сдвиг длин правого канала
const float kMaxPredelayMs = 100.0f;

struct Comb {
	std::vector<float> buf; size_t pos; float store;
	void Init(size_t n) { buf.assign(n < 1 ? 1 : n, 0.0f); pos = 0; store = 0; }
	inline float Run(float in, float feedback, float damp)
	{
		float o = buf[pos];
		store = o * (1.0f - damp) + store * damp;
		if (fabsf(store) < 1e-20f) store = 0;
		buf[pos] = in + store * feedback;
		if (++pos >= buf.size()) pos = 0;
		return o;
	}
};

struct Allpass {
	std::vector<float> buf; size_t pos;
	void Init(size_t n) { buf.assign(n < 1 ? 1 : n, 0.0f); pos = 0; }
	inline float Run(float in)
	{
		float b = buf[pos];
		float o = -in + b;
		buf[pos] = in + b * 0.5f;
		if (++pos >= buf.size()) pos = 0;
		return o;
	}
};

struct Instance {
	float rate;
	Comb comb[2][kCombs];
	Allpass all[2][kAllpass];
	std::vector<float> pre; size_t prePos;
	float *port[P_COUNT];
};

void *Instantiate(const Ladspa::Descriptor *, unsigned long rate)
{
	Instance *s = new Instance;
	s->rate = rate > 0 ? (float)rate : 44100.0f;
	const float k = s->rate / 44100.0f;
	for (int c = 0; c < 2; c++) {
		for (int i = 0; i < kCombs; i++) s->comb[c][i].Init((size_t)((kCombTune[i] + c * kSpread) * k));
		for (int i = 0; i < kAllpass; i++) s->all[c][i].Init((size_t)((kAllTune[i] + c * kSpread) * k));
	}
	s->pre.assign((size_t)(kMaxPredelayMs * 0.001f * s->rate) + 2, 0.0f);
	s->prePos = 0;
	for (int i = 0; i < P_COUNT; i++) s->port[i] = 0;
	return s;
}

void Connect(void *h, unsigned long p, float *loc)
{
	Instance *s = (Instance *)h;
	if (p < P_COUNT) s->port[p] = loc;
}

void Activate(void *h)
{
	Instance *s = (Instance *)h;
	for (int c = 0; c < 2; c++) {
		for (int i = 0; i < kCombs; i++) { std::fill(s->comb[c][i].buf.begin(), s->comb[c][i].buf.end(), 0.0f); s->comb[c][i].store = 0; }
		for (int i = 0; i < kAllpass; i++) std::fill(s->all[c][i].buf.begin(), s->all[c][i].buf.end(), 0.0f);
	}
	std::fill(s->pre.begin(), s->pre.end(), 0.0f);
}

inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

void Run(void *h, unsigned long n)
{
	Instance *s = (Instance *)h;
	for (int i = 0; i < P_COUNT; i++) if (!s->port[i]) return;
	const float size = Clamp(*s->port[P_SIZE], 0.0f, 1.0f);
	const float damp = Clamp(*s->port[P_DAMP], 0.0f, 1.0f) * 0.4f;
	const float mix = Clamp(*s->port[P_MIX], 0.0f, 100.0f) * 0.01f;
	const float feedback = 0.7f + size * 0.28f;
	const float width = Clamp(*s->port[P_WIDTH], 0.0f, 1.0f);
	const float w1 = width * 0.5f + 0.5f, w2 = (1.0f - width) * 0.5f;
	size_t delay = (size_t)(Clamp(*s->port[P_PREDELAY], 0.0f, kMaxPredelayMs) * 0.001f * s->rate);
	if (delay >= s->pre.size()) delay = s->pre.size() - 1;
	const float *inL = s->port[P_IN_L], *inR = s->port[P_IN_R];
	float *outL = s->port[P_OUT_L], *outR = s->port[P_OUT_R];
	const float inGain = 0.015f, wetScale = 3.0f;
	for (unsigned long k = 0; k < n; k++) {
		const float dl = inL[k], dr = inR[k];
		s->pre[s->prePos] = (dl + dr) * inGain;
		size_t rp = s->prePos + s->pre.size() - delay;
		if (rp >= s->pre.size()) rp -= s->pre.size();
		const float x = s->pre[rp];
		if (++s->prePos >= s->pre.size()) s->prePos = 0;
		float wet[2];
		for (int c = 0; c < 2; c++) {
			float acc = 0;
			for (int i = 0; i < kCombs; i++) acc += s->comb[c][i].Run(x, feedback, damp);
			for (int i = 0; i < kAllpass; i++) acc = s->all[c][i].Run(acc);
			wet[c] = acc;
		}
		const float wl = wet[0] * w1 + wet[1] * w2, wr = wet[1] * w1 + wet[0] * w2;
		outL[k] = dl + wl * wetScale * mix;
		outR[k] = dr + wr * wetScale * mix;
	}
}

void Cleanup(void *h) { delete (Instance *)h; }

}	// namespace

extern "C" const Ladspa::Descriptor *ladspa_descriptor(unsigned long index)
{
	if (index != 0) return 0;
	static const char *names[P_COUNT] = { "Размер комнаты", "Затухание высоких", "Смесь, %", "Предзадержка, мс", "Ширина", "Вход L", "Вход R", "Выход L", "Выход R" };
	static int desc[P_COUNT];
	static Ladspa::PortRangeHint hints[P_COUNT];
	static bool init = false;
	if (!init) {
		const int ctl = Ladspa::PORT_INPUT | Ladspa::PORT_CONTROL;
		const int B = Ladspa::HINT_BOUNDED_BELOW | Ladspa::HINT_BOUNDED_ABOVE;
		for (int i = 0; i < P_COUNT; i++) { desc[i] = ctl; hints[i] = { 0, 0, 0 }; }
		hints[P_SIZE]		= { B | Ladspa::HINT_DEFAULT_HIGH, 0.0f, 1.0f };			// 0.75
		hints[P_DAMP]		= { B | Ladspa::HINT_DEFAULT_MIDDLE, 0.0f, 1.0f };			// 0.5
		hints[P_MIX]		= { B | Ladspa::HINT_DEFAULT_LOW, 0.0f, 100.0f };			// 25
		hints[P_PREDELAY]	= { B | Ladspa::HINT_DEFAULT_0, 0.0f, kMaxPredelayMs };
		hints[P_WIDTH]		= { B | Ladspa::HINT_DEFAULT_MAXIMUM, 0.0f, 1.0f };
		desc[P_IN_L] = desc[P_IN_R] = Ladspa::PORT_INPUT | Ladspa::PORT_AUDIO;
		desc[P_OUT_L] = desc[P_OUT_R] = Ladspa::PORT_OUTPUT | Ladspa::PORT_AUDIO;
		init = true;
	}
	static Ladspa::Descriptor d = {
		907, "beae_reverb", 0, "Реверберация", "BeAE-Plus", "BSD",
		P_COUNT, desc, names, hints, 0,
		Instantiate, Connect, Activate, Run, 0, 0, 0, Cleanup
	};
	return &d;
}
