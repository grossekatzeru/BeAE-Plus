#include "ExtEncoder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fstream>
#include <sstream>

using namespace ExtEnc;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static std::string Join(const std::vector<std::string> &a)
{
	std::string r;
	for (size_t i = 0; i < a.size(); i++) { if (i) r += " "; r += a[i]; }
	return r;
}
static void WriteScript(const std::string &path, const std::string &body)
{
	std::ofstream f(path.c_str());
	f << "#!/bin/sh\n" << body << "\n";
	f.close();
	chmod(path.c_str(), 0755);
}
static bool Never(void *) { return true; }
static bool Cancel(void *c) { int *n = (int *)c; return ++*n < 3 ? true : false; }

int main()
{
	// 1) командные строки
	{
		Settings s;
		std::string a = Join(BuildArgs(MP3, s, "in.wav", "out.mp3"));
		CHECK(a == "lame --silent -V 2 -m j in.wav out.mp3", "mp3 default: %s", a.c_str());
		s.mp3Mode = 1; s.mp3Bitrate = 320; s.mp3Stereo = 2;
		a = Join(BuildArgs(MP3, s, "in.wav", "out.mp3"));
		CHECK(a == "lame --silent -b 320 -m m in.wav out.mp3", "mp3 cbr: %s", a.c_str());
		s.mp3Mode = 2; s.mp3Bitrate = 128; s.mp3Stereo = 1;
		s.tags.title = "My Song"; s.tags.artist = "Me"; s.tags.year = "2026";
		std::vector<std::string> v = BuildArgs(MP3, s, "in.wav", "out.mp3");
		CHECK(Join(v) == "lame --silent --abr 128 -m s --add-id3v2 --tt My Song --ta Me --ty 2026 in.wav out.mp3", "mp3 abr+tags: %s", Join(v).c_str());
		bool kept = false; for (size_t i = 0; i < v.size(); i++) if (v[i] == "My Song") kept = true;
		CHECK(kept, "tag with space must be a single argument");

		Settings d;
		a = Join(BuildArgs(VORBIS, d, "in.wav", "out.ogg"));
		CHECK(a == "oggenc -Q -q 5 -o out.ogg in.wav", "ogg: %s", a.c_str());
		a = Join(BuildArgs(FLAC, d, "in.wav", "out.flac"));
		CHECK(a == "flac -s -f -5 -o out.flac in.wav", "flac: %s", a.c_str());
		a = Join(BuildArgs(OPUS, d, "in.wav", "out.opus"));
		CHECK(a == "opusenc --quiet --bitrate 128 in.wav out.opus", "opus: %s", a.c_str());
		d.tags.album = "Alb"; d.tags.comment = "hi";
		a = Join(BuildArgs(FLAC, d, "in.wav", "o.flac"));
		CHECK(a.find("--tag=ALBUM=Alb") != std::string::npos && a.find("--tag=COMMENT=hi") != std::string::npos, "flac tags: %s", a.c_str());
		a = Join(BuildArgs(OPUS, d, "in.wav", "o.opus"));
		CHECK(a.find("--album Alb") != std::string::npos && a.find("--comment COMMENT=hi") != std::string::npos, "opus tags: %s", a.c_str());
		Settings bad; bad.vorbisQ = 99; bad.flacLevel = -4; bad.opusBitrate = 9999; bad.mp3Bitrate = 1;
		a = Join(BuildArgs(VORBIS, bad, "i", "o")); CHECK(a.find("-q 10") != std::string::npos, "clamp q: %s", a.c_str());
		a = Join(BuildArgs(FLAC, bad, "i", "o")); CHECK(a.find(" -0 ") != std::string::npos, "clamp flac: %s", a.c_str());
		a = Join(BuildArgs(OPUS, bad, "i", "o")); CHECK(a.find("--bitrate 510") != std::string::npos, "clamp opus: %s", a.c_str());
		bad.mp3Mode = 1;
		a = Join(BuildArgs(MP3, bad, "i", "o")); CHECK(a.find("-b 32") != std::string::npos, "clamp mp3: %s", a.c_str());
		printf("command lines ok\n");
	}
	// 2) настройки: сохранение и чтение, спецсимволы в тегах, мусор
	{
		Settings s; s.mp3Mode = 2; s.mp3Vbr = 7; s.mp3Bitrate = 256; s.mp3Stereo = 1; s.vorbisQ = -1; s.flacLevel = 8; s.opusBitrate = 64;
		s.tags.title = "a;b=c%d\nе"; s.tags.artist = "Артист"; s.tags.comment = "100%";
		Settings r; r.FromString(s.ToString());
		CHECK(r.mp3Mode == 2 && r.mp3Vbr == 7 && r.mp3Bitrate == 256 && r.mp3Stereo == 1 && r.vorbisQ == -1 && r.flacLevel == 8 && r.opusBitrate == 64, "numbers");
		CHECK(r.tags.title == s.tags.title && r.tags.artist == s.tags.artist && r.tags.comment == "100%", "tags: '%s'", r.tags.title.c_str());
		Settings g; g.FromString("mp3Mode=zzz;nonsense;flacLevel=99;=;;opusBitrate=-5");
		CHECK(g.mp3Mode == 0 && g.flacLevel == 8 && g.opusBitrate == 6, "garbage: %d %d %d", g.mp3Mode, g.flacLevel, g.opusBitrate);
		printf("settings ok\n");
	}
	// 3) WAV
	{
		const char *path = "build/t16.wav";
		std::vector<float> d;
		for (int i = 0; i < 1000; i++) { d.push_back(0.5f * sinf(i * 0.1f)); d.push_back(i == 10 ? 2.0f : -0.25f); }
		WavWriter w;
		CHECK(w.Open(path, 2, 44100, 16), "open");
		CHECK(w.Write(&d[0], 400) && w.Write(&d[800], 600), "write");
		CHECK(w.Close(), "close");
		std::ifstream f(path, std::ios::binary);
		std::stringstream ss; ss << f.rdbuf(); std::string b = ss.str();
		CHECK(b.size() == 44 + 1000 * 4, "size %zu", b.size());
		CHECK(b.substr(0, 4) == "RIFF" && b.substr(8, 8) == "WAVEfmt ", "header");
		unsigned riff = *(unsigned *)&b[4], dat = *(unsigned *)&b[40];
		CHECK(riff == 36 + 4000 && dat == 4000, "sizes %u %u", riff, dat);
		CHECK(*(unsigned short *)&b[22] == 2 && *(unsigned *)&b[24] == 44100 && *(unsigned short *)&b[34] == 16, "fmt");
		const short *s16 = (const short *)&b[44];
		CHECK(abs(s16[2 * 100] - (int)lrintf(0.5f * sinf(10.0f) * 32767.0f)) <= 1, "sample");
		CHECK(s16[2 * 100 + 1] == (short)lrint(-0.25 * 32767.0), "right");
		CHECK(w.Open(path, 2, 44100, 16) == true, "reopen after close");
		w.Close();

		WavWriter w3;
		std::vector<float> big(8, 3.0f); big[1] = -3.0f;
		CHECK(w3.Open("build/t24.wav", 2, 48000, 24) && w3.Write(&big[0], 4) && w3.Close(), "24 bit");
		std::ifstream f3("build/t24.wav", std::ios::binary);
		std::stringstream s3; s3 << f3.rdbuf(); std::string c = s3.str();
		CHECK(c.size() == 44 + 4 * 2 * 3, "24 size %zu", c.size());
		const unsigned char *p = (const unsigned char *)&c[44];
		CHECK(p[0] == 0xFF && p[1] == 0xFF && p[2] == 0x7F, "clip + %02x%02x%02x", p[2], p[1], p[0]);
		CHECK(p[3] == 0x01 && p[4] == 0x00 && p[5] == 0x80, "clip - %02x%02x%02x", p[5], p[4], p[3]);
		WavWriter bad;
		CHECK(!bad.Open("build/x.wav", 0, 44100, 16) && !bad.Open("build/x.wav", 2, 44100, 12) && !bad.Open("/nonexistent/dir/x.wav", 2, 44100, 16), "bad open");
		printf("wav ok\n");
	}
	// 4) запуск программ и поиск
	{
		const std::string dir = "build/fakebin";
		system("rm -rf build/fakebin && mkdir -p build/fakebin");
		WriteScript(dir + "/okenc", "echo working; cat \"$1\" > \"$2\"");
		WriteScript(dir + "/failenc", "echo 'bad input' >&2; exit 3");
		WriteScript(dir + "/slowenc", "sleep 30");
		char cwd[512]; getcwd(cwd, sizeof(cwd));
		const std::string full = std::string(cwd) + "/" + dir;
		setenv("PATH", (full + ":/usr/bin:/bin").c_str(), 1);
		CHECK(FindProgram("okenc") == full + "/okenc", "find: %s", FindProgram("okenc").c_str());
		CHECK(FindProgram("nothing_like_this").empty(), "missing program");
		system("echo data > build/in.txt");
		std::vector<std::string> a; a.push_back("okenc"); a.push_back("build/in.txt"); a.push_back("build/out.txt");
		RunResult r = Run(a, Never, NULL);
		CHECK(r.ok && r.exitCode == 0 && r.log.find("working") != std::string::npos, "ok run: %d '%s'", r.exitCode, r.log.c_str());
		std::ifstream o("build/out.txt"); std::string line; std::getline(o, line);
		CHECK(line == "data", "output written");
		std::vector<std::string> f; f.push_back("failenc");
		r = Run(f, NULL, NULL);
		CHECK(!r.ok && r.started && r.exitCode == 3 && r.log.find("bad input") != std::string::npos, "fail run: %d '%s'", r.exitCode, r.log.c_str());
		std::vector<std::string> s; s.push_back("slowenc");
		int n = 0;
		r = Run(s, Cancel, &n);
		CHECK(!r.ok && r.log == "Cancelled", "cancel: '%s'", r.log.c_str());
		std::vector<std::string> m; m.push_back("definitely_missing_prog");
		r = Run(m, NULL, NULL);
		CHECK(!r.ok && !r.started, "missing: started %d", (int)r.started);
		std::vector<std::string> sp; sp.push_back("okenc"); sp.push_back("build/in.txt"); sp.push_back("build/out file ; $(x) 'q'.txt");
		r = Run(sp, NULL, NULL);
		CHECK(r.ok, "name with spaces and shell characters: %d", r.exitCode);
		struct stat st; CHECK(stat("build/out file ; $(x) 'q'.txt", &st) == 0, "file with special name exists");
		printf("run ok\n");
	}
	printf(fails ? "FAILED %d\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}
