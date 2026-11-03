#include "FxChain.h"

FxChain::FxChain() : fActive(0)
{
	fLock.clear();
}

FxChain::~FxChain()
{
	Clear();
}

void FxChain::Lock()	{ while (fLock.test_and_set(std::memory_order_acquire)) {} }
void FxChain::Unlock()	{ fLock.clear(std::memory_order_release); }

void FxChain::Recount()
{
	int n = 0;
	for (size_t i = 0; i < fSlots.size(); i++)
		if (fSlots[i].enabled && (fSlots[i].runner || fSlots[i].vst)) n++;
	fActive = n;
}

bool FxChain::Add(const Ladspa::Plugin &p, float rate, std::string *err,
				  const Ladspa::Values *init, bool enabled)
{
	Slot s;
	s.kind = KIND_LADSPA;
	s.vst = NULL;
	s.plugin = p;
	s.enabled = enabled;
	s.rate = rate;
	s.runner = new Ladspa::Runner();
	std::string e;
	if (!s.runner->Start(p, rate, 2, &e)) {
		delete s.runner;
		if (err) *err = e;
		return false;
	}
	// значения: по умолчанию, поверх - сохранённые
	std::vector<int> ctl = p.ControlInputs();
	for (size_t k = 0; k < ctl.size(); k++) {
		Ladspa::Range r = Ladspa::ResolveRange(p.ports[ctl[k]], rate);
		float v = r.def;
		if (init) {
			Ladspa::Values::const_iterator it = init->find(ctl[k]);
			if (it != init->end()) v = it->second;
		}
		s.values[ctl[k]] = v;
		s.runner->SetControl(ctl[k], v);
	}
	Lock();
	fSlots.push_back(s);
	Recount();
	Unlock();
	return true;
}

bool FxChain::AddVst(const std::string &file, float rate, std::string *err,
					 const Ladspa::Values *init, bool enabled)
{
	Vst2::Instance *v = new Vst2::Instance();
	if (!v->Load(file, err) || !v->Start(rate, 8192)) {
		if (err && err->empty()) *err = "this plugin cannot be used as an effect";
		delete v;
		return false;
	}
	Slot s;
	s.kind = KIND_VST2;
	s.runner = NULL;
	s.vst = v;
	s.enabled = enabled;
	s.rate = rate;
	s.plugin.file = file;
	s.plugin.label = "vst2";
	s.plugin.name = v->GetInfo().name;
	for (int k = 0; k < v->NumParams(); k++) {
		float val = v->GetParam(k);
		if (init) {
			Ladspa::Values::const_iterator it = init->find(k);
			if (it != init->end()) { val = it->second; v->SetParam(k, val); }
		}
		s.values[k] = val;
	}
	Lock();
	fSlots.push_back(s);
	Recount();
	Unlock();
	return true;
}

bool FxChain::AddCopyOf(const Slot &s, float rate, std::string *err)
{
	if (s.kind == KIND_VST2) return AddVst(s.plugin.file, rate, err, &s.values, true);
	return Add(s.plugin, rate, err, &s.values, true);
}

void FxChain::Remove(int i)
{
	if (i < 0 || i >= Count()) return;
	Ladspa::Runner *old;
	Vst2::Instance *oldv;
	Lock();
	old = fSlots[i].runner;
	oldv = fSlots[i].vst;
	fSlots.erase(fSlots.begin() + i);
	Recount();
	Unlock();
	delete old;
	delete oldv;
}

void FxChain::Clear()
{
	std::vector<Slot> old;
	Lock();
	old.swap(fSlots);
	fActive = 0;
	Unlock();
	for (size_t i = 0; i < old.size(); i++) { delete old[i].runner; delete old[i].vst; }
}

bool FxChain::Move(int i, int delta)
{
	int j = i + delta;
	if (i < 0 || j < 0 || i >= Count() || j >= Count() || delta == 0) return false;
	Lock();
	Slot t = fSlots[i];
	fSlots[i] = fSlots[j];
	fSlots[j] = t;
	Unlock();
	return true;
}

void FxChain::SetEnabled(int i, bool on)
{
	if (i < 0 || i >= Count()) return;
	Lock();
	fSlots[i].enabled = on;
	Recount();
	Unlock();
}

void FxChain::SetValue(int i, int port, float v)
{
	if (i < 0 || i >= Count()) return;
	fSlots[i].values[port] = v;				// читает только интерфейс
	if (fSlots[i].runner) fSlots[i].runner->SetControl(port, v);
	if (fSlots[i].vst) fSlots[i].vst->SetParam(port, v);
}

void FxChain::Process(float *buf, int frames, float rate)
{
	if (fActive.load() == 0 || frames <= 0) return;
	if (fLock.test_and_set(std::memory_order_acquire)) return;	// заняли - пропускаем буфер
	for (size_t i = 0; i < fSlots.size(); i++) {
		Slot &s = fSlots[i];
		if (!s.enabled) continue;
		if (s.rate != rate) continue;		// частота выхода сменилась - плагин не трогаем
		if (s.vst) { if (s.vst->IsRunning()) s.vst->Process(buf, (size_t)frames); }
		else if (s.runner && s.runner->IsRunning()) s.runner->Process(buf, (size_t)frames);
	}
	Unlock();
}
