#include "ExtEncoder.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace ExtEncoder {

static const Profile kProfiles[COUNT] = {
	//  id    key    title                    program    ext    bits  mode   quality bitrate stereo level
	{ MP3,  "mp3",  "MP3 (lame)",            "lame",    "mp3",  16,  true,  true,   true,   true,  false },
	{ OGG,  "ogg",  "Ogg Vorbis (oggenc)",   "oggenc",  "ogg",  24,  true,  true,   true,   false, false },
	{ FLAC, "flac", "FLAC (flac)",           "flac",    "flac", 24,  false, false,  false,  false, true  },
	{ OPUS, "opus", "Opus (opusenc)",        "opusenc", "opus", 24,  true,  false,  true,   false, false },
};

const Profile &GetProfile(Id id) { return kProfiles[id]; }

static int ClampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

Settings Defaults(Id id)
{
	Settings s;
	s.mode = VBR; s.quality = 2; s.bitrate = 192; s.stereo = JOINT; s.level = 5;
	switch (id) {
	case MP3:  s.quality = 2; s.bitrate = 192; break;			// lame -V2 (~190 кбит/с), как «стандарт» у lame
	case OGG:  s.quality = 5; s.bitrate = 160; break;
	case FLAC: s.level = 5; break;
	case OPUS: s.bitrate = 128; break;
	default: break;
	}
	return s;
}

void Clamp(Id id, Settings &s)
{
	const Profile &p = GetProfile(id);
	s.mode = ClampInt(s.mode, VBR, id == MP3 ? ABR : CBR);
	if (id == MP3) s.quality = ClampInt(s.quality, 0, 9);
	else s.quality = ClampInt(s.quality, 0, 10);
	if (id == OPUS) s.bitrate = ClampInt(s.bitrate, 6, 510);
	else s.bitrate = ClampInt(s.bitrate, 32, 320);
	s.stereo = ClampInt(s.stereo, JOINT, MONO);
	s.level = ClampInt(s.level, 0, 8);
	(void)p;
}

// ------------------------------------------------------------------ настройки в строку

static std::string Esc(const std::string &v)		// в значении нельзя ';' и '=' - заменяем
{
	std::string o;
	for (size_t i = 0; i < v.size(); i++) {
		if (v[i] == ';') o += "%3B";
		else if (v[i] == '=') o += "%3D";
		else if (v[i] == '%') o += "%25";
		else if (v[i] == '\n' || v[i] == '\r') o += ' ';
		else o += v[i];
	}
	return o;
}
static std::string Unesc(const std::string &v)
{
	std::string o;
	for (size_t i = 0; i < v.size(); i++) {
		if (v[i] == '%' && i + 2 < v.size()) {
			const std::string h = v.substr(i, 3);
			if (h == "%3B") { o += ';'; i += 2; continue; }
			if (h == "%3D") { o += '='; i += 2; continue; }
			if (h == "%25") { o += '%'; i += 2; continue; }
		}
		o += v[i];
	}
	return o;
}

std::string ToString(Id id, const Settings &st)
{
	const Profile &p = GetProfile(id);
	Settings s = st;
	Clamp(id, s);
	char b[64];
	std::string r;
	if (p.hasMode)    { snprintf(b, sizeof(b), "mode=%d;", s.mode); r += b; }
	if (p.hasQuality) { snprintf(b, sizeof(b), "q=%d;", s.quality); r += b; }
	if (p.hasBitrate) { snprintf(b, sizeof(b), "br=%d;", s.bitrate); r += b; }
	if (p.hasStereo)  { snprintf(b, sizeof(b), "st=%d;", s.stereo); r += b; }
	if (p.hasLevel)   { snprintf(b, sizeof(b), "lv=%d;", s.level); r += b; }
	r += "artist=" + Esc(s.tags.artist) + ";album=" + Esc(s.tags.album) + ";year=" + Esc(s.tags.year) + ";comment=" + Esc(s.tags.comment);
	return r;				// название (title) не храним: оно своё у каждого файла
}

