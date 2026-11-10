#include <Application.h>
#include <Button.h>
#include <LayoutBuilder.h>
#include <Menu.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Size.h>
#include <Spinner.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

#include "Globals.h"
#include "CommonPool.h"
#include "AudioRecorder.h"
#include "TunerWindow.h"
#include "main.h"

static const uint32 MSG_TUNER_START	= 'tnSt';
static const uint32 MSG_TUNER_PRESET	= 'tnPr';
static const uint32 MSG_TUNER_NOTATION	= 'tnNo';
static const uint32 MSG_TUNER_A4		= 'tnA4';
static const uint32 MSG_TUNER_STATE	= 'tnSS';

// запоминаем выбор до конца работы программы
static int sPreset = 1;						// гитара 6 струн
static int sNotation = Tuner::NOTATION_LATIN;
static float sA4 = 440.0f;

static TunerWindow *sInstance = NULL;

static rgb_color Green(int v) { return make_color(0, v, (uint8)(v * 0.35f)); }

/*******************************************************
*   TunerView
*******************************************************/
TunerView::TunerView()
	: BView("Tuner view", B_WILL_DRAW | B_PULSE_NEEDED | B_FULL_UPDATE_ON_RESIZE),
	fCoreRate(0), fRunning(false), fPreset(sPreset), fA4(sA4), fNotation(sNotation),
	fShown(0), fLevelShown(-60)
{
	SetViewColor(B_TRANSPARENT_COLOR);
	SetExplicitMinSize(BSize(440, 260));
	memset(&fReading, 0, sizeof(fReading));
	fReading.stringIndex = -1;
	fStatus = "TUNER_PRESS_START";
	fCore.Init(44100);
	fCore.SetPreset(fPreset);
	fCore.SetA4(fA4);
}

void TunerView::AttachedToWindow()
{
	BView::AttachedToWindow();
	Window()->SetPulseRate(33000);
}

void TunerView::SetPreset(int index)
{
	fPreset = sPreset = index;
	fCore.SetPreset(index);
	Invalidate();
}

void TunerView::SetA4(float hz)
{
	fA4 = sA4 = hz;
	fCore.SetA4(hz);
}

void TunerView::SetNotation(int n)
{
	fNotation = sNotation = n;
	Invalidate();
}

void TunerView::SetStatus(const char *key)
{
	fStatus = key ? key : "";
	Invalidate();
}

void TunerView::NotifyState()
{
	if (Window())
		Window()->PostMessage(MSG_TUNER_STATE);
}

bool TunerView::Start()
{
	if (fRunning)
		return true;
	BString err;
	if (Recorder.StartListening(&err) != B_OK) {
		SetStatus(err.Length() ? err.String() : "REC_ERR_START");
		return false;
	}
	fCoreRate = 0;			// частоту узнаем из первого буфера
	fCore.Reset();
	fRunning = true;
	memset(&fReading, 0, sizeof(fReading));
	fReading.stringIndex = -1;
	SetStatus("TUNER_PLAY");
	return true;
}

void TunerView::Stop()
{
	if (Recorder.IsListening())
		Recorder.Stop();
	fRunning = false;
	memset(&fReading, 0, sizeof(fReading));
	fReading.stringIndex = -1;
	SetStatus("TUNER_PRESS_START");
}

