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

// Линии сетки в полосе дорожки (только если у дорожки включён Grid). Линии на ударах чуть ярче.
void LanesDrawGrid(BView *v, int slot, BRect body, double tl, double dt, float W)
{
	const MixTrack *t = Mix.Track(slot);
	if (!t || !t->grid || dt <= 0)
		return;
	double step = Mix.GridStep();
	while (step / dt < 6.0)					// слишком густо: прореживаем вдвое
		step *= 2.0;
	const double beat = 60.0 / Mix.Bpm();
	const double bar = Mix.BarLength();
	v->SetDrawingMode(B_OP_ALPHA);
	v->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
	for (long long k = (long long)floor(tl / step); k * step <= tl + W * dt + step; k++){
		const double t0 = k * step;
		if (t0 < 0) continue;
		const float x = (float)((t0 - tl) / dt);
		if (x < 0 || x > W) continue;
		const double r = t0 / beat;
		const bool onBeat = fabs(r - floor(r + 0.5)) < 1e-6;
		v->SetHighColor(255, 255, 255, onBeat ? 90 : 40);
		v->StrokeLine(BPoint(body.left + x, body.top), BPoint(body.left + x, body.bottom));
	}
	// начало каждого такта: чуть толще и тёплым цветом, чтобы было видно, но не кричало
	if (bar / dt >= 8.0){
		v->SetPenSize(2);
		v->SetHighColor(255, 200, 90, 120);
		for (long long k = (long long)floor(tl / bar); k * bar <= tl + W * dt + bar; k++){
			const double t0 = k * bar;
			if (t0 < 0) continue;
			const float x = (float)((t0 - tl) / dt);
			if (x < 0 || x > W) continue;
			v->StrokeLine(BPoint(body.left + x, body.top), BPoint(body.left + x, body.bottom));
		}
		v->SetPenSize(1);
	}
	v->SetDrawingMode(B_OP_COPY);
}

// Клипы дорожки: у каждого светлая подложка, центральные линии, огибающая волны и белые границы.
void LanesDrawClips(BView *v, int slot, BRect body, double tl, double dt, float W)
{
	const int ncl = Mix.ClipCount(slot);
	const bool stereo = (Mix.LaneChannels(slot) == 2);
	const float halfH = stereo ? body.Height() / 2.0f : body.Height();
	for (int ci = 0; ci < ncl; ci++){
		const MixClip *c = Mix.Clip(slot, ci);
		if (!c || !c->src || c->len <= 0)
			continue;
		const double rate = c->src->rate;
		const double xs = (c->pos - tl) / dt, xe = (c->End() - tl) / dt;
		if (xe < 0 || xs > W)
			continue;
		const float x0 = (float)MAX(xs, 0.0), x1 = (float)MIN(xe, (double)W - 1);
		v->SetHighColor(Prefs.back_color);
		v->FillRect(BRect(body.left + x0, body.top, body.left + x1, body.bottom));
		const int sch = c->src->channels;
		for (int ch = 0; ch < (stereo ? 2 : 1); ch++){
			const float top = body.top + ch * halfH;
			const float mid = top + halfH / 2.0f;
			const float amp = halfH / 2.0f - 1;
			v->SetHighColor(ch == 0 ? Prefs.left_color2 : Prefs.right_color2);
			v->StrokeLine(BPoint(body.left + x0, mid), BPoint(body.left + x1, mid));
			for (int x = (int)x0; x <= (int)x1; x++){
				double t0 = tl + x * dt - c->pos;
				double t1 = t0 + dt;
				if (t1 < 0 || t0 * rate >= (double)c->len)
					continue;
				int64 f0 = (int64)floor(t0 * rate);
				int64 f1 = (int64)floor(t1 * rate);
				if (f0 < 0) f0 = 0;
				if (f1 >= c->len) f1 = c->len - 1;
				if (f1 < f0) f1 = f0;
				float mn = 0, mx = 0;
				// при очень крупном шаге берём каждый k-й кадр, чтобы рисовать быстро
				int64 step = (f1 - f0) / 2048 + 1;
				const float *d = c->src->data + c->start * sch;
				for (int64 f = f0; f <= f1; f += step){
					float s = d[f * sch + ((stereo && sch == 2) ? ch : 0)];
					if (s < mn) mn = s;
					if (s > mx) mx = s;
				}
				if (mx > 1) mx = 1;
				if (mn < -1) mn = -1;
				v->StrokeLine(BPoint(body.left + x, mid - mx * amp), BPoint(body.left + x, mid - mn * amp));
			}
		}
		v->SetHighColor(make_color(255, 255, 255));
		if (xs >= 0 && xs < W) v->StrokeLine(BPoint(body.left + (float)xs, body.top), BPoint(body.left + (float)xs, body.bottom));
		if (xe >= 0 && xe < W) v->StrokeLine(BPoint(body.left + (float)xe, body.top), BPoint(body.left + (float)xe, body.bottom));
	}
}

