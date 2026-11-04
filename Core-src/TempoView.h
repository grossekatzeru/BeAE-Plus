/*
 * TempoView - темп проекта (BPM) и глубина сетки, рядом с большим таймером (дочерний вид BigView, фон рисует он).
 * Три цифры в том же стиле LCD, рядом кнопки: вверх (+1), кружок (вернуть 120), вниз (-1),
 * и поле глубины квантования (1, 1/2 ... 1/32).  Удержание стрелки - автоповтор.
 */
#ifndef TEMPO_VIEW_H
#define TEMPO_VIEW_H

#include <View.h>
#include <Bitmap.h>

#define TEMPO_VIEW_WIDTH	142

class TempoView : public BView {
public:
	TempoView(BRect r);
	virtual ~TempoView();
	virtual void Draw(BRect);
	virtual void MouseDown(BPoint);
	virtual void MouseMoved(BPoint, uint32, const BMessage *);
	virtual void MouseUp(BPoint);
	virtual void Pulse();

private:
	enum { HIT_NONE, HIT_UP, HIT_RESET, HIT_DOWN, HIT_DEPTH };
	BRect	LcdRect() const;
	BRect	ButtonRect(int which) const;
	BRect	DepthRect() const;
	int		HitTest(BPoint p) const;
	void	Step(int delta);
	void	DrawSmallButton(int which);

	BBitmap	*display, *digits;
	int		fPressed;
	double	fLastBpm;
	int		fLastDiv;
	int		fRepeat;				// пульсов с момента нажатия (автоповтор при удержании)
};

#endif
