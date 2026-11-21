BeAETap - a pass-through node of the Haiku Media Kit with a second output
=========================================================================

The program publishes a node "BeAE Tap" (several copies: "BeAE Tap 2", ...). The node has one input and
two outputs. Everything that comes into the input goes out of both outputs, so a sound that was going to
the Audio Mixer can also go to BeAE-Plus (live recording of any application, for example ZynAddSubFX).

Cortex:
    application  ->  BeAE Tap (input)
    BeAE Tap "Pass"  ->  Audio Mixer       (the sound in the speakers stays)
    BeAE Tap "Copy"  ->  the input of BeAE-Plus (or any recorder)

How to connect: in Cortex drag the output of the application to the input of BeAE Tap, then the outputs
"Pass" and "Copy" to the Audio Mixer and the recorder. The window of the program shows what is connected,
the format, the number of buffers and how many were lost (the node behind was too slow).

Notes
  * The format is the one of the first connection (raw audio); the bytes are copied as they are. The three
    connections must have the same sample rate and format.
  * The node adds the latency of the slowest node behind it (usually a few milliseconds).
  * The node is started by the program itself; it works while the program runs.

Build on Haiku:  make
