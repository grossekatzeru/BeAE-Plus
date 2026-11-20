#include "InstrumentEditor.h"

#include <Box.h>
#include <CheckBox.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Path.h>
#include <Directory.h>
#include <Entry.h>
#include <MessageRunner.h>
#include <PopUpMenu.h>
#include <Slider.h>
#include <String.h>
#include <StringView.h>

#include <OS.h>
#include <math.h>
#include <strings.h>
#include <string.h>
#include <stdio.h>
#include <string>

#include "ZynControl.h"


enum {
	MSG_ED_PARAM	= 'edpr',		// a slider moved: "index"
	MSG_ED_WAVE		= 'edwv',		// base waveform: "wave"
	MSG_ED_FTYPE	= 'edft',		// filter type: "type"
	MSG_ED_VOICE	= 'edvc',		// voice: "voice"
	MSG_ED_VON		= 'edvo',		// the voice is on or off
	MSG_ED_STEREO	= 'edst',		// ADsynth: stereo or mono
	MSG_ED_FXTYPE	= 'edxt',		// effect: "slot", "type"
	MSG_ED_FXPRESET	= 'edxp',		// effect: "slot", "preset"
	MSG_ED_FXPAR	= 'edxr',		// effect: a slider moved: "slot", "k"
	MSG_ED_FXRELOAD	= 'edxl',		// read the effects again
	MSG_ED_LOADED	= 'edld',		// the thread has read the values of the engine
	MSG_ED_PART		= 'edpt',		// show another part: "part", "name"
	MSG_ED_SAVE_AS	= 'edsa',		// menu: save the instrument
	MSG_ED_SAVE		= 'edsv'		// the file panel is done
};


// How a slider (always 0..127, or min..max) becomes a parameter of the engine
enum Kind { K_INT, K_LOG, K_FINE };		// K_FINE: fine detune, 8192 + 128 * slider

struct EditParam {
	int			row;			// 0: the upper row of boxes, 1: the lower one
	const char*	group;			// box around several sliders
	const char*	label;			// at most three letters
	const char*	path;			// below /partN/kit0/adpars/
	Kind		kind;
	int			min, max;		// slider range
	double		lo, hi;			// K_LOG: the value of the engine at 0 and at max
	int			start;			// slider position when the engine does not answer
	bool		oscillator;		// the oscillator has to be prepared again after a change
	const char*	tip;
};

static const EditParam kParams[] = {
	{ 0, "Amp", "Att", "GlobalPar/AmpEnvelope/PA_dt", K_INT, 0, 127, 0, 0, 0, false, "Attack time" },
	{ 0, "Amp", "Dec", "GlobalPar/AmpEnvelope/PD_dt", K_INT, 0, 127, 0, 0, 40, false, "Decay time" },
	{ 0, "Amp", "Sus", "GlobalPar/AmpEnvelope/PS_val", K_INT, 0, 127, 0, 0, 127, false, "Sustain level" },
	{ 0, "Amp", "Rel", "GlobalPar/AmpEnvelope/PR_dt", K_INT, 0, 127, 0, 0, 40, false, "Release time" },
	{ 0, "Filter", "Cut", "GlobalPar/GlobalFilter/basefreq", K_LOG, 0, 127, 31.25, 32000.0, 127, false,
		"Filter cutoff frequency" },
	{ 0, "Filter", "Res", "GlobalPar/GlobalFilter/baseq", K_LOG, 0, 127, 0.3, 30.0, 40, false,
		"Filter resonance (Q)" },
	{ 0, "Filter", "Stg", "GlobalPar/GlobalFilter/Pstages", K_INT, 0, 5, 0, 0, 0, false,
		"Filter stages: the slope gets steeper" },
	{ 0, "Voice", "Vol", "VoicePar0/PVolume", K_INT, 0, 127, 0, 0, 100, false,
		"Volume of the voice" },
	{ 0, "Voice", "Pan", "VoicePar0/PPanning", K_INT, 0, 127, 0, 0, 64, false,
		"Panning of the voice (64 is the middle, 0 is random)" },
	{ 0, "Voice", "Shp", "VoicePar0/OscilSmp/Pbasefuncpar", K_INT, 0, 127, 0, 0, 64, true,
		"Shape of the waveform (what it does depends on the waveform)" },
	{ 0, "Voice", "Uni", "VoicePar0/Unison_size", K_INT, 1, 8, 0, 0, 1, false,
		"Unison: how many copies of the voice play together" },
	{ 0, "Voice", "Spr", "VoicePar0/Unison_frequency_spread", K_INT, 0, 127, 0, 0, 60, false,
		"Unison: how far the copies are detuned" },
	{ 0, "LFO", "Dep", "GlobalPar/FreqLfo/Pintensity", K_INT, 0, 127, 0, 0, 0, false, "Vibrato depth" },
	{ 0, "LFO", "Spd", "GlobalPar/FreqLfo/freq", K_LOG, 0, 127, 0.0776, 85.25, 60, false, "Vibrato speed" },
	{ 0, "Out", "Vol", "GlobalPar/PVolume", K_INT, 0, 127, 0, 0, 90, false, "Volume of the instrument" },
	{ 1, "FEnv", "Sta", "GlobalPar/FilterEnvelope/PA_val", K_INT, 0, 127, 0, 0, 64, false,
		"Filter envelope: where the cutoff starts (64 = the cutoff of the filter)" },
	{ 1, "FEnv", "Att", "GlobalPar/FilterEnvelope/PA_dt", K_INT, 0, 127, 0, 0, 20, false,
		"Filter envelope: time from the start level to the peak" },
	{ 1, "FEnv", "Pea", "GlobalPar/FilterEnvelope/PD_val", K_INT, 0, 127, 0, 0, 64, false,
		"Filter envelope: the peak level (above 64 opens the filter)" },
	{ 1, "FEnv", "Dec", "GlobalPar/FilterEnvelope/PD_dt", K_INT, 0, 127, 0, 0, 60, false,
		"Filter envelope: time from the peak back to the cutoff of the filter" },
	{ 1, "FEnv", "Rel", "GlobalPar/FilterEnvelope/PR_dt", K_INT, 0, 127, 0, 0, 40, false,
		"Filter envelope: release time" },
	{ 1, "FEnv", "End", "GlobalPar/FilterEnvelope/PR_val", K_INT, 0, 127, 0, 0, 64, false,
		"Filter envelope: the level the release goes to" },
	{ 1, "Pitch", "Oct", "GlobalPar/octave", K_INT, -8, 7, 0, 0, 0, false, "Octave" },
	{ 1, "Pitch", "Crs", "GlobalPar/coarsedetune", K_INT, -64, 63, 0, 0, 0, false, "Coarse tuning (semitones)" },
	{ 1, "Pitch", "Fin", "GlobalPar/PDetune", K_FINE, -64, 63, 0, 0, 0, false, "Fine tuning" },
	{ 1, "PEnv", "Sta", "GlobalPar/FreqEnvelope/PA_val", K_INT, 0, 127, 0, 0, 64, false,
		"Pitch envelope: where the pitch starts (64 = the pitch of the note)" },
	{ 1, "PEnv", "Att", "GlobalPar/FreqEnvelope/PA_dt", K_INT, 0, 127, 0, 0, 40, false,
		"Pitch envelope: time from the start pitch to the pitch of the note" },
	{ 1, "PEnv", "End", "GlobalPar/FreqEnvelope/PR_val", K_INT, 0, 127, 0, 0, 64, false,
		"Pitch envelope: where the pitch goes when the key is released" },
	{ 1, "PEnv", "Rel", "GlobalPar/FreqEnvelope/PR_dt", K_INT, 0, 127, 0, 0, 40, false,
		"Pitch envelope: release time" },
	{ 1, "Punch", "Str", "GlobalPar/PPunchStrength", K_INT, 0, 127, 0, 0, 0, false,
		"Punch: how strong the beginning of the note is" },
	{ 1, "Punch", "Tim", "GlobalPar/PPunchTime", K_INT, 0, 127, 0, 0, 60, false,
		"Punch: how long the strong beginning lasts" }
};
static const int kParamCount = sizeof(kParams) / sizeof(kParams[0]);

