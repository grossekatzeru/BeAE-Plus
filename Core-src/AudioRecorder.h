/*
 * AudioRecorder - запись звука с аудиовхода Haiku в память BeAE.
 *
 * Формат записи (частота, число каналов, тип отсчётов) выбирает сам микшер/вход:
 * мы подключаемся к первому свободному выходу узла аудиовхода и принимаем то, что он даёт.
 * Данные хранятся как float (моно или стерео) порциями, поэтому длинная запись
 * не перекопируется при росте.
 */
#ifndef AUDIO_RECORDER_H
#define AUDIO_RECORDER_H

#include <MediaDefs.h>
#include <MediaNode.h>
#include <MediaRoster.h>
#include <MediaRecorder.h>
#include <Locker.h>
#include <String.h>
#include <vector>

class AudioRecorder {
public:
	AudioRecorder();
	~AudioRecorder();

	// Открыть вход и начать запись. При ошибке вернёт код и текст в *error (ключ языка).
	status_t	Start(BString *error);
	// Остановить запись или прослушивание (данные записи остаются до Clear()).
	void		Stop();
	bool		IsRecording() const { return fRecording; }

	// Прослушивание для тюнера: тот же вход, но звук не сохраняется, а идёт в небольшой кольцевой буфер (моно).
	// Не работает одновременно с записью: Start() останавливает прослушивание, а StartListening() при записи вернёт ошибку.
	status_t	StartListening(BString *error);
	bool		IsListening() const { return fListening; }
	// Новые кадры с прошлого вызова (не больше maxFrames, самые свежие); частота известна после первого буфера.
	size_t		ReadListened(float *dst, size_t maxFrames);
	float		ListenRate() const { return fRate; }

	int64		Frames() const { return fFrames; }
	float		Rate() const { return fRate; }
	int32		Channels() const { return fOutChannels; }	// 1 или 2
	uint32		SourceFormat() const { return fSrcFormat; }	// формат у источника (0x2, 0x4, 0x24 ...)
	int32		SourceChannels() const { return fSrcChannels; }

	// Пики за последний интервал (сбрасываются при чтении).
	void		ReadLevels(float *left, float *right);

	// Скопировать записанное в одну непрерывную область (malloc, +extra float в запасе).
	// Вызывать после Stop(). Вернёт NULL, если данных нет или нет памяти.
	float*		CopyData(size_t extraFloats = 256) const;

	void		Clear();

	// Огибающая записанного для живого показа: максимум модуля по блокам kPeakBlock кадров.
	enum { kPeakBlock = 256 };
	void		CopyPeaks(std::vector<float> &out);

private:
	static void	_RecordHook(void *cookie, bigtime_t timestamp, void *data, size_t size,
							const media_format &format);
	void		OnBuffer(void *data, size_t size, const media_format &format);
	void		OnListenBuffer(void *data, size_t size, const media_format &format);

	status_t	Open(BString *error, bool listen);

	BMediaRecorder	*fRecorder;
	media_node		fInputNode;
	bool			fHaveNode;
	volatile bool	fRecording;
	volatile bool	fListening;
	std::vector<float> fRing;			// кольцо прослушивания (моно)
	volatile int64	fRingWritten;		// всего записано кадров в кольцо
	int64			fRingRead;			// сколько уже прочитал тюнер

	std::vector<float*> fChunks;		// блоки по kChunkFloats
	size_t			fChunkUsed;			// занято в последнем блоке (в float)
	volatile int64	fFrames;
	std::vector<float> fPeaks;			// пики по блокам kPeakBlock кадров
	float			fPeakAcc;			// максимум в ещё не закрытом блоке
	int32			fPeakCount;			// кадров в нём
	float			fRate;
	int32			fOutChannels;
	int32			fSrcChannels;
	uint32			fSrcFormat;
	bool			fSwap;
	bool			fFormatKnown;
	bool			fOutOfMemory;

	BLocker			fLock;
	volatile float	fPeakL, fPeakR;
};

extern AudioRecorder Recorder;

#endif
