/*
 * Овердрайв в духе Ibanez Tube Screamer («Овердрайв (Tube Screamer)»): плагин LADSPA, стерео.
 * Модель схемы «по описанию прибора», а не точная копия. Обработка на частоте x4 (передискретизация), задержка 32 отсчёта.
 *
 * Схема (как у оригинала):
 *  - усилительный каскад с обратной связью: выход = вход + K * ФВЧ(вход), K = (51 кОм + Драйв * 500 кОм) / 4,7 кОм;
 *    ФВЧ на 720 Гц даёт «горб» в средних: низкие почти не усиливаются, поэтому овердрайв не «грязнит» бас;
 *  - диоды в цепи обратной связи ограничивают добавку (мягкий предел около 0,6), чистый сигнал идёт рядом с ограниченным;
 *  - полоса усилителя ограничена произведением коэффициент усиления x полоса (около 3 МГц), на большом драйве верха темнее;
 *  - «Тон»: ФНЧ 0,7 ... 5,6 кГц; «Уровень»; «Вход» - подстройка под уровень записи; «Смесь» - параллельный чистый сигнал.
 */
#include "../common/ladspa_abi.h"
#include "../common/oversample.h"
#include <math.h>

namespace {

enum Port { P_TRIM = 0, P_DRIVE, P_TONE, P_LEVEL, P_MIX, P_IN_L, P_IN_R, P_OUT_L, P_OUT_R, P_COUNT };

inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct Chain {
	Os::OnePole hpIn, hp720, lpGbw, tone, dc;
	float K, outGain;
	void Clear() { hpIn.Clear(); hp720.Clear(); lpGbw.Clear(); tone.Clear(); dc.Clear(); }
	inline float Tick(float x)
	{
		x = hpIn.HP(x);							// разделительный конденсатор на входе
		float z = K * hp720.HP(x);				// добавка усилителя: K * ФВЧ(720 Гц)
		z = lpGbw.LP(z);						// ограничение полосы усилителя
		const float r = z * (1.0f / 0.6f);
		const float vf = z / sqrtf(1.0f + r * r);	// диоды: мягкий предел около +-0.6
		const float y = x + vf;
		return dc.HP(tone.LP(y)) * outGain;
	}
	inline float operator()(float x) { return Tick(x); }
};

struct Instance {
	float rate;
	Os::Oversampler4 os[2];
	Os::DryDelay dry[2];
	Chain ch[2];
	float *port[P_COUNT];
};

void *Instantiate(const Ladspa::Descriptor *, unsigned long rate)
{
	Instance *s = new Instance;
	s->rate = rate > 0 ? (float)rate : 44100.0f;
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
	for (int c = 0; c < 2; c++) { s->os[c].Clear(); s->dry[c].Clear(); s->ch[c].Clear(); }
}

void Run(void *h, unsigned long n)
{
	Instance *s = (Instance *)h;
	for (int i = 0; i < P_COUNT; i++) if (!s->port[i]) return;
	const float trim = powf(10.0f, Clamp(*s->port[P_TRIM], -12.0f, 24.0f) / 20.0f);
	const float drive = Clamp(*s->port[P_DRIVE], 0.0f, 100.0f) * 0.01f;
	const float tone = Clamp(*s->port[P_TONE], 0.0f, 100.0f) * 0.01f;
	const float level = powf(10.0f, Clamp(*s->port[P_LEVEL], -24.0f, 12.0f) / 20.0f) * 0.9f;
	const float mix = Clamp(*s->port[P_MIX], 0.0f, 100.0f) * 0.01f;
	const float fs4 = 4.0f * s->rate;
	const float rf = 51000.0f + 500000.0f * drive * drive;			// потенциометр «А»: квадратичная кривая
	const float K = rf / 4700.0f;
	float gbw = 3.0e6f / (1.0f + K);
	if (gbw > 40000.0f) gbw = 40000.0f;
	const float toneFc = 700.0f * powf(8.0f, tone);
	for (int c = 0; c < 2; c++) {
		Chain &ch = s->ch[c];
		ch.K = K; ch.outGain = level;
		ch.hpIn.Set(20.0f, fs4); ch.hp720.Set(720.0f, fs4); ch.lpGbw.Set(gbw, fs4);
		ch.tone.Set(toneFc, fs4); ch.dc.Set(20.0f, fs4);
	}
	const float *in[2] = { s->port[P_IN_L], s->port[P_IN_R] };
	float *out[2] = { s->port[P_OUT_L], s->port[P_OUT_R] };
	for (unsigned long k = 0; k < n; k++) {
		for (int c = 0; c < 2; c++) {
			const float x = in[c][k];
			const float wet = s->os[c].Process(x * trim, s->ch[c]);
			const float dry = s->dry[c].Process(x);
			out[c][k] = dry * (1.0f - mix) + wet * mix;
		}
	}
}

void Cleanup(void *h) { delete (Instance *)h; }

}	// namespace

extern "C" const Ladspa::Descriptor *ladspa_descriptor(unsigned long index)
{
	if (index != 0) return 0;
	static const char *names[P_COUNT] = {
		"Вход, дБ", "Драйв, %", "Тон, %", "Уровень, дБ", "Смесь, %",
		"Вход L", "Вход R", "Выход L", "Выход R"
	};
	static int desc[P_COUNT];
	static Ladspa::PortRangeHint hints[P_COUNT];
	static bool init = false;
	if (!init) {
		const int ctl = Ladspa::PORT_INPUT | Ladspa::PORT_CONTROL;
		const int B = Ladspa::HINT_BOUNDED_BELOW | Ladspa::HINT_BOUNDED_ABOVE;
		for (int i = 0; i < P_COUNT; i++) { desc[i] = ctl; hints[i] = { 0, 0, 0 }; }
		hints[P_TRIM]	= { B | Ladspa::HINT_DEFAULT_0, -12.0f, 24.0f };
		hints[P_DRIVE]	= { B | Ladspa::HINT_DEFAULT_MIDDLE, 0.0f, 100.0f };		// 50
		hints[P_TONE]	= { B | Ladspa::HINT_DEFAULT_MIDDLE, 0.0f, 100.0f };		// 50
		hints[P_LEVEL]	= { B | Ladspa::HINT_DEFAULT_0, -24.0f, 12.0f };
		hints[P_MIX]	= { B | Ladspa::HINT_DEFAULT_MAXIMUM, 0.0f, 100.0f };		// 100
		desc[P_IN_L] = desc[P_IN_R] = Ladspa::PORT_INPUT | Ladspa::PORT_AUDIO;
		desc[P_OUT_L] = desc[P_OUT_R] = Ladspa::PORT_OUTPUT | Ladspa::PORT_AUDIO;
		init = true;
	}
	static Ladspa::Descriptor d = {
		917, "beae_tube_screamer", 0, "Овердрайв (Tube Screamer)", "BeAE-Plus", "BSD",
		P_COUNT, desc, names, hints, 0,
		Instantiate, Connect, Activate, Run, 0, 0, 0, Cleanup
	};
	return &d;
}
