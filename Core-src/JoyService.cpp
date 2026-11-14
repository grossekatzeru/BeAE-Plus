#include <StorageKit.h>
#include <String.h>
#include "JoyService.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "AudioRecorder.h"
#include "CommonPool.h"
#include "Envelope.h"
#include "Globals.h"
#include "Mixer.h"
#include "Tracks.h"

JoyService Joy;

static const float kDbMin = -60.0f, kDbMax = 6.0f;

static float GainToDb(float g) { return g <= 0.001f ? kDbMin : 20.0f * log10f(g); }
static float DbToGain(float db) { return db <= kDbMin + 0.01f ? 0.0f : powf(10.0f, db / 20.0f); }

JoyService::JoyService()
	: fStick(NULL), fAxes(0), fHats(0), fButtons(0), fHasData(false), fBad(0),
	  fLastTick(0), fLastTry(0), fWrite(false), fWrote(false), fLearn(-1), fLearnBase(false)
{
	fWriteT[0] = fWriteT[1] = fWriteT[2] = -1;
}

JoyService::~JoyService()
{
	Close();
}

std::vector<BString> JoyService::Devices()
{
	std::vector<BString> v;
	BJoystick probe;
	const int32 n = probe.CountDevices();
	for (int32 i = 0; i < n; i++){
		char name[B_OS_NAME_LENGTH];
		name[0] = 0;
		if (probe.GetDeviceName(i, name) == B_OK && name[0])
			v.push_back(BString(name));
	}
	return v;
}

void JoyService::Close()
{
	if (fStick){
		fStick->Close();
		delete fStick;
		fStick = NULL;
	}
	fAxes = fHats = fButtons = 0;
	fHasData = false;
	fLast = Ctrl::Input();
	fLastHats.clear();
	fEngine.Reset();
}

bool JoyService::Open(const char *name)
{
	Close();
	Prefs.joy_device = name ? name : "";
	fName = Prefs.joy_device;
	if (fName.Length() == 0)
		return false;
	BJoystick *s = new BJoystick();
	if (s->Open(fName.String()) < B_OK){
		delete s;
		return false;
	}
	fStick = s;
	fAxes = s->CountAxes();
	fHats = s->CountHats();
	fButtons = s->CountButtons();
	fBad = 0;
	fLastTick = 0;
	return true;
}

void JoyService::LoadFromPrefs()
{
	fMap.SetDefaults();
	if (Prefs.joy_map.Length() > 0)
		fMap.FromString(Prefs.joy_map.String());		// не разобралась - остаются привязки по умолчанию
	fMap.volRate = Prefs.joy_vol_rate;
	fMap.panRate = Prefs.joy_pan_rate;
	fMap.deadZone = Prefs.joy_dead;
}

void JoyService::SaveToPrefs()
{
	Prefs.joy_map = fMap.ToString().c_str();
	Prefs.joy_vol_rate = fMap.volRate;
	Prefs.joy_pan_rate = fMap.panRate;
	Prefs.joy_dead = fMap.deadZone;
	Prefs.Sync();
}

BString JoyService::Describe(const Ctrl::Binding &b)
{
	BString s;
	if (b.type == Ctrl::S_BUTTON){
		s << Language.get("CTRL_BUTTON") << " " << (int32)(b.index + 1);
	}else if (b.type == Ctrl::S_AXIS){
		s << Language.get("CTRL_AXIS") << " " << (int32)(b.index + 1) << (b.sign < 0 ? " -" : " +");
	}else{
		s = "-";
	}
	return s;
}

void JoyService::BeginLearn(int action, BMessenger notify)
{
	fLearn = action;
	fLearnNotify = notify;
	fLearnBase = false;			// опорные значения возьмём при ближайшем опросе
}

void JoyService::Tick()
{
	const bigtime_t now = system_time();

	if (!fStick){
		// устройство выбрано в настройках, но не открыто (отключено или ещё не искали) - пробуем раз в 2 секунды
		if (Prefs.joy_device.Length() > 0 && now - fLastTry > 2000000){
			fLastTry = now;
			const BString want = Prefs.joy_device;
			Open(want.String());
		}
		if (!fStick) return;
	}

	if (fStick->Update() < B_OK){
		fHasData = false;
		if (++fBad > 100) fEngine.Reset();
		fLastTick = now;
		return;
	}
	fBad = 0;
	fHasData = true;

	fLast.axes.assign(fAxes > 0 ? fAxes : 0, 0);
	if (fAxes > 0) fStick->GetAxisValues(&fLast.axes[0]);
	fLastHats.assign(fHats > 0 ? fHats : 0, 0);
	if (fHats > 0) fStick->GetHatValues(&fLastHats[0]);
	fLast.buttons = fStick->ButtonValues();

	const double dt = fLastTick ? (double)(now - fLastTick) / 1000000.0 : 0.0;
	fLastTick = now;

	if (fLearn >= 0){
		DoLearn();
		return;				// пока ждём привязку, ничего не управляем
	}

	Ctrl::Output out;
	fEngine.Tick(fMap, fLast, dt, out);
	Apply(out);
}

