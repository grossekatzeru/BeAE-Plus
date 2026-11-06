/*
 * TrackPanelView - панель дорожек слева от волны: для каждой дорожки имя, кнопки S/M/R,
 * регуляторы громкости и баланса и индикатор уровня. Клик по дорожке делает её активной.
 */
#ifndef TRACK_PANEL_VIEW_H
#define TRACK_PANEL_VIEW_H

#include <View.h>

class NameEdit;

// Мастер-дорожка справа от панели: заголовок и горизонтальный индикатор уровня общего выхода
class MasterLaneView : public BView {
public:
	MasterLaneView(BRect r);
	virtual void Draw(BRect);
	virtual void Pulse();
	virtual void MouseDown(BPoint);
private:
	float	fL, fR;				// сглаженные пики
	int		fClipHold;			// сколько пульсов держать красный
	float	fLastL, fLastR;
};

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
	enum { HIT_NONE, HIT_NAME, HIT_NAMEBOX, HIT_SOLO, HIT_MUTE, HIT_REC, HIT_VOL, HIT_PAN, HIT_FX, HIT_GRID, HIT_ADD, HIT_REMOVE, HIT_MASTER_MUTE, HIT_MASTER_FX, HIT_MASTER_VOL, HIT_MASTER };

	BRect	StripRect(int index) const;				// прямоугольник дорожки в координатах вида
	BRect	ButtonRect(int index, int which) const;	// 0=S 1=M 2=R 3=FX
	BRect	GridRect(int index) const;				// чекбокс Grid
	BRect	NameRect(int index) const;				// белое поле имени
	BRect	MeterRect(int index) const;				// вертикальный индикатор у левого края
	BRect	VolRect(int index) const;
	BRect	PanRect(int index) const;
	BRect	AddRect() const;
	BRect	RemoveRect() const;
	BRect	MasterRect() const;				// мастер-дорожка: фиксированная полоса внизу панели
	BRect	MasterMeterRect() const;
	BRect	MasterNameRect() const;
	BRect	MasterMuteRect() const;
	BRect	MasterFxRect() const;
	BRect	MasterVolRect() const;
	void	DrawMaster(BRect update);
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
