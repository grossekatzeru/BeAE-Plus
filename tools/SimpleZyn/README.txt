SimpleZyn 0.1 (alpha) - a simple native control panel for ZynAddSubFX
======================================================================

SimpleZyn starts the ZynAddSubFX engine without its own window ("zynaddsubfx --no-gui")
and controls it over OSC. Everything the engine says is copied to the Terminal where you
started SimpleZyn, so you can see what is going on.

What it does
  * Instruments: banks are read from the Zyn data folder (/system/data/zynaddsubfx/banks),
    from the non-packaged data folders, and from folders you add (File -> Add banks folder).
    Choose a bank, double-click an instrument to load it (.xiz) into the selected part.
    The search box looks in all banks.
  * 16 parts: select a part with the round button at its left, switch it on, MIDI channel,
    volume, pan, M (mute: the part ignores new notes), S (solo).
  * Master volume and one Reverb slider (a reverb in system effect 1; how much of every part
    goes into it).
  * Record: writes the sound to ~/SimpleZyn/SimpleZyn-DATE-TIME.wav (the engine starts writing
    with the first note). "Open in BeAE-Plus" sends the file to BeAE-Plus.
  * MIDI: connect a keyboard to the MIDI consumer "ZynAddSubFX", for example the program
    MidiKeyboard (https://github.com/HaikuArchives/MidiKeyboard) or your USB keyboard.

Needs: ZynAddSubFX (the "zynaddsubfx" command, see build_zynaddsubfx_hpkg.sh).

Build on Haiku:   make
Check the portable part anywhere (OSC, banks, launcher):   make test

Not done yet: saving the set of instruments, effects other than the reverb, the automatic
connection of MidiKeyboard.
