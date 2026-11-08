// Проверка плагина "Реверберация" на Linux через Ladspa::Runner.
#include "LadspaHost.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string>
#include <vector>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
static const float R = 44100.0f;
enum { P_SIZE, P_DAMP, P_MIX, P_PREDELAY, P_WIDTH };

static Ladspa::Plugin Find()
{
	std::vector<std::string> dirs; dirs.push_back("build");
	std::vector<Ladspa::Plugin> all = Ladspa::Scan(dirs);
	for (size_t i = 0; i < all.size(); i++) if (all[i].label == "beae_reverb") return all[i];
	printf("FAIL: plugin not found\n"); fails++;
	return Ladspa::Plugin();
}

static std::vector<float> Run(const Ladspa::Plugin &p, float size, float damp, float mix, float pre, float width, std::vector<float> in)
{
	Ladspa::Runner r; std::string err;
	if (!r.Start(p, R, 2, &err)) { printf("FAIL start: %s\n", err.c_str()); fails++; return in; }
	r.SetControl(P_SIZE, size); r.SetControl(P_DAMP, damp); r.SetControl(P_MIX, mix); r.SetControl(P_PREDELAY, pre); r.SetControl(P_WIDTH, width);
	const int frames = (int)(in.size() / 2);
	for (int pos = 0; pos < frames; pos += 512) { int n = frames - pos < 512 ? frames - pos : 512; r.Process(&in[pos * 2], n); }
	return in;
}
static std::vector<float> Impulse(int frames) { std::vector<float> b(frames * 2, 0.0f); b[0] = b[1] = 1.0f; return b; }
static double Energy(const std::vector<float> &b, double t0, double t1)
{
	double e = 0; for (size_t i = (size_t)(t0 * R); i < (size_t)(t1 * R) && i < b.size() / 2; i++) e += b[i * 2] * b[i * 2] + b[i * 2 + 1] * b[i * 2 + 1];
	return e;
}

int main()
{
	Ladspa::Plugin p = Find();
	CHECK(p.mode == Ladspa::MODE_STEREO_STEREO, "mode");
	CHECK(p.name == "Реверберация", "name: %s", p.name.c_str());
	{
		const float want[5] = { 0.75f, 0.5f, 25.0f, 0.0f, 1.0f };
		for (int i = 0; i < 5; i++) { Ladspa::Range r = Ladspa::ResolveRange(p.ports[i], R); CHECK(fabsf(r.def - want[i]) < 0.01f, "default %d: %f", i, r.def); }
	}
	// смесь 0: сигнал проходит без изменений
	{
		std::vector<float> in(2 * 20000); srand(5); for (auto &v : in) v = (float)rand() / RAND_MAX - 0.5f;
		std::vector<float> out = Run(p, 0.75f, 0.5f, 0.0f, 0.0f, 1.0f, in);
		double d = 0; for (size_t i = 0; i < in.size(); i++) d = fmax(d, fabs(out[i] - in[i]));
		CHECK(d < 1e-6, "mix 0 passthrough, diff %g", d);
	}
	// импульс: после сухого звука есть хвост, который затухает
	std::vector<float> a = Run(p, 0.75f, 0.5f, 50.0f, 0.0f, 1.0f, Impulse((int)(6 * R)));
	{
		double e1 = Energy(a, 0.05, 0.5), e2 = Energy(a, 2.0, 2.5), e3 = Energy(a, 5.0, 5.5);
		CHECK(e1 > 1e-4, "tail present: %g", e1);
		CHECK(e2 < e1 * 0.5 && e3 < e2, "tail decays: %g %g %g", e1, e2, e3);
		bool fin = true; for (float v : a) if (!isfinite(v)) fin = false;
		CHECK(fin, "finite");
	}
	// размер: больше комната - длиннее хвост
	{
		std::vector<float> small = Run(p, 0.2f, 0.5f, 50.0f, 0.0f, 1.0f, Impulse((int)(3 * R)));
		std::vector<float> big = Run(p, 1.0f, 0.5f, 50.0f, 0.0f, 1.0f, Impulse((int)(3 * R)));
		CHECK(Energy(big, 1.0, 2.0) > Energy(small, 1.0, 2.0) * 3, "size: %g vs %g", Energy(big, 1.0, 2.0), Energy(small, 1.0, 2.0));
	}
	// смесь как посыл: чем больше, тем громче хвост (пропорционально), сухой звук не меняется
	{
		std::vector<float> lo = Run(p, 0.75f, 0.5f, 10.0f, 0.0f, 1.0f, Impulse((int)(2 * R)));
		std::vector<float> hi = Run(p, 0.75f, 0.5f, 100.0f, 0.0f, 1.0f, Impulse((int)(2 * R)));
		CHECK(Energy(hi, 0.1, 1.0) > Energy(lo, 0.1, 1.0) * 50, "mix scale");
		CHECK(fabsf(lo[0] - 1.0f) < 1e-6f && fabsf(hi[0] - 1.0f) < 1e-6f, "dry impulse kept");
	}
	// предзадержка 50 мс: до неё хвоста нет
	{
		std::vector<float> d0 = Run(p, 0.75f, 0.5f, 100.0f, 0.0f, 1.0f, Impulse((int)(R)));
		std::vector<float> d50 = Run(p, 0.75f, 0.5f, 100.0f, 50.0f, 1.0f, Impulse((int)(R)));
		CHECK(Energy(d0, 0.001, 0.07) > 1e-6, "no predelay: tail starts early");
		CHECK(Energy(d50, 0.0005, 0.065) < 1e-9, "predelay 50 ms: silence before: %g", Energy(d50, 0.0005, 0.065));
		CHECK(Energy(d50, 0.07, 0.3) > 1e-6, "tail after predelay");
	}
	// ширина 0: эффект одинаков в обоих каналах
	{
		std::vector<float> w = Run(p, 0.75f, 0.5f, 100.0f, 0.0f, 0.0f, Impulse((int)(R)));
		double d = 0; for (size_t i = 4; i < w.size() / 2; i++) d = fmax(d, fabs(w[i * 2] - w[i * 2 + 1]));
		CHECK(d < 1e-6, "width 0 mono, diff %g", d);
		std::vector<float> w1 = Run(p, 0.75f, 0.5f, 100.0f, 0.0f, 1.0f, Impulse((int)(R)));
		double d1 = 0; for (size_t i = 4; i < w1.size() / 2; i++) d1 = fmax(d1, fabs(w1[i * 2] - w1[i * 2 + 1]));
		CHECK(d1 > 1e-4, "width 1 stereo, diff %g", d1);
	}
	// устойчивость: громкий шум 10 с на максимуме
	{
		std::vector<float> in((size_t)(10 * R) * 2); srand(9); for (auto &v : in) v = 2.0f * ((float)rand() / RAND_MAX - 0.5f);
		std::vector<float> out = Run(p, 1.0f, 0.0f, 100.0f, 100.0f, 1.0f, in);
		double peak = 0; bool fin = true; for (float v : out) { if (!isfinite(v)) fin = false; peak = fmax(peak, fabs(v)); }
		CHECK(fin && peak < 20.0, "stable, peak %g", peak);
	}
	printf(fails ? "FAILED %d\n" : "all ok\n", fails);
	return fails != 0;
}