static const char* const kWaves[] = {
	"Sine", "Triangle", "Pulse", "Saw", "Power", "Gauss", "Diode", "Abs sine", "Pulse sine",
	"Stretch sine", "Chirp", "Abs stretch sine", "Chebyshev", "Square", "Spike", "Circle", "Power sinus"
};
static const int kWaveCount = sizeof(kWaves) / sizeof(kWaves[0]);

static const char* const kFilterTypes[] = {
	"Low pass 1", "High pass 1", "Low pass 2", "High pass 2", "Band pass", "Notch", "Peak",
	"Low shelf", "High shelf"
};
static const int kFilterTypeCount = sizeof(kFilterTypes) / sizeof(kFilterTypes[0]);

static const char* kFilterTypePath = "GlobalPar/GlobalFilter/Ptype";
static const char* kFilterClassPath = "GlobalPar/GlobalFilter/Pcategory";	// 0: analog, the only one with these types
static const char* const kVoices[] = {
	"Voice 1", "Voice 2", "Voice 3", "Voice 4", "Voice 5", "Voice 6", "Voice 7", "Voice 8"
};
static const int kVoiceCount = sizeof(kVoices) / sizeof(kVoices[0]);

static const char* kWavePath = "VoicePar0/OscilSmp/Pcurrentbasefunc";
// ---- the effects of the part (three slots): the type, a preset, and the main parameters
struct FxSlider {
	const char*	label;
	int			index;			// the number of the parameter in the engine
	int			min, max;
	const char*	tip;
};

struct FxType {
	const char*			name;
	int					count;				// sliders in use
	FxSlider			sliders[5];
	const char* const*	presets;
	int					presetCount;
};

static const char* const kReverbPresets[] = { "Cathedral 1", "Cathedral 2", "Cathedral 3", "Hall 1", "Hall 2",
	"Room 1", "Room 2", "Basement", "Tunnel", "Echoed 1", "Echoed 2", "Very long 1", "Very long 2" };
static const char* const kEchoPresets[] = { "Echo 1", "Echo 2", "Echo 3", "Simple Echo", "Canyon",
	"Panning Echo 1", "Panning Echo 2", "Panning Echo 3", "Feedback Echo" };
static const char* const kChorusPresets[] = { "Chorus 1", "Chorus 2", "Chorus 3", "Celeste 1", "Celeste 2",
	"Flange 1", "Flange 2", "Flange 3", "Flange 4", "Flange 5" };
static const char* const kPhaserPresets[] = { "Phaser 1", "Phaser 2", "Phaser 3", "Phaser 4", "Phaser 5",
	"Phaser 6", "APhaser 1", "APhaser 2", "APhaser 3", "APhaser 4", "APhaser 5", "APhaser 6" };
static const char* const kWahPresets[] = { "Wah 1", "Wah 2", "Wah 3", "Wah 4" };
static const char* const kDistortionPresets[] = { "Overdrive 1", "Overdrive 2", "A. Exciter 1",
	"A. Exciter 2", "Guitar Amp", "Quantisize" };

