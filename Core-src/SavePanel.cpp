/*
   	Copyright (c) 2003, Xentronix
	Author: Frans van Nispen (frans@xentronix.com)
	All rights reserved.
	
	Redistribution and use in source and binary forms, with or without modification,
	are permitted provided that the following conditions are met:
	
	Redistributions of source code must retain the above copyright notice, this list
	of conditions and the following disclaimer. Redistributions in binary form must
	reproduce the above copyright notice, this list of conditions and the following
	disclaimer in the documentation and/or other materials provided with the distribution. 
	
	Neither the name of Xentronix nor the names of its contributors may be used
	to endorse or promote products derived from this software without specific prior
	written permission. 
	
	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
	EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
	OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
	SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
	INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
	PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
	INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
	LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
	OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <InterfaceKit.h>
#include <TranslationKit.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <vector>

#include "Globals.h"
#include "SavePanel.h"

// выбор формата и кодека запоминается до конца работы; по умолчанию WAV и несжатый звук
static BString sLastFormat, sLastCodec, sLastCodecFormat;

/*******************************************************
*
*******************************************************/
// ------------------- CodecMenuItem -------------------
CodecMenuItem::CodecMenuItem(media_codec_info *ci, uint32 msg_type)
	: BMenuItem(ci->pretty_name, new BMessage(msg_type))
{
	memcpy(&fCodecInfo, ci, sizeof(fCodecInfo));
}


CodecMenuItem::~CodecMenuItem()
{
}

// ------------------- FileFormatMenuItem -------------------
FileFormatMenuItem::FileFormatMenuItem(media_file_format *format)
	: BMenuItem(format->pretty_name, new BMessage(PANEL_FORMAT))
{
	memcpy(&fFileFormat, format, sizeof(fFileFormat));
}


FileFormatMenuItem::~FileFormatMenuItem()
{
}


