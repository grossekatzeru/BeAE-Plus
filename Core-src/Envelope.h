/*
 * Envelope - огибающая автоматизации: точки (время в секундах на ленте, значение), между ними линейная
 * интерполяция, до первой и после последней точки значение первой/последней.
 * Без Haiku API (проверяется тестами на Linux). Потокобезопасность обеспечивает владелец (Mixer).
 *
 * Громкость хранится в дБ (kDbMin и ниже = тишина), панорама в -1..+1.
 */
#ifndef ENVELOPE_H
#define ENVELOPE_H

#include <vector>

struct EnvPoint {
	double	t;		// секунды на ленте
	float	v;		// значение
};

class Envelope {
public:
	Envelope() {}

	bool	Empty() const { return fPts.empty(); }
	int		Count() const { return (int)fPts.size(); }
	const EnvPoint &Point(int i) const { return fPts[i]; }
	void	Clear() { fPts.clear(); }

	// Добавить точку (если рядом, в пределах kSameTime, уже есть точка - заменить её значение). Вернёт индекс.
	int		Add(double t, float v);
	void	Remove(int i);
	// Переместить точку, сохраняя порядок (не перескакивая соседей). Вернёт новый индекс.
	int		Move(int i, double t, float v);

	// Значение в момент t; def, если точек нет.
	float	Value(double t, float def) const;

	static const double kSameTime;		// точки ближе этого считаются одной

private:
	std::vector<EnvPoint> fPts;			// отсортированы по t
};

namespace Env {

const float kDbMin = -60.0f;			// громкость на этой отметке и ниже - тишина
const float kDbMax = 6.0f;

float DbToGain(float db);				// <= kDbMin даёт 0
float GainToDb(float gain);				// 0 даёт kDbMin

// Применить к стерео-буферу (interleaved L,R) громкость и панораму, зависящие от времени.
//   baseGain - линейный множитель дорожки (0 при mute), basePan - положение ползунка (-1..+1);
//   vol, pan - огибающие (могут быть NULL или пустыми); итог: gain = baseGain * DbToGain(vol(t)),
//   pan = clamp(basePan + pan(t)). Время кадра k: t0 + tstep*k. Усиление считается через каждые kStep кадров
//   и между ними меняется плавно (без щелчков).
void ApplyStereo(float *buf, int frames, double t0, double tstep,
				 float baseGain, float basePan, const Envelope *vol, const Envelope *pan);

// Усиление баланса: центр = 1.0 в обоих каналах, к краю гасится противоположный канал.
void PanGains(float gain, float pan, float &gl, float &gr);

const int kStep = 32;

}	// namespace Env

#endif