// the order is that of the engine ("efftype")
static const FxType kFxTypes[] = {
	{ "None", 0, {}, NULL, 0 },
	{ "Reverb", 5, { { "Vol", 0, 0, 127, "Wet level" }, { "Tim", 2, 0, 127, "Length of the reverb" },
		{ "Dly", 3, 0, 127, "Delay of the first reflection" }, { "Siz", 11, 0, 127, "Size of the room" },
		{ "Dmp", 9, 0, 127, "Damping of the low and high frequencies" } },
		kReverbPresets, 13 },
	{ "Echo", 5, { { "Vol", 0, 0, 127, "Wet level" }, { "Dly", 2, 0, 127, "Length of the echo" },
		{ "Fbk", 5, 0, 127, "Feedback: how many repeats" }, { "Dmp", 6, 0, 127, "Damping of the high frequencies" },
		{ "Pan", 1, 0, 127, "Panning" } },
		kEchoPresets, 9 },
	{ "Chorus", 5, { { "Vol", 0, 0, 127, "Wet level" }, { "Frq", 2, 0, 127, "Speed of the modulation" },
		{ "Dep", 6, 0, 127, "Depth of the modulation" }, { "Dly", 7, 0, 127, "Delay" },
		{ "Fbk", 8, 0, 127, "Feedback" } },
		kChorusPresets, 10 },
	{ "Phaser", 5, { { "Vol", 0, 0, 127, "Wet level" }, { "Frq", 2, 0, 127, "Speed of the modulation" },
		{ "Dep", 6, 0, 127, "Depth of the modulation" }, { "Fbk", 7, 0, 127, "Feedback" },
		{ "Stg", 8, 1, 12, "Stages" } },
		kPhaserPresets, 12 },
	{ "Alienwah", 5, { { "Vol", 0, 0, 127, "Wet level" }, { "Frq", 2, 0, 127, "Speed of the modulation" },
		{ "Dep", 6, 0, 127, "Depth of the modulation" }, { "Fbk", 7, 0, 127, "Feedback" },
		{ "Dly", 8, 1, 100, "Delay" } },
		kWahPresets, 4 },
	{ "Distortion", 5, { { "Vol", 0, 0, 127, "Wet level" }, { "Drv", 3, 0, 127, "Drive: how much the signal is pushed" },
		{ "Out", 4, 0, 127, "Output level" }, { "LPF", 7, 0, 127, "Low pass filter" },
		{ "HPF", 8, 0, 127, "High pass filter" } },
		kDistortionPresets, 6 },
	{ "EQ", 0, {}, NULL, 0 },
	{ "DynFilter", 0, {}, NULL, 0 }
};
static const int kFxTypeCount = sizeof(kFxTypes) / sizeof(kFxTypes[0]);
static const int kFxSlots = 3;
static const int kFxSliders = 5;

static const char* kPreparePath = "VoicePar0/OscilSmp/prepare";

// the parameter of the voice are written for "VoicePar0/"; the number of the voice replaces the 0
static std::string
ResolvePath(int part, int voice, const char* suffix)
{
	std::string path(suffix);
	if (path.compare(0, 10, "VoicePar0/") == 0) {
		char number[8];
		snprintf(number, sizeof(number), "%d", voice);
		path = "VoicePar" + std::string(number) + path.substr(9);
	}
	return ZynControl::AdPath(part, path.c_str());
}


static const int32 kNotRead = -1000000;




static int
SliderFromValue(const EditParam& p, double value)
{
	if (p.kind == K_FINE)
		value = (value - 8192.0) / 128.0;
	if (p.kind == K_INT || p.kind == K_FINE)
		return value < p.min ? p.min : (value > p.max ? p.max : (int)floor(value + 0.5));
	if (value < p.lo)
		value = p.lo;
	if (value > p.hi)
		value = p.hi;
	double position = log(value / p.lo) / log(p.hi / p.lo);
	return p.min + (int)floor(position * (p.max - p.min) + 0.5);
}


static double
ValueFromSlider(const EditParam& p, int slider)
{
	if (p.kind == K_INT)
		return slider;
	if (p.kind == K_FINE)
		return 8192.0 + 128.0 * slider;
	double position = (double)(slider - p.min) / (double)(p.max - p.min);
	return p.lo * pow(p.hi / p.lo, position);
}


// a popup menu of choices: every item sends `what` with the number under `key`
static BMenuField*
MakeChoice(const char* name, const char* const* items, int count, uint32 what, const char* key,
	const char* tip)
{
	BPopUpMenu* menu = new BPopUpMenu(name);
	for (int i = 0; i < count; i++) {
		BMessage* m = new BMessage(what);
		m->AddInt32(key, i);
		BMenuItem* item = new BMenuItem(items[i], m);
		if (i == 0)
			item->SetMarked(true);
		menu->AddItem(item);
	}
	menu->SetLabelFromMarked(true);
	BMenuField* field = new BMenuField(name, NULL, menu);
	field->SetToolTip(tip);
	return field;
}