Settings FromString(Id id, const std::string &str)
{
	Settings s = Defaults(id);
	size_t pos = 0;
	while (pos <= str.size()) {
		size_t end = str.find(';', pos);
		if (end == std::string::npos) end = str.size();
		const std::string item = str.substr(pos, end - pos);
		const size_t eq = item.find('=');
		if (eq != std::string::npos) {
			const std::string k = item.substr(0, eq), v = item.substr(eq + 1);
			if (k == "mode") s.mode = atoi(v.c_str());
			else if (k == "q") s.quality = atoi(v.c_str());
			else if (k == "br") s.bitrate = atoi(v.c_str());
			else if (k == "st") s.stereo = atoi(v.c_str());
			else if (k == "lv") s.level = atoi(v.c_str());
			else if (k == "artist") s.tags.artist = Unesc(v);
			else if (k == "album") s.tags.album = Unesc(v);
			else if (k == "year") s.tags.year = Unesc(v);
			else if (k == "comment") s.tags.comment = Unesc(v);
		}
		pos = end + 1;
	}
	Clamp(id, s);
	return s;
}

// ------------------------------------------------------------------ поиск программ

static bool IsExecutable(const std::string &path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) && access(path.c_str(), X_OK) == 0;
}

std::string FindProgram(const char *program, const std::vector<std::string> &extraDirs)
{
	std::vector<std::string> dirs = extraDirs;
	const char *path = getenv("PATH");
	if (path) {
		std::string p(path);
		size_t pos = 0;
		while (pos <= p.size()) {
			size_t end = p.find(':', pos);
			if (end == std::string::npos) end = p.size();
			if (end > pos) dirs.push_back(p.substr(pos, end - pos));
			pos = end + 1;
		}
	}
	// типичные места Haiku и Linux (приложение, запущенное из Tracker, может получить урезанный PATH)
	const char *fixed[] = { "/boot/system/bin", "/boot/system/non-packaged/bin", "/boot/home/config/non-packaged/bin",
		"/boot/home/config/bin", "/bin", "/usr/bin", "/usr/local/bin", 0 };
	for (int i = 0; fixed[i]; i++) dirs.push_back(fixed[i]);
	for (size_t i = 0; i < dirs.size(); i++) {
		const std::string full = dirs[i] + "/" + program;
		if (IsExecutable(full)) return full;
	}
	return "";
}

std::vector<Id> Available(const std::vector<std::string> &extraDirs)
{
	std::vector<Id> r;
	for (int i = 0; i < COUNT; i++)
		if (!FindProgram(kProfiles[i].program, extraDirs).empty()) r.push_back((Id)i);
	return r;
}

// ------------------------------------------------------------------ командная строка

static std::string Num(int v) { char b[32]; snprintf(b, sizeof(b), "%d", v); return b; }

std::vector<std::string> BuildArgs(Id id, const std::string &program, const Settings &st, const std::string &fallbackTitle,
	const std::string &inWav, const std::string &outFile)
{
	Settings s = st;
	Clamp(id, s);
	const std::string title = s.tags.title.empty() ? fallbackTitle : s.tags.title;
	std::vector<std::string> a;
	a.push_back(program);
	switch (id) {
	case MP3:
		a.push_back("--nohist");
		a.push_back("--silent");
		if (s.mode == CBR) { a.push_back("-b"); a.push_back(Num(s.bitrate)); }
		else if (s.mode == ABR) { a.push_back("--abr"); a.push_back(Num(s.bitrate)); }
		else { a.push_back("-V"); a.push_back(Num(s.quality)); }
		a.push_back("-m");
		a.push_back(s.stereo == MONO ? "m" : (s.stereo == STEREO ? "s" : "j"));
		a.push_back("--add-id3v2");
		if (!title.empty())          { a.push_back("--tt"); a.push_back(title); }
		if (!s.tags.artist.empty())  { a.push_back("--ta"); a.push_back(s.tags.artist); }
		if (!s.tags.album.empty())   { a.push_back("--tl"); a.push_back(s.tags.album); }
		if (!s.tags.year.empty())    { a.push_back("--ty"); a.push_back(s.tags.year); }
		if (!s.tags.comment.empty()) { a.push_back("--tc"); a.push_back(s.tags.comment); }
		a.push_back(inWav);
		a.push_back(outFile);
		break;
	case OGG:
		a.push_back("--quiet");
		if (s.mode == VBR) { a.push_back("-q"); a.push_back(Num(s.quality)); }
		else { a.push_back("-b"); a.push_back(Num(s.bitrate)); }
		if (!title.empty())          { a.push_back("-t"); a.push_back(title); }
		if (!s.tags.artist.empty())  { a.push_back("-a"); a.push_back(s.tags.artist); }
		if (!s.tags.album.empty())   { a.push_back("-l"); a.push_back(s.tags.album); }
		if (!s.tags.year.empty())    { a.push_back("-d"); a.push_back(s.tags.year); }
		if (!s.tags.comment.empty()) { a.push_back("-c"); a.push_back("COMMENT=" + s.tags.comment); }
		a.push_back("-o");
		a.push_back(outFile);
		a.push_back(inWav);
		break;
	case FLAC:
		a.push_back("--silent");
		a.push_back("--force");
		a.push_back("-" + Num(s.level));
		if (!title.empty())          { a.push_back("-T"); a.push_back("TITLE=" + title); }
		if (!s.tags.artist.empty())  { a.push_back("-T"); a.push_back("ARTIST=" + s.tags.artist); }
		if (!s.tags.album.empty())   { a.push_back("-T"); a.push_back("ALBUM=" + s.tags.album); }
		if (!s.tags.year.empty())    { a.push_back("-T"); a.push_back("DATE=" + s.tags.year); }
		if (!s.tags.comment.empty()) { a.push_back("-T"); a.push_back("COMMENT=" + s.tags.comment); }
		a.push_back("-o");
		a.push_back(outFile);
		a.push_back(inWav);
		break;
	case OPUS:
		a.push_back("--quiet");
		if (s.mode == CBR) a.push_back("--hard-cbr");
		a.push_back("--bitrate");
		a.push_back(Num(s.bitrate));
		if (!title.empty())          { a.push_back("--title"); a.push_back(title); }
		if (!s.tags.artist.empty())  { a.push_back("--artist"); a.push_back(s.tags.artist); }
		if (!s.tags.album.empty())   { a.push_back("--album"); a.push_back(s.tags.album); }
		if (!s.tags.year.empty())    { a.push_back("--date"); a.push_back(s.tags.year); }
		if (!s.tags.comment.empty()) { a.push_back("--comment"); a.push_back("COMMENT=" + s.tags.comment); }
		a.push_back(inWav);
		a.push_back(outFile);
		break;
	default: break;
	}
	return a;
}

