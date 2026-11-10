/*
 * Гейт / экспандер BeAE-Plus: плагин LADSPA, стерео.
 * Гейт глушит сигнал, пока он тише порога (дыхание и шорох между фразами вокала, «просачивание» в барабанах).
 * Экспандер делает то же мягче: чем тише сигнал под порогом, тем сильнее он ослабляется (степень 1:2 ... 1:10),
 * а «Глубина» ограничивает, насколько можно ослабить (до тишины или только на 15 дБ).
 *
 * Схема: детектор - пик с затуханием около 5 мс (каналы связаны: берётся более громкий), перед ним срез низких (чтобы гул
 * и «бубнёж» не открывали гейт); ослабление в дБ = (степень - 1) * (порог - уровень), с мягким коленом, не больше «Глубины»;
 * сглаживается атакой (открытие) и возвратом (закрытие), закрытие начинается не раньше, чем пройдёт «Удержание».
 * Задержки (latency) нет: первые миллисекунды после тишины идут с ослаблением, длиной в «Атаку».
 */
#include "../common/ladspa_abi.h"
#include <math.h>

namespace {

enum Port {
	P_THRESHOLD = 0, P_RATIO, P_RANGE, P_ATTACK, P_HOLD, P_RELEASE, P_KNEE, P_DET_HP,
	P_IN_L, P_IN_R, P_OUT_L, P_OUT_R, P_COUNT
};

inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct Instance {
	float rate;
	float env;				// пиковый детектор
	float att;				// текущее ослабление, дБ (>= 0)
	long  holdLeft;			// сколько кадров ещё держим открытым
	float hpS[2];			// ФНЧ для вычитания (ФВЧ детектора)
	float *port[P_COUNT];
};

void *Instantiate(const Ladspa::Descriptor *, unsigned long rate)
{
	Instance *s = new Instance;
	s->rate = rate > 0 ? (float)rate : 44100.0f;
	s->env = 0; s->att = 0; s->holdLeft = 0; s->hpS[0] = s->hpS[1] = 0;
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
	s->env = 0; s->att = 0; s->holdLeft = 0; s->hpS[0] = s->hpS[1] = 0;
}

// ослабление (дБ) при уровне levelDb. Выше порога - 0. Колено - участок под порогом шириной knee, где ослабление нарастает
// плавно (квадратично), дальше линейно; поэтому на самом пороге сигнал не ослабляется
inline float Attenuation(float levelDb, float thr, float ratio, float knee, float range)
{
	const float under = thr - levelDb, slope = ratio - 1.0f;
	float a;
	if (under <= 0.0f) a = 0.0f;
	else if (knee <= 0.0f) a = under * slope;
	else if (under < knee) a = slope * under * under / (2.0f * knee);
	else a = slope * (under - knee * 0.5f);
	return a > range ? range : a;
}

void Run(void *h, unsigned long n)
{
	Instance *s = (Instance *)h;
	for (int i = 0; i < P_COUNT; i++) if (!s->port[i]) return;
	const float thr = Clamp(*s->port[P_THRESHOLD], -80.0f, 0.0f);
	const float ratio = Clamp(*s->port[P_RATIO], 1.0f, 10.0f);
	const float range = Clamp(*s->port[P_RANGE], 0.0f, 80.0f);
	const float atkMs = Clamp(*s->port[P_ATTACK], 0.1f, 100.0f);
	const long holdN = (long)(Clamp(*s->port[P_HOLD], 0.0f, 500.0f) * 0.001f * s->rate);
	const float relMs = Clamp(*s->port[P_RELEASE], 5.0f, 2000.0f);
	const float knee = Clamp(*s->port[P_KNEE], 0.0f, 12.0f);
	const float detHp = Clamp(*s->port[P_DET_HP], 20.0f, 400.0f);
	const float aOpen = 1.0f - expf(-1.0f / (atkMs * 0.001f * s->rate));
	const float aClose = 1.0f - expf(-1.0f / (relMs * 0.001f * s->rate));
	const float decay = expf(-1.0f / (0.005f * s->rate));
	const float aHp = 1.0f - expf(-6.283185307f * detHp / s->rate);
	const float *in[2] = { s->port[P_IN_L], s->port[P_IN_R] };
	float *out[2] = { s->port[P_OUT_L], s->port[P_OUT_R] };
	for (unsigned long k = 0; k < n; k++) {
		const float x[2] = { in[0][k], in[1][k] };
		float pk = 0;
		for (int c = 0; c < 2; c++) {
			s->hpS[c] += (x[c] - s->hpS[c]) * aHp;
			if (fabsf(s->hpS[c]) < 1e-20f) s->hpS[c] = 0;
			const float d = fabsf(x[c] - s->hpS[c]);
			if (d > pk) pk = d;
		}
		s->env = pk > s->env ? pk : s->env * decay;
		if (s->env < 1e-20f) s->env = 0;
		const float levelDb = s->env > 1e-9f ? 20.0f * log10f(s->env) : -180.0f;
		float target = Attenuation(levelDb, thr, ratio, knee, range);
		// удержание: пока сигнал выше порога (ослабление 0), счётчик полон; при падении ниже порога ждём
		if (target <= 1e-4f) s->holdLeft = holdN;
		else if (s->holdLeft > 0) { s->holdLeft--; target = 0.0f; }
		s->att += (target - s->att) * (target < s->att ? aOpen : aClose);
		if (s->att < 1e-5f) s->att = 0;
		const float g = powf(10.0f, -s->att / 20.0f);
		out[0][k] = x[0] * g;
		out[1][k] = x[1] * g;
	}
}

void Cleanup(void *h) { delete (Instance *)h; }

}	// namespace

