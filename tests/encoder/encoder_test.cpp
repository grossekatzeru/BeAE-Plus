#include "ExtEncoder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string>
#include <vector>
using namespace ExtEncoder;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static bool Has(const std::vector<std::string> &a, const char *s)
{
	for (size_t i = 0; i < a.size(); i++) if (a[i] == s) return true;
	return false;
}
static int IndexOf(const std::vector<std::string> &a, const char *s)
{
	for (size_t i = 0; i < a.size(); i++) if (a[i] == s) return (int)i;
	return -1;
}
static std::string ReadAll(const std::string &p)
{
	FILE *f = fopen(p.c_str(), "rb"); std::string s; if (!f) return s;
	char b[4096]; size_t n; while ((n = fread(b, 1, sizeof b, f)) > 0) s.append(b, n); fclose(f); return s;
}
static unsigned U32(const std::string &s, size_t o) { return (unsigned char)s[o] | ((unsigned char)s[o+1] << 8) | ((unsigned char)s[o+2] << 16) | ((unsigned)(unsigned char)s[o+3] << 24); }
static unsigned U16(const std::string &s, size_t o) { return (unsigned char)s[o] | ((unsigned char)s[o+1] << 8); }

static void WriteScript(const std::string &path, const std::string &body)
{
	FILE *f = fopen(path.c_str(), "w"); fputs("#!/bin/sh\n", f); fputs(body.c_str(), f); fclose(f); chmod(path.c_str(), 0755);
}

