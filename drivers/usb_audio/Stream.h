/*
 *	Driver for USB Audio Device Class devices.
 *	Copyright (c) 2009-13 S.Zharski <imker@gmx.li>
 *	Distributed under the terms of the MIT license.
 *
 */
#ifndef _USB_AUDIO_STREAM_H_
#define _USB_AUDIO_STREAM_H_


#include "AudioStreamingInterface.h"


class Device;

class Stream : public AudioStreamingInterface {
	friend	class			Device;
public:
							Stream(Device* device, size_t interface,
								usb_interface_list* List);
							~Stream();

			status_t		Init();
			status_t		InitCheck() { return fStatus; }

			status_t		Start();
			status_t		Stop();
			bool			IsRunning() { return fIsRunning; }
			void			OnRemove();

			status_t		GetBuffers(multi_buffer_list* List);

			status_t		OnSetConfiguration(usb_device device,
							const usb_configuration_info* config);

			bool			ExchangeBuffer(multi_buffer_info* Info);
			// Channels the stream carries in every sample frame, and the
			// number of them reported to the multi_audio API (the API
			// handles at most 32 channels altogether, see Device.cpp)
			uint32			FrameChannels();
			uint32			ExposedChannels();
			void			SetChannelLimit(uint32 limit) { fChannelLimit = limit; }

			status_t		GetEnabledChannels(uint32& offset,
								multi_channel_enable* Enable);
			status_t		SetEnabledChannels(uint32& offset,
								multi_channel_enable* Enable);
			status_t		GetGlobalFormat(multi_format_info* Format);
			status_t		SetGlobalFormat(multi_format_info* Format);

protected:
			Device*			fDevice;
			status_t		fStatus;

			usb_pipe		fStreamEndpoint;
			usb_device		fUsbDevice;
			const usb_configuration_info*	fUsbConfig;

			bool			fIsRunning;
			area_id			fArea, fKernelArea;
			size_t			fAreaSize;
			usb_iso_packet_descriptor* fDescriptors;
			size_t			fDescriptorsCount;

			// recording: the USB stack receives the iso packets into a raw
			// buffer, the actual data of every packet is then copied (packed)
			// into the buffers the application sees
			area_id			fRawArea;
			uint8*			fRawBuffers;
			size_t			fRawBufferSize;		// bytes per sub-buffer
			size_t			fPacketMax;			// bytes requested per packet
			size_t			fPacketsPerBuffer;
			size_t			fFrameSize;			// bytes per sample frame
			size_t			fSubBufferBytes;	// bytes per application buffer
			size_t			fFillBytes;			// bytes already stored in it
			size_t			fWriteBuffer;		// the buffer which is filled
			size_t			fRawIndex;			// raw sub-buffer last handled
			uint8*			fBuffers;
			uint8*			fKernelBuffers;
			size_t			fCurrentBuffer;
			size_t			fSamplesCount;
			uint32			fChannelLimit;		// 0 - no limit

			bigtime_t		fRealTime;
			uint32			fStartingFrame;
			int32			fProcessedBuffers;
			int32			fInsideNotify;

private:
			status_t		_ChooseAlternate();
			status_t		_SetupBuffers();
			void			_FreeBuffers();
public:
			void			ReleaseBuffers();
private:
			status_t		_ActivateAlternate();
			void			_SetIdle();
			status_t		_SendSamplingRate();
			void			_HandleRecordedPackets(size_t rawIndex);
			status_t		_QueueNextTransfer(size_t buffer, bool start);
	static	void			_TransferCallback(void* cookie, status_t status,
								void* data, size_t actualLength);
			void			_DumpDescriptors();
};


#endif // _USB_AUDIO_STREAM_H_

