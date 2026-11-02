/*
 * TrackPanelView - панель дорожек слева от волны: для каждой дорожки имя, кнопки S/M/R,
 * регуляторы громкости и баланса и индикатор уровня. Клик по дорожке делает её активной.
 */
#ifndef TRACK_PANEL_VIEW_H
#define TRACK_PANEL_VIEW_H

#include <View.h>

#define TRACKPANEL_WIDTH	136

class TrackPanelView : public BView {
public:
	TrackPanelView(BRect r);
	virtual void AttachedToWindow();
	virtual void Draw(BRect);
	virtual void MouseDown(BPoint);
	virtual void MouseUp(BPoint);
	virtual void MouseMoved(BPoint, uint32, const BMessage *);
	virtual void Pulse();
	virtual void MessageReceived(BMessage *msg);

private:
	enum { HIT_NONE, HIT_NAME, HIT_SOLO, HIT_MUTE, HIT_REC, HIT_VOL, HIT_PAN, HIT_ADD, HIT_REMOVE };

	BRect	StripRect(int index) const;				// прямоугольник дорожки в координатах вида
	BRect	ButtonRect(int index, int which) const;	// 0=S 1=M 2=R
	BRect	VolRect(int index) const;
	BRect	PanRect(int index) const;
	BRect	AddRect() const;
	BRect	RemoveRect() const;
	int		HitTest(BPoint p, int &index) const;
	void	DrawButton(BRect r, const char *label, bool on, rgb_color onColor);
	void	DrawSlider(BRect r, float pos, bool centered);
	void	SetFromMouse(int what, int index, BPoint p);

	static float	VolToPos(float gain);
	static float	PosToVol(float pos);

	int		fDragWhat, fDragIndex;
	float	fScroll;
	int		fLastActive, fLastRevision, fLastCount;
	float	fLastScroll, fLastHeight;
};

#endif
