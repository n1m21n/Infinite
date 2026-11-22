# Presenter guide: Playing sound & picture with the brain

Read **Part 1** to understand everything behind the project in plain English.
Use **Part 2** while presenting: every slide has a short **"Say (simple)"** script you already own, and a **"Go deeper (only if asked)"** section you only open if the professor asks technical follow-ups.

Your strength is sound, visuals, and **Infinite**. The professor's strength is neuroscience. Whenever a question goes deep into brain biology or clinical EEG, the right answer is: *"That is actually why I wanted your advice."*

---

## Part 1: Every question you asked, answered simply

### 1. What are the 4 sensors (`AF7`, `AF8`, `TP9`, `TP10`)?
They are the 4 metal contact pads on a **Muse 2** headband. The names come from the standard hospital 10-20 head map (shown on Slide 09):

- **Letters = region of the head:**
  - `AF` = Anterior Frontal (**forehead**, just above your eyebrows).
  - `TP` = Temporal-Parietal (**behind your ears**, resting on the mastoid bone / side of the head).
- **Numbers = side of the head:**
  - **Odd numbers (`7`, `9`) = Left side** (`AF7` left forehead, `TP9` behind left ear).
  - **Even numbers (`8`, `10`) = Right side** (`AF8` right forehead, `TP10` behind right ear).
- **What each pair picks up:**
  - `AF7` & `AF8` (forehead): front-brain activity, **plus every blink, glance, and eyebrow frown**.
  - `TP9` & `TP10` (behind ears): side-brain activity + **alpha waves** spilling forward from the back of the head when you close your eyes, **plus jaw clenching**.
  - **What they miss:** `C3/C4` (top of the head = imagining moving hands) and `O1/O2` (back of the head = vision). That is why we never claim hand-imagery control or mind-reading.

### 2. What exact numbers and data do the sensors give you?
Every single sensor is just a tiny voltmeter touching your skin. It outputs **one number per sensor, 256 times a second (`256 Hz`)**, measured in **microvolts (`µV`)** — millionths of a volt.

Here are 4 real rows directly from our Muse 2 dataset (`datasets/eeg/heroic_csv/`):

```text
AF7       AF8       TP9       TP10      (units: µV, 256 rows every second)
-76.660   -51.270   -37.598   -45.410
-79.590   -55.664   -31.250   -23.926
-72.754   -52.246   -30.273   -20.996
-70.312   -54.199   -39.062   -44.922
```

That is literally all a headset sends over Bluetooth: **4 streams of wobbling numbers** (exactly like 4 channels of super-slow audio sampled at 256 Hz instead of 48,000 Hz).

### 3. What do EEG, EOG, and EMG look like? (Slide 08)
Because those 4 pads sit on your skin, each pad hears three different signals mixed together:

| Signal | Source | Size (`µV`) | Speed (`Hz`) | What it looks like (see Slide 08) |
|---|---|---|---|---|
| **EEG** | Neurons firing together in the brain | Tiny (`~10–50 µV`) | `1–40 Hz` | Smooth gentle waves (alpha at `8–12 Hz` looks like a steady ripple). |
| **EOG** | Your eyeball rolling when you blink or look left/right | Huge (`~150–300 µV`) | `0.5–8 Hz` | A big, slow, smooth spike on `AF7/AF8` whenever you blink. |
| **EMG** | Jaw clenching, chewing, eyebrow frowning | Large (`~50–250 µV`) | `25–150+ Hz` | Fast, jagged, spiky static bursts — loudest on `TP9/TP10` when you clench. |

