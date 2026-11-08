#include "MidiSynth.h"
#include <math.h>
#include <algorithm>

namespace {

enum { ES_DELAY, ES_ATTACK, ES_HOLD, ES_DECAY, ES_SUSTAIN, ES_RELEASE, ES_DONE };

struct Voice {
	const SfZone	*zone;
	const SfSample	*smp;
	int				ch, key;
	double			pos;
	double			baseCents;		// высота без pitch bend
	double			ratioMul;		// частота сэмпла / частота вывода
	double			ratio;
	double			loopStart, loopEnd, end;
	int				mode;			// sampleModes: 0 без петли, 1 петля, 3 петля до отпускания
	int				state;
	double			amp;			// огибающая 0..1
	double			delayLeft, attackStep, holdLeft, decayMul, sustainAmp, releaseMul;
	float			gain, gl, gr;
	bool			released;		// нота отпущена
	bool			held;			// отпущена, но ждёт педаль
};

struct Chan {
	int		bank, program;
	int		vol, expr;
	bool	sustain;
	double	bend;		// полутоны
	Chan() : bank(0), program(0), vol(127), expr(127), sustain(false), bend(0) {}
};

double TimeSec(int timecents) { return pow(2.0, timecents / 1200.0); }

void StartRelease(Voice &v, double rate)
{
	if (v.state == ES_RELEASE || v.state == ES_DONE) return;
	double rel = TimeSec(v.zone->gen[SFG_RELEASE]) * rate;
	if (rel < 1) rel = 1;
	v.releaseMul = pow(1e-5, 1.0 / rel);		// -100 дБ за время release
	v.state = ES_RELEASE;
}

} // namespace

