/*
 * LanesView - стопка полос с волнами. Каждая дорожка занимает свою полосу (высота общая с панелью
 * дорожек слева). В полосе активной дорожки лежит SampleView (редактор), остальные полосы
 * рисует сам LanesView в том же масштабе по времени. Клик по полосе неактивной дорожки
 * делает её активной и ставит курсор в эту точку.
 */
#ifndef LANES_VIEW_H
#define LANES_VIEW_H

#include <View.h>

// Рисует клипы дорожки slot в области body (волна, границы). tl - время левого края (сек), dt - секунд на
// пиксель, W - ширина в пикселях. skipFill: не заливать фон клипа (используется поверх уже нарисованной волны).
void LanesDrawClips(BView *v, int slot, BRect body, double tl, double dt, float W);

class LanesView : public BView {
public:
	LanesView(BRect r);
	virtual void AttachedToWindow();
	virtual void Draw(BRect);
	virtual void MouseDown(BPoint);
	virtual void MouseMoved(BPoint, uint32, const BMessage *);
	virtual void MouseUp(BPoint);
	virtual void FrameResized(float width, float height);
	virtual void MessageReceived(BMessage *msg);
	virtual void Pulse();

	void	Relayout();				// поставить SampleView в полосу активной дорожки

private:
	void	DrawLane(int slot, BRect lane);
	void	DrawPlayLine();
	float	PlayX() const;

	// чтобы понять, что нужно перерисовать
	int			fKeyActive, fKeyCount, fKeyRevision;
	float		fKeyScroll, fKeyW, fKeyH;
	long long	fKeyL, fKeyR, fKeyPointer;
	int			fKeySelection;
	float		fOldPlayX;
	int			fDragSlot;			// инструмент «Перемещение»: в какой дорожке тянем клип (или -1)
	int			fDragClip;
	float		fDragX0;
	double		fDragPos0;
};

#endif
