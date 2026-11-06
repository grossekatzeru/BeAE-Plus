/*
 * Графический эквалайзер BeAE-Plus: плагин LADSPA, три варианта в одной библиотеке (6, 12 и 18 полос).
 * Каждая полоса - пиковый фильтр с фиксированной частотой и добротностью, ползунок усиления -12..+12 дБ.
 * Полоса с нулевым усилением пропускается (при всех нулях звук проходит без изменений).
 */
#include "../common/ladspa_abi.h"
#include "../common/biquad.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

const int kMaxBands = 18;
const float kRange = 12.0f;		// дБ

const float kFreq6[6]   = { 63, 200, 630, 2000, 6300, 16000 };
const float kFreq12[12] = { 40, 70, 125, 200, 350, 600, 1000, 1800, 3000, 5000, 9000, 16000 };
const float kFreq18[18] = { 45, 63, 90, 125, 180, 250, 355, 500, 710, 1000, 1400, 2000, 2800, 4000, 5600, 8000, 11200, 16000 };

struct Variant {
	int				bands;
	const float		*freq;
	float			q;					// общая добротность: соседние полосы перекрываются
	int				descr[kMaxBands + 5];
	Ladspa::PortRangeHint hints[kMaxBands + 5];
	char			nameBuf[kMaxBands + 5][24];
	const char		*names[kMaxBands + 5];
	char			title[64];
	char			label[24];
	Ladspa::Descriptor d;
	bool			ready;
};

struct Instance {
	const Variant	*v;
	float			rate;
	Eq::Biquad		f[2][kMaxBands];
	float			cache[kMaxBands + 1];		// усиления, под которые рассчитаны фильтры
	bool			active[kMaxBands];
	float			outGain;
	float			*port[kMaxBands + 5];
};

inline int PortCount(const Variant *v) { return v->bands + 5; }		// усиления, выход, затем 2 входа и 2 выхода

void *Instantiate(const Ladspa::Descriptor *d, unsigned long rate)
{
	Instance *s = new Instance;
	s->v = (const Variant *)d->ImplementationData;
	s->rate = rate > 0 ? (float)rate : 44100.0f;
	for (int i = 0; i < kMaxBands + 1; i++) s->cache[i] = 1e9f;			// заставит пересчитать
	for (int i = 0; i < kMaxBands; i++) s->active[i] = false;
	for (int i = 0; i < kMaxBands + 5; i++) s->port[i] = 0;
	s->outGain = 1.0f;
	return s;
}

void Connect(void *h, unsigned long p, float *loc)
{
	Instance *s = (Instance *)h;
	if (p < (unsigned long)PortCount(s->v)) s->port[p] = loc;
}

void Activate(void *h)
{
	Instance *s = (Instance *)h;
	for (int c = 0; c < 2; c++)
		for (int i = 0; i < kMaxBands; i++) s->f[c][i].Reset();
}

void Run(void *h, unsigned long n)
{
	Instance *s = (Instance *)h;
	const Variant *v = s->v;
	const int B = v->bands;
	const int outGainPort = B, inL = B + 1, inR = B + 2, outL = B + 3, outR = B + 4;
	for (int i = 0; i <= outR; i++) if (!s->port[i]) return;

	// пересчёт только изменившихся полос
	for (int i = 0; i < B; i++) {
		float g = *s->port[i];
		g = g < -kRange ? -kRange : (g > kRange ? kRange : g);
		if (g != s->cache[i]) {
			s->cache[i] = g;
			s->active[i] = fabsf(g) > 0.005f;
			if (s->active[i]) {
				s->f[0][i].Design(Eq::PEAK, v->freq[i], g, v->q, s->rate);	// состояние фильтра при этом сохраняется
				s->f[1][i].Design(Eq::PEAK, v->freq[i], g, v->q, s->rate);
			}
		}
	}
	float og = *s->port[outGainPort];
	og = og < -kRange ? -kRange : (og > kRange ? kRange : og);
	s->outGain = powf(10.0f, og / 20.0f);

	for (int c = 0; c < 2; c++) {
		const float *in = s->port[c == 0 ? inL : inR];
		float *out = s->port[c == 0 ? outL : outR];
		for (unsigned long k = 0; k < n; k++) {
			float x = in[k];
			for (int i = 0; i < B; i++)
				if (s->active[i]) x = s->f[c][i].Run(x);
			out[k] = x * s->outGain;
		}
	}
}

void Cleanup(void *h) { delete (Instance *)h; }

void FreqName(char *buf, size_t size, float f)
{
	if (f >= 1000) snprintf(buf, size, "%g кГц", f / 1000.0f);
	else snprintf(buf, size, "%g Гц", f);
}

Variant gVariants[3];

const Ladspa::Descriptor *Build(int index)
{
	static const int counts[3] = { 6, 12, 18 };
	static const float *freqs[3] = { kFreq6, kFreq12, kFreq18 };
	static const unsigned long ids[3] = { 904, 905, 906 };
	if (index < 0 || index > 2) return 0;
	Variant &v = gVariants[index];
	if (!v.ready) {
		const int B = counts[index];
		v.bands = B; v.freq = freqs[index];
		const double ratio = pow((double)v.freq[B - 1] / v.freq[0], 1.0 / (B - 1));
		v.q = (float)(sqrt(ratio) / (ratio - 1.0));
		const int ctl = Ladspa::PORT_INPUT | Ladspa::PORT_CONTROL;
		const int bound = Ladspa::HINT_BOUNDED_BELOW | Ladspa::HINT_BOUNDED_ABOVE;
		for (int i = 0; i < B; i++) {
			v.descr[i] = ctl;
			v.hints[i].hint = bound | Ladspa::HINT_DEFAULT_0;
			v.hints[i].lower = -kRange; v.hints[i].upper = kRange;
			FreqName(v.nameBuf[i], sizeof(v.nameBuf[i]), v.freq[i]);
			v.names[i] = v.nameBuf[i];
		}
		v.descr[B] = ctl;
		v.hints[B].hint = bound | Ladspa::HINT_DEFAULT_0; v.hints[B].lower = -kRange; v.hints[B].upper = kRange;
		snprintf(v.nameBuf[B], sizeof(v.nameBuf[B]), "Выход, дБ");
		v.names[B] = v.nameBuf[B];
		const char *aud[4] = { "Вход L", "Вход R", "Выход L", "Выход R" };
		for (int k = 0; k < 4; k++) {
			v.descr[B + 1 + k] = (k < 2 ? Ladspa::PORT_INPUT : Ladspa::PORT_OUTPUT) | Ladspa::PORT_AUDIO;
			v.hints[B + 1 + k].hint = 0; v.hints[B + 1 + k].lower = 0; v.hints[B + 1 + k].upper = 0;
			snprintf(v.nameBuf[B + 1 + k], sizeof(v.nameBuf[0]), "%s", aud[k]);
			v.names[B + 1 + k] = v.nameBuf[B + 1 + k];
		}
		snprintf(v.title, sizeof(v.title), "Графический эквалайзер, %d полос", B);
		snprintf(v.label, sizeof(v.label), "beae_graphic_eq_%d", B);
		Ladspa::Descriptor d = { ids[index], v.label, 0, v.title, "BeAE-Plus", "BSD",
			(unsigned long)(B + 5), v.descr, v.names, v.hints, (void *)&v,
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
