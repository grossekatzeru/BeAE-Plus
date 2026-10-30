/*
 * LadspaHost - см. LadspaHost.h
 */
#include "LadspaHost.h"

#include <dlfcn.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <stdio.h>
#include <algorithm>
#include <fstream>
#include <sstream>

namespace Ladspa {

typedef const Descriptor *(*DescriptorFunc)(unsigned long);

std::vector<int> Plugin::ControlInputs() const
{
	std::vector<int> r;
	for (size_t i = 0; i < ports.size(); ++i)
		if (ports[i].IsControl() && ports[i].IsInput())
			r.push_back((int)i);
	return r;
}

std::string Plugin::Key() const
{
	return file + "|" + label;
}

// ------------------------------------------------------------------ диапазоны

static float LerpLog(float l, float u, float t)
{
	return expf(logf(l) * (1.0f - t) + logf(u) * t);
}

Range ResolveRange(const Port &p, float rate)
{
	Range r;
	r.integer = p.hint & HINT_INTEGER;
	r.toggled = p.hint & HINT_TOGGLED;
	r.logarithmic = p.hint & HINT_LOGARITHMIC;

	const bool hasLo = p.hint & HINT_BOUNDED_BELOW;
	const bool hasHi = p.hint & HINT_BOUNDED_ABOVE;
	const float mult = (p.hint & HINT_SAMPLE_RATE) ? rate : 1.0f;
	float lo = hasLo ? p.lower * mult : 0.0f;
	float hi = hasHi ? p.upper * mult : 0.0f;

	if (r.toggled) {				// переключатель: 0/1
		r.lower = 0.0f;
		r.upper = 1.0f;
		r.integer = true;
		r.logarithmic = false;
		lo = 0.0f; hi = 1.0f;
	} else {
		if (!hasLo && !hasHi) { lo = -1000.0f; hi = 1000.0f; }
		else if (!hasLo)	lo = hi - 1000.0f;
		else if (!hasHi)	hi = lo + 1000.0f;
		if (hi <= lo)	hi = lo + 1.0f;
		r.lower = lo;
		r.upper = hi;
	}
	// логарифмическая шкала возможна только для положительных границ
	if (r.logarithmic && r.lower <= 0.0f)
		r.logarithmic = false;

	// значение по умолчанию
	float def;
	switch (p.hint & HINT_DEFAULT_MASK) {
	case HINT_DEFAULT_MINIMUM:	def = r.lower; break;
	case HINT_DEFAULT_MAXIMUM:	def = r.upper; break;
	case HINT_DEFAULT_LOW:
		def = r.logarithmic ? LerpLog(r.lower, r.upper, 0.25f) : r.lower * 0.75f + r.upper * 0.25f;
		break;
	case HINT_DEFAULT_MIDDLE:
		def = r.logarithmic ? LerpLog(r.lower, r.upper, 0.5f) : r.lower * 0.5f + r.upper * 0.5f;
		break;
	case HINT_DEFAULT_HIGH:
		def = r.logarithmic ? LerpLog(r.lower, r.upper, 0.75f) : r.lower * 0.25f + r.upper * 0.75f;
		break;
	case HINT_DEFAULT_0:		def = 0.0f; break;
	case HINT_DEFAULT_1:		def = 1.0f; break;
	case HINT_DEFAULT_100:		def = 100.0f; break;
	case HINT_DEFAULT_440:		def = 440.0f; break;
	default:					// нет подсказки: ближайшее к нулю допустимое значение
		def = 0.0f;
		if (def < r.lower) def = r.lower;
		if (def > r.upper) def = r.upper;
		break;
	}
	if (def < r.lower) def = r.lower;
	if (def > r.upper) def = r.upper;
	if (r.integer) def = floorf(def + 0.5f);
	r.def = def;
	return r;
}

// ------------------------------------------------------------------ поиск

std::vector<std::string> DefaultPaths()
{
	std::vector<std::string> dirs;
	const char *env = getenv("LADSPA_PATH");
	if (env && *env) {
		std::string s(env);
		size_t pos = 0;
		while (pos <= s.size()) {
			size_t e = s.find(':', pos);
			if (e == std::string::npos) e = s.size();
			if (e > pos) dirs.push_back(s.substr(pos, e - pos));
			pos = e + 1;
		}
	}
	const char *home = getenv("HOME");
	std::string h = home ? home : "";
	// Haiku
	if (!h.empty()) {
		dirs.push_back(h + "/config/lib/ladspa");
		dirs.push_back(h + "/config/non-packaged/lib/ladspa");
	}
	dirs.push_back("/boot/system/lib/ladspa");
	dirs.push_back("/boot/system/non-packaged/lib/ladspa");
	// Linux (для проверки ядра)
	dirs.push_back("/usr/local/lib/ladspa");
	dirs.push_back("/usr/lib/ladspa");
	// без повторов
	std::vector<std::string> out;
	for (size_t i = 0; i < dirs.size(); ++i)
		if (std::find(out.begin(), out.end(), dirs[i]) == out.end())
			out.push_back(dirs[i]);
	return out;
}

static bool IsRegularFile(const std::string &path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static Mode ModeFor(int in, int out)
{
	if (in == 1 && out == 1) return MODE_MONO_MONO;
	if (in == 2 && out == 2) return MODE_STEREO_STEREO;
	if (in == 1 && out == 2) return MODE_MONO_STEREO;
	if (in == 2 && out == 1) return MODE_STEREO_MONO;
	return MODE_UNSUPPORTED;
}

static void ScanFile(const std::string &path, std::vector<Plugin> *out, std::vector<std::string> *log)
{
	void *h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
	if (!h) {
		if (log) { const char *e = dlerror(); log->push_back(path + ": " + (e ? e : "dlopen")); }
		return;
	}
	DescriptorFunc fn = (DescriptorFunc)dlsym(h, "ladspa_descriptor");
	if (!fn) {					// не LADSPA-библиотека
		dlclose(h);
		return;
	}
	for (unsigned long i = 0; ; ++i) {
		const Descriptor *d = fn(i);
		if (!d) break;
		if (!d->Label || !d->instantiate || !d->connect_port || !d->run)
			continue;
		Plugin p;
		p.file = path;
		p.label = d->Label;
		p.name = d->Name ? d->Name : d->Label;
		p.maker = d->Maker ? d->Maker : "";
		p.uniqueId = d->UniqueID;
		p.audioIn = p.audioOut = 0;
		p.realtimeSafe = d->Properties & 0x4;	// LADSPA_PROPERTY_HARD_RT_CAPABLE
		for (unsigned long k = 0; k < d->PortCount; ++k) {
			Port po;
			po.index = (int)k;
			po.name = d->PortNames && d->PortNames[k] ? d->PortNames[k] : "";
			po.descriptor = d->PortDescriptors[k];
			po.hint = d->PortRangeHints ? d->PortRangeHints[k].hint : 0;
			po.lower = d->PortRangeHints ? d->PortRangeHints[k].lower : 0;
			po.upper = d->PortRangeHints ? d->PortRangeHints[k].upper : 0;
			if (po.IsAudio()) {
				if (po.IsInput()) ++p.audioIn; else ++p.audioOut;
			}
			p.ports.push_back(po);
		}
		p.mode = ModeFor(p.audioIn, p.audioOut);
		out->push_back(p);
	}
	dlclose(h);
}

std::vector<Plugin> Scan(const std::vector<std::string> &dirs, std::vector<std::string> *log)
{
	std::vector<Plugin> result;
	for (size_t di = 0; di < dirs.size(); ++di) {
		DIR *d = opendir(dirs[di].c_str());
		if (!d) continue;
		std::vector<std::string> files;
		while (struct dirent *e = readdir(d)) {
			std::string n = e->d_name;
			if (n.size() < 4 || n.compare(n.size() - 3, 3, ".so") != 0)
				continue;
			std::string full = dirs[di] + "/" + n;
			if (IsRegularFile(full))
				files.push_back(full);
		}
		closedir(d);
		std::sort(files.begin(), files.end());
		for (size_t i = 0; i < files.size(); ++i)
			ScanFile(files[i], &result, log);
	}
	std::stable_sort(result.begin(), result.end(), [](const Plugin &a, const Plugin &b) {
		return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
	});
	return result;
}

// ------------------------------------------------------------------ значения

bool LoadValues(const std::string &path, Values *values)
{
	std::ifstream f(path.c_str());
	if (!f) return false;
	std::string line;
	while (std::getline(f, line)) {
		size_t eq = line.find('=');
		if (eq == std::string::npos || line.empty() || line[0] == '#') continue;
		char *end = 0;
		long idx = strtol(line.c_str(), &end, 10);
		if (end != line.c_str() + eq) continue;
		float v = (float)strtod(line.c_str() + eq + 1, 0);
		(*values)[(int)idx] = v;
	}
	return true;
}

bool SaveValues(const std::string &path, const Values &values)
{
	std::ofstream f(path.c_str());
	if (!f) return false;
	for (Values::const_iterator i = values.begin(); i != values.end(); ++i)
		f << i->first << "=" << i->second << "\n";
	return f.good();
}

// ------------------------------------------------------------------ запуск

static const size_t kBlock = 2048;		// кадров за один вызов run()

struct Runner::Impl {
	void			*lib;
	const Descriptor *desc;
	std::vector<void*> inst;			// 1 или 2 экземпляра
	std::vector<float> controls;		// по одному слоту на порт
	Plugin			plugin;
	int				channels;
	// буферы блока
	std::vector<float> in[2], out[2];
	Impl() : lib(0), desc(0), channels(2) {}
};

Runner::Runner() : fImpl(new Impl), fRunning(false) {}
Runner::~Runner() { Stop(); delete fImpl; }

void Runner::Stop()
{
	if (!fImpl) return;
	if (fImpl->desc) {
		for (size_t i = 0; i < fImpl->inst.size(); ++i) {
			if (fImpl->desc->deactivate) fImpl->desc->deactivate(fImpl->inst[i]);
			if (fImpl->desc->cleanup) fImpl->desc->cleanup(fImpl->inst[i]);
		}
	}
	fImpl->inst.clear();
	if (fImpl->lib) {
		dlclose(fImpl->lib);
		fImpl->lib = 0;
	}
	fImpl->desc = 0;
	fRunning = false;
}

bool Runner::Start(const Plugin &plugin, float rate, int channels, std::string *error)
{
	Stop();
	Impl &m = *fImpl;
	if (plugin.mode == MODE_UNSUPPORTED) {
		if (error) *error = "unsupported port layout";
		return false;
	}
	if (channels != 1 && channels != 2) {
		if (error) *error = "channels must be 1 or 2";
		return false;
	}
	m.lib = dlopen(plugin.file.c_str(), RTLD_NOW | RTLD_LOCAL);
	if (!m.lib) {
		if (error) { const char *e = dlerror(); *error = e ? e : "dlopen failed"; }
		return false;
	}
	DescriptorFunc fn = (DescriptorFunc)dlsym(m.lib, "ladspa_descriptor");
	if (fn) {
		for (unsigned long i = 0; ; ++i) {
			const Descriptor *d = fn(i);
			if (!d) break;
			if (d->Label && plugin.label == d->Label) { m.desc = d; break; }
		}
	}
	if (!m.desc) {
		if (error) *error = "plugin not found in library";
		Stop();
		return false;
	}
	m.plugin = plugin;
	m.channels = channels;

	// значения по умолчанию для всех управляющих портов
	m.controls.assign(plugin.ports.size(), 0.0f);
	for (size_t i = 0; i < plugin.ports.size(); ++i) {
		const Port &p = plugin.ports[i];
		if (p.IsControl())
			m.controls[i] = ResolveRange(p, rate).def;
	}

	// число экземпляров: моно-плагин на стерео-буфере = два экземпляра (L и R)
	size_t count = (plugin.mode == MODE_MONO_MONO && channels == 2) ? 2 : 1;
	for (int c = 0; c < 2; ++c) {
		m.in[c].assign(kBlock, 0.0f);
		m.out[c].assign(kBlock, 0.0f);
	}
	for (size_t n = 0; n < count; ++n) {
		void *h = m.desc->instantiate(m.desc, (unsigned long)(rate + 0.5f));
		if (!h) {
			if (error) *error = "instantiate failed";
			Stop();
			return false;
		}
		m.inst.push_back(h);
		int ai = 0, ao = 0;
		for (size_t i = 0; i < plugin.ports.size(); ++i) {
			const Port &p = plugin.ports[i];
			if (p.IsControl()) {
				m.desc->connect_port(h, (unsigned long)i, &m.controls[i]);
			} else if (p.IsAudio()) {
				if (p.IsInput()) {
					int slot = (count == 2) ? (int)n : ai;		// два экземпляра: свой канал
					m.desc->connect_port(h, (unsigned long)i, m.in[slot].data());
					++ai;
				} else {
					int slot = (count == 2) ? (int)n : ao;
					m.desc->connect_port(h, (unsigned long)i, m.out[slot].data());
					++ao;
				}
			}
		}
		if (m.desc->activate) m.desc->activate(h);
	}
	fRunning = true;
	return true;
}

void Runner::SetControl(int portIndex, float value)
{
	if (portIndex >= 0 && portIndex < (int)fImpl->controls.size())
		fImpl->controls[portIndex] = value;
}

float Runner::Control(int portIndex) const
{
	if (portIndex >= 0 && portIndex < (int)fImpl->controls.size())
		return fImpl->controls[portIndex];
	return 0.0f;
}

void Runner::Process(float *buf, size_t frames)
{
	if (!fRunning) return;
	Impl &m = *fImpl;
	const int ch = m.channels;
	const Mode mode = m.plugin.mode;

	size_t done = 0;
	while (done < frames) {
		size_t n = frames - done;
		if (n > kBlock) n = kBlock;
		float *b = buf + done * ch;

		// --- вход ---
		if (ch == 1) {
			for (size_t i = 0; i < n; ++i) m.in[0][i] = b[i];
			if (mode == MODE_STEREO_STEREO || mode == MODE_STEREO_MONO)
				for (size_t i = 0; i < n; ++i) m.in[1][i] = b[i];	// моно дублируем на оба входа
		} else {
			switch (mode) {
			case MODE_MONO_MONO:			// два экземпляра: L и R
				for (size_t i = 0; i < n; ++i) { m.in[0][i] = b[i * 2]; m.in[1][i] = b[i * 2 + 1]; }
				break;
			case MODE_MONO_STEREO:			// один вход: сумма каналов
				for (size_t i = 0; i < n; ++i) m.in[0][i] = 0.5f * (b[i * 2] + b[i * 2 + 1]);
				break;
			default:
				for (size_t i = 0; i < n; ++i) { m.in[0][i] = b[i * 2]; m.in[1][i] = b[i * 2 + 1]; }
				break;
			}
		}

		// --- запуск ---
		for (size_t k = 0; k < m.inst.size(); ++k)
			m.desc->run(m.inst[k], (unsigned long)n);

		// --- выход ---
		if (ch == 1) {
			for (size_t i = 0; i < n; ++i) b[i] = m.out[0][i];		// у стерео-выхода берём левый канал
		} else {
			switch (mode) {
			case MODE_MONO_MONO:			// выход каждого экземпляра = свой канал
				for (size_t i = 0; i < n; ++i) { b[i * 2] = m.out[0][i]; b[i * 2 + 1] = m.out[1][i]; }
				break;
			case MODE_STEREO_MONO:			// моно-выход на оба канала
				for (size_t i = 0; i < n; ++i) { b[i * 2] = m.out[0][i]; b[i * 2 + 1] = m.out[0][i]; }
				break;
			default:
				for (size_t i = 0; i < n; ++i) { b[i * 2] = m.out[0][i]; b[i * 2 + 1] = m.out[1][i]; }
				break;
			}
		}
		done += n;
	}
}

}	// namespace Ladspa
