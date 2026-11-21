#include "MidiPlayer.h"

#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <CheckBox.h>
#include <Entry.h>
#include <File.h>
#include <FilePanel.h>
#include <LayoutBuilder.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <Node.h>
#include <fs_attr.h>
#include <TypeConstants.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <MidiConsumer.h>
#include <MidiRoster.h>
#include <PopUpMenu.h>
#include <Slider.h>
#include <StringView.h>
#include <View.h>

#include <stdio.h>
#include <string.h>

#include "SongPlayer.h"
#include "ZynControl.h"

enum {
	MSG_MP_OPEN		= 'mpop',
	MSG_MP_PLAY		= 'mppl',
	MSG_MP_STOP		= 'mpst',
	MSG_MP_PANIC	= 'mppa',
	MSG_MP_DRAG		= 'mpdr',
	MSG_MP_SEEK		= 'mpsk',
	MSG_MP_TEMPO	= 'mptp',
	MSG_MP_LOOP		= 'mplp',
	MSG_MP_TICK		= 'mptk',
	MSG_MP_ROUTE	= 'mprt',		// "channel", "route"
	MSG_MP_SWITCH	= 'mpsw',		// mute or solo changed
	MSG_MP_VOLUME	= 'mpvl',		// "channel": the volume slider of a row
	MSG_MP_FORGET	= 'mpfg',
	MSG_MP_INSTR	= 'mpin'		// the button of the instrument: "channel"
};

static const int32 kSliderMax = 1000;
static const bigtime_t kFlash = 350000;		// how long the activity light fades


static const char* const kGmNames[128] = {
	"Acoustic Grand Piano", "Bright Acoustic Piano", "Electric Grand Piano", "Honky-tonk Piano",
	"Electric Piano 1", "Electric Piano 2", "Harpsichord", "Clavinet",
	"Celesta", "Glockenspiel", "Music Box", "Vibraphone", "Marimba", "Xylophone", "Tubular Bells", "Dulcimer",
	"Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ", "Reed Organ", "Accordion",
	"Harmonica", "Tango Accordion",
	"Nylon Guitar", "Steel Guitar", "Jazz Guitar", "Clean Guitar", "Muted Guitar", "Overdriven Guitar",
	"Distortion Guitar", "Guitar Harmonics",
	"Acoustic Bass", "Finger Bass", "Pick Bass", "Fretless Bass", "Slap Bass 1", "Slap Bass 2",
	"Synth Bass 1", "Synth Bass 2",
	"Violin", "Viola", "Cello", "Contrabass", "Tremolo Strings", "Pizzicato Strings", "Harp", "Timpani",
	"String Ensemble 1", "String Ensemble 2", "Synth Strings 1", "Synth Strings 2", "Choir Aahs",
	"Voice Oohs", "Synth Voice", "Orchestra Hit",
	"Trumpet", "Trombone", "Tuba", "Muted Trumpet", "French Horn", "Brass Section", "Synth Brass 1",
	"Synth Brass 2",
	"Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax", "Oboe", "English Horn", "Bassoon", "Clarinet",
	"Piccolo", "Flute", "Recorder", "Pan Flute", "Blown Bottle", "Shakuhachi", "Whistle", "Ocarina",
	"Square Lead", "Saw Lead", "Calliope Lead", "Chiff Lead", "Charang Lead", "Voice Lead", "Fifths Lead",
	"Bass + Lead",
	"New Age Pad", "Warm Pad", "Polysynth Pad", "Choir Pad", "Bowed Pad", "Metallic Pad", "Halo Pad",
	"Sweep Pad",
	"Rain", "Soundtrack", "Crystal", "Atmosphere", "Brightness", "Goblins", "Echoes", "Sci-fi",
	"Sitar", "Banjo", "Shamisen", "Koto", "Kalimba", "Bagpipe", "Fiddle", "Shanai",
	"Tinkle Bell", "Agogo", "Steel Drums", "Woodblock", "Taiko Drum", "Melodic Tom", "Synth Drum",
	"Reverse Cymbal",
	"Guitar Fret Noise", "Breath Noise", "Seashore", "Bird Tweet", "Telephone Ring", "Helicopter",
	"Applause", "Gunshot"
};


