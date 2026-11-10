// Проверка "Гейта / экспандера" на Linux через Ladspa::Runner.
#include "LadspaHost.h"
#include <stdio.h>
#include <math.h>
#include <string>
#include <vector>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
static const float RATE = 44100;

struct Par { float thr, ratio, range, atk, hold, rel, knee, hp; };
static Par Def() { Par p = { -30, 4, 80, 1, 0, 100, 0, 20 }; return p; }

static Ladspa::Plugin Find()
{
	std::vector<std::string> dirs; dirs.push_back("build");
	std::vector<Ladspa::Plugin> all = Ladspa::Scan(dirs);
	for (size_t i = 0; i < all.size(); i++) if (all[i].label == "beae_gate") return all[i];
	printf("FAIL: plugin not found\n"); fails++;
	return Ladspa::Plugin();
}
static std::vector<float> Run(const Ladspa::Plugin &pl, const Par &p, std::vector<float> in, int block = 512)
{
	Ladspa::Runner r; std::string err;
	if (!r.Start(pl, RATE, 2, &err)) { printf("FAIL start: %s\n", err.c_str()); fails++; return in; }
	const float v[8] = { p.thr, p.ratio, p.range, p.atk, p.hold, p.rel, p.knee, p.hp };
	for (int i = 0; i < 8; i++) r.SetControl(i, v[i]);
	const int frames = (int)(in.size() / 2);
	for (int pos = 0; pos < frames; pos += block) { int n = frames - pos < block ? frames - pos : block; r.Process(&in[pos * 2], n); }
	return in;
}
static std::vector<float> Buf(double sec) { return std::vector<float>((size_t)(sec * RATE) * 2, 0.0f); }
// синус в канал(ы): ch bit0 - левый, bit1 - правый
static void AddSine(std::vector<float> &b, double t0, double t1, double dbPeak, double f, int ch = 3)
{
	const double a = pow(10.0, dbPeak / 20.0);
	for (size_t i = (size_t)(t0 * RATE); i < (size_t)(t1 * RATE) && i < b.size() / 2; i++) {
		const float v = (float)(a * sin(2 * M_PI * f * i / RATE));
		if (ch & 1) b[i * 2] = v;
		if (ch & 2) b[i * 2 + 1] = v;
	}
}
static double LevelDb(const std::vector<float> &b, double t0, double t1, int ch = 0)
{
	double e = 0; size_t n = 0;
	for (size_t i = (size_t)(t0 * RATE); i < (size_t)(t1 * RATE) && i < b.size() / 2; i++) { e += (double)b[i * 2 + ch] * b[i * 2 + ch]; n++; }
	return 10 * log10(e / n + 1e-30) + 3.0103;
}
static double Att(const std::vector<float> &in, const std::vector<float> &out, double t0, double t1, int ch = 0)
{
	return LevelDb(in, t0, t1, ch) - LevelDb(out, t0, t1, ch);
}

