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
#include <PopUpMenu.h>
#include <Slider.h>
#include <String.h>
#include <StringView.h>

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
		"Pitch envelope: release time" }
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
static const char* kPreparePath = "VoicePar0/OscilSmp/prepare";


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
	fStatus(NULL),
	fSavePanel(NULL)
{
	for (int i = 0; i < 40; i++)
		fSliders[i] = NULL;
	_Build();
}


InstrumentEditor::~InstrumentEditor()
{
	delete fSavePanel;
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
	BGroupView* rows[2];
	for (int r = 0; r < 2; r++)
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

	fStatus = new BStringView("status", "");
	fStatus->SetExplicitMinSize(BSize(50, B_SIZE_UNSET));	// a long message must not widen the window

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(rows[0])
			.Add(rows[1])
			.Add(fStatus)
		.End();
}


// the parameters of the voice are written for "VoicePar0/"; the selected voice replaces the 0
std::string
InstrumentEditor::_Path(const char* suffix) const
{
	std::string path(suffix);
	if (path.compare(0, 10, "VoicePar0/") == 0) {
		char number[8];
		snprintf(number, sizeof(number), "%d", fVoice);
		path = "VoicePar" + std::string(number) + path.substr(9);
	}
	return ZynControl::AdPath(fPart, path.c_str());
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


// reads the values from the engine
void
InstrumentEditor::_Load()
{
	if (!fZyn->IsConnected()) {
		fStatus->SetText("The engine is not connected");
		for (int i = 0; i < kParamCount; i++)
			_ShowValue(i, false);
		return;
	}
	bool answered = true;
	int wave = 0;
	if (fZyn->GetInt(_Path(kWavePath), &wave)) {
		if (fWave != NULL && wave >= 0 && wave < kWaveCount)
			fWave->Menu()->ItemAt(wave)->SetMarked(true);
	} else {
		answered = false;
	}
	int voiceOn = 1;
	char enabledPath[64];
	snprintf(enabledPath, sizeof(enabledPath), "VoicePar0/Enabled");
	if (fVoiceOn != NULL && fZyn->GetInt(_Path(enabledPath), &voiceOn))
		fVoiceOn->SetValue(voiceOn ? B_CONTROL_ON : B_CONTROL_OFF);
	int filterClass = 0;
	int filterType = 0;
	if (fFilterType != NULL && fZyn->GetInt(ZynControl::AdPath(fPart, kFilterClassPath), &filterClass)
		&& fZyn->GetInt(ZynControl::AdPath(fPart, kFilterTypePath), &filterType)) {
		// only the analog class has these types; the menu is for it
		fFilterType->SetEnabled(filterClass == 0);
		if (filterClass == 0 && filterType >= 0 && filterType < kFilterTypeCount)
			fFilterType->Menu()->ItemAt(filterType)->SetMarked(true);
	}
	for (int i = 0; i < kParamCount && answered; i++) {
		const EditParam& p = kParams[i];
		const std::string path = _Path(p.path);
		if (p.kind == K_INT) {
			int v;
			if (fZyn->GetInt(path, &v))
				fSliders[i]->SetValue(SliderFromValue(p, v));
			else
				answered = false;
		} else {
			float v;
			if (fZyn->GetFloat(path, &v))
				fSliders[i]->SetValue(SliderFromValue(p, v));
			else
				answered = false;
		}
	}
	for (int i = 0; i < kParamCount; i++)
		_ShowValue(i, false);
	if (!answered) {
		fStatus->SetText("The engine did not answer: the sliders show typical values, not the real ones");
		return;
	}
	int ad = 1;
	char path[64];
	snprintf(path, sizeof(path), "/part%d/kit0/Padenabled", fPart);
	if (fZyn->GetInt(path, &ad) && ad == 0)
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
		case MSG_ED_VOICE: {
			int32 voice;
			if (message->FindInt32("voice", &voice) == B_OK && voice >= 0 && voice < kVoiceCount) {
				fVoice = voice;
				_Load();
			}
			break;
		}
		case MSG_ED_VON:
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
