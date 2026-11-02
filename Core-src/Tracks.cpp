#include <StorageKit.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <Window.h>

#include "CommonPool.h"
#include "Globals.h"
#include "History.h"
#include "Mixer.h"
#include "Tracks.h"
#include "AudioRecorder.h"

extern cookie_record play_cookie;

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

// Pool -> микшер (данные активной дорожки)
void Tracks_Stash()
{
	int a = Mix.Active();
	if (Pool.sample_type == NONE || Pool.sample_memory == NULL || Pool.size <= 0){
		MixTrack *t = Mix.Track(a);
		Mix.SetTrackData(a, NULL, 0, t ? t->channels : 2);
	}else{
		Mix.SetTrackData(a, Pool.sample_memory, Pool.size, Pool.sample_type);
	}
}

bool Tracks_Activate(int index)
{
	MixTrack *t = Mix.Track(index);
	if (t == NULL || index == Mix.Active())
		return t != NULL;
	if (Recorder.IsRecording())
		return false;

	// остановить воспроизведение, чтобы не менять буфер под callback-ом
	if (Pool.IsPlaying()){
		Pool.StopPlaying();
		if (Pool.mainWindow)
			Pool.mainWindow->PostMessage(TRANSPORT_STOP);
	}
	if (Pool.player)
		Pool.player->Stop();

	Tracks_Stash();

	// микшер -> Pool
	const int ch = t->channels;
	const int64 frames = t->frames;
	if (frames > 0 && t->data != NULL){
		float *mem = (float*)realloc(Pool.sample_memory, (size_t)frames*ch*4 + 1024);
		if (mem == NULL)
			return false;
		Pool.sample_memory = mem;
		memcpy(Pool.sample_memory, t->data, (size_t)frames*ch*4);
		memset((char*)Pool.sample_memory + (size_t)frames*ch*4, 0, 1024);
		Pool.size = frames;
		Pool.sample_type = (ch == 1) ? MONO : STEREO;
		Pool.m_format.u.raw_audio.channel_count = Pool.sample_type;
	}else{
		Pool.size = 0;
		Pool.sample_type = NONE;
	}

	Mix.SetActive(index);

	Pool.selection = NONE;
	Pool.r_pointer = Pool.size;
	Pool.pointer = 0;
	Pool.r_sel_pointer = 0;
	Pool.l_pointer = 0;
	play_cookie.mem = Pool.sample_memory;
	play_cookie.start_mem = Pool.sample_memory;
	play_cookie.end_mem = Pool.sample_memory + Pool.size * (Pool.sample_type == NONE ? 1 : Pool.sample_type);
	play_cookie.add = 0;

	Hist.Reset();					// undo относится к одной дорожке
	if (Pool.sample_type != NONE)
		Pool.ResetIndexView();
	Pool.sample_view_dirty = true;
	Pool.update_draw_cache = true;
	Pool.update_index = true;
	Pool.update_peak = true;
	Pool.RedrawWindow();
	Pool.UpdateToolBar();
	return true;
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
	if (a < 1 && Mix.TrackCount() < 2)
		return;
	int count = 0;
	for (int i=0; i<MIX_MAX_TRACKS; i++) if (Mix.Track(i)) count++;
	if (count < 2)
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
		Mix.SetTrackData(0, NULL, 0, 2);
	}
}
