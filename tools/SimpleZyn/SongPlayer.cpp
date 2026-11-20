#include "SongPlayer.h"

#include <algorithm>

SongPlayer::SongPlayer()
	:
	BMidiLocalProducer("Zyn MIDI Player"),
	fLock("player"),
	fNext(0),
	fPlaying(false),
	fBasePos(0),
	fBaseTime(0),
	fTempo(100),
	fLoop(false),
	fSendPrograms(false),
	fSendControllers(false),
	fThread(-1),
	fQuit(false)
{
	for (int ch = 0; ch < 16; ch++) {
		fChannelOn[ch] = true;
		fLastNote[ch] = 0;
		fLastVelocity[ch] = 0;
	}
}


bool
SongPlayer::Start()
{
	if (!IsValid())
		return false;
	Register();
	fQuit = false;
	fThread = spawn_thread(_ThreadEntry, "MIDI player", B_URGENT_DISPLAY_PRIORITY, this);
	if (fThread < B_OK)
		return false;
	resume_thread(fThread);
	return true;
}


void
SongPlayer::Shutdown()
{
	if (fThread >= B_OK) {
		fQuit = true;
		status_t result;
		wait_for_thread(fThread, &result);
		fThread = -1;
	}
	{
		BAutolock lock(fLock);
		fPlaying = false;
		_SilenceLocked();
	}
	Unregister();
}


void
SongPlayer::SetSong(const SmfSong& song)
{
	BAutolock lock(fLock);
	fPlaying = false;
	_SilenceLocked();
	fSong = song;
	_SeekLocked(0);
}


void
SongPlayer::Play()
{
	BAutolock lock(fLock);
	if (fSong.events.empty() || fPlaying)
		return;
	if (fBasePos >= fSong.durationUs)
		_SeekLocked(0);
	fBaseTime = system_time();
	fPlaying = true;
}


void
SongPlayer::Pause()
{
	BAutolock lock(fLock);
	if (!fPlaying)
		return;
	fBasePos = _PositionLocked();
	fPlaying = false;
	_SilenceLocked();
}


void
SongPlayer::Stop()
{
	BAutolock lock(fLock);
	fPlaying = false;
	_SilenceLocked();
	_SeekLocked(0);
}


void
SongPlayer::Seek(int64 us)
{
	BAutolock lock(fLock);
	_SilenceLocked();
	_SeekLocked(us);
}


void
SongPlayer::SetTempo(int percent)
{
	if (percent < 25)
		percent = 25;
	if (percent > 400)
		percent = 400;
	BAutolock lock(fLock);
	if (fPlaying) {
		fBasePos = _PositionLocked();
		fBaseTime = system_time();
	}
	fTempo = percent;
}


void
SongPlayer::SetLoop(bool loop)
{
	BAutolock lock(fLock);
	fLoop = loop;
}


void
SongPlayer::SetSendPrograms(bool send)
{
	BAutolock lock(fLock);
	fSendPrograms = send;
}


void
SongPlayer::SetSendControllers(bool send)
{
	BAutolock lock(fLock);
	fSendControllers = send;
}


void
SongPlayer::SetChannelEnabled(int channel, bool on)
{
	if (channel < 0 || channel > 15)
		return;
	BAutolock lock(fLock);
	if (fChannelOn[channel] == on)
		return;
	fChannelOn[channel] = on;
	if (!on) {
		// the notes that are sounding would never get their note off
		SprayControlChange(channel, 123, 0, 0);
		SprayControlChange(channel, 120, 0, 0);
	}
}


bigtime_t
SongPlayer::LastNoteTime(int channel, int* velocity)
{
	if (channel < 0 || channel > 15)
		return 0;
	BAutolock lock(fLock);
	if (velocity != NULL)
		*velocity = fLastVelocity[channel];
	return fLastNote[channel];
}


void
SongPlayer::Panic()
{
	BAutolock lock(fLock);
	_SilenceLocked();
}


int64
SongPlayer::Position()
{
	BAutolock lock(fLock);
	return _PositionLocked();
}


int64
SongPlayer::Duration()
{
	BAutolock lock(fLock);
	return fSong.durationUs;
}


bool
SongPlayer::IsPlaying()
{
	BAutolock lock(fLock);
	return fPlaying;
}


// #pragma mark - private


int32
SongPlayer::_ThreadEntry(void* self)
{
	static_cast<SongPlayer*>(self)->_Run();
	return 0;
}


