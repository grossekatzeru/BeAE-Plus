/*
 * LadspaFilter - см. LadspaFilter.h
 */
#include <Alert.h>
#include <Button.h>
#include <CheckBox.h>
#include <Directory.h>
#include <GroupLayout.h>
#include <SpaceLayoutItem.h>
#include <FindDirectory.h>
#include <InterfaceKit.h>
#include <LayoutBuilder.h>
#include <Path.h>
#include <Slider.h>
#include <StringView.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "Globals.h"
#include "LadspaFilter.h"
#include "main.h"

#define LADSPA_RESET_MSG	'ldRs'
static const int32 kSliderSteps = 1000;

// ======================================================================== ползунок

class LadspaSlider : public BSlider {
public:
	LadspaSlider(const char *label, BMessage *message, const Ladspa::Range &range, float initial)
		:
		BSlider(NULL, label, message, 0, kSliderSteps, B_HORIZONTAL),
		fRange(range),
		fIntMode(range.integer && (range.upper - range.lower) <= 2000.0f)
	{
		if (fIntMode)
			SetLimits((int32)floorf(range.lower + 0.5f), (int32)floorf(range.upper + 0.5f));
		SetModificationMessage(new BMessage(*message));
		SetValueF(initial);
	}

	float ValueF() const
	{
		if (fIntMode)
			return (float)Value();
		float t = (float)Value() / (float)kSliderSteps;
		float v;
		if (fRange.logarithmic)
			v = fRange.lower * powf(fRange.upper / fRange.lower, t);
		else
			v = fRange.lower + t * (fRange.upper - fRange.lower);
		if (fRange.integer)
			v = floorf(v + 0.5f);
		return v;
	}

	void SetValueF(float v)
	{
		if (v < fRange.lower) v = fRange.lower;
		if (v > fRange.upper) v = fRange.upper;
		if (fIntMode) {
			SetValue((int32)floorf(v + 0.5f));
			return;
		}
		float t;
		if (fRange.logarithmic)
			t = logf(v / fRange.lower) / logf(fRange.upper / fRange.lower);
		else
			t = (v - fRange.lower) / (fRange.upper - fRange.lower);
		SetValue((int32)floorf(t * kSliderSteps + 0.5f));
	}

	virtual const char *UpdateText() const
	{
		float v = ValueF();
		if (fRange.integer)
			snprintf(fText, sizeof(fText), "%d", (int)floorf(v + 0.5f));
		else
			snprintf(fText, sizeof(fText), "%.4g", v);
		return fText;
	}

private:
	Ladspa::Range	fRange;
	bool			fIntMode;
	mutable char	fText[32];
};

// ======================================================================== фильтр

static BString SanitizeName(const char *s)
{
	BString r;
	for (; *s; ++s) {
		char c = *s;
		bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
			|| c == '.' || c == '-' || c == '_';
		r << (ok ? c : '_');
	}
	return r;
}

LadspaFilter::LadspaFilter(const Ladspa::Plugin &plugin, bool realtime)
	:
	RealtimeFilter(plugin.name.c_str(), realtime),
	fPlugin(plugin)
{
	fTag = "LADSPA:";
	fTag << plugin.file.c_str() << "|" << plugin.label.c_str();
	LoadSaved();
}

LadspaFilter::~LadspaFilter()
{
	fRunner.Stop();
	SaveSaved();
}

BString LadspaFilter::SettingsPath() const
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return BString();
	path.Append(SETTINGS_DIR);
	path.Append("ladspa");
	create_directory(path.Path(), 0755);

	const char *slash = strrchr(fPlugin.file.c_str(), '/');
	BString name(SanitizeName(slash ? slash + 1 : fPlugin.file.c_str()));
	name << "__" << SanitizeName(fPlugin.label.c_str()) << ".conf";
	path.Append(name.String());
	return BString(path.Path());
}

void LadspaFilter::LoadSaved()
{
	fValues.clear();
	BString file = SettingsPath();
	if (file.Length() > 0)
		Ladspa::LoadValues(file.String(), &fValues);

	// недостающие и недопустимые значения заменяем значениями по умолчанию
	const float rate = Pool.system_frequency > 0 ? Pool.system_frequency : 44100.0f;
	std::vector<int> ctl = fPlugin.ControlInputs();
	for (size_t i = 0; i < ctl.size(); ++i) {
		const Ladspa::Range r = Ladspa::ResolveRange(fPlugin.ports[ctl[i]], rate);
		Ladspa::Values::iterator it = fValues.find(ctl[i]);
		if (it == fValues.end()) {
			fValues[ctl[i]] = r.def;
		} else {
			if (it->second < r.lower) it->second = r.lower;
			if (it->second > r.upper) it->second = r.upper;
		}
	}
}

