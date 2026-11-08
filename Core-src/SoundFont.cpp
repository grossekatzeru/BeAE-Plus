#include "SoundFont.h"
#include <string.h>
#include <stdio.h>
#include <algorithm>

namespace {

uint16_t U16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t U32(const uint8_t *p) { return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)); }

struct Gen { int oper; int amount; int lo, hi; };
struct Bag { int gen; };
struct ZoneRaw { std::vector<Gen> gens; };

std::string Name20(const uint8_t *p) { char b[21]; memcpy(b, p, 20); b[20] = 0; return std::string(b); }

bool IsRange(int op) { return op == SFG_KEYRANGE || op == SFG_VELRANGE; }
// генераторы, которые на уровне пресета не действуют
bool InstrumentOnly(int op)
{
	switch (op) {
	case 0: case 1: case 2: case 3: case 4: case 12: case 45: case 50: case 54: case 57: case 58: case 46: case 47:
		return true;
	}
	return false;
}

void Defaults(int *g)
{
	for (int i = 0; i < SFG_COUNT; i++) g[i] = 0;
	g[8] = 13500;
	g[SFG_DELAY] = g[SFG_ATTACK] = g[SFG_HOLD] = g[SFG_DECAY] = g[SFG_RELEASE] = -12000;
	g[SFG_SCALE_TUNING] = 100;
	g[SFG_ROOT_KEY] = -1;
}

// разбить зоны блока bags[b0..b1) на глобальную (если есть) и обычные
void SplitZones(const std::vector<ZoneRaw> &zr, int z0, int z1, int linkOp, ZoneRaw &global, bool &hasGlobal, std::vector<const ZoneRaw *> &zones)
{
	hasGlobal = false;
	for (int z = z0; z < z1; z++) {
		bool hasLink = false;
		for (size_t k = 0; k < zr[z].gens.size(); k++) if (zr[z].gens[k].oper == linkOp) hasLink = true;
		if (!hasLink && z == z0) { global = zr[z]; hasGlobal = true; }
		else if (hasLink) zones.push_back(&zr[z]);
	}
}

void ApplyGens(const ZoneRaw &z, int *g, int &kl, int &kh, int &vl, int &vh, bool additive)
{
	for (size_t k = 0; k < z.gens.size(); k++) {
		const Gen &e = z.gens[k];
		if (e.oper == SFG_KEYRANGE) { kl = std::max(kl, e.lo); kh = std::min(kh, e.hi); }
		else if (e.oper == SFG_VELRANGE) { vl = std::max(vl, e.lo); vh = std::min(vh, e.hi); }
		else if (e.oper >= 0 && e.oper < SFG_COUNT && e.oper != SFG_INSTRUMENT && e.oper != SFG_SAMPLE_ID) {
			if (additive) { if (!InstrumentOnly(e.oper)) g[e.oper] += e.amount; }
			else g[e.oper] = e.amount;
		}
	}
	(void)IsRange;
}

} // namespace

bool SoundFont::LoadFile(const char *path, std::string &err)
{
	FILE *f = fopen(path, "rb");
	if (!f) { err = "cannot open file"; return false; }
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (sz < 12) { fclose(f); err = "file too small"; return false; }
	std::vector<uint8_t> buf((size_t)sz);
	size_t got = fread(buf.data(), 1, buf.size(), f);
	fclose(f);
	if (got != buf.size()) { err = "read error"; return false; }
	return Load(buf.data(), buf.size(), err);
}

