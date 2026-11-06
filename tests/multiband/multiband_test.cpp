// Проверка многополосного компрессора на Linux: .so грузится через хост BeAE.
#include "LadspaHost.h"
#include "../../plugins/multiband/Multiband.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string>
#include <vector>

using namespace Multiband;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static const float kRate = 44100.0f;
static const double kPi = 3.14159265358979;
static Ladspa::Plugin gP;

static double Db(double x) { return 20 * log10(x + 1e-20); }
static double FromDb(double d) { return pow(10.0, d / 20); }

static std::vector<float> Defaults()
{
	std::vector<float> c(P_IN_L, 0.0f);
	for (int i = 0; i < P_IN_L; i++) c[i] = Ladspa::ResolveRange(gP.ports[i], kRate).def;
	return c;
}
static void SetBand(std::vector<float> &c, int k, float thr, float ratio, float att, float rel, float gain)
{
	const int b = P_BAND0 + k * BP_COUNT;
	c[b + BP_THRESHOLD] = thr; c[b + BP_RATIO] = ratio; c[b + BP_ATTACK] = att; c[b + BP_RELEASE] = rel; c[b + BP_GAIN] = gain;
}
// все полосы без сжатия
static std::vector<float> Flat()
{
	std::vector<float> c = Defaults();
	for (int k = 0; k < kBands; k++) SetBand(c, k, 0, 1, 10, 100, 0);
	return c;
}

static std::vector<float> Process(const std::vector<float> &in, const std::vector<float> &ctl, int block = 512)
{
	Ladspa::Runner r; std::string err;
	if (!r.Start(gP, kRate, 2, &err)) { printf("FAIL start %s\n", err.c_str()); fails++; return in; }
	for (size_t i = 0; i < ctl.size(); i++) r.SetControl((int)i, ctl[i]);
	std::vector<float> buf(in.size() * 2);
	for (size_t i = 0; i < in.size(); i++) buf[i * 2] = buf[i * 2 + 1] = in[i];
	for (size_t pos = 0; pos < in.size(); pos += block) {
		size_t n = in.size() - pos < (size_t)block ? in.size() - pos : block;
		r.Process(&buf[pos * 2], n);
	}
	std::vector<float> out(in.size());
	for (size_t i = 0; i < in.size(); i++) out[i] = buf[i * 2];
	return out;
}

static std::vector<float> Sine(double freq, double ampDb, double sec)
{
	std::vector<float> v((int)(kRate * sec));
	for (size_t i = 0; i < v.size(); i++) v[i] = (float)(FromDb(ampDb) * sin(2 * kPi * freq * i / kRate));
	return v;
}
static void Add(std::vector<float> &a, const std::vector<float> &b) { for (size_t i = 0; i < a.size() && i < b.size(); i++) a[i] += b[i]; }

// амплитуда синуса на частоте freq в окне [a, b) секунд (Гёрцель), дБ
static double Level(const std::vector<float> &x, double freq, double a, double b)
{
	const int i0 = (int)(a * kRate), i1 = (int)(b * kRate);
	double re = 0, im = 0;
	for (int i = i0; i < i1 && i < (int)x.size(); i++) {
		const double w = 2 * kPi * freq * i / kRate;
		re += x[i] * cos(w); im += x[i] * sin(w);
	}
	return Db(2 * sqrt(re * re + im * im) / (i1 - i0));
}

