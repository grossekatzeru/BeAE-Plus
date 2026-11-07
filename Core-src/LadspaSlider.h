/*
 * LadspaSlider - бегунок параметра LADSPA с полем ввода (общий для окна фильтра и окна эффектов дорожки).
 * Вписанное значение хранится точно, а не округляется до шага бегунка.
 */
#ifndef LADSPA_SLIDER_H
#define LADSPA_SLIDER_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "LadspaHost.h"
#include "ParamSlider.h"

static const int32 kSliderSteps = 1000;

class LadspaSlider : public ParamSlider {
public:
	LadspaSlider(const char *label, BMessage *message, const Ladspa::Range &range, float initial)
		:
		ParamSlider(label, message, 0, kSliderSteps),
		fRange(range),
		fIntMode(range.integer && (range.upper - range.lower) <= 2000.0f),
		fHasExact(false), fExact(0), fExactPos(0)
	{
		if (fIntMode)
			SetPosLimits(0, (int32)floorf(range.upper + 0.5f) - (int32)floorf(range.lower + 0.5f));
		SetValueF(initial);
	}

	float ValueF() const
	{
		if (fHasExact && Pos() == fExactPos)
			return fExact;
		return PosToValue(Pos());
	}

	void SetValueF(float v)
	{
		v = Clamp(v);
		if (fRange.integer) v = floorf(v + 0.5f);
		fExact = v;
		fExactPos = ValueToPos(v);
		fHasExact = true;
		SetPos(fExactPos);
		RefreshText();
	}

protected:
	virtual BString FormatText() const
	{
		char b[40];
		float v = ValueF();
		if (fRange.integer) snprintf(b, sizeof(b), "%d", (int)floorf(v + 0.5f));
		else snprintf(b, sizeof(b), "%.6g", v);
		return BString(b);
	}

	virtual bool ApplyText(const char *text)
	{
		char buf[64];
		strncpy(buf, text, sizeof(buf) - 1);
		buf[sizeof(buf) - 1] = 0;
		for (char *c = buf; *c; c++) if (*c == ',') *c = '.';		// десятичная запятая
		char *end = NULL;
		double v = strtod(buf, &end);
		if (end == buf) return false;
		SetValueF((float)v);
		return true;
	}

	virtual void SliderMoved() { fHasExact = false; }

private:
	float Clamp(float v) const
	{
		if (!(v >= fRange.lower)) v = fRange.lower;			// заодно NaN
		if (v > fRange.upper) v = fRange.upper;
		return v;
	}
	int32 ValueToPos(float v) const
	{
		if (fIntMode) return (int32)floorf(v + 0.5f) - (int32)floorf(fRange.lower + 0.5f);
		float t;
		if (fRange.logarithmic) t = logf(v / fRange.lower) / logf(fRange.upper / fRange.lower);
		else t = (v - fRange.lower) / (fRange.upper - fRange.lower);
		return (int32)floorf(t * kSliderSteps + 0.5f);
	}
	float PosToValue(int32 pos) const
	{
		if (fIntMode) return (float)(pos + (int32)floorf(fRange.lower + 0.5f));
		float t = (float)pos / (float)kSliderSteps, v;
		if (fRange.logarithmic) v = fRange.lower * powf(fRange.upper / fRange.lower, t);
		else v = fRange.lower + t * (fRange.upper - fRange.lower);
		if (fRange.integer) v = floorf(v + 0.5f);
		return v;
	}

	Ladspa::Range	fRange;
	bool			fIntMode;
	bool			fHasExact;
	float			fExact;
	int32			fExactPos;
};

#endif
