// Проверка "Пластинчатого реверба" на Linux через Ladspa::Runner.
#include "LadspaHost.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string>
#include <vector>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

struct Par { float decay, pre, damp, lowcut, density, mod, width, mix; };
static Par Def() { Par p = { 2.0f, 0.0f, 0.25f, 100.0f, 1.0f, 0.5f, 1.0f, 100.0f }; return p; }

static Ladspa::Plugin Find()
{
	std::vector<std::string> dirs; dirs.push_back("build");
	std::vector<Ladspa::Plugin> all = Ladspa::Scan(dirs);
	for (size_t i = 0; i < all.size(); i++) if (all[i].label == "beae_plate_reverb") return all[i];
	printf("FAIL: plugin not found\n"); fails++;
	return Ladspa::Plugin();
}

static std::vector<float> Run(const Ladspa::Plugin &pl, float rate, const Par &p, std::vector<float> in)
{
	Ladspa::Runner r; std::string err;
	if (!r.Start(pl, rate, 2, &err)) { printf("FAIL start: %s\n", err.c_str()); fails++; return in; }
	const float v[8] = { p.decay, p.pre, p.damp, p.lowcut, p.density, p.mod, p.width, p.mix };
	for (int i = 0; i < 8; i++) r.SetControl(i, v[i]);
	const int frames = (int)(in.size() / 2);
	for (int pos = 0; pos < frames; pos += 512) { int n = frames - pos < 512 ? frames - pos : 512; r.Process(&in[pos * 2], n); }
	return in;
}
static std::vector<float> Impulse(float rate, double sec) { std::vector<float> b((size_t)(sec * rate) * 2, 0.0f); b[0] = b[1] = 1.0f; return b; }
static double EnergyIn(const std::vector<float> &b, float rate, double t0, double t1)
{
	double e = 0; for (size_t i = (size_t)(t0 * rate); i < (size_t)(t1 * rate) && i < b.size() / 2; i++) e += b[i * 2] * b[i * 2] + b[i * 2 + 1] * b[i * 2 + 1];
	return e;
}
// RT60 по обратному интегралу Шрёдера (участок -5..-25 дБ, экстраполяция на 60 дБ); первые кадры (сухой импульс) исключены
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
// энергия на частоте f (фильтр Гёрцеля)
static double BandEnergy(const std::vector<float> &b, float rate, double t0, double t1, double f)
{
	size_t i0 = (size_t)(t0 * rate), i1 = (size_t)(t1 * rate);
	double c = 2 * cos(2 * M_PI * f / rate), s1 = 0, s2 = 0;
	for (size_t i = i0; i < i1 && i < b.size() / 2; i++) { double s0 = b[i * 2] + c * s1 - s2; s2 = s1; s1 = s0; }
	return s1 * s1 + s2 * s2 - c * s1 * s2;
}

