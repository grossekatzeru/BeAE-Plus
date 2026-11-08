/*
 * SoundFont - чтение банков SF2 (RIFF sfbk) без привязки к Haiku. Пресеты сразу «разворачиваются» в плоские зоны:
 * диапазон нот и velocity, сэмпл и итоговые значения генераторов (инструмент + прибавка пресета).
 * Модуляторы и фильтры SF2 не используются (базовый уровень).
 */
#ifndef SOUND_FONT_H
#define SOUND_FONT_H

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>

enum {
	SFG_START = 0, SFG_END = 1, SFG_STARTLOOP = 2, SFG_ENDLOOP = 3, SFG_START_COARSE = 4,
	SFG_PAN = 17,
	SFG_DELAY = 33, SFG_ATTACK = 34, SFG_HOLD = 35, SFG_DECAY = 36, SFG_SUSTAIN = 37, SFG_RELEASE = 38,
	SFG_INSTRUMENT = 41, SFG_KEYRANGE = 43, SFG_VELRANGE = 44,
	SFG_STARTLOOP_COARSE = 45, SFG_ATTENUATION = 48, SFG_ENDLOOP_COARSE = 50,
	SFG_COARSE_TUNE = 51, SFG_FINE_TUNE = 52, SFG_SAMPLE_ID = 53, SFG_SAMPLE_MODES = 54,
	SFG_SCALE_TUNING = 56, SFG_ROOT_KEY = 58, SFG_END_COARSE = 12,
	SFG_COUNT = 64
};

struct SfSample {
	std::string	name;
	uint32_t	start, end, loopStart, loopEnd, rate;
	int			key;		// ноту оригинала (255 -> 60)
	int			correction;	// центы
	int			type;
};

struct SfZone {
	int		keyLo, keyHi, velLo, velHi;
	int		sample;				// индекс в samples
	int		gen[SFG_COUNT];		// итоговые значения генераторов
};

struct SfPreset {
	std::string			name;
	int					bank, program;
	std::vector<SfZone>	zones;
};

class SoundFont {
public:
	bool Load(const uint8_t *data, size_t size, std::string &err);		// копирует нужное, data можно освободить
	bool LoadFile(const char *path, std::string &err);
	// пресет с запасными вариантами: (bank, program) -> (0, program) -> (bank, 0) -> первый; NULL - банк пуст
	const SfPreset *Find(int bank, int program) const;
	std::string	name;
	std::vector<SfSample>	samples;
	std::vector<int16_t>	pcm;
	std::vector<SfPreset>	presets;
};

#endif