bool RenderMidiTrack(const MidiTrackData &track, const SoundFont &sf, float rate, std::vector<float> &out)
{
	out.clear();
	if (track.ev.empty() || sf.presets.empty() || rate < 8000) return false;

	std::vector<MidiEvent> ev = track.ev;
	std::stable_sort(ev.begin(), ev.end(), [](const MidiEvent &a, const MidiEvent &b) { return a.t < b.t; });

	Chan ch[16];
	std::vector<Voice> voices;
	const size_t maxFrames = (size_t)(rate * 60.0 * 30.0);		// не длиннее 30 минут
	size_t pos = 0;
	size_t ei = 0;
	bool any = false;

	auto frameOf = [&](double t) { return (size_t)(t * rate + 0.5); };
	auto ensure = [&](size_t frames) { if (out.size() < frames * 2) out.resize(frames * 2, 0.0f); };

	auto renderTo = [&](size_t to) {
		if (to <= pos) return;
		ensure(to);
		for (size_t vi = 0; vi < voices.size(); vi++) {
			Voice &v = voices[vi];
			if (v.state == ES_DONE) continue;
			const int16_t *pcm = sf.pcm.data();
			v.ratio = v.ratioMul * pow(2.0, (v.baseCents + ch[v.ch].bend * 100.0) / 1200.0);
			for (size_t f = pos; f < to; f++) {
				// огибающая
				switch (v.state) {
				case ES_DELAY: v.amp = 0; if (--v.delayLeft <= 0) v.state = ES_ATTACK; break;
				case ES_ATTACK: v.amp += v.attackStep; if (v.amp >= 1.0) { v.amp = 1.0; v.state = ES_HOLD; } break;
				case ES_HOLD: if (--v.holdLeft <= 0) v.state = ES_DECAY; break;
				case ES_DECAY: v.amp *= v.decayMul; if (v.amp <= v.sustainAmp) { v.amp = v.sustainAmp; v.state = ES_SUSTAIN; } break;
				case ES_SUSTAIN: break;
				case ES_RELEASE: v.amp *= v.releaseMul; if (v.amp < 1e-5) v.state = ES_DONE; break;
				}
				if (v.state == ES_DONE) break;
				const bool looping = (v.mode == 1 || (v.mode == 3 && v.state != ES_RELEASE)) && v.loopEnd > v.loopStart + 1;
				if (looping) { while (v.pos >= v.loopEnd) v.pos -= (v.loopEnd - v.loopStart); }
				if (!looping && v.pos >= v.end - 1) { v.state = ES_DONE; break; }
				const size_t i0 = (size_t)v.pos;
				const float fr = (float)(v.pos - (double)i0);
				const float s = (pcm[i0] + (pcm[i0 + 1] - pcm[i0]) * fr) * (1.0f / 32768.0f);
				const float a = s * (float)v.amp * v.gain * ((float)(ch[v.ch].vol * ch[v.ch].vol) / (127.0f * 127.0f))
					* ((float)(ch[v.ch].expr * ch[v.ch].expr) / (127.0f * 127.0f));
				out[f * 2] += a * v.gl;
				out[f * 2 + 1] += a * v.gr;
				v.pos += v.ratio;
				any = true;
			}
		}
		pos = to;
		// удалить закончившиеся
		voices.erase(std::remove_if(voices.begin(), voices.end(), [](const Voice &v) { return v.state == ES_DONE; }), voices.end());
	};

	auto noteOn = [&](const MidiEvent &e) {
		Chan &c = ch[e.ch];
		const SfPreset *pr = sf.Find(e.ch == 9 ? 128 : c.bank, c.program);
		if (!pr) return;
		for (size_t zi = 0; zi < pr->zones.size(); zi++) {
			const SfZone &z = pr->zones[zi];
			if (e.a < z.keyLo || e.a > z.keyHi || e.b < z.velLo || e.b > z.velHi) continue;
			const SfSample &s = sf.samples[z.sample];
			Voice v;
			v.zone = &z; v.smp = &s; v.ch = e.ch; v.key = e.a;
			// адреса сэмпла с поправками генераторов
			const double start = (double)s.start + z.gen[SFG_START] + (double)z.gen[SFG_START_COARSE] * 32768.0;
			v.end = (double)s.end + z.gen[SFG_END] + (double)z.gen[SFG_END_COARSE] * 32768.0;
			v.loopStart = (double)s.loopStart + z.gen[SFG_STARTLOOP] + (double)z.gen[SFG_STARTLOOP_COARSE] * 32768.0;
			v.loopEnd = (double)s.loopEnd + z.gen[SFG_ENDLOOP] + (double)z.gen[SFG_ENDLOOP_COARSE] * 32768.0;
			if (v.end > (double)sf.pcm.size() - 1) v.end = (double)sf.pcm.size() - 1;
			if (v.loopEnd > v.end) v.loopEnd = v.end;
			if (v.loopStart < 0) v.loopStart = 0;
			if (start < 0 || start >= v.end) continue;
			v.pos = start;
			const int mode = z.gen[SFG_SAMPLE_MODES] & 3;
			v.mode = mode == 2 ? 0 : mode;
			const int root = z.gen[SFG_ROOT_KEY] >= 0 ? z.gen[SFG_ROOT_KEY] : s.key;
			v.baseCents = (e.a - root) * (double)z.gen[SFG_SCALE_TUNING] + z.gen[SFG_COARSE_TUNE] * 100.0
				+ z.gen[SFG_FINE_TUNE] + s.correction;
			v.ratioMul = (double)(s.rate > 0 ? s.rate : 44100) / rate;
			v.ratio = v.ratioMul;
			// огибающая громкости
			v.state = ES_DELAY;
			v.amp = 0;
			v.delayLeft = TimeSec(z.gen[SFG_DELAY]) * rate;
			if (v.delayLeft < 1) v.state = ES_ATTACK;
			double atk = TimeSec(z.gen[SFG_ATTACK]) * rate;
			v.attackStep = 1.0 / (atk < 1 ? 1 : atk);
			v.holdLeft = TimeSec(z.gen[SFG_HOLD]) * rate;
			double sus = z.gen[SFG_SUSTAIN] < 0 ? 0 : z.gen[SFG_SUSTAIN];			// сантибелы ослабления
			if (sus > 1000) sus = 1000;
			v.sustainAmp = pow(10.0, -sus / 200.0);
			double dec = TimeSec(z.gen[SFG_DECAY]) * rate;
			if (dec < 1) dec = 1;
			v.decayMul = pow(v.sustainAmp, 1.0 / dec);							// спад до уровня sustain за время decay
			v.releaseMul = 1.0;
			// громкость: velocity (квадратичная кривая SF2) и ослабление зоны; панорама только из самого банка (стерео-пары)
			const float vel = e.b / 127.0f;
			double att = z.gen[SFG_ATTENUATION] < 0 ? 0 : z.gen[SFG_ATTENUATION];
			v.gain = vel * vel * (float)pow(10.0, -att / 200.0);
			double pan = z.gen[SFG_PAN];
			if (pan < -500) pan = -500;
			if (pan > 500) pan = 500;
			v.gl = pan > 0 ? (float)(1.0 - pan / 500.0) : 1.0f;
			v.gr = pan < 0 ? (float)(1.0 + pan / 500.0) : 1.0f;
			v.released = false; v.held = false;
			voices.push_back(v);
		}
	};

	auto noteOff = [&](const MidiEvent &e) {
		for (size_t vi = 0; vi < voices.size(); vi++) {
			Voice &v = voices[vi];
			if (v.ch != e.ch || v.key != e.a || v.released || v.state == ES_DONE) continue;
			v.released = true;
			if (ch[e.ch].sustain) v.held = true;
			else StartRelease(v, rate);
		}
	};

	while (ei < ev.size() || !voices.empty()) {
		size_t target;
		if (ei < ev.size()) target = frameOf(ev[ei].t);
		else target = pos + 4096;					// событий нет: дорисовываем хвосты
		if (target > maxFrames) break;
		if (target < pos) target = pos;
		renderTo(target);
		if (ei >= ev.size()) continue;
		while (ei < ev.size() && frameOf(ev[ei].t) <= pos) {
			const MidiEvent &e = ev[ei++];
			Chan &c = ch[e.ch];
			switch (e.type) {
			case ME_NOTE_ON: noteOn(e); break;
			case ME_NOTE_OFF: noteOff(e); break;
			case ME_PROGRAM: c.program = e.a; break;
			case ME_BEND: c.bend = (e.a / 8192.0) * 2.0; break;
			case ME_CC:
				if (e.a == 0) c.bank = e.b;
				else if (e.a == 7) c.vol = e.b;
				else if (e.a == 11) c.expr = e.b;
				else if (e.a == 64) {
					const bool down = e.b >= 64;
					if (c.sustain && !down)
						for (size_t vi = 0; vi < voices.size(); vi++)
							if (voices[vi].ch == e.ch && voices[vi].held) { voices[vi].held = false; StartRelease(voices[vi], rate); }
					c.sustain = down;
				}
				else if (e.a == 123 || e.a == 120)
					for (size_t vi = 0; vi < voices.size(); vi++)
						if (voices[vi].ch == e.ch) { voices[vi].released = true; voices[vi].held = false; StartRelease(voices[vi], rate); }
				break;
			}
		}
	}
	if (!any) { out.clear(); return false; }
	// обрезать тишину в хвосте и не допустить перегрузки
	size_t n = out.size() / 2;
	float peak = 0;
	for (size_t k = 0; k < out.size(); k++) { float a = fabsf(out[k]); if (a > peak) peak = a; }
	while (n > 1 && fabsf(out[(n - 1) * 2]) < 1e-5f && fabsf(out[(n - 1) * 2 + 1]) < 1e-5f) n--;
	out.resize(n * 2);
	if (peak > 1.0f) { const float g = 0.99f / peak; for (size_t k = 0; k < out.size(); k++) out[k] *= g; }
	return true;
}
