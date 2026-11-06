// Проверка плагина САДП на Linux: .so грузится через хост BeAE.
#include "LadspaHost.h"
#include "../../plugins/sadp/Sadp.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string>
#include <vector>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static const float kRate = 44100.0f;
static const float kPi = 3.14159265f;
static Ladspa::Plugin gP;

static float Hiss(float rms)
{
	float s = 0; for (int i = 0; i < 12; i++) s += (float)rand() / RAND_MAX - 0.5f;	// сумма 12 равномерных: дисперсия 1
	return s * rms;
}
static float Db(double x) { return 20 * log10(x); }

static std::vector<float> Defaults()
{
	std::vector<float> c(Sadp::P_COUNT, 0.0f);
	for (int i = 0; i < Sadp::P_IN_L; i++) c[i] = Ladspa::ResolveRange(gP.ports[i], kRate).def;
	return c;
}

// прогон моно-сигнала через плагин (L = R)
static std::vector<float> Process(const std::vector<float> &in, const std::vector<float> &ctl, int block = 512)
{
	Ladspa::Runner r; std::string err;
	if (!r.Start(gP, kRate, 2, &err)) { printf("FAIL start %s\n", err.c_str()); fails++; return in; }
	for (size_t i = 0; i < ctl.size(); i++) r.SetControl((int)i, ctl[i]);
	std::vector<float> buf(in.size() * 2);
	for (size_t i = 0; i < in.size(); i++) buf[i * 2] = buf[i * 2 + 1] = in[i];
	for (size_t pos = 0; pos < in.size(); pos += block) {
		size_t n = in.size() - pos < (size_t)block ? in.size() - pos : block;
		r.Process(&buf[pos * 2], n);
	}
	std::vector<float> out(in.size());
	for (size_t i = 0; i < in.size(); i++) out[i] = buf[i * 2];
	return out;
}

static double Rms(const std::vector<float> &v, double a, double b)
{
	double e = 0; int n = 0;
	for (int i = (int)(a * kRate); i < (int)(b * kRate) && i < (int)v.size(); i++) { e += v[i] * v[i]; n++; }
	return sqrt(e / (n ? n : 1));
}

// простой фильтр высоких (2 звена по 12 дБ) для измерения энергии выше f
static std::vector<float> HighPass(const std::vector<float> &x, float f)
{
	std::vector<float> y = x;
	for (int pass = 0; pass < 2; pass++) {
		const float w = 2 * kPi * f / kRate, c = cosf(w), s = sinf(w), al = s / (2 * 0.7071f), a0 = 1 + al;
		const float b0 = (1 + c) / 2 / a0, b1 = -(1 + c) / a0, b2 = b0, a1 = -2 * c / a0, a2 = (1 - al) / a0;
		float z1 = 0, z2 = 0;
		for (size_t i = 0; i < y.size(); i++) { float o = b0 * y[i] + z1; z1 = b1 * y[i] - a1 * o + z2; z2 = b2 * y[i] - a2 * o; y[i] = o; }
	}
	return y;
}

