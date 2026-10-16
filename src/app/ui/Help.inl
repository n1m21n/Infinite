// Part of src/main.cpp, included there in order (one translation unit).
// node help texts and the help window

   const char* SpecificNodeHelpText(const std::string& typeName)
   {
      static const std::unordered_map<std::string, const char*> kText = {
         // ---------------- Source / Text ----------------
         { "Image Source", "Loads a still image. Opens the native file picker and decodes anything macOS can read - PNG, JPEG, TIFF, HEIC, RAW and more." },
#if INFINITE_GPL
         { "Punch FX", "Pocket-operator performance effects (full build, GPL). 16 pads, each with its own effect (27: loops, stutter, reverse, tape stop, filter sweeps, kills, crush, gate, echo, rewind, flanger, freeze...): click a pad to select it and pick its effect below. Every pad has its own pin, like the MPC: MIDI learn it to a controller pad, or patch a Macro / MIDI Trigger. Notes cabled into the second input also play pads (36 = pad 1). Hold to play, or latch to toggle; any number at once. Loops lock to the beat. effect + engage are a single MIDI-learnable trigger. Port of SLOOP/Felucca's punch-in FX." },
#endif
         { "Video", "Plays video and its soundtrack from separate video/audio output pins. Includes trim in/out, restart, reverse, loop and four cue inputs. Click the timeline to seek or Shift-click to create a cue; any cue input crossing above zero jumps to its marker." },
         { "VMPC", "VJ clip launcher, the MPC for video: 16 pads, one video clip each, hit with the mouse, a CV pin per pad or the note input (base note 36 = pad 1). Per pad: one shot, gate (loops while held) or loop (toggle), trim in/out and speed (negative = reverse). The output is the clip of the last pad hit; with nothing playing it is transparent (or the last frame with hold last frame). Transition (cut, fade, slide, wipe, zoom) and its time apply when a hit switches clips. Silent: use a Video node for soundtracks." },
         { "Noise", "Procedural noise: value, fBm, ridged, Voronoi, Worley edges and white. Domain warping, octaves and colour mapping included. The 4D types (Simplex, Perlin, Ridged, Turbulence, Billow 4D) work like TouchDesigner's Noise TOP: the picture is a slice of 4D noise and speed moves along the 4th axis, so it morphs in place; drift adds an optional scroll, plus translate, z, rotate and exponent." },
         { "Shape", "The base 2D vector-primitive node - pick any of its 20 shapes from the dropdown, with fill, stroke, feather and background controls. Each shape also has its own directly-spawnable named node (Circle, Hexagon, Star, ...) that just starts on that shape." },
         { "Draw", "Paint straight onto the node preview. Six procedural brushes, eraser, spacing and jitter. Patch an image in to paint over it. Record, then draw - replaying redraws the stroke in time, and the canvas size follows the input when one is patched in." },
         { "Key-Snap", "Moves every spectral peak of the sound to the nearest note of a scale, so chords and layered sources stay in key. Snap blends between untouched and fully in tune, glide sets how fast a peak slides to its new note, and global key follows the transport's key and scale. Latency is 42.7 ms (2048 samples). Ported from upstream Infinite." },
         { "Spectrum Slide", "Morphs the sound into the one on the 'to' pin by sliding its spectral peaks toward the other's positions instead of cross-fading, so a 440 Hz tone becomes 880 Hz by passing through 660 Hz. Slide sets how far along the way; with nothing on 'to' it passes the sound through. Latency is 42.7 ms (2048 samples). Ported from upstream Infinite." },
         { "Pattern Player", "Plays a pattern from the note pattern bank over a chord: the notes held on its 'chord' input, or, with nothing played into it, a chord built from the global key and scale (degree 1-7, 3 to 5 notes, octave), so it runs on its own with play / stop. The bank has arpeggios, piano and guitar accompaniments, strums, bass lines, sequencer riffs and ambient figures written for Infinite-Turbo, plus every .mid in your NotePatterns folder (one subfolder per category; rescan reads it again; the Note Looper saves takes there). Chord-relative patterns follow any chord; .mid patterns are transposed to its root. Starts on the next bar (bar sync), latch keeps the last chord, gate / swing / velocity / speed shape it. Play, previous, next, random and the pattern list are MIDI-learnable." },
         { "Note Looper", "A MIDI pattern recorder in the spirit of the MPC sequencer. REC arms and recording starts on the next bar (count-in adds a bar); a fixed length closes the loop by itself, 'free' closes it on the next REC (rounded up to whole bars), and the loop plays at once. DUB (overdub) adds a layer per pass, UNDO removes the last layer, click a layer to mute it, hold ERASE and the keys to remove their notes as the playhead passes. Quantize, strength and swing apply when it plays, so the take keeps its real timing. Eight sequences (A-H) switch on the next bar and are saved with the patch; 'save to bank' writes the sequence as .mid into the Pattern Player's folder, 'import .mid' adds a file as a layer. The live input passes through (thru). Every transport button is MIDI-learnable." },
         { "Spatial Mixer", "Mixes up to 12 inputs in 3D for headphones (binaural, measured MIT KEMAR ears or a spherical-head model) or speakers (stereo pan). Drag a dot on the top view to set its direction and distance, scroll over it for elevation, Alt-drag for level, double-click to reset. H keeps an object in front of your head, M / S mute and solo. Room adds early reflections, bass mono keeps the low end centred, the limiter holds the output at -1 dB true peak and the meter shows short-term loudness. Wire its output to Audio Out. Ported from upstream Infinite (without head tracking)." },
         { "MIDI Out", "Plays a hardware MIDI synth from a note cable. Pick the device and the channel (1-16); notes sent here do not reach Audio Out, so the synth makes its own sound. Four CC rows send a controller number and a modulatable value, only when it changes, so any LFO or macro can turn a knob on the synth. 'clock' makes Infinite the master clock (24 pulses per beat, with Start, Stop, Continue and song position) and switches itself off while Infinite follows an incoming clock. 'offset' shifts everything by -50 to +50 ms to line up with the synth's latency. Every held note is released when you bypass the node, change device, stop the transport or press panic. Windows cannot publish a virtual MIDI port: to reach another app, install loopMIDI, create a port and pick it here. Ported from upstream Infinite." },
         { "Formula", "A live GLSL shader. Pick a preset or press 'Edit GLSL...' to write your own; four knobs (uA-uD) are exposed for modulation. Four image inputs (in0-in3) are read as texture(uIn0, uv) .. uIn3, with uHasIn0..3 and uInRes0..3, like TouchDesigner's GLSL TOP; 'size from in0' renders at in0's resolution. Shadertoy mode: paste a whole Image tab (code with mainImage) and it runs as is, with iTime, iResolution, iFrame, iTimeDelta, iMouse (the mouse x / y / down params), iDate and iChannel0-3 = in0-in3. Every 'uniform float name; // lo..hi = default' line becomes an extra knob with a CV pin. For a Buffer pass turn on 'float buffer' (RGBA32F, alpha kept) and route 'last frame to' an iChannel for the node's own previous frame; chain Formula nodes for Buffer A -> B -> Image." },
         { "Texture", "Blender-standard procedural textures: Voronoi, Brick, Magic, Wave and Musgrave, each with its own parameter block." },
         { "Ramp", "Generates a gradient from scratch (no input) between up to 8 user-set colour stops, at a chosen angle, scale and offset. Gamma and dither smooth out visible banding." },
         { "Text", "Renders text using any font installed on the system, with size, colour, tracking, alignment and position." },

         // ---------------- Effects (built-in, non-filter-table) ----------------
         { "Curves", "Shadow / midtone / highlight lift plus an S-curve control, per RGB channel or all together. Drag points on the curve to bend it, click empty curve to add a point, right-click a point to remove it." },

         // ---------------- Color ----------------
         { "Color Ramp", "Recolors any 0-1 grayscale input through user-authored stops, up to 32 of them, with linear or constant interpolation. Unlike Gradient Map, it has no shape of its own - the shape comes from upstream." },

         // ---------------- Compositing ----------------
         { "Blend", "Two inputs and 32 blend modes - the full Normal / Multiply / Screen / Overlay / Hue / Saturation / Colour / Luminosity set, plus Erase and Anti-Erase (keep A only where B is opaque, a mask). Like TouchDesigner's Composite TOP it can place B instead of stretching it (pre-fit Fit / Fill / Native with justify, plus B translate, scale and rotate), combine alpha with Over / Under / Inside / Outside / Atop / Xor, and take its size from A, B or a custom resolution." },
         { "Layer Stack", "Four inputs stacked bottom-up: A is the base, D sits on top. Each layer has its own blend mode and opacity, and dragging a layer header reorders the whole layer." },
         { "Layout", "TouchDesigner-style Layout: a canvas of exact pixel size (1920x1080 by default) with up to 8 image inputs placed on it. Each layer starts at its REAL pixel size, and you set x/y in canvas pixels, a scale (anchored at the layer centre) or an exact custom width/height, and opacity. Drag layers on the miniature, or use 1:1 / fit / fill / centre. x, y, scale and opacity have CV pins." },
         { "Switcher", "Cycles between its connected inputs every N beats or seconds, with an optional crossfade. With 'manual' an index picks the input instead (A=0 .. D=3); a fractional index blends the two inputs either side of it, like TouchDesigner's Switch TOP, so modulating it crossfades." },
         { "Fit", "Resamples an input to a chosen resolution. Fit letterboxes, Fill crops, Stretch ignores aspect, Native passes through. Offset X/Y slides the result in output pixels (+ right, + up). Limit only ever shrinks: the source keeps its aspect and is scaled down to fit inside width x height. Smoothness Mipmap stops detailed images sparkling when shrunk. Use it to make differently-sized sources composite predictably." },
         { "Comment", "A free-floating note on the canvas - has no image input or output, just text. Double-click to edit." },
         { "Group", "A resizable box that owns whatever nodes are geometrically inside it - drag it and its members move together, right-click > Ungroup dissolves it (or ungroup one member from its own context menu)." },
         { "Null", "A pass-through node: its output is exactly its input, unchanged. Useful as a stable junction point to branch a cable to several destinations, or as a placeholder while rewiring." },
         { "Send Image", "Sends the image patched into it to every Receive Image with the same channel name, without a cable - Pure Data's [send]. Several Sends may share a name; a bypassed Send is muted. It has no output pin." },
         { "Receive Image", "Outputs the image of the Send Image nodes on its channel. With several Sends, combine picks the first, one by index, or adds / maxes / averages them. A Receive patched back into its own Send works like a Feedback loop: it reads the previous frame." },
         { "Send CV", "Sends the modulator patched into it to every Receive CV with the same channel name, without a cable." },
         { "Receive CV", "A modulator output carrying the Send CV nodes on its channel: the first one, or their sum, average, max or min (0..1)." },
         { "Send Notes", "Sends the note stream patched into it to every Receive Notes with the same channel name, without a cable." },
         { "Receive Notes", "Outputs the notes of every Send Notes on its channel, merged in time order, like Note Merge without the cables." },
         { "Send Audio", "Sends the audio patched into it to every Receive Audio with the same channel name, without a cable - Pure Data's [throw~]." },
         { "Receive Audio", "Outputs the sum of every Send Audio on its channel, scaled by gain - Pure Data's [catch~]. A loop through its own Send reads silence instead of feeding back." },
         { "Send Geometry", "Sends the geometry patched into it to every Receive Geometry with the same channel name, without a cable." },
         { "Receive Geometry", "Forwards the geometry of one Send Geometry on its channel (index picks which, oldest first), every material, texture and point-cloud channel included." },
         { "Viewport", "Shows its input at actual pixel size in its own resizable window, separate from the small node preview - useful for judging detail without zooming the whole canvas." },

         // ---------------- Modulators ----------------
         { "LFO", "Sine, triangle, saw up/down, square and sample-and-hold. Rate in beats, plus phase and an output range." },
         { "Random", "A new random value every N beats, with adjustable smoothing between steps. Deterministic, so rewinding replays the same sequence." },
         { "Pattern", "A 16-step sequencer: drag the bar grid (paint across bars by dragging horizontally), choose how many steps to use or fit them to the transport's time signature, and it loops through them one step every N beats. Optional glide between steps and a bipolar display mode." },
         { "Math", "Combines two modulators - add, subtract, multiply, divide, min, max, average, difference - with gain and offset. Unpatched inputs fall back to a constant, shown as an editable slider; a patched input just shows as 'patched'." },
         { "Geometry Table", "Samples up to 16 points off a patched geometry source and outputs each as its own X/Y/Z modulator, plus a centroid (cx/cy/cz) and spread aggregate that stay meaningful however the row count or point set changes. Vertex mode reads native vertices (or a point cloud's points, which always wins over its own billboard mesh); Scatter reads an area-weighted random sample instead; Contour walks the mesh's boundary/slice outline (or a patched curve) in order. Sort turns the table into something specific - by axis for a ramp bank, by angle for a phase-ordered ring, by distance from the centre. offset scrubs a read head across the sampled set without re-sampling, so it's the one control worth binding to an LFO. Fixed space maps world units through `extent` (how far the object may travel, not how big it is); Bounds self-scales to the sampled set's own bounding box, which means a rigid translation of the source produces no change - that's by design, not a bug." },
         { "Compare", "Outputs 1 when the comparison holds, 0 otherwise - >, >=, <, <=, ==, != between two modulators, with a tolerance for the equality checks." },
         { "Range to Range", "Remaps one modulator's input range onto a different output range - patch in a 0..1 LFO and remap it to -2..2, for example." },
         { "Smoothing", "An exponential moving average over another modulator, to damp jittery or steppy sources like Random or Pattern." },
         { "CV Recorder", "Records any modulator patched into its input and plays it back. Press Rec to capture (live input passes through while recording), press Rec again to stop and it starts looping straight away. speed is the playback rate - 2x plays the take twice as fast - and low/high map the take onto an output range. Takes are timed in beats, so they follow the tempo and pause with the transport (up to 64 beats), and they are saved with the patch. With nothing patched in, it records its own in knob, so you can perform a gesture by hand." },
         { "Envelope", "Applies an ADSR contour to an incoming modulator instead of generating its own trigger: rising above threshold starts attack/decay/sustain, falling back below it starts release. Patch an LFO in and its swing gets shaped by the ADSR, retriggering once per LFO cycle. Output collapses toward 0.5 as the envelope level falls - at level 0 the input has no say, at level 1 it passes through unchanged - so with nothing patched in, 'in' holds steady at its own constant value." },
         { "Mod Curve", "Remaps a modulator through a draggable transfer curve - click empty space to add a point, drag to move it, right-click to remove it (or right-click empty space to reset to a straight line). The gridline marks 0.5, where a bipolar binding's 'no modulation' point lives, and the moving dot shows where the input currently sits on the curve. mix blends between the raw input and the curved output, so 0 is a true bypass." },
         { "CV to Pitch", "Quantizes a modulator to semitone steps over range low..high, shown as a large +/-N st readout. Still outputs 0..1 like any modulator - it just restricts where in 0..1 the value can land, so the span maps onto whole semitones. Scale/root snap to scale degrees instead of every semitone (chromatic = off); glide adds portamento between steps, passing through unquantized values in transit on purpose." },
         { "Macro Knob", "A single named slider (0-1, with a response curve and invert) meant to be patched out to several other sliders at once - one control that fans out to many parameters." },
         { "Resonator Bank", "Tuned bank of up to 16 parallel bandpass resonators excited by incoming audio. Four harmonic/tuning structures (Harmonic, Odd, Chord, Metallic) with decay (T60), scatter detune and stereo spread." },
         { "Cycle Shaper", "Replaces every wavecycle of the input with a clean geometric waveform (Sine, Square, Triangle) of the same period and peak amplitude. Timbre is rebuilt while pitch and rhythm survive. Latency is one wavecycle." },
         { "Spec Blur", "Streaming phase vocoder (N=2048, hop 512) that smears spectral magnitude in time. Transients dissolve into a harmonic cloud with tilt, phase diffusion and freeze. Latency is 42.7 ms (2048 samples); default mix is pinned at 1.0." },
               { "Cycle Shaper", "Single-cycle waveform distortion and crossfade shaper." },
         { "Slideshow", "Plays the images of one folder in alphabetical order with a transition (fade, slide, wipe, zoom). Hold and transition time follow the transport; Native keeps source pixels 1:1, Best Fit stretches to fill, Proportional Fit preserves aspect. Restart / prev / next buttons are mappable (MIDI learn, Performance Mode, CV)." },
         { "Macro XY", "A 2D pad exposing X and Y as two separate modulator outputs from one drag. The pad's path can be recorded, looped and replayed in time, like Resynthesize's orb." },
         { "Keyboard", "Virtual keyboard, a hardware-free note source: click-and-drag the on-screen piano, or type on the computer keyboard (Logic/GarageBand's Musical Typing layout - ZXCVBNM... is one octave with the S D G H J row as black keys, QWERTY... the octave above, - and = shift the octave). By default typing plays while the pointer is over the node; turn on 'always' to play without hovering, for live use (still off while a text field has focus or Ctrl/Alt/Shift is held, so shortcuts keep working)." },
         { "Chord Progression", "Plays a looped chord progression locked to the transport. Click a slot to select a chord, click the keys to set its notes (or pick a root and quality and press set; inv moves the lowest note up an octave), and set how many bars it lasts. chords sets how many slots play, octave moves the keyboard, transpose shifts the whole progression, gate shortens each chord (1 = legato), and bass adds the lowest note an octave down. sets key makes each chord set the global key and a matching scale, so key-aware nodes follow the progression. play sets how each chord is played: block, strum up/down (strum = ms between notes), arp up/down/up-down/random (one note per rate step, over 1-3 octaves), pulse (the whole chord re-struck every step), alberti (low-high-middle-high) and bass + chord (oom-pah); gate is the fraction of the chord in block/strum and of each step in the stepped modes. Patch the note output into any synth or plugin instrument." },
         { "Transport Control", "Drives the global transport without the mouse. Every control is a pin: patch an LFO, a Macro, a MIDI CC (or use MIDI learn) into bpm, key, scale or the meter, and a Macro Trigger or MIDI pad into PLAY/STOP/REWIND/TAP. A section only takes over while its drive switch is on; off, its controls follow the live transport. glide ramps tempo changes (accelerando/ritardando), MIDI clock follows an external clock, on bar holds a key/scale change until the next bar. T taps tempo while the pointer is over the node. AUDIO starts/stops the audio engine (audio on open starts it whenever this patch is opened). Metronome: click turns it on (a pin, so a MIDI pad or Macro Toggle can switch it), accent raises the first beat of the bar, direct plays it straight to the audio device with no cable (and keeps it out of recordings), and the click output sends it into the graph for a mixer or Audio Out. Outputs: beat and bar are 0..1 ramps locked to the transport, bpm is the tempo mapped 20..300 to 0..1, play is 1 while playing, click is the metronome audio." },
         { "Audio Meter", "A stereo level meter: separate L and R bars on a shared -60 to +3 dBFS scale, each showing RMS (solid) inside peak (faint), a peak-hold line, and the channel's highest peak as a number on top, which turns red once that channel has reached 0 dBFS. Audio passes through unchanged. It measures whatever its input is patched to even with its output left unconnected, so it can hang off any cable as a tap. Click the meter to clear the peak numbers, holds and clip." },
         { "Macro Slider", "A named fader exposed as a modulator - drag its output onto any slider's modulation pin to drive that parameter by hand. The plain 0..1 member of the Macro family; use Macro Knob when you want a response curve and invert as well." },
         { "Macro Bipolar Knob", "A centre-detent knob running -1 to +1, exposed as a modulator - the right control for anything that has a natural middle (pan, detune, tilt). Its 0..1 output puts the detent at exactly 0.5, which is also where a bipolar modulation binding reads as 'no modulation'." },
         { "Macro Toggle", "A latching on/off switch exposed as a modulator: output is exactly 0.0 or 1.0, nothing in between. Useful for driving a mode/enable parameter, or for gating another modulator through a Math node's multiply." },
         { "Macro Trigger", "A momentary pad: output is 1.0 while pressed and 0.0 the moment it is released, with a visible flash on the press. Reach for it to fire something that responds to an edge - a Feedback burst, an envelope, a Resynthesize step - rather than to hold a value." },
         { "Macro NumBox", "A number box with drag-to-scrub and direct typing, over a range and step you set yourself - for when the value matters as a number (a count, a Hz figure, a bar length) and a 0..1 slider would be the wrong instrument." },
         { "Macro Radio Selector", "A multiple-choice selector emitting one discrete step per option - the control to use for a dropdown-shaped parameter (a mode, a waveform, a preset index), where a continuous slider would land between valid values. 'count' sets how many options." },
         { "Macro Step Gate", "An 8-step gate that advances on the transport at rate - a rhythm you draw rather than a curve. The output is the current step's on/off state, so it is the Macro family's answer to 'make this parameter pulse in time' without wiring an LFO and a Compare." },
         { "Scenes", "Turbo 0.50: one button, several changes, as radio buttons. A grid of up to 8 scenes (rows) x 8 outputs (columns), with an off row on top; cable each output to any param (a mute, a switcher slot, a drum part, a restart trigger). Press a scene: its row turns on and every output follows that row, so whatever was ON in the previous scene and is OFF here switches off. Press it again: the off row (option press again = off; all off does the same). Output modes: on/off (1 or 0, the default), choice (a stepped value, auto follows the cabled dropdown or slot), level (0..1) and pulse (a short trigger when a scene whose cell is ON starts). Scene buttons (scene 1..8), all off, prev / next and the scene selector are mappable (Performance Mode, where a scene trigger lights while its scene plays, MIDI learn, CV), and q makes changes wait for the next beat or bar." },
         { "Velocity to CV", "Converts how hard each note is played into a modulator (its 0..1 velocity), so touch can drive any parameter - brightness, pan, a filter opening on accents. It holds the last note's velocity through release. range low/high pick which velocities map onto the full 0..1 span (swap them to invert). The pitch counterpart is Note to CV." },
         { "Note Switcher", "Cycles between up to four connected note inputs every N beats or seconds, or pins to one slot with 'manual' - the note-cable counterpart of Switcher (2D) and Switcher 3D. When the active slot changes, new note-ons come only from the new slot, while notes already held on the old one still get their note-offs passed through, so nothing hangs." },
         { "Drift", "A musical random walk: an Ornstein-Uhlenbeck drift that keeps wandering around a home point and never jumps. speed is how fast it moves, stray how far it roams (a temperature), momentum how long a push carries on, range lo/hi the walls it bounces off, depth scales the swing. quantize holds the value on a tempo grid (a stepped, sample-and-hold drift) and smoothing rounds the steps off. Turbo keeps upstream's physics around a fixed home instead of the landscape upstream learns from your hand moves." },
         { "Path", "Outputs a moving 3D point (X/Y/Z, each patchable separately) travelling around a built-in shape (circle, helix, spiral, lissajous, etc.) at a beat-synced speed, or along the points of a patched-in geometry/curve source instead." },
         { "Constant", "Outputs one fixed number - the simplest possible modulator, useful for feeding a Math input or a modulation slot that expects a cable rather than manual control." },
         { "Null Modulator", "Pass-through modulation node that accepts an incoming modulator and forwards it to its output. Useful for monitoring, signal routing, or placeholders." },
         { "Palette", "Samples colours from a reference image, loaded here or patched in - a patched cable overrides the loaded file, but the file is kept so unplugging falls back to it. Drag its 'out' onto the square dot beside any colour swatch to bind it - each new cable takes the next swatch, and clicking a bound swatch steps it. Its image output is a gradient of the palette." },
         { "Audio File", "Loads an audio file for playback and Audio Analyze to read. Keeps analysing even while muted - the 'audible' checkbox only controls monitoring." },
         { "Image Analyze", "Turns an image or video into control values and modulation channels. Supports UV point probes, ROI boxes, 22 math/color operations, custom algebraic formulas, and multiple modulation output taps." },
         { "Audio Analyze", "Extracts level, low/mid/high, 8 bands and onsets as modulation values. Source: any audio cable on its audio pin (synth, VST, Audio In, mixer, Audio File); with nothing connected, the live device input (Start listening)." },
         { "Plugin", "Hosts a third-party Audio Unit effect. Drag one in from the Plugins panel (Rescan there indexes what is installed; the list is cached, so launching never rescans), or drop a .component bundle from Finder. \"open\" shows the plugin's own editor in a separate window. The sliders on the body are plugin parameters you chose to expose: turn \"configure\" on and touch a control in the plugin's own window and it appears here as a mapped row - or pick one from the dropdown, since not every plugin's editor tells the host what was touched. Each mapped row is a real param with its own modulation pin, so a Ramp or Envelope can drive it. Right-click a row to unmap it. With nothing loaded, or bypassed, audio passes through unchanged." },
         { "Oscillator", "A synth oscillator with four classic waveforms (sine, triangle, saw, square), interactive amp envelope, unison, filter, hard sync, and fine/coarse tuning. With no note cable connected, it free-runs at a set frequency; connect a note cable and it becomes polyphonic and envelope-gated." },
         { "Wavetable", "Two independent wavetable engines with unison, filter, and pitch/filter/amp envelopes, mixed by an A/B control. With no note cable connected, it free-runs at a set frequency; connect a note cable and it becomes polyphonic and envelope-gated." },
         { "Slicer", "Chops a sample into slices and maps them chromatically to the keyboard from MIDI note 36 upward - note 36 plays slice 1, 37 plays slice 2, and so on. A note past the last slice is silent; it does not wrap round to slice 1. Load a file (or drag one in from the Samples panel), or record from the audio input pin. slice by picks where the boundaries come from: onsets runs transient detection over the sample on a background thread, grid divides it arithmetically at the *global transport tempo* (there is no per-node bpm - change the tempo and the grid follows). sensitivity is the detection threshold and is the only control that re-runs the analysis; onsets just caps the result to the strongest N, and division/slice by recompute boundaries instantly. Click a slice band in the waveform to audition it, and in onsets mode drag any marker to move a boundary by hand - hand-edited markers are saved with the patch. Two separate controls decide how long a slice lasts: crossthrough sets whether playback may run PAST the slice's own next onset (off by default - each slice stops where the next begins), while decay shapes only the amplitude envelope, reading 'hold' at the top of its throw where the slice stays at full level. So: crossthrough off + hold is the classic tight chop; crossthrough off + a decay ends at whichever comes first; crossthrough on + hold plays through the rest of the sample; crossthrough on + a decay is a one-shot with a tail over the rest of the break. attack extends each slice's own fade-in from instant up to half a second." },
         { "Molder", "Analysis/genome resynthesis: decomposes a loaded or recorded sample into tracked harmonic partials plus a real residual waveform, then Roll mutates a parameter genome and re-renders a new sample from it - each roll walks further from the last, not from the original. Iterate feeds the last render back in as the new source and re-analyses it (progressively eating the sound); Reset returns fully to the originally loaded/recorded sample - generation 0 and the six shaping knobs (tone/air/snap/stretch/time/pitch) back to neutral, and the analysis itself restored, undoing any Iterate. chaos sets how far the next roll jumps; pitch offsets on top of the genome's own pitch walk; tone balances partials against residual; air/snap are the residual's steady-hiss and transient-attack levels; stretch scales inharmonicity together with harmonic spacing; time warps the attack/decay timing without changing the sample's length. This is a sound designer, not a playable instrument - it takes no note input, only a single self-triggered voice with start/end range, loop, reverse and ping-pong, the same transport as Sampler. Analysis and rendering both run on a background thread, so rolling never stalls the UI. seed/gen/f0/harm in the readout are the exact genome (seed + generation count) and the analysed pitch - two integers are enough to reproduce any rolled sound exactly on reload." },
         { "Grain Molder", "Slices audio into overlapping grains, calculates per-grain metrics (Level, Brightness, Random), and rearranges them based on a continuous blend between original temporal position and metric rank. At amount 0 it is the clean identity passthrough; at 1 it is fully sorted into a swell or brightness contour. Rendering runs asynchronously on a worker thread." },
         { "Analog", "A classic polyphonic virtual-analog synth voice with two analog-style oscillators (osc1 unison stack, osc2 tuning/detune/sync, sub osc one octave down, white noise, pre-filter drive stage, nonlinear ZDF Moog-ladder or SVF filter, and amplitude ADSR across up to 8 voices). Detune reads as the stack's true total width in cents, distributed unevenly across the stack the way separately mistuned circuits sit. Spread is stereo width: it splits the stack across two independent drive/filter chains and places each voice card in the image, so the sides carry different oscillators rather than one panned copy." },
         { "Equation Synth", "A synth defined by a live formula (y = f(x, a, b, c, d, t)) instead of a fixed waveform - knobs a-d feed the equation directly, so turning them reshapes the waveform itself rather than modulating a preset one." },
         { "Sampler", "A sample player: load a file (or drag one in from the Samples search panel), or record from the audio input pin. Click the waveform to audition from that point, or use the audition button - both preview this node on its own dedicated voice, independent of the transport and any note cable, and never cut off or get cut off by an incoming note. Drag the waveform's two edge handles to set the loop range (start/end). pitch/finetune are coarse/fine tuning, speed is a -2..2 varispeed control (negative plays backward), volume is the output level. loop/rev/p-p control what happens at the range edges: loop wraps or bounces (ping-pong) instead of stopping, reverse flips the base direction. With no note cable connected, it free-runs on the transport - starts the moment you hit space, stops when you stop it; connect a note cable and it becomes polyphonic instead, each note played back at the pitch offset from middle C. Spacebar always silences every voice this node is making." },
         { "Drum Sequencer", "An 8-lane, 8-step drum machine: 8 lane cards (waveform + transient/decay/pitch/fine tune/volume/pan) above an 8x8 step grid. Click a card's waveform to load its sample (a drag from the Samples panel or an OS file drop also work), or drag its edge handles to trim the playback range; x clears it, and the choke button cycles its choke group (0 = none - two lanes sharing a group cut each other off, the closed/open hi-hat case). In the grid, R randomises that lane's fill, M/S mute or solo it. Click a step to toggle it, drag vertically on a lit step to set its velocity, drag horizontally to paint a run of steps on/off. The bottom rows are pattern-wide: rate/steps/swing/output, then four offsets (transient/decay/pitch/pan) composed on top of every lane's own value. Plays the moment it's patched, phase-locked to the transport - there's no note input, just its own Transport-derived sequence. run stops this node's own step firing without touching the transport; randomise seeds a musical kick/snare/hat starting pattern." },
         { "MPC", "16 sample pads (pad 1 bottom-left). Each pad has one sample and a play mode: one shot (plays the whole sample; a new hit restarts it), gate (plays while held, stops on release, restarts on the next hit) or loop (a hit toggles looping on/off; off rewinds). Click a pad to play and select it; the editor below loads a sample or a whole folder (first 16 audio files), sets the mode, volume, pitch and pan. Every pad has a CV pin (gate: high = held) for MIDI controllers, and the note input plays pads from the base note up (36-51 by default). The output is the master mix; an MPC Out node picks one pad for its own chain." },
         { "MPC Out", "Takes one pad's own stereo output from an MPC wired into its input, so a pad (a kick, a snare) can get its own effects. Anything that is not an MPC passes straight through." },
         { "Clip Matrix", "Session view / clip launcher (like Ableton Live). Rows are tracks (one clip plays per row), columns are scenes. Drop audio, video or image files on the cells. Click a cell to launch it, the scene buttons on top launch a whole column, STOP stops a row. Launches are quantized on the audio thread to the next grid line (None, 1/16 to 4 bars; global or per clip). Modes: loop (default), once, gate (plays while held). Audio clips can sync to the tempo (stretched, pitch kept) and be pitched; follow actions chain clips; scenes can set the tempo and time signature. Every cell, scene and stop has a CV pin, and notes launch cells (base note + row * scenes + column). Outputs: master video (rows composited, row 1 in front) and master audio; a Clip Matrix Out splits the rows. REC > ARR records the performance into the Arrangement Timeline." },
         { "Clip Matrix Out", "Wire a Clip Matrix's audio output into it to get every row as its own video and audio output, for per-row effects and mixing." },
         { "Timeline", "The Arrangement Timeline's video as a node: the composite of every video track at the playhead (blend modes, opacity, fades, grade). Wire it to an Output to project the arrangement, or into filters like any image. width/height set its resolution. Open the timeline with Shift+T (VIEW > Arrangement timeline)." },
         { "Looper", "A live looper on one audio input. REC starts a take (with sync on, it waits for the next bar or sub-bar line of the transport), PLAY starts/stops the loop, DUB layers the input over the loop while it plays, CLEAR empties it. Length: a number of bars, a fraction of a bar, or free (REC again ends the take). Playback forward, reverse or ping-pong, looping or once. thru is the input monitoring level, level the loop volume. Every button has a CV pin. Takes are shifted by the interface round-trip latency (auto, plus a manual offset in ms) so they land on the grid. A REC pressed up to 200 ms after a line still starts on that line. UNDO / REDO step through the takes and overdub layers (UNDO during an overdub closes and removes it). The loop is saved with the patch (a WAV in the Recordings folder) and EXPORT WAV writes the mix of all layers. Up to 120 s at 48 kHz." },
         { "Super Mixer", "A 16-channel mixer: per channel an input gain (+/-24 dB), a fader, pan, mute, solo and a 3-band EQ (low shelf 120 Hz, sweepable mid peak, high shelf 8 kHz, each +/-15 dB), plus a master fader. Every control has a CV pin." },
         { "Audio In", "Captures the input device chosen in Settings > Audio (mic or line-in) as a live audio source for the effects graph - patch it into a Filter, Delay, Mixer or straight to Audio Out. The inputs menu picks a stereo pair, a single channel (mono) or a mix of every input, so several Audio In nodes can split one multichannel interface. Trim is a plain gain stage; capture starts the first time this node cooks, so it stays idle until it's actually in a patch." },
         { "Audio Filter", "One filter, one of 12 types (LP/HP at 12/24/36 dB, BP, notch, shelves, peak, all-pass). Drag the handle on the response curve to set frequency and gain, scroll over it to change Q - the picture is the control." },
         { "Audio Color Ramp", "Splits incoming audio into up to 8 frequency bands - drag the dividers right on the spectrum display to resize them - and assigns each one a colour, VIBGYOR by default from low to high. With no image patched in it outputs the resulting gradient standalone; patch one into its optional image input and it grades that image by luminance through the same audio-reactive palette instead." },
         { "EQ", "Five fixed bands (low shelf, three peaks, high shelf by default), each switchable to any of low shelf/peak/high shelf/hp 12/lp 12 and independently on or off. Drag a band's dot on the curve to set its frequency and gain, drag its diamond to set Q, double-click the dot to bypass that band - the knob row below always follows whichever band you last touched." },
         { "Dynamics", "A compressor: threshold, ratio, attack, release, makeup, a peak/RMS detector switch, and a sidechain switch that feeds the detector from the second input pin instead of the main signal. The graph shows the static transfer curve - input dB in, output dB out." },
         { "Delay", "A fractional delay line: time (tempo-synced by default, or free ms with 'sync to tempo' off), tone (bipolar tilt on the repeats), feedback (past 100% on purpose for self-oscillation - the output is soft-clipped, not the feedback itself), pan, duck (sidechains the wet signal off the dry input) and a bounce switch that cross-feeds left/right instead of repeating in place." },
         { "Reverb", "An 8-line feedback delay network: size (room size), decay (RT60 in seconds), damping (darkens and speeds up the tail), predelay (gap before the reverb starts), width (stereo spread, full at 1 down to mono at 0) and mix. Algorithmic only - no convolution engine." },
         { "Drive", "A tanh saturator: drive (0-40dB into the shaper), bias (asymmetry - the difference between fuzzy and warm), tone (bipolar tilt after the shaper) and output trim. A DC blocker always runs since bias otherwise leaks a constant offset." },
         { "Stereo", "Mid/side width control: width scales the side signal (0 = mono, 1 = unchanged, 2 = exaggerated), pan balances the result, bass mono folds everything below a frequency back to mono so low end survives a mono system. The bar shows live L/R correlation." },
         { "Pitch Shifter", "A two-tap delay-line pitch shifter: pitch in semitones, grain size in ms (shorter grains track fast material better but sound more granular). No formant correction - octave shifts will chipmunk/deepen the timbre along with the pitch." },
         { "Chorus", "2 or 3 detuned voices of one modulated delay line, LFO depth/rate, spread offsets the right channel's LFO phase for width." },
         { "Flanger", "One short modulated delay line with feedback - the jet-swoosh comb filter. Feedback past zero flips the comb's polarity for a hollower sound; the right channel reads a quarter-cycle ahead of the left for width." },
         { "Phaser", "A cascade of allpass stages (order, 2-8) sweeping around cutoff at rate/depth - each pair of stages adds one more notch to the comb. Spread offsets the right channel's sweep phase for width." },
         { "Bitcrush", "Sample-rate reduction (rate, zero-order hold) and bit-depth quantization (bits) - the two classic lo-fi digital degradations, independent of each other." },
         { "Transient Shaper", "Boosts or cuts a signal's transient hits (attack) separately from its sustained body (sustain), via a fast/slow envelope-difference detector - no threshold or ratio to set, unlike a compressor." },
         { "Stutter", "Records the first `grain` fraction of each tempo-synced (or free-ms) cycle, then loops that captured slice for the rest of the cycle before recording fresh audio next cycle - a beat-repeat glitch effect." },
         { "Ring Mod", "Multiplies the input by an oscillator (freq, waveform) - true ring modulation, not amplitude modulation, so the original frequencies are replaced by sum/difference sidebands rather than just scaled." },
         { "Frequency Shifter", "A true single-sideband frequency shifter: moves every partial by the same number of Hz rather than multiplying frequencies (Pitch Shifter) or producing sum/difference pairs (Ring Mod) - destroying harmonic relationships for classic metallic and inharmonic textures, or rising/falling barber-pole spirals with feedback." },
         { "Tremolo", "A unipolar gain envelope traced by a sub-audio LFO (rate/sync, shape, stereo phase, depth) - amplitude modulation, not ring modulation (see Ring Mod above): the original frequencies pass through unchanged, only the level rises and falls." },
         { "Formant Filter", "Three parallel bandpass resonators tuned to a vowel's formants (F1/F2/F3), morphed continuously A-E-I-O-U by the vowel knob - a vocal-tract-shaped filter for any input, not just voice." },
         { "Wavetable Shaper", "Uses a wavetable frame as a waveshaping transfer curve: the input sample's amplitude selects a phase into the table, the table's value there is the output. Drive past 0dB pushes the phase past the table's ends, where it wraps instead of clipping - true wavefolding, not distortion. Position morphs the curve across the table's 8 frames, smooth crosses to a lower-harmonic (darker) version of the same curve." },
         { "MIDI Notes", "Reads note events from a connected MIDI input device and outputs them as a note cable - the entry point for playing a synth or sampler from an external keyboard/controller." },
         { "Note Transpose", "Shifts every incoming note's pitch by a fixed number of semitones." },
         { "Pitch Bend", "A hand-driven bend wheel, patched inline in the note chain like Note Transpose. Unlike a transpose (which can only re-pitch a note as it attacks), moving this knob slides every note currently held through it in real time - drag it while a chord rings and the chord bends, no need to route it onto a synth's own bend knob. Default range is +/-2 semitones, the standard wheel range." },
         { "Velocity Curve", "Reshapes incoming note velocity through a response curve (soft/linear/hard, or a custom curve) - the note-chain equivalent of a keybed velocity curve setting." },
         { "Gate", "Overrides how long a note sounds: holdMs schedules the note-off that many ms after note-on instead of passing the real one through, for a fixed duration regardless of how long the key was held. 0 = passthrough (off)." },
         { "Humanizer", "Adds small random offsets to incoming note timing and velocity, so a mechanically even sequence sounds played by hand instead of quantized." },
         { "Quantizer", "Snaps incoming note-on timing forward to the nearest grid line, at a chosen note division." },
         { "Glide", "Plays a fast chromatic run between a note and the next one as an approximation of portamento (NoteEvent has no continuous pitch, so this is a glissando, not a true pitch ramp)." },
         { "Vibrato", "An LFO wired straight to pitch. It is a modulator, not a note-chain node - it has no note input on purpose, because a free-running wobble has no single note to attach to. Patch its output onto a synth's pitch/bend mod dot (e.g. Wavetable's 'bend' knob)." },
         { "Note Filter", "A gate on a note's pitch: scale snaps it to the nearest degree of the chosen scale/root, range drops anything outside lo..hi, and chance randomly drops the rest. A note that gets dropped has its note-off dropped with it, so nothing hangs." },
         { "Note Echo", "Repeats every incoming note event, delay ms apart, with velocity decaying and pitch shifting per repeat - a delay line for notes rather than audio. The original note always passes through first; the repeats are on top of it, not instead of it." },
         { "Predictive Notes", "Wire a note chain in and press Learn: it listens (passing the notes through), learns the pitches, rhythm, lengths and velocities as a variable-order Markov model, then plays on its own in that style. Stray at the bottom replays the phrase, the middle plays in character, the top ignores the model and picks freely in range. The learned notes are saved with the patch." },
         { "Predictive Quantize", "A groove quantizer, not a grid one: wire a note chain in and it always listens to the actual spacing between your onsets while also correcting them in the same pass - no Learn/Stop, it just keeps adapting the more you play. The learned spacing profile is global: shared by every Predictive Quantize in every patch, and it keeps only your most recent onsets, so it tracks how you're playing now rather than an ever-growing history. Mix at 0 is untouched, mix at 1 snaps fully onto the nearest learned spacing. Unlike Quantizer's fixed grid, this follows however you actually played it, including swing or a template that isn't on a clean subdivision." },
         { "Predictive Velocity", "A learned dynamics curve, not a hand-picked one: wire a note chain in and it always listens to the velocities you play while also remapping them in the same pass - no Learn/Stop, it just keeps adapting the more you play. The learned curve is global: shared by every Predictive Velocity in every patch, and it keeps only your most recent notes, so it tracks how you're playing now rather than an ever-growing history. Mix at 0 is untouched, mix at 1 snaps fully onto the learned range, so your loudest playing maps to your own real loudest instead of a theoretical 127. Unlike Velocity Curve's one fixed shape, this follows your own dynamics." },
         { "Predictive Rhythm", "Wire any note source in and press Learn, same as Predictive Notes. It learns the rhythm, note lengths and velocity as its own habit, but learns pitch as an interval from a 'root' note instead of an absolute pitch - so what comes back out reads as a repeating pattern anchored on one note with occasional deviation (think C2 C2 C2 D3 C2 C2 C2), not a free melody. Root auto-detects to the most common note you played when Learn finishes; change it afterward with the dropdown to transpose the whole learned pattern without relearning." },
         { "Note Router", "The system's only note fan-out point: one input, four distinct outputs. Round Robin cycles through them, Random picks one per note, Chain advances only when the pitch changes (a held note stays put), and Probability rolls each output independently - a note can end up on several outputs at once, or (rarely) none, in which case it falls back to output 1. A note's whole lifetime (on through off) always stays on the output(s) it started on." },
         { "Note Merge", "The system's only note fan-in point: up to four note inputs merged into one output stream, in timestamp order. Each input's notes stay independent voices matched by voice id, not pitch - so two inputs playing the same note at the same time sound as two overlapping voices, not a collision." },
         { "Arpeggiator", "Holds whatever notes are currently down and replays them one at a time on its own clock, either synced to tempo (a note division) or free-running in seconds. Up/Down/Up-Down/Down-Up/As Played order the held notes by pitch or by the order they were pressed; Converge alternates outside-in (lowest, highest, next-lowest...), Diverge alternates inside-out from the middle; Random picks one per step. Repeat x2/x4 fires each note 2 or 4 times in a row before advancing. Stairs Up/Down walks the pattern in overlapping two-note steps (C E, E G, G C...). Join and Spread only differ once octaves is above 1: Spread stacks the pattern octave-by-octave (C3 D3 E3, C4 D4 E4), Join interleaves each note's octaves together (C3 C4, D3 D4, E3 E4), and Join/Spread alternates between the two every full pass - at octaves = 1 all three play identically to Up. The 8-step gate grid below the readout is the primary control: click or drag across cells to mute individual steps without changing the note order (advancing past a muted step still moves the pattern forward, punching a rhythmic hole rather than skipping a note), and the lit cell tracks the currently-sounding step. Octaves stacks the pattern up to 4 octaves higher. Gate sets how much of each step the note actually sounds for before its off. Preset loads a complete starting point (mode, octaves, rate, gate and gate pattern) in one click." },
         { "Note Sequencer", "A self-playing step sequencer with 1 to 128 steps, edited in pages of 16. Use the arrow buttons to change page. Drag a bar's tall upper area to set pitch, drag the thin strip below it to set velocity, and click the strip to toggle the step. Rate can follow tempo or run in seconds; gate sets how much of each step the note sounds for." },
         { "Random Note Generator", "A generative source that free-runs on its own clock (synced to tempo or free-running seconds) rather than only reacting to a knob edit: each new note is the previous one plus a small random step (wander sets the max semitones), clamped to lo..hi and snapped to the chosen scale - a bounded random walk, not independent-per-step randomness, so the line wanders rather than jumps around." },
         { "Chorder", "A self-playing generative chord engine. Every groove step it picks a random scale degree and stacks chord-sized thirds on top of it, in key. Strum spaces each chord tone's onset apart instead of firing them all at once; humanise timing and velocity add per-note randomness on top; harmonics is the chance any given chord gets an extra note an octave above one of its tones." },
         { "Note Stack", "Layers transposed copies of every incoming note on top of the original - eight independent semitone voices, each switched on or off on its own. The dry note always sounds; the enabled voices are added to it, not instead of it. The set of voices is captured when a note starts, so switching one off mid-note never leaves it hanging." },
         { "Note Capturer", "A MIDI recorder and looper. Record captures whatever plays into it (tempo-relative, so it stays in sync if the BPM changes); Play loops that phrase back on top of the live input, which always passes through regardless of transport state. Loop toggles whether playback repeats or stops after one pass. Quantize snaps every recorded onset to the nearest grid line the moment recording stops. A topology rebuild elsewhere in the patch (any cable connect/disconnect) no longer clears a take - only Clear does. Like every other recorder in this app, the recording itself doesn't survive save/load - only loop/quantize do." },
         { "Bouncing Balls", "Balls bounce around inside a shape and emit a random note in lo..hi range every time one hits the wall, flashing orange briefly on the hit. Shape swaps the boundary between circle, square and triangle; size and speed are the balls' own; balls sets how many are simulated at once." },
         { "Scale Notes", "Quantizes every incoming note's pitch to the nearest degree of the chosen scale/root - nothing else about the note (timing, velocity) changes. A plain inline gate, distinct from Note Filter's range/chance gating." },

         // ---------------- Feedback ----------------
         { "Feedback", "Outputs the previous frame - a one-frame delay, not an effect. It only does something visible inside a loop: patch a downstream node's output back into a Feedback node's input, then use that Feedback output upstream, without the graph recursing. See Menu > Help > 'Using Feedback' for the full patch." },
         { "Trails", "A pre-wired feedback loop: decaying accumulation with drift, zoom, rotation and hue rotation. Reach for this before wiring a Feedback loop by hand." },
         { "Reaction Diffusion", "Gray-Scott chemical simulation, six presets. Needs no input; patch one in and its luminance varies the feed rate so the pattern grows differently through light and dark." },

         // ---------------- Mask ----------------
         { "Remove Background", "On Windows, U2Net runs asynchronously through Windows ML + DirectML/DX12 with an OpenCV CPU fallback. On macOS it uses Apple Vision. Use auto refresh and mask fps for moving video." },

         // ---------------- Resynth ----------------
         { "Resynthesize", "Each generation reads the previous one, so the image drifts away from the source. The XY pad blends four named mutation effects assigned to its corners - drag the orb to mix them. Randomise re-rolls which four effects sit in the corners, and the orb's path can be recorded, looped and replayed in time." },
         { "Resynthesize 3D", "The mesh equivalent of Resynthesize: each generation applies a weighted mix of geometry operators to the previous one, drifting the shape over time. 'step' advances one generation manually, 'auto step' runs it continuously, 'randomise' re-rolls the seed." },

         // ---------------- Output ----------------
         { "Output", "Terminal node. Shows the final image, exports a PNG, and records an H.264 .mov at a chosen frame rate. Recording captures the cooked output, so what you see is what is written." },
         { "Spout Out", "Broadcasts video, 3D renders, or visual shaders to other Windows apps through Spout2 (zero-copy GPU texture sharing: Resolume, TouchDesigner, OBS, MadMapper...). Patches that used the old Syphon Out name still open." },
         { "Projection", "Warp, corner-pin and perspective-correct an image for projectors, flat walls, or curved screens, with built-in alignment test patterns and custom resolution target. Outside the warped image the output is transparent (alpha), the outline is antialiased, and each edge can have its own edge blend (width, curve, gamma) that fades the alpha or darkens RGB, for overlapping projectors." },

         // ---------------- OSC ----------------
         { "OSC Receive", "Listens on a UDP port for Open Sound Control messages matching an address pattern, and reports the last received value as a modulator (remapped through low/high). Behaves like LFO/Random - patch its output onto any slider's modulation pin." },
         { "OSC Send", "Sends its patched modulator input as an Open Sound Control message (address + float) to a host:port over UDP, on change (past an epsilon) or at least every interval - the one node in the patch with no output of its own." },
         { "OSC to CV", "Up to 8 OSC addresses (or 8 arguments of one address) as 8 modulator outputs, each with its own input range, invert and smoothing. Press learn on a channel and move the control on your phone/controller to assign it. Listens on all network interfaces; several OSC nodes can share one port." },

         // ---------------- 3D / geometry pipeline ----------------
         { "Geometry", "The base 3D primitive node - pick any of its 24 shapes from the dropdown. Each shape also has its own directly-spawnable named node (Cube, Sphere, Torus, ...) that just starts on that shape." },
         { "Text 3D", "Extrudes text into a 3D mesh, with depth, bevel and letter tracking, using any font installed on the system." },
         { "Ocean", "A simulated ocean surface mesh - Gerstner-style waves with amplitude, wavelength, steepness, choppiness, direction and octaves for layered swell." },
         { "Material", "Applies shading, colour, metallic/roughness/opacity and emission to a geometry input, plus an optional normal map input (its strength slider only appears once something is patched into it)." },
         { "Displacement", "Displaces a geometry's vertices along their normals using a texture's luminance. Needs both a geometry input and a texture input to do anything." },
         { "Mapping", "Transforms the UV or 3D texture coordinates used by materials/textures downstream - translate, rotate and scale, independent of moving the geometry itself." },
         { "Particle System", "A GPU particle emitter - shape, rate, lifetime, launch speed/spread/direction, gravity/drag/turbulence, and start/end size and colour over each particle's life." },
         { "Cloth", "A cloth simulation over an input mesh - stiffness, iterations, damping, mass and shape retention, plus gravity, wind and optional ground collision. 'Reset' re-drops the cloth from its rest pose." },
         { "Join Geometry", "Combines two or more geometry inputs into one. The boolean modes (Union, Difference, Intersection, etc, each also spawnable as its own named node) need closed, manifold solids to produce a clean result - open surfaces can give garbage." },
         { "Metaballs", "Builds an isosurface (blobby, merging spheres) from a point cloud source, or from a manually-placed set of balls when no cloud is patched in." },
         { "Image to Points", "Converts an image into a 3D point cloud - brightness/depth-source drives per-point depth, with density, threshold, point size and optional colour-from-image." },
         { "Set Vertex Color", "Writes per-element colour - vertex colours on a mesh, per-particle colour on a point cloud, whichever the input carries. source picks where the colour comes from: flat, position, normal, index, random, a patched Palette node's swatches, or a patched texture sampled at each element's UV. Render 3D reads whatever ended up there. (Called Set Color before 0.41; old patches load it under the new name.)" },
         { "Depth Projection", "Unprojects a 2D depth map and optional color image into a 3D point cloud or triangulated surface mesh, supporting pinhole camera, planar, radial and cylindrical projections with depth curves and edge tearing." },
         { "Curve", "A generative parametric curve/tube (line, circle, spiral, helix and other presets) extruded into a mesh, with point count, smoothness, spread, height, twist and tube radius/taper controls." },
         { "Mesh to Points", "Samples the input mesh's vertices as a point cloud, for feeding Instance on Points or Metaballs." },
         { "Mesh to Edges", "Samples points along the input mesh's edges as a point cloud, for feeding Instance on Points or Metaballs." },
         { "Mesh to Faces", "Samples each face's centre point of the input mesh as a point cloud, for feeding Instance on Points or Metaballs." },
         { "Instance on Points", "Stamps a shape at every point of a point source, drawn in one instanced call. Input A is the points source (Faces/Edges/Points sampler, a point cloud, etc.), input B is the shape to stamp at each point - both are required before it draws anything." },
         { "Wrap", "Bends or conforms the source input onto the target input. Cylindrical rolls it around one axis at the target's radius with no distortion at all - letterforms, spacing and extrusion depth survive intact, which is what curves 3D text around a sphere or cylinder. Spherical adds the same bend the other way so long text also curves over the poles. Nearest Surface snaps every vertex to the closest point on the target instead: right for conforming a dense mesh to irregular geometry, but it squashes flat text. With a target connected the bend radius follows the target's size, so scaling the target moves the source with it - radius scale tunes it as a multiplier. With no target, radius sets the bend outright." },
         { "Camera", "A 3D viewpoint. Patch it into a Render 3D node's camera input to render from it instead of the default view." },
         { "Light", "A light source for the 3D scene. Render 3D takes up to 3 lights - patch more in and only the first 3 are used." },
         { "HDRI", "Loads an equirectangular .hdr or .exr image and patches into Render 3D's env input, replacing the fixed sky gradient with a real image for the background and for reflections/ambient light. Rotation turns the image around Y; intensity scales it independently of Render 3D's own env intensity. Reflections use the image's own mip chain scaled by roughness as a cheap stand-in for a proper blurred prefilter - very glossy metal will read a little softer than a full IBL renderer would give it. Use this node rather than Image Source for HDRIs - Image Source clamps to 8-bit sRGB, which throws away exactly the above-1.0 highlight range an HDRI needs." },
         { "Render 3D", "Rasterizes the geometry/camera/light/material graph into an image. Antialiasing is reduced automatically at large output sizes to stay within GPU limits. Scenes over ~2 million triangles get noticeably heavier to render. An HDRI node patched into the env input replaces the procedural sky gradient for background, reflections and ambient light." },
         { "Model 3D", "Loads a 3D model file - obj, ply, stl, usd or usdz." },
         { "Null 3D", "A pass-through node for geometry: its output is exactly its input mesh, unchanged. Useful as a stable junction point to branch geometry to several destinations." },
         { "Group 3D", "Carries up to eight geometry inputs through one pin into Render 3D (or into another Group 3D), so a scene is not limited to Render 3D's four geometry pins. Nothing is merged: each input keeps its own material, texture, mapping and transform. It only means something to Render 3D - use Join Geometry to combine meshes into one." },
         { "Delaunay Mesh", "Point cloud in (a mesh is flagged red and ignored; use Mesh to Points first), triangulated surface out. Use it to turn a scatter into a terrain or low-poly surface. Points are projected onto the chosen plane, joined into Delaunay triangles (no sliver-prone long edges), and lifted back to their real 3D positions, so a scatter with height becomes a terrain. 'max points' thins very large clouds." },
         { "Voronoi Cells", "Point cloud in (a mesh is flagged red and ignored), one polygon cell per point out, for cracked-glass, cell and fracture looks: every cell is the region closer to its point than to any other. Points on the outer hull have unbounded cells and are skipped. 'inset' pulls each cell toward its point to leave gaps between them." },
         { "Curve Ops", "Curve in, curve out. Resample spaces points evenly along the length, Simplify drops points that barely change the shape (within 'tolerance'), Smooth relaxes corners while the ends stay put, Offset draws a parallel curve at a signed distance in the XY plane with round outer corners. The result can still drive a Path and shows as a thin tube." },
         { "Switcher 3D", "Cycles between up to four connected geometry inputs every N beats or seconds, forwarding whichever one is active. Can be pinned to one input with 'manual'. Unlike the 2D Switcher, there is no crossfade - it always hard-cuts, since interpolating between two arbitrary meshes' topology isn't generally well-defined." },

         // ---------------- Join Geometry boolean modes (also spawnable directly) ----------------
         { "Union", "Boolean union: fuses two or more closed, manifold solids into one, keeping their combined outer surface." },
         { "Intersect", "Boolean intersection: keeps only the volume where all the closed, manifold input solids overlap." },
         { "Difference", "Boolean difference: subtracts every input after the first from the first, like a cookie cutter. Needs closed, manifold input." },

         // ---------------- GeometryOp operators ----------------
         { "Transform", "Moves, rotates and scales the input mesh by a fixed offset - the geometry equivalent of the 2D Transform effect." },
         { "Array", "Duplicates the input mesh in a line or radial ring. Count sets how many copies; radial mode spaces them by Radius, linear mode by Offset X/Y/Z; each copy can additionally rotate/scale a step further than the last." },
         { "Subdivide", "Subdivision surface. Each level roughly quadruples the triangle count, so higher levels get expensive fast; Smooth controls how much it rounds corners." },
         { "Solidify", "Gives a flat or open surface real Thickness, turning it into a solid shell. 'keep original' also retains the source surface." },
         { "Extrude", "Extrudes every face of the input mesh along its own normal by Distance, with Inset shrinking each face first." },
         { "Wireframe", "Replaces solid faces with a wireframe tube running along each edge, at a chosen Thickness." },
         { "Triangulate", "Re-triangulates the mesh; Jitter randomizes how each face is split, for a more organic, less uniform look." },
         { "Normals", "Recomputes vertex normals - 'flat shade' gives hard per-face edges instead of smooth shading, 'flip' inverts which way every face points." },
         { "Explode", "Pushes each face outward from the mesh's centre along its own normal, by Amount, randomized per-face by Seed." },
         { "Twist", "Twists the mesh around a chosen Axis by Angle - the further a point is along that axis, the more it rotates." },
         { "Smooth", "Iteratively averages each vertex toward its neighbours (a Laplacian smooth) - Iterations and Strength control how much it relaxes. On a low-poly mesh like a Cube there's no extra geometry between the corners to round out, so raise Pre-subdivide first to see rounded edges." },
         { "Mirror", "Mirrors the mesh across a plane perpendicular to a chosen Axis, at a given plane offset. 'keep original' keeps the source geometry too, 'weld seam' merges the two halves where they meet." },
         { "Screw", "Sweeps the input profile around an Axis in a helical path - Steps sets resolution, Turns the number of revolutions, Rise/turn the pitch, Radius offsets the sweep outward." },
         { "Decimate", "Reduces triangle count while keeping the overall shape (quadric error edge collapse). 'keep' is the fraction of triangles to leave; 'lock border' holds open edges in place. UVs, colours and hard shading survive; very low settings stop when no safe collapse remains." },
         { "Select", "Marks a subset of faces - by index range, position along an axis, normal direction, random chance, or within a radius - for downstream ops (Delete Selected / Transform Selected / Extrude Selected) to act on. Invert flips the selection, 'add to selection' unions with whatever was already selected." },
         { "Delete Selected", "Deletes the faces marked by an upstream Select op - or, with 'keep selected instead', deletes everything else and keeps only the selection." },
         { "Transform Selected", "Moves, rotates and scales only the faces marked by an upstream Select op, optionally sliding them along their own normals instead of a fixed direction." },
         { "Extrude Selected", "Extrudes only the faces marked by an upstream Select op along their own normals, by Distance, with Inset." },
      };
      auto it = kText.find(typeName);
      return it != kText.end() ? it->second : nullptr;
   }

   // Every filter-table effect - see core/FilterDefs.cpp for the actual shader
   // each of these names drives. Keyed by the raw (lowercase, spaceless-ish)
   // FilterDef::name, which is what GraphNode::typeName actually holds for these.
   const char* FilterHelpText(const std::string& typeName)
   {
      static const std::unordered_map<std::string, const char*> kText = {
         { "gaussianblur", "Blurs the image with a Gaussian-weighted kernel - a smooth, natural blur. Radius sets how far it samples." },
         { "boxblur", "Blurs by averaging a flat square of neighbouring pixels - cheaper and blockier than Gaussian Blur. Radius sets the box size." },
         { "motionblur", "Smears the image along a straight line, like camera or subject motion. Angle sets the direction, Distance how far it smears." },
         { "radialblur", "Blurs outward from a centre point, like a zoom or spin blur. Amount sets the strength, Center X/Y the origin." },
         { "unsharpmask", "Unsharp mask: blurs a copy of the image and adds back the difference, exaggerating edges. Amount is the strength, Radius how wide an edge it reacts to." },
         { "twirl", "Rotates pixels increasingly the closer they are to a centre point, like a whirlpool. Angle is the twist amount, Radius how far it reaches." },
         { "pinchpunch", "Pulls pixels toward, or pushes them away from, a centre point. Amount above 1 punches outward, below 1 pinches inward." },
         { "ripple", "Displaces pixels outward from a centre in concentric waves. Amplitude is wave height, Wavelength the spacing, Phase animates it." },
         { "pixelate", "Chunks the image into flat colour blocks. Block Size sets how large each block is." },
         { "addnoise", "Adds random per-pixel grain, re-randomised every frame. Amount sets how strong it is." },
         { "vignette", "Darkens the image toward the edges, framing the centre. Radius and Softness shape the falloff, Center X/Y offsets it." },
         { "transform", "Translates, scales, rotates, and flips (horizontal/vertical) the whole image. Scale X/Y let you stretch non-uniformly on top of the overall Scale. Pivot moves the point it scales and rotates around, order picks which of scale, rotate and translate happens first, and common > extend chooses what fills the frame past the image edge: nothing (default), the edge pixels held, the image repeated or mirrored (Limit Tiles caps how many copies). Comp Over Background fills the transparent part with Background." },
         { "brightnesscontrast", "Basic exposure control: Brightness adds or subtracts a flat amount, Contrast steepens or flattens the curve around mid-grey." },
         { "levels", "Remaps the input range: Black Point and White Point set what maps to pure black/white, Gamma curves the midtones." },
         { "hsl", "Shifts Hue, scales Saturation and adds Lightness, independent of the underlying colour." },
         { "invert", "Inverts every colour channel (alpha untouched) - a photographic negative. No parameters." },
         { "posterize", "Reduces the image to a fixed number of tonal Levels per channel, producing flat colour bands." },
         { "threshold", "Converts to pure black or white based on luminance, split at Threshold." },
         { "colorbalance", "Shifts colour along three axes - Cyan-Red, Magenta-Green, Yellow-Blue - without touching overall brightness." },
         { "exposure", "Multiplies brightness by powers of two, like a camera's exposure stop (compare Levels' linear/gamma remap)." },
         { "bloom", "Isolates pixels above a brightness Threshold, blurs them outward by Radius, and adds the glow back at Intensity - classic HDR-style bloom." },
         { "diffuseglow", "Screens a blurred copy of the whole image back over itself, glowing everything rather than just bright spots (compare Bloom, which is threshold-based)." },
         { "glitch", "Ten glitch algorithms behind one 'kind' dropdown: Slice Shift (RGB-split rows), RGB Shift, Scanlines, Blocks (jittered tiles), Wave, Datamosh (shifted slices with a streak smear), Scan Jitter (per-line exponential offsets and tear bands), VHS (drifting tracking band, chroma bleed, head-switch noise, tape noise), Compression (8/16 px macroblocks, chroma subsampling, per-block posterize, stale blocks; Detail below 0.5 = 8 px, above = 16 px) and Pixel Sort (single-pass approximation along bright spans; Detail = threshold, Amount = span length). Amount/Detail control strength/scale, Speed animates it, Seed reseeds the randomness. Extra controls (all off by default, so older patches look the same): Size Var (uneven slice/block sizes from a random split) and Splits (split depth), Contrast (more small offsets, a few big ones), Density (share of slices that move), Stagger (each slice on its own step phase), Burst (share of clean 4-step blocks), Decay (offsets settle within a block), Sync (steps locked to the beat, 1 bar to 1/32; Free uses Speed; 1 bar assumes 4/4), RGB Var (per-slice channel split), Color FX (per-slice channel swap, invert or posterize), Axis (Horizontal, Vertical, Both) and Clock (Transport follows play/pause, Free-run keeps moving while stopped)." },
         { "Datamosh", "Codec-style datamosh. Blocks that are held keep sampling the previous output displaced by a motion vector, estimated from the image (or the optional second input, the motion source) between frames. Mosh = share of held blocks, Gain scales the vectors, Bloom keeps accumulating the last vector so smears grow, Threshold ignores small motion, Block (8/16/32) and Block Var (uneven block sizes), Leak lets some mosh into refreshed blocks. Refresh (off, bar, beat, 1/2, 1/4, 1/8 of a beat) or the Refresh button (mappable) copies the live image back into a share of the blocks (Refresh Chance), like an I-frame. Seed picks which blocks are held. Renders every frame." },
         { "lensdistortion", "Simulates a camera lens: Barrel bows the image in or out, Chromatic separates the colour channels' distortion for fringing, Zoom scales the result." },
         { "displace", "Offsets each pixel using a second patched-in image as a displacement map (red moves horizontally, green vertically, or any channel you pick), or a built-in animated wave if nothing is patched. Map Scale tiles the map, Midpoint is the map value that means no movement, Amount X/Y scale each direction. common > extend sets what is pulled in from past the edge." },
         { "liquify", "Flows pixels around using animated Perlin-style noise, like a liquid warp. Scale sets the flow's feature size, Speed animates it." },
         { "symmetry", "Mirrors the image about the X axis, Y axis, or both, from a chosen centre point. Flip additionally reverses the whole image before mirroring." },
         { "kaleidoscope", "Slices the image into angular Segments around a centre and mirrors each one, kaleidoscope-style. Rotation spins the pattern, Zoom scales it." },
         { "mirror tile", "Tiles the image and alternately mirrors each tile so the edges line up seamlessly, unlike a plain repeat. Tiles sets how many repeats." },
         { "chroma key", "Removes a chosen Key Colour (green/blue-screen style), matched in a brightness-independent colour space. Tolerance/Softness shape the edge, Spill Removal desaturates colour-cast fringes, 'show: Matte' previews the alpha instead of the keyed result." },
         { "luma key", "Keys out a brightness range (Low-High) instead of a colour, with Softness at the edges and Invert to flip which range is kept. 'show: Matte' previews the alpha." },
         { "crop", "Crops in from each edge (Left/Right/Top/Bottom). With outside = Cut (the default) the cropped part is discarded and the image gets smaller, like TouchDesigner's Crop TOP. The other modes keep the input size: the cropped-away area stays transparent, gets a Fill colour, or the kept region zooms back up to fill the frame." },
         { "emboss", "Turns edges into a relief/bump look by differencing the image against itself offset along Angle/Distance. 'style: Over colour' keeps the original colours and adds the emboss on top; 'Grey' replaces the image with a flat emboss." },
         { "normal map", "Derives a tangent-space normal map from the image's luminance treated as a height field, for use as a bump/normal input elsewhere. 'output' can show the raw Normals, the source Height, or the gradient's Slope instead." },
         { "convolve", "A user-editable 3x3 convolution kernel (k11-k33) - blur, sharpen, edge-detect and emboss are all just different numbers in this grid. Divisor and Bias adjust the result, Spread scales the sample distance, Mix blends with the original." },
         { "lookup", "Recolors using a second patched-in image as a 1D palette strip - this pixel's Luminance (or a chosen channel) indexes across it. Offset shifts the index, Mix blends with the original." },
         { "halftone", "Renders the image as a dot pattern like offset printing. Scale sets dot density, Angle rotates the dot grid, 'style: Colour' uses three angled CMY-style dot layers instead of one greyscale layer." },
         { "edge sobel", "Classic Sobel edge detection: outputs the gradient magnitude as greyscale edges. Invert flips black and white." },
         { "edge outline", "Detects edges via a luminance gradient and draws them as flat-Colour outlines at a chosen Thickness and Threshold, over the original image." },
         { "lut", "Grades the image through a second patched-in HALD/strip LUT image - an N x N-sliced colour cube encoded as a flat strip. LUT Size must match the LUT image's slice count, Mix blends with the original." },
         { "gradientmap", "Remaps luminance onto a two-colour gradient (Shadow to Highlight), Photoshop Gradient Map-style. Mix blends with the original colour." },
         { "channelmixer", "Rebuilds each output channel as a weighted mix of the input's Red/Green/Blue - e.g. set 'Red from RGB' to swap or blend channels, or set all three rows equal for a weighted greyscale." },
         { "color adjustments", "All-in-one grading chain: Brightness/Contrast, Levels, Colour Balance, HSL, Vibrance, Tone Shaper (lift/gamma/gain-style S-curve), Channel Mixer, then an optional Black & White stage - so a common grade doesn't need eight separate nodes wired in series." },
         { "outerglow", "Adds a soft glow of a chosen Colour around the image's alpha edge, blurred outward. Amount controls strength." },
         { "coloroverlay", "Flat-tints the image toward a chosen Colour at a given Opacity." },
         { "dropshadow", "Offsets a blurred copy of the image's alpha behind it as a shadow, in a chosen Colour, Offset X/Y and Opacity." },
         { "show alpha", "Shows the alpha channel as a greyscale image, fully opaque - white is solid, black is transparent. A viewer, not an edit: it replaces the colour so you can see the matte a key produced, and is usually the first thing to reach for when a composite is going wrong." },
         { "opacity", "Scales the whole image's existing alpha by Opacity - a uniform fade that keeps the colour untouched. Unlike Color Overlay, nothing is tinted; unlike a Blend's opacity, the transparency is baked into the image and travels with it down the chain." },
         { "set alpha", "Replaces the alpha channel outright with the luminance of a second patched-in image - hand-drawn mattes, a Shape, a Ramp, or another branch's 'show alpha' output. With nothing patched into the second input the image is forced fully opaque instead." },
         { "alpha invert", "Swaps transparent for solid and back (alpha = 1 - alpha), leaving the colour alone - the 'invert matte' switch a key would otherwise need a second node to get." },
         { "alpha from luma", "Derives a new alpha channel from the image's own brightness, so a black background becomes transparent with nothing to key against. 'invert' flips which end is solid, for a white-on-black source." },
         { "alpha levels", "The Levels remap applied to the alpha channel only: Low/High clamp and normalise the matte's range (choking a soft edge tighter or spreading it wider) and Gamma bends the falloff between them. The go-to cleanup pass after Chroma Key or Alpha from Luma." },
         { "premultiply", "Converts between premultiplied alpha (colour already scaled by its own alpha) and straight alpha. Only reach for this when something looks wrong at the edges: dark fringing on a composite usually means straight alpha needs premultiplying, bright halos the reverse." },
      };
      auto it = kText.find(typeName);
      return it != kText.end() ? it->second : nullptr;
   }

   // The twenty 2D vector primitives spawned from ShapeNode::ShapeNames() (see
   // nodes/ShapeNode.cpp) - all one shader/class, told apart only by this text
   // since their on-node params are the shared fill/stroke/feather/background set.
   const char* ShapeHelpText(const std::string& typeName)
   {
      static const std::unordered_map<std::string, const char*> kText = {
         { "Circle", "A filled/stroked circle - one of the Shape vector primitives, with size, feather, stroke and background controls." },
         { "Ellipse", "A filled/stroked ellipse - Aspect stretches it from a circle." },
         { "Rectangle", "A filled/stroked rectangle, with an optional corner radius." },
         { "Rounded Rect", "A rectangle with rounded corners - like Rectangle but with the corner radius exposed as its own control." },
         { "Triangle", "A filled/stroked equilateral-style triangle." },
         { "Polygon", "A regular polygon - Sides sets how many." },
         { "Star", "A star with adjustable point count (Sides) and Inner Ratio for how deep the points cut in." },
         { "Ring", "A donut / annulus - a circle with a hole; corner/thick controls the ring's thickness." },
         { "Cross", "A plus-sign / cross shape." },
         { "Line", "A straight stroked line, without a fill interior." },
         { "Hexagon", "A six-sided regular polygon (a Polygon preset with Sides fixed to 6)." },
         { "Heart", "A heart-shaped outline." },
         { "Arrow", "An arrow shape - direction follows the Rotation control." },
         { "Crescent", "A crescent-moon shape - two overlapping circles subtracted from each other." },
         { "Gear", "A cogwheel silhouette - Sides sets tooth count." },
         { "Superellipse", "A 'squircle' - a shape between a rectangle and an ellipse, tunable via Inner Ratio." },
         { "Pie", "A circular wedge / pie-slice - Inner Ratio controls how much of the circle is cut away." },
         { "Teardrop", "A teardrop / droplet outline." },
         { "Chevron", "An angled bracket / chevron shape." },
         { "Blob", "An organic, wobbly rounded shape - randomised per Seed-like controls rather than a precise geometric primitive." },
      };
      auto it = kText.find(typeName);
      return it != kText.end() ? it->second : nullptr;
   }

   // The twenty-four 3D primitives spawned from GeometryNode::ShapeNames() (see
   // nodes/Geometry3DNodes.cpp) - one class/shader, told apart by this text since
   // their on-node params are the shared sides/tube/material set from DrawGeometryParams.
   const char* Geometry3DHelpText(const std::string& typeName)
   {
      static const std::unordered_map<std::string, const char*> kText = {
         { "Plane", "A flat subdivided plane - one of the Geometry 3D primitives." },
         { "Cube", "A subdivided cube/box." },
         { "Sphere", "A UV sphere - latitude/longitude subdivision." },
         { "Icosphere", "A sphere built from subdivided triangles (an icosahedron refined outward) - more even triangle distribution than a UV Sphere." },
         { "Torus", "A ring/donut solid - 'tube' sets the tube radius relative to the overall ring." },
         { "Cylinder", "A cylinder - Sides sets how many-sided (low values give a prism)." },
         { "Cone", "A cone - Sides sets how many-sided." },
         { "Torus Knot", "A knotted torus - a tube swept along a (p,q) knot path rather than a plain circle." },
         { "Capsule", "A cylinder with hemispherical caps - 'tube' sets the cap/body radius." },
         { "Tube", "A hollow cylinder - 'tube' sets the wall's inner radius." },
         { "Pyramid", "A pyramid - Sides sets the base's side count (4 = classic square pyramid)." },
         { "Prism", "A prism - an extruded polygon with Sides sides." },
         { "Helix", "A helical tube spiraling around an axis - 'tube' sets the tube's radius." },
         { "Supershape", "A superformula-based shape - 'depth', 'tooth depth' and 'hub hole' warp it into gear-like or organic silhouettes." },
         { "Tetrahedron", "A 4-faced platonic solid." },
         { "Octahedron", "An 8-faced platonic solid." },
         { "Dodecahedron", "A 12-faced platonic solid." },
         { "Rounded Cube", "A cube with rounded edges/corners - 'corner radius' sets how rounded." },
         { "Mobius Strip", "A one-sided, one-edge twisted strip - 'width' sets the strip's width." },
         { "Klein Bottle", "A non-orientable closed surface - the classic self-intersecting 'bottle with no inside or outside'." },
         { "Gear 3D", "A 3D cogwheel - 'depth' sets tooth depth, 'tooth depth'/'hub hole' shape the teeth and centre bore. Named with a '3D' suffix because the 2D Shape node already has its own 'Gear'." },
         { "Star 3D", "A 3D extruded star - 'depth' sets extrusion depth, 'inner ratio' how deep the points cut in. Named with a '3D' suffix because the 2D Shape node already has its own 'Star'." },
         { "Disc", "A flat filled circle/disc mesh (as opposed to Cylinder, which has height)." },
         { "Arrow 3D", "A 3D arrow mesh - shaft plus arrowhead. Named with a '3D' suffix because the 2D Shape node already has its own 'Arrow'." },
      };
      auto it = kText.find(typeName);
      return it != kText.end() ? it->second : nullptr;
   }

   const char* CategoryHelpText(const std::string& category)
   {
      if (category == "Source") return "A source node - generates or loads an image with no image input of its own.";
      if (category == "Text") return "Renders text as an image.";
      if (category == "Effects") return "Transforms one image into another. Its parameters (and the shape of its name) describe what it changes; see Menu > Help > Module reference for the effect families.";
      if (category == "Color") return "Adjusts the colour of its input. See Menu > Help > Module reference, 'Color' section, for the full family.";
      if (category == "Compositing") return "Combines images, or otherwise manages how they flow through the patch.";
      if (category == "Modulators") return "Produces a changing number over time, not an image. Patch its output onto the small dot beside any slider to drive that parameter.";
      if (category == "Feedback") return "Reads back a previous frame to build loops - trails, echoes, growth. See Menu > Help > 'Using Feedback'.";
      if (category == "Mask") return "Produces a mask, or isolates part of the image by colour or luminance.";
      if (category == "Resynth") return "Iteratively regenerates the image, each generation drifting from its source.";
      if (category == "PointZ") return "Point operators: geometry as points with named attributes (P, N, Color, PScale...), processed on the GPU. Any 3D source plugs in; Render 3D draws a PointZ straight from the GPU, Instance PointZ draws a shape at every point. Loops (Particle, Feedback) end in a tagged Null PointZ.";
      if (category == "3D") return "Changes geometry: booleans, extrude, subdivide, scatter and instance points, transform. Feed it from a 3D source, end in Render 3D.";
      if (category == "3DSource") return "Makes geometry: primitives, models, 3D text, metaballs, ocean, curves, particles, points from images or depth.";
      if (category == "3DScene") return "The scene around the geometry: camera, lights, HDRI, materials and UV mapping, and Render 3D, which turns it all into an image.";
      if (category == "Shapes") return "A 2D shape as an image, one node per shape (all are the Shape node with its shape preset).";
      if (category == "NoteSource") return "Makes notes: the virtual keyboard, MIDI input and files, chord progression, sequencers and generators. Feed a synth, or a note tool first.";
      if (category == "Notes") return "Part of the note chain - takes note events in on its 'notes' pin and passes them out, changed. Feed a synth (Wavetable, Sampler) from the end of the chain.";
      if (category == "Output") return "Terminal node: shows, exports or records the final result.";
      if (category == "Video") return "Video clips, cameras and Spout senders as images: Video, VMPC, Clip Matrix, Timeline, Video In, Spout In.";
      if (category == "Utility") return "Patch housekeeping: comments, groups, pass-through Null and Viewport, and wireless Send / Receive for every cable kind.";
      if (category == "AudioIO") return "Audio into and out of the graph: the sound card input and output, and audio files.";
      if (category == "AudioVisual") return "Turns audio into images or geometry: waveform/spectrum textures, colour ramps, displacement, ribbons.";
      if (category == "AudioUtility") return "Mixing and routing audio: gain, mixers, splitter, blend, looper, MPC pad outputs.";
      if (category == "AudioEffects") return "Processes audio: filters, dynamics, delays, reverbs, modulation effects, and VST3 plugins.";
      if (category == "Synths") return "Makes sound from notes: synthesizers, samplers, drum sequencer and the MPC.";
      if (category == "Prediction") return "Learns how you play and plays back or corrects in that style (ported from upstream Infinite).";
      if (category == "CVTools") return "Shapes modulation signals: math, compare, invert, range, smoothing, depth.";
      if (category == "Analysis") return "Measures audio, images or notes and turns the result into modulation values.";
      if (category == "Control") return "External control: MIDI CC/trigger, OSC in and out, and the transport. Patch their outputs onto any slider's dot.";
      if (category == "OSC") return "Sends or receives Open Sound Control messages over UDP to talk to other apps (TouchDesigner, Max, TouchOSC, lighting rigs), on this machine or the LAN.";
      return "No additional notes for this node.";
   }

   const char* NodeHelpText(const GraphNode& gn)
   {
      if (const char* specific = SpecificNodeHelpText(gn.typeName))
         return specific;
      if (const char* filter = FilterHelpText(gn.typeName))
         return filter;
      if (const char* shape = ShapeHelpText(gn.typeName))
         return shape;
      if (const char* geo3d = Geometry3DHelpText(gn.typeName))
         return geo3d;
      // PointZ family: each operator carries its own help in the table.
      if (const pop::PopDef* def = pop::FindDef(gn.typeName))
         return def->help.c_str();
      return CategoryHelpText(gn.category);
   }

   void DrawHelpWindow(bool* open)
   {
      ImGui::SetNextWindowSize(ImVec2(720, 620), ImGuiCond_FirstUseEver);
      if (!ImGui::Begin("Infinite - help & module reference", open))
      {
         ImGui::End();
         return;
      }

      if (ImGui::CollapsingHeader("Getting started", ImGuiTreeNodeFlags_DefaultOpen))
      {
         ImGui::TextWrapped(
            "Infinite is a node graph. Every node renders an image and passes it "
            "down a cable to the next one. A typical patch reads left to right:");
         ImGui::Bullet(); ImGui::TextWrapped("Source (image, video, shape, noise, formula) makes a picture.");
         ImGui::Bullet(); ImGui::TextWrapped("Effects and Color nodes change it.");
         ImGui::Bullet(); ImGui::TextWrapped("Compositing nodes combine several pictures into one.");
         ImGui::Bullet(); ImGui::TextWrapped("Output shows the result, exports a PNG, or records a video.");
         ImGui::Spacing();
         ImGui::TextWrapped(
            "Nothing enforces that order - any output can feed any input, including "
            "back into effects for feedback-style chains.");
      }

      if (ImGui::CollapsingHeader("Controls", ImGuiTreeNodeFlags_DefaultOpen))
      {
         if (ImGui::BeginTable("controls", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
         {
            ImGui::TableSetupColumn("Action");
            ImGui::TableSetupColumn("How");
            ImGui::TableHeadersRow();
            struct Row { const char* a; const char* b; };
            static const Row rows[] = {
               { "Add a node", "Right-click or double-click the canvas, then type to filter" },
               { "Note on the canvas", "Press / and start typing. Double-click an existing note to edit it." },
               { "Connect", "Drag from a node's 'out' dot to another node's input dot" },
               { "Modulate a parameter", "Drag a modulator's 'out' onto the small dot beside any slider" },
               { "Colour from a photo", "Add a Palette node, give it a reference image, then drag its 'out' onto the square dot beside any colour swatch. Each new cable takes the next swatch; click a bound swatch to step it." },
               { "Type an exact value", "Double-click a slider" },
               { "Pan the canvas", "Drag empty canvas" },
               { "Rubber-band select", "Shift + drag" },
               { "Duplicate", "Cmd+C / Cmd+V, or Shift+D to duplicate in place" },
               { "Select several", "Shift + drag a box around them, or Shift-click (or Ctrl-click) each one to add it. Then move, duplicate or delete as a group." },
               { "Delete", "Select, then Delete or Backspace" },
               { "Zoom", "Scroll (speed is adjustable in the Menu)" },
               { "Play / pause everything", "Play button in the top bar" },
            };
            for (const Row& r : rows)
            {
               ImGui::TableNextRow();
               ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(r.a);
               ImGui::TableSetColumnIndex(1); ImGui::TextWrapped("%s", r.b);
            }
            ImGui::EndTable();
         }
      }

      if (ImGui::CollapsingHeader("Transport and modulation", ImGuiTreeNodeFlags_DefaultOpen))
      {
         ImGui::TextWrapped(
            "The top bar holds a global clock: Play/Pause, Rewind and BPM. Everything "
            "time-based reads from it - modulators, video playback and animated shaders - "
            "so pausing freezes the whole patch and changing the tempo retimes all of it "
            "at once.");
         ImGui::Spacing();
         ImGui::TextWrapped(
            "Modulator rates are given in beats, not seconds. A rate of 4 means one "
            "cycle every four beats, so it stays in time when you change the BPM.");
         ImGui::Spacing();
         ImGui::TextWrapped(
            "Every slider has a small dot to its left. Patch a modulator into that dot "
            "and the slider turns amber and becomes read-only - the value is now being "
            "driven. Delete the cable to take manual control back.");
      }

      if (ImGui::CollapsingHeader("Using Feedback", ImGuiTreeNodeFlags_DefaultOpen))
      {
         ImGui::TextWrapped(
            "A Feedback node outputs what its input produced on the PREVIOUS frame. "
            "That one-frame delay is the whole point: it lets you wire a cycle "
            "without the graph chasing its own tail forever.");
         ImGui::Spacing();
         ImGui::TextWrapped("Feedback on its own does nothing visible - it is a delay, not an effect. "
                            "It only earns its keep inside a loop. The classic patch:");
         ImGui::Indent();
         ImGui::Bullet(); ImGui::TextWrapped("Shape (or any source)  ->  Blend input A");
         ImGui::Bullet(); ImGui::TextWrapped("Feedback  ->  Blend input B");
         ImGui::Bullet(); ImGui::TextWrapped("Blend  ->  transform  (scale 1.02, small rotation)");
         ImGui::Bullet(); ImGui::TextWrapped("transform  ->  back into Feedback's input   <- this closes the loop");
         ImGui::Unindent();
         ImGui::Spacing();
         ImGui::TextWrapped(
            "Now every frame is the previous frame, nudged, with the source drawn "
            "over the top - which gives you infinite-zoom tunnels, echoes and "
            "growth. Set the Blend to Screen or Lighten and lower the opacity so "
            "the history fades rather than saturating.");
         ImGui::Spacing();
         ImGui::TextWrapped(
            "If you just want trails, use the Trails node instead - it is that same "
            "loop wrapped into one node, with decay, drift, zoom and rotation built "
            "in. Reaction-Diffusion is the other pre-wired feedback node: it needs "
            "no input at all and simulates a chemical system frame over frame.");
      }

      if (ImGui::CollapsingHeader("Module reference"))
      {
         struct Entry { const char* name; const char* text; };
         struct Group { const char* category; std::vector<Entry> entries; };
         static const std::vector<Group> groups = {
            { "Source", {
               { "Image Source", "Loads a still image. Opens the native file picker and decodes anything macOS can read - PNG, JPEG, TIFF, HEIC, RAW and more." },
               { "Video", "Plays a video file. Position follows the transport, so it pauses with everything else. Loop and speed (including reverse) are available." },
               { "VMPC", "VJ clip launcher, the MPC for video: 16 pads, one video clip each, hit with the mouse, a CV pin per pad or the note input (base note 36 = pad 1). Per pad: one shot, gate (loops while held) or loop (toggle), trim in/out and speed (negative = reverse). The output is the clip of the last pad hit; with nothing playing it is transparent (or the last frame with hold last frame). Transition (cut, fade, slide, wipe, zoom) and its time apply when a hit switches clips. Silent: use a Video node for soundtracks." },
               { "Shape", "Ten vector primitives - circle, ellipse, rectangle, rounded rect, triangle, polygon, star, ring, cross, line - with fill, stroke, feather and background." },
               { "Noise", "Procedural noise: value, fBm, ridged, Voronoi, Worley edges and white. Domain warping, octaves and colour mapping included. The 4D types (Simplex, Perlin, Ridged, Turbulence, Billow 4D) work like TouchDesigner's Noise TOP: the picture is a slice of 4D noise and speed moves along the 4th axis, so it morphs in place; drift adds an optional scroll, plus translate, z, rotate and exponent." },
               { "Draw", "Paint straight onto the node preview. Six procedural brushes, eraser, spacing and jitter. Patch an image in to paint over it. Strokes can be recorded and replayed as an animation." },
               { "Formula", "A live GLSL shader. Pick a preset or press 'Edit GLSL...' to write your own; four knobs (uA-uD) are exposed for modulation. Four image inputs (in0-in3) are read as texture(uIn0, uv) .. uIn3, with uHasIn0..3 and uInRes0..3, like TouchDesigner's GLSL TOP; 'size from in0' renders at in0's resolution. Shadertoy mode: paste a whole Image tab (code with mainImage) and it runs as is, with iTime, iResolution, iFrame, iTimeDelta, iMouse (the mouse x / y / down params), iDate and iChannel0-3 = in0-in3. Every 'uniform float name; // lo..hi = default' line becomes an extra knob with a CV pin. For a Buffer pass turn on 'float buffer' (RGBA32F, alpha kept) and route 'last frame to' an iChannel for the node's own previous frame; chain Formula nodes for Buffer A -> B -> Image." },
               { "Texture", "Blender-standard procedural textures: Voronoi, Brick, Magic, Wave and Musgrave, each with its own parameter block." },
            } },
            { "Text", {
               { "Text", "Renders text using any font installed on the system, with size, colour, tracking, alignment and position." },
            } },
            { "Effects", {
               { "Blur family", "Gaussian, box, motion (angle + distance) and radial (with a centre point)." },
               { "Bloom / Diffuse Glow", "Bloom isolates pixels above a threshold and blooms them outward. Diffuse Glow screens a blurred copy back over the image." },
               { "Sharpen", "Unsharp mask - blurs a copy and adds back the difference." },
               { "Distortion", "Twirl, pinch/punch, ripple, lens distortion (with chromatic aberration), displace and liquify. Position-dependent effects have Centre X/Y." },
               { "Glitch family", "Ten kinds: slice shift, RGB shift, scanlines, blocks, wave, datamosh, scan jitter, VHS, compression and pixel sort, with size variation, beat sync, bursts and per-slice colour tricks. The separate Datamosh node does real block motion-vector datamosh." },
               { "Symmetry", "Symmetry (mirror about X, Y or both), Kaleidoscope (segment count, rotation, zoom) and Mirror Tile." },
               { "Stylise", "Halftone (mono or CMY-style colour), Sobel edge detection and Edge Outline." },
               { "Pixelate / Noise / Vignette", "Block pixelation, additive grain and a vignette with its own centre." },
            } },
            { "Color", {
               { "Basic", "Brightness/contrast, exposure, levels (black/white point + gamma), invert, posterize, threshold." },
               { "Curves", "Per-channel spline curve editor - drag control points on Red/Green/Blue/Luminance, Photoshop Curves-style." },
               { "LUT", "Applies a lookup-table image patched into the second input." },
               { "Gradient Map", "Remaps luminance onto a two-colour gradient." },
               { "Channel Mixer", "Rebuilds each output channel from a weighted mix of the input channels - set all three rows equal for a weighted greyscale." },
               { "HSL / Colour Balance", "Hue, saturation and lightness; per-axis colour shifts." },
               { "Color Adjustments", "All-in-one grading chain - brightness/contrast, levels, colour balance, HSL, vibrance, tone shaper, channel mixer and an optional black & white stage - so a common grade doesn't need eight nodes wired in series." },
               { "Color Ramp", "Recolors any 0-1 grayscale input through user-authored stops, up to 32 of them, with linear or constant interpolation. Unlike Gradient Map, it has no shape of its own - the shape comes from upstream." },
            } },
            { "Compositing", {
               { "Transform", "Translate, scale, rotate, flip horizontal and flip vertical." },
               { "Blend", "Two inputs and 32 blend modes - the full Normal / Multiply / Screen / Overlay / Hue / Saturation / Colour / Luminosity set, plus Erase and Anti-Erase (keep A only where B is opaque, a mask). Like TouchDesigner's Composite TOP it can place B instead of stretching it (pre-fit Fit / Fill / Native with justify, plus B translate, scale and rotate), combine alpha with Over / Under / Inside / Outside / Atop / Xor, and take its size from A, B or a custom resolution." },
               { "Layer Stack", "Four inputs stacked bottom-up: A is the base, D sits on top. Each layer has its own blend mode and opacity, and dragging a layer header reorders the whole layer." },
               { "Layout", "TouchDesigner-style Layout: a canvas of exact pixel size (1920x1080 by default) with up to 8 image inputs placed on it. Each layer starts at its REAL pixel size, and you set x/y in canvas pixels, a scale (anchored at the layer centre) or an exact custom width/height, and opacity. Drag layers on the miniature, or use 1:1 / fit / fill / centre. x, y, scale and opacity have CV pins." },
               { "Switcher", "Cycles between its connected inputs every N beats or seconds, with an optional crossfade. With 'manual' an index picks the input instead (A=0 .. D=3); a fractional index blends the two inputs either side of it, like TouchDesigner's Switch TOP, so modulating it crossfades." },
               { "Fit", "Resamples an input to a chosen resolution. Fit letterboxes, Fill crops, Stretch ignores aspect, Native passes through. Offset X/Y slides the result in output pixels (+ right, + up). Limit only ever shrinks: the source keeps its aspect and is scaled down to fit inside width x height. Smoothness Mipmap stops detailed images sparkling when shrunk. Use it to make differently-sized sources composite predictably." },
               { "Drop Shadow / Outer Glow / Colour Overlay", "Layer-effect style filters." },
            } },
            { "Modulators", {
               { "LFO", "Sine, triangle, saw up/down, square and sample-and-hold. Rate in beats, plus phase and an output range." },
               { "Random", "A new random value every N beats, with adjustable smoothing between steps. Deterministic, so rewinding replays the same sequence." },
               { "Pattern", "A 16-step sequencer with a drag-paint bar grid. Choose how many steps to use, or fit them to the time signature, and it loops through them one step every N beats. Optional glide and bipolar display." },
               { "Math", "Combines two modulators - add, subtract, multiply, divide, min, max, average, difference - with gain and offset. Unpatched inputs fall back to a constant." },
               { "Compare", "Outputs 1 when the comparison holds, 0 otherwise." },
               { "Range to Range", "Remaps one modulator's input range onto a different output range." },
               { "Smoothing", "An exponential moving average over another modulator, to damp jitter." },
               { "Envelope", "Shapes an incoming modulator with an ADSR contour, gated by it crossing threshold, instead of generating its own trigger." },
               { "Invert", "Mirrors a modulator around a low/high pivot. Defaults to 0..1 for a classic 1-v flip; set low/high to match an unclamped source to mirror it correctly." },
               { "Mod Curve", "Remaps a modulator through a draggable transfer curve - an S-curve, staircase, or exponential response, all things a slider can't express." },
            } },
            { "Feedback", {
               { "Feedback", "Outputs the previous frame. Nothing visible on its own - it is the delay that makes a loop legal. See 'Using Feedback' above." },
               { "Trails", "A pre-wired feedback loop: decaying accumulation with drift, zoom, rotation and hue rotation. Reach for this before wiring a loop by hand." },
               { "Reaction-Diffusion", "Gray-Scott chemical simulation, six presets. Needs no input; patch one in and its luminance varies the feed rate so the pattern grows differently through light and dark." },
            } },
            { "Mask", {
               { "Remove Background", "Windows uses U2Net through Windows ML + DirectML/DX12 and can be forced to CPU; macOS uses Apple Vision. Inference runs on a latest-frame worker, so auto refresh drops stale frames instead of blocking the UI." },
            } },
            { "Resynth", {
               { "Resynthesize", "Each generation reads the previous one, so the image drifts away from the source. The XY pad blends four named mutation effects assigned to its corners; Randomise re-rolls which four. The orb's path can be recorded, looped and replayed in time." },
            } },
            { "Output", {
               { "Output", "Terminal node. Shows the final image, exports a PNG, and records an H.264 .mov at a chosen frame rate. Recording captures the cooked output, so what you see is what is written." },
               { "Spout Out", "Broadcasts video, 3D renders, or visual shaders to other Windows apps through Spout2 (zero-copy GPU texture sharing: Resolume, TouchDesigner, OBS, MadMapper...). Patches that used the old Syphon Out name still open." },
               { "Projection", "Warp, corner-pin and perspective-correct an image for projectors, flat walls, or curved screens, with built-in alignment test patterns and custom resolution target. Outside the warped image the output is transparent (alpha), the outline is antialiased, and each edge can have its own edge blend (width, curve, gamma) that fades the alpha or darkens RGB, for overlapping projectors." },
            } },
            { "OSC", {
               { "OSC Receive", "Listens on a UDP port for Open Sound Control messages matching an address pattern, and reports the last received value as a modulator (remapped through low/high). Behaves like LFO/Random - patch its output onto any slider's modulation pin." },
               { "OSC Send", "Sends its patched modulator input as an Open Sound Control message (address + float) to a host:port over UDP, on change (past an epsilon) or at least every interval - the one node in the patch with no output of its own." },
               { "OSC to CV", "Up to 8 OSC addresses (or 8 arguments of one address) as 8 modulator outputs, each with its own input range, invert and smoothing. Press learn on a channel and move the control on your phone/controller to assign it. Listens on all network interfaces; several OSC nodes can share one port." },
            } },
         };

         for (const Group& group : groups)
         {
            ImGui::SeparatorText(group.category);
            for (const Entry& entry : group.entries)
            {
               ImGui::Bullet();
               ImGui::TextUnformatted(entry.name);
               ImGui::Indent();
               ImGui::PushTextWrapPos(0.0f);
               ImGui::TextDisabled("%s", entry.text);
               ImGui::PopTextWrapPos();
               ImGui::Unindent();
            }
         }
      }

      if (ImGui::CollapsingHeader("Tips"))
      {
         ImGui::Bullet(); ImGui::TextWrapped("Put a Fit node before a Blend or Layer Stack when your sources are different sizes.");
         ImGui::Bullet(); ImGui::TextWrapped("Modulate a Switcher's 'every' with an LFO for irregular cutting.");
         ImGui::Bullet(); ImGui::TextWrapped("Chain a Math node off two LFOs at different rates to get slow drifting motion.");
         ImGui::Bullet(); ImGui::TextWrapped("A node's output can feed several inputs at once - it only renders once per frame.");
         ImGui::Bullet(); ImGui::TextWrapped("Settings and the last graph layout are stored in ~/Library/Application Support/Infinite.");
      }

      ImGui::End();
   }
