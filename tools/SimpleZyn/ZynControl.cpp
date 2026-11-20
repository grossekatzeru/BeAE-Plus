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


std::string
ZynControl::AdPath(int part, const char* suffix)
{
	char path[160];
	snprintf(path, sizeof(path), "/part%d/kit0/adpars/%s", Clamp(part, 0, ZYN_PARTS - 1), suffix);
	return path;
}


void
ZynControl::SetInt(const std::string& path, int value)
{
	fOsc.Send(path.c_str(), "i", value);
}


void
ZynControl::SetFloat(const std::string& path, float value)
{
	fOsc.Send(path.c_str(), "f", value);
}


void
ZynControl::SetBool(const std::string& path, bool value)
{
	fOsc.Send(path.c_str(), value ? "T" : "F");
}


void
ZynControl::Perform(const std::string& path)
{
	fOsc.Send(path.c_str(), "");
}


bool
ZynControl::Query(const std::string& path, OscMessage& answer, int timeoutMs)
{
	if (!fOsc.IsOpen())
		return false;
	OscMessage old;
	while (fOsc.Receive(old, 0)) {
	}										// forget what the engine said before
	if (!fOsc.Send(path.c_str(), ""))
		return false;
	// the engine may also send other messages: wait for the one with our path
	for (int tries = 0; tries < 20; tries++) {
		if (!fOsc.Receive(answer, timeoutMs))
			return false;
		if (answer.path == path && !answer.args.empty())
			return true;
	}
	return false;
}


bool
ZynControl::GetInt(const std::string& path, int* value, int timeoutMs)
{
	OscMessage m;
	if (!Query(path, m, timeoutMs))
		return false;
	const OscArg& a = m.args[0];
	if (a.type == 'i' || a.type == 'T' || a.type == 'F')
		*value = a.i;
	else if (a.type == 'f')
		*value = (int)(a.f + (a.f < 0 ? -0.5f : 0.5f));
	else
		return false;
	return true;
}


bool
ZynControl::GetFloat(const std::string& path, float* value, int timeoutMs)
{
	OscMessage m;
	if (!Query(path, m, timeoutMs))
		return false;
	const OscArg& a = m.args[0];
	if (a.type == 'f')
		*value = a.f;
	else if (a.type == 'i')
		*value = (float)a.i;
	else
		return false;
	return true;
}


void
ZynControl::SaveInstrument(int part, const std::string& file)
{
	fOsc.Send("/save_xiz", "is", Clamp(part, 0, ZYN_PARTS - 1), file.c_str());
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
