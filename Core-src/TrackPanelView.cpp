#include <StorageKit.h>
#include <String.h>
#include <Window.h>
#include <Font.h>
#include <Message.h>
#include <TextView.h>
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
#include "FxWindow.h"
#include "AudioRecorder.h"

#define HEADER_H	INDEXVIEW_HEIGHT		// верхняя полоска - на уровне обзорной панели
// полоса дорожки: верхние POINTER_BAR_HEIGHT пикселей - как у полоски над волной
#define DB_MIN		-60.0f
#define DB_MAX		6.0f

#define MSG_NAME_OK		'tnOk'
#define MSG_NAME_CANCEL	'tnCn'
#define KNOB_W			7		// ширина бегунка
#define TICKS			10		// число засечек под бегунком

// поле ввода названия дорожки: Enter - принять, Esc - отменить, потеря фокуса - принять
class NameEdit : public BTextView {
public:
	NameEdit(BRect frame, BView *owner)
		: BTextView(frame, "nameedit", BRect(2, 1, frame.Width() - 2, frame.Height() - 1),
			B_FOLLOW_NONE, B_WILL_DRAW | B_FRAME_EVENTS),
		  fOwner(owner)
	{
		SetViewColor(255, 255, 255);
		SetLowColor(255, 255, 255);
		SetFontSize(10);
		SetMaxBytes(63);
	}
	virtual void KeyDown(const char *bytes, int32 n)
	{
		if (n == 1 && (bytes[0] == B_ENTER || bytes[0] == B_TAB)) { fOwner->Window()->PostMessage(MSG_NAME_OK, fOwner); return; }
		if (n == 1 && bytes[0] == B_ESCAPE) { fOwner->Window()->PostMessage(MSG_NAME_CANCEL, fOwner); return; }
		BTextView::KeyDown(bytes, n);
	}
	virtual void MakeFocus(bool focus = true)
	{
		BTextView::MakeFocus(focus);
		if (!focus && !IsHidden()) fOwner->Window()->PostMessage(MSG_NAME_OK, fOwner);
	}
private:
	BView *fOwner;
};

TrackPanelView::TrackPanelView(BRect r)
	: BView(r, "Track panel", B_FOLLOW_TOP_BOTTOM | B_FOLLOW_LEFT, B_WILL_DRAW | B_PULSE_NEEDED),
	  fEdit(NULL), fEditIndex(-1), fDragWhat(HIT_NONE), fDragIndex(-1), fScroll(0), fLastActive(-1), fLastRevision(-1), fLastCount(-1), fLastScroll(-1), fLastHeight(-1)
{
	SetViewColor(B_TRANSPARENT_COLOR);
}

void TrackPanelView::AttachedToWindow()
{
	SetFont(be_plain_font);
	SetFontSize(10);
	fEdit = new NameEdit(BRect(0, 0, 80, 14), this);
	AddChild(fEdit);
	fEdit->Hide();
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
	return b.Height() + 1 - HEADER_H - TIMEBAR_HEIGHT - MASTER_STRIP_H;		// внизу - мастер-дорожка
}

BRect TrackPanelView::StripRect(int slot) const
{
	int rank = Tracks_Rank(slot);
	float top, bottom;
	Tracks_LaneSpan(rank < 0 ? 0 : rank, AreaHeight(Bounds()), top, bottom);
	return BRect(0, HEADER_H + top, Bounds().right, HEADER_H + bottom);
}

#define CONTENT_X	18		// слева от этого места - индикатор уровня

BRect TrackPanelView::MeterRect(int index) const
{
	BRect s = StripRect(index);
	return BRect(s.left + 1, s.top + 2, s.left + 14, s.bottom - 3);
}

BRect TrackPanelView::NameRect(int index) const
{
	BRect s = StripRect(index);
	return BRect(s.left + CONTENT_X, s.top + 3, s.right - 5, s.top + 18);
}

BRect TrackPanelView::ButtonRect(int index, int which) const
{
	BRect s = StripRect(index);
	float x = s.left + CONTENT_X + which * 22;
	return BRect(x, s.top + 23, x + (which == 3 ? 24 : 18), s.top + 36);	// 3 = FX, шире
}