void TunerView::Pulse()
{
	if (!fRunning)
		return;
	if (!Recorder.IsListening()) {				// запись забрала вход или вход пропал
		fRunning = false;
		SetStatus(Recorder.IsRecording() ? "TUNER_BUSY" : "TUNER_PRESS_START");
		memset(&fReading, 0, sizeof(fReading));
		fReading.stringIndex = -1;
		NotifyState();
		return;
	}
	float tmp[4096];
	size_t n;
	while ((n = Recorder.ReadListened(tmp, 4096)) > 0) {
		const float rate = Recorder.ListenRate();
		if (rate > 8000.0f && fabsf(rate - fCoreRate) > 1.0f) {
			fCore.Init(rate);
			fCore.SetPreset(fPreset);
			fCore.SetA4(fA4);
			fCoreRate = rate;
		}
		if (fCoreRate > 0)
			fCore.Push(tmp, n);
		if (n < 4096)
			break;
	}
	if (fCoreRate > 0)
		fReading = fCore.Update();
	// маркер движется плавно
	const float target = fReading.voiced ? fmaxf(-50.0f, fminf(50.0f, fReading.cents)) : 0.0f;
	fShown += (target - fShown) * 0.35f;
	const float lvl = fReading.levelDb;
	fLevelShown += ((lvl < -60 ? -60 : lvl) - fLevelShown) * 0.4f;
	Invalidate();
}

