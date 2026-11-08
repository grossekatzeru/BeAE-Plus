#include <Alert.h>
#include <Button.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <ListView.h>
#include <Path.h>
#include <ScrollView.h>
#include <StringItem.h>
#include <StringView.h>
#include <strings.h>
#include <new>

#include "Globals.h"
#include "CommonPool.h"
#include "MidiFile.h"
#include "MidiSynth.h"
#include "MidiImport.h"
#include "main.h"

#define MSG_MIDI_CHOOSE		'mdCh'
#define MSG_MIDI_FINISHED	'mdFn'
#define MIDI_RATE			44100.0f

namespace {
struct Job {
	entry_ref		midi;
	std::string		sf2;
	BMessenger		target, window;
};

bool EndsWithSf2(const char *name)
{
	size_t n = strlen(name);
	return n > 4 && strcasecmp(name + n - 4, ".sf2") == 0;
}
}

MidiImportWindow::MidiImportWindow(BMessenger target)
	: BWindow(BRect(120, 120, 540, 340), Language.get("MIDI_IMPORT_TITLE"), B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_ASYNCHRONOUS_CONTROLS),
	fTarget(target), fPanel(NULL), fThread(-1), fBusy(false)
{
	BView *back = new BView(Bounds(), "back", B_FOLLOW_ALL, B_WILL_DRAW);
	back->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	AddChild(back);

	BStringView *label = new BStringView(BRect(12, 8, 408, 24), NULL, Language.get("MIDI_SOUNDFONT"));
	back->AddChild(label);
	fList = new BListView(BRect(12, 28, 392, 140), "fonts", B_SINGLE_SELECTION_LIST);
	back->AddChild(new BScrollView("fontsScroll", fList, B_FOLLOW_ALL, 0, false, true));
	fStatus = new BStringView(BRect(12, 150, 408, 166), NULL, "");
	back->AddChild(fStatus);
	fChoose = new BButton(BRect(12, 176, 232, 204), NULL, Language.get("MIDI_CHOOSE"), new BMessage(MSG_MIDI_CHOOSE));
	back->AddChild(fChoose);
	BButton *cancel = new BButton(BRect(300, 176, 408, 204), NULL, Language.get("CANCEL"), new BMessage(B_QUIT_REQUESTED));
	back->AddChild(cancel);
	fChoose->MakeDefault(true);

	Scan();
	Show();
}

MidiImportWindow::~MidiImportWindow()
{
	delete fPanel;
}

// саундфонты лежат в synth системной, пользовательской и «непакетной» папок данных
void MidiImportWindow::Scan()
{
	const directory_which dirs[] = { B_SYSTEM_DATA_DIRECTORY, B_USER_NONPACKAGED_DATA_DIRECTORY, B_USER_DATA_DIRECTORY };
	for (size_t d = 0; d < sizeof(dirs) / sizeof(dirs[0]); d++) {
		BPath path;
		if (find_directory(dirs[d], &path) != B_OK) continue;
		path.Append("synth");
		BDirectory dir(path.Path());
		if (dir.InitCheck() != B_OK) continue;
		BEntry entry;
		while (dir.GetNextEntry(&entry, true) == B_OK) {
			if (entry.IsDirectory()) continue;
			char name[B_FILE_NAME_LENGTH];
			entry.GetName(name);
			if (!EndsWithSf2(name)) continue;
			BPath full;
			if (entry.GetPath(&full) != B_OK) continue;
			bool dup = false;
			for (size_t k = 0; k < fPaths.size(); k++) if (fPaths[k] == full.Path()) dup = true;
			if (dup) continue;
			fPaths.push_back(full.Path());
			fList->AddItem(new BStringItem(name));
		}
	}
	if (fPaths.empty()) {
		fStatus->SetText(Language.get("MIDI_NO_SOUNDFONTS"));
		fChoose->SetEnabled(false);
	} else {
		fList->Select(0);
	}
}

bool MidiImportWindow::QuitRequested()
{
	return !fBusy;			// пока идёт рендер, окно держится
}

void MidiImportWindow::MessageReceived(BMessage *msg)
{
	switch (msg->what) {
	case MSG_MIDI_CHOOSE:
		if (fBusy) break;
		if (fList->CurrentSelection() < 0) {
			fStatus->SetText(Language.get("MIDI_NO_SELECTION"));
			break;
		}
		if (!fPanel) {
			BMessenger me(this);
			fPanel = new BFilePanel(B_OPEN_PANEL, &me, NULL, B_FILE_NODE, false, NULL, NULL, true, true);
		}
		fPanel->Show();
		break;
	case B_REFS_RECEIVED:
	{
		entry_ref ref;
		if (msg->FindRef("refs", &ref) == B_OK) StartRender(ref);
		break;
	}
	case MSG_MIDI_FINISHED:
		fBusy = false;
		PostMessage(B_QUIT_REQUESTED);
		break;
	default:
		BWindow::MessageReceived(msg);
	}
}

void MidiImportWindow::StartRender(const entry_ref &midi)
{
	const int32 sel = fList->CurrentSelection();
	if (sel < 0 || (size_t)sel >= fPaths.size()) return;
	Job *job = new (std::nothrow) Job;
	if (!job) return;
	job->midi = midi;
	job->sf2 = fPaths[sel];
	job->target = fTarget;
	job->window = BMessenger(this);
	fBusy = true;
	fChoose->SetEnabled(false);
	fStatus->SetText(Language.get("MIDI_RENDERING"));
	fThread = spawn_thread(Worker, "MIDI render", B_NORMAL_PRIORITY, job);
	if (fThread < 0) {
		fBusy = false;
		delete job;
		return;
	}
	resume_thread(fThread);
}

int32 MidiImportWindow::Worker(void *arg)
{
	Job *job = (Job *)arg;
	MidiImportResult *res = new (std::nothrow) MidiImportResult;
	if (res) {
		std::string err;
		SoundFont sf;
		MidiSong song;
		BFile file(&job->midi, B_READ_ONLY);
		off_t size = 0;
		std::vector<uint8_t> data;
		if (file.InitCheck() != B_OK || file.GetSize(&size) != B_OK || size < 14 || size > 32 * 1024 * 1024) {
			res->error = "MIDI_ERROR";
		} else {
			data.resize((size_t)size);
			if (file.Read(&data[0], (size_t)size) != size || !LoadMidi(&data[0], data.size(), song, err))
				res->error = "MIDI_ERROR";
		}
		if (res->error.empty() && !sf.LoadFile(job->sf2.c_str(), err))
			res->error = "MIDI_ERROR";
		if (res->error.empty()) {
			int index = 0;
			for (size_t t = 0; t < song.tracks.size(); t++) {
				std::vector<float> pcm;
				if (!RenderMidiTrack(song.tracks[t], sf, MIDI_RATE, pcm)) continue;
				SourceRef src = MakeSource(&pcm[0], (int64_t)(pcm.size() / 2), 2, MIDI_RATE);
				if (!src) {
					res->error = "MEM_ERROR";
					break;
				}
				MidiImportResult::Item item;
				item.name = MidiTrackTitle(song.tracks[t], ++index);
				item.src = src;
				res->items.push_back(item);
			}
			if (res->error.empty() && res->items.empty()) res->error = "MIDI_ERROR";
		}
		BMessage done(MIDI_IMPORT_DONE);
		done.AddPointer("result", res);
		if (job->target.SendMessage(&done) != B_OK) delete res;
	}
	BMessage fin(MSG_MIDI_FINISHED);
	job->window.SendMessage(&fin);
	delete job;
	return 0;
}
