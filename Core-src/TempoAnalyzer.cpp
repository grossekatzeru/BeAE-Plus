#include <TranslationKit.h>
#include <Window.h>
#include <math.h>
#include <vector>

#include "Globals.h"
#include "CommonPool.h"
#include "Project.h"
#include "TempoDetect.h"
#include "TempoAnalyzer.h"
#include "main.h"

#define LABEL_W		70
#define LCD_W		(3 * 20 + 14)

TempoAnalyzerView::TempoAnalyzerView(BRect r)
	: BView(r, "Tempo analyzer", B_FOLLOW_ALL, B_WILL_DRAW)
{
	SetViewColor(B_TRANSPARENT_COLOR);
	back = BTranslationUtils::GetBitmapFile("./Bitmaps/Box.png");
	display = BTranslationUtils::GetBitmapFile("./Bitmaps/BigDisplay.png");
	digits = BTranslationUtils::GetBitmapFile("./Bitmaps/BigDigits.png");
	fBpm = 0;
}

TempoAnalyzerView::~TempoAnalyzerView()
{
	delete back;
	delete display;
	delete digits;
}

void TempoAnalyzerView::Analyze()
{
	fBpm = 0;
	std::vector<float> mono;
	float rate = 0;
	double bpm = 0;
	if (Project_AnalysisMix(mono, rate) && DetectTempo(&mono[0], mono.size(), rate, bpm))
		fBpm = floor(bpm + 0.5);
	Invalidate();
}

void TempoAnalyzerView::MouseDown(BPoint)
{
	Analyze();
}

void TempoAnalyzerView::Draw(BRect)
{
	if (back){
		const int bw = back->Bounds().IntegerWidth();
		for (int x = 0; x <= Bounds().IntegerWidth(); x += bw)
			DrawBitmap(back, BPoint(x, 0));
	}
	// надпись «Темп» слева от ЖК-окошка
	SetHighColor(make_color(237, 237, 237));
	SetLowColor(make_color(60, 62, 66));
	SetFont(be_bold_font);
	SetFontSize(16);
	DrawString(Language.get("TEMPO_LABEL"), BPoint(10, 38));

	const float lx = LABEL_W + 4, ly = 4;
	const float h = display ? display->Bounds().Height() : 46;
	if (display){
		const BRect db = display->Bounds();
		DrawBitmap(display, BRect(0, 0, LCD_W - 5, db.Height()), BRect(lx, ly, lx + LCD_W - 5, ly + h));
		DrawBitmap(display, BRect(db.right - 3, 0, db.right, db.Height()),
			BRect(lx + LCD_W - 4, ly, lx + LCD_W - 1, ly + h));
	}
	const int bpm = (int)fBpm;
	const int d[3] = { bpm / 100 % 10, bpm / 10 % 10, bpm % 10 };
	bool first = true;
	for (int i = 0; i < 3; i++){
		BRect dest(lx + 7 + i * 20, ly + 10, lx + 26 + i * 20, ly + 39);
		if (bpm <= 0){
			// «---»: прочерк того же цвета и высоты по центру знакоместа, как цифры
			SetHighColor(make_color(237, 237, 237));
			FillRect(BRect(dest.left + 3, dest.top + 13, dest.right - 3, dest.top + 16));
			continue;
		}
		int g = d[i] + 1;
		if (first && d[i] == 0 && i < 2) g = 0;
		if (d[i] != 0) first = false;
		if (digits)
			DrawBitmap(digits, BRect(20 * g, 0, 20 * g + 19, 29), dest);
	}
	SetFontSize(be_plain_font->Size());
	SetFont(be_plain_font);
}

TempoAnalyzerWindow::TempoAnalyzerWindow(BPoint c)
	: BWindow(BRect(c.x - 100, c.y - 30, c.x - 100 + LABEL_W + LCD_W + 14, c.y - 30 + 54),
		Language.get("TEMPO_ANALYZER"), B_FLOATING_WINDOW_LOOK, B_FLOATING_APP_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_NOT_RESIZABLE)
{
	TempoAnalyzerView *v = new TempoAnalyzerView(Bounds());
	AddChild(v);
	v->Analyze();
	Show();
}
