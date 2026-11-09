/*
 * Оптический ламповый компрессор в духе Teletronix LA-2A («Оптика LA-2A»): плагин LADSPA, стерео.
 * Это модель «по описанию прибора», а не точная копия.
 *
 * Характер LA-2A:
 *  - один главный регулятор «Сжатие» (Peak Reduction) - чем больше, тем ниже порог; ручка «Усиление» компенсирует громкость;
 *  - очень мягкое колено и небольшая степень сжатия (около 3:1 в режиме Compress, около 10:1 в режиме Limit);
 *  - быстрая атака (около 10 мс), а возврат зависит от программы: сначала быстрый (около 60 мс), затем медленный
 *    (около 1,5 с), причём медленная часть набирается, только если сжатие длится долго - как у оптической ячейки;
 *  - лёгкая ламповая сатурация (чётные гармоники).
 *
 * Детектор: среднеквадратичный уровень (окно 5 мс), шкала - пик синуса (0 дБ = полная шкала). Каналы связаны
 * (берётся более громкий). Выход = смесь сухого и сжатого сигналов («Параллельно»): 100 % - обычная работа.
 */
#include "../common/ladspa_abi.h"
#include <math.h>

namespace {

enum Port { P_REDUCTION = 0, P_GAIN, P_MODE, P_TUBE, P_MIX, P_IN_L, P_IN_R, P_OUT_L, P_OUT_R, P_COUNT };

const float kKneeDb = 10.0f;
const float kMaxThresholdDb = 40.0f;

inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct Instance {
	float rate;
	float ms[2];				// скользящее среднее квадрата
	float fast, slow;			// быстрая и медленная составляющие сжатия, дБ
	float hp[2];				// убираем постоянную составляющую после лампы
	float dc[2];
	float *port[P_COUNT];
};

void *Instantiate(const Ladspa::Descriptor *, unsigned long rate)
{
	Instance *s = new Instance;
	s->rate = rate > 0 ? (float)rate : 44100.0f;
	s->ms[0] = s->ms[1] = s->fast = s->slow = 0;
	s->hp[0] = s->hp[1] = s->dc[0] = s->dc[1] = 0;
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
	s->ms[0] = s->ms[1] = s->fast = s->slow = 0;
	s->hp[0] = s->hp[1] = s->dc[0] = s->dc[1] = 0;
}

// статическая кривая: сжатие (дБ) при уровне levelDb, пороге thr, степени ratio и мягком колене
inline float StaticReduction(float levelDb, float thr, float ratio)
{
	const float over = levelDb - thr, slope = 1.0f - 1.0f / ratio;
	if (over <= -kKneeDb * 0.5f) return 0.0f;
	if (over >= kKneeDb * 0.5f) return slope * over;
	const float t = over + kKneeDb * 0.5f;
	return slope * t * t / (2.0f * kKneeDb);
}

// лампа: мягкий предел с небольшим смещением (чётные гармоники); при малом сигнале усиление 1
inline float Tube(float x, float amt)
{
	if (amt <= 0.0f) return x;
	const float k = 1.0f + 1.5f * amt, b = 0.20f * amt;
	const float tb = tanhf(k * b);
	return (tanhf(k * (x + b)) - tb) / (k * (1.0f - tb * tb));
}

void Run(void *h, unsigned long n)
{
	Instance *s = (Instance *)h;
	for (int i = 0; i < P_COUNT; i++) if (!s->port[i]) return;
	const float red = Clamp(*s->port[P_REDUCTION], 0.0f, 100.0f) * 0.01f;
	const float thr = -kMaxThresholdDb * red;
	const float makeup = powf(10.0f, Clamp(*s->port[P_GAIN], -20.0f, 40.0f) / 20.0f);
	const float ratio = *s->port[P_MODE] >= 0.5f ? 10.0f : 3.0f;
	const float tube = Clamp(*s->port[P_TUBE], 0.0f, 100.0f) * 0.01f;
	const float mix = Clamp(*s->port[P_MIX], 0.0f, 100.0f) * 0.01f;
	const float aMs = 1.0f - expf(-1.0f / (0.005f * s->rate));
	const float aAtk = 1.0f - expf(-1.0f / (0.010f * s->rate));
	const float aFastRel = 1.0f - expf(-1.0f / (0.060f * s->rate));
	const float aSlowAtk = 1.0f - expf(-1.0f / (0.8f * s->rate));
	const float aSlowRel = 1.0f - expf(-1.0f / (1.5f * s->rate));
	const float aDc = 1.0f - expf(-6.283185307f * 15.0f / s->rate);
	const float *in[2] = { s->port[P_IN_L], s->port[P_IN_R] };
	float *out[2] = { s->port[P_OUT_L], s->port[P_OUT_R] };
	for (unsigned long k = 0; k < n; k++) {
		float xs[2] = { in[0][k], in[1][k] };
		float p = 0;
		for (int c = 0; c < 2; c++) {
			s->ms[c] += (xs[c] * xs[c] - s->ms[c]) * aMs;
			if (s->ms[c] < 1e-20f) s->ms[c] = 0;
			if (s->ms[c] > p) p = s->ms[c];
		}
		const float levelDb = p > 1e-12f ? 10.0f * log10f(p) + 3.0103f : -120.0f;
		const float target = StaticReduction(levelDb, thr, ratio);
		s->fast += (target - s->fast) * (target > s->fast ? aAtk : aFastRel);
		s->slow += (target - s->slow) * (target > s->slow ? aSlowAtk : aSlowRel);
		if (s->fast < 1e-6f) s->fast = 0;
		if (s->slow < 1e-6f) s->slow = 0;
		const float gr = s->fast > s->slow ? s->fast : s->slow;
		const float g = powf(10.0f, -gr / 20.0f) * makeup;
		for (int c = 0; c < 2; c++) {
			float y = xs[c] * g;
			if (tube > 0.0f) {
				y = Tube(y, tube);
				s->dc[c] += (y - s->dc[c]) * aDc;		// ФВЧ 15 Гц убирает смещение от асимметрии
				y -= s->dc[c];
			}
			out[c][k] = xs[c] + (y - xs[c]) * mix;
		}
	}
}

void Cleanup(void *h) { delete (Instance *)h; }

}	// namespace

