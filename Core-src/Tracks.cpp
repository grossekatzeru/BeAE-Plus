#include <StorageKit.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <algorithm>
#include <utility>
#include <vector>
#include <Window.h>

#include "CommonPool.h"
#include "Globals.h"
#include "History.h"
#include "Mixer.h"
#include "KeyCache.h"
#include "Tracks.h"
#include "ProjectIO.h"
#include "AudioRecorder.h"

extern cookie_record play_cookie;
static float gScroll = 0;

// Активный клип лежит в Pool. Если он не менялся, при переключении возвращаем в микшер тот же
// клип (общий источник), а не копию: копии одного звука не раздуваются в памяти.
static bool		gHasOrigin = false;
static MixClip	gOrigin;
static bool		gActiveLocked = false;		// активный клип закреплён во времени (флаг живёт, пока клип в Pool)
static int		gActiveGroup = 0;			// группа активного клипа (0 - нет)

// Буфер обмена клипов: один клип или набор (выделение, группа). У каждого элемента: ссылка на источник и
// положение относительно набора - сдвиг по дорожкам (в номерах рядов от активной дорожки) и по времени (от самого раннего клипа).
struct ClipItem { MixClip clip; int rank; double rel; };
static std::vector<ClipItem>	gClip;
static bool		gClipGrouped = false;		// скопирована группа: при вставке получает новый номер и цвет
static double	gClipSpan = 0;				// длина набора на ленте, секунды (шаг вставки нескольких копий)

void Tracks_Init()
{
	// ключ сайдчейна строится из клипов дорожки вместе с активным клипом из Pool; кэш огибающих сбрасывается
	Key::Cache::Global().Clear();
	Key::Cache::Global().SetLaneSource([](int lane, std::vector<MixClip> &clips, bool &xfade) {
		const MixTrack *t = Mix.Track(lane);
		if (!t) return false;
		clips = Tracks_LaneClips(lane);
		xfade = t->xfade;
		return true;
	});
	for (int i=Mix.TrackCount(); i<DEFAULT_TRACKS; i++)
		Mix.AddEmptyTrack(NULL, 2);
	// имена «Track 1».. «Track N»
	for (int i=0; i<Mix.TrackCount(); i++){
		MixTrack *t = Mix.Track(i);
		if (t) snprintf(t->name, sizeof(t->name), "Track %d", i+1);
	}
}

// Первый и последний кадр, где есть ненулевой сэмпл. false - вся дорожка тишина.
static bool ContentRange(const float *m, int64 frames, int ch, int64 &first, int64 &last)
{
	if (m == NULL || frames <= 0)
		return false;
	int64 f = 0;
	while (f < frames){
		if (m[f*ch] != 0.0f || (ch == 2 && m[f*ch+1] != 0.0f))
			break;
		f++;
	}
	if (f >= frames)
		return false;
	int64 l = frames - 1;
	while (l > f){
		if (m[l*ch] != 0.0f || (ch == 2 && m[l*ch+1] != 0.0f))
			break;
		l--;
	}
	first = f;
	last = l;
	return true;
}

static bool PoolHasSound()
{
	return Pool.sample_type != NONE && Pool.sample_memory != NULL && Pool.size > 0;
}

// Pool не отличается от клипа, из которого был загружен (звук тот же, вокруг - тишина)?
static bool PoolMatchesOrigin()
{
	if (!gHasOrigin || !gOrigin.src || !PoolHasSound())
		return false;
	const int ch = Pool.sample_type;
	if (ch != gOrigin.src->channels || Pool.frequency != gOrigin.src->rate)
		return false;
	const int64 lead = (int64)floor(gOrigin.pos * Pool.frequency + 0.5);
	if (lead < 0 || lead + gOrigin.len > Pool.size)
		return false;
	const float *m = Pool.sample_memory;
	if (memcmp(m + lead * ch, gOrigin.src->data + gOrigin.start * ch, (size_t)gOrigin.len * ch * sizeof(float)) != 0)
		return false;
	for (int64 i = 0; i < lead * ch; i++)
		if (m[i] != 0.0f) return false;
	for (int64 i = (lead + gOrigin.len) * ch; i < Pool.size * ch; i++)
		if (m[i] != 0.0f) return false;
	return true;
}

// Pool -> клип дорожки (без тишины по краям). Возвращает индекс добавленного клипа или -1.
// Новый клип всегда добавляется в конец списка, индексы остальных клипов не меняются.
static int StashActive()
{
	const int a = Mix.Active();
	MixTrack *t = Mix.Track(a);
	if (t == NULL)
		return -1;
	int idx = -1;
	if (PoolMatchesOrigin()){
		idx = Mix.AddClip(a, gOrigin.src, gOrigin.start, gOrigin.len, gOrigin.pos);
	}else if (PoolHasSound()){
		const float rate = (Pool.frequency > 1.0f) ? Pool.frequency : t->rate;
		int64 first = 0, last = 0;
		if (ContentRange(Pool.sample_memory, Pool.size, Pool.sample_type, first, last)){
			SourceRef src = MakeSource(Pool.sample_memory + first * Pool.sample_type, last - first + 1,
				Pool.sample_type, rate);
			if (src)
				idx = Mix.AddClip(a, src, 0, last - first + 1, (double)first / rate);
		}
	}
	if (idx >= 0 && gActiveLocked)
		Mix.SetClipLocked(a, idx, true);
	if (idx >= 0 && gActiveGroup > 0)
		Mix.SetClipGroup(a, idx, gActiveGroup);
	gHasOrigin = false;
	gActiveLocked = false;
	gActiveGroup = 0;
	return idx;
}

void Tracks_Stash()
{
	StashActive();
}

// конец самого дальнего клипа на ленте, секунды (все клипы лежат в микшере)
static double ExtentSeconds()
{
	return Mix.EndTimeExtras();
}

