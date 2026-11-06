/*
 * FileImport - чтение звукового файла (любой формат, который знает Media Kit) в источник микшера.
 * Нужна для перетаскивания файлов в окно: звук ложится клипом на дорожку, не трогая Pool.
 */
#ifndef FILE_IMPORT_H
#define FILE_IMPORT_H

#include <Entry.h>
#include "Mixer.h"

enum { IMPORT_OK = 0, IMPORT_NO_AUDIO, IMPORT_NO_MEMORY };

// Читает весь звук файла (1 или 2 канала; у многоканальных берутся два первых) в out.
int ImportAudioFile(const entry_ref &ref, SourceRef &out);

#endif
