/*
 * Автотюн BeAE-Plus: плагин LADSPA для вокала (один голос).
 *
 * 1) Высота голоса определяется алгоритмом YIN (грубо на пониженной частоте, затем уточняется).
 * 2) Берётся ближайшая нота выбранного строя (хроматика, мажор, минор...), с гистерезисом.
 * 3) Высота сдвигается к этой ноте методом PSOLA (синхронно с периодом голоса): тембр (форманты)
 *    почти не меняется. Скорость коррекции задаёт, как быстро сдвиг доходит до нужного значения
 *    (0 мс - "роботический" эффект).
 * Глухие звуки и тишина проходят без обработки. Задержка 50 мс сообщается через порт "latency".
 */
#include "../common/ladspa_abi.h"

#include <math.h>
#include <string.h>
#include <vector>

namespace {

enum Port {
	P_KEY = 0,		// 0..11   тональность (0 = до, 1 = до#, ... 11 = си)
	P_SCALE,		// 0..5    строй
	P_SPEED,		// 0..400  скорость коррекции, мс
	P_AMOUNT,		// 0..100  степень коррекции, %
	P_REF,			// 415..466 эталон A, Гц
	P_FMIN,			// 60..140  нижняя граница голоса, Гц
	P_FMAX,			// 300..900 верхняя граница голоса, Гц
	P_GATE,			// -70..-30 порог тишины, дБ
	P_LATENCY,		// выход: задержка в отсчётах
	P_IN_L, P_IN_R, P_OUT_L, P_OUT_R,
	P_COUNT
};

const int kRing = 16384, kMask = kRing - 1;
const int kHop = 256;				// шаг анализа
const int kWin = 2048;				// окно анализа (отсчётов, на исходной частоте)
const int kDown = 4;				// понижение частоты для грубого поиска
const float kLog2 = 0.69314718056f;

// строи: маски нот относительно тональности (бит = полутон от тоники)
const unsigned kScaleMask[6] = {
	0xFFF,									// хроматический
	(1u<<0)|(1u<<2)|(1u<<4)|(1u<<5)|(1u<<7)|(1u<<9)|(1u<<11),		// мажор
	(1u<<0)|(1u<<2)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<8)|(1u<<10),		// натуральный минор
	(1u<<0)|(1u<<2)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<8)|(1u<<11),		// гармонический минор
	(1u<<0)|(1u<<2)|(1u<<4)|(1u<<7)|(1u<<9),						// пентатоника мажорная
	(1u<<0)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<10),						// пентатоника минорная
};

struct Instance {
	float		rate;
	int			latency;
	float		*port[P_COUNT];

	std::vector<float> in[2], acc[2], mono;
	long long	w;				// число принятых отсчётов
	int			hopCount;

	// анализ
	bool		voiced;
	int			missCount;
	float		T0;				// период, отсчётов
	float		f0hist[3];
	int			f0n;
	int			targetNote;		// текущая нота (с гистерезисом), -1 если нет
	float		shift;			// сглаженный сдвиг, полутонов

	// синтез
	double		nextOut, aPrev, aNext;
	bool		marksValid;
	float		voicedMix;
	float		rho;
};

float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

void *Instantiate(const Ladspa::Descriptor *, unsigned long rate)
{
	Instance *s = new Instance;
	s->rate = rate > 0 ? (float)rate : 44100.0f;
	s->latency = (int)(0.05f * s->rate);
	for (int i = 0; i < P_COUNT; i++) s->port[i] = 0;
	for (int c = 0; c < 2; c++) { s->in[c].assign(kRing, 0.0f); s->acc[c].assign(kRing, 0.0f); }
	s->mono.assign(kRing, 0.0f);
	return s;
}

void Connect(void *h, unsigned long p, float *loc)
{
	Instance *s = (Instance *)h;
	if (p < (unsigned long)P_COUNT) s->port[p] = loc;
}

void Activate(void *h)
{
	Instance *s = (Instance *)h;
	for (int c = 0; c < 2; c++) { std::fill(s->in[c].begin(), s->in[c].end(), 0.0f); std::fill(s->acc[c].begin(), s->acc[c].end(), 0.0f); }
	std::fill(s->mono.begin(), s->mono.end(), 0.0f);
	s->w = 0; s->hopCount = 0;
	s->voiced = false; s->missCount = 0; s->T0 = 200; s->f0n = 0; s->targetNote = -1; s->shift = 0;
	s->nextOut = 0; s->aPrev = s->aNext = 0; s->marksValid = false; s->voicedMix = 0; s->rho = 1;
	if (s->port[P_LATENCY]) *s->port[P_LATENCY] = (float)s->latency;
}

