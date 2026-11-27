// Измерение задержки записи: на выход идут короткие щелчки, одновременно идёт запись, по сдвигу щелчков
// в записи считается задержка. Нужен кабель выход-вход или микрофон рядом с колонками.
#ifndef LATENCY_CAL_H
#define LATENCY_CAL_H

#include <Message.h>
#include <Messenger.h>
#include <String.h>

#define LATENCY_RESULT	'abLr'		// ответ: bool "ok", float "ms", string "err" (ключ языка), "diag", "card"

// Показывает окно «Измерение задержки записи» (с именем карты) и по «Начать» запускает измерение в отдельном потоке
// (около 25 секунд); потом присылает LATENCY_RESULT в target. card - имя карты (пусто - системный вход).
// Вернёт false, если окно отменили или сейчас идёт запись или воспроизведение (тогда сообщение не придёт).
bool LatencyCalAsk(BMessenger target, const char *card);

// Обработка ответа: показывает результат или ошибку. true - измерение удалось; тогда в *card имя карты, в *ms задержка.
bool LatencyCalFinish(BMessage *msg, BString *card, int32 *ms);

#endif
