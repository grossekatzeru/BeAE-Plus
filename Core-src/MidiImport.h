/*
 * MidiImportWindow - «Файл → Вставить из MIDI»: выбор саундфонта (*.sf2 из папок synth) и MIDI-файла.
 * Каждая MIDI-дорожка превращается в «чистый» звук (без эффектов и панорамы) и кладётся на первую свободную
 * дорожку проекта; дорожек добавляется столько, сколько нужно. Рендер идёт в отдельном потоке; готовый результат
 * (MidiImportResult) уходит главному окну сообщением MIDI_IMPORT_DONE с указателем "result" - окно забирает и удаляет его.
 */
#ifndef MIDI_IMPORT_H
#define MIDI_IMPORT_H

#include <Window.h>
#include <Messenger.h>
#include <Entry.h>
#include <string>
#include <vector>
#include <OS.h>
#include "Mixer.h"
#include "MidiFile.h"

struct MidiImportResult {
	struct Item { std::string name; SourceRef src; };
	std::vector<Item>	items;
	std::string			error;			// не пусто - не получилось (ключ языка)
	double				setBpm;			// > 0 - установить этот темп проекта (Grid и метроном)
};

class BListView;
class BButton;
class BRadioButton;
class BStringView;
class BFilePanel;

class MidiImportWindow : public BWindow {
public:
	// projectEmpty/projectBpm - состояние проекта на момент вызова: в пустой проект темп берётся из MIDI сам,
	// в непустой при расхождении окно предлагает выбор
	MidiImportWindow(BMessenger target, bool projectEmpty, double projectBpm);
	virtual ~MidiImportWindow();
	virtual void MessageReceived(BMessage *msg);
	virtual bool QuitRequested();
private:
	void	Scan();
	void	LoadMidiFile(const entry_ref &midi);
	void	UpdateTempoUi();
	void	StartRender();
	static int32 Worker(void *arg);

	BMessenger		fTarget;
	BListView		*fList;
	BButton			*fChoose;
	BStringView		*fStatus, *fTempoInfo;
	BRadioButton	*fOption[3];			// 0 - темп проекта по MIDI, 1 - подогнать MIDI, 2 - не менять
	BButton			*fInsert;
	BFilePanel		*fPanel;
	bool			fProjectEmpty;
	double			fProjectBpm;
	bool			fHaveSong;
	MidiSong		fSong;
	std::vector<std::string>	fPaths;
	thread_id		fThread;
	bool			fBusy;
};

#endif
