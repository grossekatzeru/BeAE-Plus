// Проверка эквалайзеров (графического на 6/12/18 и параметрического на 3/5 полос) на Linux.
#include "LadspaHost.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <map>
#include <string>
#include <vector>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static const float kRate = 44100.0f;
static const float kPi = 3.14159265f;
static std::map<std::string, Ladspa::Plugin> gPlugins;

static std::vector<float> Defaults(const Ladspa::Plugin &p)
{
	std::vector<float> c;
	std::vector<int> ci = p.ControlInputs();
	for (size_t i = 0; i < ci.size(); i++) c.push_back(Ladspa::ResolveRange(p.ports[ci[i]], kRate).def);
	return c;
}

static std::vector<float> Run(const Ladspa::Plugin &p, const std::vector<float> &ctl, const std::vector<float> &in, int channels = 2, int block = 777)
{
	Ladspa::Runner r; std::string err;
	if (!r.Start(p, kRate, channels, &err)) { printf("FAIL start %s\n", err.c_str()); fails++; return in; }
	for (size_t i = 0; i < ctl.size(); i++) r.SetControl((int)i, ctl[i]);
	std::vector<float> buf(in.size() * channels);
	for (size_t i = 0; i < in.size(); i++) for (int c = 0; c < channels; c++) buf[i * channels + c] = in[i];
	for (size_t pos = 0; pos < in.size(); pos += block) {
		size_t n = in.size() - pos < (size_t)block ? in.size() - pos : block;
		r.Process(&buf[pos * channels], n);
	}
	std::vector<float> out(in.size());
	for (size_t i = 0; i < in.size(); i++) out[i] = buf[i * channels];
	return out;
}

// усиление (дБ) на частоте f: синус через плагин, сравнение амплитуд после установления
static double Gain(const Ladspa::Plugin &p, const std::vector<float> &ctl, double f)
{
	const int N = (int)(kRate * 1.0);
	std::vector<float> in(N);
	for (int i = 0; i < N; i++) in[i] = 0.25f * sinf(2 * kPi * (float)f * i / kRate);
	std::vector<float> out = Run(p, ctl, in);
	double a = 0, b = 0;
	for (int i = N / 2; i < N; i++) { a += in[i] * in[i]; b += out[i] * out[i]; }
	return 10 * log10(b / a);
}

static const Ladspa::Plugin &Get(const char *label)
{
	std::map<std::string, Ladspa::Plugin>::iterator it = gPlugins.find(label);
	if (it == gPlugins.end()) { printf("FAIL: plugin %s not found\n", label); fails++; static Ladspa::Plugin none; return none; }
	return it->second;
}

