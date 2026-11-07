/*
 * TempoDetect - оценка темпа (BPM) по моно-сигналу: огибающая атак (низ + весь диапазон)
 * и автокорреляция. Возвращает false, если темп надёжно определить нельзя
 * (слишком короткий/тихий сигнал, нет выраженной пульсации).
 */
#ifndef TEMPO_DETECT_H
#define TEMPO_DETECT_H

#include <stddef.h>

bool DetectTempo(const float *mono, size_t frames, float rate, double &bpm);

#endif
