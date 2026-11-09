// Проверка компрессора "Дистрессор" на Linux через Ladspa::Runner.
#include "LadspaHost.h"
#include <stdio.h>
#include <math.h>
#include <string>
#include <vector>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
static const float RATE = 44100;

struct Par { float in, thr, ratio, atk, rel, mode, hp, harm, amount, out, mix; };
static Par Def() { Par p = { 0, -20, 4, 2, 200, 0, 20, 0, 50, 0, 100 }; return p; }

static Ladspa::Plugin Find()
{
	std::vector<std::string> dirs; dirs.push_back("build");
	std::vector<Ladspa::Plugin> all = Ladspa::Scan(dirs);
	for (size_t i = 0; i < all.size(); i++) if (all[i].label == "beae_distressor") return all[i];
	printf("FAIL: plugin not found\n"); fails++;
	return Ladspa::Plugin();
}
static std::vector<float> Run(const Ladspa::Plugin &pl, const Par &p, std::vector<float> in, int block = 512)
{
	Ladspa::Runner r; std::string err;
	if (!r.Start(pl, RATE, 2, &err)) { printf("FAIL start: %s\n", err.c_str()); fails++; return in; }
	const float v[11] = { p.in, p.thr, p.ratio, p.atk, p.rel, p.mode, p.hp, p.harm, p.amount, p.out, p.mix };
	for (int i = 0; i < 11; i++) r.SetControl(i, v[i]);
	const int frames = (int)(in.size() / 2);
	for (int pos = 0; pos < frames; pos += block) { int n = frames - pos < block ? frames - pos : block; r.Process(&in[pos * 2], n); }
	return in;
}
static double Db(double x) { return 20 * log10(x + 1e-30); }
static void AddSine(std::vector<float> &b, double t0, double t1, double dbPeak, double f)
{
	const double a = pow(10.0, dbPeak / 20.0);
	for (size_t i = (size_t)(t0 * RATE); i < (size_t)(t1 * RATE) && i < b.size() / 2; i++)
		b[i * 2] = b[i * 2 + 1] = (float)(a * sin(2 * M_PI * f * i / RATE));
}
static std::vector<float> Buf(double sec) { return std::vector<float>((size_t)(sec * RATE) * 2, 0.0f); }
static double LevelDb(const std::vector<float> &b, double t0, double t1)
{
	double e = 0; size_t n = 0;
	for (size_t i = (size_t)(t0 * RATE); i < (size_t)(t1 * RATE) && i < b.size() / 2; i++) { e += (double)b[i * 2] * b[i * 2]; n++; }
	return 10 * log10(e / n + 1e-30) + 3.0103;
}
static double Gr(const std::vector<float> &in, const std::vector<float> &out, double t0, double t1) { return LevelDb(in, t0, t1) - LevelDb(out, t0, t1); }
static double Goertzel(const std::vector<float> &b, double t0, double t1, double f)
{
	size_t i0 = (size_t)(t0 * RATE), i1 = (size_t)(t1 * RATE);
	double c = 2 * cos(2 * M_PI * f / RATE), s1 = 0, s2 = 0;
	for (size_t i = i0; i < i1; i++) { double s = b[i * 2] + c * s1 - s2; s2 = s1; s1 = s; }
	return sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) * 2 / (i1 - i0);
}

