/*
 * AudioRecorder - запись звука с аудиовхода Haiku в память BeAE.
 * Схема подключения взята из Haiku Softphone: узел входа через BMediaRoster::GetAudioInput(),
 * подключение BMediaRecorder к первому свободному выходу, приём формата «как есть».
 */
#include <MediaRoster.h>
#include <MediaRecorder.h>
#include <Autolock.h>
#include <OS.h>
#include <stdlib.h>
#include <string.h>

#include "AudioRecorder.h"
#include "RecordConvert.h"

static const size_t kChunkFloats = 1 << 20;		// 4 МБ на блок

AudioRecorder Recorder;

AudioRecorder::AudioRecorder()
	:
	fRecorder(NULL),
	fHaveNode(false),
	fRecording(false),
	fChunkUsed(kChunkFloats),
	fFrames(0),
	fRate(44100.0f),
	fOutChannels(2),
	fSrcChannels(2),
	fSrcFormat(media_raw_audio_format::B_AUDIO_FLOAT),
	fSwap(false),
	fFormatKnown(false),
	fOutOfMemory(false),
	fPeakL(0.0f),
	fPeakR(0.0f)
{
}

AudioRecorder::~AudioRecorder()
{
	Stop();
	Clear();
}

void AudioRecorder::Clear()
{
	BAutolock lock(fLock);
	for (size_t i = 0; i < fChunks.size(); ++i)
		free(fChunks[i]);
	fChunks.clear();
	fChunkUsed = kChunkFloats;
	fFrames = 0;
	fFormatKnown = false;
	fOutOfMemory = false;
}

status_t AudioRecorder::Start(BString *error)
{
	if (fRecording)
		return B_OK;
	Clear();
	fPeakL = fPeakR = 0.0f;

	BMediaRoster *roster = BMediaRoster::Roster();
	if (roster == NULL) {
		if (error) *error = "REC_ERR_ROSTER";
		return B_ERROR;
	}

	status_t err = roster->GetAudioInput(&fInputNode);
	if (err != B_OK) {
		if (error) *error = "REC_ERR_NO_INPUT";
		return err;
	}
	fHaveNode = true;

	fRecorder = new BMediaRecorder("BeAE recorder", B_MEDIA_RAW_AUDIO);
	if (fRecorder->InitCheck() != B_OK) {
		if (error) *error = "REC_ERR_RECORDER";
		delete fRecorder;
		fRecorder = NULL;
		roster->ReleaseNode(fInputNode);
		fHaveNode = false;
		return B_ERROR;
	}

	// принимаем любой «сырой» формат: частоту и каналы выбирает вход
	media_format accept;
	accept.type = B_MEDIA_RAW_AUDIO;
	accept.u.raw_audio = media_raw_audio_format::wildcard;
	fRecorder->SetAcceptedFormat(accept);
	fRecorder->SetHooks(&AudioRecorder::_RecordHook, NULL, this);

	media_output output;
	int32 count = 0;
	err = roster->GetFreeOutputsFor(fInputNode, &output, 1, &count, B_MEDIA_RAW_AUDIO);
	if (err != B_OK || count < 1) {
		if (error) *error = "REC_ERR_NO_FREE_OUTPUT";
		if (err == B_OK) err = B_ERROR;
		goto fail;
	}

	{
		media_format fmt;
		fmt.type = B_MEDIA_RAW_AUDIO;
		fmt.u.raw_audio = output.format.u.raw_audio;

		err = fRecorder->Connect(fInputNode, &output, &fmt);
		if (err != B_OK) {
			if (error) *error = "REC_ERR_CONNECT";
			fRecorder->SetHooks(NULL, NULL, NULL);
			goto fail;
		}
	}

	fRecording = true;
	err = fRecorder->Start();
	if (err != B_OK) {
		fRecording = false;
		if (error) *error = "REC_ERR_START";
		fRecorder->Disconnect();
		fRecorder->SetHooks(NULL, NULL, NULL);
		goto fail;
	}
	return B_OK;

fail:
	delete fRecorder;
	fRecorder = NULL;
	roster->ReleaseNode(fInputNode);
	fHaveNode = false;
	return err;
}

