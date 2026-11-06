/*
 * Параметрический эквалайзер BeAE-Plus: плагин LADSPA, два варианта в одной библиотеке (3 и 5 полос).
 * Первая полоса - низкочастотная полка, последняя - высокочастотная, средние - колоколообразные
 * (частота, усиление, добротность). Усиление -15..+15 дБ. Полоса с нулевым усилением пропускается.
 */
#include "../common/ladspa_abi.h"
#include "../common/biquad.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

const int kMaxBands = 5;
const int kMaxPorts = kMaxBands * 3 + 1 + 4;
const float kRange = 15.0f;		// дБ

struct BandSpec {
	Eq::Type	type;
	const char	*title;
	float		fMin, fMax;			// диапазон частоты, Гц
	int			freqDefault;		// HINT_DEFAULT_* частоты (по среднему геометрическому диапазона или 100)
};

// Значения по умолчанию задаются подсказками LADSPA (среднее геометрическое диапазона для логарифмической шкалы):
// НЧ 100 Гц (подсказка "100"); средние: 300, 1000, 4000 Гц; ВЧ 8000 или 10000 Гц.
const BandSpec kSpec3[3] = {
	{ Eq::LOW_SHELF,  "НЧ",        20.0f,   1000.0f, Ladspa::HINT_DEFAULT_100 },
	{ Eq::PEAK,       "Средние",   200.0f,  5000.0f, Ladspa::HINT_DEFAULT_MIDDLE },	// 1000
	{ Eq::HIGH_SHELF, "ВЧ",        4000.0f, 16000.0f, Ladspa::HINT_DEFAULT_MIDDLE },	// 8000
};
const BandSpec kSpec5[5] = {
	{ Eq::LOW_SHELF,  "НЧ",        20.0f,   1000.0f, Ladspa::HINT_DEFAULT_100 },
	{ Eq::PEAK,       "Полоса 1",  100.0f,  900.0f,  Ladspa::HINT_DEFAULT_MIDDLE },	// 300
	{ Eq::PEAK,       "Полоса 2",  250.0f,  4000.0f, Ladspa::HINT_DEFAULT_MIDDLE },	// 1000
	{ Eq::PEAK,       "Полоса 3",  1000.0f, 16000.0f, Ladspa::HINT_DEFAULT_MIDDLE },	// 4000
	{ Eq::HIGH_SHELF, "ВЧ",        5000.0f, 20000.0f, Ladspa::HINT_DEFAULT_MIDDLE },	// 10000
};

struct Variant {
	int				bands;
	const BandSpec	*spec;
	int				firstPort[kMaxBands];		// индекс порта "частота" полосы
	int				outGainPort, inL, inR, outL, outR, portCount;
	int				descr[kMaxPorts];
	Ladspa::PortRangeHint hints[kMaxPorts];
	char			nameBuf[kMaxPorts][40];
	const char		*names[kMaxPorts];
	char			title[96], label[24];
	Ladspa::Descriptor d;
	bool			ready;
};

struct Instance {
	const Variant	*v;
	float			rate;
	Eq::Biquad		f[2][kMaxBands];
	float			cache[kMaxBands][3];
	bool			active[kMaxBands];
	float			*port[kMaxPorts];
};

void *Instantiate(const Ladspa::Descriptor *d, unsigned long rate)
{
	Instance *s = new Instance;
	s->v = (const Variant *)d->ImplementationData;
	s->rate = rate > 0 ? (float)rate : 44100.0f;
	for (int i = 0; i < kMaxBands; i++) { s->active[i] = false; for (int k = 0; k < 3; k++) s->cache[i][k] = 1e9f; }
	for (int i = 0; i < kMaxPorts; i++) s->port[i] = 0;
	return s;
}

void Connect(void *h, unsigned long p, float *loc)
{
	Instance *s = (Instance *)h;
	if (p < (unsigned long)s->v->portCount) s->port[p] = loc;
}

void Activate(void *h)
{
	Instance *s = (Instance *)h;
	for (int c = 0; c < 2; c++)
		for (int i = 0; i < kMaxBands; i++) s->f[c][i].Reset();
}

float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

