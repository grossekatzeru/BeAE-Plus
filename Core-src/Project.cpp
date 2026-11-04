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
#include <set>
#include <map>

#include "CommonPool.h"
#include "Globals.h"
#include "Mixer.h"
#include "Tracks.h"
#include "ProjectIO.h"
#include "Project.h"
#include "LadspaFilter.h"
#include "FxChain.h"

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

	// источники (WAV-файлы) и дорожки с клипами
	struct SrcFile { const MixSource *p; int id; };
	std::vector<SrcFile> srcs;
	std::set<std::string> usedNames;
	BString srcJson, trk;
	bool firstTrack = true;
	for (int i = 0; i < MIX_MAX_TRACKS; i++){
		MixTrack *t = Mix.Track(i);
		if (!t) continue;

		std::vector<MixClip> clips = Tracks_LaneClips(i);
		BString clipsJson;
		for (size_t k = 0; k < clips.size(); k++){
			const MixClip &c = clips[k];
			int id = 0;
			for (size_t q = 0; q < srcs.size(); q++)
				if (srcs[q].p == c.src.get()) id = srcs[q].id;
			if (id == 0){
				id = (int)srcs.size() + 1;
				char prefix[16];
				snprintf(prefix, sizeof(prefix), "%02d_", i + 1);
				BString base(prefix);
				base << Sanitize(t->name);
				BString file(base);
				for (int n = 2; usedNames.count(std::string(file.String()) + ".wav") > 0; n++){
					file = base;
					file << "_" << n;
				}
				file << ".wav";
				usedNames.insert(file.String());
				BPath wav(folder.Path(), file.String());
				if (!WavWriteFloat(wav.Path(), c.src->data, c.src->frames, c.src->channels, c.src->rate)){
					err = "Cannot write "; err << wav.Path();
					return false;
				}
				SrcFile sf; sf.p = c.src.get(); sf.id = id;
				srcs.push_back(sf);
				if (srcJson.Length() > 0) srcJson << ",\n";
				srcJson << "    {\"id\": " << id << ", \"file\": \"" << JsonEscape(file.String()).c_str()
					<< "\", \"channels\": " << c.src->channels << ", \"rate\": " << Num(c.src->rate)
					<< ", \"frames\": " << (int)c.src->frames << "}";
			}
			clipsJson << (k ? ",\n" : "\n") << "        {\"source\": " << id << ", \"start\": " << (int)c.start
				<< ", \"length\": " << (int)c.len << ", \"pos\": " << Num(c.pos) << "}";
		}

		if (!firstTrack) trk << ",\n";
		firstTrack = false;
		trk << "    {\n";
		trk << "      \"name\": \"" << JsonEscape(t->name).c_str() << "\",\n";
		trk << "      \"gain\": " << Num(t->gain) << ",\n";
		trk << "      \"pan\": " << Num(t->pan) << ",\n";
		trk << "      \"mute\": " << (t->mute ? "true" : "false") << ",\n";
		trk << "      \"solo\": " << (t->solo ? "true" : "false") << ",\n";
		trk << "      \"grid\": " << (t->grid ? "true" : "false") << ",\n";
		trk << "      \"color\": null,\n";
		trk << "      \"clips\": [" << clipsJson;
		if (clips.size() > 0) trk << "\n      ";
		trk << "],\n";
		trk << "      \"effects\": [";
		{
			FxChain &fx = Mix.Fx(i);
			for (int e = 0; e < fx.Count(); e++){
				const FxChain::Slot *s = fx.At(e);
				trk << (e ? ",\n" : "\n") << "        {\"type\": \"" << (s->kind == FxChain::KIND_VST2 ? "vst2" : "ladspa") << "\", \"name\": \"" << JsonEscape(s->plugin.name).c_str()
					<< "\", \"file\": \"" << JsonEscape(s->plugin.file).c_str()
					<< "\", \"label\": \"" << JsonEscape(s->plugin.label).c_str()
					<< "\", \"enabled\": " << (s->enabled ? "true" : "false") << ", \"values\": {";
				bool fv = true;
				for (Ladspa::Values::const_iterator it = s->values.begin(); it != s->values.end(); ++it){
					if (!fv) trk << ", ";
					fv = false;
					trk << "\"" << it->first << "\": " << Num(it->second);
				}
				trk << "}}";
			}
			if (fx.Count() > 0) trk << "\n      ";
		}
		trk << "]\n";
		trk << "    }";
	}

	BString json;
	json << "{\n  \"format\": \"BeMIX\",\n  \"version\": 2,\n  \"name\": \"" << JsonEscape(name).c_str() << "\",\n";
	json << "  \"active\": " << Mix.Active() << ",\n";
	json << "  \"bpm\": " << Num(Mix.Bpm()) << ",\n  \"grid_div\": " << Mix.GridDiv() << ",\n";
	json << "  \"sources\": [\n" << srcJson << "\n  ],\n";
	json << "  \"tracks\": [\n" << trk << "\n  ]\n}\n";

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
	Mix.SetBpm(doc.Number("bpm", 120.0));
	Mix.SetGridDiv((int)doc.Number("grid_div", 4));
	BString warnings;
	std::map<int, SourceRef> sources;			// id -> источник (версия 2)
	const JValue *jsrc = doc.Get("sources");
	if (jsrc && jsrc->type == JValue::Arr){
		for (size_t q = 0; q < jsrc->a.size(); q++){
			const JValue &sj = jsrc->a[q];
			std::string file = sj.String("file");
			if (file.empty()) continue;
			BPath wav(folder.Path(), file.c_str());
			std::vector<float> samples; long long frames = 0; int ch = 2; float rate = 44100; std::string werr;
			if (WavRead(wav.Path(), samples, frames, ch, rate, werr) && frames > 0){
				sources[(int)sj.Number("id", -1)] = MakeSource(&samples[0], frames, ch, rate);
			}else{
				warnings << file.c_str() << ": " << werr.c_str() << "\n";
			}
		}
	}
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
		Mix.SetGrid(slot, jt.Boolean("grid", false));

		// клипы: версия 2 - список клипов со ссылками на источники; версия 1 - один файл и смещение
		const JValue *jc = jt.Get("clips");
		if (jc && jc->type == JValue::Arr){
			for (size_t q = 0; q < jc->a.size(); q++){
				const JValue &cj = jc->a[q];
				std::map<int, SourceRef>::iterator it = sources.find((int)cj.Number("source", -1));
				if (it == sources.end() || !it->second) continue;
				const int64 start = (int64)cj.Number("start", 0);
				int64 len = (int64)cj.Number("length", (double)it->second->frames);
				if (start < 0 || start >= it->second->frames) continue;
				if (start + len > it->second->frames) len = it->second->frames - start;
				Mix.AddClip(slot, it->second, start, len, cj.Number("pos", 0.0));
			}
		}else{
			const JValue *jf = jt.Get("file");
			if (jf && jf->type == JValue::Str && !jf->s.empty()){
				BPath wav(folder.Path(), jf->s.c_str());
				std::vector<float> samples; long long frames = 0; int ch = 2; float rate = 44100; std::string werr;
				if (WavRead(wav.Path(), samples, frames, ch, rate, werr) && frames > 0){
					SourceRef src = MakeSource(&samples[0], frames, ch, rate);
					if (src) Mix.AddClip(slot, src, 0, frames, jt.Number("offset", 0.0));
				}else{
					warnings << jf->s.c_str() << ": " << werr.c_str() << "\n";
				}
			}
		}
		// эффекты дорожки
		const JValue *jfx = jt.Get("effects");
		if (jfx && jfx->type == JValue::Arr){
			const float sysRate = Pool.system_frequency > 0 ? Pool.system_frequency : 44100.0f;
			for (size_t e = 0; e < jfx->a.size(); e++){
				const JValue &je = jfx->a[e];
				std::string file = je.String("file"), label = je.String("label"), pname = je.String("name");
				const bool isVst = je.String("type") == "vst2";
				const Ladspa::Plugin *pl = NULL;
				if (!isVst){
					pl = LadspaFind(file.c_str(), label.c_str());
					if (!pl){		// файл мог переехать: ищем по метке
						const std::vector<Ladspa::Plugin> &all = LadspaList();
						for (size_t q = 0; q < all.size() && !pl; q++)
							if (all[q].label == label && all[q].name == pname) pl = &all[q];
					}
					if (!pl){
						warnings << Language.get("FX_NOT_FOUND") << " " << pname.c_str() << "\n";
						continue;
					}
				}
				Ladspa::Values vals;
				const JValue *jv = je.Get("values");
				if (jv && jv->type == JValue::Obj)
					for (size_t q = 0; q < jv->o.size(); q++)
						if (jv->o[q].second.type == JValue::Num)
							vals[atoi(jv->o[q].first.c_str())] = (float)jv->o[q].second.n;
				std::string ferr;
				bool added = isVst ? Mix.Fx(slot).AddVst(file, sysRate, &ferr, &vals, je.Boolean("enabled", true))
					: Mix.Fx(slot).Add(*pl, sysRate, &ferr, &vals, je.Boolean("enabled", true));
				if (!added)
					warnings << pname.c_str() << ": " << ferr.c_str() << "\n";
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

// Добавляет звук клипа в стерео-буфер dst (total кадров на частоте outRate) с множителями gl, gr.
static void RenderClipOffline(const MixClip &c, float *dst, int64 total, float outRate, float gl, float gr)
{
	if (!c.src || c.len <= 0) return;
	const double rate = c.src->rate;
	const int ch = c.src->channels;
	const float *d = c.src->data + c.start * ch;
	const int64 n0 = (int64)(c.pos * outRate + 0.5);
	const int64 n1 = n0 + (int64)((double)c.len * outRate / rate);
	const double step = rate / outRate;
	for (int64 n = n0; n < n1 && n < total; n++){
		double pos = (n - c.pos * outRate) * step;
		if (pos < 0) pos = 0;
		int64 i0 = (int64)pos;
		if (i0 >= c.len) break;
		int64 i1 = (i0 + 1 < c.len) ? i0 + 1 : i0;
		float f = (float)(pos - (double)i0);
		float l, r;
		if (ch == 2){
			l = d[i0*2]   + (d[i1*2]   - d[i0*2])   * f;
			r = d[i0*2+1] + (d[i1*2+1] - d[i0*2+1]) * f;
		}else{
			l = r = d[i0] + (d[i1] - d[i0]) * f;
		}
		dst[(size_t)n*2]   += l * gl;
		dst[(size_t)n*2+1] += r * gr;
	}
}

bool Project_Mixdown(const char *path, BString &err)
{
	struct Lane { int idx; std::vector<MixClip> clips; float gl, gr; bool fx; };
	std::vector<Lane> lanes;
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
		if (t->mute || (anySolo && !t->solo)) continue;
		Lane l;
		l.idx = i;
		l.clips = Tracks_LaneClips(i);
		if (l.clips.empty()) continue;
		l.gl = t->gain * (t->pan > 0 ? 1.0f - t->pan : 1.0f);
		l.gr = t->gain * (t->pan < 0 ? 1.0f + t->pan : 1.0f);
		l.fx = Mix.Fx(i).HasEffects();
		for (size_t k = 0; k < l.clips.size(); k++){
			if (l.clips[k].Rate() > outRate) outRate = l.clips[k].Rate();
			double e = l.clips[k].End() + (l.fx ? 4.0 : 0.0);	// хвост эффектов
			if (e > endTime) endTime = e;
		}
		lanes.push_back(l);
	}
	if (lanes.empty() || outRate <= 0){ err = Language.get("MIXDOWN_EMPTY"); return false; }

	const int64 total = (int64)(endTime * outRate + 0.5) + 1;
	if (total * 8 > 0xFFFFFF00LL){ err = "The mixdown is too long for a WAV file"; return false; }
	std::vector<float> out;
	try { out.assign((size_t)total * 2, 0.0f); } catch (...) { err = "Not enough memory"; return false; }

	std::vector<float> tmp;
	for (size_t k = 0; k < lanes.size(); k++){
		const Lane &l = lanes[k];
		if (!l.fx){
			for (size_t q = 0; q < l.clips.size(); q++)
				RenderClipOffline(l.clips[q], &out[0], total, outRate, l.gl, l.gr);
			continue;
		}
		try { tmp.assign((size_t)total * 2, 0.0f); } catch (...) { err = "Not enough memory"; return false; }
		for (size_t q = 0; q < l.clips.size(); q++)
			RenderClipOffline(l.clips[q], &tmp[0], total, outRate, 1.0f, 1.0f);
		// отдельные экземпляры плагинов на частоте микса, чтобы не мешать воспроизведению
		FxChain local;
		FxChain &live = Mix.Fx(l.idx);
		for (int e = 0; e < live.Count(); e++){
			const FxChain::Slot *sl = live.At(e);
			std::string ferr;
			if (!sl->enabled) continue;
			if (!local.AddCopyOf(*sl, outRate, &ferr)){
				err = sl->plugin.name.c_str(); err << ": " << ferr.c_str();
				return false;
			}
		}
		for (int64 from = 0; from < total; from += 4096){
			int nfr = (int)((total - from < 4096) ? total - from : 4096);
			local.Process(&tmp[(size_t)from * 2], nfr, outRate);
		}
		for (int64 n = 0; n < total; n++){
			out[(size_t)n*2]   += tmp[(size_t)n*2]   * l.gl;
			out[(size_t)n*2+1] += tmp[(size_t)n*2+1] * l.gr;
		}
	}
	if (!WavWriteFloat(path, &out[0], total, 2, outRate)){
		err = "Cannot write "; err << path;
		return false;
	}
	return true;
}

// --------------------------------------------------------------- окно имени

ProjectNameWindow::ProjectNameWindow(BWindow *target, uint32 msgWhat, const char *defaultName,
	const char *title, const char *label)
	: BWindow(BRect(0, 0, 299, 89), title ? title : Language.get("PROJECT_NAME_TITLE"), B_TITLED_WINDOW_LOOK,
		B_MODAL_APP_WINDOW_FEEL, B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_ASYNCHRONOUS_CONTROLS),
	  fTarget(target), fWhat(msgWhat)
{
	BView *bg = new BView(Bounds(), "bg", B_FOLLOW_ALL, B_WILL_DRAW);
	bg->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	AddChild(bg);
	fText = new BTextControl(BRect(12, 12, 287, 32), "name", label ? label : Language.get("PROJECT_NAME"), defaultName, NULL);
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
