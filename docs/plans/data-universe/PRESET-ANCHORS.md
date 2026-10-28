# Preset Anchor Points: Grounding Generative Sound in Human Taste

**Document:** `PRESET-ANCHORS.md`  
**Date:** 2026-10-28  
**Scope:** Architecture and exact specifications for 40 native anchor presets (20 Analog, 20 Oscillator) across 6 musical categories.  
**Purpose:** Replaces unguided random parameter search with musically grounded sweet-spot anchors for the **Sample/Sound Generator**, **Instrument Generator**, and **Global Emotional Knobs**.

---

## 1. The Theory: Why Preset Anchors Are Required

In a 33-parameter synthesizer like `Analog`, the theoretical parameter space contains an astronomical number of possible knob combinations ($10^{33}$). 

* **The Machine Desert (95% of space):** Uniform random draws produce subsonic mud, inaudible high-frequency squeaks, self-oscillating feedback, clipping, or DC-offset clicks.
* **The Musical Manifold (5% of space):** Professional sound designers tune knobs into delicate, highly correlated relationships (e.g., pulling resonance down when cutoff is low, balancing sub-oscillator gain against main wave volume, tailoring envelope decay to pitch register).

```
  [ Full Parameter Space: 95% Unmusical Desert ]
  ┌────────────────────────────────────────────────────────────────────────┐
  │   (screeching)         (silence)                  (clipping)           │
  │              ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░                          │
  │              ░  ★ ANCHOR: Moog Sub Bass     ░  ◄── Musical Sweet Spot  │
  │              ░  ★ ANCHOR: Reese Pad         ░      (Human Manifold)    │
  │              ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░                          │
  │   (DC offset)          (subsonic mud)             (dead knobs)         │
  └────────────────────────────────────────────────────────────────────────┘
```

### How the Generator Uses Anchor Points

Instead of starting from zero when a user selects a category and characteristic, the system performs a **two-stage generation**:

1. **Category Retrieval:** User selects `[ Bass ]` + `[ Warm ]`. The system instantly loads the nearest tuned anchor preset (e.g., `Analog_Bass_02_WarmSub`).
2. **Local Manifold Traversal:** The learned forward model applies micro-adjustments around that anchor to meet the exact requested characteristic (raising low-mid saturation, slightly closing the filter).
3. **Emotional Macro Steering:** When the producer turns a global emotional knob like **Tension** or **Nostalgia**, the model modulates *relative offsets* from this anchor point, ensuring the patch never falls off the musical cliff into unplayable noise.

---

## 2. The 6 Core Musical Categories & Taxonomy

Every anchor preset is assigned to one of six universal instrument categories:

| Category | Primary Acoustic Role | Envelope Profile | Register Range | Typical Node Engines |
|---|---|---|---|---|
| **`BASS`** | Low-end foundation, mono stability | Fast attack ($0\text{--}10\text{ ms}$), zero stereo spread | C1 – C2 ($32\text{--}65\text{ Hz}$) | Analog (sub + saw), Oscillator |
| **`LEAD`** | Melodic focal point, vocal-like cut | Medium attack ($10\text{--}50\text{ ms}$), high sustain | C3 – C5 ($130\text{--}520\text{ Hz}$) | Analog (sync, 2-osc detune), FM |
| **`PAD`** | Harmonic texture, background wash | Slow attack ($200\text{--}800\text{ ms}$), long release | C2 – C4 ($65\text{--}260\text{ Hz}$) | Oscillator (unison spread), Wavetable |
| **`PLUCK`** | Rhythmic arpeggios, transient accents | Instant attack ($0\text{--}2\text{ ms}$), exponential decay | C3 – C5 ($130\text{--}520\text{ Hz}$) | Analog, Metallic, String |
| **`KEYS`** | Polyphonic chords, dynamic touch | Dynamic velocity-to-filter response | C2 – C5 ($65\text{--}520\text{ Hz}$) | FM (e-piano), Analog |
| **`PERC/FX`** | Drums, sweeps, risers, textures | Transient bursts, noise sweeps | Full register | Analog (noise), Metallic |

---

## 3. Concrete Anchor Specifications: Analog (20 Presets)

Parameters correspond to Infinite's `Analog` engine (33 parameters; swept values normalized $0.0\text{--}1.0$):
* **Waves:** `0: Saw, 1: Square/Pulse, 2: Triangle, 3: Sine`
* **Filter Types:** `0: LowPass 24dB, 1: LowPass 12dB, 2: HighPass, 3: BandPass`

