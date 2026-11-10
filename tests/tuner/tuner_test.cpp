// Проверка TunerCore: точность, октавные ошибки, шум, поток блоками, ноты и пресеты.
#include "TunerCore.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <vector>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

// набор гармоник: amp[0] - основная
static std::vector<float> Synth(float rate, double f, const std::vector<double> &amp, double sec, double decaySec = 0, double inharm = 0, double level = 0.3)
{
	std::vector<float> x((size_t)(sec * rate), 0.0f);
	double norm = 0; for (size_t h = 0; h < amp.size(); h++) norm += fabs(amp[h]);
	for (size_t i = 0; i < x.size(); i++) {
		const double t = i / rate;
		double v = 0;
		for (size_t h = 0; h < amp.size(); h++) v += amp[h] * sin(2 * M_PI * f * (h + 1) * (1.0 + inharm * (h + 1) * (h + 1)) * t + 0.7 * h);
		v = v / norm * level;
		if (decaySec > 0) v *= exp(-t / decaySec);
		x[i] = (float)v;
	}
	return x;
}
static std::vector<float> Noise(float rate, double sec, float amp)
{
	std::vector<float> x((size_t)(sec * rate)); unsigned s = 4242;
	for (size_t i = 0; i < x.size(); i++) { s = s * 1664525u + 1013904223u; x[i] = amp * ((int)(s >> 8) / 8388608.0f - 1.0f); }
	return x;
}
// подаём блоками по 512 и вызываем Update после каждого; возвращает последнее показание
static Tuner::Reading Feed(Tuner::Core &c, const std::vector<float> &x, size_t block = 512)
{
	Tuner::Reading r; memset(&r, 0, sizeof(r));
	for (size_t p = 0; p < x.size(); p += block) {
		const size_t n = x.size() - p < block ? x.size() - p : block;
		c.Push(&x[p], n);
		r = c.Update();
	}
	return r;
}
static double CentsErr(const Tuner::Reading &r, double f, float a4 = 440.0f) { return 1200.0 * log2((double)r.freq / f) ; (void)a4; }

