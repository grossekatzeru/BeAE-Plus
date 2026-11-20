#include "SmfFile.h"

#include <algorithm>

namespace {

struct Reader {
	const uint8_t* p;
	const uint8_t* end;
	bool ok;
	Reader(const uint8_t* b, const uint8_t* e) : p(b), end(e), ok(true) {}
	bool More() const { return p < end; }
	uint8_t U8() { if (p >= end) { ok = false; return 0; } return *p++; }
	uint16_t U16() { uint16_t a = U8(); return (uint16_t)((a << 8) | U8()); }
	uint32_t U32() { uint32_t a = U16(); return (a << 16) | U16(); }
	uint32_t Vlq() {
		uint32_t v = 0;
		for (int i = 0; i < 4; i++) {
			uint8_t b = U8();
			v = (v << 7) | (b & 0x7F);
			if (!(b & 0x80) || !ok) break;
		}
		return v;
	}
	void Skip(uint32_t n) {
		if ((size_t)(end - p) < n) { ok = false; p = end; } else p += n;
	}
};

struct Raw {
	uint64_t	tick;
	uint32_t	order;
	uint8_t		status, d1, d2;
};

struct TempoPoint {
	uint64_t	tick;
	uint32_t	usPerQuarter;
	int64_t		us;			// time of this point
};

bool RawLess(const Raw& a, const Raw& b)
{
	if (a.tick != b.tick)
		return a.tick < b.tick;
	return a.order < b.order;
}

}	// namespace

