#include <LayoutBuilder.h>
#include <InterfaceKit.h>
#include <SeparatorView.h>
#include <String.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "Globals.h"
#include "AudioRecorder.h"
#include "PrefAudio.h"

#define BUFFER_SLIDER		'abSl'
#define BUFFER_TEXT			'abTx'
#define LATENCY_CHANGED		'abLt'
#define FINE_TUNE			'abFt'

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

	BStringView *soon = new BStringView(NULL, Language.get("AUDIO_COMING_SOON"));
	soon->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_2_TINT);

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
		.Add(soon)
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

	case FINE_TUNE:									// пока пусто
		break;

	default:
		BView::MessageReceived(msg);
		break;
	}
}
