// Проверка "Вокальной цепочки (слэп + пластина)" на Linux через Ladspa::Runner.
#include "LadspaHost.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string>
#include <vector>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

struct Par { float time, fb, smix, decay, pre, drop, rmix; };
static Par Def() { Par p = { 30.0f, 5.0f, 12.0f, 1.8f, 32.0f, 4500.0f, 25.0f }; return p; }

static Ladspa::Plugin Find()
{
	std::vector<std::string> dirs; dirs.push_back("build");
	std::vector<Ladspa::Plugin> all = Ladspa::Scan(dirs);
	for (size_t i = 0; i < all.size(); i++) if (all[i].label == "beae_vocal_slap_plate") return all[i];
	printf("FAIL: plugin not found\n"); fails++;
	return Ladspa::Plugin();
}

static std::vector<float> Run(const Ladspa::Plugin &pl, float rate, const Par &p, std::vector<float> in, int block = 512)
{
	Ladspa::Runner r; std::string err;
	if (!r.Start(pl, rate, 2, &err)) { printf("FAIL start: %s\n", err.c_str()); fails++; return in; }
	const float v[7] = { p.time, p.fb, p.smix, p.decay, p.pre, p.drop, p.rmix };
	for (int i = 0; i < 7; i++) r.SetControl(i, v[i]);
	const int frames = (int)(in.size() / 2);
	for (int pos = 0; pos < frames; pos += block) { int n = frames - pos < block ? frames - pos : block; r.Process(&in[pos * 2], n); }
	return in;
}
static std::vector<float> Impulse(float rate, double sec) { std::vector<float> b((size_t)(sec * rate) * 2, 0.0f); b[0] = b[1] = 1.0f; return b; }
static std::vector<float> Noise(float rate, double sec, float amp)
{
	std::vector<float> b((size_t)(sec * rate) * 2); unsigned s = 12345;
	for (size_t i = 0; i < b.size(); i++) { s = s * 1664525u + 1013904223u; b[i] = amp * ((int)(s >> 8) / 8388608.0f - 1.0f); }
	return b;
}
static double Energy(const std::vector<float> &b, float rate, double t0, double t1)
{
	double e = 0; for (size_t i = (size_t)(t0 * rate); i < (size_t)(t1 * rate) && i < b.size() / 2; i++) e += b[i * 2] * b[i * 2] + b[i * 2 + 1] * b[i * 2 + 1];
	return e;
}
static double Rt60(const std::vector<float> &b, float rate)
{
	const size_t n = b.size() / 2;
	std::vector<double> edc(n); double acc = 0;
	for (size_t i = n; i-- > 0;) { if (i >= 8) acc += (double)b[i * 2] * b[i * 2] + (double)b[i * 2 + 1] * b[i * 2 + 1]; edc[i] = acc; }
	const double e0 = edc[8];
	double t5 = -1, t25 = -1;
	for (size_t i = 8; i < n; i++) {
		double db = 10 * log10(edc[i] / e0 + 1e-30);
		if (t5 < 0 && db <= -5) t5 = i / rate;
		if (t25 < 0 && db <= -25) { t25 = i / rate; break; }
	}
	if (t5 < 0 || t25 < 0) return -1;
	return (t25 - t5) * 3.0;
}
static double BandEnergy(const std::vector<float> &b, float rate, double f)
{
	double c = 2 * cos(2 * M_PI * f / rate), s1 = 0, s2 = 0;
	for (size_t i = 0; i < b.size() / 2; i++) { double s = b[i * 2] + c * s1 - s2; s2 = s1; s1 = s; }
	return s1 * s1 + s2 * s2 - c * s1 * s2;
}

