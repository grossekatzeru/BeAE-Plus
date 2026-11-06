/*
 * NoiseFilter - см. NoiseFilter.h
 */
#include <LayoutBuilder.h>
#include <Window.h>
#include <View.h>
#include <InterfaceKit.h>
#include <stdlib.h>
#include <stdio.h>
#include <vector>

#include "Globals.h"
#include "RealtimeFilter.h"
#include "NoiseFilter.h"

#define NOISE_LEARN		'nsLr'

std::shared_ptr<const NoiseProfile> NoiseFilter::sProfile;

NoiseFilter::NoiseFilter(bool b) : RealtimeFilter(Language.get("NOISE_FILTER"), b)
	, core(NULL), reduction(NULL), sensitivity(NULL), smooth(NULL), useProfile(NULL), listen(NULL), learn(NULL), status(NULL)
{
}

NoiseFilter::~NoiseFilter()
{
	delete core;
}

BView *NoiseFilter::ConfigView()
{
	BView *view = new BView(NULL, B_WILL_DRAW);
	view->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	reduction = new SpinSlider(NULL, Language.get("NOISE_REDUCTION"), new BMessage(CONTROL_CHANGED), 0, 40);
	reduction->SetValue((int32)(Prefs.filter_noise_reduction + 0.5f));
	sensitivity = new SpinSlider(NULL, Language.get("NOISE_SENSITIVITY"), new BMessage(CONTROL_CHANGED), 10, 30);	// 1.0 .. 3.0
	sensitivity->SetValue((int32)(Prefs.filter_noise_sensitivity * 10 + 0.5f));
	smooth = new SpinSlider(NULL, Language.get("NOISE_SMOOTH"), new BMessage(CONTROL_CHANGED), 0, 100);
	smooth->SetValue((int32)(Prefs.filter_noise_smooth * 100 + 0.5f));

	useProfile = new BCheckBox(NULL, Language.get("NOISE_USE_PROFILE"), new BMessage(CONTROL_CHANGED));
	useProfile->SetValue(Prefs.filter_noise_profile ? B_CONTROL_ON : B_CONTROL_OFF);
	learn = new BButton(NULL, Language.get("NOISE_LEARN"), new BMessage(NOISE_LEARN));
	status = new BStringView(NULL, "");
	listen = new BCheckBox(NULL, Language.get("NOISE_LISTEN"), new BMessage(CONTROL_CHANGED));

	BLayoutBuilder::Group<>(view, B_VERTICAL)
		.Add(reduction)
		.Add(sensitivity)
		.Add(smooth)
		.Add(useProfile)
		.AddGroup(B_HORIZONTAL)
			.Add(learn)
			.Add(status)
			.AddGlue()
		.End()
		.Add(listen);
	UpdateStatus();
	return view;
}

void NoiseFilter::UpdateStatus()
{
	if (!status) return;
	BString s;
	if (sProfile) {
		char buf[64];
		snprintf(buf, sizeof(buf), Language.get("NOISE_PROFILE_SEC"), sProfile->seconds);
		s = buf;
	} else
		s = Language.get("NOISE_PROFILE_NONE");
	status->SetText(s.String());
}

void NoiseFilter::UpdateValues()
{
	if (!reduction) return;
	Prefs.filter_noise_reduction = reduction->Value();
	Prefs.filter_noise_sensitivity = sensitivity->Value() / 10.0f;
	Prefs.filter_noise_smooth = smooth->Value() / 100.0f;
	Prefs.filter_noise_profile = useProfile->Value() == B_CONTROL_ON;
	if (core)
		core->SetParams(Prefs.filter_noise_reduction, Prefs.filter_noise_sensitivity, Prefs.filter_noise_smooth,
			Prefs.filter_noise_profile, listen && listen->Value() == B_CONTROL_ON);
}

void NoiseFilter::MessageReceived(BMessage *msg)
{
	if (msg->what == NOISE_LEARN)
		LearnFromSelection();
	else
		RealtimeFilter::MessageReceived(msg);
}

// профиль - из того, что сейчас выделено в треке (это должен быть кусок, где слышен только шум)
void NoiseFilter::LearnFromSelection()
{
	if (Pool.sample_type == NONE || Pool.selection == NONE || Pool.sample_memory == NULL) {
		status->SetText(Language.get("NOISE_SELECT_FIRST"));
		return;
	}
	const int32 channels = Pool.sample_type;
	const int64 frames = Pool.r_sel_pointer - Pool.pointer + 1;
	if (frames < NoiseCore::kFrame * 2) {
		status->SetText(Language.get("NOISE_SELECT_LONGER"));
		return;
	}
	const float *p = Pool.sample_memory + Pool.pointer * channels;
	std::shared_ptr<const NoiseProfile> prof = NoiseCore::LearnFrom(p, (int)frames, channels, Pool.system_frequency);
	if (!prof) {
		status->SetText(Language.get("NOISE_SELECT_LONGER"));
		return;
	}
	sProfile = prof;
	if (core) core->SetProfile(sProfile);
	useProfile->SetValue(B_CONTROL_ON);
	UpdateValues();
	UpdateStatus();
}

bool NoiseFilter::InitFilter(float f, int32 c, int32 pass, int32 size)
{
	RealtimeFilter::InitFilter(f, c, pass, size);
	delete core;
	core = new NoiseCore(f);
	core->SetProfile(sProfile);
	core->SetParams(Prefs.filter_noise_reduction, Prefs.filter_noise_sensitivity, Prefs.filter_noise_smooth,
		Prefs.filter_noise_profile, listen && listen->Value() == B_CONTROL_ON);
	return true;
}

void NoiseFilter::DeAllocate()
{
	NoiseCore *old = core;
	core = NULL;
	delete old;
}

void NoiseFilter::FilterBuffer(float *buffer, size_t size)
{
	NoiseCore *c = core;
	if (c && m_channels > 0)
		c->Process(buffer, (int)(size / m_channels), m_channels);
}
