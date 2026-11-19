// ZynControl - the commands SimpleZyn sends to the ZynAddSubFX engine (OSC paths of Zyn 3.0).
#ifndef ZYN_CONTROL_H
#define ZYN_CONTROL_H

#include <string>
#include "Osc.h"

#define ZYN_PARTS 16

class ZynControl {
public:
	bool		Connect(int port) { return fOsc.Open("127.0.0.1", port); }
	void		Disconnect() { fOsc.Close(); }
	bool		IsConnected() const { return fOsc.IsOpen(); }
	long		SentCount() const { return fOsc.SentCount(); }

	// First-time setup: a reverb in system effect 1 (it is used by the "Reverb" slider)
	void		Setup();

	void		LoadInstrument(int part, const std::string& file);	// also switches the part on
	void		SetEnabled(int part, bool on);
	void		SetVolume(int part, int value);						// 0..127
	void		SetPan(int part, int value);						// 0..127, 64 = centre
	void		SetChannel(int part, int channel);					// 0..15 (MIDI channel 1..16)
	void		SetMuted(int part, bool muted);						// keeps the part on but ignores new notes
	void		SetMasterVolume(int value);							// 0..127
	void		SetReverb(int value);								// 0..127: how much of every part goes to the reverb

	// Recording into a WAV file (the engine writes the file)
	void		RecordStart(const std::string& path);
	void		RecordStop();
	// the engine writes the WAV header only when it closes the file, that is when
	// the next file is prepared: this makes it close the finished recording now
	void		RecordFinish();

private:
	std::string	PartPath(int part, const char* name) const;

	OscClient	fOsc;
};

#endif