void Run(void *h, unsigned long n)
{
	Instance *s = (Instance *)h;
	const Variant *v = s->v;
	for (int i = 0; i < v->portCount; i++) if (!s->port[i]) return;

	for (int i = 0; i < v->bands; i++) {
		const BandSpec &sp = v->spec[i];
		const bool peak = sp.type == Eq::PEAK;
		const int base = v->firstPort[i];
		const float f = Clamp(*s->port[base], sp.fMin, sp.fMax);
		const float g = Clamp(*s->port[base + 1], -kRange, kRange);
		const float q = peak ? Clamp(*s->port[base + 2], 0.25f, 4.0f) : 1.0f;
		if (f != s->cache[i][0] || g != s->cache[i][1] || q != s->cache[i][2]) {
			s->cache[i][0] = f; s->cache[i][1] = g; s->cache[i][2] = q;
			s->active[i] = fabsf(g) > 0.005f;
			if (s->active[i])
				for (int c = 0; c < 2; c++) s->f[c][i].Design(sp.type, f, g, q, s->rate);
		}
	}
	const float og = powf(10.0f, Clamp(*s->port[v->outGainPort], -kRange, kRange) / 20.0f);

	for (int c = 0; c < 2; c++) {
		const float *in = s->port[c == 0 ? v->inL : v->inR];
		float *out = s->port[c == 0 ? v->outL : v->outR];
		for (unsigned long k = 0; k < n; k++) {
			float x = in[k];
			for (int i = 0; i < v->bands; i++)
				if (s->active[i]) x = s->f[c][i].Run(x);
			out[k] = x * og;
		}
	}
}

void Cleanup(void *h) { delete (Instance *)h; }

Variant gVariants[2];

const Ladspa::Descriptor *Build(int index)
{
	if (index < 0 || index > 1) return 0;
	Variant &v = gVariants[index];
	if (!v.ready) {
		const int B = index == 0 ? 3 : 5;
		v.bands = B;
		v.spec = index == 0 ? kSpec3 : kSpec5;
		const int ctl = Ladspa::PORT_INPUT | Ladspa::PORT_CONTROL;
		const int bound = Ladspa::HINT_BOUNDED_BELOW | Ladspa::HINT_BOUNDED_ABOVE;
		int p = 0;
		for (int i = 0; i < B; i++) {
			const BandSpec &sp = v.spec[i];
			v.firstPort[i] = p;
			// частота (логарифмическая шкала)
			v.descr[p] = ctl;
			v.hints[p].hint = bound | Ladspa::HINT_LOGARITHMIC | sp.freqDefault;
			v.hints[p].lower = sp.fMin; v.hints[p].upper = sp.fMax;
			snprintf(v.nameBuf[p], sizeof(v.nameBuf[p]), "%s: частота, Гц", sp.title);
			v.names[p] = v.nameBuf[p]; p++;
			// усиление
			v.descr[p] = ctl;
			v.hints[p].hint = bound | Ladspa::HINT_DEFAULT_0;
			v.hints[p].lower = -kRange; v.hints[p].upper = kRange;
			snprintf(v.nameBuf[p], sizeof(v.nameBuf[p]), "%s: усиление, дБ", sp.title);
			v.names[p] = v.nameBuf[p]; p++;
			// добротность (только у колоколов)
			if (sp.type == Eq::PEAK) {
				v.descr[p] = ctl;
				v.hints[p].hint = bound | Ladspa::HINT_LOGARITHMIC | Ladspa::HINT_DEFAULT_MIDDLE;	// 1.0
				v.hints[p].lower = 0.25f; v.hints[p].upper = 4.0f;
				snprintf(v.nameBuf[p], sizeof(v.nameBuf[p]), "%s: добротность Q", sp.title);
				v.names[p] = v.nameBuf[p]; p++;
			}
		}
		v.outGainPort = p;
		v.descr[p] = ctl;
		v.hints[p].hint = bound | Ladspa::HINT_DEFAULT_0; v.hints[p].lower = -kRange; v.hints[p].upper = kRange;
		snprintf(v.nameBuf[p], sizeof(v.nameBuf[p]), "Выход, дБ");
		v.names[p] = v.nameBuf[p]; p++;
		const char *aud[4] = { "Вход L", "Вход R", "Выход L", "Выход R" };
		v.inL = p; v.inR = p + 1; v.outL = p + 2; v.outR = p + 3;
		for (int k = 0; k < 4; k++, p++) {
			v.descr[p] = (k < 2 ? Ladspa::PORT_INPUT : Ladspa::PORT_OUTPUT) | Ladspa::PORT_AUDIO;
			v.hints[p].hint = 0; v.hints[p].lower = 0; v.hints[p].upper = 0;
			snprintf(v.nameBuf[p], sizeof(v.nameBuf[p]), "%s", aud[k]);
			v.names[p] = v.nameBuf[p];
		}
		v.portCount = p;
		snprintf(v.title, sizeof(v.title), "%s", B == 3 ? "Параметрический эквалайзер, 3 полосы" : "Параметрический эквалайзер, 5 полос");
		snprintf(v.label, sizeof(v.label), "beae_parametric_eq_%d", B);
		Ladspa::Descriptor d = { (unsigned long)(907 + index), v.label, 0, v.title, "BeAE-Plus", "BSD",
			(unsigned long)p, v.descr, v.names, v.hints, (void *)&v,
			Instantiate, Connect, Activate, Run, 0, 0, 0, Cleanup };
		v.d = d;
		v.ready = true;
	}
	return &v.d;
}

}	// namespace

extern "C" const Ladspa::Descriptor *ladspa_descriptor(unsigned long index)
{
	return Build((int)index);
}
