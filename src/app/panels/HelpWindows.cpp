// Node help text tables, shortcuts and help windows (moved verbatim from main.cpp).
#include "app/AppShared.h"

namespace app
{
   // Per-node "Help" popup text, keyed by the exact registered typeName (the
   // same string GraphNode::typeName holds - see REGISTER_NODE / FilterDef::name).
   // This is where usage notes, wiring conventions and gotchas live now, rather
   // than as permanent text drawn on the node body: a node you already know how
   // to use shouldn't have to carry its own manual around forever.
   const char* SpecificNodeHelpText(const std::string& typeName)
   {
      static const std::unordered_map<std::string, const char*> kText = {
         // ---------------- Source / Text ----------------
         { "Image Source", "Loads a still image. Opens the native file picker and decodes anything macOS can read - PNG, JPEG, TIFF, HEIC, RAW and more." },
         { "Slideshow", "Plays the supported images in one folder in alphabetical order. Hold and transition time follow the global transport; Native Size keeps source pixels 1:1, Best Fit stretches to fill, and Proportional Fit preserves aspect." },
         { "Video", "Plays a video file. Position follows the transport, so it pauses with everything else. Loop and speed (including reverse) are available. Also outputs the clip's own audio track, if it has one, on the same transport-driven clock as the picture (audioEnabled/volume)." },
         { "Noise", "Procedural noise: value, fBm, ridged, Voronoi, Worley edges and white. Domain warping, octaves and colour mapping included." },
         { "Shape", "The base 2D vector-primitive node - pick any of its 20 shapes from the dropdown, with fill, stroke, feather and background controls. Each shape also has its own directly-spawnable named node (Circle, Hexagon, Star, ...) that just starts on that shape." },
         { "Draw", "Paint straight onto the node preview. Six procedural brushes, eraser, spacing and jitter. Patch an image in to paint over it. Record, then draw - replaying redraws the stroke in time, and the canvas size follows the input when one is patched in." },
         { "Formula", "A live GLSL shader. Pick a preset or press 'Edit GLSL...' to write your own; four knobs (uA-uD) are exposed for modulation." },
         { "Field Modifier", "Runs a per-vertex Field kernel over geometry, modifying P, N, uv, Cd and custom attributes with rate-inferred execution." },
         { "Field Primitive", "Generates procedural 3D point geometry from scratch using a per-element Field kernel (Circle, Spiral, Grid Lattice, Fibonacci Sphere, Helix, Torus Knot)." },
         { "Field Effect", "Runs a Field kernel once per audio sample, on the audio thread - write your own sample-domain audio effect. `in` is the audio input pin, `state` declares per-voice memory, `param` exposes a modulatable knob. `reduce.rms(in, loHz, hiHz)` publishes a band-limited RMS meter reading once per block." },
         { "Field Graph", "Runs a Field kernel once, at edit time, to build part of the graph itself - emit(\"Type Name\", k0, k1, ...) spawns a node and hands back a handle, connect()/set()/place() wire it up, set its params and position it. Press Regenerate to re-run: it diffs against what it built last time (by emit-site identity) instead of deleting and respawning, so hand-edits to params on the spawned nodes survive an unrelated Regenerate." },
         { "Texture", "Blender-standard procedural textures: Voronoi, Brick, Magic, Wave and Musgrave, each with its own parameter block." },
         { "Ramp", "Generates a gradient from scratch (no input) between up to 8 user-set colour stops, at a chosen angle, scale and offset. Gamma and dither smooth out visible banding." },
         { "Text", "Renders text using any font installed on the system, with size, colour, tracking, alignment and position." },

         // ---------------- Effects (built-in, non-filter-table) ----------------
         { "Curves", "Shadow / midtone / highlight lift plus an S-curve control, per RGB channel or all together. Drag points on the curve to bend it, click empty curve to add a point, right-click a point to remove it." },

         // ---------------- Color ----------------
         { "Color Ramp", "Recolors any 0-1 grayscale input through user-authored stops, up to 32 of them, with linear or constant interpolation. Unlike Gradient Map, it has no shape of its own - the shape comes from upstream." },
         { "Predictive Coloring", "Learns what 'graded' footage looks like for you and grades incoming frames toward it. Press Learn on already-graded footage to build a target profile (or leave self normalize on to auto-level/contrast without one); mix blends the grade in. Wander makes the grade gently drift among looks the profile has actually seen instead of solving to the exact same answer every frame - 0 is the old fixed behavior, higher wanders more, scaled down automatically for a thin or low-confidence profile." },

         // ---------------- Compositing ----------------
         { "Blend", "Two inputs and 32 blend modes - the full Normal / Multiply / Screen / Overlay / Hue / Saturation / Colour / Luminosity set, plus Erase." },
         { "Layer Stack", "Four inputs stacked bottom-up: A is the base, D sits on top. Each layer has its own blend mode and opacity, and dragging a layer header reorders the whole layer." },
         { "Switcher", "Cycles between its connected inputs every N beats or seconds, with an optional crossfade. Can be pinned to one input with 'manual'." },
         { "Fit", "Resamples an input to a chosen resolution. Fit letterboxes, Fill crops, Stretch ignores aspect, Native passes through. Offset X/Y slides the result in output pixels (+ right, + up). Use it to make differently-sized sources composite predictably." },
         { "Comment", "A free-floating note on the canvas - has no image input or output, just text. Double-click to edit." },
         { "Group", "Created with " MODKEY "+G on a selection, not spawned from the palette. Sizes itself automatically to fit its members - drag a node in to grow the box, drag one out to shrink it. Drag anywhere inside the box to move the whole group; right-click > Ungroup (or " MODKEY "+U, " MODKEY "+Shift+G) dissolves it, leaving members in place (or ungroup one member from its own context menu)." },
         { "Null", "A pass-through node: its output is exactly its input, unchanged. Useful as a stable junction point to branch a cable to several destinations, or as a placeholder while rewiring." },
         { "Viewport", "Shows its input at actual pixel size in its own resizable window, separate from the small node preview - useful for judging detail without zooming the whole canvas." },

         // ---------------- Modulators ----------------
         { "LFO", "Sine, triangle, saw up/down, square and sample-and-hold. Rate in beats, plus phase and an output range." },
         { "Random", "A new random value every N beats, with adjustable smoothing between steps. Deterministic, so rewinding replays the same sequence." },
         { "Pattern", "A 16-step sequencer: drag the bar grid (paint across bars by dragging horizontally), choose how many steps to use or fit them to the transport's time signature, and it loops through them one step every N beats. Optional glide between steps and a bipolar display mode." },
         { "Math", "Combines two modulators - add, subtract, multiply, divide, min, max, average, difference - with gain and offset. Unpatched inputs fall back to a constant, shown as an editable slider; a patched input just shows as 'patched'." },
         { "Compare", "Outputs 1 when the comparison holds, 0 otherwise - >, >=, <, <=, ==, != between two modulators, with a tolerance for the equality checks." },
         { "Range to Range", "Remaps one modulator's input range onto a different output range - patch in a 0..1 LFO and remap it to -2..2, for example." },
         { "Smoothing", "An exponential moving average over another modulator, to damp jittery or steppy sources like Random or Pattern." },
         { "Envelope", "Applies an ADSR contour to an incoming modulator instead of generating its own trigger: rising above threshold starts attack/decay/sustain, falling back below it starts release. Patch an LFO in and its swing gets shaped by the ADSR, retriggering once per LFO cycle. Output collapses toward 0.5 as the envelope level falls - at level 0 the input has no say, at level 1 it passes through unchanged - so with nothing patched in, 'in' holds steady at its own constant value." },
         { "Mod Curve", "Remaps a modulator through a draggable transfer curve - click empty space to add a point, drag to move it, right-click to remove it (or right-click empty space to reset to a straight line). The gridline marks 0.5, where a bipolar binding's 'no modulation' point lives, and the moving dot shows where the input currently sits on the curve. mix blends between the raw input and the curved output, so 0 is a true bypass." },
         { "Drift", "Learns where you leave each knob and how fast you move it, then keeps the knob going after you let go, settles it into your usual places and wanders between them at your pace. Speed scales the learned pace, Stray widens or narrows the wandering, Momentum is how long a release carries on, Link lets your live hand activity stir it: quiet knobs wake up while you play and settle when you stop. Shift-drag a driven knob to take over; the dot on the knob shows how much history backs it and the faint marks show where it is likely to go next. Freeze stores the learned profile in the patch." },
         { "Moves", "Your Moves macro faders: extracts the principal axes of your multi-knob gestures using PCA on deltas, letting a few macro faders drive many coupled parameters together in your style." },
         { "Predictive Modulator", "Wire a modulator in and press Learn: it passes the signal through while it listens, fits a small dynamical model (Dynamic Mode Decomposition) of its shape, then on Stop free-runs that model on its own - decoupled from the live cable - instead of replaying the exact recording. Rank sets how many delayed copies of the signal the fit is built from (2, 3, 4 or 8); the readout shows the fit's spectral radius, how close the learned pattern sits to the edge of instability." },
         { "CV to Pitch", "Quantizes a modulator to semitone steps over range low..high, shown as a large +/-N st readout. Still outputs 0..1 like any modulator - it just restricts where in 0..1 the value can land, so the span maps onto whole semitones. Scale/root snap to scale degrees instead of every semitone (chromatic = off); glide adds portamento between steps, passing through unquantized values in transit on purpose." },
         { "Macro Knob", "A single named slider (0-1, with a response curve and invert) meant to be patched out to several other sliders at once - one control that fans out to many parameters." },
         { "Macro XY", "A 2D pad exposing X and Y as two separate modulator outputs from one drag. The pad's path can be recorded, looped and replayed in time, like Resynthesize's orb." },
         { "Path", "Outputs a moving 3D point (X/Y/Z, each patchable separately) travelling around a built-in shape (circle, helix, spiral, lissajous, etc.) at a beat-synced speed, or along the points of a patched-in geometry/curve source instead." },
         { "Geometry Table", "Samples up to 16 points off a patched geometry source and outputs each as its own X/Y/Z modulator, plus a centroid (cx/cy/cz) and spread aggregate that stay meaningful however the row count or point set changes. Vertex mode reads native vertices (or a point cloud's points, which always wins over its own billboard mesh); Scatter reads an area-weighted random sample instead; Contour walks the mesh's boundary/slice outline (or a patched curve) in order. Sort turns the table into something specific - by axis for a ramp bank, by angle for a phase-ordered ring, by distance from the centre. offset scrubs a read head across the sampled set without re-sampling, so it's the one control worth binding to an LFO. Fixed space maps world units through `extent` (how far the object may travel, not how big it is); Bounds self-scales to the sampled set's own bounding box, which means a rigid translation of the source produces no change - that's by design, not a bug." },
         { "Constant", "Outputs one fixed number - the simplest possible modulator, useful for feeding a Math input or a modulation slot that expects a cable rather than manual control." },
         { "Null Modulator", "Pass-through modulation node that accepts an incoming modulator and forwards it to its output. Useful for monitoring, signal routing, or placeholders." },
         { "Palette", "Samples colours from a reference image, loaded here or patched in - a patched cable overrides the loaded file, but the file is kept so unplugging falls back to it. Drag its 'out' onto the square dot beside any colour swatch to bind it - each new cable takes the next swatch, and clicking a bound swatch steps it. Its image output is a gradient of the palette." },
         { "Audio File", "Loads an audio file for playback and Audio Analyze to read. Keeps analysing even while muted - the 'audible' checkbox only controls monitoring." },
         { "Image Analyze", "Turns an image or video into control values and modulation channels. Supports UV point probes, ROI boxes, 22 math/color operations, custom algebraic formulas, and multiple modulation output taps." },
          { "Audio Analyze", "Extracts level, band and onset values from audio for modulation - patch any audio source into it (Audio In, Audio File, a Filter, a Mixer, an Oscillator) and every output can drive any slider in the graph. With nothing patched in it falls back to its own Start listening button, a live tap on the system's default input device. It passes its input straight through, so it can sit inline in a chain as well as hang off one as a tap. Outputs: level, low/mid/high, onset, and b1-b8, eight raw frequency bands running low to high." },
         { "Plugin", "Hosts a third-party Audio Unit effect. Drag one in from the Plugins panel (Rescan there indexes what is installed; the list is cached, so launching never rescans), or drop a .component bundle from Finder. \"open\" shows the plugin's own editor in a separate window. The sliders on the body are plugin parameters you chose to expose: turn \"configure\" on and touch a control in the plugin's own window and it appears here as a mapped row - or pick one from the dropdown, since not every plugin's editor tells the host what was touched. Each mapped row is a real param with its own modulation pin, so a Ramp or Envelope can drive it. Right-click a row to unmap it. With nothing loaded, or bypassed, audio passes through unchanged." },
         { "Oscillator", "A synth oscillator with four classic waveforms (sine, triangle, saw, square), interactive amp envelope, unison, filter, hard sync, and fine/coarse tuning. With no note cable connected, it free-runs at a set frequency; connect a note cable and it becomes polyphonic and envelope-gated." },
         { "Wavetable", "Two independent wavetable engines with unison, filter, and pitch/filter/amp envelopes, mixed by an A/B control. With no note cable connected, it free-runs at a set frequency; connect a note cable and it becomes polyphonic and envelope-gated." },
         { "Analog", "A classic polyphonic virtual-analog synth voice with two analog-style oscillators (osc1 unison stack, osc2 tuning/detune/sync, sub osc one octave down, white noise, pre-filter drive stage, nonlinear ZDF Moog-ladder or SVF filter, and amplitude ADSR across up to 8 voices). Detune reads as the stack's true total width in cents, distributed unevenly across the stack the way separately mistuned circuits sit. Spread is stereo width: it splits the stack across two independent drive/filter chains and places each voice card in the image, so the sides carry different oscillators rather than one panned copy." },
         { "Equation Synth", "A synth defined by a live formula (y = f(x, a, b, c, d, t)) instead of a fixed waveform - knobs a-d feed the equation directly, so turning them reshapes the waveform itself rather than modulating a preset one." },
         { "Sampler", "A sample player: load a file (or drag one in from the Samples search panel), or record from the audio input pin. Click the waveform to audition from that point, or use the audition button - both preview this node on its own dedicated voice, independent of the transport and any note cable, and never cut off or get cut off by an incoming note. Drag the waveform's two edge handles to set the loop range (start/end). pitch/finetune are coarse/fine tuning, speed is a -2..2 varispeed control (negative plays backward), volume is the output level. loop/rev/p-p control what happens at the range edges: loop wraps or bounces (ping-pong) instead of stopping, reverse flips the base direction. fade in / fade out are lengths in ms applied at the start and end of every pass through the range (each loop lap, ping-pong leg or one-shot), so a loop dips at its seam instead of clicking; 3 ms each by default, 0 for a hard edge. Patches saved with the old loop xfade load with the defaults. With no note cable connected, it free-runs on the transport - starts the moment you hit space, stops when you stop it; connect a note cable and it becomes polyphonic instead, each note played back at the pitch offset from middle C. Spacebar always silences every voice this node is making." },
         { "Slicer", "Chops a sample into slices and maps them chromatically to the keyboard from MIDI note 36 upward - note 36 plays slice 1, 37 plays slice 2, and so on. A note past the last slice is silent; it does not wrap round to slice 1. Load a file (or drag one in from the Samples panel), or record from the audio input pin. slice by picks where the boundaries come from: onsets runs transient detection over the sample on a background thread, grid divides it arithmetically at the *global transport tempo* (there is no per-node bpm - change the tempo and the grid follows). sensitivity is the detection threshold and is the only control that re-runs the analysis; onsets just caps the result to the strongest N, and division/slice by recompute boundaries instantly. Click a slice band in the waveform to audition it, and in onsets mode drag any marker to move a boundary by hand - hand-edited markers are saved with the patch. Two separate controls decide how long a slice lasts: crossthrough sets whether playback may run PAST the slice's own next onset (off by default - each slice stops where the next begins), while decay shapes only the amplitude envelope, reading 'hold' at the top of its throw where the slice stays at full level. So: crossthrough off + hold is the classic tight chop; crossthrough off + a decay ends at whichever comes first; crossthrough on + hold plays through the rest of the sample; crossthrough on + a decay is a one-shot with a tail over the rest of the break. attack extends each slice's own fade-in from instant up to half a second." },
         { "Molder", "Analysis/genome resynthesis: decomposes a loaded or recorded sample into tracked harmonic partials plus a real residual waveform, then Roll mutates a parameter genome and re-renders a new sample from it - each roll walks further from the last, not from the original. Iterate feeds the last render back in as the new source and re-analyses it (progressively eating the sound); Reset returns fully to the originally loaded/recorded sample - generation 0 and the six shaping knobs (tone/air/snap/stretch/time/pitch) back to neutral, and the analysis itself restored, undoing any Iterate. chaos sets how far the next roll jumps; pitch offsets on top of the genome's own pitch walk; tone balances partials against residual; air/snap are the residual's steady-hiss and transient-attack levels; stretch scales inharmonicity together with harmonic spacing; time warps the attack/decay timing without changing the sample's length. This is a sound designer, not a playable instrument - it takes no note input, only a single self-triggered voice with start/end range, loop, reverse and ping-pong, the same transport as Sampler. Analysis and rendering both run on a background thread, so rolling never stalls the UI. seed/gen/f0/harm in the readout are the exact genome (seed + generation count) and the analysed pitch - two integers are enough to reproduce any rolled sound exactly on reload." },
         { "Grain Molder", "Slices audio into overlapping grains, calculates per-grain metrics (Level, Brightness, Random), and rearranges them based on a continuous blend between original temporal position and metric rank. At amount 0 it is the clean identity passthrough; at 1 it is fully sorted into a swell or brightness contour. Rendering runs asynchronously on a worker thread." },
         { "Drum Sequencer", "An 8-lane, up-to-32-step drum machine: 8 lane cards (waveform + transient/decay/pitch/fine tune/volume/pan) above an 8-lane step grid that shows 16 steps at a time (the small 1-16 | 17-32 pill at the right of the line under it switches pages once steps goes past 16 - greyed out until then; a dot marks the page the playhead is on). Click a card's waveform to load its sample (a drag from the Samples panel or an OS file drop also work), or drag its edge handles to trim the playback range; x clears it, and the choke button cycles its choke group (0 = none - two lanes sharing a group cut each other off, the closed/open hi-hat case). In the grid, R randomises that lane's fill, M/S mute or solo it. Click a step to toggle it, drag vertically on a lit step to set its velocity, drag horizontally to paint a run of steps on/off. Between the lane cards and the grid, the groove picker holds a built-in library of 141 grooves in 10 categories: pick a category (jumps to its first groove) or a groove (the list is grouped and has a filter box), step through with < and >, and switch the groove's A verse / B bridge / C chorus part with the small A B C pill at the end of the row; picking replaces the pattern, rate, swing and step count in one undo step and fills any empty lane from the bundled kit (a lane you loaded yourself is never touched). The bottom rows are pattern-wide: rate/steps/swing/output, then four offsets (transient/decay/pitch/pan) composed on top of every lane's own value. Plays the moment it's patched, phase-locked to the transport - there's no note input, just its own Transport-derived sequence. run stops this node's own step firing without touching the transport; randomise seeds a musical kick/snare/hat starting pattern." },
         { "MPC", "How to use: wire the out into Audio Out, drop audio files onto the pads (or click an empty pad, or Load... / Folder... for the first 16 files of a folder), then click a pad or send notes into the notes input - notes 36 to 51 play pads 1 to 16. A 16-pad sample player: pad 1 is bottom-left like a hardware MPC, every pad is a square tile showing its waveform, number and mode, and each pad has its own CV pin, so a MIDI CC / Note modulator or any gate can play it. Every pad is its own voice, so pads play together, and a click plays at once. A hit follows the pad's mode: one shot plays the whole sample and a new hit restarts it; gate plays while held (mouse down, CV high or note held) and stops on release; loop toggles a looping playback on each hit. Click selects a pad (right-click selects without playing); the rows below the pads edit the selected pad: sync and rate, fine tune (cents), pitch (semitones), speed (negative plays backwards), volume, pan, mode, and fade in / fade out (ms, at the start and end of every pass), defined as on the Sampler. sync is per pad: Free (the default) plays a hit at once; Synced latches the hit and fires it on the next grid line of the transport at the chosen rate (1/4, 1/8, 1 bar ... the same divisions as every other node), sample-accurately; a hit on the line itself fires on it, with the transport stopped it fires at once, and in gate mode letting go before the line cancels the hit. In loop mode a synced pad re-triggers the sample on every division while it is on (one pass per division: a longer sample is cut at the line, a shorter one leaves a gap), and with the transport stopped it plays as a plain loop. The rate control is greyed while a pad is Free. Every param of every pad can be modulated at any time: a cable stays bound to its own pad when you select another pad, and an orange dot on a tile shows that pad has a modulated param. A tile shows the mode and, when synced, the rate. The node's audio out is the mix of all pads. Loaded sample paths and pad settings are saved with the patch; the audio is re-read on load." },
         { "Looper", "How to use: wire the sound to loop into the input and the out to Audio Out, press Rec (with the transport playing and sync on it waits for the next bar; pressed up to 200 ms late, the take still starts on the line just passed), and the take ends by itself after the take length, then loops; Play stops and restarts it, Dub layers what comes in over it, Clear empties it. The waveform shows the loop with a playhead and the beat grid, and the line under the title says what the looper is doing. Each button has a CV pin, so a footswitch or MIDI note can drive them (a rising edge presses). take sets the length: a musical division (1 bar by default), or free, where the next Rec press ends it. Pressing Dub while recording ends the take and goes straight into overdub. Undo steps back one layer at a time (the last take, overdub or Clear, up to 8 steps) and the loop keeps playing; pressed during an overdub it throws that layer away. Playback has the Sampler's controls: finetune (cents), pitch (semitones), speed (negative plays backwards), volume, and fade in / fade out (ms) at the start and end of every pass. At exactly 1.00x the loop stays on the grid; at any other rate it plays in length / rate and DRIFTS against the transport, and Dub is paused while it does (layers are only written at the rate they were recorded). thru monitors the live input. Each take is always shifted by the audio interface's measured round-trip latency, so a loop played in time sits on the grid. The loop (up to 60 s) is saved as a WAV in your Recordings folder and comes back, stopped, when the patch is opened." },
         { "Audio In","Captures the default input device (mic or line-in) as a live audio source for the effects graph - patch it into a Filter, Delay, Mixer or straight to Audio Out. Trim is a plain gain stage; the mic tap starts the first time this node cooks and macOS will prompt for microphone permission then, so it stays idle until it's actually in a patch. The capture runs on its own engine bound to the system default input, independently of whichever output device is selected, and the header line says why it isn't live when it isn't." },
         { "Audio Filter", "One filter, one of 12 types (LP/HP at 12/24/36 dB, BP, notch, shelves, peak, all-pass). Drag the handle on the response curve to set frequency and gain, Shift-drag to set Q - the picture is the control." },
         { "Audio Color Ramp", "Splits incoming audio into up to 8 frequency bands - drag the dividers right on the spectrum display to resize them - and assigns each one a colour, VIBGYOR by default from low to high. With no image patched in it outputs the resulting gradient standalone; patch one into its optional image input and it grades that image by luminance through the same audio-reactive palette instead." },
         { "EQ", "Five fixed bands (low shelf, three peaks, high shelf by default), each switchable to any of low shelf/peak/high shelf/hp 12/lp 12 and independently on or off. Drag a band's dot on the curve to set its frequency and gain, Shift-drag to set Q, double-click the dot to bypass that band - the knob row below always follows whichever band you last touched." },
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
         { "Resonator Bank", "Tuned bank of up to 16 parallel bandpass resonators excited by incoming audio. Four harmonic/tuning structures (Harmonic, Odd, Chord, Metallic) with decay (T60), scatter detune and stereo spread." },
         { "Cycle Shaper", "Replaces every wavecycle of the input with a clean geometric waveform (Sine, Square, Triangle) of the same period and peak amplitude. Timbre is rebuilt while pitch and rhythm survive. Latency is one wavecycle." },
         { "Key-Snap", "Moves every spectral peak of the sound to the nearest note of a scale, so chords and layered sources stay in key. Snap blends between untouched and fully in tune, glide sets how fast a peak slides to its new note, and global key follows the transport's key and scale. Latency is 42.7 ms (2048 samples)." },
         { "Shape Resonator", "Rings the vibration modes of the shape on the 'shape' pin, like striking that object. Tune sets the lowest mode's pitch; the other modes follow from the shape's geometry, so a flat plate sounds bell-like and a long bar nearly harmonic. Pos is where it is struck and heard from: a mode stays silent when struck on its node line. With no shape connected it rings a flat rectangular plate." },
         { "Spectrum Slide", "Morphs the sound into the one on the 'to' pin by sliding its spectral peaks toward the other's positions instead of cross-fading, so a 440 Hz tone becomes 880 Hz by passing through 660 Hz. Slide sets how far along the way; with nothing on 'to' it passes the sound through. Latency is 42.7 ms (2048 samples)." },
         { "Spec Blur", "Streaming phase vocoder (N=2048, hop 512) that smears spectral magnitude in time. Transients dissolve into a harmonic cloud with tilt, phase diffusion and freeze. Latency is 42.7 ms (2048 samples); default mix is pinned at 1.0." },
         { "MIDI Notes", "Reads note events from a connected MIDI input device and outputs them as a note cable - the entry point for playing a synth or sampler from an external keyboard/controller. Turn on mpe for an MPE controller (Seaboard, Osmose, Linnstrument): each held note follows its own pitch bend." },
         { "Keyboard", "A hardware-free note source: click-and-drag the on-screen piano, or hover the node and type on your laptop keyboard (Logic/GarageBand's Musical Typing layout - ZXCVBNM... is one octave, QWERTY... the octave above) to test a patch with no MIDI controller at all." },
         { "Note Transpose", "Shifts every incoming note's pitch by a fixed number of semitones." },
         { "Pitch Bend", "A hand-driven bend wheel, patched inline in the note chain like Note Transpose. Unlike a transpose (which can only re-pitch a note as it attacks), moving this knob slides every note currently held through it in real time - drag it while a chord rings and the chord bends, no need to route it onto a synth's own bend knob. Default range is +/-2 semitones, the standard wheel range." },
         { "Velocity Curve", "Reshapes incoming note velocity through a response curve (soft/linear/hard, or a custom curve) - the note-chain equivalent of a keybed velocity curve setting." },
         { "Gate", "Overrides how long a note sounds: holdMs schedules the note-off that many ms after note-on instead of passing the real one through, for a fixed duration regardless of how long the key was held. 0 = passthrough (off)." },
         { "Humanizer", "Adds small random offsets to incoming note timing and velocity, so a mechanically even sequence sounds played by hand instead of quantized." },
         { "Quantizer", "Snaps incoming note-on timing forward to the nearest grid line, at a chosen note division." },
         { "Glide", "Plays a fast chromatic run between a note and the next one as an approximation of portamento (NoteEvent has no continuous pitch, so this is a glissando, not a true pitch ramp)." },
         { "Vibrato", "An LFO wired straight to pitch. It is a modulator, not a note-chain node - it has no note input on purpose, because a free-running wobble has no single note to attach to. Patch its output onto a synth's pitch/bend mod dot (e.g. Wavetable's 'bend' knob)." },
         { "Note Filter", "A gate on a note's pitch: scale snaps it to the nearest degree of the chosen scale/root, range drops anything outside lo..hi, and chance randomly drops the rest. A note that gets dropped has its note-off dropped with it, so nothing hangs." },
         { "Predictive Notes", "Wire a note chain in and press Learn: it listens (passing the notes through), learns the pitches, rhythm, lengths and velocities as a variable-order Markov model, then plays on its own in that style. Stray at the bottom replays the phrase, the middle plays in character, the top ignores the model and picks freely in range. The learned notes are saved with the patch." },
         { "Predictive Quantize", "A groove quantizer, not a grid one: wire a note chain in and it always listens to the actual spacing between your onsets while also correcting them in the same pass - no Learn/Stop, it just keeps adapting the more you play. The learned spacing profile is global: shared by every Predictive Quantize in every patch, and it keeps only your most recent onsets, so it tracks how you're playing now rather than an ever-growing history. Mix at 0 is untouched, mix at 1 snaps fully onto the nearest learned spacing. Unlike Quantizer's fixed grid, this follows however you actually played it, including swing or a template that isn't on a clean subdivision." },
         { "Predictive Velocity", "A learned dynamics curve, not a hand-picked one: wire a note chain in and it always listens to the velocities you play while also remapping them in the same pass - no Learn/Stop, it just keeps adapting the more you play. The learned curve is global: shared by every Predictive Velocity in every patch, and it keeps only your most recent notes, so it tracks how you're playing now rather than an ever-growing history. Mix at 0 is untouched, mix at 1 snaps fully onto the learned range, so your loudest playing maps to your own real loudest instead of a theoretical 127. Unlike Velocity Curve's one fixed shape, this follows your own dynamics." },
         { "Predictive Rhythm", "Wire any note source in and press Learn, same as Predictive Notes. It learns the rhythm, note lengths and velocity as its own habit, but learns pitch as an interval from a 'root' note instead of an absolute pitch - so what comes back out reads as a repeating pattern anchored on one note with occasional deviation (think C2 C2 C2 D3 C2 C2 C2), not a free melody. Root auto-detects to the most common note you played when Learn finishes; change it afterward with the dropdown to transpose the whole learned pattern without relearning." },
         { "Note Echo", "Repeats every incoming note event, delay ms apart, with velocity decaying and pitch shifting per repeat - a delay line for notes rather than audio. The original note always passes through first; the repeats are on top of it, not instead of it." },
         { "Note Router", "The system's only note fan-out point: one input, four distinct outputs. Round Robin cycles through them, Random picks one per note, Chain advances only when the pitch changes (a held note stays put), and Probability rolls each output independently - a note can end up on several outputs at once, or (rarely) none, in which case it falls back to output 1. A note's whole lifetime (on through off) always stays on the output(s) it started on." },
         { "Note Merge", "The system's only note fan-in point: up to four note inputs merged into one output stream, in timestamp order. Each input's notes stay independent voices matched by voice id, not pitch - so two inputs playing the same note at the same time sound as two overlapping voices, not a collision." },
         { "Arpeggiator", "Holds whatever notes are currently down and replays them one at a time on its own clock, either synced to tempo (a note division) or free-running in seconds. Up/Down/Up-Down/Down-Up/As Played order the held notes by pitch or by the order they were pressed; Converge alternates outside-in (lowest, highest, next-lowest...), Diverge alternates inside-out from the middle; Random picks one per step. Repeat x2/x4 fires each note 2 or 4 times in a row before advancing. Stairs Up/Down walks the pattern in overlapping two-note steps (C E, E G, G C...). Join and Spread only differ once octaves is above 1: Spread stacks the pattern octave-by-octave (C3 D3 E3, C4 D4 E4), Join interleaves each note's octaves together (C3 C4, D3 D4, E3 E4), and Join/Spread alternates between the two every full pass - at octaves = 1 all three play identically to Up. The 8-step gate grid below the readout is the primary control: click or drag across cells to mute individual steps without changing the note order (advancing past a muted step still moves the pattern forward, punching a rhythmic hole rather than skipping a note), and the lit cell tracks the currently-sounding step. Octaves stacks the pattern up to 4 octaves higher. Gate sets how much of each step the note actually sounds for before its off. Preset loads a complete starting point (mode, octaves, rate, gate and gate pattern) in one click." },
         { "Note Sequencer", "A self-playing step sequencer, up to 16 steps. Drag a bar's tall upper area to set that step's pitch, drag the thin strip below it to set velocity, click the strip to toggle the step on/off. Steps sets how many loop, rate is either synced to tempo (a note division) or free-running in seconds, gate is how much of each step the note actually sounds for." },
         { "MIDI File", "Plays a Standard MIDI File as a note source. Load a .mid (or drop one on the canvas) and it follows the transport: play, stop and seek move the playhead through the file, and the project tempo rules - the file's own tempo is ignored. transpose shifts every note, position (0 to 1 across the file) is where playback starts - it restarts from there whenever the transport starts or seeks, and jumps there when you move the knob. speed 1 follows the project bpm and scales it from there (the file's own tempo is ignored - it is just notes). vel scales every velocity, and loop repeats the file, rounded up to a whole 4/4 bar. Drum-channel (10) notes are never transposed. Format 0/1/2, SMPTE-timed (mapped at 2 beats per second) and RMID-wrapped files load; pitch bend and CCs are not played." },
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
         { "Remove Background", "On-device segmentation via the OS - no model download, no network, no key. Subject mode needs macOS 14, Person mode macOS 12. Segmentation is expensive, so the mask is computed on this interval rather than every frame; for video, raise the interval or use auto-refresh." },

         // ---------------- Resynth ----------------
         { "Resynthesize", "Each generation reads the previous one, so the image drifts away from the source. The XY pad blends four named mutation effects assigned to its corners - drag the orb to mix them. Randomise re-rolls which four effects sit in the corners, and the orb's path can be recorded, looped and replayed in time." },
         { "Resynthesize 3D", "The mesh equivalent of Resynthesize: each generation applies a weighted mix of geometry operators to the previous one, drifting the shape over time. 'step' advances one generation manually, 'auto step' runs it continuously, 'randomise' re-rolls the seed." },

         // ---------------- Output ----------------
         { "Output", "Terminal node. Shows the final image, exports a PNG, and records an H.264 .mov at a chosen frame rate. Recording captures the cooked output, so what you see is what is written." },
#if defined(_WIN32)
         { "Syphon Out", "Broadcasts video, 3D renders, or visual shaders to other Windows applications in real-time via Spout, zero-copy GPU texture sharing." },
#elif !defined(__APPLE__)
         { "Syphon Out", "Syphon/Spout texture sharing is not available on Linux." },
#else
         { "Syphon Out", "Broadcasts video, 3D renders, or visual shaders to other macOS applications in real-time via zero-copy GPU texture sharing." },
#endif
         { "Projection", "Warp, corner-pin and perspective-correct an image for projectors, flat walls, or curved screens, with built-in alignment test patterns and custom resolution target." },

         // ---------------- OSC ----------------
         { "OSC Receive", "Listens on a UDP port for Open Sound Control messages matching an address pattern, and reports the last received value as a modulator (remapped through low/high). Behaves like LFO/Random - patch its output onto any slider's modulation pin." },
         { "OSC Send", "Sends its patched modulator input as an Open Sound Control message (address + float) to a host:port over UDP, on change (past an epsilon) or at least every interval - the one node in the patch with no output of its own." },

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
         { "Depth Projection", "Unprojects a 2D depth map and optional color image into a 3D point cloud or triangulated surface mesh, supporting pinhole camera, planar, radial and cylindrical projections with depth curves and edge tearing." },
         { "Curve", "A generative parametric curve/tube (line, circle, spiral, helix and other presets) extruded into a mesh, with point count, smoothness, spread, height, twist and tube radius/taper controls." },
         { "Mesh to Points", "Samples the input mesh's vertices as a point cloud, for feeding Instance on Points or Metaballs." },
         { "Mesh to Edges", "Samples points along the input mesh's edges as a point cloud, for feeding Instance on Points or Metaballs." },
         { "Mesh to Faces", "Samples each face's centre point of the input mesh as a point cloud, for feeding Instance on Points or Metaballs." },
         { "Instance on Points", "Stamps a shape at every point of a point source, drawn in one instanced call. Input A is the points source (Faces/Edges/Points sampler, a point cloud, etc.), input B is the shape to stamp at each point - both are required before it draws anything. Any node chained after this one operates on the shape being stamped, once, not on the realized scatter - except Transform, which is the exception that moves the whole scatter as one rigid group." },
         { "Wrap", "Bends or conforms the source input onto the target input. Cylindrical rolls it around one axis at the target's radius with no distortion at all - letterforms, spacing and extrusion depth survive intact, which is what curves 3D text around a sphere or cylinder. Spherical adds the same bend the other way so long text also curves over the poles. Nearest Surface snaps every vertex to the closest point on the target instead: right for conforming a dense mesh to irregular geometry, but it squashes flat text. With a target connected the bend radius follows the target's size, so scaling the target moves the source with it - radius scale tunes it as a multiplier. With no target, radius sets the bend outright." },
         { "Camera", "A 3D viewpoint. Patch it into a Render 3D node's camera input to render from it instead of the default view." },
         { "Light", "A light source for the 3D scene. Render 3D takes up to 3 lights - patch more in and only the first 3 are used." },
         { "HDRI", "Loads an equirectangular .hdr or .exr image and patches into Render 3D's env input, replacing the fixed sky gradient with a real image for the background and for reflections/ambient light. Rotation turns the image around Y; intensity scales it independently of Render 3D's own env intensity. Reflections use the image's own mip chain scaled by roughness as a cheap stand-in for a proper blurred prefilter - very glossy metal will read a little softer than a full IBL renderer would give it. Use this node rather than Image Source for HDRIs - Image Source clamps to 8-bit sRGB, which throws away exactly the above-1.0 highlight range an HDRI needs." },
         { "Render 3D", "Rasterizes the geometry/camera/light/material graph into an image. Antialiasing is reduced automatically at large output sizes to stay within GPU limits. Scenes over ~2 million triangles get noticeably heavier to render. An HDRI node patched into the env input replaces the procedural sky gradient for background, reflections and ambient light." },
         { "Model 3D", "Loads a 3D model file - obj, ply, stl, usd or usdz." },
         { "Null 3D", "A pass-through node for geometry: its output is exactly its input mesh, unchanged. Useful as a stable junction point to branch geometry to several destinations." },
         { "Group 3D", "Carries up to eight geometry inputs through one pin into Render 3D (or into another Group 3D), so a scene is not limited to Render 3D's four geometry pins. Nothing is merged: each input keeps its own material, texture, mapping and transform. It only means something to Render 3D - use Join Geometry to combine meshes into one." },
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
         { "Select", "Marks a subset of faces - by index range, position along an axis, normal direction, random chance, or within a radius - for downstream ops (Delete Selected / Transform Selected / Extrude Selected) to act on. Invert flips the selection, 'add to selection' unions with whatever was already selected." },
         { "Delete Selected", "Deletes the faces marked by an upstream Select op - or, with 'keep selected instead', deletes everything else and keeps only the selection." },
         { "Transform Selected", "Moves, rotates and scales only the faces marked by an upstream Select op, optionally sliding them along their own normals instead of a fixed direction." },
         { "Extrude Selected", "Extrudes only the faces marked by an upstream Select op along their own normals, by Distance, with Inset." },

         // ---------------- Point distribution / vertex (3D) ----------------
         { "Distribute Points on Faces", "Scatters points across the input mesh's surface, area-weighted so large faces get proportionally more - even coverage, unlike Mesh to Points, which walks the vertex list and therefore clumps wherever the topology does (a UV Sphere's poles). Density sets how many; 'method' chooses plain Random or Poisson (blue-noise, honouring Min Distance so no two points crowd together)." },
         { "Distribute Points in Grid", "Generates a flat rectangular lattice of points from nothing - no geometry input. Count X/Y and Spacing X/Y set the grid, Jitter randomises each point off its cell centre. Ordering matches Image to Points exactly (row-major, cell-centre UV), so a grid and an Image to Points at the same counts line up point-for-point." },
         { "Points to Vertices", "Point cloud in, mesh out: one vertex per point, with no edges or faces. This is the bridge that lets a cloud (Particle System, a Distribute node, Image to Points) re-enter the mesh operators that only know how to walk a Mesh. 'alive only' skips dead particles." },
         { "Merge by Distance", "Welds vertices closer together than Threshold into one - the cleanup pass that makes a scatter or conversion result usable, closing seams a conversion left behind. Pushed to a large radius it collapses the geometry outright, which is sometimes the point." },
         { "Set Vertex Color", "Writes per-element colour - vertex colours on a mesh, per-particle colour on a point cloud, whichever the input actually carries. 'source' picks where the colour comes from: a Flat colour, a two-colour Ramp, Random per element, a patched texture sampled at each element's UV, or a patched Palette node's swatches (offset steps which swatch starts the run). Nothing downstream has to know which - Render 3D reads whatever ended up there." },

         // ---------------- Audio-reactive geometry / texture ----------------
         { "Audio Displacement", "Deforms a geometry input using live audio patched into its second pin - five modes, each a different physical reading of the same signal: Normal Waveform pushes each vertex along its own normal by the oscilloscope trace, Spectral Bands maps the FFT across the X/U axis, Acoustic Ripples sends concentric waves out from the centre, Cymatics (Chladni) solves the standing-wave modal pattern for mode numbers m/n (the classic salt-on-a-plate figures), and Directional Axis pushes along a fixed local X/Y/Z. Attack/decay shape how fast the surface answers; Subdivide adds the resolution a displacement needs to be visible at all." },
         { "Audio Ribbon", "Turns audio into a 3D mesh in the most literal way available: the waveform extruded as a flat ribbon through space, Segments points wide, with width/height scale and its own transform. Smoothing damps the trace between frames so it reads as a surface rather than a flicker. Takes audio on its own pin - it is a geometry source, so it goes into Render 3D, not into Audio Out." },
         { "Audio Texture", "Renders incoming audio as an image, so every image node in the app becomes an audio visualiser: Waveform draws the oscilloscope trace, Spectrum the FFT magnitude across a chosen window size. gain scales the drawn amplitude, smoothing damps it between frames. Distinct from Audio Analyze, which produces numbers for modulation - this produces pixels." },

         // ---------------- Audio routing (Utility) ----------------
         { "Audio Out", "The terminal node for audio, the counterpart of Output for images: whatever is patched in is summed into the selected output device. It has no processing of its own and no output pin. Two cables into one Audio Out is refused on purpose - route them through a Mixer instead, which is the only node in the app that sums audio. With several Audio Outs, plugin delay is compensated between them so they land together; press live to opt this one out when you are playing through it and want the lowest delay." },
         { "Mixer", "The only node that sums audio - every other audio input pin takes exactly one cable, so any time two signals have to meet, they meet here. Four to eight slots, each with its own gain, pan, mute and solo, summed to one output." },
         { "Splitter", "The explicit fan-out point for audio: an ordinary audio output feeds exactly one destination, so sending one signal to several places needs this node. Its own output is the one exempt from that rule. On the audio thread it is a plain copy - it exists for the graph-level visibility and the fan-out cap, not because copying is otherwise needed." },
         { "Gain", "A single gain stage in dB, with a level meter - the simplest audio utility there is. Reach for it to trim a source before a Mixer, or to set up a clean level for something that has no output volume of its own." },
         { "Blend Audio", "A two-input crossfader: blend 0 is A only, 1 is B only, 0.5 an equal-power centre. Not a second place to sum two signals (that's Mixer) - it picks a point between them, and its blend knob has a modulation pin, so an LFO or Macro can sweep the crossfade." },
         { "Audio Meter", "A stereo level meter: separate L and R bars on a shared -60 to +3 dBFS scale, each showing RMS (solid) inside peak (faint), a peak-hold line, and the channel's highest peak as a number on top, which turns red once that channel has reached 0 dBFS. Audio passes through unchanged. It measures whatever its input is patched to even with its output left unconnected, so it can hang off any cable as a tap. Click the meter to clear the peak numbers, holds and clip." },

         // ---------------- Synths ----------------
         { "Granular", "A granular synthesizer: it dissects a loaded sample (or audio recorded through its own 'record in' pin) into micro-grains and re-emits them - grain length and density set the texture, position/scan set where in the sample the grain cloud reads from, and freeze pins it there. The four random controls (position spray, length, pitch, pan) plus reverse probability are what turn a sample into a cloud rather than a stutter. Drag the waveform's edge handles to trim start/end; the grain dots on the display are the live emission." },
         { "PaulStretch", "Extreme time-stretching - the PaulStretch algorithm, built for stretch factors where a few seconds becomes minutes of evolving ambient texture rather than a recognisable sample. Large-window FFT with phase randomisation is what keeps it smooth instead of stuttery: at phase randomisation 1 the result is pure texture, lower values keep more of the original's transient character. Also offers spectral pitch shift, frequency shift and unison detune. Load a file or record into its 'record in' pin." },
         { "Spectral Synth", "Image-to-sound additive resynthesis, in the MetaSynth tradition: any image, glyph, drawing or live video patched in is read as a spectrogram and rebuilt out of 64-256 sine partials. X is time (the scan axis), Y is frequency (linear or logarithmic, between min/max Hz), brightness is amplitude, and in Hue-Pan colour mode the hue becomes stereo position. The scan can sync to the transport at a note division, free-run at its own speed, or be scrubbed by hand with position - and with a note cable connected it transposes polyphonically." },
         { "Wave Terrain", "Wave terrain synthesis: an image patched in is read as a height field z = T(u,v), and the oscillator's waveform is whatever a closed orbit traced across that terrain reads back. Orbit type/centre/radius/ratio decide the path, so moving the orbit is a timbre control rather than a pitch one - and because the terrain is an ordinary image input, a Noise, Draw, Video or Reaction Diffusion node upstream reshapes the sound live. Each cycle is band-limited through a 10-level mip pyramid, so it stays alias-free however sharp the source image is." },
         { "Metallic", "A physical-modelling synth for struck and plucked metal - bells, gongs, bars, plates, chimes, tines and strings - via extended Karplus-Strong waveguides and inharmonic modal resonator banks. 'material' picks the model, transient how hard it is struck, decay how long it rings, stiffness how inharmonic the partials are (which is what separates a bell from a string). Polyphonic with a note cable connected, free-running at 'frequency' when unpatched." },
         { "Field Synth", "A polyphonic synthesizer whose voice is a Field kernel you write yourself, compiled to a sample-domain register machine - the note-driven counterpart of Field Effect. `state` declares per-voice memory, `param` exposes a modulatable knob, and the kernel runs once per sample per voice, on the audio thread. maxVoices caps the polyphony; presets are complete starting kernels." },

         // ---------------- Macros ----------------
         // One family, one shape: a hand control whose only output is a
         // modulator, meant to be dragged onto other nodes' modulation pins.
         { "Macro Slider", "A named fader exposed as a modulator - drag its output onto any slider's modulation pin to drive that parameter by hand. The plain 0..1 member of the Macro family; use Macro Knob when you want a response curve and invert as well." },
         { "Macro Bipolar Knob", "A centre-detent knob running -1 to +1, exposed as a modulator - the right control for anything that has a natural middle (pan, detune, tilt). Its 0..1 output puts the detent at exactly 0.5, which is also where a bipolar modulation binding reads as 'no modulation'." },
         { "Macro Toggle", "A latching on/off switch exposed as a modulator: output is exactly 0.0 or 1.0, nothing in between. Useful for driving a mode/enable parameter, or for gating another modulator through a Math node's multiply." },
         { "Macro Trigger", "A momentary pad: output is 1.0 while pressed and 0.0 the moment it is released, with a visible flash on the press. Reach for it to fire something that responds to an edge - a Feedback burst, an envelope, a Resynthesize step - rather than to hold a value." },
         { "Macro NumBox", "A number box with drag-to-scrub and direct typing, over a range and step you set yourself - for when the value matters as a number (a count, a Hz figure, a bar length) and a 0..1 slider would be the wrong instrument." },
         { "Macro Radio Selector", "A multiple-choice selector emitting one discrete step per option - the control to use for a dropdown-shaped parameter (a mode, a waveform, a preset index), where a continuous slider would land between valid values. 'count' sets how many options." },
         { "Macro Step Gate", "An 8-step gate that advances on the transport at rateBeats - a rhythm you draw rather than a curve. The output is the current step's on/off state, so it is the Macro family's answer to 'make this parameter pulse in time' without wiring an LFO and a Compare." },

         // ---------------- MIDI / conversion modulators ----------------
         { "MIDI CC", "Binds one physical control on a MIDI controller - a knob, fader or pad - and reports its position as a modulator. Press Learn and move the control; the node remembers that channel + controller number and polls only that binding. low/high remap the output range and invert flips it. Works with any class-compliant USB MIDI controller, since it only ever reads generic Control Change / Note On messages. You rarely need to add this node by hand: right-click any parameter, choose MIDI learn, and move a control - this node is created (or reused if that control already has one) and bound to the parameter over its full range. Only one MIDI learn runs at a time; starting another cancels the first, and Esc cancels." },
         { "MIDI Trigger", "The pad counterpart of MIDI CC: it fires a decaying 0..1 pulse whenever one specific MIDI note is hit, then sits at 0. A pad hit is an event rather than a position, so this spikes and decays over 'hold' seconds the way Audio Analyze's onset output does, instead of holding a live value. Velocity sensitivity scales the spike by how hard the pad was struck." },
         { "Note to CV", "Converts a note stream's pitch into a modulator, so which note is playing can drive any parameter - a filter cutoff, a warp amount, pan. It is a pitch tracker, not an envelope: it holds the last note's pitch on release rather than falling back toward 0 (that's Envelope's job). rangeLow/High set which note range maps onto the full 0..1 span, glide smooths the jump between notes." },
         { "Velocity to CV", "Converts how hard each note is played into a modulator (its 0..1 velocity), so touch can drive any parameter - brightness, pan, a filter opening on accents. It holds the last note's velocity through release. range low/high pick which velocities map onto the full 0..1 span (swap them to invert). The pitch counterpart is Note to CV." },
         { "CV Recorder", "Records any modulator patched into its input and plays it back. Press Rec to capture (live input passes through while recording), press Rec again to stop and it starts looping straight away. speed is the playback rate - 2x plays the take twice as fast - and low/high map the take onto an output range. Takes are timed in beats, so they follow the tempo and pause with the transport (up to 64 beats), and they are saved with the patch. With nothing patched in, it records its own in knob, so you can perform a gesture by hand." },
         { "Audio to CV", "Converts audio into a modulator through an amplitude follower - Peak or RMS detection with its own attack and release. The lightweight, single-output counterpart of Audio Analyze: reach for this when all you want is 'this parameter follows how loud that is', and for Audio Analyze when you want bands, onsets and a passthrough." },
         { "Invert", "Mirrors a modulator around the midpoint of a low/high window, so what was at the top of the range lands at the bottom. Deliberately not a flat 1-v, which is why it still does the right thing when fed something already outside 0..1 - an unclamped Math output, for instance." },
         { "Mod Depth", "Scales how much of a modulator's swing reaches its destination, without having to know anything about the destination. Because a modulation binding overrides the knob outright, 'depth' collapses the signal toward 0.5 rather than adding a fraction on top: at 0 the destination sits at its own mid-range and the modulator has no say, at 1 the source passes through unchanged. Negative depth inverts, so one knob covers how much and which direction." },

         // ---------------- Notes ----------------
         { "Note Switcher", "Cycles between up to four connected note inputs every N beats or seconds, or pins to one slot with 'manual' - the note-cable counterpart of Switcher (2D) and Switcher 3D. When the active slot changes, new note-ons come only from the new slot, while notes already held on the old one still get their note-offs passed through, so nothing hangs." },
         { "Note Strum", "Spreads the notes of a chord across time instead of firing them together - strumMs per voice, in ascending pitch order, with each note's original gate length preserved. One knob: at 0 it is a passthrough, and the further up it goes the more the chord reads as strummed rather than struck." },

         // ---------------- Source / capture ----------------
#if defined(_WIN32)
         { "Video In", "Captures a live camera as an image source - built-in, USB webcams and capture devices, through Media Foundation. 'mirror' flips it horizontally, which is usually what you want for a front-facing camera; 'resolution' picks the capture format. Windows will ask for camera permission the first time this node cooks, so it stays idle until it is actually in a patch." },
#elif defined(__linux__)
         { "Video In", "Captures a live camera as an image source via V4L2 - built-in, USB webcams and capture devices. 'mirror' flips it horizontally, which is usually what you want for a front-facing camera; 'resolution' picks the capture format. The device is opened the first time this node cooks, so it stays idle until it is actually in a patch." },
#else
         { "Video In", "Captures a live camera as an image source - the built-in camera, external USB webcams, and Continuity Camera (an iPhone used as a webcam). 'mirror' flips it horizontally, which is usually what you want for a front-facing camera; 'resolution' picks the capture format. macOS will prompt for camera permission the first time this node cooks, so it stays idle until it is actually in a patch." },
#endif
#if defined(_WIN32)
         { "Syphon In", "Receives real-time video from another Windows application over Spout2 - Resolume, OBS, TouchDesigner, MadMapper, Unreal, Unity - as a zero-copy shared GPU texture. Pick a sender from the list; it rescans periodically, so a sender that starts after this node does will appear." },
#elif !defined(__APPLE__)
         { "Syphon In", "Syphon/Spout texture sharing is not available on Linux, so this node has no sender to receive from and outputs a placeholder. Use Video In (V4L2) or a file source instead." },
#else
         { "Syphon In", "Receives real-time video from another macOS application over Syphon - Resolume, OBS, TouchDesigner, MadMapper, Unreal, Unity - as a zero-copy shared GPU texture (IOSurface). Pick a server from the list; it rescans periodically, so a server that starts after this node does will appear." },
#endif
         { "FieldPixel", "Runs a Field kernel once per output pixel, on the GPU - write your own image generator or effect in Field instead of GLSL. `state` declares persistent per-pixel memory that survives to the next frame (which is what makes reaction-diffusion and trail effects expressible here), `param` exposes a modulatable knob, and extra input/output image pins can be declared by the kernel. width/height set the output resolution; presets are complete starting kernels." },
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
         { "transform", "Translates, scales, rotates, and flips (horizontal/vertical) the whole image. Scale X/Y let you stretch non-uniformly on top of the overall Scale. Crop X/Y symmetrically crop the source in from each axis before the rest of the transform is applied." },
         { "invert", "Inverts every colour channel (alpha untouched) - a photographic negative. No parameters." },
         { "posterize", "Reduces the image to a fixed number of tonal Levels per channel, producing flat colour bands." },
         { "threshold", "Converts to pure black or white based on luminance, split at Threshold." },
         { "exposure", "Multiplies brightness by powers of two, like a camera's exposure stop (compare Levels' linear/gamma remap)." },
         { "bloom", "Isolates pixels above a brightness Threshold, blurs them outward by Radius, and adds the glow back at Intensity - classic HDR-style bloom." },
         { "diffuseglow", "Screens a blurred copy of the whole image back over itself, glowing everything rather than just bright spots (compare Bloom, which is threshold-based)." },
         { "glitch", "Six glitch algorithms behind one 'kind' dropdown: Slice Shift (blocky RGB-split rows), RGB Shift, Scanlines, Blocks (jittered tiles), Wave, and Datamosh (sliced/shuffled rows with colour smear). Amount/Detail control strength/scale, Speed animates it, Seed reseeds the randomness." },
         { "lensdistortion", "Simulates a camera lens: Barrel bows the image in or out, Chromatic separates the colour channels' distortion for fringing, Zoom scales the result." },
         { "displace", "Offsets each pixel using a second patched-in image's red/green channels as a displacement map, or a built-in animated wave if nothing is patched. Map Scale tiles the map." },
         { "liquify", "Flows pixels around using animated Perlin-style noise, like a liquid warp. Scale sets the flow's feature size, Speed animates it." },
         { "symmetry", "Mirrors the image about the X axis, Y axis, or both, from a chosen centre point. Flip additionally reverses the whole image before mirroring." },
         { "kaleidoscope", "Slices the image into angular Segments around a centre and mirrors each one, kaleidoscope-style. Rotation spins the pattern, Zoom scales it." },
         { "mirror tile", "Tiles the image and alternately mirrors each tile so the edges line up seamlessly, unlike a plain repeat. Tiles sets how many repeats." },
         { "chroma key", "Removes a chosen Key Colour (green/blue-screen style), matched in a brightness-independent colour space. Tolerance/Softness shape the edge, Spill Removal desaturates colour-cast fringes, 'show: Matte' previews the alpha instead of the keyed result." },
         { "luma key", "Keys out a brightness range (Low-High) instead of a colour, with Softness at the edges and Invert to flip which range is kept. 'show: Matte' previews the alpha." },
         { "crop", "Crops in from each edge (Left/Right/Top/Bottom). 'outside' chooses what happens to the cropped-away area: stays transparent, the kept region zooms to fill the frame, or gets replaced with a Fill colour." },
         { "emboss", "Turns edges into a relief/bump look by differencing the image against itself offset along Angle/Distance. 'style: Over colour' keeps the original colours and adds the emboss on top; 'Grey' replaces the image with a flat emboss." },
         { "normal map", "Derives a tangent-space normal map from the image's luminance treated as a height field, for use as a bump/normal input elsewhere. 'output' can show the raw Normals, the source Height, or the gradient's Slope instead." },
         { "convolve", "A user-editable 3x3 convolution kernel (k11-k33) - blur, sharpen, edge-detect and emboss are all just different numbers in this grid. Divisor and Bias adjust the result, Spread scales the sample distance, Mix blends with the original." },
         { "lookup", "Recolors using a second patched-in image as a 1D palette strip - this pixel's Luminance (or a chosen channel) indexes across it. Offset shifts the index, Mix blends with the original." },
         { "halftone", "Renders the image as a dot pattern like offset printing. Scale sets dot density, Angle rotates the dot grid, 'style: Colour' uses three angled CMY-style dot layers instead of one greyscale layer." },
         { "edge sobel", "Classic Sobel edge detection: outputs the gradient magnitude as greyscale edges. Invert flips black and white." },
         { "edge outline", "Detects edges via a luminance gradient and draws them as flat-Colour outlines at a chosen Thickness and Threshold, over the original image." },
         { "lut", "Grades the image through a second patched-in HALD/strip LUT image - an N x N-sliced colour cube encoded as a flat strip. LUT Size must match the LUT image's slice count, Mix blends with the original." },
         { "gradientmap", "Remaps luminance onto a two-colour gradient (Shadow to Highlight), Photoshop Gradient Map-style. Mix blends with the original colour." },
         { "color adjustments", "All-in-one grading chain: Brightness/Contrast, Levels, Colour Balance, HSL, Vibrance, Tone Shaper (lift/gamma/gain-style S-curve), Channel Mixer, then an optional Black & White stage - so a common grade doesn't need eight separate nodes wired in series." },
         { "outerglow", "Adds a soft glow of a chosen Colour around the image's alpha edge, blurred outward. Amount controls strength." },
         { "coloroverlay", "Flat-tints the image toward a chosen Colour at a given Opacity." },
         { "dropshadow", "Offsets a blurred copy of the image's alpha behind it as a shadow, in a chosen Colour, Offset X/Y and Opacity." },

         // ---------------- Alpha / opacity operators ----------------
         // The one filter family that edits an existing alpha channel rather
         // than producing a new one (that's Chroma Key / Luma Key's job).
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
      if (category == "Effects") return "Transforms one image into another. Its parameters (and the shape of its name) describe what it changes; see Menu > Help > Module reference for the effect families.";
      if (category == "Compositing") return "Combines images, or otherwise manages how they flow through the patch - including colour, mask, and feedback/loop nodes. See Menu > Help > Module reference for the full family.";
      if (category == "Modulators") return "Produces a changing number over time, not an image. Patch its output onto the small dot beside any slider to drive that parameter.";
      if (category == "3D") return "Part of the 3D geometry/render pipeline - geometry and point-cloud nodes feed into Render 3D via a Camera and Lights.";
      if (category == "Notes") return "Part of the note chain - takes note events in on its 'notes' pin and passes them out, changed. Feed a synth (Wavetable, Sampler) from the end of the chain.";
      if (category == "Synths") return "A sound source - it produces audio rather than an image. With a note cable connected it plays polyphonically; with nothing patched into its note pin it free-runs at its own frequency. Route its output into an audio effect chain and on to Audio Out.";
      if (category == "AudioEffects") return "Processes audio: one cable in, one cable out. Audio input pins take exactly one cable each - use a Mixer to sum signals and a Splitter to fan one out. See Menu > Help > Module reference for the effect families.";
      if (category == "Macros") return "A hand control whose only output is a modulator. Drag its output onto the small dot beside any slider to drive that parameter by hand, or onto several at once to make this one control the whole gesture.";
      if (category == "Utility") return "Utility node: audio routing, terminal/output nodes (shows, exports or records the final result), Syphon and OSC I/O.";
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
      return CategoryHelpText(gn.category);
   }

   void DrawSettingsWindow(bool* open);

   void DrawShortcutsWindow(bool* open)
   {
      // Every other floating dialog (Settings, Field editor, colour picker)
      // goes through PushElevatedPanelStyle - this one didn't, which is why
      // it was the one window still carrying a different backdrop/border/
      // rounding than the rest of the "material above the canvas" family.
      // Centering on first appearance matches Settings for the same reason
      // (see DrawSettingsWindow).
      ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
      ImGui::SetNextWindowSize(ImVec2(680, 520), ImGuiCond_FirstUseEver);
      PushElevatedPanelStyle(/*isChild=*/false);
      if (!ImGui::Begin("All Shortcuts", open))
      {
         ImGui::End();
         PopElevatedPanelStyle();
         return;
      }

      static char filterBuf[128] = "";
      ImGui::SetNextItemWidth(250);
      ImGui::InputTextWithHint("##filter", "Filter shortcuts...", filterBuf, sizeof(filterBuf));
      ImGui::SameLine();
      if (filterBuf[0] != '\0' && ImGui::SmallButton("Clear"))
         filterBuf[0] = '\0';

      std::string filter = filterBuf;
      for (char& c : filter)
         c = (char)tolower((unsigned char)c);

      struct ShortcutEntry {
         const char* category;
         const char* action;
         const char* key;
         const char* description;
      };

      static const ShortcutEntry kShortcuts[] = {
         // Patch & File
         { "File & Patch", "Settings...", MODKEY "+0", "Open appearance, canvas, audio, and performance settings" },
         { "File & Patch", "New Patch", MODKEY "+N", "Create a new empty patch" },
         { "File & Patch", "Open Patch", MODKEY "+O", "Open an existing .inf patch file" },
         { "File & Patch", "Save", MODKEY "+S", "Save current patch" },
         { "File & Patch", "Save As...", MODKEY "+Shift+S", "Save patch to a new file" },

         // Edit & Canvas
         { "Edit & Canvas", "Undo", MODKEY "+Z", "Undo last graph action" },
         { "Edit & Canvas", "Redo", MODKEY "+Shift+Z / Ctrl+Y", "Redo last undone action" },
         { "Edit & Canvas", "Cut / Copy", MODKEY "+C", "Copy selected nodes and internal connections" },
         { "Edit & Canvas", "Paste", MODKEY "+V", "Paste copied nodes with automatic offset" },
         { "Edit & Canvas", "Duplicate", MODKEY "+D / Shift+D", "Duplicate selected nodes in-place. Canvas only - with the timeline focused, this duplicates clips instead" },
         { "Edit & Canvas", "Delete", "Delete / Backspace / Shift+X", "Delete selected nodes, groups, or links" },
         { "Edit & Canvas", "Delete Cable", "X", "Delete selected cable/link only" },
         { "Edit & Canvas", "Select All", "Shift+A", "Select all nodes on the canvas" },
         { "Edit & Canvas", "UI Scale", MODKEY "+= / " MODKEY "+-", "Make the whole interface bigger or smaller in 0.1 steps (0.5x to 2x); same setting as Settings > UI Scale" },
         { "Edit & Canvas", "Walk Params", "Tab / Shift+Tab", "Click a node, then Tab loops through that node's parameters (Shift+Tab goes backwards). Digits type a value; Esc leaves" },
         { "Edit & Canvas", "Nudge Param", "Left / Right", "With a param focused by Tab: Left/Down lowers it, Right/Up raises it one step; Alt = x10" },
         { "Edit & Canvas", "Move Node", "Up / Down / Left / Right", "Click a node, then the arrow keys move it one grid step. With a param focused they nudge the value instead" },
         { "Edit & Canvas", "Next Node", "Shift+Up / Down / Left / Right", "Select the neighbouring node in that direction" },
         { "Edit & Canvas", "Zoom Into Node", "Shift+Enter", "Zoom the view into the selected node; Enter zooms back out to where you were" },
         { "Edit & Canvas", "Node Help", "H (again to close)", "Show the help for the selected node" },
         { "Edit & Canvas", "Bypass Selection", "B", "Toggle bypass (power off) on the selected nodes. Canvas only - with the timeline focused, B is the blade tool instead" },
         { "Edit & Canvas", "Group Selection", MODKEY "+G", "Wrap selected nodes in a group box" },
         { "Edit & Canvas", "Ungroup", MODKEY "+U / " MODKEY "+Shift+G", "Dissolve the selected group without deleting nodes" },
         { "Edit & Canvas", "Add Node", "Shift+N", "Open quick type-to-filter node picker" },
         { "Edit & Canvas", "Add Note / Comment", "/", "Drop a comment note under mouse pointer" },

         // Canvas & View
         { "Canvas & View", "Pan Canvas", "Drag Canvas", "Pan graph view" },
         { "Canvas & View", "Zoom", "Scroll Wheel", "Zoom in and out" },
         { "Canvas & View", "Rubber-band Select", "Shift + Drag", "Select multiple nodes in box" },
         { "Canvas & View", "Toggle Params", "Shift+H", "Show / hide parameter knobs & sliders" },
         { "Canvas & View", "Viewport Panel", "Shift+V", "Toggle viewport panel (or dock selected nodes)" },
         { "Canvas & View", "Modulation Matrix", "Shift+M", "Toggle docked modulation matrix" },
         { "Canvas & View", "Performance Matrix", "Shift+P", "Toggle docked performance matrix" },
         { "Canvas & View", "Arrangement Timeline", "Shift+T", "Toggle docked arrangement timeline" },
         { "Canvas & View", "Fit View to Content", "F / Shift+Y", "Frame the whole patch in the canvas view" },
         { "Canvas & View", "Pan Canvas (keys)", "W / A / S / D", "Hold to pan the canvas up / left / down / right. Not while a hovered audio keyboard node is using the letters" },

         // Transport & Audio
         { "Transport & Audio", "Play / Pause", "Space", "Start / pause timeline and animations" },
         { "Transport & Audio", "Toggle Audio Engine", "Shift+K", "Start / stop audio device. Canvas only - with the timeline focused, Shift+K adds an audio track instead" },

         // Arrangement Timeline - these fire only while the timeline panel
         // owns the keyboard (click inside it) and no text field is active;
         // the canvas's own Cmd+C/V/D/G and Delete stand down meanwhile.
         { "Arrangement Timeline", "Add Video Track", "Shift+J", "Add a new video track below the selected track. Every key in this section needs the timeline panel focused - click inside it first" },
         { "Arrangement Timeline", "Add Audio Track", "Shift+K", "Add a new audio track below the selected track (Shift+K toggles the audio engine when the canvas has focus)" },
         { "Arrangement Timeline", "Copy / Paste Clips", MODKEY "+C / V", "Copy the selected clips; paste at the playhead on the last-clicked clip's lane" },
         { "Arrangement Timeline", "Duplicate Clips", MODKEY "+D / Shift+D", "Copy the selected block right after itself (the same keys duplicate nodes when the canvas has focus)" },
         { "Arrangement Timeline", "Split at Playhead", MODKEY "+E", "Cut every selected clip the playhead passes through" },
         { "Arrangement Timeline", "Enable / Disable Clips", "0 / Keypad 0", "Mute the selected clips (they draw hatched) or bring them back" },
         { "Arrangement Timeline", "Blade Tool", "B", "Toggle the blade: click a clip to cut it (and its group) at the mouse; Esc turns it off (B bypasses nodes when the canvas has focus)" },
         { "Arrangement Timeline", "Rename", MODKEY "+R", "Rename selected track, group, clip, or multiple selected clips" },
         { "Arrangement Timeline", "Group / Ungroup Clips", MODKEY "+G / " MODKEY "+Shift+G", "Group merges whole groups and loose clips into one; Ungroup dissolves every group touched" },
         { "Arrangement Timeline", "Delete Clips", "Delete / Backspace", "Delete the selected clips" },
         { "Arrangement Timeline", "Add Marker", "M", "Drop a marker at the playhead, on the snap grid" },
         { "Arrangement Timeline", "Previous / Next Marker", "Alt+Left / Right", "Jump the playhead to the previous or next marker" },
         { "Arrangement Timeline", "Nudge Clips / Step Playhead", "Left / Right", "Move the selected clips one grid step; with nothing selected, step the playhead" },
         { "Arrangement Timeline", "Playhead to Start", "Home", "Move the playhead to the start of the timeline" },
         { "Arrangement Timeline", "Playhead to End", "End", "Move the playhead to the end of the last clip" },
         { "Arrangement Timeline", "Zoom Timeline", MODKEY " + Scroll Wheel", "Zoom around the mouse (trackpad pinch works too)" },
      };

      const char* lastCat = nullptr;
      bool inTable = false;

      for (const auto& s : kShortcuts)
      {
         if (!filter.empty())
         {
            std::string text = std::string(s.category) + " " + s.action + " " + s.key + " " + s.description;
            for (char& c : text)
               c = (char)tolower((unsigned char)c);
            if (text.find(filter) == std::string::npos)
               continue;
         }

         if (lastCat == nullptr || strcmp(lastCat, s.category) != 0)
         {
            if (inTable)
            {
               ImGui::EndTable();
               ImGui::Spacing();
               inTable = false;
            }
            lastCat = s.category;
            ImGui::SeparatorText(s.category);
         }

         if (!inTable)
         {
            char tableId[64];
            snprintf(tableId, sizeof(tableId), "tbl_%s", s.category);
            if (ImGui::BeginTable(tableId, 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
            {
               ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 150.0f);
               ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed, 180.0f);
               ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
               ImGui::TableHeadersRow();
               inTable = true;
            }
         }

         if (inTable)
         {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(s.action);

            ImGui::TableSetColumnIndex(1);
            // Pale sky-blue reads fine on the dark table row background but
            // washes out against light mode's light row background - branch
            // it the same way every other themed accent-text spot in the
            // app does rather than leave it a fixed dark-mode-only colour.
            ImGui::PushStyleColor(ImGuiCol_Text, IsThemeLight() ? ImVec4(0.05f, 0.35f, 0.68f, 1.0f)
                                                                : ImVec4(0.45f, 0.82f, 1.0f, 1.0f));
            ImGui::TextUnformatted(s.key);
            ImGui::PopStyleColor();

            ImGui::TableSetColumnIndex(2);
            ImGui::TextWrapped("%s", s.description);
         }
      }

      if (inTable)
         ImGui::EndTable();

      ImGui::End();
      PopElevatedPanelStyle();
   }

   void DrawHelpWindow(bool* open)
   {
      // Same fix as DrawShortcutsWindow: this dialog was the other one
      // outside the PushElevatedPanelStyle treatment every other floating
      // window (Settings, Field editor, colour picker, All Shortcuts) gets,
      // which is why it kept rendering with the wrong backdrop/border in
      // light mode instead of the theme's actual panel colour.
      ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
      ImGui::SetNextWindowSize(ImVec2(720, 620), ImGuiCond_FirstUseEver);
      PushElevatedPanelStyle(/*isChild=*/false);
      if (!ImGui::Begin("Infinite - help & module reference", open))
      {
         ImGui::End();
         PopElevatedPanelStyle();
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
               { "Add a node", "Right-click or double-click the canvas, then type to filter - or press Shift+N anywhere (Shift+N again closes it)" },
               { "Note on the canvas", "Press / and start typing. Double-click an existing note to edit it." },
               { "Connect", "Drag from a node's 'out' dot to another node's input dot" },
               { "Modulate a parameter", "Drag a modulator's 'out' onto the small dot beside any slider" },
               { "Colour from a photo", "Add a Palette node, give it a reference image, then drag its 'out' onto the square dot beside any colour swatch. Each new cable takes the next swatch; click a bound swatch to step it." },
               { "Type an exact value", "Double-click a slider" },
               { "Pan the canvas", "Drag empty canvas" },
               { "Rubber-band select", "Shift + drag" },
               { "Duplicate", MODKEY "+C / " MODKEY "+V, or Shift+D / " MODKEY "+D to duplicate in place" },
               { "Select several", "Shift + drag a box around them, or Shift-click (or Ctrl-click) each one to add it. Then move, duplicate or delete as a group." },
               { "Delete", "Select, then Delete, Backspace or Shift+X" },
               { "Delete a cable", "Click the cable, then press X" },
               { "Viewport panel", "Shift+V toggles panel (or docks/undocks selected nodes)" },
               { "Hide / show params", "Shift+H - the selected nodes, or every node when nothing is selected" },
               { "Modulation matrix", "Shift+M toggles the docked matrix of every active binding" },
               { "Audio engine on / off", "Shift+K, same as the top bar's Start/Stop Audio" },
               { "Arrangement timeline", "Shift+T toggles the docked timeline. Once it has focus its own keys take over - see 'Arrangement timeline' below, or Menu > Help > Shortcuts for the full list." },
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

      if (ImGui::CollapsingHeader("Arrangement timeline", ImGuiTreeNodeFlags_DefaultOpen))
      {
         ImGui::TextWrapped(
            "Shift+T opens a timeline docked beside the canvas. It does not replace the "
            "patch - it schedules it. A track ('lane') is video or audio, and every clip "
            "on it points at a node that already exists in your graph; the clip decides "
            "WHEN that node is heard or seen, not what it does.");
         ImGui::Spacing();
         ImGui::TextWrapped("To build an arrangement:");
         ImGui::Indent();
         ImGui::Bullet(); ImGui::TextWrapped("Shift+J adds a video track, Shift+K an audio track (with the timeline focused).");
         ImGui::Bullet(); ImGui::TextWrapped("Drag an audio, video or image file straight onto a track - it spawns the right source node on the canvas and makes a clip for it in one step.");
         ImGui::Bullet(); ImGui::TextWrapped("Or drag empty track space to draw a clip, then assign a source node to it from the inspector.");
         ImGui::Bullet(); ImGui::TextWrapped("Double-click a clip, track or group header to open its inspector (double-click again to close it).");
         ImGui::Unindent();
         ImGui::Spacing();
         ImGui::TextWrapped(
            "Positions are in bars and beats, never seconds, so the whole arrangement "
            "retimes with the top bar's BPM. Audio clips from a file can follow project "
            "tempo: switch Sync on in the inspector and the clip is time-stretched by "
            "tempo / sample BPM, live, as you change the tempo. Switch it off and the clip "
            "plays at its native speed instead.");
         ImGui::Spacing();
         ImGui::TextWrapped(
            "Video tracks composite back to front, each clip with its own blend mode, "
            "opacity and brightness/contrast/saturation grade. Audio clips carry gain, "
            "pan, pitch and fades, summed per track. A clip's waveform is drawn from what "
            "was actually played, so a clip you have not played yet shows a flat line "
            "until the playhead has crossed it once.");
         ImGui::Spacing();
         ImGui::TextWrapped(
            "Two clips on different tracks should not share one source node: there is one "
            "playback position per node, so they cannot be retriggered independently. The "
            "inspector warns you when that happens, with a Make Unique button that gives the "
            "clip its own copy of the node - same inputs, same modulations, its own position.");
         ImGui::Spacing();
         ImGui::TextWrapped(
            "Right-click a track or group header to render or export just that track or "
            "group; the full patch export is still in the top bar.");
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
            "in. Reaction Diffusion is the other pre-wired feedback node: it needs "
            "no input at all and simulates a chemical system frame over frame.");
      }

      if (ImGui::CollapsingHeader("Module reference"))
      {
         struct Entry { const char* name; const char* text; };
         struct Group { const char* category; std::vector<Entry> entries; };
         static const std::vector<Group> groups = {
            { "Source", {
#if defined(_WIN32)
               { "Image Source", "Loads a still image. Opens the native file picker and decodes anything Windows Imaging Component can read - PNG, JPEG, TIFF, BMP, GIF, HEIF and the camera RAW formats WIC has a codec for." },
#elif defined(__linux__)
               { "Image Source", "Loads a still image. Opens the native file picker and decodes PNG, JPEG, BMP, GIF, TGA and HDR. Platform image codecs vary on Linux, so HEIC and camera RAW are not decoded here - convert them first." },
#else
               { "Image Source", "Loads a still image. Opens the native file picker and decodes anything macOS can read - PNG, JPEG, TIFF, HEIC, RAW and more." },
#endif
               { "Video", "Plays a video file. Position follows the transport, so it pauses with everything else. Loop and speed (including reverse) are available. Also outputs the clip's own audio track, if it has one, on the same transport-driven clock as the picture (audioEnabled/volume)." },
               { "Shape", "Twenty vector primitives - circle, ellipse, rectangle, rounded rect, triangle, polygon, star, ring, cross, line, hexagon, heart, arrow, crescent, gear, superellipse, pie, teardrop, chevron and blob - with fill, stroke, feather and background. Each one is also directly spawnable as its own named node." },
               { "Noise", "Procedural noise: value, fBm, ridged, Voronoi, Worley edges and white. Domain warping, octaves and colour mapping included." },
               { "Draw", "Paint straight onto the node preview. Six procedural brushes, eraser, spacing and jitter. Patch an image in to paint over it. Strokes can be recorded and replayed as an animation." },
               { "Formula", "A live GLSL shader. Pick a preset or press 'Edit GLSL...' to write your own; four knobs (uA-uD) are exposed for modulation." },
               { "Texture", "Blender-standard procedural textures: Voronoi, Brick, Magic, Wave and Musgrave, each with its own parameter block." },
               { "Text", "Renders text using any font installed on the system, with size, colour, tracking, alignment and position." },
               { "Ramp", "A linear, radial, angular or diamond gradient with its own stops - the usual starting point for a mask or a Gradient Map source." },
               { "Slideshow", "Steps through a folder of images on a beat interval, with an optional crossfade." },
#if defined(_WIN32)
               { "Video In", "Live camera or capture input through Media Foundation - pick a device and a resolution from the node. Frames arrive on the device's own clock, so this one source does not pause with the transport." },
#elif defined(__linux__)
               { "Video In", "Live camera or capture input through V4L2 (/dev/video*) - pick a device and a resolution from the node. Frames arrive on the device's own clock, so this one source does not pause with the transport." },
#else
               { "Video In", "Live camera or capture input through AVFoundation - pick a device and a resolution from the node. macOS asks for camera permission the first time. Frames arrive on the device's own clock, so this one source does not pause with the transport." },
#endif
               { "Audio Texture", "Turns live audio into an image - scrolling spectrogram, waveform or level field - so any image effect downstream becomes an audio visualiser." },
               { "FieldPixel", "An image generated by a Field kernel you write yourself: the per-pixel counterpart of Field Effect, compiled rather than interpreted." },
            } },
            { "3D", {
               { "Render 3D", "Rasterizes the geometry / camera / light / material graph into an image, with shadow mapping, ambient occlusion and HDR environment reflections (patch an HDRI node into its env input to replace the procedural sky). Antialiasing steps down automatically at large output sizes to stay inside GPU limits." },
               { "Camera & Light", "Camera is a 3D viewpoint patched into Render 3D's camera input. Light is a scene light - Render 3D reads up to three, and ignores any beyond that." },
               { "Primitives & Mesh", "Geometry covers 24 primitives (plane, cube, sphere, icosphere, torus, cylinder, cone, torus knot, capsule, tube, pyramid, prism, helix, supershape, the platonic solids, rounded cube, Mobius strip, Klein bottle, gear, star, disc, arrow), each also spawnable by name. Plus Model 3D (obj, ply, stl, usd, usdz), Text 3D, Curve, Ocean and Metaballs." },
               { "Procedural Operations", "Transform, Array, Subdivide, Solidify, Extrude, Wireframe, Triangulate, Normals, Explode, Twist, Smooth, Mirror and Screw - plus Select and the ops that act only on its marked faces (Delete / Transform / Extrude Selected), Join Geometry's booleans (Union, Intersect, Difference), Displacement, Wrap and Merge by Distance." },
               { "Point Distribution & Instancing", "Mesh to Points / Edges / Faces, Distribute Points on Faces (area-weighted, random or Poisson), Distribute Points in Grid, Image to Points, Points to Vertices, Set Vertex Color, and Instance on Points to stamp a shape at every point in one instanced call." },
               { "Audio Displacement", "Deforms 3D vertex geometries in real-time driven by live audio frequencies or RMS waveforms." },
               { "Particle System & Cloth", "Particle System is a 3D emitter with gravity, turbulence, collision planes and spring-mass dynamics. Cloth is a pinned mass-spring sheet with wind and collision." },
               { "Material, Mapping & HDRI", "Material is the shading block Render 3D reads per slot (base colour, metal/rough, emission, texture inputs). Mapping controls how a texture is projected onto the geometry. HDRI supplies the environment map for reflections and the background." },
               { "Field Primitive & Field Modifier", "Signed-distance geometry written as Field kernels and combined/warped by modifiers, then meshed - the procedural counterpart to the fixed primitives." },
            } },
            { "Compositing", {
               { "Transform", "Translate, scale, rotate, flip horizontal and flip vertical." },
               { "Blend", "Two inputs and 32 blend modes - the full Normal / Multiply / Screen / Overlay / Hue / Saturation / Colour / Luminosity set, plus Erase." },
               { "Layer Stack", "Four inputs stacked bottom-up: A is the base, D sits on top. Each layer has its own blend mode and opacity, and dragging a layer header reorders the whole layer." },
               { "Switcher", "Cycles between its connected inputs every N beats or seconds, with an optional crossfade. Can be pinned to one input with 'manual'." },
               { "Fit", "Resamples an input to a chosen resolution. Fit letterboxes, Fill crops, Stretch ignores aspect, Native passes through. Offset X/Y slides the result in output pixels (+ right, + up). Use it to make differently-sized sources composite predictably." },
               { "Drop Shadow / Outer Glow / Colour Overlay", "Layer-effect style filters." },
               { "Alpha operators", "The one family that edits an existing alpha channel rather than making a new one: Show Alpha (view the matte), Opacity, Set Alpha (take alpha from a second image's luminance), Alpha Invert, Alpha from Luma, Alpha Levels (choke or spread a soft edge) and Premultiply / Unpremultiply." },
               { "Basic", "Exposure, invert, posterize and threshold. Brightness/contrast, levels, HSL, colour balance and channel mixer no longer exist as standalone nodes - they are all sections of Color Adjustments now." },
               { "Curves", "Per-channel spline curve editor - drag control points on Red/Green/Blue/Luminance, Photoshop Curves-style." },
               { "LUT", "Applies a lookup-table image patched into the second input." },
               { "Gradient Map", "Remaps luminance onto a two-colour gradient." },
               { "Color Adjustments", "All-in-one grading chain - brightness/contrast, levels, colour balance, HSL, vibrance, tone shaper, channel mixer and an optional black & white stage - so a common grade doesn't need eight nodes wired in series." },
               { "Color Ramp", "Recolors any 0-1 grayscale input through user-authored stops, up to 32 of them, with linear or constant interpolation. Unlike Gradient Map, it has no shape of its own - the shape comes from upstream." },
               { "Predictive Coloring", "Learns what 'graded' footage looks like for you and grades incoming frames toward it. Press Learn on already-graded footage to build a target profile (or leave self normalize on to auto-level/contrast without one); mix blends the grade in. Wander makes the grade gently drift among looks the profile has actually seen instead of solving to the exact same answer every frame." },
#if defined(_WIN32)
               { "Remove Background", "On-device segmentation with no network access and no API key: Windows has no Vision equivalent, so a small model (u2netp) ships with the build and runs through ONNX Runtime on the DirectML GPU provider, falling back to CPU when DirectML does not register - the node's header line says which. Both Subject and Person modes use the same model. Segmentation is expensive, so the mask is computed on this interval rather than every frame." },
#elif defined(__linux__)
               { "Remove Background", "On-device segmentation with no network access and no API key: a small model (u2netp) ships with the build and runs through ONNX Runtime on the CPU provider. Both Subject and Person modes use the same model. Segmentation is expensive, and CPU inference more so, so raise the interval for video rather than computing a mask every frame." },
#else
               { "Remove Background", "On-device segmentation via Apple Vision - no model download, no network, no key. Subject mode (any salient foreground) needs macOS 14, Person mode macOS 12; the node says so when the OS is too old. Segmentation is expensive, so the mask is computed on this interval rather than every frame." },
#endif
               { "Feedback", "Outputs the previous frame. Nothing visible on its own - it is the delay that makes a loop legal. See 'Using Feedback' above." },
               { "Trails", "A pre-wired feedback loop: decaying accumulation with drift, zoom, rotation and hue rotation. Reach for this before wiring a loop by hand." },
               { "Reaction Diffusion", "Gray-Scott chemical simulation, six presets. Needs no input; patch one in and its luminance varies the feed rate so the pattern grows differently through light and dark." },
            } },
            { "Effects", {
               { "Blur family", "Gaussian, box, motion (angle + distance) and radial (with a centre point)." },
               { "Bloom / Diffuse Glow", "Bloom isolates pixels above a threshold and blooms them outward. Diffuse Glow screens a blurred copy back over the image." },
               { "Sharpen", "Unsharp mask - blurs a copy and adds back the difference." },
               { "Distortion", "Twirl, pinch/punch, ripple, lens distortion (with chromatic aberration), displace and liquify. Position-dependent effects have Centre X/Y." },
               { "Glitch family", "Five kinds: the original combined glitch, RGB shift, scanlines, blocks, wave and datamosh." },
               { "Symmetry", "Symmetry (mirror about X, Y or both), Kaleidoscope (segment count, rotation, zoom) and Mirror Tile." },
               { "Stylise", "Halftone (mono or CMY-style colour), Sobel edge detection and Edge Outline." },
               { "Pixelate / Noise / Vignette", "Block pixelation, additive grain and a vignette with its own centre." },
               { "Resynthesize", "Each generation reads the previous one, so the image drifts away from the source. The XY pad blends four named mutation effects assigned to its corners; Randomise re-rolls which four. The orb's path can be recorded, looped and replayed in time." },
            } },
            { "Modulators", {
               { "LFO", "Sine, triangle, saw up/down, square and sample-and-hold. Rate in beats, plus phase and an output range." },
               { "Random", "A new random value every N beats, with adjustable smoothing between steps. Deterministic, so rewinding replays the same sequence." },
               { "Pattern", "A 16-step sequencer with a drag-paint bar grid. Choose how many steps to use, or fit them to the time signature, and it loops through them one step every N beats. Optional glide and bipolar display." },
               { "Math", "Combines two modulators - add, subtract, multiply, divide, min, max, average, difference - with gain and offset. Unpatched inputs fall back to a constant." },
               { "Compare", "Outputs 1 when the comparison holds, 0 otherwise." },
               { "Range to Range", "Remaps one modulator's input range onto a different output range." },
               { "Smoothing", "An exponential moving average over another modulator, to damp jitter." },
               { "CV Recorder", "Records any patched modulator - hit Rec, then it loops the take back with adjustable speed and low/high range." },
               { "Velocity to CV", "Turns note velocity into a 0..1 modulator with an adjustable velocity range." },
               { "Envelope", "Shapes an incoming modulator with an ADSR contour, gated by it crossing threshold, instead of generating its own trigger." },
               { "Invert", "Mirrors a modulator around a low/high pivot. Defaults to 0..1 for a classic 1-v flip; set low/high to match an unclamped source to mirror it correctly." },
               { "Mod Curve", "Remaps a modulator through a draggable transfer curve - an S-curve, staircase, or exponential response, all things a slider can't express." },
               { "Drift", "Learns where you leave a knob and how you move it, then continues, settles and wanders like you. Shift-drag a driven knob to correct it." },
               { "Moves", "Your Moves macro faders: few faders drive many parameters based on learned co-movement PCA." },
               { "Predictive Modulator", "Learns a small dynamical model of a single patched-in modulator, then free-runs it on its own once Learn stops." },
            } },
            { "Macros", {
               { "Macro Controls", "Macro Knob, Macro Slider, Macro Bipolar Knob, Macro XY, Macro Toggle, Macro Trigger, Macro NumBox, Macro Radio Selector, and Macro Step Gate - unified live performance controls surfaced in the Performance Matrix." },
            } },
            { "Utility", {
               { "Output", "Terminal node. Shows the final image, exports a PNG, and records an H.264 .mov at a chosen frame rate. Recording captures the cooked output, so what you see is what is written." },
#if defined(_WIN32)
               { "Syphon Out", "Broadcasts video, 3D renders, or visual shaders to other Windows applications in real-time via Spout, zero-copy GPU texture sharing." },
#elif !defined(__APPLE__)
               { "Syphon Out", "Syphon/Spout texture sharing is not available on Linux." },
#else
               { "Syphon Out", "Broadcasts video, 3D renders, or visual shaders to other macOS applications in real-time via zero-copy GPU texture sharing." },
#endif
#if defined(_WIN32)
               { "Syphon In", "Receives a video texture published by another Windows application over Spout, zero-copy on the GPU. Pick a publisher from the node's list." },
#elif !defined(__APPLE__)
               { "Syphon In", "Syphon/Spout texture sharing is not available on Linux, so this node has no sources to receive from." },
#else
               { "Syphon In", "Receives a video texture published by another macOS application over Syphon, zero-copy on the GPU. Pick a publisher from the node's list." },
#endif
               { "Audio In", "Live input from an audio device - the input side of the same engine Audio Out feeds. Choose the device in Settings > Audio." },
               { "Field Graph", "Plots any Field kernel's output as a curve or surface, so you can see what a kernel does before patching it into Field Effect, Field Synth or FieldPixel." },
               { "Projection", "Warp, corner-pin and perspective-correct an image for projectors, flat walls, or curved screens, with built-in alignment test patterns and custom resolution target." },
               { "OSC Receive", "Listens on a UDP port for Open Sound Control messages matching an address pattern, and reports the last received value as a modulator (remapped through low/high). Behaves like LFO/Random - patch its output onto any slider's modulation pin." },
               { "OSC Send", "Sends its patched modulator input as an Open Sound Control message (address + float) to a host:port over UDP, on change (past an epsilon) or at least every interval - the one node in the patch with no output of its own." },
               { "Audio Out, Gain, Mixer, Splitter, Blend Audio", "The audio routing family. Audio Out is the terminal node. Every audio input pin takes exactly one cable, so Mixer is the only node that sums (4-8 slots, each with gain/pan/mute/solo) and Splitter is the only fan-out point. Gain is a single dB stage with a meter; Blend Audio is an equal-power crossfade between two inputs." },
            } },
            { "Notes", {
               { "MIDI Notes", "External hardware MIDI controller input, note record, and polyphonic voice dispatch. Note cables carry pitch/velocity/gate, not audio - they end at a synth." },
               { "Keyboard", "An interactive on-screen piano plus laptop-keyboard typing (Musical Typing layout) - the hardware-free way to play or test a patch." },
               { "Arpeggiator", "Tempo-synced arpeggiation patterns (Up, Down, Up/Down, Random, Chord, As-Played), octave span, and gate length." },
               { "Quantizer & Chorder", "Quantizer snaps incoming notes to a scale and mode (the standard modes through Chromatic). Chorder builds a chord from each single note it is given." },
               { "Bouncing Balls", "Physics-based gravity bounce note generator (up to 12 balls, with speed, size, and range controls) creating organic rhythmic polyrhythms as balls hit walls." },
               { "Note Transpose, Pitch Bend, Velocity Curve", "Pitch shifting, interval offset, pitch wheel modulation, and non-linear velocity mapping curves." },
               { "Gate, Humanizer, Glide", "Note gate length shaping, timing/velocity jitter humanization, and portamento glide." },
               { "Predictive Notes, Predictive Quantize, Predictive Velocity", "Predictive Notes learns a played phrase as a Markov model and plays on in that style. Predictive Quantize learns the actual spacing between your onsets and pulls future timing toward it - a groove template, not Quantizer's fixed grid. Predictive Velocity learns the shape of your own dynamic range and remaps future velocities onto it, instead of Velocity Curve's one fixed exponent." },
               { "Predictive Rhythm", "Single note input, same shape as Predictive Notes, but pitch is learned as an interval from a 'root' note rather than an absolute pitch. Root auto-detects to the most common learned note and is then a plain param - the pattern's relPitch is fixed by what was learned, but changing root after the fact transposes playback live without relearning, since resolution happens at play time against whatever root is currently selected." },
               { "Note Stack", "Polyphonic chord generator, harmony generator, and interval stacking." },
            } },
            { "Synths", {
               { "Wavetable", "Multi-voice polyphonic wavetable oscillator (up to 8 voices) with two independent A/B wavetable engines crossfaded against each other, wavetable position morphing, detuned unison, and integrated stereo spread." },
               { "Analog", "Virtual-analog polyphonic synth with dual oscillators, unevenly detuned unison stacking, osc hard sync, sub-oscillator, noise, pre-filter drive, nonlinear ZDF Moog ladder and SVF filters, dual-path stereo spread, and amp ADSR." },
               { "Sampler", "High-resolution multi-sample player with pitch tracking, root note detection, start/end trimming, loop crossfades, and one-shot playback." },
               { "Drum Sequencer", "8-lane, up to 32-step pattern drum sequencer with a 141-groove library and bundled kit, individual sample slots, per-step velocity, swing, choke groups, per-lane mute/solo, and decay envelopes." },
              { "MPC", "16-pad sample player: per-pad mode (one shot / gate / loop), sync (free or quantised to a division), volume, pitch, pan, speed, fine tune, fade in / out; polyphonic; pads playable by mouse, CV pin or note 36-51." },
                   { "Looper", "Live audio looper: record, play, overdub and clear with tempo-synced take lengths and interface latency compensation." },
               { "Slicer","Transient- or grid-sliced sample playback: chops a loaded sample into up to 64 slices and maps them chromatically from MIDI note 36, with draggable slice markers, a per-slice attack/decay pair, and a crossthrough toggle that lets a slice run past its own boundary." },
               { "Equation Synth", "Real-time bytebeat and mathematical expression synthesis evaluating user formulas with dynamic variables (t, x, y, inputs)." },
               { "Wave Terrain", "2D terrain trajectory orbital synthesis - a moving point traces a path across a height-mapped surface to generate a waveform." },
               { "Spectral Synth", "Image-to-spectral additive resynthesis, MetaSynth-style: any image, drawing or live video patched in is read as a spectrogram and rebuilt from 64-256 sine partials - X is time, Y is frequency, brightness is amplitude, and hue can become stereo position." },
               { "Granular", "Granular synthesis over a loaded or recorded sample: grain length and density set the texture, position/scan and freeze set where the cloud reads from, and the random position/length/pitch/pan controls plus reverse probability are what make it a cloud rather than a stutter." },
               { "PaulStretch", "Extreme time-stretching built for factors where seconds become minutes of evolving ambient texture, via large-window FFT with phase randomisation, plus spectral pitch shift, frequency shift and unison detune." },
               { "Metallic", "Physical modelling for struck and plucked metal - bells, gongs, bars, plates, chimes, tines, strings - via extended Karplus-Strong waveguides and inharmonic modal resonator banks." },
               { "Oscillator", "Four classic waveforms (sine, triangle, saw, square) with an interactive amp envelope, unison, filter, hard sync and fine/coarse tuning." },
               { "Molder & Grain Molder", "Molder decomposes a sample into tracked harmonic partials plus a real residual, then mutates a parameter genome and re-renders from it, each roll walking further from the last. Grain Molder rearranges grains along a blend between original position and a per-grain metric (level, brightness, random)." },
               { "Field Synth", "A polyphonic synth whose voice is a Field kernel you write yourself, compiled to a sample-domain register machine - the note-driven counterpart of Field Effect." },
            } },
            { "AudioEffects", {
               { "Audio Filter", "Multi-mode state-variable & ladder filter (LP12/24/36, HP, BP, Notch, Peak) with drive and resonance control." },
               { "Dynamics", "A compressor with peak or RMS detection, threshold, ratio, attack, release, makeup and an external sidechain input, over a live static transfer curve. Limiter is its own separate node." },
               { "Delay & Reverb", "Tempo-synced ping-pong / stereo bounce delay, feedback filters, high-density Feedback Delay Network (FDN) reverberator with predelay and damping." },
               { "Stutter & Glitch", "Beat-synced buffer repeater, freeze, reverse playback, and granular slice retriggering." },
               { "Wavetable Shaper", "Non-linear transfer curve distortion, saturation drive, bias, and anti-aliased oversampled waveshaping." },
               { "EQ", "Five-band parametric equalizer - low shelf, three peaks and a high shelf by default, each switchable to shelf / peak / HP12 / LP12 and independently on or off - over an interactive frequency response display. Drag a band's dot for frequency and gain, Shift-drag for Q." },
               { "Resonator Bank", "Up to 16 parallel bandpass resonators forming a tuned modal filter bank, including a 'Metallic' tuning mode." },
               { "Cycle Shaper", "Single-cycle waveform distortion and crossfade shaper." },
               { "Key-Snap", "Snaps every spectral peak of the sound to the nearest note of a scale, polyphonically." },
               { "Shape Resonator", "Rings the vibration modes of a geometry input." },
               { "Spectrum Slide", "Morphs one sound into another by sliding spectral peaks instead of cross-fading." },
               { "Spectral Blur & Frequency Shifter", "FFT spectral domain phase smearing, freeze, and frequency SSB modulation." },
               { "Field Effect", "An audio effect whose DSP is a Field kernel you write in the node, compiled to a sample-domain register machine rather than interpreted. Field Synth is its note-driven sibling and Field Graph plots what a kernel does." },
#if defined(_WIN32)
               { "Plugin", "Hosts third-party VST3 effects, with full state save/restore and parameter automation through mapped modulation pins. The plugin's own editor opens in its own native window. Audio Units are a macOS-only format and are not available here." },
#elif defined(__linux__)
               { "Plugin", "Hosts third-party VST3 effects, with full state save/restore and parameter automation through mapped modulation pins. The plugin's own editor opens in its own X11 window. Audio Units are a macOS-only format and are not available here." },
#else
               { "Plugin", "Hosts third-party Audio Unit and VST3 effects, with full state save/restore and parameter automation through mapped modulation pins. The plugin's own editor opens in its own native window." },
#endif
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
#if defined(_WIN32)
         ImGui::Bullet(); ImGui::TextWrapped("Settings and the last graph layout are stored in %%APPDATA%%\\Infinite.");
#elif defined(__linux__)
         ImGui::Bullet(); ImGui::TextWrapped("Settings and the last graph layout are stored in $XDG_CONFIG_HOME/Infinite (~/.config/Infinite by default).");
#else
         ImGui::Bullet(); ImGui::TextWrapped("Settings and the last graph layout are stored in ~/Library/Application Support/Infinite.");
#endif
      }

      ImGui::End();
      PopElevatedPanelStyle();
   }
}
