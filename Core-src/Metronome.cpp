#include "Metronome.h"
#include <math.h>
#include <stdint.h>

Metronome::Metronome() : fRate(0) {}

const char *Metronome::MeterName(int meter)
{
	static const char *n[METER_COUNT] = { "4/4", "3/4", "6/8", "2/4" };
	return (meter >= 0 && meter < METER_COUNT) ? n[meter] : n[0];
}

int Metronome::BarUnits(int meter)
{
	static const int u[METER_COUNT] = { 32, 24, 24, 16 };
	return (meter >= 0 && meter < METER_COUNT) ? u[meter] : 32;
}

int Metronome::StepUnits(int div)
{
	switch (div) {
	case 1: case 2: case 4: case 8: case 16: case 32:	return 32 / div;
	}
	return 0;
}

Metronome::Kind Metronome::KindAt(int meter, int pos)
{
	const int bar = BarUnits(meter);
	pos %= bar;
	if (pos == 0) return KICK;
	if (meter == M44 && pos == 16) return SNARE;
	if (meter == M68 && pos == 12) return SNARE;
	if (meter == M24 && pos == 8) return SNARE;
	if (meter == M34 && (pos == 8 || pos == 16)) return SNARE;
	const int base = (meter == M68) ? 4 : 8;						// главная доля хай-хета: восьмая в 6/8, иначе четверть
	int level = 0;
	for (int u = base; u > 1 && pos % u != 0; u /= 2) level++;
	return (Kind)(HAT_QUARTER + (level > 3 ? 3 : level));
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

void Metronome::Render(float *buf, int frames, double t0, double sysRate, double bpm, int meter, int div, float gain)
{
	if (frames <= 0 || sysRate <= 1.0 || bpm <= 0 || gain <= 0) return;
	int stepUnits = StepUnits(div);
	if (stepUnits <= 0) return;
	const int bar = BarUnits(meter);
	Prepare(sysRate);

	const double unit = (60.0 / bpm) / 8.0;				// секунд в одной 32-й доле
	const double step = unit * stepUnits;
	const double tEnd = t0 + frames / sysRate;
	const double maxLen = 0.25;							// дольше любой звук не звучит
	long long n0 = (long long)ceil((t0 - maxLen) / step);
	if (n0 < 0) n0 = 0;
	for (long long n = n0; n * step < tEnd; n++) {
		const double th = n * step;
		const int pos = (int)((n * stepUnits) % bar);
		const std::vector<float> &s = fSound[KindAt(meter, pos)];
		const long long off = (long long)floor((th - t0) * sysRate + 0.5);
		long long j0 = off < 0 ? -off : 0;
		for (long long j = j0; j < (long long)s.size(); j++) {
			const long long fr = off + j;
			if (fr >= frames) break;
			buf[2 * fr] += s[j] * gain;
			buf[2 * fr + 1] += s[j] * gain;
		}
	}
}
