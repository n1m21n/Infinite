# Logic Pro: plugin checklist (step 2.8)

Format: AU only (Instrument = Infinite Instrument, FX = Infinite FX under Audio FX > Infinite).

| Field | Value |
|---|---|
| DAW version | |
| Mac / macOS | |
| Kit build (commit) | |
| Date | |

Result: PASS, FAIL (say what happened) or SKIP (say why). Patches are in the kit's `patches/` folder; open one with the plugin's File > Open.

| # | Check | Pass when | Result | Notes |
|---|---|---|---|---|
| C1 | Open, close and reopen the plugin window 10 times | No crash, the picture comes back each time, engine memory up by less than 20 MB (Activity Monitor: `Infinite Engine`) | | |
| C2 | 4 instances: 2 Instrument (R1) + 2 FX (R2), each with its own patch | All 4 play; each keeps its own patch | | |
| C3 | Bounce R1 and R2 offline, and record the same bars live; flip one and sum | The two match (M-5: null <= -60 dBFS, or sounds identical by ear if the DAW cannot sum) | | |
| C4 | Bind a Macro to Host slot 1 (S1-slot.inf), record automation by turning it in the plugin view, play it back | The curve is written; playback moves the Macro | | |
| C5 | Save the project, quit the DAW, reopen | Same patch, same slot values, sounds the same (M-11) | | |
| C6 | Sample rate 44.1 -> 48 -> 96 kHz with the project open | No crash, pitch stays right | | |
| C7 | Buffer 32, 64, 512, 2048 with R1 playing | No dropouts at 64 and up (32 recorded either way) | | |
| C8 | Play, stop, loop, scrub | Notes stop on stop; sequencers jump with the playhead | | |
| C9 | 1 hour with R1 playing and the plugin window open | No dropouts; engine memory flat | | |
| C10 | Kill the engine (`python3 crash_drill.py kill --host <DAW>`, or Activity Monitor: Force Quit `Infinite`) | The DAW keeps running; the plugin goes silent and shows Engine stopped + Restart; Restart brings the patch back | | |
| L1 | Plug-in Manager: Reset & Rescan Selection on both Infinite AUs | Both pass validation in Logic | | |
| L2 | FX on an audio track with a MIDI FX/sidechain: route MIDI to Infinite FX | FX receives the notes | | |
| L3 | Bounce in place, and Track Freeze | Matches live playback | | |
| L4 | Settings > Audio: note whether Logic runs the plugin out of process (Activity Monitor shows `AUHostingService`) | Either way works | | |