// микшер -> Pool: клип clipIdx дорожки lane (или пустая дорожка, если -1) с тишиной по краям до конца
// ленты. Делает дорожку активной. Активный клип в этот момент должен быть уже убран из Pool в микшер.
static bool Load(int lane, int clipIdx)
{
	MixTrack *t = Mix.Track(lane);
	if (t == NULL)
		return false;
	const double ext = ExtentSeconds();
	const MixClip *pc = (clipIdx >= 0) ? Mix.Clip(lane, clipIdx) : NULL;
	int ch = pc ? pc->src->channels : t->channels;
	float rate = pc ? pc->src->rate : ((Pool.frequency > 1.0f && PoolHasSound()) ? Pool.frequency : t->rate);

	if (ext <= 0.0){
		// проект пуст: Pool пустой, как при запуске
		Mix.SetActive(lane);
		Pool.size = 0;
		Pool.sample_type = NONE;
		Pool.pad_lead = Pool.pad_tail = 0;
		gHasOrigin = false;
		gActiveLocked = false;
		gActiveGroup = 0;
		return true;
	}

	const int64 F = (int64)ceil((ext + TIMELINE_MARGIN) * rate);
	float *mem = (float*)calloc((size_t)F * ch + 256, sizeof(float));
	if (mem == NULL)
		return false;

	int64 lead = 0, len = 0;
	MixClip clip;
	if (pc != NULL && Mix.TakeClip(lane, clipIdx, clip)){
		lead = (int64)floor(clip.pos * rate + 0.5);
		len = clip.len;
		if (lead + len > F) len = F - lead;
		if (len < 0) len = 0;
		memcpy(mem + lead * ch, clip.src->data + clip.start * ch, (size_t)len * ch * sizeof(float));
		gOrigin = clip;
		gHasOrigin = (len == clip.len);
		gActiveLocked = clip.locked;
		gActiveGroup = clip.group;
	}else{
		gHasOrigin = false;
		gActiveLocked = false;
		gActiveGroup = 0;
	}

	free(Pool.sample_memory);
	Pool.sample_memory = mem;
	Pool.size = F;
	Pool.sample_type = (ch == 1) ? MONO : STEREO;
	Pool.frequency = rate;
	Pool.pad_lead = lead;
	Pool.pad_tail = F - lead - len;
	if (Pool.m_format.type != B_MEDIA_RAW_AUDIO || Pool.m_format.u.raw_audio.format == 0){
		// проект ещё не знает формата (открыт без файла) - пишем без потерь, float
		Pool.m_format = media_format();
		Pool.m_format.type = B_MEDIA_RAW_AUDIO;
		Pool.m_format.u.raw_audio.format = media_raw_audio_format::B_AUDIO_FLOAT;
		Pool.m_format.u.raw_audio.byte_order = B_MEDIA_HOST_ENDIAN;
		Pool.m_format.u.raw_audio.buffer_size = 4 * ch * 2048;
		Pool.sample_bits = 32;
	}
	Pool.m_format.u.raw_audio.channel_count = Pool.sample_type;
	Pool.m_format.u.raw_audio.frame_rate = rate;

	Mix.SetActive(lane);
	return true;
}

// Положение окна волны и курсора в секундах - чтобы пережить пересборку дорожки
struct ViewState {
	bool	had;
	double	tl, tr, tp;
};

static ViewState CaptureView()
{
	ViewState v;
	const double rate = (Pool.frequency > 1.0f) ? Pool.frequency : 44100.0;
	v.had = (Pool.sample_type != NONE && Pool.size > 0);
	v.tl = Pool.l_pointer / rate;
	v.tr = Pool.r_pointer / rate;
	v.tp = Pool.pointer / rate;
	return v;
}

static void RefreshUI(const ViewState &v)
{
	Pool.selection = NONE;
	Pool.r_sel_pointer = 0;
	Pool.l_pointer = 0;
	Pool.r_pointer = Pool.size;
	Pool.pointer = 0;
	if (v.had && Pool.sample_type != NONE && Pool.size > 2){
		const double rate = Pool.frequency;
		int64 l = (int64)floor(v.tl * rate + 0.5);
		int64 r = (int64)floor(v.tr * rate + 0.5);
		int64 p = (int64)floor(v.tp * rate + 0.5);
		if (l < 0) l = 0;
		if (r > Pool.size) r = Pool.size;
		if (r - l >= 16){
			Pool.l_pointer = l;
			Pool.r_pointer = r;
		}
		if (p < 0) p = 0;
		if (p >= Pool.size) p = Pool.size - 1;
		Pool.pointer = p;
	}
	play_cookie.mem = Pool.sample_memory + (Pool.sample_type == NONE ? 0 : Pool.pointer * Pool.sample_type);
	play_cookie.start_mem = play_cookie.mem;
	play_cookie.end_mem = Pool.sample_memory + Pool.size * (Pool.sample_type == NONE ? 1 : Pool.sample_type);
	play_cookie.frequency = Pool.frequency;
	play_cookie.add = 0;

	if (Pool.sample_type != NONE)
		Pool.ResetIndexView();
	Pool.sample_view_dirty = true;
	Pool.update_draw_cache = true;
	Pool.update_index = true;
	Pool.update_peak = true;
	Pool.RedrawWindow();
	Pool.UpdateToolBar();
}

static void StopAll()
{
	if (Pool.IsPlaying()){
		Pool.StopPlaying();
		if (Pool.mainWindow)
			Pool.mainWindow->PostMessage(TRANSPORT_STOP);
	}
	if (Pool.player)
		Pool.player->Stop();
}

// ---- отмена операций с клипами: стек снимков раскладки (клипы всех дорожек) ----
struct LayoutSnap {
	std::vector<MixClip> lane[MIX_MAX_TRACKS];
};
static std::vector<LayoutSnap> gUndo, gRedo;
#define CLIP_UNDO_DEPTH	100

static void CaptureLayout(LayoutSnap &s)
{
	for (int i = 0; i < MIX_MAX_TRACKS; i++){
		s.lane[i].clear();
		if (!Mix.Track(i)) continue;
		for (int k = 0; k < Mix.ClipCount(i); k++)
			s.lane[i].push_back(*Mix.Clip(i, k));
	}
}

