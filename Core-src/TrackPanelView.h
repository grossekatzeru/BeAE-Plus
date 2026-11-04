/*
 * TrackPanelView - панель дорожек слева от волны: для каждой дорожки имя, кнопки S/M/R,
 * регуляторы громкости и баланса и индикатор уровня. Клик по дорожке делает её активной.
 */
#ifndef TRACK_PANEL_VIEW_H
#define TRACK_PANEL_VIEW_H

#include <View.h>

class NameEdit;

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
	void	CommitNameEdit() { EndNameEdit(true); }
	virtual void MessageReceived(BMessage *msg);

private:
	enum { HIT_NONE, HIT_NAME, HIT_NAMEBOX, HIT_SOLO, HIT_MUTE, HIT_REC, HIT_VOL, HIT_PAN, HIT_FX, HIT_GRID, HIT_ADD, HIT_REMOVE };

	BRect	StripRect(int index) const;				// прямоугольник дорожки в координатах вида
	BRect	ButtonRect(int index, int which) const;	// 0=S 1=M 2=R 3=FX
	BRect	GridRect(int index) const;				// чекбокс Grid
	BRect	NameRect(int index) const;				// белое поле имени
	BRect	MeterRect(int index) const;				// вертикальный индикатор у левого края
	BRect	VolRect(int index) const;
	BRect	PanRect(int index) const;
	BRect	AddRect() const;
	BRect	RemoveRect() const;
	int		HitTest(BPoint p, int &index) const;
	void	DrawButton(BRect r, const char *label, bool on, rgb_color onColor);
	void	DrawSlider(BRect r, float pos, bool centered);
	void	SetFromMouse(int what, int index, BPoint p);
	void	BeginNameEdit(int index);
	void	EndNameEdit(bool commit);

	static float	VolToPos(float gain);
	static float	PosToVol(float pos);

	NameEdit	*fEdit;
	int		fEditIndex;
	int		fDragWhat, fDragIndex;
	float	fScroll;
	int		fLastActive, fLastRevision, fLastCount;
	float	fLastScroll, fLastHeight;
};

#endif
