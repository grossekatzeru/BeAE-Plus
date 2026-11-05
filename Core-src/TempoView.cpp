#include <TranslationKit.h>
#include <PopUpMenu.h>
#include <MenuItem.h>
#include <Window.h>
#include <stdio.h>
#include <math.h>

#include "Globals.h"
#include "Mixer.h"
#include "TempoView.h"

#define LCD_DIGITS	3
#define DEFAULT_BPM	120

static const int kDivs[] = { 1, 2, 4, 8, 16, 32 };
static const int kDivCount = 6;

TempoView::TempoView(BRect r)
	: BView(r, "Tempo view", B_FOLLOW_NONE, B_WILL_DRAW | B_PULSE_NEEDED),
	  fPressed(HIT_NONE), fLastBpm(-1), fLastDiv(-1), fLastMetro(false), fLastMeter(-1), fLastVol(-1), fRepeat(0)
{
	SetViewColor(B_TRANSPARENT_COLOR);
	back = BTranslationUtils::GetBitmapFile("./Bitmaps/Box.png");
	display = BTranslationUtils::GetBitmapFile("./Bitmaps/BigDisplay.png");
	digits = BTranslationUtils::GetBitmapFile("./Bitmaps/BigDigits.png");
}

TempoView::~TempoView()
{
	delete back;
	delete display;
	delete digits;
}

BRect TempoView::LcdRect() const
{
	return BRect(2, 8, 2 + LCD_DIGITS * 20 + 13, 8 + (display ? display->Bounds().Height() : 47));
}

BRect TempoView::ButtonRect(int which) const
{
	BRect l = LcdRect();
	const float x = l.right + 4;
	const float h = (l.Height() - 4) / 3.0f;
	const int k = which - HIT_UP;							// 0 вверх, 1 кружок, 2 вниз
	const float y = l.top + k * (h + 2);
	return BRect(x, y, x + 17, y + h);
}

BRect TempoView::DepthRect() const
{
	BRect l = LcdRect();
	const float x = ButtonRect(HIT_UP).right + 6;
	return BRect(x, l.top, x + 44, l.top + 16);
}

BRect TempoView::MeterRect() const
{
	BRect d = DepthRect();
	return BRect(d.left, d.top + 27, d.right, d.top + 43);
}

BRect TempoView::MetroRect() const
{
	BRect d = DepthRect();
	return BRect(d.right + 8, d.top, d.right + 8 + 44, d.bottom);
}

BRect TempoView::VolumeRect() const
{
	BRect m = MetroRect();
	return BRect(m.left, m.top + 27, m.right, m.top + 43);
}

int TempoView::HitTest(BPoint p) const
{
	for (int w = HIT_UP; w <= HIT_DOWN; w++)
		if (ButtonRect(w).Contains(p)) return w;
	if (DepthRect().Contains(p)) return HIT_DEPTH;
	if (MeterRect().Contains(p)) return HIT_METER;
	if (MetroRect().Contains(p)) return HIT_METRO;
	if (VolumeRect().Contains(p)) return HIT_VOLUME;
	return HIT_NONE;
}

void TempoView::DrawSmallButton(int which)
{
	BRect r = ButtonRect(which);
	const bool down = (fPressed == which);
	rgb_color face = down ? make_color(90, 96, 104) : make_color(150, 156, 164);
	SetHighColor(face);
	FillRect(r);
	SetHighColor(make_color(20, 20, 20));
	StrokeRect(r);
	SetHighColor(make_color(255, 255, 255));
	const float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
	if (which == HIT_UP)
		FillTriangle(BPoint(cx - 4, cy + 2), BPoint(cx + 4, cy + 2), BPoint(cx, cy - 3));
	else if (which == HIT_DOWN)
		FillTriangle(BPoint(cx - 4, cy - 2), BPoint(cx + 4, cy - 2), BPoint(cx, cy + 3));
	else
		FillEllipse(BPoint(cx, cy), 3, 3);
}

