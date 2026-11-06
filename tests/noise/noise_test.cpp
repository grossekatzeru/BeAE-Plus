// Проверка ядра шумодава (NoiseCore) на Linux.
#include "NoiseCore.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <vector>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static const float kRate = 44100.0f;
static const int L = NoiseCore::kFrame;

static float Noise(float sigma)	// примерно гауссов шум (сумма равномерных)
{
	float s = 0; for (int i = 0; i < 6; i++) s += (float)rand() / RAND_MAX - 0.5f;
	return s * sigma * 1.414f;
}
// "речь": тон 880 Гц включён 0.25 с из каждых 0.5 с
static float Gate(float t) { return fmodf(t, 0.5f) < 0.25f ? 1.0f : 0.0f; }

static double Rms(const std::vector<float> &v, int ch, int chans, double a, double b)
{
	double e = 0; int n = 0;
	for (int i = (int)(a * kRate); i < (int)(b * kRate) && i < (int)(v.size() / chans); i++) { float x = v[i * chans + ch]; e += x * x; n++; }
	return sqrt(e / (n ? n : 1));
}

static void Run(NoiseCore &c, std::vector<float> &buf, int chans, int block = 512)
{
	int frames = (int)(buf.size() / chans);
	for (int pos = 0; pos < frames; pos += block) {
		int n = frames - pos < block ? frames - pos : block;
		c.Process(&buf[(size_t)pos * chans], n, chans);
	}
}

