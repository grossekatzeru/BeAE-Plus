#include "PrefControllers.h"

#include <LayoutBuilder.h>
#include <stdio.h>
#include <string.h>

#include "Globals.h"

#define CTRL_TICK		'ctTk'
#define CTRL_SELECT		'ctSl'
#define CTRL_RESCAN		'ctRs'

static const int kMaxAxesShown = 8;
static const int kMaxHatsShown = 2;
static const float kRowH = 16;

// ------------------------------------------------------------------ JoyLiveView

JoyLiveView::JoyLiveView()
	: BView("joy live", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE), fButtons(0), fPressed(0)
{
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	const float h = kRowH * (kMaxAxesShown + kMaxHatsShown) + 2 * 20 + 12;		// оси, шляпки, два ряда кнопок
	SetExplicitMinSize(BSize(320, h));
	SetExplicitPreferredSize(BSize(380, h));
}

void JoyLiveView::SetState(const std::vector<int16> &axes, const std::vector<uint8> &hats, int buttons, uint32 pressed)
{
	fAxes = axes;
	fHats = hats;
	fButtons = buttons;
	fPressed = pressed;
	Invalidate();
}

void JoyLiveView::Draw(BRect)
{
	const rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
	const rgb_color text = ui_color(B_PANEL_TEXT_COLOR);
	BRect b = Bounds();
	SetFont(be_plain_font);
	float y = 4;

	const int na = (int)fAxes.size() < kMaxAxesShown ? (int)fAxes.size() : kMaxAxesShown;
	for (int i = 0; i < na; i++){
		char s[32];
		SetHighColor(text);
		snprintf(s, sizeof(s), "%s %d", Language.get("CTRL_AXIS"), i + 1);
		DrawString(s, BPoint(4, y + 12));
		BRect bar(70, y + 1, b.right - 56, y + kRowH - 3);
		SetHighColor(tint_color(base, B_DARKEN_3_TINT));
		FillRect(bar);
		SetHighColor(tint_color(base, B_DARKEN_4_TINT));
		StrokeRect(bar);
		const float mid = (bar.left + bar.right) / 2;
		const float pos = mid + (fAxes[i] / 32768.0f) * (bar.Width() / 2 - 1);
		SetHighColor(make_color(60, 200, 90));
		FillRect(BRect(pos < mid ? pos : mid, bar.top + 1, pos < mid ? mid : pos, bar.bottom - 1));
		SetHighColor(make_color(255, 255, 255));
		StrokeLine(BPoint(mid, bar.top), BPoint(mid, bar.bottom));
		SetHighColor(text);
		snprintf(s, sizeof(s), "%d", (int)fAxes[i]);
		DrawString(s, BPoint(b.right - 50, y + 12));
		y += kRowH;
	}
	y = 4 + kRowH * kMaxAxesShown;
	const int nh = (int)fHats.size() < kMaxHatsShown ? (int)fHats.size() : kMaxHatsShown;
	for (int i = 0; i < nh; i++){
		char s[48];
		SetHighColor(text);
		snprintf(s, sizeof(s), "%s %d: %d", Language.get("CTRL_HAT"), i + 1, (int)fHats[i]);
		DrawString(s, BPoint(4, y + 12));
		y += kRowH;
	}
	y = 4 + kRowH * (kMaxAxesShown + kMaxHatsShown) + 6;
	if (fButtons > 0){
		SetHighColor(text);
		DrawString(Language.get("CTRL_BUTTONS"), BPoint(4, y + 10));
		y += 14;
		for (int i = 0; i < fButtons && i < 32; i++){
			const int row = i / 16, col = i % 16;
			BRect cell(4 + col * 22, y + row * 22, 4 + col * 22 + 19, y + row * 22 + 19);
			const bool on = (fPressed & (1UL << i)) != 0;
			SetHighColor(on ? make_color(60, 200, 90) : tint_color(base, B_DARKEN_2_TINT));
			FillRoundRect(cell, 4, 4);
			SetHighColor(tint_color(base, B_DARKEN_4_TINT));
			StrokeRoundRect(cell, 4, 4);
			char s[8];
			snprintf(s, sizeof(s), "%d", i + 1);
			SetHighColor(on ? make_color(0, 0, 0) : text);
			DrawString(s, BPoint(cell.left + (20 - StringWidth(s)) / 2, cell.top + 14));
		}
	}
}

// ------------------------------------------------------------------ PrefControllers

