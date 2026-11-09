// Проверка "Оптического компрессора (LA-2A)" на Linux через Ladspa::Runner.
#include "LadspaHost.h"
#include <stdio.h>
#include <math.h>
#include <string>
#include <vector>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
static const float RATE = 44100;

struct Par { float red, gain, mode, tube, mix; };
static Par Def() { Par p = { 50.0f, 0.0f, 0.0f, 0.0f, 100.0f }; return p; }

static Ladspa::Plugin Find()
{
	std::vector<std::string> dirs; dirs.push_back("build");
	std::vector<Ladspa::Plugin> all = Ladspa::Scan(dirs);
	for (size_t i = 0; i < all.size(); i++) if (all[i].label == "beae_la2a") return all[i];
	printf("FAIL: plugin not found\n"); fails++;
	return Ladspa::Plugin();
}
static std::vector<float> Run(const Ladspa::Plugin &pl, const Par &p, std::vector<float> in, int block = 512)
{
	Ladspa::Runner r; std::string err;
	if (!r.Start(pl, RATE, 2, &err)) { printf("FAIL start: %s\n", err.c_str()); fails++; return in; }
	const float v[5] = { p.red, p.gain, p.mode, p.tube, p.mix };
	for (int i = 0; i < 5; i++) r.SetControl(i, v[i]);
	const int frames = (int)(in.size() / 2);
	for (int pos = 0; pos < frames; pos += block) { int n = frames - pos < block ? frames - pos : block; r.Process(&in[pos * 2], n); }
	return in;
}
static double Db(double x) { return 20 * log10(x + 1e-30); }
// синус: амплитуда по шкале dBFS (пик), нужный отрезок времени; правый канал может быть другим
static void AddSine(std::vector<float> &b, double t0, double t1, double dbPeak, double f, int ch = 3)
{
	const double a = pow(10.0, dbPeak / 20.0);
	for (size_t i = (size_t)(t0 * RATE); i < (size_t)(t1 * RATE) && i < b.size() / 2; i++) {
		const float v = (float)(a * sin(2 * M_PI * f * i / RATE));
		if (ch & 1) b[i * 2] = v;
		if (ch & 2) b[i * 2 + 1] = v;
	}
}
static std::vector<float> Buf(double sec) { return std::vector<float>((size_t)(sec * RATE) * 2, 0.0f); }
// RMS (в дБ по шкале пика синуса) канала ch на отрезке
static double LevelDb(const std::vector<float> &b, double t0, double t1, int ch = 0)
{
	double e = 0; size_t n = 0;
	for (size_t i = (size_t)(t0 * RATE); i < (size_t)(t1 * RATE) && i < b.size() / 2; i++) { e += (double)b[i * 2 + ch] * b[i * 2 + ch]; n++; }
	return 10 * log10(e / n + 1e-30) + 3.0103;
}
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

	// 1) без сжатия, лампа выключена: сигнал не меняется
	{
		Par p = Def(); p.red = 0;
		std::vector<float> in = Buf(1.0); AddSine(in, 0, 1, -20, 1000);
		std::vector<float> out = Run(pl, p, in);
		double d = LevelDb(out, 0.5, 1.0) - LevelDb(in, 0.5, 1.0);
		CHECK(fabs(d) < 0.1, "unity diff %.3f dB", d);
		printf("no reduction: %.3f dB\n", d);
	}
	// 2) статическая кривая: порог -20 дБ (сжатие 50), режим Compress 3:1: вход -10 -> сжатие 6.67 дБ, -4 -> 10.67 дБ
	{
		Par p = Def();
		double gr[2]; const double lv[2] = { -10, -4 };
		for (int i = 0; i < 2; i++) {
			std::vector<float> in = Buf(3.0); AddSine(in, 0, 3, lv[i], 1000);
			std::vector<float> out = Run(pl, p, in);
			gr[i] = LevelDb(in, 2.5, 3.0) - LevelDb(out, 2.5, 3.0);
		}
		CHECK(fabs(gr[0] - 6.67) < 0.7 && fabs(gr[1] - 10.67) < 0.7, "GR %.2f %.2f", gr[0], gr[1]);
		printf("compress 3:1: GR at -10 = %.2f (6.67), at -4 = %.2f (10.67)\n", gr[0], gr[1]);
		// режим Limit 10:1: -10 -> 9, -4 -> 14.4
		p.mode = 1;
		for (int i = 0; i < 2; i++) {
			std::vector<float> in = Buf(3.0); AddSine(in, 0, 3, lv[i], 1000);
			std::vector<float> out = Run(pl, p, in);
			gr[i] = LevelDb(in, 2.5, 3.0) - LevelDb(out, 2.5, 3.0);
		}
		CHECK(fabs(gr[0] - 9.0) < 0.7 && fabs(gr[1] - 14.4) < 0.7, "limit GR %.2f %.2f", gr[0], gr[1]);
		printf("limit 10:1: GR at -10 = %.2f (9.0), at -4 = %.2f (14.4)\n", gr[0], gr[1]);
	}
	// 3) атака: в первые 10 мс сжатие ещё мало, к 300 мс установилось
	{
		Par p = Def();
		std::vector<float> in = Buf(1.0); AddSine(in, 0.1, 1.0, -10, 1000);
		std::vector<float> out = Run(pl, p, in);
		const double early = LevelDb(in, 0.1, 0.11) - LevelDb(out, 0.1, 0.11);
		const double late = LevelDb(in, 0.8, 1.0) - LevelDb(out, 0.8, 1.0);
		CHECK(early < late - 3.0, "attack too fast: early %.2f late %.2f", early, late);
		CHECK(late > 6.0, "late GR %.2f", late);
		printf("attack: GR first 10 ms %.2f dB, steady %.2f dB\n", early, late);
	}
	// 4) возврат зависит от программы: после короткой вспышки быстро, после длинной медленно
	{
		Par p = Def();
		double grShort, grLong;
		{
			std::vector<float> in = Buf(2.0); AddSine(in, 0, 0.05, -10, 1000); AddSine(in, 0.05, 2.0, -45, 1000);
			std::vector<float> out = Run(pl, p, in);
			grShort = (LevelDb(in, 0.3, 0.4) - LevelDb(out, 0.3, 0.4));
		}
		{
			std::vector<float> in = Buf(6.0); AddSine(in, 0, 4.0, -10, 1000); AddSine(in, 4.0, 6.0, -45, 1000);
			std::vector<float> out = Run(pl, p, in);
			grLong = (LevelDb(in, 4.3, 4.4) - LevelDb(out, 4.3, 4.4));
		}
		CHECK(grShort < 1.0 && grLong > 3.0, "release short %.2f long %.2f", grShort, grLong);
		printf("release 300 ms after burst: short %.2f dB, long %.2f dB\n", grShort, grLong);
	}
	// 5) усиление +6 дБ
	{
		Par p = Def(); p.red = 0; p.gain = 6;
		std::vector<float> in = Buf(0.5); AddSine(in, 0, 0.5, -20, 1000);
		std::vector<float> out = Run(pl, p, in);
		const double d = LevelDb(out, 0.2, 0.5) - LevelDb(in, 0.2, 0.5);
		CHECK(fabs(d - 6.0) < 0.1, "makeup %.3f", d);
	}
	// 6) лампа: гармоники при -6 дБ и 100 %, и почти нет при 0 %; малый сигнал не искажается
	{
		Par p = Def(); p.red = 0; p.tube = 100;
		std::vector<float> in = Buf(1.0); AddSine(in, 0, 1, -6, 1000);
		std::vector<float> out = Run(pl, p, in);
		const double f1 = Goertzel(out, 0.3, 0.9, 1000), h2 = Goertzel(out, 0.3, 0.9, 2000), h3 = Goertzel(out, 0.3, 0.9, 3000);
		CHECK(h2 / f1 > 0.01 && h2 > h3, "tube h2 %.4f h3 %.4f of %.3f", h2, h3, f1);
		printf("tube 100%%: 2nd %.1f dB, 3rd %.1f dB below fundamental\n", Db(h2 / f1), Db(h3 / f1));
		p.tube = 0;
		out = Run(pl, p, in);
		const double h20 = Goertzel(out, 0.3, 0.9, 2000) / Goertzel(out, 0.3, 0.9, 1000);
		CHECK(h20 < 1e-4, "tube 0 harmonic %.6f", h20);
		p.tube = 100;
		std::vector<float> small = Buf(0.5); AddSine(small, 0, 0.5, -50, 1000);
		out = Run(pl, p, small);
		const double d = LevelDb(out, 0.2, 0.5) - LevelDb(small, 0.2, 0.5);
		CHECK(fabs(d) < 0.5, "small-signal gain %.3f", d);
		printf("tube small-signal gain %.3f dB\n", d);
	}
	// 7) связь каналов: громкий левый, тихий правый -> правый тоже сжимается
	{
		Par p = Def();
		std::vector<float> in = Buf(2.0); AddSine(in, 0, 2, -4, 1000, 1); AddSine(in, 0, 2, -30, 1000, 2);
		// AddSine с ch=2 перезаписал бы только правый, левый уже записан отдельно
		std::vector<float> out = Run(pl, p, in);
		const double grR = LevelDb(in, 1.5, 2.0, 1) - LevelDb(out, 1.5, 2.0, 1);
		const double grL = LevelDb(in, 1.5, 2.0, 0) - LevelDb(out, 1.5, 2.0, 0);
		CHECK(fabs(grR - grL) < 0.2 && grL > 6, "link L %.2f R %.2f", grL, grR);
		printf("stereo link: GR L %.2f R %.2f\n", grL, grR);
	}
	// 8) параллельно 0 % = без изменений; блоки не влияют; устойчивость
	{
		Par p = Def(); p.mix = 0; p.tube = 50;
		std::vector<float> in = Buf(0.5); AddSine(in, 0, 0.5, -3, 800);
		std::vector<float> out = Run(pl, p, in);
		double d = 0; for (size_t i = 0; i < in.size(); i++) d = fmax(d, fabs(in[i] - out[i]));
		CHECK(d < 1e-6, "mix 0 diff %g", d);
		p = Def(); p.tube = 40;
		std::vector<float> a = Run(pl, p, in, 512), b = Run(pl, p, in, 31);
		d = 0; for (size_t i = 0; i < a.size(); i++) d = fmax(d, fabs(a[i] - b[i]));
		CHECK(d < 1e-6, "block dependence %g", d);
		Par e = { 100.0f, 40.0f, 1.0f, 100.0f, 100.0f };
		std::vector<float> nz(in.size()); unsigned s = 5;
		for (size_t i = 0; i < nz.size(); i++) { s = s * 1664525u + 1013904223u; nz[i] = (int)(s >> 8) / 8388608.0f; }
		std::vector<float> o = Run(pl, e, nz);
		bool fin = true; float pk = 0; for (size_t i = 0; i < o.size(); i++) { if (!isfinite(o[i])) fin = false; else pk = fmaxf(pk, fabsf(o[i])); }
		CHECK(fin && pk < 200, "extreme finite %d peak %g", (int)fin, pk);
	}
	printf(fails ? "FAILED %d\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}
