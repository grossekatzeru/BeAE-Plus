#include "Envelope.h"
#include <stdio.h>
#include <math.h>
#include <vector>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
static bool Near(double a, double b, double e = 1e-4) { return fabs(a - b) <= e; }

int main()
{
	// --- Envelope: добавление, порядок, интерполяция ---
	{
		Envelope e;
		CHECK(e.Empty());
		CHECK(Near(e.Value(1.0, -3.0f), -3.0f));		// нет точек: значение по умолчанию
		e.Add(2.0, -10.0f);
		e.Add(0.0, 0.0f);
		e.Add(4.0, -20.0f);
		CHECK(e.Count() == 3);
		CHECK(Near(e.Point(0).t, 0.0) && Near(e.Point(1).t, 2.0) && Near(e.Point(2).t, 4.0));
		CHECK(Near(e.Value(-1.0, 99), 0.0));			// до первой: значение первой
		CHECK(Near(e.Value(1.0, 99), -5.0));			// середина первого отрезка
		CHECK(Near(e.Value(3.0, 99), -15.0));
		CHECK(Near(e.Value(10.0, 99), -20.0));			// после последней
		int i = e.Add(2.00001, -12.0f);					// почти то же время: заменяет значение
		CHECK(e.Count() == 3 && i == 1 && Near(e.Point(1).v, -12.0));
		e.Remove(1);
		CHECK(e.Count() == 2 && Near(e.Value(2.0, 99), -10.0));
		e.Remove(7);									// несуществующая: ничего
		CHECK(e.Count() == 2);
	}
	// --- Move не перескакивает соседей ---
	{
		Envelope e;
		e.Add(1, 0); e.Add(2, 0); e.Add(3, 0);
		e.Move(1, 5.0, 1.0f);
		CHECK(e.Point(1).t < 3.0 && e.Point(1).t > 2.99 && Near(e.Point(1).v, 1.0));
		e.Move(1, -5.0, 0.0f);
		CHECK(e.Point(1).t > 1.0 && e.Point(1).t < 1.01);
		CHECK(e.Point(0).t < e.Point(1).t && e.Point(1).t < e.Point(2).t);
	}
	// --- дБ <-> усиление ---
	{
		CHECK(Near(Env::DbToGain(0), 1.0));
		CHECK(Near(Env::DbToGain(6), 1.99526, 1e-3));
		CHECK(Near(Env::DbToGain(-20), 0.1, 1e-4));
		CHECK(Env::DbToGain(Env::kDbMin) == 0.0f);
		CHECK(Env::DbToGain(-100) == 0.0f);
		CHECK(Near(Env::GainToDb(1.0f), 0.0));
		CHECK(Near(Env::GainToDb(0.0f), Env::kDbMin));
	}
	// --- ApplyStereo без огибающих: как раньше (центр = 1.0, к краю гасится противоположный) ---
	{
		std::vector<float> b(2 * 100, 1.0f);
		Env::ApplyStereo(&b[0], 100, 0, 1.0 / 1000, 0.5f, 0.5f, NULL, NULL);
		CHECK(Near(b[0], 0.25) && Near(b[1], 0.5));	// pan +0.5: левый x0.5, правый как есть
		std::vector<float> c(2 * 10, 1.0f);
		Env::ApplyStereo(&c[0], 10, 0, 0.001, 1.0f, 0.0f, NULL, NULL);
		CHECK(Near(c[0], 1.0) && Near(c[19], 1.0));
	}
	// --- затухание громкости: 0 дБ в 0 с, -60 дБ (тишина) в 1 с, частота 1000 Гц ---
	{
		Envelope v; v.Add(0.0, 0.0f); v.Add(1.0, -60.0f);
		std::vector<float> b(2 * 1000, 1.0f);
		Env::ApplyStereo(&b[0], 1000, 0.0, 0.001, 1.0f, 0.0f, &v, NULL);
		CHECK(Near(b[0], 1.0, 1e-3));						// начало 0 дБ
		const float mid = b[2 * 500];						// -30 дБ
		CHECK(Near(mid, Env::DbToGain(-30.0f), 2e-3));
		CHECK(b[2 * 999] < 0.002f);							// почти тишина
		for (int k = 1; k < 1000; k++) CHECK(b[2*k] <= b[2*(k-1)] + 1e-6);	// монотонно, без скачков вверх
		float maxStep = 0;
		for (int k = 1; k < 1000; k++) maxStep = fmaxf(maxStep, fabsf(b[2*k] - b[2*(k-1)]));
		CHECK(maxStep < 0.05f);								// нет щелчков
	}
	// --- панорама: огибающая от -1 (лево) до +1 (право) ---
	{
		Envelope p; p.Add(0.0, -1.0f); p.Add(1.0, 1.0f);
		std::vector<float> b(2 * 1000, 1.0f);
		Env::ApplyStereo(&b[0], 1000, 0.0, 0.001, 1.0f, 0.0f, NULL, &p);
		CHECK(Near(b[0], 1.0, 1e-3) && Near(b[1], 0.0, 1e-3));		// слева: правый канал погашен
		CHECK(Near(b[2*500], 1.0, 0.02) && Near(b[2*500+1], 1.0, 0.02));	// в середине центр: оба канала 1.0
		CHECK(Near(b[2*999], 0.0, 1e-2) && Near(b[2*999+1], 1.0, 1e-2));	// справа: левый погашен
	}
	// --- блоки равны одному куску (время считается непрерывно) ---
	{
		Envelope v; v.Add(0.0, 0.0f); v.Add(2.0, -30.0f); v.Add(3.0, 3.0f);
		std::vector<float> a(2 * 3000, 1.0f), c(2 * 3000, 1.0f);
		Env::ApplyStereo(&a[0], 3000, 0.0, 0.001, 1.0f, 0.0f, &v, NULL);
		for (int from = 0; from < 3000; from += 256) {
			int n = 3000 - from < 256 ? 3000 - from : 256;
			Env::ApplyStereo(&c[2 * from], n, from * 0.001, 0.001, 1.0f, 0.0f, &v, NULL);
		}
		double worst = 0;
		for (int k = 0; k < 6000; k++) worst = fmax(worst, fabs(a[k] - c[k]));
		CHECK(worst < 0.02);
	}
	// --- реверс: tstep отрицательный ---
	{
		Envelope v; v.Add(0.0, 0.0f); v.Add(1.0, -60.0f);
		std::vector<float> b(2 * 1000, 1.0f);
		Env::ApplyStereo(&b[0], 1000, 1.0, -0.001, 1.0f, 0.0f, &v, NULL);	// идём от конца к началу: громкость растёт
		CHECK(b[0] < 0.01f && b[2 * 999] > 0.95f);
	}
	// --- mute (baseGain = 0): тишина, даже с огибающей ---
	{
		Envelope v; v.Add(0.0, 0.0f);
		std::vector<float> b(2 * 100, 1.0f);
		Env::ApplyStereo(&b[0], 100, 0.0, 0.001, 0.0f, 0.0f, &v, NULL);
		for (int k = 0; k < 200; k++) CHECK(b[k] == 0.0f);
	}

	printf(fails ? "FAILED: %d\n" : "OK: envelope\n", fails);
	return fails ? 1 : 0;
}
