#include "TunerCore.h"
#include <math.h>
#include <string.h>

namespace Tuner {

static const size_t kBuf = 1 << 17;			// 131072 отсчётов: 1,3 с при 96 кГц
static const size_t kMask = kBuf - 1;
static const float kGateDb = -55.0f;		// тише - считаем тишиной
static const float kYinThreshold = 0.15f;
static const float kAcceptMax = 0.35f;
static const double kHoldSec = 0.35;

static const Preset kPresets[] = {
	{ "TUNER_PRESET_CHROMATIC",	0, { 0 } },
	{ "TUNER_PRESET_GUITAR6",	6, { 40, 45, 50, 55, 59, 64 } },
	{ "TUNER_PRESET_GUITAR7",	7, { 35, 40, 45, 50, 55, 59, 64 } },
	{ "TUNER_PRESET_BASS4",		4, { 28, 33, 38, 43 } },
	{ "TUNER_PRESET_BASS5",		5, { 23, 28, 33, 38, 43 } },
};

int PresetCount() { return (int)(sizeof(kPresets) / sizeof(kPresets[0])); }
const Preset &PresetAt(int i) { return kPresets[(i < 0 || i >= PresetCount()) ? 0 : i]; }

float FreqToMidi(float f, float a4) { return 69.0f + 12.0f * log2f(f / a4); }
float MidiToFreq(float m, float a4) { return a4 * powf(2.0f, (m - 69.0f) / 12.0f); }

const char *NoteName(int midi, int notation)
{
	static const char *latin[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
	static const char *solf[12] = { "До", "До#", "Ре", "Ре#", "Ми", "Фа", "Фа#", "Соль", "Соль#", "Ля", "Ля#", "Си" };
	const int n = ((midi % 12) + 12) % 12;
	return notation == NOTATION_SOLFEGE ? solf[n] : latin[n];
}

int Octave(int midi) { return (int)floorf(midi / 12.0f) - 1; }

Core::Core() : fRate(44100.0f), fA4(440.0f), fPreset(0), fPushed(0), fLastVoiced(0), fHistN(0), fActive(false)
{
	fBuf.assign(kBuf, 0.0f);
	memset(&fLast, 0, sizeof(fLast));
	fLast.stringIndex = -1;
	for (int i = 0; i < 5; i++) fHist[i] = 0;
}

void Core::Init(float sampleRate)
{
	fRate = sampleRate > 8000.0f ? sampleRate : 44100.0f;
	Reset();
}

void Core::SetA4(float hz) { fA4 = hz < 415.0f ? 415.0f : (hz > 466.0f ? 466.0f : hz); }

void Core::SetPreset(int index)
{
	fPreset = (index < 0 || index >= PresetCount()) ? 0 : index;
	fHistN = 0; fActive = false;
}

void Core::Reset()
{
	std::fill(fBuf.begin(), fBuf.end(), 0.0f);
	fPushed = 0; fLastVoiced = 0; fHistN = 0; fActive = false;
	memset(&fLast, 0, sizeof(fLast));
	fLast.stringIndex = -1;
}

void Core::Push(const float *x, size_t n)
{
	for (size_t i = 0; i < n; i++)
		fBuf[(fPushed + i) & kMask] = x[i];
	fPushed += n;
}

float Core::LevelDb() const
{
	const size_t n = fPushed < 1024 ? (size_t)fPushed : 1024;
	if (n == 0) return -120.0f;
	double e = 0;
	for (size_t i = 1; i <= n; i++) { const float v = fBuf[(fPushed - i) & kMask]; e += (double)v * v; }
	return (float)(10.0 * log10(e / n + 1e-20));
}

void Core::SearchRange(float *fmin, float *fmax) const
{
	const Preset &p = PresetAt(fPreset);
	if (p.count == 0) { *fmin = 25.0f; *fmax = 1400.0f; return; }
	*fmin = MidiToFreq((float)(p.midi[0] - 5), fA4);
	*fmax = MidiToFreq((float)(p.midi[p.count - 1] + 5), fA4);
}

// YIN на проредженном сигнале; возвращает частоту найденной основной
bool Core::Detect(float fmin, float fmax, float *freq)
{
	int D = (int)floorf(fRate / (24.0f * fmax));
	if (D < 1) D = 1;
	const float fsd = fRate / D;
	int tauMin = (int)floorf(fsd / fmax); if (tauMin < 2) tauMin = 2;
	int tauMax = (int)ceilf(fsd / fmin);
	if (tauMax <= tauMin + 3) return false;
	const int Nd = 2 * tauMax + 4;
	const uint64_t need = (uint64_t)Nd * D;
	if (fPushed < need) return false;

	std::vector<float> y(Nd);
	const uint64_t start = fPushed - need;
	double mean = 0;
	for (int k = 0; k < Nd; k++) {
		float a = 0;
		for (int j = 0; j < D; j++) a += fBuf[(start + (uint64_t)k * D + j) & kMask];
		y[k] = a / D; mean += y[k];
	}
	mean /= Nd;
	for (int k = 0; k < Nd; k++) y[k] -= (float)mean;

	const int W = Nd - tauMax - 2;
	std::vector<float> cm(tauMax + 2);
	double run = 0; cm[0] = 1.0f;
	for (int tau = 1; tau <= tauMax + 1; tau++) {
		double s = 0;
		for (int j = 0; j < W; j++) { const float df = y[j] - y[j + tau]; s += (double)df * df; }
		run += s;
		cm[tau] = run > 0 ? (float)(s * tau / run) : 1.0f;
	}

	// первое снижение ниже порога, затем спуск до локального минимума
	int tau = -1;
	for (int t = tauMin; t <= tauMax; t++) {
		if (cm[t] < kYinThreshold) {
			while (t + 1 <= tauMax && cm[t + 1] < cm[t]) t++;
			tau = t; break;
		}
	}
	if (tau < 0) {			// порога никто не прошёл: берём глобальный минимум, если он достаточно хорош
		float best = 1e9f; int bt = -1;
		for (int t = tauMin; t <= tauMax; t++) if (cm[t] < best) { best = cm[t]; bt = t; }
		if (bt < 0 || best > kAcceptMax) return false;
		tau = bt;
	}

	// проверка удвоенного периода: слабая основная при сильной 2-й гармонике
	if (cm[tau] > 0.03f && 2 * tau + 2 <= tauMax) {
		int t2 = 2 * tau, bt = t2; float bv = cm[t2];
		for (int t = 2 * tau - 2; t <= 2 * tau + 2 && t <= tauMax; t++) if (t > 0 && cm[t] < bv) { bv = cm[t]; bt = t; }
		if (bv < cm[tau] * 0.5f && bv < kYinThreshold) {
			tau = bt;
			while (tau + 1 <= tauMax && cm[tau + 1] < cm[tau]) tau++;
			while (tau - 1 >= tauMin && cm[tau - 1] < cm[tau]) tau--;
		}
	}

	// параболическая интерполяция
	float t = (float)tau;
	if (tau > tauMin && tau < tauMax) {
		const float a = cm[tau - 1], b = cm[tau], c = cm[tau + 1];
		const float den = a - 2.0f * b + c;
		if (den > 1e-9f) {
			float d = 0.5f * (a - c) / den;
			if (d > 1.0f) d = 1.0f; else if (d < -1.0f) d = -1.0f;
			t += d;
		}
	}
	*freq = fsd / t;
	return *freq > 0;
}

static float Median5(const float *h, int n)
{
	float a[5];
	for (int i = 0; i < n; i++) a[i] = h[i];
	for (int i = 1; i < n; i++) { float v = a[i]; int j = i - 1; while (j >= 0 && a[j] > v) { a[j + 1] = a[j]; j--; } a[j + 1] = v; }
	return a[n / 2];
}

Reading Core::Update()
{
	Reading r;
	memset(&r, 0, sizeof(r));
	r.stringIndex = -1;
	r.levelDb = LevelDb();

	float f = 0;
	float fmin, fmax;
	SearchRange(&fmin, &fmax);
	const bool ok = r.levelDb > kGateDb && Detect(fmin, fmax, &f);

	if (ok) {
		const float m = FreqToMidi(f, fA4);
		// новая оценка далеко от предыдущих (> 60 центов) - начинаем накопление заново
		if (fHistN > 0 && fabsf(m - Median5(fHist, fHistN)) > 0.6f) { fHistN = 0; fActive = false; }
		if (fHistN < 5) fHist[fHistN++] = m;
		else { for (int i = 0; i < 4; i++) fHist[i] = fHist[i + 1]; fHist[4] = m; }
		if (fHistN >= 2) fActive = true;
		if (fActive) {
			const float med = Median5(fHist, fHistN);
			r.voiced = true;
			r.midi = med;
			r.freq = MidiToFreq(med, fA4);
			fLastVoiced = fPushed;
			const Preset &p = PresetAt(fPreset);
			if (p.count == 0) {
				r.note = (int)floorf(med + 0.5f);
			} else {
				int best = 0; float bd = 1e9f;
				for (int i = 0; i < p.count; i++) { const float d = fabsf(med - p.midi[i]); if (d < bd) { bd = d; best = i; } }
				r.stringIndex = best; r.note = p.midi[best];
			}
			r.cents = (med - r.note) * 100.0f;
			fLast = r;
			return r;
		}
	}
	// звука нет: удерживаем последнее показание короткое время
	if (fActive && (double)(fPushed - fLastVoiced) <= kHoldSec * fRate) {
		r = fLast; r.held = true; r.levelDb = LevelDb();
		return r;
	}
	if (!ok) { fHistN = 0; fActive = false; }
	return r;
}

}	// namespace Tuner