int main()
{
	Ladspa::Plugin pl = Find();
	if (pl.label.empty()) return 1;

	// 1) параллельно 0 %: без изменений; степень 1:1: без сжатия
	{
		Par p = Def(); p.mix = 0; p.harm = 1;
		std::vector<float> in = Buf(0.5); AddSine(in, 0, 0.5, -3, 800);
		std::vector<float> out = Run(pl, p, in);
		double d = 0; for (size_t i = 0; i < in.size(); i++) d = fmax(d, fabs(in[i] - out[i]));
		CHECK(d < 1e-6, "mix 0 diff %g", d);
		p = Def(); p.ratio = 1;
		out = Run(pl, p, in);
		CHECK(fabs(Gr(in, out, 0.3, 0.5)) < 0.05, "ratio 1 GR %.3f", Gr(in, out, 0.3, 0.5));
	}
	// 2) статическая кривая: порог -20, вход -10: 4:1 -> 7.5 дБ, 10:1 -> 9, Nuke -> ~9.9; мягкий режим то же вне колена
	{
		struct { float ratio, mode, want; } c[4] = { { 4, 0, 7.5f }, { 10, 0, 9.0f }, { 4, 2, 9.9f }, { 4, 1, 7.5f } };
		for (int i = 0; i < 4; i++) {
			Par p = Def(); p.ratio = c[i].ratio; p.mode = c[i].mode;
			std::vector<float> in = Buf(3.0); AddSine(in, 0, 3, -10, 1000);
			std::vector<float> out = Run(pl, p, in);
			const double g = Gr(in, out, 2.5, 3.0);
			CHECK(fabs(g - c[i].want) < 0.8, "GR ratio %.0f mode %.0f: %.2f want %.2f", c[i].ratio, c[i].mode, g, c[i].want);
			printf("ratio %.0f mode %.0f: GR %.2f (want %.2f)\n", c[i].ratio, c[i].mode, g, c[i].want);
		}
	}
	// 3) атака 20 мс: 63 % сжатия достигается за 14..30 мс
	{
		Par p = Def(); p.thr = -30; p.ratio = 20; p.atk = 20;
		std::vector<float> in = Buf(2.0); AddSine(in, 0.1, 2.0, -10, 1000);
		std::vector<float> out = Run(pl, p, in);
		const double fin = Gr(in, out, 1.5, 2.0);
		double t63 = -1;
		for (double t = 0.1; t < 0.5; t += 0.002) if (Gr(in, out, t, t + 0.002) >= 0.632 * fin) { t63 = t - 0.1 + 0.001; break; }
		CHECK(t63 > 0.012 && t63 < 0.032, "attack t63 %.1f ms", t63 * 1000);
		printf("attack 20 ms: GR final %.1f dB, 63%% at %.1f ms\n", fin, t63 * 1000);
	}
	// 4) возврат 300 мс: сжатие падает до 37 % за 230..380 мс
	{
		Par p = Def(); p.thr = -30; p.ratio = 20; p.rel = 300;
		std::vector<float> in = Buf(3.0); AddSine(in, 0, 1.0, -10, 1000); AddSine(in, 1.0, 3.0, -60, 1000);
		std::vector<float> out = Run(pl, p, in);
		const double g0 = Gr(in, out, 0.9, 1.0);
		double t37 = -1;
		for (double t = 1.02; t < 2.5; t += 0.005) if (Gr(in, out, t, t + 0.005) <= 0.368 * g0) { t37 = t - 1.0 + 0.0025; break; }
		CHECK(t37 > 0.23 && t37 < 0.38, "release t37 %.0f ms (GR0 %.1f)", t37 * 1000, g0);
		printf("release 300 ms: GR %.1f dB, falls to 37%% at %.0f ms\n", g0, t37 * 1000);
	}
	// 5) гармоники
	{
		Par p = Def(); p.thr = 0; p.ratio = 1; p.amount = 100;
		std::vector<float> in = Buf(1.0); AddSine(in, 0, 1, -6, 1000);
		double h2[3], h3[3];
		for (int m = 0; m < 3; m++) {
			p.harm = (float)m;
			std::vector<float> out = Run(pl, p, in);
			const double f1 = Goertzel(out, 0.3, 0.9, 1000);
			h2[m] = Db(Goertzel(out, 0.3, 0.9, 2000) / f1); h3[m] = Db(Goertzel(out, 0.3, 0.9, 3000) / f1);
			printf("harmonics mode %d: 2nd %.1f dB, 3rd %.1f dB\n", m, h2[m], h3[m]);
		}
		CHECK(h2[0] < -80 && h3[0] < -80, "off: %.1f %.1f", h2[0], h3[0]);
		CHECK(h2[1] > -30 && h2[1] > h3[1] + 10, "even: %.1f %.1f", h2[1], h3[1]);
		CHECK(h3[2] > -40 && h3[2] > h2[2] + 10, "odd: %.1f %.1f", h2[2], h3[2]);
		Par q = p; q.harm = 1;
		std::vector<float> small = Buf(0.5); AddSine(small, 0, 0.5, -50, 1000);
		std::vector<float> o = Run(pl, q, small);
		CHECK(fabs(LevelDb(o, 0.2, 0.5) - LevelDb(small, 0.2, 0.5)) < 0.3, "small signal changed");
	}
	// 6) фильтр детектора: низкий тон 40 Гц при 400 Гц почти не сжимается
	{
		std::vector<float> in = Buf(2.0); AddSine(in, 0, 2, -10, 40);
		Par p = Def(); p.hp = 20;
		const double g20 = Gr(in, Run(pl, p, in), 1.0, 2.0);
		p.hp = 400;
		const double g400 = Gr(in, Run(pl, p, in), 1.0, 2.0);
		CHECK(g20 > 3.0 && g400 < 1.0, "detector HP: %.2f vs %.2f", g20, g400);
		printf("detector HP 20 Hz: GR %.2f, 400 Hz: GR %.2f\n", g20, g400);
	}
	// 7) вход и выход: +6 дБ на входе без сжатия и +6 дБ на выходе
	{
		Par p = Def(); p.ratio = 1; p.in = 6;
		std::vector<float> in = Buf(0.5); AddSine(in, 0, 0.5, -30, 1000);
		double d = LevelDb(Run(pl, p, in), 0.2, 0.5) - LevelDb(in, 0.2, 0.5);
		CHECK(fabs(d - 6) < 0.1, "input gain %.2f", d);
		p.in = 0; p.out = -6;
		d = LevelDb(Run(pl, p, in), 0.2, 0.5) - LevelDb(in, 0.2, 0.5);
		CHECK(fabs(d + 6) < 0.1, "output gain %.2f", d);
	}
	// 8) режим «Мягкий»: возврат зависит от программы; в обычном режиме - нет
	{
		Par p = Def(); p.rel = 100;
		double res[2][2];
		for (int m = 0; m < 2; m++) {
			p.mode = (float)m;
			std::vector<float> a = Buf(2.0); AddSine(a, 0, 0.05, -10, 1000); AddSine(a, 0.05, 2.0, -50, 1000);
			res[m][0] = Gr(a, Run(pl, p, a), 0.45, 0.5);
			std::vector<float> b = Buf(6.0); AddSine(b, 0, 4.0, -10, 1000); AddSine(b, 4.0, 6.0, -50, 1000);
			res[m][1] = Gr(b, Run(pl, p, b), 4.4, 4.45);
			printf("mode %d: GR 400 ms after short burst %.2f, after long %.2f\n", m, res[m][0], res[m][1]);
		}
		CHECK(res[1][0] < 1.0 && res[1][1] > 2.5, "soft mode %.2f %.2f", res[1][0], res[1][1]);
		CHECK(res[0][1] < 1.0, "normal mode long %.2f", res[0][1]);
	}
	// 9) блоки и устойчивость
	{
		Par p = Def(); p.harm = 1; p.mode = 1;
		std::vector<float> in = Buf(0.5); AddSine(in, 0, 0.5, -3, 800);
		std::vector<float> a = Run(pl, p, in, 512), b = Run(pl, p, in, 31);
		double d = 0; for (size_t i = 0; i < a.size(); i++) d = fmax(d, fabs(a[i] - b[i]));
		CHECK(d < 1e-6, "block dependence %g", d);
		Par e = { 30, -60, 20, 0.05f, 20, 2, 400, 2, 100, 30, 100 };
		std::vector<float> nz(in.size() * 8); unsigned s = 9;
		for (size_t i = 0; i < nz.size(); i++) { s = s * 1664525u + 1013904223u; nz[i] = (int)(s >> 8) / 8388608.0f; }
		std::vector<float> o = Run(pl, e, nz);
		bool fin = true; for (size_t i = 0; i < o.size(); i++) if (!isfinite(o[i])) fin = false;
		CHECK(fin, "extreme settings not finite");
	}
	printf(fails ? "FAILED %d\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}
