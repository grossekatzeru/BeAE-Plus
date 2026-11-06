/*
 * NoiseFilter - "Шумодав" в меню Transform: обработка выделения (с предпрослушиванием).
 * Само подавление в NoiseCore. Задержка ядра при применении убирается (см. ExecuteFilter).
 */
#ifndef NOISE_FILTER_H
#define NOISE_FILTER_H

#include <memory>
#include "NoiseCore.h"

class NoiseFilter : public RealtimeFilter {
  public:
	NoiseFilter(bool realtime);
	virtual ~NoiseFilter();

	virtual bool InitFilter(float f, int32 channels = 2, int32 pass = 0, int32 size = 0);
	virtual void DeAllocate();
	virtual void FilterBuffer(float *, size_t);
	virtual void UpdateValues();
	virtual BView *ConfigView();
	virtual void MessageReceived(BMessage *);
	virtual int32 Latency() const { return NoiseCore::Latency(); }

  private:
	void	LearnFromSelection();
	void	UpdateStatus();

	NoiseCore	*core;
	SpinSlider	*reduction, *sensitivity, *smooth;
	BCheckBox	*useProfile, *listen;
	BButton		*learn;
	BStringView	*status;

	// профиль общий для всех вызовов фильтра: выделяют шум, запоминают, затем обрабатывают весь трек
	static std::shared_ptr<const NoiseProfile>	sProfile;
};

#endif
