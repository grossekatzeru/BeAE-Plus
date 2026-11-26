# usb_record_test

Talks directly to a `hmulti` audio driver (e.g. `usb_audio`) using the same
ioctl sequence as the `multi_audio` media add-on, without the media layer.
It shows which buffer cycles `B_MULTI_BUFFER_EXCHANGE` returns and the peak
level of each new record buffer.

    make
    ./usb_record_test /dev/audio/hmulti/usb/1 10      # record only
    ./usb_record_test /dev/audio/hmulti/usb/1 10 -p   # also feed playback

Stop the media services first (`killall media_server media_addon_server`),
otherwise the device is already open. Lines with `*` mark a new cycle.
Not compiled or tested on Haiku by the author.