// ------------------------------------------------------------------ временный WAV

static void Put16(unsigned char *p, uint16_t v) { p[0] = v & 255; p[1] = v >> 8; }
static void Put32(unsigned char *p, uint32_t v) { p[0] = v & 255; p[1] = (v >> 8) & 255; p[2] = (v >> 16) & 255; p[3] = v >> 24; }

bool WriteWavPcm(const char *path, const float *data, long long frames, int channels, float rate, int bits,
	ProgressFn progress, void *user)
{
	if (bits != 16 && bits != 24) return false;
	if (channels < 1 || channels > 2 || frames < 0) return false;
	const int bytes = bits / 8;
	const uint64_t dataSize = (uint64_t)frames * channels * bytes;
	if (dataSize > 0xFFFFFF00ULL) return false;				// WAV до 4 ГБ
	FILE *f = fopen(path, "wb");
	if (!f) return false;
	unsigned char h[44];
	memcpy(h, "RIFF", 4); Put32(h + 4, (uint32_t)(36 + dataSize));
	memcpy(h + 8, "WAVEfmt ", 8); Put32(h + 16, 16);
	Put16(h + 20, 1); Put16(h + 22, (uint16_t)channels);
	const uint32_t rateInt = (uint32_t)(rate + 0.5f);
	Put32(h + 24, rateInt); Put32(h + 28, rateInt * channels * bytes);
	Put16(h + 32, (uint16_t)(channels * bytes)); Put16(h + 34, (uint16_t)bits);
	memcpy(h + 36, "data", 4); Put32(h + 40, (uint32_t)dataSize);
	bool ok = fwrite(h, 1, 44, f) == 44;
	std::vector<unsigned char> buf((size_t)65536 * bytes * channels);
	uint32_t rng = 12345u;
	long long done = 0;
	while (ok && done < frames) {
		const long long n = (frames - done < 65536) ? frames - done : 65536;
		unsigned char *o = &buf[0];
		for (long long i = 0; i < n * channels; i++) {
			double v = data[done * channels + i];
			if (bits == 16) {
				rng = rng * 1664525u + 1013904223u; const double r1 = (rng >> 8) / 16777216.0;
				rng = rng * 1664525u + 1013904223u; const double r2 = (rng >> 8) / 16777216.0;
				v = v * 32767.0 + (r1 - r2);			// треугольный дизеринг ±1 LSB
				long q = lrint(v);
				if (q > 32767) q = 32767;
				if (q < -32768) q = -32768;
				Put16(o, (uint16_t)(int16_t)q); o += 2;
			} else {
				long q = lrint(v * 8388607.0);
				if (q > 8388607) q = 8388607;
				if (q < -8388608) q = -8388608;
				const uint32_t u = (uint32_t)(int32_t)q;
				o[0] = u & 255; o[1] = (u >> 8) & 255; o[2] = (u >> 16) & 255; o += 3;
			}
		}
		ok = fwrite(&buf[0], 1, (size_t)(o - &buf[0]), f) == (size_t)(o - &buf[0]);
		done += n;
		if (ok && progress && !progress(frames > 0 ? (double)done / frames : 1.0, user)) { ok = false; break; }
	}
	if (fclose(f) != 0) ok = false;
	if (!ok) unlink(path);
	return ok;
}

