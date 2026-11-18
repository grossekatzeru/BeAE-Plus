#include "ToneNode.h"

#include <Buffer.h>
#include <MediaRoster.h>
#include <TimeSource.h>

#include <math.h>
#include <stdio.h>
#include <string.h>


static const float kRate = 48000.0f;
static const int32 kChannels = 2;


ToneNode::ToneNode(const char* name, float frequency)
	:
	BMediaNode(name),
	BMediaEventLooper(),
	BBufferProducer(B_MEDIA_RAW_AUDIO),
	fBufferGroup(NULL),
	fConnected(false),
	fEnabled(false),
	fRunning(false),
	fThread(-1),
	fPerformanceBase(0),
	fFramesSent(0),
	fPhase(0.0),
	fFrequency(frequency),
	fLevel(0.5f)
{
	fOutput.destination = media_destination::null;
	fFormat = media_raw_audio_format::wildcard;
}


ToneNode::~ToneNode()
{
	_HandleStop();
	Quit();
}


BMediaAddOn*
ToneNode::AddOn(int32* id) const
{
	return NULL;
}


status_t
ToneNode::HandleMessage(int32 message, const void* data, size_t size)
{
	return B_ERROR;
}


void
ToneNode::NodeRegistered()
{
	fOutput.node = Node();
	fOutput.source.port = ControlPort();
	fOutput.source.id = 0;
	fOutput.destination = media_destination::null;
	strlcpy(fOutput.name, Name(), sizeof(fOutput.name));

	fOutput.format.type = B_MEDIA_RAW_AUDIO;
	fOutput.format.u.raw_audio = media_raw_audio_format::wildcard;
	fOutput.format.u.raw_audio.format = media_raw_audio_format::B_AUDIO_FLOAT;
	fOutput.format.u.raw_audio.frame_rate = kRate;
	fOutput.format.u.raw_audio.channel_count = kChannels;
	fOutput.format.u.raw_audio.byte_order = B_MEDIA_HOST_ENDIAN;
	fOutput.format.u.raw_audio.buffer_size = kFrames * kChannels * sizeof(float);

	SetPriority(B_REAL_TIME_PRIORITY);
	Run();
}


void
ToneNode::HandleEvent(const media_timed_event* event, bigtime_t lateness, bool realTimeEvent)
{
	switch (event->type) {
		case BTimedEventQueue::B_START:
			_HandleStart(event->event_time);
			break;
		case BTimedEventQueue::B_STOP:
			_HandleStop();
			break;
		default:
			break;
	}
}


status_t
ToneNode::FormatSuggestionRequested(media_type type, int32 quality, media_format* format)
{
	if (type != B_MEDIA_RAW_AUDIO && type != B_MEDIA_UNKNOWN_TYPE)
		return B_MEDIA_BAD_FORMAT;
	*format = fOutput.format;
	return B_OK;
}


status_t
ToneNode::FormatProposal(const media_source& output, media_format* format)
{
	if (format == NULL)
		return B_BAD_VALUE;
	if (output != fOutput.source)
		return B_MEDIA_BAD_SOURCE;
	status_t err = format_is_compatible(*format, fOutput.format) ? B_OK : B_MEDIA_BAD_FORMAT;
	*format = fOutput.format;
	return err;
}


status_t
ToneNode::FormatChangeRequested(const media_source& source, const media_destination& destination,
	media_format* ioFormat, int32* deprecated)
{
	return B_ERROR;
}


status_t
ToneNode::GetNextOutput(int32* cookie, media_output* output)
{
	if (output == NULL)
		return B_BAD_VALUE;
	if (*cookie != 0)
		return B_BAD_INDEX;
	*output = fOutput;
	(*cookie)++;
	return B_OK;
}


status_t
ToneNode::DisposeOutputCookie(int32 cookie)
{
	return B_OK;
}


status_t
ToneNode::SetBufferGroup(const media_source& forSource, BBufferGroup* group)
{
	return B_ERROR;
}


status_t
ToneNode::GetLatency(bigtime_t* latency)
{
	*latency = EventLatency() + SchedulingLatency();
	return B_OK;
}


status_t
ToneNode::PrepareToConnect(const media_source& what, const media_destination& where,
	media_format* format, media_source* outSource, char* outName)
{
	if (fConnected)
		return EALREADY;
	if (what != fOutput.source)
		return B_MEDIA_BAD_SOURCE;
	if (fOutput.destination != media_destination::null)
		return B_MEDIA_ALREADY_CONNECTED;

	if (!format_is_compatible(*format, fOutput.format)) {
		*format = fOutput.format;
		return B_MEDIA_BAD_FORMAT;
	}

	// we always deliver 48 kHz stereo float; fill in what the consumer left open
	if (format->u.raw_audio.format == media_raw_audio_format::wildcard.format)
		format->u.raw_audio.format = fOutput.format.u.raw_audio.format;
	if (format->u.raw_audio.frame_rate == media_raw_audio_format::wildcard.frame_rate)
		format->u.raw_audio.frame_rate = fOutput.format.u.raw_audio.frame_rate;
	if (format->u.raw_audio.channel_count == media_raw_audio_format::wildcard.channel_count)
		format->u.raw_audio.channel_count = fOutput.format.u.raw_audio.channel_count;
	if (format->u.raw_audio.byte_order == media_raw_audio_format::wildcard.byte_order)
		format->u.raw_audio.byte_order = fOutput.format.u.raw_audio.byte_order;
	format->u.raw_audio.buffer_size = fOutput.format.u.raw_audio.buffer_size;

	if (format->u.raw_audio.format != media_raw_audio_format::B_AUDIO_FLOAT
		|| format->u.raw_audio.channel_count != (uint32)kChannels
		|| format->u.raw_audio.byte_order != B_MEDIA_HOST_ENDIAN) {
		*format = fOutput.format;
		return B_MEDIA_BAD_FORMAT;
	}

	*outSource = fOutput.source;
	strlcpy(outName, fOutput.name, B_MEDIA_NAME_LENGTH);
	fOutput.destination = where;
	return B_OK;
}