/*******************************************************
*   Панель «Сохранить как...» / «Миксдаун как...»
*
*   Внизу три строки:
*     1. имя файла на всю ширину;
*     2. «Формат» (Несжатый PCM / Сжатый) и кнопка «Сохранить»;
*     3. «Кодек» (активен для сжатого формата), кнопка «Опции...» и «Отменить».
*   Несжатый - это WAV (AIFF в TODO). Сжатые форматы пока пустые: сначала смотрим внешний вид.
*******************************************************/
SavePanel::SavePanel(BHandler *handler, uint32 saveMessage)
	: BFilePanel(B_SAVE_PANEL, new BMessenger(handler), NULL, B_FILE_NODE, false, new BMessage(saveMessage), NULL, true, true),
	fFormatMenu(NULL), fAudioMenu(NULL), fOptions(NULL), fHaveWav(false), fHaveRaw(false), fCompressed(false)
{
	FindWavFormat();

	if (Window()->Lock()) {
		float minw, maxw, minh, maxh;
		Window()->GetSizeLimits(&minw, &maxw, &minh, &maxh);
		minw = 420;
		minh = 340;
		Window()->SetSizeLimits(minw, maxw, minh, maxh);
		Window()->ResizeTo(MAX(Window()->Frame().Width(), minw), MAX(Window()->Frame().Height(), minh));
		Window()->SetTitle(Language.get("PANEL_SAVE"));

		// Add the new BHandler to the window's looper
		Window()->AddHandler(this);

		BView *background = Window()->ChildAt(0);
		BView *poseview = background->FindView("PoseView");
		BButton *insert = (BButton *)background->FindView("default button");
		BButton *cancel = (BButton *)background->FindView("cancel button");
		BScrollBar *hscrollbar = (BScrollBar *)background->FindView("HScrollBar");
		BScrollBar *vscrollbar = (BScrollBar *)background->FindView("VScrollBar");
		BView *countvw = background->FindView("CountVw");
		BView *textview = background->FindView("text view");
		if (!poseview || !insert || !cancel || !hscrollbar || !vscrollbar || !countvw || !textview) {
			Window()->Unlock();
			return;
		}

		// Вложенные виды Tracker лежат не прямо в фоне: положение берём в координатах фона
		struct Pos {
			static BPoint In(BView *bg, BView *v) { return bg->ConvertFromScreen(v->ConvertToScreen(BPoint(0, 0))); }
		};

		// Размеры кнопок и высота полей остаются системными, меняется только положение и ширина.
		const float margin = 12, gap = 6;
		const BRect B = background->Bounds();
		const float bh = insert->Bounds().Height();			// высота системной кнопки
		const float bw = insert->Bounds().Width();
		const float fh = textview->Bounds().Height();		// высота полей: как была (хорошо подобрана)
		const float rh = MAX(bh, fh);						// высота строки
		const float row3 = B.bottom - margin - rh;
		const float row2 = row3 - gap - rh;
		const float row1 = row2 - gap - rh;

		// кнопки у правого края: «Сохранить» во второй строке, «Отменить» в третьей
		insert->MoveBy(B.right - margin - bw - Pos::In(background, insert).x, row2 + (rh - bh) / 2 - Pos::In(background, insert).y);
		insert->SetResizingMode(B_FOLLOW_RIGHT | B_FOLLOW_BOTTOM);
		cancel->MoveBy(B.right - margin - bw - Pos::In(background, cancel).x, row3 + (rh - bh) / 2 - Pos::In(background, cancel).y);
		cancel->SetResizingMode(B_FOLLOW_RIGHT | B_FOLLOW_BOTTOM);

		// первая строка: имя файла до правого края
		textview->MoveBy(margin - Pos::In(background, textview).x, row1 + (rh - fh) / 2 - Pos::In(background, textview).y);
		textview->ResizeTo(B.Width() - 2 * margin, fh);
		textview->SetResizingMode(B_FOLLOW_LEFT_RIGHT | B_FOLLOW_BOTTOM);

		// список файлов заканчивается над первой строкой
		// низ блока списка - самый нижний из его видов (список, обе полосы прокрутки, счётчик)
		float listBottom = 0;
		BView *parts[4] = { poseview, hscrollbar, vscrollbar, countvw };
		for (int i = 0; i < 4; i++)
			listBottom = MAX(listBottom, Pos::In(background, parts[i]).y + parts[i]->Bounds().Height());
		float delta = (row1 - gap) - listBottom;
		if (poseview->Bounds().Height() + delta < 60)				// не сжимать список до нуля
			delta = 60 - poseview->Bounds().Height();
		poseview->ResizeBy(0, delta);
		hscrollbar->MoveBy(0, delta);
		vscrollbar->ResizeBy(0, delta);
		countvw->MoveBy(0, delta);

		// подписи одной ширины, чтобы поля начинались ровно
		const float label = MAX(be_plain_font->StringWidth(Language.get("FORMAT")), be_plain_font->StringWidth(Language.get("CODEC"))) + 10;
		const float insertLeft = B.right - margin - bw;

		// вторая строка: формат - до кнопки «Сохранить»
		BPopUpMenu *popmenu = new BPopUpMenu(Language.get("FORMAT"));
		fFormatMenu = new BMenuField(BRect(margin, row2 + (rh - fh) / 2, insertLeft - gap, row2 + (rh - fh) / 2 + fh), NULL,
			Language.get("FORMAT"), popmenu, true, B_FOLLOW_LEFT_RIGHT | B_FOLLOW_BOTTOM, B_WILL_DRAW | B_NAVIGABLE);
		fFormatMenu->SetDivider(label);
		background->AddChild(fFormatMenu);

		// третья строка: кнопка «Опции...» рядом с «Отменить», кодек - до «Опции...»
		const float ow = MAX(be_plain_font->StringWidth(Language.get("SAVE_OPTIONS")) + 30, bw);
		fOptions = new BButton(BRect(insertLeft - gap - ow, row3 + (rh - bh) / 2, insertLeft - gap, row3 + (rh - bh) / 2 + bh), NULL,
			Language.get("SAVE_OPTIONS"), new BMessage(PANEL_OPTIONS), B_FOLLOW_RIGHT | B_FOLLOW_BOTTOM);
		fOptions->SetTarget(this);
		background->AddChild(fOptions);

		popmenu = new BPopUpMenu(Language.get("CODEC"));
		fAudioMenu = new BMenuField(BRect(margin, row3 + (rh - fh) / 2, insertLeft - gap - ow - gap, row3 + (rh - fh) / 2 + fh), NULL,
			Language.get("CODEC"), popmenu, true, B_FOLLOW_LEFT_RIGHT | B_FOLLOW_BOTTOM, B_WILL_DRAW | B_NAVIGABLE);
		fAudioMenu->SetDivider(label);
		background->AddChild(fAudioMenu);

		BuildFormatMenu();
		UpdateControls();

		Window()->Unlock();
	}
}

