// Измерение задержки записи: на выход идут короткие щелчки, одновременно идёт запись, по сдвигу щелчков
// в записи считается задержка. Нужен кабель выход-вход или микрофон рядом с колонками.
#ifndef LATENCY_CAL_H
#define LATENCY_CAL_H

#include <Messenger.h>

#define LATENCY_RESULT	'abLr'		// ответ: bool "ok", float "ms", string "err" (ключ языка)

// Запускает измерение в отдельном потоке (около 4 секунд) и присылает LATENCY_RESULT в target.
void LatencyCalStart(BMessenger target);

#endif
