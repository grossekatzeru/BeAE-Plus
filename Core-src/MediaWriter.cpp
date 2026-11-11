#include "MediaWriter.h"

#include <Entry.h>
#include <File.h>
#include <MediaTrack.h>
#include <NodeInfo.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

BString MediaNameWithExtension(const char *name, const media_file_format *ff)
{
	BString n(name);
	if (ff != NULL && ff->file_extension[0] != 0) {
		BString dot(".");
		dot << ff->file_extension;
		if (n.Length() < dot.Length() || n.IFindLast(dot.String()) != n.Length() - dot.Length())
			n << dot;
	}
	return n;
}

static inline double Clip1(float v)
{
	return v > 1.0f ? 1.0 : (v < -1.0f ? -1.0 : (double)v);	// перегруз не должен заворачиваться через край целого
}

bool MediaWriteFloat(const entry_ref &dirRef, const char *name, const media_file_format &ff, const media_codec_info *ci,
	const float *frames, long long count, int channels, float rate, BString &err)
{
	BDirectory dir(&dirRef);
	if (dir.InitCheck() != B_OK) { err = "Cannot open the folder"; return false; }
	const BString fileName = MediaNameWithExtension(name, &ff);
	BFile newFile;
	if (dir.CreateFile(fileName.String(), &newFile) != B_OK) { err = "Cannot create the file"; return false; }
	BEntry entry(&dir, fileName.String());
	entry_ref ref;
	if (entry.InitCheck() != B_OK || entry.GetRef(&ref) != B_OK) { err = "Cannot create the file"; return false; }

	bool ok = false;
	{
		media_format format;
		memset(&format, 0, sizeof(format));
		format.type = B_MEDIA_RAW_AUDIO;
		media_raw_audio_format *raf = &format.u.raw_audio;
		raf->frame_rate = rate;
		raf->channel_count = channels;
		raf->format = media_raw_audio_format::B_AUDIO_FLOAT;
		raf->byte_order = B_MEDIA_HOST_ENDIAN;
		raf->buffer_size = 0;

		media_file_format fileFormat = ff;
		media_codec_info codec;
		if (ci) codec = *ci;
		BMediaFile file(&ref, &fileFormat, B_MEDIA_FILE_REPLACE_MODE);
		if (file.InitCheck() != B_OK) {
			err = "The file format cannot be written";
		} else {
			BMediaTrack *track = ci ? file.CreateTrack(&format, &codec) : file.CreateTrack(&format);
			if (track == NULL) {
				err = "The codec does not accept this audio";
			} else if (file.CommitHeader() != B_OK) {
				err = "Cannot write the file header";
			} else {
				raf = &format.u.raw_audio;			// кодек мог поменять формат (например, на 16 бит)
				const int ch = raf->channel_count;
				const int bytes = raf->format & 0xf;
				const int frameSize = bytes * ch;
				if (frameSize <= 0 || ch != channels) {
					err = "The codec uses an unsupported audio format";
				} else {
					size_t bufSize = raf->buffer_size;
					if (bufSize < (size_t)frameSize * 64) bufSize = (size_t)frameSize * 2048;
					const long long step = bufSize / frameSize;
					char *buffer = (char *)malloc(bufSize);
					if (buffer == NULL) {
						err = "Not enough memory";
					} else {
						ok = true;
						for (long long i = 0; i < count && ok; i += step) {
							const long long block = (count - i < step) ? count - i : step;
							const float *src = frames + i * channels;
							const long long n = block * ch;
							switch (raf->format) {
							case 0x24: {		// float
								float *d = (float *)buffer;
								for (long long k = 0; k < n; k++) d[k] = src[k];
								break;
							}
							case 0x4: {			// int32
								int32 *d = (int32 *)buffer;
								for (long long k = 0; k < n; k++) d[k] = (int32)lrint(Clip1(src[k]) * 2147483647.0);
								break;
							}
							case 0x2: {			// int16
								int16 *d = (int16 *)buffer;
								for (long long k = 0; k < n; k++) d[k] = (int16)lrint(Clip1(src[k]) * 32767.0);
								break;
							}
							case 0x11: {		// uint8
								uint8 *d = (uint8 *)buffer;
								for (long long k = 0; k < n; k++) d[k] = (uint8)(lrint(Clip1(src[k]) * 127.0) + 128);
								break;
							}
							case 0x1: {			// int8
								int8 *d = (int8 *)buffer;
								for (long long k = 0; k < n; k++) d[k] = (int8)lrint(Clip1(src[k]) * 127.0);
								break;
							}
							default:
								err = "The codec uses an unsupported sample format";
								ok = false;
							}
							if (ok && track->WriteFrames(buffer, block) != B_OK) {
								err = "Writing to the file failed";
								ok = false;
							}
						}
						free(buffer);
						if (ok) track->Flush();
					}
				}
			}
			file.CloseFile();					// только здесь дописывается заголовок (размеры)
		}
	}
	if (ok && ff.mime_type[0] != 0) {
		BNodeInfo info(&newFile);
		info.SetType(ff.mime_type);
	}
	off_t size = 0;
	if (!ok || (newFile.GetSize(&size) == B_OK && size == 0))
		entry.Remove();						// пустой или недописанный файл не оставляем
	return ok;
}