// What the value of a slider means in the units of the sound: seconds, semitones, Hz...
// (the formulas are those of the engine)
static BString
ValueText(const EditParam& p, int slider)
{
	const double value = ValueFromSlider(p, slider);
	char text[64];
	if (strstr(p.path, "_dt") != NULL) {
		// the times of the envelopes: 0 .. 127 is 0 .. 41 s, exponentially
		const double seconds = (pow(2.0, slider / 127.0 * 12.0) - 1.0) / 100.0;
		if (seconds < 1.0)
			snprintf(text, sizeof(text), "%.0f ms", seconds * 1000.0);
		else
			snprintf(text, sizeof(text), "%.1f s", seconds);
	} else if (strstr(p.path, "FreqEnvelope") != NULL && strstr(p.path, "_val") != NULL) {
		// 64 = the pitch of the note
		const double semitones = (slider < 64 ? -1.0 : 1.0) * (pow(2.0, 6.0 * fabs(slider / 64.0 - 1.0)) - 1.0);
		snprintf(text, sizeof(text), "%+.1f semitones", semitones);
	} else if (strstr(p.path, "FilterEnvelope") != NULL && strstr(p.path, "_val") != NULL) {
		// 64 = the cutoff of the filter
		snprintf(text, sizeof(text), "%+.1f octaves", (slider - 64.0) / 64.0 * 6.0);
	} else if (strstr(p.path, "basefreq") != NULL) {
		if (value >= 1000.0)
			snprintf(text, sizeof(text), "%.1f kHz", value / 1000.0);
		else
			snprintf(text, sizeof(text), "%.0f Hz", value);
	} else if (strstr(p.path, "FreqLfo/freq") != NULL) {
		snprintf(text, sizeof(text), "%.2f Hz", value);
	} else if (strstr(p.path, "baseq") != NULL) {
		snprintf(text, sizeof(text), "Q %.1f", value);
	} else if (strstr(p.path, "GlobalPar/octave") != NULL) {
		snprintf(text, sizeof(text), "%+d octaves", slider);
	} else if (strstr(p.path, "coarsedetune") != NULL) {
		snprintf(text, sizeof(text), "%+d semitones", slider);
	} else {
		snprintf(text, sizeof(text), "%d", slider);
	}
	return BString(text);
}


InstrumentEditor::InstrumentEditor(ZynControl* zyn, const BMessenger& owner, uint32 savedWhat)
	:
	BWindow(BRect(120, 100, 700, 420), "Instrument", B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS | B_NOT_ZOOMABLE),
	fZyn(zyn),
	fOwner(owner),
	fSavedWhat(savedWhat),
	fPart(0),
	fWave(NULL),
	fFilterType(NULL),
	fVoiceMenu(NULL),
	fVoiceOn(NULL),
	fVoice(0),
	fLoading(false),
	fReload(false),
	fCancel(false),
	fApplying(false),
	fGeneration(0),
	fLoader(-1),
	fStatus(NULL),
	fSavePanel(NULL),
	fStereo(NULL),
	fFxRunner(NULL)
{
	for (int slot = 0; slot < kFxSlots; slot++) {
		fFx[slot].typeField = NULL;
		fFx[slot].presetField = NULL;
		fFx[slot].type = 0;
		for (int k = 0; k < kFxSliders; k++) {
			fFx[slot].boxes[k] = NULL;
			fFx[slot].sliders[k] = NULL;
			fFx[slot].shown[k] = true;
			fFx[slot].loaded[k] = kNotRead;
		}
	}
	for (int i = 0; i < 40; i++) {
		fSliders[i] = NULL;
		fLoadedValue[i] = kNotRead;
	}
	_Build();
}


InstrumentEditor::~InstrumentEditor()
{
	if (fLoading) {
		fCancel = true;
		status_t unused;
		wait_for_thread(fLoader, &unused);	// the thread ends quickly: it stops at the next question
	}
	delete fSavePanel;
	delete fFxRunner;
}


bool
InstrumentEditor::QuitRequested()
{
	Hide();					// it stays alive: the owner shows it again
	return false;
}


BMessage
InstrumentEditor::MsgSetPart(int part, const char* instrumentName)
{
	BMessage m(MSG_ED_PART);
	m.AddInt32("part", part);
	m.AddString("name", instrumentName ? instrumentName : "");
	return m;
}