int main()
{
	Ladspa::Plugin pl = Find();
	if (pl.label.empty()) return 1;
	const float rates[3] = { 44100.0f, 48000.0f, 96000.0f };

	// 1) оба уровня в ноль: сигнал проходит без изменений
	{
		Par p = Def(); p.smix = 0; p.rmix = 0;
		std::vector<float> in = Noise(44100, 0.5, 0.5f), out = Run(pl, 44100, p, in);
		double d = 0; for (size_t i = 0; i < in.size(); i++) d = fmax(d, fabs(in[i] - out[i]));
		CHECK(d < 1e-6, "passthrough diff %g", d);
		printf("passthrough max diff %g\n", d);
	}
	// 2) слэп: эхо через заданное время с амплитудой = уровню; на разных частотах дискретизации
	for (int r = 0; r < 3; r++) {
		const float rate = rates[r];
		Par p = Def(); p.rmix = 0; p.fb = 0;
		std::vector<float> out = Run(pl, rate, p, Impulse(rate, 0.3));
		const size_t want = (size_t)(0.030 * rate + 0.5);
		size_t pk = 1; float best = 0;
		for (size_t i = 1; i < out.size() / 2; i++) if (fabsf(out[i * 2]) > best) { best = fabsf(out[i * 2]); pk = i; }
		CHECK(pk + 1 >= want && pk <= want + 1, "echo at %zu, want %zu (%g Hz)", pk, want, rate);
		CHECK(fabs(best - 0.12) < 0.005, "echo amp %.4f want 0.12 (%g Hz)", best, rate);
		printf("slap %g Hz: echo at %.2f ms, amp %.4f\n", rate, pk * 1000.0 / rate, best);
		// после эха тишина (обратной связи нет)
		CHECK(Energy(out, rate, 0.040, 0.3) < 1e-9, "unexpected tail with feedback 0");
	}
	// 3) обратная связь: второе эхо = повтор * уровень
	{
		const float rate = 44100; Par p = Def(); p.rmix = 0; p.fb = 10;
		std::vector<float> out = Run(pl, rate, p, Impulse(rate, 0.3));
		const size_t t = (size_t)(0.030 * rate + 0.5);
		float e2 = 0; for (size_t i = 2 * t - 1; i <= 2 * t + 1; i++) e2 = fmaxf(e2, fabsf(out[i * 2]));
		CHECK(fabs(e2 - 0.012) < 0.001, "second echo %.4f want 0.012", e2);
		printf("feedback 10%%: second echo %.4f (want 0.012)\n", e2);
	}
	// 4) предзадержка реверба: до неё хвоста нет
	{
		const float rate = 44100; Par p = Def(); p.smix = 0; p.rmix = 100; p.pre = 40;
		std::vector<float> out = Run(pl, rate, p, Impulse(rate, 1.0));
		out[0] = out[1] = 0;		// убрать сухой импульс
		double pre = Energy(out, rate, 0.0, 0.038), tail = Energy(out, rate, 0.040, 1.0);
		CHECK(pre < 1e-12 && tail > 1e-4, "pre %g tail %g", pre, tail);
		printf("pre-delay 40 ms: energy before %g, after %g\n", pre, tail);
	}
	// 5) время затухания реверба
	{
		const float rate = 44100; const float rts[3] = { 1.5f, 1.8f, 2.2f };
		for (int i = 0; i < 3; i++) {
			Par p = Def(); p.smix = 0; p.rmix = 100; p.pre = 0; p.drop = 16000; p.decay = rts[i];
			std::vector<float> out = Run(pl, rate, p, Impulse(rate, 4.0));
			double rt = Rt60(out, rate);
			CHECK(rt > rts[i] * 0.85 && rt < rts[i] * 1.3, "RT60 set %.1f got %.2f", rts[i], rt);
			printf("RT60 set %.1f -> %.2f s\n", rts[i], rt);
		}
	}
	// 6) завал высоких: на 8 кГц хвост тише, на 1 кГц - не меняется
	{
		const float rate = 44100; Par a = Def(), b = Def();
		a.smix = b.smix = 0; a.rmix = b.rmix = 100; a.drop = 16000; b.drop = 3000;
		std::vector<float> in = Noise(rate, 2.0, 0.3f), oa = Run(pl, rate, a, in), ob = Run(pl, rate, b, in);
		for (size_t i = 0; i < in.size(); i++) { oa[i] -= in[i]; ob[i] -= in[i]; }		// только хвост
		const double h = BandEnergy(ob, rate, 8000) / BandEnergy(oa, rate, 8000);
		const double l = BandEnergy(ob, rate, 1000) / BandEnergy(oa, rate, 1000);
		CHECK(h < 0.2 && l > 0.7, "8k ratio %.3f, 1k ratio %.3f", h, l);
		printf("hi-drop 3 kHz vs 16 kHz: 8 kHz x%.3f, 1 kHz x%.3f\n", h, l);
	}
	// 7) блоки разной длины дают одно и то же
	{
		const float rate = 44100; Par p = Def();
		std::vector<float> in = Noise(rate, 0.6, 0.4f), o1 = Run(pl, rate, p, in, 512), o2 = Run(pl, rate, p, in, 37);
		double d = 0; for (size_t i = 0; i < o1.size(); i++) d = fmax(d, fabs(o1[i] - o2[i]));
		CHECK(d < 1e-5, "block size dependence %g", d);
		printf("block-size dependence %g\n", d);
	}
	// 8) устойчивость при крайних значениях и смене времени слэпа на ходу
	{
		const float rate = 44100; Par p = { 80.0f, 30.0f, 50.0f, 6.0f, 100.0f, 2000.0f, 100.0f };
		std::vector<float> in = Noise(rate, 4.0, 1.0f);
		Ladspa::Runner r; std::string err; r.Start(pl, rate, 2, &err);
		const float v[7] = { p.time, p.fb, p.smix, p.decay, p.pre, p.drop, p.rmix };
		for (int i = 0; i < 7; i++) r.SetControl(i, v[i]);
		float peak = 0; bool fin = true; const int frames = (int)(in.size() / 2);
		for (int pos = 0, blk = 0; pos < frames; pos += 512, blk++) {
			int n = frames - pos < 512 ? frames - pos : 512;
			r.SetControl(0, (blk & 1) ? 10.0f : 80.0f);
			r.Process(&in[pos * 2], n);
			for (int i = 0; i < n * 2; i++) { float x = in[pos * 2 + i]; if (!isfinite(x)) fin = false; else peak = fmaxf(peak, fabsf(x)); }
		}
		CHECK(fin && peak < 20.0f, "unstable: finite %d peak %g", (int)fin, peak);
		printf("extreme settings: peak %.2f finite %d\n", peak, (int)fin);
	}
	printf(fails ? "FAILED %d\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}
