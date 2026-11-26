/*
 *	Driver for USB Audio Device Class devices.
 *	Copyright (c) 2009-13 S.Zharski <imker@gmx.li>
 *	Distributed under the terms of the MIT license.
 */


#include "Stream.h"

#include <kernel.h>
#include <usb/USB_audio.h>

#include "Device.h"
#include "Driver.h"
#include "Settings.h"


Stream::Stream(Device* device, size_t interface, usb_interface_list* List)
	:
	AudioStreamingInterface(&device->AudioControl(), interface, List),
	fDevice(device),
	fStatus(B_NO_INIT),
	fStreamEndpoint(0),
	fIsRunning(false),
	fArea(-1),
	fKernelArea(-1),
	fAreaSize(0),
	fDescriptors(NULL),
	fDescriptorsCount(0),
	fRawArea(-1),
	fRawBuffers(NULL),
	fRawBufferSize(0),
	fPacketMax(0),
	fPacketsPerBuffer(0),
	fFrameSize(0),
	fSubBufferBytes(0),
	fFillBytes(0),
	fWriteBuffer(0),
	fRawIndex(0),
	fCurrentBuffer(0),
	fSamplesCount(0),
	fChannelLimit(0),
	fRealTime(0),
	fStartingFrame(0),
	fProcessedBuffers(0),
	fInsideNotify(0)
{
}


// The frame of a stream may carry more channels than the multi_audio API
// can handle: it keeps the channels in a 32 bit mask.
static const uint32 kMaxStreamChannels = 32;


Stream::~Stream()
{
	_FreeBuffers();
}


status_t
Stream::_ChooseAlternate()
{
	// lookup alternate with maximal (ch * 100 + resolution)
	uint16 maxChxRes = 0;
	for (int i = 0; i < fAlternates.Count(); i++) {
		if (fAlternates[i]->Interface() == 0) {
			TRACE(INF, "Ignore alternate %d - zero interface description.\n", i);
			continue;
		}

		if (fAlternates[i]->Format() == 0) {
			TRACE(INF, "Ignore alternate %d - zero format description.\n", i);
			continue;
		}

		if (fAlternates[i]->Format()->fFormatType
				!= USB_AUDIO_FORMAT_TYPE_I) {
			TRACE(ERR, "Ignore alternate %d - format type %#02x "
				"is not supported.\n", i, fAlternates[i]->Format()->fFormatType);
			continue;
		}

		switch (fAlternates[i]->Interface()->fFormatTag) {
			case USB_AUDIO_FORMAT_PCM:
			case USB_AUDIO_FORMAT_PCM8:
			case USB_AUDIO_FORMAT_IEEE_FLOAT:
		//	case USB_AUDIO_FORMAT_ALAW:
		//	case USB_AUDIO_FORMAT_MULAW:
				break;
			default:
				TRACE(ERR, "Ignore alternate %d - format %#04x is not "
					"supported.\n", i, fAlternates[i]->Interface()->fFormatTag);
			continue;
		}

		TypeIFormatDescriptor* format
			= static_cast<TypeIFormatDescriptor*>(fAlternates[i]->Format());

		if (format->fNumChannels > kMaxStreamChannels) {
			TRACE(ERR, "Ignore alternate %d - channel count %d "
				"is not supported.\n", i, format->fNumChannels);
			continue;
		}

		if (fAlternates[i]->Interface()->fFormatTag == USB_AUDIO_FORMAT_PCM) {
			switch(format->fBitResolution) {
				default:
				TRACE(ERR, "Ignore alternate %d - bit resolution %d "
					"is not supported.\n", i, format->fBitResolution);
					continue;
				case 8: case 16: case 18: case 20: case 24: case 32:
					break;
			}

			switch(format->fSubframeSize) {
				case 1: case 2: case 4:
					// The USB-Audio 1.0 specification also allows 3 bytes per subframe, but the
					// multi-audio resampler will write 4 bytes in that case, overwriting things
					// in the neighboring bytes.
					// It can be allowed after https://dev.haiku-os.org/ticket/20267 is resolved.
					break;
				default:
					TRACE(ERR, "Ignore alternate %d - subframe size %d is not supported.\n", i,
						format->fSubframeSize);
					continue;
			}
		}

		uint16 chxRes = format->fNumChannels * 100 + format->fBitResolution;
		if (chxRes > maxChxRes) {
			maxChxRes = chxRes;
			fActiveAlternate = i;
		}
	}

	if (maxChxRes <= 0) {
		TRACE(ERR, "No compatible alternate found. "
			"Stream initialization failed.\n");
		return B_NO_INIT;
	}

	const ASEndpointDescriptor* endpoint
		= fAlternates[fActiveAlternate]->Endpoint();
	fIsInput = (endpoint->fEndpointAddress & USB_ENDPOINT_ADDR_DIR_IN)
		== USB_ENDPOINT_ADDR_DIR_IN;

	if (fIsInput)
		fCurrentBuffer = (size_t)-1;

	TRACE(INF, "Alternate %d EP:%x selected for %s!\n",
		fActiveAlternate, endpoint->fEndpointAddress,
		fIsInput ? "recording" : "playback");

	return B_OK;
}