int main()
{
	srand(7);
	std::vector<std::string> dirs; dirs.push_back("build");
	std::vector<Ladspa::Plugin> all = Ladspa::Scan(dirs);
	bool found = false;
	for (size_t i = 0; i < all.size(); i++) if (all[i].label == "beae_sadp") { gP = all[i]; found = true; }
	CHECK(found, "plugin not found");
	if (!found) return 1;
	CHECK(gP.latencyPort == -1, "no latency expected");
	CHECK(gP.ControlInputs().size() == Sadp::P_IN_L, "control count");

	// значения по умолчанию из подсказок LADSPA
	{
		const float want[Sadp::P_IN_L] = { -55, 40, 40, 15, 5.75f, 141.4f, 141.4f, 4000, 0 };
		for (int i = 0; i < Sadp::P_IN_L; i++) {
			Ladspa::Range r = Ladspa::ResolveRange(gP.ports[i], kRate);
			CHECK(fabsf(r.def - want[i]) < fabsf(want[i]) * 0.02f + 0.05f, "default %d: %f vs %f", i, r.def, want[i]);
		}
	}

	// ---- прозрачность: подавление 0 дБ, амплитуда синусов в разных полосах сохраняется ----
	{
		std::vector<float> c = Defaults(); c[Sadp::P_DEPTH] = 0;
		const float freqs[] = { 60, 100, 200, 500, 1000, 2000, 3500, 5000, 8000, 12000, 16000 };
		for (size_t k = 0; k < sizeof(freqs) / sizeof(freqs[0]); k++) {
			std::vector<float> in((int)(kRate * 1.5));
			for (size_t i = 0; i < in.size(); i++) in[i] = 0.3f * sinf(2 * kPi * freqs[k] * i / kRate);
			std::vector<float> out = Process(in, c);
			double d = Db(Rms(out, 0.5, 1.4) / Rms(in, 0.5, 1.4));
			CHECK(fabs(d) < 0.3, "transparency %g Hz: %.2f dB", freqs[k], d);
		}
		std::vector<float> in((int)(kRate * 2));
		for (size_t i = 0; i < in.size(); i++) in[i] = Hiss(0.1f);
		std::vector<float> out = Process(in, c);
		double d = Db(Rms(out, 0.5, 1.9) / Rms(in, 0.5, 1.9));
		CHECK(fabs(d) < 0.3, "transparency noise: %.2f dB", d);
	}

	// ---- громкий широкополосный сигнал: все полосы открыты, ослабления нет ----
	{
		std::vector<float> c = Defaults();
		std::vector<float> in((int)(kRate * 2));
		for (size_t i = 0; i < in.size(); i++) in[i] = Hiss(0.1f);
		std::vector<float> out = Process(in, c);
		double d = Db(Rms(out, 0.8, 1.9) / Rms(in, 0.8, 1.9));
		printf("loud broadband: %.2f dB\n", d);
		CHECK(fabs(d) < 0.7, "loud broadband attenuated %.2f dB", d);
	}

	// ---- шипение при музыке только на средних: ВЧ шум падает, тоны сохраняются ----
	{
		std::vector<float> c = Defaults();
		const int N = (int)(kRate * 4);
		std::vector<float> tones(N), hiss(N), in(N);
		for (int i = 0; i < N; i++) {
			tones[i] = 0.1f * sinf(2 * kPi * 440.0f * i / kRate) + 0.07f * sinf(2 * kPi * 880.0f * i / kRate);
			hiss[i] = Hiss(0.0006f);		// около -64 дБ относительно полной шкалы
			in[i] = tones[i] + hiss[i];
		}
		std::vector<float> out = Process(in, c);
		std::vector<float> hin = HighPass(in, 8000), hout = HighPass(out, 8000);
		double hfDb = Db(Rms(hout, 2.0, 3.9) / Rms(hin, 2.0, 3.9));
		// тоны: сравниваем через узкую фильтрацию вычитанием: уровень на 440 Гц по корреляции
		double a = 0, b = 0, ca = 0, cb = 0;
		for (int i = (int)(kRate * 2); i < (int)(kRate * 3.9); i++) {
			float s = sinf(2 * kPi * 440.0f * i / kRate), cs = cosf(2 * kPi * 440.0f * i / kRate);
			a += in[i] * s; ca += in[i] * cs; b += out[i] * s; cb += out[i] * cs;
		}
		double toneDb = Db(sqrt(b * b + cb * cb) / sqrt(a * a + ca * ca));
		printf("hiss with tones: HF noise %.1f dB, 440 Hz tone %.2f dB\n", hfDb, toneDb);
		CHECK(hfDb < -9.0, "HF noise reduced only %.1f dB", hfDb);
		CHECK(fabs(toneDb) < 0.3, "tone changed by %.2f dB", toneDb);
	}

	// ---- всплеск 10 кГц при шипении: полоса быстро открывается и сигнал не искажён ----
	{
		std::vector<float> c = Defaults();
		const int N = (int)(kRate * 3.5);
		std::vector<float> in(N);
		for (int i = 0; i < N; i++) {
			float t = i / kRate;
			float burst = (t > 2.0 && t < 2.5) ? 0.03f * sinf(2 * kPi * 10000.0f * t) : 0.0f;		// около -30 дБ
			in[i] = 0.1f * sinf(2 * kPi * 440.0f * t) + Hiss(0.0006f) + burst;
		}
		std::vector<float> out = Process(in, c);
		auto env10k = [&](const std::vector<float> &v, double a, double b) {
			double sa = 0, ca = 0;
			for (int i = (int)(a * kRate); i < (int)(b * kRate); i++) { double t = i / kRate; sa += v[i] * sin(2 * kPi * 10000.0 * t); ca += v[i] * cos(2 * kPi * 10000.0 * t); }
			return sqrt(sa * sa + ca * ca);
		};
		double fast = Db(env10k(out, 2.02, 2.04) / env10k(in, 2.02, 2.04));		// через 20-40 мс после начала
		double steady = Db(env10k(out, 2.2, 2.4) / env10k(in, 2.2, 2.4));
		printf("burst 10k: after 20-40 ms %.2f dB, steady %.2f dB\n", fast, steady);
		CHECK(fast > -1.5, "burst opens too slowly: %.2f dB", fast);
		CHECK(fabs(steady) < 0.5, "burst level changed %.2f dB", steady);
	}

	// ---- низкочастотный фон (гул 50 Гц ниже порога) приглушается, когда есть основной сигнал ----
	{
		std::vector<float> c = Defaults();
		const int N = (int)(kRate * 4);
		std::vector<float> in(N);
		for (int i = 0; i < N; i++) {
			float t = i / kRate;
			in[i] = 0.2f * sinf(2 * kPi * 1000.0f * t) + 0.0004f * sinf(2 * kPi * 50.0f * t);		// гул на 54 дБ ниже сигнала
		}
		std::vector<float> out = Process(in, c);
		double a = 0, ca = 0, b = 0, cb = 0;
		for (int i = (int)(kRate * 2); i < (int)(kRate * 3.9); i++) {
			double t = i / kRate;
			a += in[i] * sin(2 * kPi * 50.0 * t); ca += in[i] * cos(2 * kPi * 50.0 * t);
			b += out[i] * sin(2 * kPi * 50.0 * t); cb += out[i] * cos(2 * kPi * 50.0 * t);
		}
		double d = Db(sqrt(b * b + cb * cb) / sqrt(a * a + ca * ca));
		printf("LF hum: %.1f dB\n", d);
		CHECK(d < -8.0, "LF hum reduced only %.1f dB", d);
	}

	// ---- слушать удаляемое + результат = вход ----
	{
		std::vector<float> c = Defaults(), c2 = c; c2[Sadp::P_LISTEN] = 1;
		const int N = (int)(kRate * 2);
		std::vector<float> in(N);
		for (int i = 0; i < N; i++) in[i] = 0.1f * sinf(2 * kPi * 440.0f * i / kRate) + Hiss(0.0006f);
		std::vector<float> a = Process(in, c), b = Process(in, c2);
		double maxErr = 0;
		for (int i = 0; i < N; i++) maxErr = fmax(maxErr, fabs(a[i] + b[i] - in[i]));
		CHECK(maxErr < 1e-4, "clean+removed != input: %g", maxErr);
	}

	// ---- крайние значения параметров и моно-буфер: всё конечно ----
	{
		const float lo[Sadp::P_IN_L] = { -80, 20, 20, 30, 1, 20, 50, 2000, 0 };
		const float hi[Sadp::P_IN_L] = { -30, 60, 60, 0, 20, 1000, 400, 8000, 1 };
		for (int v = 0; v < 2; v++) {
			std::vector<float> c(Sadp::P_COUNT, 0.0f);
			for (int i = 0; i < Sadp::P_IN_L; i++) c[i] = v ? hi[i] : lo[i];
			std::vector<float> in((int)(kRate * 2));
			for (size_t i = 0; i < in.size(); i++) in[i] = 0.5f * sinf(0.02f * i) + Hiss(0.05f);
			std::vector<float> out = Process(in, c, 333);
			bool ok = true; float peak = 0;
			for (size_t i = 0; i < out.size(); i++) { if (!isfinite(out[i])) ok = false; if (fabsf(out[i]) > peak) peak = fabsf(out[i]); }
			CHECK(ok && peak < 3.0f, "extreme set %d: ok=%d peak=%f", v, ok, peak);
		}
		Ladspa::Runner r; std::string err;
		CHECK(r.Start(gP, kRate, 1, &err), "mono start %s", err.c_str());
		std::vector<float> b(3000, 0.1f); r.Process(b.data(), 3000);
		CHECK(isfinite(b[2000]), "mono finite");
	}

	printf(fails ? "FAILED %d\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}
