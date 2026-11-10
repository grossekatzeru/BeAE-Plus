#include <Alert.h>
#include <Button.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <ListView.h>
#include <RadioButton.h>
#include <CheckBox.h>
#include <String.h>
#include <math.h>
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
#include "LadspaFilter.h"
#include "FxChain.h"

#define MSG_MIDI_CHOOSE		'mdCh'
#define MSG_MIDI_FINISHED	'mdFn'
#define MSG_MIDI_INSERT		'mdIn'
#define MIDI_RATE			44100.0f

namespace {
struct Job {
	MidiSong		song;
	std::string		sf2;
	double			scale;			// множитель времени событий (подгонка MIDI под темп проекта)
	double			setBpm;			// > 0 - сообщить, какой темп установить в проекте
	bool			effects;		// передать значения эффектных контроллеров (панорама, реверб, хорус)
	BMessenger		target, window;
};

BString Bpm(double v)
{
	BString s;
	s << (int)floor(v + 0.5);
	return s;
}

bool EndsWithSf2(const char *name)
{
	size_t n = strlen(name);
	return n > 4 && strcasecmp(name + n - 4, ".sf2") == 0;
}
}

MidiImportWindow::MidiImportWindow(BMessenger target, bool projectEmpty, double projectBpm)
	: BWindow(BRect(120, 120, 540, 448), Language.get("MIDI_IMPORT_TITLE"), B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_ASYNCHRONOUS_CONTROLS),
	fTarget(target), fPanel(NULL), fProjectEmpty(projectEmpty), fProjectBpm(projectBpm), fHaveSong(false),
	fThread(-1), fBusy(false)
{
	BView *back = new BView(Bounds(), "back", B_FOLLOW_ALL, B_WILL_DRAW);
	back->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	AddChild(back);

	BStringView *label = new BStringView(BRect(12, 8, 408, 24), NULL, Language.get("MIDI_SOUNDFONT"));
	back->AddChild(label);
	fList = new BListView(BRect(12, 28, 392, 106), "fonts", B_SINGLE_SELECTION_LIST);
	back->AddChild(new BScrollView("fontsScroll", fList, B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP, 0, false, true));
	fChoose = new BButton(BRect(12, 118, 232, 146), NULL, Language.get("MIDI_CHOOSE"), new BMessage(MSG_MIDI_CHOOSE));
	back->AddChild(fChoose);
	fTempoInfo = new BStringView(BRect(12, 152, 408, 168), NULL, "");
	back->AddChild(fTempoInfo);
	const char *keys[3] = { "MIDI_OPT_SET_PROJECT", "MIDI_OPT_FIT_MIDI", "MIDI_OPT_KEEP" };
	for (int i = 0; i < 3; i++) {
		fOption[i] = new BRadioButton(BRect(24, 170 + i * 20, 408, 188 + i * 20), NULL, Language.get(keys[i]), new BMessage('mdOp'));
		back->AddChild(fOption[i]);
		fOption[i]->Hide();
	}
	fOption[0]->SetValue(B_CONTROL_ON);
	fEffects = new BCheckBox(BRect(12, 236, 408, 254), NULL, Language.get("MIDI_OPT_EFFECTS"), new BMessage('mdFx'));
	back->AddChild(fEffects);
	fStatus = new BStringView(BRect(12, 262, 408, 278), NULL, "");
	back->AddChild(fStatus);
	fInsert = new BButton(BRect(190, 290, 300, 318), NULL, Language.get("MIDI_INSERT"), new BMessage(MSG_MIDI_INSERT));
	back->AddChild(fInsert);
	fInsert->SetEnabled(false);
	fInsert->MakeDefault(true);
	BButton *cancel = new BButton(BRect(308, 290, 408, 318), NULL, Language.get("CANCEL"), new BMessage(B_QUIT_REQUESTED));
	back->AddChild(cancel);
	ResizeTo(420, 328);

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
		if (!fPanel) {
			BMessenger me(this);
			fPanel = new BFilePanel(B_OPEN_PANEL, &me, NULL, B_FILE_NODE, false, NULL, NULL, true, true);
		}
		fPanel->Show();
		break;
	case B_REFS_RECEIVED:
	{
		entry_ref ref;
		if (msg->FindRef("refs", &ref) == B_OK) LoadMidiFile(ref);
		break;
	}
	case MSG_MIDI_INSERT:
		if (!fBusy) StartRender();
		break;
	case MSG_MIDI_FINISHED:
		fBusy = false;
		PostMessage(B_QUIT_REQUESTED);
		break;
	default:
		BWindow::MessageReceived(msg);
	}
}

// Файл читается сразу: после выбора видно темп MIDI, и окно предлагает, что делать с темпом проекта.
void MidiImportWindow::LoadMidiFile(const entry_ref &midi)
{
	fHaveSong = false;
	fInsert->SetEnabled(false);
	std::string err;
	BFile file(&midi, B_READ_ONLY);
	off_t size = 0;
	std::vector<uint8_t> data;
	if (file.InitCheck() != B_OK || file.GetSize(&size) != B_OK || size < 14 || size > 32 * 1024 * 1024
		|| (data.resize((size_t)size), file.Read(&data[0], (size_t)size) != size)
		|| !LoadMidi(&data[0], data.size(), fSong, err)) {
		fStatus->SetText(Language.get("MIDI_ERROR"));
		UpdateTempoUi();
		return;
	}
	fHaveSong = true;
	fStatus->SetText(midi.name);
	UpdateTempoUi();
	fInsert->SetEnabled(true);
}