// ---- определение высоты (YIN) ----
// возвращает период в отсчётах (исходной частоты) или 0
float DetectPeriod(Instance *s, float fmin, float fmax, float gateDb)
{
	const float fs = s->rate;
	float *mono = &s->mono[0];
	const long long end = s->w;			// анализируем отсчёты [end - kWin, end)

	// громкость последних 1024 отсчётов
	double e = 0;
	for (int i = 1; i <= 1024; i++) { float x = mono[(end - i) & kMask]; e += x * x; }
	if (10.0 * log10(e / 1024.0 + 1e-20) < gateDb) return 0;

	// понижение частоты в kDown раз (усреднение), без постоянной составляющей
	const int Nd = kWin / kDown;
	float y[kWin / kDown];
	double mean = 0;
	for (int k = 0; k < Nd; k++) {
		float a = 0;
		for (int j = 0; j < kDown; j++) a += mono[(end - kWin + k * kDown + j) & kMask];
		y[k] = a / kDown; mean += y[k];
	}
	mean /= Nd;
	for (int k = 0; k < Nd; k++) y[k] -= (float)mean;

	int tauMin = (int)floorf(fs / kDown / fmax); if (tauMin < 2) tauMin = 2;
	int tauMax = (int)ceilf(fs / kDown / fmin);
	if (tauMax > Nd / 2 - 2) tauMax = Nd / 2 - 2;
	if (tauMax <= tauMin + 2) return 0;
	const int M = Nd - tauMax - 1;

	std::vector<float> d(tauMax + 2), cm(tauMax + 2);
	for (int tau = 1; tau <= tauMax + 1 && tau < (int)d.size(); tau++) {
		double sum = 0;
		for (int j = 0; j < M; j++) { float df = y[j] - y[j + tau]; sum += df * df; }
		d[tau] = (float)sum;
	}
	double run = 0;
	cm[0] = 1;
	for (int tau = 1; tau <= tauMax + 1; tau++) {
		run += d[tau];
		cm[tau] = run > 0 ? (float)(d[tau] * tau / run) : 1.0f;
	}

	int best = -1;
	for (int tau = tauMin; tau <= tauMax; tau++) {
		if (cm[tau] < 0.15f) {					// первый провал ниже порога: идём до его минимума
			while (tau + 1 <= tauMax && cm[tau + 1] < cm[tau]) tau++;
			best = tau; break;
		}
	}
	if (best < 0) {								// запасной вариант: глобальный минимум
		float mn = 1e9f; int at = -1;
		for (int tau = tauMin; tau <= tauMax; tau++) if (cm[tau] < mn) { mn = cm[tau]; at = tau; }
		if (at < 0 || mn > 0.25f) return 0;
		best = at;
	}
	// параболическая интерполяция
	float tf = (float)best;
	if (best > tauMin && best < tauMax) {
		float a = cm[best - 1], b = cm[best], c = cm[best + 1];
		float den = a - 2 * b + c;
		if (den != 0) tf += 0.5f * (a - c) / den;
	}
	const float coarse = tf * kDown;

	// уточнение на исходной частоте: минимум разностной функции вблизи грубой оценки
	const int range = kDown + 2;
	int lo = (int)coarse - range, hi = (int)coarse + range;
	if (lo < 8) lo = 8;
	const int W = 1024;
	float dv[64];
	int cnt = hi - lo + 1; if (cnt > 63) cnt = 63;
	for (int k = 0; k < cnt; k++) {
		const int tau = lo + k;
		double sum = 0;
		for (int j = 0; j < W; j++) {
			float a = mono[(end - 1 - j) & kMask], b = mono[(end - 1 - j - tau) & kMask];
			sum += (a - b) * (a - b);
		}
		dv[k] = (float)sum;
	}
	int bi = 0; for (int k = 1; k < cnt; k++) if (dv[k] < dv[bi]) bi = k;
	float tp = (float)(lo + bi);
	if (bi > 0 && bi < cnt - 1) {
		float a = dv[bi - 1], b = dv[bi], c = dv[bi + 1];
		float den = a - 2 * b + c;
		if (den != 0) tp += 0.5f * (a - c) / den;
	}
	return tp;
}

float Median3(float a, float b, float c) { return a > b ? (b > c ? b : (a > c ? c : a)) : (a > c ? a : (b > c ? c : b)); }

