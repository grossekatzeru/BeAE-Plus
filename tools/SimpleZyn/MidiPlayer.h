// MidiPlayer - a window that plays a .mid file into ZynAddSubFX. Every channel of the file
// is a row of a table: the name of the track, the activity, where it sounds, the instrument of
// the Zyn part that plays it, mute and solo.
#ifndef MIDI_PLAYER_H
#define MIDI_PLAYER_H

#include <Window.h>
#include <String.h>
#include <string>
#include <vector>

#include "Banks.h"
#include "SmfFile.h"

class BButton;
class BCheckBox;
class BFilePanel;
class BGroupView;
class BMenuField;
class BMessageRunner;
class BSlider;
class BStringView;
class BView;
class SongPlayer;
class ZynControl;
class ActivityView;

class MidiPlayerWindow : public BWindow {
public:
	MidiPlayerWindow(ZynControl* zyn, const std::vector<Bank>& banks);

	virtual bool	QuitRequested();			// hides the window; the owner calls Quit() when it ends
	virtual void	Quit();
	virtual void	MessageReceived(BMessage* message);

	// the banks have been scanned again (called from another thread, it may do nothing)
	void			SetBanks(const std::vector<Bank>& banks);

private:
	enum { kRoutes = 3 };
	struct Row {
		BStringView*	channel;
		BStringView*	name;
		ActivityView*	activity;
		BMenuField*		route;
		BButton*		instrument;
		BSlider*		volume;			// the volume of the Zyn part of this channel
		BCheckBox*		mute;
		BCheckBox*		solo;
		int				routeIndex;		// 0 Zyn, 1 GM synth, 2 off
		bool			used;			// the file plays this channel
	};

	void			_Build();
	void			_Load(const entry_ref& ref);
	void			_ShowSong(const SmfSong& song);
	void			_ShowRoute(int channel);
	void			_ApplyChannels();	// route, mute and solo of all rows go to the player
	void			_PickInstrument(int channel);
	void			_ShowTime(int64 us);
	void			_UpdateDisplay();
	void			_RefreshDestination();
	void			_Connect(int32 id);

	ZynControl*		fZyn;
	SongPlayer*		fPlayer;
	bool			fPlayerOk;
	std::vector<Bank>	fBanks;
	BFilePanel*		fPanel;
	BMessageRunner*	fRunner;
	int				fTicks;

	BStringView*	fFile;
	BButton*		fPlay;
	BButton*		fStopButton;
	BSlider*		fPos;
	BStringView*	fTime;
	BSlider*		fTempo;
	BCheckBox*		fLoop;
	BStringView*	fStatus;
	BGroupView*		fTable;
	Row				fRows[16];

	int32			fDestId;
	bool			fDragging;
	bool			fLoaded;
};

#endif