void LadspaFilter::SaveSaved()
{
	BString file = SettingsPath();
	if (file.Length() > 0)
		Ladspa::SaveValues(file.String(), fValues);
}

void LadspaFilter::ResetToDefaults()
{
	const float rate = Pool.system_frequency > 0 ? Pool.system_frequency : 44100.0f;
	for (size_t i = 0; i < fControls.size(); ++i) {
		Control &c = fControls[i];
		const Ladspa::Range r = Ladspa::ResolveRange(fPlugin.ports[c.port], rate);
		if (c.slider)
			c.slider->SetValueF(r.def);
		if (c.check)
			c.check->SetValue(r.def > 0.5f ? B_CONTROL_ON : B_CONTROL_OFF);
	}
	UpdateValues();
}

BView *LadspaFilter::ConfigView()
{
	std::vector<int> ctl = fPlugin.ControlInputs();
	if (ctl.empty())
		return NULL;			// без параметров: применяется сразу

	const float rate = Pool.system_frequency > 0 ? Pool.system_frequency : 44100.0f;

	BView *view = new BView(NULL, B_WILL_DRAW);
	view->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	fControls.clear();
	for (size_t i = 0; i < ctl.size(); ++i) {
		const Ladspa::Port &p = fPlugin.ports[ctl[i]];
		Control c;
		c.port = ctl[i];
		c.range = Ladspa::ResolveRange(p, rate);
		c.slider = NULL;
		c.check = NULL;
		float v = fValues.count(c.port) ? fValues[c.port] : c.range.def;
		if (c.range.toggled) {
			c.check = new BCheckBox(NULL, p.name.c_str(), new BMessage(CONTROL_CHANGED));
			c.check->SetValue(v > 0.5f ? B_CONTROL_ON : B_CONTROL_OFF);
		} else {
			c.slider = new LadspaSlider(p.name.c_str(), new BMessage(CONTROL_CHANGED), c.range, v);
			c.slider->SetExplicitMinSize(BSize(260, B_SIZE_UNSET));
		}
		fControls.push_back(c);
	}

	// при большом числе параметров — две колонки
	const bool twoCols = fControls.size() > 8;
	const size_t half = twoCols ? (fControls.size() + 1) / 2 : fControls.size();

	BGroupLayout *root = new BGroupLayout(B_VERTICAL, B_USE_SMALL_SPACING);
	view->SetLayout(root);
	if (fPlugin.maker.size() > 0)
		root->AddView(new BStringView(NULL, fPlugin.maker.c_str()));

	BGroupLayout *columns = new BGroupLayout(B_HORIZONTAL, B_USE_DEFAULT_SPACING);
	root->AddItem(columns);
	const size_t colCount = twoCols ? 2 : 1;
	for (size_t col = 0; col < colCount; ++col) {
		BGroupLayout *group = new BGroupLayout(B_VERTICAL, B_USE_SMALL_SPACING);
		columns->AddItem(group);
		size_t from = col * half;
		size_t to = twoCols ? (col == 0 ? half : fControls.size()) : fControls.size();
		for (size_t i = from; i < to; ++i) {
			if (fControls[i].slider)
				group->AddView(fControls[i].slider);
			else
				group->AddView(fControls[i].check);
		}
	}

	BGroupLayout *buttons = new BGroupLayout(B_HORIZONTAL);
	root->AddItem(buttons);
	buttons->AddView(new BButton(NULL, Language.get("LADSPA_RESET"), new BMessage(LADSPA_RESET_MSG)));
	buttons->AddItem(BSpaceLayoutItem::CreateGlue());

	return view;
}

void LadspaFilter::MessageReceived(BMessage *message)
{
	switch (message->what) {
	case LADSPA_RESET_MSG:
		ResetToDefaults();
		break;
	default:
		RealtimeFilter::MessageReceived(message);
		break;
	}
}

void LadspaFilter::UpdateValues()
{
	for (size_t i = 0; i < fControls.size(); ++i) {
		Control &c = fControls[i];
		float v = c.slider ? c.slider->ValueF() : (c.check->Value() == B_CONTROL_ON ? 1.0f : 0.0f);
		fValues[c.port] = v;
		if (fRunner.IsRunning())
			fRunner.SetControl(c.port, v);
	}
}

bool LadspaFilter::InitFilter(float f, int32 channels, int32 pass, int32 total)
{
	RealtimeFilter::InitFilter(f, channels, pass, total);

	std::string error;
	if (!fRunner.Start(fPlugin, f, channels == 1 ? 1 : 2, &error)) {
		fprintf(stderr, "LADSPA: %s: %s\n", fPlugin.name.c_str(), error.c_str());
		return false;
	}
	for (Ladspa::Values::const_iterator it = fValues.begin(); it != fValues.end(); ++it)
		fRunner.SetControl(it->first, it->second);
	return true;
}