// a small light: how recently a note started on the channel
class ActivityView : public BView {
public:
	ActivityView()
		:
		BView("activity", B_WILL_DRAW),
		fLevel(0)
	{
		SetExplicitMinSize(BSize(40, 14));
		SetExplicitMaxSize(BSize(40, 14));
		SetExplicitPreferredSize(BSize(40, 14));
		SetViewColor(B_TRANSPARENT_COLOR);
	}

	void SetLevel(int level)		// 0..8
	{
		if (level != fLevel) {
			fLevel = level;
			Invalidate();
		}
	}

	virtual void Draw(BRect)
	{
		BRect r = Bounds();
		SetHighColor(ui_color(B_PANEL_BACKGROUND_COLOR));
		FillRect(r);
		SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_DARKEN_2_TINT));
		StrokeRect(r);
		SetHighColor(ui_color(B_SUCCESS_COLOR));
		for (int i = 0; i < fLevel; i++)
			FillRect(BRect(2 + i * 4.5f, 2, 2 + i * 4.5f + 3, r.bottom - 2));
	}

private:
	int		fLevel;
};


static void
FormatTime(int64 us, char* out, size_t size)
{
	int64 s = us / 1000000;
	snprintf(out, size, "%d:%02d", (int)(s / 60), (int)(s % 60));
}


MidiPlayerWindow::MidiPlayerWindow(ZynControl* zyn, const std::vector<Bank>& banks)
	:
	BWindow(BRect(140, 120, 760, 520), "MIDI player", B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS | B_NOT_ZOOMABLE),
	fZyn(zyn),
	fPlayer(new SongPlayer()),
	fPlayerOk(false),
	fBanks(banks),
	fPanel(NULL),
	fRunner(NULL),
	fTicks(0),
	fDestId(-1),
	fDragging(false),
	fLoaded(false),
	fDirty(false),
	fDirtyTime(0)
{
	fPlayerOk = fPlayer->Start();
	_Build();
	_RefreshDestination();
	fRunner = new BMessageRunner(BMessenger(this), new BMessage(MSG_MP_TICK), 50000);
}


bool
MidiPlayerWindow::QuitRequested()
{
	_SaveMap();
	fPlayer->Stop();						// the window only hides itself
	Hide();
	return false;
}


void
MidiPlayerWindow::Quit()
{
	_SaveMap();
	delete fRunner;
	fRunner = NULL;
	delete fPanel;
	fPanel = NULL;
	fPlayer->Shutdown();
	fPlayer->Release();
	BWindow::Quit();
}


void
MidiPlayerWindow::SetBanks(const std::vector<Bank>& banks)
{
	fBanks = banks;
}