int main()
{
	srand(1);

	// ---- подавление 0 дБ: выход = вход, сдвинутый на задержку (моно и стерео, блоки не кратны шагу) ----
	for (int chans = 1; chans <= 2; chans++) {
		NoiseCore c(kRate); c.SetParams(0, 2, 0.5f, false, false);
		const int frames = 20000;
		std::vector<float> in(frames * chans), buf;
		for (int i = 0; i < frames; i++)
			for (int k = 0; k < chans; k++) in[i * chans + k] = (k ? -1 : 1) * (0.3f * sinf(2 * 3.14159265f * 1000.0f * i / kRate) + Noise(0.05f));
		buf = in;
		Run(c, buf, chans, 300);
		double maxErr = 0;
		for (int i = L + 2048; i < frames; i++)
			for (int k = 0; k < chans; k++) maxErr = fmax(maxErr, fabs(buf[i * chans + k] - in[(i - L) * chans + k]));
		CHECK(maxErr < 1e-3, "identity error chans=%d: %g", chans, maxErr);
	}

	// ---- применение к выделению: вход + хвост из нулей, выход сдвинут назад ровно на задержку ----
	{
		NoiseCore c(kRate); c.SetParams(0, 2, 0.5f, false, false);
		const int total = 30011;				// не кратно блоку
		std::vector<float> in(total);
		for (int i = 0; i < total; i++) in[i] = 0.4f * sinf(2 * 3.14159265f * 500.0f * i / kRate);
		std::vector<float> res;
		const int block = 2048;
		for (int pos = 0; pos < total; pos += block) {
			int n = total - pos < block ? total - pos : block;
			std::vector<float> b(in.begin() + pos, in.begin() + pos + n);
			c.Process(&b[0], n, 1);
			res.insert(res.end(), b.begin(), b.end());
		}
		for (int flush = L; flush > 0; flush -= block) {
			int n = flush < block ? flush : block;
			std::vector<float> b(n, 0.0f);
			c.Process(&b[0], n, 1);
			res.insert(res.end(), b.begin(), b.end());
		}
		double maxErr = 0;
		for (int i = 0; i < total; i++) maxErr = fmax(maxErr, fabs(res[i + L] - in[i]));
		// первые и последние кадры у краёв: окно ещё/уже без сигнала, ошибка допустима только в самом начале
		double edge = 0;
		for (int i = total - 8; i < total; i++) edge = fmax(edge, fabs(res[i + L] - in[i]));
		CHECK(maxErr < 0.05, "offline shift max error %g", maxErr);
		CHECK(edge < 1e-3, "offline tail error %g", edge);
		printf("offline: max error %.4f (start edge), tail %.1e\n", maxErr, edge);
	}

	// ---- авто-режим: шум 0..5 с, потом "речь"+шум; шум падает, тон остаётся ----
	{
		NoiseCore c(kRate); c.SetParams(12, 2, 0.5f, false, false);
		const int frames = (int)(kRate * 8);
		std::vector<float> in(frames);
		for (int i = 0; i < frames; i++) { float t = i / kRate; in[i] = Noise(0.05f) + (t > 5 ? Gate(t) * 0.3f * sinf(2 * 3.14159265f * 880.0f * t) : 0.0f); }
		std::vector<float> buf = in;
		Run(c, buf, 1);
		const double lat = L / (double)kRate;
		double db = 20 * log10(Rms(buf, 0, 1, 3.0 + lat, 4.5 + lat) / Rms(in, 0, 1, 3.0, 4.5));
		double tIn = 0, tOut = 0;
		for (int k = 0; k < 4; k++) { double a = 6.5 + k * 0.5 + 0.05, b = a + 0.15; tIn += Rms(in, 0, 1, a, b); tOut += Rms(buf, 0, 1, a + lat, b + lat); }
		double tdb = 20 * log10(tOut / tIn);
		printf("auto: noise %.1f dB, tone %.1f dB\n", db, tdb);
		CHECK(db < -8.0, "auto reduction only %.1f dB", db);
		CHECK(fabs(tdb) < 1.5, "auto tone changed by %.1f dB", tdb);
	}

	// ---- профиль: записываем шум (LearnFrom), потом работаем по профилю ----
	{
		const int part = (int)(kRate * 1.5);
		std::vector<float> noise(part);
		for (int i = 0; i < part; i++) noise[i] = Noise(0.05f);
		std::shared_ptr<const NoiseProfile> prof = NoiseCore::LearnFrom(&noise[0], part, 1, kRate);
		CHECK(prof && prof->seconds > 1.4 && !prof->bins[0].empty(), "profile not learned");
		NoiseCore c(kRate); c.SetParams(12, 2, 0.5f, true, false); c.SetProfile(prof);
		const int frames = (int)(kRate * 3);
		std::vector<float> in(frames);
		for (int i = 0; i < frames; i++) { float t = i / kRate; in[i] = Noise(0.05f) + (t > 1.5 ? Gate(t) * 0.3f * sinf(2 * 3.14159265f * 880.0f * t) : 0.0f); }
		std::vector<float> buf = in;
		Run(c, buf, 1);
		const double lat = L / (double)kRate;
		double db = 20 * log10(Rms(buf, 0, 1, 0.5 + lat, 1.4 + lat) / Rms(in, 0, 1, 0.5, 1.4));
		double tIn = 0, tOut = 0;
		for (int k = 0; k < 2; k++) { double a = 2.0 + k * 0.5 + 0.05, b = a + 0.15; tIn += Rms(in, 0, 1, a, b); tOut += Rms(buf, 0, 1, a + lat, b + lat); }
		double tdb = 20 * log10(tOut / tIn);
		printf("profile: noise %.1f dB, tone %.1f dB\n", db, tdb);
		CHECK(db < -8.0, "profile reduction only %.1f dB", db);
		CHECK(fabs(tdb) < 1.5, "profile tone changed by %.1f dB", tdb);
	}

	// ---- слушать удаляемое: вместе с обработанным даёт исходный сигнал ----
	{
		NoiseCore c1(kRate), c2(kRate);
		c1.SetParams(12, 2, 0.5f, false, false); c2.SetParams(12, 2, 0.5f, false, true);
		const int frames = (int)(kRate * 3);
		std::vector<float> in(frames * 2);
		for (int i = 0; i < frames; i++) in[i*2] = in[i*2+1] = Noise(0.05f) + 0.2f * sinf(2 * 3.14159265f * 300.0f * i / kRate);
		std::vector<float> a = in, b = in;
		Run(c1, a, 2); Run(c2, b, 2);
		double maxErr = 0;
		for (int i = L; i < frames; i++) maxErr = fmax(maxErr, fabs(a[i*2] + b[i*2] - in[(i - L) * 2]));
		CHECK(maxErr < 1e-3, "clean+removed != input: %g", maxErr);
	}

	// ---- Reset и крайние значения: всё конечно, громкость в разумных пределах ----
	{
		NoiseCore c(kRate); c.SetParams(40, 3, 1.0f, false, false);
		std::vector<float> buf(8192 * 2);
		bool ok = true; float peak = 0;
		for (int round = 0; round < 6; round++) {
			for (size_t i = 0; i < buf.size(); i++) buf[i] = 0.5f * sinf(0.01f * (float)i) + Noise(0.1f);
			Run(c, buf, 2);
			for (size_t i = 0; i < buf.size(); i++) { if (!isfinite(buf[i])) ok = false; if (fabsf(buf[i]) > peak) peak = fabsf(buf[i]); }
			if (round == 2) c.Reset();
		}
		CHECK(ok && peak < 1.5f, "extreme: ok=%d peak=%f", ok, peak);
	}

	printf(fails ? "FAILED %d\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}
