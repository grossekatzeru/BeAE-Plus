/*
 * Фленджер BeAE-Plus: плагин LADSPA (стерео). Короткая задержка (доли миллисекунды - десятки мс), которую
 * качает низкочастотный генератор; задержанный сигнал складывается с сухим и даёт «гребёнку» провалов АЧХ,
 * которая плывёт вверх-вниз («реактивный» свист). Обратная связь положительная или отрицательная
 * (отрицательная - более «металлический» звук с провалами на другом наборе частот).
 *
 * Задержка d(t) = Ручная * (1 + 0.9 * Глубина * sin(фаза LFO)); при Глубине 0 фленджер стоит на месте.
 * Правый канал отстаёт по фазе LFO на заданный угол (стерео-разнесение).
 * «Смесь» работает как посыл: сухой остаётся, к нему добавляется эффект (100 % - глубокие провалы).
 */
#include "../common/ladspa_abi.h"
#include <math.h>
#include <algorithm>
#include <vector>

namespace {

enum Port { P_RATE = 0, P_DEPTH, P_MANUAL, P_FEEDBACK, P_STEREO, P_MIX, P_IN_L, P_IN_R, P_OUT_L, P_OUT_R, P_COUNT };

const float kMaxManualMs = 10.0f;

struct Instance {
	float rate;
	std::vector<float> buf[2];
	size_t pos;
	double phase;
	float *port[P_COUNT];
};

void *Instantiate(const Ladspa::Descriptor *, unsigned long rate)
{
	Instance *s = new Instance;
	s->rate = rate > 0 ? (float)rate : 44100.0f;
	const size_t n = (size_t)(kMaxManualMs * 1.95f * 0.001f * s->rate) + 8;
	s->buf[0].assign(n, 0.0f); s->buf[1].assign(n, 0.0f);
	s->pos = 0; s->phase = 0;
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
	for (int c = 0; c < 2; c++) std::fill(s->buf[c].begin(), s->buf[c].end(), 0.0f);
	s->phase = 0;
}

inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

void Run(void *h, unsigned long n)
{
	Instance *s = (Instance *)h;
	for (int i = 0; i < P_COUNT; i++) if (!s->port[i]) return;
	const float rateHz = Clamp(*s->port[P_RATE], 0.02f, 10.0f);
	const float depth = Clamp(*s->port[P_DEPTH], 0.0f, 1.0f) * 0.9f;
	const float manual = Clamp(*s->port[P_MANUAL], 0.2f, kMaxManualMs) * 0.001f * s->rate;
	const float fb = Clamp(*s->port[P_FEEDBACK], -95.0f, 95.0f) * 0.01f;
	const double stereo = Clamp(*s->port[P_STEREO], 0.0f, 180.0f) / 360.0;
	const float mix = Clamp(*s->port[P_MIX], 0.0f, 100.0f) * 0.01f;
	const double inc = rateHz / s->rate;
	const size_t size = s->buf[0].size();
	const float *in[2] = { s->port[P_IN_L], s->port[P_IN_R] };
	float *out[2] = { s->port[P_OUT_L], s->port[P_OUT_R] };
	for (unsigned long k = 0; k < n; k++) {
		for (int c = 0; c < 2; c++) {
			double ph = s->phase + (c ? stereo : 0.0);
			ph -= floor(ph);
			float d = manual * (1.0f + depth * sinf((float)(6.283185307179586 * ph)));
			if (d < 1.5f) d = 1.5f;
			// значение на d кадров назад: целая часть di и дробь fr считаются отдельно (без выхода за границы буфера)
			const size_t di = (size_t)d;
			const float fr = d - (float)di;
			const size_t ia = (s->pos + size - di) % size;				// отсчёт «di назад»
			const size_t ib = (ia + size - 1) % size;					// на один старше
			const float wet = s->buf[c][ia] + (s->buf[c][ib] - s->buf[c][ia]) * fr;
			const float x = in[c][k];
			s->buf[c][s->pos] = x + wet * fb;
			out[c][k] = x + wet * mix;
		}
		if (++s->pos >= size) s->pos = 0;
		s->phase += inc;
		if (s->phase >= 1.0) s->phase -= 1.0;
	}
}

void Cleanup(void *h) { delete (Instance *)h; }

}	// namespace

extern "C" const Ladspa::Descriptor *ladspa_descriptor(unsigned long index)
{
	if (index != 0) return 0;
	static const char *names[P_COUNT] = {
		"Скорость, Гц", "Глубина", "Ручная задержка, мс", "Обратная связь, %", "Стерео-разнесение, °", "Смесь, %",
		"Вход L", "Вход R", "Выход L", "Выход R"
	};
	static int desc[P_COUNT];
	static Ladspa::PortRangeHint hints[P_COUNT];
	static bool init = false;
	if (!init) {
		const int ctl = Ladspa::PORT_INPUT | Ladspa::PORT_CONTROL;
		const int B = Ladspa::HINT_BOUNDED_BELOW | Ladspa::HINT_BOUNDED_ABOVE;
		for (int i = 0; i < P_COUNT; i++) { desc[i] = ctl; hints[i] = { 0, 0, 0 }; }
		hints[P_RATE]		= { B | Ladspa::HINT_LOGARITHMIC | Ladspa::HINT_DEFAULT_LOW, 0.05f, 5.0f };	// около 0.16 Гц
		hints[P_DEPTH]		= { B | Ladspa::HINT_DEFAULT_HIGH, 0.0f, 1.0f };			// 0.75
		hints[P_MANUAL]		= { B | Ladspa::HINT_LOGARITHMIC | Ladspa::HINT_DEFAULT_MIDDLE, 0.2f, kMaxManualMs };	// около 1.4 мс
		hints[P_FEEDBACK]	= { B | Ladspa::HINT_DEFAULT_MIDDLE, -95.0f, 95.0f };		// 0
		hints[P_STEREO]		= { B | Ladspa::HINT_DEFAULT_MIDDLE, 0.0f, 180.0f };		// 90
		hints[P_MIX]		= { B | Ladspa::HINT_DEFAULT_MIDDLE, 0.0f, 100.0f };		// 50
		desc[P_IN_L] = desc[P_IN_R] = Ladspa::PORT_INPUT | Ladspa::PORT_AUDIO;
		desc[P_OUT_L] = desc[P_OUT_R] = Ladspa::PORT_OUTPUT | Ladspa::PORT_AUDIO;
		init = true;
	}
	static Ladspa::Descriptor d = {
		913, "beae_flanger", 0, "Фленджер", "BeAE-Plus", "BSD",
		P_COUNT, desc, names, hints, 0,
		Instantiate, Connect, Activate, Run, 0, 0, 0, Cleanup
	};
	return &d;
}
