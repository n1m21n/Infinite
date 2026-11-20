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

### Slide 01 — Title
- **Say (simple):** Infinite is an open-source audiovisual workstation I built — like a modular synth and visualiser in one app. This talk is about plugging a brainwave headband into it as a signal source: what already works, what failed honestly, and where I need your neuroscience advice.

### Slide 02 — One patch plays music, visuals and 3D live
- **Say (simple):** This is Infinite. You wire boxes called nodes — oscillators, filters, shaders, 3D lights — and one patch plays music and draws visuals live.
- **Go deeper (if asked):** 353 node types today, written in C++, free and open source on macOS, Windows, and Linux.

### Slide 03 — Inspiration
- **Say (simple):** Two inspirations: in 1965 Alvin Lucier made brainwaves physically vibrate gongs and snare drums; in 2023 musician Benn Jordan patched a consumer EEG band into modular synths. Infinite is built for exactly that kind of patching, with visuals and personal calibration built in.

### Slide 04 — Swap an LFO for a brain signal
- **Say (simple):** Here is the whole idea in one picture. On the left an LFO gently wobbles a visual shader. Infinite doesn't care where a `0` to `1` control signal comes from — swap the LFO for your brain, and the picture breathes with you.

### Slide 05 — Skip mood guesses
- **Say (simple):** Our first version averaged all 4 sensors into "mood" scores and drove a preset flower. Research showed mood scores on 4 dry sensors aren't reliable, and averaging erased the best signal behind the ears. Now we keep all 4 sensors separate and hand raw signals straight to the patch.
- **Go deeper (if asked):** Brain Sculpt lets you hold Shift and grab any knob while relaxed or focused to teach your own mapping by example, instead of using hardcoded weights.

### Slide 06 — Eight building blocks
- **Say (simple):** Instead of guessing emotions, we turn brain voltage into Infinite's eight basic building blocks — pixels, pitch, modulation knobs, triggers, switches, relationships, 3D points, and time. Five are already built and tested.

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