void JoyService::DoLearn()
{
	if (!fLearnBase){
		fLearnStart = fLast;
		fLearnBase = true;
		return;
	}
	Ctrl::Binding found;
	// кнопка: новая нажатая
	const uint32 fresh = fLast.buttons & ~fLearnStart.buttons;
	for (int i = 0; i < 32 && found.type == Ctrl::S_NONE; i++)
		if (fresh & (1u << i)) found = Ctrl::Binding(Ctrl::S_BUTTON, i);
	// ось: ушла от исходного положения заметно (больше ~60% хода)
	for (size_t i = 0; i < fLast.axes.size() && found.type == Ctrl::S_NONE; i++){
		const int base = i < fLearnStart.axes.size() ? fLearnStart.axes[i] : 0;
		const int d = (int)fLast.axes[i] - base;
		if (abs(d) > 19000) found = Ctrl::Binding(Ctrl::S_AXIS, (int)i, d < 0 ? -1 : 1);
	}
	if (found.type == Ctrl::S_NONE) return;

	const int action = fLearn;
	fLearn = -1;
	if (action >= 0 && action < Ctrl::A_COUNT){
		for (int a = 0; a < Ctrl::A_COUNT; a++)			// один источник - одно действие
			if (a != action && fMap.b[a] == found) fMap.b[a] = Ctrl::Binding();
		fMap.b[action] = found;
		SaveToPrefs();
	}
	fEngine.Reset();
	BMessage m(JOY_LEARNED);
	m.AddInt32("action", action);
	fLearnNotify.SendMessage(&m);
}

// Записать точку в кривую; пока рука на управлении, точки между прошлой записью и текущим моментом заменяются
static void WritePoint(Envelope &e, double &lastT, double t, float v)
{
	if (lastT >= 0 && t > lastT){
		for (int i = e.Count() - 1; i >= 0; i--){
			const double pt = e.Point(i).t;
			if (pt > lastT && pt < t) e.Remove(i);
		}
	}
	e.Add(t, v);
	lastT = t;
}

void JoyService::Apply(const Ctrl::Output &o)
{
	const int active = Mix.Active();
	if (o.writeToggle){
		SetWriting(!fWrite);
		fWrote = true;
	}
	const bool writing = fWrite && Pool.IsPlaying();
	if (writing && Mix.Track(active)){
		const double t = Mix.PlayNow();
		if (t < fWriteT[0] || t < fWriteT[1] || t < fWriteT[2])		// перемотка назад
			fWriteT[0] = fWriteT[1] = fWriteT[2] = -1;
		// громкость: кривая - добавка в дБ к ползунку (0 = как на ползунке)
		if (o.dVolDb != 0.0 || o.volReset){
			Envelope ev;
			Envelope ep;
			const int slot = o.master ? 2 : 0;
			if (o.master) Mix.GetMasterEnvelope(ev); else Mix.GetEnvelopes(active, ev, ep);
			float v = o.volReset ? 0.0f : ev.Value(t, 0.0f) + (float)o.dVolDb;
			if (v > Env::kDbMax) v = Env::kDbMax;
			if (v < Env::kDbMin) v = Env::kDbMin;
			WritePoint(ev, fWriteT[slot], t, v);
			if (o.master) Mix.SetMasterEnvelope(ev); else Mix.SetEnvelopes(active, ev, ep);
			fWrote = true;
			Pool.changed = true;
		}else{
			fWriteT[0] = -1; fWriteT[2] = -1;			// рука отпущена: следующее касание начнёт новый участок
		}
		if (o.dPan != 0.0 || o.panCenter){
			Envelope ev, ep;
			Mix.GetEnvelopes(active, ev, ep);
			float v = o.panCenter ? 0.0f : ep.Value(t, 0.0f) + (float)o.dPan;
			if (v > 1.0f) v = 1.0f;
			if (v < -1.0f) v = -1.0f;
			WritePoint(ep, fWriteT[1], t, v);
			Mix.SetEnvelopes(active, ev, ep);
			fWrote = true;
			Pool.changed = true;
		}else{
			fWriteT[1] = -1;
		}
		Ctrl::Output rest = o;							// остальное (дорожки, транспорт) работает как обычно
		rest.dVolDb = 0; rest.dPan = 0; rest.volReset = false; rest.panCenter = false;
		ApplyRest(rest);
		return;
	}
	ApplyRest(o);
}

void JoyService::ApplyRest(const Ctrl::Output &o)
{
	const int active = Mix.Active();
	if (o.dVolDb != 0.0){
		if (o.master){
			const float db = GainToDb(Mix.MasterGain()) + (float)o.dVolDb;
			Mix.SetMasterGain(DbToGain(db > kDbMax ? kDbMax : (db < kDbMin ? kDbMin : db)));
		}else if (const MixTrack *t = Mix.Track(active)){
			const float db = GainToDb(t->gain) + (float)o.dVolDb;
			Mix.SetGain(active, DbToGain(db > kDbMax ? kDbMax : (db < kDbMin ? kDbMin : db)));
		}
	}
	if (o.dPan != 0.0){
		if (const MixTrack *t = Mix.Track(active))
			Mix.SetPan(active, t->pan + (float)o.dPan);
	}
	if (o.volReset){
		if (o.master) Mix.SetMasterGain(1.0f);
		else if (Mix.Track(active)) Mix.SetGain(active, 1.0f);
	}
	if (o.panCenter && Mix.Track(active))
		Mix.SetPan(active, 0.0f);
	if ((o.trackPrev || o.trackNext) && !Recorder.IsRecording()){
		const int r = Tracks_Rank(active);
		if (r >= 0){
			const int slot = Tracks_SlotAt(r + (o.trackNext ? 1 : -1));
			if (slot >= 0) Tracks_Activate(slot);
		}
	}
	if (o.playStop && fTarget.IsValid())
		fTarget.SendMessage(Pool.IsPlaying() ? TRANSPORT_STOP : TRANSPORT_PLAY);
}
