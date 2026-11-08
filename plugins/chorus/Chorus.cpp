/*
 * Хорус BeAE-Plus: плагин LADSPA (стерео), три голоса с плавающей задержкой (около 12 мс), фазы LFO разнесены,
 * правый канал сдвинут по фазе относительно левого. «Смесь» работает как посыл: сухой сигнал остаётся как есть,
 * к нему добавляется эффект. Плагин годится для любой дорожки; BeAE при импорте MIDI подставляет
 * в него значение контроллера «хорус» (CC93).
 */
#include "../common/ladspa_abi.h"
#include <math.h>
#include <algorithm>
#include <vector>

namespace {

enum Port { P_RATE = 0, P_DEPTH, P_MIX, P_FEEDBACK, P_IN_L, P_IN_R, P_OUT_L, P_OUT_R, P_COUNT };

const int kVoices = 3;
const float kBaseMs = 12.0f, kMaxDepthMs = 6.0f;

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
	const size_t n = (size_t)((kBaseMs + kMaxDepthMs + 4.0f) * 0.001f * s->rate) + 4;
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
	const float rateHz = Clamp(*s->port[P_RATE], 0.05f, 10.0f);
	const float depth = Clamp(*s->port[P_DEPTH], 0.0f, kMaxDepthMs) * 0.001f * s->rate;
	const float mix = Clamp(*s->port[P_MIX], 0.0f, 100.0f) * 0.01f;
	const float fb = Clamp(*s->port[P_FEEDBACK], 0.0f, 60.0f) * 0.01f;
	const double inc = rateHz / s->rate;
	const float base = kBaseMs * 0.001f * s->rate;
	const size_t size = s->buf[0].size();
	const float *in[2] = { s->port[P_IN_L], s->port[P_IN_R] };
	float *out[2] = { s->port[P_OUT_L], s->port[P_OUT_R] };
	for (unsigned long k = 0; k < n; k++) {
		for (int c = 0; c < 2; c++) {
			float wet = 0;
			for (int v = 0; v < kVoices; v++) {
				double ph = s->phase + (double)v / kVoices + (c ? 0.25 : 0.0);
				ph -= floor(ph);
				const float d = base + depth * (0.5f + 0.5f * sinf((float)(6.283185307179586 * ph)));
				float rp = (float)s->pos - d;
				while (rp < 0) rp += (float)size;
				const size_t i0 = (size_t)rp % size;
				const size_t i1 = (i0 + 1) % size;
				const float fr = rp - floorf(rp);
				wet += s->buf[c][i0] + (s->buf[c][i1] - s->buf[c][i0]) * fr;
			}
			wet *= 1.0f / kVoices;
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
	static const char *names[P_COUNT] = { "Скорость, Гц", "Глубина, мс", "Смесь, %", "Обратная связь, %", "Вход L", "Вход R", "Выход L", "Выход R" };
	static int desc[P_COUNT];
	static Ladspa::PortRangeHint hints[P_COUNT];
	static bool init = false;
	if (!init) {
		const int ctl = Ladspa::PORT_INPUT | Ladspa::PORT_CONTROL;
		const int B = Ladspa::HINT_BOUNDED_BELOW | Ladspa::HINT_BOUNDED_ABOVE;
		for (int i = 0; i < P_COUNT; i++) { desc[i] = ctl; hints[i] = { 0, 0, 0 }; }
		hints[P_RATE]		= { B | Ladspa::HINT_LOGARITHMIC | Ladspa::HINT_DEFAULT_MIDDLE, 0.1f, 5.0f };	// около 0.7 Гц
		hints[P_DEPTH]		= { B | Ladspa::HINT_DEFAULT_MIDDLE, 0.0f, kMaxDepthMs };		// 3 мс
		hints[P_MIX]		= { B | Ladspa::HINT_DEFAULT_MIDDLE, 0.0f, 100.0f };			// 50
		hints[P_FEEDBACK]	= { B | Ladspa::HINT_DEFAULT_0, 0.0f, 60.0f };
		desc[P_IN_L] = desc[P_IN_R] = Ladspa::PORT_INPUT | Ladspa::PORT_AUDIO;
		desc[P_OUT_L] = desc[P_OUT_R] = Ladspa::PORT_OUTPUT | Ladspa::PORT_AUDIO;
		init = true;
	}
	static Ladspa::Descriptor d = {
		908, "beae_chorus", 0, "Хорус", "BeAE-Plus", "BSD",
		P_COUNT, desc, names, hints, 0,
		Instantiate, Connect, Activate, Run, 0, 0, 0, Cleanup
	};
	return &d;
}
