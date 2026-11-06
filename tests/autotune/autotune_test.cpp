// Проверка плагина "Автотюн" на Linux: .so грузится через хост BeAE.
#include "LadspaHost.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string>
#include <vector>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static const float kPi = 3.14159265f;
static Ladspa::Plugin gP;
enum { KEY, SCALE, SPEED, AMOUNT, REF, FMIN, FMAX, GATE };

static std::vector<float> Defaults(float rate)
{
	std::vector<float> c(8);
	for (int i = 0; i < 8; i++) c[i] = Ladspa::ResolveRange(gP.ports[i], rate).def;
	return c;
}

// "голос": 8 гармоник с убывающей амплитудой, частота задаётся функцией от времени
template <class F>
static std::vector<float> Voice(float rate, double seconds, F freqAt, float amp = 0.3f)
{
	const int N = (int)(rate * seconds);
	std::vector<float> v(N);
	double ph = 0;
	for (int i = 0; i < N; i++) {
		ph += 2 * M_PI * freqAt(i / rate) / rate;
		double s = 0;
		for (int h = 1; h <= 8; h++) s += sin(h * ph) / h;
		v[i] = (float)(amp * s * 0.6);
	}
	return v;
}

static std::vector<float> Run(const std::vector<float> &ctl, const std::vector<float> &in, float rate, int block = 512)
{
	Ladspa::Runner r; std::string err;
	if (!r.Start(gP, rate, 2, &err)) { printf("FAIL start %s\n", err.c_str()); fails++; return in; }
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

// высота по автокорреляции на отрезке [a, b) секунд; возвращает частоту и периодичность (0..1)
static double Pitch(const std::vector<float> &v, float rate, double a, double b, double *periodicity = 0)
{
	const int s = (int)(a * rate), e = (int)(b * rate);
	const int lagMin = (int)(rate / 700), lagMax = (int)(rate / 70);
	double r0 = 0; for (int i = s; i < e - lagMax; i++) r0 += v[i] * v[i];
	std::vector<double> r(lagMax + 2, 0.0);
	for (int lag = lagMin; lag <= lagMax; lag++) {
		double sum = 0; for (int i = s; i < e - lagMax; i++) sum += v[i] * v[i + lag];
		r[lag] = sum;
	}
	int best = lagMin; for (int lag = lagMin; lag <= lagMax; lag++) if (r[lag] > r[best]) best = lag;
	// первый заметный максимум (против октавных ошибок берём наименьший лаг с r > 0.95 * max)
	for (int lag = lagMin; lag <= lagMax; lag++) if (r[lag] > 0.95 * r[best] && r[lag] >= r[lag - 1] && r[lag] >= r[lag + 1]) { best = lag; break; }
	double lagF = best;
	if (best > lagMin && best < lagMax) {
		double A = r[best - 1], B = r[best], C = r[best + 1], den = A - 2 * B + C;
		if (den != 0) lagF += 0.5 * (A - C) / den;
	}
	if (periodicity) *periodicity = r0 > 0 ? r[best] / r0 : 0;
	return rate / lagF;
}
static double Cents(double f, double ref) { return 1200 * log2(f / ref); }

int main()
{
	srand(3);
	std::vector<std::string> dirs; dirs.push_back("build");
	std::vector<Ladspa::Plugin> all = Ladspa::Scan(dirs);
	bool found = false;
	for (size_t i = 0; i < all.size(); i++) if (all[i].label == "beae_autotune") { gP = all[i]; found = true; }
	CHECK(found, "plugin not found");
	if (!found) return 1;
	const float R = 44100.0f;
	const int L = (int)(0.05f * R);

	// порт задержки и значения по умолчанию
	CHECK(gP.latencyPort >= 0, "latency port");
	CHECK(gP.ControlInputs().size() == 8, "controls %d", (int)gP.ControlInputs().size());
	{
		const float want[8] = { 0, 0, 100, 100, 440, 80, 750, -50 };
		std::vector<float> d = Defaults(R);
		for (int i = 0; i < 8; i++) CHECK(fabsf(d[i] - want[i]) < 0.01f * fabsf(want[i]) + 0.01f, "default %d: %f vs %f", i, d[i], want[i]);
	}
	{
		Ladspa::Runner r; std::string err; r.Start(gP, R, 2, &err);
		CHECK(r.Latency() == L, "latency %d vs %d", r.Latency(), L);
	}

	const double C4 = 261.6256, A4 = 440.0, D4 = 293.6648, E4 = 329.6276;

	// ---- степень 0: звук = исходный, задержанный на latency ----
	{
		std::vector<float> c = Defaults(R); c[AMOUNT] = 0;
		std::vector<float> in = Voice(R, 1.5, [&](double) { return C4 * 1.02; });
		std::vector<float> out = Run(c, in, R);
		double md = 0; for (size_t i = 0; i + L < in.size(); i++) md = fmax(md, fabs(out[i + L] - in[i]));
		CHECK(md < 1e-6, "amount 0 not transparent: %g", md);
	}

	// ---- хроматика, мгновенная коррекция: ноты подтягиваются ----
	struct Case { const char *name; double fIn, fWant; int key, scale; };
	const Case cases[] = {
		{ "C4 +35c, chromatic",  C4 * pow(2, 35 / 1200.0),  C4, 0, 0 },
		{ "A4 -30c, chromatic",  A4 * pow(2, -30 / 1200.0), A4, 0, 0 },
		{ "E4 +25c, chromatic",  E4 * pow(2, 25 / 1200.0),  E4, 0, 0 },
		{ "C#4 +10c, C major",   277.1826 * pow(2, 10 / 1200.0), D4, 0, 1 },
	};
	for (size_t k = 0; k < sizeof(cases) / sizeof(cases[0]); k++) {
		const Case &cs = cases[k];
		std::vector<float> c = Defaults(R); c[SPEED] = 0; c[KEY] = (float)cs.key; c[SCALE] = (float)cs.scale;
		std::vector<float> in = Voice(R, 2.0, [&](double) { return cs.fIn; });
		for (int block = 256; block <= 4096; block *= 4) {
			std::vector<float> out = Run(c, in, R, block);
			double per; double f = Pitch(out, R, 1.2, 1.9, &per);
			double inCents = Cents(Pitch(in, R, 1.2, 1.9), cs.fWant), outCents = Cents(f, cs.fWant);
			printf("%s (block %d): input %+.1f c -> output %+.1f c, periodicity %.2f\n", cs.name, block, inCents, outCents, per);
			CHECK(fabs(outCents) < 6.0, "%s: pitch error %.1f cents", cs.name, outCents);
			CHECK(per > 0.85, "%s: periodicity %.2f", cs.name, per);
		}
	}

	// ---- мелодия: две ноты подряд, обе подтягиваются ----
	{
		std::vector<float> c = Defaults(R); c[SPEED] = 0;
		const double f1 = C4 * pow(2, 30 / 1200.0), f2 = E4 * pow(2, -28 / 1200.0);
		std::vector<float> in = Voice(R, 4.0, [&](double t) { return t < 2.0 ? f1 : f2; });
		std::vector<float> out = Run(c, in, R);
		double c1 = Cents(Pitch(out, R, 1.0, 1.7), C4), c2 = Cents(Pitch(out, R, 3.0, 3.7), E4);
		printf("melody: %+.1f c, %+.1f c\n", c1, c2);
		CHECK(fabs(c1) < 6 && fabs(c2) < 6, "melody errors %.1f / %.1f", c1, c2);
	}

	// ---- медленная коррекция: за 150 мс ещё не дошла до ноты, потом доходит ----
	{
		std::vector<float> c = Defaults(R); c[SPEED] = 400;
		const double fIn = C4 * pow(2, 40 / 1200.0);
		std::vector<float> in = Voice(R, 4.0, [&](double) { return fIn; });
		std::vector<float> out = Run(c, in, R);
		double early = Cents(Pitch(out, R, 0.3, 0.45), C4), late = Cents(Pitch(out, R, 3.2, 3.9), C4);
		printf("slow retune: early %+.1f c, late %+.1f c\n", early, late);
		CHECK(early > 8.0, "slow retune too fast: %.1f c", early);
		CHECK(fabs(late) < 6.0, "slow retune did not arrive: %.1f c", late);
	}

	// ---- степень 50%: половина пути ----
	{
		std::vector<float> c = Defaults(R); c[SPEED] = 0; c[AMOUNT] = 50;
		const double fIn = C4 * pow(2, 40 / 1200.0);
		std::vector<float> in = Voice(R, 2.0, [&](double) { return fIn; });
		std::vector<float> out = Run(c, in, R);
		double e = Cents(Pitch(out, R, 1.2, 1.9), C4);
		CHECK(fabs(e - 20.0) < 6.0, "amount 50: %.1f c (want ~20)", e);
	}

	// ---- 48 кГц ----
	{
		const float R2 = 48000.0f;
		std::vector<float> c = Defaults(R2); c[SPEED] = 0;
		const double fIn = A4 * pow(2, 33 / 1200.0);
		std::vector<float> in = Voice(R2, 2.0, [&](double) { return fIn; });
		std::vector<float> out = Run(c, in, R2);
		double e = Cents(Pitch(out, R2, 1.2, 1.9), A4);
		CHECK(fabs(e) < 6.0, "48 kHz: %.1f c", e);
	}

	// ---- шум и тишина: проходят, всё конечно ----
	{
		std::vector<float> c = Defaults(R);
		std::vector<float> noise(R * 2);
		for (size_t i = 0; i < noise.size(); i++) noise[i] = 0.2f * ((float)rand() / RAND_MAX - 0.5f);
		std::vector<float> out = Run(c, noise, R);
		bool ok = true; double ei = 0, eo = 0;
		for (size_t i = L; i < noise.size(); i++) { if (!isfinite(out[i])) ok = false; ei += noise[i - L] * noise[i - L]; eo += out[i] * out[i]; }
		CHECK(ok, "noise finite");
		CHECK(fabs(10 * log10(eo / ei)) < 2.0, "noise level changed %.1f dB", 10 * log10(eo / ei));
		std::vector<float> sil(R, 0.0f);
		std::vector<float> o2 = Run(c, sil, R);
		double mx = 0; for (size_t i = 0; i < o2.size(); i++) mx = fmax(mx, fabs(o2[i]));
		CHECK(mx == 0.0, "silence not silent: %g", mx);
	}

	// ---- голос с вибрато, крайние настройки: без сбоев ----
	{
		for (int ext = 0; ext < 2; ext++) {
			std::vector<float> c = Defaults(R);
			if (ext) { c[KEY] = 11; c[SCALE] = 5; c[SPEED] = 0; c[FMIN] = 60; c[FMAX] = 900; c[GATE] = -70; c[REF] = 466; }
			std::vector<float> in = Voice(R, 3.0, [&](double t) { return 220.0 * (1 + 0.03 * sin(2 * M_PI * 5.5 * t)); }, 0.4f);
			std::vector<float> out = Run(c, in, R, 333);
			bool ok = true; double pk = 0;
			for (size_t i = 0; i < out.size(); i++) { if (!isfinite(out[i])) ok = false; pk = fmax(pk, fabs(out[i])); }
			CHECK(ok && pk < 2.5, "vibrato/extreme %d: ok=%d peak=%g", ext, ok, pk);
		}
	}

	printf(fails ? "FAILED %d\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}