void
MidiPlayerWindow::_Build()
{
	fFile = new BStringView("file", "Open a .mid file (or drop it on the window)");
	BButton* open = new BButton("open", "Open" B_UTF8_ELLIPSIS, new BMessage(MSG_MP_OPEN));
	fPlay = new BButton("play", "Play", new BMessage(MSG_MP_PLAY));
	fStopButton = new BButton("stop", "Stop", new BMessage(MSG_MP_STOP));
	BButton* panic = new BButton("panic", "Silence", new BMessage(MSG_MP_PANIC));
	fPlay->SetEnabled(false);
	fStopButton->SetEnabled(false);

	fPos = new BSlider("pos", NULL, new BMessage(MSG_MP_SEEK), 0, kSliderMax, B_HORIZONTAL);
	fPos->SetModificationMessage(new BMessage(MSG_MP_DRAG));
	fPos->SetHashMarks(B_HASH_MARKS_NONE);
	fPos->SetEnabled(false);
	fTime = new BStringView("time", "0:00 / 0:00");
	fTempo = new BSlider("tempo", "Tempo: 100%", new BMessage(MSG_MP_TEMPO), 25, 200, B_HORIZONTAL);
	fTempo->SetModificationMessage(new BMessage(MSG_MP_TEMPO));
	fTempo->SetValue(100);
	fLoop = new BCheckBox("loop", "Repeat", new BMessage(MSG_MP_LOOP));
	fStatus = new BStringView("status", "");
	fStatus->SetExplicitMinSize(BSize(50, B_SIZE_UNSET));

	// the table: a row for every channel, the unused ones are hidden
	fTable = new BGroupView(B_VERTICAL, B_USE_SMALL_SPACING);
	for (int ch = 0; ch < 16; ch++) {
		Row& row = fRows[ch];
		char text[8];
		snprintf(text, sizeof(text), "%d", ch + 1);
		row.channel = new BStringView("ch", text);
		row.channel->SetExplicitMinSize(BSize(24, B_SIZE_UNSET));
		row.name = new BStringView("name", "");
		row.name->SetExplicitMinSize(BSize(190, B_SIZE_UNSET));
		row.activity = new ActivityView();

		BPopUpMenu* menu = new BPopUpMenu("route");
		const char* kRouteNames[kRoutes] = { "Zyn", "GM synth", "off" };
		for (int r = 0; r < kRoutes; r++) {
			BMessage* m = new BMessage(MSG_MP_ROUTE);
			m->AddInt32("channel", ch);
			m->AddInt32("route", r);
			BMenuItem* item = new BMenuItem(kRouteNames[r], m);
			if (r == 0)
				item->SetMarked(true);
			menu->AddItem(item);
		}
		menu->SetLabelFromMarked(true);
		row.route = new BMenuField("routeField", NULL, menu);
		row.routeIndex = 0;

		BMessage* im = new BMessage(MSG_MP_INSTR);
		im->AddInt32("channel", ch);
		row.instrument = new BButton("instrument", "(as it is in Zyn)", im);
		// the button takes all the space between the menu and the slider, whatever its label is
		row.instrument->SetExplicitMinSize(BSize(210, B_SIZE_UNSET));
		row.instrument->SetExplicitPreferredSize(BSize(210, B_SIZE_UNSET));
		row.instrument->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
		row.instrument->SetToolTip("Choose the instrument of the synthesizer for this channel");

		BMessage* vm = new BMessage(MSG_MP_VOLUME);
		vm->AddInt32("channel", ch);
		row.volume = new BSlider("volume", NULL, vm, 0, 127, B_HORIZONTAL);
		row.volume->SetModificationMessage(new BMessage(*vm));
		row.volume->SetHashMarks(B_HASH_MARKS_NONE);
		row.volume->SetValue(96);
		row.volume->SetExplicitMinSize(BSize(110, B_SIZE_UNSET));
		row.volume->SetExplicitMaxSize(BSize(110, B_SIZE_UNSET));
		row.volume->SetToolTip("Volume of the channel (the volume of the Zyn part)");

		BMessage* mm = new BMessage(MSG_MP_SWITCH);
		row.mute = new BCheckBox("mute", "M", mm);
		row.mute->SetToolTip("Mute the channel");
		row.solo = new BCheckBox("solo", "S", new BMessage(MSG_MP_SWITCH));
		row.solo->SetToolTip("Play only the channels with S");
		row.used = false;
		row.pendingGm = false;
		row.savedGmVolume = -1;

		BGroupView* line = new BGroupView(B_HORIZONTAL, B_USE_SMALL_SPACING);
		line->GroupLayout()->AddView(row.channel);
		line->GroupLayout()->AddView(row.name);
		line->GroupLayout()->AddView(row.activity);
		line->GroupLayout()->AddView(row.route);
		line->GroupLayout()->AddView(row.instrument, 1.0f);
		line->GroupLayout()->AddView(row.volume);
		line->GroupLayout()->AddView(row.mute);
		line->GroupLayout()->AddView(row.solo);
		line->SetName("line");
		line->Hide();
		fTable->GroupLayout()->AddView(line);
	}

	BMenuBar* menuBar = new BMenuBar("menu");
	BMenu* fileMenu = new BMenu("File");
	fileMenu->AddItem(new BMenuItem("Open" B_UTF8_ELLIPSIS, new BMessage(MSG_MP_OPEN), 'O'));
	fileMenu->AddSeparatorItem();
	fileMenu->AddItem(new BMenuItem("Close", new BMessage(B_QUIT_REQUESTED), 'W'));
	menuBar->AddItem(fileMenu);
	BMenu* mapMenu = new BMenu("Mapping");
	mapMenu->AddItem(new BMenuItem("Forget the saved mapping of this file", new BMessage(MSG_MP_FORGET)));
	menuBar->AddItem(mapMenu);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(open)
			.Add(fPlay)
			.Add(fStopButton)
			.Add(panic)
			.Add(fLoop)
			.AddGlue()
		.End()
		.Add(fFile)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(fPos)
			.Add(fTime)
		.End()
		.Add(fTempo)
		.Add(fTable)
		.Add(fStatus)
		.End();
}


