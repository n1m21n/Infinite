---
name: rhythmic-quantization-standard
description: Canonical rhythmic time division and quantization rules for Infinite nodes — whenever an audio, note, synth, or modulator node needs tempo sync, clock division, rate, or quantization options, it must use the standardized table in src/audio/MusicTime.h (4 bars to 1/32 with dotted and triplets, plus 1/64) rather than inventing a local list.
---

Paths below are relative to the repo root (`/Users/namansoni/infinte`).

## The Rule

**Never hardcode a local list of rhythmic divisions or quantization intervals** (such as `{"1/1", "1/2", "1/4", "1/8", "1/16", "1/32"}` or `{"Off", "1/4", "1/8", ...}`).
All tempo-synced timing and grid quantization across all node categories (Notes, Synths, Audio Effects, Modulators) must use the single canonical source of truth in `src/audio/MusicTime.h`.

---

## 1. The Standardized Rhythmic Table

Declared in `MusicTime::RateDivision` (`src/audio/MusicTime.h`), ordered strictly slowest to fastest:

| Index | Name | Multiplier / Beats (at 4/4) | Description |
|---|---|---|---|
| 0 | `4 bars` | 4 bars (16.0 beats) | 4 full measures (time-signature relative) |
| 1 | `2 bars` | 2 bars (8.0 beats) | 2 full measures |
| 2 | `1 bar` | 1 bar (4.0 beats) | 1 measure |
| 3 | `1/2` | 2.0 beats | Half note |
| 4 | `1/2.` | 3.0 beats | Dotted half note |
| 5 | `1/2T` | 4/3 beats (~1.333) | Half note triplet |
| 6 | `1/4` | 1.0 beat | Quarter note |
| 7 | `1/4.` | 1.5 beats | Dotted quarter note |
| 8 | `1/4T` | 2/3 beat (~0.667) | Quarter note triplet |
| 9 | `1/8` | 0.5 beat | Eighth note |
| 10 | `1/8.` | 0.75 beat | Dotted eighth note |
| 11 | `1/8T` | 1/3 beat (~0.333) | Eighth note triplet |
| 12 | `1/16` | 0.25 beat | Sixteenth note |
| 13 | `1/16.` | 0.375 beat | Dotted sixteenth note |
| 14 | `1/16T` | 1/6 beat (~0.167) | Sixteenth note triplet |
| 15 | `1/32` | 0.125 beat | Thirty-second note |
| 16 | `1/32.` | 0.1875 beat | Dotted thirty-second note |
| 17 | `1/32T` | 1/12 beat (~0.0833) | Thirty-second note triplet |
| 18 | `1/64` | 0.0625 beat | Sixty-fourth note |

Total count: `MusicTime::kNumRateDivisions = 19`.

### Quantize Grid Variant
For nodes with an "Off" state (such as `QuantizerNode` and `NoteCapturerNode`), use `MusicTime::QuantizeGridList()`:
- Index `0`: `"Off"` (`QuantizeGridBeats(0) == 0.0`)
- Index `1..19`: maps to `RateDivision(index - 1)` via `QuantizeGridBeats(i)`.

---

## 2. Shared Functions in `MusicTime.h`

- `MusicTime::RateDivisionList()`: `const std::vector<std::string>&` for `AudioKnobRow::Dropdown` or `AudioBareDropdown`.
- `MusicTime::RateDivisionNames()`: array of `const char*` names.
- `MusicTime::RateDivisionName(int d)`: returns string label for division index `d`.
- `MusicTime::BeatsFor(RateDivision d)`: exact period in quarter-note beats (handles dotted `* 1.5`, triplet `* 2/3`, and time-signature aware bars).
- `MusicTime::HzForRateDivision(RateDivision d, double bpm)`: cycle frequency in Hz for LFOs/modulators.
- `MusicTime::NearestRateDivision(double beats)`: resolves any float/double beat duration to the closest enum index.
- `MusicTime::QuantizeGridList()`: returns `{"Off", "4 bars", ..., "1/64"}`.
- `MusicTime::QuantizeGridBeats(int gridIndex)`: returns `0.0` for Off, else `BeatsFor(gridIndex - 1)`.
- `MusicTime::QuantizeGridName(int gridIndex)`: returns `"Off"` or the division name.

---

## 3. UI Implementation Patterns

### Pattern A: Dual Synced/Free Rate Mode (`DrawRateModeControls`)
Used by `Arpeggiator`, `Note Sequencer`, `Random Note Generator`, `Note Echo`, `Note Switcher`:
```cpp
DrawRateModeControls(row, &n->rateMode, &n->rateBeats, &n->rateSeconds);
```
Inside, it automatically maps `*rateBeats` to/from `MusicTime::RateDivisionList()` and `MusicTime::NearestRateDivision`, keeping param ordinals stable for modulation.

### Pattern B: Direct Rhythmic Dropdown
Used by `Drum Sequencer`, `Image Spectral Synth`, and effect nodes (`Delay`, `Tremolo`, `Audio Filter`, etc.):
```cpp
row.Dropdown("rate", MusicTime::RateDivisionList(), n->rate, [n](int i) {
   PushUndoCheckpoint();
   n->rate = i;
});
```

### Pattern C: Quantize Grid Dropdown
Used by `Quantizer` and `Note Capturer`:
```cpp
AudioKnobRow row(1);
row.Dropdown("grid", MusicTime::QuantizeGridList(), n->div, [n](int i) {
   PushUndoCheckpoint();
   n->div = i;
});
row.End();
```

---

## 4. Audio Thread DSP Patterns

Inside `ProcessBlock`, compute grid / step timing in samples:
```cpp
const double bpm = (double)Transport::Instance().Tempo();
const double samplesPerBeat = mSampleRate * 60.0 / std::max(1.0, bpm);

// For RateDivision:
const double stepBeats = MusicTime::BeatsFor((MusicTime::RateDivision)rateDiv);
const double stepSamples = stepBeats * samplesPerBeat;

// For QuantizeGrid (0 = Off):
if (quantizeDiv > 0)
{
   const double gridBeats = MusicTime::QuantizeGridBeats(quantizeDiv);
   const double gridSamples = std::max(1.0, gridBeats * samplesPerBeat);
   // snap forward or to nearest
}
```

---

## 5. Verification Checklist for New Nodes

When creating or modifying any node involving time divisions or quantization:
- [ ] No local string arrays of `1/4`, `1/8`, `1/16`.
- [ ] Dropdown options come from `MusicTime::RateDivisionList()` or `MusicTime::QuantizeGridList()`.
- [ ] `BeatsFor` or `QuantizeGridBeats` is used for duration calculation.
- [ ] Dotted and triplet subdivisions work correctly.
- [ ] `NearestRateDivision` is used when converting continuous beats to a dropdown index.
- [ ] `DSPTEST musictime table completeness` passes in `main.cpp`.
