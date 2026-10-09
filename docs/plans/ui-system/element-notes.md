# Per-node element notes (design-language pass)

Working notes for the sweep: every node family has bespoke elements (keyboard, step grid, arena, curve...).
The rule: each element type has ONE grammar, drawn by a shared component; the node only supplies data.

## Shared grammar (what every viewer/grid obeys)
| Piece | Rule | Component |
|---|---|---|
| Viewer frame | full body width (shared edges, P1), `radius_field`, scope bg + scope border | `AudioViz::Fill/Border/Begin/End` |
| Step / gate cell | recess + border; on = accent tint; playhead = bright + heavier border; hover = accent edge | `StepCell::Draw` |
| Dropdown / checkbox / slider | field well, no chevron, one height | `NodeDropdownField`, `CheckBox`, `AudioSlider` |
| Idle state | never blank: dim label or at-rest shape | `AudioViz::IdleLabel` |
| Chips inside viewers (note name, velocity) | small tile on `radius_field`, text centred | TODO shared chip |

## Notes family: how each element fits
- **MIDI Notes / Keyboard / Note Filter / Note Capturer — piano strip** (`DrawMidiKeyboard`): already one shared strip. Keep anchored on C (P9). Frame it like a viewer (it is inside the scope border now). Nothing bespoke left.
- **Arpeggiator — gate grid**: 8 `StepCell`s, playhead from `CurrentGridStep()`. Done.
- **Note Sequencer — step columns**: each column = a tall bar (pitch) + note chip on top + velocity chip below + step number. Bespoke because each step carries pitch AND velocity. Fit: column recess/border/playhead must read as a StepCell (same recess colour, same playhead role); chips are the shared chip. TODO: move the column background + playhead to `StepCell`, keep pitch bar as content inside it.
- **Bouncing Balls — arena**: physics needs a square world. Frame is the full-width viewer frame (shared edges); the square arena is centred inside it. Ball rest/hit colours are the accent / amber hit flash. Done.
- **Velocity Curve — chart**: viewer frame, 4x4 graticule, curve line in accent. Same frame as scopes. OK; graticule colour must come from the scope role, not a literal.
- **Note Router — lit dots**: 4 outputs; dots are the only state. Fit: dots = `StatusDot` role (idle dim, active accent), centred row above the selector.
- **Quantizer / Gate / Humanizer / Glide / Note Transpose — one-param nodes**: no viewer; knob/selector centred, readout strip carries the state (spec §7). Do not invent a viewer.
- **Note Echo / Note Strum / Chorder / Note Stack — knob grids only**: row grammar only; check P3 (selector left) and P4.
- **MIDI File / Note Capturer — transport-like**: buttons are `ActionButton`; waveform-ish piano roll (if any) uses the viewer frame.
- **Random Note Generator / Note Switcher / Note Merge — router family**: same dots as Note Router.

## Open decisions
- Playhead colour: DECIDED amber everywhere (StepCell and Note Sequencer column outline, step number, cap, velocity fill). Known collision with the modulation role; playhead is always a filled/outlined cell, never a pin.
- Shared chip component for Note Sequencer chips and any value tag inside a viewer.

## Template for the next families (fill before touching each)
For every node: (1) what is the bespoke element, (2) which shared frame/cell/chip does it map to, (3) what stays node-specific (the data and the interaction), (4) the at-rest state.