int main()
{
	Ladspa::Plugin pl = Find();
	if (pl.label.empty()) return 1;

	// 1) выше порога без изменений; степень 1:1 - без изменений при любом уровне
	{
		Par p = Def();
		std::vector<float> in = Buf(1.0); AddSine(in, 0, 1, -15, 1000);
		CHECK(fabs(Att(in, Run(pl, p, in), 0.5, 1.0)) < 0.1, "above threshold changed");
		p.ratio = 1;
		std::vector<float> q = Buf(1.0); AddSine(q, 0, 1, -70, 1000);
		CHECK(fabs(Att(q, Run(pl, p, q), 0.5, 1.0)) < 0.1, "ratio 1 changed");
	}
	// 2) статическая кривая: порог -30, степень 3: вход -40 -> ослабление 20, вход -50 -> 40 дБ (наклон 3)
	{
		Par p = Def(); p.ratio = 3;
		double a[2]; const double lv[2] = { -40, -50 };
		for (int i = 0; i < 2; i++) {
			std::vector<float> in = Buf(2.0); AddSine(in, 0, 2, lv[i], 1000);
			a[i] = Att(in, Run(pl, p, in), 1.5, 2.0);
		}
		CHECK(fabs(a[0] - 20) < 2.0 && fabs(a[1] - 40) < 2.5, "expansion %.2f %.2f (want 20, 40)", a[0], a[1]);
		printf("expander 1:3, thr -30: att at -40 = %.2f (20), at -50 = %.2f (40)\n", a[0], a[1]);
	}
	// 3) глубина ограничивает ослабление: степень 10, глубина 20, вход -60 -> 20 дБ
	{
		Par p = Def(); p.ratio = 10; p.range = 20;
		std::vector<float> in = Buf(2.0); AddSine(in, 0, 2, -60, 1000);
		const double a = Att(in, Run(pl, p, in), 1.5, 2.0);
		CHECK(fabs(a - 20) < 0.5, "range %.2f", a);
		printf("range 20 dB: att %.2f\n", a);
	}
	// 4) мягкое колено (под порогом): на 3 дБ ниже порога ослабление заметно меньше, чем при жёстком колене; на пороге 0
	{
		Par p = Def(); p.ratio = 4; p.knee = 12;
		std::vector<float> in = Buf(2.0); AddSine(in, 0, 2, -33, 1000);	// 3 дБ ниже порога: внутри колена (12 дБ)
		const double soft = Att(in, Run(pl, p, in), 1.5, 2.0);
		p.knee = 0;
		const double hard = Att(in, Run(pl, p, in), 1.5, 2.0);
		CHECK(soft < hard - 3, "knee soft %.2f hard %.2f", soft, hard);
		printf("knee: att 3 dB below threshold soft %.2f, hard %.2f\n", soft, hard);
	}
	// 5) удержание: после громкого куска гейт ещё 100 мс открыт, потом закрывается
	{
		Par p = Def(); p.hold = 100; p.rel = 50; p.ratio = 10; p.range = 40;
		std::vector<float> in = Buf(2.0); AddSine(in, 0, 0.5, -15, 1000); AddSine(in, 0.5, 2.0, -60, 1000);
		std::vector<float> out = Run(pl, p, in);
		const double early = Att(in, out, 0.53, 0.58), late = Att(in, out, 0.9, 1.0);
		CHECK(early < 1.0 && late > 30, "hold early %.2f late %.2f", early, late);
		printf("hold 100 ms: att at +30..80 ms %.2f, at +400 ms %.1f\n", early, late);
		p.hold = 0;
		out = Run(pl, p, in);
		const double early0 = Att(in, out, 0.53, 0.58);
		CHECK(early0 > 6, "hold 0 should close quickly: %.2f", early0);
	}
	// 6) атака 20 мс: закрытый гейт открывается, 63 % открытия через 13..28 мс
	{
		Par p = Def(); p.atk = 20; p.ratio = 10; p.range = 40; p.rel = 50;
		std::vector<float> in = Buf(2.0); AddSine(in, 0, 1.0, -60, 1000); AddSine(in, 1.0, 2.0, -10, 1000);
		std::vector<float> out = Run(pl, p, in);
		const double a0 = Att(in, out, 0.8, 1.0);
		double t63 = -1;
		for (double t = 1.0; t < 1.3; t += 0.002) if (Att(in, out, t, t + 0.002) <= 0.37 * a0) { t63 = t - 1.0 + 0.001; break; }
		CHECK(a0 > 38 && t63 > 0.013 && t63 < 0.03, "attack: closed att %.1f, t63 %.1f ms", a0, t63 * 1000);
		printf("attack 20 ms: closed att %.1f dB, 63%% open at %.1f ms\n", a0, t63 * 1000);
	}
	// 7) возврат 100 мс: после ухода сигнала ослабление растёт до 63 % за 65..135 мс
	{
		Par p = Def(); p.rel = 100; p.ratio = 10; p.range = 40;
		std::vector<float> in = Buf(3.0); AddSine(in, 0, 1.0, -15, 1000); AddSine(in, 1.0, 3.0, -70, 1000);
		std::vector<float> out = Run(pl, p, in);
		const double aEnd = Att(in, out, 2.5, 3.0);
		double t63 = -1;
		for (double t = 1.0; t < 2.0; t += 0.004) if (Att(in, out, t, t + 0.004) >= 0.632 * aEnd) { t63 = t - 1.0 + 0.002; break; }
		CHECK(aEnd > 38 && t63 > 0.065 && t63 < 0.14, "release: closed att %.1f, t63 %.0f ms", aEnd, t63 * 1000);
		printf("release 100 ms: att %.1f dB, 63%% closed at %.0f ms\n", aEnd, t63 * 1000);
	}
	// 8) фильтр детектора: тон 40 Гц громкий, но при фильтре 400 Гц гейт его не слышит и закрывается
	{
		Par p = Def(); p.ratio = 10; p.range = 40; p.thr = -20;
		std::vector<float> in = Buf(2.0); AddSine(in, 0, 2, -10, 40);
		const double open = Att(in, Run(pl, p, in), 1.0, 2.0);
		p.hp = 400;
		const double closed = Att(in, Run(pl, p, in), 1.0, 2.0);
		CHECK(open < 0.5 && closed > 10, "detector HP: %.2f vs %.2f", open, closed);
		printf("detector HP 20 Hz: att %.2f, 400 Hz: att %.2f\n", open, closed);
	}
	// 9) связь каналов: громкий левый открывает и тихий правый
	{
		Par p = Def(); p.ratio = 10; p.range = 40;
		std::vector<float> in = Buf(2.0); AddSine(in, 0, 2, -10, 1000, 1); AddSine(in, 0, 2, -60, 1000, 2);
		std::vector<float> out = Run(pl, p, in);
		const double aR = Att(in, out, 1.5, 2.0, 1);
		CHECK(aR < 1.0, "stereo link right att %.2f", aR);
		printf("stereo link: right channel att %.2f\n", aR);
	}
	// 10) блоки и устойчивость
	{
		Par p = Def(); p.hold = 30; p.knee = 6; p.ratio = 5;
		std::vector<float> in = Buf(1.0); AddSine(in, 0, 0.3, -20, 800); AddSine(in, 0.5, 0.8, -50, 300);
		std::vector<float> a = Run(pl, p, in, 512), b = Run(pl, p, in, 29);
		double d = 0; for (size_t i = 0; i < a.size(); i++) d = fmax(d, fabs(a[i] - b[i]));
		CHECK(d < 1e-6, "block dependence %g", d);
		Par e = { 0, 10, 80, 0.1f, 500, 5, 12, 400 };
		std::vector<float> nz(in.size() * 4); unsigned s = 11;
		for (size_t i = 0; i < nz.size(); i++) { s = s * 1664525u + 1013904223u; nz[i] = (int)(s >> 8) / 8388608.0f - 1.0f; }
		std::vector<float> o = Run(pl, e, nz);
		bool fin = true; float pk = 0; for (size_t i = 0; i < o.size(); i++) { if (!isfinite(o[i])) fin = false; else pk = fmaxf(pk, fabsf(o[i])); }
		CHECK(fin && pk <= 1.0f, "extreme finite %d peak %g", (int)fin, pk);
	}
	printf(fails ? "FAILED %d\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}