void
MidiPlayerWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MSG_MP_OPEN:
			if (fPanel == NULL) {
				BMessenger me(this);
				fPanel = new BFilePanel(B_OPEN_PANEL, &me, NULL, B_FILE_NODE, false);
			}
			fPanel->Show();
			break;
		case B_REFS_RECEIVED:
		case B_SIMPLE_DATA: {
			entry_ref ref;
			if (message->FindRef("refs", &ref) == B_OK)
				_Load(ref);
			break;
		}
		case MSG_MP_PLAY:
			if (!fLoaded)
				break;
			if (fPlayer->IsPlaying())
				fPlayer->Pause();
			else
				fPlayer->Play();
			_UpdateDisplay();
			break;
		case MSG_MP_STOP:
			fPlayer->Stop();
			_UpdateDisplay();
			break;
		case MSG_MP_PANIC:
			fPlayer->Panic();
			break;
		case MSG_MP_DRAG:
			fDragging = true;
			_ShowTime((int64)fPos->Value() * fPlayer->Duration() / kSliderMax);
			break;
		case MSG_MP_SEEK:
			fPlayer->Seek((int64)fPos->Value() * fPlayer->Duration() / kSliderMax);
			fDragging = false;
			break;
		case MSG_MP_TEMPO: {
			int pct = fTempo->Value();
			fPlayer->SetTempo(pct);
			char label[32];
			snprintf(label, sizeof(label), "Tempo: %d%%", pct);
			fTempo->SetLabel(label);
			break;
		}
		case MSG_MP_LOOP:
			fPlayer->SetLoop(fLoop->Value() == B_CONTROL_ON);
			break;
		case MSG_MP_ROUTE: {
			int32 channel, route;
			if (message->FindInt32("channel", &channel) == B_OK && message->FindInt32("route", &route) == B_OK
				&& channel >= 0 && channel < 16 && route >= 0 && route < kRoutes) {
				Row& row = fRows[channel];
				if (route == SongPlayer::kRouteGm && !fPlayer->IsGmReady()) {
					// the synthesizer loads in the background: the row switches when it is ready
					row.pendingGm = true;
					fPlayer->RequestGm();
					fStatus->SetText("Loading the General MIDI synthesizer of the system...");
					break;
				}
				row.pendingGm = false;
				row.routeIndex = route;
				_ShowRoute(channel);
				_ApplyChannels();
				_MarkDirty();
			}
			break;
		}
		case MSG_MP_VOLUME: {
			int32 channel;
			if (message->FindInt32("channel", &channel) == B_OK && channel >= 0 && channel < 16) {
				int value = fRows[channel].volume->Value();
				if (fRows[channel].routeIndex == SongPlayer::kRouteGm)
					fPlayer->SetGmVolume(channel, value);
				else
					fZyn->SetVolume(channel, value);
				_MarkDirty();
				char text[64];
				snprintf(text, sizeof(text), "Channel %d volume: %d", (int)channel + 1, value);
				fStatus->SetText(text);
			}
			break;
		}
		case MSG_MP_FORGET:
			_ForgetMap();
			break;
		case MSG_MP_SWITCH:
			_ApplyChannels();
			_MarkDirty();
			break;
		case MSG_MP_INSTR: {
			int32 channel;
			if (message->FindInt32("channel", &channel) == B_OK && channel >= 0 && channel < 16)
				_PickInstrument(channel);
			break;
		}
		case MSG_MP_TICK: {
			_UpdateDisplay();
			_CheckGm();
			if (fDirty && system_time() - fDirtyTime > 1000000)
				_SaveMap();
			bigtime_t now = system_time();
			for (int ch = 0; ch < 16; ch++) {
				if (!fRows[ch].used)
					continue;
				int velocity = 0;
				bigtime_t last = fPlayer->LastNoteTime(ch, &velocity);
				int level = 0;
				if (last > 0 && now - last < kFlash)
					level = (int)((kFlash - (now - last)) * (1 + velocity * 7 / 127) / kFlash);
				fRows[ch].activity->SetLevel(level);
			}
			if (++fTicks % 20 == 0)
				_RefreshDestination();
			break;
		}
		default:
			BWindow::MessageReceived(message);
	}
}


