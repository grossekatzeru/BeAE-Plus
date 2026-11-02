#include <StorageKit.h>
#include <InterfaceKit.h>
#include <Window.h>
#include <Message.h>
#include <Font.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

#include "CommonPool.h"
#include "Globals.h"
#include "Mixer.h"
#include "Tracks.h"
#include "Preferences.h"
#include "LanesView.h"

#ifndef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#endif
#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif

extern cookie_record play_cookie;

LanesView::LanesView(BRect r)
	: BView(r, "Lanes view", B_FOLLOW_ALL, B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE | B_PULSE_NEEDED),
	  fKeyActive(-1), fKeyCount(-1), fKeyRevision(-1), fKeyScroll(-1), fKeyW(-1), fKeyH(-1),
	  fKeyL(-1), fKeyR(-1), fKeyPointer(-1), fKeySelection(-1), fOldPlayX(-1)
{
	SetViewColor(B_TRANSPARENT_COLOR);
}

void LanesView::AttachedToWindow()
{
	SetFont(be_plain_font);
	SetFontSize(9);
	Relayout();
}

void LanesView::FrameResized(float, float)
{
	Relayout();
	Invalidate();
}

// SampleView ставится в полосу активной дорожки
void LanesView::Relayout()
{
	BView *sv = FindView("Sample view");
	if (sv == NULL)
		return;
	const float area = Bounds().Height() + 1;
	const int rank = Tracks_Rank(Mix.Active());
	if (rank < 0)
		return;
	float top, bottom;
	Tracks_LaneSpan(rank, area, top, bottom);
	const float w = Bounds().Width();
	const float h = bottom - top + 1;
	BRect cur = sv->Frame();
	if (cur.left != 0 || cur.top != top || cur.Width() != w || cur.Height() + 1 != h + 0.0f){
		sv->MoveTo(0, top);
		sv->ResizeTo(w, h - 1);
	}
}

// положение вертикальной линии воспроизведения на экране (или -1)
float LanesView::PlayX() const
{
	if (!Pool.IsPlaying() || Pool.sample_type == NONE || Pool.r_pointer <= Pool.l_pointer)
		return -1;
	return (Pool.last_pointer - Pool.l_pointer) * Bounds().Width() / (Pool.r_pointer - Pool.l_pointer);
}

void LanesView::DrawPlayLine()
{
	const float area = Bounds().Height() + 1;
	const int n = Tracks_Count();
	SetDrawingMode(B_OP_INVERT);
	for (int rank=0; rank<n; rank++){
		int slot = Tracks_SlotAt(rank);
		if (slot == Mix.Active())
			continue;
		float top, bottom;
		Tracks_LaneSpan(rank, area, top, bottom);
		if (bottom < 0 || top > area) continue;
		if (fOldPlayX >= 0)
			StrokeLine(BPoint(fOldPlayX, top + POINTER_BAR_HEIGHT), BPoint(fOldPlayX, bottom));
	}
	SetDrawingMode(B_OP_COPY);
}

void LanesView::Pulse()
{
	// SampleView на месте?
	Relayout();

	// перерисовка только когда что-то поменялось
	const long long l = Pool.l_pointer, r = Pool.r_pointer, p = Pool.pointer;
	if (fKeyActive != Mix.Active() || fKeyCount != Tracks_Count() || fKeyRevision != Mix.Revision()
		|| fKeyScroll != Tracks_Scroll() || fKeyW != Bounds().Width() || fKeyH != Bounds().Height()
		|| fKeyL != l || fKeyR != r
		|| (!Pool.IsPlaying() && (fKeyPointer != p || fKeySelection != Pool.selection))){
		fKeyActive = Mix.Active();
		fKeyCount = Tracks_Count();
		fKeyRevision = Mix.Revision();
		fKeyScroll = Tracks_Scroll();
		fKeyW = Bounds().Width();
		fKeyH = Bounds().Height();
		fKeyL = l;
		fKeyR = r;
		fKeyPointer = p;
		fKeySelection = Pool.selection;
		fOldPlayX = -1;
		Invalidate();
		return;
	}

	// линия воспроизведения в неактивных полосах (так же, как в SampleView - инвертированная линия)
	if (Pool.IsPlaying()){
		float x = PlayX();
		if (x != fOldPlayX){
			const float area = Bounds().Height() + 1;
			const int n = Tracks_Count();
			SetDrawingMode(B_OP_INVERT);
			for (int rank=0; rank<n; rank++){
				if (Tracks_SlotAt(rank) == Mix.Active())
					continue;
				float top, bottom;
				Tracks_LaneSpan(rank, area, top, bottom);
				if (fOldPlayX >= 0)
					StrokeLine(BPoint(fOldPlayX, top + POINTER_BAR_HEIGHT), BPoint(fOldPlayX, bottom));
				if (x >= 0)
					StrokeLine(BPoint(x, top + POINTER_BAR_HEIGHT), BPoint(x, bottom));
			}
			SetDrawingMode(B_OP_COPY);
			fOldPlayX = x;
		}
	}else if (fOldPlayX >= 0){
		Invalidate();				// убрать след линии воспроизведения
		fOldPlayX = -1;
	}
}

