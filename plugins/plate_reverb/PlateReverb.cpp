/*
 * Пластинчатый ревербератор BeAE-Plus («Пластина EMT»): плагин LADSPA, стерео.
 * Воспроизводит характер EMT 140 (стальная пластина, 1957): плотный хвост без отдельных эхо, яркий звук,
 * время затухания от долей секунды до нескольких секунд, слабый бас, два датчика на выходе (стерео).
 *
 * Схема: вход (моно) -> срез низких -> предзадержка -> четыре диффузора (всепропускающие) -> «бак» из двух
 * перекрёстно связанных ветвей (модулируемый всепропускающий, линия задержки, демпфирование высоких,
 * ещё один всепропускающий и линия) -> выход со снятием сигнала с нескольких точек обеих ветвей.
 * Топология Джона Даттарро (1997); длины линий заданы для 29761 Гц и пересчитываются под частоту хоста.
 * Время затухания (RT60) выставляется через коэффициент обратной связи по суммарной задержке бака.
 *
 * Это модель «по описанию устройства», а не точная копия: параметры подбираются на слух.
 */
#include "../common/ladspa_abi.h"
#include "../common/plate_core.h"

namespace {

enum Port {
	P_DECAY = 0,	// 0.5..8 с (логарифмическая): время затухания RT60 на средних частотах
	P_PREDELAY,		// 0..200 мс
	P_DAMP,			// 0..1 затухание высоких в хвосте (0 - яркий, как EMT)
	P_LOWCUT,		// 20..500 Гц срез низких на входе (слабый бас пластины)
	P_DENSITY,		// 0..1 плотность (диффузия на входе)
	P_MOD,			// 0..1 лёгкая модуляция (убирает металлический звон)
	P_WIDTH,		// 0..1 ширина стерео
	P_MIX,			// 0..100 % посыл: сухой остаётся, к нему добавляется эффект
	P_IN_L, P_IN_R, P_OUT_L, P_OUT_R,
	P_COUNT
};

const float kMaxPredelayMs = PlateCore::kMaxPredelayMs;

inline float Clamp(float v, float lo, float hi) { return PlateCore::Clamp(v, lo, hi); }

struct Instance {
	PlateCore::Core core;
	std::vector<float> wl, wr;
	float *port[P_COUNT];
};

void *Instantiate(const Ladspa::Descriptor *, unsigned long rate)
{
	Instance *s = new Instance;
	s->core.Init(rate > 0 ? (float)rate : 44100.0f);
	for (int i = 0; i < P_COUNT; i++) s->port[i] = 0;
	return s;
}

void Connect(void *h, unsigned long p, float *loc)
{
	Instance *s = (Instance *)h;
	if (p < P_COUNT) s->port[p] = loc;
}

void Activate(void *h) { ((Instance *)h)->core.Clear(); }

void Run(void *h, unsigned long n)
{
	Instance *s = (Instance *)h;
	for (int i = 0; i < P_COUNT; i++) if (!s->port[i]) return;
	if (s->wl.size() < n) { s->wl.resize(n); s->wr.resize(n); }
	PlateCore::Params p;
	p.decay = *s->port[P_DECAY]; p.predelayMs = *s->port[P_PREDELAY]; p.damp = *s->port[P_DAMP];
	p.lowcut = *s->port[P_LOWCUT]; p.density = *s->port[P_DENSITY]; p.mod = *s->port[P_MOD]; p.width = *s->port[P_WIDTH];
	const float mix = Clamp(*s->port[P_MIX], 0.0f, 100.0f) * 0.01f;
	const float *inL = s->port[P_IN_L], *inR = s->port[P_IN_R];
	float *outL = s->port[P_OUT_L], *outR = s->port[P_OUT_R];
	// вход и выход могут совпадать (обработка «на месте»): хвост считаем во временные буферы
	s->core.Process(inL, inR, &s->wl[0], &s->wr[0], n, p);
	for (unsigned long k = 0; k < n; k++) {
		const float dl = inL[k], dr = inR[k];
		outL[k] = dl + s->wl[k] * mix;
		outR[k] = dr + s->wr[k] * mix;
	}
}

void Cleanup(void *h) { delete (Instance *)h; }

}	// namespace

extern "C" const Ladspa::Descriptor *ladspa_descriptor(unsigned long index)
{
	if (index != 0) return 0;
	static const char *names[P_COUNT] = {
		"Время затухания, с", "Предзадержка, мс", "Затухание высоких", "Срез низких, Гц", "Плотность", "Модуляция", "Ширина", "Смесь, %",
		"Вход L", "Вход R", "Выход L", "Выход R"
	};
	static int desc[P_COUNT];
	static Ladspa::PortRangeHint hints[P_COUNT];
	static bool init = false;
	if (!init) {
		const int ctl = Ladspa::PORT_INPUT | Ladspa::PORT_CONTROL;
		const int B = Ladspa::HINT_BOUNDED_BELOW | Ladspa::HINT_BOUNDED_ABOVE;
		for (int i = 0; i < P_COUNT; i++) { desc[i] = ctl; hints[i] = { 0, 0, 0 }; }
		hints[P_DECAY]		= { B | Ladspa::HINT_LOGARITHMIC | Ladspa::HINT_DEFAULT_MIDDLE, 0.5f, 8.0f };	// 2 с
		hints[P_PREDELAY]	= { B | Ladspa::HINT_DEFAULT_0, 0.0f, kMaxPredelayMs };
		hints[P_DAMP]		= { B | Ladspa::HINT_DEFAULT_LOW, 0.0f, 1.0f };			// 0.25
		hints[P_LOWCUT]		= { B | Ladspa::HINT_LOGARITHMIC | Ladspa::HINT_DEFAULT_100, 20.0f, 500.0f };
		hints[P_DENSITY]	= { B | Ladspa::HINT_DEFAULT_MAXIMUM, 0.0f, 1.0f };
		hints[P_MOD]		= { B | Ladspa::HINT_DEFAULT_MIDDLE, 0.0f, 1.0f };
		hints[P_WIDTH]		= { B | Ladspa::HINT_DEFAULT_MAXIMUM, 0.0f, 1.0f };
		hints[P_MIX]		= { B | Ladspa::HINT_DEFAULT_LOW, 0.0f, 100.0f };			// 25
		desc[P_IN_L] = desc[P_IN_R] = Ladspa::PORT_INPUT | Ladspa::PORT_AUDIO;
		desc[P_OUT_L] = desc[P_OUT_R] = Ladspa::PORT_OUTPUT | Ladspa::PORT_AUDIO;
		init = true;
	}
	static Ladspa::Descriptor d = {
		909, "beae_plate_reverb", 0, "Пластинчатый реверб (EMT 140)", "BeAE-Plus", "BSD",
		P_COUNT, desc, names, hints, 0,
		Instantiate, Connect, Activate, Run, 0, 0, 0, Cleanup
	};
	return &d;
}