// одинаковый набор клипов (порядок не важен: переключение дорожки переставляет клипы)
static bool SameLayout(const LayoutSnap &a, const LayoutSnap &b)
{
	for (int i = 0; i < MIX_MAX_TRACKS; i++){
		if (a.lane[i].size() != b.lane[i].size()) return false;
		std::vector<bool> used(b.lane[i].size(), false);
		for (size_t x = 0; x < a.lane[i].size(); x++){
			bool found = false;
			for (size_t y = 0; y < b.lane[i].size() && !found; y++){
				if (used[y]) continue;
				const MixClip &p = a.lane[i][x], &q = b.lane[i][y];
				if (p.src.get() == q.src.get() && p.start == q.start && p.len == q.len && fabs(p.pos - q.pos) < 1e-9){
					used[y] = true; found = true;
				}
			}
			if (!found) return false;
		}
	}
	return true;
}

bool Tracks_CanUndo() { return !gUndo.empty(); }
bool Tracks_CanRedo() { return !gRedo.empty(); }
void Tracks_ClearUndo() { gUndo.clear(); gRedo.clear(); }

// Общая часть всех правок структуры: убрать активный клип в микшер, выполнить правку (op получает
// индекс убранного клипа или -1), собрать Pool заново вокруг клипа, который вернёт op (-1 - никакого).
// Окно волны и курсор сохраняются (по времени).
template <class Op>
static bool Restructure(Op op, bool resetUndo = true, bool record = true)
{
	if (Recorder.IsRecording())
		return false;
	ViewState view = CaptureView();
	StopAll();
	int stashed = StashActive();
	int lane = Mix.Active();
	LayoutSnap before;
	if (record) CaptureLayout(before);
	int toLoad = op(lane, stashed, view);
	if (record){
		LayoutSnap after;
		CaptureLayout(after);
		if (!SameLayout(before, after)){
			gUndo.push_back(before);
			gRedo.clear();							// новая правка обрывает цепочку «вернуть»
			if (gUndo.size() > CLIP_UNDO_DEPTH) gUndo.erase(gUndo.begin());
		}
	}
	if (toLoad < -1)
		toLoad = -1;
	if (!Load(lane, toLoad)){
		Load(lane, stashed);				// вернуть, как было
		return false;
	}
	if (resetUndo)
		Hist.Reset();
	RefreshUI(view);
	return true;
}

// Клип дорожки lane по умолчанию: тот, что накрывает время, иначе первый, иначе -1
static int ChooseClip(int lane, double time)
{
	int c = (time >= 0.0) ? Mix.ClipAt(lane, time) : -1;
	if (c < 0 && Mix.ClipCount(lane) > 0) c = 0;
	return c;
}

bool Tracks_SetName(int index, const char *name)
{
	MixTrack *t = Mix.Track(index);
	if (!t || !name) return false;
	size_t n = strlen(name);
	if (n > sizeof(t->name) - 1) {
		n = sizeof(t->name) - 1;
		while (n > 0 && ((unsigned char)name[n] & 0xC0) == 0x80) n--;	// не рвать UTF-8 символ
	}
	memcpy(t->name, name, n);
	t->name[n] = 0;
	Mix.Touch();
	return true;
}

bool Tracks_Activate(int lane, double cursorTime, int clip)
{
	MixTrack *t = Mix.Track(lane);
	if (t == NULL)
		return false;
	if (lane == Mix.Active() && clip < 0)
		return true;
	if (Recorder.IsRecording())
		return false;

	ViewState view = CaptureView();
	if (cursorTime >= 0.0)
		view.tp = cursorTime;
	StopAll();
	int stashed = StashActive();
	const int old = Mix.Active();
	int pick = clip;
	if (pick < 0 || pick >= Mix.ClipCount(lane))
		pick = ChooseClip(lane, view.tp);
	if (!Load(lane, pick)){
		Load(old, stashed);			// вернуть прежнюю
		return false;
	}
	Hist.Reset();					// undo относится к одному клипу
	RefreshUI(view);
	return true;
}

void Tracks_PlaceActive(double offsetSeconds)
{
	if (!PoolHasSound())
		return;
	int a = Mix.Active();
	if (!Mix.Track(a))
		return;
	gHasOrigin = false;				// в Pool свежий звук, прежний активный клип заменён
	gActiveLocked = false;
	gActiveGroup = 0;
	SourceRef src = MakeSource(Pool.sample_memory, Pool.size, Pool.sample_type, Pool.frequency);
	if (!src)
		return;
	int idx = Mix.AddClip(a, src, 0, Pool.size, offsetSeconds < 0 ? 0 : offsetSeconds);
	Load(a, idx);
}

// Положение активного клипа на ленте (по первому ненулевому кадру), сек
static double ActiveClipStart()
{
	int64 first = 0, last = 0;
	if (PoolHasSound() && ContentRange(Pool.sample_memory, Pool.size, Pool.sample_type, first, last))
		return (double)first / Pool.frequency;
	return 0;
}

bool Tracks_IsEmpty()
{
	if (PoolHasSound()) return false;
	for (int i = 0; i < MIX_MAX_TRACKS; i++)
		if (Mix.Track(i) && Mix.ClipCount(i) > 0) return false;
	return true;
}

void Tracks_PublishActiveSpan()
{
	double s = 0, e = 0;
	if (PoolHasSound()){
		if (PoolMatchesOrigin()){
			s = gOrigin.pos;
			e = gOrigin.End();
		}else{
			int64 first = 0, last = 0;
			if (ContentRange(Pool.sample_memory, Pool.size, Pool.sample_type, first, last) && Pool.frequency > 1.0f){
				s = (double)first / Pool.frequency;
				e = (double)(last + 1) / Pool.frequency;
			}
		}
	}
	Mix.SetActiveSpan(s, e);
}

double Tracks_Offset(int index)
{
	MixTrack *t = Mix.Track(index);
	if (t == NULL)
		return 0;
	if (index == Mix.Active())
		return ActiveClipStart();
	const MixClip *c = Mix.Clip(index, 0);
	return c ? c->pos : 0;
}