// ближайшая допустимая нота (номер MIDI) с гистерезисом; m - высота в полутонах (дробный MIDI)
int NearestNote(float m, int key, unsigned mask, int prev)
{
	if (prev >= 0 && ((mask >> (((prev - key) % 12 + 12) % 12)) & 1) && fabsf(m - (float)prev) < 0.7f)
		return prev;
	int best = -1; float bd = 1e9f;
	const int c = (int)floorf(m + 0.5f);
	for (int n = c - 7; n <= c + 7; n++) {
		if (!((mask >> (((n - key) % 12 + 12) % 12)) & 1)) continue;
		float dd = fabsf(m - (float)n);
		if (dd < bd) { bd = dd; best = n; }
	}
	return best;
}

void Analyze(Instance *s, int key, unsigned mask, float speedMs, float amount, float refA, float fmin, float fmax, float gateDb)
{
	const float fs = s->rate;
	float T = DetectPeriod(s, fmin, fmax, gateDb);
	if (T > 0) {
		float f = fs / T;
		if (s->f0n >= 1 && fabsf(log2f(f / (fs / s->T0))) > 0.5f && s->f0n < 2) {
			// резкий скачок: ждём подтверждения следующим кадром
			s->f0hist[2] = s->f0hist[1]; s->f0hist[1] = s->f0hist[0]; s->f0hist[0] = f; s->f0n++;
			if (!s->voiced) { s->T0 = T; }
		} else {
			s->f0hist[2] = s->f0hist[1]; s->f0hist[1] = s->f0hist[0]; s->f0hist[0] = f;
			if (s->f0n < 3) s->f0n++;
			float fm = s->f0n >= 3 ? Median3(s->f0hist[0], s->f0hist[1], s->f0hist[2]) : f;
			s->T0 = fs / fm;
		}
		s->missCount = 0;
		if (!s->voiced) { s->voiced = true; s->shift = 0; s->marksValid = false; }
	} else {
		if (++s->missCount >= 2) { s->voiced = false; s->f0n = 0; s->targetNote = -1; }
	}

	if (s->voiced) {
		const float f = fs / s->T0;
		const float m = 69.0f + 12.0f * log2f(f / refA);
		const int n = NearestNote(m, key, mask, s->targetNote);
		if (n >= 0) {
			s->targetNote = n;
			float want = ((float)n - m) * amount;
			const float alpha = speedMs <= 0.5f ? 1.0f : 1.0f - expf(-kHop / (fs * speedMs * 0.001f));
			s->shift += alpha * (want - s->shift);
		}
		s->rho = Clamp(powf(2.0f, s->shift / 12.0f), 0.7071f, 1.4142f);
	}
}

void Run(void *h, unsigned long n)
{
	Instance *s = (Instance *)h;
	for (int i = 0; i < P_COUNT; i++) if (!s->port[i]) return;

	const int key = (int)(Clamp(*s->port[P_KEY], 0, 11) + 0.5f);
	const int scale = (int)(Clamp(*s->port[P_SCALE], 0, 5) + 0.5f);
	const unsigned mask = kScaleMask[scale];
	const float speed = Clamp(*s->port[P_SPEED], 0, 400);
	const float amount = Clamp(*s->port[P_AMOUNT], 0, 100) / 100.0f;
	const float refA = Clamp(*s->port[P_REF], 415, 466);
	const float fmin = Clamp(*s->port[P_FMIN], 60, 140);
	const float fmax = Clamp(*s->port[P_FMAX], 300, 900);
	const float gate = Clamp(*s->port[P_GATE], -70, -30);
	*s->port[P_LATENCY] = (float)s->latency;

	const float mixCoef = 1.0f - expf(-1.0f / (0.008f * s->rate));		// плавное включение/выключение, 8 мс

	for (unsigned long i = 0; i < n; i++) {
		const float xl = s->port[P_IN_L][i], xr = s->port[P_IN_R][i];
		const int wi = (int)(s->w & kMask);
		s->in[0][wi] = xl; s->in[1][wi] = xr; s->mono[wi] = 0.5f * (xl + xr);
		s->w++;

		if (++s->hopCount >= kHop) {
			s->hopCount = 0;
			if (s->w >= kWin) Analyze(s, key, mask, speed, amount, refA, fmin, fmax, gate);
		}

		const long long te = s->w - 1 - s->latency;			// время выдаваемого сейчас отсчёта
		const bool active = s->voiced && amount > 0.001f;

		// синтез: ставим зёрна, пока они перекрывают отсчёт te
		if (active) {
			const double T0 = s->T0;
			const int Hi = (int)(T0 + 0.5);
			if (!s->marksValid) {
				s->nextOut = (double)te - T0;
				s->aPrev = s->nextOut; s->aNext = s->aPrev + T0;
				s->marksValid = true;
			}
			while (s->nextOut - Hi <= (double)te) {
				const double c = s->nextOut;
				if (s->aNext < c - 4.0 * T0) { s->aPrev = c; s->aNext = c + T0; }
				while (s->aNext <= c) { s->aPrev = s->aNext; s->aNext += T0; }
				const double inMark = (c - s->aPrev <= s->aNext - c) ? s->aPrev : s->aNext;
				const long long ci = (long long)floor(c + 0.5), ii = (long long)floor(inMark + 0.5);
				const float scale = 1.0f / s->rho;
				for (int ch = 0; ch < 2; ch++) {
					for (int j = -Hi; j <= Hi; j++) {
						const long long idx = ci + j;
						if (idx < te) continue;
						const long long src = ii + j;
						if (src >= s->w || src < 0) continue;
						const float win = 0.5f + 0.5f * cosf(3.14159265f * (float)j / (float)Hi);
						s->acc[ch][idx & kMask] += win * scale * s->in[ch][src & kMask];
					}
				}
				s->nextOut += T0 / s->rho;
			}
		} else {
			s->marksValid = false;
		}

		// выход: смесь обработанного и задержанного исходного
		s->voicedMix += mixCoef * ((active ? 1.0f : 0.0f) - s->voicedMix);
		for (int ch = 0; ch < 2; ch++) {
			float dry = te >= 0 ? s->in[ch][te & kMask] : 0.0f;
			float wet = s->acc[ch][te & kMask];
			s->acc[ch][te & kMask] = 0.0f;
			s->port[ch == 0 ? P_OUT_L : P_OUT_R][i] = s->voicedMix * wet + (1.0f - s->voicedMix) * dry;
		}
	}
}

