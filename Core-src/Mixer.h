/*
 * Mixer.h - многодорожечный микшер с клипами (без зависимостей от Haiku, чтобы тестировать на Linux).
 *
 * Звук хранится в неизменяемых источниках (MixSource), на которые ссылаются клипы (MixClip):
 * «источник, с какого кадра, сколько кадров, где лежит на шкале времени». Один источник может
 * лежать в нескольких клипах (копии) - память не дублируется. Дорожка = параметры канала
 * (громкость, баланс, mute, solo, эффекты) + список клипов.
 *
 * Одна дорожка активна, и один её клип («активный клип») лежит не в микшере, а в Pool.sample_memory
 * (правка, фильтры, undo): буфер Pool покрывает всю ленту времени, поэтому номер кадра в нём и есть
 * время. Остальные клипы этой дорожки и все клипы других дорожек микшер воспроизводит сам.
 * Переключение между клипами - см. Tracks.cpp.
 */
#ifndef MIXER_H
#define MIXER_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <atomic>
#include <memory>
#include <vector>
#include "FxChain.h"
#include "Metronome.h"

#define MIX_MAX_TRACKS	16

// Неизменяемый кусок звука (interleaved float, 1 или 2 канала).
struct MixSource {
	float	*data;
	int64_t	frames;
	int		channels;
	float	rate;
	MixSource() : data(NULL), frames(0), channels(2), rate(44100.0f) {}
	~MixSource() { free(data); }
private:
	MixSource(const MixSource &);
	MixSource &operator=(const MixSource &);
};
typedef std::shared_ptr<const MixSource> SourceRef;

// Источник из копии данных (nullptr, если нет памяти или аргументы неверны).
SourceRef MakeSource(const float *data, int64_t frames, int channels, float rate);

struct MixClip {
	SourceRef	src;
	int64_t		start;		// первый кадр в источнике
	int64_t		len;		// число кадров
	double		pos;		// начало на ленте времени, секунды (>= 0)
	MixClip() : start(0), len(0), pos(0) {}
	float	Rate() const { return src ? src->rate : 44100.0f; }
	double	Length() const { return src ? (double)len / src->rate : 0.0; }	// секунды
	double	End() const { return pos + Length(); }
};

struct MixTrack {
	bool	used;
	int		channels;		// формат по умолчанию для пустой дорожки (1 или 2)
	float	rate;
	float	gain;			// 0..2 (линейно)
	float	pan;			// -1 (лево) .. +1 (право)
	bool	mute, solo, armed;
	bool	grid;			// у дорожки включена липкая сетка
	char	name[64];
	std::vector<MixClip>	clips;
	MixTrack() { Reset(); }
	void	Reset()
	{
		used = false; channels = 2; rate = 44100.0f; gain = 1.0f; pan = 0;
		mute = solo = armed = grid = false; name[0] = 0; clips.clear();
	}
};

class Mixer {
public:
	Mixer();
	~Mixer();

	int		TrackCount() const { return fCount; }
	const MixTrack *Track(int i) const { return (i>=0 && i<MIX_MAX_TRACKS && fTracks[i].used) ? &fTracks[i] : NULL; }
	MixTrack *Track(int i)		{ return (i>=0 && i<MIX_MAX_TRACKS && fTracks[i].used) ? &fTracks[i] : NULL; }

	// Дорожки. AddTrack: данные копируются, получается дорожка с одним клипом на 0 с (если frames > 0).
	int		AddTrack(const char *name, const float *data, int64_t frames, int channels, float rate = 44100.0f);
	int		AddEmptyTrack(const char *name, int channels);
	void	RemoveTrack(int i);								// основную (0) удалить нельзя
	void	ResetAll();										// новый проект: одна пустая дорожка 0
	int		Active() const { return fActive; }
	void	SetActive(int i) { if (Track(i)) fActive = i; }
	int		ArmedTrack() const;							// -1, если нет

