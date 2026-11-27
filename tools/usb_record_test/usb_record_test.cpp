// usb_record_test: talks to a hmulti audio driver directly, with the same
// ioctl sequence as the multi_audio media add-on, but without the media
// layer. Prints which buffer cycles the driver returns and the peak level
// of the record buffers. NOT compiled or tested on Haiku by the author.
//
// Usage: usb_record_test /dev/audio/hmulti/usb/1 [seconds] [-p]
//   -p  also feed silence to the playback buffers (like the media add-on).
//   -s  feed a 440 Hz sine to the playback buffers (you should hear it):
//       2 s left only, 2 s right only, 2 s both, repeated.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <OS.h>
#include <hmulti_audio.h>

#define MAXCH 32
#define MAXBUF 32

static int fd = -1;

static status_t
call(int32 op, void* data, size_t size)
{
	if (ioctl(fd, op, data, size) != 0)
		return errno;
	return B_OK;
}

static uint32
pick_rate(uint32 r)
{
	for (uint32 c = B_SR_1536000; c != 0; c >>= 1)
		if (r & c)
			return c;
	return 0;
}

static uint32
pick_format(uint32 f)
{
	if (f & B_FMT_32BIT) return B_FMT_32BIT;
	if (f & B_FMT_24BIT) return B_FMT_24BIT;
	if (f & B_FMT_20BIT) return B_FMT_20BIT;
	if (f & B_FMT_18BIT) return B_FMT_18BIT;
	if (f & B_FMT_16BIT) return B_FMT_16BIT;
	if (f & B_FMT_8BIT_S) return B_FMT_8BIT_S;
	if (f & B_FMT_8BIT_U) return B_FMT_8BIT_U;
	return 0;
}

static float
peak_of(const char* base, size_t stride, uint32 frames, uint32 format)
{
	float peak = 0;
	for (uint32 i = 0; i < frames; i++) {
		const char* p = base + i * stride;
		float v = 0;
		switch (format) {
			case B_FMT_16BIT: v = *(const int16*)p / 32768.0f; break;
			case B_FMT_FLOAT: v = *(const float*)p; break;
			case B_FMT_8BIT_S: v = *(const int8*)p / 128.0f; break;
			case B_FMT_8BIT_U: v = (*(const uint8*)p - 128) / 128.0f; break;
			default: v = *(const int32*)p / 2147483648.0f; break;
		}
		if (fabsf(v) > peak)
			peak = fabsf(v);
	}
	return peak;
}

