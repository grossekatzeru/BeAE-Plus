#include "Mixer.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

Mixer Mix;

Mixer::Mixer() : fCount(1), fRecordOffset(0), fActive(0), fRevision(0)
{
	fLock.clear();
	memset(fTracks, 0, sizeof(fTracks));
	memset(fPeakL, 0, sizeof(fPeakL));
	memset(fPeakR, 0, sizeof(fPeakR));
	MixTrack &t = fTracks[0];
	t.used = true;
	t.owns_data = true;
	t.channels = 2;
	t.rate = 44100.0f;
	t.gain = 1.0f;
	strcpy(t.name, "Track 1");
}

Mixer::~Mixer()
{
	for (int i=0; i<MIX_MAX_TRACKS; i++)
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
	t.rate = 44100.0f;
	t.gain = 1.0f;
	if (name) { strncpy(t.name, name, sizeof(t.name)-1); t.name[sizeof(t.name)-1] = 0; }
	else snprintf(t.name, sizeof(t.name), "Track %d", idx+1);
	if (idx >= fCount) fCount = idx+1;
	Unlock();
	return idx;
}

void Mixer::ResetAll()
{
	float *old[MIX_MAX_TRACKS];
	Lock();
	for (int i=0; i<MIX_MAX_TRACKS; i++) {
		old[i] = fTracks[i].used ? fTracks[i].data : NULL;
		memset(&fTracks[i], 0, sizeof(MixTrack));
	}
	MixTrack &t = fTracks[0];
	t.used = true;
	t.owns_data = true;
	t.channels = 2;
	t.rate = 44100.0f;
	t.gain = 1.0f;
	strcpy(t.name, "Track 1");
	fCount = 1;
	fActive = 0;
	Unlock();
	for (int i=0; i<MIX_MAX_TRACKS; i++) { free(old[i]); fFx[i].Clear(); }
	fRevision++;
}

void Mixer::RemoveTrack(int i)
{
	if (i < 1 || i >= MIX_MAX_TRACKS) return;
	Lock();
	if (fTracks[i].used) {
		float *old = fTracks[i].data;
		if (fActive == i) fActive = 0;
		fTracks[i].used = false;
		fTracks[i].armed = false;
		fTracks[i].data = NULL;
		fTracks[i].frames = 0;
		while (fCount > 1 && !fTracks[fCount-1].used) fCount--;
		Unlock();
		free(old);
		fFx[i].Clear();
		return;
	}
	Unlock();
}