void Cleanup(void *h) { delete (Instance *)h; }

const char *kNames[P_COUNT] = {
	"Тональность (0 до, 1 до#, 2 ре ... 11 си)",
	"Строй (0 хроматика, 1 мажор, 2 минор, 3 гарм. минор, 4 пентат. мажор, 5 пентат. минор)",
	"Скорость коррекции, мс (0 = мгновенно)", "Степень коррекции, %", "Эталон A, Гц",
	"Нижняя частота голоса, Гц", "Верхняя частота голоса, Гц", "Порог тишины, дБ",
	"latency", "Вход L", "Вход R", "Выход L", "Выход R"
};

}	// namespace

extern "C" const Ladspa::Descriptor *ladspa_descriptor(unsigned long index)
{
	if (index != 0) return 0;
	static int desc[P_COUNT];
	static Ladspa::PortRangeHint hints[P_COUNT];
	static bool init = false;
	if (!init) {
		const int B = Ladspa::HINT_BOUNDED_BELOW | Ladspa::HINT_BOUNDED_ABOVE;
		for (int i = 0; i < P_COUNT; i++) { desc[i] = Ladspa::PORT_INPUT | Ladspa::PORT_CONTROL; hints[i].hint = 0; hints[i].lower = hints[i].upper = 0; }
		hints[P_KEY]	= { B | Ladspa::HINT_INTEGER | Ladspa::HINT_DEFAULT_0, 0.0f, 11.0f };
		hints[P_SCALE]	= { B | Ladspa::HINT_INTEGER | Ladspa::HINT_DEFAULT_0, 0.0f, 5.0f };
		hints[P_SPEED]	= { B | Ladspa::HINT_DEFAULT_LOW, 0.0f, 400.0f };						// 100 мс
		hints[P_AMOUNT]	= { B | Ladspa::HINT_DEFAULT_MAXIMUM, 0.0f, 100.0f };
		hints[P_REF]	= { B | Ladspa::HINT_DEFAULT_440, 415.0f, 466.0f };
		hints[P_FMIN]	= { B | Ladspa::HINT_DEFAULT_LOW, 60.0f, 140.0f };						// 80 Гц
		hints[P_FMAX]	= { B | Ladspa::HINT_DEFAULT_HIGH, 300.0f, 900.0f };					// 750 Гц
		hints[P_GATE]	= { B | Ladspa::HINT_DEFAULT_MIDDLE, -70.0f, -30.0f };					// -50 дБ
		desc[P_LATENCY] = Ladspa::PORT_OUTPUT | Ladspa::PORT_CONTROL;
		desc[P_IN_L] = desc[P_IN_R] = Ladspa::PORT_INPUT | Ladspa::PORT_AUDIO;
		desc[P_OUT_L] = desc[P_OUT_R] = Ladspa::PORT_OUTPUT | Ladspa::PORT_AUDIO;
		init = true;
	}
	static Ladspa::Descriptor d = {
		910, "beae_autotune", 0, "Автотюн (вокал)", "BeAE-Plus", "BSD",
		P_COUNT, desc, kNames, hints, 0,
		Instantiate, Connect, Activate, Run, 0, 0, 0, Cleanup
	};
	return &d;
}