int main()
{
	const float rates[3] = { 44100, 48000, 96000 };

	// 1) чистые синусы: точность в центах (хроматический режим), разные частоты дискретизации
	{
		const double freqs[] = { 30.87, 41.2, 61.74, 82.41, 110.0, 196.0, 329.63, 440.0, 1000.0 };
		const double det[] = { -35, -5, 0, 7, 40 };
		double worst = 0;
		for (int r = 0; r < 3; r++) for (size_t i = 0; i < sizeof(freqs) / sizeof(freqs[0]); i++) for (size_t d = 0; d < 5; d++) {
			Tuner::Core c; c.Init(rates[r]); c.SetPreset(0);
			const double f = freqs[i] * pow(2.0, det[d] / 1200.0);
			Tuner::Reading rd = Feed(c, Synth(rates[r], f, std::vector<double>(1, 1.0), 0.8));
			const double e = rd.voiced ? fabs(CentsErr(rd, f)) : 999;
			if (e > worst) worst = e;
			CHECK(rd.voiced && e < 0.6, "sine %.2f Hz (%+.0f c) @%g: err %.3f cents voiced=%d", freqs[i], det[d], rates[r], e, (int)rd.voiced);
		}
		printf("sines: worst error %.3f cents\n", worst);
	}
	// 2) гармонически богатый звук, 2-я гармоника сильнее основной: нет скачка на октаву (басовый пресет и хроматический)
	{
		std::vector<double> amp; amp.push_back(0.15); amp.push_back(1.0); amp.push_back(0.6); amp.push_back(0.4); amp.push_back(0.25);
		for (int pr = 0; pr < 2; pr++) {
			const int preset = pr == 0 ? 0 : 3;		// хроматический, бас 4
			Tuner::Core c; c.Init(44100); c.SetPreset(preset);
			Tuner::Reading rd = Feed(c, Synth(44100, 41.2, amp, 1.0));
			const double e = rd.voiced ? fabs(CentsErr(rd, 41.2)) : 999;
			CHECK(rd.voiced && e < 1.0, "weak fundamental, preset %d: err %.2f cents (freq %.2f)", preset, e, rd.freq);
			printf("weak fundamental E1 (preset %d): %.2f Hz, err %.2f cents\n", preset, rd.freq, e);
		}
	}
	// 3) щипок: затухающие частичные с лёгкой неидеальностью (струна), на конце затухания точность до 1 цента
	{
		std::vector<double> amp; for (int h = 1; h <= 8; h++) amp.push_back(1.0 / h);
		const double notes[] = { 82.41, 110.0, 146.83, 196.0, 246.94, 329.63 };
		for (size_t i = 0; i < 6; i++) {
			Tuner::Core c; c.Init(48000); c.SetPreset(1);
			Tuner::Reading rd = Feed(c, Synth(48000, notes[i] * pow(2.0, -12.0 / 1200.0), amp, 1.2, 0.6, 0.00001));
			const double e = rd.voiced ? fabs(CentsErr(rd, notes[i] * pow(2.0, -12.0 / 1200.0))) : 999;
			CHECK(rd.voiced && e < 1.0, "pluck %.2f: err %.2f voiced %d", notes[i], e, (int)rd.voiced);
			CHECK(rd.stringIndex == (int)i, "string index %d want %zu", rd.stringIndex, i);
			CHECK(rd.voiced && fabs(rd.cents + 12.0) < 1.0, "cents %.2f want -12", rd.cents);
		}
		printf("plucked guitar strings detected with correct string and cents\n");
	}
	// 4) шум и тишина, тихий сигнал ниже порога
	{
		Tuner::Core c; c.Init(44100); c.SetPreset(0);
		Tuner::Reading rd = Feed(c, Noise(44100, 1.0, 0.3f));
		CHECK(!rd.voiced, "white noise voiced (%.1f Hz)", rd.freq);
		Tuner::Core c2; c2.Init(44100);
		rd = Feed(c2, std::vector<float>(44100, 0.0f));
		CHECK(!rd.voiced, "silence voiced");
		Tuner::Core c3; c3.Init(44100);
		rd = Feed(c3, Synth(44100, 220.0, std::vector<double>(1, 1.0), 1.0, 0, 0, 0.0005));		// около -66 дБ
		CHECK(!rd.voiced, "very quiet signal voiced");
	}
	// 5) ноты и центы: 440 -> A4, 261.63 -> C4, смена эталона A4=432
	{
		Tuner::Core c; c.Init(44100); c.SetPreset(0);
		Tuner::Reading rd = Feed(c, Synth(44100, 440.0, std::vector<double>(1, 1.0), 0.6));
		CHECK(rd.note == 69 && fabs(rd.cents) < 0.6, "A4: note %d cents %.2f", rd.note, rd.cents);
		CHECK(strcmp(Tuner::NoteName(rd.note, Tuner::NOTATION_LATIN), "A") == 0 && Tuner::Octave(rd.note) == 4, "name/octave");
		c.Reset();
		rd = Feed(c, Synth(44100, 261.63, std::vector<double>(1, 1.0), 0.6));
		CHECK(rd.note == 60 && fabs(rd.cents) < 1.0, "C4: note %d cents %.2f", rd.note, rd.cents);
		Tuner::Core c2; c2.Init(44100); c2.SetA4(432.0f);
		rd = Feed(c2, Synth(44100, 432.0, std::vector<double>(1, 1.0), 0.6));
		CHECK(rd.note == 69 && fabs(rd.cents) < 0.6, "A4=432: note %d cents %.2f", rd.note, rd.cents);
		CHECK(strcmp(Tuner::NoteName(61, Tuner::NOTATION_LATIN), "C#") == 0 && strcmp(Tuner::NoteName(69, Tuner::NOTATION_SOLFEGE), "Ля") == 0, "names");
		CHECK(Tuner::Octave(23) == 0 && Tuner::Octave(28) == 1, "octaves");
	}
	// 6) пресеты: 7-струнная B1 (61.74), бас 5 B0 (30.87) находят свою струну; струна далеко от строя всё равно ближайшая
	{
		Tuner::Core c; c.Init(44100); c.SetPreset(2);
		Tuner::Reading rd = Feed(c, Synth(44100, 61.74, std::vector<double>(1, 1.0), 1.0));
		CHECK(rd.stringIndex == 0 && rd.note == 35 && fabs(rd.cents) < 1.0, "7-string B1: idx %d note %d cents %.2f", rd.stringIndex, rd.note, rd.cents);
		Tuner::Core b; b.Init(44100); b.SetPreset(4);
		rd = Feed(b, Synth(44100, 30.87, std::vector<double>(1, 1.0), 1.5));
		CHECK(rd.stringIndex == 0 && rd.note == 23 && fabs(rd.cents) < 1.0, "bass B0: idx %d note %d cents %.2f", rd.stringIndex, rd.note, rd.cents);
		Tuner::Core g; g.Init(44100); g.SetPreset(1);
		rd = Feed(g, Synth(44100, 107.0, std::vector<double>(1, 1.0), 0.8));		// A2 понижена примерно на 45 центов
		CHECK(rd.stringIndex == 1 && rd.cents < -40 && rd.cents > -50, "flat A string: idx %d cents %.1f", rd.stringIndex, rd.cents);
		printf("presets: B1, B0, flat A string recognised\n");
		CHECK(Tuner::PresetCount() == 5 && Tuner::PresetAt(1).count == 6 && Tuner::PresetAt(2).count == 7, "preset table");
	}
	// 7) скорость: за сколько блоков (по 512 при 44.1 кГц) появляется устойчивое показание на E2 и E1
	{
		const double fs[2] = { 82.41, 41.2 };
		for (int k = 0; k < 2; k++) {
			Tuner::Core c; c.Init(44100); c.SetPreset(k == 0 ? 1 : 3);
			std::vector<float> x = Synth(44100, fs[k], std::vector<double>(1, 1.0), 1.0);
			int first = -1;
			for (size_t p = 0, blk = 0; p + 512 <= x.size(); p += 512, blk++) {
				c.Push(&x[p], 512);
				Tuner::Reading r = c.Update();
				if (r.voiced && first < 0) first = (int)blk;
			}
			const double ms = first < 0 ? -1 : (first + 1) * 512.0 * 1000.0 / 44100.0;
			CHECK(ms > 0 && ms < 400, "first reading at %.0f ms", ms);
			printf("time to first reading for %.2f Hz: %.0f ms\n", fs[k], ms);
		}
	}
	// 8) удержание: после затухания звука показание держится около 0.35 с, потом пропадает
	{
		Tuner::Core c; c.Init(44100); c.SetPreset(1);
		Feed(c, Synth(44100, 110.0, std::vector<double>(1, 1.0), 0.6));
		std::vector<float> z(512, 0.0f);
		int heldBlocks = 0, total = 0; bool gone = false;
		for (int i = 0; i < 60; i++) {
			c.Push(&z[0], 512);
			Tuner::Reading r = c.Update();
			total++;
			if (r.voiced && r.held) heldBlocks++;
			if (!r.voiced) { gone = true; break; }
		}
		const double heldMs = heldBlocks * 512.0 * 1000.0 / 44100.0;
		CHECK(gone && heldMs > 250 && heldMs < 700, "hold %.0f ms gone=%d", heldMs, (int)gone);
		printf("hold after silence: %.0f ms\n", heldMs);
	}
	// 9) скачок на другую ноту: показание переключается
	{
		Tuner::Core c; c.Init(44100); c.SetPreset(1);
		Feed(c, Synth(44100, 82.41, std::vector<double>(1, 1.0), 0.6));
		Tuner::Reading rd = Feed(c, Synth(44100, 110.0, std::vector<double>(1, 1.0), 0.6));
		CHECK(rd.voiced && rd.stringIndex == 1, "after switch idx %d", rd.stringIndex);
	}
	printf(fails ? "FAILED %d\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}
