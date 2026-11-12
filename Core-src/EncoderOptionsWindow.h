/*
 * Окно «Опции…» панели сохранения для сжатого формата: настройки выбранного внешнего кодировщика
 * (lame, oggenc, flac, opusenc) и теги. Значения сохраняются в настройках программы (Prefs.encSettings).
 */
#ifndef ENCODER_OPTIONS_WINDOW_H
#define ENCODER_OPTIONS_WINDOW_H

#include <Window.h>
#include <Messenger.h>
#include "ExtEncoder.h"

class BMenuField;
class BSpinner;
class BTextControl;

class EncoderOptionsWindow : public BWindow {
public:
	EncoderOptionsWindow(ExtEncoder::Id id, BWindow *parent);
	virtual void MessageReceived(BMessage *msg);

private:
	void UpdateEnabled();
	void Apply();							// из полей в настройки (Prefs)

	ExtEncoder::Id	fId;
	BMenuField		*fMode, *fStereo;
	BSpinner		*fQuality, *fBitrate, *fLevel;
	BTextControl	*fArtist, *fAlbum, *fYear, *fComment;
};

#endif
