// Проверка "Дисторшна (DS-1)" на Linux через Ladspa::Runner.
#include "LadspaHost.h"
#include <stdio.h>
#include <math.h>
#include <string>
#include <vector>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

struct Par { float trim, dist, tone, level, mix; };
static Par Def() { Par p = { 0, 50, 50, 0, 100 }; return p; }

static Ladspa::Plugin Find()
{
	std::vector<std::string> dirs; dirs.push_back("build");
	std::vector<Ladspa::Plugin> all = Ladspa::Scan(dirs);
	for (size_t i = 0; i < all.size(); i++) if (all[i].label == "beae_ds1") return all[i];
	printf("FAIL: plugin not found\n"); fails++;
	return Ladspa::Plugin();
}
static std::vector<float> Run(const Ladspa::Plugin &pl, float rate, const Par &p, std::vector<float> in, int block = 512)
{
	Ladspa::Runner r; std::string err;
	if (!r.Start(pl, rate, 2, &err)) { printf("FAIL start: %s\n", err.c_str()); fails++; return in; }
	const float v[5] = { p.trim, p.dist, p.tone, p.level, p.mix };
	for (int i = 0; i < 5; i++) r.SetControl(i, v[i]);
	const int frames = (int)(in.size() / 2);
	for (int pos = 0; pos < frames; pos += block) { int n = frames - pos < block ? frames - pos : block; r.Process(&in[pos * 2], n); }
	return in;
}
static std::vector<float> Sine(float rate, double f, double dbPeak, double sec)
{
	std::vector<float> b((size_t)(sec * rate) * 2); const double a = pow(10.0, dbPeak / 20.0);
	for (size_t i = 0; i < b.size() / 2; i++) b[i * 2] = b[i * 2 + 1] = (float)(a * sin(2 * M_PI * f * i / rate));
	return b;
}
static double Goertzel(const std::vector<float> &b, float rate, double t0, double t1, double f)
{
	size_t i0 = (size_t)(t0 * rate), i1 = (size_t)(t1 * rate);
	double c = 2 * cos(2 * M_PI * f / rate), s1 = 0, s2 = 0;
	for (size_t i = i0; i < i1; i++) { double s = b[i * 2] + c * s1 - s2; s2 = s1; s1 = s; }
	return sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) * 2 / (i1 - i0);
}
static double Db(double x) { return 20 * log10(x + 1e-30); }
static double Rms(const std::vector<float> &b, float rate, double t0, double t1)
{
	double e = 0; size_t n = 0;
	for (size_t i = (size_t)(t0 * rate); i < (size_t)(t1 * rate) && i < b.size() / 2; i++) { e += (double)b[i * 2] * b[i * 2]; n++; }
	return sqrt(e / n);
}