status_t
Stream::Init()
{
	fStatus = _ChooseAlternate();
	if (fStatus != B_OK)
		return fStatus;

	// The sampling rate was never initialized, the driver reported rate 0
	// and had no buffers until the application changed the format.
	// Prefer the usual rates, otherwise use the highest one.
	AudioStreamAlternate* alternate = fAlternates[fActiveAlternate];
	if (alternate->SetSamplingRate(48000) != B_OK
		&& alternate->SetSamplingRate(44100) != B_OK) {
		alternate->SetSamplingRate(0);
	}

	fStatus = _SetupBuffers();
	return fStatus;
}


void
Stream::OnRemove()
{
	// the transfer callback schedule traffic - so we must ensure that we are
	// not inside the callback anymore before returning, as we would otherwise
	// violate the promise not to use any of the pipes after returning from the
	// removed callback
	while (atomic_get(&fInsideNotify) != 0)
		snooze(100);

	gUSBModule->cancel_queued_transfers(fStreamEndpoint);
}


void
Stream::_FreeBuffers()
{
	if (fArea >= 0)
		delete_area(fArea);
	if (fKernelArea >= 0)
		delete_area(fKernelArea);
	if (fRawArea >= 0)
		delete_area(fRawArea);
	delete[] fDescriptors;

	fArea = fKernelArea = fRawArea = -1;
	fDescriptors = NULL;
	fBuffers = fKernelBuffers = fRawBuffers = NULL;
	fAreaSize = 0;
	fDescriptorsCount = 0;
	fSamplesCount = 0;
}