BRect TrackPanelView::GridRect(int index) const
{
	BRect s = StripRect(index);
	// строка на всю ширину регуляторов: подпись у левого края, чекбокс у правого; следующие чекбоксы - строками ниже
	return BRect(s.left + CONTENT_X, s.top + 80, PanRect(index).right + KNOB_W / 2, s.top + 93);
}

BRect TrackPanelView::AutoRect(int index) const
{
	const int rank = Tracks_Rank(index);
	float top, bottom;
	Tracks_StripSpan(rank < 0 ? 0 : rank, AreaHeight(Bounds()), top, bottom);
	return BRect(0, HEADER_H + top, Bounds().right, HEADER_H + bottom);
}

BRect TrackPanelView::ShowVolRect(int index) const
{
	BRect a = AutoRect(index), g = GridRect(index);
	return BRect(g.left, a.top + 4, g.right, a.top + 17);
}

BRect TrackPanelView::ShowPanRect(int index) const
{
	BRect v = ShowVolRect(index);
	return BRect(v.left, v.top + 17, v.right, v.bottom + 17);
}

BRect TrackPanelView::XfadeRect(int index) const
{
	BRect g = GridRect(index);
	return BRect(g.left, g.top + 17, g.right, g.bottom + 17);
}

BRect TrackPanelView::StickyRect(int index) const
{
	BRect g = GridRect(index);
	return BRect(g.left, g.top + 34, g.right, g.bottom + 34);
}

BRect TrackPanelView::VolRect(int index) const
{
	BRect s = StripRect(index);
	return BRect(s.left + CONTENT_X + KNOB_W / 2, s.top + 41, s.right - 48, s.top + 51);
}

BRect TrackPanelView::PanRect(int index) const
{
	BRect s = StripRect(index);
	return BRect(s.left + CONTENT_X + KNOB_W / 2, s.top + 62, s.right - 48, s.top + 72);
}

BRect TrackPanelView::AddRect() const
{
	return BRect(Bounds().right - 40, 2, Bounds().right - 22, HEADER_H - 3);
}

BRect TrackPanelView::RemoveRect() const
{
	return BRect(Bounds().right - 20, 2, Bounds().right - 2, HEADER_H - 3);
}

BRect TrackPanelView::MasterRect() const
{
	const float top = HEADER_H + AreaHeight(Bounds());
	return BRect(0, top, Bounds().right, top + MASTER_STRIP_H - 1);
}

BRect TrackPanelView::MasterMeterRect() const
{
	BRect s = MasterRect();
	return BRect(s.left + 1, s.top + 2, s.left + 14, s.bottom - 3);
}

BRect TrackPanelView::MasterNameRect() const
{
	BRect s = MasterRect();
	return BRect(s.left + CONTENT_X, s.top + 3, s.right - 5, s.top + 18);
}

BRect TrackPanelView::MasterMuteRect() const
{
	BRect s = MasterRect();
	return BRect(s.left + CONTENT_X, s.top + 23, s.left + CONTENT_X + 18, s.top + 36);
}

BRect TrackPanelView::MasterFxRect() const
{
	BRect s = MasterRect();
	return BRect(s.left + CONTENT_X + 22, s.top + 23, s.left + CONTENT_X + 22 + 24, s.top + 36);
}

