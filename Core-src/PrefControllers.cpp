#include "PrefControllers.h"

#include <GroupLayout.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <stdio.h>
#include <string.h>

#include "Globals.h"

#define CTRL_TICK		'ctTk'
#define CTRL_SELECT		'ctSl'
#define CTRL_RESCAN		'ctRs'
#define CTRL_BIND		'ctBd'
#define CTRL_CLEAR		'ctCl'
#define CTRL_DEFAULTS	'ctDf'
#define CTRL_RATES		'ctRt'

static const int kMaxAxesShown = 4;
static const int kMaxHatsShown = 1;
static const float kRowH = 16;

// ------------------------------------------------------------------ JoyLiveView

JoyLiveView::JoyLiveView()
	: BView("joy live", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE), fButtons(0), fPressed(0)
{
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	const float h = kRowH * (kMaxAxesShown + kMaxHatsShown) + 2 * 20 + 12;		// оси, шляпки, два ряда кнопок
	SetExplicitMinSize(BSize(200, h));
	SetExplicitPreferredSize(BSize(300, h));
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

// ------------------------------------------------------------------ ScrollHolder

ScrollHolder::ScrollHolder(BView *content)
	: BView("scroll holder", B_FRAME_EVENTS), fContent(content)
{
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	SetExplicitMinSize(BSize(200, 100));
	AddChild(content);
}

void ScrollHolder::AttachedToWindow()
{
	BView::AttachedToWindow();
	Update();
}

void ScrollHolder::FrameResized(float, float)
{
	Update();
}

void ScrollHolder::Update()
{
	const float w = Bounds().Width();
	float h = fContent->PreferredSize().height;
	if (h < Bounds().Height()) h = Bounds().Height();
	fContent->MoveTo(0, 0);
	fContent->ResizeTo(w, h);
	if (BScrollBar *bar = ScrollBar(B_VERTICAL)){
		const float range = h - Bounds().Height();
		bar->SetRange(0, range > 0 ? range : 0);
		bar->SetProportion(h > 0 ? Bounds().Height() / h : 1.0f);
		bar->SetSteps(16, Bounds().Height() * 0.8f);
	}
}

// ------------------------------------------------------------------ PrefControllers

PrefControllers::PrefControllers()
	: BView("Prefs controllers", 0), fRunner(NULL)
{
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	fBody = new BView("body", 0);
	fBody->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	BStringView *title = new BStringView(NULL, Language.get("CTRL_TITLE"));
	title->SetFont(be_bold_font);
	title->SetExplicitMinSize(BSize(40, B_SIZE_UNSET));
	BTextView *hint = new BTextView("hint", B_WILL_DRAW);
	hint->SetText(Language.get("CTRL_HINT"));
	hint->MakeEditable(false);
	hint->MakeSelectable(false);
	hint->SetWordWrap(true);
	hint->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	hint->SetLowUIColor(B_PANEL_BACKGROUND_COLOR);
	hint->SetExplicitMinSize(BSize(40, 78));
	hint->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 78));
	hint->SetExplicitPreferredSize(BSize(300, 78));

	fMenu = new BPopUpMenu("-");
	fDevice = new BMenuField(NULL, Language.get("CTRL_DEVICE"), fMenu);
	BButton *rescan = new BButton("rescan", Language.get("CTRL_RESCAN"), new BMessage(CTRL_RESCAN));
	fInfo = new BStringView(NULL, "");
	fStatus = new BStringView(NULL, "");
	fStatus->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_2_TINT);
	fInfo->SetTruncation(B_TRUNCATE_END);
	fStatus->SetTruncation(B_TRUNCATE_END);
	fInfo->SetExplicitMinSize(BSize(40, B_SIZE_UNSET));
	fStatus->SetExplicitMinSize(BSize(40, B_SIZE_UNSET));
	fLive = new JoyLiveView();

	BStringView *bindTitle = new BStringView(NULL, Language.get("CTRL_BINDINGS"));
	bindTitle->SetFont(be_bold_font);
	bindTitle->SetExplicitMinSize(BSize(40, B_SIZE_UNSET));

	BGroupLayout *table = new BGroupLayout(B_VERTICAL, 2);
	BView *tableView = new BView("bind table", 0);
	tableView->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	tableView->SetLayout(table);
	for (int a = 0; a < Ctrl::A_COUNT; a++){
		BStringView *label = new BStringView(NULL, Language.get(Ctrl::Map::ActionKey(a)));
		BMessage *bm = new BMessage(CTRL_BIND);
		bm->AddInt32("action", a);
		fBind[a] = new BButton("bind", "-", bm);
		label->SetTruncation(B_TRUNCATE_END);
		label->SetExplicitMinSize(BSize(40, B_SIZE_UNSET));
		fBind[a]->SetExplicitMinSize(BSize(110, B_SIZE_UNSET));
		fBind[a]->SetExplicitMaxSize(BSize(130, B_SIZE_UNSET));
		BMessage *cm = new BMessage(CTRL_CLEAR);
		cm->AddInt32("action", a);
		BButton *clear = new BButton("clear", "x", cm);
		clear->SetExplicitMaxSize(BSize(28, B_SIZE_UNLIMITED));
		BGroupView *row = new BGroupView(B_HORIZONTAL, B_USE_SMALL_SPACING);
		BLayoutBuilder::Group<>(row)
			.SetInsets(0)
			.Add(label, 1.0f)
			.Add(fBind[a])
			.Add(clear)
			.End();
		table->AddView(row);
	}

	fVolRate = new BSlider("volrate", Language.get("CTRL_VOL_RATE"), new BMessage(CTRL_RATES), 2, 40, B_HORIZONTAL);
	fPanRate = new BSlider("panrate", Language.get("CTRL_PAN_RATE"), new BMessage(CTRL_RATES), 10, 200, B_HORIZONTAL);
	fDead = new BSlider("dead", Language.get("CTRL_DEAD"), new BMessage(CTRL_RATES), 5, 60, B_HORIZONTAL);
	fVolRate->SetValue((int32)(Joy.Map().volRate + 0.5f));
	fPanRate->SetValue((int32)(Joy.Map().panRate * 100.0f + 0.5f));
	fDead->SetValue((int32)(Joy.Map().deadZone * 100.0f + 0.5f));
	BButton *defaults = new BButton("defaults", Language.get("CTRL_DEFAULTS"), new BMessage(CTRL_DEFAULTS));

	BLayoutBuilder::Group<>(fBody, B_VERTICAL, B_USE_SMALL_SPACING)
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
		.Add(bindTitle)
		.Add(tableView)
		.Add(fVolRate)
		.Add(fPanRate)
		.Add(fDead)
		.AddGroup(B_HORIZONTAL)
			.Add(defaults)
			.AddGlue()
		.End()
		.AddGlue()
		.End();

	BScrollView *scroll = new BScrollView("scroll", new ScrollHolder(fBody), 0, false, true, B_NO_BORDER);
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.SetInsets(0)
		.Add(scroll)
		.End();
}