bool ParseSmf(const uint8_t* data, size_t size, SmfSong& song, std::string& error)
{
	song = SmfSong();

	// some files are wrapped into RIFF (RMID): look for the header in the first bytes
	size_t start = 0;
	bool found = false;
	for (; start + 14 <= size && start < 64; start++) {
		if (data[start] == 'M' && data[start + 1] == 'T' && data[start + 2] == 'h' && data[start + 3] == 'd') {
			found = true;
			break;
		}
	}
	if (!found) {
		error = "Это не MIDI-файл (нет заголовка MThd)";
		return false;
	}

	Reader r(data + start + 4, data + size);
	uint32_t hlen = r.U32();
	int format = r.U16();
	int ntracks = r.U16();
	int division = r.U16();
	if (!r.ok || hlen < 6) {
		error = "Повреждённый заголовок MIDI-файла";
		return false;
	}
	r.Skip(hlen - 6);
	if (format > 1) {
		error = "Формат 2 (последовательности песен) не поддерживается";
		return false;
	}
	if (division == 0) {
		error = "Некорректное разрешение (division = 0)";
		return false;
	}

	bool smpte = (division & 0x8000) != 0;
	double ticksPerSecond = 0;
	uint32_t ppq = 480;
	if (smpte) {
		int fps = -(int8_t)(division >> 8);
		int sub = division & 0xFF;
		ticksPerSecond = (double)fps * sub;
		if (ticksPerSecond <= 0) {
			error = "Некорректное SMPTE-разрешение";
			return false;
		}
	} else
		ppq = (uint32_t)division;

	std::vector<Raw> raws;
	std::vector<std::pair<uint64_t, uint32_t> > tempos;
	uint64_t maxTick = 0;
	uint32_t order = 0;
	int tracksRead = 0;

	while (r.More() && tracksRead < ntracks && r.ok) {
		if (r.end - r.p < 8)
			break;
		uint32_t id = r.U32();
		uint32_t len = r.U32();
		if (!r.ok)
			break;
		if ((size_t)(r.end - r.p) < len)
			len = (uint32_t)(r.end - r.p);		// truncated file: take what is there
		if (id != 0x4D54726B) {					// not "MTrk": skip the chunk
			r.Skip(len);
			continue;
		}
		tracksRead++;

		Reader t(r.p, r.p + len);
		r.Skip(len);
		uint64_t tick = 0;
		uint8_t running = 0;
		std::string trackName;
		bool usedHere[16] = { false };
		while (t.More() && t.ok) {
			tick += t.Vlq();
			if (!t.ok)
				break;
			uint8_t b = t.U8();
			if (b == 0xFF) {						// meta event
				uint8_t type = t.U8();
				uint32_t mlen = t.Vlq();
				if (type == 0x51 && mlen == 3 && (size_t)(t.end - t.p) >= 3) {
					uint32_t v = ((uint32_t)t.p[0] << 16) | ((uint32_t)t.p[1] << 8) | t.p[2];
					if (v > 0)
						tempos.push_back(std::make_pair(tick, v));
				}
				if (type == 0x03 && trackName.empty() && (size_t)(t.end - t.p) >= mlen) {
					for (uint32_t k = 0; k < mlen; k++) {
						uint8_t ch = t.p[k];
						if (ch >= 32)
							trackName += (char)ch;
					}
				}
				t.Skip(mlen);
				if (type == 0x2F)
					break;
				continue;
			}
			if (b == 0xF0 || b == 0xF7) {			// system exclusive: not played
				uint32_t slen = t.Vlq();
				t.Skip(slen);
				continue;
			}
			uint8_t status;
			uint8_t d1;
			if (b & 0x80) {
				status = b;
				if (status >= 0xF0)
					continue;
				running = status;
				d1 = t.U8();
			} else {
				if (!running)
					break;								// garbage
				status = running;
				d1 = b;
			}
			uint8_t d2 = 0;
			uint8_t cmd = status & 0xF0;
			if (cmd != 0xC0 && cmd != 0xD0)
				d2 = t.U8();
			if (!t.ok)
				break;
			if (cmd == 0x90 && d2 == 0) {			// note on with velocity 0 is a note off
				status = (uint8_t)(0x80 | (status & 0x0F));
			}
			{
				SmfChannel& sc = song.channels[status & 0x0F];
				usedHere[status & 0x0F] = true;
				if (cmd == 0x90 && d2 > 0)
					sc.notes++;
				else if (cmd == 0xC0 && sc.program < 0)
					sc.program = d1 & 0x7F;
			}
			Raw e;
			e.tick = tick;
			e.order = order++;
			e.status = status;
			e.d1 = d1 & 0x7F;
			e.d2 = d2 & 0x7F;
			raws.push_back(e);
		}
		if (tick > maxTick)
			maxTick = tick;
		// a track that plays one channel names it
		int used = 0, only = 0;
		for (int ch = 0; ch < 16; ch++) {
			if (usedHere[ch]) {
				used++;
				only = ch;
			}
		}
		if (used == 1 && song.channels[only].name.empty())
			song.channels[only].name = trackName;
	}

	if (tracksRead == 0) {
		error = "В файле нет дорожек";
		return false;
	}

	std::stable_sort(raws.begin(), raws.end(), RawLess);

	// tempo map
	std::stable_sort(tempos.begin(), tempos.end());
	std::vector<TempoPoint> map;
	TempoPoint first;
	first.tick = 0;
	first.usPerQuarter = 500000;
	first.us = 0;
	map.push_back(first);
	for (size_t i = 0; i < tempos.size(); i++) {
		TempoPoint& last = map.back();
		if (tempos[i].first == last.tick) {
			last.usPerQuarter = tempos[i].second;
			continue;
		}
		TempoPoint tp;
		tp.tick = tempos[i].first;
		tp.usPerQuarter = tempos[i].second;
		tp.us = last.us + (int64_t)((tp.tick - last.tick) * (uint64_t)last.usPerQuarter / ppq);
		map.push_back(tp);
	}

	size_t seg = 0;
	song.events.reserve(raws.size());
	for (size_t i = 0; i < raws.size(); i++) {
		SmfEvent e;
		if (smpte)
			e.us = (int64_t)((double)raws[i].tick * 1000000.0 / ticksPerSecond);
		else {
			while (seg + 1 < map.size() && map[seg + 1].tick <= raws[i].tick)
				seg++;
			e.us = map[seg].us + (int64_t)((raws[i].tick - map[seg].tick) * (uint64_t)map[seg].usPerQuarter / ppq);
		}
		e.status = raws[i].status;
		e.d1 = raws[i].d1;
		e.d2 = raws[i].d2;
		song.events.push_back(e);
	}

	if (smpte)
		song.durationUs = (int64_t)((double)maxTick * 1000000.0 / ticksPerSecond);
	else {
		size_t s = 0;
		while (s + 1 < map.size() && map[s + 1].tick <= maxTick)
			s++;
		song.durationUs = map[s].us + (int64_t)((maxTick - map[s].tick) * (uint64_t)map[s].usPerQuarter / ppq);
	}
	if (!song.events.empty() && song.events.back().us > song.durationUs)
		song.durationUs = song.events.back().us;

	song.format = format;
	song.tracks = tracksRead;
	if (song.events.empty()) {
		error = "В файле нет нот и других MIDI-событий";
		return false;
	}
	return true;
}
