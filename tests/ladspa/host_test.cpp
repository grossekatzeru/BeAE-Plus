#include "LadspaHost.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <strings.h>
using namespace Ladspa;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
static bool near(float a, float b, float e = 1e-4f) { return fabsf(a - b) < e; }
static const Plugin *Find(const std::vector<Plugin> &v, const char *label) {
	for (size_t i = 0; i < v.size(); ++i)
		if (v[i].label == label) return &v[i];
	return 0; }

int main(int argc, char **argv)
{
	std::vector<std::string> dirs; dirs.push_back(argc > 1 ? argv[1] : ".");
	std::vector<std::string> log;
	std::vector<Plugin> list = Scan(dirs, &log);
	for (size_t i = 0; i < log.size(); ++i) printf("log: %s\n", log[i].c_str());
	CHECK(list.size() == 6);
	// сортировка по имени без учёта регистра
	for (size_t i = 1; i < list.size(); ++i) CHECK(strcasecmp(list[i-1].name.c_str(), list[i].name.c_str()) <= 0);

	const Plugin *amp = Find(list, "amp"), *st = Find(list, "stereo"), *wd = Find(list, "widen"),
		*sm = Find(list, "sum"), *mt = Find(list, "meter"), *gn = Find(list, "gen");
	CHECK(amp && st && wd && sm && mt && gn);
	CHECK(amp->mode == MODE_MONO_MONO && st->mode == MODE_STEREO_STEREO && wd->mode == MODE_MONO_STEREO
		  && sm->mode == MODE_STEREO_MONO && mt->mode == MODE_MONO_MONO && gn->mode == MODE_UNSUPPORTED);
	CHECK(amp->ControlInputs().size() == 1 && st->ControlInputs().size() == 2 && mt->ControlInputs().size() == 1);

	// диапазоны
	Range r = ResolveRange(amp->ports[0], 48000);
	CHECK(near(r.lower, 0) && near(r.upper, 4) && near(r.def, 1) && !r.toggled && !r.integer);
	r = ResolveRange(st->ports[1], 48000);
	CHECK(r.toggled && near(r.def, 0) && near(r.upper, 1));
	r = ResolveRange(mt->ports[0], 48000);       // log, sample-rate, MIDDLE: sqrt(48 * 21600)
	CHECK(r.logarithmic && near(r.lower, 48.0f, 1e-2f) && near(r.upper, 21600.0f, 1e-1f));
	CHECK(near(r.def, sqrtf(48.0f * 21600.0f), 0.5f));

	// ---- обработка ----
	const size_t N = 5000;				// больше kBlock = 2048 (проверка разбиения)
	std::vector<float> a(N * 2), b;
	for (size_t i = 0; i < N; ++i) { a[i*2] = (float)(i % 100) / 100.0f; a[i*2+1] = -(float)(i % 50) / 100.0f; }

	std::string err;
	{	// mono плагин на стерео-буфере: два экземпляра, каждый канал отдельно
		Runner r1;
		CHECK(r1.Start(*amp, 48000, 2, &err));
		r1.SetControl(0, 2.0f);
		b = a; r1.Process(b.data(), N);
		bool ok = true;
		for (size_t i = 0; i < N; ++i) if (!near(b[i*2], a[i*2]*2) || !near(b[i*2+1], a[i*2+1]*2)) { ok = false; break; }
		CHECK(ok);
		CHECK(near(r1.Control(0), 2.0f));
		r1.Stop(); CHECK(!r1.IsRunning());
	}
	{	// mono плагин на моно-буфере
		Runner r1; CHECK(r1.Start(*amp, 44100, 1, &err));
		std::vector<float> m(N); for (size_t i = 0; i < N; ++i) m[i] = a[i*2];
		std::vector<float> o = m; r1.Process(o.data(), N);        // gain по умолчанию = 1
		bool ok = true; for (size_t i = 0; i < N; ++i) if (!near(o[i], m[i])) { ok = false; break; } CHECK(ok);
	}
	{	// stereo плагин: gain 0.5, инверсия правого канала
		Runner r1; CHECK(r1.Start(*st, 48000, 2, &err));
		r1.SetControl(0, 0.5f); r1.SetControl(1, 1.0f);
		b = a; r1.Process(b.data(), N);
		bool ok = true;
		for (size_t i = 0; i < N; ++i) if (!near(b[i*2], a[i*2]*0.5f) || !near(b[i*2+1], -a[i*2+1]*0.5f)) { ok = false; break; }
		CHECK(ok);
	}
	{	// stereo плагин на моно-буфере: берём левый выход (вход продублирован)
		Runner r1; CHECK(r1.Start(*st, 48000, 1, &err));
		r1.SetControl(0, 2.0f);
		std::vector<float> m(100, 0.25f); r1.Process(m.data(), 100);
		CHECK(near(m[0], 0.5f) && near(m[99], 0.5f));
	}
	{	// 1->2 на стерео-буфере: вход = среднее L/R, выходы L и R*0.5
		Runner r1; CHECK(r1.Start(*wd, 48000, 2, &err));
		b = a; r1.Process(b.data(), N);
		float mid = 0.5f * (a[2*77] + a[2*77+1]);
		CHECK(near(b[2*77], mid) && near(b[2*77+1], mid * 0.5f));
	}
	{	// 2->1 на стерео-буфере: сумма на оба канала
		Runner r1; CHECK(r1.Start(*sm, 48000, 2, &err));
		b = a; r1.Process(b.data(), N);
		float s = a[2*33] + a[2*33+1];
		CHECK(near(b[2*33], s) && near(b[2*33+1], s));
	}
	{	// выходной управляющий порт и непрерывность состояния между вызовами
		Runner r1; CHECK(r1.Start(*mt, 48000, 1, &err));
		std::vector<float> m(100, 0.0f); m[7] = 0.9f;
		r1.Process(m.data(), 100);
		CHECK(near(r1.Control(1), 0.9f));
		r1.Process(m.data(), 100);       // повторный вызов не должен ломаться
		CHECK(near(r1.Control(1), 0.9f));
	}
	{	// неподдерживаемый плагин
		Runner r1; CHECK(!r1.Start(*gn, 48000, 2, &err));
		CHECK(!err.empty());
		CHECK(!r1.IsRunning());
		r1.Process(a.data(), 10);       // не падает
	}
	{	// повторный Start на том же Runner (то же, что делает ExecuteFilter после предпросмотра)
		Runner r1; CHECK(r1.Start(*amp, 48000, 2, &err)); r1.SetControl(0, 3.0f);
		CHECK(r1.Start(*amp, 48000, 2, &err));
		CHECK(near(r1.Control(0), 1.0f));      // значения сброшены к умолчанию
	}
	// значения: сохранить/прочитать
	{
		Values v; v[0] = 1.5f; v[3] = -2.25f;
		CHECK(SaveValues("vals.tmp", v));
		Values w; CHECK(LoadValues("vals.tmp", &w));
		CHECK(w.size() == 2 && near(w[0], 1.5f) && near(w[3], -2.25f));
		Values none; CHECK(!LoadValues("no_such_file.tmp", &none));
		remove("vals.tmp");
	}
	// пути
	setenv("LADSPA_PATH", "/a:/b::/c", 1);
	std::vector<std::string> dp = DefaultPaths();
	CHECK(dp.size() >= 3 && dp[0] == "/a" && dp[1] == "/b" && dp[2] == "/c");
	printf(fails ? "FAILED %d\n" : "all ok\n", fails);
	return fails != 0;
}