extern "C" const Ladspa::Descriptor *ladspa_descriptor(unsigned long index)
{
	if (index != 0) return 0;
	static const char *names[P_COUNT] = {
		"Порог, дБ", "Степень (1:N)", "Глубина, дБ", "Атака, мс", "Удержание, мс", "Возврат, мс", "Колено, дБ", "Фильтр детектора, Гц",
		"Вход L", "Вход R", "Выход L", "Выход R"
	};
	static int desc[P_COUNT];
	static Ladspa::PortRangeHint hints[P_COUNT];
	static bool init = false;
	if (!init) {
		const int ctl = Ladspa::PORT_INPUT | Ladspa::PORT_CONTROL;
		const int B = Ladspa::HINT_BOUNDED_BELOW | Ladspa::HINT_BOUNDED_ABOVE;
		for (int i = 0; i < P_COUNT; i++) { desc[i] = ctl; hints[i] = { 0, 0, 0 }; }
		hints[P_THRESHOLD]	= { B | Ladspa::HINT_DEFAULT_MIDDLE, -80.0f, 0.0f };			// -40
		hints[P_RATIO]		= { B | Ladspa::HINT_LOGARITHMIC | Ladspa::HINT_DEFAULT_MIDDLE, 1.0f, 10.0f };	// около 3,2
		hints[P_RANGE]		= { B | Ladspa::HINT_DEFAULT_MIDDLE, 0.0f, 80.0f };			// 40
		hints[P_ATTACK]		= { B | Ladspa::HINT_LOGARITHMIC | Ladspa::HINT_DEFAULT_MIDDLE, 0.1f, 100.0f };	// около 3
		hints[P_HOLD]		= { B | Ladspa::HINT_DEFAULT_LOW, 0.0f, 200.0f };				// 50
		hints[P_RELEASE]	= { B | Ladspa::HINT_LOGARITHMIC | Ladspa::HINT_DEFAULT_MIDDLE, 5.0f, 2000.0f };	// 100
		hints[P_KNEE]		= { B | Ladspa::HINT_DEFAULT_LOW, 0.0f, 12.0f };				// 3
		hints[P_DET_HP]		= { B | Ladspa::HINT_LOGARITHMIC | Ladspa::HINT_DEFAULT_MINIMUM, 20.0f, 400.0f };
		desc[P_IN_L] = desc[P_IN_R] = Ladspa::PORT_INPUT | Ladspa::PORT_AUDIO;
		desc[P_OUT_L] = desc[P_OUT_R] = Ladspa::PORT_OUTPUT | Ladspa::PORT_AUDIO;
		init = true;
	}
	static Ladspa::Descriptor d = {
		916, "beae_gate", 0, "Гейт / экспандер", "BeAE-Plus", "BSD",
		P_COUNT, desc, names, hints, 0,
		Instantiate, Connect, Activate, Run, 0, 0, 0, Cleanup
	};
	return &d;
}
