#include "Envelope.h"

#include <math.h>
#include <algorithm>

const double Envelope::kSameTime = 1e-4;

int Envelope::Add(double t, float v)
{
	size_t i = 0;
	while (i < fPts.size() && fPts[i].t < t - kSameTime) i++;
	if (i < fPts.size() && fabs(fPts[i].t - t) <= kSameTime) {
		fPts[i].v = v;
		return (int)i;
	}
	EnvPoint p; p.t = t; p.v = v;
	fPts.insert(fPts.begin() + i, p);
	return (int)i;
}

void Envelope::Remove(int i)
{
	if (i >= 0 && i < (int)fPts.size())
		fPts.erase(fPts.begin() + i);
}

int Envelope::Move(int i, double t, float v)
{
	if (i < 0 || i >= (int)fPts.size()) return i;
	// не перескакиваем соседей: остаёмся между ними (с зазором kSameTime)
	if (i > 0 && t < fPts[i-1].t + kSameTime * 2) t = fPts[i-1].t + kSameTime * 2;
	if (i + 1 < (int)fPts.size() && t > fPts[i+1].t - kSameTime * 2) t = fPts[i+1].t - kSameTime * 2;
	fPts[i].t = t;
	fPts[i].v = v;
	return i;
}

float Envelope::Value(double t, float def) const
{
	const size_t n = fPts.size();
	if (n == 0) return def;
	if (t <= fPts[0].t) return fPts[0].v;
	if (t >= fPts[n-1].t) return fPts[n-1].v;
	// бинарный поиск: первая точка с временем > t
	size_t lo = 0, hi = n - 1;
	while (hi - lo > 1) {
		size_t mid = (lo + hi) / 2;
		if (fPts[mid].t <= t) lo = mid; else hi = mid;
	}
	const double span = fPts[hi].t - fPts[lo].t;
	if (span <= 0) return fPts[hi].v;
	const double k = (t - fPts[lo].t) / span;
	return (float)(fPts[lo].v + (fPts[hi].v - fPts[lo].v) * k);
}

namespace Env {

float DbToGain(float db)
{
	if (db <= kDbMin) return 0.0f;
	return powf(10.0f, db / 20.0f);
}

float GainToDb(float g)
{
	if (g <= 0.0f) return kDbMin;
	float db = 20.0f * log10f(g);
	return db < kDbMin ? kDbMin : db;
}

void PanGains(float gain, float pan, float &gl, float &gr)
{
	gl = gain * (pan > 0 ? 1.0f - pan : 1.0f);
	gr = gain * (pan < 0 ? 1.0f + pan : 1.0f);
}

static void Targets(double t, float baseGain, float basePan, const Envelope *vol, const Envelope *pan,
					float &gl, float &gr)
{
	float g = baseGain;
	if (vol && !vol->Empty())
		g *= DbToGain(vol->Value(t, 0.0f));
	float p = basePan;
	if (pan && !pan->Empty())
		p += pan->Value(t, 0.0f);
	p = p < -1.0f ? -1.0f : (p > 1.0f ? 1.0f : p);
	PanGains(g, p, gl, gr);
}

void ApplyStereo(float *buf, int frames, double t0, double tstep,
				 float baseGain, float basePan, const Envelope *vol, const Envelope *pan)
{
	const bool hasVol = vol && !vol->Empty();
	const bool hasPan = pan && !pan->Empty();
	if (!hasVol && !hasPan) {		// огибающих нет: как раньше, постоянное усиление
		float gl, gr;
		PanGains(baseGain, basePan < -1.0f ? -1.0f : (basePan > 1.0f ? 1.0f : basePan), gl, gr);
		if (gl != 1.0f || gr != 1.0f)
			for (int k = 0; k < frames; k++) { buf[2*k] *= gl; buf[2*k+1] *= gr; }
		return;
	}
	float gl0, gr0;
	Targets(t0, baseGain, basePan, vol, pan, gl0, gr0);
	for (int from = 0; from < frames; from += kStep) {
		const int n = (frames - from < kStep) ? frames - from : kStep;
		float gl1, gr1;
		Targets(t0 + tstep * (from + n), baseGain, basePan, vol, pan, gl1, gr1);
		const float dl = (gl1 - gl0) / (float)n, dr = (gr1 - gr0) / (float)n;
		float l = gl0, r = gr0;
		for (int k = 0; k < n; k++) {
			buf[2*(from+k)] *= l;
			buf[2*(from+k)+1] *= r;
			l += dl; r += dr;
		}
		gl0 = gl1; gr0 = gr1;
	}
}

}	// namespace Env
