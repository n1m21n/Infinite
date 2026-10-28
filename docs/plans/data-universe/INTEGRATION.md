# Integration: one way to join the layers (owner 2026-10-28)

The end goal is a better sample generator whose whole samples the owner rates. Each layer is its own model with its own claims;
they join through two shared things only, so a layer can be replaced without touching the others.

```
 note model ──► events ─┐
 sound model ─► recipe ─┼─► compose ─► one patch (.inf) ─► render ─► wav ─► measure (22 axes) ─► claims
 effects model► chain ──┤
 mix model ───► levels ─┘
```

## The two shared things

| Shared | What it is | Why |
|---|---|---|
| The patch (`.inf` text) | the only thing a layer hands on. The note layer writes note events, the sound layer writes one synth recipe, the effects layer writes the nodes and cables after it, the mix layer writes gain, pan and sends | the app renders exactly what a user would open, so a result is real |
| The 22-axis characteristics | every layer is measured in the same language, on the same render | a layer's claim is scored on what comes out, never on its own internal numbers |

## Layer contracts

| Layer | Input | Output | Done when (claim) |
|---|---|---|---|
| Note | context (key, tempo, mood, part) | `events.json`: `t, pitch, velocity 0-1, length` per note (the format `--notes` takes) | note gates B1-B10 (passing) |
| Sound | a target on the axes, or a free draw | one `recipe`: every parameter of one engine (`own_sweeps.recipe`) | C1, C2, C3, C7 |
| Effects / chain | the sound's axes `x_0` | `chain`: ordered effect nodes with settings; each node's output axes `x_k` | C9 (`CHAIN-TRACK.md`) |
| Mix | the parts' renders | gain, pan, send per part | G8 (needs the Ableton sets) |

## Rules that keep it joined

| Rule | Why |
|---|---|
| a layer never reads another layer's internals, only the patch fields above | a layer can be swapped or retrained alone |
| `compose.py` is the only place that writes a whole patch | one wiring, one bug site (the MIDI File node plays nothing headless: notes always go in as events) |
| every layer reports its claim on its own output first, then on the composed render | a sample that sounds bad is traced to the layer that caused it |
| the final test is whole samples, shuffled and blind, today's site generator next to the new one | the end goal is the sample, not a layer score |

## Order of work (heavy renders one at a time; MIDI-only work runs beside them)

1. Sound layer: the rest of Steps 3-7 (running).
2. Note layer: step 2j weak spots (MIDI only; runs now).
3. Effects layer: chain-track renders after the embeddings finish.
4. `compose.py`: note events + recipe + chain in one patch (first version already in `listen_model.py`: events + recipe + Reverb).
5. Mix layer: when the Ableton sets are in.
6. Whole-sample blind test against the live site generator.
