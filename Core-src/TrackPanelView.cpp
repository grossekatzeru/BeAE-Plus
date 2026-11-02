#include <StorageKit.h>
#include <Window.h>
#include <Font.h>
#include <Message.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

#include "CommonPool.h"
#include "Globals.h"
#include "Mixer.h"
#include "Tracks.h"
#include <Region.h>
#include "TrackPanelView.h"
#include "OffsetWindow.h"

#define HEADER_H	INDEXVIEW_HEIGHT		// верхняя полоска - на уровне обзорной панели
// полоса дорожки: верхние POINTER_BAR_HEIGHT пикселей - как у полоски над волной
#define DB_MIN		-60.0f
#define DB_MAX		6.0f

TrackPanelView::TrackPanelView(BRect r)
	: BView(r, "Track panel", B_FOLLOW_TOP_BOTTOM | B_FOLLOW_LEFT, B_WILL_DRAW | B_PULSE_NEEDED),
	  fDragWhat(HIT_NONE), fDragIndex(-1), fScroll(0), fLastActive(-1), fLastRevision(-1), fLastCount(-1), fLastScroll(-1), fLastHeight(-1)
{
	SetViewColor(B_TRANSPARENT_COLOR);
}

void TrackPanelView::AttachedToWindow()
{
	SetFont(be_plain_font);
	SetFontSize(10);
}

float TrackPanelView::VolToPos(float gain)
{
	if (gain <= 0.001f) return 0;
	float db = 20.0f * log10f(gain);
	float p = (db - DB_MIN) / (DB_MAX - DB_MIN);
	return p < 0 ? 0 : (p > 1 ? 1 : p);
}

float TrackPanelView::PosToVol(float pos)
{
	if (pos <= 0.005f) return 0;
	float db = DB_MIN + pos * (DB_MAX - DB_MIN);
	return powf(10.0f, db / 20.0f);
}

// область полос по высоте: от низа заголовка до верха шкалы времени (как у полос с волнами)
static float AreaHeight(BRect b)
{
	return b.Height() + 1 - HEADER_H - TIMEBAR_HEIGHT;
}

BRect TrackPanelView::StripRect(int slot) const
{
	int rank = Tracks_Rank(slot);
	float top, bottom;
	Tracks_LaneSpan(rank < 0 ? 0 : rank, AreaHeight(Bounds()), top, bottom);
	return BRect(0, HEADER_H + top, Bounds().right, HEADER_H + bottom);
}

BRect TrackPanelView::ButtonRect(int index, int which) const
{
	BRect s = StripRect(index);
	float x = s.left + 4 + which * 22;
	return BRect(x, s.top + 16, x + 18, s.top + 29);
}

BRect TrackPanelView::VolRect(int index) const
{
	BRect s = StripRect(index);
	return BRect(s.left + 6, s.top + 33, s.right - 44, s.top + 40);
}

BRect TrackPanelView::PanRect(int index) const
{
	BRect s = StripRect(index);
	return BRect(s.left + 6, s.top + 42, s.right - 44, s.top + 49);
}

BRect TrackPanelView::AddRect() const
{
	return BRect(Bounds().right - 40, 2, Bounds().right - 22, HEADER_H - 3);
}

BRect TrackPanelView::RemoveRect() const
{
	return BRect(Bounds().right - 20, 2, Bounds().right - 2, HEADER_H - 3);
}

void TrackPanelView::DrawButton(BRect r, const char *label, bool on, rgb_color onColor)
{
	rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
	SetHighColor(on ? onColor : tint_color(base, B_DARKEN_1_TINT));
	FillRect(r);
	SetHighColor(tint_color(base, B_DARKEN_3_TINT));
	StrokeRect(r);
	SetHighColor(on ? make_color(0,0,0) : tint_color(base, B_DARKEN_4_TINT));
	font_height fh;
	GetFontHeight(&fh);
	float w = StringWidth(label);
	DrawString(label, BPoint(r.left + (r.Width() - w)/2 + 1, r.bottom - 3));
}

void TrackPanelView::DrawSlider(BRect r, float pos, bool centered)
{
	rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
	BRect groove(r.left, r.top + 2, r.right, r.bottom - 2);
	SetHighColor(tint_color(base, B_DARKEN_2_TINT));
	FillRect(groove);
	SetHighColor(tint_color(base, B_DARKEN_3_TINT));
	StrokeRect(groove);
	if (centered){
		float cx = (r.left + r.right) / 2;
		SetHighColor(tint_color(base, B_DARKEN_4_TINT));
		StrokeLine(BPoint(cx, r.top), BPoint(cx, r.bottom));
	}
	float x = r.left + pos * r.Width();
	BRect knob(x - 2, r.top, x + 2, r.bottom);
	SetHighColor(ui_color(B_CONTROL_HIGHLIGHT_COLOR));
	FillRect(knob);
	SetHighColor(tint_color(base, B_DARKEN_4_TINT));
	StrokeRect(knob);
}

