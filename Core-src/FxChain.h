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

class FxChain {
public:
	struct Slot {
		Ladspa::Plugin	plugin;
		Ladspa::Values	values;		// порт -> значение (всегда заполнены все управляющие входы)
		bool			enabled;
		Ladspa::Runner	*runner;
		float			rate;
	};

	FxChain();
	~FxChain();

	int			Count() const { return (int)fSlots.size(); }
	const Slot	*At(int i) const { return (i >= 0 && i < Count()) ? &fSlots[i] : NULL; }
	bool		Add(const Ladspa::Plugin &p, float rate, std::string *err,
					const Ladspa::Values *init = NULL, bool enabled = true);
	void		Remove(int i);
	void		Clear();
	bool		Move(int i, int delta);			// сдвиг вверх (-1) или вниз (+1)
	void		SetEnabled(int i, bool on);
	void		SetValue(int i, int port, float v);
	bool		HasEffects() const { return fActive.load() > 0; }	// есть включённые плагины

	// Обработка на месте: interleaved стерео, frames кадров, частота rate.
	void		Process(float *buf, int frames, float rate);

private:
	void		Lock();
	void		Unlock();
	void		Recount();

	std::vector<Slot>	fSlots;
	std::atomic_flag	fLock;
	std::atomic<int>	fActive;
};

#endif