BRect TrackPanelView::MasterVolRect() const
{
	BRect s = MasterRect();
	return BRect(s.left + CONTENT_X + KNOB_W / 2, s.top + 41, s.right - 48, s.top + 51);
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
	BRect groove(r.left - KNOB_W / 2, r.top + 3, r.right + KNOB_W / 2, r.bottom - 3);
	SetHighColor(tint_color(base, B_DARKEN_2_TINT));
	FillRect(groove);
	SetHighColor(tint_color(base, B_DARKEN_3_TINT));
	StrokeRect(groove);
	if (centered){
		float cx = floorf((r.left + r.right) / 2);
		SetHighColor(tint_color(base, B_DARKEN_4_TINT));
		StrokeLine(BPoint(cx, r.top), BPoint(cx, r.bottom));
	}
	// 10 засечек под бегунком
	SetHighColor(tint_color(base, B_DARKEN_4_TINT));
	for (int k = 0; k < TICKS; k++){
		float x = floorf(r.left + k * r.Width() / (TICKS - 1) + 0.5f);
		StrokeLine(BPoint(x, r.bottom + 2), BPoint(x, r.bottom + 4));
	}
	float x = floorf(r.left + pos * r.Width() + 0.5f);
	BRect knob(x - KNOB_W / 2, r.top, x + KNOB_W / 2, r.bottom);
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

		{	// объёмная «растяжка» фона, как у панели большого таймера: светлая кромка сверху, плоская середина, затемнение книзу
			const rgb_color c0 = (i == active) ? sel : tint_color(base, B_LIGHTEN_1_TINT);
			const float h = s.Height();
			for (float y = s.top; y <= s.bottom; y += 1.0f){
				const float k = (y - s.top) / h;				// 0 сверху .. 1 снизу
				rgb_color c = c0;
				if (k < 0.08f){									// блик у верхнего края
					const float w = 0.55f * (1.0f - k / 0.08f);
					c.red += (uint8)((255 - c.red) * w); c.green += (uint8)((255 - c.green) * w); c.blue += (uint8)((255 - c.blue) * w);
				}else if (k > 0.45f){							// плавное затемнение книзу
					const float w = 0.16f * (k - 0.45f) / 0.55f;
					c.red = (uint8)(c.red * (1 - w)); c.green = (uint8)(c.green * (1 - w)); c.blue = (uint8)(c.blue * (1 - w));
				}
				SetHighColor(c);
				StrokeLine(BPoint(s.left, y), BPoint(s.right, y));
			}
		}
		SetHighColor(tint_color(base, B_DARKEN_2_TINT));
		StrokeLine(BPoint(s.left, s.bottom), BPoint(s.right, s.bottom));

		// поле имени: белое, чёрная рамка
		BRect nb = NameRect(i);
		SetHighColor(255, 255, 255);
		FillRect(nb);
		SetHighColor(0, 0, 0);
		StrokeRect(nb);
		if (i != fEditIndex){
			SetHighColor(0, 0, 0);
			SetLowColor(255, 255, 255);
			float room = nb.Width() - 8;
			const double laneOff = (i == active) ? (Pool.frequency > 1.0f ? (double)Pool.pad_lead / Pool.frequency : 0.0)
				: (Mix.Clip(i, 0) ? Mix.Clip(i, 0)->pos : 0.0);
			if (laneOff > 0.0005){
				char off[24];
				snprintf(off, sizeof(off), "+%.3f s", laneOff);
				SetHighColor(110, 110, 110);
				DrawString(off, BPoint(nb.right - 4 - StringWidth(off), nb.bottom - 4));
				room -= StringWidth(off) + 6;
				SetHighColor(0, 0, 0);
			}
			BString shown(t->name);
			TruncateString(&shown, B_TRUNCATE_END, room < 20 ? 20 : room);
			DrawString(shown.String(), BPoint(nb.left + 4, nb.bottom - 4));
		}
		SetLowColor(i == active ? sel : tint_color(base, B_LIGHTEN_1_TINT));

		// кнопки
		DrawButton(ButtonRect(i, 0), "S", t->solo, make_color(255, 220, 0));
		DrawButton(ButtonRect(i, 1), "M", t->mute, make_color(255, 140, 0));
		DrawButton(ButtonRect(i, 2), "R", t->armed, make_color(255, 40, 40));
		DrawButton(ButtonRect(i, 3), "FX", Mix.Fx(i).Count() > 0, make_color(110, 200, 110));
		// под регулировкой баланса - строки «подпись + чекбокс»: Grid (липкая сетка) и Xfade (кроссфейд перекрытий)
		for (int row = 0; row < 3; row++){
			BRect g = (row == 0) ? GridRect(i) : (row == 1 ? XfadeRect(i) : StickyRect(i));
			const bool on = (row == 0) ? t->grid : (row == 1 ? t->xfade : t->sticky);
			BRect box(g.right - 11, g.top + 1, g.right, g.top + 12);
			SetHighColor(text);
			DrawString(row == 0 ? "Grid" : (row == 1 ? "Xfade" : Language.get("TRACK_STICKY")), BPoint(g.left, g.bottom - 3));
			SetHighColor(make_color(255, 255, 255));
			FillRect(box);
			SetHighColor(make_color(0, 0, 0));
			StrokeRect(box);
			if (on){
				SetPenSize(2);
				StrokeLine(BPoint(box.left + 3, box.top + 6), BPoint(box.left + 5, box.bottom - 3));
				StrokeLine(BPoint(box.left + 5, box.bottom - 3), BPoint(box.right - 2, box.top + 3));
				SetPenSize(1);
			}
			// вдавленная черта под строкой: от начала подписи до конца чекбокса
			SetHighColor(tint_color(base, B_DARKEN_3_TINT));
			StrokeLine(BPoint(g.left, g.bottom + 1), BPoint(g.right, g.bottom + 1));
			SetHighColor(tint_color(base, B_LIGHTEN_2_TINT));
			StrokeLine(BPoint(g.left, g.bottom + 2), BPoint(g.right, g.bottom + 2));
		}
		// под дорожкой (место полосы автоматизации): чекбоксы показа кривых; цвет подписи - цвет кривой
		for (int row = 0; row < 2 && t->autoOpen; row++){
			BRect g = (row == 0) ? ShowVolRect(i) : ShowPanRect(i);
			const bool on = (row == 0) ? t->showVol : t->showPan;
			BRect box(g.right - 11, g.top + 1, g.right, g.top + 12);
			SetHighColor(row == 0 ? make_color(200, 30, 35) : make_color(40, 90, 210));
			DrawString(Language.get(row == 0 ? "AUTO_VOL" : "AUTO_PAN"), BPoint(g.left, g.bottom - 3));
			SetHighColor(make_color(255, 255, 255));
			FillRect(box);
			SetHighColor(make_color(0, 0, 0));
			StrokeRect(box);
			if (on){
				SetPenSize(2);
				StrokeLine(BPoint(box.left + 3, box.top + 6), BPoint(box.left + 5, box.bottom - 3));
				StrokeLine(BPoint(box.left + 5, box.bottom - 3), BPoint(box.right - 2, box.top + 3));
				SetPenSize(1);
			}
			SetHighColor(tint_color(base, B_DARKEN_3_TINT));
			StrokeLine(BPoint(g.left, g.bottom + 1), BPoint(g.right, g.bottom + 1));
			SetHighColor(tint_color(base, B_LIGHTEN_2_TINT));
			StrokeLine(BPoint(g.left, g.bottom + 2), BPoint(g.right, g.bottom + 2));
		}

		// индикатор уровня: вертикальный, у левого края, фон «мокрый асфальт»
		BRect m = MeterRect(i);
		SetHighColor(52, 73, 94);
		FillRect(m);
		SetHighColor(0, 0, 0);
		StrokeRect(m);
		{
			float pl = playing ? Mix.PeakL(i) : 0, pr = playing ? Mix.PeakR(i) : 0;
			const float inner = m.Height() - 2;
			const float bw = 4;
			float hl = (pl > 1 ? 1 : pl) * inner, hr = (pr > 1 ? 1 : pr) * inner;
			SetHighColor(pl >= 1.0f ? make_color(255, 60, 40) : make_color(40, 210, 70));
			if (hl >= 1) FillRect(BRect(m.left + 2, m.bottom - 1 - hl + 1, m.left + 2 + bw - 1, m.bottom - 1));
			SetHighColor(pr >= 1.0f ? make_color(255, 60, 40) : make_color(40, 210, 70));
			if (hr >= 1) FillRect(BRect(m.left + 2 + bw + 1, m.bottom - 1 - hr + 1, m.left + 2 + 2 * bw, m.bottom - 1));
		}

		// громкость
		DrawSlider(VolRect(i), VolToPos(t->gain), false);
		char buf[24];
		if (t->gain <= 0.001f) strcpy(buf, "-inf");
		else snprintf(buf, sizeof(buf), "%.1f dB", 20.0f * log10f(t->gain));
		SetHighColor(text);
		DrawString(buf, BPoint(VolRect(i).right + KNOB_W / 2 + 4, VolRect(i).bottom - 1));

		// баланс
		DrawSlider(PanRect(i), (t->pan + 1.0f) / 2.0f, true);
		int pv = (int)floorf(fabsf(t->pan) * 100 + 0.5f);
		if (pv == 0) strcpy(buf, "C");
		else snprintf(buf, sizeof(buf), "%c%d", t->pan < 0 ? 'L' : 'R', pv);
		SetHighColor(text);
		DrawString(buf, BPoint(PanRect(i).right + KNOB_W / 2 + 4, PanRect(i).bottom - 1));
	}

	ConstrainClippingRegion(NULL);
	DrawMaster(update);
	SetHighColor(tint_color(base, B_DARKEN_3_TINT));
	StrokeLine(BPoint(b.right, 0), BPoint(b.right, b.bottom));
}

