#include <InterfaceKit.h>
#include <PopUpMenu.h>
#include <String.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <algorithm>

#include "BeFxSidechain.h"
#include "BeFxSidechainWindow.h"
#include "CommonPool.h"
#include "Globals.h"
#include "KeyCache.h"
#include "LadspaSlider.h"
#include "Mixer.h"

#define BEFX_PARAM		'bfPm'
#define BEFX_KEY_PICK	'bfKy'
#define BEFX_RANGE_PICK	'bfRg'
#define BEFX_CURVE_PICK	'bfCv'
#define BEFX_REFRESH	'bfRf'
#define BEFX_TICK		'bfTk'

static std::vector<BeFxSidechainWindow*> sWindows;

// ======================================================================== график

// Огибающая ключа в децибелах по времени, линия порога и столбик текущего сжатия справа.
class SidechainGraph : public BView {
public:
	SidechainGraph(BRect frame, BeFxSidechainWindow *owner)
		: BView(frame, "sidechain_graph", B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP, B_WILL_DRAW | B_PULSE_NEEDED | B_FULL_UPDATE_ON_RESIZE),
		  fOwner(owner)
	{
		SetViewColor(B_TRANSPARENT_COLOR);
	}

	virtual void Pulse()
	{
		if (Window()) Window()->PostMessage(BEFX_TICK);
		Invalidate();
	}

	virtual void Draw(BRect)
	{
		const BRect r = Bounds();
		const float barW = 16;
		const BRect plot(r.left, r.top, r.right - barW - 4, r.bottom);
		SetHighColor(28, 30, 36);
		FillRect(r);
		BeFxSidechainWindow::State st;
		const bool have = fOwner->GetState(st);
		const float dbTop = 0, dbBottom = -60;
		// сетка и подписи
		SetLowColor(28, 30, 36);
		for (int db = -12; db > dbBottom; db -= 12) {
			const float y = Y(plot, (float)db, dbTop, dbBottom);
			SetHighColor(55, 58, 68);
			StrokeLine(BPoint(plot.left, y), BPoint(plot.right, y));
			char b[16]; snprintf(b, sizeof(b), "%d", db);
			SetHighColor(120, 124, 136);
			DrawString(b, BPoint(plot.left + 3, y - 2));
		}
		if (have && st.lane >= 0) {
			std::shared_ptr<const Key::Envelope> env = Key::Cache::Global().Get(st.lane, st.range, st.cutoff);
			if (env && !env->peak.empty()) {
				const int w = (int)plot.Width();
				const size_t n = env->peak.size();
				SetHighColor(90, 170, 245);
				for (int x = 0; x < w; x++) {
					const size_t i0 = (size_t)((double)x * (double)n / w);
					size_t i1 = (size_t)((double)(x + 1) * (double)n / w);
					if (i1 <= i0) i1 = i0 + 1;
					if (i1 > n) i1 = n;
					float m = 0;
					for (size_t i = i0; i < i1; i++) if (env->peak[i] > m) m = env->peak[i];
					const float db = m > 1e-6f ? 20.0f * log10f(m) : dbBottom;
					const float y = Y(plot, db, dbTop, dbBottom);
					StrokeLine(BPoint(plot.left + x, plot.bottom), BPoint(plot.left + x, y));
				}
			}
			// порог
			const float yt = Y(plot, st.threshold, dbTop, dbBottom);
			SetHighColor(255, 120, 60);
			StrokeLine(BPoint(plot.left, yt), BPoint(plot.right, yt));
		}
		// столбик сжатия: чем больше приседание, тем выше заливка (весь столбик = глубина)
		SetHighColor(45, 48, 58);
		const BRect bar(plot.right + 4, r.top + 2, r.right - 1, r.bottom - 2);
		FillRect(bar);
		if (have && st.depth > 0.01f) {
			float f = st.reduction / st.depth;
			if (f < 0) f = 0;
			if (f > 1) f = 1;
			SetHighColor(255, 200, 60);
			FillRect(BRect(bar.left, bar.bottom - bar.Height() * f, bar.right, bar.bottom));
		}
		SetHighColor(80, 84, 96);
		StrokeRect(r);
	}

private:
	static float Y(const BRect &r, float db, float top, float bottom)
	{
		if (db > top) db = top;
		if (db < bottom) db = bottom;
		return r.top + (top - db) / (top - bottom) * (r.Height() - 1);
	}
	BeFxSidechainWindow *fOwner;
};

