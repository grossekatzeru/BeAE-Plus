#include "MidiSynth.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <string>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

typedef std::vector<uint8_t> Bytes;
static void P16(Bytes &b, int v) { b.push_back(v & 255); b.push_back((v >> 8) & 255); }
static void P32(Bytes &b, uint32_t v) { P16(b, v & 0xffff); P16(b, v >> 16); }
static void PStr(Bytes &b, const char *s, int n) { for (int i = 0; i < n; i++) b.push_back(i < (int)strlen(s) ? s[i] : 0); }
static void Chunk(Bytes &o, const char *id, const Bytes &body) { o.insert(o.end(), id, id + 4); P32(o, body.size()); o.insert(o.end(), body.begin(), body.end()); if (body.size() & 1) o.push_back(0); }
static void List(Bytes &o, const char *type, const Bytes &body) { Bytes b; b.insert(b.end(), type, type + 4); b.insert(b.end(), body.begin(), body.end()); Chunk(o, "LIST", b); }

struct G { int op, val; };
struct TInst { std::vector<G> gens; int sample; };
struct TSample { std::vector<int16_t> data; int key; bool loop; };
struct TPreset { const char *name; int bank, prog; int inst; };

// собирает SF2: сэмплы с нулями-подкладкой, инструмент на сэмпл, пресет на инструмент
static Bytes BuildSf2(const std::vector<TSample> &smp, const std::vector<TInst> &inst, const std::vector<TPreset> &pre)
{
	Bytes pcm; std::vector<uint32_t> starts;
	for (size_t i = 0; i < smp.size(); i++) {
		starts.push_back(pcm.size() / 2);
		for (size_t k = 0; k < smp[i].data.size(); k++) P16(pcm, (uint16_t)smp[i].data[k]);
		for (int k = 0; k < 46; k++) P16(pcm, 0);
	}
	Bytes shdr, phdr, pbag, pgen, instb, ibag, igen;
	for (size_t i = 0; i < smp.size(); i++) {
		PStr(shdr, "smp", 20); P32(shdr, starts[i]); P32(shdr, starts[i] + smp[i].data.size());
		P32(shdr, smp[i].loop ? starts[i] : starts[i] + 1); P32(shdr, smp[i].loop ? starts[i] + smp[i].data.size() : starts[i] + 2);
		P32(shdr, 44100); shdr.push_back(smp[i].key); shdr.push_back(0); P16(shdr, 0); P16(shdr, 1);
	}
	PStr(shdr, "EOS", 20); for (int k = 0; k < 26; k++) shdr.push_back(0);
	int ig = 0;
	for (size_t i = 0; i < inst.size(); i++) {
		PStr(instb, "inst", 20); P16(instb, (int)i);
		P16(ibag, ig); P16(ibag, 0);
		for (size_t k = 0; k < inst[i].gens.size(); k++) { P16(igen, inst[i].gens[k].op); P16(igen, (uint16_t)inst[i].gens[k].val); ig++; }
		P16(igen, 53); P16(igen, inst[i].sample); ig++;
	}
	PStr(instb, "EOI", 20); P16(instb, (int)inst.size());
	P16(ibag, ig); P16(ibag, 0);
	P16(igen, 0); P16(igen, 0);
	for (size_t i = 0; i < pre.size(); i++) {
		PStr(phdr, pre[i].name, 20); P16(phdr, pre[i].prog); P16(phdr, pre[i].bank); P16(phdr, (int)i); P32(phdr, 0); P32(phdr, 0); P32(phdr, 0);
		P16(pbag, (int)i); P16(pbag, 0);
		P16(pgen, 41); P16(pgen, pre[i].inst);
	}
	PStr(phdr, "EOP", 20); P16(phdr, 0); P16(phdr, 0); P16(phdr, (int)pre.size()); P32(phdr, 0); P32(phdr, 0); P32(phdr, 0);
	P16(pbag, (int)pre.size()); P16(pbag, 0);
	P16(pgen, 0); P16(pgen, 0);
	// pmod/imod - пустые терминаторы
	Bytes pmod(10, 0), imod(10, 0);
	Bytes info; { Bytes n; PStr(n, "TestSF", 8); Chunk(info, "INAM", n); }
	Bytes sdta; Chunk(sdta, "smpl", pcm);
	Bytes pdta;
	Chunk(pdta, "phdr", phdr); Chunk(pdta, "pbag", pbag); Chunk(pdta, "pmod", pmod); Chunk(pdta, "pgen", pgen);
	Chunk(pdta, "inst", instb); Chunk(pdta, "ibag", ibag); Chunk(pdta, "imod", imod); Chunk(pdta, "igen", igen); Chunk(pdta, "shdr", shdr);
	Bytes body; body.insert(body.end(), "sfbk", "sfbk" + 4);
	List(body, "INFO", info); List(body, "sdta", sdta); List(body, "pdta", pdta);
	Bytes out; Chunk(out, "RIFF", body);
	return out;
}

