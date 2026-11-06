// Проверка SpringReverb на Linux: через Ladspa::Runner как встроенный плагин.
#include "LadspaHost.h"
#include "../../plugins/spring_reverb/SpringReverb.h"
#include <stdio.h>
#include <math.h>
#include <string>
#include <vector>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static const float kRate = 44100.0f;

static Ladspa::Plugin Find()
{
	std::vector<std::string> dirs; dirs.push_back("build");		// сюда скрипт кладёт spring_reverb.so
	std::vector<Ladspa::Plugin> all = Ladspa::Scan(dirs);
	for (size_t i = 0; i < all.size(); i++)
		if (all[i].label == "beae_spring_reverb") return all[i];
	printf("FAIL: plugin not found\n"); fails++;
	return Ladspa::Plugin();
}

// прогон: интерливд-стерео, импульс в начале
static std::vector<float> Impulse(const Ladspa::Plugin &p, const std::vector<float> &ctl, int frames, float amp = 1.0f)
{
	Ladspa::Runner r;
	std::string err;
	if (!r.Start(p, kRate, 2, &err)) { printf("FAIL start: %s\n", err.c_str()); fails++; return std::vector<float>(); }
	for (size_t i = 0; i < ctl.size(); i++) r.SetControl((int)i, ctl[i]);
	std::vector<float> buf(frames * 2, 0.0f);
	buf[0] = buf[1] = amp;
	// блоками по 512 кадров (как в аудио-потоке)
	for (int pos = 0; pos < frames; pos += 512) {
		int n = frames - pos < 512 ? frames - pos : 512;
		r.Process(&buf[pos * 2], n);
	}
	return buf;
}

static Ladspa::Plugin gPlugin;
static std::vector<float> Defaults()
{
	std::vector<float> c(SpringReverb::P_COUNT, 0.0f);
	for (int i = 0; i < SpringReverb::P_IN_L; i++) c[i] = Ladspa::ResolveRange(gPlugin.ports[i], kRate).def;
	return c;
}