void TempoView::Draw(BRect)
{
	// фон - тот же градиент Box.png, что и у большого таймера
	if (back){
		const int bw = back->Bounds().IntegerWidth();
		for (int x = 0; x <= Bounds().IntegerWidth(); x += bw) DrawBitmap(back, BPoint(x, 0));
	}
	BRect l = LcdRect();
	if (display){
		const BRect db = display->Bounds();
		const float w = l.Width();
		DrawBitmap(display, BRect(0, 0, w - 4, db.Height()), BRect(l.left, l.top, l.left + w - 4, l.bottom));
		DrawBitmap(display, BRect(db.right - 3, 0, db.right, db.Height()), BRect(l.right - 3, l.top, l.right, l.bottom));
	}
	if (digits){
		int bpm = (int)floor(Mix.Bpm() + 0.5);
		const int d[3] = { bpm / 100 % 10, bpm / 10 % 10, bpm % 10 };
		bool first = true;
		for (int i = 0; i < 3; i++){
			int g = d[i] + 1;
			if (first && d[i] == 0 && i < 2) g = 0;			// ведущие нули гасим
			if (d[i] != 0) first = false;
			BRect num(20 * g, 0, 20 * g + 19, 29);
			BRect dest(l.left + 7 + i * 20, l.top + 10, l.left + 26 + i * 20, l.top + 39);
			DrawBitmap(digits, num, dest);
		}
	}
	for (int w = HIT_UP; w <= HIT_DOWN; w++) DrawSmallButton(w);

	// два «окошка» одного вида: глубина сетки (длительность ноты) и музыкальный размер
	char s[16];
	for (int w = 0; w < 2; w++){
		BRect dr = (w == 0) ? DepthRect() : MeterRect();
		SetHighColor(make_color(20, 30, 20));
		FillRect(dr);
		SetHighColor(make_color(0, 0, 0));
		StrokeRect(dr);
		if (w == 0){
			if (Mix.GridDiv() == 1) snprintf(s, sizeof(s), "1");
			else snprintf(s, sizeof(s), "1/%d", Mix.GridDiv());
		}else{
			snprintf(s, sizeof(s), "%s", Metronome::MeterName(Mix.Meter()));
		}
		SetHighColor(make_color(120, 255, 120));
		SetLowColor(make_color(20, 30, 20));
		DrawString(s, BPoint(dr.left + (dr.Width() - StringWidth(s)) / 2, dr.bottom - 4));
	}
	// кнопка метронома: силуэт с маятником, включённый светится зелёным
	{
		BRect m = MetroRect();
		const bool on = Mix.MetronomeOn();
		SetHighColor(on ? make_color(60, 200, 90) : make_color(150, 156, 164));
		FillRect(m);
		SetHighColor(make_color(20, 20, 20));
		StrokeRect(m);
		const float cx = (m.left + m.right) / 2;
		SetHighColor(on ? make_color(0, 40, 10) : make_color(30, 30, 30));
		FillTriangle(BPoint(cx - 6, m.bottom - 2), BPoint(cx + 6, m.bottom - 2), BPoint(cx, m.top + 2));
		SetHighColor(on ? make_color(220, 255, 230) : make_color(240, 240, 240));
		StrokeLine(BPoint(cx, m.bottom - 4), BPoint(cx + 4, m.top + 3));
	}
	// громкость метронома: жёлоб с бегунком
	{
		BRect vr = VolumeRect();
		SetHighColor(make_color(20, 30, 20));
		FillRect(vr);
		SetHighColor(make_color(0, 0, 0));
		StrokeRect(vr);
		const float inner = vr.Width() - 8;
		const float x = vr.left + 4 + Mix.MetroVolume() * inner;
		SetHighColor(make_color(60, 120, 70));
		FillRect(BRect(vr.left + 4, vr.top + 6, x, vr.bottom - 6));
		SetHighColor(make_color(120, 255, 120));
		FillRect(BRect(x - 2, vr.top + 3, x + 2, vr.bottom - 3));
	}
	SetHighColor(make_color(230, 230, 230));
	SetLowColor(make_color(60, 60, 60));
	SetFontSize(9);
	const char *caps[4] = { "GRID", "METER", "CLICK", "VOL" };
	BRect cr[4] = { DepthRect(), MeterRect(), MetroRect(), VolumeRect() };
	for (int i = 0; i < 4; i++)
		DrawString(caps[i], BPoint(cr[i].left + (cr[i].Width() - StringWidth(caps[i])) / 2, cr[i].bottom + 10));
	SetFontSize(be_plain_font->Size());
}