void
InstrumentEditor::_Build()
{
	BMenuBar* menuBar = new BMenuBar("menu");
	BMenu* file = new BMenu("File");
	file->AddItem(new BMenuItem("Save instrument as" B_UTF8_ELLIPSIS, new BMessage(MSG_ED_SAVE_AS), 'S'));
	file->AddSeparatorItem();
	file->AddItem(new BMenuItem("Close", new BMessage(B_QUIT_REQUESTED), 'W'));
	menuBar->AddItem(file);

	// one box around every group, inside it one box with the name for each slider
	BGroupView* rows[3];
	BGroupView* outColumn = NULL;
	for (int r = 0; r < 3; r++)
		rows[r] = new BGroupView(B_HORIZONTAL, B_USE_DEFAULT_SPACING);
	const char* lastGroup = NULL;
	BGroupView* groupRow = NULL;
	for (int i = 0; i < kParamCount; i++) {
		const EditParam& p = kParams[i];
		if (lastGroup == NULL || strcmp(lastGroup, p.group) != 0) {
			groupRow = new BGroupView(B_HORIZONTAL, B_USE_SMALL_SPACING);
			BGroupView* groupColumn = new BGroupView(B_VERTICAL, B_USE_SMALL_SPACING);
			groupColumn->GroupLayout()->SetInsets(B_USE_SMALL_INSETS);
			BBox* groupBox = new BBox(p.group, B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE_JUMP,
				B_FANCY_BORDER, groupColumn);
			groupBox->SetLabel(p.group);
			// a menu goes on top of the sliders of a group
			if (strcmp(p.group, "Voice") == 0) {
				// which voice of the instrument this box edits, and whether it plays
				fVoiceMenu = MakeChoice("voiceField", kVoices, kVoiceCount, MSG_ED_VOICE, "voice",
					"The voice of the instrument that this box edits");
				fVoiceOn = new BCheckBox("voiceOn", "On", new BMessage(MSG_ED_VON));
				fVoiceOn->SetToolTip("The voice plays");
				BGroupView* voiceRow = new BGroupView(B_HORIZONTAL, B_USE_SMALL_SPACING);
				voiceRow->GroupLayout()->AddView(fVoiceMenu);
				voiceRow->GroupLayout()->AddView(fVoiceOn);
				groupColumn->GroupLayout()->AddView(voiceRow);
				fWave = MakeChoice("waveField", kWaves, kWaveCount, MSG_ED_WAVE, "wave",
					"Waveform of the oscillator");
				groupColumn->GroupLayout()->AddView(fWave);
			} else if (strcmp(p.group, "Filter") == 0) {
				fFilterType = MakeChoice("filterTypeField", kFilterTypes, kFilterTypeCount, MSG_ED_FTYPE,
					"type", "Type of the filter");
				groupColumn->GroupLayout()->AddView(fFilterType);
			}
			groupColumn->GroupLayout()->AddView(groupRow);
			rows[p.row]->GroupLayout()->AddView(groupBox);
			if (strcmp(p.group, "Out") == 0)
				outColumn = groupColumn;
			lastGroup = p.group;
		}

		BMessage* m = new BMessage(MSG_ED_PARAM);
		m->AddInt32("index", i);
		BSlider* slider = new BSlider(p.label, NULL, m, p.min, p.max, B_VERTICAL);
		slider->SetModificationMessage(new BMessage(*m));
		slider->SetHashMarks(B_HASH_MARKS_NONE);
		slider->SetValue(p.start);
		slider->SetToolTip(p.tip);
		slider->SetExplicitMinSize(BSize(B_SIZE_UNSET, 150));
		slider->SetExplicitMaxSize(BSize(B_SIZE_UNSET, 150));
		fSliders[i] = slider;

		BGroupView* inner = new BGroupView(B_VERTICAL, 0);
		inner->GroupLayout()->SetInsets(B_USE_SMALL_INSETS);
		inner->GroupLayout()->AddView(slider);
		BBox* box = new BBox(p.label, B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE_JUMP,
			B_FANCY_BORDER, inner);
		box->SetLabel(p.label);
		groupRow->GroupLayout()->AddView(box);
	}

	// the Out group: the volume of the ADsynth, and whether it is stereo
	if (outColumn != NULL) {
		fStereo = new BCheckBox("stereo", "Stereo", new BMessage(MSG_ED_STEREO));
		fStereo->SetToolTip("The ADsynth plays in stereo (off: mono)");
		fStereo->SetValue(B_CONTROL_ON);
		outColumn->GroupLayout()->AddView(fStereo);
	}
	_BuildEffects(rows[2]);

	fStatus = new BStringView("status", "");
	fStatus->SetExplicitMinSize(BSize(50, B_SIZE_UNSET));	// a long message must not widen the window

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(rows[0])
			.Add(rows[1])
			.Add(rows[2])
			.Add(fStatus)
		.End();
}


std::string
InstrumentEditor::_Path(const char* suffix) const
{
	return ResolvePath(fPart, fVoice, suffix);
}


std::string
InstrumentEditor::_FxPath(int slot, const char* name, int index) const
{
	char path[96];
	if (index >= 0)
		snprintf(path, sizeof(path), "/part%d/partefx%d/%s%d", fPart, slot, name, index);
	else
		snprintf(path, sizeof(path), "/part%d/partefx%d/%s", fPart, slot, name);
	return path;
}


// the third row: three effect slots, each with the type, a preset and up to five sliders
void
InstrumentEditor::_BuildEffects(BGroupView* row)
{
	for (int slot = 0; slot < kFxSlots; slot++) {
		FxSlot& fx = fFx[slot];
		char title[16];
		snprintf(title, sizeof(title), "Effect %d", slot + 1);

		BPopUpMenu* types = new BPopUpMenu("type");
		for (int t = 0; t < kFxTypeCount; t++) {
			BMessage* m = new BMessage(MSG_ED_FXTYPE);
			m->AddInt32("slot", slot);
			m->AddInt32("type", t);
			BMenuItem* item = new BMenuItem(kFxTypes[t].name, m);
			if (t == 0)
				item->SetMarked(true);
			types->AddItem(item);
		}
		types->SetLabelFromMarked(true);
		fx.typeField = new BMenuField("fxType", NULL, types);
		fx.typeField->SetToolTip("Type of the effect");

		BPopUpMenu* presets = new BPopUpMenu("Preset");
		fx.presetField = new BMenuField("fxPreset", NULL, presets);
		fx.presetField->SetToolTip("Presets of the effect");

		BGroupView* menus = new BGroupView(B_HORIZONTAL, B_USE_SMALL_SPACING);
		menus->GroupLayout()->AddView(fx.typeField);
		menus->GroupLayout()->AddView(fx.presetField);

		BGroupView* sliders = new BGroupView(B_HORIZONTAL, B_USE_SMALL_SPACING);
		for (int k = 0; k < kFxSliders; k++) {
			BMessage* m = new BMessage(MSG_ED_FXPAR);
			m->AddInt32("slot", slot);
			m->AddInt32("k", k);
			BSlider* slider = new BSlider("fxSlider", NULL, m, 0, 127, B_VERTICAL);
			slider->SetModificationMessage(new BMessage(*m));
			slider->SetHashMarks(B_HASH_MARKS_NONE);
			slider->SetExplicitMinSize(BSize(B_SIZE_UNSET, 150));
			slider->SetExplicitMaxSize(BSize(B_SIZE_UNSET, 150));
			fx.sliders[k] = slider;
			BGroupView* inner = new BGroupView(B_VERTICAL, 0);
			inner->GroupLayout()->SetInsets(B_USE_SMALL_INSETS);
			inner->GroupLayout()->AddView(slider);
			fx.boxes[k] = new BBox("fxBox", B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE_JUMP,
				B_FANCY_BORDER, inner);
			fx.boxes[k]->SetLabel("Vol");
			sliders->GroupLayout()->AddView(fx.boxes[k]);
		}

		BGroupView* column = new BGroupView(B_VERTICAL, B_USE_SMALL_SPACING);
		column->GroupLayout()->SetInsets(B_USE_SMALL_INSETS);
		column->GroupLayout()->AddView(menus);
		column->GroupLayout()->AddView(sliders);
		BBox* group = new BBox("fxGroup", B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE_JUMP,
			B_FANCY_BORDER, column);
		group->SetLabel(title);
		row->GroupLayout()->AddView(group);
		_SetFxType(slot, 0);
	}
}


