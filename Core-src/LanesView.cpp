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
#include "Envelope.h"

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
	// начало каждого такта: тот же цвет, что у ударов, но шире
	if (bar / dt >= 8.0){
		v->SetPenSize(2);
		v->SetHighColor(255, 255, 255, 90);		// цвет как у линий на ударах
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

// 16 пастельных цветов для групп клипов; группа n красится цветом (n-1) % 16, после 16-й - снова с первого
rgb_color GroupColor(int group)
{
	static const rgb_color kPalette[16] = {
		{255, 173, 173, 255}, {255, 214, 165, 255}, {253, 255, 182, 255}, {202, 255, 191, 255},
		{155, 246, 255, 255}, {160, 196, 255, 255}, {189, 178, 255, 255}, {255, 198, 255, 255},
		{230, 190, 150, 255}, {190, 225, 150, 255}, {150, 225, 200, 255}, {170, 210, 230, 255},
		{215, 170, 200, 255}, {240, 170, 130, 255}, {210, 210, 120, 255}, {175, 175, 215, 255}
	};
	return kPalette[((group > 0 ? group : 1) - 1) % 16];
}

// Полупрозрачная пастельная заливка поверх клипа группы (фон под волной подкрашивается, волна остаётся читаемой)
void LanesTintGroup(BView *v, BRect r, int group)
{
	if (group <= 0 || !r.IsValid()) return;
	rgb_color col = GroupColor(group);
	col.alpha = 85;
	v->SetDrawingMode(B_OP_ALPHA);
	v->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
	v->SetHighColor(col);
	v->FillRect(r);
	v->SetDrawingMode(B_OP_COPY);
}

// Выделенный клип: заливка цветом выделения системы (тот же, что у активной дорожки в панели)
void LanesTintSelected(BView *v, BRect r)
{
	if (!r.IsValid()) return;
	rgb_color col = ui_color(B_LIST_SELECTED_BACKGROUND_COLOR);
	col.alpha = 90;
	v->SetDrawingMode(B_OP_ALPHA);
	v->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
	v->SetHighColor(col);
	v->FillRect(r);
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
		if (c->group > 0)
			LanesTintGroup(v, BRect(body.left + x0, body.top, body.left + x1, body.bottom), c->group);
		if (c->selected)
			LanesTintSelected(v, BRect(body.left + x0, body.top, body.left + x1, body.bottom));
		v->SetHighColor(make_color(255, 255, 255));
		if (xs >= 0 && xs < W) v->StrokeLine(BPoint(body.left + (float)xs, body.top), BPoint(body.left + (float)xs, body.bottom));
		if (xe >= 0 && xe < W) v->StrokeLine(BPoint(body.left + (float)xe, body.top), BPoint(body.left + (float)xe, body.bottom));
		if (c->selected){				// выделенный клип: синяя рамка
			v->SetHighColor(70, 130, 255);
			v->SetPenSize(2);
			v->StrokeRect(BRect(body.left + x0 + 1, body.top + 1, body.left + x1 - 1, body.bottom - 1));
			v->SetPenSize(1);
		}
		if (c->locked && x1 - x0 > 14){		// закреплён во времени: маленький замок в углу
			const float lx = body.left + x0 + 4, ly = body.top + 4;
			v->SetHighColor(0, 0, 0);
			v->StrokeLine(BPoint(lx + 2, ly + 4), BPoint(lx + 2, ly + 1));
			v->StrokeLine(BPoint(lx + 6, ly + 4), BPoint(lx + 6, ly + 1));
			v->StrokeLine(BPoint(lx + 2, ly + 1), BPoint(lx + 6, ly + 1));
			v->SetHighColor(255, 200, 0);
			v->FillRect(BRect(lx, ly + 4, lx + 8, ly + 10));
			v->SetHighColor(0, 0, 0);
			v->StrokeRect(BRect(lx, ly + 4, lx + 8, ly + 10));
		}
	}
}