// ======================================================================== окно

static const int kPorts[] = { BeFx::SC_CUTOFF, BeFx::SC_THRESHOLD, BeFx::SC_DEPTH, BeFx::SC_ATTACK, BeFx::SC_HOLD,
	BeFx::SC_RELEASE, BeFx::SC_KNEE, BeFx::SC_MIX, BeFx::SC_OUTPUT };

void BeFxSidechainWindow::Open(int track, int slot, BWindow *near)
{
	FxChain &fx = Mix.Fx(track);
	const FxChain::Slot *s = fx.At(slot);
	if (!s || !s->runner || s->plugin.label != BeFx::kSidechainLabel) return;
	for (size_t i = 0; i < sWindows.size(); i++)
		if (sWindows[i]->fRunner == s->runner && sWindows[i]->Lock()) {
			sWindows[i]->Activate();
			sWindows[i]->Unlock();
			return;
		}
	BeFxSidechainWindow *w = new BeFxSidechainWindow(track, slot, near);
	w->Show();
}

BeFxSidechainWindow::BeFxSidechainWindow(int track, int slot, BWindow *near)
	: BWindow(BRect(100, 100, 100 + 584, 100 + 470), Language.get("BEFX_TITLE"), B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_ASYNCHRONOUS_CONTROLS),
	  fTrack(track), fRunner(NULL), fKeyMenu(NULL), fRevision(-1), fNeedCheck(true), fCutoffChanged(0)
{
	FxChain &fx = Mix.Fx(track);
	const FxChain::Slot *s = fx.At(slot);
	if (s) fRunner = s->runner;
	const MixTrack *t = track == MIX_MASTER ? NULL : Mix.Track(track);
	BString title(Language.get("BEFX_TITLE"));
	title << " - " << (t ? t->name : "Master");
	SetTitle(title.String());

	BView *bg = new BView(Bounds(), "bg", B_FOLLOW_ALL, B_WILL_DRAW);
	bg->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	AddChild(bg);

	// ключ и диапазон
	fKeyMenu = new BPopUpMenu(Language.get("BEFX_KEY_NONE"));
	fKeyField = new BMenuField(BRect(12, 10, 292, 32), "key", Language.get("BEFX_KEY"), fKeyMenu);
	fKeyField->SetDivider(100);
	bg->AddChild(fKeyField);

	BPopUpMenu *range = new BPopUpMenu("");
	const char *rk[2] = { "BEFX_RANGE_ALL", "BEFX_RANGE_LOW" };
	for (int i = 0; i < 2; i++) {
		BMessage *m = new BMessage(BEFX_RANGE_PICK); m->AddInt32("v", i);
		range->AddItem(new BMenuItem(Language.get(rk[i]), m));
	}
	fRangeField = new BMenuField(BRect(300, 10, 572, 32), "range", Language.get("BEFX_RANGE"), range);
	fRangeField->SetDivider(100);
	bg->AddChild(fRangeField);

	fStatus = new BStringView(BRect(12, 40, 400, 56), "status", "");
	bg->AddChild(fStatus);
	fRefresh = new BButton(BRect(412, 36, 572, 60), "refresh", Language.get("BEFX_REFRESH"), new BMessage(BEFX_REFRESH));
	bg->AddChild(fRefresh);

	// график
	fGraph = new SidechainGraph(BRect(12, 68, 572, 208), this);
	bg->AddChild(fGraph);
	fReduction = new BStringView(BRect(12, 212, 400, 228), "gr", "");
	bg->AddChild(fReduction);

	// параметры в два столбца
	const float rate = Pool.system_frequency > 0 ? Pool.system_frequency : 44100.0f;
	const int n = sizeof(kPorts) / sizeof(kPorts[0]);
	for (int i = 0; i < n; i++) {
		const int port = kPorts[i];
		const Ladspa::Port &p = s->plugin.ports[port];
		Ladspa::Range r = Ladspa::ResolveRange(p, rate);
		const float v = s->values.count(port) ? s->values.find(port)->second : r.def;
		LadspaSlider *sl = new LadspaSlider(p.name.c_str(), new BMessage(BEFX_PARAM), r, v);
		const int col = i < 5 ? 0 : 1, row = i < 5 ? i : i - 5;
		sl->MoveTo(12 + col * 282, 236 + row * 46);
		sl->ResizeTo(270, 44);
		bg->AddChild(sl);
		sl->SetTarget(this);
		fSliders.push_back(sl); fPorts.push_back(port);
	}
	BPopUpMenu *curve = new BPopUpMenu("");
	const char *ck[2] = { "BEFX_CURVE_EXP", "BEFX_CURVE_LIN" };
	for (int i = 0; i < 2; i++) {
		BMessage *m = new BMessage(BEFX_CURVE_PICK); m->AddInt32("v", i);
		curve->AddItem(new BMenuItem(Language.get(ck[i]), m));
	}
	fCurveField = new BMenuField(BRect(294, 236 + 4 * 46 + 8, 572, 236 + 4 * 46 + 30), "curve", Language.get("BEFX_CURVE"), curve);
	fCurveField->SetDivider(100);
	bg->AddChild(fCurveField);

	if (near) MoveTo(near->Frame().left + 60, near->Frame().top + 40);
	range->SetTargetForItems(this);
	curve->SetTargetForItems(this);
	fRefresh->SetTarget(this);
	FillKeyMenu();
	if (BMenuItem *it = range->ItemAt((int)(GetPort(BeFx::SC_RANGE) + 0.5f))) it->SetMarked(true);
	if (BMenuItem *it = curve->ItemAt((int)(GetPort(BeFx::SC_CURVE) + 0.5f))) it->SetMarked(true);
	SetPulseRate(100000);
	sWindows.push_back(this);
	Tick();
}

