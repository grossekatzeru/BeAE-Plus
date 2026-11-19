// SimpleZyn - a simple native control panel for the ZynAddSubFX synthesizer engine.
// Pick instruments from the banks, set up 16 parts (channel, volume, pan, mute, solo),
// add some reverb, record the sound to a WAV file and open it in BeAE-Plus.
// The engine runs without its own window; SimpleZyn talks to it over OSC.

#include <Alert.h>
#include <Application.h>
#include <List.h>
#include <Messenger.h>
#include <Box.h>
#include <Button.h>
#include <CheckBox.h>
#include <ControlLook.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <RadioButton.h>
#include <Roster.h>
#include <ScrollView.h>
#include <Slider.h>
#include <StringItem.h>
#include <StringView.h>
#include <String.h>
#include <TextControl.h>
#include <Window.h>

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <string>
#include <vector>

#include "Banks.h"
#include "Engine.h"
#include "ZynControl.h"

static const char* kAppSignature = "application/x-vnd.SimpleZyn";
static const char* kBeAESignature = "application/x-vnd.BeAE-BeAE";
// reverb presets of the engine (their order is the engine's) and the type each one sets
static const int kReverbPresetCount = 13;
static const char* const kReverbPresets[kReverbPresetCount] = {
	"Cathedral 1", "Cathedral 2", "Cathedral 3", "Hall 1", "Hall 2", "Room 1", "Room 2",
	"Basement", "Tunnel", "Echoed 1", "Echoed 2", "Very long 1", "Very long 2"
};
static const int kReverbPresetType[kReverbPresetCount] = { 1, 0, 1, 1, 1, 0, 1, 0, 1, 1, 1, 0, 1 };
static const uint32 kAddFileTrack = 'adTr';	// BeAE-Plus: add the files of "refs" as new tracks

enum {
	MSG_POLL = 'poll',
	MSG_BANK = 'bank',
	MSG_SEARCH = 'srch',
	MSG_LOAD = 'load',
	MSG_TARGET = 'targ',
	MSG_VOLUME = 'vol_',
	MSG_PAN = 'pan_',
	MSG_CHANNEL = 'chan',
	MSG_MUTE = 'mute',
	MSG_SOLO = 'solo',
	MSG_MASTER = 'mast',
	MSG_REVERB = 'rvb_',
	MSG_RECORD = 'rec_',
	MSG_OPEN_BEAE = 'obea',
	MSG_NEWRP = 'nrpt',
	MSG_REV_PRESET = 'rvpr',
	MSG_REV_TYPE = 'rvty',
	MSG_ADD_FOLDER = 'addf',
	MSG_FOLDER_CHOSEN = 'fcho',
	MSG_RESCAN = 'resc',
	MSG_RESTART = 'rest'
};


// ---------------------------------------------------------------- settings

static BString
SettingsPath()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return BString("");
	path.Append("SimpleZyn-folders");
	return BString(path.Path());
}


static void
LoadFolders(std::vector<std::string>& folders)
{
	BString file = SettingsPath();
	if (file.Length() == 0)
		return;
	FILE* f = fopen(file.String(), "r");
	if (f == NULL)
		return;
	char line[1024];
	while (fgets(line, sizeof(line), f) != NULL) {
		size_t n = strlen(line);
		while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
			line[--n] = 0;
		if (n > 0)
			folders.push_back(line);
	}
	fclose(f);
}


static void
SaveFolders(const std::vector<std::string>& folders)
{
	BString file = SettingsPath();
	if (file.Length() == 0)
		return;
	FILE* f = fopen(file.String(), "w");
	if (f == NULL)
		return;
	for (size_t i = 0; i < folders.size(); i++)
		fprintf(f, "%s\n", folders[i].c_str());
	fclose(f);
}


static void
DefaultBankRoots(std::vector<std::string>& roots)
{
	BPath path;
	if (find_directory(B_SYSTEM_DATA_DIRECTORY, &path) == B_OK) {
		path.Append("zynaddsubfx/banks");
		roots.push_back(path.Path());
	}
	if (find_directory(B_SYSTEM_NONPACKAGED_DATA_DIRECTORY, &path) == B_OK) {
		path.Append("zynaddsubfx/banks");
		roots.push_back(path.Path());
	}
	if (find_directory(B_USER_NONPACKAGED_DATA_DIRECTORY, &path) == B_OK) {
		path.Append("zynaddsubfx/banks");
		roots.push_back(path.Path());
	}
}


