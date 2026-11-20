SimpleZyn 0.3 (alpha) - a simple native control panel for ZynAddSubFX
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
    part. Upper row: Amp (Att, Dec, Sus, Rel), Filter (type, Cut, Res, Stg: stages), Voice (the voice 1..8
    of the ADsynth that the box edits and its On switch, waveform, Vol, Pan, Shp, Uni, Spr: shape of
    the wave, unison size and detune), LFO (Dep, Spd: vibrato), Vol.
    Lower row: FEnv, the filter envelope (Sta start level, Att, Pea peak level, Dec, Rel, End release
    level; 64 means "the cutoff of the filter"), Pitch (Oct, Crs, Fin: octave, coarse and fine tuning)
    and PEnv, the pitch envelope (Sta, Att, End, Rel; 64 means "the pitch of the note"). The window follows the
    selected part and reads the real values from the engine (if the engine does not answer, the
    status line says so). File > Save instrument as... writes a .xiz file; the default folder is
    ~/SimpleZyn/Banks/My, which is scanned like every other bank folder.
    Punch (Str, Tim: how strong and how long the beginning of the note is) is next to the envelopes,
    Stereo (the ADsynth plays in stereo or mono) is under Vol of the Out box.
    The third row is the voice: the amplitude envelope (VAmp), the filter (VFlt: Cut, Res) and its
    envelope (VFEnv) of the voice chosen in the Voice box. They only work when the On box of the
    group is ticked. The fourth row is the effects of the part: three slots, each with the type (Reverb, Echo, Chorus,
    Phaser, Alienwah, Distortion, EQ, DynFilter), a preset and the main parameters on sliders. The
    effect is an insert of the part (the Rev slider of the main window is a separate send reverb).
    The EQ has 8 bands: the menu next to the type chooses the band, and the sliders (Typ, Frq, Gai,
    Q) edit that band; Vol is the output level. DynFilter has 5 presets (WahWah, AutoWah, Sweep,
    Vocal Morph 1/2) and the sliders Vol, Frq, Dep, Sns, Smo.
    A slider shows its value in the units of the sound (seconds, semitones, octaves, Hz) in the
    status line while it is moved, and in its tool tip.
    Not in the editor yet: PADsynth, SUBsynth, the amplitude and filter of every voice, effects of the part.
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

MIDI player (menu MIDI > MIDI player...)
  A separate window like the editor. Open a .mid file (or drop it on the window), Play/Pause, Stop,
  Silence, a position slider, Tempo 25-200 %, Repeat. The player connects to the MIDI consumer
  "ZynAddSubFX" by itself. The table has a row for every channel the file plays: the channel, the name
  of the track (and the General MIDI program), a light that flashes with the notes, where the channel
  sounds (Zyn or off), the instrument and the M (mute) and S (solo) boxes. The instrument button opens
  the banks: the choice loads the instrument into the Zyn part of the same number (channel N plays
  part N) and sets the part to listen to that channel. Every row has a volume slider (the volume of that part). "(as it is in Zyn)" does not change the part.
  Program changes and controllers of the file are not sent: the instruments are the ones you chose.
  The menu "where it sounds" has three choices: Zyn, GM synth and off. GM synth is the General MIDI
  synthesizer of Haiku (the one the system MidiPlayer uses; it loads when you first choose it, it takes
  a moment): the channel is played with the instrument of the file (programs and drums of General
  MIDI), and the slider is its volume. Choose it for the drum channel (10) to get a real drum kit.

History
  0.3  MIDI player. Effects of the part with EQ (8 bands) and DynFilter. Per-voice amplitude envelope, filter and
       filter envelope (VAmp, VFlt, VFEnv). Rows of the editor fold with the button over them. Editor: second row (envelopes, pitch,
       Punch), voices 1-8, Stereo, units in the tooltips, values are read from the engine in a
       background thread.
  0.2  Window in the style of the original: System and Instrument boxes, a slider with its own box for
       every value. Reverb menu (presets and types). Instrument editor (File > Edit instrument) with
       saving to .xiz. A recording is named after the instrument; it goes to a new track of the open
       BeAE-Plus project (NewRP in the Engine menu: automatically). The WAV header problem of the
       engine is solved. Rst button.
  0.1  First version: banks, instruments, 16 parts, volume, pan, channel, mute, solo, reverb, recording.