void TrackPanelView::Draw(BRect update)
{
	rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
	rgb_color text = ui_color(B_PANEL_TEXT_COLOR);
	rgb_color sel = ui_color(B_LIST_SELECTED_BACKGROUND_COLOR);
	BRect b = Bounds();

	SetLowColor(base);
	SetHighColor(base);
	FillRect(b);

	// заголовок с кнопками + и -
	SetHighColor(tint_color(base, B_DARKEN_1_TINT));
	FillRect(BRect(0, 0, b.right, HEADER_H - 1));
	SetHighColor(text);
	DrawString("Tracks", BPoint(6, HEADER_H - 6));
	DrawButton(AddRect(), "+", false, make_color(0,0,0));
	DrawButton(RemoveRect(), "-", false, make_color(0,0,0));

	const int active = Mix.Active();
	const bool playing = Pool.IsPlaying();
	BRegion clip(BRect(0, HEADER_H, b.right, HEADER_H + AreaHeight(b) - 1));
	ConstrainClippingRegion(&clip);
	for (int i=0; i<MIX_MAX_TRACKS; i++){
		MixTrack *t = Mix.Track(i);
		if (!t) continue;
		BRect s = StripRect(i);
		if (s.bottom < HEADER_H || s.top > b.bottom) continue;

		SetHighColor(i == active ? sel : tint_color(base, B_LIGHTEN_1_TINT));
		FillRect(s);
		SetHighColor(tint_color(base, B_DARKEN_2_TINT));
		StrokeLine(BPoint(s.left, s.bottom), BPoint(s.right, s.bottom));

		// имя
		SetHighColor(text);
		SetLowColor(i == active ? sel : tint_color(base, B_LIGHTEN_1_TINT));
		char name[40];
		snprintf(name, sizeof(name), "%s%s", t->name, t->channels == 1 ? " (mono)" : "");
		DrawString(name, BPoint(s.left + 5, s.top + 12));
		if (t->offset > 0.0005){
			char off[24];
			snprintf(off, sizeof(off), "+%.3f s", t->offset);
			DrawString(off, BPoint(s.right - 6 - StringWidth(off), s.top + 12));
		}

		// кнопки
		DrawButton(ButtonRect(i, 0), "S", t->solo, make_color(255, 220, 0));
		DrawButton(ButtonRect(i, 1), "M", t->mute, make_color(255, 140, 0));
		DrawButton(ButtonRect(i, 2), "R", t->armed, make_color(255, 40, 40));

		// индикатор уровня справа от кнопок
		BRect m(s.left + 74, s.top + 17, s.right - 6, s.top + 28);
		SetHighColor(tint_color(base, B_DARKEN_2_TINT));
		FillRect(m);
		float pl = playing ? Mix.PeakL(i) : 0, pr = playing ? Mix.PeakR(i) : 0;
		float hl = (pl > 1 ? 1 : pl) * m.Width(), hr = (pr > 1 ? 1 : pr) * m.Width();
		SetHighColor(make_color(40, 200, 60));
		FillRect(BRect(m.left, m.top + 1, m.left + hl, m.top + 4));
		FillRect(BRect(m.left, m.top + 6, m.left + hr, m.top + 9));

		// громкость
		DrawSlider(VolRect(i), VolToPos(t->gain), false);
		char buf[24];
		if (t->gain <= 0.001f) strcpy(buf, "-inf");
		else snprintf(buf, sizeof(buf), "%.1f dB", 20.0f * log10f(t->gain));
		SetHighColor(text);
		DrawString(buf, BPoint(VolRect(i).right + 4, VolRect(i).bottom));

		// баланс
		DrawSlider(PanRect(i), (t->pan + 1.0f) / 2.0f, true);
		int pv = (int)floorf(fabsf(t->pan) * 100 + 0.5f);
		if (pv == 0) strcpy(buf, "C");
		else snprintf(buf, sizeof(buf), "%c%d", t->pan < 0 ? 'L' : 'R', pv);
		SetHighColor(text);
		DrawString(buf, BPoint(PanRect(i).right + 4, PanRect(i).bottom));
	}

	ConstrainClippingRegion(NULL);
	SetHighColor(tint_color(base, B_DARKEN_3_TINT));
	StrokeLine(BPoint(b.right, 0), BPoint(b.right, b.bottom));
}

