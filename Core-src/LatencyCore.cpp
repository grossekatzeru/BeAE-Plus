#include "LatencyCore.h"
#include <math.h>
#include <algorithm>
#include <vector>

namespace Latency {

float BurstSample(long long frame, double rate)
{
	const double t = (double)frame / rate;
	for (int b = 0; b < kBursts; b++){
		const double t0 = kFirstBurst + b * kBurstSpacing;
		if (t >= t0 && t < t0 + kBurstLength){
			const double x = (t - t0) / kBurstLength;			// 0..1
			const double env = 0.5 - 0.5 * cos(2.0 * M_PI * x);	// плавное нарастание и спад
			return (float)(0.6 * env * sin(2.0 * M_PI * 1500.0 * (t - t0)));
		}
	}
	return 0.0f;
}

long long FindOnset(const float *mono, long long n, double rate, double expected, double maxDelay)
{
	if (n < 16 || rate < 1.0) return -1;
	// фон: первые 0.2 с (до первого щелчка)
	long long noiseN = (long long)(0.2 * rate);
	if (noiseN > n) noiseN = n;
	float noise = 0;
	for (long long i = 0; i < noiseN; i++){
		float a = fabsf(mono[i]);
		if (a > noise) noise = a;
	}
	long long from = (long long)((expected - 0.05) * rate);
	long long to = (long long)((expected + maxDelay) * rate);
	if (from < noiseN) from = noiseN;
	if (to > n) to = n;
	if (from >= to) return -1;
	float peak = 0;
	for (long long i = from; i < to; i++){
		float a = fabsf(mono[i]);
		if (a > peak) peak = a;
	}
	if (peak < 0.01f || peak < noise * 3.0f) return -1;		// ничего не слышно
	float thr = peak * 0.2f;
	if (thr < noise * 2.0f) thr = noise * 2.0f;
	for (long long i = from; i < to; i++)
		if (fabsf(mono[i]) >= thr) return i;
	return -1;
}

bool MeasureMs(const float *mono, long long n, double rate, double *ms)
{
	std::vector<double> v;
	for (int b = 0; b < kBursts; b++){
		const double expected = kFirstBurst + b * kBurstSpacing;
		long long on = FindOnset(mono, n, rate, expected);
		if (on >= 0)
			v.push_back(((double)on / rate - expected) * 1000.0);
	}
	if (v.size() < 2) return false;
	std::sort(v.begin(), v.end());
	*ms = v[v.size() / 2];
	return true;
}

}	// namespace Latency