```
┌────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                 20 NATIVE ANCHOR PRESETS FOR ANALOG                                    │
├────┬───────────┬──────────────────────┬─────────┬─────────┬────────┬────────┬─────────┬────────┬───────┤
│ ID │ Category  │ Preset Name          │ Wave 1  │ Wave 2  │ Sub/Noi│ Cutoff │ Res/Drv │ Attack │ Decay │
├────┼───────────┼──────────────────────┼─────────┼─────────┼────────┼────────┼─────────┼────────┼───────┤
│ 01 │ BASS      │ Deep Solid Sub       │ Sine    │ Sine    │ Sub 1.0│ 0.25   │ 0.0/0.1 │ 0.01s  │ 0.40s │
│ 02 │ BASS      │ Warm Moog Saw Bass   │ Saw     │ Saw     │ Sub 0.4│ 0.35   │ 0.2/0.2 │ 0.01s  │ 0.60s │
│ 03 │ BASS      │ Punchy Square Bass   │ Square  │ SubSine │ Sub 0.5│ 0.30   │ 0.3/0.3 │ 0.00s  │ 0.35s │
│ 04 │ BASS      │ Acid Drive Squelch   │ Saw     │ None    │ Sub 0.0│ 0.40   │ 0.7/0.6 │ 0.01s  │ 0.30s │
│ 05 │ LEAD      │ Singing Solo Saw     │ Saw     │ Saw     │ Det 0.2│ 0.65   │ 0.2/0.2 │ 0.03s  │ 0.80s │
│ 06 │ LEAD      │ 80s Brass Sync Lead  │ Saw     │ Pulse   │ Sync On│ 0.55   │ 0.4/0.3 │ 0.05s  │ 0.50s │
│ 07 │ LEAD      │ Mellow Triangle Flute│ Triangle│ Sine    │ Det 0.1│ 0.45   │ 0.1/0.0 │ 0.06s  │ 0.40s │
│ 08 │ LEAD      │ Screaming Drive Lead │ Saw     │ Square  │ Det 0.3│ 0.75   │ 0.5/0.7 │ 0.02s  │ 0.70s │
│ 09 │ PAD       │ Warm Analog Swell    │ Saw     │ Saw     │ Det 0.4│ 0.35   │ 0.1/0.1 │ 0.60s  │ 1.80s │
│ 10 │ PAD       │ Vintage String Wash  │ Pulse   │ Saw     │ Det 0.3│ 0.45   │ 0.2/0.1 │ 0.45s  │ 2.00s │
│ 11 │ PAD       │ Dark Ambient Drone   │ Triangle│ Square  │ Sub 0.6│ 0.25   │ 0.2/0.2 │ 0.80s  │ 2.50s │
│ 12 │ PAD       │ Ethereal Breath Pad  │ Saw     │ Triangle│ Noi 0.2│ 0.40   │ 0.1/0.0 │ 0.50s  │ 1.50s │
│ 13 │ PLUCK     │ Tight Woody Pluck    │ Pulse   │ Triangle│ Sub 0.2│ 0.30   │ 0.3/0.1 │ 0.00s  │ 0.25s │
│ 14 │ PLUCK     │ Glassy Analog Bell   │ Sine    │ Pulse   │ Sync On│ 0.50   │ 0.4/0.1 │ 0.00s  │ 0.45s │
│ 15 │ PLUCK     │ Punchy Filter Snap   │ Saw     │ Saw     │ Det 0.1│ 0.35   │ 0.5/0.2 │ 0.00s  │ 0.20s │
│ 16 │ KEYS      │ Mellow Electric Felt │ Triangle│ Sine    │ Sub 0.2│ 0.40   │ 0.1/0.1 │ 0.02s  │ 0.80s │
│ 17 │ KEYS      │ Classic Analog Wurli │ Pulse   │ Triangle│ Det 0.1│ 0.50   │ 0.2/0.2 │ 0.01s  │ 0.70s │
│ 18 │ PERC/FX   │ Sub Heavy 808 Kick   │ Sine    │ PitchDrp│ Sub 0.8│ 0.20   │ 0.0/0.4 │ 0.00s  │ 0.45s │
│ 19 │ PERC/FX   │ Analog Snare Snap    │ Triangle│ Noise   │ Noi 0.7│ 0.55   │ 0.3/0.3 │ 0.00s  │ 0.18s │
│ 20 │ PERC/FX   │ White Noise Rise/Fall│ Noise   │ None    │ Noi 1.0│ 0.45   │ 0.6/0.1 │ 0.80s  │ 1.20s │
└────┴───────────┴──────────────────────┴─────────┴─────────┴────────┴────────┴─────────┴────────┴───────┘
```

### Detailed Parameter Recipes for Analog Anchors (Representative Examples)

