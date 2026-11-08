/*
 * MidiSynth - отрисовка одной MIDI-дорожки в звук по банку SF2 (базовый уровень: сэмплы с подстройкой высоты,
 * зацикливание, огибающая громкости, velocity, громкость/expression канала, pitch bend, педаль сустейна).
 * Эффекты, панорама канала, фильтры и модуляторы SF2 не используются - получается «чистый» звук.
 * Без привязки к Haiku.
 */
#ifndef MIDI_SYNTH_H
#define MIDI_SYNTH_H

#include "MidiFile.h"
#include "SoundFont.h"

// out - чередующиеся L/R. Возвращает false, если нот нет или звук слишком длинный.
bool RenderMidiTrack(const MidiTrackData &track, const SoundFont &sf, float rate, std::vector<float> &out);

#endif
