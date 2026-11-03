#include <StorageKit.h>
#include <InterfaceKit.h>
#include <PopUpMenu.h>
#include <String.h>
#include <stdio.h>
#include <string.h>

#include "CommonPool.h"
#include "Globals.h"
#include "Mixer.h"
#include "LadspaFilter.h"
#include "LadspaSlider.h"
#include "FxWindow.h"

#define FX_ADD_BTN		'fxAb'
#define FX_ADD_PLUGIN	'fxAp'
#define FX_REMOVE		'fxRm'
#define FX_UP			'fxUp'
#define FX_DOWN			'fxDn'
#define FX_SELECT		'fxSl'
#define FX_ENABLED		'fxEn'
#define FX_PARAM		'fxPm'

static FxWindow *sOpen[MIX_MAX_TRACKS];

void FxWindow::Open(int track, BWindow *near)
{
	if (track < 0 || track >= MIX_MAX_TRACKS || !Mix.Track(track)) return;
	if (sOpen[track]){
		sOpen[track]->Activate();
		return;
	}
	sOpen[track] = new FxWindow(track, near);
	sOpen[track]->Show();
}

FxWindow::FxWindow(int track, BWindow *near)
	: BWindow(BRect(0, 0, 579, 379), "Effects", B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_ASYNCHRONOUS_CONTROLS),
	  fTrack(track)
{
	MixTrack *t = Mix.Track(track);
	BString title(t ? t->name : "Track");
	title << " - " << Language.get("FX_TITLE");
	SetTitle(title.String());

	BView *bg = new BView(Bounds(), "bg", B_FOLLOW_ALL, B_WILL_DRAW);
	bg->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	AddChild(bg);

	fList = new BListView(BRect(12, 12, 212 - B_V_SCROLL_BAR_WIDTH, 300), "fxlist", B_SINGLE_SELECTION_LIST, B_FOLLOW_LEFT | B_FOLLOW_TOP_BOTTOM);
	fList->SetSelectionMessage(new BMessage(FX_SELECT));
	bg->AddChild(new BScrollView("fxlistscroll", fList, B_FOLLOW_LEFT | B_FOLLOW_TOP_BOTTOM, 0, false, true));

	fAdd = new BButton(BRect(12, 312, 102, 336), "add", Language.get("FX_ADD"), new BMessage(FX_ADD_BTN), B_FOLLOW_LEFT | B_FOLLOW_BOTTOM);
	fRemove = new BButton(BRect(108, 312, 198, 336), "rm", Language.get("FX_REMOVE"), new BMessage(FX_REMOVE), B_FOLLOW_LEFT | B_FOLLOW_BOTTOM);
	fUp = new BButton(BRect(12, 342, 102, 366), "up", Language.get("FX_UP"), new BMessage(FX_UP), B_FOLLOW_LEFT | B_FOLLOW_BOTTOM);
	fDown = new BButton(BRect(108, 342, 198, 366), "dn", Language.get("FX_DOWN"), new BMessage(FX_DOWN), B_FOLLOW_LEFT | B_FOLLOW_BOTTOM);
	bg->AddChild(fAdd); bg->AddChild(fRemove); bg->AddChild(fUp); bg->AddChild(fDown);

	fEnabled = new BCheckBox(BRect(236, 12, 567, 30), "en", Language.get("FX_ENABLED"), new BMessage(FX_ENABLED), B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP);
	bg->AddChild(fEnabled);

	fParams = new BView(BRect(0, 0, 330, 100), "params", B_FOLLOW_LEFT_RIGHT, B_WILL_DRAW);
	fParams->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	fScroll = new BScrollView("paramscroll", fParams, B_FOLLOW_ALL, 0, false, true);
	fScroll->MoveTo(224, 38);
	fScroll->ResizeTo(343, 328);
	bg->AddChild(fScroll);

	if (near) MoveTo(near->Frame().left + 80, near->Frame().top + 60);
	RefreshList(0);
}

bool FxWindow::QuitRequested()
{
	if (fTrack >= 0 && fTrack < MIX_MAX_TRACKS && sOpen[fTrack] == this) sOpen[fTrack] = NULL;
	return true;
}

void FxWindow::RefreshList(int select)
{
	fList->MakeEmpty();
	FxChain &fx = Mix.Fx(fTrack);
	for (int i = 0; i < fx.Count(); i++){
		const FxChain::Slot *s = fx.At(i);
		BString n(s->enabled ? "" : "[off] ");
		n << s->plugin.name.c_str();
		fList->AddItem(new BStringItem(n.String()));
	}
	if (select >= fx.Count()) select = fx.Count() - 1;
	if (select >= 0 && fx.Count() > 0) fList->Select(select);
	BuildParams();
}