#### Anchor 02: `Analog_Bass_02_WarmMoog`
```json
{
  "name": "Warm Moog Saw Bass",
  "category": "BASS",
  "engine": "Analog",
  "params": {
    "wave1": 0, "wave2": 0, "filterType": 0, "sync": 0, "analog": 0.7,
    "osc1Vol": 0.8, "osc2Vol": 0.6, "sub": 0.45, "noise": 0.0, "oscMix": 0.5,
    "oct1": 0, "semi1": 0, "fine1": 0.0, "oct2": -1, "semi2": 0, "fine2": 0.06,
    "cutoff": 420.0, "resonance": 0.25, "drive": 0.25, "keyTrack": 0.6,
    "attack": 0.005, "decay": 0.45, "sustain": 0.40, "release": 0.15,
    "pw1": 0.5, "detune": 0.12, "voices": 1, "spread": 0.0
  }
}
```

#### Anchor 09: `Analog_Pad_09_WarmSwell`
```json
{
  "name": "Warm Analog Swell",
  "category": "PAD",
  "engine": "Analog",
  "params": {
    "wave1": 0, "wave2": 0, "filterType": 0, "sync": 0, "analog": 0.85,
    "osc1Vol": 0.7, "osc2Vol": 0.7, "sub": 0.1, "noise": 0.02, "oscMix": 0.5,
    "oct1": 0, "semi1": 0, "fine1": -0.08, "oct2": 0, "semi2": 0, "fine2": 0.08,
    "cutoff": 850.0, "resonance": 0.15, "drive": 0.10, "keyTrack": 0.8,
    "attack": 0.65, "decay": 1.50, "sustain": 0.75, "release": 1.20,
    "pw1": 0.5, "detune": 0.35, "voices": 4, "spread": 0.65
  }
}
```

---

## 4. Concrete Anchor Specifications: Oscillator (20 Presets)

Parameters correspond to Infinite's `Oscillator` engine (22 parameters; multi-wave, unison, filter, ADSR):
* **Waves:** `0: Saw, 1: Square, 2: Triangle, 3: Sine, 4: Noise`

```
┌────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                               20 NATIVE ANCHOR PRESETS FOR OSCILLATOR                                  │
├────┬───────────┬──────────────────────┬─────────┬────────┬────────┬─────────┬────────┬────────┬────────┤
│ ID │ Category  │ Preset Name          │ Wave    │ Unison │ Detune │ Cutoff  │ Res/Drv│ Attack │ Decay  │
├────┼───────────┼──────────────────────┼─────────┼────────┼────────┼─────────┼────────┼────────┼────────┤
│ 21 │ BASS      │ Pure Sine Sub        │ Sine    │ 1      │ 0.00   │ 280 Hz  │ 0.0/0.0│ 0.005s │ 0.40s  │
│ 22 │ BASS      │ Fat Square Bass      │ Square  │ 2      │ 0.08   │ 450 Hz  │ 0.2/0.2│ 0.002s │ 0.35s  │
│ 23 │ BASS      │ Reonant Saw Bass     │ Saw     │ 1      │ 0.00   │ 600 Hz  │ 0.5/0.3│ 0.004s │ 0.50s  │
│ 24 │ BASS      │ 2-Voice Reese Bass   │ Saw     │ 2      │ 0.22   │ 380 Hz  │ 0.1/0.1│ 0.010s │ 0.60s  │
│ 25 │ LEAD      │ Supersaw Euphoria    │ Saw     │ 5      │ 0.45   │ 2800 Hz │ 0.2/0.2│ 0.020s │ 0.80s  │
│ 26 │ LEAD      │ Vintage Square Lead  │ Square  │ 1      │ 0.00   │ 1600 Hz │ 0.3/0.2│ 0.015s │ 0.60s  │
│ 27 │ LEAD      │ Pure Whistle Sine    │ Sine    │ 1      │ 0.00   │ 3500 Hz │ 0.0/0.0│ 0.040s │ 0.50s  │
│ 28 │ LEAD      │ Hard Sync Saw Lead   │ Saw     │ 1      │ 0.00   │ 2200 Hz │ 0.4/0.4│ 0.010s │ 0.70s  │
│ 29 │ PAD       │ 7-Voice Lush Cloud   │ Saw     │ 7      │ 0.55   │ 950 Hz  │ 0.1/0.1│ 0.700s │ 2.20s  │
│ 30 │ PAD       │ Hollow Square Space  │ Square  │ 3      │ 0.30   │ 750 Hz  │ 0.2/0.0│ 0.500s │ 1.80s  │
│ 31 │ PAD       │ Warm Triangle Bed    │ Triangle│ 4      │ 0.25   │ 650 Hz  │ 0.0/0.1│ 0.600s │ 2.00s  │
│ 32 │ PAD       │ Glass Shimmer Pad    │ Sine    │ 5      │ 0.40   │ 1400 Hz │ 0.2/0.0│ 0.400s │ 1.60s  │
│ 33 │ PLUCK     │ EDM Hyper Pluck      │ Saw     │ 3      │ 0.25   │ 800 Hz  │ 0.3/0.2│ 0.001s │ 0.22s  │
│ 34 │ PLUCK     │ Short Organic Wood   │ Triangle│ 1      │ 0.00   │ 550 Hz  │ 0.2/0.1│ 0.001s │ 0.18s  │
│ 35 │ PLUCK     │ Bright Chime Pluck   │ Square  │ 2      │ 0.15   │ 1200 Hz │ 0.4/0.1│ 0.002s │ 0.30s  │
│ 36 │ KEYS      │ Digital EPiano Chime │ Sine    │ 2      │ 0.08   │ 1100 Hz │ 0.2/0.1│ 0.008s │ 0.90s  │
│ 37 │ KEYS      │ Soft Mellow Felt     │ Triangle│ 1      │ 0.00   │ 700 Hz  │ 0.1/0.0│ 0.012s │ 0.80s  │
│ 38 │ PERC/FX   │ Clean Punch Sub Drop │ Sine    │ 1      │ 0.00   │ 300 Hz  │ 0.0/0.2│ 0.001s │ 0.35s  │
│ 39 │ PERC/FX   │ Noise Hi-Hat Click   │ Noise   │ 1      │ 0.00   │ 4500 Hz │ 0.3/0.0│ 0.001s │ 0.08s  │
│ 40 │ PERC/FX   │ Laser Sweep Drop     │ Saw     │ 1      │ 0.00   │ 2000 Hz │ 0.6/0.3│ 0.001s │ 0.25s  │
└────┴───────────┴──────────────────────┴─────────┴────────┴────────┴─────────┴────────┴────────┴────────┘
```