LanesView::LanesView(BRect r)
	: BView(r, "Lanes view", B_FOLLOW_ALL, B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE | B_PULSE_NEEDED),
	  fKeyActive(-1), fKeyCount(-1), fKeyRevision(-1), fKeyScroll(-1), fKeyW(-1), fKeyH(-1),
	  fKeyL(-1), fKeyR(-1), fKeyPointer(-1), fKeySelection(-1), fOldPlayX(-1), fDragSlot(-1), fDragClip(-1), fDragX0(0), fDragPos0(0), fStickTol(0), fDropLane(-1), fScrollBar(NULL), fGroupDelta(0), fStripSlot(-1), fStripVol(true), fStripPoint(-1)
{
	fStripInfo[0] = 0;
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

LanesScrollBar::LanesScrollBar(BRect r, LanesView *lanes)
	: BScrollBar(r, "Lanes scroll bar", NULL, 0, 0, B_VERTICAL), fLanes(lanes), fSyncing(false)
{
	SetResizingMode(B_FOLLOW_RIGHT | B_FOLLOW_TOP_BOTTOM);
}

void LanesScrollBar::ValueChanged(float newValue)
{
	if (fSyncing || !fLanes) return;
	Tracks_SetScroll(newValue, fLanes->Bounds().Height() + 1);
	fLanes->Relayout();
	fLanes->Invalidate();					// панель дорожек и шкала значений подтянутся по пульсу
}

void LanesScrollBar::Sync()
{
	if (!fLanes) return;
	const float area = fLanes->Bounds().Height() + 1;
	const float total = Tracks_TotalHeight(area);
	float max = total - area;
	if (max < 0) max = 0;
	float lo, hi;
	GetRange(&lo, &hi);
	fSyncing = true;
	if (hi != max) SetRange(0, max);
	const float prop = (total > 0 && max > 0) ? area / total : 1.0f;
	if (Proportion() != prop) SetProportion(prop);
	SetSteps(MIN_LANE_HEIGHT / 4.0f, area * 0.9f);
	if (Value() != Tracks_Scroll()) SetValue(Tracks_Scroll());
	fSyncing = false;
}

void LanesView::Pulse()
{
	// SampleView на месте?
	Relayout();
	if (fScrollBar) fScrollBar->Sync();			// колесо мыши и смена числа дорожек двигают и полосу прокрутки

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
	uint32 buttons = 0;
	if (Window() && Window()->CurrentMessage())
		Window()->CurrentMessage()->FindInt32("buttons", (int32 *)&buttons);
	if ((buttons & B_SECONDARY_MOUSE_BUTTON) && Pool.tool_mode == MOVE_TOOL){	// правая кнопка: из «Перемещения» обратно в выделение
		Pool.tool_mode = SELECT_TOOL;
		Pool.UpdateToolBar();
	}

	const float area = Bounds().Height() + 1;
	const int n = Tracks_Count();
	for (int rank=0; rank<n; rank++){					// сначала полосы автоматизации
		float top, bottom;
		Tracks_StripSpan(rank, area, top, bottom);
		if (p.y >= top && p.y <= bottom && p.y < area){
			StripMouseDown(Tracks_SlotAt(rank), BRect(0, top, Bounds().right, bottom), p, buttons);
			return;
		}
	}
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
		if (clip >= 0 && (modifiers() & (B_COMMAND_KEY | B_CONTROL_KEY)) && !(buttons & B_SECONDARY_MOUSE_BUTTON)){
			Tracks_ToggleClipSelected(slot, clip);		// Ctrl+щелчок: добавить клип в выделение или убрать
			Invalidate();
			return;
		}
		if (!(buttons & B_SECONDARY_MOUSE_BUTTON)){
			const MixClip *c0 = Mix.Clip(slot, clip);
			if (!(Pool.tool_mode == MOVE_TOOL && c0 && c0->selected))	// щелчок по выделенному клипу в «Перемещении» - тянуть всех
				Tracks_ClearClipSelection();
			if (Pool.tool_mode == MOVE_TOOL)
				Tracks_SelectGroupOf(slot, clip);		// клип в группе: выделяется вся группа
		}
		if (Pool.tool_mode == MOVE_TOOL){			// тянем клип по ленте, не делая его активным
			const MixClip *mc = Mix.Clip(slot, clip);
			if (mc && Pool.sample_type != NONE){
				fDragSlot = slot;
				fDragClip = clip;
				fDragX0 = p.x;
				fDragPos0 = mc->pos;
				fDropLane = -1;
				fGroup.clear();
				fGroupDelta = 0;
				if (mc->selected && Tracks_SelectedCount() > 1){		// группа: все выделенные клипы микшера
					for (int s = 0; s < MIX_MAX_TRACKS; s++)
						for (int k = 0; k < Mix.ClipCount(s); k++){
							const MixClip *g = Mix.Clip(s, k);
							if (g->selected){ ClipRef r = { s, k, g->pos }; fGroup.push_back(r); }
						}
				}
				SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);
			}
			return;
		}
		Tracks_Activate(slot, t, clip);
		Tracks_SelectActiveGroup();					// остальные клипы группы выделяются вместе с активным
		Relayout();
		Invalidate();
		return;
	}
}

