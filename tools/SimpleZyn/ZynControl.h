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

	// The reverb itself (system effect 1)
	void		SetReverbPreset(int preset);						// 0..12, see kReverbPresets in main.cpp
	void		SetReverbType(int type);							// 0 Random, 1 Freeverb, 2 Bandwidth

	// Any parameter of an instrument's ADsynth: path = "/part0/kit0/adpars/" + suffix
	static std::string	AdPath(int part, const char* suffix);
	void		SetInt(const std::string& path, int value);
	void		SetFloat(const std::string& path, float value);
	void		SetBool(const std::string& path, bool value);
	void		Perform(const std::string& path);					// a message without arguments
	// The engine answers a request without arguments with the value. false: no answer in time.
	bool		GetInt(const std::string& path, int* value, int timeoutMs = 150);
	bool		GetFloat(const std::string& path, float* value, int timeoutMs = 150);
	// Writes the instrument of a part into a .xiz file (the engine writes it)
	void		SaveInstrument(int part, const std::string& file);

	// Recording into a WAV file (the engine writes the file)
	void		RecordStart(const std::string& path);
	void		RecordStop();
	// the engine writes the WAV header only when it closes the file, that is when
	// the next file is prepared: this makes it close the finished recording now
	void		RecordFinish();

private:
	bool		Query(const std::string& path, OscMessage& answer, int timeoutMs);

	std::string	PartPath(int part, const char* name) const;

	OscClient	fOsc;
};

#endif
