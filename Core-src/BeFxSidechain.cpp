#include "BeFxSidechain.h"
#include "Ducker.h"
#include "KeyCache.h"
#include <math.h>

namespace BeFx {

const char *kSidechainLabel = "beae_befx_sidechain";

namespace {

inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct Instance {
	float rate;
	Ducker duck;
	uint64_t ctxId;				// номер вызова Key::Scope, для которого считаем offset
	double offset;				// кадров с начала вызова
	float *port[SC_COUNT];
};

void *Instantiate(const Ladspa::Descriptor *, unsigned long rate)
{
	Instance *s = new Instance;
	s->rate = rate > 0 ? (float)rate : 44100.0f;
	s->duck.Init(s->rate);
	s->ctxId = 0; s->offset = 0;
	for (int i = 0; i < SC_COUNT; i++) s->port[i] = 0;
	return s;
}

void Connect(void *h, unsigned long p, float *loc)
{
	Instance *s = (Instance *)h;
	if (p < SC_COUNT) s->port[p] = loc;
}

void Activate(void *h)
{
	Instance *s = (Instance *)h;
	s->duck.Reset();
	s->ctxId = 0; s->offset = 0;
}

void Run(void *h, unsigned long n)
{
	Instance *s = (Instance *)h;
	for (int i = 0; i < SC_COUNT; i++) if (!s->port[i]) return;
	const int keyPort = (int)(Clamp(*s->port[SC_KEY], 0.0f, 64.0f) + 0.5f);
	const int range = *s->port[SC_RANGE] >= 0.5f ? Key::RANGE_LOW : Key::RANGE_ALL;
	const float cutoff = Clamp(*s->port[SC_CUTOFF], 40.0f, 1000.0f);
	DuckParams p;
	p.thresholdDb = Clamp(*s->port[SC_THRESHOLD], -60.0f, 0.0f);
	p.depthDb = Clamp(*s->port[SC_DEPTH], 0.0f, 40.0f);
	p.attackMs = Clamp(*s->port[SC_ATTACK], 0.1f, 100.0f);
	p.holdMs = Clamp(*s->port[SC_HOLD], 0.0f, 500.0f);
	p.releaseMs = Clamp(*s->port[SC_RELEASE], 5.0f, 2000.0f);
	p.kneeDb = Clamp(*s->port[SC_KNEE], 0.0f, 24.0f);
	p.linearRelease = *s->port[SC_CURVE] >= 0.5f;
	s->duck.Set(p);
	const float mix = Clamp(*s->port[SC_MIX], 0.0f, 100.0f) * 0.01f;
	const float outGain = powf(10.0f, Clamp(*s->port[SC_OUTPUT], -24.0f, 12.0f) / 20.0f);

	const Key::Context *ctx = Key::CurrentContext();
	std::shared_ptr<const Key::Envelope> env;
	if (ctx && keyPort > 0) env = ctx->cache->Get(keyPort - 1, range, cutoff);
	double t = 0, step = 0;
	if (ctx) {
		if (ctx->id != s->ctxId) { s->ctxId = ctx->id; s->offset = 0; }
		t = ctx->t0 + s->offset * ctx->step;
		step = ctx->step;
	}
	const float *inL = s->port[SC_IN_L], *inR = s->port[SC_IN_R];
	float *outL = s->port[SC_OUT_L], *outR = s->port[SC_OUT_R];
	for (unsigned long k = 0; k < n; k++) {
		const float key = env ? env->At(t) : 0.0f;
		t += step;
		const float g = s->duck.Next(key);
		const float m = (1.0f + mix * (g - 1.0f)) * outGain;
		const float l = inL[k], r = inR[k];
		outL[k] = l * m; outR[k] = r * m;
	}
	if (ctx) s->offset += (double)n;
	*s->port[SC_REDUCTION] = s->duck.Reduction();
}

void Cleanup(void *h) { delete (Instance *)h; }

const Ladspa::Descriptor *Descriptor()
{
	static const char *names[SC_COUNT] = {
		"Ключ: дорожка", "Диапазон ключа: 0 весь звук, 1 низ", "Частота среза ключа, Гц", "Порог, дБ", "Глубина, дБ",
		"Атака, мс", "Удержание, мс", "Возврат, мс", "Колено, дБ", "Форма возврата: 0 экспонента, 1 линейная",
		"Параллельно (смесь), %", "Выход, дБ", "Сжатие, дБ",
		"Вход L", "Вход R", "Выход L", "Выход R"
	};
	static int desc[SC_COUNT];
	static Ladspa::PortRangeHint hints[SC_COUNT];
	static bool init = false;
	if (!init) {
		const int ctl = Ladspa::PORT_INPUT | Ladspa::PORT_CONTROL;
		const int B = Ladspa::HINT_BOUNDED_BELOW | Ladspa::HINT_BOUNDED_ABOVE;
		const int I = Ladspa::HINT_INTEGER;
		for (int i = 0; i < SC_COUNT; i++) { desc[i] = ctl; hints[i] = { 0, 0, 0 }; }
		hints[SC_KEY]		= { B | I | Ladspa::HINT_DEFAULT_0, 0.0f, 64.0f };
		hints[SC_RANGE]		= { B | I | Ladspa::HINT_DEFAULT_0, 0.0f, 1.0f };
		hints[SC_CUTOFF]	= { B | Ladspa::HINT_LOGARITHMIC | Ladspa::HINT_DEFAULT_100, 40.0f, 1000.0f };
		hints[SC_THRESHOLD]	= { B | Ladspa::HINT_DEFAULT_MIDDLE, -60.0f, 0.0f };
		hints[SC_DEPTH]		= { B | Ladspa::HINT_DEFAULT_LOW, 0.0f, 40.0f };
		hints[SC_ATTACK]	= { B | Ladspa::HINT_LOGARITHMIC | Ladspa::HINT_DEFAULT_MIDDLE, 0.1f, 100.0f };
		hints[SC_HOLD]		= { B | Ladspa::HINT_DEFAULT_0, 0.0f, 500.0f };
		hints[SC_RELEASE]	= { B | Ladspa::HINT_LOGARITHMIC | Ladspa::HINT_DEFAULT_MIDDLE, 5.0f, 2000.0f };
		hints[SC_KNEE]		= { B | Ladspa::HINT_DEFAULT_MIDDLE, 0.0f, 24.0f };
		hints[SC_CURVE]		= { B | I | Ladspa::HINT_DEFAULT_0, 0.0f, 1.0f };
		hints[SC_MIX]		= { B | Ladspa::HINT_DEFAULT_MAXIMUM, 0.0f, 100.0f };
		hints[SC_OUTPUT]	= { B | Ladspa::HINT_DEFAULT_0, -24.0f, 12.0f };
		desc[SC_REDUCTION] = Ladspa::PORT_OUTPUT | Ladspa::PORT_CONTROL;
		desc[SC_IN_L] = desc[SC_IN_R] = Ladspa::PORT_INPUT | Ladspa::PORT_AUDIO;
		desc[SC_OUT_L] = desc[SC_OUT_R] = Ladspa::PORT_OUTPUT | Ladspa::PORT_AUDIO;
		init = true;
	}
	static Ladspa::Descriptor d = {
		916, kSidechainLabel, 0, "Сайдчейн-приседание", "BeAE-Plus", "BSD",
		SC_COUNT, desc, names, hints, 0,
		Instantiate, Connect, Activate, Run, 0, 0, 0, Cleanup
	};
	return &d;
}

}	// namespace

void RegisterAll()
{
	// точные значения по умолчанию (подсказки LADSPA дают приблизительные)
	static float defaults[SC_COUNT];
	for (int i = 0; i < SC_COUNT; i++) defaults[i] = 0;
	defaults[SC_KEY] = 0; defaults[SC_RANGE] = 0; defaults[SC_CUTOFF] = 150.0f;
	defaults[SC_THRESHOLD] = -24.0f; defaults[SC_DEPTH] = 9.0f; defaults[SC_ATTACK] = 5.0f;
	defaults[SC_HOLD] = 0.0f; defaults[SC_RELEASE] = 150.0f; defaults[SC_KNEE] = 6.0f; defaults[SC_CURVE] = 0;
	defaults[SC_MIX] = 100.0f; defaults[SC_OUTPUT] = 0;
	Ladspa::RegisterBuiltin(Descriptor(), defaults, "BeFX");
}

}	// namespace BeFx
