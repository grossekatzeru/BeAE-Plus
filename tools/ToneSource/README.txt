ToneSource - test sound source for the Haiku Media Kit
=======================================================

A tiny program that publishes a Media Kit node producing a sine tone
(stereo, 48 kHz, float). Its frequency is typed by hand (440 Hz by default;
the start value can also be given on the command line: ToneSource 880).

Start several copies to get several independent sources: they are named
"Tone Source", "Tone Source 2", ... and show up

  * in Cortex (output of the node), and
  * in the list of recording sources of BeAE-Plus 3.2 alpha.

It is meant for testing recording from several sources without a second
sound card. The tone is produced while something is connected to the node
and has started it (for example BeAE-Plus while recording, or Cortex).

Build on Haiku:  make