// ---------------------------------------------------------------- window

struct PartState {
	PartState() : enabled(false), volume(96), pan(64), channel(0), mute(false), solo(false) {}

	bool		enabled;
	int			volume;
	int			pan;
	int			channel;		// 0..15
	bool		mute;
	bool		solo;
	std::string	name;			// instrument name
	std::string	file;			// instrument file (loaded again after an engine restart)
};


class MainWindow : public BWindow {
public:
	MainWindow()
		:
		BWindow(BRect(80, 60, 780, 480), "SimpleZyn", B_TITLED_WINDOW,
			B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
		fSelectedPart(0),
		fRecording(false),
		fFinishTries(0),
		fRevPreset(-1),
		fRevType(-1),
		fRunner(NULL),
		fOpenPanel(NULL),
		fFallbackTried(false),
		fConnected(false)
	{
		for (int i = 0; i < ZYN_PARTS; i++)
			fParts[i].channel = i % 16;			// like in the engine: part N listens to channel N
		fParts[0].enabled = true;				// and only the first part is on
		_BuildInterface();
		_ShowPart();

		LoadFolders(fUserFolders);
		_ScanBanks();

		_StartEngine("zynaddsubfx");
		fRunner = new BMessageRunner(BMessenger(this), new BMessage(MSG_POLL), 200000);
	}

	~MainWindow()
	{
		delete fRunner;
		delete fOpenPanel;
	}

	virtual bool QuitRequested()
	{
		if (fRecording)
			fZyn.RecordStop();
		be_app->PostMessage(B_QUIT_REQUESTED);
		return true;
	}

	virtual void MessageReceived(BMessage* message)
	{
		PartState& p = fParts[fSelectedPart];
		switch (message->what) {
			case MSG_POLL:
				_Poll();
				_CheckRecordFinished();
				break;
			case MSG_BANK:
			case MSG_SEARCH:
				_ShowInstruments();
				break;
			case MSG_LOAD:
				_LoadSelected();
				break;
			case MSG_TARGET: {
				int32 part;
				if (message->FindInt32("part", &part) == B_OK && part >= 0 && part < ZYN_PARTS) {
					fSelectedPart = part;
					_ShowPart();
				}
				break;
			}
			case MSG_VOLUME:
				p.volume = fVolume->Value();
				fZyn.SetVolume(fSelectedPart, p.volume);
				break;
			case MSG_PAN:
				p.pan = fPan->Value();
				fZyn.SetPan(fSelectedPart, p.pan);
				break;
			case MSG_CHANNEL: {
				int32 channel;
				if (message->FindInt32("channel", &channel) == B_OK) {
					p.channel = channel;
					fZyn.SetChannel(fSelectedPart, channel);
				}
				break;
			}
			case MSG_MUTE:
				p.mute = fMute->Value() == B_CONTROL_ON;
				_ApplyMuteAndSolo();
				break;
			case MSG_SOLO:
				p.solo = fSolo->Value() == B_CONTROL_ON;
				_ApplyMuteAndSolo();
				break;
			case MSG_MASTER:
				fZyn.SetMasterVolume(fMaster->Value());
				break;
			case MSG_REVERB:
				fZyn.SetReverb(fReverb->Value());
				break;
			case MSG_RECORD:
				_ToggleRecord();
				break;
			case MSG_REV_PRESET: {
				int32 preset;
				if (message->FindInt32("preset", &preset) == B_OK && preset >= 0 && preset < kReverbPresetCount) {
					fRevPreset = preset;
					fRevType = kReverbPresetType[preset];	// a preset sets the type as well
					fZyn.SetReverbPreset(preset);
					_MarkReverbMenus();
				}
				break;
			}
			case MSG_REV_TYPE: {
				int32 type;
				if (message->FindInt32("type", &type) == B_OK && type >= 0 && type < 3) {
					fRevType = type;
					fZyn.SetReverbType(type);
					_MarkReverbMenus();
				}
				break;
			}
			case MSG_NEWRP:
				fNewRPItem->SetMarked(!fNewRPItem->IsMarked());
				break;
			case MSG_OPEN_BEAE:
				_OpenInBeAE();
				break;
			case MSG_ADD_FOLDER:
				_ChooseFolder();
				break;
			case MSG_FOLDER_CHOSEN: {
				entry_ref ref;
				if (message->FindRef("refs", &ref) == B_OK) {
					BPath path(&ref);
					if (path.InitCheck() == B_OK) {
						fUserFolders.push_back(path.Path());
						SaveFolders(fUserFolders);
						_ScanBanks();
					}
				}
				break;
			}
			case MSG_RESCAN:
				_ScanBanks();
				break;
			case MSG_RESTART:
				fFallbackTried = false;
				_StartEngine("zynaddsubfx");
				break;
			default:
				BWindow::MessageReceived(message);
				break;
		}
	}

private:
	// ---- interface

	void _BuildInterface()
	{
		BMenuBar* menuBar = new BMenuBar("menu");
		BMenu* file = new BMenu("File");
		file->AddItem(new BMenuItem("Add banks folder" B_UTF8_ELLIPSIS, new BMessage(MSG_ADD_FOLDER)));
		file->AddItem(new BMenuItem("Rescan banks", new BMessage(MSG_RESCAN)));
		fOpenItem = new BMenuItem("Send last recording to BeAE-Plus", new BMessage(MSG_OPEN_BEAE));
		fOpenItem->SetEnabled(false);
		file->AddItem(fOpenItem);
		file->AddSeparatorItem();
		file->AddItem(new BMenuItem("Quit", new BMessage(B_QUIT_REQUESTED), 'Q'));
		menuBar->AddItem(file);
		BMenu* engine = new BMenu("Engine");
		engine->AddItem(new BMenuItem("Restart engine", new BMessage(MSG_RESTART)));
		fNewRPItem = new BMenuItem("NewRP: send every recording to BeAE-Plus", new BMessage(MSG_NEWRP));
		engine->AddItem(fNewRPItem);
		menuBar->AddItem(engine);

		// the reverb: a preset (it sets all the reverb parameters) and its type
		BMenu* reverbMenu = new BMenu("Reverb");
		fRevPresetMenu = new BMenu("Preset");
		for (int i = 0; i < kReverbPresetCount; i++) {
			BMessage* m = new BMessage(MSG_REV_PRESET);
			m->AddInt32("preset", i);
			fRevPresetMenu->AddItem(new BMenuItem(kReverbPresets[i], m));
		}
		reverbMenu->AddItem(fRevPresetMenu);
		fRevTypeMenu = new BMenu("Type");
		static const char* const kTypes[3] = { "Random", "Freeverb", "Bandwidth" };
		for (int i = 0; i < 3; i++) {
			BMessage* m = new BMessage(MSG_REV_TYPE);
			m->AddInt32("type", i);
			fRevTypeMenu->AddItem(new BMenuItem(kTypes[i], m));
		}
		reverbMenu->AddItem(fRevTypeMenu);
		menuBar->AddItem(reverbMenu);

		// ---- box "Part": a MIDI channel (part) with its instrument
		BPopUpMenu* partMenu = new BPopUpMenu("part");
		for (int i = 0; i < ZYN_PARTS; i++) {
			BString text;
			text << (i + 1);
			BMessage* m = new BMessage(MSG_TARGET);
			m->AddInt32("part", i);
			BMenuItem* item = new BMenuItem(text.String(), m);
			if (i == 0)
				item->SetMarked(true);
			partMenu->AddItem(item);
		}
		partMenu->SetLabelFromMarked(true);
		fPartMenu = new BMenuField("partField", "Part", partMenu);
		fPartMenu->SetToolTip("Part (1-16)");

		fSearch = new BTextControl("search", "Search", "", new BMessage(MSG_SEARCH));
		fSearch->SetModificationMessage(new BMessage(MSG_SEARCH));
		fSearch->SetToolTip("Search instruments in all banks");
		fBanks = new BListView("banks");
		fBanks->SetSelectionMessage(new BMessage(MSG_BANK));
		fInstruments = new BListView("instruments");
		fInstruments->SetInvocationMessage(new BMessage(MSG_LOAD));
		fInstruments->SetToolTip("Double-click an instrument to load it into the part");
		BScrollView* banksScroll = new BScrollView("banksScroll", fBanks, 0, false, true);
		BScrollView* instrumentsScroll = new BScrollView("instrumentsScroll", fInstruments, 0, false, true);
		banksScroll->SetExplicitMinSize(BSize(110, 150));
		instrumentsScroll->SetExplicitMinSize(BSize(150, 150));

		fVolume = new BSlider("volume", NULL, new BMessage(MSG_VOLUME), 0, 127, B_VERTICAL);
		fVolume->SetModificationMessage(new BMessage(MSG_VOLUME));
		fVolume->SetHashMarks(B_HASH_MARKS_NONE);
		fVolume->SetToolTip("Part volume");
		fPan = new BSlider("pan", NULL, new BMessage(MSG_PAN), 0, 127, B_VERTICAL);
		fPan->SetModificationMessage(new BMessage(MSG_PAN));
		fPan->SetHashMarks(B_HASH_MARKS_NONE);
		fPan->SetToolTip("Panning (the middle is the centre)");

		BPopUpMenu* channelMenu = new BPopUpMenu("channel");
		for (int ch = 0; ch < 16; ch++) {
			BString text;
			text << (ch + 1);
			BMessage* m = new BMessage(MSG_CHANNEL);
			m->AddInt32("channel", ch);
			BMenuItem* item = new BMenuItem(text.String(), m);
			if (ch == 0)
				item->SetMarked(true);
			channelMenu->AddItem(item);
		}
		channelMenu->SetLabelFromMarked(true);
		fChannel = new BMenuField("channelField", "Ch", channelMenu);
		fChannel->SetToolTip("MIDI channel the part listens to");

		fMute = new BCheckBox("mute", "M", new BMessage(MSG_MUTE));
		fMute->SetToolTip("Mute: the part ignores new notes");
		fSolo = new BCheckBox("solo", "S", new BMessage(MSG_SOLO));
		fSolo->SetToolTip("Solo");

		// the labels of "Part" and "Search" have the same width, so both fields start at one line
		const float labelWidth = be_plain_font->StringWidth("Search") + 10;
		fPartMenu->SetDivider(labelWidth);
		fSearch->SetDivider(labelWidth);

		// every slider has its own box, the box carries the name
		BBox* volumeBox = new BBox("volumeBox", B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE_JUMP,
			B_FANCY_BORDER, BLayoutBuilder::Group<>(B_VERTICAL, 0)
				.SetInsets(B_USE_SMALL_INSETS).Add(fVolume).View());
		volumeBox->SetLabel("Vol");
		BBox* panBox = new BBox("panBox", B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE_JUMP,
			B_FANCY_BORDER, BLayoutBuilder::Group<>(B_VERTICAL, 0)
				.SetInsets(B_USE_SMALL_INSETS).Add(fPan).View());
		panBox->SetLabel("Pan");
		BGroupView* slidersView = new BGroupView(B_HORIZONTAL, B_USE_SMALL_SPACING);
		slidersView->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH, B_ALIGN_TOP));
		BLayoutBuilder::Group<>(slidersView)
			.Add(volumeBox)
			.Add(panBox);
		BGroupView* listsView = new BGroupView(B_HORIZONTAL, B_USE_SMALL_SPACING);
		BLayoutBuilder::Group<>(listsView)
			.Add(banksScroll, 1.0f)
			.Add(instrumentsScroll, 2.0f);
		BGroupView* lowerView = new BGroupView(B_VERTICAL, B_USE_SMALL_SPACING);
		BGroupView* muteView = new BGroupView(B_HORIZONTAL, B_USE_SMALL_SPACING);
		BLayoutBuilder::Group<>(muteView)
			.Add(fMute)
			.Add(fSolo);
		BLayoutBuilder::Group<>(lowerView)
			.Add(fChannel)
			.Add(muteView);