// пересобрать активную дорожку под текущую ленту (например, после удаления дорожки)
static void Reload()
{
	gUndo.clear(); gRedo.clear();							// число дорожек изменилось - прежние снимки не годятся
	Restructure([](int, int stashed, const ViewState&){ return stashed; }, true, false);
}

bool Tracks_SetOffset(int index, double seconds)
{
	if (!Mix.Track(index) || Recorder.IsRecording())
		return false;
	if (seconds < 0) seconds = 0;
	const bool isActive = (index == Mix.Active());
	if (isActive ? gActiveLocked : (Mix.Clip(index, 0) && Mix.Clip(index, 0)->locked))
		return false;					// закреплён во времени
	return Restructure([&](int lane, int stashed, const ViewState&){
		if (isActive){
			if (stashed >= 0) Mix.SetClipPos(lane, stashed, seconds);
			return stashed;
		}
		Mix.SetClipPos(index, 0, seconds);		// неактивная дорожка: её первый клип
		return stashed;
	});
}

bool Tracks_MoveClip(int lane, int clip, double seconds)
{
	if (!Mix.Clip(lane, clip) || Recorder.IsRecording() || Mix.Clip(lane, clip)->locked)
		return false;
	if (seconds < 0) seconds = 0;
	return Restructure([&](int, int stashed, const ViewState&){
		Mix.SetClipPos(lane, clip, seconds);
		return stashed;
	});
}

bool Tracks_ActiveClipLocked()
{
	return PoolHasSound() && gActiveLocked;
}

void Tracks_ToggleActiveClipLock()
{
	if (!PoolHasSound()) return;
	gActiveLocked = !gActiveLocked;
	gOrigin.locked = gActiveLocked;
	Mix.Touch();
}

// ---- выделение нескольких клипов ----

static bool ActiveClipExists()
{
	int64 first = 0, last = 0;
	return Tracks_ActiveRange(first, last);
}

void Tracks_SelectLaneClips()
{
	const int lane = Mix.Active();
	for (int k = 0; k < Mix.ClipCount(lane); k++){
		Mix.SetClipSelected(lane, k, true);
		Tracks_SelectGroupOf(lane, k);			// клип в группе тянет за собой всю группу
	}
}

void Tracks_ClearClipSelection()
{
	for (int i = 0; i < MIX_MAX_TRACKS; i++)
		for (int k = 0; k < Mix.ClipCount(i); k++)
			if (Mix.Clip(i, k)->selected)
				Mix.SetClipSelected(i, k, false);
}

bool Tracks_ToggleClipSelected(int lane, int clip)
{
	const MixClip *c = Mix.Clip(lane, clip);
	if (!c) return false;
	const bool on = !c->selected;
	if (c->group > 0){							// клип в группе: выделяется и снимается вся группа целиком
		if (gActiveGroup == c->group && ActiveClipExists())
			return false;						// в группе активный клип: он выделен всегда, группу не снять
		const int g = c->group;
		for (int i = 0; i < MIX_MAX_TRACKS; i++)
			for (int k = 0; k < Mix.ClipCount(i); k++)
				if (Mix.Clip(i, k)->group == g) Mix.SetClipSelected(i, k, on);
		return true;
	}
	Mix.SetClipSelected(lane, clip, on);
	return true;
}

int Tracks_ActiveClipGroup()
{
	return ActiveClipExists() ? gActiveGroup : 0;
}

void Tracks_SelectGroupOf(int lane, int clip)
{
	const MixClip *c = Mix.Clip(lane, clip);
	if (!c || c->group <= 0) return;
	const int g = c->group;
	for (int i = 0; i < MIX_MAX_TRACKS; i++)
		for (int k = 0; k < Mix.ClipCount(i); k++)
			if (Mix.Clip(i, k)->group == g) Mix.SetClipSelected(i, k, true);
}

void Tracks_SelectActiveGroup()
{
	if (!ActiveClipExists() || gActiveGroup <= 0) return;
	for (int i = 0; i < MIX_MAX_TRACKS; i++)
		for (int k = 0; k < Mix.ClipCount(i); k++)
			if (Mix.Clip(i, k)->group == gActiveGroup) Mix.SetClipSelected(i, k, true);
}

// выделенные клипы образуют одну группу
bool Tracks_SelectionGrouped()
{
	if (Tracks_SelectedCount() < 2) return false;
	int g = ActiveClipExists() ? gActiveGroup : -1;
	if (ActiveClipExists() && g <= 0) return false;
	for (int i = 0; i < MIX_MAX_TRACKS; i++)
		for (int k = 0; k < Mix.ClipCount(i); k++){
			const MixClip *c = Mix.Clip(i, k);
			if (!c->selected) continue;
			if (c->group <= 0) return false;
			if (g < 0) g = c->group;
			else if (c->group != g) return false;
		}
	return g > 0;
}

// наименьший свободный номер группы: цвета не расходуются зря
static int FreeGroupId()
{
	for (int g = 1;; g++){
		bool used = (ActiveClipExists() && gActiveGroup == g);
		for (int i = 0; i < MIX_MAX_TRACKS && !used; i++)
			for (int k = 0; k < Mix.ClipCount(i) && !used; k++)
				if (Mix.Clip(i, k)->group == g) used = true;
		if (!used) return g;
	}
}

// «Group»: сгруппировать выделенные клипы (не меньше двух) в новую группу, а если они уже группа - разгруппировать
bool Tracks_ToggleSelectionGroup()
{
	if (Tracks_SelectedCount() < 2 || Recorder.IsRecording())
		return false;
	const int target = Tracks_SelectionGrouped() ? 0 : FreeGroupId();
	if (ActiveClipExists()){
		gActiveGroup = target;
		gOrigin.group = target;
	}
	for (int i = 0; i < MIX_MAX_TRACKS; i++)
		for (int k = 0; k < Mix.ClipCount(i); k++)
			if (Mix.Clip(i, k)->selected)
				Mix.SetClipGroup(i, k, target);
	Mix.Touch();
	return true;
}