void LadspaFilter::DeAllocate()
{
	fRunner.Stop();
}

void LadspaFilter::FilterBuffer(float *buffer, size_t size)
{
	if (!fRunner.IsRunning() || m_channels < 1)
		return;
	fRunner.Process(buffer, size / (size_t)(m_channels == 1 ? 1 : 2));
}

const char *LadspaFilter::RepeatTag() const
{
	return fTag.String();
}

// ======================================================================== список и меню

static std::vector<Ladspa::Plugin> sPlugins;
static bool sScanned = false;

void LadspaRescan()
{
	std::vector<std::string> log;
	std::vector<Ladspa::Plugin> all = Ladspa::Scan(Ladspa::DefaultPaths(), &log);
	for (size_t i = 0; i < log.size(); ++i)
		fprintf(stderr, "LADSPA: %s\n", log[i].c_str());
	// в список попадают только плагины с поддерживаемой схемой портов
	sPlugins.clear();
	for (size_t i = 0; i < all.size(); ++i)
		if (all[i].mode != Ladspa::MODE_UNSUPPORTED)
			sPlugins.push_back(all[i]);
	sScanned = true;
}

const std::vector<Ladspa::Plugin> &LadspaList()
{
	if (!sScanned)
		LadspaRescan();
	return sPlugins;
}

const Ladspa::Plugin *LadspaFind(const char *file, const char *label)
{
	const std::vector<Ladspa::Plugin> &l = LadspaList();
	for (size_t i = 0; i < l.size(); ++i)
		if (l[i].file == file && l[i].label == label)
			return &l[i];
	return NULL;
}

static BMenuItem *MakeLadspaItem(const Ladspa::Plugin &p)
{
	BMessage *m = new BMessage(RUN_LADSPA);
	m->AddString("file", p.file.c_str());
	m->AddString("label", p.label.c_str());
	BString name(p.name.c_str());
	if (!p.ControlInputs().empty())
		name << "...";
	return new BMenuItem(name.String(), m);
}

BMenu *BuildLadspaMenu()
{
	BMenu *menu = new BMenu(Language.get("LADSPA_MENU"));
	const std::vector<Ladspa::Plugin> &list = LadspaList();

	if (list.empty()) {
		BMenuItem *none = new BMenuItem(Language.get("LADSPA_NONE"), NULL);
		none->SetEnabled(false);
		menu->AddItem(none);
	} else if (list.size() <= 30) {
		for (size_t i = 0; i < list.size(); ++i)
			menu->AddItem(MakeLadspaItem(list[i]));
	} else {
		// много плагинов: подменю по первой букве названия
		BMenu *group = NULL;
		char letter = 0;
		for (size_t i = 0; i < list.size(); ++i) {
			char c = list[i].name.empty() ? '#' : list[i].name[0];
			if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
			if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) c = '#';
			if (group == NULL || c != letter) {
				letter = c;
				char s[2] = { c, 0 };
				group = new BMenu(s);
				menu->AddItem(group);
			}
			group->AddItem(MakeLadspaItem(list[i]));
		}
	}
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem(Language.get("LADSPA_RESCAN"), new BMessage(RESCAN_LADSPA)));
	return menu;
}

bool IsLadspaTag(const char *tag)
{
	return tag && strncmp(tag, "LADSPA:", 7) == 0;
}

static bool SplitTag(const char *tag, BString *file, BString *label)
{
	if (!IsLadspaTag(tag))
		return false;
	BString rest(tag + 7);
	int32 bar = rest.FindLast("|");
	if (bar < 0)
		return false;
	rest.CopyInto(*file, 0, bar);
	rest.CopyInto(*label, bar + 1, rest.Length() - bar - 1);
	return true;
}

RealtimeFilter *CreateLadspaFilterFromTag(const char *tag)
{
	BString file, label;
	if (!SplitTag(tag, &file, &label))
		return NULL;
	const Ladspa::Plugin *p = LadspaFind(file.String(), label.String());
	if (p == NULL)
		return NULL;
	return new LadspaFilter(*p, false);
}

const char *FilterTagDisplayName(const char *tag, BString *storage)
{
	BString file, label;
	if (SplitTag(tag, &file, &label)) {
		const Ladspa::Plugin *p = LadspaFind(file.String(), label.String());
		*storage = p ? p->name.c_str() : label.String();
		return storage->String();
	}
	return Language.get(tag);
}
