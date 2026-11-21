#include "TapNode.h"

#include <Buffer.h>
#include <MediaRoster.h>
#include <TimeSource.h>

#include <stdio.h>
#include <string.h>


static const float kDefaultRate = 44100.0f;
static const uint32 kDefaultChannels = 2;


TapNode::TapNode(const char* name)
	:
	BMediaNode(name),
	BMediaEventLooper(),
	BBufferConsumer(B_MEDIA_RAW_AUDIO),
	BBufferProducer(B_MEDIA_RAW_AUDIO),
	fInputConnected(false),
	fHasFormat(false),
	fDownstreamLatency(0),
	fBuffers(0),
	fLost(0)
{
	fFormat = media_raw_audio_format::wildcard;
	for (int i = 0; i < kOutputs; i++) {
		fOut[i].group = NULL;
		fOut[i].connected = false;
		fOut[i].enabled = true;
		fOut[i].output.destination = media_destination::null;
	}
	fInput.destination = media_destination::null;
	fInput.source = media_source::null;
}


TapNode::~TapNode()
{
	Quit();
	for (int i = 0; i < kOutputs; i++) {
		delete fOut[i].group;
		fOut[i].group = NULL;
	}
}


BMediaAddOn*
TapNode::AddOn(int32* id) const
{
	return NULL;
}


status_t
TapNode::HandleMessage(int32 message, const void* data, size_t size)
{
	return B_ERROR;
}


void
TapNode::NodeRegistered()
{
	fInput.node = Node();
	fInput.destination.port = ControlPort();
	fInput.destination.id = 0;
	fInput.source = media_source::null;
	strlcpy(fInput.name, "Input", sizeof(fInput.name));
	fInput.format.type = B_MEDIA_RAW_AUDIO;
	fInput.format.u.raw_audio = media_raw_audio_format::wildcard;

	static const char* const kNames[kOutputs] = { "Pass", "Copy" };
	for (int i = 0; i < kOutputs; i++) {
		media_output& o = fOut[i].output;
		o.node = Node();
		o.source.port = ControlPort();
		o.source.id = i;
		o.destination = media_destination::null;
		strlcpy(o.name, kNames[i], sizeof(o.name));
		o.format.type = B_MEDIA_RAW_AUDIO;
		o.format.u.raw_audio = media_raw_audio_format::wildcard;
	}

	SetPriority(B_REAL_TIME_PRIORITY);
	Run();
}


// ---------------------------------------------------------------- the format

// Fills in what the other side left open: all the connections of the node have the same format.
// false: the format is not raw audio or does not match the one in use.
bool
TapNode::_Specialize(media_format* format) const
{
	if (format->type != B_MEDIA_RAW_AUDIO && format->type != B_MEDIA_UNKNOWN_TYPE)
		return false;
	format->type = B_MEDIA_RAW_AUDIO;
	media_raw_audio_format& f = format->u.raw_audio;
	const media_raw_audio_format wild = media_raw_audio_format::wildcard;

	if (fHasFormat) {
		if ((f.format != wild.format && f.format != fFormat.format)
			|| (f.frame_rate != wild.frame_rate && f.frame_rate != fFormat.frame_rate)
			|| (f.channel_count != wild.channel_count && f.channel_count != fFormat.channel_count)
			|| (f.byte_order != wild.byte_order && f.byte_order != fFormat.byte_order))
			return false;
		f.format = fFormat.format;
		f.frame_rate = fFormat.frame_rate;
		f.channel_count = fFormat.channel_count;
		f.byte_order = fFormat.byte_order;
	} else {
		if (f.format == wild.format)
			f.format = media_raw_audio_format::B_AUDIO_FLOAT;
		if (f.frame_rate == wild.frame_rate)
			f.frame_rate = kDefaultRate;
		if (f.channel_count == wild.channel_count)
			f.channel_count = kDefaultChannels;
		if (f.byte_order == wild.byte_order)
			f.byte_order = B_MEDIA_HOST_ENDIAN;
	}
	return true;
}


int
TapNode::_OutputIndex(const media_source& source) const
{
	for (int i = 0; i < kOutputs; i++) {
		if (source == fOut[i].output.source)
			return i;
	}
	return -1;
}


// ---------------------------------------------------------------- the input

status_t
TapNode::AcceptFormat(const media_destination& destination, media_format* format)
{
	if (destination != fInput.destination)
		return B_MEDIA_BAD_DESTINATION;
	if (!_Specialize(format))
		return B_MEDIA_BAD_FORMAT;
	return B_OK;
}


status_t
TapNode::GetNextInput(int32* cookie, media_input* input)
{
	if (*cookie != 0)
		return B_BAD_INDEX;
	*input = fInput;
	(*cookie)++;
	return B_OK;
}


void
TapNode::DisposeInputCookie(int32 cookie)
{
}


