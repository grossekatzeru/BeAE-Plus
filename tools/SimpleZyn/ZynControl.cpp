#include "ZynControl.h"

#include <stdio.h>


std::string
ZynControl::PartPath(int part, const char* name) const
{
	char path[96];
	snprintf(path, sizeof(path), "/part%d/%s", part, name);
	return std::string(path);
}


static int
Clamp(int v, int lo, int hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}


void
ZynControl::Setup()
{
	fOsc.Send("/sysefx0/efftype", "i", 1);			// 1 = Reverb
}


void
ZynControl::LoadInstrument(int part, const std::string& file)
{
	part = Clamp(part, 0, ZYN_PARTS - 1);
	fOsc.Send("/load_xiz", "is", part, file.c_str());
	SetEnabled(part, true);
}


void
ZynControl::SetEnabled(int part, bool on)
{
	part = Clamp(part, 0, ZYN_PARTS - 1);
	if (on)
		fOsc.Send(PartPath(part, "Penabled").c_str(), "T");
	else
		fOsc.Send(PartPath(part, "Penabled").c_str(), "F");
}


void
ZynControl::SetVolume(int part, int value)
{
	part = Clamp(part, 0, ZYN_PARTS - 1);
	fOsc.Send(PartPath(part, "Pvolume").c_str(), "i", Clamp(value, 0, 127));
}


void
ZynControl::SetPan(int part, int value)
{
	part = Clamp(part, 0, ZYN_PARTS - 1);
	fOsc.Send(PartPath(part, "Ppanning").c_str(), "i", Clamp(value, 0, 127));
}


void
ZynControl::SetChannel(int part, int channel)
{
	part = Clamp(part, 0, ZYN_PARTS - 1);
	fOsc.Send(PartPath(part, "Prcvchn").c_str(), "i", Clamp(channel, 0, 15));
}


void
ZynControl::SetMuted(int part, bool muted)
{
	part = Clamp(part, 0, ZYN_PARTS - 1);
	if (muted)
		fOsc.Send(PartPath(part, "Pnoteon").c_str(), "F");
	else
		fOsc.Send(PartPath(part, "Pnoteon").c_str(), "T");
}


void
ZynControl::SetMasterVolume(int value)
{
	fOsc.Send("/Pvolume", "i", Clamp(value, 0, 127));
}


void
ZynControl::SetReverb(int value)
{
	value = Clamp(value, 0, 127);
	for (int part = 0; part < ZYN_PARTS; part++) {
		char path[48];
		snprintf(path, sizeof(path), "/Psysefxvol0/part%d", part);
		fOsc.Send(path, "i", value);
	}
}


void
ZynControl::SetReverbPreset(int preset)
{
	fOsc.Send("/sysefx0/preset", "i", Clamp(preset, 0, 12));
}


void
ZynControl::SetReverbType(int type)
{
	fOsc.Send("/sysefx0/parameter10", "i", Clamp(type, 0, 2));	// effect parameter 10 = reverb type
}


void
ZynControl::RecordStart(const std::string& path)
{
	fOsc.Send("/HDDRecorder/preparefile", "s", path.c_str());
	fOsc.Send("/HDDRecorder/start", "");
}


void
ZynControl::RecordStop()
{
	fOsc.Send("/HDDRecorder/stop", "");
}


void
ZynControl::RecordFinish()
{
	fOsc.Send("/HDDRecorder/preparefile", "s", "/dev/null");
}