status_t
Stream::_SetupBuffers()
{
	TypeIFormatDescriptor* format = static_cast<TypeIFormatDescriptor*>(
		fAlternates[fActiveAlternate]->Format());

	uint32 samplingRate = fAlternates[fActiveAlternate]->GetSamplingRate();
	fFrameSize = format->fNumChannels * format->fSubframeSize;

	if (samplingRate == 0 || fFrameSize == 0) {
		TRACE(ERR, "bad sampling rate %d or frame size %d!\n", samplingRate,
			fFrameSize);
		return B_BAD_VALUE;
	}

	if (fArea != -1 || fKernelArea != -1 || fRawArea != -1) {
		Stop();
		_FreeBuffers();
	}

	// The USB frame is 1 ms. At 44.1 kHz there are 44.1 sample frames per
	// packet, so the packets carry 44 or 45 of them. A buffer must contain
	// a whole number of sample frames, so it is built from packets for a
	// whole number of 1/rate periods: rate/gcd(rate, 1000) frames are sent
	// in 1000/gcd(rate, 1000) packets.
	uint32 a = samplingRate, b = 1000;
	while (b != 0) {
		uint32 t = a % b;
		a = b;
		b = t;
	}
	size_t packetsStep = 1000 / a;

	// about kSamplesBufferSize sample frames in a buffer
	size_t packets = (kSamplesBufferSize * 1000ULL / samplingRate
		+ packetsStep - 1) / packetsStep * packetsStep;
	if (packets < packetsStep)
		packets = packetsStep;

	fPacketsPerBuffer = packets;
	size_t framesPerBuffer = (uint64)samplingRate * packets / 1000;
	fSubBufferBytes = framesPerBuffer * fFrameSize;
	fSamplesCount = framesPerBuffer * kSamplesBufferCount;
	TRACE(INF, "packets per buffer:%ld frames per buffer:%ld\n", packets,
		framesPerBuffer);

	fAreaSize = fSubBufferBytes * kSamplesBufferCount;
	TRACE(INF, "estimate fAreaSize:%d\n", fAreaSize);

	// round up to B_PAGE_SIZE and create area
	fAreaSize = (fAreaSize + (B_PAGE_SIZE - 1)) &~ (B_PAGE_SIZE - 1);
	TRACE(INF, "rounded up fAreaSize:%d\n", fAreaSize);

	// the pages are locked: the buffers are written from the USB callback
	fArea = create_area(fIsInput ? DRIVER_NAME "_record_area"
		: DRIVER_NAME "_playback_area", (void**)&fBuffers,
		B_ANY_ADDRESS, fAreaSize, B_FULL_LOCK,
		B_READ_AREA | B_WRITE_AREA);
	if (fArea < 0) {
		TRACE(ERR, "Error of creating %#x - "
			"bytes size buffer area:%#010x\n", fAreaSize, fArea);
		fStatus = fArea;
		fArea = -1;
		fAreaSize = 0;
		return fStatus;
	}

	// The kernel is not allowed to touch userspace areas, so we clone our
	// area into kernel space.
	fKernelArea = clone_area("usb_audio cloned area", (void**)&fKernelBuffers,
		B_ANY_ADDRESS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, fArea);
	if (fKernelArea < 0) {
		fStatus = fKernelArea;
		fKernelArea = -1;
		_FreeBuffers();
		return fStatus;
	}

	TRACE(INF, "Created area id:%d at addr:%p size:%#010lx\n",
		fArea, fBuffers, fAreaSize);

	fDescriptorsCount = packets * kSamplesBufferCount;
	fDescriptors = new(std::nothrow) usb_iso_packet_descriptor[
		fDescriptorsCount];
	if (fDescriptors == NULL) {
		_FreeBuffers();
		return B_NO_MEMORY;
	}

	if (fIsInput) {
		// ask for the biggest packet the endpoint can send, the real
		// length of every packet is known only after the transfer
		size_t nominal = (samplingRate + 999) / 1000 * fFrameSize;
		size_t endpointMax = 0;
		const ASEndpointDescriptor* endpoint
			= fAlternates[fActiveAlternate]->Endpoint();
		if (endpoint != NULL) {
			// bits 11-12: additional transactions of a high-speed endpoint
			endpointMax = (endpoint->fMaxPacketSize & 0x7ff)
				* (1 + ((endpoint->fMaxPacketSize >> 11) & 3));
		}
		fPacketMax = endpointMax / fFrameSize * fFrameSize;
		if (fPacketMax < nominal)
			fPacketMax = nominal + fFrameSize;

		fRawBufferSize = fPacketMax * packets;
		size_t rawSize = (fRawBufferSize * kSamplesBufferCount
			+ (B_PAGE_SIZE - 1)) &~ (B_PAGE_SIZE - 1);
		fRawArea = create_area(DRIVER_NAME "_raw_area", (void**)&fRawBuffers,
			B_ANY_KERNEL_ADDRESS, rawSize, B_FULL_LOCK,
			B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
		if (fRawArea < 0) {
			TRACE(ERR, "Error of creating raw area:%#010x\n", fRawArea);
			fStatus = fRawArea;
			fRawArea = -1;
			_FreeBuffers();
			return fStatus;
		}
		TRACE(INF, "packet max:%ld raw buffer:%ld\n", fPacketMax,
			fRawBufferSize);

		for (size_t i = 0; i < fDescriptorsCount; i++) {
			fDescriptors[i].request_length = fPacketMax;
			fDescriptors[i].actual_length = 0;
			fDescriptors[i].status = B_OK;
		}
	} else {
		// the frames per packet are distributed evenly (44, 44, 45, ...),
		// otherwise the device would be fed slower than it plays
		for (size_t buffer = 0; buffer < kSamplesBufferCount; buffer++) {
			for (size_t i = 0; i < packets; i++) {
				uint64 frames = ((uint64)samplingRate * (i + 1)) / 1000
					- ((uint64)samplingRate * i) / 1000;
				usb_iso_packet_descriptor& d
					= fDescriptors[buffer * packets + i];
				d.request_length = frames * fFrameSize;
				d.actual_length = 0;
				d.status = B_OK;
			}
		}
	}

	fFillBytes = 0;
	fWriteBuffer = 0;
	fRawIndex = kSamplesBufferCount - 1;
	fProcessedBuffers = 0;
	fCurrentBuffer = fIsInput ? (size_t)-1 : 0;

	return fStatus = B_OK;
}


status_t
Stream::OnSetConfiguration(usb_device device,
		const usb_configuration_info* config)
{
	if (config == NULL) {
		TRACE(ERR, "NULL configuration. Not set.\n");
		return B_ERROR;
	}

	usb_interface_info* interface
		= &config->interface[fInterface].alt[fActiveAlternate];
	if (interface == NULL) {
		TRACE(ERR, "NULL interface. Not set.\n");
		return B_ERROR;
	}

	status_t status = gUSBModule->set_alt_interface(device, interface);
	uint8 address = fAlternates[fActiveAlternate]->Endpoint()->fEndpointAddress;

	TRACE(INF, "set_alt_interface %x\n", status);

	for (size_t i = 0; i < interface->endpoint_count; i++) {
		if (address == interface->endpoint[i].descr->endpoint_address) {
			fStreamEndpoint = interface->endpoint[i].handle;
			TRACE(INF, "%s Stream Endpoint [address %#04x] handle is: %#010x.\n",
				fIsInput ? "Input" : "Output", address, fStreamEndpoint);

			// the device must run at the rate the buffers were made for
			_SendSamplingRate();
			return B_OK;
		}
	}

	TRACE(INF, "%s Stream Endpoint [address %#04x] was not found.\n",
		fIsInput ? "Input" : "Output", address);
	return B_ERROR;
}


status_t
Stream::_SendSamplingRate()
{
	AudioStreamAlternate* alternate = fAlternates[fActiveAlternate];
	TypeIFormatDescriptor* format
		= static_cast<TypeIFormatDescriptor*>(alternate->Format());
	const ASEndpointDescriptor* endpoint = alternate->Endpoint();
	if (format == NULL || endpoint == NULL)
		return B_NO_INIT;

	// bit 0 of the endpoint attributes: the sampling frequency control is
	// supported. A device with a single fixed rate needs no request at all
	// (many of them stall it).
	bool control = (endpoint->fCSAttributes
		& (1 << (USB_AUDIO_SAMPLING_FREQ_CONTROL - 1))) != 0;
	bool severalRates = format->fSampleFrequencyType != 1;
	if (!control && !severalRates)
		return B_OK;

	uint32 samplingRate = alternate->GetSamplingRate();
	size_t actualLength = 0;
	usb_audio_sampling_freq freq = _ASFormatDescriptor::GetSamFreq(samplingRate);
	uint8 address = endpoint->fEndpointAddress;

	status_t status = gUSBModule->send_request(fDevice->fDevice,
		USB_REQTYPE_CLASS | USB_REQTYPE_ENDPOINT_OUT,
		USB_AUDIO_SET_CUR, USB_AUDIO_SAMPLING_FREQ_CONTROL << 8,
		address, sizeof(freq), &freq, &actualLength);

	TRACE(status == B_OK ? INF : ERR,
		"set_speed %02x%02x%02x for ep %#x (control bit %d) %d: %s\n",
		freq.bytes[0], freq.bytes[1], freq.bytes[2],
		address, control, actualLength, strerror(status));
	return status;
}


status_t
Stream::Start()
{
	status_t result = B_BUSY;
	if (!fIsRunning) {
		if (fIsInput) {
			fFillBytes = 0;
			fWriteBuffer = 0;
			fRawIndex = kSamplesBufferCount - 1;
			fCurrentBuffer = (size_t)-1;
		}

		// the callback ignores its calls until this flag is set
		fIsRunning = true;
		for (size_t i = 0; i < kSamplesBufferCount; i++) {
			result = _QueueNextTransfer(i, i == 0);
			if (result != B_OK) {
				TRACE(ERR, "Error of queueing the transfer %ld: %s\n", i,
					strerror(result));
				break;
			}
		}
		if (result != B_OK) {
			fIsRunning = false;
			gUSBModule->cancel_queued_transfers(fStreamEndpoint);
		}
	}
	return result;
}


status_t
Stream::Stop()
{
	if (fIsRunning) {
		// stop new callbacks from queueing transfers, then wait until the
		// notification handling in progress finished
		fIsRunning = false;
		while (atomic_get(&fInsideNotify) != 0)
			snooze(100);
	}
	gUSBModule->cancel_queued_transfers(fStreamEndpoint);

	return B_OK;
}


status_t
Stream::_QueueNextTransfer(size_t queuedBuffer, bool start)
{
	uint8* data;
	size_t dataSize;
	if (fIsInput) {
		data = fRawBuffers + fRawBufferSize * queuedBuffer;
		dataSize = fRawBufferSize;
	} else {
		data = fKernelBuffers + fSubBufferBytes * queuedBuffer;
		dataSize = fSubBufferBytes;
	}

	usb_iso_packet_descriptor* descriptors
		= fDescriptors + queuedBuffer * fPacketsPerBuffer;
	if (fIsInput) {
		for (size_t i = 0; i < fPacketsPerBuffer; i++) {
			descriptors[i].request_length = fPacketMax;
			descriptors[i].actual_length = 0;
			descriptors[i].status = B_OK;
		}
	}

	TRACE(DTA, "buffers:%p[%#lx]\ndescrs:%p[%#lx]\n", data, dataSize,
		descriptors, fPacketsPerBuffer);

	status_t status = gUSBModule->queue_isochronous(fStreamEndpoint,
		data, dataSize, descriptors, fPacketsPerBuffer,
		&fStartingFrame, USB_ISO_ASAP,
		Stream::_TransferCallback, this);

	TRACE(DTA, "frame:%#010x\n", fStartingFrame);
	return status;
}


void
Stream::_HandleRecordedPackets(size_t rawIndex)
{
	const uint8* raw = fRawBuffers + rawIndex * fRawBufferSize;
	usb_iso_packet_descriptor* descriptors
		= fDescriptors + rawIndex * fPacketsPerBuffer;
	size_t bad = 0;
	status_t firstBad = B_OK;

	for (size_t i = 0; i < fPacketsPerBuffer; i++) {
		// the packets have different length (44 or 45 frames at 44.1 kHz) and
		// the device may send nothing: copy the real data only, otherwise the
		// recording gets gaps (clicks)
		// A short packet is normal here (the packets are requested with the
		// maximal size), and the status of a short packet may be not B_OK, so
		// the received bytes are used whenever there are any.
		size_t length = descriptors[i].actual_length;
		if (descriptors[i].status != B_OK) {
			if (bad++ == 0)
				firstBad = descriptors[i].status;
		}
		if (length > fPacketMax)
			length = fPacketMax;
		length -= length % fFrameSize;

		const uint8* data = raw + i * fPacketMax;
		while (length > 0) {
			size_t chunk = min_c(length, fSubBufferBytes - fFillBytes);
			memcpy(fKernelBuffers + fWriteBuffer * fSubBufferBytes
				+ fFillBytes, data, chunk);
			fFillBytes += chunk;
			data += chunk;
			length -= chunk;

			if (fFillBytes < fSubBufferBytes)
				continue;

			// the buffer is complete
			fFillBytes = 0;
			fCurrentBuffer = fWriteBuffer;
			fWriteBuffer = (fWriteBuffer + 1) % kSamplesBufferCount;

			if (atomic_add(&fProcessedBuffers, 1) > (int32)kSamplesBufferCount)
				TRACE(ERR, "Processed buffers overflow:%d\n", fProcessedBuffers);
			fRealTime = system_time();
			release_sem_etc(fDevice->fBuffersReadySem, 1, B_DO_NOT_RESCHEDULE);
		}
	}

	if (bad != 0) {
		TRACE(ERR, "%ld of %ld packets with a status (first: %#x: %s)\n", bad,
			fPacketsPerBuffer, firstBad, strerror(firstBad));
	}
}


void
Stream::_TransferCallback(void* cookie, status_t status, void* data,
	size_t actualLength)
{
	Stream* stream = (Stream*)cookie;

	TRACE(status == B_OK ? DTA : ERR,
		"stream:%010x: status:%#010x, data:%p, len:%d\n",
		stream->fStreamEndpoint, status, data, actualLength);

	atomic_add(&stream->fInsideNotify, 1);
	if (status == B_CANCELED || stream->fDevice->fRemoved || !stream->fIsRunning) {
		TRACE(ERR, "Cancelled: c:%p st:%#010x, data:%p, len:%d\n",
			cookie, status, data, actualLength);
		atomic_add(&stream->fInsideNotify, -1);
		return;
	}

#if 0
	stream->_DumpDescriptors();
#endif

	if (stream->fIsInput) {
		// transfers complete in the order they were queued
		stream->fRawIndex = (stream->fRawIndex + 1) % kSamplesBufferCount;
		stream->_HandleRecordedPackets(stream->fRawIndex);
		status = stream->_QueueNextTransfer(stream->fRawIndex, false);
	} else {
		if (atomic_add(&stream->fProcessedBuffers, 1)
				> (int32)kSamplesBufferCount) {
			TRACE(ERR, "Processed buffers overflow:%d\n",
				stream->fProcessedBuffers);
		}
		stream->fRealTime = system_time();

		release_sem_etc(stream->fDevice->fBuffersReadySem, 1,
			B_DO_NOT_RESCHEDULE);

		stream->fCurrentBuffer
			= (stream->fCurrentBuffer + 1) % kSamplesBufferCount;
		status = stream->_QueueNextTransfer(stream->fCurrentBuffer, false);
	}

	if (status != B_OK)
		TRACE(ERR, "Error of queueing the next transfer: %s\n",
			strerror(status));

	atomic_add(&stream->fInsideNotify, -1);
}


void
Stream::_DumpDescriptors()
{
	for (size_t i = 0; i < fDescriptorsCount; i++)
		TRACE(ISO, "%d:req_len:%d; act_len:%d; stat:%#010x\n", i,
			fDescriptors[i].request_length,	fDescriptors[i].actual_length,
			fDescriptors[i].status);
}


uint32
Stream::FrameChannels()
{
	TypeIFormatDescriptor* format = static_cast<TypeIFormatDescriptor*>(
		fAlternates[fActiveAlternate]->Format());
	return format->fNumChannels;
}


uint32
Stream::ExposedChannels()
{
	uint32 channels = FrameChannels();
	if (fChannelLimit != 0 && channels > fChannelLimit)
		channels = fChannelLimit;
	return channels;
}


status_t
Stream::GetEnabledChannels(uint32& offset, multi_channel_enable* Enable)
{
	AudioChannelCluster* cluster = ChannelCluster();
	if (cluster == 0)
		return B_ERROR;

	// the description reports ExposedChannels() channels for the stream
	for (size_t i = 0; i < ExposedChannels(); i++) {
		if (offset < 32)
			B_SET_CHANNEL(Enable->enable_bits, offset, true);
		offset++;
		TRACE(INF, "Report channel %d as enabled.\n", offset);
	}

	return B_OK;
}


status_t
Stream::SetEnabledChannels(uint32& offset, multi_channel_enable* Enable)
{
	AudioChannelCluster* cluster = ChannelCluster();
	if (cluster == 0)
		return B_ERROR;

	for (size_t i = 0; i < ExposedChannels(); i++, offset++) {
		TRACE(INF, "%s channel %d.\n",
			(offset < 32 && B_TEST_CHANNEL(Enable->enable_bits, offset)
			? "Enable" : "Disable"), offset + 1);
	}

	return B_OK;
}


status_t
Stream::GetGlobalFormat(multi_format_info* Format)
{
	_multi_format* format = fIsInput ? &Format->input : &Format->output;
	format->cvsr = fAlternates[fActiveAlternate]->GetSamplingRate();
	format->rate = fAlternates[fActiveAlternate]->GetSamplingRateId(0);
	format->format = fAlternates[fActiveAlternate]->GetFormatId();
	TRACE(INF, "%s.rate:%d cvsr:%f format:%#08x\n",
		fIsInput ? "input" : "ouput",
		format->rate, format->cvsr, format->format);
	return B_OK;
}


status_t
Stream::SetGlobalFormat(multi_format_info* Format)
{
	_multi_format* format = fIsInput ? &Format->input : &Format->output;
	AudioStreamAlternate* alternate = fAlternates[fActiveAlternate];

	uint32 oldRate = alternate->GetSamplingRate();
	uint32 newRate = alternate->SamplingRateFromId(format->rate);
	status_t status = B_OK;

	if (newRate != 0 && newRate != oldRate) {
		// cancel data flow - it will be rewaked at next buffer exchange call
		Stop();

		// the rate is checked against the descriptor of the device first
		status = alternate->SetSamplingRate(newRate);
		if (status != B_OK) {
			TRACE(ERR, "Rate %d is not supported, stay at %d.\n", newRate,
				oldRate);
			// the format structure reports what the device really does
			status = B_OK;
		} else {
			status = _SendSamplingRate();
			if (status != B_OK) {
				// the device refused: keep the old rate on both sides
				alternate->SetSamplingRate(oldRate);
			} else {
				// layout of buffers should be adjusted after changing
				// sampling rate
				status = _SetupBuffers();
				if (status != B_OK) {
					// the buffers are gone, make the old ones back
					alternate->SetSamplingRate(oldRate);
					_SendSamplingRate();
					_SetupBuffers();
				}
			}
		}
	} else if (fAreaSize == 0) {
		status = _SetupBuffers();
	} else
		TRACE(INF, "No changes required\n");

	alternate->SetFormatId(format->format);

	format->cvsr = alternate->GetSamplingRate();
	format->rate = alternate->GetSamplingRateId(0);
	format->format = alternate->GetFormatId();
	TRACE(INF, "%s.rate:%d cvsr:%f format:%#08x: %s\n",
		fIsInput ? "input" : "output",
		format->rate, format->cvsr, format->format, strerror(status));
	return status;
}


status_t
Stream::GetBuffers(multi_buffer_list* List)
{
	if (fAreaSize == 0)
		return B_NO_INIT;

	int32 startChannel = List->return_playback_channels;
	int32 requestChannels = List->request_playback_channels;
	buffer_desc** Buffers = List->playback_buffers;

	if (fIsInput) {
		List->flags |= B_MULTI_BUFFER_RECORD;
		List->return_record_buffer_size = fSamplesCount / kSamplesBufferCount;
		List->return_record_buffers = kSamplesBufferCount;
		startChannel = List->return_record_channels;
		requestChannels = List->request_record_channels;
		Buffers = List->record_buffers;

		TRACE(DTA, "flags:%#10x\nreturn_record_buffer_size:%#010x\n"
			"return_record_buffers:%#010x\n", List->flags,
			List->return_record_buffer_size, List->return_record_buffers);
	} else {
		List->flags |= B_MULTI_BUFFER_PLAYBACK;
		List->return_playback_buffer_size = fSamplesCount / kSamplesBufferCount;
		List->return_playback_buffers = kSamplesBufferCount;

		TRACE(DTA, "flags:%#10x\nreturn_playback_buffer_size:%#010x\n"
			"return_playback_buffers:%#010x\n", List->flags,
			List->return_playback_buffer_size, List->return_playback_buffers);
	}

	TypeIFormatDescriptor* format = static_cast<TypeIFormatDescriptor*>(
		fAlternates[fActiveAlternate]->Format());

	// the channels of this stream follow the channels of the streams already
	// handled (startChannel); never write more than the caller asked for
	int32 channels = ExposedChannels();
	if (channels > requestChannels - startChannel)
		channels = requestChannels - startChannel;
	if (channels <= 0)
		return B_OK;

	// [buffer][channel] init buffers
	for (size_t buffer = 0; buffer < kSamplesBufferCount; buffer++) {
		TRACE(DTA, "%s buffer #%d:\n", fIsInput ? "input" : "output", buffer + 1);

		struct buffer_desc descs[channels];
		memset(descs, 0, sizeof(descs));
		for (int32 channel = 0; channel < channels; channel++) {
			// init stride to the same for all buffers
			uint32 stride = format->fSubframeSize * format->fNumChannels;
			descs[channel].stride = stride;

			descs[channel].base = (char*)fBuffers;
			descs[channel].base += buffer * fSubBufferBytes;
			descs[channel].base += channel * format->fSubframeSize;

			TRACE(DTA, "%d:%d: base:%p; stride:%#010x\n", buffer, channel,
				descs[channel].base, descs[channel].stride);
		}

		// the descriptors of this stream go behind the ones already written
		if (!IS_USER_ADDRESS(Buffers[buffer])
				|| user_memcpy(Buffers[buffer] + startChannel, descs,
					sizeof(descs)) < B_OK) {
			return B_BAD_ADDRESS;
		}
	}

	if (fIsInput) {
		List->return_record_channels += channels;
		TRACE(MIX, "return_record_channels:%#010x\n",
			List->return_record_channels);
	} else {
		List->return_playback_channels += channels;
		TRACE(MIX, "return_playback_channels:%#010x\n",
			List->return_playback_channels);
	}

	return B_OK;
}


bool
Stream::ExchangeBuffer(multi_buffer_info* Info)
{
	if (atomic_get(&fProcessedBuffers) <= 0)
		return false;

	if (fIsInput) {
		Info->recorded_real_time = fRealTime;
		Info->recorded_frames_count += fSamplesCount / kSamplesBufferCount;
		Info->record_buffer_cycle = fCurrentBuffer;
	} else {
		Info->played_real_time = fRealTime;
		Info->played_frames_count += fSamplesCount / kSamplesBufferCount;
		Info->playback_buffer_cycle = fCurrentBuffer;
	}

	atomic_add(&fProcessedBuffers, -1);

	return true;
}