---

## 5. How Global Emotional Knobs Steer Relative to Anchors

When an emotional macro dial is turned, it modifies parameters **as bounded offsets $(\Delta)$ from the loaded anchor**:

```
                       [ Loaded Anchor: Warm Moog Bass ]
                                      │
              ┌───────────────────────┴───────────────────────┐
              ▼                                               ▼
     [ Turn TENSION Up ]                            [ Turn NOSTALGIA Up ]
  • Filter Cutoff  += +15%                       • Filter Cutoff  += -25%
  • Resonance      += +30%                       • Fine Pitch LFO += +0.05 (flutter)
  • Detune         += +0.10 (beating)            • Saturation Drv += +15%
  • Env Decay      += -10% (tighter)             • Release Space  += +20%
```

### The 4 Primary Emotional Macro Rules:

1. **`TENSION` (0 to 10):**
   * *Acoustic Goal:* Increase dissonance, sharpen transients, boost inharmonic resonance.
   * *Formula:* $\Delta \text{Resonance} = +0.05 \cdot T$, $\Delta \text{Detune} = +0.03 \cdot T$, $\Delta \text{Decay} = -0.04 \cdot T$.
2. **`NOSTALGIA / LOFI` (0 to 10):**
   * *Acoustic Goal:* High-frequency roll-off, pitch wow/flutter, tape saturation.
   * *Formula:* $\Delta \text{Cutoff} = -0.06 \cdot N$, $\Delta \text{Drive} = +0.04 \cdot N$, $\Delta \text{Fine2} = \text{LFO}(0.5\text{ Hz}, 0.02 \cdot N)$.
3. **`AGGRESSION / PUNCH` (0 to 10):**
   * *Acoustic Goal:* Drive harmonics, high crest factor, transient bite.
   * *Formula:* $\Delta \text{Drive} = +0.08 \cdot A$, $\Delta \text{Attack} = \max(0.001, \text{Attack} \cdot (1 - 0.08 A))$.
4. **`SPACE / ETHER` (0 to 10):**
   * *Acoustic Goal:* Stereo width, atmospheric decay, high diffusion.
   * *Formula:* $\Delta \text{Release} = +0.12 \cdot S$, $\Delta \text{Spread} = +0.08 \cdot S$, $\Delta \text{Cutoff} = +0.03 \cdot S$.

---

## 6. Execution & Implementation in Infinite

1. **Storage:**  
   Store these 40 recipes as JSON objects in `training-data/own/anchors/analog_anchors.json` and `oscillator_anchors.json`.
2. **Verification Render:**  
   Render all 40 anchors across C1–C6 using `own_sweeps.py check` to verify zero clipping, zero DC offset, and clean frequency response.
3. **Evaluation Metric (The Ground Truth Gap):**  
   Score your forward proxy on these 40 anchors. The $R^2$ on these 40 presets is your true **Human Playability Score**.
4. **UI Linkage:**  
   When the user clicks `[Bass]`, the instrument loads the corresponding anchor recipe directly into the C++ DSP core in $< 1\text{ ms}$.
