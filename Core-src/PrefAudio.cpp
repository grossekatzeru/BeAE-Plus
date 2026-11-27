#include <LayoutBuilder.h>
#include <InterfaceKit.h>
#include <MenuField.h>
#include <PopUpMenu.h>
#include <SeparatorView.h>
#include <String.h>
#include <Path.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "Globals.h"
#include "AudioRecorder.h"
#include "PrefAudio.h"
#include "MidiImport.h"
#include "LatencyCal.h"

#define BUFFER_SLIDER		'abSl'
#define BUFFER_TEXT			'abTx'
#define LATENCY_CHANGED		'abLt'
#define FINE_TUNE			'abFt'
#define FONT_SELECTED		'abSf'
#define CARD_SELECTED		'abCd'

#define FRAME_BYTES			8			// стерео float: 2 канала по 4 байта
#define MIN_FRAMES			64
#define MAX_FRAMES			16384
#define STEP_FRAMES			64			// шаг бегунка
#define RECOMMENDED_FRAMES	2048

/*******************************************************
*
*******************************************************/
PrefAudio::PrefAudio() : BView("Prefs audio", 0)
{
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	BStringView *title_buf = new BStringView(NULL, Language.get("AUDIO_BUFFER"));
	title_buf->SetFont(be_bold_font);
	BStringView *hint_buf = new BStringView(NULL, Language.get("AUDIO_BUFFER_HINT"));
	hint_buf->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_2_TINT);

	s_buffer = new BSlider("buffer", NULL, new BMessage(BUFFER_SLIDER), MIN_FRAMES / STEP_FRAMES,
		MAX_FRAMES / STEP_FRAMES, B_HORIZONTAL);
	s_buffer->SetModificationMessage(new BMessage(BUFFER_SLIDER + 1));	// живое обновление числа, пока тянем
	s_buffer->SetKeyIncrementValue(1);

	t_buffer = new BTextControl("bufferv", NULL, "", new BMessage(BUFFER_TEXT));
	t_buffer->SetExplicitMinSize(BSize(be_plain_font->StringWidth("00000") + 24, B_SIZE_UNSET));
	t_buffer->SetExplicitMaxSize(BSize(be_plain_font->StringWidth("00000") + 24, B_SIZE_UNSET));
	for (int c = 32; c < 127; c++)					// только цифры (управляющие клавиши, в том числе Backspace, не трогаем)
		if (!(c >= '0' && c <= '9')) t_buffer->TextView()->DisallowChar(c);

	BStringView *title_rec = new BStringView(NULL, Language.get("AUDIO_REC_TITLE"));
	title_rec->SetFont(be_bold_font);
	v_rec_hint = new BStringView(NULL, "");
	v_rec_hint->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_2_TINT);

	pm_cards = new BPopUpMenu("");
	m_cards = new BMenuField("cards", NULL, pm_cards);
	m_cards->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));	// во всю ширину: имя карты бывает длинным
	m_cards->SetToolTip(Language.get("CARD_LIST_TIP"));

	s_latency = new BSpinner(NULL, Language.get("REC_LATENCY"), new BMessage(LATENCY_CHANGED));
	s_latency->SetRange(0, 1000);
	s_latency->SetValue(0);

	b_fine = new BButton("fine", Language.get("AUDIO_FINE_TUNE"), new BMessage(FINE_TUNE));

	BStringView *title_midi = new BStringView(NULL, Language.get("AUDIO_MIDI_TITLE"));
	title_midi->SetFont(be_bold_font);
	BStringView *label_font = new BStringView(NULL, Language.get("MIDI_SOUNDFONT_PREF"));
	l_fonts = new BListView("fonts", B_SINGLE_SELECTION_LIST);
	l_fonts->SetSelectionMessage(new BMessage(FONT_SELECTED));
	BScrollView *scroll_fonts = new BScrollView("fontsScroll", l_fonts, 0, false, true);
	scroll_fonts->SetExplicitMinSize(BSize(B_SIZE_UNSET, 90));
	v_fonts = new BStringView(NULL, "");
	v_fonts->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_2_TINT);
	fonts = MidiSoundfontPaths();
	{
		int32 current = -1;
		for (size_t i = 0; i < fonts.size(); i++) {
			BPath p(fonts[i].c_str());
			l_fonts->AddItem(new BStringItem(p.Leaf()));
			if (Prefs.midi_soundfont == fonts[i].c_str()) current = (int32)i;
		}
		if (current < 0 && !fonts.empty()) current = 0;		// ничего не выбрано - работает первый найденный
		if (current >= 0) l_fonts->Select(current);
		if (fonts.empty()) v_fonts->SetText(Language.get("MIDI_NO_SOUNDFONTS"));
	}

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(title_buf)
		.Add(hint_buf)
		.AddGroup(B_HORIZONTAL)
			.Add(s_buffer)
			.Add(t_buffer)
		.End()
		.Add(new BSeparatorView(B_HORIZONTAL))
		.Add(title_rec)
		.Add(m_cards)
		.Add(v_rec_hint)
		.Add(s_latency)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(b_fine)
		.End()
		.Add(new BSeparatorView(B_HORIZONTAL))
		.Add(title_midi)
		.Add(label_font)
		.Add(scroll_fonts)
		.Add(v_fonts)
		.AddGlue();

	RefreshCards();
	ShowFrames(Prefs.buffer_size / FRAME_BYTES);
}

