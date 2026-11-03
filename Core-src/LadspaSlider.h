/*
 * LadspaSlider - ползунок параметра LADSPA (общий для окна фильтра и окна эффектов дорожки).
 */
#ifndef LADSPA_SLIDER_H
#define LADSPA_SLIDER_H

#include <Slider.h>
#include <math.h>
#include <stdio.h>
#include "LadspaHost.h"

static const int32 kSliderSteps = 1000;

class LadspaSlider : public BSlider {
public:
	LadspaSlider(const char *label, BMessage *message, const Ladspa::Range &range, float initial)
		:
		BSlider(NULL, label, message, 0, kSliderSteps, B_HORIZONTAL),
		fRange(range),
		fIntMode(range.integer && (range.upper - range.lower) <= 2000.0f)
	{
		if (fIntMode)
			SetLimits((int32)floorf(range.lower + 0.5f), (int32)floorf(range.upper + 0.5f));
		SetModificationMessage(new BMessage(*message));
		SetValueF(initial);
	}

	float ValueF() const
	{
		if (fIntMode)
			return (float)Value();
		float t = (float)Value() / (float)kSliderSteps;
		float v;
		if (fRange.logarithmic)
			v = fRange.lower * powf(fRange.upper / fRange.lower, t);
		else
			v = fRange.lower + t * (fRange.upper - fRange.lower);
		if (fRange.integer)
			v = floorf(v + 0.5f);
		return v;
	}

	void SetValueF(float v)
	{
		if (v < fRange.lower) v = fRange.lower;
		if (v > fRange.upper) v = fRange.upper;
		if (fIntMode) {
			SetValue((int32)floorf(v + 0.5f));
			return;
		}
		float t;
		if (fRange.logarithmic)
			t = logf(v / fRange.lower) / logf(fRange.upper / fRange.lower);
		else
			t = (v - fRange.lower) / (fRange.upper - fRange.lower);
		SetValue((int32)floorf(t * kSliderSteps + 0.5f));
	}

	virtual const char *UpdateText() const
	{
		float v = ValueF();
		if (fRange.integer)
			snprintf(fText, sizeof(fText), "%d", (int)floorf(v + 0.5f));
		else
			snprintf(fText, sizeof(fText), "%.4g", v);
		return fText;
	}

private:
	Ladspa::Range	fRange;
	bool			fIntMode;
	mutable char	fText[32];
};


#endif