void
MidiPlayerWindow::_Load(const entry_ref& ref)
{
	BFile file(&ref, B_READ_ONLY);
	off_t size = 0;
	if (file.InitCheck() != B_OK || file.GetSize(&size) != B_OK || size <= 0 || size > 32 * 1024 * 1024) {
		(new BAlert("err", "The file could not be read.", "OK", NULL, NULL, B_WIDTH_AS_USUAL,
			B_STOP_ALERT))->Go(NULL);
		return;
	}
	std::vector<uint8> data((size_t)size);
	if (file.Read(&data[0], data.size()) != (ssize_t)data.size()) {
		(new BAlert("err", "The file could not be read.", "OK", NULL, NULL, B_WIDTH_AS_USUAL,
			B_STOP_ALERT))->Go(NULL);
		return;
	}
	SmfSong song;
	std::string error;
	if (!ParseSmf(&data[0], data.size(), song, error)) {
		(new BAlert("err", error.c_str(), "OK", NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT))->Go(NULL);
		return;
	}
	_SaveMap();								// the table of the file that was open
	fPlayer->Stop();
	fRef = ref;
	fSong = song;
	fPlayer->SetSong(song);
	fLoaded = true;
	fPlay->SetEnabled(true);
	fStopButton->SetEnabled(true);
	fPos->SetEnabled(true);

	BString text = ref.name;
	char total[16];
	FormatTime(song.durationUs, total, sizeof(total));
	text << "  (" << total << ", tracks: " << song.tracks << ")";
	fFile->SetText(text.String());
	SetTitle(BString("MIDI player - ") << ref.name);
	_ShowSong(song);
	_LoadMap();
	_UpdateDisplay();
}


// the rows of the table for the channels of the file
static const char* const kMapAttribute = "SimpleZyn:channels";


void
MidiPlayerWindow::_MarkDirty()
{
	if (!fLoaded)
		return;
	if (!fDirty)
		fDirtyTime = system_time();
	fDirty = true;
}


// the table (where every channel sounds, its instrument, volume, M and S) goes into an attribute
// of the .mid file: it stays with the file when the file is renamed or moved
void
MidiPlayerWindow::_SaveMap()
{
	fDirty = false;
	if (!fLoaded)
		return;
	BMessage map;
	for (int ch = 0; ch < 16; ch++) {
		const Row& row = fRows[ch];
		if (!row.used)
			continue;
		BMessage c;
		c.AddInt32("channel", ch);
		c.AddInt32("route", row.pendingGm ? (int32)SongPlayer::kRouteGm : (int32)row.routeIndex);
		c.AddString("file", row.instrumentFile.c_str());
		c.AddString("name", row.instrumentName.c_str());
		c.AddInt32("volume", row.volume->Value());
		c.AddBool("mute", row.mute->Value() == B_CONTROL_ON);
		c.AddBool("solo", row.solo->Value() == B_CONTROL_ON);
		map.AddMessage("ch", &c);
	}
	const ssize_t size = map.FlattenedSize();
	if (size <= 0)
		return;
	std::vector<char> buffer((size_t)size);
	if (map.Flatten(&buffer[0], size) != B_OK)
		return;
	BNode node(&fRef);
	if (node.InitCheck() == B_OK)
		node.WriteAttr(kMapAttribute, B_MESSAGE_TYPE, 0, &buffer[0], (size_t)size);
}