LanesView::LanesView(BRect r)
	: BView(r, "Lanes view", B_FOLLOW_ALL, B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE | B_PULSE_NEEDED),
	  fKeyActive(-1), fKeyCount(-1), fKeyRevision(-1), fKeyScroll(-1), fKeyW(-1), fKeyH(-1),
	  fKeyL(-1), fKeyR(-1), fKeyPointer(-1), fKeySelection(-1), fOldPlayX(-1), fDragSlot(-1), fDragClip(-1), fDragX0(0), fDragPos0(0)
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
		const int clip = (t >= 0) ? Mix.ClipAt(slot, t) : -1;
		if (Pool.tool_mode == MOVE_TOOL){			// тянем клип по ленте, не делая его активным
			const MixClip *mc = Mix.Clip(slot, clip);
			if (mc && Pool.sample_type != NONE){
				fDragSlot = slot;
				fDragClip = clip;
				fDragX0 = p.x;
				fDragPos0 = mc->pos;
				SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);
			}
			return;
		}
		Tracks_Activate(slot, t, clip);
		Relayout();
		Invalidate();
		return;
	}
}

void LanesView::MouseMoved(BPoint p, uint32, const BMessage *)
{
	if (Pool.tool_mode == MOVE_TOOL)
		SetViewCursor(Pool.mouseMove);
	else
		SetViewCursor(B_CURSOR_SYSTEM_DEFAULT);

	if (fDragSlot >= 0 && Mix.Clip(fDragSlot, fDragClip) && Pool.r_pointer > Pool.l_pointer && Pool.frequency > 1.0f){
		const double dt = ((double)(Pool.r_pointer - Pool.l_pointer) / Pool.frequency) / Bounds().Width();
		double pos = fDragPos0 + (p.x - fDragX0) * dt;
		pos = Mix.Snap(fDragSlot, pos);				// липкая сетка
		if (pos < 0) pos = 0;
		Mix.SetClipPos(fDragSlot, fDragClip, pos);	// живой сдвиг: звук клипа не копируется
		Invalidate();
	}
}

void LanesView::MouseUp(BPoint)
{
	if (fDragSlot >= 0){
		const int slot = fDragSlot, clip = fDragClip;
		fDragSlot = -1;
		fDragClip = -1;
		const MixClip *mc = Mix.Clip(slot, clip);
		if (mc){
			const double np = mc->pos;
			if (np != fDragPos0) Mix.SetClipPos(slot, clip, fDragPos0);	// вернуть старое: так в отмену попадёт исходная раскладка
			Tracks_MoveClip(slot, clip, np);		// лента могла удлиниться: пересобрать активный клип
			Pool.changed = true;
		}
		Relayout();
		Invalidate();
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

	// пустая лента затемнена, клипы светлые (как в Audacity)
	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
	SetHighColor(0, 0, 0, 110);
	FillRect(body);
	SetDrawingMode(B_OP_COPY);
	LanesDrawClips(this, slot, body, tl, dt, W);
	LanesDrawGrid(this, slot, body, tl, dt, W);

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
	const MixClip *first = Mix.Clip(slot, 0);
	float nx = first ? (float)((first->pos - tl) / dt) + 4 : 4;
	if (nx < 4) nx = 4;
	DrawString(t->name, BPoint(nx, lane.top + POINTER_BAR_HEIGHT - 3));
}
