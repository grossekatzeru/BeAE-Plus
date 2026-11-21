// TapNode - a Media Kit node that passes a sound through and makes a copy of it.
// One input and two outputs: the sound that comes in goes out of both outputs.
//
//   application ----> [ BeAE Tap ] ----> Audio Mixer       (output 1, "Pass")
//                                  \---> BeAE-Plus          (output 2, "Copy")
//
// The format is the one of the first connection (raw audio, any sample format); the bytes are
// copied as they are.
#ifndef TAP_NODE_H
#define TAP_NODE_H

#include <BufferConsumer.h>
#include <BufferGroup.h>
#include <BufferProducer.h>
#include <MediaEventLooper.h>
#include <MediaNode.h>
#include <OS.h>

class TapNode : public virtual BMediaEventLooper, public virtual BBufferConsumer,
	public virtual BBufferProducer {
public:
						TapNode(const char* name);
	virtual				~TapNode();

	// for the window
	bool				InputConnected() const { return fInputConnected; }
	bool				OutputConnected(int index) const { return fOut[index].connected; }
	int64				BuffersPassed() const { return fBuffers; }
	int64				BuffersLost() const { return fLost; }
	media_raw_audio_format	Format() const { return fFormat; }
	bool				HasFormat() const { return fHasFormat; }

	// BMediaNode
	virtual BMediaAddOn* AddOn(int32* id) const;
	virtual status_t	HandleMessage(int32 message, const void* data, size_t size);

protected:
	// BMediaEventLooper
	virtual void		NodeRegistered();
	virtual void		HandleEvent(const media_timed_event* event, bigtime_t lateness,
							bool realTimeEvent = false);

	// BBufferConsumer
	virtual status_t	AcceptFormat(const media_destination& destination, media_format* format);
	virtual status_t	GetNextInput(int32* cookie, media_input* input);
	virtual void		DisposeInputCookie(int32 cookie);
	virtual void		BufferReceived(BBuffer* buffer);
	virtual void		ProducerDataStatus(const media_destination& forWhom, int32 status,
							bigtime_t atPerformanceTime);
	virtual status_t	GetLatencyFor(const media_destination& forWhom, bigtime_t* latency,
							media_node_id* timeSource);
	virtual status_t	Connected(const media_source& producer, const media_destination& where,
							const media_format& withFormat, media_input* input);
	virtual void		Disconnected(const media_source& producer, const media_destination& where);
	virtual status_t	FormatChanged(const media_source& producer, const media_destination& consumer,
							int32 changeTag, const media_format& format);

	// BBufferProducer
	virtual status_t	FormatSuggestionRequested(media_type type, int32 quality, media_format* format);
	virtual status_t	FormatProposal(const media_source& output, media_format* format);
	virtual status_t	FormatChangeRequested(const media_source& source, const media_destination& destination,
							media_format* ioFormat, int32* deprecated);
	virtual status_t	GetNextOutput(int32* cookie, media_output* output);
	virtual status_t	DisposeOutputCookie(int32 cookie);
	virtual status_t	SetBufferGroup(const media_source& forSource, BBufferGroup* group);
	virtual status_t	GetLatency(bigtime_t* latency);
	virtual status_t	PrepareToConnect(const media_source& what, const media_destination& where,
							media_format* format, media_source* outSource, char* outName);
	virtual void		Connect(status_t error, const media_source& source, const media_destination& destination,
							const media_format& format, char* ioName);
	virtual void		Disconnect(const media_source& what, const media_destination& where);
	virtual void		LateNoticeReceived(const media_source& what, bigtime_t howMuch, bigtime_t performanceTime);
	virtual void		EnableOutput(const media_source& what, bool enabled, int32* deprecated);
	virtual void		AdditionalBufferRequested(const media_source& source, media_buffer_id prevBuffer,
							bigtime_t prevTime, const media_seek_tag* prevTag);

private:
	enum { kOutputs = 2 };
	enum { kBufferBytes = 32768, kBufferCount = 8 };

	struct Output {
		media_output	output;
		BBufferGroup*	group;
		bool			connected;
		bool			enabled;
	};

	// the format that all the connections share
	bool				_Specialize(media_format* format) const;
	int					_OutputIndex(const media_source& source) const;
	void				_UpdateLatency();
	void				_Pass(BBuffer* buffer);

	media_input			fInput;
	volatile bool		fInputConnected;
	Output				fOut[kOutputs];

	media_raw_audio_format	fFormat;			// valid when fHasFormat
	volatile bool		fHasFormat;
	bigtime_t			fDownstreamLatency;

	volatile int64		fBuffers;
	volatile int64		fLost;
};

#endif
