#include "MidiFile.h"
#include <string.h>
#include <algorithm>
#include <map>

namespace {

struct Reader {
	const uint8_t *p; size_t n, i;
	bool ok() const { return i < n; }
	uint8_t u8() { return i < n ? p[i++] : 0; }
	uint32_t be(int bytes) { uint32_t v = 0; while (bytes--) v = (v << 8) | u8(); return v; }
	uint32_t vlq() { uint32_t v = 0; for (int k = 0; k < 4 && i < n; k++) { uint8_t b = u8(); v = (v << 7) | (b & 0x7f); if (!(b & 0x80)) break; } return v; }
};

struct RawEv { uint64_t tick; MidiEvent e; };
struct TempoPt { uint64_t tick; uint32_t us; };

struct RawTrack { std::string name; std::vector<RawEv> ev; };

} // namespace

bool LoadMidi(const uint8_t *data, size_t size, MidiSong &song, std::string &err)
{
	song = MidiSong();
	Reader r = { data, size, 0 };
	if (size < 14 || memcmp(data, "MThd", 4) != 0) { err = "not a MIDI file"; return false; }
	r.i = 4;
	uint32_t hlen = r.be(4);
	int format = (int)r.be(2), ntrk = (int)r.be(2), div = (int)r.be(2);
	if (hlen > 6) r.i += hlen - 6;
	if (div & 0x8000) { err = "SMPTE time division is not supported"; return false; }
	if (div <= 0) { err = "bad time division"; return false; }
	song.format = format;

	std::vector<RawTrack> raw;
	std::vector<TempoPt> tempos;
	for (int t = 0; t < ntrk && r.i + 8 <= size; t++) {
		if (memcmp(data + r.i, "MTrk", 4) != 0) {	// пропуск чужих блоков
			r.i += 4; uint32_t l = r.be(4); r.i += l; t--; continue;
		}
		r.i += 4;
		uint32_t len = r.be(4);
		size_t end = std::min(size, r.i + (size_t)len);
		RawTrack rt;
		uint64_t tick = 0;
		int running = 0;
		while (r.i < end) {
			tick += r.vlq();
			int st = data[r.i];
			if (st & 0x80) { r.i++; if (st < 0xF0) running = st; } else st = running;
			if (st == 0xFF) {
				int type = r.u8(); uint32_t l = r.vlq();
				if (r.i + l > end) break;
				if (type == 0x03 && rt.name.empty()) rt.name.assign((const char *)data + r.i, l);
				else if (type == 0x51 && l == 3) { TempoPt tp = { tick, (uint32_t)((data[r.i] << 16) | (data[r.i+1] << 8) | data[r.i+2]) }; tempos.push_back(tp); }
				r.i += l;
				if (type == 0x2F) break;
			} else if (st == 0xF0 || st == 0xF7) {
				uint32_t l = r.vlq(); r.i += l;
			} else if (st >= 0x80) {
				RawEv re; re.tick = tick; re.e.t = 0; re.e.ch = st & 0x0f; re.e.a = re.e.b = 0;
				int hi = st & 0xF0;
				if (hi == 0xC0 || hi == 0xD0) {
					int a = r.u8() & 0x7f;
					if (hi == 0xC0) { re.e.type = ME_PROGRAM; re.e.a = a; rt.ev.push_back(re); }
				} else {
					int a = r.u8() & 0x7f, b = r.u8() & 0x7f;
					if (hi == 0x90 && b > 0) { re.e.type = ME_NOTE_ON; re.e.a = a; re.e.b = b; rt.ev.push_back(re); }
					else if (hi == 0x80 || hi == 0x90) { re.e.type = ME_NOTE_OFF; re.e.a = a; re.e.b = 0; rt.ev.push_back(re); }
					else if (hi == 0xB0) { re.e.type = ME_CC; re.e.a = a; re.e.b = b; rt.ev.push_back(re); }
					else if (hi == 0xE0) { re.e.type = ME_BEND; re.e.a = ((b << 7) | a) - 8192; rt.ev.push_back(re); }
				}
			} else break;			// мусор
		}
		r.i = end;
		raw.push_back(rt);
	}
	if (raw.empty()) { err = "no tracks"; return false; }

	// карта темпов: тики -> секунды
	std::stable_sort(tempos.begin(), tempos.end(), [](const TempoPt &a, const TempoPt &b) { return a.tick < b.tick; });
	struct Seg { uint64_t tick; double sec; uint32_t us; };
	std::vector<Seg> segs;
	{
		Seg s = { 0, 0.0, 500000 };
		segs.push_back(s);
		for (size_t k = 0; k < tempos.size(); k++) {
			const Seg &l = segs.back();
			double sec = l.sec + (double)(tempos[k].tick - l.tick) * l.us / 1e6 / div;
			if (tempos[k].tick == l.tick) segs.back().us = tempos[k].us;
			else { Seg ns = { tempos[k].tick, sec, tempos[k].us }; segs.push_back(ns); }
		}
	}
	auto toSec = [&](uint64_t tick) {
		size_t k = segs.size() - 1;
		while (k > 0 && segs[k].tick > tick) k--;
		return segs[k].sec + (double)(tick - segs[k].tick) * segs[k].us / 1e6 / div;
	};

	for (size_t ti = 0; ti < raw.size(); ti++) {
		RawTrack &rt = raw[ti];
		std::stable_sort(rt.ev.begin(), rt.ev.end(), [](const RawEv &a, const RawEv &b) { return a.tick < b.tick; });
		for (size_t k = 0; k < rt.ev.size(); k++) rt.ev[k].e.t = toSec(rt.ev[k].tick);
		int notes = 0, chmask = 0;
		for (size_t k = 0; k < rt.ev.size(); k++)
			if (rt.ev[k].e.type == ME_NOTE_ON) { notes++; chmask |= 1 << rt.ev[k].e.ch; }
		if (!notes) continue;
		int nch = 0;
		for (int c = 0; c < 16; c++) if (chmask & (1 << c)) nch++;
		// формат 0 (или несколько каналов в одной дорожке формата 0): по дорожке на канал
		const bool split = (format == 0 && nch > 1);
		for (int c = 0; c < 16; c++) {
			if (split ? !(chmask & (1 << c)) : (c != 0)) continue;
			MidiTrackData td;
			td.name = rt.name;
			td.notes = 0;
			td.channel = split ? c : (nch == 1 ? __builtin_ctz(chmask) : -1);
			td.program = -1;
			for (size_t k = 0; k < rt.ev.size(); k++) {
				const MidiEvent &e = rt.ev[k].e;
				if (split && e.ch != c) continue;
				td.ev.push_back(e);
				if (e.type == ME_NOTE_ON) td.notes++;
				if (e.type == ME_PROGRAM && td.program < 0) td.program = e.a;
			}
			if (split && td.name.empty()) td.name = "";
			song.tracks.push_back(td);
		}
	}
	if (song.tracks.empty()) { err = "no notes"; return false; }
	return true;
}

std::string MidiTrackTitle(const MidiTrackData &t, int index)
{
	if (!t.name.empty()) {
		std::string s = t.name;
		while (!s.empty() && (s[s.size()-1] == ' ' || s[s.size()-1] == '\r' || s[s.size()-1] == '\n')) s.erase(s.size()-1);
		if (!s.empty()) return s;
	}
	if (t.channel == 9) return "Drums";
	static const char *family[16] = { "Piano", "Chromatic percussion", "Organ", "Guitar", "Bass", "Strings", "Ensemble", "Brass",
		"Reed", "Pipe", "Synth lead", "Synth pad", "Synth effects", "Ethnic", "Percussive", "Sound effects" };
	if (t.program >= 0 && t.program < 128) return family[t.program / 8];
	return "MIDI " + std::to_string(index);
}
