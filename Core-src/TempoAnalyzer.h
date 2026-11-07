/*
 * TempoAnalyzerWindow - «Анализ → Анализатор темпа». Считает темп сведённого проекта и показывает его
 * теми же большими цифрами, что и таймер: «Темп 128», а если темп определить нельзя - «Темп ---».
 * Клик по окну пересчитывает.
 */
#ifndef TEMPO_ANALYZER_H
#define TEMPO_ANALYZER_H

#include <Window.h>
#include <View.h>
#include <Bitmap.h>

class TempoAnalyzerView : public BView {
public:
	TempoAnalyzerView(BRect r);
	virtual ~TempoAnalyzerView();
	virtual void Draw(BRect r);
	virtual void MouseDown(BPoint p);
	void	Analyze();
private:
	BBitmap	*back, *display, *digits;
	double	fBpm;			// 0 - не определён
};

class TempoAnalyzerWindow : public BWindow {
public:
	TempoAnalyzerWindow(BPoint center);
};

#endif