PrefControllers::~PrefControllers()
{
	delete fRunner;
}

static void TargetAll(BView *v, BHandler *h)
{
	if (BControl *c = dynamic_cast<BControl *>(v))
		c->SetTarget(h);
	for (int32 i = 0; i < v->CountChildren(); i++)
		TargetAll(v->ChildAt(i), h);
}

void PrefControllers::AttachedToWindow()
{
	BView::AttachedToWindow();
	TargetAll(this, this);
	Rescan();
	fRunner = new BMessageRunner(BMessenger(this), new BMessage(CTRL_TICK), 40000);	// 25 раз в секунду
}

void PrefControllers::DetachedFromWindow()
{
	delete fRunner;
	fRunner = NULL;
	Joy.CancelLearn();
	BView::DetachedFromWindow();
}

void PrefControllers::Rescan()
{
	while (fMenu->CountItems() > 0)
		delete fMenu->RemoveItem((int32)0);

	const std::vector<BString> names = Joy.Devices();
	for (size_t i = 0; i < names.size(); i++){
		BMessage *m = new BMessage(CTRL_SELECT);
		m->AddString("name", names[i].String());
		BMenuItem *item = new BMenuItem(names[i].String(), m);
		item->SetTarget(this);
		if (names[i] == Prefs.joy_device)
			item->SetMarked(true);
		fMenu->AddItem(item);
	}
	if (!names.empty()){
		BMenuItem *marked = fMenu->FindMarked();
		if (marked == NULL){
			marked = fMenu->ItemAt(0);
			marked->SetMarked(true);
		}
		if (!Joy.IsOpen() || Joy.Name() != marked->Label()){
			Joy.Open(marked->Label());
			Prefs.Sync();
		}
	}
	Refresh();
}