// пустой проект - темп возьмётся из MIDI сам; иначе при расхождении (> 0.5 BPM) выбор из трёх вариантов
void MidiImportWindow::UpdateTempoUi()
{
	for (int i = 0; i < 3; i++) if (!fOption[i]->IsHidden()) fOption[i]->Hide();
	if (!fHaveSong) { fTempoInfo->SetText(""); return; }
	BString s;
	if (fProjectEmpty) {
		s = Language.get("MIDI_TEMPO_SET_EMPTY");
		s.ReplaceFirst("%s", Bpm(fSong.bpm).String());
	} else if (fabs(fSong.bpm - fProjectBpm) <= 0.5) {
		s = Language.get("MIDI_TEMPO_SAME");
	} else {
		s = Language.get("MIDI_TEMPO_INFO");
		s.ReplaceFirst("%s", Bpm(fSong.bpm).String());
		s.ReplaceFirst("%s", Bpm(fProjectBpm).String());
		for (int i = 0; i < 3; i++) fOption[i]->Show();
	}
	if (fSong.tempoVaries) s << Language.get("MIDI_TEMPO_VARIES");
	fTempoInfo->SetText(s.String());
}

void MidiImportWindow::StartRender()
{
	const int32 sel = fList->CurrentSelection();
	if (!fHaveSong) return;
	if (sel < 0 || (size_t)sel >= fPaths.size()) {
		fStatus->SetText(Language.get("MIDI_NO_SELECTION"));
		return;
	}
	Job *job = new (std::nothrow) Job;
	if (!job) return;
	job->song = fSong;
	job->sf2 = fPaths[sel];
	job->scale = 1.0;
	job->effects = fEffects->Value() == B_CONTROL_ON;
	job->setBpm = 0;
	if (fProjectEmpty) {
		job->setBpm = fSong.bpm;
	} else if (fabs(fSong.bpm - fProjectBpm) > 0.5) {
		if (fOption[0]->Value() == B_CONTROL_ON)
			job->setBpm = fSong.bpm;						// темп проекта - как в MIDI
		else if (fOption[1]->Value() == B_CONTROL_ON)
			job->scale = fSong.bpm / fProjectBpm;			// MIDI играет в темпе проекта: времена укорачиваются/растягиваются
	}
	job->target = fTarget;
	job->window = BMessenger(this);
	fBusy = true;
	fChoose->SetEnabled(false);
	fInsert->SetEnabled(false);
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
		res->setBpm = job->setBpm;
		std::string err;
		SoundFont sf;
		if (!sf.LoadFile(job->sf2.c_str(), err)) {
			res->error = "MIDI_ERROR";
		} else {
			int index = 0;
			for (size_t t = 0; t < job->song.tracks.size(); t++) {
				MidiTrackData td = job->song.tracks[t];
				if (job->scale != 1.0)
					for (size_t k = 0; k < td.ev.size(); k++) td.ev[k].t *= job->scale;
				std::vector<float> pcm;
				if (!RenderMidiTrack(td, sf, MIDI_RATE, pcm)) continue;
				SourceRef src = MakeSource(&pcm[0], (int64_t)(pcm.size() / 2), 2, MIDI_RATE);
				if (!src) {
					res->error = "MEM_ERROR";
					break;
				}
				MidiImportResult::Item item;
				item.name = MidiTrackTitle(job->song.tracks[t], ++index);
				item.src = src;
				if (job->effects) {
					const MidiEffects fx = MidiTrackEffects(job->song.tracks[t]);
					item.pan = fx.pan; item.reverb = fx.reverb; item.chorus = fx.chorus;
				}
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

// Соответствие «эффект MIDI -> плагин»: метка плагина, номер порта «Смесь, %», процент смеси при значении 127.
namespace {
struct FxRole { const char *label; int mixPort; float maxPercent; };
const FxRole kReverbRole = { "beae_reverb", 2, 50.0f };
const FxRole kChorusRole = { "beae_chorus", 2, 100.0f };

bool AddRole(int lane, const FxRole &role, int cc, std::string &warn)
{
	if (cc <= 0) return true;							// 0 - эффекта нет
	const Ladspa::Plugin *plugin = NULL;
	const std::vector<Ladspa::Plugin> &all = LadspaList();
	for (size_t i = 0; i < all.size() && !plugin; i++)
		if (all[i].label == role.label) plugin = &all[i];
	if (!plugin) { warn += role.label; warn += " "; return false; }
	Ladspa::Values values;
	values[role.mixPort] = (cc > 127 ? 127 : cc) / 127.0f * role.maxPercent;
	std::string err;
	const float rate = Pool.system_frequency > 0 ? Pool.system_frequency : 44100.0f;
	return Mix.Fx(lane).Add(*plugin, rate, &err, &values);
}
}

void MidiApplyEffects(int lane, int pan, int reverb, int chorus, std::string &warn)
{
	if (pan >= 0) Mix.SetPan(lane, (float)(pan - 64) / 63.0f);		// 0 - лево, 64 - центр, 127 - право
	AddRole(lane, kReverbRole, reverb, warn);
	AddRole(lane, kChorusRole, chorus, warn);
}