int TrackPanelView::HitTest(BPoint p, int &index) const
{
	index = -1;
	if (AddRect().Contains(p)) return HIT_ADD;
	if (RemoveRect().Contains(p)) return HIT_REMOVE;
	for (int i=0; i<MIX_MAX_TRACKS; i++){
		if (!Mix.Track(i)) continue;
		BRect s = StripRect(i);
		if (!s.Contains(p) || p.y < HEADER_H) continue;
		index = i;
		if (ButtonRect(i, 0).Contains(p)) return HIT_SOLO;
		if (ButtonRect(i, 1).Contains(p)) return HIT_MUTE;
		if (ButtonRect(i, 2).Contains(p)) return HIT_REC;
		BRect v = VolRect(i), a = PanRect(i);
		v.InsetBy(-3, -2); a.InsetBy(-3, -2);
		if (v.Contains(p)) return HIT_VOL;
		if (a.Contains(p)) return HIT_PAN;
		return HIT_NAME;
	}
	return HIT_NONE;
}

void TrackPanelView::SetFromMouse(int what, int index, BPoint p)
{
	MixTrack *t = Mix.Track(index);
	if (!t) return;
	if (what == HIT_VOL){
		BRect r = VolRect(index);
		float pos = (p.x - r.left) / r.Width();
		pos = pos < 0 ? 0 : (pos > 1 ? 1 : pos);
		Mix.SetGain(index, PosToVol(pos));
	}else if (what == HIT_PAN){
		BRect r = PanRect(index);
		float pos = (p.x - r.left) / r.Width();
		pos = pos < 0 ? 0 : (pos > 1 ? 1 : pos);
		float pan = pos * 2.0f - 1.0f;
		if (fabsf(pan) < 0.04f) pan = 0;		// «прилипание» к центру
		Mix.SetPan(index, pan);
	}
	Invalidate();
}

void TrackPanelView::MouseDown(BPoint p)
{
	int index;
	int what = HitTest(p, index);
	int32 clicks = 1;
	BMessage *msg = Window()->CurrentMessage();
	if (msg) msg->FindInt32("clicks", &clicks);

	switch (what){
	case HIT_ADD:
		Tracks_Add();
		Invalidate();
		break;
	case HIT_REMOVE:
		Tracks_RemoveActive();
		Invalidate();
		break;
	case HIT_SOLO:
		Mix.SetSolo(index, !Mix.Track(index)->solo);
		Invalidate();
		break;
	case HIT_MUTE:
		Mix.SetMute(index, !Mix.Track(index)->mute);
		Invalidate();
		break;
	case HIT_REC:
		if (Mix.Track(index)->armed){
			Mix.SetArmed(index, false);
		}else{
			Mix.SetArmed(index, true);
			Tracks_Activate(index);		// запись идёт в активную дорожку
		}
		Invalidate();
		break;
	case HIT_NAME:
		if (clicks >= 2){				// двойной клик по имени - смещение дорожки
			(new OffsetWindow(index, Window()))->Show();
		}else{
			Tracks_Activate(index);
		}
		Invalidate();
		break;
	case HIT_VOL:
	case HIT_PAN:
		if (clicks >= 2){				// двойной клик - сброс
			if (what == HIT_VOL) Mix.SetGain(index, 1.0f);
			else Mix.SetPan(index, 0);
			Invalidate();
			break;
		}
		fDragWhat = what;
		fDragIndex = index;
		SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);
		SetFromMouse(what, index, p);
		break;
	default:
		break;
	}
}

void TrackPanelView::MouseMoved(BPoint p, uint32, const BMessage *)
{
	if (fDragWhat != HIT_NONE)
		SetFromMouse(fDragWhat, fDragIndex, p);
}

void TrackPanelView::MouseUp(BPoint)
{
	fDragWhat = HIT_NONE;
	fDragIndex = -1;
}

void TrackPanelView::MessageReceived(BMessage *msg)
{
	if (msg->what == B_MOUSE_WHEEL_CHANGED){
		float dy = 0;
		if (msg->FindFloat("be:wheel_delta_y", &dy) == B_OK){
			Tracks_SetScroll(Tracks_Scroll() + dy * MIN_LANE_HEIGHT / 2, AreaHeight(Bounds()));
			Invalidate();
		}
		return;
	}
	BView::MessageReceived(msg);
}

void TrackPanelView::Pulse()
{
	// перерисовка, пока идёт воспроизведение (индикаторы уровня) или сменилась активная дорожка
	if (Pool.IsPlaying() || fLastActive != Mix.Active() || fLastRevision != Mix.Revision()
		|| fLastScroll != Tracks_Scroll() || fLastCount != Tracks_Count() || fLastHeight != Bounds().Height()){
		fLastActive = Mix.Active();
		fLastRevision = Mix.Revision();
		fLastScroll = Tracks_Scroll();
		fLastCount = Tracks_Count();
		fLastHeight = Bounds().Height();
		Invalidate();
	}
}
