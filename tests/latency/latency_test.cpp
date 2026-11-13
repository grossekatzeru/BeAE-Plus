#include "LatencyCore.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <vector>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

// имитация: сигнал приходит с задержкой delayMs, с затуханием gain и шумом
static std::vector<float> Simulate(double rate, double delayMs, float gain, float noise)
{
	long long n = (long long)(Latency::kTotalSeconds * rate);
	std::vector<float> v(n);
	long long d = (long long)(delayMs * rate / 1000.0);
	srand(1);
	for (long long i = 0; i < n; i++){
		float s = (i >= d) ? Latency::BurstSample(i - d, rate) * gain : 0.0f;
		v[i] = s + noise * ((rand() / (float)RAND_MAX) * 2 - 1);
	}
	return v;
}

int main()
{
	const double rate = 44100;
	double ms = 0;
	for (double d : { 0.0, 12.0, 47.5, 130.0, 300.0 }){
		std::vector<float> v = Simulate(rate, d, 1.0f, 0.0f);
		CHECK(Latency::MeasureMs(&v[0], v.size(), rate, &ms));
		CHECK(fabs(ms - d) < 1.5);
	}
	// тихий сигнал с шумом
	{
		std::vector<float> v = Simulate(rate, 85.0, 0.15f, 0.004f);
		CHECK(Latency::MeasureMs(&v[0], v.size(), rate, &ms));
		CHECK(fabs(ms - 85.0) < 2.0);
	}
	// тишина - не найдено
	{
		std::vector<float> v = Simulate(rate, 50.0, 0.0f, 0.001f);
		CHECK(!Latency::MeasureMs(&v[0], v.size(), rate, &ms));
	}
	// один щелчок потерялся (обнуляем второй) - медиана по двум остальным
	{
		std::vector<float> v = Simulate(rate, 60.0, 1.0f, 0.0f);
		for (long long i = (long long)(1.4 * rate); i < (long long)(2.3 * rate); i++) v[i] = 0;
		CHECK(Latency::MeasureMs(&v[0], v.size(), rate, &ms));
		CHECK(fabs(ms - 60.0) < 1.5);
	}
	// 48 кГц
	{
		std::vector<float> v = Simulate(48000, 33.0, 0.8f, 0.0f);
		CHECK(Latency::MeasureMs(&v[0], v.size(), 48000, &ms));
		CHECK(fabs(ms - 33.0) < 1.5);
	}
	printf(fails ? "FAILED\n" : "all ok\n");
	return fails ? 1 : 0;
}