		// two columns: on the left Part, Search and the lists, on the right the sliders with Ch, M, S;
		// both start at the top, the lists end at the level of the M and S boxes
		BGroupView* leftColumn = new BGroupView(B_VERTICAL, B_USE_SMALL_SPACING);
		BLayoutBuilder::Group<>(leftColumn)
			.Add(fPartMenu)
			.Add(fSearch)
			.Add(listsView, 1.0f);
		BGroupView* rightColumn = new BGroupView(B_VERTICAL, B_USE_SMALL_SPACING);
		BLayoutBuilder::Group<>(rightColumn)
			.Add(slidersView)
			.AddGlue()
			.Add(lowerView);
		BView* partGrid = BLayoutBuilder::Group<>(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.SetInsets(B_USE_SMALL_INSETS)
			.Add(leftColumn, 1.0f)
			.Add(rightColumn, 0.0f)
			.View();
		BView* partContent = partGrid;
		fPartBox = new BBox("part", B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE_JUMP,
			B_FANCY_BORDER, partContent);
		fPartBox->SetLabel("Inst");

		// ---- box "System": things that belong to the whole synthesizer
		fMaster = _NewSlider("master", "Vol", MSG_MASTER, 80);
		fMaster->SetToolTip("Master volume");
		fReverb = _NewSlider("reverb", "Rev", MSG_REVERB, 0);
		fReverb->SetToolTip("Reverb: how much of every part goes into the reverb");
		fRecordButton = new BButton("record", "Rec", new BMessage(MSG_RECORD));
		fRecordButton->SetToolTip("Record the sound to a WAV file (starts with the first note)");
		fRestartButton = new BButton("restart", "Rst", new BMessage(MSG_RESTART));
		fRestartButton->SetToolTip("Restart the synthesizer engine");

		// the sliders take the height, the buttons keep their own height
		// the same height for all four sliders; the System sliders start at the same level
		// as the Vol and Pan sliders of the Part box (the top of the box content)
		const float kSliderHeight = 150;
		BSlider* allSliders[] = { fMaster, fReverb, fVolume, fPan };
		for (int i = 0; i < 4; i++) {
			allSliders[i]->SetExplicitMinSize(BSize(B_SIZE_UNSET, kSliderHeight));
			allSliders[i]->SetExplicitMaxSize(BSize(B_SIZE_UNSET, kSliderHeight));
		}
		const float buttonHeight = be_plain_font->Size() * 2.6f;
		fRecordButton->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, buttonHeight));
		fRestartButton->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, buttonHeight));

		BBox* masterBox = new BBox("masterBox", B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE_JUMP,
			B_FANCY_BORDER, BLayoutBuilder::Group<>(B_VERTICAL, 0)
				.SetInsets(B_USE_SMALL_INSETS).Add(fMaster).View());
		masterBox->SetLabel("Vol");
		BBox* reverbBox = new BBox("reverbBox", B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE_JUMP,
			B_FANCY_BORDER, BLayoutBuilder::Group<>(B_VERTICAL, 0)
				.SetInsets(B_USE_SMALL_INSETS).Add(fReverb).View());
		reverbBox->SetLabel("Rev");

		BView* systemContent = BLayoutBuilder::Group<>(B_VERTICAL, B_USE_SMALL_SPACING)
			.SetInsets(B_USE_SMALL_INSETS)
			.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING, 0.0f)
				.Add(masterBox)
				.Add(reverbBox)
			.End()
			.AddGlue()
			.Add(fRestartButton, 0.0f)
			.Add(fRecordButton, 0.0f)
			.View();
		BBox* systemBox = new BBox("system", B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE_JUMP,
			B_FANCY_BORDER, systemContent);
		systemBox->SetLabel("System");

		fStatus = new BStringView("status", "Engine: starting" B_UTF8_ELLIPSIS);
		fStatus->SetExplicitMinSize(BSize(50, B_SIZE_UNSET));	// long messages must not widen the window

		BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
			.Add(menuBar)
			.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
				.SetInsets(B_USE_WINDOW_INSETS)
				.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
					.Add(systemBox, 0.0f)
					.Add(fPartBox, 1.0f)
				.End()
				.Add(fStatus)
			.End();
	}

	BSlider* _NewSlider(const char* name, const char* label, uint32 what, int32 value)
	{
		BSlider* slider = new BSlider(name, NULL, new BMessage(what), 0, 127, B_VERTICAL);
		slider->SetValue(value);
		slider->SetModificationMessage(new BMessage(what));
		slider->SetHashMarks(B_HASH_MARKS_NONE);
		return slider;
	}

	// the controls show the part that is selected
	void _ShowPart()
	{
		const PartState& p = fParts[fSelectedPart];
		fPartMenu->Menu()->ItemAt(fSelectedPart)->SetMarked(true);
		BString title("Instrument: ");
		title << (p.name.empty() ? "(empty)" : p.name.c_str());
		fPartBox->SetLabel(title.String());
		fVolume->SetValue(p.volume);
		fPan->SetValue(p.pan);
		fChannel->Menu()->ItemAt(p.channel)->SetMarked(true);
		fMute->SetValue(p.mute ? B_CONTROL_ON : B_CONTROL_OFF);
		fSolo->SetValue(p.solo ? B_CONTROL_ON : B_CONTROL_OFF);
	}

	// ---- engine

	void _StartEngine(const char* program)
	{
		fZyn.Disconnect();
		fConnected = false;
		if (!fEngine.Start(program)) {
			fStatus->SetText("Engine: cannot start");
			return;
		}
		BString text("Engine: starting ");
		text << program << B_UTF8_ELLIPSIS;
		fStatus->SetText(text.String());
	}

	void _Poll()
	{
		if (!fConnected && fEngine.IsRunning()) {
			if (fEngine.Poll() && fEngine.HasPort()) {
				if (fZyn.Connect(fEngine.Port())) {
					fConnected = true;
					fZyn.Setup();
					_SendAllState();
					BString text("Engine: running, OSC port ");
					text << (int32)fEngine.Port() << ". MIDI: connect a keyboard to \"ZynAddSubFX\"";
					fStatus->SetText(text.String());
				}
			}
		} else if (fConnected) {
			fEngine.Poll();									// keep copying the engine's messages to the Terminal
		}

		if (fEngine.HasExited()) {
			fConnected = false;
			fZyn.Disconnect();
			if (!fFallbackTried) {
				fFallbackTried = true;
				_StartEngine("/boot/system/apps/ZynAddSubFX/zynaddsubfx");
				return;
			}
			fStatus->SetText("Engine stopped");
			(new BAlert("engine", "The ZynAddSubFX engine could not be started or has stopped.\n"
				"Install ZynAddSubFX (the \"zynaddsubfx\" command must work in a Terminal), "
				"then choose Engine \xE2\x86\x92 Restart engine.", "OK"))->Go(NULL);
		}
	}

	void _MarkReverbMenus()
	{
		for (int i = 0; i < kReverbPresetCount; i++)
			fRevPresetMenu->ItemAt(i)->SetMarked(i == fRevPreset);
		for (int i = 0; i < 3; i++)
			fRevTypeMenu->ItemAt(i)->SetMarked(i == fRevType);
	}

	void _SendAllState()
	{
		fZyn.SetMasterVolume(fMaster->Value());
		fZyn.SetReverb(fReverb->Value());
		if (fRevPreset >= 0)
			fZyn.SetReverbPreset(fRevPreset);				// first the preset, it sets the type too
		if (fRevType >= 0)
			fZyn.SetReverbType(fRevType);
		for (int i = 0; i < ZYN_PARTS; i++) {
			const PartState& p = fParts[i];
			fZyn.SetEnabled(i, p.enabled);
			fZyn.SetVolume(i, p.volume);
			fZyn.SetPan(i, p.pan);
			fZyn.SetChannel(i, p.channel);
			if (!p.file.empty())
				fZyn.LoadInstrument(i, p.file);
		}
		_ApplyMuteAndSolo();
	}

	// ---- banks

	void _ScanBanks()
	{
		std::vector<std::string> roots;
		DefaultBankRoots(roots);
		for (size_t i = 0; i < fUserFolders.size(); i++)
			roots.push_back(fUserFolders[i]);
		ScanBanks(roots, fBankData);

		fBanks->MakeEmpty();
		for (size_t i = 0; i < fBankData.size(); i++) {
			BString text(fBankData[i].name.c_str());
			text << "  (" << (int32)fBankData[i].instruments.size() << ")";
			fBanks->AddItem(new BStringItem(text.String()));
		}
		if (fBankData.empty())
			fStatus->SetText("No banks found. File \xE2\x86\x92 Add banks folder" B_UTF8_ELLIPSIS);
		if (!fBankData.empty())
			fBanks->Select(0);
		_ShowInstruments();
	}

	void _ShowInstruments()
	{
		fInstruments->MakeEmpty();
		fShown.clear();
		const std::string pattern(fSearch->Text());

		if (pattern.empty()) {
			int32 bank = fBanks->CurrentSelection();
			if (bank < 0 || bank >= (int32)fBankData.size())
				return;
			const Bank& b = fBankData[bank];
			for (size_t i = 0; i < b.instruments.size(); i++) {
				fShown.push_back(&b.instruments[i]);
				fInstruments->AddItem(new BStringItem(b.instruments[i].name.c_str()));
			}
		} else {
			// search in all banks
			for (size_t k = 0; k < fBankData.size(); k++) {
				const Bank& b = fBankData[k];
				for (size_t i = 0; i < b.instruments.size(); i++) {
					if (!NameMatches(b.instruments[i].name, pattern))
						continue;
					fShown.push_back(&b.instruments[i]);
					BString text(b.name.c_str());
					text << " / " << b.instruments[i].name.c_str();
					fInstruments->AddItem(new BStringItem(text.String()));
				}
			}
		}
	}

	void _LoadSelected()
	{
		int32 at = fInstruments->CurrentSelection();
		if (at < 0 || at >= (int32)fShown.size())
			return;
		const Instrument* ins = fShown[at];
		PartState& p = fParts[fSelectedPart];
		p.name = ins->name;
		p.file = ins->file;
		p.enabled = true;
		_ShowPart();
		if (fConnected)
			fZyn.LoadInstrument(fSelectedPart, ins->file);
		else
			fStatus->SetText("Engine is not ready yet; the instrument will be loaded when it starts");
	}

	void _ChooseFolder()
	{
		if (fOpenPanel == NULL) {
			BMessenger me(this);
			fOpenPanel = new BFilePanel(B_OPEN_PANEL, &me, NULL, B_DIRECTORY_NODE, false,
				new BMessage(MSG_FOLDER_CHOSEN));
		}
		fOpenPanel->Show();
	}

	// ---- parts

	void _ApplyMuteAndSolo()
	{
		bool anySolo = false;
		for (int i = 0; i < ZYN_PARTS; i++) {
			if (fParts[i].solo)
				anySolo = true;
		}
		for (int i = 0; i < ZYN_PARTS; i++) {
			bool muted = fParts[i].mute || (anySolo && !fParts[i].solo);
			fZyn.SetMuted(i, muted);
		}
	}

	// ---- recording

	void _ToggleRecord()
	{
		if (!fConnected) {
			fStatus->SetText("Engine is not ready");
			return;
		}
		if (!fRecording) {
			BPath dir;
			if (find_directory(B_USER_DIRECTORY, &dir) != B_OK)
				return;
			dir.Append("SimpleZyn");
			create_directory(dir.Path(), 0755);

			time_t now = time(NULL);
			struct tm t;
			localtime_r(&now, &t);
			// file name: instrument of the selected part - day.hour.minutes.seconds
			std::string base(fParts[fSelectedPart].name);
			for (size_t i = 0; i < base.size(); i++) {
				if (base[i] == '/' || base[i] == ':' || (unsigned char)base[i] < 32)
					base[i] = '_';
			}
			if (base.empty())
				base = "SimpleZyn";
			char stamp[32];
			strftime(stamp, sizeof(stamp), "%d.%H.%M.%S", &t);
			BString name(base.c_str());
			name << "-" << stamp << ".wav";
			dir.Append(name.String());
			fRecordPath = dir.Path();

			fZyn.RecordStart(fRecordPath);
			fRecording = true;
			fRecordButton->SetLabel("End");
			fOpenItem->SetEnabled(false);
			BString text("Recording starts with the first note: ");
			text << fRecordPath.c_str();
			fStatus->SetText(text.String());
		} else {
			fZyn.RecordStop();
			fZyn.RecordFinish();
			fRecording = false;
			fRecordButton->SetLabel("Rec");
			fFinishTries = 25;		// the file is checked every 200 ms, see _CheckRecordFinished()
			BString text("Finishing: ");
			text << fRecordPath.c_str();
			fStatus->SetText(text.String());
		}
	}

	// the engine closes the WAV file (and writes its header) a moment after the stop
	void _CheckRecordFinished()
	{
		if (fFinishTries <= 0)
			return;
		fFinishTries--;

		bool finished = false;
		off_t size = 0;
		FILE* f = fopen(fRecordPath.c_str(), "rb");
		if (f != NULL) {
			char head[12];
			if (fread(head, 1, 12, f) == 12 && memcmp(head, "RIFF", 4) == 0
				&& memcmp(head + 8, "WAVE", 4) == 0) {
				finished = true;
			}
			fseek(f, 0, SEEK_END);
			size = ftell(f);
			fclose(f);
		}

		if (finished) {
			fFinishTries = 0;
			fOpenItem->SetEnabled(true);
			BString text("Saved: ");
			text << fRecordPath.c_str();
			fStatus->SetText(text.String());
			if (fNewRPItem->IsMarked())
				_OpenInBeAE();
		} else if (fFinishTries == 0) {
			BString text;
			if (size <= 44)
				text << "Nothing was recorded (play a note after Rec): ";
			else
				text << "The engine did not finish the file: ";
			text << fRecordPath.c_str();
			fStatus->SetText(text.String());
		}
	}

	void _OpenInBeAE()
	{
		if (fRecordPath.empty())
			return;
		entry_ref ref;
		if (get_ref_for_path(fRecordPath.c_str(), &ref) != B_OK) {
			fStatus->SetText("The recording file was not found (no note was played?)");
			return;
		}
		// BeAE-Plus is already running: the file goes to a new track of its open project
		// (BeAE-Plus 3.2 and newer; an older one ignores the message)
		team_id running = -1;
		BList teams;
		be_roster->GetAppList(kBeAESignature, &teams);
		if (teams.CountItems() > 0)
			running = (team_id)(addr_t)teams.ItemAt(teams.CountItems() - 1);
		if (running >= 0) {
			BMessage add(kAddFileTrack);
			add.AddRef("refs", &ref);
			BMessenger(kBeAESignature, running).SendMessage(&add);
			fStatus->SetText("Sent to the open BeAE-Plus project (a new track)");
			return;
		}

		BMessage refs(B_REFS_RECEIVED);
		refs.AddRef("refs", &ref);
		status_t err = be_roster->Launch(kBeAESignature, &refs);
		if (err != B_OK && err != B_ALREADY_RUNNING) {
			BString text("Cannot start BeAE-Plus: ");
			text << strerror(err);
			fStatus->SetText(text.String());
		}
	}

	Engine						fEngine;
	ZynControl					fZyn;
	std::vector<std::string>	fUserFolders;
	std::vector<Bank>			fBankData;
	std::vector<const Instrument*>	fShown;
	PartState					fParts[ZYN_PARTS];

	BSlider*					fMaster;
	BSlider*					fReverb;
	BTextControl*				fSearch;
	BListView*					fBanks;
	BListView*					fInstruments;
	BMenuField*					fPartMenu;
	BBox*						fPartBox;
	BSlider*					fVolume;
	BSlider*					fPan;
	BMenuField*					fChannel;
	BCheckBox*					fMute;
	BCheckBox*					fSolo;
	BButton*					fRecordButton;
	BButton*					fRestartButton;
	BMenuItem*					fOpenItem;
	BMenuItem*					fNewRPItem;
	BStringView*				fStatus;

	int							fSelectedPart;
	bool						fRecording;
	int							fFinishTries;
	int							fRevPreset;		// -1: not chosen, the engine's own
	int							fRevType;
	BMenu*						fRevPresetMenu;
	BMenu*						fRevTypeMenu;
	std::string					fRecordPath;
	BMessageRunner*				fRunner;
	BFilePanel*					fOpenPanel;
	bool						fFallbackTried;
	bool						fConnected;
};


class SimpleZynApp : public BApplication {
public:
	SimpleZynApp()
		:
		BApplication(kAppSignature)
	{
	}

	virtual void ReadyToRun()
	{
		(new MainWindow())->Show();
	}
};


int
main()
{
	SimpleZynApp app;
	app.Run();
	return 0;
}
