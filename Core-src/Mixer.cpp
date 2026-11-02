#include "Mixer.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

Mixer Mix;

Mixer::Mixer() : fCount(1), fRecordOffset(0)
{
	fLock.clear();
	memset(fTracks, 0, sizeof(fTracks));
	memset(fPeakL, 0, sizeof(fPeakL));
	memset(fPeakR, 0, sizeof(fPeakR));
	MixTrack &t = fTracks[0];
	t.used = true;
	t.owns_data = false;
	t.channels = 2;
	t.gain = 1.0f;
	strcpy(t.name, "Track 1");
}

Mixer::~Mixer()
{
	for (int i=1; i<MIX_MAX_TRACKS; i++)
		if (fTracks[i].used && fTracks[i].owns_data)
			free(fTracks[i].data);
}

void Mixer::Lock()	{ while (fLock.test_and_set(std::memory_order_acquire)) {} }
void Mixer::Unlock()	{ fLock.clear(std::memory_order_release); }
bool Mixer::TryLock()	{ return !fLock.test_and_set(std::memory_order_acquire); }

int Mixer::AddEmptyTrack(const char *name, int channels)
{
	return AddTrack(name, NULL, 0, channels);
}

int Mixer::AddTrack(const char *name, const float *data, int64_t frames, int channels)
{
	if (channels != 1 && channels != 2) return -1;
	if (frames < 0) frames = 0;
	float *copy = NULL;
	if (frames > 0) {
		copy = (float*)malloc((size_t)frames*channels*sizeof(float));
		if (!copy) return -1;
		if (data) memcpy(copy, data, (size_t)frames*channels*sizeof(float));
		else memset(copy, 0, (size_t)frames*channels*sizeof(float));
	}
	Lock();
	int idx = -1;
	for (int i=1; i<MIX_MAX_TRACKS; i++) {
		if (!fTracks[i].used) { idx = i; break; }
	}
	if (idx < 0) { Unlock(); free(copy); return -1; }
	MixTrack &t = fTracks[idx];
	memset(&t, 0, sizeof(t));
	t.used = true;
	t.owns_data = true;
	t.data = copy;
	t.frames = frames;
	t.channels = channels;
	t.gain = 1.0f;
	if (name) { strncpy(t.name, name, sizeof(t.name)-1); t.name[sizeof(t.name)-1] = 0; }
	else snprintf(t.name, sizeof(t.name), "Track %d", idx+1);
	if (idx >= fCount) fCount = idx+1;
	Unlock();
	return idx;
}

void Mixer::RemoveTrack(int i)
{
	if (i < 1 || i >= MIX_MAX_TRACKS) return;
	Lock();
	if (fTracks[i].used) {
		float *old = fTracks[i].data;
		fTracks[i].used = false;
		fTracks[i].data = NULL;
		fTracks[i].frames = 0;
		while (fCount > 1 && !fTracks[fCount-1].used) fCount--;
		Unlock();
		free(old);
		return;
	}
	Unlock();
}

bool Mixer::SetTrackData(int i, const float *data, int64_t frames)
{
	if (i < 1 || i >= MIX_MAX_TRACKS || frames < 0) return false;
	float *copy = NULL;
	if (fTracks[i].used == false) return false;
	int ch = fTracks[i].channels;
	if (frames > 0) {
		copy = (float*)malloc((size_t)frames*ch*sizeof(float));
		if (!copy) return false;
		memcpy(copy, data, (size_t)frames*ch*sizeof(float));
	}
	Lock();
	float *old = fTracks[i].data;
	fTracks[i].data = copy;
	fTracks[i].frames = frames;
	Unlock();
	free(old);
	return true;
}

bool Mixer::AppendTrackData(int i, const float *data, int64_t frames)
{
	if (i < 1 || i >= MIX_MAX_TRACKS || frames <= 0 || !fTracks[i].used) return false;
	int ch = fTracks[i].channels;
	int64_t nf = fTracks[i].frames + frames;
	// новый буфер, чтобы callback не читал освобождённую память
	float *copy = (float*)malloc((size_t)nf*ch*sizeof(float));
	if (!copy) return false;
	if (fTracks[i].frames > 0)
		memcpy(copy, fTracks[i].data, (size_t)fTracks[i].frames*ch*sizeof(float));
	memcpy(copy + (size_t)fTracks[i].frames*ch, data, (size_t)frames*ch*sizeof(float));
	Lock();
	float *old = fTracks[i].data;
	fTracks[i].data = copy;
	fTracks[i].frames = nf;
	Unlock();
	free(old);
	return true;
}

void Mixer::SetGain(int i, float g)	{ if (Track(i)) fTracks[i].gain = g<0?0:(g>2?2:g); }
void Mixer::SetPan(int i, float p)	{ if (Track(i)) fTracks[i].pan = p<-1?-1:(p>1?1:p); }
void Mixer::SetMute(int i, bool m)	{ if (Track(i)) fTracks[i].mute = m; }
void Mixer::SetSolo(int i, bool s)	{ if (Track(i)) fTracks[i].solo = s; }
void Mixer::SetArmed(int i, bool a)	{ if (Track(i)) fTracks[i].armed = a; }

// Баланс: центр = 1.0 в обоих каналах (как раньше у моно), к краю гасится противоположный канал
void Mixer::Gains(const MixTrack &t, bool anySolo, float &gl, float &gr)
{
	if (t.mute || (anySolo && !t.solo)) { gl = gr = 0; return; }
	float p = t.pan;
	gl = t.gain * (p > 0 ? 1.0f - p : 1.0f);
	gr = t.gain * (p < 0 ? 1.0f + p : 1.0f);
}

void Mixer::Process(float *buf, int frames, int64_t start, bool mixExtras)
{
	bool anySolo = false;
	for (int i=0; i<fCount; i++)
		if (fTracks[i].used && fTracks[i].solo) { anySolo = true; break; }

	// основная дорожка: канал в самом буфере
	{
		const MixTrack &t = fTracks[0];
		float gl, gr;
		Gains(t, anySolo, gl, gr);
		float pl = 0, pr = 0;
		if (gl != 1.0f || gr != 1.0f) {
			for (int k=0; k<frames; k++) { buf[2*k] *= gl; buf[2*k+1] *= gr; }
		}
		for (int k=0; k<frames; k++) {
			float a = fabsf(buf[2*k]), b = fabsf(buf[2*k+1]);
			if (a > pl) pl = a;
			if (b > pr) pr = b;
		}
		fPeakL[0] = pl; fPeakR[0] = pr;
	}

	if (!mixExtras || fCount < 2 || !TryLock()) {
		for (int i=1; i<fCount; i++) fPeakL[i] = fPeakR[i] = 0;
		return;
	}
	for (int i=1; i<fCount; i++) {
		MixTrack &t = fTracks[i];
		fPeakL[i] = fPeakR[i] = 0;
		if (!t.used || !t.data || t.frames <= 0) continue;
		float gl, gr;
		Gains(t, anySolo, gl, gr);
		float pl = 0, pr = 0;
		for (int k=0; k<frames; k++) {
			int64_t pos = start + k;
			if (pos < 0 || pos >= t.frames) continue;
			float l, r;
			if (t.channels == 1) { l = r = t.data[pos]; }
			else { l = t.data[2*pos]; r = t.data[2*pos+1]; }
			l *= gl; r *= gr;
			buf[2*k] += l; buf[2*k+1] += r;
			if (fabsf(l) > pl) pl = fabsf(l);
			if (fabsf(r) > pr) pr = fabsf(r);
		}
		fPeakL[i] = pl; fPeakR[i] = pr;
	}
	Unlock();
}
