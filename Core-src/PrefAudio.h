/*
 * PrefAudio - страница настроек «Аудио»: размер буфера воспроизведения и поправка задержки записи.
 */
#ifndef _PREF_AUDIO_H
#define _PREF_AUDIO_H

#include <Application.h>
#include <AppKit.h>
#include <InterfaceKit.h>
#include <Spinner.h>
#include <string>
#include <vector>

class PrefAudio : public BView {
public:
	PrefAudio();
	~PrefAudio();
	virtual void AttachedToWindow();
	virtual void MessageReceived(BMessage*);

private:
	void	ShowFrames(int32 frames);		// значение в поле, бегунке и подсказке про задержку записи
	void	SetFrames(int32 frames, bool apply);
	void	UpdateHint();
	void	ApplyBuffer();					// перезапустить проигрыватель с новым буфером

	BSlider			*s_buffer;
	BTextControl	*t_buffer;
	BStringView		*v_rec_hint;
	BSpinner		*s_latency;
	BButton			*b_fine;
	BString			fFineLabel;		// подпись кнопки до начала измерения
	BListView		*l_fonts;			// саундфонты для импорта MIDI
	BStringView		*v_fonts;			// подсказка или «не найдено»
	std::vector<std::string> fonts;
};

#endif
