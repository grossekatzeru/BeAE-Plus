#include "ProjectIO.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

// ------------------------------------------------------------------ JSON

const JValue *JValue::Get(const char *key) const
{
	if (type != Obj) return NULL;
	for (size_t i = 0; i < o.size(); i++)
		if (o[i].first == key) return &o[i].second;
	return NULL;
}

double JValue::Number(const char *key, double def) const
{
	const JValue *v = Get(key);
	return (v && v->type == Num) ? v->n : def;
}

bool JValue::Boolean(const char *key, bool def) const
{
	const JValue *v = Get(key);
	return (v && v->type == Bool) ? v->b : def;
}

std::string JValue::String(const char *key, const std::string &def) const
{
	const JValue *v = Get(key);
	return (v && v->type == Str) ? v->s : def;
}

namespace {

struct Parser {
	const std::string &t;
	size_t p;
	std::string err;
	Parser(const std::string &text) : t(text), p(0) {}

	void Ws() { while (p < t.size() && (t[p]==' '||t[p]=='\t'||t[p]=='\n'||t[p]=='\r')) p++; }
	bool Fail(const char *m) { if (err.empty()) { char b[64]; snprintf(b, sizeof(b), " (at %zu)", p); err = std::string(m) + b; } return false; }

	static void AppendUtf8(std::string &s, unsigned cp)
	{
		if (cp < 0x80) s += (char)cp;
		else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
		else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
		else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
	}

	bool Hex4(unsigned &v)
	{
		if (p + 4 > t.size()) return Fail("bad \\u escape");
		v = 0;
		for (int i = 0; i < 4; i++) {
			char c = t[p++];
			v <<= 4;
			if (c >= '0' && c <= '9') v |= c - '0';
			else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
			else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
			else return Fail("bad \\u escape");
		}
		return true;
	}

	bool Str(std::string &out)
	{
		if (p >= t.size() || t[p] != '"') return Fail("string expected");
		p++;
		out.clear();
		while (p < t.size()) {
			char c = t[p++];
			if (c == '"') return true;
			if (c == '\\') {
				if (p >= t.size()) break;
				char e = t[p++];
				switch (e) {
				case '"': out += '"'; break;
				case '\\': out += '\\'; break;
				case '/': out += '/'; break;
				case 'b': out += '\b'; break;
				case 'f': out += '\f'; break;
				case 'n': out += '\n'; break;
				case 'r': out += '\r'; break;
				case 't': out += '\t'; break;
				case 'u': {
					unsigned v;
					if (!Hex4(v)) return false;
					if (v >= 0xD800 && v < 0xDC00 && p + 1 < t.size() && t[p] == '\\' && t[p+1] == 'u') {
						p += 2;
						unsigned lo;
						if (!Hex4(lo)) return false;
						v = 0x10000 + ((v - 0xD800) << 10) + (lo - 0xDC00);
					}
					AppendUtf8(out, v);
					break;
				}
				default: return Fail("bad escape");
				}
			} else out += c;
		}
		return Fail("unterminated string");
	}

	bool Value(JValue &v, int depth)
	{
		if (depth > 64) return Fail("too deep");
		Ws();
		if (p >= t.size()) return Fail("unexpected end");
		char c = t[p];
		if (c == '{') {
			p++;
			v.type = JValue::Obj;
			Ws();
			if (p < t.size() && t[p] == '}') { p++; return true; }
			for (;;) {
				Ws();
				std::string key;
				if (!Str(key)) return false;
				Ws();
				if (p >= t.size() || t[p] != ':') return Fail("':' expected");
				p++;
				JValue item;
				if (!Value(item, depth + 1)) return false;
				v.o.push_back(std::make_pair(key, item));
				Ws();
				if (p < t.size() && t[p] == ',') { p++; continue; }
				if (p < t.size() && t[p] == '}') { p++; return true; }
				return Fail("',' or '}' expected");
			}
		}
		if (c == '[') {
			p++;
			v.type = JValue::Arr;
			Ws();
			if (p < t.size() && t[p] == ']') { p++; return true; }
			for (;;) {
				JValue item;
				if (!Value(item, depth + 1)) return false;
				v.a.push_back(item);
				Ws();
				if (p < t.size() && t[p] == ',') { p++; continue; }
				if (p < t.size() && t[p] == ']') { p++; return true; }
				return Fail("',' or ']' expected");
			}
		}
		if (c == '"') { v.type = JValue::Str; return Str(v.s); }
		if (t.compare(p, 4, "true") == 0) { p += 4; v.type = JValue::Bool; v.b = true; return true; }
		if (t.compare(p, 5, "false") == 0) { p += 5; v.type = JValue::Bool; v.b = false; return true; }
		if (t.compare(p, 4, "null") == 0) { p += 4; v.type = JValue::Null; return true; }
		// число
		const char *start = t.c_str() + p;
		char *end = NULL;
		double d = strtod(start, &end);
		if (end == start) return Fail("value expected");
		p += (size_t)(end - start);
		v.type = JValue::Num;
		v.n = d;
		return true;
	}
};

}	// namespace

