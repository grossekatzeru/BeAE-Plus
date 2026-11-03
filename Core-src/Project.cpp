#include <StorageKit.h>
#include <Window.h>
#include <View.h>
#include <Button.h>
#include <TextControl.h>
#include <Alert.h>
#include <Message.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "CommonPool.h"
#include "Globals.h"
#include "Mixer.h"
#include "Tracks.h"
#include "ProjectIO.h"
#include "Project.h"

static BString	gName;
static BString	gFolder;		// полный путь к папке проекта (с подпапкой)

bool Project_HasFolder()	{ return gName.Length() > 0 && gFolder.Length() > 0; }
const char *Project_Name()	{ return gName.String(); }
void Project_Forget()		{ gName = ""; gFolder = ""; }

BString Project_Title(const char *item)
{
	BString t("BeAE - ");
	if (gName.Length() > 0){
		t << gName;
		if (item && *item) t << " - " << item;
	}else{
		t << ((item && *item) ? item : Language.get("UNTITLED"));
	}
	return t;
}

// имя без символов, недопустимых в имени файла
static BString Sanitize(const char *s)
{
	BString r;
	for (const char *p = s; *p; p++){
		unsigned char c = (unsigned char)*p;
		if (c == '/' || c < 0x20 || c == ':' || c == '\\' || c == '"') r << '_';
		else r.Append((const char*)p, 1);
	}
	r.Trim();
	while (r.Length() > 0 && r[0] == '.') r.RemoveFirst(".");
	if (r.Length() == 0) r = "Project";
	if (r.Length() > 100) { r.Truncate(100); }
	return r;
}

static BString Num(double v)
{
	char b[48];
	snprintf(b, sizeof(b), "%.9g", v);
	return BString(b);
}