PrefControllers::PrefControllers()
	: BView("Prefs controllers", 0), fStick(NULL), fRunner(NULL), fBadReads(0)
{
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	BStringView *title = new BStringView(NULL, Language.get("CTRL_TITLE"));
	title->SetFont(be_bold_font);
	BStringView *hint = new BStringView(NULL, Language.get("CTRL_HINT"));
	hint->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_2_TINT);

	fMenu = new BPopUpMenu("-");
	fDevice = new BMenuField(NULL, Language.get("CTRL_DEVICE"), fMenu);
	BButton *rescan = new BButton("rescan", Language.get("CTRL_RESCAN"), new BMessage(CTRL_RESCAN));
	fInfo = new BStringView(NULL, "");
	fStatus = new BStringView(NULL, "");
	fStatus->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_2_TINT);
	fLive = new JoyLiveView();

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(title)
		.Add(hint)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(fDevice)
			.Add(rescan)
			.AddGlue()
		.End()
		.Add(fInfo)
		.Add(fStatus)
		.Add(fLive)
		.AddGlue()
		.End();
}

PrefControllers::~PrefControllers()
{
	delete fRunner;
	CloseDevice();
}

void PrefControllers::AttachedToWindow()
{
	BView::AttachedToWindow();
	for (int32 i = 0; i < fMenu->CountItems(); i++)
		fMenu->ItemAt(i)->SetTarget(this);
	if (BButton *b = dynamic_cast<BButton *>(FindView("rescan")))
		b->SetTarget(this);
	Rescan();
	fRunner = new BMessageRunner(BMessenger(this), new BMessage(CTRL_TICK), 40000);	// 25 раз в секунду
}

void PrefControllers::DetachedFromWindow()
{
	delete fRunner;
	fRunner = NULL;
	CloseDevice();
	BView::DetachedFromWindow();
}

void PrefControllers::CloseDevice()
{
	if (fStick){
		fStick->Close();
		delete fStick;
		fStick = NULL;
	}
}

void PrefControllers::Rescan()
{
	CloseDevice();
	while (fMenu->CountItems() > 0)
		delete fMenu->RemoveItem((int32)0);

	BJoystick probe;
	const int32 n = probe.CountDevices();
	for (int32 i = 0; i < n; i++){
		char name[B_OS_NAME_LENGTH];
		name[0] = 0;
		if (probe.GetDeviceName(i, name) != B_OK || !name[0])
			continue;
		BMessage *m = new BMessage(CTRL_SELECT);
		m->AddString("name", name);
		BMenuItem *item = new BMenuItem(name, m);
		item->SetTarget(this);
		fMenu->AddItem(item);
	}
	std::vector<int16> noAxes;
	std::vector<uint8> noHats;
	fLive->SetState(noAxes, noHats, 0, 0);
	if (fMenu->CountItems() == 0){
		fInfo->SetText(Language.get("CTRL_NONE"));
		fStatus->SetText("");
		return;
	}
	BMenuItem *first = fMenu->ItemAt(0);
	first->SetMarked(true);
	OpenDevice(first->Label());
}

void PrefControllers::OpenDevice(const char *name)
{
	CloseDevice();
	fBadReads = 0;
	fStick = new BJoystick();
	const status_t err = fStick->Open(name);
	if (err < B_OK){
		char s[160];
		snprintf(s, sizeof(s), Language.get("CTRL_ERR_OPEN"), name, strerror(err));
		fInfo->SetText(s);
		fStatus->SetText("");
		delete fStick;
		fStick = NULL;
		return;
	}
	BString module, ctrl;
	fStick->GetControllerModule(&module);
	fStick->GetControllerName(&ctrl);
	char s[200];
	snprintf(s, sizeof(s), Language.get("CTRL_INFO"), ctrl.Length() ? ctrl.String() : name,
		(int)fStick->CountAxes(), (int)fStick->CountHats(), (int)fStick->CountButtons());
	fInfo->SetText(s);
	fStatus->SetText(module.Length() ? module.String() : "");
}

void PrefControllers::Tick()
{
	if (!fStick) return;
	const status_t err = fStick->Update();
	if (err < B_OK){
		if (++fBadReads == 25)						// секунда подряд без данных
			fStatus->SetText(Language.get("CTRL_NO_DATA"));
		return;
	}
	if (fBadReads >= 25) fStatus->SetText("");
	fBadReads = 0;
	const int na = fStick->CountAxes(), nh = fStick->CountHats(), nb = fStick->CountButtons();
	std::vector<int16> axes(na > 0 ? na : 1);
	std::vector<uint8> hats(nh > 0 ? nh : 1);
	if (na > 0) fStick->GetAxisValues(&axes[0]);
	if (nh > 0) fStick->GetHatValues(&hats[0]);
	axes.resize(na > 0 ? na : 0);
	hats.resize(nh > 0 ? nh : 0);
	fLive->SetState(axes, hats, nb, fStick->ButtonValues());
}

void PrefControllers::MessageReceived(BMessage *msg)
{
	switch (msg->what){
	case CTRL_TICK:
		Tick();
		break;
	case CTRL_RESCAN:
		Rescan();
		break;
	case CTRL_SELECT: {
		const char *name = NULL;
		if (msg->FindString("name", &name) == B_OK && name)
			OpenDevice(name);
		break;
	}
	default:
		BView::MessageReceived(msg);
	}
}