bool JsonParse(const std::string &text, JValue &out, std::string &err)
{
	Parser ps(text);
	out = JValue();
	// пропустить BOM
	if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
		ps.p = 3;
	if (!ps.Value(out, 0)) { err = ps.err; return false; }
	ps.Ws();
	if (ps.p != text.size()) { err = "extra data after the document"; return false; }
	return true;
}

std::string JsonEscape(const std::string &s)
{
	std::string r;
	for (size_t i = 0; i < s.size(); i++) {
		unsigned char c = (unsigned char)s[i];
		switch (c) {
		case '"': r += "\\\""; break;
		case '\\': r += "\\\\"; break;
		case '\n': r += "\\n"; break;
		case '\r': r += "\\r"; break;
		case '\t': r += "\\t"; break;
		default:
			if (c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); r += b; }
			else r += (char)c;
		}
	}
	return r;
}

// ------------------------------------------------------------------ WAV

static void Put16(FILE *f, uint16_t v) { unsigned char b[2] = { (unsigned char)(v & 0xFF), (unsigned char)(v >> 8) }; fwrite(b, 1, 2, f); }
static void Put32(FILE *f, uint32_t v) { unsigned char b[4] = { (unsigned char)(v & 0xFF), (unsigned char)((v >> 8) & 0xFF), (unsigned char)((v >> 16) & 0xFF), (unsigned char)(v >> 24) }; fwrite(b, 1, 4, f); }

bool WavWriteFloat(const char *path, const float *data, long long frames, int channels, float rate)
{
	if (channels < 1 || channels > 2 || frames < 0 || rate <= 0) return false;
	FILE *f = fopen(path, "wb");
	if (!f) return false;
	const uint64_t dataBytes = (uint64_t)frames * channels * 4;
	if (dataBytes > 0xFFFFFF00ULL) { fclose(f); return false; }
	fwrite("RIFF", 1, 4, f);
	Put32(f, (uint32_t)(4 + 8 + 16 + 8 + 4 + 8 + dataBytes));
	fwrite("WAVE", 1, 4, f);
	fwrite("fmt ", 1, 4, f);
	Put32(f, 16);
	Put16(f, 3);							// IEEE float
	Put16(f, (uint16_t)channels);
	Put32(f, (uint32_t)(rate + 0.5f));
	Put32(f, (uint32_t)((uint64_t)(rate + 0.5f) * channels * 4));
	Put16(f, (uint16_t)(channels * 4));
	Put16(f, 32);
	fwrite("fact", 1, 4, f);
	Put32(f, 4);
	Put32(f, (uint32_t)frames);
	fwrite("data", 1, 4, f);
	Put32(f, (uint32_t)dataBytes);
	bool ok = true;
	// порядок байт: записываем как есть на little-endian (x86), иначе по одному
	const long long total = frames * channels;
	for (long long i = 0; i < total && ok; i += 16384) {
		long long n = (total - i < 16384) ? total - i : 16384;
		uint32_t tmp[16384];
		for (long long k = 0; k < n; k++) { memcpy(&tmp[k], &data[i + k], 4); }
		unsigned char b[16384 * 4];
		for (long long k = 0; k < n; k++) {
			b[4*k] = tmp[k] & 0xFF; b[4*k+1] = (tmp[k] >> 8) & 0xFF; b[4*k+2] = (tmp[k] >> 16) & 0xFF; b[4*k+3] = tmp[k] >> 24;
		}
		ok = fwrite(b, 1, (size_t)n * 4, f) == (size_t)n * 4;
	}
	if (fclose(f) != 0) ok = false;
	return ok;
}