int main()
{
	// 1) настройки: умолчания, границы, туда и обратно
	{
		Settings d = Defaults(MP3);
		CHECK(d.mode == VBR && d.quality == 2, "lame default must be VBR V2");
		Settings s = d; s.mode = ABR; s.bitrate = 9999; s.quality = 77; s.tags.artist = "A;B=C%"; s.tags.comment = "привет";
		Clamp(MP3, s);
		CHECK(s.bitrate == 320 && s.quality == 9, "clamp: %d %d", s.bitrate, s.quality);
		Settings r = FromString(MP3, ToString(MP3, s));
		CHECK(r.mode == ABR && r.bitrate == 320 && r.quality == 9, "roundtrip numbers");
		CHECK(r.tags.artist == "A;B=C%" && r.tags.comment == "привет", "roundtrip tags '%s'", r.tags.artist.c_str());
		Settings bad = FromString(OGG, "mode=7;q=-5;br=1;junk;=;q");
		CHECK(bad.mode <= CBR && bad.quality == 0 && bad.bitrate == 32, "garbage clamp %d %d %d", bad.mode, bad.quality, bad.bitrate);
		Settings o = FromString(OPUS, "");
		CHECK(o.bitrate == 128, "opus default");
		CHECK(ToString(FLAC, Defaults(FLAC)).find("lv=5") != std::string::npos, "flac level in string");
		CHECK(ToString(FLAC, Defaults(FLAC)).find("br=") == std::string::npos, "flac has no bitrate");
	}
	// 2) командные строки
	{
		Settings s = Defaults(MP3);
		std::vector<std::string> a = BuildArgs(MP3, "/bin/lame", s, "Песня 1", "in.wav", "out.mp3");
		CHECK(a[0] == "/bin/lame" && a[a.size()-2] == "in.wav" && a.back() == "out.mp3", "mp3 order");
		CHECK(IndexOf(a, "-V") >= 0 && a[IndexOf(a, "-V") + 1] == "2", "mp3 -V2");
		CHECK(Has(a, "--tt") && a[IndexOf(a, "--tt") + 1] == "Песня 1", "mp3 title fallback");
		CHECK(!Has(a, "--ta"), "no empty artist");
		s.mode = CBR; s.bitrate = 256; s.stereo = MONO; s.tags.title = "T"; s.tags.artist = "Art";
		a = BuildArgs(MP3, "lame", s, "x", "i", "o");
		CHECK(a[IndexOf(a, "-b") + 1] == "256" && a[IndexOf(a, "-m") + 1] == "m" && a[IndexOf(a, "--tt") + 1] == "T" && Has(a, "--ta"), "mp3 cbr mono tags");
		s.mode = ABR; a = BuildArgs(MP3, "lame", s, "x", "i", "o");
		CHECK(a[IndexOf(a, "--abr") + 1] == "256", "mp3 abr");

		s = Defaults(OGG); a = BuildArgs(OGG, "oggenc", s, "N", "i.wav", "o.ogg");
		CHECK(a[IndexOf(a, "-q") + 1] == "5" && a[IndexOf(a, "-o") + 1] == "o.ogg" && a.back() == "i.wav", "ogg quality");
		s.mode = CBR; a = BuildArgs(OGG, "oggenc", s, "N", "i", "o");
		CHECK(Has(a, "-b") && !Has(a, "-q"), "ogg bitrate mode");

		s = Defaults(FLAC); s.level = 8; a = BuildArgs(FLAC, "flac", s, "N", "i.wav", "o.flac");
		CHECK(Has(a, "-8") && Has(a, "--force") && a[IndexOf(a, "-o") + 1] == "o.flac", "flac level");
		CHECK(a[IndexOf(a, "-T") + 1] == "TITLE=N", "flac title tag");

		s = Defaults(OPUS); a = BuildArgs(OPUS, "opusenc", s, "N", "i.wav", "o.opus");
		CHECK(a[IndexOf(a, "--bitrate") + 1] == "128" && a[a.size()-2] == "i.wav" && a.back() == "o.opus", "opus args");
		CHECK(!Has(a, "--hard-cbr"), "opus vbr");
		s.mode = CBR; a = BuildArgs(OPUS, "opusenc", s, "N", "i", "o");
		CHECK(Has(a, "--hard-cbr"), "opus cbr");
	}
	// 3) временный WAV: заголовок, разрядность, значения, дизеринг, отмена
	{
		std::vector<float> d(2 * 1000);
		for (int i = 0; i < 1000; i++) { d[i*2] = 0.5f * sinf(i * 0.1f); d[i*2+1] = i < 500 ? 0.0f : 2.0f; }	// 2.0 - перегруз
		CHECK(WriteWavPcm("build/t24.wav", &d[0], 1000, 2, 44100, 24), "write 24");
		std::string w = ReadAll("build/t24.wav");
		CHECK(w.size() == 44 + 1000 * 2 * 3, "size 24: %zu", w.size());
		CHECK(w.substr(0, 4) == "RIFF" && w.substr(8, 4) == "WAVE" && U16(w, 20) == 1 && U16(w, 22) == 2 && U32(w, 24) == 44100 && U16(w, 34) == 24, "header 24");
		CHECK(U32(w, 40) == 6000 && U32(w, 4) == 36 + 6000 && U32(w, 28) == 44100 * 6 && U16(w, 32) == 6, "sizes 24");
		// правый канал, отсчёт 600: перегруз ограничен до +8388607
		const size_t o = 44 + (600 * 2 + 1) * 3;
		const int v = (unsigned char)w[o] | ((unsigned char)w[o+1] << 8) | ((signed char)w[o+2] << 16);
		CHECK(v == 8388607, "clip 24: %d", v);
		CHECK(WriteWavPcm("build/t16.wav", &d[0], 1000, 2, 48000, 16), "write 16");
		w = ReadAll("build/t16.wav");
		CHECK(w.size() == 44 + 1000 * 2 * 2 && U16(w, 34) == 16 && U32(w, 24) == 48000, "header 16");
		const size_t o2 = 44 + (600 * 2 + 1) * 2;
		const int v2 = (short)U16(w, o2);
		CHECK(v2 == 32767, "clip 16: %d", v2);
		// тишина с дизерингом: шум в пределах ±2 LSB, не всегда ноль
		std::vector<float> z(2 * 5000, 0.0f);
		WriteWavPcm("build/z16.wav", &z[0], 5000, 2, 44100, 16);
		w = ReadAll("build/z16.wav");
		int nz = 0, mx = 0;
		for (int i = 0; i < 10000; i++) { int s = (short)U16(w, 44 + i * 2); if (s) nz++; if (abs(s) > mx) mx = abs(s); }
		CHECK(mx <= 2 && nz > 1000, "dither: max %d nonzero %d", mx, nz);
		CHECK(!WriteWavPcm("build/bad.wav", &d[0], 10, 2, 44100, 12), "bad bits");
		CHECK(!WriteWavPcm("/nonexistent_dir/x.wav", &d[0], 10, 2, 44100, 16), "bad path");
	}
	// 4) поиск и запуск: поддельные программы
	{
		const std::string dir = "build/fakebin";
		mkdir(dir.c_str(), 0755);
		// «lame»: записывает аргументы и копирует вход в выход
		WriteScript(dir + "/lame", "for a in \"$@\"; do echo \"$a\"; done > build/args_lame.txt\neval out=\\${$#}\neval in=\\${$(($#-1))}\ncp \"$in\" \"$out\"\n");
		WriteScript(dir + "/flac", "echo 'ERROR: bad input' >&2\nexit 3\n");
		std::vector<std::string> extra(1, dir);
		CHECK(FindProgram("lame", extra) == dir + "/lame", "find lame");
		CHECK(FindProgram("definitely_not_here_xyz", extra).empty(), "not found");
		std::vector<Id> av = Available(extra);
		bool hasMp3 = false, hasFlac = false;
		for (size_t i = 0; i < av.size(); i++) { if (av[i] == MP3) hasMp3 = true; if (av[i] == FLAC) hasFlac = true; }
		CHECK(hasMp3 && hasFlac, "available");

		std::vector<float> d(2 * 4410, 0.25f);
		std::string err;
		mkdir("build/out dir", 0755);							// пробел в имени папки
		Settings s = Defaults(MP3);
		bool ok = Encode(MP3, s, "build", "Тест", &d[0], 4410, 2, 44100, "build/out dir/файл 1.mp3", err, 0, 0, 0, extra);
		CHECK(ok, "encode ok: %s", err.c_str());
		CHECK(ReadAll("build/out dir/файл 1.mp3").size() == 44 + 4410 * 2 * 2, "fake output is the temp wav (16 bit)");
		std::string args = ReadAll("build/args_lame.txt");
		CHECK(args.find("Тест") != std::string::npos && args.find("файл 1.mp3") != std::string::npos, "args with utf-8 and spaces");
		// временный файл удалён
		int leftovers = 0;
		FILE *p = popen("ls build | grep -c beae_enc_ || true", "r"); if (p) { char b[16] = ""; if (fgets(b, sizeof b, p)) leftovers = atoi(b); pclose(p); }
		CHECK(leftovers == 0, "temp wav left behind: %d", leftovers);

		ok = Encode(FLAC, Defaults(FLAC), "build", "x", &d[0], 4410, 2, 44100, "build/out.flac", err, 0, 0, 0, extra);
		CHECK(!ok && err.find("bad input") != std::string::npos && err.find("flac failed") != std::string::npos, "failure text: %s", err.c_str());
		ok = Encode(OGG, Defaults(OGG), "build", "x", &d[0], 4410, 2, 44100, "build/out.ogg", err, 0, 0, 0, extra);
		// oggenc может быть установлен в системе; проверим только согласованность
		if (FindProgram("oggenc", extra).empty()) CHECK(!ok && err.find("not installed") != std::string::npos, "not installed text: %s", err.c_str());
		// отмена
		WriteScript(dir + "/opusenc", "sleep 5\n");
		struct C { static bool Yes(void *) { return true; } };
		ok = Encode(OPUS, Defaults(OPUS), "build", "x", &d[0], 4410, 2, 44100, "build/out.opus", err, 0, C::Yes, 0, extra);
		CHECK(!ok && err == "Cancelled", "cancel: %s", err.c_str());
		CHECK(access("build/out.opus", F_OK) != 0, "cancelled output removed");
		// отмена при записи WAV
		struct P { static bool Stop(double, void *) { return false; } };
		ok = Encode(MP3, s, "build", "x", &d[0], 4410, 2, 44100, "build/o2.mp3", err, P::Stop, 0, 0, extra);
		CHECK(!ok, "cancel in wav stage");
	}
	printf(fails ? "FAILED %d\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}