### 4. How long do I have to wear the headband?
- **Put it on:** ~30 seconds (wet your forehead/behind ears slightly with water if dry, check the 4 green pills in Infinite's `Brain Source` node).
- **Calibrate (sound-check):** **72 seconds** once per session.
- **Play:** As long as you like — 5 minutes, 15 minutes, or a whole performance.

### 5. What is the 72-second calibration, and why eyes open vs eyes closed?
Calibration is a 3-phase recording built into the `Calibrate` button on `Brain Source` (`src/core/bci/BciTypes.h` & `BciDspEngine.cpp`):

1. **30 seconds eyes open** — records your normal alert baseline (low alpha, normal noise floor).
2. **30 seconds eyes closed** — records your relaxed baseline. **Why eyes closed?** In 1929 Hans Berger discovered that closing your eyes causes `8–12 Hz` alpha waves to rise dramatically in almost every human. It is the simplest, most reliable "brain on/off switch" you can trigger on demand without any lab equipment.
3. **12 seconds "Blink 5 times"** — records 5 deliberate blinks so the engine learns how tall, wide, and positive/negative *your* blinks are.

`30 s + 30 s + 12 s = 72 s`.

**What does eyes-open vs eyes-closed actually tell us?**
Everyone's skull thickness, hair, and alpha frequency are different. Comparing your 30 s eyes-open numbers to your 30 s eyes-closed numbers lets Infinite automatically find which combination of sensors and frequency bands best tells *your* calm state from *your* alert state (using a simple statistical separator called shrinkage LDA), instead of guessing fixed rules that fail half of people.

### 6. Where is the coding part involved, and how does the build work?
Everything is already compiled into `worktrees/bci-architecture/build-dev/Infinite.app` (Slide 11 shows a live screenshot of the `Brain Source` node from that build):

1. **Hardware driver (`src/core/bci/BciRuntime.cpp`):** Dynamically loads the open-source **BrainFlow** C++ library to read Muse 2/S, OpenBCI Cyton/Ganglion, or Neurosity Crown over Bluetooth — or replays recorded CSV files / the built-in `SyntheticCortex` simulator if no headset is plugged in.
2. **Signal processing (`src/core/bci/BciDspEngine.cpp`):** Runs on a background thread (so it never stalls audio or 3D rendering). Removes 50/60 Hz wall-power hum and slow drift, splits the 4 streams into 5 frequency bands (Delta, Theta, Alpha, Beta, Gamma) above the `1/f` noise slope, detects blinks and jaw clenches, and runs the 72 s calibration fit in **0.10 ms** per update.
3. **Infinite node & UI (`src/nodes/BrainSourceNode.cpp` & `src/app/bodies/ParamBodies2.cpp`):** Draws the 4 electrode contact pills (`TP9 | AF7 | AF8 | TP10`), live waveforms, `Calibrate` button, and exposes all **26 output pins** (`24` modulation pins + `image` + `audio`).
4. **Starter patch (`assets/examples/bci_alpha_bell.inf`):** Wires `Brain Source` into a `FieldPixel` glowing bell shader + `Reverb` + `Gain` + `Logic Gate` + `Brain Sculpt`.

### 7. How do we get all 24 control pins (+ Image & Audio)?
Every `0–1` control pin in `BrainSourceNode::OutputLabel` (`src/nodes/BrainSourceNode.cpp:236`) is computed from those same 4 microvolt streams inside `BciDspEngine.cpp`:

- **5 Brain Bands (`delta`, `theta`, `alpha`, `beta`, `gamma`):** Measure energy in `1–4`, `4–8`, `8–12`, `12–30`, `30+ Hz` above the pink-noise (`1/f`) floor. Once calibrated, **`alpha`** switches to your calibrated `0–1` eyes-closed calm score!
- **5 Face / Muscle Triggers (`blink`, `gaze L/R`, `brow`, `jaw L`, `jaw R`):**
  - `blink`: fires `1.0` when `AF7/AF8` sees a blink-shaped spike.
  - `gaze L/R`: difference between `AF7` and `AF8` slow eye voltage.
  - `brow` / `jaw L` / `jaw R`: high-frequency (`25–150 Hz`) muscle energy on forehead (`AF7/AF8`) or left/right ear (`TP9` / `TP10`).
- **Safety & Autonomic (`presence`, `heart`):** `presence` checks if all 4 sensors have clean skin contact; `heart` uses the Muse optical PPG sensor if present.
- **12 Legacy Mood / Verb Shortcuts (`arousal`, `valence`, `entropy`, `coherence`, `density`, `smoothness`, `motion`, `space`, `tension`, `drift`, `surge`, `stillness`):** Weighted formulas kept from v1 for backward compatibility (we explicitly tell the professor these are unreliable on Muse and not our main path!).
- **2 Direct Pins (`image`, `audio`):**
  - `image`: a `256 × 180` scrolling spectrogram texture (top bar = calibrated calm score, 4 rows = `AF7, AF8, TP9, TP10` from `1–40 Hz`).
  - `audio`: each sensor's wave sped up **32×** so `10 Hz` alpha becomes an audible `320 Hz` tone (`AF7+TP9` left ear, `AF8+TP10` right ear).

### 8. What are the 4 datasets (`PhysioNet`, `GIPSA`, `HEROIC`, `EEG-EyeBlinks`)?
Since we didn't have a live headset on hand during this build, we downloaded 4 real public EEG datasets into `datasets/eeg/` to stress-test our code:

| Dataset | Device | People | What was recorded | What we got from it |
|---|---|---|---|---|
| **PhysioNet** (`eegmmidb` R01/R02) | **Gel lab cap** (64 sensors) | **109 people** | 1 min eyes open, 1 min eyes closed | Clean lab baseline (`FP1/FP2/T7/T8` mapped to Muse spots) to test alpha rise. |
| **GIPSA** | **Gel lab cap** (16 sensors) | **20 people** | Repeated 10 s eyes-open & eyes-closed blocks | Strong lab reference (`FP1/FP2/P7/P8`) confirming temporal/parietal alpha. |
| **HEROIC** | **Real Muse 2** (4 dry sensors!) | **14–15 people** (64 sessions, multi-day) | Real dry headband at home & lab, eyes open/closed markers | **The honest consumer test** — shows how weak Muse is vs gel caps, and cross-day drift. |
| **EEG-EyeBlinks** (`IO`, `VV`, `VR`) | **OpenBCI** (`Fp1/Fp2` forehead) | **46 people** (2,300+ labelled blinks) | Stimulus-cued blinks (`IO`), watching video (`VV`), reading (`VR`) | Ground-truth blink timestamps to score whether our blink switch catches real blinks without false triggers. |

---

## Part 2: Slide-by-slide presenter script

### Slide 01: About me
- **Say (simple):** I'm Naman. Three things pull me: how the mind works, making things, and sound. Infinite is the one canvas where all three meet, and this project adds the brain as a live signal.
- **Go deeper (only if asked):** I'm a creative technologist: dance 2017 to 2020, drawing from 2019, 3D art and augmented reality from 2020, electronic music from 2023, founded Infinite Devices in 2026. I'm not a trained neuroscientist, hence your advice.

> Slides 02 onward below are the old 20-slide script. They are replaced one by one as the new deck is approved.

### Slide 02: About Infinite
- **Say (simple):** Infinite connects nodes with cables to make sound, images and 3D. I put them on one canvas because sound, image and geometry are not separate things, they are one connected whole, like nature. Next I want body signals as instruments: tiny muscle movements, heart rate from an Apple Watch, and brain signals from EEG.
- **Go deeper (only if asked):** It is for musicians, VJs, creative coders, visual artists and experimenters. 372 node types, open source (MIT), macOS, Windows and Linux. Field is its own small language for writing what a node does. It also runs from the command line and has MCP tools for AI agents.

### Slide 03: Inspiration
- **Say (simple):** Two people did this before me. In 1965 Alvin Lucier used his own brainwaves to shake speakers touching a gong and drums. In 2023 Benn Jordan wore a consumer EEG headband and let his focus and stress control synth sounds. Infinite is built for exactly this kind of patching.
- **Go deeper (only if asked):** Lucier's piece is "Music for Solo Performer" and relied on calm, eyes-closed alpha waves. Check Benn Jordan's exact headband and software before presenting; the slide describes it from his video.

### Slide 04: Everything is numbers
- **Say (simple):** Under the hood, all three are just numbers. Music is a wave that repeats, and how fast it repeats, in Hz, is the pitch. A picture is a grid of tiny dots, each a mix of red, green and blue. A 3D shape is points in space, each with an x, y and z. A brain headset gives numbers too (tiny voltages). Infinite turns them into 0 to 1 signals that can drive the wave, the pixels and the points.
- **Go deeper (only if asked):** 440 Hz is the note A. Each colour channel is 0 to 255 (or 0 to 1). Because everything is numbers, one can control another, which is what the nodes on the next slide do.

### Slide 05: Swap the LFO for a brain
- **Say (simple):** A control signal is a number from 0 to 1 that keeps changing, and a cable carries it to a knob. Today an LFO, a simple repeating wave, moves this picture. Swap the LFO for brain signals and the same cable lets your brain move a picture, a sound or a 3D shape. The Brain Source node does that, turning a headset's raw brain voltage into 24 of these signals.
- **Go deeper (only if asked):** Brain Source reads Bluetooth headsets through BrainFlow, replays CSV recordings, or runs a synthetic test signal. Pins carry band powers and similar features; raw microvolts are also available.

### Slide 06: Inside the Brain node
- **Say (simple):** Four steps. The headset gives 4 sensor streams, 256 numbers a second each. The node removes mains hum and slow drift, splits what is left into five brain rhythms plus blink and jaw detectors, and outputs 24 signals from 0 to 1 plus a picture and a sound. A Muse 2 only has 4 dry sensors, so we keep what they show reliably: calm (alpha), blinks, jaw and brow. With no headset, a built-in simulator makes fake brain waves, and four public datasets give real recordings. Automatic checks test each step.
- **Go deeper (only if asked):** Sensors AF7, AF8 (forehead) and TP9, TP10 (behind ears). Rhythms: delta, theta, alpha, beta, gamma. The simulator (SyntheticCortex) has calm, deep focus, theta dream and gesture presets with 1/f background, blink, gaze and jaw bursts. Self-test checks: calm vs focus, jaw left vs right, reflex vs deliberate blink, calibration, and real PhysioNet eyes open vs closed. One engine update costs about 0.10 ms.

### Slide 07: The headband and its outputs

**Say (simple):** "This is the headband from above. Two sensors sit on the forehead, two sit behind the ears. They feed the Brain node, which gives 24 signals. The grey ones are old mood-style pins that I do not trust on a Muse. At the bottom are every setting of the node."

**Go deeper (only if asked):** The 24 pins are presence, heart, five rhythms (delta to gamma), blink, gaze left/right, brow, jaw left/right, plus 12 old mood-style pins kept so old patches still load. Image and audio pins sit on top. Settings: device, synthetic preset (calm, focus, dream, micro-gesture), blink filter, smoothing, hold ms, artifact blanking, startle guard, play baked take, calibrate.

### Slide 08: Where the data comes from
- **Say (simple):** Fake brains for testing, real brains for proof. On the left is our built-in simulator with four presets so we can test code anytime without a headset. On the right are four public EEG datasets: two hospital gel-cap datasets for a clean baseline, one real dry Muse 2 dataset (HEROIC, 14 to 15 people across multiple days), and one blink dataset with over 2,300 labelled blinks.
- **Go deeper (only if asked):** PhysioNet eegmmidb (109 people, 64-channel gel cap, R01/R02 eyes open/closed); GIPSA (20 people, 16-channel gel cap); HEROIC (14–15 people, 64 sessions on a real Muse 2); EEG-EyeBlinks (46 people on OpenBCI forehead channels across cued blinks, video, and reading).

### Slide 09: Experiment 1: gel cap vs Muse
- **Say (simple):** A hospital gel cap uses 16 to 64 wet sensors, while a Muse 2 uses 4 dry metal pads. When we ran both through the exact same code, closing your eyes on a gel cap raised alpha 2.25x on the forehead and 3.59x behind the ears. On a dry Muse 2, the forehead barely moved, and behind the ears rose 1.44x, less than half as strong. That is why a one-size-fits-all rule fails on Muse.
- **Go deeper (only if asked):** GIPSA (20 subjects, gel cap): median eyes-closed/open alpha prominence ratio is 2.14x (AF7), 2.36x (AF8), 3.04x (TP9), 3.95x (TP10), 3.59x (EARS combined). HEROIC (64 sessions, dry Muse 2): forehead stays near 1.0x and behind-the-ears median is 1.44x.

### Slide 10: Experiment 2: the 72-second calibration
- **Say (simple):** Personal calibration makes a cheap dry headband actually work. It is a 72-second sound check before you play: 30 seconds eyes open, 30 seconds eyes closed, and 12 seconds blinking 5 times. It fits you, not an average person. On one public Muse 2 dataset, a single fixed rule scored 68% and dropped to a 50% coin flip on a later day. Personal calibration jumped to 88.5% same day, and held 85.5% on a later day.
- **Go deeper (only if asked):** Shrinkage LDA (shrink = 0.3) on 16 log-band features (4 bands x 4 sensors), split-half validation, 6 s smoothing (dropping smoothing to 2 s lowers accuracy to 73.7%). Cross-day 85.5% re-centres the midpoint using the unlabelled median of the new session.

### Slide 11: Visual results: from calibrated data to sound & picture
- **Say (simple):** Here is how the calibrated data turns into actual sound and picture. The 72-second profile turns raw headband waves into a steady 0 to 1 calm score. Cables carry that number straight into Infinite's bell shader and synth. When you are calm with your eyes closed, the ring glows bright and the sound swells. When you focus, it dims and quiets down.
- **Go deeper (only if asked):** Headless test renders of `assets/examples/bci_alpha_bell.inf` at `t = 5.00 s`: CalmAlpha reaches `0.41` mean luma and `-13.6 LUFS` (`369 Hz` peak); DeepFocus stays at `0.13` luma and `-24.2 LUFS`. Blinks toggle the raw `256 x 180` spectrogram overlay; the `audio` pin speeds brainwaves up `32x` so `10 Hz` alpha becomes a `320 Hz` tone.
- **Live CSV demo (optional right after Slide 11):** Open `build-dev/Infinite.app` → `bci_alpha_bell.inf` → set `Brain Source` `device` to `CSV Replay` and paste `/Users/namansoni/Infinite/datasets/eeg/heroic_csv/demo_muse2_open_closed.csv`. First 30 s (eyes open) stays dim & quiet; at 30–60 s (eyes closed) the bell blooms bright and the `320 Hz` brainwave tone swells.

### Slide 12: Help needed: research, headsets & next steps
- **Say (simple):** This is where I would love your advice. First, which papers and other approaches should we look at for cleaning 4-channel dry EEG? Second, which university labs, researchers, or grants are working on similar creative BCI problems? Third, how should we source headsets cheaply for a small pilot—is a ~$250 Muse 2 enough, or is there a lab loan or cheaper path? And finally, is 1.12 s reaction lag (82% accuracy) fast enough for a live instrument, and what ethics and consent steps do we need for a 3-to-5 person pilot?
- **Go deeper (only if asked):** Infinite already supports Muse 2/S (`~$250`) and OpenBCI Cyton/Ganglion (`~$600–$1,200`) via BrainFlow. Double-blink switching (0.0/min median false triggers during reading) and 1-bad-sensor calibration fallback are already built and verified (`BCI_SELFTEST: passed=370 failed=0`). Pilot plan: 3 to 5 people, 15 minutes each.

### Slide 07 — Alpha Bell starter instrument
- **Say (simple):** Here is our starter patch, Alpha Bell. When you close your eyes and calm alpha waves rise, the ring glows bright and the sound swells. When you open your eyes and focus, it dims and quiets down. Blinks toggle the raw spectrogram overlay.
- **Go deeper (if asked):** Headless test renders at `t = 5.00 s`: CalmAlpha reaches mean luma `0.41` and `-13.6 LUFS` (`369 Hz` peak); DeepFocus stays at `0.13` luma and `-24.2 LUFS`.

### Slide 08 — What EEG, eye blinks, and jaw muscles look like
- **Say (simple):** Each sensor on the head hears three things mixed together: tiny smooth ripples from the brain, a giant spike when you blink, and jagged static when you clench your jaw. Scientists throw blinks and jaw noise away; for an instrument, we keep them as triggers.
- **Go deeper (if asked):** Real 2-second recordings from PhysioNet, EEG-EyeBlinks, and HEROIC. Brain rhythms are ~10–50 µV; blinks are 150–300 µV because the eyeball acts like a dipole battery; jaw EMG is 25–150+ Hz broadband.

### Slide 09 — Four dry sensors (10-20 map)
- **Say (simple):** A Muse headband only has 4 dry sensors — two on the forehead (`AF7`, `AF8`) and two behind the ears (`TP9`, `TP10`). It cannot reach the top of the head (`C3/C4` for imagined movement) or the back (`O1/O2` for vision). So we only claim rhythm, left/right balance, and muscle — never mind-reading.

### Slide 10 — Brain rhythms as slow LFOs
- **Say (simple):** Brain rhythms are super slow waves — 1 to 40 cycles a second, like LFOs in music. Alpha at 8–12 Hz is our main signal: Hans Berger first recorded it in 1929 when someone closed their eyes. Speed it up 32× and 10 Hz becomes a clear musical tone around middle C.
- **Go deeper (if asked):** Underneath sits a pink-noise (`1/f`) background slope. We fit that slope from 3–40 Hz and measure alpha as the bump above it.

### Slide 11 — Inside Infinite: Brain Source node
- **Say (simple):** On the left is the live `Brain Source` node in our C++ build. You put the band on, run a 72-second sound-check (30 s eyes open, 30 s eyes closed, 5 blinks), and the node outputs 24 control pins plus live Image and Audio pins in `0.10 ms` per update.
- **Go deeper (if asked):** Runs on a background worker thread via BrainFlow (supports Muse 2/S, OpenBCI Cyton/Ganglion, Neurosity Crown, plus CSV replay). Uses 16 relative log-band features (4 bands × 4 sensors) with shrinkage LDA (`shrink = 0.3`).

### Slide 12 — Result 1: gel lab cap vs dry Muse
- **Say (simple):** We tested our code on 3 public datasets: two recorded with wet hospital gel caps (PhysioNet, GIPSA) and one on a real dry Muse 2 (HEROIC). On a Muse, the forehead barely changes when you close your eyes — the alpha rise lives behind the ears (`1.44×`), about a third as strong as a lab gel cap.

### Slide 13 — Result 2: 72-second calibration
- **Say (simple):** Using one fixed rule only scored 68% on Muse — and dropped to a 50/50 coin flip on a later day. Fitting each person's own 72-second calibration jumps same-day accuracy to `88.5%`, and holds `85.5%` on a later day just by re-centring the midpoint.
- **Go deeper (if asked):** Split-half validation (first half calibrates, second half tests, 6 s smoothing). Cross-day re-centring uses the unlabelled median of the new session.

### Slide 14 — Result 3: readability test
- **Say (simple):** Can someone tell eyes-open from eyes-closed from a 10-second clip? An automatic check got 73% from the raw spectrogram alone, 86% by listening to the 32× sped-up sound, and 92% from the calibrated top strip. I got 10/10 visually, though I built it — so we need a naive viewer next.

### Slide 15 — Honest scorecard
- **Say (simple):** We wrote down 6 pass/fail bars before downloading any EEG data. Three passed (zero hardcoded weights, `0.10 ms` live speed, clip readability) and three failed (uncalibrated Muse alpha, fixed single-sensor rule, and single-blink switching). The alpha fails are why we built per-person calibration.

### Slide 16 — Six open questions
- **Say (simple):** From here on, I'd love your neuroscience advice on six practical questions before we run a small live pilot.

### Slide 17 — Which headset & budget?
- **Say (simple):** Infinite already supports Muse (`~$250`) and OpenBCI (`~$600–$1,200`). For a 5-person pilot, one shared Muse costs ~$250. **My question for you:** is a 4-sensor Muse credible enough to start, or do we need 8 sensors over the back of the head?

### Slide 18 — Signal purity, datasets & drift
- **Say (simple):** Three questions here: first, how much of a Muse feature is still subtle eye/muscle noise, and how should we filter it with only 4 channels? Second, none of the public datasets recorded people making music — what task would convince you? Third, 6 s smoothing gives 88.5% while 2 s drops to 73.7% — how would you handle speed vs day-to-day drift?

### Slide 19 — What fails today
- **Say (simple):** Four honest gaps: first, single blinks fail as an on/off switch (`78%` cued, `60–65%` reading/video, ~1 false trigger/min) because natural and deliberate blinks overlap — we want to try double blinks next. Second, everything so far is offline replay, not a live headset. Third, learning over practice isn't tested yet. Fourth, one bad sensor currently pauses calibration, and the audio pin has a `125 ms` filter lag.

### Slide 20 — Next: 3–5 person pilot
- **Say (simple):** Our next step is a small 15-minute pilot with 3 to 5 people on a real Muse: 72 s calibration, 10 minutes playing Alpha Bell, and 10 cued double blinks. I would love your feedback on the pilot design, ethics/consent for recording people, and whether any lab headset is available to benchmark against.

---

## Quick fallback lines if you get stuck
- *"I built the audio, shader, and software engine side. On the biology and experimental design, I'm relying on the papers and looking for your guidance."*
- **Never claim mind-reading.** The claim is: *a learnable audiovisual instrument driven by brain rhythms, eye/muscle triggers, and left/right balance.*
- **Fonts note:** The deck uses Infinite's brand fonts (`Geist`, `Geist Mono`, `Caveat`). If opening `BCI_results.pptx` in PowerPoint/Keynote on a machine without them, install the `.ttf` files from `art/brand/fonts/` (or open in Google Slides, which loads them via Google Fonts).