void LanesView::MouseMoved(BPoint p, uint32, const BMessage *)
{
	if (fStripSlot >= 0){
		StripDrag(p);
		return;
	}
	if (Pool.tool_mode == MOVE_TOOL)
		SetViewCursor(Pool.mouseMove);
	else
		SetViewCursor(B_CURSOR_SYSTEM_DEFAULT);

	if (fDragSlot >= 0 && Mix.Clip(fDragSlot, fDragClip) && Pool.r_pointer > Pool.l_pointer && Pool.frequency > 1.0f){
		const double dt = ((double)(Pool.r_pointer - Pool.l_pointer) / Pool.frequency) / Bounds().Width();
		double pos = fDragPos0 + (p.x - fDragX0) * dt;
		pos = Mix.Snap(fDragSlot, pos);				// липкая сетка
		fStickTol = dt * 8.0;						// притяжка к краям соседей: 8 пикселей
		const MixClip *dc0 = Mix.Clip(fDragSlot, fDragClip);
		if (dc0 && !dc0->locked)
			pos = Mix.StickPos(fDragSlot, pos, dc0->Length(), fStickTol, fDragClip, !fGroup.empty());
		if (pos < 0) pos = 0;
		const MixClip *dc = Mix.Clip(fDragSlot, fDragClip);
		if (dc && dc->locked) pos = fDragPos0;		// закреплённый во времени клип не едет по горизонтали
		if (!fGroup.empty()){						// группа: все незакреплённые выделенные клипы едут на одну величину
			double delta = pos - fDragPos0;
			if (dc && dc->locked) delta = 0;
			double minPos = 1e18;
			for (size_t g = 0; g < fGroup.size(); g++){
				const MixClip *gc = Mix.Clip(fGroup[g].slot, fGroup[g].idx);
				if (gc && !gc->locked) minPos = MIN(minPos, fGroup[g].pos0);
			}
			if (minPos < 1e17 && minPos + delta < 0) delta = -minPos;
			for (size_t g = 0; g < fGroup.size(); g++){
				const MixClip *gc = Mix.Clip(fGroup[g].slot, fGroup[g].idx);
				if (gc && !gc->locked) Mix.SetClipPos(fGroup[g].slot, fGroup[g].idx, fGroup[g].pos0 + delta);
			}
			fGroupDelta = delta;
			int lane = -1;							// курсор над другой дорожкой: вся группа уйдёт на столько же дорожек
			const int target = SlotAtY(p.y);
			if (target >= 0 && target != fDragSlot && Tracks_SelectionCanShiftLanes(Tracks_Rank(target) - Tracks_Rank(fDragSlot)))
				lane = target;
			SetDropLane(lane);
			Invalidate();
			return;
		}
		Mix.SetClipPos(fDragSlot, fDragClip, pos);	// живой сдвиг: звук клипа не копируется
		const int target = SlotAtY(p.y);			// курсор над другой дорожкой: при отпускании клип уйдёт туда
		SetDropLane((target >= 0 && target != fDragSlot) ? target : -1);
		Invalidate();
	}
}

void LanesView::MouseUp(BPoint)
{
	if (fStripSlot >= 0){
		fStripSlot = -1;
		fStripPoint = -1;
		fStripInfo[0] = 0;
		Invalidate();
		return;
	}
	if (fDragSlot >= 0){
		const int slot = fDragSlot, clip = fDragClip;
		const int drop = fDropLane;
		fDragSlot = -1;
		fDragClip = -1;
		fDropLane = -1;
		if (!fGroup.empty()){
			for (size_t g = 0; g < fGroup.size(); g++)		// вернуть старые положения: в отмену попадёт исходная раскладка
				if (Mix.Clip(fGroup[g].slot, fGroup[g].idx)) Mix.SetClipPos(fGroup[g].slot, fGroup[g].idx, fGroup[g].pos0);
			const double delta = fGroupDelta;
			fGroup.clear();
			fGroupDelta = 0;
			const int dr = (drop >= 0 && Mix.Track(drop)) ? Tracks_Rank(drop) - Tracks_Rank(slot) : 0;
			if (Tracks_MoveSelection(delta, dr)) Pool.changed = true;
			Relayout();
			Invalidate();
			return;
		}
		const MixClip *mc = Mix.Clip(slot, clip);
		if (mc){
			double np = mc->pos;
			if (np != fDragPos0) Mix.SetClipPos(slot, clip, fDragPos0);	// вернуть старое: так в отмену попадёт исходная раскладка
			if (drop >= 0 && Mix.Track(drop)){
				if (!mc->locked){							// на новой дорожке своя сетка и свои края (закреплённый остаётся в том же моменте)
					np = Mix.Snap(drop, np);
					np = Mix.StickPos(drop, np, mc->Length(), fStickTol);
				}
				Tracks_MoveClipToLane(slot, clip, drop, np);
			}else{
				Tracks_MoveClip(slot, clip, np);	// лента могла удлиниться: пересобрать активный клип
			}
			Pool.changed = true;
		}
		Relayout();
		Invalidate();
	}
}

