SimpleZyn 0.2 (alpha) - a simple native control panel for ZynAddSubFX
======================================================================

SimpleZyn starts the ZynAddSubFX engine without its own window ("zynaddsubfx --no-gui")
and controls it over OSC. Everything the engine says is copied to the Terminal where you
started SimpleZyn, so you can see what is going on.

What it does
  * Instruments: banks are read from the Zyn data folder (/system/data/zynaddsubfx/banks),
    from the non-packaged data folders, and from folders you add (File -> Add banks folder).
    Choose a bank, double-click an instrument to load it (.xiz) into the selected part.
    The search box looks in all banks.
  * 16 parts, one panel (like in Zyn itself): choose the part number in the number menu in the "Part" box,
    then Ch (MIDI channel), Vol, Pan, M (mute: the part ignores new notes) and S (solo).
    The box title shows the instrument of the selected part. Loading an instrument switches the part on.
  * Box "System" (the whole synthesizer): Vol (master volume), Rev (one reverb in system effect 1;
    how much of every part goes into it), Rst (restart the engine) and Rec.
  * File > Edit instrument (Ctrl+E): a window with the main parameters of the ADsynth of the selected
    part. Upper row: Amp (Att, Dec, Sus, Rel), Filter (type, Cut, Res, Stg: stages), Osc (waveform,
    Shp, Uni, Spr: the shape of the wave, unison size and detune), LFO (Dep, Spd: vibrato), Vol.
    Lower row: FEnv, the filter envelope (Sta start level, Att, Pea peak level, Dec, Rel, End release
    level; 64 means "the cutoff of the filter"), Pitch (Oct, Crs, Fin: octave, coarse and fine tuning)
    and PEnv, the pitch envelope (Sta, Att, End, Rel; 64 means "the pitch of the note"). The window follows the
    selected part and reads the real values from the engine (if the engine does not answer, the
    status line says so). File > Save instrument as... writes a .xiz file; the default folder is
    ~/SimpleZyn/Banks/My, which is scanned like every other bank folder.
    Not in the editor yet: PADsynth, SUBsynth, the other voices of the ADsynth, effects of the part.
  * Menu Reverb: Preset (13 presets of the engine: halls, rooms, a tunnel...; a preset sets all the
    reverb parameters) and Type: Random, Freeverb, Bandwidth (the last one adds a shimmering,
    slightly detuned smear to the tail). The Rev slider is how much of every part goes into it.
  * Rec: writes the sound to ~/SimpleZyn/INSTRUMENT-DD.HH.MM.SS.wav (the instrument of the selected part, day.hour.minutes.seconds) (the engine starts writing
    with the first note). Menu Engine > NewRP (New Record Port; checked or not): when checked, every
    finished recording is sent to BeAE-Plus at once. File > Send last recording does it by hand.
    If BeAE-Plus (3.2 or newer, built from this repository) is already running, the recording goes
    to a new track of its open project; otherwise BeAE-Plus is started with the file.
    A live port for BeAE-Plus or a video editor is planned.
  * MIDI: connect a keyboard to the MIDI consumer "ZynAddSubFX", for example the program
    MidiKeyboard (https://github.com/HaikuArchives/MidiKeyboard) or your USB keyboard.

Needs: ZynAddSubFX (the "zynaddsubfx" command, see build_zynaddsubfx_hpkg.sh).

Build on Haiku:   make
Check the portable part anywhere (OSC, banks, launcher):   make test

Not done yet: saving the set of instruments, effects other than the reverb, the automatic
connection of MidiKeyboard.

History
  0.2  Window in the style of the original: System and Instrument boxes, a slider with its own box for
       every value. Reverb menu (presets and types). Instrument editor (File > Edit instrument) with
       saving to .xiz. A recording is named after the instrument; it goes to a new track of the open
       BeAE-Plus project (NewRP in the Engine menu: automatically). The WAV header problem of the
       engine is solved. Rst button.
  0.1  First version: banks, instruments, 16 parts, volume, pan, channel, mute, solo, reverb, recording.