static bool WriteText(const BString &path, const BString &text)
{
	BFile f(path.String(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (f.InitCheck() != B_OK) return false;
	return f.Write(text.String(), text.Length()) == (ssize_t)text.Length();
}

static bool DoSave(const BPath &parent, const char *name, BString &err)
{
	BString proj = Sanitize(name);
	BPath folder(parent.Path(), proj.String());
	if (create_directory(folder.Path(), 0755) != B_OK){
		err = "Cannot create the folder: "; err << folder.Path();
		return false;
	}

	BString json;
	json << "{\n  \"format\": \"BeMIX\",\n  \"version\": 1,\n  \"name\": \"" << JsonEscape(name).c_str() << "\",\n";
	json << "  \"active\": " << Mix.Active() << ",\n  \"tracks\": [\n";

	bool first = true;
	for (int i = 0; i < MIX_MAX_TRACKS; i++){
		MixTrack *t = Mix.Track(i);
		if (!t) continue;

		const float *data = NULL; int64 frames = 0; int ch = 2; float rate = 44100; double offset = 0;
		BString file;
		if (Tracks_Snapshot(i, data, frames, ch, rate, offset)){
			char prefix[16];
			snprintf(prefix, sizeof(prefix), "%02d_", i + 1);
			file = prefix;
			file << Sanitize(t->name) << ".wav";
			BPath wav(folder.Path(), file.String());
			if (!WavWriteFloat(wav.Path(), data, frames, ch, rate)){
				err = "Cannot write "; err << wav.Path();
				return false;
			}
		}else{
			ch = t->channels; rate = t->rate;
		}

		if (!first) json << ",\n";
		first = false;
		json << "    {\n";
		json << "      \"name\": \"" << JsonEscape(t->name).c_str() << "\",\n";
		if (file.Length() > 0) json << "      \"file\": \"" << JsonEscape(file.String()).c_str() << "\",\n";
		else json << "      \"file\": null,\n";
		json << "      \"offset\": " << Num(offset) << ",\n";
		json << "      \"gain\": " << Num(t->gain) << ",\n";
		json << "      \"pan\": " << Num(t->pan) << ",\n";
		json << "      \"mute\": " << (t->mute ? "true" : "false") << ",\n";
		json << "      \"solo\": " << (t->solo ? "true" : "false") << ",\n";
		json << "      \"channels\": " << ch << ",\n";
		json << "      \"rate\": " << Num(rate) << ",\n";
		json << "      \"color\": null,\n";
		json << "      \"effects\": []\n";
		json << "    }";
	}
	json << "\n  ]\n}\n";

	BString descName(proj);
	descName << ".bemix";
	BPath desc(folder.Path(), descName.String());
	if (!WriteText(desc.Path(), json)){
		err = "Cannot write "; err << desc.Path();
		return false;
	}
	gName = name;
	gFolder = folder.Path();
	Pool.changed = false;			// всё уже лежит в проекте
	return true;
}

bool Project_Save(const BPath &dir, const char *name, BString &err)
{
	if (!name || !*name){ err = "Empty project name"; return false; }
	return DoSave(dir, name, err);
}

bool Project_SaveAgain(BString &err)
{
	if (!Project_HasFolder()){ err = "No project folder"; return false; }
	BPath folder(gFolder.String());
	BPath parent;
	if (folder.GetParent(&parent) != B_OK){ err = "Bad project folder"; return false; }
	return DoSave(parent, gName.String(), err);
}

bool Project_Open(const char *bemixPath, BString &err)
{
	BFile f(bemixPath, B_READ_ONLY);
	if (f.InitCheck() != B_OK){ err = "Cannot open the project file"; return false; }
	off_t size = 0;
	f.GetSize(&size);
	if (size <= 0 || size > 16 * 1024 * 1024){ err = "Bad project file"; return false; }
	std::string text((size_t)size, '\0');
	if (f.Read(&text[0], (size_t)size) != (ssize_t)size){ err = "Cannot read the project file"; return false; }

	JValue doc; std::string perr;
	if (!JsonParse(text, doc, perr)){ err = "Project file: "; err << perr.c_str(); return false; }
	if (doc.String("format") != "BeMIX"){ err = "This is not a BeMIX project"; return false; }
	const JValue *tracks = doc.Get("tracks");
	if (!tracks || tracks->type != JValue::Arr || tracks->a.empty()){ err = "The project has no tracks"; return false; }

	BPath desc(bemixPath), folder;
	desc.GetParent(&folder);

	Tracks_BeginProject();
	BString warnings;
	int slot = 0;
	for (size_t k = 0; k < tracks->a.size() && k < MIX_MAX_TRACKS; k++){
		const JValue &jt = tracks->a[k];
		std::string tname = jt.String("name", "");
		if (slot > 0){
			if (Mix.AddEmptyTrack(tname.c_str(), 2) != slot) break;
		}
		MixTrack *t = Mix.Track(slot);
		if (!t) break;
		if (!tname.empty()) Tracks_SetName(slot, tname.c_str());
		Mix.SetGain(slot, (float)jt.Number("gain", 1.0));
		Mix.SetPan(slot, (float)jt.Number("pan", 0.0));
		Mix.SetMute(slot, jt.Boolean("mute", false));
		Mix.SetSolo(slot, jt.Boolean("solo", false));

		const JValue *jf = jt.Get("file");
		if (jf && jf->type == JValue::Str && !jf->s.empty()){
			BPath wav(folder.Path(), jf->s.c_str());
			std::vector<float> samples; long long frames = 0; int ch = 2; float rate = 44100; std::string werr;
			if (WavRead(wav.Path(), samples, frames, ch, rate, werr) && frames > 0){
				Mix.SetTrackData(slot, &samples[0], frames, ch, rate);
				Mix.SetOffset(slot, jt.Number("offset", 0.0));
			}else{
				warnings << jf->s.c_str() << ": " << werr.c_str() << "\n";
			}
		}
		slot++;
	}

	BString name(doc.String("name", "").c_str());
	if (name.Length() == 0){ BString leaf(desc.Leaf()); leaf.RemoveLast(".bemix"); name = leaf; }
	gName = name;
	gFolder = folder.Path();
	Tracks_FinishProject();
	if (warnings.Length() > 0) err = warnings;
	return true;
}

// --------------------------------------------------------------- сведение

bool Project_MixdownDefault(BPath &out)
{
	if (!Project_HasFolder()) return false;
	BString n = Sanitize(gName.String());
	n << ".wav";
	return out.SetTo(gFolder.String(), n.String()) == B_OK;
}

bool Project_Mixdown(const char *path, BString &err)
{
	struct Src { const float *d; int64 n; int ch; float rate; double off; float gl, gr; };
	std::vector<Src> src;
	bool anySolo = false;
	for (int i = 0; i < MIX_MAX_TRACKS; i++){
		MixTrack *t = Mix.Track(i);
		if (t && t->solo) anySolo = true;
	}
	float outRate = 0;
	double endTime = 0;
	for (int i = 0; i < MIX_MAX_TRACKS; i++){
		MixTrack *t = Mix.Track(i);
		if (!t) continue;
		Src s;
		if (!Tracks_Snapshot(i, s.d, s.n, s.ch, s.rate, s.off)) continue;
		if (t->mute || (anySolo && !t->solo)) continue;
		s.gl = t->gain * (t->pan > 0 ? 1.0f - t->pan : 1.0f);
		s.gr = t->gain * (t->pan < 0 ? 1.0f + t->pan : 1.0f);
		src.push_back(s);
		if (s.rate > outRate) outRate = s.rate;
		double e = s.off + (double)s.n / s.rate;
		if (e > endTime) endTime = e;
	}
	if (src.empty() || outRate <= 0){ err = Language.get("MIXDOWN_EMPTY"); return false; }

	const int64 total = (int64)(endTime * outRate + 0.5) + 1;
	if (total * 8 > 0xFFFFFF00LL){ err = "The mixdown is too long for a WAV file"; return false; }
	std::vector<float> out;
	try { out.assign((size_t)total * 2, 0.0f); } catch (...) { err = "Not enough memory"; return false; }

	for (size_t k = 0; k < src.size(); k++){
		const Src &s = src[k];
		const int64 n0 = (int64)(s.off * outRate + 0.5);
		const int64 n1 = n0 + (int64)((double)s.n * outRate / s.rate);
		const double step = (double)s.rate / outRate;
		for (int64 n = n0; n < n1 && n < total; n++){
			double pos = (n - s.off * outRate) * step;
			if (pos < 0) pos = 0;
			int64 i0 = (int64)pos;
			if (i0 >= s.n) break;
			int64 i1 = (i0 + 1 < s.n) ? i0 + 1 : i0;
			float f = (float)(pos - (double)i0);
			float l, r;
			if (s.ch == 2){
				l = s.d[i0*2]   + (s.d[i1*2]   - s.d[i0*2])   * f;
				r = s.d[i0*2+1] + (s.d[i1*2+1] - s.d[i0*2+1]) * f;
			}else{
				l = r = s.d[i0] + (s.d[i1] - s.d[i0]) * f;
			}
			out[(size_t)n*2]   += l * s.gl;
			out[(size_t)n*2+1] += r * s.gr;
		}
	}
	if (!WavWriteFloat(path, &out[0], total, 2, outRate)){
		err = "Cannot write "; err << path;
		return false;
	}
	return true;
}

// --------------------------------------------------------------- окно имени

ProjectNameWindow::ProjectNameWindow(BWindow *target, uint32 msgWhat, const char *defaultName)
	: BWindow(BRect(0, 0, 299, 89), Language.get("PROJECT_NAME_TITLE"), B_TITLED_WINDOW_LOOK,
		B_MODAL_APP_WINDOW_FEEL, B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_ASYNCHRONOUS_CONTROLS),
	  fTarget(target), fWhat(msgWhat)
{
	BView *bg = new BView(Bounds(), "bg", B_FOLLOW_ALL, B_WILL_DRAW);
	bg->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	AddChild(bg);
	fText = new BTextControl(BRect(12, 12, 287, 32), "name", Language.get("PROJECT_NAME"), defaultName, NULL);
	fText->SetDivider(100);
	bg->AddChild(fText);
	BButton *ok = new BButton(BRect(210, 52, 287, 76), "ok", Language.get("OK"), new BMessage('pnok'));
	ok->MakeDefault(true);
	bg->AddChild(ok);
	bg->AddChild(new BButton(BRect(128, 52, 204, 76), "cancel", Language.get("CANCEL"), new BMessage('pncn')));
	if (target) MoveTo(target->Frame().left + 60, target->Frame().top + 120);
	fText->MakeFocus(true);
	fText->TextView()->SelectAll();
}

void ProjectNameWindow::MessageReceived(BMessage *msg)
{
	switch (msg->what){
	case 'pnok': {
		BString n(fText->Text());
		n.Trim();
		if (n.Length() > 0 && fTarget){
			BMessage m(fWhat);
			m.AddString("name", n.String());
			fTarget->PostMessage(&m);
		}
		PostMessage(B_QUIT_REQUESTED);
		break;
	}
	case 'pncn':
		PostMessage(B_QUIT_REQUESTED);
		break;
	default:
		BWindow::MessageReceived(msg);
	}
}
