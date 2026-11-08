/*
 * MidiFile - чтение стандартных MIDI-файлов (SMF формат 0/1). Без привязки к Haiku.
 * Время событий сразу переводится в секунды по карте темпов.
 */
#ifndef MIDI_FILE_H
#define MIDI_FILE_H

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>

enum { ME_NOTE_ON, ME_NOTE_OFF, ME_PROGRAM, ME_CC, ME_BEND };

struct MidiEvent {
	double	t;			// секунды
	uint8_t	type;		// ME_*
	uint8_t	ch;			// канал 0..15
	int		a;			// нота / номер программы / номер контроллера / bend -8192..8191
	int		b;			// velocity / значение контроллера
};

struct MidiTrackData {
	std::string				name;		// имя из мета-события Track Name (может быть пустым)
	std::vector<MidiEvent>	ev;			// по времени
	int						notes;		// число нот
	int						channel;	// единственный канал дорожки (-1 - несколько каналов)
	int						program;	// первая программа (-1 - не задана)
};

struct MidiSong {
	int							format;
	double						bpm;			// начальный темп (четвертей в минуту), 120 если в файле не задан
	bool						tempoVaries;	// темп в файле меняется
	std::vector<MidiTrackData>	tracks;		// только дорожки с нотами; в формате 0 разбито по каналам
};

// Значения «эффектных» контроллеров дорожки на момент первой ноты (-1 - не заданы):
// панорама CC10 (0..127, 64 - центр), посыл на реверберацию CC91, посыл на хорус CC93.
struct MidiEffects { int pan, reverb, chorus; };
MidiEffects MidiTrackEffects(const MidiTrackData &t);

// Название дорожки для проекта: имя из файла, иначе семейство инструмента General MIDI / «Drums» / «MIDI N» (index с 1)
std::string MidiTrackTitle(const MidiTrackData &t, int index);

bool LoadMidi(const uint8_t *data, size_t size, MidiSong &song, std::string &err);

#endif
