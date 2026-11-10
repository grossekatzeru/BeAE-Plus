/*
 * Дисторшн в духе BOSS DS-1 («Дисторшн (DS-1)»): плагин LADSPA, стерео.
 * Модель схемы «по описанию прибора», а не точная копия. Обработка на частоте x4 (передискретизация), задержка 32 отсчёта.
 *
 * Схема (как у оригинала):
 *  - входной транзисторный каскад: срез низких (около 100 Гц), усиление около 2 раз и лёгкая асимметрия (чётные гармоники);
 *  - операционный усилитель с большим усилением: от 10 до 300 раз («Дисторшн»), срез низких около 220 Гц, ограничение полосы
 *    произведением коэффициент усиления x полоса (около 1,5 МГц) - на большом усилении верха темнее;
 *  - жёсткое ограничение диодами на землю после усилителя (резкое колено, пределы +0,66 и -0,60);
 *  - «Тон»: плавный переход между ФНЧ 720 Гц и ФВЧ 2,4 кГц (в середине получается провал в средних, как у оригинала);
 *  - «Уровень», «Вход» (подстройка под запись), «Смесь» (параллельный чистый сигнал).
 */
#include "../common/ladspa_abi.h"
#include "../common/oversample.h"
#include <math.h>

namespace {

enum Port { P_TRIM = 0, P_DIST, P_TONE, P_LEVEL, P_MIX, P_IN_L, P_IN_R, P_OUT_L, P_OUT_R, P_COUNT };

inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// жёсткий мягко-угловой предел с порогом v: x / (1 + (|x|/v)^3)^(1/3)
inline float HardClip(float x, float v)
{
	const float r = fabsf(x) / v;
	return x / cbrtf(1.0f + r * r * r);
}

struct Chain {
	Os::OnePole hpIn, hpQ, hpOp, lpGbw, lpTone, hpTone, dc;
	float K, toneMix, outGain;
	void Clear() { hpIn.Clear(); hpQ.Clear(); hpOp.Clear(); lpGbw.Clear(); lpTone.Clear(); hpTone.Clear(); dc.Clear(); }
	inline float Tick(float x)
	{
		x = hpIn.HP(x);
		// транзисторный каскад: срез низких, усиление ~2, асимметрия
		const float q = hpQ.HP(x);
		const float pre = 1.7f * (0.6f * tanhf(2.0f * q + 0.08f) - 0.6f * tanhf(0.08f));
		// операционный усилитель
		float z = K * hpOp.HP(pre);
		z = lpGbw.LP(z);
		// диоды на землю: пределы немного разные
		const float c = z >= 0 ? HardClip(z, 0.66f) : HardClip(z, 0.60f);
		// тон: ФНЧ <-> ФВЧ
		const float lo = lpTone.LP(c);
		const float hi = hpTone.HP(c);
		const float t = (1.0f - toneMix) * lo * 1.7f + toneMix * hi * 1.2f;
		return dc.HP(t) * outGain;
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
	const float dist = Clamp(*s->port[P_DIST], 0.0f, 100.0f) * 0.01f;
	const float tone = Clamp(*s->port[P_TONE], 0.0f, 100.0f) * 0.01f;
	const float level = powf(10.0f, Clamp(*s->port[P_LEVEL], -24.0f, 12.0f) / 20.0f);
	const float mix = Clamp(*s->port[P_MIX], 0.0f, 100.0f) * 0.01f;
	const float fs4 = 4.0f * s->rate;
	const float K = 10.0f * powf(30.0f, dist);				// 10 ... 300
	float gbw = 1.5e6f / (1.0f + K);
	if (gbw > 40000.0f) gbw = 40000.0f;
	for (int c = 0; c < 2; c++) {
		Chain &ch = s->ch[c];
		ch.K = K; ch.toneMix = tone; ch.outGain = level;
		ch.hpIn.Set(30.0f, fs4); ch.hpQ.Set(100.0f, fs4); ch.hpOp.Set(220.0f, fs4); ch.lpGbw.Set(gbw, fs4);
		ch.lpTone.Set(720.0f, fs4); ch.hpTone.Set(2400.0f, fs4); ch.dc.Set(20.0f, fs4);
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
		"Вход, дБ", "Дисторшн, %", "Тон, %", "Уровень, дБ", "Смесь, %",
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
		hints[P_DIST]	= { B | Ladspa::HINT_DEFAULT_MIDDLE, 0.0f, 100.0f };		// 50
		hints[P_TONE]	= { B | Ladspa::HINT_DEFAULT_MIDDLE, 0.0f, 100.0f };		// 50
		hints[P_LEVEL]	= { B | Ladspa::HINT_DEFAULT_0, -24.0f, 12.0f };
		hints[P_MIX]	= { B | Ladspa::HINT_DEFAULT_MAXIMUM, 0.0f, 100.0f };		// 100
		desc[P_IN_L] = desc[P_IN_R] = Ladspa::PORT_INPUT | Ladspa::PORT_AUDIO;
		desc[P_OUT_L] = desc[P_OUT_R] = Ladspa::PORT_OUTPUT | Ladspa::PORT_AUDIO;
		init = true;
	}
	static Ladspa::Descriptor d = {
		918, "beae_ds1", 0, "Дисторшн (DS-1)", "BeAE-Plus", "BSD",
		P_COUNT, desc, names, hints, 0,
		Instantiate, Connect, Activate, Run, 0, 0, 0, Cleanup
	};
	return &d;
}