void
TapNode::BufferReceived(BBuffer* buffer)
{
	if (buffer->Header()->destination != fInput.destination.id) {
		buffer->Recycle();
		return;
	}
	// the buffer is handled at its time; the event queue recycles it afterwards
	media_timed_event event(buffer->Header()->start_time, BTimedEventQueue::B_HANDLE_BUFFER, buffer,
		BTimedEventQueue::B_RECYCLE_BUFFER);
	EventQueue()->AddEvent(event);
}


void
TapNode::ProducerDataStatus(const media_destination& forWhom, int32 status, bigtime_t atPerformanceTime)
{
	// the same to the nodes behind: "no data" / "data"
	for (int i = 0; i < kOutputs; i++) {
		if (fOut[i].connected)
			SendDataStatus(status, fOut[i].output.destination, atPerformanceTime);
	}
}


status_t
TapNode::GetLatencyFor(const media_destination& forWhom, bigtime_t* latency, media_node_id* timeSource)
{
	if (forWhom != fInput.destination)
		return B_MEDIA_BAD_DESTINATION;
	*latency = EventLatency() + SchedulingLatency();
	*timeSource = TimeSource()->ID();
	return B_OK;
}


status_t
TapNode::Connected(const media_source& producer, const media_destination& where,
	const media_format& withFormat, media_input* input)
{
	if (where != fInput.destination)
		return B_MEDIA_BAD_DESTINATION;
	if (fInputConnected)
		return B_MEDIA_ALREADY_CONNECTED;

	fInput.source = producer;
	fInput.format = withFormat;
	if (!fHasFormat) {
		fFormat = withFormat.u.raw_audio;
		fHasFormat = true;
	}
	fInputConnected = true;
	*input = fInput;
	_UpdateLatency();
	return B_OK;
}


void
TapNode::Disconnected(const media_source& producer, const media_destination& where)
{
	if (where != fInput.destination || producer != fInput.source)
		return;
	fInputConnected = false;
	fInput.source = media_source::null;
	if (!fOut[0].connected && !fOut[1].connected)
		fHasFormat = false;
}


status_t
TapNode::FormatChanged(const media_source& producer, const media_destination& consumer,
	int32 changeTag, const media_format& format)
{
	return B_ERROR;
}


// ---------------------------------------------------------------- the outputs

status_t
TapNode::FormatSuggestionRequested(media_type type, int32 quality, media_format* format)
{
	if (type != B_MEDIA_RAW_AUDIO && type != B_MEDIA_UNKNOWN_TYPE)
		return B_MEDIA_BAD_FORMAT;
	format->type = B_MEDIA_RAW_AUDIO;
	format->u.raw_audio = media_raw_audio_format::wildcard;
	return _Specialize(format) ? B_OK : B_MEDIA_BAD_FORMAT;
}


status_t
TapNode::FormatProposal(const media_source& output, media_format* format)
{
	if (format == NULL)
		return B_BAD_VALUE;
	if (_OutputIndex(output) < 0)
		return B_MEDIA_BAD_SOURCE;
	return _Specialize(format) ? B_OK : B_MEDIA_BAD_FORMAT;
}


status_t
TapNode::FormatChangeRequested(const media_source& source, const media_destination& destination,
	media_format* ioFormat, int32* deprecated)
{
	return B_ERROR;
}


status_t
TapNode::GetNextOutput(int32* cookie, media_output* output)
{
	if (output == NULL)
		return B_BAD_VALUE;
	if (*cookie < 0 || *cookie >= kOutputs)
		return B_BAD_INDEX;
	*output = fOut[*cookie].output;
	(*cookie)++;
	return B_OK;
}


status_t
TapNode::DisposeOutputCookie(int32 cookie)
{
	return B_OK;
}


status_t
TapNode::SetBufferGroup(const media_source& forSource, BBufferGroup* group)
{
	return B_ERROR;				// the node uses its own buffers
}


status_t
TapNode::GetLatency(bigtime_t* latency)
{
	*latency = EventLatency() + SchedulingLatency();
	return B_OK;
}


status_t
TapNode::PrepareToConnect(const media_source& what, const media_destination& where,
	media_format* format, media_source* outSource, char* outName)
{
	const int index = _OutputIndex(what);
	if (index < 0)
		return B_MEDIA_BAD_SOURCE;
	if (fOut[index].output.destination != media_destination::null)
		return B_MEDIA_ALREADY_CONNECTED;
	if (!_Specialize(format))
		return B_MEDIA_BAD_FORMAT;
	format->u.raw_audio.buffer_size = fHasFormat && fFormat.buffer_size != 0
		? fFormat.buffer_size : (uint32)kBufferBytes;

	*outSource = fOut[index].output.source;
	strlcpy(outName, fOut[index].output.name, B_MEDIA_NAME_LENGTH);
	fOut[index].output.destination = where;
	return B_OK;
}


