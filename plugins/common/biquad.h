/*
 * biquad.h - двухполюсный фильтр (формулы Роберта Бристоу-Джонсона, Audio EQ Cookbook) для эквалайзеров.
 * Расчёт и состояние в double: низкие частоты (20 Гц) на 44,1 кГц чувствительны к точности.
 */
#ifndef BEAE_BIQUAD_H
#define BEAE_BIQUAD_H

#include <math.h>

namespace Eq {

enum Type { PEAK, LOW_SHELF, HIGH_SHELF };

struct Biquad {
	double b0, b1, b2, a1, a2, z1, z2;
	Biquad() : b0(1), b1(0), b2(0), a1(0), a2(0), z1(0), z2(0) {}
	void Reset() { z1 = z2 = 0; }

	inline float Run(float xf)
	{
		const double x = xf;
		const double y = b0 * x + z1;
		z1 = b1 * x - a1 * y + z2;
		z2 = b2 * x - a2 * y;
		return (float)y;
	}

	// f - частота, Гц; gainDb - усиление; q - добротность (для полки наклон S = 1, q не используется)
	void Design(Type type, double f, double gainDb, double q, double rate)
	{
		const double nyq = rate * 0.49;
		if (f > nyq) f = nyq;
		if (f < 1.0) f = 1.0;
		const double A = pow(10.0, gainDb / 40.0);
		const double w = 2.0 * 3.14159265358979323846 * f / rate;
		const double c = cos(w), s = sin(w);
		double a0;
		if (type == PEAK) {
			const double alpha = s / (2.0 * q);
			b0 = 1 + alpha * A; b1 = -2 * c; b2 = 1 - alpha * A;
			a0 = 1 + alpha / A; a1 = -2 * c; a2 = 1 - alpha / A;
		} else {
			const double alpha = s / 2.0 * sqrt(2.0);			// S = 1: sqrt((A+1/A)*(1/S-1)+2) = sqrt(2)
			const double sa = 2 * sqrt(A) * alpha;
			if (type == LOW_SHELF) {
				b0 = A * ((A + 1) - (A - 1) * c + sa);
				b1 = 2 * A * ((A - 1) - (A + 1) * c);
				b2 = A * ((A + 1) - (A - 1) * c - sa);
				a0 = (A + 1) + (A - 1) * c + sa;
				a1 = -2 * ((A - 1) + (A + 1) * c);
				a2 = (A + 1) + (A - 1) * c - sa;
			} else {
				b0 = A * ((A + 1) + (A - 1) * c + sa);
				b1 = -2 * A * ((A - 1) + (A + 1) * c);
				b2 = A * ((A + 1) + (A - 1) * c - sa);
				a0 = (A + 1) - (A - 1) * c + sa;
				a1 = 2 * ((A - 1) - (A + 1) * c);
				a2 = (A + 1) - (A - 1) * c - sa;
			}
		}
		b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
	}
};

}	// namespace Eq

#endif
