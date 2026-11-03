/*
 * Mixer.h - многодорожечный микшер (без зависимостей от Haiku, чтобы тестировать на Linux).
 *
 * Активная дорожка - это тот самый буфер Pool.sample_memory, с которым работают
 * правка, фильтры, undo и буфер обмена; её сэмплы микшер в этот момент не читает
 * (их воспроизводит BufferPlayer), он только применяет параметры её канала (громкость,
 * баланс, mute, solo). Остальные дорожки хранят сэмплы сами (interleaved float, 1 или 2
 * канала, частота дискретизации = Pool.frequency). При смене активной дорожки данные
 * копируются между Pool и микшером (см. Tracks.cpp).
 */
#ifndef MIXER_H
#define MIXER_H

#include <stdint.h>
#include <stddef.h>
#include <atomic>
#include "FxChain.h"

#define MIX_MAX_TRACKS	16

struct MixTrack {
	bool	used;
	bool	owns_data;		// данные принадлежат микшеру (у активной дорожки актуальные сэмплы лежат в Pool)
	float	*data;			// interleaved
	int64_t	frames;
	int		channels;		// 1 или 2
	float	rate;			// частота дискретизации дорожки, Гц
	double	offset;			// смещение начала дорожки на общей шкале времени, секунды (>= 0)
	float	gain;			// 0..2 (линейно)
	float	pan;			// -1 (лево) .. +1 (право)
	bool	mute, solo, armed;
	char	name[64];
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
	// Заменить содержимое: данные копируются (i может быть 0, channels 1/2).
	bool	SetTrackData(int i, const float *data, int64_t frames, int channels, float rate);
	void	ResetAll();								// новый проект: одна пустая дорожка 0, остальные удалены
	int		Active() const { return fActive; }
	void	SetActive(int i) { if (Track(i)) fActive = i; }
	int		ArmedTrack() const;							// -1, если нет
	// Дописывать при записи (вызывается не из аудио-потока воспроизведения)
	bool	AppendTrackData(int i, const float *data, int64_t frames);

	FxChain	&Fx(int i) { return fFx[(i>=0 && i<MIX_MAX_TRACKS) ? i : 0]; }	// цепочка эффектов дорожки

	void	SetGain(int i, float g);
	void	SetPan(int i, float p);
	void	SetMute(int i, bool m);
	void	SetSolo(int i, bool s);
	void	SetArmed(int i, bool a);			// armed только у одной дорожки
	void	SetOffset(int i, double seconds);
	// Конец самой «длинной» неактивной дорожки на шкале времени (сек); 0, если данных нет
	double	EndTimeExtras() const;
	// Счётчик изменений параметров - интерфейс перерисовывается, когда он меняется
	int		Revision() const { return fRevision.load(); }
	void	Touch() { fRevision++; }

	// Задержка записи (кадров): записанные данные сдвигаются на это число кадров назад.
	void	SetRecordOffset(int64_t frames) { fRecordOffset = frames; }
	int64_t	RecordOffset() const { return fRecordOffset; }

	// Вызывается из аудио-callback. buf - уже заполненный активной дорожкой стерео-буфер
	// (interleaved L,R), frames - число кадров. activePos - позиция активной дорожки (в её
	// кадрах) на начало буфера, activeRate - её частота (отрицательная = реверс), sysRate -
	// частота выхода. Применяет канал активной дорожки и подмешивает остальные дорожки
	// с учётом их собственной частоты (линейная интерполяция).
	void	Process(float *buf, int frames, double activePos, double activeRate,
				double sysRate, bool mixExtras);

	// Пиковые уровни последнего буфера для дорожки (для индикаторов)
	float	PeakL(int i) const { return (i>=0 && i<MIX_MAX_TRACKS) ? fPeakL[i] : 0; }
	float	PeakR(int i) const { return (i>=0 && i<MIX_MAX_TRACKS) ? fPeakR[i] : 0; }

private:
	void	Lock();
	void	Unlock();
	bool	TryLock();
	static void Gains(const MixTrack &t, bool anySolo, float &gl, float &gr);

	MixTrack	fTracks[MIX_MAX_TRACKS];
	FxChain		fFx[MIX_MAX_TRACKS];
	float		fTmp[2 * 4096];			// буфер одной дорожки при обработке эффектами
	int			fCount;
	int64_t		fRecordOffset;
	std::atomic_flag fLock;
	float		fPeakL[MIX_MAX_TRACKS], fPeakR[MIX_MAX_TRACKS];
	int			fActive;
	std::atomic<int> fRevision;
};

extern Mixer Mix;
#endif
