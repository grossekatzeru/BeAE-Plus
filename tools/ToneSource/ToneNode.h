// ToneNode - Media Kit node that produces a sine tone (raw float audio, stereo).
// Used as a test source: it appears in Cortex and in the source list of BeAE-Plus.
#ifndef TONE_NODE_H
#define TONE_NODE_H

#include <BufferGroup.h>
#include <BufferProducer.h>
#include <MediaEventLooper.h>
#include <MediaNode.h>
#include <OS.h>

class ToneNode : public virtual BMediaEventLooper, public virtual BBufferProducer {
public:
						ToneNode(const char* name, float frequency);
	virtual				~ToneNode();

	void				SetFrequency(float hz) { fFrequency = hz; }
	float				Frequency() const { return fFrequency; }
	void				SetLevel(float level) { fLevel = level; }

	// BMediaNode
	virtual BMediaAddOn* AddOn(int32* id) const;
	virtual status_t	HandleMessage(int32 message, const void* data, size_t size);

	// BMediaEventLooper
protected:
	virtual void		NodeRegistered();
	virtual void		HandleEvent(const media_timed_event* event, bigtime_t lateness, bool realTimeEvent = false);

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
	void				_HandleStart(bigtime_t performanceTime);
	void				_HandleStop();
	int32				_Generate();
	static int32		_GenerateEntry(void* self);

	enum { kFrames = 1024 };			// frames in one buffer

	BBufferGroup*		fBufferGroup;
	media_output		fOutput;
	media_raw_audio_format	fFormat;
	volatile bool		fConnected;
	volatile bool		fEnabled;
	volatile bool		fRunning;

	thread_id			fThread;
	bigtime_t			fPerformanceBase;
	int64				fFramesSent;
	double				fPhase;

	volatile float		fFrequency;
	volatile float		fLevel;
};

#endif
