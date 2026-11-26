# usb_audio - Haiku USB Audio Class driver (BeAE-Plus fork)

This directory holds the Haiku kernel driver `usb_audio`
(`src/add-ons/kernel/drivers/audio/usb` in the Haiku tree) with fixes made for
the BeAE-Plus project. The first commit of this directory (`b8950fc`) is the
original, unmodified source; every change is a separate commit, so
`git diff b8950fc -- drivers/usb_audio` shows exactly what was changed.

**Status: stage 1 is written but NOT compiled and NOT tested on Haiku.**
Only a syntax check against the Haiku headers was done. Treat it as
experimental.

Original code: Copyright (c) 2009-13 S.Zharski, MIT license.

## What the driver supports

- USB Audio Class 1.0 devices (AudioControl + AudioStreaming interfaces),
  Format Type I, PCM 8/16/18/20/24/32 bit and IEEE float, up to 32 channels
  per stream (stage 2, see below), subframe sizes 1, 2 and 4 bytes.
- USB Audio Class 2.0 devices are **not** supported yet: the AudioControl
  header with `bcdADC != 0x0100` is rejected in
  `AudioControlInterface::Init()`, so such a device is not initialised at all.
  (This is about the audio class version, not the USB 2.0 bus.)

## Changes made (stage 1)

### Stream.cpp / Stream.h

1. **Sampling rate is initialised.** `fSamplingRate` was never set, so the
   driver reported rate 0 and created no buffers until the application
   changed the format. `Stream::Init()` now selects 48000, else 44100, else
   the highest rate of the alternate. The buffers are still created in the
   ioctl context (`SetGlobalFormat()` or, if it was not called, `GetBuffers()`)
   and released in `Device::Close()`: the area given to the application has to
   be made by a thread of the application's team, otherwise the application
   gets kernel addresses it can not use (an earlier version of this change
   created them in `Init()`, which broke recording).
2. **Recording: packets are packed.** Isochronous packets have different
   length (44 or 45 frames at 44.1 kHz), but the old code requested a fixed
   `floor(rate/1000)` frames per packet and treated the buffer as contiguous.
   Longer packets were truncated, short ones left gaps, which gives clicks and
   crackle. Now the stack receives packets into a separate raw area
   (`request_length` = maximal packet size of the endpoint) and
   `_HandleRecordedPackets()` copies only the real `actual_length` bytes into
   the application buffers, using every packet with received data, a packet status other than B_OK is only logged.
3. **Playback: frames are spread over the packets.** The frames of a buffer are
   distributed evenly (44, 44, ..., 45 at 44.1 kHz) and a buffer always covers
   a whole number of 1/rate periods (`rate / gcd(rate, 1000)` frames in
   `1000 / gcd(rate, 1000)` packets). Before, `floor()` was used for every
   packet, so the device was fed slower than it played.
4. **Buffer areas.** Created with `B_FULL_LOCK` (the USB callback writes to
   them), the raw recording area is a kernel area. `_FreeBuffers()` replaces
   the duplicated clean-up code, `delete[]` is used for the descriptor array,
   the creation failures are handled.
5. **`SetGlobalFormat()`.**
   - the requested rate is validated against the device descriptor first (the
     result used to be ignored);
   - the `SET_CUR` sampling frequency request is sent before the buffers are
     rebuilt, and the old rate is restored if the device refuses;
   - the request is sent only if the endpoint has the Sampling Frequency
     Control bit (bit 0 of `bmAttributes`) or the stream has several rates; a
     fixed-rate device gets no request (many of them stall it);
   - the structure returns the format the device really uses;
   - buffers are created if they do not exist yet.
6. **`OnSetConfiguration()`** sends the rate to the device (`_SendSamplingRate()`),
   so the device runs at the rate the buffers were made for.
7. **`Start()`**: `fIsRunning` is set before the transfers are queued (the
   completion callback ignored its first calls otherwise); a queueing error
   cancels the start.
8. **`GetBuffers()`**: the channels of the second stream of the same direction
   are written at offset `startChannel`. The old loop
   `for (channel = startChannel; channel < fNumChannels; ...)` did not run for
   such a stream and copied an uninitialised kernel stack array to user space;
   even with a fixed loop the descriptors would have overwritten the ones of
   the first stream. The number of channels is limited to what the caller
   requested and the array is zeroed.

### Device.cpp / Driver.cpp

- `new Stream` is checked for NULL.
- On a replug (`CompareAndReattach()` calls `_SetupEndpoints()` again) the old
  streams are deleted; they were duplicated before.
- `B_MULTI_GET_BUFFERS` and `B_MULTI_GET_MIX/SET_MIX` limit the sizes taken from
  user space (`request_*_buffers` <= 64, `item_count` <= 1024); they size
  arrays on the kernel stack.
- `usb_audio_open()` finds the device by the number in the published name
  (`audio/hmulti/usb/N`). The old loop stopped at the first empty slot of
  `gDevices` and compared against a compacted name list, so it could open the
  wrong device or fail after unplugging one.

## Changes made (stage 2: more than 2 channels)

Also not compiled and not tested on Haiku, and not tested with a multichannel
device at all.

