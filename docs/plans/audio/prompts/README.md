# Execution prompts — one file, one session

Generated from `../P3c-P3a2-design.md` §5's prompt generator and its pre-filled
field table. Each file is self-contained: paste it into a fresh session, which
then reads the two skills and the design doc itself. Nothing else needs pasting.

Order is the design's own build order (§1 "Build order for Part A", §2 "Build
order for Part B"), with one addition — see 01b.

| # | Session | Why here |
|---|---|---|
| 01 | §0 infrastructure + Audio Filter | §0 must land before every other node in both parts. Audio Filter is folded in because it is what forces `AudioEffectNode` into a shape proven by use. |
| 01b | `AUDIOPARAMSWEEPTEST` + `AUDIOTEARDOWNSWEEPTEST` | **Not in the original phase order.** Both are already cited as exit criteria by `new-audio-node` §5 and by every prompt below, and neither exists. Built now, every later node is covered automatically; built in P4, each is covered only if someone remembers to enrol it. |
| 02 | Dynamics | reuses 01's draggable-curve visualizer pattern |
| 03 | Delay | first user of `RateDivision` (§0.1) |
| 04 | Stereo | placed early out of difficulty order on purpose — small, and once it exists every later effect can be auditioned in stereo |
| 05 | Drive | |
| 06 | Reverb | |
| 07 | Pitch Time | last: the only kernel that can slip a session |
| 08 | Note Filter | smallest note node; establishes the note-on→note-off map |
| 09 | Note Modify | second-smallest; same map, pure transform |
| 10 | Arpeggiator | first *generator* — proves scheduled note-off ownership |
| 11 | Note Echo | reuses that directly |
| 12 | Note Router | only node in either part with >1 output |
| 13 | Note Display | |
| 14 | Note Sequencer | last despite being the headline node: largest UI in the part, and it consumes the scale/swing/humanise helpers the other six settle |

Parts A (02–07) and B (08–14) are independent — either order, or parallel
sessions — once 01 has landed.

After each session, `STATUS.md` is the file that records what shipped. The
design doc and `README.md` track the plan and do not change.