extern "C" const Ladspa::Descriptor *ladspa_descriptor(unsigned long index)
{
	if (index != 0) return 0;
	static const char *names[P_COUNT] = {
		"Сжатие (Peak Reduction)", "Усиление, дБ", "Режим: 0 Compress, 1 Limit", "Лампа, %", "Параллельно (смесь), %",
		"Вход L", "Вход R", "Выход L", "Выход R"
	};
	static int desc[P_COUNT];
	static Ladspa::PortRangeHint hints[P_COUNT];
	static bool init = false;
	if (!init) {
		const int ctl = Ladspa::PORT_INPUT | Ladspa::PORT_CONTROL;
		const int B = Ladspa::HINT_BOUNDED_BELOW | Ladspa::HINT_BOUNDED_ABOVE;
		for (int i = 0; i < P_COUNT; i++) { desc[i] = ctl; hints[i] = { 0, 0, 0 }; }
		hints[P_REDUCTION]	= { B | Ladspa::HINT_DEFAULT_MIDDLE, 0.0f, 100.0f };			// 50
		hints[P_GAIN]		= { B | Ladspa::HINT_DEFAULT_0, -20.0f, 40.0f };
		hints[P_MODE]		= { B | Ladspa::HINT_INTEGER | Ladspa::HINT_DEFAULT_0, 0.0f, 1.0f };
		hints[P_TUBE]		= { B | Ladspa::HINT_DEFAULT_LOW, 0.0f, 100.0f };				// 25
		hints[P_MIX]		= { B | Ladspa::HINT_DEFAULT_MAXIMUM, 0.0f, 100.0f };			// 100
		desc[P_IN_L] = desc[P_IN_R] = Ladspa::PORT_INPUT | Ladspa::PORT_AUDIO;
		desc[P_OUT_L] = desc[P_OUT_R] = Ladspa::PORT_OUTPUT | Ladspa::PORT_AUDIO;
		init = true;
	}
	static Ladspa::Descriptor d = {
		914, "beae_la2a", 0, "Оптический компрессор (LA-2A)", "BeAE-Plus", "BSD",
		P_COUNT, desc, names, hints, 0,
		Instantiate, Connect, Activate, Run, 0, 0, 0, Cleanup
	};
	return &d;
}