int main()
{
	Ladspa::Plugin pl = Find();
	CHECK(pl.mode == Ladspa::MODE_STEREO_STEREO, "mode");
	CHECK(pl.name == "Пластинчатый реверб (EMT 140)", "name %s", pl.name.c_str());
	{
		const float want[8] = { 2.0f, 0.0f, 0.25f, 100.0f, 1.0f, 0.5f, 1.0f, 25.0f };
		for (int i = 0; i < 8; i++) { Ladspa::Range r = Ladspa::ResolveRange(pl.ports[i], 44100); CHECK(fabsf(r.def - want[i]) < want[i] * 0.01f + 0.01f, "default %d: %f", i, r.def); }
	}
	const float R = 44100.0f;
	{	// смесь 0: без изменений
		std::vector<float> in(2 * 30000); srand(5); for (auto &v : in) v = (float)rand() / RAND_MAX - 0.5f;
		Par p = Def(); p.mix = 0;
		std::vector<float> out = Run(pl, R, p, in);
		double d = 0; for (size_t i = 0; i < in.size(); i++) d = fmax(d, fabs(out[i] - in[i]));
		CHECK(d < 1e-6, "mix 0 passthrough %g", d);
	}
	for (float rt : { 1.0f, 2.0f, 4.0f }) {		// время затухания близко к заданному
		Par p = Def(); p.decay = rt; p.damp = 0.0f; p.lowcut = 20.0f; p.mod = 0.0f;
		std::vector<float> o = Run(pl, R, p, Impulse(R, rt * 2.0 + 2));
		double got = Rt60(o, R);
		printf("RT60 set %.1f -> %.2f s\n", rt, got);
		CHECK(got > rt * 0.7 && got < rt * 1.3, "RT60 %f vs %f", got, rt);
	}
	for (float rate : { 48000.0f, 96000.0f, 22050.0f }) {		// другая частота дискретизации - то же время затухания
		Par p = Def(); p.decay = 2.0f; p.damp = 0.0f; p.lowcut = 20.0f; p.mod = 0.0f;
		std::vector<float> o = Run(pl, rate, p, Impulse(rate, 5.0));
		double got = Rt60(o, rate);
		printf("RT60 at %g Hz -> %.2f s\n", rate, got);
		CHECK(got > 1.4 && got < 2.6, "RT60 at %g Hz: %f", rate, got);
	}
	{	// плотность: после 150 мс огибающая гладкая (соседние окна по 10 мс различаются не более чем в 2 раза): отдельных эхо нет
		Par p = Def();
		std::vector<float> o = Run(pl, R, p, Impulse(R, 1.5));
		double worst = 1, prev = EnergyIn(o, R, 0.15, 0.16);
		for (double t = 0.16; t < 0.8; t += 0.01) {
			double e = EnergyIn(o, R, t, t + 0.01);
			worst = fmax(worst, fmax(e / prev, prev / e));
			prev = e;
		}
		printf("max change between neighbouring 10 ms windows: x%.2f\n", worst);
		CHECK(worst < 2.0, "dense tail: neighbour window ratio %f", worst);
	}
	{	// затухание высоких
		Par bright = Def(); bright.damp = 0.0f; Par dark = Def(); dark.damp = 0.9f;
		std::vector<float> a = Run(pl, R, bright, Impulse(R, 3)), b = Run(pl, R, dark, Impulse(R, 3));
		double ha = 0, hb = 0, la = 0, lb = 0;
		for (double f : { 6000.0, 7000.0, 8000.0, 9000.0 }) { ha += BandEnergy(a, R, 1.0, 2.5, f); hb += BandEnergy(b, R, 1.0, 2.5, f); }
		for (double f : { 400.0, 600.0, 800.0, 1000.0 }) { la += BandEnergy(a, R, 1.0, 2.5, f); lb += BandEnergy(b, R, 1.0, 2.5, f); }
		CHECK(hb / lb < 0.3 * (ha / la), "damping HF ratio: dark %g vs bright %g", hb / lb, ha / la);
	}
	{	// срез низких
		Par lo = Def(); lo.lowcut = 20; Par hi = Def(); hi.lowcut = 400;
		std::vector<float> in(2 * (size_t)(3 * R)); srand(4); for (auto &v : in) v = (float)rand() / RAND_MAX - 0.5f;
		std::vector<float> a = Run(pl, R, lo, in), b = Run(pl, R, hi, in);
		for (size_t i = 0; i < in.size(); i++) { a[i] -= in[i]; b[i] -= in[i]; }		// только эффект, без сухого
		double ea = 0, eb = 0; for (double f : { 40.0, 60.0, 80.0, 100.0 }) { ea += BandEnergy(a, R, 1.0, 2.9, f); eb += BandEnergy(b, R, 1.0, 2.9, f); }
		CHECK(eb < ea * 0.4, "lowcut: %g vs %g", eb, ea);
	}
	{	// предзадержка 80 мс
		Par p = Def(); p.pre = 80;
		std::vector<float> o = Run(pl, R, p, Impulse(R, 0.5));
		CHECK(EnergyIn(o, R, 0.0005, 0.078) < 1e-12, "predelay silence %g", EnergyIn(o, R, 0.0005, 0.078));
		CHECK(EnergyIn(o, R, 0.09, 0.4) > 1e-6, "tail after predelay");
	}
	{	// ширина
		Par p = Def(); p.width = 0;
		std::vector<float> o = Run(pl, R, p, Impulse(R, 1));
		double d = 0; for (size_t i = 4; i < o.size() / 2; i++) d = fmax(d, fabs(o[i * 2] - o[i * 2 + 1]));
		CHECK(d < 1e-6, "width 0 diff %g", d);
		Par q = Def(); std::vector<float> o2 = Run(pl, R, q, Impulse(R, 1));
		double d2 = 0; for (size_t i = 4; i < o2.size() / 2; i++) d2 = fmax(d2, fabs(o2[i * 2] - o2[i * 2 + 1]));
		CHECK(d2 > 1e-4, "width 1 stereo %g", d2);
	}
	{	// уровень хвоста относительно входа (белый шум, смесь 100 %)
		std::vector<float> in(2 * (size_t)(6 * R)); srand(7); double ein = 0;
		for (size_t i = 0; i < in.size() / 2; i++) { float v = 2.0f * ((float)rand() / RAND_MAX - 0.5f) * 1.732f * 0.25f; in[i * 2] = in[i * 2 + 1] = v; ein += v * v; }
		ein /= in.size() / 2;
		Par p = Def();
		std::vector<float> o = Run(pl, R, p, in);
		double e = 0; size_t c = 0; for (size_t i = (size_t)(3 * R); i < o.size() / 2; i++) { double w = o[i * 2] - in[i * 2]; e += w * w; c++; }
		e /= c;
		printf("wet/dry level (noise, mix 100%%): %.2f\n", sqrt(e / ein));
		CHECK(sqrt(e / ein) > 0.3 && sqrt(e / ein) < 3.0, "wet level %f", sqrt(e / ein));
	}
	{	// устойчивость
		std::vector<float> in((size_t)(10 * R) * 2); srand(9); for (auto &v : in) v = 2.0f * ((float)rand() / RAND_MAX - 0.5f);
		Par p = Def(); p.decay = 8; p.damp = 0; p.mod = 1; p.pre = 200;
		std::vector<float> out = Run(pl, R, p, in);
		double peak = 0; bool fin = true; for (float v : out) { if (!isfinite(v)) fin = false; peak = fmax(peak, fabs(v)); }
		CHECK(fin && peak < 30.0, "stable, peak %g", peak);
	}
	printf(fails ? "FAILED %d\n" : "all ok\n", fails);
	return fails != 0;
}
