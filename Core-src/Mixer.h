/*
 * Mixer.h - многодорожечный микшер (без зависимостей от Haiku, чтобы тестировать на Linux).
 *
 * Дорожка 0 («основная») - это тот самый буфер Pool.sample_memory, с которым работают
 * правка, фильтры, undo и буфер обмена; её сэмплы микшер не хранит, он только держит
 * параметры её канала (громкость, баланс, mute, solo). Остальные дорожки хранят
 * свои сэмплы сами (interleaved float, 1 или 2 канала, частота дискретизации = Pool.frequency).
 */
#ifndef MIXER_H
#define MIXER_H

#include <stdint.h>
#include <stddef.h>
#include <atomic>

#define MIX_MAX_TRACKS	16

struct MixTrack {
	bool	used;
	bool	owns_data;		// false у основной дорожки
	float	*data;			// interleaved
	int64_t	frames;
	int		channels;		// 1 или 2
	float	gain;			// 0..2 (линейно)
	float	pan;			// -1 (лево) .. +1 (право)
	bool	mute, solo, armed;
	char	name[32];
};

class Mixer {
public:
	Mixer();
	~Mixer();

	int		TrackCount() const { return fCount; }
	MixTrack *Track(int i)		{ return (i>=0 && i<MIX_MAX_TRACKS && fTracks[i].used) ? &fTracks[i] : NULL; }

	// Добавить дорожку: данные копируются. channels 1/2. Возвращает индекс или -1.
	int		AddTrack(const char *name, const float *data, int64_t frames, int channels);
	int		AddEmptyTrack(const char *name, int channels);	// пустая, для записи
	void	RemoveTrack(int i);								// основную (0) удалить нельзя
	// Заменить содержимое (после записи): данные копируются.
	bool	SetTrackData(int i, const float *data, int64_t frames);
	// Дописывать при записи (вызывается не из аудио-потока воспроизведения)
	bool	AppendTrackData(int i, const float *data, int64_t frames);

	void	SetGain(int i, float g);
	void	SetPan(int i, float p);
	void	SetMute(int i, bool m);
	void	SetSolo(int i, bool s);
	void	SetArmed(int i, bool a);

	// Задержка записи (кадров): записанные данные сдвигаются на это число кадров назад.
	void	SetRecordOffset(int64_t frames) { fRecordOffset = frames; }
	int64_t	RecordOffset() const { return fRecordOffset; }

	// Вызывается из аудио-callback. buf - уже заполненный основной дорожкой стерео-буфер
	// (interleaved L,R), frames - число кадров, start - позиция в кадрах от начала.
	// Применяет канал основной дорожки и подмешивает остальные.
	// mixExtras=false, если ход воспроизведения не 1:1 (пересэмплинг/реверс).
	void	Process(float *buf, int frames, int64_t start, bool mixExtras);

	// Пиковые уровни последнего буфера для дорожки (для индикаторов)
	float	PeakL(int i) const { return (i>=0 && i<MIX_MAX_TRACKS) ? fPeakL[i] : 0; }
	float	PeakR(int i) const { return (i>=0 && i<MIX_MAX_TRACKS) ? fPeakR[i] : 0; }

private:
	void	Lock();
	void	Unlock();
	bool	TryLock();
	static void Gains(const MixTrack &t, bool anySolo, float &gl, float &gr);

	MixTrack	fTracks[MIX_MAX_TRACKS];
	int			fCount;
	int64_t		fRecordOffset;
	std::atomic_flag fLock;
	float		fPeakL[MIX_MAX_TRACKS], fPeakR[MIX_MAX_TRACKS];
};

extern Mixer Mix;
#endif
