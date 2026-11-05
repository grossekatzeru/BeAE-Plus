// Проверка шумодава на Linux: через Ladspa::Runner как встроенный плагин.
#include "NoiseReducer.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string>
#include <vector>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static const float kRate = 44100.0f;

static Ladspa::Plugin Find()
{
	Ladspa::RegisterBuiltin(NoiseReducer::Descriptor(), NoiseReducer::kDefaults);
	std::vector<Ladspa::Plugin> all = Ladspa::Scan(std::vector<std::string>());
	for (size_t i = 0; i < all.size(); i++)
		if (all[i].label == "beae_noise_reducer") return all[i];
	printf("FAIL: plugin not found\n"); fails++;
	return Ladspa::Plugin();
}

static std::vector<float> Defaults()
{
	std::vector<float> c(NoiseReducer::P_COUNT, 0.0f);
	for (int i = 0; i < NoiseReducer::P_LATENCY; i++) c[i] = NoiseReducer::kDefaults[i];
	return c;
}

static float Noise(float sigma)	// примерно гауссов шум (сумма равномерных)
{
	float s = 0; for (int i = 0; i < 6; i++) s += (float)rand() / RAND_MAX - 0.5f;
	return s * sigma * 1.414f * 1.0f;
}

static double Rms(const std::vector<float> &v, double a, double b)
{
	double e = 0; int n = 0;
	for (int i = (int)(a * kRate); i < (int)(b * kRate) && i < (int)(v.size() / 2); i++) { e += v[i * 2] * v[i * 2]; n++; }
	return sqrt(e / (n ? n : 1));
}

// "речь": тон 880 Гц включён 0.25 с из каждых 0.5 с
static float Gate(float t) { return fmodf(t, 0.5f) < 0.25f ? 1.0f : 0.0f; }

static void RunBlocks(Ladspa::Runner &r, std::vector<float> &buf, int block = 512)
{
	int frames = (int)(buf.size() / 2);
	for (int pos = 0; pos < frames; pos += block) {
		int n = frames - pos < block ? frames - pos : block;
		r.Process(&buf[pos * 2], n);
	}
}

