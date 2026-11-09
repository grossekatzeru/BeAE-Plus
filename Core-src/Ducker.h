/*
 * Ducker.h - расчёт «приседания» для плагина сайдчейна BeFX (без зависимостей, тестируется на Linux).
 *
 * По уровню ключа (линейный пик) считает сжатие в дБ: ниже порога - 0, выше порога плюс колено - полная «Глубина»,
 * внутри колена - плавный переход (косинус). Дальше сжатие сглаживается: атака (экспонента, постоянная времени),
 * удержание, возврат (экспонента или линейный). Возвращает множитель громкости для цели.
 */
#ifndef DUCKER_H
#define DUCKER_H

#include <math.h>

struct DuckParams {
	float thresholdDb;		// -60..0
	float depthDb;			// 0..40 (на сколько опускаем сигнал)
	float attackMs;			// 0.1..100
	float holdMs;			// 0..500
	float releaseMs;		// 5..2000
	float kneeDb;			// 0..24
	bool  linearRelease;	// false - экспонента, true - линейно (по дБ)
};

class Ducker {
public:
	Ducker() : fRate(44100.0f), fGr(0), fHold(0) {}
	void Init(float rate) { fRate = rate > 0 ? rate : 44100.0f; Reset(); }
	void Reset() { fGr = 0; fHold = 0; }
	float Reduction() const { return fGr; }		// текущее сжатие, дБ (>= 0)

	// Задаёт параметры (дёшево, можно в каждом блоке)
	void Set(const DuckParams &p)
	{
		fP = p;
		fAtk = 1.0f - expf(-1.0f / (fmaxf(p.attackMs, 0.05f) * 0.001f * fRate));
		fRel = 1.0f - expf(-1.0f / (fmaxf(p.releaseMs, 1.0f) * 0.001f * fRate));
		fRelStep = p.depthDb / (fmaxf(p.releaseMs, 1.0f) * 0.001f * fRate);
		fHoldN = (long)(p.holdMs * 0.001f * fRate);
	}

	// Один кадр: key - линейный пик ключа; возвращает множитель громкости (<= 1)
	float Next(float key)
	{
		const float levelDb = key > 1e-9f ? 20.0f * log10f(key) : -180.0f;
		const float over = levelDb - fP.thresholdDb;
		float s;
		if (fP.kneeDb <= 0.0f) s = over >= 0 ? 1.0f : 0.0f;
		else if (over <= -fP.kneeDb * 0.5f) s = 0.0f;
		else if (over >= fP.kneeDb * 0.5f) s = 1.0f;
		else s = 0.5f - 0.5f * cosf(3.14159265f * (over / fP.kneeDb + 0.5f));
		const float target = fP.depthDb * s;
		if (target > fGr) {
			fGr += (target - fGr) * fAtk;
			fHold = fHoldN;
		} else if (fHold > 0) {
			fHold--;
		} else if (fP.linearRelease) {
			fGr -= fRelStep;
			if (fGr < target) fGr = target;
		} else {
			fGr += (target - fGr) * fRel;
		}
		if (fGr < 1e-5f) fGr = 0;
		return powf(10.0f, -fGr / 20.0f);
	}

private:
	float fRate, fGr;
	long fHold, fHoldN;
	float fAtk, fRel, fRelStep;
	DuckParams fP;
};

#endif