void PrefControllers::Refresh()
{
	if (fMenu->CountItems() == 0){
		fInfo->SetText(Language.get("CTRL_NONE"));
		fStatus->SetText("");
	}else if (!Joy.IsOpen()){
		char s[160];
		snprintf(s, sizeof(s), Language.get("CTRL_ERR_OPEN"), Joy.Name().String(), "?");
		fInfo->SetText(s);
		fStatus->SetText("");
	}else{
		char s[200];
		snprintf(s, sizeof(s), Language.get("CTRL_INFO"), Joy.Name().String(),
			(int)Joy.CountAxes(), (int)Joy.CountHats(), (int)Joy.CountButtons());
		fInfo->SetText(s);
		fStatus->SetText(Joy.HasData() ? "" : Language.get("CTRL_NO_DATA"));
	}
	const int learning = Joy.LearnAction();
	for (int a = 0; a < Ctrl::A_COUNT; a++){
		if (a == learning)
			fBind[a]->SetLabel(Language.get("CTRL_PRESS"));
		else
			fBind[a]->SetLabel(JoyService::Describe(Joy.Map().b[a]).String());
	}
}

void PrefControllers::Tick()
{
	fLive->SetState(Joy.Last().axes, Joy.LastHats(), Joy.CountButtons(), Joy.Last().buttons);
	const bool ok = Joy.IsOpen() ? Joy.HasData() : true;
	if (Joy.IsOpen()) fStatus->SetText(ok ? "" : Language.get("CTRL_NO_DATA"));
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
		if (msg->FindString("name", &name) == B_OK && name){
			Joy.Open(name);
			Prefs.Sync();
			Refresh();
		}
		break;
	}
	case CTRL_BIND: {
		int32 a = -1;
		if (msg->FindInt32("action", &a) == B_OK && a >= 0 && a < Ctrl::A_COUNT){
			if (Joy.LearnAction() == a)
				Joy.CancelLearn();
			else
				Joy.BeginLearn(a, BMessenger(this));
			Refresh();
		}
		break;
	}
	case JOY_LEARNED:
		Refresh();
		break;
	case CTRL_CLEAR: {
		int32 a = -1;
		if (msg->FindInt32("action", &a) == B_OK && a >= 0 && a < Ctrl::A_COUNT){
			Joy.CancelLearn();
			Joy.Map().b[a] = Ctrl::Binding();
			Joy.SaveToPrefs();
			Refresh();
		}
		break;
	}
	case CTRL_DEFAULTS: {
		Joy.CancelLearn();
		Joy.Map().SetDefaults();
		fVolRate->SetValue((int32)(Joy.Map().volRate + 0.5f));
		fPanRate->SetValue((int32)(Joy.Map().panRate * 100.0f + 0.5f));
		fDead->SetValue((int32)(Joy.Map().deadZone * 100.0f + 0.5f));
		Joy.SaveToPrefs();
		Refresh();
		break;
	}
	case CTRL_RATES:
		Joy.Map().volRate = (float)fVolRate->Value();
		Joy.Map().panRate = (float)fPanRate->Value() / 100.0f;
		Joy.Map().deadZone = (float)fDead->Value() / 100.0f;
		Joy.SaveToPrefs();
		break;
	default:
		BView::MessageReceived(msg);
	}
}