bool Mixer::SetTrackData(int i, const float *data, int64_t frames, int channels, float rate)
{
	if (i < 0 || i >= MIX_MAX_TRACKS || frames < 0 || !fTracks[i].used) return false;
	if (channels != 1 && channels != 2) return false;
	float *copy = NULL;
	if (frames > 0) {
		copy = (float*)malloc((size_t)frames*channels*sizeof(float));
		if (!copy) return false;
		memcpy(copy, data, (size_t)frames*channels*sizeof(float));
	}
	Lock();
	float *old = fTracks[i].data;
	fTracks[i].data = copy;
	fTracks[i].frames = frames;
	fTracks[i].channels = channels;
	if (rate > 0) fTracks[i].rate = rate;
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

void Mixer::SetGain(int i, float g)	{ if (Track(i)) { fTracks[i].gain = g<0?0:(g>2?2:g); fRevision++; } }
void Mixer::SetPan(int i, float p)	{ if (Track(i)) { fTracks[i].pan = p<-1?-1:(p>1?1:p); fRevision++; } }
void Mixer::SetMute(int i, bool m)	{ if (Track(i)) { fTracks[i].mute = m; fRevision++; } }
void Mixer::SetSolo(int i, bool s)	{ if (Track(i)) { fTracks[i].solo = s; fRevision++; } }
void Mixer::SetArmed(int i, bool a)
{
	if (!Track(i)) return;
	if (a) for (int k=0; k<MIX_MAX_TRACKS; k++) fTracks[k].armed = false;
	fTracks[i].armed = a;
}

void Mixer::SetOffset(int i, double seconds)
{
	if (!Track(i)) return;
	fTracks[i].offset = seconds < 0 ? 0 : seconds;
	fRevision++;
}

double Mixer::EndTimeExtras() const
{
	double end = 0;
	for (int i=0; i<MIX_MAX_TRACKS; i++) {
		const MixTrack &t = fTracks[i];
		if (!t.used || i == fActive || !t.data || t.frames <= 0 || t.rate <= 0) continue;
		double e = t.offset + (double)t.frames / t.rate;
		if (e > end) end = e;
	}
	return end;
}

int Mixer::ArmedTrack() const
{
	for (int k=0; k<MIX_MAX_TRACKS; k++)
		if (fTracks[k].used && fTracks[k].armed) return k;
	return -1;
}

// Баланс: центр = 1.0 в обоих каналах (как раньше у моно), к краю гасится противоположный канал
void Mixer::Gains(const MixTrack &t, bool anySolo, float &gl, float &gr)
{
	if (t.mute || (anySolo && !t.solo)) { gl = gr = 0; return; }
	float p = t.pan;
	gl = t.gain * (p > 0 ? 1.0f - p : 1.0f);
	gr = t.gain * (p < 0 ? 1.0f + p : 1.0f);
}

void Mixer::Process(float *buf, int frames, double activePos, double activeRate,
	double sysRate, bool mixExtras)
{
	bool anySolo = false;
	for (int i=0; i<fCount; i++)
		if (fTracks[i].used && fTracks[i].solo) { anySolo = true; break; }

	// активная дорожка: она уже в буфере, применяем её канал
	const int act = fActive;
	{
		const MixTrack &t = fTracks[act];
		float gl, gr;
		Gains(t, anySolo, gl, gr);
		float pl = 0, pr = 0;
		fFx[act].Process(buf, frames, (float)sysRate);
		if (gl != 1.0f || gr != 1.0f) {
			for (int k=0; k<frames; k++) { buf[2*k] *= gl; buf[2*k+1] *= gr; }
		}
		for (int k=0; k<frames; k++) {
			float a = fabsf(buf[2*k]), b = fabsf(buf[2*k+1]);
			if (a > pl) pl = a;
			if (b > pr) pr = b;
		}
		fPeakL[act] = pl; fPeakR[act] = pr;
	}

	if (!mixExtras || fCount < 2 || !TryLock()) {
		for (int i=0; i<fCount; i++) if (i != act) fPeakL[i] = fPeakR[i] = 0;
		return;
	}
	for (int i=0; i<fCount; i++) {
		if (i == act) continue;
		MixTrack &t = fTracks[i];
		fPeakL[i] = fPeakR[i] = 0;
		if (!t.used || !t.data || t.frames <= 0) continue;
		float gl, gr;
		Gains(t, anySolo, gl, gr);
		float pl = 0, pr = 0;
		// позиция в кадрах этой дорожки: то же время, что у активной
		const double aRate = fabs(activeRate) > 1.0 ? fabs(activeRate) : 44100.0;
		const double dir = (activeRate < 0) ? -1.0 : 1.0;
		const double step = dir * (double)t.rate / sysRate;
		// время на общей шкале в начале буфера -> позиция в этой дорожке
		const double now = activePos / aRate + fTracks[act].offset;
		double p = (now - t.offset) * (double)t.rate;
		FxChain &fx = fFx[i];
		const bool useFx = fx.HasEffects();
		// после конца дорожки эффекты (реверберация, эхо) ещё звучат - даём им хвост
		const double tailFrames = useFx ? 4.0 * (double)t.rate : 0.0;
		for (int from = 0; from < frames; from += 4096) {
			const int n = (frames - from < 4096) ? frames - from : 4096;
			const double pEnd = p + step * n;
			const double lo = p < pEnd ? p : pEnd, hi = p < pEnd ? pEnd : p;
			if (hi < 0 || lo >= (double)t.frames + tailFrames) { p = pEnd; continue; }
			for (int k=0; k<n; k++) {
				const double q = p + step * k;
				float l = 0, r = 0;
				if (q >= 0 && q < (double)t.frames) {
					int64_t i0 = (int64_t)q;
					int64_t i1 = (i0 + 1 < t.frames) ? i0 + 1 : i0;
					float fr = (float)(q - (double)i0);
					if (t.channels == 1) {
						l = r = t.data[i0] + (t.data[i1] - t.data[i0]) * fr;
					} else {
						l = t.data[2*i0]   + (t.data[2*i1]   - t.data[2*i0])   * fr;
						r = t.data[2*i0+1] + (t.data[2*i1+1] - t.data[2*i0+1]) * fr;
					}
				}
				fTmp[2*k] = l; fTmp[2*k+1] = r;
			}
			if (useFx) fx.Process(fTmp, n, (float)sysRate);
			for (int k=0; k<n; k++) {
				float l = fTmp[2*k] * gl, r = fTmp[2*k+1] * gr;
				buf[2*(from+k)] += l; buf[2*(from+k)+1] += r;
				if (fabsf(l) > pl) pl = fabsf(l);
				if (fabsf(r) > pr) pr = fabsf(r);
			}
			p = pEnd;
		}
		fPeakL[i] = pl; fPeakR[i] = pr;
	}
	Unlock();
}