// Мастер-дорожка: всегда последняя, не удаляется и не выбирается как активная.
// Громкость, mute, эффекты (FX) и индикатор уровня общего выхода.
void TrackPanelView::DrawMaster(BRect)
{
	rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
	rgb_color text = ui_color(B_PANEL_TEXT_COLOR);
	BRect s = MasterRect();
	const bool playing = Pool.IsPlaying();

	SetHighColor(tint_color(base, B_DARKEN_1_TINT));
	FillRect(s);
	SetHighColor(tint_color(base, B_DARKEN_3_TINT));
	StrokeLine(BPoint(s.left, s.top), BPoint(s.right, s.top));
	SetHighColor(tint_color(base, B_LIGHTEN_2_TINT));
	StrokeLine(BPoint(s.left, s.top + 1), BPoint(s.right, s.top + 1));
	SetLowColor(tint_color(base, B_DARKEN_1_TINT));

	// название
	BRect nb = MasterNameRect();
	SetHighColor(tint_color(base, B_DARKEN_2_TINT));
	FillRect(nb);
	SetHighColor(0, 0, 0);
	StrokeRect(nb);
	SetHighColor(255, 255, 255);
	SetLowColor(tint_color(base, B_DARKEN_2_TINT));
	DrawString("Master", BPoint(nb.left + 4, nb.bottom - 4));
	SetLowColor(tint_color(base, B_DARKEN_1_TINT));

	DrawButton(MasterMuteRect(), "M", Mix.MasterMute(), make_color(255, 140, 0));
	DrawButton(MasterFxRect(), "FX", Mix.Fx(MIX_MASTER).Count() > 0, make_color(110, 200, 110));

	// индикатор уровня
	BRect m = MasterMeterRect();
	SetHighColor(52, 73, 94);
	FillRect(m);
	SetHighColor(0, 0, 0);
	StrokeRect(m);
	{
		float pl = playing ? Mix.MasterPeakL() : 0, pr = playing ? Mix.MasterPeakR() : 0;
		const float inner = m.Height() - 2;
		const float bw = 4;
		float hl = (pl > 1 ? 1 : pl) * inner, hr = (pr > 1 ? 1 : pr) * inner;
		SetHighColor(pl >= 1.0f ? make_color(255, 60, 40) : make_color(40, 210, 70));
		if (hl >= 1) FillRect(BRect(m.left + 2, m.bottom - 1 - hl + 1, m.left + 2 + bw - 1, m.bottom - 1));
		SetHighColor(pr >= 1.0f ? make_color(255, 60, 40) : make_color(40, 210, 70));
		if (hr >= 1) FillRect(BRect(m.left + 2 + bw + 1, m.bottom - 1 - hr + 1, m.left + 2 + 2 * bw, m.bottom - 1));
	}

	// громкость
	const float g = Mix.MasterGain();
	DrawSlider(MasterVolRect(), VolToPos(g), false);
	char buf[24];
	if (g <= 0.001f) strcpy(buf, "-inf");
	else snprintf(buf, sizeof(buf), "%.1f dB", 20.0f * log10f(g));
	SetHighColor(text);
	DrawString(buf, BPoint(MasterVolRect().right + KNOB_W / 2 + 4, MasterVolRect().bottom - 1));
}

