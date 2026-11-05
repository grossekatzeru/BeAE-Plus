/*
 * FxChain - цепочка эффектов (LADSPA) одной дорожки: плагины идут один за другим.
 * Без зависимостей от Haiku. Правка цепочки - из интерфейса, Process - из аудио-потока:
 * доступ защищён спин-блокировкой, а аудио-поток при занятом замке просто пропускает буфер.
 * Экземпляры плагинов создаются до взятия замка, удаляются после.
 */
#ifndef FX_CHAIN_H
#define FX_CHAIN_H

#include <atomic>
#include <string>
#include <vector>
#include "LadspaHost.h"
#include "Vst2Host.h"

class FxChain {
public:
	enum Kind { KIND_LADSPA, KIND_VST2 };
	struct Slot {
		Kind			kind;
		Ladspa::Plugin	plugin;		// у VST2 заполнены только file и name
		Ladspa::Values	values;		// порт -> значение (всегда заполнены все управляющие входы)
		bool			enabled;
		Ladspa::Runner	*runner;	// LADSPA
		Vst2::Instance	*vst;		// VST2; значения - параметры 0..1 по номеру
		float			rate;
	};

	FxChain();
	~FxChain();

	int			Count() const { return (int)fSlots.size(); }
	const Slot	*At(int i) const { return (i >= 0 && i < Count()) ? &fSlots[i] : NULL; }
	bool		Add(const Ladspa::Plugin &p, float rate, std::string *err,
					const Ladspa::Values *init = NULL, bool enabled = true);
	bool		AddVst(const std::string &file, float rate, std::string *err,
					const Ladspa::Values *init = NULL, bool enabled = true);
	bool		AddCopyOf(const Slot &s, float rate, std::string *err);	// тот же плагин и параметры (для Mixdown)
	void		Remove(int i);
	void		Clear();
	bool		Move(int i, int delta);			// сдвиг вверх (-1) или вниз (+1)
	void		SetEnabled(int i, bool on);
	void		SetValue(int i, int port, float v);
	bool		HasEffects() const { return fActive.load() > 0; }	// есть включённые плагины
	int			Latency() const { return fLatency.load(); }			// сумма задержек включённых плагинов, отсчётов
	void		Reset();		// сбросить состояние плагинов (перемотка); из аудио-потока безопасно

	// Обработка на месте: interleaved стерео, frames кадров, частота rate.
	void		Process(float *buf, int frames, float rate);

private:
	void		Lock();
	void		Unlock();
	void		Recount();

	std::vector<Slot>	fSlots;
	std::atomic_flag	fLock;
	std::atomic<int>	fActive;
	std::atomic<int>	fLatency;
};

#endif