int
main(int argc, char** argv)
{
	const char* path = argc > 1 ? argv[1] : "/dev/audio/hmulti/usb/1";
	int seconds = argc > 2 ? atoi(argv[2]) : 10;
	bool feedPlayback = false;
	bool sine = false;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-p") == 0)
			feedPlayback = true;
		if (strcmp(argv[i], "-s") == 0)
			feedPlayback = sine = true;
	}
	double phase = 0;
	int lastMode = -1;

	fd = open(path, O_WRONLY);
	if (fd < 0) {
		printf("open %s: %s\n", path, strerror(errno));
		return 1;
	}

	static multi_channel_info chInfo[MAXCH];
	multi_description desc;
	memset(&desc, 0, sizeof(desc));
	desc.info_size = sizeof(desc);
	desc.request_channel_count = MAXCH;
	desc.channels = chInfo;
	status_t st = call(B_MULTI_GET_DESCRIPTION, &desc, sizeof(desc));
	if (st != B_OK) { printf("GET_DESCRIPTION: %s\n", strerror(st)); return 1; }
	printf("name '%s' out=%d in=%d outbus=%d inbus=%d\n", desc.friendly_name,
		(int)desc.output_channel_count, (int)desc.input_channel_count,
		(int)desc.output_bus_channel_count, (int)desc.input_bus_channel_count);
	printf("rates out 0x%x in 0x%x, formats out 0x%x in 0x%x\n",
		desc.output_rates, desc.input_rates, desc.output_formats,
		desc.input_formats);

	int total = desc.output_channel_count + desc.input_channel_count;
	uint32 bits = 0;
	multi_channel_enable en;
	memset(&en, 0, sizeof(en));
	en.info_size = sizeof(en);
	en.enable_bits = (uchar*)&bits;
	st = call(B_MULTI_GET_ENABLED_CHANNELS, &en, sizeof(en));
	printf("GET_ENABLED_CHANNELS: %s\n", strerror(st));
	bits = total >= 32 ? 0xffffffff : ((1u << total) - 1);
	en.lock_source = B_MULTI_LOCK_INTERNAL;
	st = call(B_MULTI_SET_ENABLED_CHANNELS, &en, sizeof(en));
	printf("SET_ENABLED_CHANNELS 0x%x: %s\n", bits, strerror(st));

	multi_format_info fi;
	memset(&fi, 0, sizeof(fi));
	fi.info_size = sizeof(fi);
	fi.output.rate = pick_rate(desc.output_rates);
	fi.output.format = pick_format(desc.output_formats);
	fi.input.rate = pick_rate(desc.input_rates);
	fi.input.format = pick_format(desc.input_formats);
	st = call(B_MULTI_SET_GLOBAL_FORMAT, &fi, sizeof(fi));
	printf("SET_GLOBAL_FORMAT: %s\n", strerror(st));
	st = call(B_MULTI_GET_GLOBAL_FORMAT, &fi, sizeof(fi));
	printf("GET_GLOBAL_FORMAT: %s, in rate 0x%x fmt 0x%x, out rate 0x%x fmt 0x%x\n",
		strerror(st), fi.input.rate, fi.input.format, fi.output.rate,
		fi.output.format);

	static buffer_desc playDesc[MAXBUF * MAXCH], recDesc[MAXBUF * MAXCH];
	static buffer_desc* playPtr[MAXBUF];
	static buffer_desc* recPtr[MAXBUF];
	for (int i = 0; i < MAXBUF; i++) {
		playPtr[i] = &playDesc[i * MAXCH];
		recPtr[i] = &recDesc[i * MAXCH];
	}
	multi_buffer_list bl;
	memset(&bl, 0, sizeof(bl));
	bl.info_size = sizeof(bl);
	bl.request_playback_buffers = MAXBUF;
	bl.request_playback_channels = desc.output_channel_count;
	bl.playback_buffers = playPtr;
	bl.request_record_buffers = MAXBUF;
	bl.request_record_channels = desc.input_channel_count;
	bl.record_buffers = recPtr;
	st = call(B_MULTI_GET_BUFFERS, &bl, sizeof(bl));
	printf("GET_BUFFERS: %s play %d x %d x %u frames, rec %d x %d x %u frames\n",
		strerror(st), (int)bl.return_playback_buffers,
		(int)bl.return_playback_channels, bl.return_playback_buffer_size,
		(int)bl.return_record_buffers, (int)bl.return_record_channels,
		bl.return_record_buffer_size);
	if (st != B_OK)
		return 1;
	for (int b = 0; b < bl.return_record_buffers && b < 4; b++)
		printf("  rec[%d][0] base %p stride %lu\n", b, recPtr[b][0].base,
			(unsigned long)recPtr[b][0].stride);

	multi_buffer_info bi;
	memset(&bi, 0, sizeof(bi));
	bi.info_size = sizeof(bi);

	bigtime_t start = system_time();
	int n = 0, lastPlay = -2, lastRec = -2;
	while (system_time() - start < seconds * 1000000LL) {
		st = call(B_MULTI_BUFFER_EXCHANGE, &bi, sizeof(bi));
		if (st != B_OK) {
			printf("BUFFER_EXCHANGE: %s\n", strerror(st));
			break;
		}
		n++;
		bool recNew = bi.record_buffer_cycle != lastRec;
		bool playNew = bi.playback_buffer_cycle != lastPlay;
		float peak = -1;
		if (recNew && bl.return_record_channels > 0
			&& bi.record_buffer_cycle >= 0
			&& bi.record_buffer_cycle < bl.return_record_buffers) {
			buffer_desc& d = recPtr[bi.record_buffer_cycle][0];
			peak = peak_of(d.base, d.stride, bl.return_record_buffer_size,
				fi.input.format);
		}
		if (feedPlayback && playNew && bl.return_playback_channels > 0
			&& bi.playback_buffer_cycle >= 0
			&& bi.playback_buffer_cycle < bl.return_playback_buffers) {
			for (int c = 0; c < bl.return_playback_channels; c++) {
				buffer_desc& d = playPtr[bi.playback_buffer_cycle][c];
				double p = phase;
				// which channel sounds: 0 left only, 1 right only, 2 both
				int mode = (int)(((system_time() - start) / 2000000) % 3);
				if (c == 0 && mode != lastMode) {
					printf("-> %s\n", mode == 0 ? "LEFT only" : mode == 1
						? "RIGHT only" : "BOTH");
					lastMode = mode;
				}
				bool on = (mode == 2) || (mode == c);
				for (uint32 i = 0; i < bl.return_playback_buffer_size; i++) {
					double v = (sine && on) ? 0.3 * sin(p) : 0.0;
					p += 2 * M_PI * 440.0 / 48000.0;
					char* at = d.base + i * d.stride;
					switch (fi.output.format) {
						case B_FMT_16BIT: *(int16*)at = (int16)(v * 32767); break;
						case B_FMT_FLOAT: *(float*)at = (float)v; break;
						case B_FMT_8BIT_S: *(int8*)at = (int8)(v * 127); break;
						case B_FMT_8BIT_U: *(uint8*)at = (uint8)(128 + v * 127); break;
						default: *(int32*)at = (int32)(v * 2147483647.0); break;
					}
				}
				if (c == bl.return_playback_channels - 1)
					phase = p;
			}
		}
		if (n <= 40 || n % 50 == 0)
			printf("#%d t=%lldms play cycle %d%s rec cycle %d%s frames %lld/%lld peak %.4f\n",
				n, (long long)((system_time() - start) / 1000),
				(int)bi.playback_buffer_cycle, playNew ? "*" : "",
				(int)bi.record_buffer_cycle, recNew ? "*" : "",
				(long long)bi.played_frames_count,
				(long long)bi.recorded_frames_count, peak);
		lastPlay = bi.playback_buffer_cycle;
		lastRec = bi.record_buffer_cycle;
	}
	printf("done: %d exchanges\n", n);
	call(B_MULTI_BUFFER_FORCE_STOP, NULL, 0);
	close(fd);
	return 0;
}