int LanesView::SlotAtY(float y) const
{
	const float area = Bounds().Height() + 1;
	const int n = Tracks_Count();
	for (int rank=0; rank<n; rank++){
		float top, bottom;
		Tracks_LaneSpan(rank, area, top, bottom);
		if (y >= top && y <= bottom && y < area)
			return Tracks_SlotAt(rank);
	}
	return -1;
}

void LanesView::SetDropLane(int slot)
{
	if (slot == fDropLane) return;
	fDropLane = slot;
	Invalidate();
}

// полупрозрачная подсветка полосы-цели поверх всего (в том числе поверх SampleView активной дорожки)
void LanesView::DrawAfterChildren(BRect)
{
	if (fDropLane < 0 || !Mix.Track(fDropLane)) return;
	const int rank = Tracks_Rank(fDropLane);
	if (rank < 0) return;
	const float area = Bounds().Height() + 1;
	float top, bottom;
	Tracks_LaneSpan(rank, area, top, bottom);
	BRect r(0, top, Bounds().right, bottom);
	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
	SetHighColor(70, 130, 255, 70);
	FillRect(r);
	SetDrawingMode(B_OP_COPY);
	SetHighColor(70, 130, 255);
	SetPenSize(2);
	StrokeRect(r.InsetByCopy(1, 1));
	SetPenSize(1);
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
	for (int rank=0; rank<n; rank++){					// полосы автоматизации (у всех дорожек, и у активной)
		float top, bottom;
		Tracks_StripSpan(rank, area, top, bottom);
		if (bottom < update.top || top > update.bottom)
			continue;
		DrawStrip(Tracks_SlotAt(rank), BRect(0, top, Bounds().right, bottom));
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


// ---------------------------------------------------------------- полоса автоматизации

namespace {

const rgb_color kVolColor = {230, 48, 52, 255};		// красная линия - громкость (у верхнего края)
const rgb_color kPanColor = {52, 112, 232, 255};		// синяя линия - панорама (по середине)
const float kStripToggleW = 14;						// слева: треугольник свернуть/развернуть

struct StripGeom {
	BRect	body;			// область линий
	double	tl, dt;			// время левого края (сек) и секунд на пиксель
	float VolY(float db) const
	{
		if (db < Env::kDbMin) db = Env::kDbMin;
		if (db > Env::kDbMax) db = Env::kDbMax;
		return body.top + (Env::kDbMax - db) / (Env::kDbMax - Env::kDbMin) * body.Height();
	}
	float PanY(float p) const
	{
		if (p < -1) p = -1;
		if (p > 1) p = 1;
		return body.top + (1.0f - p) * 0.5f * body.Height();	// выше - правее
	}
	float XAt(double t) const { return (float)((t - tl) / dt); }
	double TimeAt(float x) const { const double t = tl + x * dt; return t < 0 ? 0 : t; }
	float VolAt(float y) const				// обратное к VolY, с «прилипанием» к 0 дБ и шагом 0,1 дБ
	{
		float db = Env::kDbMax - (y - body.top) / body.Height() * (Env::kDbMax - Env::kDbMin);
		if (db < Env::kDbMin) db = Env::kDbMin;
		if (db > Env::kDbMax) db = Env::kDbMax;
		if (fabsf(db) < 0.6f) db = 0.0f;
		return floorf(db * 10.0f + 0.5f) / 10.0f;
	}
	float PanAt(float y) const				// обратное к PanY, с «прилипанием» к центру
	{
		float p = 1.0f - 2.0f * (y - body.top) / body.Height();
		if (p < -1) p = -1;
		if (p > 1) p = 1;
		if (fabsf(p) < 0.05f) p = 0.0f;
		return floorf(p * 100.0f + 0.5f) / 100.0f;
	}
};

bool MakeGeom(BRect strip, bool open, float W, StripGeom &g)
{
	if (Pool.sample_type == NONE || Pool.r_pointer <= Pool.l_pointer || Pool.frequency < 1.0f)
		return false;
	g.tl = (double)Pool.l_pointer / Pool.frequency;
	const double tr = (double)Pool.r_pointer / Pool.frequency;
	g.dt = (tr - g.tl) / W;
	g.body = strip;
	g.body.InsetBy(0, open ? 6 : 3);
	return g.dt > 0 && g.body.Height() > 2;
}

void DrawCurve(BView *v, const StripGeom &g, const Envelope &e, bool isVol, rgb_color col, bool open, float W)
{
	const bool has = !e.Empty();
	const float neutral = isVol ? g.VolY(0.0f) : g.PanY(0.0f);
	const double tl = g.tl, tr = g.tl + g.dt * W;
	std::vector<BPoint> pts;
	if (!has){
		pts.push_back(BPoint(0, neutral));
		pts.push_back(BPoint(W, neutral));
	}else{
		const float def = 0.0f;
		pts.push_back(BPoint(0, isVol ? g.VolY(e.Value(tl, def)) : g.PanY(e.Value(tl, def))));
		for (int i = 0; i < e.Count(); i++){
			const EnvPoint &p = e.Point(i);
			if (p.t <= tl || p.t >= tr) continue;
			pts.push_back(BPoint(g.XAt(p.t), isVol ? g.VolY(p.v) : g.PanY(p.v)));
		}
		pts.push_back(BPoint(W, isVol ? g.VolY(e.Value(tr, def)) : g.PanY(e.Value(tr, def))));
	}
	v->SetDrawingMode(B_OP_ALPHA);
	v->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
	if (has && isVol && open){					// мягкая заливка под линией громкости
		BPolygon poly;
		poly.AddPoints(&pts[0], (int32)pts.size());
		const BPoint corner[2] = { BPoint(W, g.body.bottom + 3), BPoint(0, g.body.bottom + 3) };
		poly.AddPoints(corner, 2);
		v->SetHighColor(col.red, col.green, col.blue, 40);
		v->FillPolygon(&poly);
	}
	v->SetHighColor(col.red, col.green, col.blue, has ? 255 : 110);
	v->SetPenSize(has && open ? 2.0f : 1.0f);
	for (size_t i = 1; i < pts.size(); i++)
		v->StrokeLine(pts[i - 1], pts[i]);
	v->SetPenSize(1.0f);
	v->SetDrawingMode(B_OP_COPY);
	if (has && open){							// точки
		for (int i = 0; i < e.Count(); i++){
			const EnvPoint &p = e.Point(i);
			if (p.t < tl || p.t > tr) continue;
			const float x = g.XAt(p.t), y = isVol ? g.VolY(p.v) : g.PanY(p.v);
			BRect dot(x - 3, y - 3, x + 3, y + 3);
			v->SetHighColor(255, 255, 255);
			v->FillRect(dot);
			v->SetHighColor(col);
			v->FillRect(dot.InsetByCopy(1, 1));
		}
	}
}

// Фон полосы автоматизации, кромки, треугольник-переключатель (и подпись «Auto» у свёрнутой)
void StripFrame(BView *v, BRect r, bool open)
{
	v->SetDrawingMode(B_OP_COPY);
	v->SetHighColor(make_color(46, 48, 54));
	v->FillRect(r);
	v->SetHighColor(make_color(96, 98, 108));
	v->StrokeLine(BPoint(r.left, r.top), BPoint(r.right, r.top));
	v->SetHighColor(make_color(26, 27, 31));
	v->StrokeLine(BPoint(r.left, r.bottom), BPoint(r.right, r.bottom));
	const float cx = r.left + 7, cy = r.top + 7;
	v->SetHighColor(make_color(200, 200, 205));
	if (open)
		v->FillTriangle(BPoint(cx - 4, cy - 2), BPoint(cx + 4, cy - 2), BPoint(cx, cy + 3));
	else
		v->FillTriangle(BPoint(cx - 2, cy - 4), BPoint(cx - 2, cy + 4), BPoint(cx + 3, cy));
	if (!open){
		BFont f0;
		v->GetFont(&f0);
		BFont small0(f0);
		small0.SetSize(9);
		v->SetFont(&small0);
		v->SetHighColor(make_color(130, 132, 140));
		v->DrawString("Auto", BPoint(r.left + kStripToggleW + 2, r.top + 10));
		v->SetFont(&f0);
	}
}

}	// namespace

void LanesView::DrawStrip(int slot, BRect r)
{
	const MixTrack *tr = Mix.Track(slot);
	if (!tr) return;
	const bool showVol = tr->showVol, showPan = tr->showPan;
	const bool open = tr->autoOpen;
	const float W = r.Width() + 1;

	StripFrame(this, r, open);
	if (!open)
		return;									// свёрнута: кривые не видны, но на звук они влияют

	StripGeom g;
	if (MakeGeom(r, true, W, g)){
		Envelope vol, pan;
		Mix.GetEnvelopes(slot, vol, pan);
		SetDrawingMode(B_OP_ALPHA);				// направляющие: центр панорамы и -30 дБ
		SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
		SetHighColor(255, 255, 255, 22);
		if (showPan) StrokeLine(BPoint(0, g.PanY(0.0f)), BPoint(W, g.PanY(0.0f)));
		if (showVol) StrokeLine(BPoint(0, g.VolY(Env::kDbMin / 2)), BPoint(W, g.VolY(Env::kDbMin / 2)));
		SetDrawingMode(B_OP_COPY);
		if (showPan) DrawCurve(this, g, pan, false, kPanColor, true, W);
		if (showVol) DrawCurve(this, g, vol, true, kVolColor, true, W);
	}

	// подписи кривых и значение при перетаскивании
	BFont f;
	GetFont(&f);
	BFont small(f);
	small.SetSize(9);
	SetFont(&small);
	float lx = r.left + kStripToggleW + 2;
	if (showVol){
		SetHighColor(kVolColor);
		DrawString("Vol", BPoint(lx, r.top + 11));
		lx += 20;
	}
	if (showPan){
		SetHighColor(kPanColor);
		DrawString("Pan", BPoint(lx, r.top + 11));
	}
	if (fStripSlot == slot && fStripInfo[0]){
		SetHighColor(make_color(235, 235, 240));
		DrawString(fStripInfo, BPoint(r.right - 8 - small.StringWidth(fStripInfo), r.top + 11));
	}
	SetFont(&f);
}

// Щелчок по полосе автоматизации. Возвращает true, если событие обработано.
//  - свёрнутая разворачивается; у развёрнутой треугольник слева сворачивает;
//  - в развёрнутой: щелчок по пустому месту ставит точку на ближайшей кривой и сразу тянет её,
//    щелчок по точке тянет её, двойной щелчок или правая кнопка по точке удаляет её.
bool LanesView::StripMouseDown(int slot, BRect strip, BPoint p, uint32 buttons)
{
	const MixTrack *tr = Mix.Track(slot);
	if (!tr)
		return true;
	const bool open = tr->autoOpen;
	if (!open || p.x - strip.left < kStripToggleW){		// свёрнутая разворачивается по щелчку, развёрнутая сворачивается по треугольнику
		Tracks_SetStripOpen(slot, !open);
		Relayout();
		Invalidate();
		return true;
	}
	if (!(tr->showVol || tr->showPan))
		return true;						// ни одна кривая не отмечена: рисовать нечего
	const bool canVol = tr->showVol, canPan = tr->showPan;
	const float W = strip.Width() + 1;
	StripGeom g;
	if (!MakeGeom(strip, true, W, g))
		return true;						// нет ленты - рисовать не на чем

	Envelope vol, pan;
	Mix.GetEnvelopes(slot, vol, pan);
	int32 clicks = 1;
	if (Window() && Window()->CurrentMessage())
		Window()->CurrentMessage()->FindInt32("clicks", &clicks);
	const bool right = (buttons & B_SECONDARY_MOUSE_BUTTON) != 0;

	// ближайшая точка в пределах 8 пикселей
	float best = 9.0f;
	bool bestVol = true;
	int bestIdx = -1;
	for (int k = 0; k < 2; k++){
		if (k == 0 ? !canVol : !canPan) continue;
		const Envelope &e = (k == 0) ? vol : pan;
		for (int i = 0; i < e.Count(); i++){
			const float x = g.XAt(e.Point(i).t);
			const float y = (k == 0) ? g.VolY(e.Point(i).v) : g.PanY(e.Point(i).v);
			const float d = sqrtf((x - p.x) * (x - p.x) + (y - p.y) * (y - p.y));
			if (d < best){ best = d; bestVol = (k == 0); bestIdx = i; }
		}
	}
	if (bestIdx >= 0 && (right || clicks >= 2)){
		(bestVol ? vol : pan).Remove(bestIdx);
		Mix.SetEnvelopes(slot, vol, pan);
		Pool.changed = true;
		Invalidate();
		return true;
	}
	if (bestIdx < 0){
		if (right)
			return true;
		// новая точка на той кривой, которая ближе к щелчку (пустая кривая - это прямая на 0 дБ / по центру)
		const double t = g.TimeAt(p.x);
		const float dv = canVol ? fabsf(p.y - g.VolY(vol.Value(t, 0.0f))) : 1e9f;
		const float dp = canPan ? fabsf(p.y - g.PanY(pan.Value(t, 0.0f))) : 1e9f;
		bestVol = dv <= dp;
		bestIdx = bestVol ? vol.Add(t, g.VolAt(p.y)) : pan.Add(t, g.PanAt(p.y));
		Mix.SetEnvelopes(slot, vol, pan);
		Pool.changed = true;
	}
	fStripSlot = slot;
	fStripVol = bestVol;
	fStripPoint = bestIdx;
	fStripRect = strip;
	SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);
	Invalidate(strip);
	return true;
}

// Перетаскивание точки автоматизации.
void LanesView::StripDrag(BPoint p)
{
	StripGeom g;
	if (!MakeGeom(fStripRect, true, fStripRect.Width() + 1, g) || !Mix.Track(fStripSlot))
		return;
	Envelope vol, pan;
	Mix.GetEnvelopes(fStripSlot, vol, pan);
	Envelope &e = fStripVol ? vol : pan;
	if (fStripPoint < 0 || fStripPoint >= e.Count())
		return;
	const double t = g.TimeAt(p.x);
	const float v = fStripVol ? g.VolAt(p.y) : g.PanAt(p.y);
	fStripPoint = e.Move(fStripPoint, t, v);
	Mix.SetEnvelopes(fStripSlot, vol, pan);
	Pool.changed = true;
	if (fStripVol){
		if (v <= Env::kDbMin) snprintf(fStripInfo, sizeof(fStripInfo), "-inf dB  %.2f s", t);
		else snprintf(fStripInfo, sizeof(fStripInfo), "%+.1f dB  %.2f s", (double)v, t);
	}else{
		const int pc = (int)floorf(fabsf(v) * 100.0f + 0.5f);
		if (pc == 0) snprintf(fStripInfo, sizeof(fStripInfo), "C  %.2f s", t);
		else snprintf(fStripInfo, sizeof(fStripInfo), "%c %d%%  %.2f s", v < 0 ? 'L' : 'R', pc, t);
	}
	Invalidate(fStripRect);
}


// ---------------------------------------------------------------- полоса автоматизации мастера

MasterAutoView::MasterAutoView(BRect r)
	: BView(r, "Master auto", B_FOLLOW_BOTTOM | B_FOLLOW_LEFT_RIGHT, B_WILL_DRAW | B_PULSE_NEEDED | B_FULL_UPDATE_ON_RESIZE),
	  fKeyRevision(-1), fKeyW(-1), fKeyL(-1), fKeyR(-1), fPoint(-1)
{
	SetViewColor(B_TRANSPARENT_COLOR);
	fInfo[0] = 0;
}

void MasterAutoView::Draw(BRect)
{
	const bool open = Mix.MasterAutoOpen();
	BRect r = Bounds();
	const float W = r.Width() + 1;
	StripFrame(this, r, open);
	if (!open)
		return;
	StripGeom g;
	if (MakeGeom(r, true, W, g)){
		Envelope mv;
		Mix.GetMasterEnvelope(mv);
		SetDrawingMode(B_OP_ALPHA);
		SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
		SetHighColor(255, 255, 255, 22);
		StrokeLine(BPoint(0, g.VolY(Env::kDbMin / 2)), BPoint(W, g.VolY(Env::kDbMin / 2)));
		SetDrawingMode(B_OP_COPY);
		DrawCurve(this, g, mv, true, kVolColor, true, W);
	}
	BFont f;
	GetFont(&f);
	BFont small(f);
	small.SetSize(9);
	SetFont(&small);
	SetHighColor(kVolColor);
	DrawString("Vol", BPoint(r.left + kStripToggleW + 2, r.top + 11));
	if (fPoint >= 0 && fInfo[0]){
		SetHighColor(make_color(235, 235, 240));
		DrawString(fInfo, BPoint(r.right - 8 - small.StringWidth(fInfo), r.top + 11));
	}
	SetFont(&f);
}

void MasterAutoView::ApplyLayout()
{
	BWindow *w = Window();
	if (!w) return;
	const float want = Tracks_MasterAutoHeight();
	const float cur = Bounds().Height() + 1;
	if (want == cur) return;
	const float delta = want - cur;
	BView *lanes = w->FindView("Lanes view");
	BView *master = w->FindView("Master lane");
	if (lanes) lanes->ResizeBy(0, -delta);			// стопка дорожек ниже/выше на столько же
	if (master) master->MoveBy(0, -delta);
	MoveBy(0, -delta);								// нижний край остаётся над шкалой времени
	ResizeBy(0, delta);
	BView *panel = w->FindView("Track panel");
	if (panel) panel->Invalidate();
	BView *values = w->FindView("Values view");
	if (values) values->Invalidate();
	Invalidate();
}

void MasterAutoView::Pulse()
{
	ApplyLayout();									// например, после открытия проекта с развёрнутой полосой
	const long long l = Pool.l_pointer, r = Pool.r_pointer;
	if (fKeyRevision != Mix.Revision() || fKeyW != Bounds().Width() || fKeyL != l || fKeyR != r){
		fKeyRevision = Mix.Revision();
		fKeyW = Bounds().Width();
		fKeyL = l;
		fKeyR = r;
		Invalidate();
	}
}

void MasterAutoView::MouseDown(BPoint p)
{
	uint32 buttons = 0;
	int32 clicks = 1;
	if (Window() && Window()->CurrentMessage()){
		Window()->CurrentMessage()->FindInt32("buttons", (int32 *)&buttons);
		Window()->CurrentMessage()->FindInt32("clicks", &clicks);
	}
	const bool open = Mix.MasterAutoOpen();
	if (!open || p.x < kStripToggleW){
		Mix.SetMasterAutoOpen(!open);
		ApplyLayout();
		return;
	}
	const BRect r = Bounds();
	StripGeom g;
	if (!MakeGeom(r, true, r.Width() + 1, g))
		return;
	Envelope mv;
	Mix.GetMasterEnvelope(mv);
	const bool right = (buttons & B_SECONDARY_MOUSE_BUTTON) != 0;
	float best = 9.0f;
	int idx = -1;
	for (int i = 0; i < mv.Count(); i++){
		const float x = g.XAt(mv.Point(i).t), y = g.VolY(mv.Point(i).v);
		const float d = sqrtf((x - p.x) * (x - p.x) + (y - p.y) * (y - p.y));
		if (d < best){ best = d; idx = i; }
	}
	if (idx >= 0 && (right || clicks >= 2)){
		mv.Remove(idx);
		Mix.SetMasterEnvelope(mv);
		Pool.changed = true;
		Invalidate();
		return;
	}
	if (idx < 0){
		if (right) return;
		idx = mv.Add(g.TimeAt(p.x), g.VolAt(p.y));
		Mix.SetMasterEnvelope(mv);
		Pool.changed = true;
	}
	fPoint = idx;
	SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);
	Invalidate();
}

void MasterAutoView::MouseMoved(BPoint p, uint32, const BMessage *)
{
	if (fPoint < 0) return;
	const BRect r = Bounds();
	StripGeom g;
	if (!MakeGeom(r, true, r.Width() + 1, g)) return;
	Envelope mv;
	Mix.GetMasterEnvelope(mv);
	if (fPoint >= mv.Count()) return;
	const double t = g.TimeAt(p.x);
	const float v = g.VolAt(p.y);
	fPoint = mv.Move(fPoint, t, v);
	Mix.SetMasterEnvelope(mv);
	Pool.changed = true;
	if (v <= Env::kDbMin) snprintf(fInfo, sizeof(fInfo), "-inf dB  %.2f s", t);
	else snprintf(fInfo, sizeof(fInfo), "%+.1f dB  %.2f s", (double)v, t);
	Invalidate();
}

void MasterAutoView::MouseUp(BPoint)
{
	if (fPoint >= 0){
		fPoint = -1;
		fInfo[0] = 0;
		Invalidate();
	}
}