int main()
{
	srand(1);
	Ladspa::Plugin p = Find();
	CHECK(p.mode == Ladspa::MODE_STEREO_STEREO, "mode");
	CHECK(p.latencyPort == NoiseReducer::P_LATENCY, "latency port %d", p.latencyPort);
	CHECK(p.ControlInputs().size() == 6, "controls %d", (int)p.ControlInputs().size());

	// ---- задержка сообщается и равна окну ----
	{
		Ladspa::Runner r; std::string err;
		CHECK(r.Start(p, kRate, 2, &err), "start %s", err.c_str());
		CHECK(r.Latency() == NoiseReducer::kFrame, "latency %d", r.Latency());
	}

	// ---- подавление 0 дБ: выход = вход, сдвинутый на задержку ----
	{
		Ladspa::Runner r; std::string err; r.Start(p, kRate, 2, &err);
		std::vector<float> c = Defaults(); c[NoiseReducer::P_REDUCTION] = 0;
		for (size_t i = 0; i < c.size(); i++) r.SetControl((int)i, c[i]);
		const int frames = 20000;
		std::vector<float> in(frames * 2), buf;
		for (int i = 0; i < frames; i++) { float v = 0.3f * sinf(2 * 3.14159265f * 1000.0f * i / kRate) + Noise(0.05f); in[i*2] = v; in[i*2+1] = -v; }
		buf = in;
		RunBlocks(r, buf, 300);		// размер блока не кратен шагу окна
		double maxErr = 0;
		const int L = NoiseReducer::kFrame;
		for (int i = L + 2048; i < frames; i++) {
			maxErr = fmax(maxErr, fabs(buf[i*2] - in[(i - L) * 2]));
			maxErr = fmax(maxErr, fabs(buf[i*2+1] - in[(i - L) * 2 + 1]));
		}
		CHECK(maxErr < 1e-3, "identity error %g", maxErr);
	}

	// ---- авто-режим: шум 0..4 с, потом тон + шум; шум падает, тон остаётся ----
	{
		Ladspa::Runner r; std::string err; r.Start(p, kRate, 2, &err);
		std::vector<float> c = Defaults();
		for (size_t i = 0; i < c.size(); i++) r.SetControl((int)i, c[i]);
		const int frames = (int)(kRate * 8);
		std::vector<float> in(frames * 2);
		for (int i = 0; i < frames; i++) {
			float t = i / kRate;
			float v = Noise(0.05f) + (t > 5 ? Gate(t) * 0.3f * sinf(2 * 3.14159265f * 880.0f * t) : 0.0f);
			in[i*2] = in[i*2+1] = v;
		}
		std::vector<float> buf = in;
		RunBlocks(r, buf, 512);
		const double lat = NoiseReducer::kFrame / (double)kRate;
		double nIn = Rms(in, 3.0, 4.5), nOut = Rms(buf, 3.0 + lat, 4.5 + lat);
		double db = 20 * log10(nOut / nIn);
		printf("auto: noise %.4f -> %.4f (%.1f dB)\n", nIn, nOut, db);
		CHECK(db < -8.0, "noise reduction only %.1f dB", db);
		double tIn = 0, tOut = 0;
		for (int k = 0; k < 4; k++) {			// середины включённых отрезков (6.0, 6.5, ...)
			double a = 6.5 + k * 0.5 + 0.05, b = a + 0.15;
			tIn += Rms(in, a, b); tOut += Rms(buf, a + lat, b + lat);
		}
		double tdb = 20 * log10(tOut / tIn);
		printf("auto: tone+noise %.4f -> %.4f (%.1f dB)\n", tIn, tOut, tdb);
		CHECK(fabs(tdb) < 1.5, "tone changed by %.1f dB", tdb);
	}

	// ---- профиль: запоминаем шум в первые 1.5 с, дальше работаем по профилю ----
	{
		Ladspa::Runner r; std::string err; r.Start(p, kRate, 2, &err);
		std::vector<float> c = Defaults();
		c[NoiseReducer::P_LEARN] = 1;
		for (size_t i = 0; i < c.size(); i++) r.SetControl((int)i, c[i]);
		const int part = (int)(kRate * 1.5);
		std::vector<float> a(part * 2);
		for (int i = 0; i < part; i++) a[i*2] = a[i*2+1] = Noise(0.05f);
		RunBlocks(r, a, 512);
		r.SetControl(NoiseReducer::P_LEARN, 0);
		r.SetControl(NoiseReducer::P_USEPROFILE, 1);
		const int frames = (int)(kRate * 3);
		std::vector<float> in(frames * 2);
		for (int i = 0; i < frames; i++) {
			float t = i / kRate;
			float v = Noise(0.05f) + (t > 1.5 ? Gate(t) * 0.3f * sinf(2 * 3.14159265f * 880.0f * t) : 0.0f);
			in[i*2] = in[i*2+1] = v;
		}
		std::vector<float> buf = in;
		RunBlocks(r, buf, 512);
		const double lat = NoiseReducer::kFrame / (double)kRate;
		double nIn = Rms(in, 0.5, 1.4), nOut = Rms(buf, 0.5 + lat, 1.4 + lat);
		double db = 20 * log10(nOut / nIn);
		printf("profile: noise %.4f -> %.4f (%.1f dB)\n", nIn, nOut, db);
		CHECK(db < -8.0, "profile reduction only %.1f dB", db);
		double tIn = 0, tOut = 0;
		for (int k = 0; k < 2; k++) {
			double a = 2.0 + k * 0.5 + 0.05, b = a + 0.15;
			tIn += Rms(in, a, b); tOut += Rms(buf, a + lat, b + lat);
		}
		double tdb = 20 * log10(tOut / tIn);
		printf("profile: tone %.1f dB\n", tdb);
		CHECK(fabs(tdb) < 1.5, "profile tone changed by %.1f dB", tdb);
	}

	// ---- слушать удаляемое: вместе с обработанным даёт исходный сигнал ----
	{
		Ladspa::Runner r1, r2; std::string err; r1.Start(p, kRate, 2, &err); r2.Start(p, kRate, 2, &err);
		std::vector<float> c = Defaults();
		for (size_t i = 0; i < c.size(); i++) { r1.SetControl((int)i, c[i]); r2.SetControl((int)i, c[i]); }
		r2.SetControl(NoiseReducer::P_LISTEN, 1);
		const int frames = (int)(kRate * 3);
		std::vector<float> in(frames * 2);
		for (int i = 0; i < frames; i++) in[i*2] = in[i*2+1] = Noise(0.05f) + 0.2f * sinf(2 * 3.14159265f * 300.0f * i / kRate);
		std::vector<float> a = in, b = in;
		RunBlocks(r1, a, 512); RunBlocks(r2, b, 512);
		double maxErr = 0; const int L = NoiseReducer::kFrame;
		for (int i = L; i < frames; i++) maxErr = fmax(maxErr, fabs(a[i*2] + b[i*2] - in[(i - L) * 2]));
		CHECK(maxErr < 1e-3, "clean+removed != input: %g", maxErr);
	}

	// ---- Reset не ломает работу, значения конечны, громкость в разумных пределах ----
	{
		Ladspa::Runner r; std::string err; r.Start(p, kRate, 2, &err);
		std::vector<float> c = Defaults(); c[NoiseReducer::P_REDUCTION] = 40; c[NoiseReducer::P_SENSITIVITY] = 4;
		for (size_t i = 0; i < c.size(); i++) r.SetControl((int)i, c[i]);
		std::vector<float> buf(8192 * 2);
		bool ok = true; float peak = 0;
		for (int round = 0; round < 6; round++) {
			for (size_t i = 0; i < buf.size(); i++) buf[i] = 0.5f * sinf(0.01f * (float)i) + Noise(0.1f);
			RunBlocks(r, buf, 512);
			for (size_t i = 0; i < buf.size(); i++) { if (!isfinite(buf[i])) ok = false; if (fabsf(buf[i]) > peak) peak = fabsf(buf[i]); }
			if (round == 2) r.Reset();
		}
		CHECK(ok && peak < 1.5f, "extreme: ok=%d peak=%f", ok, peak);
		CHECK(r.Latency() == NoiseReducer::kFrame, "latency after reset %d", r.Latency());
	}

	// ---- моно-буфер ----
	{
		Ladspa::Runner r; std::string err;
		CHECK(r.Start(p, kRate, 1, &err), "mono %s", err.c_str());
		std::vector<float> b(5000, 0.1f);
		r.Process(b.data(), 5000);
		CHECK(isfinite(b[4000]), "mono finite");
	}

	printf(fails ? "FAILED %d\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}
