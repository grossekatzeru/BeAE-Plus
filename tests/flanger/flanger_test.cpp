// Проверка "Фленджера" на Linux через Ladspa::Runner.
#include "LadspaHost.h"
#include <stdio.h>
#include <math.h>
#include <string>
#include <vector>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

struct Par { float rate, depth, manual, fb, stereo, mix; };
static Par Def() { Par p = { 0.3f, 0.0f, 2.0f, 0.0f, 90.0f, 100.0f }; return p; }

static Ladspa::Plugin Find()
{
	std::vector<std::string> dirs; dirs.push_back("build");
	std::vector<Ladspa::Plugin> all = Ladspa::Scan(dirs);
	for (size_t i = 0; i < all.size(); i++) if (all[i].label == "beae_flanger") return all[i];
	printf("FAIL: plugin not found\n"); fails++;
	return Ladspa::Plugin();
}
static std::vector<float> Run(const Ladspa::Plugin &pl, float rate, const Par &p, std::vector<float> in, int block = 512)
{
	Ladspa::Runner r; std::string err;
	if (!r.Start(pl, rate, 2, &err)) { printf("FAIL start: %s\n", err.c_str()); fails++; return in; }
	const float v[6] = { p.rate, p.depth, p.manual, p.fb, p.stereo, p.mix };
	for (int i = 0; i < 6; i++) r.SetControl(i, v[i]);
	const int frames = (int)(in.size() / 2);
	for (int pos = 0; pos < frames; pos += block) { int n = frames - pos < block ? frames - pos : block; r.Process(&in[pos * 2], n); }
	return in;
}
static std::vector<float> Zeros(float rate, double sec) { return std::vector<float>((size_t)(sec * rate) * 2, 0.0f); }
static std::vector<float> Noise(float rate, double sec, float amp)
{
	std::vector<float> b((size_t)(sec * rate) * 2); unsigned s = 777;
	for (size_t i = 0; i < b.size(); i++) { s = s * 1664525u + 1013904223u; b[i] = amp * ((int)(s >> 8) / 8388608.0f - 1.0f); }
	return b;
}