	// Клипы дорожки (правит только поток интерфейса; воспроизведение читает под замком).
	int		ClipCount(int lane) const { return (lane>=0 && lane<MIX_MAX_TRACKS && fTracks[lane].used) ? (int)fTracks[lane].clips.size() : 0; }
	const MixClip *Clip(int lane, int idx) const;
	int		AddClip(int lane, const SourceRef &src, int64_t start, int64_t len, double pos);	// индекс или -1
	bool	RemoveClip(int lane, int idx);
	bool	TakeClip(int lane, int idx, MixClip &out);	// убрать и вернуть (клип уходит в Pool)
	bool	SetClipPos(int lane, int idx, double pos);
	bool	SetClipRange(int lane, int idx, int64_t start, int64_t len);
	void	ClearClips(int lane);
	int		LaneChannels(int lane) const;				// 1, если все клипы моно, иначе 2
	int		ClipAt(int lane, double time) const;		// клип, накрывающий время (последний из подходящих), или -1

	void	SetGain(int i, float g);
	void	SetPan(int i, float p);
	void	SetMute(int i, bool m);
	void	SetSolo(int i, bool s);
	void	SetGrid(int i, bool g);

	// Темп проекта и сетка. Проект к тактам не привязан: темп нужен только для шага сетки.
	// Глубина: 1 = целая нота (4 удара), 2, 4 (= удар), 8, 16, 32.
	double	Bpm() const { return fBpm; }
	void	SetBpm(double bpm);
	int		GridDiv() const { return fGridDiv; }
	void	SetGridDiv(int div);						// длительность ноты сетки: 1, 2, 4, 8, 16, 32
	int		Meter() const { return fMeter; }
	void	SetMeter(int meter);						// музыкальный размер: Metronome::M44 .. M24 (метроном и линии тактов в сетке)
	double	GridStep() const { return 60.0 / fBpm / 8.0 * Metronome::StepUnits(fGridDiv); }	// секунд между линиями
	double	BarLength() const { return 60.0 / fBpm / 8.0 * Metronome::BarUnits(fMeter); }
	double	Snap(int lane, double t) const;

	// Метроном-барабанщик: звучит при воспроизведении, в Mixdown не попадает
	void	SetMetronome(bool on) { fMetroOn = on; }
	bool	MetronomeOn() const { return fMetroOn; }
	void	SetMetroVolume(float v) { fMetroVol = v < 0 ? 0 : (v > 1 ? 1 : v); fRevision++; }
	float	MetroVolume() const { return fMetroVol; }			// t притянуто к сетке, если у дорожки она включена

	void	SetArmed(int i, bool a);			// armed только у одной дорожки
	FxChain	&Fx(int i) { return fFx[(i>=0 && i<MIX_MAX_TRACKS) ? i : 0]; }	// цепочка эффектов дорожки

	// Конец самого дальнего клипа, лежащего в микшере (сек); 0, если клипов нет
	double	EndTimeExtras() const;
	// Счётчик изменений параметров - интерфейс перерисовывается, когда он меняется
	int		Revision() const { return fRevision.load(); }
	void	Touch() { fRevision++; }

	// Задержка записи (кадров): записанные данные сдвигаются на это число кадров назад.
	void	SetRecordOffset(int64_t frames) { fRecordOffset = frames; }
	int64_t	RecordOffset() const { return fRecordOffset; }

	// Вызывается из аудио-callback. buf - уже заполненный активным клипом стерео-буфер
	// (interleaved L,R), frames - число кадров. activePos - позиция в буфере Pool (в его кадрах,
	// он же время на ленте) на начало буфера, activeRate - частота Pool (отрицательная = реверс),
	// sysRate - частота выхода. Подмешивает остальные клипы, применяет эффекты и канал дорожки.
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
	double		fBpm;
	int			fGridDiv;
	int			fMeter;
	float		fMetroVol;			// 0..1, по умолчанию 0.7
	std::atomic<bool> fMetroOn;
	Metronome	fMetro;
	double		fNextNow;				// ожидаемое время на ленте для следующего буфера (поиск перемотки)
	bool		fHaveNext;
};

extern Mixer Mix;
#endif