void TunerView::Draw(BRect)
{
	const BRect b = Bounds();
	const float w = b.Width(), h = b.Height();
	SetHighColor(0, 0, 0);
	FillRect(b);

	const float cx = w / 2;
	const Tuner::Preset &pr = Tuner::PresetAt(fPreset);
	const bool inTune = fReading.voiced && fabsf(fReading.cents) <= 3.0f;
	const bool nearTune = fReading.voiced && fabsf(fReading.cents) <= 15.0f;
	const rgb_color bright = make_color(60, 255, 110);
	const rgb_color amber = make_color(255, 176, 32);

	// --- нижний ряд: струны пресета и уровень ---
	const float levelH = 5;
	float bottom = h - levelH - 6;
	if (pr.count > 0) {
		const float slot = (w * 0.86f) / pr.count;
		float r = slot * 0.36f;
		if (r > 20) r = 20;
		if (r < 9) r = 9;
		const float y = bottom - r - 2;
		SetFont(be_bold_font);
		SetFontSize(r * 0.85f);
		for (int i = 0; i < pr.count; i++) {
			const float x = cx - w * 0.43f + slot * (i + 0.5f);
			const bool active = (fReading.voiced && fReading.stringIndex == i);
			if (active) {
				SetHighColor(inTune ? bright : (nearTune ? Green(190) : amber));
				FillEllipse(BPoint(x, y), r, r);
				SetHighColor(0, 0, 0);
			} else {
				SetHighColor(Green(110));
				StrokeEllipse(BPoint(x, y), r, r);
				SetHighColor(Green(170));
			}
			const char *nm = Tuner::NoteName(pr.midi[i], fNotation);
			// без решётки: она не помещается в кружок, а струны гитары без диезов
			char base[16]; strncpy(base, nm, sizeof(base) - 1); base[sizeof(base) - 1] = 0;
			float tw = StringWidth(base);
			float fs = r * 0.85f;
			while (tw > r * 1.7f && fs > 6) { fs -= 1; SetFontSize(fs); tw = StringWidth(base); }
			DrawString(base, BPoint(x - tw / 2, y + fs * 0.35f));
			SetFontSize(r * 0.85f);
		}
		bottom = y - r - 8;
	}
	// уровень входа
	{
		const float lw = w * 0.84f * ((fLevelShown + 60.0f) / 60.0f);
		SetHighColor(Green(55));
		FillRect(BRect(cx - w * 0.42f, h - levelH, cx + w * 0.42f, h - 1));
		SetHighColor(Green(170));
		if (lw > 0)
			FillRect(BRect(cx - w * 0.42f, h - levelH, cx - w * 0.42f + lw, h - 1));
	}

	// --- линейка ---
	const float rw = w * 0.84f;
	const float left = cx - rw / 2, right = cx + rw / 2;
	const float ry = bottom - 58;
	const float tickBig = 22;
	// зона «в строю» ±3 цента
	SetHighColor(Green(60));
	FillRect(BRect(cx - rw / 100.0f * 3.0f, ry - tickBig, cx + rw / 100.0f * 3.0f, ry + tickBig));
	SetHighColor(Green(130));
	StrokeLine(BPoint(left, ry), BPoint(right, ry));
	for (int c = -50; c <= 50; c += 5) {
		const float x = cx + c / 50.0f * rw / 2;
		float hh = (c % 10 == 0) ? 12.0f : 6.0f;
		if (c == 0) hh = tickBig;
		else if (c == -50 || c == 50) hh = 18.0f;
		SetHighColor(c == 0 ? Green(230) : Green(130));
		StrokeLine(BPoint(x, ry - hh), BPoint(x, ry + hh));
	}
	// подписи концов шкалы
	SetFont(be_plain_font);
	SetFontSize(11);
	SetHighColor(Green(110));
	DrawString("-50", BPoint(left - 2, ry + tickBig + 14));
	DrawString("0", BPoint(cx - StringWidth("0") / 2, ry + tickBig + 14));
	DrawString("+50", BPoint(right - StringWidth("+50") + 2, ry + tickBig + 14));

	// маркер
	if (fReading.voiced) {
		const float x = cx + fShown / 50.0f * rw / 2;
		SetHighColor(inTune ? bright : (nearTune ? Green(220) : amber));
		FillRect(BRect(x - 2, ry - tickBig - 6, x + 2, ry + tickBig + 6));
		FillTriangle(BPoint(x - 8, ry - tickBig - 18), BPoint(x + 8, ry - tickBig - 18), BPoint(x, ry - tickBig - 5));
	}

	// лампы: ниже строя / в строю / выше строя
	{
		const float ly = ry - tickBig - 38;
		const bool low = fReading.voiced && fReading.cents < -3.0f;
		const bool high = fReading.voiced && fReading.cents > 3.0f;
		SetHighColor(low ? amber : Green(60));
		FillTriangle(BPoint(cx - 70, ly), BPoint(cx - 52, ly - 10), BPoint(cx - 52, ly + 10));
		SetHighColor(inTune ? bright : Green(60));
		FillEllipse(BPoint(cx, ly), 11, 11);
		SetHighColor(high ? amber : Green(60));
		FillTriangle(BPoint(cx + 70, ly), BPoint(cx + 52, ly - 10), BPoint(cx + 52, ly + 10));
	}

	// --- большая нота ---
	{
		const float areaTop = 6, areaBottom = ry - tickBig - 58;
		float fs = areaBottom - areaTop;
		if (fs > w * 0.30f) fs = w * 0.30f;
		if (fs < 24) fs = 24;
		SetFont(be_bold_font);
		if (fReading.voiced) {
			BString name = Tuner::NoteName(fReading.note, fNotation);
			char oct[8]; snprintf(oct, sizeof(oct), "%d", Tuner::Octave(fReading.note));
			SetFontSize(fs);
			float tw = StringWidth(name.String());
			while (tw > w * 0.55f && fs > 24) { fs -= 2; SetFontSize(fs); tw = StringWidth(name.String()); }
			const float ow = fs * 0.38f * 0.6f * strlen(oct);
			const float x0 = cx - (tw + ow) / 2;
			const float base = areaTop + fs * 0.88f;
			SetHighColor(inTune ? bright : (nearTune ? Green(220) : Green(170)));
			DrawString(name.String(), BPoint(x0, base));
			SetFontSize(fs * 0.38f);
			DrawString(oct, BPoint(x0 + tw + 2, base));
		} else {
			SetFontSize(fs);
			SetHighColor(Green(70));
			const float tw = StringWidth("--");
			DrawString("--", BPoint(cx - tw / 2, areaTop + fs * 0.88f));
		}
		SetFont(be_plain_font);
		SetFontSize(14);
		// частота и отклонение, либо подсказка
		if (fReading.voiced) {
			char buf[64];
			snprintf(buf, sizeof(buf), "%.2f Hz    %+.0f \xC2\xA2", fReading.freq, fReading.cents);
			SetHighColor(Green(200));
			DrawString(buf, BPoint(cx - StringWidth(buf) / 2, ry + tickBig + 36));
		} else if (fStatus.Length()) {
			const char *t = Language.get(fStatus.String());
			SetHighColor(fStatus == "TUNER_PLAY" || fStatus == "TUNER_PRESS_START" ? Green(150) : amber);
			DrawString(t, BPoint(cx - StringWidth(t) / 2, ry + tickBig + 36));
		}
	}
}