int main()
{
	Ladspa::Plugin pl = Find();
	if (pl.label.empty()) return 1;

	// 1) смесь 0: без изменений
	{
		Par p = Def(); p.mix = 0; p.depth = 1; p.fb = 50;
		std::vector<float> in = Noise(44100, 0.3, 0.5f), out = Run(pl, 44100, p, in);
		double d = 0; for (size_t i = 0; i < in.size(); i++) d = fmax(d, fabs(in[i] - out[i]));
		CHECK(d < 1e-6, "passthrough %g", d);
	}
	// 2) неподвижный фленджер: эхо через "ручную" задержку (48 кГц, 2 мс = 96 кадров), смесь 100 % = амплитуда 1
	{
		const float rate = 48000; Par p = Def();
		std::vector<float> in = Zeros(rate, 0.2); in[0] = in[1] = 1;
		std::vector<float> out = Run(pl, rate, p, in);
		CHECK(fabs(out[96 * 2] - 1.0f) < 1e-4, "echo at 96: %.4f", out[96 * 2]);
		CHECK(fabs(out[0] - 1.0f) < 1e-6, "dry");
		double rest = 0; for (size_t i = 1; i < out.size() / 2; i++) if (i != 96) rest += fabs(out[i * 2]);
		CHECK(rest < 1e-4, "extra energy %g", rest);
		printf("static: echo %.4f at 96 frames\n", out[96 * 2]);
	}
	// 3) обратная связь: +50 % даёт второе эхо +0.5, -50 % даёт -0.5
	for (int sgn = -1; sgn <= 1; sgn += 2) {
		const float rate = 48000; Par p = Def(); p.fb = 50.0f * sgn;
		std::vector<float> in = Zeros(rate, 0.2); in[0] = in[1] = 1;
		std::vector<float> out = Run(pl, rate, p, in);
		CHECK(fabs(out[192 * 2] - 0.5f * sgn) < 1e-4, "second echo fb %d: %.4f", sgn * 50, out[192 * 2]);
		printf("feedback %+d%%: second echo %.4f\n", sgn * 50, out[192 * 2]);
	}
	// 4) качание: положение эха меняется от 0.2 до 3.8 мс при Глубине 1 и ручной 2 мс (диапазон 0.9*)
	{
		const float rate = 48000; Par p = Def(); p.depth = 1; p.rate = 0.5f;
		std::vector<float> in = Zeros(rate, 4.0);
		for (size_t t = 0; t < in.size() / 2; t += (size_t)(0.03 * rate)) in[t * 2] = in[t * 2 + 1] = 1;
		std::vector<float> out = Run(pl, rate, p, in);
		double mn = 1e9, mx = 0;
		for (size_t t = 0; t + (size_t)(0.03 * rate) < in.size() / 2; t += (size_t)(0.03 * rate)) {
			size_t pk = t + 3; float best = 0;
			for (size_t i = t + 3; i < t + (size_t)(0.026 * rate); i++) if (fabsf(out[i * 2]) > best) { best = fabsf(out[i * 2]); pk = i; }
			if (best < 0.3f) continue;
			const double ms = (pk - t) * 1000.0 / rate;
			mn = fmin(mn, ms); mx = fmax(mx, ms);
		}
		CHECK(mn < 0.5 && mx > 3.4 && mx < 4.0, "delay sweep %.2f..%.2f ms", mn, mx);
		printf("sweep: delay %.2f..%.2f ms (expected 0.2..3.8)\n", mn, mx);
	}
	// 5) стерео-разнесение: при 90 градусах задержки каналов различаются, при 0 одинаковы
	{
		const float rate = 48000; Par p = Def(); p.depth = 1; p.rate = 1.0f;
		std::vector<float> in = Zeros(rate, 0.1);
		const size_t t0 = (size_t)(0.05 * rate);		// фаза LFO 0.05 цикла
		in[t0 * 2] = in[t0 * 2 + 1] = 1;
		for (int st = 0; st <= 90; st += 90) {
			p.stereo = (float)st;
			std::vector<float> out = Run(pl, rate, p, in);
			size_t pl_ = t0 + 1, pr = t0 + 1; float bl = 0, br = 0;
			for (size_t i = t0 + 3; i < t0 + 300; i++) {
				if (fabsf(out[i * 2]) > bl) { bl = fabsf(out[i * 2]); pl_ = i; }
				if (fabsf(out[i * 2 + 1]) > br) { br = fabsf(out[i * 2 + 1]); pr = i; }
			}
			printf("stereo %d deg: L %zu R %zu frames\n", st, pl_ - t0, pr - t0);
			if (st == 0) CHECK(pl_ == pr, "channels differ at 0 deg");
			else CHECK(llabs((long long)pl_ - (long long)pr) > 20, "channels equal at 90 deg");
		}
	}
	// 6) блоки разной длины дают то же самое
	{
		Par p = Def(); p.depth = 0.8f; p.fb = 40; p.rate = 2;
		std::vector<float> in = Noise(44100, 0.5, 0.4f), a = Run(pl, 44100, p, in, 512), b = Run(pl, 44100, p, in, 29);
		double d = 0; for (size_t i = 0; i < a.size(); i++) d = fmax(d, fabs(a[i] - b[i]));
		CHECK(d < 1e-5, "block dependence %g", d);
	}
	// 7) устойчивость при предельных настройках
	{
		Par p = { 10.0f, 1.0f, 10.0f, 95.0f, 180.0f, 100.0f };
		std::vector<float> out = Run(pl, 44100, p, Noise(44100, 4.0, 1.0f));
		float peak = 0; bool fin = true;
		for (size_t i = 0; i < out.size(); i++) { if (!isfinite(out[i])) fin = false; else peak = fmaxf(peak, fabsf(out[i])); }
		CHECK(fin && peak < 60.0f, "unstable finite %d peak %g", (int)fin, peak);
		printf("extreme: peak %.1f finite %d\n", peak, (int)fin);
		p.fb = -95; out = Run(pl, 44100, p, Noise(44100, 4.0, 1.0f));
		peak = 0; fin = true;
		for (size_t i = 0; i < out.size(); i++) { if (!isfinite(out[i])) fin = false; else peak = fmaxf(peak, fabsf(out[i])); }
		CHECK(fin && peak < 60.0f, "unstable (neg) finite %d peak %g", (int)fin, peak);
	}
	printf(fails ? "FAILED %d\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}
