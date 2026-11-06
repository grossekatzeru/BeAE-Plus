#include <MediaDefs.h>
#include <MediaFile.h>
#include <MediaTrack.h>
#include <stdlib.h>
#include <string.h>
#include <new>
#include <vector>

#include "FileImport.h"
#include "ByteSwap.h"

// один отсчёт буфера в формате fmt, приведённый к float -1..1
static float ReadSample(const char *buf, uint32 fmt, int64 idx)
{
	float x = 0;
	switch (fmt){
	case media_raw_audio_format::B_AUDIO_FLOAT:
		x = ((const float*)buf)[idx];
		break;
	case media_raw_audio_format::B_AUDIO_INT:
		x = (float)((double)((const int32*)buf)[idx] / 2147483648.0);
		break;
	case media_raw_audio_format::B_AUDIO_SHORT:
		x = ((const int16*)buf)[idx] / 32767.0f;
		break;
	case media_raw_audio_format::B_AUDIO_UCHAR:
		x = ((const uint8*)buf)[idx] / 127.0f - 1.0f;
		break;
	case media_raw_audio_format::B_AUDIO_CHAR:
		x = ((const int8*)buf)[idx] / 127.0f;
		break;
	}
	if (x < -1.0f) x = -1.0f;
	else if (x > 1.0f) x = 1.0f;
	return x;
}

int ImportAudioFile(const entry_ref &ref, SourceRef &out)
{
	out.reset();
	BMediaFile file(&ref);
	if (file.InitCheck() != B_OK)
		return IMPORT_NO_AUDIO;

	BMediaTrack *track = NULL;
	media_format format;
	memset(&format, 0, sizeof(format));
	const int32 tracks = file.CountTracks();
	for (int32 i = 0; i < tracks; i++){
		BMediaTrack *t = file.TrackAt(i);
		if (t == NULL) continue;
		media_format f;
		memset(&f, 0, sizeof(f));
		t->EncodedFormat(&f);
		if (track == NULL && f.IsAudio()){
			track = t;
			format = f;
			t->DecodedFormat(&format);		// так же, как при открытии файла
		}else{
			file.ReleaseTrack(t);
		}
	}
	if (track == NULL)
		return IMPORT_NO_AUDIO;

	int result = IMPORT_NO_AUDIO;
	const media_raw_audio_format &raw = format.u.raw_audio;
	const int channels = (int)raw.channel_count;
	const int64 frameCount = track->CountFrames();
	const size_t bufSize = raw.buffer_size;
	const uint32 fmt = raw.format;
	char *buffer = NULL;
	ByteSwap *swap = NULL;

	if (format.type == B_MEDIA_RAW_AUDIO && channels >= 1 && frameCount > 0 && bufSize > 0 && raw.frame_rate > 0
		&& (fmt == media_raw_audio_format::B_AUDIO_FLOAT || fmt == media_raw_audio_format::B_AUDIO_INT
			|| fmt == media_raw_audio_format::B_AUDIO_SHORT || fmt == media_raw_audio_format::B_AUDIO_UCHAR
			|| fmt == media_raw_audio_format::B_AUDIO_CHAR)){
		const int outCh = channels > 1 ? 2 : 1;
		std::vector<float> data;
		bool memOk = true;
		try {
			data.resize((size_t)frameCount * outCh);
		} catch (std::bad_alloc &) {
			memOk = false;
		}
		buffer = (char*)malloc(bufSize);
		if (!memOk || buffer == NULL){
			result = IMPORT_NO_MEMORY;
		}else{
			if (raw.byte_order != B_MEDIA_HOST_ENDIAN
				&& (fmt == media_raw_audio_format::B_AUDIO_FLOAT || fmt == media_raw_audio_format::B_AUDIO_INT
					|| fmt == media_raw_audio_format::B_AUDIO_SHORT))
				swap = new ByteSwap(fmt);
			int64 done = 0;
			while (done < frameCount){
				int64 framesRead = 0;
				media_header mh;
				memset(buffer, 0, bufSize);
				if (track->ReadFrames(buffer, &framesRead, &mh) != B_OK || framesRead <= 0)
					break;
				if (swap) swap->Swap(buffer, bufSize);
				if (done + framesRead > frameCount) framesRead = frameCount - done;
				for (int64 f = 0; f < framesRead; f++)
					for (int c = 0; c < outCh; c++)
						data[(size_t)((done + f) * outCh + c)] = ReadSample(buffer, fmt, f * channels + c);
				done += framesRead;
			}
			if (done > 0){
				out = MakeSource(&data[0], done, outCh, raw.frame_rate);
				result = out ? IMPORT_OK : IMPORT_NO_MEMORY;
			}
		}
	}
	delete swap;
	free(buffer);
	file.ReleaseAllTracks();
	return result;
}
