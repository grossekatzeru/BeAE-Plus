/*
 * Metronome - метроном-барабанщик. Звуки синтезируются кодом (файлов не нужно).
 * Такт 4/4, начало такта - нулевая секунда ленты (как у сетки). Глубина квантования div (1..32)
 * определяет, какие доли звучат; каждый уровень добавляет свой звук:
 *   1    - бочка на «раз»
 *   1/2  - + малый барабан на «три»
 *   1/4  - + тихий хай-хет на «два» и «четыре»
 *   1/8, 1/16, 1/32 - хай-хет на всё более мелких долях, чем мельче, тем тише.
 * Не зависит от Haiku, проверяется тестами на Linux.
 */
#ifndef METRONOME_H
#define METRONOME_H

#include <vector>

class Metronome {
public:
	Metronome();

	enum Kind { KICK = 0, SNARE, HAT_QUARTER, HAT_EIGHTH, HAT_SIXTEENTH, HAT_THIRTYSECOND, KIND_COUNT };

	// какой звук стоит на доле pos (0..31, в 32-х долях такта)
	static Kind KindAt(int pos);

	// Подмешать удары в стерео-буфер (interleaved L,R): frames кадров, первый кадр лежит на ленте в момент t0 (сек).
	void	Render(float *buf, int frames, double t0, double sysRate, double bpm, int div);

private:
	void	Prepare(double rate);

	double				fRate;
	std::vector<float>	fSound[KIND_COUNT];
};

#endif
