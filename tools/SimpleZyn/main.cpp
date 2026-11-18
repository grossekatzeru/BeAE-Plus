// SimpleZyn - a simple native control panel for the ZynAddSubFX synthesizer engine.
// Pick instruments from the banks, set up 16 parts (channel, volume, pan, mute, solo),
// add some reverb, record the sound to a WAV file and open it in BeAE-Plus.
// The engine runs without its own window; SimpleZyn talks to it over OSC.

#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <CheckBox.h>
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

enum {
	MSG_POLL = 'poll',
	MSG_BANK = 'bank',
	MSG_SEARCH = 'srch',
	MSG_LOAD = 'load',
	MSG_TARGET = 'targ',
	MSG_ENABLE = 'enab',
	MSG_VOLUME = 'vol_',
	MSG_PAN = 'pan_',
	MSG_CHANNEL = 'chan',
	MSG_MUTE = 'mute',
	MSG_SOLO = 'solo',
	MSG_MASTER = 'mast',
	MSG_REVERB = 'rvb_',
	MSG_RECORD = 'rec_',
	MSG_OPEN_BEAE = 'obea',
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

struct PartStrip {
	BRadioButton*	target;
	BCheckBox*		enable;
	BStringView*	name;
	BMenuField*		channel;
	BSlider*		volume;
	BSlider*		pan;
	BCheckBox*		mute;
	BCheckBox*		solo;
	std::string		loadedFile;		// instrument loaded into the part (loaded again after an engine restart)
};


class MainWindow : public BWindow {
public:
	MainWindow()
		:
		BWindow(BRect(80, 80, 1080, 720), "SimpleZyn", B_TITLED_WINDOW,
			B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
		fSelectedPart(0),
		fRecording(false),
		fRunner(NULL),
		fOpenPanel(NULL),
		fFallbackTried(false),
		fConnected(false)
	{
		_BuildInterface();

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
		switch (message->what) {
			case MSG_POLL:
				_Poll();
				break;
			case MSG_BANK:
				_ShowInstruments();
				break;
			case MSG_SEARCH:
				_ShowInstruments();
				break;
			case MSG_LOAD:
				_LoadSelected();
				break;
			case MSG_TARGET: {
				int32 part;
				if (message->FindInt32("part", &part) == B_OK)
					_SelectPart(part);
				break;
			}
			case MSG_ENABLE: {
				int32 part;
				if (message->FindInt32("part", &part) == B_OK)
					fZyn.SetEnabled(part, fStrips[part].enable->Value() == B_CONTROL_ON);
				break;
			}
			case MSG_VOLUME: {
				int32 part;
				if (message->FindInt32("part", &part) == B_OK)
					fZyn.SetVolume(part, fStrips[part].volume->Value());
				break;
			}
			case MSG_PAN: {
				int32 part;
				if (message->FindInt32("part", &part) == B_OK)
					fZyn.SetPan(part, fStrips[part].pan->Value());
				break;
			}
			case MSG_CHANNEL: {
				int32 part, channel;
				if (message->FindInt32("part", &part) == B_OK && message->FindInt32("channel", &channel) == B_OK)
					fZyn.SetChannel(part, channel);
				break;
			}
			case MSG_MUTE:
			case MSG_SOLO:
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
		file->AddSeparatorItem();
		file->AddItem(new BMenuItem("Quit", new BMessage(B_QUIT_REQUESTED), 'Q'));
		menuBar->AddItem(file);
		BMenu* engine = new BMenu("Engine");
		engine->AddItem(new BMenuItem("Restart engine", new BMessage(MSG_RESTART)));
		menuBar->AddItem(engine);

		// master volume and reverb
		fMaster = _NewSlider("master", "Master", MSG_MASTER, 80);
		fReverb = _NewSlider("reverb", "Reverb", MSG_REVERB, 0);

		// banks and instruments
		fSearch = new BTextControl("search", "Search:", "", new BMessage(MSG_SEARCH));
		fSearch->SetModificationMessage(new BMessage(MSG_SEARCH));
		fBanks = new BListView("banks");
		fBanks->SetSelectionMessage(new BMessage(MSG_BANK));
		fInstruments = new BListView("instruments");
		fInstruments->SetInvocationMessage(new BMessage(MSG_LOAD));
		BScrollView* banksScroll = new BScrollView("banksScroll", fBanks, 0, false, true);
		BScrollView* instrumentsScroll = new BScrollView("instrumentsScroll", fInstruments, 0, false, true);
		fHint = new BStringView("hint", "Double-click an instrument to load it into the selected part");

		// the 16 parts
		BGroupView* partsView = new BGroupView(B_VERTICAL, 2);
		for (int i = 0; i < ZYN_PARTS; i++)
			fStrips[i] = _NewStrip(i, partsView);

		// recording
		fRecordButton = new BButton("record", "Record", new BMessage(MSG_RECORD));
		fOpenButton = new BButton("openbeae", "Open in BeAE-Plus", new BMessage(MSG_OPEN_BEAE));
		fOpenButton->SetEnabled(false);
		fStatus = new BStringView("status", "Engine: starting" B_UTF8_ELLIPSIS);

		BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
			.Add(menuBar)
			.AddGroup(B_VERTICAL, B_USE_DEFAULT_SPACING)
				.SetInsets(B_USE_WINDOW_INSETS)
				.AddGroup(B_HORIZONTAL)
					.Add(fMaster)
					.Add(fReverb)
				.End()
				.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
					.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING, 1.0f)
						.Add(fSearch)
						.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
							.Add(banksScroll, 1.0f)
							.Add(instrumentsScroll, 2.0f)
						.End()
						.Add(fHint)
					.End()
					.Add(partsView, 2.0f)
				.End()
				.AddGroup(B_HORIZONTAL)
					.Add(fRecordButton)
					.Add(fOpenButton)
					.Add(fStatus)
					.AddGlue()
				.End()
			.End();

		_SelectPart(0);
		fStrips[0].enable->SetValue(B_CONTROL_ON);
	}

	BSlider* _NewSlider(const char* name, const char* label, uint32 what, int32 value)
	{
		BSlider* slider = new BSlider(name, label, new BMessage(what), 0, 127, B_HORIZONTAL);
		slider->SetValue(value);
		slider->SetModificationMessage(new BMessage(what));
		slider->SetHashMarks(B_HASH_MARKS_NONE);
		return slider;
	}

	PartStrip _NewStrip(int part, BGroupView* parent)
	{
		PartStrip s;
		BString label;
		label << (part + 1);

		BMessage* targetMessage = new BMessage(MSG_TARGET);
		targetMessage->AddInt32("part", part);
		s.target = new BRadioButton("target", label.String(), targetMessage);

		BMessage* enableMessage = new BMessage(MSG_ENABLE);
		enableMessage->AddInt32("part", part);
		s.enable = new BCheckBox("enable", "On", enableMessage);

		s.name = new BStringView("name", "(empty)");
		s.name->SetExplicitMinSize(BSize(150, B_SIZE_UNSET));

		BPopUpMenu* channelMenu = new BPopUpMenu("channel");
		for (int ch = 0; ch < 16; ch++) {
			BString text;
			text << "Ch " << (ch + 1);
			BMessage* m = new BMessage(MSG_CHANNEL);
			m->AddInt32("part", part);
			m->AddInt32("channel", ch);
			BMenuItem* item = new BMenuItem(text.String(), m);
			if (ch == part % 16)
				item->SetMarked(true);
			channelMenu->AddItem(item);
		}
		s.channel = new BMenuField("channelField", NULL, channelMenu);

		BMessage* volumeMessage = new BMessage(MSG_VOLUME);
		volumeMessage->AddInt32("part", part);
		s.volume = new BSlider("volume", NULL, volumeMessage, 0, 127, B_HORIZONTAL);
		s.volume->SetValue(96);
		BMessage* volumeMod = new BMessage(MSG_VOLUME);
		volumeMod->AddInt32("part", part);
		s.volume->SetModificationMessage(volumeMod);
		s.volume->SetHashMarks(B_HASH_MARKS_NONE);
		s.volume->SetBarThickness(6);

		BMessage* panMessage = new BMessage(MSG_PAN);
		panMessage->AddInt32("part", part);
		s.pan = new BSlider("pan", NULL, panMessage, 0, 127, B_HORIZONTAL);
		s.pan->SetValue(64);
		BMessage* panMod = new BMessage(MSG_PAN);
		panMod->AddInt32("part", part);
		s.pan->SetModificationMessage(panMod);
		s.pan->SetHashMarks(B_HASH_MARKS_NONE);
		s.pan->SetBarThickness(6);

		BMessage* muteMessage = new BMessage(MSG_MUTE);
		muteMessage->AddInt32("part", part);
		s.mute = new BCheckBox("mute", "M", muteMessage);
		BMessage* soloMessage = new BMessage(MSG_SOLO);
		soloMessage->AddInt32("part", part);
		s.solo = new BCheckBox("solo", "S", soloMessage);

		BLayoutBuilder::Group<>(parent, B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(s.target)
			.Add(s.enable)
			.Add(s.name)
			.Add(s.channel)
			.Add(s.volume, 2.0f)
			.Add(s.pan, 1.0f)
			.Add(s.mute)
			.Add(s.solo);
		return s;
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

	void _SendAllState()
	{
		fZyn.SetMasterVolume(fMaster->Value());
		fZyn.SetReverb(fReverb->Value());
		for (int i = 0; i < ZYN_PARTS; i++) {
			fZyn.SetEnabled(i, fStrips[i].enable->Value() == B_CONTROL_ON);
			fZyn.SetVolume(i, fStrips[i].volume->Value());
			fZyn.SetPan(i, fStrips[i].pan->Value());
			fZyn.SetChannel(i, i % 16);
			// the instruments already chosen keep their names in the strips; load them again
			if (!fStrips[i].loadedFile.empty())
				fZyn.LoadInstrument(i, fStrips[i].loadedFile);
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
			fHint->SetText("No banks found. File \xE2\x86\x92 Add banks folder" B_UTF8_ELLIPSIS);
		else
			fHint->SetText("Double-click an instrument to load it into the selected part");
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
		fStrips[fSelectedPart].name->SetText(ins->name.c_str());
		fStrips[fSelectedPart].enable->SetValue(B_CONTROL_ON);
		fStrips[fSelectedPart].loadedFile = ins->file;
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

	void _SelectPart(int part)
	{
		fSelectedPart = part;
		for (int i = 0; i < ZYN_PARTS; i++)
			fStrips[i].target->SetValue(i == part ? B_CONTROL_ON : B_CONTROL_OFF);
	}

	void _ApplyMuteAndSolo()
	{
		bool anySolo = false;
		for (int i = 0; i < ZYN_PARTS; i++) {
			if (fStrips[i].solo->Value() == B_CONTROL_ON)
				anySolo = true;
		}
		for (int i = 0; i < ZYN_PARTS; i++) {
			bool muted = fStrips[i].mute->Value() == B_CONTROL_ON;
			if (anySolo && fStrips[i].solo->Value() != B_CONTROL_ON)
				muted = true;
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
			char name[64];
			strftime(name, sizeof(name), "SimpleZyn-%Y%m%d-%H%M%S.wav", &t);
			dir.Append(name);
			fRecordPath = dir.Path();

			fZyn.RecordStart(fRecordPath);
			fRecording = true;
			fRecordButton->SetLabel("Stop");
			fOpenButton->SetEnabled(false);
			BString text("Recording starts with the first note: ");
			text << fRecordPath.c_str();
			fStatus->SetText(text.String());
		} else {
			fZyn.RecordStop();
			fRecording = false;
			fRecordButton->SetLabel("Record");
			fOpenButton->SetEnabled(true);
			BString text("Saved: ");
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

	BSlider*					fMaster;
	BSlider*					fReverb;
	BTextControl*				fSearch;
	BListView*					fBanks;
	BListView*					fInstruments;
	BStringView*				fHint;
	BButton*					fRecordButton;
	BButton*					fOpenButton;
	BStringView*				fStatus;
	PartStrip					fStrips[ZYN_PARTS];

	int							fSelectedPart;
	bool						fRecording;
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