void
MidiPlayerWindow::_LoadMap()
{
	BNode node(&fRef);
	attr_info info;
	if (node.InitCheck() != B_OK || node.GetAttrInfo(kMapAttribute, &info) != B_OK || info.size <= 0
		|| info.size > 1024 * 1024)
		return;
	std::vector<char> buffer((size_t)info.size);
	if (node.ReadAttr(kMapAttribute, B_MESSAGE_TYPE, 0, &buffer[0], buffer.size()) != info.size)
		return;
	BMessage map;
	if (map.Unflatten(&buffer[0]) != B_OK)
		return;

	bool wantsGm = false;
	BMessage c;
	for (int32 i = 0; map.FindMessage("ch", i, &c) == B_OK; i++) {
		int32 ch, route, volume;
		if (c.FindInt32("channel", &ch) != B_OK || ch < 0 || ch > 15 || !fRows[ch].used
			|| c.FindInt32("route", &route) != B_OK || route < 0 || route >= kRoutes)
			continue;
		Row& row = fRows[ch];
		bool mute = false, solo = false;
		c.FindBool("mute", &mute);
		c.FindBool("solo", &solo);
		row.mute->SetValue(mute ? B_CONTROL_ON : B_CONTROL_OFF);
		row.solo->SetValue(solo ? B_CONTROL_ON : B_CONTROL_OFF);
		if (c.FindInt32("volume", &volume) != B_OK)
			volume = row.volume->Value();
		if (volume < 0)
			volume = 0;
		if (volume > 127)
			volume = 127;
		row.route->Menu()->ItemAt(route)->SetMarked(true);
		if (route == SongPlayer::kRouteGm) {
			row.pendingGm = true;			// it switches when the synthesizer is loaded
			row.savedGmVolume = volume;
			wantsGm = true;
			continue;
		}
		row.routeIndex = route;
		row.volume->SetValue(volume);
		BString file, name;
		if (c.FindString("file", &file) == B_OK && file.Length() > 0 && BEntry(file.String()).Exists()) {
			fZyn->SetChannel(ch, ch);
			fZyn->LoadInstrument(ch, file.String());
			row.instrumentFile = file.String();
			if (c.FindString("name", &name) == B_OK)
				row.instrumentName = name.String();
			row.instrument->SetLabel(row.instrumentName.c_str());
		}
		if (route == SongPlayer::kRouteZyn)
			fZyn->SetVolume(ch, volume);
	}
	_ApplyChannels();
	if (wantsGm) {
		fPlayer->RequestGm();
		fStatus->SetText("Loading the General MIDI synthesizer of the system...");
	} else {
		fStatus->SetText("The saved mapping of the file is restored");
	}
}


void
MidiPlayerWindow::_ForgetMap()
{
	if (!fLoaded)
		return;
	BNode node(&fRef);
	if (node.InitCheck() == B_OK)
		node.RemoveAttr(kMapAttribute);
	fDirty = false;
	_ShowSong(fSong);
	fStatus->SetText("The saved mapping is deleted, the table is as at the start");
}


// the rows that wait for the synthesizer of the system
void
MidiPlayerWindow::_CheckGm()
{
	bool waiting = false;
	for (int ch = 0; ch < 16; ch++)
		waiting = waiting || fRows[ch].pendingGm;
	if (!waiting)
		return;
	const bool ready = fPlayer->IsGmReady();
	if (!ready && !fPlayer->GmFailed())
		return;
	for (int ch = 0; ch < 16; ch++) {
		Row& row = fRows[ch];
		if (!row.pendingGm)
			continue;
		row.pendingGm = false;
		if (ready) {
			row.routeIndex = SongPlayer::kRouteGm;
			_ShowRoute(ch);
		} else {
			row.route->Menu()->ItemAt(row.routeIndex)->SetMarked(true);
		}
	}
	if (ready) {
		_ApplyChannels();
		_MarkDirty();
		fStatus->SetText("The General MIDI synthesizer is ready");
	} else {
		(new BAlert("err", "The General MIDI synthesizer of the system could not be loaded.", "OK", NULL,
			NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT))->Go(NULL);
	}
}


