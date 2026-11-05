#include "Metronome.h"
#include <math.h>
#include <stdint.h>

#define UNITS	32			// долей в такте (32-е ноты)

Metronome::Metronome() : fRate(0) {}

Metronome::Kind Metronome::KindAt(int pos)
{
	pos &= UNITS - 1;
	if (pos == 0) return KICK;
	if (pos == UNITS / 2) return SNARE;
	if (pos % 8 == 0) return HAT_QUARTER;
	if (pos % 4 == 0) return HAT_EIGHTH;
	if (pos % 2 == 0) return HAT_SIXTEENTH;
	return HAT_THIRTYSECOND;
}

void Metronome::Prepare(double rate)
{
	if (rate == fRate) return;
	fRate = rate;
	uint32_t seed = 12345;
	auto noise = [&seed]() {
		seed = seed * 1664525u + 1013904223u;
		return (float)((int32_t)seed) / 2147483648.0f;
	};
	// бочка: синус с быстро падающей частотой
	{
		const int n = (int)(0.22 * rate);
		std::vector<float> &s = fSound[KICK];
		s.assign(n, 0);
		double ph = 0;
		for (int i = 0; i < n; i++) {
			const double t = i / rate;
			const double f = 48.0 + 120.0 * exp(-t / 0.028);
			ph += 2 * M_PI * f / rate;
			s[i] = (float)(0.9 * sin(ph) * exp(-t / 0.07));
		}
	}
	// малый: шум плюс тон
	{
		const int n = (int)(0.16 * rate);
		std::vector<float> &s = fSound[SNARE];
		s.assign(n, 0);
		for (int i = 0; i < n; i++) {
			const double t = i / rate;
			s[i] = (float)(0.45 * noise() * exp(-t / 0.045) + 0.35 * sin(2 * M_PI * 190.0 * t) * exp(-t / 0.05));
		}
	}
	// хай-хет: высокочастотный шум (разность соседних отсчётов), чем мельче доля, тем тише
	static const float level[4] = { 0.32f, 0.22f, 0.15f, 0.10f };
	for (int k = 0; k < 4; k++) {
		const int n = (int)(0.05 * rate);
		std::vector<float> &s = fSound[HAT_QUARTER + k];
		s.assign(n, 0);
		float prev = 0;
		for (int i = 0; i < n; i++) {
			const double t = i / rate;
			const float x = noise();
			s[i] = (float)((x - prev) * 0.5 * level[k] * exp(-t / 0.012));
			prev = x;
		}
	}
}

void Metronome::Render(float *buf, int frames, double t0, double sysRate, double bpm, int div)
{
	if (frames <= 0 || sysRate <= 1.0 || bpm <= 0) return;
	if (div < 1) div = 1;
	if (div > UNITS) div = UNITS;
	Prepare(sysRate);

	const double unit = (4.0 * 60.0 / bpm) / UNITS;		// секунд в одной 32-й доле
	const int stepUnits = UNITS / div;
	const double step = unit * stepUnits;
	const double tEnd = t0 + frames / sysRate;
	const double maxLen = 0.25;							// дольше любой звук не звучит
	long long n0 = (long long)ceil((t0 - maxLen) / step);
	if (n0 < 0) n0 = 0;
	for (long long n = n0; n * step < tEnd; n++) {
		const double th = n * step;
		const int pos = (int)((n * stepUnits) % UNITS);
		const std::vector<float> &s = fSound[KindAt(pos)];
		const long long off = (long long)floor((th - t0) * sysRate + 0.5);
		long long j0 = off < 0 ? -off : 0;
		for (long long j = j0; j < (long long)s.size(); j++) {
			const long long fr = off + j;
			if (fr >= frames) break;
			buf[2 * fr] += s[j];
			buf[2 * fr + 1] += s[j];
		}
	}
}
