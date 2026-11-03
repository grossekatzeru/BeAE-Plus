#include <StorageKit.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <Window.h>

#include "CommonPool.h"
#include "Globals.h"
#include "History.h"
#include "Mixer.h"
#include "Tracks.h"
#include "AudioRecorder.h"

extern cookie_record play_cookie;
static float gScroll = 0;

void Tracks_Init()
{
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

// Pool -> микшер: сэмплы активной дорожки без тишины по краям + смещение
void Tracks_Stash()
{
	int a = Mix.Active();
	MixTrack *t = Mix.Track(a);
	if (t == NULL)
		return;
	const float rate = (Pool.frequency > 1.0f) ? Pool.frequency : t->rate;
	int64 first = 0, last = 0;
	if (Pool.sample_type == NONE || Pool.sample_memory == NULL || Pool.size <= 0
		|| !ContentRange(Pool.sample_memory, Pool.size, Pool.sample_type, first, last)){
		Mix.SetTrackData(a, NULL, 0, t->channels, rate);
		Mix.SetOffset(a, 0);
		return;
	}
	Mix.SetTrackData(a, Pool.sample_memory + first * Pool.sample_type, last - first + 1,
		Pool.sample_type, rate);
	Mix.SetOffset(a, (double)first / rate);
}

// конец самой дальней дорожки на ленте, секунды
static double ExtentSeconds()
{
	double e = 0;
	for (int i=0; i<MIX_MAX_TRACKS; i++){
		MixTrack *t = Mix.Track(i);
		if (!t || !t->data || t->frames <= 0 || t->rate <= 0)
			continue;
		double end = t->offset + (double)t->frames / t->rate;
		if (end > e) e = end;
	}
	return e;
}

// микшер -> Pool: дорожка index с тишиной по краям до конца ленты. Делает дорожку активной.
static bool Load(int index)
{
	MixTrack *t = Mix.Track(index);
	if (t == NULL)
		return false;
	const double ext = ExtentSeconds();
	const int ch = t->channels;
	const float rate = t->rate;

	if (ext <= 0.0){
		// проект пуст: Pool пустой, как при запуске
		Mix.SetActive(index);
		Pool.size = 0;
		Pool.sample_type = NONE;
		Pool.pad_lead = Pool.pad_tail = 0;
		return true;
	}

	const int64 F = (int64)ceil((ext + TIMELINE_MARGIN) * rate);
	float *mem = (float*)calloc((size_t)F * ch + 256, sizeof(float));
	if (mem == NULL)
		return false;

	int64 lead = 0, len = 0;
	if (t->data != NULL && t->frames > 0){
		lead = (int64)floor(t->offset * rate + 0.5);
		len = t->frames;
		if (lead + len > F) len = F - lead;
		if (len < 0) len = 0;
		memcpy(mem + lead * ch, t->data, (size_t)len * ch * sizeof(float));
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

	// данные теперь живут в Pool, копию в микшере освобождаем
	Mix.SetTrackData(index, NULL, 0, ch, rate);
	Mix.SetOffset(index, 0);
	Mix.SetActive(index);
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

bool Tracks_Activate(int index, double cursorTime)
{
	MixTrack *t = Mix.Track(index);
	if (t == NULL || index == Mix.Active())
		return t != NULL;
	if (Recorder.IsRecording())
		return false;

	ViewState view = CaptureView();
	if (cursorTime >= 0.0)
		view.tp = cursorTime;
	StopAll();
	Tracks_Stash();
	if (!Load(index)){
		Load(Mix.Active());			// вернуть прежнюю
		return false;
	}
	Hist.Reset();					// undo относится к одной дорожке
	RefreshUI(view);
	return true;
}

void Tracks_PlaceActive(double offsetSeconds)
{
	if (Pool.sample_type == NONE || Pool.sample_memory == NULL || Pool.size <= 0)
		return;
	int a = Mix.Active();
	if (!Mix.Track(a))
		return;
	Mix.SetTrackData(a, Pool.sample_memory, Pool.size, Pool.sample_type, Pool.frequency);
	Mix.SetOffset(a, offsetSeconds < 0 ? 0 : offsetSeconds);
	Load(a);
}

double Tracks_Offset(int index)
{
	MixTrack *t = Mix.Track(index);
	if (t == NULL)
		return 0;
	if (index != Mix.Active())
		return t->offset;
	int64 first = 0, last = 0;
	if (Pool.sample_type != NONE && ContentRange(Pool.sample_memory, Pool.size, Pool.sample_type, first, last))
		return (double)first / Pool.frequency;
	return 0;
}

// пересобрать активную дорожку под текущую ленту (например, после удаления дорожки)
static void Reload()
{
	ViewState view = CaptureView();
	StopAll();
	Tracks_Stash();
	if (Load(Mix.Active())){
		Hist.Reset();
		RefreshUI(view);
	}
}

bool Tracks_SetOffset(int index, double seconds)
{
	if (!Mix.Track(index) || Recorder.IsRecording())
		return false;
	if (seconds < 0) seconds = 0;
	ViewState view = CaptureView();
	StopAll();
	Tracks_Stash();					// активная дорожка без полей, лента пересчитывается ниже
	Mix.SetOffset(index, seconds);
	if (!Load(Mix.Active()))
		return false;
	Hist.Reset();
	RefreshUI(view);
	return true;
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

bool Tracks_Snapshot(int index, const float *&data, int64 &frames, int &channels, float &rate, double &offset)
{
	MixTrack *t = Mix.Track(index);
	if (!t) return false;
	if (index == Mix.Active()){
		int64 first = 0, last = 0;
		if (Pool.sample_type == NONE || Pool.sample_memory == NULL || Pool.size <= 0
			|| !ContentRange(Pool.sample_memory, Pool.size, Pool.sample_type, first, last))
			return false;
		channels = Pool.sample_type;
		data = Pool.sample_memory + first * channels;
		frames = last - first + 1;
		rate = Pool.frequency;
		offset = (double)first / rate;
		return true;
	}
	if (!t->data || t->frames <= 0) return false;
	data = t->data; frames = t->frames; channels = t->channels; rate = t->rate; offset = t->offset;
	return true;
}

void Tracks_BeginProject()
{
	StopAll();
	Hist.Reset();
	Mix.ResetAll();
	free(Pool.sample_memory);
	Pool.sample_memory = NULL;
	Pool.size = 0;
	Pool.sample_type = NONE;
	Pool.pad_lead = Pool.pad_tail = 0;
	Pool.m_format = media_format();				// формат определится при загрузке
	gScroll = 0;
}

void Tracks_FinishProject()
{
	ViewState none;
	none.had = false; none.tl = none.tr = none.tp = 0;
	Load(0);
	Hist.Reset();
	Pool.changed = false;
	RefreshUI(none);
}

bool Tracks_ActiveRange(int64 &first, int64 &last)
{
	if (Pool.sample_type == NONE || Pool.sample_memory == NULL || Pool.size <= 0)
		return false;
	return ContentRange(Pool.sample_memory, Pool.size, Pool.sample_type, first, last);
}

int Tracks_Add()
{
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
	if (a >= 1){
		Mix.RemoveTrack(a);
	}else{
		// дорожка 0 не удаляется физически - очищаем её
		Mix.SetTrackData(0, NULL, 0, 2, Pool.frequency);
		Mix.SetOffset(0, 0);
	}
	Reload();						// лента могла укоротиться: пересобрать активную
}

void Tracks_SaveRange(int64 &start, int64 &end)
{
	if (Pool.sample_type == NONE || Pool.sample_memory == NULL || Pool.size <= 1)
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
