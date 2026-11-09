/*
 * Вокальная цепочка 80-х (слэп + пластина) BeAE-Plus: плагин LADSPA, стерео.
 * Классическая «поп-вокальная» связка студий 80-х: очень короткая задержка (слэп-бэк, «дабл-трек»),
 * подмешанная почти незаметно, и пластинчатый реверб с предзадержкой и завалом высоких в хвосте.
 *
 * Сигнал: вход -> слэп (стерео линия задержки с малой обратной связью) -> к сухому добавляется слэп;
 * реверб питается сухим + слэпом, его хвост проходит через завал высоких (два ФНЧ по 6 дБ/окт) и добавляется
 * с уровнем «Реверб, %». Реверб - общее ядро с плагином «Пластинчатый реверб» (common/plate_core.h).
 *
 * Значения по умолчанию взяты из типовой рекомендации: слэп 30 мс, обратная связь 5 %, уровень 12 %;
 * реверб 1,8 с, предзадержка 32 мс, завал высоких 4,5 кГц, уровень 25 %.
 */
#include "../common/ladspa_abi.h"
#include "../common/plate_core.h"

namespace {

enum Port {
	P_SLAP_TIME = 0,	// 10..80 мс
	P_SLAP_FB,			// 0..30 % обратная связь слэпа
	P_SLAP_MIX,			// 0..50 % уровень слэпа
	P_DECAY,			// 0.5..6 с время затухания реверба
	P_PREDELAY,			// 0..100 мс предзадержка реверба
	P_HIDROP,			// 2000..16000 Гц завал высоких в хвосте
	P_REV_MIX,			// 0..100 % уровень реверба
	P_IN_L, P_IN_R, P_OUT_L, P_OUT_R,
	P_COUNT
};

const float kMaxSlapMs = 80.0f;

inline float Clamp(float v, float lo, float hi) { return PlateCore::Clamp(v, lo, hi); }

struct Instance {
	float rate;
	PlateCore::Core core;
	PlateCore::Delay dl, dr;		// линии слэпа
	float dSmooth;					// сглаженная задержка, кадры
	bool dInit;
	float lp1L, lp1R, lp2L, lp2R;	// завал высоких
	std::vector<float> sl, sr, wl, wr;
	float *port[P_COUNT];
};

void *Instantiate(const Ladspa::Descriptor *, unsigned long rate)
{
	Instance *s = new Instance;
	s->rate = rate > 0 ? (float)rate : 44100.0f;
	s->core.Init(s->rate);
	const size_t n = (size_t)(kMaxSlapMs * 0.001f * s->rate) + 8;
	s->dl.Init(n); s->dr.Init(n);
	s->dSmooth = 0; s->dInit = false;
	s->lp1L = s->lp1R = s->lp2L = s->lp2R = 0;
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
	s->core.Clear(); s->dl.Clear(); s->dr.Clear();
	s->dInit = false;
	s->lp1L = s->lp1R = s->lp2L = s->lp2R = 0;
}

void Run(void *h, unsigned long n)
{
	Instance *s = (Instance *)h;
	for (int i = 0; i < P_COUNT; i++) if (!s->port[i]) return;
	if (s->sl.size() < n) { s->sl.resize(n); s->sr.resize(n); s->wl.resize(n); s->wr.resize(n); }

	const float *inL = s->port[P_IN_L], *inR = s->port[P_IN_R];
	float *outL = s->port[P_OUT_L], *outR = s->port[P_OUT_R];
	const float slapMs = Clamp(*s->port[P_SLAP_TIME], 10.0f, kMaxSlapMs);
	const float fb = Clamp(*s->port[P_SLAP_FB], 0.0f, 30.0f) * 0.01f;
	const float slapMix = Clamp(*s->port[P_SLAP_MIX], 0.0f, 50.0f) * 0.01f;
	const float revMix = Clamp(*s->port[P_REV_MIX], 0.0f, 100.0f) * 0.01f;
	const float target = slapMs * 0.001f * s->rate;
	if (!s->dInit) { s->dSmooth = target; s->dInit = true; }
	const float sm = 1.0f - expf(-1.0f / (0.02f * s->rate));	// сглаживание времени ~20 мс (без щелчков)

	// 1) слэп: сухой сигнал копируем во временные буферы (вход и выход могут совпадать)
	std::vector<float> &sl = s->sl, &sr = s->sr;
	std::vector<float> dryL(inL, inL + n), dryR(inR, inR + n);
	for (unsigned long k = 0; k < n; k++) {
		s->dSmooth += (target - s->dSmooth) * sm;
		const float d = s->dSmooth < 1.0f ? 1.0f : s->dSmooth;
		const float eL = s->dl.ReadFrac(d), eR = s->dr.ReadFrac(d);
		s->dl.Write(dryL[k] + fb * eL);
		s->dr.Write(dryR[k] + fb * eR);
		sl[k] = eL * slapMix; sr[k] = eR * slapMix;
	}
	// 2) реверб питается сухим + слэпом
	for (unsigned long k = 0; k < n; k++) { s->wl[k] = dryL[k] + sl[k]; s->wr[k] = dryR[k] + sr[k]; }
	PlateCore::Params p;
	p.decay = *s->port[P_DECAY]; p.predelayMs = *s->port[P_PREDELAY];
	p.damp = 0.25f; p.lowcut = 120.0f; p.density = 1.0f; p.mod = 0.5f; p.width = 1.0f;
	std::vector<float> rl(n), rr(n);
	s->core.Process(&s->wl[0], &s->wr[0], &rl[0], &rr[0], n, p);
	// 3) завал высоких в хвосте: два однополюсных ФНЧ
	const float fc = Clamp(*s->port[P_HIDROP], 2000.0f, 16000.0f);
	const float a = 1.0f - expf(-6.283185307f * fc / s->rate);
	for (unsigned long k = 0; k < n; k++) {
		s->lp1L += (rl[k] - s->lp1L) * a; s->lp2L += (s->lp1L - s->lp2L) * a;
		s->lp1R += (rr[k] - s->lp1R) * a; s->lp2R += (s->lp1R - s->lp2R) * a;
		PlateCore::Flush(s->lp1L); PlateCore::Flush(s->lp2L); PlateCore::Flush(s->lp1R); PlateCore::Flush(s->lp2R);
		outL[k] = dryL[k] + sl[k] + s->lp2L * revMix;
		outR[k] = dryR[k] + sr[k] + s->lp2R * revMix;
	}
}

void Cleanup(void *h) { delete (Instance *)h; }

}	// namespace

