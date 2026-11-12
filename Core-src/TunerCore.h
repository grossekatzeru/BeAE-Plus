/*
 * TunerCore - ядро гитарного тюнера BeAE-Plus (без зависимостей от Haiku, проверяется на Linux).
 *
 * Определение высоты: алгоритм YIN (разностная функция со средненормированной нормировкой, порог, поиск
 * локального минимума, параболическая интерполяция) на проредженном сигнале. Прореживание зависит от диапазона
 * пресета: частота после него не меньше 24 частот верхней струны, поэтому период самой высокой струны занимает
 * не меньше 24 отсчётов, а точность на чистых тонах лучше полцента. Диапазон пресета сужает поиск и убирает ошибки на октаву;
 * дополнительно проверяется удвоенный период (сильная 2-я гармоника при слабой основной).
 *
 * Ядро получает звук порциями (Push, моно), а Update() по накопленному считает показания: сглаживание медианой
 * последних оценок, удержание ноты после затухания (0,35 с), ближайшая струна пресета или ближайшая нота.
 */
#ifndef TUNER_CORE_H
#define TUNER_CORE_H

#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace Tuner {

enum { NOTATION_LATIN = 0, NOTATION_SOLFEGE = 1 };

struct Preset {
	const char	*key;			// ключ языка (TUNER_PRESET_...)
	int			count;			// число струн; 0 - хроматический режим (все ноты)
	int			midi[8];		// ноты струн по возрастанию высоты (MIDI: A4 = 69)
};

int			PresetCount();
const Preset &PresetAt(int index);

float		FreqToMidi(float freq, float a4);
float		MidiToFreq(float midi, float a4);
const char	*NoteName(int midi, int notation);		// "C#", "Ля"
int			Octave(int midi);						// C4 = 60 -> 4

struct Reading {
	bool	voiced;			// есть устойчивый звук (или удерживается после затухания)
	bool	held;			// звук затух, показание удерживается
	float	freq;			// Гц
	float	midi;			// дробная нота
	int		note;			// целевая нота (MIDI): ближайшая нота или нота ближайшей струны
	int		stringIndex;	// номер струны в пресете (с нуля) или -1
	float	cents;			// отклонение от целевой ноты, центы (+ выше)
	float	levelDb;		// уровень сигнала, дБ (0 - полная шкала)
};

enum { kHist = 12 };		// длина истории оценок высоты (сглаживание)

class Core {
public:
	Core();

	void	Init(float sampleRate);
	void	SetA4(float hz);			// 415..466
	float	A4() const { return fA4; }
	void	SetPreset(int index);
	int		PresetIndex() const { return fPreset; }
	void	Reset();

	void	Push(const float *mono, size_t frames);
	Reading	Update();
	float	LevelDb() const;

	// Для тестов и вывода: границы поиска для текущего пресета
	void	SearchRange(float *fmin, float *fmax) const;

private:
	bool	Detect(float fmin, float fmax, float *freq);

	float	fRate, fA4;
	int		fPreset;
	std::vector<float> fBuf;
	uint64_t fPushed;
	uint64_t fLastVoiced;
	float	fHist[kHist];
	int		fHistN;
	bool	fActive;				// уже было подтверждение
	Reading	fLast;
};

}	// namespace Tuner

#endif