// ------------------------------------------------------------------ запуск

int Run(const std::vector<std::string> &args, std::string &output, CancelFn cancel, void *user)
{
	output.clear();
	if (args.empty()) return -1;
	int pipefd[2];
	if (pipe(pipefd) != 0) return -1;
	std::vector<char *> argv;
	for (size_t i = 0; i < args.size(); i++) argv.push_back(const_cast<char *>(args[i].c_str()));
	argv.push_back(0);
	const pid_t pid = fork();
	if (pid < 0) { close(pipefd[0]); close(pipefd[1]); return -1; }
	if (pid == 0) {
		dup2(pipefd[1], 1);
		dup2(pipefd[1], 2);
		close(pipefd[0]); close(pipefd[1]);
		const int nul = open("/dev/null", O_RDONLY);
		if (nul >= 0) { dup2(nul, 0); close(nul); }
		execv(argv[0], &argv[0]);
		_exit(127);
	}
	close(pipefd[1]);
	fcntl(pipefd[0], F_SETFL, fcntl(pipefd[0], F_GETFL) | O_NONBLOCK);
	int status = 0;
	bool cancelled = false, exited = false;
	char buf[1024];
	while (!exited) {
		struct pollfd pfd; pfd.fd = pipefd[0]; pfd.events = POLLIN; pfd.revents = 0;
		poll(&pfd, 1, 100);
		for (;;) {
			const ssize_t n = read(pipefd[0], buf, sizeof(buf));
			if (n > 0) { output.append(buf, (size_t)n); if (output.size() > 8192) output.erase(0, output.size() - 4096); }
			else break;
		}
		if (cancel && cancel(user)) { kill(pid, SIGKILL); cancelled = true; }
		const pid_t w = waitpid(pid, &status, WNOHANG);
		if (w == pid) exited = true;
		else if (w < 0 && errno != EINTR) { exited = true; status = 0; }
	}
	for (;;) {
		const ssize_t n = read(pipefd[0], buf, sizeof(buf));
		if (n > 0) output.append(buf, (size_t)n); else break;
	}
	close(pipefd[0]);
	if (output.size() > 4096) output.erase(0, output.size() - 4096);
	if (cancelled) return -2;
	if (WIFEXITED(status)) return WEXITSTATUS(status) == 127 && output.empty() ? -1 : WEXITSTATUS(status);
	return -1;
}

static std::string TempName(const std::string &dir)
{
	static int counter = 0;
	char b[64];
	snprintf(b, sizeof(b), "/beae_enc_%d_%d.wav", (int)getpid(), counter++);
	return dir + b;
}

bool Encode(Id id, const Settings &s, const std::string &tempDir, const std::string &fallbackTitle,
	const float *data, long long frames, int channels, float rate, const std::string &outFile, std::string &err,
	ProgressFn progress, CancelFn cancel, void *user, const std::vector<std::string> &extraDirs)
{
	const Profile &p = GetProfile(id);
	const std::string prog = FindProgram(p.program, extraDirs);
	if (prog.empty()) { err = std::string(p.program) + " is not installed"; return false; }
	const std::string tmp = TempName(tempDir.empty() ? "/tmp" : tempDir);
	if (!WriteWavPcm(tmp.c_str(), data, frames, channels, rate, p.wavBits, progress, user)) {
		err = "Cannot write the temporary file " + tmp;
		return false;
	}
	std::string out;
	const int code = Run(BuildArgs(id, prog, s, fallbackTitle, tmp, outFile), out, cancel, user);
	unlink(tmp.c_str());
	if (code == -2) { unlink(outFile.c_str()); err = "Cancelled"; return false; }
	if (code != 0) {
		unlink(outFile.c_str());
		err = std::string(p.program) + (code < 0 ? " cannot be started" : " failed");
		if (!out.empty()) err += ":\n" + out;
		return false;
	}
	return true;
}

}	// namespace ExtEncoder