extern "C" const Ladspa::Descriptor *ladspa_descriptor(unsigned long index)
{
	if (index != 0) return 0;
	static const char *names[P_COUNT] = {
		"Слэп: время, мс", "Слэп: повторы, %", "Слэп: уровень, %",
		"Реверб: затухание, с", "Реверб: предзадержка, мс", "Реверб: завал высоких, Гц", "Реверб: уровень, %",
		"Вход L", "Вход R", "Выход L", "Выход R"
	};
	static int desc[P_COUNT];
	static Ladspa::PortRangeHint hints[P_COUNT];
	static bool init = false;
	if (!init) {
		const int ctl = Ladspa::PORT_INPUT | Ladspa::PORT_CONTROL;
		const int B = Ladspa::HINT_BOUNDED_BELOW | Ladspa::HINT_BOUNDED_ABOVE;
		for (int i = 0; i < P_COUNT; i++) { desc[i] = ctl; hints[i] = { 0, 0, 0 }; }
		hints[P_SLAP_TIME]	= { B | Ladspa::HINT_DEFAULT_LOW, 10.0f, kMaxSlapMs };			// ~30
		hints[P_SLAP_FB]	= { B | Ladspa::HINT_DEFAULT_LOW, 0.0f, 30.0f };				// ~7
		hints[P_SLAP_MIX]	= { B | Ladspa::HINT_DEFAULT_LOW, 0.0f, 50.0f };				// ~12
		hints[P_DECAY]		= { B | Ladspa::HINT_LOGARITHMIC | Ladspa::HINT_DEFAULT_MIDDLE, 0.5f, 6.0f };	// ~1.7
		hints[P_PREDELAY]	= { B | Ladspa::HINT_DEFAULT_LOW, 0.0f, 100.0f };				// ~25
		hints[P_HIDROP]		= { B | Ladspa::HINT_LOGARITHMIC | Ladspa::HINT_DEFAULT_MIDDLE, 2000.0f, 16000.0f };	// ~5600
		hints[P_REV_MIX]	= { B | Ladspa::HINT_DEFAULT_LOW, 0.0f, 100.0f };				// 25
		desc[P_IN_L] = desc[P_IN_R] = Ladspa::PORT_INPUT | Ladspa::PORT_AUDIO;
		desc[P_OUT_L] = desc[P_OUT_R] = Ladspa::PORT_OUTPUT | Ladspa::PORT_AUDIO;
		init = true;
	}
	static Ladspa::Descriptor d = {
		912, "beae_vocal_slap_plate", 0, "Вокальная цепочка 80-х (слэп + пластина)", "BeAE-Plus", "BSD",
		P_COUNT, desc, names, hints, 0,
		Instantiate, Connect, Activate, Run, 0, 0, 0, Cleanup
	};
	return &d;
}