static std::vector<int16_t> Sine(int periods, int per, float amp = 0.5f) { std::vector<int16_t> d(periods * per); for (size_t i = 0; i < d.size(); i++) d[i] = (int16_t)(amp * 32767 * sin(2 * M_PI * i / per)); return d; }

static double Freq(const std::vector<float> &o, double t0, double t1, float rate)
{
	int cross = 0; size_t a = (size_t)(t0 * rate), b = (size_t)(t1 * rate);
	for (size_t i = a + 1; i < b && i < o.size() / 2; i++) if (o[(i - 1) * 2] < 0 && o[i * 2] >= 0) cross++;
	return cross / (t1 - t0);
}
static float PeakIn(const std::vector<float> &o, double t0, double t1, float rate)
{
	float p = 0; for (size_t i = (size_t)(t0 * rate); i < (size_t)(t1 * rate) && i < o.size() / 2; i++) p = fmaxf(p, fabsf(o[i * 2]));
	return p;
}

static MidiEvent E(double t, int type, int ch, int a, int b = 0) { MidiEvent e; e.t = t; e.type = (uint8_t)type; e.ch = (uint8_t)ch; e.a = a; e.b = b; return e; }

static MidiTrackData Track(std::vector<MidiEvent> ev) { MidiTrackData t; t.ev = ev; t.notes = 1; t.channel = 0; t.program = -1; return t; }