BeFxSidechainWindow::~BeFxSidechainWindow() {}

bool BeFxSidechainWindow::QuitRequested()
{
	sWindows.erase(std::remove(sWindows.begin(), sWindows.end(), this), sWindows.end());
	return true;
}

void BeFxSidechainWindow::WindowActivated(bool active)
{
	BWindow::WindowActivated(active);
	if (active) fNeedCheck = true;			// вернулись в окно: проверим, не изменилась ли дорожка ключа
}

int BeFxSidechainWindow::Locate() const
{
	FxChain &fx = Mix.Fx(fTrack);
	for (int i = 0; i < fx.Count(); i++)
		if (fx.At(i)->runner == fRunner) return i;
	return -1;
}

float BeFxSidechainWindow::GetPort(int port) const
{
	const int slot = Locate();
	const FxChain::Slot *s = slot >= 0 ? Mix.Fx(fTrack).At(slot) : NULL;
	if (!s) return 0;
	std::map<int, float>::const_iterator it = s->values.find(port);
	return it == s->values.end() ? 0 : it->second;
}

void BeFxSidechainWindow::SetPort(int port, float v)
{
	const int slot = Locate();
	if (slot < 0) return;
	Mix.Fx(fTrack).SetValue(slot, port, v);
	Mix.Touch();
}

bool BeFxSidechainWindow::GetState(State &st) const
{
	const int slot = Locate();
	const FxChain::Slot *s = slot >= 0 ? Mix.Fx(fTrack).At(slot) : NULL;
	if (!s) return false;
	const BeFx::KeyParams k = BeFx::ReadKey(s->values);
	st.lane = k.lane; st.range = k.range; st.cutoff = k.cutoff;
	st.threshold = GetPort(BeFx::SC_THRESHOLD);
	st.depth = GetPort(BeFx::SC_DEPTH);
	st.reduction = s->runner ? s->runner->Control(BeFx::SC_REDUCTION) : 0.0f;
	return true;
}