bool SoundFont::Load(const uint8_t *d, size_t size, std::string &err)
{
	samples.clear(); pcm.clear(); presets.clear(); name.clear();
	if (size < 12 || memcmp(d, "RIFF", 4) != 0 || memcmp(d + 8, "sfbk", 4) != 0) { err = "not an SF2 file"; return false; }

	const uint8_t *phdr = NULL, *pbag = NULL, *pgen = NULL, *inst = NULL, *ibag = NULL, *igen = NULL, *shdr = NULL;
	size_t nphdr = 0, npbag = 0, npgen = 0, ninst = 0, nibag = 0, nigen = 0, nshdr = 0;
	const uint8_t *smpl = NULL; size_t smplBytes = 0;

	size_t i = 12;
	while (i + 8 <= size) {
		uint32_t len = U32(d + i + 4);
		const uint8_t *body = d + i + 8;
		if (i + 8 + (size_t)len > size) len = (uint32_t)(size - i - 8);
		if (memcmp(d + i, "LIST", 4) == 0 && len >= 4) {
			const char *type = (const char *)body;
			size_t j = 4;
			while (j + 8 <= len) {
				uint32_t cl = U32(body + j + 4);
				const uint8_t *cb = body + j + 8;
				if (j + 8 + (size_t)cl > len) cl = (uint32_t)(len - j - 8);
				const char *id = (const char *)(body + j);
				if (!memcmp(type, "INFO", 4) && !memcmp(id, "INAM", 4)) name = std::string((const char *)cb, strnlen((const char *)cb, cl));
				else if (!memcmp(type, "sdta", 4) && !memcmp(id, "smpl", 4)) { smpl = cb; smplBytes = cl; }
				else if (!memcmp(type, "pdta", 4)) {
					if (!memcmp(id, "phdr", 4)) { phdr = cb; nphdr = cl / 38; }
					else if (!memcmp(id, "pbag", 4)) { pbag = cb; npbag = cl / 4; }
					else if (!memcmp(id, "pgen", 4)) { pgen = cb; npgen = cl / 4; }
					else if (!memcmp(id, "inst", 4)) { inst = cb; ninst = cl / 22; }
					else if (!memcmp(id, "ibag", 4)) { ibag = cb; nibag = cl / 4; }
					else if (!memcmp(id, "igen", 4)) { igen = cb; nigen = cl / 4; }
					else if (!memcmp(id, "shdr", 4)) { shdr = cb; nshdr = cl / 46; }
				}
				j += 8 + cl + (cl & 1);
			}
		}
		i += 8 + (size_t)len + (len & 1);
	}
	if (!phdr || !pbag || !pgen || !inst || !ibag || !igen || !shdr || !smpl || nphdr < 2 || ninst < 2 || nshdr < 2) {
		err = "incomplete SF2 file"; return false;
	}

	pcm.resize(smplBytes / 2);
	for (size_t k = 0; k < pcm.size(); k++) pcm[k] = (int16_t)U16(smpl + k * 2);

	for (size_t k = 0; k + 1 < nshdr; k++) {		// последняя запись - EOS
		const uint8_t *p = shdr + k * 46;
		SfSample s;
		s.name = Name20(p);
		s.start = U32(p + 20); s.end = U32(p + 24); s.loopStart = U32(p + 28); s.loopEnd = U32(p + 32);
		s.rate = U32(p + 36);
		s.key = p[40] == 255 ? 60 : p[40];
		s.correction = (int8_t)p[41];
		s.type = U16(p + 44);
		if (s.end > pcm.size()) s.end = (uint32_t)pcm.size();
		samples.push_back(s);
	}

	auto readGens = [](const uint8_t *g, size_t n, size_t from, size_t to, ZoneRaw &out) {
		for (size_t k = from; k < to && k < n; k++) {
			Gen e; e.oper = U16(g + k * 4);
			e.amount = (int16_t)U16(g + k * 4 + 2);
			e.lo = g[k * 4 + 2]; e.hi = g[k * 4 + 3];
			out.gens.push_back(e);
		}
	};
	auto readZones = [&](const uint8_t *bag, size_t nbag, const uint8_t *gen, size_t ngen, std::vector<ZoneRaw> &zr) {
		for (size_t k = 0; k + 1 < nbag; k++) {
			ZoneRaw z;
			readGens(gen, ngen, U16(bag + k * 4), U16(bag + (k + 1) * 4), z);
			zr.push_back(z);
		}
	};
	std::vector<ZoneRaw> pzr, izr;
	readZones(pbag, npbag, pgen, npgen, pzr);
	readZones(ibag, nibag, igen, nigen, izr);

	for (size_t pi = 0; pi + 1 < nphdr; pi++) {
		const uint8_t *p = phdr + pi * 38;
		SfPreset pr;
		pr.name = Name20(p);
		pr.program = U16(p + 20);
		pr.bank = U16(p + 22);
		int pb0 = U16(p + 24), pb1 = U16(p + 38 + 24);
		if (pb1 > (int)pzr.size()) pb1 = (int)pzr.size();
		ZoneRaw pglob; bool hasPG;
		std::vector<const ZoneRaw *> pzones;
		SplitZones(pzr, pb0, pb1, SFG_INSTRUMENT, pglob, hasPG, pzones);
		for (size_t zi = 0; zi < pzones.size(); zi++) {
			int instIdx = -1;
			for (size_t k = 0; k < pzones[zi]->gens.size(); k++)
				if (pzones[zi]->gens[k].oper == SFG_INSTRUMENT) instIdx = pzones[zi]->gens[k].amount & 0xffff;
			if (instIdx < 0 || (size_t)instIdx + 1 >= ninst) continue;
			const uint8_t *ip = inst + instIdx * 22;
			int ib0 = U16(ip + 20), ib1 = U16(ip + 22 + 20);
			if (ib1 > (int)izr.size()) ib1 = (int)izr.size();
			ZoneRaw iglob; bool hasIG;
			std::vector<const ZoneRaw *> izones;
			SplitZones(izr, ib0, ib1, SFG_SAMPLE_ID, iglob, hasIG, izones);
			for (size_t ii = 0; ii < izones.size(); ii++) {
				SfZone z;
				Defaults(z.gen);
				z.keyLo = 0; z.keyHi = 127; z.velLo = 0; z.velHi = 127;
				if (hasIG) ApplyGens(iglob, z.gen, z.keyLo, z.keyHi, z.velLo, z.velHi, false);
				ApplyGens(*izones[ii], z.gen, z.keyLo, z.keyHi, z.velLo, z.velHi, false);
				z.sample = -1;
				for (size_t k = 0; k < izones[ii]->gens.size(); k++)
					if (izones[ii]->gens[k].oper == SFG_SAMPLE_ID) z.sample = izones[ii]->gens[k].amount & 0xffff;
				if (z.sample < 0 || z.sample >= (int)samples.size()) continue;
				if (hasPG) ApplyGens(pglob, z.gen, z.keyLo, z.keyHi, z.velLo, z.velHi, true);
				ApplyGens(*pzones[zi], z.gen, z.keyLo, z.keyHi, z.velLo, z.velHi, true);
				if (z.keyLo > z.keyHi || z.velLo > z.velHi) continue;
				pr.zones.push_back(z);
			}
		}
		presets.push_back(pr);
	}
	if (presets.empty()) { err = "no presets"; return false; }
	return true;
}

const SfPreset *SoundFont::Find(int bank, int program) const
{
	const SfPreset *first = presets.empty() ? NULL : &presets[0];
	const SfPreset *p0 = NULL, *b0 = NULL;
	for (size_t k = 0; k < presets.size(); k++) {
		const SfPreset &p = presets[k];
		if (p.bank == bank && p.program == program) return &p;
		if (p.bank == 0 && p.program == program && !p0) p0 = &p;
		if (p.bank == bank && p.program == 0 && !b0) b0 = &p;
	}
	if (bank == 128) return b0 ? b0 : first;		// ударные: другой набор, но не мелодический
	if (p0) return p0;
	if (b0) return b0;
	return first;
}
