// Ядро измерения задержки записи (без Haiku API, проверяется тестами на Linux):
// тестовый сигнал из коротких «щелчков» и поиск их начала в записи.
#ifndef LATENCY_CORE_H
#define LATENCY_CORE_H

namespace Latency {

enum { kBursts = 3 };
const double kFirstBurst = 0.5;		// секунд от старта воспроизведения до первого щелчка
const double kBurstSpacing = 1.0;	// интервал между щелчками
const double kBurstLength = 0.008;	// длительность щелчка
const double kTotalSeconds = 4.0;	// сколько записывать

// Значение тестового сигнала в кадре frame (0, если щелчка нет). Нужен для генерации звука в потоке воспроизведения.
float BurstSample(long long frame, double rate);

// Начало щелчка с ожидаемым временем expected (сек) в моно-записи mono длиной n кадров. Возвращает номер кадра
// или -1, если щелчок не найден (слишком тихо). noiseEnd - сколько кадров от начала считать фоновым шумом.
long long FindOnset(const float *mono, long long n, double rate, double expected, double maxDelay = 0.9);

// Задержка в миллисекундах: медиана по найденным щелчкам. Возвращает false, если найдено меньше двух.
bool MeasureMs(const float *mono, long long n, double rate, double *ms);

}	// namespace Latency

#endif