void TempoView::Step(int delta)
{
	double bpm = floor(Mix.Bpm() + 0.5) + delta;
	if (bpm < 20) bpm = 20;
	if (bpm > 300) bpm = 300;
	if (bpm == Mix.Bpm()) return;
	Mix.SetBpm(bpm);
	Pool.changed = true;
	Invalidate();
}

void TempoView::MouseDown(BPoint p)
{
	const int hit = HitTest(p);
	if (hit == HIT_NONE) return;
	if (hit == HIT_METRO){
		Mix.SetMetronome(!Mix.MetronomeOn());
		Invalidate();
		return;
	}
	if (hit == HIT_VOLUME){
		fPressed = HIT_VOLUME;
		SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);
		SetVolumeFromMouse(p);
		return;
	}
	if (hit == HIT_METER){
		BPopUpMenu *menu = new BPopUpMenu("meter", false, false);
		for (int i = 0; i < Metronome::METER_COUNT; i++){
			BMenuItem *it = new BMenuItem(Metronome::MeterName(i), NULL);
			it->SetMarked(i == Mix.Meter());
			menu->AddItem(it);
		}
		BRect mr = MeterRect();
		BMenuItem *sel = menu->Go(ConvertToScreen(BPoint(mr.left, mr.bottom + 1)), false, true);
		if (sel){
			Mix.SetMeter(menu->IndexOf(sel));
			Pool.changed = true;
		}
		delete menu;
		Invalidate();
		return;
	}
	if (hit == HIT_DEPTH){
		BPopUpMenu *menu = new BPopUpMenu("depth", false, false);
		for (int i = 0; i < kDivCount; i++){
			char s[16];
			if (kDivs[i] == 1) snprintf(s, sizeof(s), "1");
			else snprintf(s, sizeof(s), "1/%d", kDivs[i]);
			BMenuItem *it = new BMenuItem(s, NULL);
			it->SetMarked(kDivs[i] == Mix.GridDiv());
			menu->AddItem(it);
		}
		BRect dr = DepthRect();
		BMenuItem *sel = menu->Go(ConvertToScreen(BPoint(dr.left, dr.bottom + 1)), false, true);
		if (sel){
			const int idx = menu->IndexOf(sel);
			if (idx >= 0 && idx < kDivCount){
				Mix.SetGridDiv(kDivs[idx]);
				Pool.changed = true;
			}
		}
		delete menu;
		Invalidate();
		return;
	}
	fPressed = hit;
	fRepeat = 0;
	SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);
	if (hit == HIT_UP) Step(+1);
	else if (hit == HIT_DOWN) Step(-1);
	else if (hit == HIT_RESET){
		if (Mix.Bpm() != DEFAULT_BPM){ Mix.SetBpm(DEFAULT_BPM); Pool.changed = true; }
	}
	Invalidate();
}

void TempoView::SetVolumeFromMouse(BPoint p)
{
	BRect vr = VolumeRect();
	float v = (p.x - vr.left - 4) / (vr.Width() - 8);
	Mix.SetMetroVolume(v);
	Pool.changed = true;
	Invalidate();
}

void TempoView::MouseMoved(BPoint p, uint32, const BMessage *)
{
	if (fPressed == HIT_VOLUME) SetVolumeFromMouse(p);
}

void TempoView::MouseUp(BPoint)
{
	fPressed = HIT_NONE;
	Invalidate();
}

// удержание стрелки - автоповтор; заодно подхватываем изменения темпа из проекта
void TempoView::Pulse()
{
	if (fPressed == HIT_UP || fPressed == HIT_DOWN){
		if (++fRepeat > 4) Step(fPressed == HIT_UP ? +1 : -1);
	}
	if (fLastBpm != Mix.Bpm() || fLastDiv != Mix.GridDiv() || fLastMetro != Mix.MetronomeOn()
		|| fLastMeter != Mix.Meter() || fLastVol != Mix.MetroVolume()){
		fLastMeter = Mix.Meter();
		fLastVol = Mix.MetroVolume();
		fLastMetro = Mix.MetronomeOn();
		fLastBpm = Mix.Bpm();
		fLastDiv = Mix.GridDiv();
		Invalidate();
	}
}
