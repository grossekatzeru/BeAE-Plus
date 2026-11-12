/*
 * MidiImportWindow - «Файл → Вставить из MIDI»: выбор саундфонта (*.sf2 из папок synth) и MIDI-файла.
 * Каждая MIDI-дорожка превращается в «чистый» звук (без эффектов и панорамы) и кладётся на первую свободную
 * дорожку проекта; дорожек добавляется столько, сколько нужно. Рендер идёт в отдельном потоке; готовый результат
 * (MidiImportResult) уходит главному окну сообщением MIDI_IMPORT_DONE с указателем "result" - окно забирает и удаляет его.
 */
#ifndef MIDI_IMPORT_H
#define MIDI_IMPORT_H

#include <Window.h>
#include <FilePanel.h>
#include <Handler.h>
#include <Messenger.h>
#include <Entry.h>
#include <string>
#include <vector>
#include <OS.h>
#include "Mixer.h"
#include "MidiFile.h"

// Эффекты MIDI, для которых в BeAE есть подходящие плагины. Название плагина не содержит слова MIDI: это обычные
// плагины для любой дорожки, а здесь только записано, какой контроллер MIDI им соответствует.
//   CC10 (панорама)        -> баланс дорожки
//   CC91 (реверберация)    -> плагин beae_reverb, порт «Смесь, %» (CC 127 = 50 %)
//   CC93 (хорус)           -> плагин beae_chorus, порт «Смесь, %» (CC 127 = 100 %)
// Применяет MidiApplyEffects (вызывается главным окном после вставки дорожки).

struct MidiImportResult {
	// pan/reverb/chorus: значения контроллеров CC10/CC91/CC93 из MIDI (-1 - не заданы или эффекты не просили)
	struct Item { std::string name; SourceRef src; int pan, reverb, chorus; Item() : pan(-1), reverb(-1), chorus(-1) {} };
	std::vector<Item>	items;
	std::string			error;			// не пусто - не получилось (ключ языка)
	double				setBpm;			// > 0 - установить этот темп проекта (Grid и метроном)
};

// Саундфонты (*.sf2) из папок synth системной, пользовательской и «непакетной» папок данных (полные пути).
// Какой из них использовать при импорте MIDI, выбирается в «Настройки -> Аудио -> MIDI» (Prefs.midi_soundfont).
std::vector<std::string> MidiSoundfontPaths();

// Применить эффекты MIDI к дорожке lane: баланс и плагины. warn - что не удалось (например, не найден плагин), пусто - всё хорошо.
void MidiApplyEffects(int lane, int pan, int reverb, int chorus, std::string &warn);

class BListView;
class BButton;
class BRadioButton;
class BCheckBox;
class BStringView;
class BFilePanel;
class BStringView;

// Сообщение панели выбора MIDI-файла: "refs" с выбранным файлом уходит главному окну
#define MSG_MIDI_FILE	'mdFl'

// Панель «Загрузка MIDI-файла...»: такая же, как панель открытия аудиофайла (кнопки Загрузить/Отмена, справа внизу
// сведения о выбранном файле: число дорожек и темп). Предпрослушивания нет: для него нужен рендер саундфонтом (см. TODO.md).
#include <FilePanel.h>
class BButton;
class BMessageRunner;
class BMidiSynthFile;

// Панель выбора MIDI-файла. Кнопка Play включает предпрослушивание системным MIDI-синтезатором Haiku
// (BMidiSynthFile, банк из настроек Midi системы): нужно лишь составить представление о файле.
class MidiOpenPanel : public BFilePanel, public BHandler {
public:
	MidiOpenPanel(BMessenger target);
	virtual ~MidiOpenPanel();
	virtual void SelectionChanged();
	virtual void WasHidden();
	virtual void MessageReceived(BMessage *msg);
private:
	void	StopPreview();
	void	StartPreview();
	BStringView	*fLine1, *fLine2;
	BButton		*fPlay;
	BMidiSynthFile *fSynth;
	BMessageRunner *fTimer;
	entry_ref	fRef;
	bool		fHaveRef, fPlaying;
};

class MidiImportWindow : public BWindow {
public:
	// projectEmpty/projectBpm - состояние проекта на момент вызова: в пустой проект темп берётся из MIDI сам,
	// в непустой при расхождении окно предлагает выбор
	// midi - уже выбранный в панели MidiOpenPanel файл; окно показывает параметры вставки (темп, эффекты)
	MidiImportWindow(BMessenger target, bool projectEmpty, double projectBpm, const entry_ref &midi);
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
	BStringView		*fFont;				// какой саундфонт будет использован (выбирается в настройках)
	BStringView		*fStatus, *fTempoInfo;
	BRadioButton	*fOption[3];			// 0 - темп проекта по MIDI, 1 - подогнать MIDI, 2 - не менять
	BButton			*fInsert;
	BCheckBox		*fFxPan, *fFxReverb, *fFxChorus;	// какие эффекты MIDI перенести: баланс дорожки и плагины реверба / хоруса
	bool			fProjectEmpty;
	double			fProjectBpm;
	bool			fHaveSong;
	MidiSong		fSong;
	std::string		fSf2;				// путь к выбранному саундфонту, пусто - не найден
	thread_id		fThread;
	bool			fBusy;
};

#endif
