#include "TempoDetect.h"
#include <math.h>
#include <vector>

bool DetectTempo(const float *x, size_t frames, float rate, double &bpm)
{
	bpm = 0;
	if (!x || rate < 8000 || frames < (size_t)(rate * 6)) return false;
	if (frames > (size_t)(rate * 180)) frames = (size_t)(rate * 180);	// хватает трёх минут

	// огибающая энергии кадрами по 5 мс, отдельно низ (однополюсный ФНЧ ~150 Гц) и весь сигнал
	const size_t hop = (size_t)(rate * 0.005f);
	const double fps = rate / (double)hop;
	const size_t n = frames / hop;
	std::vector<double> eAll(n, 0.0), eLow(n, 0.0);
	const double a = 1.0 - exp(-2.0 * M_PI * 150.0 / rate);
	double lp = 0, peak = 0;
	for (size_t i = 0; i < n; i++){
		double sa = 0, sl = 0;
		for (size_t k = 0; k < hop; k++){
			double v = x[i * hop + k];
			lp += a * (v - lp);
			sa += v * v;
			sl += lp * lp;
		}
		eAll[i] = sqrt(sa / hop);
		eLow[i] = sqrt(sl / hop);
		if (eAll[i] > peak) peak = eAll[i];
	}
	if (peak < 1e-4)return false;				// тишина

	// огибающая атак: положительный прирост логарифма энергии
	std::vector<double> on(n, 0.0);
	const double fl = peak * 1e-3;
	for (size_t i = 3; i < n; i++){
		double d1 = log(eAll[i] + fl) - log(eAll[i - 3] + fl);
		double d2 = log(eLow[i] + fl) - log(eLow[i - 3] + fl);
		d1 -= 0.15; d2 -= 0.15;				// шум огибающей (рябь тона) не считается атакой
		on[i] = (d1 > 0 ? d1 : 0) + (d2 > 0 ? d2 : 0);
	}
	// вычесть медленное среднее (окно ~0.5 с) и обрезать отрицательное
	{
		const size_t w = (size_t)(fps * 0.25);
		std::vector<double> o2(n, 0.0);
		double s = 0; size_t cnt = 0;
		for (size_t i = 0; i < n; i++){
			s += on[i]; cnt++;
			if (i >= 2 * w + 1){ s -= on[i - 2 * w - 1]; cnt--; }
			size_t c = (i >= w) ? i - w : 0;
			double m = s / cnt;
			double v = on[c] - m;
			o2[c] = v > 0 ? v : 0;
		}
		on.swap(o2);
	}
	double tot = 0;
	for (size_t i = 0; i < n; i++) tot += on[i];
	if (tot <= 0)return false;

	// автокорреляция в диапазоне 50..220 BPM
	const size_t lagMin = (size_t)(fps * 60.0 / 220.0);
	const size_t lagMax = (size_t)(fps * 60.0 / 50.0) + 1;
	if (lagMax * 3 > n)return false;
	std::vector<double> ac(lagMax + 2, 0.0);
	for (size_t L = lagMin - 1; L <= lagMax + 1; L++){
		double s = 0;
		for (size_t i = L; i < n; i++) s += on[i] * on[i - L];
		ac[L] = s / (double)(n - L);
	}
	double zero = 0;
	for (size_t i = 0; i < n; i++) zero += on[i] * on[i];
	zero /= (double)n;
	if (zero <= 0) return false;

	// взвешивание в пользу «человеческого» диапазона 80..160 (гауссиана по лог-темпу)
	size_t best = 0; double bestV = 0, sum = 0; int cntv = 0;
	for (size_t L = lagMin; L <= lagMax; L++){
		double b = 60.0 * fps / (double)L;
		double w = exp(-0.5 * pow(log2(b / 120.0) / 0.9, 2));
		double v = ac[L] * w;
		sum += ac[L]; cntv++;
		if (ac[L] >= ac[L - 1] && ac[L] >= ac[L + 1] && v > bestV){ bestV = v; best = L; }
	}
	if (!best) return false;
	// уверенность: пик относительно среднего уровня и нулевого сдвига
	const double mean = sum / cntv;
	if (ac[best] < 1.8 * mean || ac[best] < 0.12 * zero) return false;

	// уточнение параболой
	double y0 = ac[best - 1], y1 = ac[best], y2 = ac[best + 1];
	double den = y0 - 2 * y1 + y2;
	double off = (den != 0) ? 0.5 * (y0 - y2) / den : 0;
	if (off < -1) off = -1;
	if (off > 1) off = 1;
	double lag = (double)best + off;
	double v = 60.0 * fps / lag;

	// свести к 70..180 удвоением/делением, предпочитая вариант с более сильным пиком
	while (v < 70) v *= 2;
	while (v > 180) v /= 2;
	bpm = v;
	return true;
}
