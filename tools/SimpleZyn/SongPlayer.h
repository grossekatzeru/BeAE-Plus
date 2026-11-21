// SongPlayer.h - plays a SmfSong as a MIDI Kit producer (Haiku).
#ifndef SONG_PLAYER_H
#define SONG_PLAYER_H

#include <Autolock.h>
#include <Locker.h>
#include <MidiProducer.h>
#include <MidiSynth.h>
#include <deque>
#include <Locker.h>
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
	// where a channel (0..15) sounds: kRouteZyn (the MIDI output), kRouteGm (the General MIDI
	// synthesizer of the system, played by this program) or kRouteOff (nothing is sent)
	enum { kRouteZyn = 0, kRouteGm = 1, kRouteOff = 2 };
	void			SetChannelRoute(int channel, int route);
	void			RequestGm();				// loads the synthesizer of the system in the background
	bool			IsGmReady();				// loaded
	bool			GmFailed();					// it could not be loaded
	void			SetGmVolume(int channel, int value);	// 0..127
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
	void			_GmPost(uint8 status, uint8 d1, uint8 d2);	// a command for the synthesizer of the system
	void			_GmRun();
	static int32	_GmThreadEntry(void* self);
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
	int				fRoute[16];
	// the synthesizer of the system is used only by its own thread: its calls may wait a long time
	BMidiSynth		fGm;
	BLocker			fGmLock;
	std::deque<uint32>	fGmQueue;			// status | d1 << 8 | d2 << 16; status 0: load the synthesizer
	sem_id			fGmSem;
	thread_id		fGmThread;
	volatile bool	fGmReady;
	volatile bool	fGmLoading;
	volatile bool	fGmFailed;
	bigtime_t		fLastNote[16];
	int				fLastVelocity[16];
	thread_id		fThread;
	volatile bool	fQuit;
};

#endif
