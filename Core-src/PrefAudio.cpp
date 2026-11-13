#include <LayoutBuilder.h>
#include <InterfaceKit.h>
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

	s_latency = new BSpinner(NULL, Language.get("REC_LATENCY"), new BMessage(LATENCY_CHANGED));
	s_latency->SetRange(0, 1000);
	s_latency->SetValue(Prefs.rec_latency_ms);

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

	ShowFrames(Prefs.buffer_size / FRAME_BYTES);
}

PrefAudio::~PrefAudio()
{
}

void PrefAudio::AttachedToWindow()
{
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

	case LATENCY_CHANGED:
		Prefs.rec_latency_ms = s_latency->Value();
		break;

	case FINE_TUNE:									// измерение задержки записи
	{
		if (Recorder.IsRecording() || Pool.IsPlaying()){
			(new BAlert(NULL, Language.get("CAL_ERR_BUSY"), Language.get("OK")))->Go();
			break;
		}
		BAlert *a = new BAlert(NULL, Language.get("CAL_INTRO"), Language.get("CANCEL"), Language.get("CAL_START"));
		if (a->Go() != 1)
			break;
		b_fine->SetEnabled(false);
		fFineLabel = b_fine->Label();
		b_fine->SetLabel(Language.get("CAL_RUNNING"));
		LatencyCalStart(BMessenger(this));
		break;
	}

	case LATENCY_RESULT:							// ответ потока измерения
	{
		b_fine->SetEnabled(true);
		b_fine->SetLabel(fFineLabel.String());
		if (msg->FindBool("ok")){
			int32 ms = (int32)(msg->FindFloat("ms") + 0.5f);
			if (ms < 0) ms = 0;
			if (ms > 1000) ms = 1000;
			Prefs.rec_latency_ms = ms;
			s_latency->SetValue(ms);
			char s[200];
			snprintf(s, sizeof(s), Language.get("CAL_RESULT"), (int)ms);
			BString full(s);
			const char *diag = msg->FindString("diag");
			if (diag && diag[0])
				full << "\n\n[diag]\n" << diag;
			(new BAlert(NULL, full.String(), Language.get("OK")))->Go();
		}else{
			const char *key = msg->FindString("err");
			BString text(Language.get(key && key[0] ? key : "CAL_ERR_NO_SIGNAL"));
			const char *diag = msg->FindString("diag");
			if (diag && diag[0])
				text << "\n\n[diag]\n" << diag;
			(new BAlert(NULL, text.String(), Language.get("OK")))->Go();
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