// what the row shows for the place where the channel sounds
void
MidiPlayerWindow::_ShowRoute(int ch)
{
	Row& row = fRows[ch];
	const bool gm = row.routeIndex == SongPlayer::kRouteGm;
	row.instrument->SetEnabled(!gm);
	if (gm) {
		row.instrument->SetLabel("(from the file)");
		const int volume = row.savedGmVolume >= 0 ? row.savedGmVolume : 100;
		row.savedGmVolume = -1;
		row.volume->SetValue(volume);
		fPlayer->SetGmVolume(ch, volume);
	} else if (strcmp(row.instrument->Label(), "(from the file)") == 0) {
		row.instrument->SetLabel("(as it is in Zyn)");
		char path[48];
		snprintf(path, sizeof(path), "/part%d/Pvolume", ch);
		int volume;
		if (fZyn->GetInt(path, &volume, 60))
			row.volume->SetValue(volume);
	}
}


void
MidiPlayerWindow::_ShowSong(const SmfSong& song)
{
	for (int ch = 0; ch < 16; ch++) {
		Row& row = fRows[ch];
		const SmfChannel& c = song.channels[ch];
		row.used = c.notes > 0;
		row.pendingGm = false;
		BView* line = fTable->ChildAt(ch);
		if (!row.used) {
			if (!line->IsHidden())
				line->Hide();
			continue;
		}
		BString name;
		if (ch == 9) {
			name = "Drums";
		} else if (c.program >= 0) {
			name = kGmNames[c.program];
		}
		if (c.name.length() > 0) {
			BString track(c.name.c_str());
			if (name.Length() > 0 && track != name)
				name = track << "  (" << name << ")";
			else
				name = track;
		}
		if (name.Length() == 0)
			name = "(no name)";
		row.name->SetText(name.String());
		row.mute->SetValue(B_CONTROL_OFF);
		row.solo->SetValue(B_CONTROL_OFF);
		row.routeIndex = 0;
		BMenu* menu = row.route->Menu();
		menu->ItemAt(0)->SetMarked(true);
		row.instrument->SetLabel("(as it is in Zyn)");
		row.instrumentFile = "";
		row.instrumentName = "";
		row.savedGmVolume = -1;
		row.instrument->SetEnabled(true);
		// the volume of the part as the engine has it now
		char path[48];
		snprintf(path, sizeof(path), "/part%d/Pvolume", ch);
		int volume;
		if (fZyn->GetInt(path, &volume, 60))
			row.volume->SetValue(volume);
		if (line->IsHidden())
			line->Show();
	}
	_ApplyChannels();
}


// route, mute and solo of the rows decide which channels the player sends
void
MidiPlayerWindow::_ApplyChannels()
{
	bool anySolo = false;
	for (int ch = 0; ch < 16; ch++) {
		if (fRows[ch].used && fRows[ch].solo->Value() == B_CONTROL_ON)
			anySolo = true;
	}
	for (int ch = 0; ch < 16; ch++) {
		const Row& row = fRows[ch];
		const bool audible = row.mute->Value() != B_CONTROL_ON
			&& (!anySolo || row.solo->Value() == B_CONTROL_ON);
		fPlayer->SetChannelRoute(ch, audible ? row.routeIndex : (int)SongPlayer::kRouteOff);
	}
}


