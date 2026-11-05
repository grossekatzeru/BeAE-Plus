/*
 * SpringReverb - см. SpringReverb.h
 */
#include "SpringReverb.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

namespace SpringReverb {

static const int	kMinSprings = 3;
static const int	kMaxSprings = 5;
static const int	kStages = 24;			// фазовых фильтров на пружину
static const float	kRatio[kMaxSprings] = { 1.00f, 1.31f, 1.67f, 2.09f, 2.53f };	// длины пружин
static const float	kStereo = 1.037f;		// правый канал чуть длиннее: шире картина
static const float	kMaxDelayMs = 250.0f;
static const float	kPi = 3.14159265f;

const float kDefaults[P_COUNT] = {
	0.50f,		// Dwell
	0.50f,		// Decay
	0.0f,		// Tone
	0.35f,		// Mix
	3.0f,		// Springs
	45.0f,		// Delay, мс
	100.0f,		// Low cut, Гц
	5500.0f,	// High cut, Гц
	0.25f,		// Nonlinearity
	0.60f,		// Boing
	0, 0, 0, 0
};

static inline float Flush(float x) { return (fabsf(x) < 1e-18f) ? 0.0f : x; }

// одна пружина одного канала
struct Spring {
	std::vector<float>	line;
	int		pos;
	float	ap[kStages];			// состояния фазовых фильтров
	float	lp;						// фильтр высоких частот внутри петли
	float	delay;					// текущая длина (сглаженная), кадров
	Spring() : pos(0), lp(0), delay(1) { memset(ap, 0, sizeof(ap)); }
};

struct Channel {
	Spring	sp[kMaxSprings];
	float	hp;						// фильтр низких на входе
	float	tone;					// разделитель для Tone
	float	outLp;					// фильтр высоких на выходе
	Channel() : hp(0), tone(0), outLp(0) {}
};

struct Instance {
	float		rate;
	Channel		ch[2];
	float		*port[P_COUNT];
	bool		primed;			// длины линий установлены под текущее значение
	Instance() : rate(44100), primed(false) { for (int i = 0; i < P_COUNT; i++) port[i] = 0; }
};

static float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void *Instantiate(const Ladspa::Descriptor *, unsigned long rate)
{
	Instance *s = new Instance;
	s->rate = rate > 0 ? (float)rate : 44100.0f;
	const int cap = (int)(kMaxDelayMs * 0.001f * s->rate * kRatio[kMaxSprings - 1] * kStereo) + 8;
	for (int c = 0; c < 2; c++)
		for (int k = 0; k < kMaxSprings; k++) {
			s->ch[c].sp[k].line.assign(cap, 0.0f);
			s->ch[c].sp[k].delay = 0.045f * s->rate;
		}
	return s;
}

static void Connect(void *h, unsigned long port, float *loc)
{
	Instance *s = (Instance *)h;
	if (port < (unsigned long)P_COUNT) s->port[port] = loc;
}

static void Activate(void *h)
{
	Instance *s = (Instance *)h;
	s->primed = false;
	for (int c = 0; c < 2; c++) {
		Channel &ch = s->ch[c];
		ch.hp = ch.tone = ch.outLp = 0;
		for (int k = 0; k < kMaxSprings; k++) {
			Spring &sp = ch.sp[k];
			std::fill(sp.line.begin(), sp.line.end(), 0.0f);
			sp.pos = 0; sp.lp = 0;
			memset(sp.ap, 0, sizeof(sp.ap));
		}
	}
}

static inline float OnePoleCoef(float hz, float rate)
{
	return 1.0f - expf(-2.0f * kPi * Clamp(hz, 1.0f, rate * 0.45f) / rate);
}

static void Run(void *h, unsigned long n)
{
	Instance *s = (Instance *)h;
	for (int i = 0; i < P_COUNT; i++)
		if (!s->port[i]) return;

	const float rate = s->rate;
	const float dwell = Clamp(*s->port[P_DWELL], 0.0f, 1.0f);
	const float decay = Clamp(*s->port[P_DECAY], 0.0f, 1.0f);
	const float tone = Clamp(*s->port[P_TONE], -1.0f, 1.0f);
	const float mix = Clamp(*s->port[P_MIX], 0.0f, 1.0f);
	int springs = (int)(*s->port[P_SPRINGS] + 0.5f);
	springs = springs < kMinSprings ? kMinSprings : (springs > kMaxSprings ? kMaxSprings : springs);
	const float delayMs = Clamp(*s->port[P_DELAY], 10.0f, kMaxDelayMs);
	const float lowCut = Clamp(*s->port[P_LOWCUT], 0.0f, 300.0f);
	const float highCut = Clamp(*s->port[P_HIGHCUT], 4000.0f, 20000.0f);
	const float nl = Clamp(*s->port[P_NONLIN], 0.0f, 1.0f);
	const float boing = Clamp(*s->port[P_BOING], 0.0f, 1.0f);

	// Коэффициенты
	const float hpA = lowCut < 1.0f ? 0.0f : OnePoleCoef(lowCut, rate);		// 0 = фильтр выключен
	const float lpA = OnePoleCoef(highCut, rate);
	const float toneA = OnePoleCoef(900.0f, rate);
	const float apA = 0.25f + 0.55f * boing;			// чем больше, тем сильнее разброс по частотам
	// Decay задаёт время затухания на 60 дБ: от 0.3 с до 4 с; усиление петли считается по длине пружины
	const float rt60 = 0.3f * powf(4.0f / 0.3f, decay);
	const float drive = 0.25f + 2.75f * dwell;			// подача в пружину
	const float shape = 1.0f + 5.0f * nl;				// крутизна насыщения
	const float norm = 1.0f / sqrtf((float)springs);
	const float lowG = 1.0f - 0.6f * tone, highG = 1.0f + 0.6f * tone;
	const float slew = 1.0f - expf(-1.0f / (0.05f * rate));	// сглаживание длины линии, ~50 мс

	for (int c = 0; c < 2; c++) {
		const float *in = s->port[c == 0 ? P_IN_L : P_IN_R];
		float *out = s->port[c == 0 ? P_OUT_L : P_OUT_R];
		Channel &ch = s->ch[c];
		const float lenBase = delayMs * 0.001f * rate * (c == 1 ? kStereo : 1.0f);
		float fbk[kMaxSprings];
		for (int k = 0; k < kMaxSprings; k++) {
			float g = powf(0.001f, lenBase * kRatio[k] / rate / rt60);
			fbk[k] = g > 0.97f ? 0.97f : g;
		}
		if (!s->primed)
			for (int k = 0; k < kMaxSprings; k++) {
				float t = lenBase * kRatio[k];
				const float cap = (float)(ch.sp[k].line.size() - 4);
				ch.sp[k].delay = t > cap ? cap : t;
			}

		for (unsigned long i = 0; i < n; i++) {
			const float dry = in[i];
			// фильтр низких: вычитаем сглаженный сигнал
			float x = dry;
			if (hpA > 0.0f) { ch.hp += hpA * (x - ch.hp); ch.hp = Flush(ch.hp); x -= ch.hp; }
			// подача + лёгкая нелинейность (при nl=0 почти линейно)
			x *= drive;
			x = (1.0f - nl) * x + nl * (tanhf(shape * x) / tanhf(shape));

			float wet = 0.0f;
			for (int k = 0; k < springs; k++) {
				Spring &sp = ch.sp[k];
				const int cap = (int)sp.line.size();
				float target = lenBase * kRatio[k];
				if (target > (float)(cap - 4)) target = (float)(cap - 4);
				sp.delay += slew * (target - sp.delay);

				// чтение с дробной задержкой
				float rd = (float)sp.pos - sp.delay;
				while (rd < 0) rd += (float)cap;
				int i0 = (int)rd;
				float fr = rd - (float)i0;
				int i1 = i0 + 1; if (i1 >= cap) i1 = 0;
				float y = sp.line[i0] * (1.0f - fr) + sp.line[i1] * fr;

				// дисперсия: цепочка фазовых фильтров (высокие частоты приходят раньше низких)
				for (int a = 0; a < kStages; a++) {
					float o = apA * y + sp.ap[a];
					sp.ap[a] = Flush(y - apA * o);
					y = o;
				}
				// потери на высоких частотах в петле
				sp.lp += lpA * (y - sp.lp);
				sp.lp = Flush(sp.lp);
				y = sp.lp;

				float w = x + fbk[k] * y;
				w = tanhf(w);						// мягкое ограничение: петля не разгоняется
				sp.line[sp.pos] = Flush(w);
				if (++sp.pos >= cap) sp.pos = 0;
				wet += y;
			}
			wet *= norm;

			// выходной фильтр высоких и тембр
			ch.outLp += lpA * (wet - ch.outLp);
			ch.outLp = Flush(ch.outLp);
			wet = ch.outLp;
			ch.tone += toneA * (wet - ch.tone);
			ch.tone = Flush(ch.tone);
			wet = ch.tone * lowG + (wet - ch.tone) * highG;

			out[i] = dry * (1.0f - mix * 0.5f) + wet * mix * 1.6f;
		}
	}
	s->primed = true;
}

static void Cleanup(void *h) { delete (Instance *)h; }

const Ladspa::Descriptor *Descriptor()
{
	static const char *names[P_COUNT] = {
		"Dwell (подача в пружину)", "Decay (затухание)", "Tone (тембр)", "Mix (эффект)",
		"Пружин", "Задержка круга, мс", "Срез низких, Гц", "Срез высоких, Гц",
		"Нелинейность", "Боинг (дисперсия)",
		"Вход L", "Вход R", "Выход L", "Выход R"
	};
	static int desc[P_COUNT];
	static Ladspa::PortRangeHint hints[P_COUNT];
	static bool init = false;
	if (!init) {
		const int ctl = Ladspa::PORT_INPUT | Ladspa::PORT_CONTROL;
		const int B = Ladspa::HINT_BOUNDED_BELOW | Ladspa::HINT_BOUNDED_ABOVE;
		for (int i = 0; i < P_COUNT; i++) { desc[i] = ctl; hints[i].hint = B; }
		hints[P_DWELL]	= { B | Ladspa::HINT_DEFAULT_MIDDLE, 0.0f, 1.0f };
		hints[P_DECAY]	= { B | Ladspa::HINT_DEFAULT_MIDDLE, 0.0f, 1.0f };
		hints[P_TONE]	= { B | Ladspa::HINT_DEFAULT_0, -1.0f, 1.0f };
		hints[P_MIX]	= { B | Ladspa::HINT_DEFAULT_LOW, 0.0f, 1.0f };
		hints[P_SPRINGS] = { B | Ladspa::HINT_INTEGER | Ladspa::HINT_DEFAULT_MIDDLE, 3.0f, 5.0f };
		hints[P_DELAY]	= { B | Ladspa::HINT_DEFAULT_LOW, 10.0f, kMaxDelayMs };
		hints[P_LOWCUT]	= { B | Ladspa::HINT_DEFAULT_LOW, 0.0f, 300.0f };
		hints[P_HIGHCUT] = { B | Ladspa::HINT_DEFAULT_LOW, 4000.0f, 20000.0f };
		hints[P_NONLIN]	= { B | Ladspa::HINT_DEFAULT_LOW, 0.0f, 1.0f };
		hints[P_BOING]	= { B | Ladspa::HINT_DEFAULT_MIDDLE, 0.0f, 1.0f };
		desc[P_IN_L] = desc[P_IN_R] = Ladspa::PORT_INPUT | Ladspa::PORT_AUDIO;
		desc[P_OUT_L] = desc[P_OUT_R] = Ladspa::PORT_OUTPUT | Ladspa::PORT_AUDIO;
		for (int i = P_IN_L; i < P_COUNT; i++) hints[i] = { 0, 0, 0 };
		init = true;
	}
	static Ladspa::Descriptor d = {
		0x42654501, "beae_spring_reverb", 0, "Ламповое эхо (пружинный реверб)", "BeAE-Plus", "BSD",
		P_COUNT, desc, names, hints, 0,
		Instantiate, Connect, Activate, Run, 0, 0, 0, Cleanup
	};
	return &d;
}

}	// namespace SpringReverb
