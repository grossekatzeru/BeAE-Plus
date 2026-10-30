/*
 * RecordConvert.h - перевод буферов записи Haiku Media Kit в float.
 *
 * Не зависит от Haiku API (только стандартная библиотека), чтобы логику можно было
 * проверить на любой системе. Константы форматов совпадают с
 * media_raw_audio_format (B_AUDIO_FLOAT=0x24, B_AUDIO_INT=0x4, B_AUDIO_SHORT=0x2,
 * B_AUDIO_UCHAR=0x11, B_AUDIO_CHAR=0x1).
 *
 * Лицензия: BSD (как у BeAE).
 */
#ifndef RECORD_CONVERT_H
#define RECORD_CONVERT_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace RecConv {

enum {
	FMT_CHAR  = 0x01,
	FMT_UCHAR = 0x11,
	FMT_SHORT = 0x02,
	FMT_INT   = 0x04,
	FMT_FLOAT = 0x24
};

// размер одного отсчёта в байтах (0 = неизвестный формат)
inline int BytesPerSample(uint32_t fmt)
{
	switch (fmt) {
	case FMT_CHAR: case FMT_UCHAR:	return 1;
	case FMT_SHORT:					return 2;
	case FMT_INT: case FMT_FLOAT:	return 4;
	}
	return 0;
}

inline float Clip(float x)
{
	return x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x);
}

inline uint16_t Swap16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }
inline uint32_t Swap32(uint32_t v)
{
	return (v >> 24) | ((v >> 8) & 0x0000ff00u) | ((v << 8) & 0x00ff0000u) | (v << 24);
}

// Один отсчёт -> float в диапазоне [-1, 1]. src указывает на отсчёт в формате fmt.
inline float SampleToFloat(const uint8_t *src, uint32_t fmt, bool swap)
{
	switch (fmt) {
	case FMT_FLOAT: {
		uint32_t u;
		memcpy(&u, src, 4);
		if (swap) u = Swap32(u);
		float f;
		memcpy(&f, &u, 4);
		return Clip(f);
	}
	case FMT_INT: {
		uint32_t u;
		memcpy(&u, src, 4);
		if (swap) u = Swap32(u);
		int32_t v = (int32_t)u;
		return Clip((float)((double)v / 2147483648.0));
	}
	case FMT_SHORT: {
		uint16_t u;
		memcpy(&u, src, 2);
		if (swap) u = Swap16(u);
		int16_t v = (int16_t)u;
		return Clip((float)v / 32768.0f);
	}
	case FMT_UCHAR:
		return Clip(((int)src[0] - 128) / 128.0f);
	case FMT_CHAR:
		return Clip((float)(int8_t)src[0] / 128.0f);
	}
	return 0.0f;
}

// Преобразует кадры. srcChannels — число каналов во входе, outChannels — 1 или 2.
// Вход моно -> выход моно; вход стерео -> стерео; больше двух каналов: берутся первые два.
// dst должен вмещать frames*outChannels float. Возвращает пик (0..1) по каналам.
inline void ConvertFrames(const void *src, size_t frames, uint32_t fmt, int srcChannels,
						  bool swap, int outChannels, float *dst,
						  float *peakL, float *peakR)
{
	const uint8_t *p = (const uint8_t *)src;
	const int bps = BytesPerSample(fmt);
	float pl = 0.0f, pr = 0.0f;
	if (bps == 0 || srcChannels < 1) {
		memset(dst, 0, frames * outChannels * sizeof(float));
		if (peakL) *peakL = 0.0f;
		if (peakR) *peakR = 0.0f;
		return;
	}
	const size_t frameBytes = (size_t)bps * srcChannels;
	for (size_t i = 0; i < frames; ++i, p += frameBytes) {
		float l = SampleToFloat(p, fmt, swap);
		float r = (srcChannels >= 2) ? SampleToFloat(p + bps, fmt, swap) : l;
		float al = l < 0 ? -l : l, ar = r < 0 ? -r : r;
		if (al > pl) pl = al;
		if (ar > pr) pr = ar;
		if (outChannels == 1) {
			dst[i] = l;
		} else {
			dst[i * 2]     = l;
			dst[i * 2 + 1] = r;
		}
	}
	if (peakL) *peakL = pl;
	if (peakR) *peakR = pr;
}

}	// namespace RecConv

#endif
