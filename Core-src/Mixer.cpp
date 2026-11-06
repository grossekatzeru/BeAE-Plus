#include "Mixer.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

Mixer Mix;

SourceRef MakeSource(const float *data, int64_t frames, int channels, float rate)
{
	if (frames <= 0 || (channels != 1 && channels != 2) || rate <= 0) return SourceRef();
	MixSource *s = new MixSource();
	s->data = (float*)malloc((size_t)frames * channels * sizeof(float));
	if (!s->data) { delete s; return SourceRef(); }
	if (data) memcpy(s->data, data, (size_t)frames * channels * sizeof(float));
	else memset(s->data, 0, (size_t)frames * channels * sizeof(float));
	s->frames = frames;
	s->channels = channels;
	s->rate = rate;
	return SourceRef(s);
}

Mixer::Mixer() : fCount(1), fRecordOffset(0), fActive(0), fRevision(0), fMeter(Metronome::M44), fMetroVol(0.7f), fMetroOn(false)
{
	fMasterGain = 1.0f; fMasterMute = false; fMasterPeakL = fMasterPeakR = 0;
	fLock.clear();
	fNextNow = 0; fHaveNext = false;
	memset(fTmp, 0, sizeof(fTmp));
	memset(fPeakL, 0, sizeof(fPeakL));
	memset(fPeakR, 0, sizeof(fPeakR));
	fBpm = 120.0; fGridDiv = 4;
	fTracks[0].used = true;
	strcpy(fTracks[0].name, "Track 1");
}

Mixer::~Mixer() {}

void Mixer::Lock()	{ while (fLock.test_and_set(std::memory_order_acquire)) {} }
void Mixer::Unlock()	{ fLock.clear(std::memory_order_release); }
bool Mixer::TryLock()	{ return !fLock.test_and_set(std::memory_order_acquire); }

int Mixer::AddEmptyTrack(const char *name, int channels)
{
	return AddTrack(name, NULL, 0, channels);
}

int Mixer::AddTrack(const char *name, const float *data, int64_t frames, int channels, float rate)
{
	if (channels != 1 && channels != 2) return -1;
	if (frames < 0) frames = 0;
	SourceRef src;
	if (frames > 0) {
		src = MakeSource(data, frames, channels, rate);
		if (!src) return -1;
	}
	Lock();
	int idx = -1;
	for (int i=1; i<MIX_MAX_TRACKS; i++) {
		if (!fTracks[i].used) { idx = i; break; }
	}
	if (idx < 0) { Unlock(); return -1; }
	MixTrack &t = fTracks[idx];
	t.Reset();
	t.used = true;
	t.channels = channels;
	t.rate = rate;
	if (name) { strncpy(t.name, name, sizeof(t.name)-1); t.name[sizeof(t.name)-1] = 0; }
	else snprintf(t.name, sizeof(t.name), "Track %d", idx+1);
	if (src) {
		MixClip c;
		c.src = src; c.start = 0; c.len = frames; c.pos = 0;
		t.clips.push_back(c);
	}
	if (idx >= fCount) fCount = idx+1;
	Unlock();
	return idx;
}

void Mixer::ResetAll()
{
	fHaveNext = false;
	std::vector<MixClip> old[MIX_MAX_TRACKS];		// освободятся после выхода из замка
	Lock();
	for (int i=0; i<MIX_MAX_TRACKS; i++) {
		old[i].swap(fTracks[i].clips);
		fTracks[i].Reset();
	}
	fTracks[0].used = true;
	strcpy(fTracks[0].name, "Track 1");
	fCount = 1;
	fActive = 0;
	fBpm = 120.0; fGridDiv = 4; fMeter = Metronome::M44; fMetroVol = 0.7f;
	Unlock();
	for (int i=0; i<=MIX_MAX_TRACKS; i++) fFx[i].Clear();
	fMasterGain = 1.0f; fMasterMute = false;
	fRevision++;
}

void Mixer::RemoveTrack(int i)
{
	if (i < 1 || i >= MIX_MAX_TRACKS) return;
	std::vector<MixClip> old;
	Lock();
	if (!fTracks[i].used) { Unlock(); return; }
	if (fActive == i) fActive = 0;
	old.swap(fTracks[i].clips);
	fTracks[i].Reset();
	while (fCount > 1 && !fTracks[fCount-1].used) fCount--;
	Unlock();
	fFx[i].Clear();
	fRevision++;
}