void AudioRecorder::Stop()
{
	fRecording = false;
	if (fRecorder) {
		fRecorder->Stop(true);
		fRecorder->Disconnect();
		fRecorder->SetHooks(NULL, NULL, NULL);
		delete fRecorder;
		fRecorder = NULL;
	}
	if (fHaveNode) {
		BMediaRoster *roster = BMediaRoster::Roster();
		if (roster)
			roster->ReleaseNode(fInputNode);	// отпускаем микрофон для остальной системы
		fHaveNode = false;
		snooze(50000);
	}
}

void AudioRecorder::ReadLevels(float *left, float *right)
{
	if (left)	*left = fPeakL;
	if (right)	*right = fPeakR;
	fPeakL = fPeakR = 0.0f;
}

void AudioRecorder::_RecordHook(void *cookie, bigtime_t, void *data, size_t size,
								const media_format &format)
{
	static_cast<AudioRecorder *>(cookie)->OnBuffer(data, size, format);
}

void AudioRecorder::OnBuffer(void *data, size_t size, const media_format &format)
{
	if (!fRecording || fOutOfMemory)
		return;
	if (format.type != B_MEDIA_RAW_AUDIO)
		return;

	const media_raw_audio_format &raw = format.u.raw_audio;
	const int bps = RecConv::BytesPerSample(raw.format);
	const int srcCh = raw.channel_count;
	if (bps == 0 || srcCh < 1)
		return;

	BAutolock lock(fLock);

	if (!fFormatKnown) {
		fRate = raw.frame_rate > 0 ? raw.frame_rate : 44100.0f;
		fSrcFormat = raw.format;
		fSrcChannels = srcCh;
		fOutChannels = (srcCh == 1) ? 1 : 2;
		fSwap = (raw.byte_order != B_MEDIA_HOST_ENDIAN) && bps > 1;
		fFormatKnown = true;
	}
	// если формат внезапно изменился — игнорируем такие буферы (запись продолжается в первом формате)
	if ((int32)raw.channel_count != fSrcChannels || raw.format != fSrcFormat)
		return;

	size_t frames = size / ((size_t)bps * srcCh);
	if (frames == 0)
		return;

	float peakL = 0.0f, peakR = 0.0f;

	// временный буфер на стеке мал, поэтому конвертируем сразу в блоки
	size_t done = 0;						// обработано кадров
	const uint8 *src = (const uint8 *)data;
	const size_t frameBytes = (size_t)bps * srcCh;
	while (done < frames) {
		if (fChunkUsed >= kChunkFloats || fChunks.empty()
			|| (kChunkFloats - fChunkUsed) < (size_t)fOutChannels) {
			float *chunk = (float *)malloc(kChunkFloats * sizeof(float));
			if (chunk == NULL) {
				fOutOfMemory = true;			// остановимся на том, что уже есть
				return;
			}
			fChunks.push_back(chunk);
			fChunkUsed = 0;
		}
		size_t room = (kChunkFloats - fChunkUsed) / fOutChannels;	// кадров в блоке
		size_t n = frames - done;
		if (n > room)
			n = room;
		float pl, pr;
		RecConv::ConvertFrames(src + done * frameBytes, n, raw.format, srcCh, fSwap,
							   fOutChannels, fChunks.back() + fChunkUsed, &pl, &pr);
		if (pl > peakL) peakL = pl;
		if (pr > peakR) peakR = pr;
		fChunkUsed += n * fOutChannels;
		done += n;
	}
	fFrames += (int64)frames;

	if (peakL > fPeakL) fPeakL = peakL;
	if (peakR > fPeakR) fPeakR = peakR;
}

float* AudioRecorder::CopyData(size_t extraFloats) const
{
	if (fFrames <= 0)
		return NULL;
	size_t total = (size_t)fFrames * fOutChannels;
	float *mem = (float *)malloc((total + extraFloats) * sizeof(float));
	if (mem == NULL)
		return NULL;
	size_t left = total, pos = 0;
	for (size_t i = 0; i < fChunks.size() && left > 0; ++i) {
		size_t n = (i + 1 == fChunks.size()) ? fChunkUsed : kChunkFloats;
		if (n > left) n = left;
		memcpy(mem + pos, fChunks[i], n * sizeof(float));
		pos += n;
		left -= n;
	}
	memset(mem + total, 0, extraFloats * sizeof(float));
	return mem;
}