int Tracks_SelectedCount()
{
	int n = ActiveClipExists() ? 1 : 0;
	for (int i = 0; i < MIX_MAX_TRACKS; i++)
		for (int k = 0; k < Mix.ClipCount(i); k++)
			if (Mix.Clip(i, k)->selected) n++;
	return n;
}

bool Tracks_SelectionLocked()
{
	int n = 0;
	if (ActiveClipExists()){
		if (!gActiveLocked) return false;
		n++;
	}
	for (int i = 0; i < MIX_MAX_TRACKS; i++)
		for (int k = 0; k < Mix.ClipCount(i); k++){
			const MixClip *c = Mix.Clip(i, k);
			if (!c->selected) continue;
			if (!c->locked) return false;
			n++;
		}
	return n > 0;
}

void Tracks_ToggleSelectionLock()
{
	const bool target = !Tracks_SelectionLocked();
	if (ActiveClipExists()){
		gActiveLocked = target;
		gOrigin.locked = target;
	}
	for (int i = 0; i < MIX_MAX_TRACKS; i++)
		for (int k = 0; k < Mix.ClipCount(i); k++)
			if (Mix.Clip(i, k)->selected)
				Mix.SetClipLocked(i, k, target);
	Mix.Touch();
}

bool Tracks_MoveSelectionBy(double delta, bool includeActive)
{
	if (Recorder.IsRecording() || delta == 0)
		return false;
	return Restructure([&](int lane, int stashed, const ViewState&){
		std::vector<std::pair<int, int> > movers;				// (дорожка, номер клипа) незакреплённых выделенных
		for (int i = 0; i < MIX_MAX_TRACKS; i++)
			for (int k = 0; k < Mix.ClipCount(i); k++){
				const MixClip *c = Mix.Clip(i, k);
				if (c->selected && !c->locked && !(i == lane && k == stashed)) movers.push_back(std::make_pair(i, k));
			}
		if (includeActive && stashed >= 0 && Mix.Clip(lane, stashed) && !Mix.Clip(lane, stashed)->locked)
			movers.push_back(std::make_pair(lane, stashed));
		double minPos = 1e18;
		for (size_t m = 0; m < movers.size(); m++)
			minPos = std::min(minPos, Mix.Clip(movers[m].first, movers[m].second)->pos);
		double d = delta;
		if (!movers.empty() && minPos + d < 0) d = -minPos;		// не дальше начала ленты
		for (size_t m = 0; m < movers.size(); m++){
			const MixClip *c = Mix.Clip(movers[m].first, movers[m].second);
			Mix.SetClipPos(movers[m].first, movers[m].second, c->pos + d);
		}
		return stashed;
	});
}

// слот дорожки, лежащей на dr рядов ниже (выше при dr < 0) дорожки slot; -1, если такой нет
static int ShiftSlot(int slot, int dr)
{
	const int r = Tracks_Rank(slot);
	if (r < 0) return -1;
	return Tracks_SlotAt(r + dr);
}

bool Tracks_SelectionCanShiftLanes(int dr)
{
	if (ActiveClipExists() && ShiftSlot(Mix.Active(), dr) < 0) return false;
	for (int i = 0; i < MIX_MAX_TRACKS; i++)
		for (int k = 0; k < Mix.ClipCount(i); k++)
			if (Mix.Clip(i, k)->selected && ShiftSlot(i, dr) < 0) return false;
	return true;
}

// Выделенные клипы (в том числе группа) вместе: сдвиг по времени и на dr дорожек. Закреплённые во времени остаются
// в своём моменте, но дорожку меняют.
bool Tracks_MoveSelection(double timeDelta, int dr)
{
	if (dr == 0) return Tracks_MoveSelectionBy(timeDelta, true);
	if (Recorder.IsRecording() || !Tracks_SelectionCanShiftLanes(dr))
		return false;
	int newLane = -1, newIdx = -1;
	double newPos = 0;
	const bool ok = Restructure([&](int lane, int stashed, const ViewState &view){
		struct Mv { MixClip mc; int to; bool active; };
		std::vector<Mv> mv;
		if (stashed >= 0){
			Mv m; m.active = true; m.to = ShiftSlot(lane, dr);
			if (Mix.TakeClip(lane, stashed, m.mc)) mv.push_back(m);
		}
		for (int i = 0; i < MIX_MAX_TRACKS; i++)
			for (int k = Mix.ClipCount(i) - 1; k >= 0; k--)
				if (Mix.Clip(i, k)->selected){
					Mv m; m.active = false; m.to = ShiftSlot(i, dr);
					if (Mix.TakeClip(i, k, m.mc)) mv.push_back(m);
				}
		double minPos = 1e18;
		for (size_t m = 0; m < mv.size(); m++)
			if (!mv[m].mc.locked) minPos = std::min(minPos, mv[m].mc.pos);
		double d = timeDelta;
		if (minPos < 1e17 && minPos + d < 0) d = -minPos;
		for (size_t m = 0; m < mv.size(); m++){
			const MixClip &c = mv[m].mc;
			const double pos = c.locked ? c.pos : c.pos + d;
			const int ni = Mix.AddClip(mv[m].to, c.src, c.start, c.len, pos);
			if (ni < 0) continue;
			if (c.locked) Mix.SetClipLocked(mv[m].to, ni, true);
			if (c.group > 0) Mix.SetClipGroup(mv[m].to, ni, c.group);
			if (mv[m].active){ newLane = mv[m].to; newIdx = ni; newPos = pos; }
			else Mix.SetClipSelected(mv[m].to, ni, true);
		}
		return ChooseClip(lane, view.tp);
	});
	if (ok && newIdx >= 0)
		Tracks_Activate(newLane, newPos, newIdx);			// активный клип уходит вместе с остальными
	return ok;
}

