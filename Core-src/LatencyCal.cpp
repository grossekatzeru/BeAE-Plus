#include <MediaDefs.h>
#include <Message.h>
#include <OS.h>
#include <SoundPlayer.h>
#include <String.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <vector>

#include "Globals.h"
#include "AudioRecorder.h"
#include "LatencyCal.h"
#include "LatencyCore.h"

namespace {

struct Cookie {
	long long	frame;
	double		rate;
	volatile bool silent;
};

// поток воспроизведения: выдаёт тестовый сигнал в оба канала
void PlayHook(void *cookie, void *buffer, size_t size, const media_raw_audio_format &format)
{
	Cookie *c = (Cookie *)cookie;
	float *out = (float *)buffer;
	const int ch = format.channel_count > 0 ? format.channel_count : 2;
	const size_t frames = size / (sizeof(float) * ch);
	for (size_t i = 0; i < frames; i++){
		const float s = c->silent ? 0.0f : Latency::BurstSample(c->frame++, c->rate);
		for (int k = 0; k < ch; k++)
			out[i * ch + k] = s;
	}
}

struct Job {
	BMessenger target;
};

void Reply(BMessenger &target, bool ok, float ms, const char *err)
{
	BMessage m(LATENCY_RESULT);
	m.AddBool("ok", ok);
	m.AddFloat("ms", ms);
	m.AddString("err", err);
	target.SendMessage(&m);
}

int32 Run(void *data)
{
	Job *job = (Job *)data;
	BMessenger target = job->target;
	delete job;

	if (Recorder.IsRecording() || Recorder.IsListening() || Pool.IsPlaying()){
		Reply(target, false, 0, "CAL_ERR_BUSY");
		return 0;
	}

	// сначала создаём проигрыватель (как в настоящей записи он уже существует),
	// затем запускаем запись и сразу воспроизведение
	media_raw_audio_format format;
	memset(&format, 0, sizeof(format));
	format.frame_rate = (Pool.system_frequency > 1.0f) ? Pool.system_frequency : 44100.0f;
	format.format = media_raw_audio_format::B_AUDIO_FLOAT;
	format.channel_count = 2;
	format.buffer_size = Prefs.buffer_size;			// тот же размер буфера, что у основного проигрывателя
	format.byte_order = B_MEDIA_HOST_ENDIAN;
	Cookie cookie;
	cookie.frame = 0;
	cookie.silent = false;
	cookie.rate = format.frame_rate;
	BSoundPlayer *player = new BSoundPlayer(&format, "BeAE latency", PlayHook, NULL, &cookie);
	if (player->InitCheck() != B_OK){
		delete player;
		Reply(target, false, 0, "CAL_ERR_PLAYER");
		return 0;
	}
	cookie.rate = player->Format().frame_rate;

	// «прогрев»: основной проигрыватель при записи уже работал раньше (Stop -> Start),
	// а у только что созданного первый Start дольше. Прогоняем его вхолостую (тишина) и останавливаем.
	cookie.silent = true;
	player->Start();
	player->SetHasData(true);
	snooze(700000);
	player->SetHasData(false);
	player->Stop();
	snooze(100000);
	cookie.frame = 0;
	cookie.silent = false;

	// Замеры бывают двух «кучек» (около 180 и около 235 мс — зависит от фазы буфера),
	// поэтому делаем несколько попыток и берём нижнюю кучку (2-й по величине снизу результат).
	const int kTrials = 5;
	std::vector<double> results;
	BString error;
	for (int t = 0; t < kTrials; t++){
		cookie.frame = 0;
		cookie.silent = false;
		Recorder.Clear();
		if (Recorder.Start(&error) != B_OK){
			if (results.empty()){
				delete player;
				Recorder.Clear();
				Reply(target, false, 0, error.Length() > 0 ? error.String() : "REC_ERR_START");
				return 0;
			}
			break;
		}

		// воспроизведение стартует сразу после запуска записи, как при настоящей записи с дорожками
		player->Start();
		player->SetHasData(true);
		snooze((bigtime_t)(Latency::kTotalSeconds * 1000000.0));
		player->SetHasData(false);
		player->Stop();
		Recorder.Stop();

		const long long frames = Recorder.Frames();
		const int ch = Recorder.Channels();
		const double rate = Recorder.Rate();
		float *mem = Recorder.CopyData(0);
		double ms = 0;
		if (mem != NULL && frames > 0 && rate > 1.0){
			std::vector<float> mono((size_t)frames);
			for (long long i = 0; i < frames; i++)
				mono[(size_t)i] = (ch == 2) ? (mem[i * 2] + mem[i * 2 + 1]) * 0.5f : mem[i];
			if (Latency::MeasureMs(&mono[0], frames, rate, &ms))
				results.push_back(ms);
		}
		free(mem);
		Recorder.Clear();
		snooze(300000);
	}
	delete player;

	if (results.empty()){
		Reply(target, false, 0, "CAL_ERR_NO_SIGNAL");
	}else{
		std::sort(results.begin(), results.end());
		const size_t idx = results.size() > 1 ? 1 : 0;
		Reply(target, true, (float)results[idx], "");
	}
	return 0;
}

}	// namespace

void LatencyCalStart(BMessenger target)
{
	Job *job = new Job;
	job->target = target;
	thread_id t = spawn_thread(Run, "latency calibration", B_NORMAL_PRIORITY, job);
	if (t < B_OK){
		delete job;
		return;
	}
	resume_thread(t);
}
