#include "InstrumentEditor.h"

#include <Box.h>
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
	MSG_ED_PART		= 'edpt',		// show another part: "part", "name"
	MSG_ED_SAVE_AS	= 'edsa',		// menu: save the instrument
	MSG_ED_SAVE		= 'edsv'		// the file panel is done
};


// How a slider (always 0..127, or min..max) becomes a parameter of the engine
enum Kind { K_INT, K_LOG };

struct EditParam {
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
	{ "Amp", "Att", "GlobalPar/AmpEnvelope/PA_dt", K_INT, 0, 127, 0, 0, 0, false, "Attack time" },
	{ "Amp", "Dec", "GlobalPar/AmpEnvelope/PD_dt", K_INT, 0, 127, 0, 0, 40, false, "Decay time" },
	{ "Amp", "Sus", "GlobalPar/AmpEnvelope/PS_val", K_INT, 0, 127, 0, 0, 127, false, "Sustain level" },
	{ "Amp", "Rel", "GlobalPar/AmpEnvelope/PR_dt", K_INT, 0, 127, 0, 0, 40, false, "Release time" },
	{ "Filter", "Cut", "GlobalPar/GlobalFilter/basefreq", K_LOG, 0, 127, 31.25, 32000.0, 127, false,
		"Filter cutoff frequency" },
	{ "Filter", "Res", "GlobalPar/GlobalFilter/baseq", K_LOG, 0, 127, 0.3, 30.0, 40, false,
		"Filter resonance (Q)" },
	{ "Osc", "Shp", "VoicePar0/OscilSmp/Pbasefuncpar", K_INT, 0, 127, 0, 0, 64, true,
		"Shape of the waveform (what it does depends on the waveform)" },
	{ "Osc", "Uni", "VoicePar0/Unison_size", K_INT, 1, 8, 0, 0, 1, false,
		"Unison: how many copies of the voice play together" },
	{ "Osc", "Spr", "VoicePar0/Unison_frequency_spread", K_INT, 0, 127, 0, 0, 60, false,
		"Unison: how far the copies are detuned" },
	{ "LFO", "Dep", "GlobalPar/FreqLfo/Pintensity", K_INT, 0, 127, 0, 0, 0, false, "Vibrato depth" },
	{ "LFO", "Spd", "GlobalPar/FreqLfo/freq", K_LOG, 0, 127, 0.0776, 85.25, 60, false, "Vibrato speed" },
	{ "Out", "Vol", "GlobalPar/PVolume", K_INT, 0, 127, 0, 0, 90, false, "Volume of the instrument" }
};
static const int kParamCount = sizeof(kParams) / sizeof(kParams[0]);

static const char* const kWaves[] = {
	"Sine", "Triangle", "Pulse", "Saw", "Power", "Gauss", "Diode", "Abs sine", "Pulse sine",
	"Stretch sine", "Chirp", "Abs stretch sine", "Chebyshev", "Square", "Spike", "Circle", "Power sinus"
};
static const int kWaveCount = sizeof(kWaves) / sizeof(kWaves[0]);

static const char* kWavePath = "VoicePar0/OscilSmp/Pcurrentbasefunc";
static const char* kPreparePath = "VoicePar0/OscilSmp/prepare";


static int
SliderFromValue(const EditParam& p, double value)
{
	if (p.kind == K_INT)
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
	double position = (double)(slider - p.min) / (double)(p.max - p.min);
	return p.lo * pow(p.hi / p.lo, position);
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
	fStatus(NULL),
	fSavePanel(NULL)
{
	for (int i = 0; i < 16; i++)
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
	BGroupView* row = new BGroupView(B_HORIZONTAL, B_USE_DEFAULT_SPACING);
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
			if (strcmp(p.group, "Osc") == 0) {
				// the waveform menu goes on top of the sliders of the oscillator
				BPopUpMenu* waves = new BPopUpMenu("wave");
				for (int w = 0; w < kWaveCount; w++) {
					BMessage* m = new BMessage(MSG_ED_WAVE);
					m->AddInt32("wave", w);
					BMenuItem* item = new BMenuItem(kWaves[w], m);
					if (w == 0)
						item->SetMarked(true);
					waves->AddItem(item);
				}
				waves->SetLabelFromMarked(true);
				fWave = new BMenuField("waveField", NULL, waves);
				fWave->SetToolTip("Waveform of the oscillator");
				groupColumn->GroupLayout()->AddView(fWave);
			}
			groupColumn->GroupLayout()->AddView(groupRow);
			row->GroupLayout()->AddView(groupBox);
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
			.Add(row)
			.Add(fStatus)
		.End();
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
		return;
	}
	bool answered = true;
	int wave = 0;
	if (fZyn->GetInt(ZynControl::AdPath(fPart, kWavePath), &wave)) {
		if (fWave != NULL && wave >= 0 && wave < kWaveCount)
			fWave->Menu()->ItemAt(wave)->SetMarked(true);
	} else {
		answered = false;
	}
	for (int i = 0; i < kParamCount && answered; i++) {
		const EditParam& p = kParams[i];
		const std::string path = ZynControl::AdPath(fPart, p.path);
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


void
InstrumentEditor::_Send(int index)
{
	if (index < 0 || index >= kParamCount || fSliders[index] == NULL)
		return;
	const EditParam& p = kParams[index];
	const std::string path = ZynControl::AdPath(fPart, p.path);
	const double value = ValueFromSlider(p, fSliders[index]->Value());
	if (p.kind == K_INT)
		fZyn->SetInt(path, (int)value);
	else
		fZyn->SetFloat(path, (float)value);
	if (p.oscillator)
		fZyn->Perform(ZynControl::AdPath(fPart, kPreparePath));
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
				fZyn->SetInt(ZynControl::AdPath(fPart, kWavePath), wave);
				fZyn->Perform(ZynControl::AdPath(fPart, kPreparePath));
			}
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