void
ToneNode::Connect(status_t error, const media_source& source, const media_destination& destination,
	const media_format& format, char* ioName)
{
	if (fConnected)
		return;

	if (source != fOutput.source || error < B_OK) {
		fOutput.destination = media_destination::null;
		return;
	}

	fOutput.destination = destination;
	fOutput.format = format;
	fFormat = format.u.raw_audio;
	strlcpy(ioName, fOutput.name, B_MEDIA_NAME_LENGTH);

	bigtime_t latency = 0;
	media_node_id timeSource = 0;
	FindLatencyFor(fOutput.destination, &latency, &timeSource);
	SetEventLatency(latency + (bigtime_t)(1000000.0 * kFrames / fFormat.frame_rate));

	fBufferGroup = new BBufferGroup(kFrames * kChannels * sizeof(float), 8);
	if (fBufferGroup->InitCheck() < B_OK) {
		delete fBufferGroup;
		fBufferGroup = NULL;
		return;
	}

	fConnected = true;
	fEnabled = true;
}


void
ToneNode::Disconnect(const media_source& what, const media_destination& where)
{
	if (!fConnected || what != fOutput.source || where != fOutput.destination)
		return;

	fEnabled = false;
	fConnected = false;
	_HandleStop();							// the thread uses the buffer group
	fOutput.destination = media_destination::null;
	delete fBufferGroup;
	fBufferGroup = NULL;
}


void
ToneNode::LateNoticeReceived(const media_source& what, bigtime_t howMuch, bigtime_t performanceTime)
{
}


void
ToneNode::EnableOutput(const media_source& what, bool enabled, int32* deprecated)
{
	if (what == fOutput.source)
		fEnabled = enabled;
}


void
ToneNode::AdditionalBufferRequested(const media_source& source, media_buffer_id prevBuffer,
	bigtime_t prevTime, const media_seek_tag* prevTag)
{
}


void
ToneNode::_HandleStart(bigtime_t performanceTime)
{
	if (fRunning)
		return;
	// время старта, присланное записывающим узлом, может быть по другим часам; берём собственные часы узла
	fPerformanceBase = TimeSource()->Now();
	fFramesSent = 0;
	fRunning = true;
	fThread = spawn_thread(_GenerateEntry, "tone generator", B_REAL_TIME_PRIORITY, this);
	if (fThread < B_OK) {
		fRunning = false;
		return;
	}
	resume_thread(fThread);
}


void
ToneNode::_HandleStop()
{
	if (!fRunning)
		return;
	fRunning = false;
	if (fThread >= 0) {
		status_t result;
		wait_for_thread(fThread, &result);
		fThread = -1;
	}
}


int32
ToneNode::_GenerateEntry(void* self)
{
	return static_cast<ToneNode*>(self)->_Generate();
}


int32
ToneNode::_Generate()
{
	while (fRunning) {
		const float rate = fFormat.frame_rate > 0 ? fFormat.frame_rate : kRate;
		bigtime_t perf = fPerformanceBase + (bigtime_t)(1000000.0 * fFramesSent / rate);
		const bigtime_t now = TimeSource()->Now();
		if (perf < now - 100000) {			// отстали больше чем на 100 мс (часы, пауза): не догоняем потоком, а начинаем заново
			fPerformanceBase = now;
			fFramesSent = 0;
			perf = now;
		}
		const bigtime_t wakeUp = TimeSource()->RealTimeFor(perf, 0) - EventLatency();
		snooze_until(wakeUp, B_SYSTEM_TIMEBASE);
		if (!fRunning)
			break;

		if (!fConnected || !fEnabled || fBufferGroup == NULL) {
			fFramesSent += kFrames;			// stay on the clock while nobody listens
			continue;
		}

		BBuffer* buffer = fBufferGroup->RequestBuffer(kFrames * kChannels * sizeof(float), 0LL);
		if (buffer == NULL) {
			fFramesSent += kFrames;
			continue;
		}

		float* out = static_cast<float*>(buffer->Data());
		const double step = 2.0 * M_PI * fFrequency / rate;
		const float level = fLevel;
		for (int i = 0; i < kFrames; i++) {
			const float v = (float)sin(fPhase) * level;
			fPhase += step;
			for (int c = 0; c < kChannels; c++)
				out[i * kChannels + c] = v;
		}
		if (fPhase > 2.0 * M_PI * 1000.0)
			fPhase = fmod(fPhase, 2.0 * M_PI);

		media_header* header = buffer->Header();
		header->type = B_MEDIA_RAW_AUDIO;
		header->time_source = TimeSource()->ID();
		header->size_used = kFrames * kChannels * sizeof(float);
		header->start_time = perf;
		header->file_pos = 0;
		header->orig_size = 0;
		header->data_offset = 0;

		if (SendBuffer(buffer, fOutput.source, fOutput.destination) < B_OK)
			buffer->Recycle();
		fFramesSent += kFrames;
	}
	return B_OK;
}
