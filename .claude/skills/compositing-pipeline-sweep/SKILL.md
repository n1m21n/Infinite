---
name: compositing-pipeline-sweep
description: Sweep Infinite's 2D / compositing image pipeline for bugs on both macOS and Windows - every node cooking a real texture, bypass passing through untouched, cook memoization holding under fan-out, ImageCable pulls carrying the right frame id, colour/palette extraction, and idle-frame caching. Use when a 2D node was added or changed (Blend, Layer Stack, Switcher, Filter, Curves, Feedback, Trails, Reaction Diffusion, Resynth, Fit, Ramp, Palette, Remove Background), when an image comes out black, stale, doubled or wrong-sized, when bypass leaves a node's effect on, when the same patch looks different depending on how many cables leave a node, or before a release as a 2D-pipeline regression gate.
---

Paths below are relative to the repo root (`/Users/namansoni/infinte`).

## Run this first

```bash
.claude/skills/compositing-pipeline-sweep/driver.sh
```

`--skip-build` to reuse the existing binary. Exit 0 means every asserted
fixture passed and both static checks are clean. The observation fixtures are
never graded - the driver prints their log paths and you read them.

Static checks alone, no build, ~1s:

```bash
python3 .claude/skills/compositing-pipeline-sweep/check.py
```

## What the 2D chain actually is

```
source node          Shape / Noise / Ramp / Image Source / Text / Video
   |  cooks into its own FBO, publishes GetOutputTexture()
   v
ImageCable           Resolved() walks past bypassed nodes (64-hop cap),
   |                 Pull(frameId) cooks the resolved source and returns its texture
   v
image op             Blend / Filter / Curves / Layer Stack / Switcher / Fit / ...
   |                 cooks into its own FBO from the pulled texture(s)
   v
terminal             Output / Viewport / Syphon / Projection - identity pass into
                     its own FBO so it has a normal cook lifecycle
```

Three properties make this correct, and each has its own check below:

1. **Every node publishes a real texture.** Non-zero GL texture id, non-zero
   width and height, after being fed from a real source. `IMAGERESYNTH_SELFTEST`
   asserts this for *every registered node type* at once.
2. **Cooking is once per frame, not once per consumer.** `CookIfNeeded(frameId)`
   is invoked once per incoming edge. A node with three outgoing cables is asked
   to cook three times in the same frame.
3. **Bypass is transparent.** A bypassed node must hand its input straight
   through, both in what downstream reads and in what `ImageCable::Resolved()`
   walks to.

## Rule 2 is the one that bites

For a stateless node (Filter, Blend) a missing memo is only wasted GPU time.
For an **accumulating** node it is a visible, hard-to-diagnose bug: Feedback,
Trails, Reaction Diffusion and Resynth advance their ping-pong state inside the
cook, so without the memo they advance once *per downstream cable*. The
symptom is "the effect runs faster when I plug it into two things", and it will
not reproduce for anyone with a single-consumer patch.

`check.py` gates this statically: every `CookIfNeeded(int frameId)` definition
under `src/nodes/` and `src/core/` must compare the frame id against a latched
member (`mLastCookFrame`, `mCookedFrame`, or any `Last*Frame`) and return early.

Its second rule is the companion: a `.Pull(...)` **inside** a cook must be
handed that cook's own `frameId` parameter. Pulling with anything else re-cooks
the upstream node against a different frame than the graph is on, which defeats
the memo one level up. Both rules are currently clean (107 cook bodies, 32
Pull calls, 0 problems as of this skill being written) - so any failure is a
new node that got it wrong, not pre-existing debt.

## The fixtures

| fixture | frames | asserts |
| --- | --- | --- |
| `IMAGERESYNTH_SELFTEST` | 3 | spawns one of *every* registered node type, wires each image input to a real Shape, cooks, and prints `WxH tex=N OK/FAIL` per node plus a `N node types, K failures` total |
| `INFINITE_BYPASSTEST` | 8 | Shape -> Invert -> Output. Frame 3: output is dark (`inverted OK`). Bypass is then set; frame 6: output is bright again (`PASSED THROUGH OK`, else `STILL INVERTED - BUG`) |
| `INFINITE_PALETTETEST` | 12 | Ramp -> Palette: three genuinely distinct swatches, deterministic for the same seed, bound stops applied to the target Ramp, saturation shaping regrades without re-clustering, binding survives a patch round trip. Verdict `PALETTE OK` / `SUSPECT` |
| `INFINITE_REMOVEBGTEST` | 1 | headless subject-mask check; prints `REMOVEBGTEST OK`, or `REMOVEBGTEST SKIP: <reason>` where the platform has no backend |

Observation only (numbers, no verdict - the driver prints the log path):

| fixture | read it for |
| --- | --- |
| `INFINITE_RESYNTHTEST` | per-generation pixel `drift=`. All-zero drift means the accumulator stopped stepping; a huge jump on one generation means it reset |
| `INFINITE_CACHETEST` (+`SHOWCASE`) | `work=` and `idleStreak=` per frame. A static patch should reach a rising idle streak. A streak stuck at 0 means something re-cooks unconditionally every frame |

`CACHETEST` is run *on top of* `SHOWCASE` because it only observes
`NodeWorkCounter()` - on the empty canvas a normal launch gives you, the
counter never moves and the log proves nothing.

## Windows parity

Everything above is platform-neutral by construction and is what makes this
sweep meaningful as a two-version check:

- `check.py` reads source only, so it gives the identical verdict on either OS.
- The fixtures run through the compiled binary, so running the driver on
  Windows exercises the Windows GL path, Windows FBO formats and the Windows
  readback. Nothing in the fixture list is macOS-gated.
- The one platform-divergent entry is `REMOVEBGTEST`, which SKIPs where the OS
  has no subject-mask backend. A SKIP is graded as a pass on purpose: a missing
  OS feature is not a compositing regression. If it prints `FAIL` on one OS and
  `OK` on the other, that *is* a real divergence.

If a 2D node needs `#ifdef _WIN32` anywhere under `src/nodes/`, that is the
bug - see `new-compositing-node` and `new-utility-node` for the
`Platform::`-seam rule.

## What this sweep does not cover

Be honest about these rather than reading a green run as more than it is:

- **No pixel-correctness assertions per op.** Nothing checks that Blend's
  "multiply" is actually a multiply. The fixtures check that a texture exists,
  that bypass is transparent, and that cooking is memoized - not that the
  shaders are mathematically right.
- **No alpha / premultiplication check.** There is no fixture asserting how
  alpha composites through a chain.
- **Resolution negotiation is unasserted.** SELFTEST prints each node's `WxH`
  but only fails on zero, so a node that silently clamps a 4K input to 1024
  passes.
- **Layer Stack and Switcher have no dedicated fixture.** They are covered only
  by SELFTEST's "it cooks something" bar and by the `SHOWCASE`/`SHOWCASE2`
  visual patches.

Closing any of these means adding an assertion to `src/main.cpp` next to
`INFINITE_BYPASSTEST` and adding it to `ASSERT` in `driver.sh` - the readback
lambda there is the pattern to copy.

## Reading a failure

- `[NO VERDICT]` - the fixture printed no OK/FAIL line. Almost always the frame
  budget in `driver.sh` is below the frame the verdict prints on. Raise it;
  never lower the bar to make it quiet.
- `[CRASH]` - non-zero exit. Read the log; a GL error or an assert fires before
  the verdict.
- SELFTEST `FAIL` rows - read *which* node types. One node failing is that
  node's cook; every node failing is the shared FBO/shader path or the feeder.