// the menu of presets and the sliders of an effect type (the values come later from the engine)
void
InstrumentEditor::_SetFxType(int slot, int type)
{
	if (type < 0 || type >= kFxTypeCount)
		type = 0;
	FxSlot& fx = fFx[slot];
	fx.type = type;
	const FxType& ft = kFxTypes[type];

	fx.typeField->Menu()->ItemAt(type)->SetMarked(true);

	BMenu* menu = fx.presetField->Menu();
	while (menu->CountItems() > 0)
		delete menu->RemoveItem((int32)0);
	for (int i = 0; i < ft.presetCount; i++) {
		BMessage* m = new BMessage(MSG_ED_FXPRESET);
		m->AddInt32("slot", slot);
		m->AddInt32("preset", i);
		menu->AddItem(new BMenuItem(ft.presets[i], m));
	}
	fx.presetField->SetEnabled(ft.presetCount > 0);

	for (int k = 0; k < kFxSliders; k++) {
		const bool use = k < ft.count;
		if (use) {
			const FxSlider& fs = ft.sliders[k];
			fx.boxes[k]->SetLabel(fs.label);
			fx.sliders[k]->SetLimits(fs.min, fs.max);
			fx.sliders[k]->SetToolTip(fs.tip);
		}
		if (use && !fx.shown[k]) {
			fx.boxes[k]->Show();
			fx.shown[k] = true;
		} else if (!use && fx.shown[k]) {
			fx.boxes[k]->Hide();
			fx.shown[k] = false;
		}
	}
}


void
InstrumentEditor::_SendFxSlider(int slot, int k)
{
	if (slot < 0 || slot >= kFxSlots || k < 0 || k >= kFxSliders)
		return;
	FxSlot& fx = fFx[slot];
	if (k >= kFxTypes[fx.type].count)
		return;
	if (fx.loaded[k] != kNotRead) {
		// the message of a slider that was set from the engine: nothing to write
		const bool same = fx.sliders[k]->Value() == fx.loaded[k];
		fx.loaded[k] = kNotRead;
		if (same)
			return;
	}
	const FxSlider& fs = kFxTypes[fx.type].sliders[k];
	fZyn->SetInt(_FxPath(slot, "parameter", fs.index), fx.sliders[k]->Value());
	char text[96];
	snprintf(text, sizeof(text), "Effect %d, %s: %d   (%s)", slot + 1, fs.label,
		(int)fx.sliders[k]->Value(), fs.tip);
	fStatus->SetText(text);
}


void
InstrumentEditor::ShowPart(int part, const char* instrumentName)
{
	fPart = part < 0 ? 0 : (part > 15 ? 15 : part);
	fName = instrumentName ? instrumentName : "";
	BString title("Instrument: ");
	title << (fName.empty() ? "(empty)" : fName.c_str());
	SetTitle(title.String());
	_Load();
}


struct LoadJob {
	BMessenger	target;
	ZynControl*	zyn;
	int			part;
	int			voice;
	int			generation;
	volatile bool* cancel;
};


// Reads the values from the engine. It runs in a thread of its own: the engine may be slow or
// silent (a disabled voice, for instance), and the window must go on working meanwhile.
// The result goes to the window as a message.
static int32
LoadThread(void* data)
{
	LoadJob* job = (LoadJob*)data;
	ZynControl* zyn = job->zyn;
	BMessage result(MSG_ED_LOADED);
	result.AddInt32("generation", job->generation);

	bool answered = true;
	int v = 0;
	const int timeout = 100;
	// the first question tells whether the engine answers at all
	answered = zyn->GetInt(ResolvePath(job->part, job->voice, kWavePath), &v, timeout);
	result.AddInt32("wave", answered ? v : kNotRead);
	result.AddInt32("voiceon", zyn->GetInt(ResolvePath(job->part, job->voice, "VoicePar0/Enabled"), &v,
		timeout) ? v : kNotRead);
	int filterClass = kNotRead;
	int filterType = kNotRead;
	if (zyn->GetInt(ZynControl::AdPath(job->part, kFilterClassPath), &v, timeout))
		filterClass = v;
	if (zyn->GetInt(ZynControl::AdPath(job->part, kFilterTypePath), &v, timeout))
		filterType = v;
	result.AddInt32("filterclass", filterClass);
	result.AddInt32("filtertype", filterType);

	for (int i = 0; i < kParamCount; i++) {
		int32 slider = kNotRead;
		if (answered && !*job->cancel) {
			const EditParam& p = kParams[i];
			const std::string path = ResolvePath(job->part, job->voice, p.path);
			if (p.kind == K_INT || p.kind == K_FINE) {
				int iv;
				if (zyn->GetInt(path, &iv, timeout))
					slider = SliderFromValue(p, iv);
				else
					answered = false;
			} else {
				float fv;
				if (zyn->GetFloat(path, &fv, timeout))
					slider = SliderFromValue(p, fv);
				else
					answered = false;
			}
		}
		result.AddInt32("slider", slider);
	}
	result.AddBool("answered", answered);

	char padPath[64];
	snprintf(padPath, sizeof(padPath), "/part%d/kit0/Padenabled", job->part);
	result.AddInt32("ad", answered && zyn->GetInt(padPath, &v, timeout) ? v : kNotRead);

	// the effects of the part: the type of every slot and the values of its sliders
	for (int slot = 0; slot < kFxSlots; slot++) {
		char path[96];
		snprintf(path, sizeof(path), "/part%d/partefx%d/efftype", job->part, slot);
		int type = kNotRead;
		if (!*job->cancel && zyn->GetInt(path, &v, timeout))
			type = v;
		result.AddInt32("fxtype", type);
		for (int k = 0; k < kFxSliders; k++) {
			int32 slider = kNotRead;
			if (type > 0 && type < kFxTypeCount && k < kFxTypes[type].count && !*job->cancel) {
				snprintf(path, sizeof(path), "/part%d/partefx%d/parameter%d", job->part, slot,
					kFxTypes[type].sliders[k].index);
				if (zyn->GetInt(path, &v, timeout))
					slider = v;
			}
			result.AddInt32("fxslider", slider);
		}
	}
	int stereo = kNotRead;
	if (answered && zyn->GetInt(ZynControl::AdPath(job->part, "GlobalPar/PStereo"), &v, timeout))
		stereo = v;
	result.AddInt32("stereo", stereo);

	job->target.SendMessage(&result);
	delete job;
	return 0;
}


