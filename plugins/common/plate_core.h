/*
 * Ядро пластинчатого ревербератора BeAE-Plus (модель EMT 140, топология Даттарро).
 * Общий код для плагинов "Пластинчатый реверб" и "Вокальная цепочка (слэп + пластина)".
 *
 * Вход: стерео (сводится в моно). Выход: только «мокрый» сигнал (стерео), смесь делает плагин.
 * Длины линий заданы для 29761 Гц и пересчитываются под частоту хоста.
 */
#ifndef BEAE_PLATE_CORE_H
#define BEAE_PLATE_CORE_H

#include <math.h>
#include <string.h>
#include <algorithm>
#include <vector>

namespace PlateCore {

const float kRefRate = 29761.0f;
const float kMaxPredelayMs = 200.0f;
const float kWetScale = 1.0f;			// уровень хвоста при смеси 100 % близок к уровню входа

inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline void Flush(float &v) { if (fabsf(v) < 1e-20f) v = 0; }

struct Delay {
	std::vector<float> buf; size_t w;
	void Init(size_t n) { buf.assign(n + 2, 0.0f); w = 0; }
	void Clear() { std::fill(buf.begin(), buf.end(), 0.0f); }
	inline void Write(float v) { buf[w] = v; if (++w >= buf.size()) w = 0; }
	// задержка d кадров (целая) от последней записи: Read(1) - только что записанное
	inline float Read(size_t d) const { size_t i = w + buf.size() - d; if (i >= buf.size()) i -= buf.size(); return buf[i]; }
	inline float ReadFrac(float d) const
	{
		const size_t id = (size_t)d;
		const float fr = d - (float)id;
		return Read(id) * (1.0f - fr) + Read(id + 1) * fr;
	}
};

struct Allpass {
	Delay d; size_t len;
	void Init(size_t n, size_t pad) { len = n < 2 ? 2 : n; d.Init(len + pad); }		// pad - запас под модуляцию длины
	// схема: w = x + g*dly; y = dly - g*w; dly - выход линии на глубине len
	inline float Run(float x, float g)
	{
		const float dly = d.Read(len);
		const float w = x + g * dly;
		d.Write(w);
		return dly - g * w;
	}
	inline float RunMod(float x, float g, float mod)		// длина len + mod (дробная)
	{
		const float dly = d.ReadFrac((float)len + mod);
		const float w = x + g * dly;
		d.Write(w);
		return dly - g * w;
	}
};

struct Params {
	float decay;		// RT60, с (0.3..12)
	float predelayMs;	// 0..200
	float damp;			// 0..1 затухание высоких в хвосте
	float lowcut;		// 20..500 Гц
	float density;		// 0..1
	float mod;			// 0..1
	float width;		// 0..1
};

struct Core {
	float rate, k;					// k = rate / kRefRate
	Delay pre;
	Allpass in[4];
	Allpass la1, la2, ra1, ra2;
	Delay ld1, ld2, rd1, rd2;
	size_t lenLd1, lenLd2, lenRd1, lenRd2;
	size_t tapL[7], tapR[7];
	float lpL, lpR;					// состояние демпфирования
	float hpState;					// срез низких на входе
	float inLp;						// ограничение полосы входа
	double lfoPhaseL, lfoPhaseR;
	float lastL, lastR;				// выходы ветвей (перекрёстная обратная связь)
	float maxMod;

	static size_t Scaled(int n, float k) { size_t v = (size_t)(n * k + 0.5f); return v < 2 ? 2 : v; }

	void Init(float sampleRate)
	{
		rate = sampleRate > 0 ? sampleRate : 44100.0f;
		k = rate / kRefRate;
		pre.Init((size_t)(kMaxPredelayMs * 0.001f * rate) + 4);
		const int inLen[4] = { 142, 107, 379, 277 };
		const size_t pad = (size_t)(12.0f * k) + 8;
		for (int i = 0; i < 4; i++) in[i].Init(Scaled(inLen[i], k), 2);
		la1.Init(Scaled(672, k), pad); ra1.Init(Scaled(908, k), pad);
		la2.Init(Scaled(1800, k), 2); ra2.Init(Scaled(2656, k), 2);
		lenLd1 = Scaled(4453, k); lenLd2 = Scaled(3720, k);
		lenRd1 = Scaled(4217, k); lenRd2 = Scaled(3163, k);
		ld1.Init(lenLd1); ld2.Init(lenLd2); rd1.Init(lenRd1); rd2.Init(lenRd2);
		maxMod = 12.0f * k;
		// отводы для левого и правого выхода (позиции из схемы Даттарро), не больше длины линии
		const int tl[7] = { 266, 2974, 1913, 1996, 1990, 187, 1066 };
		const int tr[7] = { 353, 3627, 1228, 2673, 2111, 335, 121 };
		for (int i = 0; i < 7; i++) { tapL[i] = Scaled(tl[i], k); tapR[i] = Scaled(tr[i], k); }
		lpL = lpR = hpState = inLp = 0;
		lfoPhaseL = 0; lfoPhaseR = 0.37;
		lastL = lastR = 0;
	}