bool Tracks_MoveClipToLane(int from, int clip, int to, double seconds)
{
	if (from == to) return Tracks_MoveClip(from, clip, seconds);
	if (!Mix.Clip(from, clip) || !Mix.Track(to) || Recorder.IsRecording())
		return false;
	if (from == Mix.Active())
		return false;					// активный клип лежит в Pool: для него Tracks_MoveActiveClipToLane
	if (seconds < 0) seconds = 0;
	return Restructure([&](int, int stashed, const ViewState&){
		MixClip mc;
		if (Mix.TakeClip(from, clip, mc)){
			// закреплённый во времени клип меняет дорожку, но остаётся в том же моменте и закреплённым
			const int ni = Mix.AddClip(to, mc.src, mc.start, mc.len, mc.locked ? mc.pos : seconds);
			if (ni >= 0 && mc.locked) Mix.SetClipLocked(to, ni, true);
			if (ni >= 0 && mc.group > 0) Mix.SetClipGroup(to, ni, mc.group);
		}
		return stashed;
	});
}

bool Tracks_MoveActiveClipToLane(int to, double seconds)
{
	if (!Mix.Track(to) || to == Mix.Active() || !PoolHasSound() || Recorder.IsRecording())
		return false;
	if (seconds < 0) seconds = 0;
	int newIdx = -1;
	double placed = seconds;
	bool ok = Restructure([&](int lane, int stashed, const ViewState &view){
		if (stashed < 0) return stashed;
		MixClip mc;
		if (!Mix.TakeClip(lane, stashed, mc)) return stashed;
		if (mc.locked) placed = mc.pos;				// закреплённый во времени клип остаётся в том же моменте
		newIdx = Mix.AddClip(to, mc.src, mc.start, mc.len, placed);
		if (newIdx >= 0 && mc.locked) Mix.SetClipLocked(to, newIdx, true);
		if (newIdx >= 0 && mc.group > 0) Mix.SetClipGroup(to, newIdx, mc.group);
		return ChooseClip(lane, view.tp);			// на прежней дорожке остаётся другой клип или пусто
	});
	if (ok && newIdx >= 0)
		Tracks_Activate(to, placed, newIdx);		// идём за клипом
	return ok;
}

// дорожка пуста: нет клипов в микшере и в Pool (если она активна) нет звука
static bool LaneIsEmpty(int i)
{
	if (!Mix.Track(i) || Mix.ClipCount(i) > 0) return false;
	int64 first = 0, last = 0;
	return !(i == Mix.Active() && Tracks_ActiveRange(first, last));
}

int Tracks_ImportClip(int lane, const SourceRef &src, double seconds, const char *name)
{
	if (!src || src->frames <= 0 || Recorder.IsRecording())
		return -1;
	if (seconds < 0) seconds = 0;
	if (lane < 0 || !Mix.Track(lane)){
		lane = -1;
		for (int i = 0; i < MIX_MAX_TRACKS && lane < 0; i++)	// первая пустая дорожка
			if (LaneIsEmpty(i))
				lane = i;
		if (lane < 0) lane = Tracks_Add();
		if (lane < 0) return -1;
	}
	MixTrack *t = Mix.Track(lane);
	const bool wasEmpty = t && LaneIsEmpty(lane);
	int newIdx = -1;
	if (!Restructure([&](int, int stashed, const ViewState&){
			newIdx = Mix.AddClip(lane, src, 0, src->frames, seconds);
			return stashed;
		}))
		return -1;
	if (wasEmpty && name && *name && t && strncmp(t->name, "Track ", 6) == 0)
		Tracks_SetName(lane, name);
	if (newIdx >= 0)
		Tracks_Activate(lane, seconds, newIdx);
	return lane;
}

bool Tracks_ActiveRange(int64 &first, int64 &last)
{
	if (!PoolHasSound())
		return false;
	return ContentRange(Pool.sample_memory, Pool.size, Pool.sample_type, first, last);
}

int Tracks_Add()
{
	gUndo.clear(); gRedo.clear();
	char name[32];
	snprintf(name, sizeof(name), "Track %d", Mix.TrackCount()+1);
	return Mix.AddEmptyTrack(name, 2);
}

void Tracks_RemoveActive()
{
	int a = Mix.Active();
	int count = 0;
	for (int i=0; i<MIX_MAX_TRACKS; i++) if (Mix.Track(i)) count++;
	if (count < 2 || Recorder.IsRecording())
		return;
	// сначала переключиться на соседнюю дорожку
	int other = -1;
	for (int i=0; i<MIX_MAX_TRACKS && other<0; i++)
		if (i != a && Mix.Track(i)) other = i;
	if (other < 0 || !Tracks_Activate(other))
		return;
	gUndo.clear(); gRedo.clear();
	if (a >= 1){
		Mix.RemoveTrack(a);
	}else{
		Mix.ClearClips(0);					// дорожка 0 не удаляется физически - очищаем её
	}
	Reload();						// лента могла укоротиться: пересобрать активную
}

void Tracks_SaveRange(int64 &start, int64 &end)
{
	if (!PoolHasSound() || Pool.size <= 1)
		return;
	int64 first = 0, last = 0;
	if (!ContentRange(Pool.sample_memory, Pool.size, Pool.sample_type, first, last))
		return;						// одна тишина - сохраняем как есть
	int64 s = Pool.pad_lead;
	int64 e = Pool.size - Pool.pad_tail;
	if (first < s) s = first;		// звук, вставленный в поля, не теряем
	if (last + 1 > e) e = last + 1;
	if (s < 0) s = 0;
	if (e > Pool.size) e = Pool.size;
	if (e - s < 2)
		return;
	start = s;
	end = e;
}