void LanesView::MessageReceived(BMessage *msg)
{
	if (msg->what == B_MOUSE_WHEEL_CHANGED){
		float dy = 0;
		if (msg->FindFloat("be:wheel_delta_y", &dy) == B_OK){
			const float area = Bounds().Height() + 1;
			Tracks_SetScroll(Tracks_Scroll() + dy * MIN_LANE_HEIGHT / 2, area);
			Relayout();
			Invalidate();
		}
		return;
	}
	BView::MessageReceived(msg);
}

void LanesView::MouseDown(BPoint p)
{
	const float area = Bounds().Height() + 1;
	const int n = Tracks_Count();
	for (int rank=0; rank<n; rank++){
		float top, bottom;
		Tracks_LaneSpan(rank, area, top, bottom);
		if (p.y < top || p.y > bottom)
			continue;
		int slot = Tracks_SlotAt(rank);
		if (slot < 0 || slot == Mix.Active())
			return;
		// время под курсором мыши
		double t = -1.0;
		if (Pool.sample_type != NONE && Pool.r_pointer > Pool.l_pointer && Pool.frequency > 1.0f){
			double frame = Pool.l_pointer + p.x * (Pool.r_pointer - Pool.l_pointer) / Bounds().Width();
			t = frame / Pool.frequency;
			if (t < 0) t = 0;
		}
		Tracks_Activate(slot, t);
		Relayout();
		Invalidate();
		return;
	}
}

void LanesView::Draw(BRect update)
{
	rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
	SetLowColor(base);
	SetHighColor(base);
	FillRect(update);

	const float area = Bounds().Height() + 1;
	const int n = Tracks_Count();
	for (int rank=0; rank<n; rank++){
		int slot = Tracks_SlotAt(rank);
		float top, bottom;
		Tracks_LaneSpan(rank, area, top, bottom);
		if (bottom < update.top || top > update.bottom)
			continue;
		if (slot == Mix.Active())
			continue;									// здесь лежит SampleView
		DrawLane(slot, BRect(0, top, Bounds().right, bottom));
	}

	if (Pool.IsPlaying() && fOldPlayX >= 0)
		DrawPlayLine();
}