// ---- клипы ----
const MixClip *Mixer::Clip(int lane, int idx) const
{
	if (lane < 0 || lane >= MIX_MAX_TRACKS || !fTracks[lane].used) return NULL;
	if (idx < 0 || idx >= (int)fTracks[lane].clips.size()) return NULL;
	return &fTracks[lane].clips[idx];
}

int Mixer::AddClip(int lane, const SourceRef &src, int64_t start, int64_t len, double pos)
{
	if (!Track(lane) || !src || start < 0 || len <= 0 || start + len > src->frames) return -1;
	MixClip c;
	c.src = src; c.start = start; c.len = len; c.pos = pos < 0 ? 0 : pos;
	Lock();
	fTracks[lane].clips.push_back(c);
	int idx = (int)fTracks[lane].clips.size() - 1;
	Unlock();
	fRevision++;
	return idx;
}

bool Mixer::RemoveClip(int lane, int idx)
{
	MixClip dead;
	return TakeClip(lane, idx, dead);
}

bool Mixer::TakeClip(int lane, int idx, MixClip &out)
{
	if (!Clip(lane, idx)) return false;
	Lock();
	out = fTracks[lane].clips[idx];
	fTracks[lane].clips.erase(fTracks[lane].clips.begin() + idx);
	Unlock();
	fRevision++;
	return true;
}

bool Mixer::SetClipPos(int lane, int idx, double pos)
{
	if (!Clip(lane, idx)) return false;
	Lock();
	fTracks[lane].clips[idx].pos = pos < 0 ? 0 : pos;
	Unlock();
	fRevision++;
	return true;
}

bool Mixer::SetClipLocked(int lane, int idx, bool locked)
{
	if (!Clip(lane, idx)) return false;
	Lock();
	fTracks[lane].clips[idx].locked = locked;
	Unlock();
	fRevision++;
	return true;
}

bool Mixer::SetClipSelected(int lane, int idx, bool selected)
{
	if (!Clip(lane, idx)) return false;
	Lock();
	fTracks[lane].clips[idx].selected = selected;
	Unlock();
	return true;
}

bool Mixer::SetClipRange(int lane, int idx, int64_t start, int64_t len)
{
	const MixClip *c = Clip(lane, idx);
	if (!c || start < 0 || len <= 0 || start + len > c->src->frames) return false;
	Lock();
	fTracks[lane].clips[idx].start = start;
	fTracks[lane].clips[idx].len = len;
	Unlock();
	fRevision++;
	return true;
}

void Mixer::ClearClips(int lane)
{
	if (!Track(lane)) return;
	std::vector<MixClip> old;
	Lock();
	old.swap(fTracks[lane].clips);
	Unlock();
	fRevision++;
}

int Mixer::LaneChannels(int lane) const
{
	if (lane < 0 || lane >= MIX_MAX_TRACKS || !fTracks[lane].used) return 2;
	const std::vector<MixClip> &cl = fTracks[lane].clips;
	if (cl.empty()) return fTracks[lane].channels;
	for (size_t i=0; i<cl.size(); i++)
		if (cl[i].src && cl[i].src->channels == 2) return 2;
	return 1;
}

int Mixer::ClipAt(int lane, double time) const
{
	if (lane < 0 || lane >= MIX_MAX_TRACKS || !fTracks[lane].used) return -1;
	int r = -1;
	const std::vector<MixClip> &cl = fTracks[lane].clips;
	for (int i=0; i<(int)cl.size(); i++)
		if (time >= cl[i].pos && time < cl[i].End()) r = i;
	return r;
}