void FxWindow::BuildParams()
{
	while (BView *c = fParams->ChildAt(0)){
		fParams->RemoveChild(c);
		delete c;
	}
	fSliders.clear(); fChecks.clear(); fPorts.clear();

	FxChain &fx = Mix.Fx(fTrack);
	int sel = fList->CurrentSelection();
	const FxChain::Slot *s = fx.At(sel);
	fEnabled->SetEnabled(s != NULL);
	fRemove->SetEnabled(s != NULL);
	fUp->SetEnabled(s != NULL && sel > 0);
	fDown->SetEnabled(s != NULL && sel < fx.Count() - 1);
	if (!s){
		fEnabled->SetValue(B_CONTROL_OFF);
		fParams->ResizeTo(fParams->Bounds().Width(), 10);
		return;
	}
	fEnabled->SetValue(s->enabled ? B_CONTROL_ON : B_CONTROL_OFF);

	const float rate = Pool.system_frequency > 0 ? Pool.system_frequency : 44100.0f;
	std::vector<int> ctl = s->plugin.ControlInputs();
	float y = 6;
	const float w = fParams->Bounds().Width() - 12;
	for (size_t i = 0; i < ctl.size(); i++){
		const Ladspa::Port &p = s->plugin.ports[ctl[i]];
		Ladspa::Range r = Ladspa::ResolveRange(p, rate);
		float v = s->values.count(ctl[i]) ? s->values.find(ctl[i])->second : r.def;
		BMessage *m = new BMessage(FX_PARAM);
		if (r.toggled){
			BCheckBox *cb = new BCheckBox(BRect(6, y, 6 + w, y + 18), NULL, p.name.c_str(), m);
			cb->SetValue(v > 0.5f ? B_CONTROL_ON : B_CONTROL_OFF);
			fParams->AddChild(cb);
			fChecks.push_back(cb); fSliders.push_back(NULL);
			y += 24;
		}else{
			LadspaSlider *sl = new LadspaSlider(p.name.c_str(), m, r, v);
			sl->MoveTo(6, y);
			sl->ResizeTo(w, 44);
			fParams->AddChild(sl);
			fSliders.push_back(sl); fChecks.push_back(NULL);
			y += 50;
		}
		fPorts.push_back(ctl[i]);
	}
	if (ctl.empty()){
		fParams->AddChild(new BStringView(BRect(6, 6, 6 + w, 22), NULL, Language.get("FX_NO_PARAMS")));
		y = 30;
	}
	fParams->ResizeTo(fParams->Bounds().Width(), y + 6);
	// после добавления окно ещё не знает о новых детях: прицепить цель сообщений
	for (int i = 0; i < fParams->CountChildren(); i++)
		if (BControl *c = dynamic_cast<BControl*>(fParams->ChildAt(i))) c->SetTarget(this);
}

void FxWindow::ShowAddMenu()
{
	BPopUpMenu *menu = new BPopUpMenu("fxadd", false, false);
	const std::vector<Ladspa::Plugin> &list = LadspaList();
	if (list.empty()){
		BMenuItem *none = new BMenuItem(Language.get("LADSPA_NONE"), NULL);
		none->SetEnabled(false);
		menu->AddItem(none);
	}else{
		BMenu *group = NULL;
		char letter = 0;
		const bool grouped = list.size() > 30;
		for (size_t i = 0; i < list.size(); i++){
			BMessage *m = new BMessage(FX_ADD_PLUGIN);
			m->AddString("file", list[i].file.c_str());
			m->AddString("label", list[i].label.c_str());
			BMenuItem *it = new BMenuItem(list[i].name.c_str(), m);
			if (!grouped){ menu->AddItem(it); continue; }
			char c = list[i].name.empty() ? '#' : list[i].name[0];
			if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
			if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) c = '#';
			if (group == NULL || c != letter){
				letter = c;
				char s[2] = { c, 0 };
				group = new BMenu(s);
				menu->AddItem(group);
			}
			group->AddItem(it);
		}
	}
	menu->SetTargetForItems(this);
	BPoint where = fAdd->ConvertToScreen(BPoint(0, fAdd->Bounds().Height() + 1));
	menu->Go(where, true, true, true);
}

void FxWindow::MessageReceived(BMessage *msg)
{
	if (!Mix.Track(fTrack)){ PostMessage(B_QUIT_REQUESTED); return; }
	FxChain &fx = Mix.Fx(fTrack);
	switch (msg->what){
	case FX_ADD_BTN:
		ShowAddMenu();
		break;
	case FX_ADD_PLUGIN:
	{
		const char *file = NULL, *label = NULL;
		if (msg->FindString("file", &file) != B_OK || msg->FindString("label", &label) != B_OK) break;
		const Ladspa::Plugin *p = LadspaFind(file, label);
		if (!p) break;
		std::string err;
		const float rate = Pool.system_frequency > 0 ? Pool.system_frequency : 44100.0f;
		if (!fx.Add(*p, rate, &err)){
			(new BAlert(NULL, err.c_str(), Language.get("OK")))->Go();
			break;
		}
		Mix.Touch();
		RefreshList(fx.Count() - 1);
		break;
	}
	case FX_REMOVE:
	{
		int sel = fList->CurrentSelection();
		if (sel < 0) break;
		fx.Remove(sel);
		Mix.Touch();
		RefreshList(sel);
		break;
	}
	case FX_UP:
	case FX_DOWN:
	{
		int sel = fList->CurrentSelection();
		int d = (msg->what == FX_UP) ? -1 : 1;
		if (sel >= 0 && fx.Move(sel, d)){
			Mix.Touch();
			RefreshList(sel + d);
		}
		break;
	}
	case FX_SELECT:
		BuildParams();
		break;
	case FX_ENABLED:
	{
		int sel = fList->CurrentSelection();
		if (sel < 0) break;
		fx.SetEnabled(sel, fEnabled->Value() == B_CONTROL_ON);
		Mix.Touch();
		RefreshList(sel);
		break;
	}
	case FX_PARAM:
	{
		int sel = fList->CurrentSelection();
		if (sel < 0) break;
		for (size_t i = 0; i < fPorts.size(); i++){
			float v;
			if (fSliders[i]) v = fSliders[i]->ValueF();
			else v = fChecks[i]->Value() == B_CONTROL_ON ? 1.0f : 0.0f;
			fx.SetValue(sel, fPorts[i], v);
		}
		break;
	}
	default:
		BWindow::MessageReceived(msg);
	}
}
