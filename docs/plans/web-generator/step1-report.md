# Web generator: step 1 report (measure before building)

Stop point: nothing is built. Sizes and times below are **estimates** (no Emscripten is installed on this Mac: `emcc` not found, no emsdk, not in brew). The first action after sign-off is to install it and replace every estimate with a measurement.

## 1. What a style card can place (SampleStyles::PoolTypes, 32 types)
Counted from `GenerateSamplesTables.inc`; classes and size from a source scan (lines = header + cpp).

| Group | Types | Coupling found | Verdict for Emscripten |
|---|---|---|---|
| Plain DSP synths | Analog (374), FM (541), Metallic (573), Oscillator (203), Wavetable (407), Field Synth (1 498) | none (no platform, thread, GL, UI) | compile as is |
| File-driven synths | Sampler (1 076), Granular (984), Partials (808, also `std::thread`), Spectral Synth | `Platform::SampleBuffer` and file decode; kit wavs on disk (`DrumSequencerNode::KitDir`) | shim: a WAV decoder + the 8 kit wavs embedded in the wasm data (the engine only reads bundled kit files); Partials' thread must run inline |
| Drums | Drum Sequencer (1 581) | `Platform::` (kit path, sample decode) | same shim as above |
| Note nodes | Arpeggiator, Chorder, Humanizer, Note Echo, Note Sequencer, Note Strum, Velocity Curve (all in `NoteNodes.h`, 6 724 lines) | `Platform::` only (no thread/UI) | shim the Platform calls; the nodes compile as is |
| Effects | Audio Filter, Bitcrush, Chorus, Convolution Reverb, Delay, Drive, Dynamics, EQ, Limiter, Phaser, Reverb, Saturator, Stutter | not found by the scan (classes are table-driven, not one class each); expected plain DSP | compile as is, to confirm with the build; Convolution Reverb needs its impulse responses embedded |
| Mixer | Mixer (`AudioNodes.h`) | `Platform::` | shim |

Must drop: nothing found so far. Must change before any of it builds:
- `OfflineGraph.cpp` includes `app/AppShared.h`, the whole app/UI header (ImGui included). A headless target needs the few declarations it uses split out of it. This is the largest single piece of work.
- `OfflineGraph.cpp:44` sets the denormal flags with `_mm_setcsr` (x86 SSE only). Needs a `#ifdef __EMSCRIPTEN__` no-op; Apple silicon already takes another path.
- `Render()` is run on a `std::thread` by the node: in the browser the whole render runs inside a Web Worker instead, no threads needed.
- The node registry (`NodeRegistry.cpp`) registers every node in the app; the wasm target registers only the 32 above.

## 2. Smallest headless target and its cost
Target `infinite_gen_wasm`: SampleStyles + UserStyle-free recipe path + OfflineGraph + the 32 nodes + core/Patch + AudioSummary (for Master) + the embedded kit/IR data. No UI, GL, audio device.

| Item | Estimate | Basis | Budget |
|---|---|---|---|
| wasm code, gzipped | 1.0 to 1.8 MB | about 20 000 lines of node DSP plus core; typical -O2 Emscripten output for DSP code of that size | under 3 MB with data |
| embedded data (8 kit wavs, impulse responses), gzipped | 0.3 to 1.5 MB, to measure | kit files plus IRs; if the IRs are large, trim to the ones cards use | |
| 2-bar render, native release | about 0.5 s (the node's own budget; debug builds take 15 s) | `GENSAMPLETEST` "slowest 2-bar generate" | |
| 2-bar render, wasm desktop Chrome/Safari | 1 to 2 s | wasm without threads typically runs 1.5 to 3 times slower than native | under 2 s |
| 2-bar render, mid-range phone | 3 to 6 s | roughly 2 to 3 times slower again | **over budget** |
| Redraws | up to 3 more renders when the loop is silent or flat | `kMaxRedraws` in the node | worst case multiplies the time |

Conclusion: size fits; time fits on desktop and is the risk on phones. Mitigations if the measurement confirms it: render at 1 bar for the preview and 2 bars on download, lower the sample rate to 44.1 kHz, or cap the effect chain on phones. Each would break parity with Desktop, so they are choices for you, not defaults.

## 3. Alternatives rejected, with numbers
| Option | Numbers | Why not |
|---|---|---|
| Render server on a paid host | about 5 to 20 USD a month for a small VPS or 0.0001 to 0.001 USD a loop on serverless; 0.5 to 2 s render plus network; needs the Linux build of the engine running behind an API | breaks the static GitHub Pages rule (no server), adds an always-on cost and an abuse surface, and cannot work offline |
| Bigger pre-rendered pool | now 34 cards x 5 parts x 3 seeds = 510 loops; a 2-bar 16-bit stereo wav is about 0.8 MB, so about 400 MB as wav or about 33 MB as 128 kbps mp3; every extra seed multiplies it | still a finite set that repeats, which is the thing you asked to remove; the pool was never published either |
| wasm in the browser (chosen path) | about 1.3 to 3.3 MB once, then unlimited new loops, free to host | the phone time is the open risk above |

## 4. Things that change with the sound-type rebuild
- The wasm target compiles the same `Compile()` the node uses, so every generator improvement (pluck, pad, drum kits, bass, FX, foley) reaches the website by rebuilding the wasm. No second engine to keep in step.
- The Desktop node now draws the style card from the seed and a hidden taste profile; styles are no longer a user choice. On the web, no personal profile exists, so the card is drawn uniformly at random and the visitor picks the category (part). The prompt's "style is random within the chosen category" becomes "card is random, part is the category".
- Parity test scope is therefore recipe-level (card, part, seed, key, scale, bars) with `personal = false`; that is exactly what the Desktop produces when the profile is empty.

## 5. What sign-off unlocks
1. Install Emscripten (about 1 GB download via emsdk or brew); needs your yes.
2. Split the headless declarations out of `AppShared.h` for `OfflineGraph`, add the CMake target, embed the data, and measure real size and time on desktop and a phone.
3. Then the exports, worker, parity test and website steps from the prompt.
Decision for you: go with the wasm route, and accept that phone render time may need a trade-off after measurement.