int TrackPanelView::HitTest(BPoint p, int &index) const
{
	index = -1;
	if (AddRect().Contains(p)) return HIT_ADD;
	if (RemoveRect().Contains(p)) return HIT_REMOVE;
	if (MasterRect().Contains(p)){
		index = MIX_MASTER;
		if (MasterMuteRect().Contains(p)) return HIT_MASTER_MUTE;
		if (MasterFxRect().Contains(p)) return HIT_MASTER_FX;
		BRect v = MasterVolRect();
		v.InsetBy(-KNOB_W / 2 - 1, -2);
		if (v.Contains(p)) return HIT_MASTER_VOL;
		return HIT_MASTER;
	}
	if (p.y >= MasterRect().top) return HIT_NONE;
	for (int i=0; i<MIX_MAX_TRACKS; i++){
		if (!Mix.Track(i)) continue;
		{	// место под дорожкой: чекбоксы показа кривых автоматизации
			const BRect st = AutoRect(i);
			if (st.Contains(p) && p.y >= HEADER_H){
				index = i;
				if (!Mix.Track(i)->autoOpen) return HIT_NONE;
				if (ShowVolRect(i).Contains(p)) return HIT_SHOWVOL;
				if (ShowPanRect(i).Contains(p)) return HIT_SHOWPAN;
				return HIT_NONE;
			}
		}
		BRect s = StripRect(i);
		if (!s.Contains(p) || p.y < HEADER_H) continue;
		index = i;
		if (ButtonRect(i, 0).Contains(p)) return HIT_SOLO;
		if (ButtonRect(i, 1).Contains(p)) return HIT_MUTE;
		if (ButtonRect(i, 2).Contains(p)) return HIT_REC;
		if (ButtonRect(i, 3).Contains(p)) return HIT_FX;
		if (GridRect(i).Contains(p)) return HIT_GRID;
		if (XfadeRect(i).Contains(p)) return HIT_XFADE;
		if (StickyRect(i).Contains(p)) return HIT_STICKY;
		if (NameRect(i).Contains(p)) return HIT_NAMEBOX;
		BRect v = VolRect(i), a = PanRect(i);
		v.InsetBy(-KNOB_W / 2 - 1, -2); a.InsetBy(-KNOB_W / 2 - 1, -2);
		if (v.Contains(p)) return HIT_VOL;
		if (a.Contains(p)) return HIT_PAN;
		return HIT_NAME;
	}
	return HIT_NONE;
}

