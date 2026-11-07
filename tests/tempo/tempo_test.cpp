#include "TempoDetect.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
static int fails = 0;
static void check(bool ok, const char *n){ printf("%s %s\n", ok ? "ok  " : "FAIL", n); if (!ok) fails++; }

static std::vector<float> Beats(double bpm, float rate, double sec, bool swing = false, double noise = 0)
{
	std::vector<float> x((size_t)(rate * sec), 0.f);
	double beat = 60.0 / bpm;
	srand(1);
	for (int k = 0; k * beat < sec - 0.2; k++){
		size_t s = (size_t)(k * beat * rate);
		for (size_t i = 0; i < 4000 && s + i < x.size(); i++){
			double t = i / rate;
			x[s + i] += (float)(0.8 * exp(-t * 30) * sin(2 * M_PI * 60 * t));	// бочка
			if (k % 2 == 1) x[s + i] += (float)(0.3 * exp(-t * 60) * ((rand() / (double)RAND_MAX) - 0.5));
		}
		if (swing){
			size_t s2 = s + (size_t)(beat * 0.5 * rate);
			for (size_t i = 0; i < 1500 && s2 + i < x.size(); i++)
				x[s2 + i] += (float)(0.2 * exp(-(double)i / rate * 120) * ((rand() / (double)RAND_MAX) - 0.5));
		}
	}
	if (noise > 0) for (auto &v : x) v += (float)(noise * ((rand() / (double)RAND_MAX) - 0.5));
	return x;
}

int main()
{
	const float R = 44100;
	double bpms[] = { 90, 100, 120, 128, 140, 174 };
	for (double b : bpms){
		auto x = Beats(b, R, 30, true);
		double r = 0; bool ok = DetectTempo(x.data(), x.size(), R, r);
		char n[64]; snprintf(n, sizeof n, "click %.0f -> %.1f", b, r);
		double d = fabs(r - b);
		if (fabs(r - b * 2) < d) d = fabs(r - b * 2);		// октавная неоднозначность допустима
		if (fabs(r - b / 2) < d) d = fabs(r - b / 2);
		check(ok && d < 1.0, n);
	}
	{ auto x = Beats(110, R, 30, false, 0.05); double r; bool ok = DetectTempo(x.data(), x.size(), R, r);
	  check(ok && fabs(r - 110) < 1.5, "noisy 110"); }
	{ std::vector<float> x((size_t)(R * 20), 0.f); double r; check(!DetectTempo(x.data(), x.size(), R, r), "silence fails"); }
	{ std::vector<float> x((size_t)(R * 20)); srand(3); for (auto &v : x) v = 0.3f * ((rand() / (double)RAND_MAX) - 0.5f);
	  double r; check(!DetectTempo(x.data(), x.size(), R, r), "white noise fails"); }
	{ auto x = Beats(120, R, 3); double r; check(!DetectTempo(x.data(), x.size(), R, r), "too short fails"); }
	{ std::vector<float> x((size_t)(R * 20)); for (size_t i = 0; i < x.size(); i++) x[i] = 0.3f * sinf(2 * M_PI * 440 * i / R);
	  double r; check(!DetectTempo(x.data(), x.size(), R, r), "steady tone fails"); }
	printf(fails ? "FAILED\n" : "all ok\n");
	return fails != 0;
}