void LanesView::DrawLane(int slot, BRect lane)
{
	MixTrack *t = Mix.Track(slot);
	if (t == NULL)
		return;
	const float W = lane.Width() + 1;

	// верхняя полоска: такая же, как у активной дорожки, но с названием
	for (int y=0; y<POINTER_BAR_HEIGHT; y++){
		rgb_color c = (y == 0) ? make_color(255,255,255)
				: (y == POINTER_BAR_HEIGHT-1) ? make_color(128,128,128) : make_color(192,192,192);
		SetHighColor(c);
		StrokeLine(BPoint(lane.left, lane.top + y), BPoint(lane.right, lane.top + y));
	}
	BRect body(lane.left, lane.top + POINTER_BAR_HEIGHT, lane.right, lane.bottom);
	if (body.Height() < 2)
		return;

	// фон
	SetHighColor(Prefs.back_color);
	FillRect(body);

	if (Pool.sample_type == NONE || Pool.r_pointer <= Pool.l_pointer || Pool.frequency < 1.0f){
		SetHighColor(make_color(0,0,0));
		DrawString(t->name, BPoint(lane.left + 4, lane.top + POINTER_BAR_HEIGHT - 3));
		return;
	}

	// окно по времени - то же, что у активной дорожки (Pool лежит на ленте целиком)
	const double tl = (double)Pool.l_pointer / Pool.frequency;
	const double tr = (double)Pool.r_pointer / Pool.frequency;
	const double dt = (tr - tl) / W;				// секунд на пиксель

	// центральные линии
	const bool stereo = (t->channels == 2);
	const float halfH = stereo ? body.Height() / 2.0f : body.Height();
	SetHighColor(Prefs.left_color2);
	// волна
	if (t->data != NULL && t->frames > 0 && t->rate > 0){
		for (int ch=0; ch<(stereo ? 2 : 1); ch++){
			const float top = body.top + ch * halfH;
			const float mid = top + halfH / 2.0f;
			const float amp = halfH / 2.0f - 1;
			SetHighColor(ch == 0 ? Prefs.left_color2 : Prefs.right_color2);
			StrokeLine(BPoint(body.left, mid), BPoint(body.right, mid));
			for (int x=0; x<(int)W; x++){
				double t0 = tl + x * dt - t->offset;
				double t1 = t0 + dt;
				if (t1 < 0 || t0 >= (double)t->frames / t->rate)
					continue;
				int64 f0 = (int64)floor(t0 * t->rate);
				int64 f1 = (int64)floor(t1 * t->rate);
				if (f0 < 0) f0 = 0;
				if (f1 >= t->frames) f1 = t->frames - 1;
				if (f1 < f0) f1 = f0;
				float mn = 0, mx = 0;
				// при очень крупном шаге берём каждый k-й кадр, чтобы рисовать быстро
				int64 step = (f1 - f0) / 2048 + 1;
				const int cc = t->channels;
				for (int64 f=f0; f<=f1; f+=step){
					float v = t->data[f * cc + (stereo ? ch : 0)];
					if (v < mn) mn = v;
					if (v > mx) mx = v;
				}
				if (mx > 1) mx = 1;
				if (mn < -1) mn = -1;
				StrokeLine(BPoint(x, mid - mx * amp), BPoint(x, mid - mn * amp));
			}
		}
	}

	// как в Audacity: до начала звука и после его конца - затемнённая «пустая» лента
	{
		const double clipStart = t->offset;
		const double clipEnd = (t->data && t->frames > 0 && t->rate > 0) ? t->offset + (double)t->frames / t->rate : t->offset;
		float xs = (float)((clipStart - tl) / dt);
		float xe = (float)((clipEnd - tl) / dt);
		SetDrawingMode(B_OP_ALPHA);
		SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
		SetHighColor(0, 0, 0, 110);
		if (t->data == NULL || t->frames <= 0){
			FillRect(body);
		}else{
			if (xs > 0)
				FillRect(BRect(body.left, body.top, MIN(xs, body.right), body.bottom));
			if (xe < W)
				FillRect(BRect(MAX(xe, body.left), body.top, body.right, body.bottom));
		}
		SetDrawingMode(B_OP_COPY);
		// границы звука
		if (t->data && t->frames > 0){
			SetHighColor(make_color(255,255,255));
			if (xs >= 0 && xs < W) StrokeLine(BPoint(xs, body.top), BPoint(xs, body.bottom));
			if (xe >= 0 && xe < W) StrokeLine(BPoint(xe, body.top), BPoint(xe, body.bottom));
		}
	}

	// курсор (положение на ленте)
	if (Pool.selection == NONE && !Pool.IsPlaying()){
		float x = (float)((Pool.pointer - Pool.l_pointer) * (double)W / (Pool.r_pointer - Pool.l_pointer));
		if (x >= 0 && x < W){
			SetHighColor(Prefs.pointer_color);
			for (float y = body.top; y < body.bottom; y += 4)
				StrokeLine(BPoint(x, y), BPoint(x, y));
		}
	}

	// имя дорожки на верхней полоске, у начала звука (если оно видно) или у левого края
	SetHighColor(make_color(0,0,0));
	float nx = (t->data && t->frames > 0) ? (float)((t->offset - tl) / dt) + 4 : 4;
	if (nx < 4) nx = 4;
	DrawString(t->name, BPoint(nx, lane.top + POINTER_BAR_HEIGHT - 3));
}