void
InstrumentEditor::_Load()
{
	if (!fZyn->IsConnected()) {
		fStatus->SetText("The engine is not connected");
		for (int i = 0; i < kParamCount; i++)
			_ShowValue(i, false);
		return;
	}
	if (fLoading) {
		fReload = true;			// start again with the new part or voice when this one is done
		fCancel = true;
		return;
	}
	fCancel = false;
	fLoading = true;
	fStatus->SetText("Reading the values from the engine" B_UTF8_ELLIPSIS);
	LoadJob* job = new LoadJob;
	job->target = BMessenger(this);
	job->zyn = fZyn;
	job->part = fPart;
	job->voice = fVoice;
	job->generation = ++fGeneration;
	job->cancel = &fCancel;
	fLoader = spawn_thread(LoadThread, "SimpleZyn editor load", B_NORMAL_PRIORITY, job);
	if (fLoader < 0) {
		delete job;
		fLoading = false;
		fStatus->SetText("Could not start reading the engine");
		return;
	}
	resume_thread(fLoader);
}


// the values have been read: they go to the sliders, but not back to the engine
void
InstrumentEditor::_ApplyLoaded(const BMessage* result)
{
	int32 value;
	if (fWave != NULL && result->FindInt32("wave", &value) == B_OK && value >= 0 && value < kWaveCount)
		fWave->Menu()->ItemAt(value)->SetMarked(true);
	if (fVoiceOn != NULL && result->FindInt32("voiceon", &value) == B_OK && value != kNotRead) {
		fApplying = true;
		fVoiceOn->SetValue(value ? B_CONTROL_ON : B_CONTROL_OFF);
		fApplying = false;
	}
	int32 filterClass = kNotRead, filterType = kNotRead;
	result->FindInt32("filterclass", &filterClass);
	result->FindInt32("filtertype", &filterType);
	if (fFilterType != NULL && filterClass != kNotRead && filterType != kNotRead) {
		// only the analog class has these types; the menu is for it
		fFilterType->SetEnabled(filterClass == 0);
		if (filterClass == 0 && filterType >= 0 && filterType < kFilterTypeCount)
			fFilterType->Menu()->ItemAt(filterType)->SetMarked(true);
	}
	for (int i = 0; i < kParamCount; i++) {
		if (result->FindInt32("slider", i, &value) == B_OK && value != kNotRead) {
			// the slider may send its message when it is set: that one is not written to the engine
			fLoadedValue[i] = value;
			fSliders[i]->SetValue(value);
		}
		_ShowValue(i, false);
	}
	if (fStereo != NULL && result->FindInt32("stereo", &value) == B_OK && value != kNotRead) {
		fApplying = true;
		fStereo->SetValue(value ? B_CONTROL_ON : B_CONTROL_OFF);
		fApplying = false;
	}
	for (int slot = 0; slot < kFxSlots; slot++) {
		int32 type = kNotRead;
		if (result->FindInt32("fxtype", slot, &type) != B_OK || type == kNotRead)
			continue;
		_SetFxType(slot, type);
		for (int k = 0; k < kFxSliders; k++) {
			int32 v = kNotRead;
			if (result->FindInt32("fxslider", slot * kFxSliders + k, &v) == B_OK && v != kNotRead) {
				fFx[slot].loaded[k] = v;
				fFx[slot].sliders[k]->SetValue(v);
			}
		}
	}
	bool answered = false;
	result->FindBool("answered", &answered);
	if (!answered) {
		fStatus->SetText("The engine did not answer: the sliders show typical values, not the real ones");
		return;
	}
	int32 ad = 1;
	result->FindInt32("ad", &ad);
	if (ad == 0)
		fStatus->SetText("This instrument has no ADsynth: these sliders will not change its sound");
	else
		fStatus->SetText("Values read from the engine");
}


// the description and the value in the units of the sound: in the tool tip, and when the slider
// is moved, in the status line as well
void
InstrumentEditor::_ShowValue(int index, bool inStatus)
{
	if (index < 0 || index >= kParamCount || fSliders[index] == NULL)
		return;
	const EditParam& p = kParams[index];
	const BString value = ValueText(p, fSliders[index]->Value());
	BString tip(p.tip);
	tip << "\n" << value;
	fSliders[index]->SetToolTip(tip.String());
	if (inStatus) {
		BString text(p.group);
		text << " " << p.label << ": " << value << "   (" << p.tip << ")";
		fStatus->SetText(text.String());
	}
}