// список дорожек для выбора ключа: «N - название»; отмечена текущая
void BeFxSidechainWindow::FillKeyMenu()
{
	std::string names;
	for (int i = 0; i < MIX_MAX_TRACKS; i++) {
		const MixTrack *t = Mix.Track(i);
		if (t) { char b[16]; snprintf(b, sizeof(b), "%d:", i); names += b; names += t->name; names += "|"; }
	}
	const int cur = (int)(GetPort(BeFx::SC_KEY) + 0.5f);
	char sel[16]; snprintf(sel, sizeof(sel), "#%d", cur);
	names += sel;
	if (names == fKeyNames) return;
	fKeyNames = names;
	while (fKeyMenu->CountItems() > 0) delete fKeyMenu->RemoveItem((int32)0);
	BMessage *none = new BMessage(BEFX_KEY_PICK); none->AddInt32("lane", -1);
	BMenuItem *ni = new BMenuItem(Language.get("BEFX_KEY_NONE"), none);
	fKeyMenu->AddItem(ni);
	if (cur == 0) ni->SetMarked(true);
	fKeyMenu->AddSeparatorItem();
	for (int i = 0; i < MIX_MAX_TRACKS; i++) {
		const MixTrack *t = Mix.Track(i);
		if (!t) continue;
		BString label; label << (i + 1) << " - " << (t->name[0] ? t->name : "Track");
		BMessage *m = new BMessage(BEFX_KEY_PICK); m->AddInt32("lane", i);
		BMenuItem *it = new BMenuItem(label.String(), m);
		fKeyMenu->AddItem(it);
		if (cur == i + 1) it->SetMarked(true);
	}
	fKeyMenu->SetTargetForItems(this);
}

void BeFxSidechainWindow::RequestKey(bool force)
{
	State st;
	if (!GetState(st) || st.lane < 0) return;
	Key::Cache::Global().Request(st.lane, st.range, st.cutoff, force);
	fNeedCheck = true;
}

void BeFxSidechainWindow::Tick()
{
	State st;
	if (Locate() < 0 || !GetState(st)) { PostMessage(B_QUIT_REQUESTED); return; }
	FillKeyMenu();
	// частота среза: пересобираем огибающую, когда ползунок успокоился
	if (fCutoffChanged && system_time() - fCutoffChanged > 400000) { fCutoffChanged = 0; RequestKey(false); }
	if (st.lane < 0) {
		fStatus->SetText(Language.get("BEFX_ST_NONE"));
		fRefresh->SetEnabled(false);
	} else if (!Mix.Track(st.lane)) {
		fStatus->SetText(Language.get("BEFX_ST_MISSING"));
		fRefresh->SetEnabled(false);
	} else {
		fRefresh->SetEnabled(true);
		Key::Status ks = Key::Cache::Global().Peek(st.lane, st.range, st.cutoff);
		if (ks == Key::ST_NONE) { RequestKey(false); ks = Key::ST_BUILDING; }
		else if (ks == Key::ST_READY && (fNeedCheck || fRevision != Mix.Revision())) {
			fNeedCheck = false; fRevision = Mix.Revision();
			ks = Key::Cache::Global().Check(st.lane, st.range, st.cutoff);
		}
		const char *k = ks == Key::ST_READY ? "BEFX_ST_READY" : (ks == Key::ST_STALE ? "BEFX_ST_STALE" : "BEFX_ST_BUILD");
		fStatus->SetText(Language.get(k));
	}
	char b[64];
	snprintf(b, sizeof(b), "%s: %.1f dB", Language.get("BEFX_GR"), -st.reduction);
	fReduction->SetText(b);
}

void BeFxSidechainWindow::MessageReceived(BMessage *msg)
{
	switch (msg->what) {
	case BEFX_TICK:
		Tick();
		break;
	case BEFX_PARAM:
		for (size_t i = 0; i < fSliders.size(); i++) {
			const float v = fSliders[i]->ValueF();
			if (fPorts[i] == BeFx::SC_CUTOFF && fabsf(v - GetPort(BeFx::SC_CUTOFF)) > 0.001f) fCutoffChanged = system_time();
			SetPort(fPorts[i], v);
		}
		break;
	case BEFX_KEY_PICK:
	{
		int32 lane = -1;
		if (msg->FindInt32("lane", &lane) != B_OK) break;
		SetPort(BeFx::SC_KEY, (float)(lane + 1));
		fKeyNames = "";			// перестроить пометку
		FillKeyMenu();
		RequestKey(false);
		break;
	}
	case BEFX_RANGE_PICK:
	{
		int32 v = 0;
		if (msg->FindInt32("v", &v) != B_OK) break;
		SetPort(BeFx::SC_RANGE, (float)v);
		RequestKey(false);
		break;
	}
	case BEFX_CURVE_PICK:
	{
		int32 v = 0;
		if (msg->FindInt32("v", &v) != B_OK) break;
		SetPort(BeFx::SC_CURVE, (float)v);
		break;
	}
	case BEFX_REFRESH:
		RequestKey(true);
		break;
	default:
		BWindow::MessageReceived(msg);
	}
}
