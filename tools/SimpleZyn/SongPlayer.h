// SongPlayer.h - plays a SmfSong as a MIDI Kit producer (Haiku).
#ifndef SONG_PLAYER_H
#define SONG_PLAYER_H

#include <Autolock.h>
#include <Locker.h>
#include <MidiProducer.h>
#include <OS.h>

#include "SmfFile.h"

class SongPlayer : public BMidiLocalProducer {
public:
					SongPlayer();

	bool			Start();					// registers the producer, starts the timing thread
	void			Shutdown();					// stops everything, unregisters

	void			SetSong(const SmfSong& song);
	void			Play();
	void			Pause();
	void			Stop();						// pause and go to the beginning
	void			Seek(int64 us);
	void			SetTempo(int percent);		// 25..400
	void			SetLoop(bool loop);
	void			SetSendPrograms(bool send);		// program changes and bank select (CC 0, 32)
	void			SetSendControllers(bool send);	// volume, pan, effects and other settings (CC)
	void			Panic();					// silence everything
	void			SetChannelEnabled(int channel, bool on);	// 0..15: off = nothing is sent for it
	bigtime_t		LastNoteTime(int channel, int* velocity);	// when a note of the channel started

	int64			Position();
	int64			Duration();
	bool			IsPlaying();

private:
	static int32	_ThreadEntry(void* self);
	void			_Run();
	int64			_PositionLocked();
	void			_SendLocked(const SmfEvent& e);
	void			_SilenceLocked();
	void			_SeekLocked(int64 us);
	bool			_Passes(const SmfEvent& e) const;

	BLocker			fLock;
	SmfSong			fSong;
	size_t			fNext;
	bool			fPlaying;
	int64			fBasePos;
	bigtime_t		fBaseTime;
	int				fTempo;
	bool			fLoop;
	bool			fSendPrograms;
	bool			fSendControllers;
	bool			fChannelOn[16];
	bigtime_t		fLastNote[16];
	int				fLastVelocity[16];
	thread_id		fThread;
	volatile bool	fQuit;
};

#endif
