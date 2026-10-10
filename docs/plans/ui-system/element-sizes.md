# Element-size table (one number per control)

Every node reads these. Tokens live in `src/app/AppShared.h` (`kFaderH` block, `kKnobStd`, `kMacro*`) and in the
component headers named below. A body never writes a size literal.
Reference nodes measured from code (and the gallery for the final shots): Mixer, Gain, Wavetable, Audio Filter, Delay.
Frame height is derived: font 15 pt + 2 x ImGui default FramePadding.y (3) = 21 pt.

| Element | Size (pt) | Source of truth | Reference node |
|---|---|---|---|
| Knob cap | 56 dia (one size, `kKnobStd`) | `kKnobStd` | Filter, Delay, Wavetable |
| Knob caption gap | 4 under the cap, then one text line | `kKnobCaptionGap` | all knob rows |
| Knob cell width | content width / N (audio rows); 184 for a single macro control | `AudioKnobRow`, `kMacroCell` | Filter 440/4 = 110 |
| Fader | 22 wide hit, 138 tall box, 18 x 12 handle, 6 rail | `kFaderW`, `kFaderH`, `VFader.h` | Mixer, Gain |
| Fader meter | 8 wide, left edge +14 from cell centre, 6 inset top/bottom. Signal faders only | `kFaderMeter*` | Mixer, Gain |
| Slider | frame height (21) tall; width = half content (AudioHalfWidth) or full | `AudioSlider`, `FieldWell` | Wavetable sections |
| Dropdown | frame height (21); width cell - 8, max 112, grows to fit its value, no chevron | `DropdownField` | Filter mode |
| Checkbox | 16 box, 24 hit | `CheckBox.h` | Delay sync |
| Switch | 30 x 16 (Settings and panels; nodes keep CheckBox, Macro Toggle moves to it in step 2) | `Switch.h` | Settings |
| Numbox | frame height wide-cell field; Macro NumBox is `kMacroRowH` (26) | `kMacroRowH` | Macro NumBox |
| Step cell | `kMacroRowH` (26) tall, 8 across a wide cell, gap 4 | `StepCell.h`, `kMacroWideCell` | Arpeggiator |
| Wide cell | 200 (selector, step gate, XY) | `kMacroWideCell` | Macro Selector |
| Viewer frame | full body width, `radius_field` 4, scope bg + border; 150 tall time viz, 190 tall response curve | `AudioViz.h`, `kAudioTimeVizH` | Delay, Filter |
| Readout strip | 21 tall (line + 6) + 2 gap, first row of every audio body | `BeginAudioBody` | all audio nodes |
| Node top padding | pin row, then readout strip; no extra literal | `StageNodeBodies.cpp` | all |
| Audio body width | 440 / 200 narrow / 960 wide | `kAudioNodeWidth` ... | Wavetable |

## Decisions
- **Macro Slider** uses the Mixer fader (22 x 138). **No meter**: a macro value is a control, not a signal, and the accent fill
  already shows it. The meter rows above apply to signal faders (Mixer, Gain) only.