- `Stream::_ChooseAlternate()` accepts streams with up to 32 channels (was 2).
  The alternate with the most channels/bits is still preferred.
- The multi_audio API (and the `multi_audio` media add-on) keeps the enabled
  channels in a 32 bit mask, so the stream channels plus the bus channels can
  not exceed 32 together. `Device::_UpdateChannelLimits()` shares this budget
  between the streams (a small stream keeps all its channels, the rest is
  divided equally). A stream with more channels still carries all of them in
  the USB frame; only the first ones are reported to the API
  (`Stream::ExposedChannels()`), and the buffer descriptors, enabled-channel
  bits and the description use the same number.
- `B_MULTI_GET_DESCRIPTION` no longer truncates the channel list to 16
  entries (32 now); the list is zeroed and `request_channel_count` is checked.
- `AudioControlInterface::GetTerminalChannels()` reports exactly as many
  channels as the stream has. The designations come from the cluster's channel
  configuration bitmap; if it is empty ("unspecified" layout of a
  multi-channel interface) or has fewer bits than channels, the missing
  channels get a generic designation. Before, such a stream was reported with
  0 or fewer channels and the channel numbers did not match the buffers.
- `B_MULTI_LIST_MIX_CONTROLS` and negative buffer counts are range checked.

Limit: 32 reported channels in total. A device with e.g. 18 inputs and 20
outputs shows 16 + 16; more needs a change of the multi_audio add-on and
the API.

## Device names

`friendly_name` (what the Media preferences list) is the USB product string of
the device instead of the fixed "USB Audio" (`Device::_ReadProductName()`,
UTF-16 to UTF-8, up to 31 bytes). If the device has no string, "USB Audio" is
used. The name comes from the device, so cards of the same model look alike.

## Known problems that are NOT fixed yet

- USB Audio Class 2.0 (clock sources, 4-byte feature unit controls, sample
  rates via GET_RANGE/SET_CUR on the clock entity, AS interface revision 2).
- More than 32 reported channels in total (the multi_audio API limit).
- 24 bit in 3-byte subframes (blocked by Haiku ticket #20267, resampler).
- High-speed microframes (125 us); packets are assumed to be 1 ms.
- Explicit feedback endpoint for asynchronous playback.
- Playback buffer sequencing: the logic of `fCurrentBuffer` and the double
  queueing of the first buffer at start looks suspicious; left unchanged
  because it can not be verified without hardware.
- Re-initialisation of the AudioControl interface on replug may duplicate
  controls.

## Build and install

Copy the files into `src/add-ons/kernel/drivers/audio/usb` of the Haiku tree
(keep a copy of the original), `touch` them (jam compares file times), then
from the generated directory:

```
jam -q usb_audio
```

Install without touching the system files:

```
mkdir -p ~/config/non-packaged/add-ons/kernel/drivers/dev/audio/hmulti
cp <built usb_audio> ~/config/non-packaged/add-ons/kernel/drivers/dev/audio/hmulti/
```

Reboot. To roll back, delete that file and reboot (hold Shift at boot for the
safe mode if the system does not start).

## Diagnostics

`usb_audio.settings` (installed to `~/config/settings/kernel/drivers/`):

```
trace 0x7F
logfile /boot/home/usb_audio.log
```

Useful lines: `Alternate N EP:x selected`, `packets per buffer`,
`packet max`, `set_speed ... for ep`, `N of M packets with a status`.
Please attach the log and the output of `listusb -v` for the device when
reporting a problem.

## Buffer exchange: stale semaphore (found with tools/usb_record_test)

`B_MULTI_BUFFER_EXCHANGE` returned `B_ERROR` at once on a freshly opened
device: the shared `fBuffersReadySem` kept counts from the previous session,
so the wait passed while no buffer was ready. Now the semaphore is drained in
`Open()`, `fProcessedBuffers` is reset in `Stream::Start()`, and the exchange
waits again when a count has no ready buffer; any other wait error is
returned as it is. Not compiled or tested on Haiku.

## Diagnostic patch for the media add-on

`multi_audio-diag.patch` adds two `debug_printf` lines to
`MultiAudioNode::_OutputThread` (they go to `/var/log/syslog`, prefix
`multi_audio DIAG`): the node run state, the connection, the buffer group,
the resampler and the record cycles, and whether a buffer was produced.
Apply in `src/add-ons/media/media-add-ons/multi_audio` with `patch -p2 < ...`.
Not compiled or tested on Haiku.

## Why recording stayed at 0:00: the node is never started

The DIAG patch showed `run=1` (B_STOPPED) for a connected USB node.
`BMediaRecorder::Start()` calls `StartTimeSource()` for a producer that is a
time source (and `multi_audio` nodes are), and never `StartNode()`.
`MultiAudioNode::TimeSourceOp(B_TIMESOURCE_START)` did nothing, so the node
stayed stopped and `_OutputThread` (which sends recorded buffers only in
`B_STARTED`) delivered nothing. It worked for the default output device
only because the system mixer starts that node. `multi_audio-start.patch`
queues a `B_START` event in `TimeSourceOp`. Apply with `patch -p2` in
`src/add-ons/media/media-add-ons/multi_audio`. Not compiled or tested on Haiku.
