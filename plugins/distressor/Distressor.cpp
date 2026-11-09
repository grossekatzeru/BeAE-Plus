/*
 * Универсальный компрессор с «характером» в духе Empirical Labs Distressor («Дистрессор»): плагин LADSPA, стерео.
 * Модель «по описанию прибора», а не точная копия.
 *
 * Что есть:
 *  - порог, степень сжатия 1:1 ... 20:1, атака (0,05 - 100 мс) и возврат (20 мс - 3 с), усиление на входе и выходе;
 *  - три режима: «Обычный» (колено 3 дБ), «Мягкий / оптика» (колено 12 дБ, возврат зависит от программы:
 *    после долгого сжатия медленнее) и «Nuke» (почти лимитер: степень 100:1, атака не медленнее 0,5 мс);
 *  - фильтр детектора: срез низких (чтобы бас не «качал» сжатие);
 *  - гармоники: «чётные» (лампа, 2-я гармоника) или «нечётные» (транзистор, 3-я гармоника) с регулировкой количества;
 *  - «Параллельно»: смесь сухого и сжатого сигналов.
 *
 * Детектор: пиковый с затуханием около 5 мс; каналы связаны (берётся более громкий). Сжатие считается в дБ
 * и сглаживается постоянными времени атаки/возврата.
 */
#include "../common/ladspa_abi.h"
#include <math.h>

namespace {

enum Port {
	P_INPUT = 0, P_THRESHOLD, P_RATIO, P_ATTACK, P_RELEASE, P_MODE, P_DET_HP, P_HARM, P_AMOUNT, P_OUTPUT, P_MIX,
	P_IN_L, P_IN_R, P_OUT_L, P_OUT_R, P_COUNT
};

inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct Instance {
	float rate;
	float env;				// пиковый детектор
	float gr, slow;			// сжатие, дБ; медленная составляющая («оптика»)
	float hpS[2];			// состояние ФВЧ детектора (ФНЧ для вычитания)
	float dc[2];			// ФВЧ после гармоник
	float *port[P_COUNT];
};

void *Instantiate(const Ladspa::Descriptor *, unsigned long rate)
{
	Instance *s = new Instance;
	s->rate = rate > 0 ? (float)rate : 44100.0f;
	s->env = s->gr = s->slow = 0;
	s->hpS[0] = s->hpS[1] = s->dc[0] = s->dc[1] = 0;
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
	s->env = s->gr = s->slow = 0;
	s->hpS[0] = s->hpS[1] = s->dc[0] = s->dc[1] = 0;
}

inline float StaticReduction(float levelDb, float thr, float ratio, float knee)
{
	const float over = levelDb - thr, slope = 1.0f - 1.0f / ratio;
	if (knee <= 0.0f) return over > 0 ? slope * over : 0.0f;
	if (over <= -knee * 0.5f) return 0.0f;
	if (over >= knee * 0.5f) return slope * over;
	const float t = over + knee * 0.5f;
	return slope * t * t / (2.0f * knee);
}

// гармоники: 1 - чётные (x*x), 2 - нечётные (мягкий куб); при малом сигнале усиление 1
inline float Harmonics(float x, int mode, float amt)
{
	if (mode == 1) return x + amt * x * x;
	if (mode == 2) {
		return x - amt * 0.8f * x * x * x;
	}
	return x;
}

void Run(void *h, unsigned long n)
{
	Instance *s = (Instance *)h;
	for (int i = 0; i < P_COUNT; i++) if (!s->port[i]) return;
	const float inGain = powf(10.0f, Clamp(*s->port[P_INPUT], 0.0f, 30.0f) / 20.0f);
	const float outGain = powf(10.0f, Clamp(*s->port[P_OUTPUT], -30.0f, 30.0f) / 20.0f);
	const float thr = Clamp(*s->port[P_THRESHOLD], -60.0f, 0.0f);
	const float ratioPort = Clamp(*s->port[P_RATIO], 1.0f, 20.0f);
	const int mode = (int)(Clamp(*s->port[P_MODE], 0.0f, 2.0f) + 0.5f);
	const float ratio = mode == 2 ? 100.0f : ratioPort;
	const float knee = mode == 0 ? 3.0f : (mode == 1 ? 12.0f : 0.0f);
	float atkMs = Clamp(*s->port[P_ATTACK], 0.05f, 100.0f);
	if (mode == 2 && atkMs > 0.5f) atkMs = 0.5f;
	const float relMs = Clamp(*s->port[P_RELEASE], 20.0f, 3000.0f);
	const float detHp = Clamp(*s->port[P_DET_HP], 20.0f, 400.0f);
	const int harm = (int)(Clamp(*s->port[P_HARM], 0.0f, 2.0f) + 0.5f);
	const float amount = Clamp(*s->port[P_AMOUNT], 0.0f, 100.0f) * 0.01f;
	const float mix = Clamp(*s->port[P_MIX], 0.0f, 100.0f) * 0.01f;
	const float aAtk = 1.0f - expf(-1.0f / (atkMs * 0.001f * s->rate));
	const float aRel = 1.0f - expf(-1.0f / (relMs * 0.001f * s->rate));
	const float aSlowAtk = 1.0f - expf(-1.0f / (0.8f * s->rate));
	const float aSlowRel = 1.0f - expf(-1.0f / (relMs * 5.0f * 0.001f * s->rate));
	const float hold = expf(-1.0f / (0.005f * s->rate));
	const float aHp = 1.0f - expf(-6.283185307f * detHp / s->rate);
	const float aDc = 1.0f - expf(-6.283185307f * 15.0f / s->rate);
	const float *in[2] = { s->port[P_IN_L], s->port[P_IN_R] };
	float *out[2] = { s->port[P_OUT_L], s->port[P_OUT_R] };
	for (unsigned long k = 0; k < n; k++) {
		float xs[2], pk = 0;
		for (int c = 0; c < 2; c++) {
			xs[c] = in[c][k] * inGain;
			// детектор: ФВЧ для боковой цепи
			s->hpS[c] += (xs[c] - s->hpS[c]) * aHp;
			if (fabsf(s->hpS[c]) < 1e-20f) s->hpS[c] = 0;
			const float d = fabsf(xs[c] - s->hpS[c]);
			if (d > pk) pk = d;
		}
		s->env = pk > s->env ? pk : s->env * hold;
		if (s->env < 1e-20f) s->env = 0;
		const float levelDb = s->env > 1e-9f ? 20.0f * log10f(s->env) : -180.0f;
		const float target = StaticReduction(levelDb, thr, ratio, knee);
		float g;
		if (mode == 1) {		// оптика: быстрая + медленная составляющие
			s->gr += (target - s->gr) * (target > s->gr ? aAtk : aRel);
			s->slow += (target - s->slow) * (target > s->slow ? aSlowAtk : aSlowRel);
			if (s->gr < 1e-6f) s->gr = 0;
			if (s->slow < 1e-6f) s->slow = 0;
			g = s->gr > s->slow ? s->gr : s->slow;
		} else {
			s->gr += (target - s->gr) * (target > s->gr ? aAtk : aRel);
			if (s->gr < 1e-6f) s->gr = 0;
			g = s->gr;
		}
		const float lin = powf(10.0f, -g / 20.0f) * outGain;
		for (int c = 0; c < 2; c++) {
			float y = xs[c] * lin;
			if (harm) {
				y = Harmonics(y, harm, amount);
				s->dc[c] += (y - s->dc[c]) * aDc;
				y -= s->dc[c];
			}
			const float dry = in[c][k];
			out[c][k] = dry + (y - dry) * mix;
		}
	}
}

void Cleanup(void *h) { delete (Instance *)h; }

}	// namespace