	void Clear()
	{
		pre.Clear();
		for (int i = 0; i < 4; i++) in[i].d.Clear();
		la1.d.Clear(); la2.d.Clear(); ra1.d.Clear(); ra2.d.Clear();
		ld1.Clear(); ld2.Clear(); rd1.Clear(); rd2.Clear();
		lpL = lpR = hpState = inLp = 0;
		lastL = lastR = 0;
	}

	// Обрабатывает n кадров: wetL/wetR получают только хвост (уже с масштабом 0.6)
	void Process(const float *inL, const float *inR, float *wetL, float *wetR, unsigned long n, const Params &p)
	{
		const float rt = Clamp(p.decay, 0.3f, 12.0f);
		const float damp = Clamp(p.damp, 0.0f, 1.0f) * 0.85f;
		const float density = Clamp(p.density, 0.0f, 1.0f);
		const float modAmt = Clamp(p.mod, 0.0f, 1.0f) * maxMod;
		const float width = Clamp(p.width, 0.0f, 1.0f);
		const float lowcut = Clamp(p.lowcut, 20.0f, 500.0f);
		size_t pdelay = (size_t)(Clamp(p.predelayMs, 0.0f, kMaxPredelayMs) * 0.001f * rate);
		if (pdelay + 2 >= pre.buf.size()) pdelay = pre.buf.size() - 3;

		// коэффициент спада на один проход ветви: полный круг «восьмёрки» = две ветви
		const float loopSec = (float)(la1.len + lenLd1 + la2.len + lenLd2
			+ ra1.len + lenRd1 + ra2.len + lenRd2) / rate;
		float decay = powf(10.0f, -3.0f * (loopSec * 0.5f) / rt);
		decay = Clamp(decay, 0.0f, 0.9995f);
		const float dd1 = 0.7f;
		const float dd2 = Clamp(decay + 0.15f, 0.25f, 0.5f);
		const float id1 = 0.75f * density, id2 = 0.625f * density;
		const float hpA = expf(-6.283185307f * lowcut / rate);		// однополюсный фильтр верхних частот
		const double inc1 = 1.0 / (rate * 1.0), inc2 = 0.83 / rate;		// LFO около 1 и 0.83 Гц
		const float w1 = width * 0.5f + 0.5f, w2 = (1.0f - width) * 0.5f;

		for (unsigned long i = 0; i < n; i++) {
			// вход: моно, срез низких, лёгкое ограничение верхних
			float x = (inL[i] + inR[i]) * 0.5f;
			hpState = x * (1.0f - hpA) + hpState * hpA;			// ФНЧ -> вычитаем получаем ФВЧ
			x -= hpState;
			inLp = x * 0.7f + inLp * 0.3f;
			Flush(hpState); Flush(inLp);
			x = inLp;
			// предзадержка
			pre.Write(x);
			x = pre.Read(pdelay + 1);
			// диффузоры
			x = in[0].Run(x, id1);
			x = in[1].Run(x, id1);
			x = in[2].Run(x, id2);
			x = in[3].Run(x, id2);

			// «бак»: две ветви, перекрёстная обратная связь
			lfoPhaseL += inc1; if (lfoPhaseL >= 1.0) lfoPhaseL -= 1.0;
			lfoPhaseR += inc2; if (lfoPhaseR >= 1.0) lfoPhaseR -= 1.0;
			const float mL = modAmt * sinf((float)(6.283185307179586 * lfoPhaseL));
			const float mR = modAmt * sinf((float)(6.283185307179586 * lfoPhaseR));

			float l = x + lastR;
			l = la1.RunMod(l, dd1, mL);
			ld1.Write(l);
			float ld1o = ld1.Read(lenLd1);
			lpL = ld1o * (1.0f - damp) + lpL * damp; Flush(lpL);
			l = lpL * decay;
			l = la2.Run(l, dd2);
			ld2.Write(l);
			lastL = ld2.Read(lenLd2);

			float r = x + lastL;
			r = ra1.RunMod(r, dd1, mR);
			rd1.Write(r);
			float rd1o = rd1.Read(lenRd1);
			lpR = rd1o * (1.0f - damp) + lpR * damp; Flush(lpR);
			r = lpR * decay;
			r = ra2.Run(r, dd2);
			rd2.Write(r);
			lastR = rd2.Read(lenRd2);

			// выход: отводы с обеих ветвей (стереопара «датчиков»)
			const float yl = rd1.Read(tapL[0]) + rd1.Read(tapL[1]) - ra2.d.Read(tapL[2])
				+ rd2.Read(tapL[3]) - ld1.Read(tapL[4]) - la2.d.Read(tapL[5]) - ld2.Read(tapL[6]);
			const float yr = ld1.Read(tapR[0]) + ld1.Read(tapR[1]) - la2.d.Read(tapR[2])
				+ ld2.Read(tapR[3]) - rd1.Read(tapR[4]) - ra2.d.Read(tapR[5]) - rd2.Read(tapR[6]);
			wetL[i] = (yl * w1 + yr * w2) * 0.6f * kWetScale;
			wetR[i] = (yr * w1 + yl * w2) * 0.6f * kWetScale;
		}
	}
};

}	// namespace PlateCore

#endif