void
InstrumentEditor::_Send(int index)
{
	if (index < 0 || index >= kParamCount || fSliders[index] == NULL)
		return;
	_ShowValue(index, true);
	if (fLoadedValue[index] != kNotRead) {
		// this message comes from setting the slider to the value that was read: nothing to write
		const bool same = fSliders[index]->Value() == fLoadedValue[index];
		fLoadedValue[index] = kNotRead;
		if (same)
			return;
	}
	const EditParam& p = kParams[index];
	const std::string path = _Path(p.path);
	const double value = ValueFromSlider(p, fSliders[index]->Value());
	if (p.kind == K_INT)
		fZyn->SetInt(path, (int)value);
	else
		fZyn->SetFloat(path, (float)value);
	if (p.oscillator)
		fZyn->Perform(_Path(kPreparePath));
}


void
InstrumentEditor::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MSG_ED_PARAM: {
			int32 index;
			if (message->FindInt32("index", &index) == B_OK)
				_Send(index);
			break;
		}
		case MSG_ED_WAVE: {
			int32 wave;
			if (message->FindInt32("wave", &wave) == B_OK) {
				fZyn->SetInt(_Path(kWavePath), wave);
				fZyn->Perform(_Path(kPreparePath));
			}
			break;
		}
		case MSG_ED_LOADED: {
			fLoading = false;
			if (fReload) {
				fReload = false;
				_Load();				// the part or the voice has changed: this answer is old
			} else {
				_ApplyLoaded(message);
			}
			break;
		}
		case MSG_ED_STEREO:
			if (!fApplying)
				fZyn->SetBool(ZynControl::AdPath(fPart, "GlobalPar/PStereo"), fStereo->Value() == B_CONTROL_ON);
			break;
		case MSG_ED_FXTYPE: {
			int32 slot, type;
			if (message->FindInt32("slot", &slot) == B_OK && message->FindInt32("type", &type) == B_OK
				&& slot >= 0 && slot < kFxSlots) {
				fZyn->SetInt(_FxPath(slot, "efftype"), type);
				_SetFxType(slot, type);
				// the engine needs a moment to make the effect; then its values are read
				delete fFxRunner;
				fFxRunner = new BMessageRunner(BMessenger(this), new BMessage(MSG_ED_FXRELOAD), 400000, 1);
			}
			break;
		}
		case MSG_ED_FXPRESET: {
			int32 slot, preset;
			if (message->FindInt32("slot", &slot) == B_OK && message->FindInt32("preset", &preset) == B_OK
				&& slot >= 0 && slot < kFxSlots) {
				fZyn->SetInt(_FxPath(slot, "preset"), preset);
				delete fFxRunner;
				fFxRunner = new BMessageRunner(BMessenger(this), new BMessage(MSG_ED_FXRELOAD), 400000, 1);
			}
			break;
		}
		case MSG_ED_FXPAR: {
			int32 slot, k;
			if (message->FindInt32("slot", &slot) == B_OK && message->FindInt32("k", &k) == B_OK)
				_SendFxSlider(slot, k);
			break;
		}
		case MSG_ED_FXRELOAD:
			_Load();
			break;
		case MSG_ED_VOICE: {
			int32 voice;
			if (message->FindInt32("voice", &voice) == B_OK && voice >= 0 && voice < kVoiceCount) {
				fVoice = voice;
				_Load();
			}
			break;
		}
		case MSG_ED_VON:
			if (!fApplying)
				fZyn->SetBool(_Path("VoicePar0/Enabled"), fVoiceOn->Value() == B_CONTROL_ON);
			break;
		case MSG_ED_FTYPE: {
			int32 type;
			if (message->FindInt32("type", &type) == B_OK)
				fZyn->SetInt(ZynControl::AdPath(fPart, kFilterTypePath), type);
			break;
		}
		case MSG_ED_PART: {
			int32 part;
			const char* name = "";
			message->FindString("name", &name);
			if (message->FindInt32("part", &part) == B_OK)
				ShowPart(part, name);
			break;
		}
		case MSG_ED_SAVE_AS: {
			if (fSavePanel == NULL) {
				BPath dir;
				if (find_directory(B_USER_DIRECTORY, &dir) == B_OK) {
					dir.Append("SimpleZyn/Banks/My");
					create_directory(dir.Path(), 0755);
				}
				entry_ref ref;
				if (get_ref_for_path(dir.Path(), &ref) == B_OK) {
					fSavePanel = new BFilePanel(B_SAVE_PANEL, new BMessenger(this), &ref, B_FILE_NODE,
						false, new BMessage(MSG_ED_SAVE));
				} else {
					fSavePanel = new BFilePanel(B_SAVE_PANEL, new BMessenger(this), NULL, B_FILE_NODE,
						false, new BMessage(MSG_ED_SAVE));
				}
			}
			BString name(fName.empty() ? "Instrument" : fName.c_str());
			name << ".xiz";
			fSavePanel->SetSaveText(name.String());
			fSavePanel->Show();
			break;
		}
		case MSG_ED_SAVE:
			_Save(message);
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
InstrumentEditor::_Save(const BMessage* message)
{
	entry_ref dirRef;
	const char* name;
	if (message->FindRef("directory", &dirRef) != B_OK || message->FindString("name", &name) != B_OK)
		return;
	BPath path(&dirRef);
	BString file(name);
	if (file.Length() < 4 || strcasecmp(file.String() + file.Length() - 4, ".xiz") != 0)
		file << ".xiz";
	path.Append(file.String());
	fZyn->SaveInstrument(fPart, path.Path());
	BString text("Saved: ");
	text << path.Path();
	fStatus->SetText(text.String());
	BMessage saved(fSavedWhat);
	fOwner.SendMessage(&saved);
}
