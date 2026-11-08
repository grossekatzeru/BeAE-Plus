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

struct MidiImportResult {
	struct Item { std::string name; SourceRef src; };
	std::vector<Item>	items;
	std::string			error;			// не пусто - не получилось
};

class BListView;
class BButton;
class BStringView;
class BFilePanel;

class MidiImportWindow : public BWindow {
public:
	MidiImportWindow(BMessenger target);
	virtual ~MidiImportWindow();
	virtual void MessageReceived(BMessage *msg);
	virtual bool QuitRequested();
private:
	void	Scan();
	void	StartRender(const entry_ref &midi);
	static int32 Worker(void *arg);

	BMessenger		fTarget;
	BListView		*fList;
	BButton			*fChoose;
	BStringView		*fStatus;
	BFilePanel		*fPanel;
	std::vector<std::string>	fPaths;
	thread_id		fThread;
	bool			fBusy;
};

#endif