void TrackPanelView::SetFromMouse(int what, int index, BPoint p)
{
	if (what == HIT_MASTER_VOL){
		BRect r = MasterVolRect();
		float pos = (p.x - r.left) / r.Width();
		pos = pos < 0 ? 0 : (pos > 1 ? 1 : pos);
		Mix.SetMasterGain(PosToVol(pos));
		Invalidate();
		return;
	}
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

void TrackPanelView::BeginNameEdit(int index)
{
	MixTrack *t = Mix.Track(index);
	if (!t || !fEdit) return;
	EndNameEdit(true);
	BRect r = NameRect(index);
	r.InsetBy(1, 1);
	fEditIndex = index;
	fEdit->MoveTo(r.LeftTop());
	fEdit->ResizeTo(r.Width(), r.Height());
	fEdit->SetTextRect(BRect(2, 0, r.Width() - 2, r.Height()));
	fEdit->SetText(t->name);
	fEdit->Show();
	fEdit->MakeFocus(true);
	fEdit->SelectAll();
}

void TrackPanelView::EndNameEdit(bool commit)
{
	if (fEditIndex < 0 || !fEdit) return;
	const int idx = fEditIndex;
	fEditIndex = -1;							// до Hide(): потеря фокуса не должна зациклить
	if (commit){
		BString n(fEdit->Text());
		n.Trim();
		if (n.Length() > 0) Tracks_SetName(idx, n.String());
	}
	if (!fEdit->IsHidden()) fEdit->Hide();
	Invalidate();
}

void TrackPanelView::MouseDown(BPoint p)
{
	if (fEditIndex >= 0) EndNameEdit(true);	// клик в другом месте принимает имя
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
	case HIT_MASTER_MUTE:
		Mix.SetMasterMute(!Mix.MasterMute());
		Invalidate();
		break;
	case HIT_MASTER_FX:
		FxWindow::Open(MIX_MASTER, Window());
		break;
	case HIT_MASTER_VOL:
		if (clicks >= 2){				// двойной клик - 0 дБ
			Mix.SetMasterGain(1.0f);
			Invalidate();
			break;
		}
		fDragWhat = what;
		fDragIndex = MIX_MASTER;
		SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);
		SetFromMouse(what, MIX_MASTER, p);
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
	case HIT_GRID:
		Mix.SetGrid(index, !Mix.Track(index)->grid);
		Pool.changed = true;
		Invalidate();
		break;
	case HIT_XFADE:
		Mix.SetCrossfade(index, !Mix.Track(index)->xfade);
		Pool.changed = true;
		Invalidate();
		break;
	case HIT_STICKY:
		Mix.SetSticky(index, !Mix.Track(index)->sticky);
		Pool.changed = true;
		Invalidate();
		break;
	case HIT_SHOWVOL:
		Mix.SetShowAuto(index, !Mix.Track(index)->showVol, Mix.Track(index)->showPan);
		Pool.changed = true;
		Invalidate();
		break;
	case HIT_SHOWPAN:
		Mix.SetShowAuto(index, Mix.Track(index)->showVol, !Mix.Track(index)->showPan);
		Pool.changed = true;
		Invalidate();
		break;
	case HIT_FX:
		FxWindow::Open(index, Window());
		break;
	case HIT_NAMEBOX:
		if (index != Mix.Active()) Tracks_Activate(index);
		BeginNameEdit(index);
		Invalidate();
		break;
	case HIT_NAME:
		if (clicks >= 2){				// двойной клик по фону дорожки - смещение
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
	if (msg->what == MSG_NAME_OK){ EndNameEdit(true); return; }
	if (msg->what == MSG_NAME_CANCEL){ EndNameEdit(false); return; }
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
	if (fEditIndex >= 0){						// прокрутка или смена числа дорожек - поле идёт за дорожкой
		if (!Mix.Track(fEditIndex)){ EndNameEdit(false); }
		else{
			BRect r = NameRect(fEditIndex);
			r.InsetBy(1, 1);
			if (fEdit->Frame().LeftTop() != r.LeftTop()) fEdit->MoveTo(r.LeftTop());
		}
	}
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


// ---------------------------------------------------------------- мастер-дорожка (полоса справа от панели)

MasterLaneView::MasterLaneView(BRect r)
	: BView(r, "Master lane", B_FOLLOW_BOTTOM | B_FOLLOW_LEFT_RIGHT, B_WILL_DRAW | B_PULSE_NEEDED | B_FULL_UPDATE_ON_RESIZE),
	  fL(0), fR(0), fInL(0), fInR(0), fClipHold(0), fInClipHold(0), fLastL(-1), fLastR(-1), fLastInL(-1), fLastInR(-1)
{
	SetViewColor(B_TRANSPARENT_COLOR);
}

static float PeakToPos(float peak)
{
	if (peak <= 0.001f) return 0;
	float db = 20.0f * log10f(peak);
	float p = (db - DB_MIN) / (0.0f - DB_MIN);
	return p < 0 ? 0 : (p > 1 ? 1 : p);
}

void MasterLaneView::Draw(BRect)
{
	rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
	BRect b = Bounds();
	SetHighColor(tint_color(base, B_DARKEN_1_TINT));
	FillRect(b);
	SetHighColor(tint_color(base, B_DARKEN_3_TINT));
	StrokeLine(BPoint(b.left, b.top), BPoint(b.right, b.top));
	SetHighColor(tint_color(base, B_LIGHTEN_2_TINT));
	StrokeLine(BPoint(b.left, b.top + 1), BPoint(b.right, b.top + 1));

	SetFont(be_plain_font);
	SetFontSize(9);
	const float x0 = 36, x1 = b.right - 40;
	if (x1 - x0 < 40) return;
	SetHighColor(tint_color(base, B_DARKEN_4_TINT));
	SetLowColor(tint_color(base, B_DARKEN_1_TINT));

	// два индикатора: In (уровень входа при записи) и Out (общий выход); у каждого каналы L и R
	const float h = 8;
	const float ys[4] = { b.top + 5, b.top + 14, b.top + 26, b.top + 35 };
	const float vals[4] = { fInL, fInR, fL, fR };
	DrawString(Language.get("METER_IN"), BPoint(4, ys[0] + h + 2));
	DrawString(Language.get("METER_OUT"), BPoint(4, ys[2] + h + 2));
	for (int ch = 0; ch < 4; ch++){
		BRect bar(x0, ys[ch], x1, ys[ch] + h);
		SetHighColor(52, 73, 94);
		FillRect(bar);
		SetHighColor(0, 0, 0);
		StrokeRect(bar);
		const float w = (bar.Width() - 2) * PeakToPos(vals[ch]);
		if (w >= 1){
			SetHighColor((ch < 2 ? fInClipHold : fClipHold) > 0 ? make_color(255, 60, 40) : make_color(40, 210, 70));
			FillRect(BRect(bar.left + 1, bar.top + 1, bar.left + w, bar.bottom - 1));
		}
		SetHighColor(tint_color(base, B_DARKEN_4_TINT));
		DrawString((ch & 1) == 0 ? "L" : "R", BPoint(x0 - 10, ys[ch] + h - 1));
	}
	// шкала, дБ
	const int marks[] = { -48, -36, -24, -12, -6, 0 };
	SetHighColor(tint_color(base, B_DARKEN_4_TINT));
	for (size_t k = 0; k < sizeof(marks) / sizeof(marks[0]); k++){
		const float x = floorf(x0 + 1 + (x1 - x0 - 2) * (marks[k] - DB_MIN) / (0.0f - DB_MIN));
		StrokeLine(BPoint(x, ys[3] + h + 2), BPoint(x, ys[3] + h + 5));
		char s[8];
		snprintf(s, sizeof(s), "%d", marks[k]);
		DrawString(s, BPoint(x - StringWidth(s) / 2, b.bottom - 1));
	}
}

void MasterLaneView::Pulse()
{
	float l = Pool.IsPlaying() ? Mix.MasterPeakL() : 0, r = Pool.IsPlaying() ? Mix.MasterPeakR() : 0;
	fL = l > fL ? l : fL * 0.85f;			// быстрый подъём, плавный спад
	fR = r > fR ? r : fR * 0.85f;
	if (fL < 0.001f) fL = 0;
	if (fR < 0.001f) fR = 0;
	if (l >= 1.0f || r >= 1.0f) fClipHold = 20;
	else if (fClipHold > 0) fClipHold--;

	// вход: пики записи (только пока идёт запись; индикатор VU у кнопок масштаба показывает воспроизведение)
	float il = 0, ir = 0;
	if (Recorder.IsRecording())
		Recorder.ReadLevels(&il, &ir);
	fInL = il > fInL ? il : fInL * 0.85f;
	fInR = ir > fInR ? ir : fInR * 0.85f;
	if (fInL < 0.001f) fInL = 0;
	if (fInR < 0.001f) fInR = 0;
	if (il >= 1.0f || ir >= 1.0f) fInClipHold = 20;
	else if (fInClipHold > 0) fInClipHold--;

	if (fL != fLastL || fR != fLastR || fClipHold == 19
		|| fInL != fLastInL || fInR != fLastInR || fInClipHold == 19){
		fLastL = fL; fLastR = fR; fLastInL = fInL; fLastInR = fInR;
		Invalidate();
	}
}

void MasterLaneView::MouseDown(BPoint)
{
	fClipHold = 0;				// клик по полосе сбрасывает красные отметки перегрузки
	fInClipHold = 0;
	Invalidate();
}