std::vector<MixClip> Tracks_LaneClips(int lane)
{
	std::vector<MixClip> out;
	if (!Mix.Track(lane))
		return out;
	for (int i=0; i<Mix.ClipCount(lane); i++)
		out.push_back(*Mix.Clip(lane, i));
	if (lane == Mix.Active() && PoolHasSound()){
		if (PoolMatchesOrigin()){
			MixClip o = gOrigin;
			o.locked = gActiveLocked;
			o.group = gActiveGroup;
			out.push_back(o);
		}else{
			int64 first = 0, last = 0;
			if (ContentRange(Pool.sample_memory, Pool.size, Pool.sample_type, first, last)){
				SourceRef src = MakeSource(Pool.sample_memory + first * Pool.sample_type, last - first + 1,
					Pool.sample_type, Pool.frequency);
				if (src){
					MixClip c;
					c.src = src; c.start = 0; c.len = last - first + 1; c.pos = (double)first / Pool.frequency;
					c.locked = gActiveLocked;
					c.group = gActiveGroup;
					out.push_back(c);
				}
			}
		}
	}
	return out;
}

bool Tracks_ActiveClipSpan(double &start, double &end)
{
	int64 first = 0, last = 0;
	if (!PoolHasSound() || !ContentRange(Pool.sample_memory, Pool.size, Pool.sample_type, first, last))
		return false;
	start = (double)first / Pool.frequency;
	end = (double)(last + 1) / Pool.frequency;
	return true;
}

void Tracks_BeginProject()
{
	gUndo.clear(); gRedo.clear();
	StopAll();
	Hist.Reset();
	Mix.ResetAll();
	free(Pool.sample_memory);
	Pool.sample_memory = NULL;
	Pool.size = 0;
	Pool.sample_type = NONE;
	Pool.pad_lead = Pool.pad_tail = 0;
	Pool.m_format = media_format();				// формат определится при загрузке
	gHasOrigin = false;
	gClip.clear();
	gScroll = 0;
}

void Tracks_FinishProject()
{
	ViewState none;
	none.had = false; none.tl = none.tr = none.tp = 0;
	Load(0, ChooseClip(0, 0.0));
	Hist.Reset();
	Pool.changed = false;
	RefreshUI(none);
}

// ======================================================================== клипы

bool Tracks_SplitAtCursor()
{
	if (!PoolHasSound() || Recorder.IsRecording())
		return false;
	const double t = (double)Pool.pointer / Pool.frequency;
	bool done = false;
	Restructure([&](int lane, int stashed, const ViewState&){
		const MixClip *c = Mix.Clip(lane, stashed);
		if (!c) return stashed;
		const int64 k = (int64)floor((t - c->pos) * c->Rate() + 0.5);
		if (k <= 0 || k >= c->len) return stashed;		// курсор не внутри клипа
		const SourceRef src = c->src;
		const int64 start = c->start, len = c->len;
		const double pos = c->pos;
		const bool locked = c->locked;
		const int grp = c->group;
		Mix.SetClipRange(lane, stashed, start, k);		// левая часть остаётся
		int right = Mix.AddClip(lane, src, start + k, len - k, pos + (double)k / src->rate);
		if (right >= 0 && locked) Mix.SetClipLocked(lane, right, true);	// закрепление наследуют обе части
		if (right >= 0 && grp > 0) Mix.SetClipGroup(lane, right, grp);	// и группу тоже
		done = (right >= 0);
		return right >= 0 ? right : stashed;			// правая часть становится активной
	});
	return done;
}

// Заменить раскладку клипов на снимок; текущую - положить в стек to
static bool ApplyLayout(const LayoutSnap &s, std::vector<LayoutSnap> &to)
{
	return Restructure([&](int lane, int, const ViewState &view){
		LayoutSnap cur;
		CaptureLayout(cur);
		to.push_back(cur);
		for (int i = 0; i < MIX_MAX_TRACKS; i++){
			if (!Mix.Track(i)) continue;
			Mix.ClearClips(i);
			for (size_t k = 0; k < s.lane[i].size(); k++){
				const MixClip &c = s.lane[i][k];
				const int ni = Mix.AddClip(i, c.src, c.start, c.len, c.pos);
				if (ni >= 0 && c.locked) Mix.SetClipLocked(i, ni, true);
				if (ni >= 0 && c.group > 0) Mix.SetClipGroup(i, ni, c.group);
			}
		}
		return ChooseClip(lane, view.tp);
	}, true, false);
}

bool Tracks_Undo()
{
	if (gUndo.empty() || Recorder.IsRecording())
		return false;
	const LayoutSnap s = gUndo.back();
	gUndo.pop_back();
	return ApplyLayout(s, gRedo);
}

bool Tracks_Redo()
{
	if (gRedo.empty() || Recorder.IsRecording())
		return false;
	const LayoutSnap s = gRedo.back();
	gRedo.pop_back();
	return ApplyLayout(s, gUndo);
}

// Активный клип - в файл WAV (32-бит float), оригинал не трогаем
bool Tracks_SaveActiveClip(const char *path, BString &err)
{
	if (!PoolHasSound()){ err = "No clip"; return false; }
	std::vector<MixClip> v = Tracks_LaneClips(Mix.Active());
	if (v.empty() || !v.back().src){ err = "No clip"; return false; }
	const MixClip &c = v.back();
	const int ch = c.src->channels;
	if (!WavWriteFloat(path, c.src->data + c.start * ch, c.len, ch, c.src->rate)){
		err = "Cannot write "; err << path;
		return false;
	}
	return true;
}