PrefAudio::~PrefAudio()
{
}

// найти звуковые карты и заполнить выпадающий список; выбранной остаётся прежняя, иначе системная
void PrefAudio::RefreshCards()
{
	BString system;
	AudioRecorder::ListCards(cards, &system);

	while (pm_cards->CountItems() > 0)
		delete pm_cards->RemoveItem((int32)0);

	if (cards.empty()) {
		BMenuItem *none = new BMenuItem(Language.get("CARD_NONE"), NULL);
		none->SetEnabled(false);
		pm_cards->AddItem(none);
		curCard = "";
		s_latency->SetEnabled(false);
		b_fine->SetEnabled(false);
		return;
	}

	int32 pick = 0;
	for (size_t i = 0; i < cards.size(); i++) {
		BMessage *m = new BMessage(CARD_SELECTED);
		m->AddInt32("index", (int32)i);
		pm_cards->AddItem(new BMenuItem(cards[i].String(), m));
		if (curCard.Length() > 0 ? cards[i] == curCard : cards[i] == system)
			pick = (int32)i;
	}
	pm_cards->SetTargetForItems(this);
	pm_cards->ItemAt(pick)->SetMarked(true);
	s_latency->SetEnabled(true);
	b_fine->SetEnabled(true);
	SelectCard(pick);
}

void PrefAudio::SelectCard(int32 index)
{
	if (index < 0 || (size_t)index >= cards.size())
		return;
	curCard = cards[index];
	s_latency->SetValue(Prefs.card_latency.Get(curCard.String()));
}

void PrefAudio::AttachedToWindow()
{
	pm_cards->SetTargetForItems(this);
	s_buffer->SetTarget(this);
	t_buffer->SetTarget(this);
	s_latency->SetTarget(this);
	b_fine->SetTarget(this);
	l_fonts->SetTarget(this);
}

// число кадров -> поле, бегунок и подсказка (без перезапуска проигрывателя)
void PrefAudio::ShowFrames(int32 frames)
{
	if (frames < MIN_FRAMES) frames = MIN_FRAMES;
	if (frames > MAX_FRAMES) frames = MAX_FRAMES;
	char s[16];
	snprintf(s, sizeof(s), "%d", (int)frames);
	t_buffer->SetText(s);
	int32 pos = (frames + STEP_FRAMES / 2) / STEP_FRAMES;
	if (s_buffer->Value() != pos) s_buffer->SetValue(pos);
	UpdateHint();
}