int main()
{
	std::vector<std::string> dirs; dirs.push_back("build");
	std::vector<Ladspa::Plugin> all = Ladspa::Scan(dirs);
	for (size_t i = 0; i < all.size(); i++) gPlugins[all[i].label] = all[i];
	CHECK(gPlugins.size() == 5, "expected 5 plugins, found %d", (int)gPlugins.size());

	// ---------- графический ----------
	const struct { const char *label; int bands; } gv[3] = { { "beae_graphic_eq_6", 6 }, { "beae_graphic_eq_12", 12 }, { "beae_graphic_eq_18", 18 } };
	for (int v = 0; v < 3; v++) {
		const Ladspa::Plugin &p = Get(gv[v].label);
		if (p.label.empty()) continue;
		const int B = gv[v].bands;
		std::vector<float> def = Defaults(p);
		CHECK((int)def.size() == B + 1, "%s: controls %d", gv[v].label, (int)def.size());
		for (size_t i = 0; i < def.size(); i++) CHECK(def[i] == 0.0f, "%s: default %d = %f", gv[v].label, (int)i, def[i]);

		// все нули: звук проходит без изменений
		std::vector<float> in(20000);
		for (size_t i = 0; i < in.size(); i++) in[i] = 0.3f * sinf(0.05f * i) + 0.2f * sinf(0.7f * i);
		std::vector<float> out = Run(p, def, in);
		double md = 0; for (size_t i = 0; i < in.size(); i++) md = fmax(md, fabs(out[i] - in[i]));
		CHECK(md == 0.0, "%s: flat not transparent: %g", gv[v].label, md);

		// усиление одной полосы: на её частоте = заданному, далеко от неё почти нет
		const int idx[3] = { 0, B / 2, B - 1 };
		for (int k = 0; k < 3; k++) {
			Ladspa::Plugin q = p;
			std::string nm = p.ports[p.ControlInputs()[idx[k]]].name;		// "63 Гц", "1 кГц"
			double f = atof(nm.c_str()); if (nm.find("кГц") != std::string::npos) f *= 1000;
			for (int sign = -1; sign <= 1; sign += 2) {
				std::vector<float> c = def; c[idx[k]] = 12.0f * sign;
				double g = Gain(p, c, f);
				CHECK(fabs(g - 12.0 * sign) < 0.8, "%s band %g Hz %+d: %.2f dB", gv[v].label, f, sign * 12, g);
				double far = f > 3000 ? f / 16 : f * 16;
				if (far < 20000 && far > 20) {
					double gf = Gain(p, c, far);
					CHECK(fabs(gf) < 2.5, "%s band %g Hz: far (%g Hz) %.2f dB", gv[v].label, f, far, gf);
				}
			}
		}
		// выходное усиление
		{
			std::vector<float> c = def; c[B] = 6;
			double g = Gain(p, c, 1000);
			CHECK(fabs(g - 6.0) < 0.1, "%s out gain %.2f", gv[v].label, g);
		}
		printf("%s: ok (%d bands)\n", gv[v].label, B);
	}

	// ---------- параметрический ----------
	{
		const Ladspa::Plugin &p3 = Get("beae_parametric_eq_3"), &p5 = Get("beae_parametric_eq_5");
		CHECK(p3.ControlInputs().size() == 2 + 3 + 2 + 1, "p3 controls %d", (int)p3.ControlInputs().size());	// НЧ(2) + колокол(3) + ВЧ(2) + выход
		CHECK(p5.ControlInputs().size() == 2 + 9 + 2 + 1, "p5 controls %d", (int)p5.ControlInputs().size());
		// значения по умолчанию: частоты, нули, Q = 1
		std::vector<float> d3 = Defaults(p3), d5 = Defaults(p5);
		const float want3[] = { 100, 0, 1000, 0, 1, 8000, 0, 0 };
		for (size_t i = 0; i < d3.size(); i++) CHECK(fabsf(d3[i] - want3[i]) < want3[i] * 0.02f + 0.01f, "p3 default %d: %f vs %f", (int)i, d3[i], want3[i]);
		const float want5[] = { 100, 0, 300, 0, 1, 1000, 0, 1, 4000, 0, 1, 10000, 0, 0 };
		for (size_t i = 0; i < d5.size(); i++) CHECK(fabsf(d5[i] - want5[i]) < want5[i] * 0.02f + 0.01f, "p5 default %d: %f vs %f", (int)i, d5[i], want5[i]);

		// все нули: без изменений
		std::vector<float> in(10000);
		for (size_t i = 0; i < in.size(); i++) in[i] = 0.3f * sinf(0.05f * i);
		std::vector<float> o = Run(p3, d3, in);
		double md = 0; for (size_t i = 0; i < in.size(); i++) md = fmax(md, fabs(o[i] - in[i]));
		CHECK(md == 0.0, "p3 flat not transparent");

		// колокол: усиление на своей частоте; Q сужает
		std::vector<float> c = d3; c[2] = 1000; c[3] = 10; c[4] = 1.0f;
		CHECK(fabs(Gain(p3, c, 1000) - 10.0) < 0.3, "peak 1k: %.2f", Gain(p3, c, 1000));
		CHECK(fabs(Gain(p3, c, 12000)) < 1.0 && fabs(Gain(p3, c, 80)) < 1.0, "peak far");
		double wide = 0, narrow = 0;
		{ std::vector<float> cw = c; cw[4] = 0.5f; wide = Gain(p3, cw, 1500); std::vector<float> cn = c; cn[4] = 4.0f; narrow = Gain(p3, cn, 1500); }
		CHECK(wide > narrow + 3.0, "Q: wide %.2f narrow %.2f", wide, narrow);
		// срез: отрицательное усиление
		c[3] = -12; CHECK(fabs(Gain(p3, c, 1000) + 12.0) < 0.3, "cut 1k");
		// полки
		std::vector<float> ls = d3; ls[0] = 200; ls[1] = 10;
		CHECK(fabs(Gain(p3, ls, 30) - 10.0) < 1.2, "low shelf at 30 Hz: %.2f", Gain(p3, ls, 30));
		CHECK(fabs(Gain(p3, ls, 5000)) < 0.5, "low shelf at 5k: %.2f", Gain(p3, ls, 5000));
		std::vector<float> hs = d3; hs[5] = 8000; hs[6] = 10;
		CHECK(fabs(Gain(p3, hs, 18000) - 10.0) < 1.8, "high shelf at 18k: %.2f", Gain(p3, hs, 18000));
		CHECK(fabs(Gain(p3, hs, 500)) < 0.5, "high shelf at 500: %.2f", Gain(p3, hs, 500));
		// пять полос: две полосы одновременно
		std::vector<float> c5 = d5; c5[5] = 1000; c5[6] = 8; c5[8] = 4000; c5[9] = -8;
		CHECK(fabs(Gain(p5, c5, 1000) - 8.0) < 1.5, "p5 band2: %.2f", Gain(p5, c5, 1000));
		CHECK(fabs(Gain(p5, c5, 4000) + 8.0) < 1.5, "p5 band3: %.2f", Gain(p5, c5, 4000));

		// крайние значения и моно-буфер
		for (int w = 0; w < 2; w++) {
			const Ladspa::Plugin &p = w ? p5 : p3;
			std::vector<float> e(p.ControlInputs().size());
			for (size_t i = 0; i < e.size(); i++) {
				Ladspa::Range r = Ladspa::ResolveRange(p.ports[p.ControlInputs()[i]], kRate);
				e[i] = (i % 2) ? r.upper : r.lower;
			}
			std::vector<float> x(8000);
			for (size_t i = 0; i < x.size(); i++) x[i] = 0.5f * sinf(0.3f * i) + 0.4f * ((float)rand() / RAND_MAX - 0.5f);
			std::vector<float> y = Run(p, e, x, 1, 333);
			bool ok = true; for (size_t i = 0; i < y.size(); i++) if (!isfinite(y[i]) || fabsf(y[i]) > 100) ok = false;
			CHECK(ok, "extreme values p%d", w ? 5 : 3);
		}
	}

	printf(fails ? "FAILED %d\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}
