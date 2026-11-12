/*
 * FileImport - чтение звукового файла (любой формат, который знает Media Kit) в источник микшера.
 * Нужна для перетаскивания файлов в окно: звук ложится клипом на дорожку, не трогая Pool.
 */
#ifndef FILE_IMPORT_H
#define FILE_IMPORT_H

#include <Entry.h>
#include "Mixer.h"

enum { IMPORT_OK = 0, IMPORT_NO_AUDIO, IMPORT_NO_MEMORY };

#include <MediaTrack.h>

// Запрашивает у декодера дорожки сырой звук float (стандартный способ Media Kit) и пишет итог в консоль;
// если декодер отказался - оставляет то, что он выбрал сам. Возвращает формат в fmt.
void RequestDecodedFormat(BMediaTrack *track, media_format &fmt, const char *what);

// Читает весь звук файла (1 или 2 канала; у многоканальных берутся два первых) в out.
int ImportAudioFile(const entry_ref &ref, SourceRef &out);

#endif