void Mixer::SetGain(int i, float g)	{ if (Track(i)) { fTracks[i].gain = g<0?0:(g>2?2:g); fRevision++; } }
void Mixer::SetPan(int i, float p)	{ if (Track(i)) { fTracks[i].pan = p<-1?-1:(p>1?1:p); fRevision++; } }
void Mixer::SetMute(int i, bool m)	{ if (Track(i)) { fTracks[i].mute = m; fRevision++; } }
void Mixer::SetSolo(int i, bool s)	{ if (Track(i)) { fTracks[i].solo = s; fRevision++; } }
void Mixer::SetGrid(int i, bool g)	{ if (Track(i)) { fTracks[i].grid = g; fRevision++; } }
void Mixer::SetBpm(double bpm)
{
	fBpm = bpm < 20.0 ? 20.0 : (bpm > 300.0 ? 300.0 : bpm);
	fRevision++;
}
void Mixer::SetGridDiv(int div)
{
	if (Metronome::StepUnits(div) > 0) { fGridDiv = div; fRevision++; }
}
void Mixer::SetMeter(int meter)
{
	if (meter < 0 || meter >= Metronome::METER_COUNT) return;
	fMeter = meter;
	fRevision++;
}
double Mixer::Snap(int lane, double t) const
{
	const MixTrack *tr = Track(lane);
	if (!tr || !tr->grid) return t;
	const double s = GridStep();
	const double r = floor(t / s + 0.5) * s;
	return r < 0 ? 0 : r;
}
void Mixer::SetArmed(int i, bool a)
{
	if (!Track(i)) return;
	if (a) for (int k=0; k<MIX_MAX_TRACKS; k++) fTracks[k].armed = false;
	fTracks[i].armed = a;
}