void PrefAudio::UpdateHint()
{
	const int32 frames = atoi(t_buffer->Text());
	const double rate = (Pool.system_frequency > 1.0f) ? Pool.system_frequency : 44100.0;
	const int ms = (int)ceil(frames * 1000.0 / rate);
	char s[200];
	snprintf(s, sizeof(s), Language.get("AUDIO_REC_HINT"), (int)frames, ms, ms);
	v_rec_hint->SetText(s);
}

// сохранить размер в настройках и перезапустить проигрыватель
void PrefAudio::SetFrames(int32 frames, bool apply)
{
	if (frames < MIN_FRAMES) frames = MIN_FRAMES;
	if (frames > MAX_FRAMES) frames = MAX_FRAMES;
	ShowFrames(frames);
	Prefs.buffer_size = frames * FRAME_BYTES;
	if (apply) ApplyBuffer();
}

void PrefAudio::ApplyBuffer()
{
	if (Recorder.IsRecording())
		return;										// вступит в силу при следующем запуске воспроизведения
	if (Pool.IsPlaying()){
		Pool.StopPlaying();
		if (Pool.mainWindow)
			Pool.mainWindow->PostMessage(TRANSPORT_STOP);
	}
	Pool.InitBufferPlayer((Pool.frequency > 1.0f) ? Pool.frequency : 44100.0f);
}

void PrefAudio::MessageReceived(BMessage *msg)
{
	switch (msg->what){
	case BUFFER_SLIDER + 1:							// бегунок тянут: только число
		{
			const int32 frames = s_buffer->Value() * STEP_FRAMES;
			char s[16];
			snprintf(s, sizeof(s), "%d", (int)frames);
			t_buffer->SetText(s);
			UpdateHint();
		}
		break;

	case BUFFER_SLIDER:								// бегунок отпущен: принять и применить
		SetFrames(s_buffer->Value() * STEP_FRAMES, true);
		break;

	case BUFFER_TEXT:								// число вписано руками
		SetFrames(atoi(t_buffer->Text()), true);
		break;

	case CARD_SELECTED:
		SelectCard(msg->FindInt32("index"));
		break;

	case LATENCY_CHANGED:
		// SetValue() при выборе карты тоже шлёт это сообщение: не записываем неизменённое значение,
		// иначе карта без задержки «определилась» бы нулём и при запуске о ней уже не спрашивали бы
		if (curCard.Length() > 0 && s_latency->Value() != Prefs.card_latency.Get(curCard.String()))
			Prefs.card_latency.Set(curCard.String(), s_latency->Value());
		break;

	case FINE_TUNE:									// измерение задержки записи выбранной карты
	{
		if (curCard.Length() == 0)
			break;
		fFineLabel = b_fine->Label();
		if (LatencyCalAsk(BMessenger(this), curCard.String())){
			b_fine->SetEnabled(false);
			b_fine->SetLabel(Language.get("CAL_RUNNING"));
		}
		break;
	}

	case LATENCY_RESULT:							// ответ потока измерения
	{
		b_fine->SetEnabled(curCard.Length() > 0);
		b_fine->SetLabel(fFineLabel.String());
		BString card;
		int32 ms = 0;
		if (LatencyCalFinish(msg, &card, &ms)){
			Prefs.card_latency.Set(card.String(), ms);
			if (card == curCard)
				s_latency->SetValue(ms);
		}
		break;
	}

	case FONT_SELECTED:								// выбран саундфонт для импорта MIDI
	{
		const int32 i = l_fonts->CurrentSelection();
		if (i >= 0 && (size_t)i < fonts.size())
			Prefs.midi_soundfont.SetTo(fonts[i].c_str());
		break;
	}

	default:
		BView::MessageReceived(msg);
		break;
	}
}
