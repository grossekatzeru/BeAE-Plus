BeAE audio editor for Haiku, with a wide array of features.

![Screenshot](BeAe.png)

<hr></hr>
Features:<br>
Fade-int function<br>
Fade-out function<br>
Function Normilize<br>
Bass bost function<br>
Compressor function<br>
Amplifier<br>
Delay<br>
Reverb<br>
Room Effects<br>
Invert opportunity<br>
Exchange channels possibility<br>

<hr></hr>
Recording:<br>
The red Record button records from the system audio input into a new document.
The format (sample rate, channels, sample type) is whatever the Haiku media mixer/input provides,
and the document keeps that sample rate. Press Record or Stop to finish; the recording opens as an
unsaved document. The VU meters show the input level while recording.<br>

<hr></hr>
LADSPA plug-ins:<br>
Transform &gt; LADSPA plug-ins lists the LADSPA effects found in <code>$LADSPA_PATH</code>,
<code>~/config/lib/ladspa</code>, <code>~/config/non-packaged/lib/ladspa</code>,
<code>/boot/system/lib/ladspa</code> and <code>/boot/system/non-packaged/lib/ladspa</code>
(use "Rescan plug-ins" after installing new ones). Each plug-in gets a window with sliders
for its control ports, a real-time preview and Apply. Supported audio layouts: 1 in/1 out,
2 in/2 out, 1 in/2 out and 2 in/1 out. Parameter values are remembered in
<code>~/config/settings/BeAE/ladspa/</code>, and "Repeat last filter" works with plug-ins.<br>
The built-in effects stay available.<br>
