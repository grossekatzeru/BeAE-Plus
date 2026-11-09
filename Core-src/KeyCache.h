/*
 * KeyCache.h - огибающая «ключа» для плагина сайдчейна BeFX (без зависимостей от Haiku, тестируется на Linux).
 *
 * Дорожка-ключ (маскирующая) один раз превращается в огибающую: моно, максимум за каждую миллисекунду
 * (линейная амплитуда), только чистый звук клипов - без громкости, баланса, mute и эффектов дорожки.
 * Плагин читает огибающую по времени на ленте, поэтому перемотка, цикл и реверс работают сами,
 * а ключ известен заранее (под будущее «Опережение»).
 *
 * Диапазон ключа: «весь звук» (пик из двух каналов) или «низ» (ФНЧ 4-го порядка с заданной частотой среза, например для бочки).
 * Огибающая строится в фоновом потоке. Состояния: нет / строится / готова / устарела (дорожка изменилась с момента построения;
 * сравниваются подписи списка клипов). Автоматической пересборки нет: пользователь нажимает «Обновить».
 *
 * Контекст блока: Mixer перед вызовом плагинов дорожки задаёт время первого кадра и шаг (Key::Scope),
 * плагин по нему находит нужное место огибающей. Контекст - в thread_local, поэтому сведение в другом потоке не мешает воспроизведению.
 */
#ifndef KEY_CACHE_H
#define KEY_CACHE_H

#include <stdint.h>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include "Mixer.h"

namespace Key {

enum Range { RANGE_ALL = 0, RANGE_LOW = 1 };
enum Status { ST_NONE = 0, ST_BUILDING, ST_READY, ST_STALE };

const float kEnvRate = 1000.0f;			// значений огибающей в секунду

struct Envelope {
	std::vector<float> peak;			// линейный пик за каждую миллисекунду
	uint64_t signature;
	Envelope() : signature(0) {}
	// значение в момент t (сек); вне записи - 0
	float At(double t) const
	{
		if (t < 0) return 0.0f;
		const size_t i = (size_t)(t * kEnvRate);
		return i < peak.size() ? peak[i] : 0.0f;
	}
};

// Подпись списка клипов: позиции, длины, источники (размеры + выборка отсчётов), кроссфейд.
uint64_t Signature(const std::vector<MixClip> &clips, bool xfade);

// Строит огибающую синхронно. cancelled (может быть пустой) опрашивается между блоками; false - отменено.
bool BuildEnvelope(const std::vector<MixClip> &clips, bool xfade, int range, float cutoffHz,
	Envelope &out, const std::function<bool()> &cancelled = std::function<bool()>());

// Откуда брать клипы дорожки (вызывается из потока интерфейса); false - дорожки нет.
typedef std::function<bool(int lane, std::vector<MixClip> &clips, bool &xfade)> LaneSource;

class Cache {
public:
	Cache();
	~Cache();
	static Cache &Global();

	void	SetLaneSource(const LaneSource &src);
	// Состояние огибающей для дорожки/диапазона (сравнивает подпись текущих клипов; вызывать из интерфейса, не часто)
	Status	Check(int lane, int range, float cutoffHz);
	// Запускает построение в фоне, если огибающей нет или она устарела (force - пересобрать в любом случае)
	void	Request(int lane, int range, float cutoffHz, bool force = false);
	// Строит синхронно и ждёт (экспорт, тесты); true - готова
	bool	BuildNow(int lane, int range, float cutoffHz);
	// Для аудио-потока: готовая огибающая или пустой указатель (не блокирует: при занятости - пусто)
	std::shared_ptr<const Envelope> Get(int lane, int range, float cutoffHz) const;
	// Состояние без пересчёта подписи (дёшево)
	Status	Peek(int lane, int range, float cutoffHz) const;
	void	Clear();			// новый проект: всё забыть

private:
	struct Entry { std::shared_ptr<const Envelope> env; Status st; int gen; Entry() : st(ST_NONE), gen(0) {} };
	static int64_t Key(int lane, int range, float cutoffHz);
	void	Finish(int64_t key, int gen, std::shared_ptr<Envelope> env, bool ok);
	void	JoinAll();

	LaneSource				fSource;
	mutable std::mutex		fMutex;
	std::map<int64_t, Entry>	fEntries;
	std::vector<std::thread>	fThreads;
	std::atomic<int>		fGen;
	std::atomic<bool>		fQuit;
};

// ---- контекст блока ----
struct Context {
	const Cache	*cache;
	double		t0;			// время на ленте первого кадра вызова FxChain::Process, сек
	double		step;		// шаг на кадр, сек (отрицательный при реверсе)
	uint64_t	id;			// номер вызова: плагин по нему узнаёт, что начался новый вызов
};
const Context *CurrentContext();		// null вне Scope

class Scope {
public:
	Scope(double t0, double step, const Cache *cache = &Cache::Global());
	~Scope();
private:
	Context fCtx;
	const Context *fPrev;
};

}	// namespace Key

#endif
