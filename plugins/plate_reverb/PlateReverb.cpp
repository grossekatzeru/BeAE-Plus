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
#include <math.h>
#include <string.h>
#include <algorithm>
#include <vector>

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

const float kRefRate = 29761.0f;
const float kMaxPredelayMs = 200.0f;
const float kWetScale = 1.0f;			// подобрано тестом: уровень хвоста при смеси 100 % близок к уровню входа

inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct Delay {
	std::vector<float> buf; size_t w;
	void Init(size_t n) { buf.assign(n + 2, 0.0f); w = 0; }
	void Clear() { std::fill(buf.begin(), buf.end(), 0.0f); }
	inline void Write(float v) { buf[w] = v; if (++w >= buf.size()) w = 0; }
	// задержка d кадров (целая) от последней записи: Read(1) - только что записанное
	inline float Read(size_t d) const { size_t i = w + buf.size() - d; if (i >= buf.size()) i -= buf.size(); return buf[i]; }
	inline float ReadFrac(float d) const
	{
		const size_t id = (size_t)d;
		const float fr = d - (float)id;
		return Read(id) * (1.0f - fr) + Read(id + 1) * fr;
	}
};

struct Allpass {
	Delay d; size_t len;
	void Init(size_t n, size_t pad) { len = n < 2 ? 2 : n; d.Init(len + pad); }		// pad - запас под модуляцию длины
	// схема: w = x + g*dly; y = dly - g*w; dly - выход линии на глубине len
	inline float Run(float x, float g)
	{
		const float dly = d.Read(len);
		const float w = x + g * dly;
		d.Write(w);
		return dly - g * w;
	}
	inline float RunMod(float x, float g, float mod)		// длина len + mod (дробная)
	{
		const float dly = d.ReadFrac((float)len + mod);
		const float w = x + g * dly;
		d.Write(w);
		return dly - g * w;
	}
};

struct Instance {
	float rate, k;					// k = rate / kRefRate
	Delay pre;
	Allpass in[4];
	Allpass la1, la2, ra1, ra2;
	Delay ld1, ld2, rd1, rd2;
	size_t lenLd1, lenLd2, lenRd1, lenRd2;
	size_t tapL[7], tapR[7];
	float lpL, lpR;					// состояние демпфирования
	float hpState;					// срез низких на входе
	float inLp;						// ограничение полосы входа
	double lfoPhaseL, lfoPhaseR;
	float lastL, lastR;				// выходы ветвей (перекрёстная обратная связь)
	float *port[P_COUNT];
	float maxMod;
};

size_t Scaled(int n, float k) { size_t v = (size_t)(n * k + 0.5f); return v < 2 ? 2 : v; }

void *Instantiate(const Ladspa::Descriptor *, unsigned long rate)
{
	Instance *s = new Instance;
	s->rate = rate > 0 ? (float)rate : 44100.0f;
	s->k = s->rate / kRefRate;
	const float k = s->k;
	s->pre.Init((size_t)(kMaxPredelayMs * 0.001f * s->rate) + 4);
	const int inLen[4] = { 142, 107, 379, 277 };
	const size_t pad = (size_t)(12.0f * k) + 8;
	for (int i = 0; i < 4; i++) s->in[i].Init(Scaled(inLen[i], k), 2);
	s->la1.Init(Scaled(672, k), pad); s->ra1.Init(Scaled(908, k), pad);
	s->la2.Init(Scaled(1800, k), 2); s->ra2.Init(Scaled(2656, k), 2);
	s->lenLd1 = Scaled(4453, k); s->lenLd2 = Scaled(3720, k);
	s->lenRd1 = Scaled(4217, k); s->lenRd2 = Scaled(3163, k);
	s->ld1.Init(s->lenLd1); s->ld2.Init(s->lenLd2); s->rd1.Init(s->lenRd1); s->rd2.Init(s->lenRd2);
	s->maxMod = 12.0f * k;
	for (int i = 0; i < 7; i++) s->tapL[i] = s->tapR[i] = 1;
	// отводы для левого и правого выхода (позиции из схемы Даттарро), не больше длины линии
	const int tl[7] = { 266, 2974, 1913, 1996, 1990, 187, 1066 };
	const int tr[7] = { 353, 3627, 1228, 2673, 2111, 335, 121 };
	for (int i = 0; i < 7; i++) { s->tapL[i] = Scaled(tl[i], k); s->tapR[i] = Scaled(tr[i], k); }
	s->lpL = s->lpR = s->hpState = s->inLp = 0;
	s->lfoPhaseL = 0; s->lfoPhaseR = 0.37;
	s->lastL = s->lastR = 0;
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
	s->pre.Clear();
	for (int i = 0; i < 4; i++) s->in[i].d.Clear();
	s->la1.d.Clear(); s->la2.d.Clear(); s->ra1.d.Clear(); s->ra2.d.Clear();
	s->ld1.Clear(); s->ld2.Clear(); s->rd1.Clear(); s->rd2.Clear();
	s->lpL = s->lpR = s->hpState = s->inLp = 0;
	s->lastL = s->lastR = 0;
}

inline void Flush(float &v) { if (fabsf(v) < 1e-20f) v = 0; }