double Mixer::EndTimeExtras() const
{
	double end = 0;
	for (int i=0; i<MIX_MAX_TRACKS; i++) {
		if (!fTracks[i].used) continue;
		for (size_t k=0; k<fTracks[i].clips.size(); k++) {
			double e = fTracks[i].clips[k].End();
			if (e > end) end = e;
		}
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

// Добавляет звук всех клипов дорожки в стерео-буфер dst (n кадров). t0 - время первого кадра (сек),
// tstep - шаг времени на кадр (отрицательный при обратном проигрывании). Линейная интерполяция.
static void RenderClips(const MixTrack &t, float *dst, int n, double t0, double tstep)
{
	for (size_t ci = 0; ci < t.clips.size(); ci++) {
		const MixClip &c = t.clips[ci];
		if (!c.src || c.len <= 0) continue;
		const double rate = c.src->rate;
		double p = (t0 - c.pos) * rate;
		const double pstep = tstep * rate;
		const double pEnd = p + pstep * n;
		const double lo = p < pEnd ? p : pEnd, hi = p < pEnd ? pEnd : p;
		if (hi < 0 || lo >= (double)c.len) continue;
		const float *d = c.src->data + c.start * c.src->channels;
		const int ch = c.src->channels;
		for (int k=0; k<n; k++, p += pstep) {
			if (p < 0 || p >= (double)c.len) continue;
			int64_t i0 = (int64_t)p;
			int64_t i1 = (i0 + 1 < c.len) ? i0 + 1 : i0;
			float fr = (float)(p - (double)i0);
			if (ch == 1) {
				float v = d[i0] + (d[i1] - d[i0]) * fr;
				dst[2*k] += v; dst[2*k+1] += v;
			} else {
				dst[2*k]   += d[2*i0]   + (d[2*i1]   - d[2*i0])   * fr;
				dst[2*k+1] += d[2*i0+1] + (d[2*i1+1] - d[2*i0+1]) * fr;
			}
		}
	}
}

void Mixer::Process(float *buf, int frames, double activePos, double activeRate,
	double sysRate, bool mixExtras)
{
	bool anySolo = false;
	for (int i=0; i<fCount; i++)
		if (fTracks[i].used && fTracks[i].solo) { anySolo = true; break; }

	const int act = fActive;
	const double aRate = fabs(activeRate) > 1.0 ? fabs(activeRate) : 44100.0;
	const double dir = (activeRate < 0) ? -1.0 : 1.0;
	const double tstep = dir / sysRate;
	const double now = activePos / aRate;		// время на ленте: Pool лежит на ней целиком
	const bool extras = mixExtras && TryLock();

	// перемотка (скачок позиции): плагинам сбрасываем состояние, иначе из буферов задержки
	// и хвостов прозвучит то, что было до скачка
	if (mixExtras) {
		if (fHaveNext && fabs(now - fNextNow) > 2.0 * frames / sysRate + 0.05) {
			for (int i=0; i<fCount; i++) fFx[i].Reset();
			fFx[MIX_MASTER].Reset();
		}
		fNextNow = now + tstep * frames;
		fHaveNext = true;
	}
	// плагины с задержкой (например, шумодав): активная дорожка отстаёт на latAct отсчётов,
	// остальные дорожки сдвигаем на столько же, а свою задержку каждая дорожка компенсирует чтением вперёд
	const int latAct = fFx[act].Latency();

	// активная дорожка: активный клип уже в буфере; остальные её клипы добавляем, затем эффекты и канал
	{
		const MixTrack &t = fTracks[act];
		if (extras && !t.clips.empty())
			RenderClips(t, buf, frames, now, tstep);
		fFx[act].Process(buf, frames, (float)sysRate);
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
		fPeakL[act] = pl; fPeakR[act] = pr;
	}

	// метроном идёт после мастера и сдвигается на задержки активной дорожки и мастера
	const int latMaster = fFx[MIX_MASTER].Latency();
	const bool metro = fMetroOn && mixExtras && dir > 0;

	if (!extras) {
		for (int i=0; i<fCount; i++) if (i != act) fPeakL[i] = fPeakR[i] = 0;
		ApplyMaster(buf, frames, sysRate);
		if (metro) fMetro.Render(buf, frames, now - tstep * (latAct + latMaster), sysRate, fBpm, fMeter, fGridDiv, fMetroVol / 0.7f);
		return;
	}
	for (int i=0; i<fCount; i++) {
		if (i == act) continue;
		MixTrack &t = fTracks[i];
		fPeakL[i] = fPeakR[i] = 0;
		if (!t.used || t.clips.empty()) continue;
		float gl, gr;
		Gains(t, anySolo, gl, gr);
		float pl = 0, pr = 0;
		FxChain &fx = fFx[i];
		const bool useFx = fx.HasEffects();
		// границы звука дорожки на ленте; после конца эффекты (реверберация, эхо) ещё звучат - даём хвост
		double laneStart = 1e30, laneEnd = 0;
		for (size_t k=0; k<t.clips.size(); k++) {
			if (t.clips[k].pos < laneStart) laneStart = t.clips[k].pos;
			double e = t.clips[k].End();
			if (e > laneEnd) laneEnd = e;
		}
		if (useFx) laneEnd += 4.0;
		const double shift = tstep * (double)((useFx ? fx.Latency() : 0) - latAct);
		for (int from = 0; from < frames; from += 4096) {
			const int n = (frames - from < 4096) ? frames - from : 4096;
			const double ta = now + tstep * from + shift, tb = ta + tstep * n;
			const double lo = ta < tb ? ta : tb, hi = ta < tb ? tb : ta;
			if (hi < laneStart || lo >= laneEnd) continue;
			memset(fTmp, 0, sizeof(float) * 2 * n);
			RenderClips(t, fTmp, n, ta, tstep);
			if (useFx) fx.Process(fTmp, n, (float)sysRate);
			for (int k=0; k<n; k++) {
				float l = fTmp[2*k] * gl, r = fTmp[2*k+1] * gr;
				buf[2*(from+k)] += l; buf[2*(from+k)+1] += r;
				if (fabsf(l) > pl) pl = fabsf(l);
				if (fabsf(r) > pr) pr = fabsf(r);
			}
		}
		fPeakL[i] = pl; fPeakR[i] = pr;
	}
	ApplyMaster(buf, frames, sysRate);
	if (metro) fMetro.Render(buf, frames, now - tstep * (latAct + latMaster), sysRate, fBpm, fMeter, fGridDiv, fMetroVol / 0.7f);
	Unlock();
}

// Мастер-шина: эффекты, громкость, mute, индикатор уровня.
void Mixer::ApplyMaster(float *buf, int frames, double sysRate)
{
	FxChain &fx = fFx[MIX_MASTER];
	if (fx.HasEffects()) fx.Process(buf, frames, (float)sysRate);
	const float g = fMasterMute ? 0.0f : fMasterGain;
	float pl = 0, pr = 0;
	for (int k=0; k<frames; k++) {
		float l = buf[2*k] * g, r = buf[2*k+1] * g;
		buf[2*k] = l; buf[2*k+1] = r;
		if (fabsf(l) > pl) pl = fabsf(l);
		if (fabsf(r) > pr) pr = fabsf(r);
	}
	fMasterPeakL = pl; fMasterPeakR = pr;
}