int main()
{
	std::vector<std::string> dirs; dirs.push_back("build");
	std::vector<Ladspa::Plugin> all = Ladspa::Scan(dirs);
	bool found = false;
	for (size_t i = 0; i < all.size(); i++) if (all[i].label == "beae_multiband") { gP = all[i]; found = true; }
	CHECK(found, "plugin not found");
	if (!found) return 1;
	CHECK(gP.latencyPort == -1, "no latency expected");
	CHECK(gP.ControlInputs().size() == P_IN_L, "control count %d", (int)gP.ControlInputs().size());

	// ---- значения по умолчанию ----
	{
		std::vector<float> d = Defaults();
		for (int k = 0; k < kBands; k++) {
			const int b = P_BAND0 + k * BP_COUNT;
			CHECK(fabs(d[b + BP_THRESHOLD] + 15) < 0.1, "default threshold %f", d[b + BP_THRESHOLD]);
			CHECK(fabs(d[b + BP_RATIO] - 2.1) < 0.1, "default ratio %f", d[b + BP_RATIO]);
			CHECK(fabs(d[b + BP_ATTACK] - 10) < 0.3, "default attack %f", d[b + BP_ATTACK]);
			CHECK(fabs(d[b + BP_RELEASE] - 141) < 3, "default release %f", d[b + BP_RELEASE]);
			CHECK(fabs(d[b + BP_GAIN]) < 0.01, "default gain %f", d[b + BP_GAIN]);
		}
		CHECK(fabs(d[P_X1] - 173) < 4 && fabs(d[P_X2] - 1095) < 20 && fabs(d[P_X3] - 5657) < 100, "default crossovers %f %f %f", d[P_X1], d[P_X2], d[P_X3]);
		CHECK(fabs(d[P_KNEE] - 6) < 0.01 && fabs(d[P_OUTPUT]) < 0.01 && d[P_SOLO] == 0, "default knee/out/solo");
	}

	// ---- прозрачность без сжатия, в том числе при крайних границах ----
	{
		const float xs[4][3] = { { 173, 1095, 5657 }, { 60, 300, 2000 }, { 500, 4000, 16000 }, { 500, 300, 2000 } };
		const double freqs[] = { 40, 100, 250, 600, 1500, 3000, 8000, 15000 };
		for (int e = 0; e < 4; e++) {
			std::vector<float> c = Flat(); c[P_X1] = xs[e][0]; c[P_X2] = xs[e][1]; c[P_X3] = xs[e][2];
			for (size_t k = 0; k < sizeof(freqs) / sizeof(freqs[0]); k++) {
				std::vector<float> in = Sine(freqs[k], -10, 1.0), out = Process(in, c);
				double d = Level(out, freqs[k], 0.4, 0.95) - Level(in, freqs[k], 0.4, 0.95);
				CHECK(fabs(d) < 0.4, "set %d: transparency %g Hz: %.2f dB", e, freqs[k], d);
			}
		}
	}

	// ---- статическая кривая: порог -20, степень 4, колено 0, тон -8 дБ в полосе 1 ----
	{
		std::vector<float> c = Flat(); c[P_KNEE] = 0;
		SetBand(c, 1, -20, 4, 5, 100, 0);
		std::vector<float> in = Sine(435, -8, 1.5), out = Process(in, c);
		double d = Level(out, 435, 1.0, 1.4) - Level(in, 435, 1.0, 1.4);
		printf("static: over 12 dB, ratio 4 -> %.2f dB (ideal -9)\n", d);
		CHECK(d < -7.5 && d > -10.5, "static reduction %.2f", d);

		// тот же тон тише порога не трогается
		std::vector<float> in2 = Sine(435, -30, 1.5), out2 = Process(in2, c);
		double d2 = Level(out2, 435, 1.0, 1.4) - Level(in2, 435, 1.0, 1.4);
		CHECK(fabs(d2) < 0.3, "below threshold changed %.2f dB", d2);
	}

	// ---- полосы независимы: громкие низы не давят тихую середину ----
	{
		std::vector<float> c = Flat(); c[P_KNEE] = 0;
		SetBand(c, 0, -30, 10, 5, 100, 0);
		std::vector<float> in = Sine(100, -3, 2.0); Add(in, Sine(3000, -30, 2.0));
		std::vector<float> out = Process(in, c);
		double dl = Level(out, 100, 1.2, 1.9) - Level(in, 100, 1.2, 1.9);
		double dh = Level(out, 3000, 1.2, 1.9) - Level(in, 3000, 1.2, 1.9);
		printf("independence: 100 Hz %.1f dB, 3 kHz %.2f dB\n", dl, dh);
		CHECK(dl < -15, "low band should be compressed: %.1f", dl);
		CHECK(fabs(dh) < 0.5, "high band touched: %.2f", dh);
	}

	// ---- атака ----
	{
		std::vector<float> in = Sine(435, -40, 0.3); Add(in, std::vector<float>());
		std::vector<float> loud = Sine(435, -8, 1.0);
		in.insert(in.end(), loud.begin(), loud.end());
		double r20[2];
		for (int a = 0; a < 2; a++) {
			std::vector<float> c = Flat(); c[P_KNEE] = 0;
			SetBand(c, 1, -20, 8, a ? 100.0f : 1.0f, 100, 0);
			std::vector<float> out = Process(in, c);
			r20[a] = Level(out, 435, 0.3 + 0.03, 0.3 + 0.06) - Level(in, 435, 0.3 + 0.03, 0.3 + 0.06);
		}
		printf("attack: reduction at 30-60 ms after step: 1 ms -> %.1f dB, 100 ms -> %.1f dB\n", r20[0], r20[1]);
		CHECK(r20[0] < r20[1] - 3, "fast attack should reduce more early");
	}

	// ---- спад ----
	{
		std::vector<float> in = Sine(435, -8, 0.8), quiet = Sine(435, -40, 1.5);
		in.insert(in.end(), quiet.begin(), quiet.end());
		double rec[2];
		for (int r = 0; r < 2; r++) {
			std::vector<float> c = Flat(); c[P_KNEE] = 0;
			SetBand(c, 1, -20, 8, 2, r ? 1000.0f : 20.0f, 0);
			std::vector<float> out = Process(in, c);
			rec[r] = Level(out, 435, 0.8 + 0.25, 0.8 + 0.30) - Level(in, 435, 0.8 + 0.25, 0.8 + 0.30);
		}
		printf("release: remaining reduction 250 ms after drop: 20 ms -> %.1f dB, 1000 ms -> %.1f dB\n", rec[0], rec[1]);
		CHECK(rec[0] > -1.0, "fast release should have recovered: %.1f", rec[0]);
		CHECK(rec[1] < -4.0, "slow release should still hold: %.1f", rec[1]);
	}

	// ---- мягкое колено ----
	{
		double d[2];
		for (int k = 0; k < 2; k++) {
			std::vector<float> c = Flat(); c[P_KNEE] = k ? 12.0f : 0.0f;
			SetBand(c, 1, -20, 4, 5, 100, 0);
			std::vector<float> in = Sine(435, -20.4, 1.5), out = Process(in, c);		// около порога с учётом потерь в полосе
			d[k] = Level(out, 435, 1.0, 1.4) - Level(in, 435, 1.0, 1.4);
		}
		printf("knee at threshold: hard %.2f dB, soft %.2f dB\n", d[0], d[1]);
		CHECK(d[1] < d[0] - 0.5, "soft knee should compress earlier");
	}

	// ---- усиление полосы, выход ----
	{
		std::vector<float> c = Flat(); SetBand(c, 1, 0, 1, 10, 100, 6);
		std::vector<float> in = Sine(435, -20, 1.0), out = Process(in, c);
		double d = Level(out, 435, 0.4, 0.95) - Level(in, 435, 0.4, 0.95);
		CHECK(fabs(d - 6) < 0.5, "band gain +6: %.2f", d);
		std::vector<float> c2 = Flat(); c2[P_OUTPUT] = -6;
		std::vector<float> o2 = Process(in, c2);
		double d2 = Level(o2, 435, 0.4, 0.95) - Level(in, 435, 0.4, 0.95);
		CHECK(fabs(d2 + 6) < 0.4, "output -6: %.2f", d2);
	}

	// ---- соло полосы ----
	{
		std::vector<float> in = Sine(100, -10, 1.0); Add(in, Sine(435, -10, 1.0)); Add(in, Sine(3000, -10, 1.0)); Add(in, Sine(10000, -10, 1.0));
		const double fr[4] = { 100, 435, 3000, 10000 };
		for (int s = 1; s <= 4; s++) {
			std::vector<float> c = Flat(); c[P_SOLO] = (float)s;
			std::vector<float> out = Process(in, c);
			for (int k = 0; k < 4; k++) {
				double d = Level(out, fr[k], 0.4, 0.95) - Level(in, fr[k], 0.4, 0.95);
				if (k == s - 1) CHECK(fabs(d) < 1.5, "solo %d: own tone %.2f dB", s, d);
				else CHECK(d < -15, "solo %d: tone %g leaks %.1f dB", s, fr[k], d);
			}
		}
	}

	// ---- устойчивость: громкий шум, нули, пустые блоки ----
	{
		std::vector<float> in((int)(kRate * 2));
		for (size_t i = 0; i < in.size(); i++) in[i] = (float)rand() / RAND_MAX * 2 - 1;
		std::vector<float> c = Defaults(); c[P_OUTPUT] = 12; for (int k = 0; k < kBands; k++) c[P_BAND0 + k * BP_COUNT + BP_GAIN] = 12;
		std::vector<float> out = Process(in, c, 37);
		bool ok = true; for (size_t i = 0; i < out.size(); i++) if (!(fabs(out[i]) < 1000)) ok = false;
		CHECK(ok, "output finite");
		std::vector<float> z(1000, 0.0f), oz = Process(z, Defaults());
		bool zero = true; for (size_t i = 0; i < oz.size(); i++) if (oz[i] != 0) zero = false;
		CHECK(zero, "silence in -> silence out");
	}

	printf(fails ? "FAILED %d\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}