int main()
{
	Ladspa::Plugin pl = Find();
	if (pl.label.empty()) return 1;
	const float R = 44100;

	// 1) смесь 0: сухой сигнал с задержкой ровно 32 отсчёта
	{
		Par p = Def(); p.mix = 0;
		std::vector<float> in(R * 0.2 * 2, 0.0f);
		for (int i = 0; i < 400; i++) { in[(size_t)(100 + i) * 2] = in[(size_t)(100 + i) * 2 + 1] = sinf(i * 0.37f) * 0.5f; }
		std::vector<float> out = Run(pl, R, p, in);
		double d = 0;
		for (size_t i = 32; i < in.size() / 2; i++) d = fmax(d, fabs(out[i * 2] - in[(i - 32) * 2]));
		CHECK(d < 1e-6, "dry delay mismatch %g", d);
		printf("mix 0: dry delayed by 32 samples, max error %g\n", d);
	}
	// 2) драйв: усиление малого сигнала растёт примерно на 20 дБ (K от 10,9 до 117), горб в средних: 1 кГц сильнее 100 Гц
	{
		Par lo = Def(), hi = Def(); lo.dist = 0; hi.dist = 100; lo.tone = hi.tone = 100;
		std::vector<float> in = Sine(R, 1000, -60, 0.6);
		const double a = Db(Goertzel(Run(pl, R, lo, in), R, 0.3, 0.6, 1000) / Goertzel(in, R, 0.3, 0.6, 1000));
		const double b = Db(Goertzel(Run(pl, R, hi, in), R, 0.3, 0.6, 1000) / Goertzel(in, R, 0.3, 0.6, 1000));
		CHECK(b - a > 26 && b - a < 31, "drive range %.1f dB", b - a);
		printf("small-signal gain at 1 kHz: drive 0 = %.1f dB, drive 100 = %.1f dB (span %.1f)\n", a, b, b - a);
		std::vector<float> low = Sine(R, 100, -60, 0.8);
		const double g100 = Db(Goertzel(Run(pl, R, hi, low), R, 0.4, 0.8, 100) / Goertzel(low, R, 0.4, 0.8, 100));
		CHECK(b - g100 > 10, "mid hump %.1f dB", b - g100);
		printf("mid hump: 1 kHz is %.1f dB above 100 Hz at drive 100\n", b - g100);
	}
	// 3) ограничение и выравнивание уровней; пик ограничен
	{
		Par p = Def(); p.dist = 70;
		const double l1 = Rms(Run(pl, R, p, Sine(R, 1000, -24, 0.5)), R, 0.25, 0.5);
		const double l2 = Rms(Run(pl, R, p, Sine(R, 1000, -6, 0.5)), R, 0.25, 0.5);
		CHECK(fabs(Db(l2) - Db(l1)) < 7, "level spread %.1f dB", Db(l2) - Db(l1));
		printf("clipping: output level changes %.1f dB for an 18 dB input change\n", Db(l2) - Db(l1));
		p.dist = 100; p.tone = 100;
		std::vector<float> o = Run(pl, R, p, Sine(R, 440, 0, 0.5));
		float pk = 0; for (size_t i = (size_t)(0.2 * R) * 2; i < o.size(); i++) pk = fmaxf(pk, fabsf(o[i]));
		CHECK(pk < 1.5f && pk > 0.3f, "peak %.2f", pk);
		printf("peak at full drive and full input: %.2f\n", pk);
	}
	// 4) симметричное ограничение: нечётные гармоники сильнее чётных
	{
		Par p = Def();
		std::vector<float> o = Run(pl, R, p, Sine(R, 440, -20, 1.0));
		const double f1 = Goertzel(o, R, 0.3, 0.9, 440), h2 = Goertzel(o, R, 0.3, 0.9, 880), h3 = Goertzel(o, R, 0.3, 0.9, 1320);
		CHECK(Db(h3) > Db(h2) + 12, "h2 %.1f h3 %.1f dB", Db(h2 / f1), Db(h3 / f1));
		printf("harmonics at drive 50: 2nd %.1f dB, 3rd %.1f dB\n", Db(h2 / f1), Db(h3 / f1));
	}
	// 5) тон: на 4 кГц при тоне 0 заметно тише, чем при тоне 100
	{
		Par a = Def(), b = Def(); a.tone = 0; b.tone = 100;
		std::vector<float> in = Sine(R, 4000, -30, 0.6);
		const double d = Db(Goertzel(Run(pl, R, a, in), R, 0.3, 0.6, 4000)) - Db(Goertzel(Run(pl, R, b, in), R, 0.3, 0.6, 4000));
		CHECK(d < -9, "tone span at 4 kHz %.1f dB", d);
		printf("tone 0 vs 100 at 4 kHz: %.1f dB\n", d);
	}
	// 6) уровень +6 дБ = x1.995; смесь линейна
	{
		Par a = Def(), b = Def(); b.level = 6;
		std::vector<float> in = Sine(R, 440, -20, 0.5);
		const double r = Rms(Run(pl, R, b, in), R, 0.25, 0.5) / Rms(Run(pl, R, a, in), R, 0.25, 0.5);
		CHECK(fabs(r - 1.9953) < 0.01, "level ratio %.4f", r);
		Par m0 = Def(), m100 = Def(), m50 = Def(); m0.mix = 0; m50.mix = 50;
		std::vector<float> o0 = Run(pl, R, m0, in), o100 = Run(pl, R, m100, in), o50 = Run(pl, R, m50, in);
		double d = 0; for (size_t i = 0; i < o0.size(); i++) d = fmax(d, fabs(o50[i] - 0.5f * (o0[i] + o100[i])));
		CHECK(d < 1e-5, "mix linearity %g", d);
	}
	// 7) алиасинг: 5 кГц, сильное ограничение; 9-я гармоника (45 кГц) складывается на 0,9 кГц, 11-я (55 кГц) - на 10,9 кГц
	{
		Par p = Def(); p.dist = 100; p.tone = 100;
		std::vector<float> in = Sine(R, 5000, -6, 1.0);
		std::vector<float> o = Run(pl, R, p, in);
		const double f1 = Goertzel(o, R, 0.3, 0.9, 5000);
		const double a1 = Goertzel(o, R, 0.3, 0.9, 900), a2 = Goertzel(o, R, 0.3, 0.9, 10900);
		CHECK(Db(a1 / f1) < -50 && Db(a2 / f1) < -50, "alias %.1f / %.1f dB", Db(a1 / f1), Db(a2 / f1));
		printf("aliasing: 0.9 kHz %.1f dB, 10.9 kHz %.1f dB below the 5 kHz tone\n", Db(a1 / f1), Db(a2 / f1));
	}
	// 8) другие частоты дискретизации: усиление малого сигнала 1 кГц то же
	{
		Par p = Def(); p.dist = 0; p.tone = 100;
		double g[3]; const float rs[3] = { 44100, 48000, 96000 };
		for (int i = 0; i < 3; i++) {
			std::vector<float> in = Sine(rs[i], 1000, -60, 0.6);
			g[i] = Db(Goertzel(Run(pl, rs[i], p, in), rs[i], 0.3, 0.6, 1000) / Goertzel(in, rs[i], 0.3, 0.6, 1000));
		}
		CHECK(fabs(g[0] - g[1]) < 0.5 && fabs(g[0] - g[2]) < 0.8, "rates %.2f %.2f %.2f", g[0], g[1], g[2]);
		printf("gain at 1 kHz for 44.1/48/96 kHz: %.2f / %.2f / %.2f dB\n", g[0], g[1], g[2]);
	}
	// 9) блоки, устойчивость
	{
		Par p = Def(); p.mix = 70;
		std::vector<float> in = Sine(R, 330, -10, 0.4);
		std::vector<float> a = Run(pl, R, p, in, 512), b = Run(pl, R, p, in, 29);
		double d = 0; for (size_t i = 0; i < a.size(); i++) d = fmax(d, fabs(a[i] - b[i]));
		CHECK(d < 1e-6, "block dependence %g", d);
		Par e = { 24, 100, 100, 12, 100 };
		std::vector<float> nz(R * 2 * 2); unsigned s = 3;
		for (size_t i = 0; i < nz.size(); i++) { s = s * 1664525u + 1013904223u; nz[i] = (int)(s >> 8) / 8388608.0f - 1.0f; }
		std::vector<float> o = Run(pl, R, e, nz);
		bool fin = true; float pk = 0; for (size_t i = 0; i < o.size(); i++) { if (!isfinite(o[i])) fin = false; else pk = fmaxf(pk, fabsf(o[i])); }
		CHECK(fin && pk < 100, "extreme finite %d peak %g", (int)fin, pk);
	}
	printf(fails ? "FAILED %d\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}