// the menu of the banks: the instrument goes into the part of the same number (channel N plays part N)
void
MidiPlayerWindow::_PickInstrument(int channel)
{
	BPopUpMenu* menu = new BPopUpMenu("instruments", false, false);
	BMessage* keep = new BMessage(MSG_MP_INSTR);
	keep->AddInt32("channel", channel);
	keep->AddInt32("bank", -1);
	menu->AddItem(new BMenuItem("(as it is in Zyn)", keep));
	menu->AddSeparatorItem();
	for (size_t b = 0; b < fBanks.size(); b++) {
		BMenu* sub = new BMenu(fBanks[b].name.c_str());
		for (size_t i = 0; i < fBanks[b].instruments.size(); i++) {
			BMessage* m = new BMessage(MSG_MP_INSTR);
			m->AddInt32("channel", channel);
			m->AddInt32("bank", (int32)b);
			m->AddInt32("instrument", (int32)i);
			sub->AddItem(new BMenuItem(fBanks[b].instruments[i].name.c_str(), m));
		}
		menu->AddItem(sub);
	}
	BButton* button = fRows[channel].instrument;
	BPoint where = button->ConvertToScreen(BPoint(0, button->Bounds().bottom + 1));
	BMenuItem* picked = menu->Go(where, false, true, button->ConvertToScreen(button->Bounds()));
	BMessage* m = picked != NULL ? picked->Message() : NULL;
	int32 bank = -1, ins = -1;
	if (m != NULL && m->FindInt32("bank", &bank) == B_OK) {
		if (bank < 0) {
			fRows[channel].instrument->SetLabel("(as it is in Zyn)");
			fRows[channel].instrumentFile = "";
			fRows[channel].instrumentName = "";
			_MarkDirty();
		} else if (m->FindInt32("instrument", &ins) == B_OK && bank < (int32)fBanks.size()
			&& ins >= 0 && ins < (int32)fBanks[bank].instruments.size()) {
			const Instrument& instrument = fBanks[bank].instruments[ins];
			fZyn->SetChannel(channel, channel);
			fZyn->LoadInstrument(channel, instrument.file);
			fRows[channel].instrument->SetLabel(instrument.name.c_str());
			fRows[channel].instrumentFile = instrument.file;
			fRows[channel].instrumentName = instrument.name;
			_MarkDirty();
			// it sounds now
			fRows[channel].routeIndex = SongPlayer::kRouteZyn;
			fRows[channel].route->Menu()->ItemAt(0)->SetMarked(true);
			_ApplyChannels();
		}
	}
	delete menu;
}


void
MidiPlayerWindow::_ShowTime(int64 us)
{
	char a[16], b[16], line[48];
	FormatTime(us, a, sizeof(a));
	FormatTime(fPlayer->Duration(), b, sizeof(b));
	snprintf(line, sizeof(line), "%s / %s", a, b);
	fTime->SetText(line);
}


void
MidiPlayerWindow::_UpdateDisplay()
{
	fPlay->SetLabel(fPlayer->IsPlaying() ? "Pause" : "Play");
	if (!fDragging) {
		int64 pos = fPlayer->Position();
		int64 dur = fPlayer->Duration();
		fPos->SetValue(dur > 0 ? (int32)(pos * kSliderMax / dur) : 0);
		_ShowTime(pos);
	}
}


void
MidiPlayerWindow::_Connect(int32 id)
{
	if (fDestId >= 0) {
		BMidiEndpoint* old = BMidiRoster::FindEndpoint(fDestId);
		if (old != NULL) {
			if (old->IsConsumer())
				fPlayer->Disconnect(static_cast<BMidiConsumer*>(old));
			old->Release();
		}
		fDestId = -1;
	}
	if (id >= 0) {
		BMidiEndpoint* e = BMidiRoster::FindEndpoint(id);
		if (e != NULL && e->IsConsumer()) {
			if (fPlayer->Connect(static_cast<BMidiConsumer*>(e)) == B_OK)
				fDestId = id;
		}
		if (e != NULL)
			e->Release();
	}
}


// the MIDI input of the engine is a consumer "ZynAddSubFX": the player connects to it by itself
void
MidiPlayerWindow::_RefreshDestination()
{
	int32 found = -1;
	int32 id = 0;
	BMidiEndpoint* e;
	while ((e = BMidiRoster::NextEndpoint(&id)) != NULL) {
		if (e->IsConsumer() && e->IsRemote() && e->IsValid() && strcmp(e->Name(), "ZynAddSubFX") == 0)
			found = e->ID();
		e->Release();
	}
	if (found != fDestId)
		_Connect(found);
	if (!fPlayerOk)
		fStatus->SetText("The MIDI source could not be started");
	else if (fDestId >= 0)
		fStatus->SetText("Connected to ZynAddSubFX");
	else
		fStatus->SetText("ZynAddSubFX is not found in the MIDI roster (is the engine running?)");
}