void
SongPlayer::_Run()
{
	while (!fQuit) {
		bigtime_t wait = 5000;
		{
			BAutolock lock(fLock);
			if (fPlaying) {
				int64 pos = _PositionLocked();
				while (fNext < fSong.events.size() && fSong.events[fNext].us <= pos)
					_SendLocked(fSong.events[fNext++]);

				if (fNext >= fSong.events.size() && pos >= fSong.durationUs) {
					_SilenceLocked();
					if (fLoop && fSong.durationUs > 0) {
						_SeekLocked(0);
						fBaseTime = system_time();
					} else {
						fPlaying = false;
						fBasePos = fSong.durationUs;
					}
				} else {
					int64 target = fNext < fSong.events.size()
						? fSong.events[fNext].us : fSong.durationUs;
					int64 gap = (target - pos) * 100 / fTempo;
					wait = (bigtime_t)std::max<int64>(100, std::min<int64>(gap, 5000));
				}
			}
		}
		snooze(wait);
	}
}


int64
SongPlayer::_PositionLocked()
{
	if (!fPlaying)
		return fBasePos;
	int64 pos = fBasePos + (system_time() - fBaseTime) * fTempo / 100;
	return std::min<int64>(pos, fSong.durationUs);
}


// What the "send programs" and "send controllers" switches allow through.
// Notes, pitch bend, pressure, modulation (1), expression (11) and pedals (64-67)
// always pass: they are the playing itself, not the setup of the instrument.
bool
SongPlayer::_Passes(const SmfEvent& e) const
{
	uint8 cmd = e.status & 0xF0;
	if (!fChannelOn[e.status & 0x0F])
		return false;
	if (cmd == 0xC0)
		return fSendPrograms;
	if (cmd == 0xB0) {
		uint8 cc = e.d1;
		if (cc == 0 || cc == 32)
			return fSendPrograms;
		if (cc == 1 || cc == 11 || (cc >= 64 && cc <= 67) || cc >= 120)
			return true;
		return fSendControllers;
	}
	return true;
}


void
SongPlayer::_SendLocked(const SmfEvent& e)
{
	if (!_Passes(e))
		return;
	uchar ch = e.status & 0x0F;
	switch (e.status & 0xF0) {
		case 0x80:	SprayNoteOff(ch, e.d1, e.d2, 0); break;
		case 0x90:
			SprayNoteOn(ch, e.d1, e.d2, 0);
			fLastNote[ch] = system_time();
			fLastVelocity[ch] = e.d2;
			break;
		case 0xA0:	SprayKeyPressure(ch, e.d1, e.d2, 0); break;
		case 0xB0:	SprayControlChange(ch, e.d1, e.d2, 0); break;
		case 0xC0:	SprayProgramChange(ch, e.d1, 0); break;
		case 0xD0:	SprayChannelPressure(ch, e.d1, 0); break;
		case 0xE0:	SprayPitchBend(ch, e.d1, e.d2, 0); break;
	}
}


void
SongPlayer::_SilenceLocked()
{
	for (uchar ch = 0; ch < 16; ch++) {
		SprayControlChange(ch, 64, 0, 0);		// sustain pedal off
		SprayControlChange(ch, 123, 0, 0);		// all notes off
		SprayControlChange(ch, 120, 0, 0);		// all sound off
	}
}


// Sets the position and, when it is not the beginning, sends the last programs
// and controller values that were in force there (so the sound is right).
void
SongPlayer::_SeekLocked(int64 us)
{
	if (us < 0)
		us = 0;
	if (us > fSong.durationUs)
		us = fSong.durationUs;
	fBasePos = us;
	fBaseTime = system_time();

	size_t n = 0;
	while (n < fSong.events.size() && fSong.events[n].us < us)
		n++;
	fNext = n;

	if (n == 0)
		return;
	int program[16];
	int controller[16][120];
	for (int c = 0; c < 16; c++) {
		program[c] = -1;
		for (int k = 0; k < 120; k++)
			controller[c][k] = -1;
	}
	for (size_t i = 0; i < n; i++) {
		const SmfEvent& e = fSong.events[i];
		if (!_Passes(e))
			continue;
		int c = e.status & 0x0F;
		if ((e.status & 0xF0) == 0xC0)
			program[c] = e.d1;
		else if ((e.status & 0xF0) == 0xB0 && e.d1 < 120)
			controller[c][e.d1] = e.d2;
	}
	for (int c = 0; c < 16; c++) {
		if (program[c] >= 0)
			SprayProgramChange(c, program[c], 0);
		for (int k = 0; k < 120; k++) {
			if (controller[c][k] >= 0)
				SprayControlChange(c, k, controller[c][k], 0);
		}
	}
}
