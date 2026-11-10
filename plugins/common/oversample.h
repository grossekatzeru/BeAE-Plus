/*
 * Передискретизация x4 для нелинейных эффектов (дисторшн, овердрайв) и простые аналоговые фильтры.
 * Фильтр: оконный sinc (окно Кайзера, 129 отсчётов на частоте x4), полоса пропускания до ~0.37 частоты дискретизации,
 * подавление за её половиной ~70 дБ. Общая задержка: 32 отсчёта исходной частоты (kLatency) - ровно целая,
 * поэтому «сухой» сигнал для параллельной смеси задерживается на то же число отсчётов.
 */
#ifndef BEAE_OVERSAMPLE_H
#define BEAE_OVERSAMPLE_H

#include <math.h>
#include <string.h>

namespace Os {

const int kLatency = 32;

inline double BesselI0(double x)
{
	double s = 1.0, t = 1.0;
	const double q = x * x / 4.0;
	for (int k = 1; k < 60; k++) { t *= q / ((double)k * k); s += t; if (t < 1e-14 * s) break; }
	return s;
}

inline float Flush(float v) { return fabsf(v) < 1e-20f ? 0.0f : v; }

// однополюсный фильтр: LP и HP (HP = вход - LP)
struct OnePole {
	float a, z;
	OnePole() : a(0.1f), z(0) {}
	void Set(float fc, float fs)
	{
		if (fc > 0.45f * fs) fc = 0.45f * fs;
		a = 1.0f - expf(-6.283185307f * fc / fs);
	}
	void Clear() { z = 0; }
	inline float LP(float x) { z += a * (x - z); z = Flush(z); return z; }
	inline float HP(float x) { return x - LP(x); }
};

struct Oversampler4 {
	enum { N = 129, L = 4, PH = 33 };
	float h[N];
	float xin[2 * PH]; int xi;
	float yup[2 * N]; int yi;

	Oversampler4() { Init(); }

	void Init()
	{
		const double fc = 0.5 / L * 0.86;				// граница в долях частоты x4 (около 0.43 исходной Найквиста x2)
		const double beta = 8.0, mid = (N - 1) / 2.0, i0b = BesselI0(beta);
		double sum = 0;
		double tmp[N];
		for (int m = 0; m < N; m++) {
			const double d = m - mid;
			const double sinc = d == 0 ? 2.0 * fc : sin(2.0 * M_PI * fc * d) / (M_PI * d);
			const double r = d / mid;
			const double w = BesselI0(beta * sqrt(fmax(0.0, 1.0 - r * r))) / i0b;
			tmp[m] = sinc * w; sum += tmp[m];
		}
		for (int m = 0; m < N; m++) h[m] = (float)(tmp[m] / sum);		// усиление на постоянном токе = 1
		Clear();
	}

	void Clear()
	{
		memset(xin, 0, sizeof(xin)); memset(yup, 0, sizeof(yup));
		xi = 0; yi = 0;
	}

	// Один отсчёт на входе -> один на выходе; f(float) - нелинейная цепь на частоте x4 (вызывается 4 раза)
	template <class F> inline float Process(float x, F &f)
	{
		xi = (xi == 0) ? PH - 1 : xi - 1;
		xin[xi] = x; xin[xi + PH] = x;
		float out = 0;
		for (int p = 0; p < L; p++) {
			float u = 0;
			for (int k = 0; 4 * k + p < N; k++) u += h[4 * k + p] * xin[xi + k];
			const float v = f(u * (float)L);
			yi = (yi == 0) ? N - 1 : yi - 1;
			yup[yi] = v; yup[yi + N] = v;
			if (p == 0) {
				float s = 0;
				for (int m = 0; m < N; m++) s += h[m] * yup[yi + m];
				out = s;
			}
		}
		return out;
	}
};

// линия задержки для «сухого» сигнала (kLatency отсчётов)
struct DryDelay {
	float buf[kLatency + 1]; int pos;
	DryDelay() { Clear(); }
	void Clear() { memset(buf, 0, sizeof(buf)); pos = 0; }
	inline float Process(float x)
	{
		buf[pos] = x;
		pos = (pos + 1) % (kLatency + 1);
		return buf[pos];		// самый старый: задержка kLatency
	}
};

}	// namespace Os

#endif
