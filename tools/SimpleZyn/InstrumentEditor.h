// InstrumentEditor - a window with the main parameters of the ADsynth of one part
// (amplitude envelope, filter, oscillator, vibrato, volume) and "Save instrument as...".
#ifndef INSTRUMENT_EDITOR_H
#define INSTRUMENT_EDITOR_H

#include <Window.h>
#include <Messenger.h>
#include <OS.h>
#include <string>

class BBox;
class BCheckBox;
class BMessageRunner;
class BGroupView;
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
	void			_ApplyLoaded(const BMessage* result);
	std::string		_Path(const char* suffix) const;
	void			_BuildEffects(BGroupView* row);
	void			_SetFxType(int slot, int type);		// menus and sliders for this effect type
	void			_SendFxSlider(int slot, int k);
	std::string		_FxPath(int slot, const char* name, int index = -1) const;	// "VoicePar0/..." is the selected voice
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
	bool			fLoading;		// the thread that reads the engine is running
	bool			fReload;		// ... and the part or the voice has changed meanwhile
	volatile bool	fCancel;
	bool			fApplying;
	int				fGeneration;
	thread_id		fLoader;
	int32			fLoadedValue[40];	// what the sliders were set to from the engine

	BStringView*	fStatus;
	BFilePanel*		fSavePanel;

	// the effects of the part
	struct FxSlot {
		BMenuField*	typeField;
		BMenuField*	presetField;
		BBox*		boxes[5];
		BSlider*	sliders[5];
		bool		shown[5];
		int			type;
		int			band;			// the band of the EQ that the sliders edit
		int32		loaded[5];
	};
	FxSlot			fFx[3];
	BCheckBox*		fStereo;
	BMessageRunner*	fFxRunner;			// reads the effect again a moment after a change of its type
};

#endif
