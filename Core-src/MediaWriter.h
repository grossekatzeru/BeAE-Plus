#ifndef MEDIA_WRITER_H
#define MEDIA_WRITER_H

#include <MediaFile.h>
#include <MediaDefs.h>
#include <MediaFormats.h>
#include <String.h>
#include <Directory.h>

// Запись чередующихся float-кадров в файл любого формата и кодека Media Kit (так же, как «Сохранить как...»).
// dir/name - куда писать (расширение формата к имени добавляется само), ff/ci - выбор в панели SavePanel.
// Заголовок дописывается при закрытии; MIME-тип ставится по формату уже после закрытия, пустой файл при ошибке удаляется.
bool MediaWriteFloat(const entry_ref &dir, const char *name, const media_file_format &ff, const media_codec_info *ci,		// ci == NULL: без кодека (несжатый)
	const float *frames, long long count, int channels, float rate, BString &err);

// Имя файла с расширением формата (если его ещё нет).
BString MediaNameWithExtension(const char *name, const media_file_format *ff);

#endif