void Run(void *h, unsigned long n)
{
	Instance *s = (Instance *)h;
	for (int i = 0; i < P_COUNT; i++) if (!s->port[i]) return;
	const float rt = Clamp(*s->port[P_DECAY], 0.3f, 12.0f);
	const float damp = Clamp(*s->port[P_DAMP], 0.0f, 1.0f) * 0.85f;
	const float density = Clamp(*s->port[P_DENSITY], 0.0f, 1.0f);
	const float mod = Clamp(*s->port[P_MOD], 0.0f, 1.0f) * s->maxMod;
	const float width = Clamp(*s->port[P_WIDTH], 0.0f, 1.0f);
	const float mix = Clamp(*s->port[P_MIX], 0.0f, 100.0f) * 0.01f;
	const float lowcut = Clamp(*s->port[P_LOWCUT], 20.0f, 500.0f);
	size_t pdelay = (size_t)(Clamp(*s->port[P_PREDELAY], 0.0f, kMaxPredelayMs) * 0.001f * s->rate);
	if (pdelay + 2 >= s->pre.buf.size()) pdelay = s->pre.buf.size() - 3;

	// коэффициент спада на один проход ветви: полный круг «восьмёрки» = две ветви
	const float loopSec = (float)(s->la1.len + s->lenLd1 + s->la2.len + s->lenLd2
		+ s->ra1.len + s->lenRd1 + s->ra2.len + s->lenRd2) / s->rate;
	float decay = powf(10.0f, -3.0f * (loopSec * 0.5f) / rt);
	decay = Clamp(decay, 0.0f, 0.9995f);
	const float dd1 = 0.7f;
	const float dd2 = Clamp(decay + 0.15f, 0.25f, 0.5f);
	const float id1 = 0.75f * density, id2 = 0.625f * density;
	const float hpA = expf(-6.283185307f * lowcut / s->rate);		// однополюсный фильтр верхних частот
	const double inc1 = 1.0 / (s->rate * 1.0), inc2 = 0.83 / s->rate;		// LFO около 1 и 0.83 Гц
	const float *inL = s->port[P_IN_L], *inR = s->port[P_IN_R];
	float *outL = s->port[P_OUT_L], *outR = s->port[P_OUT_R];
	const float w1 = width * 0.5f + 0.5f, w2 = (1.0f - width) * 0.5f;

	for (unsigned long k = 0; k < n; k++) {
		const float dl = inL[k], dr = inR[k];
		// вход: моно, срез низких, лёгкое ограничение верхних
		float x = (dl + dr) * 0.5f;
		s->hpState = x * (1.0f - hpA) + s->hpState * hpA;			// ФНЧ -> вычитаем получаем ФВЧ
		x -= s->hpState;
		s->inLp = x * 0.7f + s->inLp * 0.3f;
		Flush(s->hpState); Flush(s->inLp);
		x = s->inLp;
		// предзадержка
		s->pre.Write(x);
		x = s->pre.Read(pdelay + 1);
		// диффузоры
		x = s->in[0].Run(x, id1);
		x = s->in[1].Run(x, id1);
		x = s->in[2].Run(x, id2);
		x = s->in[3].Run(x, id2);

		// «бак»: две ветви, перекрёстная обратная связь
		s->lfoPhaseL += inc1; if (s->lfoPhaseL >= 1.0) s->lfoPhaseL -= 1.0;
		s->lfoPhaseR += inc2; if (s->lfoPhaseR >= 1.0) s->lfoPhaseR -= 1.0;
		const float mL = mod * sinf((float)(6.283185307179586 * s->lfoPhaseL));
		const float mR = mod * sinf((float)(6.283185307179586 * s->lfoPhaseR));

		float l = x + s->lastR;
		l = s->la1.RunMod(l, dd1, mL);
		s->ld1.Write(l);
		float ld1o = s->ld1.Read(s->lenLd1);
		s->lpL = ld1o * (1.0f - damp) + s->lpL * damp; Flush(s->lpL);
		l = s->lpL * decay;
		l = s->la2.Run(l, dd2);
		s->ld2.Write(l);
		s->lastL = s->ld2.Read(s->lenLd2);

		float r = x + s->lastL;
		r = s->ra1.RunMod(r, dd1, mR);
		s->rd1.Write(r);
		float rd1o = s->rd1.Read(s->lenRd1);
		s->lpR = rd1o * (1.0f - damp) + s->lpR * damp; Flush(s->lpR);
		r = s->lpR * decay;
		r = s->ra2.Run(r, dd2);
		s->rd2.Write(r);
		s->lastR = s->rd2.Read(s->lenRd2);

		// выход: отводы с обеих ветвей (стереопара «датчиков»)
		const float yl = s->rd1.Read(s->tapL[0]) + s->rd1.Read(s->tapL[1]) - s->ra2.d.Read(s->tapL[2])
			+ s->rd2.Read(s->tapL[3]) - s->ld1.Read(s->tapL[4]) - s->la2.d.Read(s->tapL[5]) - s->ld2.Read(s->tapL[6]);
		const float yr = s->ld1.Read(s->tapR[0]) + s->ld1.Read(s->tapR[1]) - s->la2.d.Read(s->tapR[2])
			+ s->ld2.Read(s->tapR[3]) - s->rd1.Read(s->tapR[4]) - s->ra2.d.Read(s->tapR[5]) - s->rd2.Read(s->tapR[6]);
		const float wl = yl * w1 + yr * w2, wr = yr * w1 + yl * w2;
		outL[k] = dl + wl * 0.6f * kWetScale * mix;
		outR[k] = dr + wr * 0.6f * kWetScale * mix;
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