/*******************************************************
*   Handle messages from controls we've added
*******************************************************/
void SavePanel::MessageReceived(BMessage *message) {
	switch (message->what){
	case PANEL_FORMAT:
	{
		BMenuItem *f = fFormatMenu->Menu()->FindMarked();
		fCompressed = f && fFormatMenu->Menu()->IndexOf(f) == 1;
		UpdateControls();
		break;
	}

	case PANEL_CODEC:
		break;

	case PANEL_OPTIONS:				// настройки выбранного кодека: пока заготовка
		break;

	default:
		BHandler::MessageReceived(message);
		break;
	}
}

/*******************************************************
*   Формат WAV и несжатый кодек («Raw audio») ищутся у системы
*******************************************************/
void SavePanel::FindWavFormat()
{
	media_file_format mfi;
	int32 cookie = 0;
	fHaveWav = false;
	fHaveRaw = false;
	while (get_next_file_format(&cookie, &mfi) == B_OK) {
		if (mfi.family == B_WAV_FORMAT_FAMILY || strcasecmp(mfi.file_extension, "wav") == 0) {
			fWavFormat = mfi;
			fHaveWav = true;
			break;
		}
	}
	if (!fHaveWav) return;

	media_format format, outfmt;
	memset(&format, 0, sizeof(format));
	memset(&outfmt, 0, sizeof(outfmt));
	media_codec_info info;
	cookie = 0;
	format.type = B_MEDIA_RAW_AUDIO;
	format.u.raw_audio = media_raw_audio_format::wildcard;
	while (get_next_encoder(&cookie, &fWavFormat, &format, &outfmt, &info) == B_OK) {
		BString n(info.pretty_name);
		if (n.IFindFirst("raw") >= 0 || n.IFindFirst("pcm") >= 0) {
			fRawCodec = info;
			fHaveRaw = true;
			break;
		}
		format.type = B_MEDIA_RAW_AUDIO;
		format.u.raw_audio = media_raw_audio_format::wildcard;
	}
}

/*******************************************************
*   Два пункта: несжатый (WAV) и сжатый
*******************************************************/
void SavePanel::BuildFormatMenu() {
	BMenu *menu = fFormatMenu->Menu();
	BMenuItem *item;
	while ((item = menu->RemoveItem((int32)0)) != NULL) delete item;

	item = new BMenuItem(Language.get("SAVE_FORMAT_PCM"), new BMessage(PANEL_FORMAT));
	item->SetTarget(this);
	menu->AddItem(item);
	item->SetMarked(true);
	item = new BMenuItem(Language.get("SAVE_FORMAT_COMPRESSED"), new BMessage(PANEL_FORMAT));
	item->SetTarget(this);
	menu->AddItem(item);
	fCompressed = false;
}

// кодек и «Опции...» работают только для сжатого формата; список кодеков пока пуст
void SavePanel::UpdateControls()
{
	BMenu *menu = fAudioMenu->Menu();
	BMenuItem *item;
	while ((item = menu->RemoveItem((int32)0)) != NULL) delete item;
	fAudioMenu->SetEnabled(fCompressed);
	fOptions->SetEnabled(fCompressed);
}

/*******************************************************
*   Несжатый: WAV и кодек Raw audio; сжатый: пока ничего (format == NULL)
*******************************************************/
void SavePanel::GetSelectedFormatInfo(media_file_format **format, media_codec_info **audio)
{
	*format = NULL;
	*audio = NULL;
	if (fCompressed || !fHaveWav) return;
	*format = &fWavFormat;
	if (fHaveRaw) *audio = &fRawCodec;			// если «Raw audio» нет, пишется вызовом без кодека
}

bool SavePanel::CompressedSelected() const
{
	return fCompressed;
}

void SavePanel::SetFormatInfo(media_file_format *format, media_codec_info *audio)
{
}

SavePanel::~SavePanel() {
}
