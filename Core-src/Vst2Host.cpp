#include "Vst2Host.h"
#include <dlfcn.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

namespace Vst2 {

// ---- ABI VST 2.4 ----
struct AEffect {
	int32_t		magic;
	intptr_t	(*dispatcher)(AEffect*, int32_t opcode, int32_t index, intptr_t value, void *ptr, float opt);
	void		(*process)(AEffect*, float **in, float **out, int32_t n);
	void		(*setParameter)(AEffect*, int32_t index, float value);
	float		(*getParameter)(AEffect*, int32_t index);
	int32_t		numPrograms, numParams, numInputs, numOutputs, flags;
	intptr_t	resvd1, resvd2;
	int32_t		initialDelay, realQualities, offQualities;
	float		ioRatio;
	void		*object, *user;
	int32_t		uniqueID, version;
	void		(*processReplacing)(AEffect*, float **in, float **out, int32_t n);
};

enum {
	kMagic = 0x56737450,			// 'VstP'
	effOpen = 0, effClose = 1, effGetParamLabel = 6, effGetParamDisplay = 7, effGetParamName = 8,
	effSetSampleRate = 10, effSetBlockSize = 11, effMainsChanged = 12,
	effGetEffectName = 45, effGetVendorString = 47, effGetProductString = 48,
	flagCanReplacing = 1 << 4, flagIsSynth = 1 << 8
};

typedef intptr_t (*HostCb)(AEffect*, int32_t, int32_t, intptr_t, void*, float);
typedef AEffect *(*EntryFn)(HostCb);

static intptr_t HostCallback(AEffect*, int32_t opcode, int32_t, intptr_t, void *ptr, float)
{
	switch (opcode) {
	case 1:	return 2400;										// audioMasterVersion
	case 32: if (ptr) { strcpy((char*)ptr, "BeAE"); return 1; } return 0;	// vendor
	case 33: if (ptr) { strcpy((char*)ptr, "BeAE"); return 1; } return 0;	// product
	case 34: return 1;											// vendor version
	default: return 0;
	}
}

static std::string Trim(const char *s)
{
	std::string r(s);
	size_t a = r.find_first_not_of(" \t\r\n");
	if (a == std::string::npos) return "";
	size_t b = r.find_last_not_of(" \t\r\n");
	return r.substr(a, b - a + 1);
}

struct Instance::Impl {
	void		*lib;
	AEffect		*fx;
	float		*in[8], *out[8];
	int			nIn, nOut;			// число буферов
	int			maxBlock;
	Impl() : lib(NULL), fx(NULL), nIn(0), nOut(0), maxBlock(0)
	{
		for (int i = 0; i < 8; i++) in[i] = out[i] = NULL;
	}
	std::string Str(int opcode, int index) const
	{
		char b[256]; memset(b, 0, sizeof(b));
		fx->dispatcher(fx, opcode, index, 0, b, 0);
		return Trim(b);
	}
};

Instance::Instance() : fImpl(new Impl), fRunning(false)
{
	fInfo.numParams = fInfo.numIn = fInfo.numOut = 0;
	fInfo.synth = false;
}

Instance::~Instance()
{
	Stop();
	if (fImpl->fx) fImpl->fx->dispatcher(fImpl->fx, effClose, 0, 0, NULL, 0);
	if (fImpl->lib) dlclose(fImpl->lib);
	for (int i = 0; i < 8; i++) { free(fImpl->in[i]); free(fImpl->out[i]); }
	delete fImpl;
}

bool Instance::Load(const std::string &path, std::string *err)
{
	Impl &m = *fImpl;
	if (m.lib) { if (err) *err = "already loaded"; return false; }
	m.lib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
	if (!m.lib) { if (err) { const char *e = dlerror(); *err = e ? e : "cannot load"; } return false; }
	EntryFn entry = (EntryFn)dlsym(m.lib, "VSTPluginMain");
	if (!entry) entry = (EntryFn)dlsym(m.lib, "main_plugin");		// так называется точка входа в Haiku
	if (!entry) entry = (EntryFn)dlsym(m.lib, "main");
	if (!entry) { if (err) *err = "no VST entry point"; dlclose(m.lib); m.lib = NULL; return false; }
	m.fx = entry(HostCallback);
	if (!m.fx || m.fx->magic != kMagic) {
		if (err) *err = "not a VST 2 plugin";
		m.fx = NULL; dlclose(m.lib); m.lib = NULL;
		return false;
	}
	m.fx->dispatcher(m.fx, effOpen, 0, 0, NULL, 0);
	fInfo.file = path;
	fInfo.name = m.Str(effGetEffectName, 0);
	if (fInfo.name.empty()) {
		size_t p = path.find_last_of('/');
		fInfo.name = (p == std::string::npos) ? path : path.substr(p + 1);
	}
	fInfo.vendor = m.Str(effGetVendorString, 0);
	fInfo.product = m.Str(effGetProductString, 0);
	fInfo.numParams = m.fx->numParams;
	fInfo.numIn = m.fx->numInputs;
	fInfo.numOut = m.fx->numOutputs;
	fInfo.synth = (m.fx->flags & flagIsSynth) != 0 || m.fx->numInputs <= 0;
	return true;
}

bool Instance::Start(float rate, int maxBlock)
{
	Impl &m = *fImpl;
	if (!m.fx || fRunning || maxBlock < 16) return false;
	if (fInfo.synth) return false;
	m.maxBlock = maxBlock;
	m.nIn = fInfo.numIn > 8 ? 8 : (fInfo.numIn < 2 ? 2 : fInfo.numIn);
	m.nOut = fInfo.numOut > 8 ? 8 : (fInfo.numOut < 2 ? 2 : fInfo.numOut);
	for (int i = 0; i < m.nIn; i++) { free(m.in[i]); m.in[i] = (float*)calloc(maxBlock, sizeof(float)); if (!m.in[i]) return false; }
	for (int i = 0; i < m.nOut; i++) { free(m.out[i]); m.out[i] = (float*)calloc(maxBlock, sizeof(float)); if (!m.out[i]) return false; }
	m.fx->dispatcher(m.fx, effSetSampleRate, 0, 0, NULL, rate);
	m.fx->dispatcher(m.fx, effSetBlockSize, 0, maxBlock, NULL, 0);
	m.fx->dispatcher(m.fx, effMainsChanged, 0, 1, NULL, 0);
	fRunning = true;
	return true;
}

void Instance::Stop()
{
	if (fRunning && fImpl->fx)
		fImpl->fx->dispatcher(fImpl->fx, effMainsChanged, 0, 0, NULL, 0);
	fRunning = false;
}

std::string Instance::ParamName(int i) const	{ return fImpl->fx ? fImpl->Str(effGetParamName, i) : ""; }
std::string Instance::ParamUnit(int i) const	{ return fImpl->fx ? fImpl->Str(effGetParamLabel, i) : ""; }
std::string Instance::ParamDisplay(int i) const	{ return fImpl->fx ? fImpl->Str(effGetParamDisplay, i) : ""; }
float Instance::GetParam(int i) const			{ return (fImpl->fx && i >= 0 && i < fInfo.numParams) ? fImpl->fx->getParameter(fImpl->fx, i) : 0; }
void Instance::SetParam(int i, float v)
{
	if (!fImpl->fx || i < 0 || i >= fInfo.numParams) return;
	if (v < 0) v = 0;
	if (v > 1) v = 1;
	fImpl->fx->setParameter(fImpl->fx, i, v);
}

void Instance::Process(float *buf, size_t frames)
{
	Impl &m = *fImpl;
	if (!fRunning || !m.fx) return;
	const bool replacing = (m.fx->flags & flagCanReplacing) && m.fx->processReplacing;
	for (size_t from = 0; from < frames; from += (size_t)m.maxBlock) {
		const int n = (int)((frames - from < (size_t)m.maxBlock) ? frames - from : (size_t)m.maxBlock);
		float *b = buf + from * 2;
		if (fInfo.numIn >= 2) {
			for (int k = 0; k < n; k++) { m.in[0][k] = b[2*k]; m.in[1][k] = b[2*k+1]; }
		} else {
			for (int k = 0; k < n; k++) m.in[0][k] = 0.5f * (b[2*k] + b[2*k+1]);
		}
		for (int c = 2; c < m.nIn; c++) memset(m.in[c], 0, n * sizeof(float));
		for (int c = 0; c < m.nOut; c++) memset(m.out[c], 0, n * sizeof(float));
		if (replacing) m.fx->processReplacing(m.fx, m.in, m.out, n);
		else m.fx->process(m.fx, m.in, m.out, n);
		if (fInfo.numOut >= 2) {
			for (int k = 0; k < n; k++) { b[2*k] = m.out[0][k]; b[2*k+1] = m.out[1][k]; }
		} else {
			for (int k = 0; k < n; k++) b[2*k] = b[2*k+1] = m.out[0][k];
		}
	}
}

// ---- поиск ----
std::vector<std::string> DefaultPaths()
{
	std::vector<std::string> d;
	const char *env = getenv("VST_PATH");
	if (env) {
		std::string s(env);
		size_t a = 0;
		while (a <= s.size()) {
			size_t b = s.find(':', a);
			if (b == std::string::npos) b = s.size();
			if (b > a) d.push_back(s.substr(a, b - a));
			a = b + 1;
		}
	}
	d.push_back("/boot/system/add-ons/media/vstplugins");
	d.push_back("/boot/system/non-packaged/add-ons/media/vstplugins");
	const char *home = getenv("HOME");
	if (home) {
		d.push_back(std::string(home) + "/config/non-packaged/add-ons/media/vstplugins");
		d.push_back(std::string(home) + "/config/add-ons/media/vstplugins");
	}
	return d;
}

static void ScanDir(const std::string &dir, std::vector<Info> &out, std::vector<std::string> *log, int depth)
{
	DIR *d = opendir(dir.c_str());
	if (!d) return;
	while (struct dirent *e = readdir(d)) {
		if (e->d_name[0] == '.') continue;
		std::string p = dir + "/" + e->d_name;
		struct stat st;
		if (stat(p.c_str(), &st) != 0) continue;
		if (S_ISDIR(st.st_mode)) {
			if (depth < 3) ScanDir(p, out, log, depth + 1);
			continue;
		}
		if (!S_ISREG(st.st_mode)) continue;
		bool dup = false;
		for (size_t i = 0; i < out.size(); i++) if (out[i].file == p) dup = true;
		if (dup) continue;
		Instance inst;
		std::string err;
		if (inst.Load(p, &err)) out.push_back(inst.GetInfo());
		else if (log) log->push_back(p + ": " + err);
	}
	closedir(d);
}

std::vector<Info> Scan(const std::vector<std::string> &dirs, std::vector<std::string> *log)
{
	std::vector<Info> out;
	for (size_t i = 0; i < dirs.size(); i++) ScanDir(dirs[i], out, log, 0);
	return out;
}

}	// namespace Vst2