int main()
{
	Ladspa::Plugin p = Find();
	gPlugin = p;
	CHECK(p.mode == Ladspa::MODE_STEREO_STEREO, "mode");
	CHECK(!Ladspa::IsBuiltin(p), "not builtin");

	// значения по умолчанию из подсказок LADSPA (их увидит любой хост)
	{
		const float want[SpringReverb::P_IN_L] = { 0.5f, 0.5f, 0.0f, 0.25f, 3.0f, 60.0f, 100.0f, 5981.0f, 0.25f, 0.5f };
		for (int i = 0; i < SpringReverb::P_IN_L; i++) {
			Ladspa::Range r = Ladspa::ResolveRange(p.ports[i], kRate);
			CHECK(fabsf(r.def - want[i]) < want[i] * 0.01f + 0.01f, "default port %d: %f vs %f", i, r.def, want[i]);
		}
		CHECK(std::vector<int>(p.ControlInputs()).size() == SpringReverb::P_IN_L, "control count");
	}

	// диапазоны заданы как просил пользователь
	Ladspa::Range lc = Ladspa::ResolveRange(p.ports[SpringReverb::P_LOWCUT], kRate);
	Ladspa::Range hc = Ladspa::ResolveRange(p.ports[SpringReverb::P_HIGHCUT], kRate);
	Ladspa::Range sp = Ladspa::ResolveRange(p.ports[SpringReverb::P_SPRINGS], kRate);
	CHECK(lc.lower == 0 && lc.upper == 300, "lowcut range");
	CHECK(hc.lower == 4000 && hc.upper == 20000, "highcut range");
	CHECK(sp.lower == 3 && sp.upper == 5 && sp.integer, "springs range");

	// mix = 0: выход равен входу
	{
		std::vector<float> c = Defaults();
		c[SpringReverb::P_MIX] = 0.0f;
		std::vector<float> o = Impulse(p, c, 4096);
		CHECK(fabsf(o[0] - 1.0f) < 1e-5f, "dry impulse %f", o[0]);
		float rest = 0; for (size_t i = 2; i < o.size(); i++) rest += fabsf(o[i]);
		CHECK(rest < 1e-3f, "dry tail %f", rest);
	}

	// по умолчанию: есть хвост, он конечен и затухает
	{
		std::vector<float> o = Impulse(p, Defaults(), (int)(kRate * 6), 0.5f);
		bool finite = true; float peak = 0;
		for (size_t i = 0; i < o.size(); i++) { if (!isfinite(o[i])) finite = false; if (fabsf(o[i]) > peak) peak = fabsf(o[i]); }
		CHECK(finite, "finite");
		CHECK(peak < 2.0f, "peak %f", peak);
		auto energy = [&](double a, double b) {
			double e = 0; for (int i = (int)(a * kRate); i < (int)(b * kRate); i++) e += o[i * 2] * o[i * 2];
			return e;
		};
		double e1 = energy(0.02, 0.5), e2 = energy(2.0, 2.5), e3 = energy(5.0, 6.0);
		CHECK(e1 > 1e-8, "no reverb tail e1=%g", e1);
		CHECK(e2 < e1, "tail not decaying e1=%g e2=%g", e1, e2);
		CHECK(e3 < e2 * 1.0 + 1e-12, "tail late e2=%g e3=%g", e2, e3);
		printf("tail energy 0.02-0.5s=%g, 2-2.5s=%g, 5-6s=%g\n", e1, e2, e3);
	}

	// Decay на максимуме даёт длинный хвост (около 4 с), на минимуме - короткий
	{
		auto tail = [&](float decay) {
			std::vector<float> c = Defaults(); c[SpringReverb::P_DECAY] = decay; c[SpringReverb::P_MIX] = 1;
			std::vector<float> o = Impulse(p, c, (int)(kRate * 3.5), 0.5f);
			double e = 0; for (int i = (int)(kRate * 1.5); i < (int)(kRate * 3.0); i++) e += o[i * 2] * o[i * 2];
			return e;
		};
		double lo = tail(0.1f), hi = tail(1.0f);
		CHECK(hi > lo * 1000.0 && hi > 1e-9, "decay lo=%g hi=%g", lo, hi);
	}

	// первое эхо рядом со временем круга (Springs=1, Boing=0, без нелинейности)
	{
		std::vector<float> c = Defaults();
		c[SpringReverb::P_SPRINGS] = 3; c[SpringReverb::P_BOING] = 0; c[SpringReverb::P_NONLIN] = 0;
		c[SpringReverb::P_DELAY] = 100; c[SpringReverb::P_MIX] = 1; c[SpringReverb::P_LOWCUT] = 0;
		c[SpringReverb::P_HIGHCUT] = 20000;
		std::vector<float> o = Impulse(p, c, (int)(kRate * 0.4), 0.5f);
		int peakAt = 0;
		for (int i = (int)(0.03 * kRate); i < (int)(0.35 * kRate); i++)
			if (fabsf(o[i * 2]) > 0.01f) { peakAt = i; break; }		// первое заметное эхо
		double ms = peakAt * 1000.0 / kRate;
		CHECK(ms > 90 && ms < 125, "first echo at %.1f ms", ms);
		printf("first echo at %.1f ms (delay 100)\n", ms);
	}

	// крайние значения: ничего не взрывается, при любом числе пружин
	for (int sprn = 3; sprn <= 5; sprn++) {
		std::vector<float> c = Defaults();
		c[SpringReverb::P_SPRINGS] = (float)sprn; c[SpringReverb::P_DECAY] = 1; c[SpringReverb::P_DWELL] = 1;
		c[SpringReverb::P_NONLIN] = 1; c[SpringReverb::P_BOING] = 1; c[SpringReverb::P_MIX] = 1;
		c[SpringReverb::P_DELAY] = 210; c[SpringReverb::P_TONE] = 1;
		std::vector<float> o = Impulse(p, c, (int)(kRate * 3), 1.0f);
		float peak = 0; bool ok = true;
		for (size_t i = 0; i < o.size(); i++) { if (!isfinite(o[i])) ok = false; if (fabsf(o[i]) > peak) peak = fabsf(o[i]); }
		CHECK(ok && peak < 4.0f, "extreme springs=%d peak=%f", sprn, peak);
	}

	// непрерывный сигнал: синус, без щелчков и NaN, громкость в разумных пределах
	{
		Ladspa::Runner r; std::string err;
		r.Start(p, kRate, 2, &err);
		std::vector<float> c = Defaults();
		for (size_t i = 0; i < c.size(); i++) r.SetControl((int)i, c[i]);
		float peak = 0; bool ok = true;
		std::vector<float> b(1024 * 2);
		for (int blk = 0; blk < 200; blk++) {
			for (int i = 0; i < 1024; i++) { float v = 0.5f * sinf(2 * 3.14159265f * 440.0f * (blk * 1024 + i) / kRate); b[i*2] = b[i*2+1] = v; }
			r.Process(b.data(), 1024);
			for (size_t i = 0; i < b.size(); i++) { if (!isfinite(b[i])) ok = false; if (fabsf(b[i]) > peak) peak = fabsf(b[i]); }
			if (blk == 100) { r.SetControl(SpringReverb::P_DELAY, 120); r.SetControl(SpringReverb::P_SPRINGS, 5); }
		}
		CHECK(ok && peak < 2.0f, "sine peak=%f", peak);
	}

	// моно-буфер (1 канал) тоже работает
	{
		Ladspa::Runner r; std::string err;
		CHECK(r.Start(p, kRate, 1, &err), "mono start %s", err.c_str());
		std::vector<float> b(2048, 0.0f); b[0] = 0.5f;
		r.Process(b.data(), 2048);
		CHECK(isfinite(b[100]), "mono finite");
	}

	printf(fails ? "FAILED %d\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}