int main()
{
	const float R = 44100;
	std::vector<TSample> smp;
	{ TSample s; s.data = Sine(44, 100); s.key = 69; s.loop = true; smp.push_back(s); }		// 441 Гц, зациклен
	{ TSample s; s.data = std::vector<int16_t>(4000, 16000); s.key = 60; s.loop = false; smp.push_back(s); }	// постоянный, не зациклен (ударный)
	{ TSample s; s.data = Sine(44, 100, 0.25f); s.key = 69; s.loop = true; smp.push_back(s); }	// тише в 2 раза
	std::vector<TInst> inst(3);
	inst[0].sample = 0; inst[0].gens.push_back(G{54, 1}); inst[0].gens.push_back(G{58, 69});
	inst[1].sample = 1;
	inst[2].sample = 2; inst[2].gens.push_back(G{54, 1}); inst[2].gens.push_back(G{58, 69});
	std::vector<TPreset> pre = { { "Sine", 0, 0, 0 }, { "Quiet", 0, 1, 2 }, { "Drums", 128, 0, 1 } };
	Bytes sf2 = BuildSf2(smp, inst, pre);
	SoundFont sf; std::string err;
	CHECK(sf.Load(sf2.data(), sf2.size(), err));
	CHECK(sf.presets.size() == 3 && sf.samples.size() == 3);
	CHECK(sf.Find(0, 1) && sf.Find(0, 1)->name == "Quiet");
	CHECK(sf.Find(128, 0) && sf.Find(128, 0)->name == "Drums");
	CHECK(sf.Find(0, 99) != NULL);
	{ SoundFont bad; CHECK(!bad.Load((const uint8_t *)"RIFFxxxxWAVE", 12, err)); }

	std::vector<float> o;
	// нота 69, 1 с: частота 441, амплитуда ~0.5
	CHECK(RenderMidiTrack(Track({ E(0, ME_NOTE_ON, 0, 69, 127), E(1.0, ME_NOTE_OFF, 0, 69) }), sf, R, o));
	CHECK(fabs(Freq(o, 0.1, 0.9, R) - 441) < 8);
	CHECK(fabs(PeakIn(o, 0.1, 0.9, R) - 0.5f) < 0.03f);
	CHECK(o.size() / 2 > R * 0.99 && o.size() / 2 < R * 1.05);			// почти ровно секунда (release 1 мс)
	// октава выше
	CHECK(RenderMidiTrack(Track({ E(0, ME_NOTE_ON, 0, 81, 127), E(0.5, ME_NOTE_OFF, 0, 81) }), sf, R, o));
	CHECK(fabs(Freq(o, 0.05, 0.45, R) - 882) < 15);
	// velocity 64: громкость (64/127)^2
	CHECK(RenderMidiTrack(Track({ E(0, ME_NOTE_ON, 0, 69, 64), E(0.5, ME_NOTE_OFF, 0, 69) }), sf, R, o));
	CHECK(fabs(PeakIn(o, 0.1, 0.4, R) - 0.5f * (64.f / 127) * (64.f / 127)) < 0.02f);
	// смена программы: другой пресет (в 2 раза тише)
	CHECK(RenderMidiTrack(Track({ E(0, ME_PROGRAM, 0, 1), E(0, ME_NOTE_ON, 0, 69, 127), E(0.5, ME_NOTE_OFF, 0, 69) }), sf, R, o));
	CHECK(fabs(PeakIn(o, 0.1, 0.4, R) - 0.25f) < 0.02f);
	// канал 10 - ударные (банк 128): постоянный сэмпл, не зациклен -> закончится сам через 4000 кадров
	CHECK(RenderMidiTrack(Track({ E(0, ME_NOTE_ON, 9, 60, 127), E(2.0, ME_NOTE_OFF, 9, 60) }), sf, R, o));
	CHECK(o.size() / 2 < 5000 && o.size() / 2 > 3500);
	CHECK(PeakIn(o, 0.01, 0.05, R) > 0.4f);
	// pitch bend вверх до упора: +2 полутона
	CHECK(RenderMidiTrack(Track({ E(0, ME_BEND, 0, 8191), E(0, ME_NOTE_ON, 0, 69, 127), E(0.5, ME_NOTE_OFF, 0, 69) }), sf, R, o));
	CHECK(fabs(Freq(o, 0.05, 0.45, R) - 441 * pow(2.0, 2.0 / 12)) < 10);
	// педаль: нота отпущена в 0.2, педаль снята в 0.6
	CHECK(RenderMidiTrack(Track({ E(0, ME_CC, 0, 64, 127), E(0, ME_NOTE_ON, 0, 69, 127), E(0.2, ME_NOTE_OFF, 0, 69), E(0.6, ME_CC, 0, 64, 0) }), sf, R, o));
	CHECK(PeakIn(o, 0.3, 0.55, R) > 0.4f);
	CHECK(o.size() / 2 > R * 0.59 && o.size() / 2 < R * 0.68);
	// без педали - тишина после 0.2
	CHECK(RenderMidiTrack(Track({ E(0, ME_NOTE_ON, 0, 69, 127), E(0.2, ME_NOTE_OFF, 0, 69) }), sf, R, o));
	CHECK(o.size() / 2 < R * 0.25);
	// громкость канала CC7 = 64 в (64/127)^2
	CHECK(RenderMidiTrack(Track({ E(0, ME_CC, 0, 7, 64), E(0, ME_NOTE_ON, 0, 69, 127), E(0.4, ME_NOTE_OFF, 0, 69) }), sf, R, o));
	CHECK(fabs(PeakIn(o, 0.1, 0.35, R) - 0.5f * (64.f / 127) * (64.f / 127)) < 0.02f);
	// панорама канала (CC10) игнорируется: оба канала одинаковы
	CHECK(RenderMidiTrack(Track({ E(0, ME_CC, 0, 10, 0), E(0, ME_NOTE_ON, 0, 69, 127), E(0.3, ME_NOTE_OFF, 0, 69) }), sf, R, o));
	CHECK(fabsf(o[5000 * 2] - o[5000 * 2 + 1]) < 1e-6f);
	// нет нот - false
	CHECK(!RenderMidiTrack(Track({ E(0, ME_PROGRAM, 0, 3) }), sf, R, o));

	// release 0.5 c (timecents -1200 дают 0.5 c): хвост после отпускания
	{
		std::vector<TInst> in2 = inst; in2[0].gens.push_back(G{38, -1200});
		Bytes b2 = BuildSf2(smp, in2, pre); SoundFont s2; CHECK(s2.Load(b2.data(), b2.size(), err));
		CHECK(RenderMidiTrack(Track({ E(0, ME_NOTE_ON, 0, 69, 127), E(0.5, ME_NOTE_OFF, 0, 69) }), s2, R, o));
		CHECK(o.size() / 2 > R * 0.9 && o.size() / 2 < R * 1.05);
		CHECK(PeakIn(o, 0.5, 0.52, R) > 0.3f);
		CHECK(PeakIn(o, 0.9, 0.95, R) < 0.02f);
	}

	// ---- MIDI-файл ----
	{
		Bytes m; // формат 1, 2 дорожки, division 480
		const uint8_t hd[] = { 'M','T','h','d', 0,0,0,6, 0,1, 0,2, 0x01,0xE0 };
		m.insert(m.end(), hd, hd + sizeof hd);
		Bytes t0 = { 0x00,0xFF,0x51,0x03,0x07,0xA1,0x20, 0x00,0xFF,0x2F,0x00 };		// 500000 мкс/четверть
		m.insert(m.end(), { 'M','T','r','k', 0,0,0,(uint8_t)t0.size() }); m.insert(m.end(), t0.begin(), t0.end());
		Bytes t1 = { 0x00,0xFF,0x03,0x04,'B','a','s','s',
			0x00,0xC0,0x20, 0x83,0x60,0x90,0x40,0x64,		// тик 480: нота
			0x83,0x60,0x80,0x40,0x00,					// тик 960: конец
			0x00,0xFF,0x2F,0x00 };
		m.insert(m.end(), { 'M','T','r','k', 0,0,0,(uint8_t)t1.size() }); m.insert(m.end(), t1.begin(), t1.end());
		MidiSong song; CHECK(LoadMidi(m.data(), m.size(), song, err));
		CHECK(song.tracks.size() == 1 && song.tracks[0].name == "Bass");
		CHECK(fabs(song.bpm - 120.0) < 1e-9 && !song.tempoVaries);
		CHECK(song.tracks[0].notes == 1 && song.tracks[0].program == 0x20 && song.tracks[0].channel == 0);
		bool ok = false; for (auto &e : song.tracks[0].ev) if (e.type == ME_NOTE_ON) ok = fabs(e.t - 0.5) < 1e-9;
		CHECK(ok);
		ok = false; for (auto &e : song.tracks[0].ev) if (e.type == ME_NOTE_OFF) ok = fabs(e.t - 1.0) < 1e-9;
		CHECK(ok);
	}
	{
		// формат 0 с двумя каналами -> две дорожки; темп 120 BPM; running status
		Bytes m;
		const uint8_t hd[] = { 'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0x01,0xE0 };
		m.insert(m.end(), hd, hd + sizeof hd);
		Bytes t = { 0x00,0x90,0x3C,0x50, 0x00,0x3E,0x50,			// running status: вторая нота того же канала
			0x00,0x91,0x40,0x50,
			0x81,0x70,0x80,0x3C,0x00, 0x00,0x3E,0x00, 0x00,0x81,0x40,0x00,
			0x00,0xFF,0x2F,0x00 };
		m.insert(m.end(), { 'M','T','r','k', 0,0,0,(uint8_t)t.size() }); m.insert(m.end(), t.begin(), t.end());
		MidiSong song; CHECK(LoadMidi(m.data(), m.size(), song, err));
		CHECK(song.tracks.size() == 2);
		CHECK(song.tracks[0].channel == 0 && song.tracks[1].channel == 1);
		CHECK(fabs(song.bpm - 120.0) < 1e-9);			// темп не задан - 120
		CHECK(song.tracks[0].notes == 2 && song.tracks[1].notes == 1);
	}
	{
		// темп: 250000 мкс -> 240 BPM; затем 500000 на тике 480 - темп меняется
		Bytes m;
		const uint8_t hd[] = { 'M','T','h','d', 0,0,0,6, 0,1, 0,2, 0x01,0xE0 };
		m.insert(m.end(), hd, hd + sizeof hd);
		Bytes t0 = { 0x00,0xFF,0x51,0x03,0x03,0xD0,0x90, 0x83,0x60,0xFF,0x51,0x03,0x07,0xA1,0x20, 0x00,0xFF,0x2F,0x00 };
		m.insert(m.end(), { 'M','T','r','k', 0,0,0,(uint8_t)t0.size() }); m.insert(m.end(), t0.begin(), t0.end());
		Bytes t1 = { 0x00,0x90,0x40,0x64, 0x87,0x40,0x80,0x40,0x00, 0x00,0xFF,0x2F,0x00 };		// конец на тике 960
		m.insert(m.end(), { 'M','T','r','k', 0,0,0,(uint8_t)t1.size() }); m.insert(m.end(), t1.begin(), t1.end());
		MidiSong song; CHECK(LoadMidi(m.data(), m.size(), song, err));
		CHECK(fabs(song.bpm - 240.0) < 1e-9 && song.tempoVaries);
		// тики 0..480 по 0.25 c, 480..960 по 0.5 c: конец на 0.25 + 0.5 = 0.75 c
		double tOff = -1; for (auto &e : song.tracks[0].ev) if (e.type == ME_NOTE_OFF) tOff = e.t;
		CHECK(fabs(tOff - 0.75) < 1e-9);
	}
	{ MidiSong s; CHECK(!LoadMidi((const uint8_t *)"garbage garbage!", 16, s, err)); }

	printf(fails ? "FAILED %d\n" : "all ok\n", fails);
	return fails != 0;
}