extern "C" const Ladspa::Descriptor *ladspa_descriptor(unsigned long index)
{
	if (index != 0) return 0;
	static const char *names[P_COUNT] = {
		"Вход, дБ", "Порог, дБ", "Степень сжатия, :1", "Атака, мс", "Возврат, мс",
		"Режим: 0 Обычный, 1 Мягкий, 2 Nuke", "Фильтр детектора, Гц", "Гармоники: 0 нет, 1 чётные, 2 нечётные", "Гармоник, %",
		"Выход, дБ", "Параллельно (смесь), %",
		"Вход L", "Вход R", "Выход L", "Выход R"
	};
	static int desc[P_COUNT];
	static Ladspa::PortRangeHint hints[P_COUNT];
	static bool init = false;
	if (!init) {
		const int ctl = Ladspa::PORT_INPUT | Ladspa::PORT_CONTROL;
		const int B = Ladspa::HINT_BOUNDED_BELOW | Ladspa::HINT_BOUNDED_ABOVE;
		for (int i = 0; i < P_COUNT; i++) { desc[i] = ctl; hints[i] = { 0, 0, 0 }; }
		hints[P_INPUT]		= { B | Ladspa::HINT_DEFAULT_0, 0.0f, 30.0f };
		hints[P_THRESHOLD]	= { B | Ladspa::HINT_DEFAULT_HIGH, -60.0f, 0.0f };			// -15
		hints[P_RATIO]		= { B | Ladspa::HINT_LOGARITHMIC | Ladspa::HINT_DEFAULT_MIDDLE, 1.0f, 20.0f };	// около 4.5
		hints[P_ATTACK]		= { B | Ladspa::HINT_LOGARITHMIC | Ladspa::HINT_DEFAULT_MIDDLE, 0.05f, 100.0f };	// около 2.2
		hints[P_RELEASE]	= { B | Ladspa::HINT_LOGARITHMIC | Ladspa::HINT_DEFAULT_MIDDLE, 20.0f, 3000.0f };	// около 245
		hints[P_MODE]		= { B | Ladspa::HINT_INTEGER | Ladspa::HINT_DEFAULT_0, 0.0f, 2.0f };
		hints[P_DET_HP]		= { B | Ladspa::HINT_LOGARITHMIC | Ladspa::HINT_DEFAULT_MINIMUM, 20.0f, 400.0f };
		hints[P_HARM]		= { B | Ladspa::HINT_INTEGER | Ladspa::HINT_DEFAULT_0, 0.0f, 2.0f };
		hints[P_AMOUNT]		= { B | Ladspa::HINT_DEFAULT_MIDDLE, 0.0f, 100.0f };			// 50
		hints[P_OUTPUT]		= { B | Ladspa::HINT_DEFAULT_0, -30.0f, 30.0f };
		hints[P_MIX]		= { B | Ladspa::HINT_DEFAULT_MAXIMUM, 0.0f, 100.0f };			// 100
		desc[P_IN_L] = desc[P_IN_R] = Ladspa::PORT_INPUT | Ladspa::PORT_AUDIO;
		desc[P_OUT_L] = desc[P_OUT_R] = Ladspa::PORT_OUTPUT | Ladspa::PORT_AUDIO;
		init = true;
	}
	static Ladspa::Descriptor d = {
		915, "beae_distressor", 0, "Компрессор «Дистрессор»", "BeAE-Plus", "BSD",
		P_COUNT, desc, names, hints, 0,
		Instantiate, Connect, Activate, Run, 0, 0, 0, Cleanup
	};
	return &d;
}