void
TapNode::Connect(status_t error, const media_source& source, const media_destination& destination,
	const media_format& format, char* ioName)
{
	const int index = _OutputIndex(source);
	if (index < 0)
		return;
	Output& o = fOut[index];
	if (error < B_OK) {
		o.output.destination = media_destination::null;
		return;
	}

	o.output.destination = destination;
	o.output.format = format;
	strlcpy(ioName, o.output.name, B_MEDIA_NAME_LENGTH);
	if (!fHasFormat) {
		fFormat = format.u.raw_audio;
		fHasFormat = true;
	}

	delete o.group;
	o.group = new BBufferGroup(kBufferBytes, kBufferCount);
	if (o.group->InitCheck() < B_OK) {
		delete o.group;
		o.group = NULL;
		return;
	}
	o.connected = true;
	o.enabled = true;
	_UpdateLatency();
}


void
TapNode::Disconnect(const media_source& what, const media_destination& where)
{
	const int index = _OutputIndex(what);
	if (index < 0 || where != fOut[index].output.destination)
		return;
	Output& o = fOut[index];
	o.connected = false;
	o.output.destination = media_destination::null;
	BBufferGroup* group = o.group;
	o.group = NULL;
	delete group;
	if (!fInputConnected && !fOut[0].connected && !fOut[1].connected)
		fHasFormat = false;
	_UpdateLatency();
}


void
TapNode::LateNoticeReceived(const media_source& what, bigtime_t howMuch, bigtime_t performanceTime)
{
}


void
TapNode::EnableOutput(const media_source& what, bool enabled, int32* deprecated)
{
	const int index = _OutputIndex(what);
	if (index >= 0)
		fOut[index].enabled = enabled;
}


void
TapNode::AdditionalBufferRequested(const media_source& source, media_buffer_id prevBuffer,
	bigtime_t prevTime, const media_seek_tag* prevTag)
{
}


// The buffers must leave the node early enough for the slowest node behind.
void
TapNode::_UpdateLatency()
{
	bigtime_t worst = 0;
	for (int i = 0; i < kOutputs; i++) {
		if (!fOut[i].connected)
			continue;
		bigtime_t latency = 0;
		media_node_id timeSource = 0;
		if (FindLatencyFor(fOut[i].output.destination, &latency, &timeSource) == B_OK && latency > worst)
			worst = latency;
	}
	fDownstreamLatency = worst;
	SetEventLatency(worst + 1000);
	if (fInputConnected)
		SendLatencyChange(fInput.source, fInput.destination, EventLatency() + SchedulingLatency());
}


// ---------------------------------------------------------------- the sound

void
TapNode::HandleEvent(const media_timed_event* event, bigtime_t lateness, bool realTimeEvent)
{
	switch (event->type) {
		case BTimedEventQueue::B_HANDLE_BUFFER:
			_Pass(static_cast<BBuffer*>(event->pointer));
			break;
		default:
			break;
	}
}


// the same bytes go to every connected output (in pieces that fit into our buffers)
void
TapNode::_Pass(BBuffer* buffer)
{
	if (buffer == NULL || !fHasFormat)
		return;
	const media_header* in = buffer->Header();
	const uint8* data = static_cast<const uint8*>(buffer->Data());
	const size_t total = in->size_used;
	const size_t frameSize = (fFormat.format & media_raw_audio_format::B_AUDIO_SIZE_MASK)
		* fFormat.channel_count;
	if (frameSize == 0 || fFormat.frame_rate <= 0)
		return;

	fBuffers++;
	for (int i = 0; i < kOutputs; i++) {
		Output& o = fOut[i];
		if (!o.connected || !o.enabled || o.group == NULL)
			continue;

		size_t done = 0;
		while (done < total) {
			size_t piece = total - done;
			if (piece > (size_t)kBufferBytes)
				piece = (size_t)kBufferBytes - ((size_t)kBufferBytes % frameSize);
			BBuffer* out = o.group->RequestBuffer(kBufferBytes, 0LL);
			if (out == NULL) {
				fLost++;				// the node behind is too slow
				break;
			}
			memcpy(out->Data(), data + done, piece);
			media_header* header = out->Header();
			header->type = B_MEDIA_RAW_AUDIO;
			header->time_source = TimeSource()->ID();
			header->size_used = piece;
			header->start_time = in->start_time
				+ (bigtime_t)(1000000.0 * (done / frameSize) / fFormat.frame_rate);
			header->file_pos = 0;
			header->orig_size = 0;
			header->data_offset = 0;
			if (SendBuffer(out, o.output.source, o.output.destination) < B_OK) {
				out->Recycle();
				fLost++;
				break;
			}
			done += piece;
		}
	}
}
