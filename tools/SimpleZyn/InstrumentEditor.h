// InstrumentEditor - a window with the main parameters of the ADsynth of one part
// (amplitude envelope, filter, oscillator, vibrato, volume) and "Save instrument as...".
#ifndef INSTRUMENT_EDITOR_H
#define INSTRUMENT_EDITOR_H

#include <Window.h>
#include <Messenger.h>
#include <string>

class BCheckBox;
class BFilePanel;
class BMenuField;
class BSlider;
class BStringView;
class ZynControl;

class InstrumentEditor : public BWindow {
public:
	// owner gets a message with "savedWhat" as its code after an instrument has been saved
	InstrumentEditor(ZynControl* zyn, const BMessenger& owner, uint32 savedWhat);
	virtual ~InstrumentEditor();

	virtual bool	QuitRequested();			// hides the window; the owner calls Quit() when it ends
	virtual void	MessageReceived(BMessage* message);

	// Shows (and reads from the engine) the instrument of this part.
	// Post it as a message: see MsgSetPart()
	static BMessage MsgSetPart(int part, const char* instrumentName);
	void			ShowPart(int part, const char* instrumentName);

private:
	void			_Build();
	void			_Load();
	std::string		_Path(const char* suffix) const;	// "VoicePar0/..." is the selected voice
	void			_Send(int index);
	void			_ShowValue(int index, bool inStatus);
	void			_Save(const BMessage* message);

	ZynControl*		fZyn;
	BMessenger		fOwner;
	uint32			fSavedWhat;
	int				fPart;
	std::string		fName;
	BSlider*		fSliders[40];
	BMenuField*		fWave;
	BMenuField*		fFilterType;
	BMenuField*		fVoiceMenu;
	BCheckBox*		fVoiceOn;
	int				fVoice;			// the voice (0..7) of the Voice box
	BStringView*	fStatus;
	BFilePanel*		fSavePanel;
};

#endif