static uint32_t Get32(const unsigned char *b) { return b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24); }
static uint16_t Get16(const unsigned char *b) { return (uint16_t)(b[0] | (b[1] << 8)); }

bool WavRead(const char *path, std::vector<float> &out, long long &frames, int &channels,
			 float &rate, std::string &err)
{
	FILE *f = fopen(path, "rb");
	if (!f) { err = "cannot open the file"; return false; }
	unsigned char h[12];
	if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) != 0 || memcmp(h + 8, "WAVE", 4) != 0) {
		fclose(f); err = "not a WAV file"; return false;
	}
	int tag = 0, bits = 0;
	channels = 0; rate = 0; frames = 0;
	bool haveFmt = false;
	for (;;) {
		unsigned char ch[8];
		if (fread(ch, 1, 8, f) != 8) { fclose(f); err = "no data chunk"; return false; }
		uint32_t size = Get32(ch + 4);
		if (memcmp(ch, "fmt ", 4) == 0) {
			if (size < 16 || size > 4096) { fclose(f); err = "bad fmt chunk"; return false; }
			std::vector<unsigned char> b(size);
			if (fread(&b[0], 1, size, f) != size) { fclose(f); err = "bad fmt chunk"; return false; }
			tag = Get16(&b[0]);
			channels = Get16(&b[2]);
			rate = (float)Get32(&b[4]);
			bits = Get16(&b[14]);
			if (tag == 0xFFFE && size >= 26) tag = Get16(&b[24]);	// extensible: подформат
			haveFmt = true;
			if (size & 1) fseek(f, 1, SEEK_CUR);
		} else if (memcmp(ch, "data", 4) == 0) {
			if (!haveFmt) { fclose(f); err = "data before fmt"; return false; }
			if (channels < 1 || channels > 2 || rate <= 0) { fclose(f); err = "unsupported channel count"; return false; }
			int bytes = bits / 8;
			bool okFmt = (tag == 3 && (bits == 32 || bits == 64)) || (tag == 1 && (bits == 16 || bits == 24 || bits == 32 || bits == 8));
			if (!okFmt) { fclose(f); err = "unsupported sample format"; return false; }
			// если размер в заголовке не указан или больше файла - читаем до конца
			long cur = ftell(f);
			fseek(f, 0, SEEK_END);
			long fileEnd = ftell(f);
			fseek(f, cur, SEEK_SET);
			uint64_t avail = (uint64_t)(fileEnd - cur);
			uint64_t n = size;
			if (n == 0 || n > avail) n = avail;
			frames = (long long)(n / ((uint64_t)bytes * channels));
			out.assign((size_t)frames * channels, 0.0f);
			std::vector<unsigned char> buf((size_t)65536 * bytes);
			long long done = 0, total = frames * channels;
			while (done < total) {
				long long chunk = total - done < 65536 ? total - done : 65536;
				if (fread(&buf[0], 1, (size_t)chunk * bytes, f) != (size_t)chunk * bytes) { fclose(f); err = "file is truncated"; return false; }
				for (long long k = 0; k < chunk; k++) {
					const unsigned char *s = &buf[(size_t)k * bytes];
					float v = 0;
					if (tag == 3 && bits == 32) { uint32_t u = Get32(s); memcpy(&v, &u, 4); }
					else if (tag == 3 && bits == 64) { uint64_t u = (uint64_t)Get32(s) | ((uint64_t)Get32(s + 4) << 32); double d; memcpy(&d, &u, 8); v = (float)d; }
					else if (bits == 16) v = (int16_t)Get16(s) / 32768.0f;
					else if (bits == 24) { int32_t x = (s[0] << 8) | (s[1] << 16) | ((int32_t)s[2] << 24); v = x / 2147483648.0f; }
					else if (bits == 32) v = (int32_t)Get32(s) / 2147483648.0f;
					else v = (s[0] - 128) / 128.0f;
					out[(size_t)(done + k)] = v;
				}
				done += chunk;
			}
			fclose(f);
			return true;
		} else {
			if (fseek(f, (long)size + (size & 1), SEEK_CUR) != 0) { fclose(f); err = "no data chunk"; return false; }
		}
	}
}