bool Tracks_CopyClip()
{
	if (!PoolHasSound())
		return false;
	std::vector<ClipItem> items;
	Restructure([&](int lane, int stashed, const ViewState&){
		// активный клип, если он есть, и все выделенные клипы микшера (в том числе вся группа)
		std::vector<std::pair<int, int> > src;				// (дорожка, номер)
		if (Mix.Clip(lane, stashed)) src.push_back(std::make_pair(lane, stashed));
		for (int i = 0; i < MIX_MAX_TRACKS; i++)
			for (int k = 0; k < Mix.ClipCount(i); k++)
				if (Mix.Clip(i, k)->selected && !(i == lane && k == stashed)) src.push_back(std::make_pair(i, k));
		if (src.empty()) return stashed;
		const int anchor = Tracks_Rank(src[0].first);
		double minPos = 1e18, maxEnd = 0;
		for (size_t s = 0; s < src.size(); s++){
			const MixClip *c = Mix.Clip(src[s].first, src[s].second);
			minPos = std::min(minPos, c->pos);
			maxEnd = std::max(maxEnd, c->End());
		}
		bool sameGroup = src.size() > 1;
		const int g0 = Mix.Clip(src[0].first, src[0].second)->group;
		for (size_t s = 0; s < src.size(); s++){
			const MixClip *c = Mix.Clip(src[s].first, src[s].second);
			if (c->group != g0 || g0 <= 0) sameGroup = false;
			ClipItem it;
			it.clip = *c;
			it.clip.locked = false; it.clip.selected = false; it.clip.group = 0;	// копия не закреплена и вне групп
			it.rank = Tracks_Rank(src[s].first) - anchor;
			it.rel = c->pos - minPos;
			items.push_back(it);
		}
		gClipGrouped = sameGroup;
		gClipSpan = maxEnd - minPos;
		return stashed;
	}, false, false);
	if (items.empty())
		return false;
	gClip = items;
	return true;
}

bool Tracks_HasClipboard()
{
	return !gClip.empty() && gClip[0].clip.src && gClip[0].clip.len > 0;
}

// Дорожки для вставки: набор встаёт от активной дорожки (сдвигаясь вниз, если часть клипов лежала выше её)
static int PasteBaseRank(int &maxRel)
{
	int minRel = 0;
	maxRel = 0;
	for (size_t i = 0; i < gClip.size(); i++){
		minRel = std::min(minRel, gClip[i].rank);
		maxRel = std::max(maxRel, gClip[i].rank);
	}
	return std::max(Tracks_Rank(Mix.Active()), -minRel);
}

bool Tracks_PasteFits()
{
	if (!Tracks_HasClipboard()) return true;
	int maxRel = 0;
	const int base = PasteBaseRank(maxRel);
	return base + maxRel < MIX_MAX_TRACKS;			// недостающие дорожки добавятся сами, пока не упрёмся в предел
}

bool Tracks_PasteClip(double timePos, int count)
{
	if (!Tracks_HasClipboard() || count < 1 || count > 1000 || Recorder.IsRecording())
		return false;
	if (timePos < 0) timePos = 0;
	timePos = Mix.Snap(Mix.Active(), timePos);			// первая копия встаёт на сетку
	const std::vector<ClipItem> items = gClip;
	const bool grouped = gClipGrouped;
	const double step = items.size() == 1 ? items[0].clip.Length() : gClipSpan;

	// недостающие дорожки добавляются снизу; на пределе числа дорожек вставки нет (окно сообщает об этом)
	int maxRel = 0;
	const int baseRank = PasteBaseRank(maxRel);
	if (baseRank + maxRel >= MIX_MAX_TRACKS) return false;
	while (Tracks_Count() <= baseRank + maxRel)
		if (Tracks_Add() < 0) return false;
	return Restructure([&](int lane, int stashed, const ViewState&){
		for (int n = 0; n < count; n++){
			const int g = grouped ? FreeGroupId() : 0;	// каждая вставленная группа - новый номер и цвет
			for (size_t i = 0; i < items.size(); i++){
				const int slot = (items.size() == 1) ? lane : Tracks_SlotAt(baseRank + items[i].rank);
				if (slot < 0) continue;
				const int ni = Mix.AddClip(slot, items[i].clip.src, items[i].clip.start, items[i].clip.len, timePos + n * step + items[i].rel);
				if (ni >= 0 && g > 0) Mix.SetClipGroup(slot, ni, g);
			}
		}
		return stashed;
	});
}

bool Tracks_DeleteClip()
{
	if (!PoolHasSound() || Recorder.IsRecording())
		return false;
	return Restructure([&](int lane, int stashed, const ViewState&){
		if (stashed >= 0) Mix.RemoveClip(lane, stashed);
		for (int i = 0; i < MIX_MAX_TRACKS; i++)		// выделенные (в том числе вся группа) удаляются вместе с активным
			for (int k = Mix.ClipCount(i) - 1; k >= 0; k--)
				if (Mix.Clip(i, k)->selected) Mix.RemoveClip(i, k);
		return ChooseClip(lane, (double)Pool.pointer / Pool.frequency);
	});
}

// ---- раскладка по вертикали ----

int Tracks_Count()
{
	int n = 0;
	for (int i=0; i<MIX_MAX_TRACKS; i++)
		if (Mix.Track(i)) n++;
	return n;
}

int Tracks_Rank(int slot)
{
	if (!Mix.Track(slot)) return -1;
	int r = 0;
	for (int i=0; i<slot; i++)
		if (Mix.Track(i)) r++;
	return r;
}

int Tracks_SlotAt(int rank)
{
	int r = 0;
	for (int i=0; i<MIX_MAX_TRACKS; i++){
		if (!Mix.Track(i)) continue;
		if (r == rank) return i;
		r++;
	}
	return -1;
}

float Tracks_LaneHeight(float areaHeight)
{
	int n = Tracks_Count();
	if (n < 1) n = 1;
	float h = floorf(areaHeight / n);
	return (h < MIN_LANE_HEIGHT) ? (float)MIN_LANE_HEIGHT : h;
}

float Tracks_Scroll()
{
	return gScroll;
}

void Tracks_SetScroll(float scroll, float areaHeight)
{
	float total = Tracks_LaneHeight(areaHeight) * Tracks_Count();
	float max = total - areaHeight;
	if (max < 0) max = 0;
	if (scroll > max) scroll = max;
	if (scroll < 0) scroll = 0;
	gScroll = scroll;
}

void Tracks_LaneSpan(int rank, float areaHeight, float &top, float &bottom)
{
	const float h = Tracks_LaneHeight(areaHeight);
	const int n = Tracks_Count();
	Tracks_SetScroll(gScroll, areaHeight);			// вдруг дорожек стало меньше
	top = rank * h - gScroll;
	bottom = top + h - 1;
	if (rank == n - 1 && n * h <= areaHeight)
		bottom = areaHeight - 1;					// последняя полоса добирает остаток, пока всё помещается
}
