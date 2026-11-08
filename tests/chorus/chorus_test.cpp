// Проверка плагина "Хорус" на Linux через Ladspa::Runner.
#include "LadspaHost.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string>
#include <vector>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
static const float R = 44100.0f;
enum { P_RATE, P_DEPTH, P_MIX, P_FB };

static Ladspa::Plugin Find()
{
	std::vector<std::string> dirs; dirs.push_back("build");
	std::vector<Ladspa::Plugin> all = Ladspa::Scan(dirs);
	for (size_t i = 0; i < all.size(); i++) if (all[i].label == "beae_chorus") return all[i];
	printf("FAIL: plugin not found\n"); fails++;
	return Ladspa::Plugin();
}
static std::vector<float> Run(const Ladspa::Plugin &p, float rate, float depth, float mix, float fb, std::vector<float> in)
{
	Ladspa::Runner r; std::string err;
	if (!r.Start(p, R, 2, &err)) { printf("FAIL start: %s\n", err.c_str()); fails++; return in; }
	r.SetControl(P_RATE, rate); r.SetControl(P_DEPTH, depth); r.SetControl(P_MIX, mix); r.SetControl(P_FB, fb);
	const int frames = (int)(in.size() / 2);
	for (int pos = 0; pos < frames; pos += 512) { int n = frames - pos < 512 ? frames - pos : 512; r.Process(&in[pos * 2], n); }
	return in;
}
static std::vector<float> Sine(double f, double sec, float amp = 0.5f)
{
	std::vector<float> b((size_t)(sec * R) * 2);
	for (size_t i = 0; i < b.size() / 2; i++) b[i * 2] = b[i * 2 + 1] = amp * sinf((float)(2 * M_PI * f * i / R));
	return b;
}
// СКЗ в окнах по 50 мс: возвращает разброс (макс/мин) после первых 0.2 с
static double RmsSpread(const std::vector<float> &b)
{
	const size_t win = (size_t)(0.05 * R); double mn = 1e9, mx = 0;
	for (size_t s = (size_t)(0.2 * R); s + win < b.size() / 2; s += win) {
		double e = 0; for (size_t i = s; i < s + win; i++) e += b[i * 2] * b[i * 2];
		e = sqrt(e / win); mn = fmin(mn, e); mx = fmax(mx, e);
	}
	return mx / mn;
}

int main()
{
	Ladspa::Plugin p = Find();
	CHECK(p.mode == Ladspa::MODE_STEREO_STEREO, "mode");
	CHECK(p.name == "Хорус", "name: %s", p.name.c_str());
	{
		Ladspa::Range r0 = Ladspa::ResolveRange(p.ports[P_RATE], R), r1 = Ladspa::ResolveRange(p.ports[P_DEPTH], R),
			r2 = Ladspa::ResolveRange(p.ports[P_MIX], R), r3 = Ladspa::ResolveRange(p.ports[P_FB], R);
		CHECK(fabsf(r0.def - 0.707f) < 0.02f, "rate default %f", r0.def);
		CHECK(fabsf(r1.def - 3.0f) < 0.01f && fabsf(r2.def - 50.0f) < 0.01f && r3.def == 0.0f, "defaults %f %f %f", r1.def, r2.def, r3.def);
	}
	// смесь 0: без изменений
	{
		std::vector<float> in = Sine(330, 1.0);
		std::vector<float> out = Run(p, 0.7f, 3.0f, 0.0f, 0.0f, in);
		double d = 0; for (size_t i = 0; i < in.size(); i++) d = fmax(d, fabs(out[i] - in[i]));
		CHECK(d < 1e-6, "mix 0 passthrough %g", d);
	}
	// глубина 0: постоянная задержка (гребенчатый фильтр) - громкость ровная; глубина 5 мс - громкость «плывёт»
	{
		double s0 = RmsSpread(Run(p, 1.0f, 0.0f, 50.0f, 0.0f, Sine(1000, 3.0)));
		double s5 = RmsSpread(Run(p, 1.0f, 5.0f, 50.0f, 0.0f, Sine(1000, 3.0)));
		CHECK(s0 < 1.03, "depth 0 steady: %f", s0);
		CHECK(s5 > 1.05 && s5 > s0 + 0.03, "depth 5 moves: %f vs %f", s5, s0);
	}
	// вывод конечен и в разумных пределах, в том числе с обратной связью 60 %
	{
		std::vector<float> in((size_t)(5 * R) * 2); srand(2); for (auto &v : in) v = 2.0f * ((float)rand() / RAND_MAX - 0.5f);
		std::vector<float> out = Run(p, 5.0f, 6.0f, 100.0f, 60.0f, in);
		double peak = 0; bool fin = true; for (float v : out) { if (!isfinite(v)) fin = false; peak = fmax(peak, fabs(v)); }
		CHECK(fin && peak < 12.0, "stable, peak %g", peak);
	}
	// стерео: каналы различаются (разные фазы LFO)
	{
		std::vector<float> out = Run(p, 1.0f, 4.0f, 100.0f, 0.0f, Sine(800, 1.0));
		double d = 0; for (size_t i = (size_t)(0.3 * R); i < out.size() / 2; i++) d = fmax(d, fabs(out[i * 2] - out[i * 2 + 1]));
		CHECK(d > 0.05, "stereo spread %g", d);
	}
	printf(fails ? "FAILED %d\n" : "all ok\n", fails);
	return fails != 0;
}