/*******************************************************
*   TunerWindow
*******************************************************/
void TunerWindow::Open(BPoint c)
{
	if (sInstance) {
		sInstance->Activate();
		return;
	}
	sInstance = new TunerWindow(c);
	sInstance->Show();
}

TunerWindow::TunerWindow(BPoint c)
	: BWindow(BRect(c.x - 260, c.y - 190, c.x + 260, c.y + 190), Language.get("TUNER_TITLE"),
		B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL, B_AUTO_UPDATE_SIZE_LIMITS)
{
	fView = new TunerView();
	fStartButton = new BButton("start", Language.get("TUNER_START"), new BMessage(MSG_TUNER_START));

	// пресеты
	BPopUpMenu *presets = new BPopUpMenu("");
	for (int i = 0; i < Tuner::PresetCount(); i++) {
		BMessage *m = new BMessage(MSG_TUNER_PRESET);
		m->AddInt32("index", i);
		BMenuItem *it = new BMenuItem(Language.get(Tuner::PresetAt(i).key), m);
		it->SetMarked(i == sPreset);
		presets->AddItem(it);
	}
	BMenuField *presetField = new BMenuField("preset", Language.get("TUNER_PRESET"), presets);

	// обозначение нот
	BPopUpMenu *notation = new BPopUpMenu("");
	const char *nk[2] = { "TUNER_NOTATION_LATIN", "TUNER_NOTATION_SOLFEGE" };
	for (int i = 0; i < 2; i++) {
		BMessage *m = new BMessage(MSG_TUNER_NOTATION);
		m->AddInt32("notation", i);
		BMenuItem *it = new BMenuItem(Language.get(nk[i]), m);
		it->SetMarked(i == sNotation);
		notation->AddItem(it);
	}
	BMenuField *notationField = new BMenuField("notation", Language.get("TUNER_NOTATION"), notation);

	fA4Spinner = new BSpinner("a4", Language.get("TUNER_A4"), new BMessage(MSG_TUNER_A4));
	fA4Spinner->SetRange(415, 466);
	fA4Spinner->SetValue((int32)floorf(sA4 + 0.5f));

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.SetInsets(B_USE_SMALL_INSETS)
			.Add(fStartButton)
			.Add(presetField)
			.Add(notationField)
			.Add(fA4Spinner)
			.AddGlue()
		.End()
		.Add(fView)
		.End();

	fView->SetPreset(sPreset);
	fView->SetNotation(sNotation);
	fView->SetA4(sA4);
}

TunerWindow::~TunerWindow()
{
	sInstance = NULL;
}

bool TunerWindow::QuitRequested()
{
	fView->Stop();
	return true;
}

void TunerWindow::UpdateButton()
{
	fStartButton->SetLabel(Language.get(fView->Running() ? "TUNER_STOP" : "TUNER_START"));
}

void TunerWindow::MessageReceived(BMessage *message)
{
	switch (message->what) {
	case MSG_TUNER_START:
		if (fView->Running())
			fView->Stop();
		else
			fView->Start();
		UpdateButton();
		break;
	case MSG_TUNER_STATE:
		UpdateButton();
		break;
	case MSG_TUNER_PRESET: {
		int32 i;
		if (message->FindInt32("index", &i) == B_OK)
			fView->SetPreset(i);
		break;
	}
	case MSG_TUNER_NOTATION: {
		int32 i;
		if (message->FindInt32("notation", &i) == B_OK)
			fView->SetNotation(i);
		break;
	}
	case MSG_TUNER_A4:
		fView->SetA4((float)fA4Spinner->Value());
		break;
	default:
		BWindow::MessageReceived(message);
	}
}
