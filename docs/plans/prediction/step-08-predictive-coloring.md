# Prediction step 8: Predictive Coloring

Status: **design only, nothing built.** Written 2026-09-22 after the fundamentals reframe: the
music-side predictors (Drift, Predictive Notes) each predict one dimension a musician already has
a word for — pitch, timing, dynamics. Image color has the equivalent fundamentals — RGB, HSV,
tone zones — and no predictor touches any of them yet. This is that predictor's design.

One line: **it learns what "graded" looks like for this patch's own footage, live, and nudges every
new frame toward it.** One button (Learn), one knob (Mix). Everything else is the model's problem,
not the user's.

Line numbers are from commit `1ccdbe9`; re-grep the symbol if one has drifted.

## 0. What already exists (and what doesn't)

Checked before designing anything, because this is the one predictor idea with almost no existing
infrastructure to lean on:

| Piece | Exists? | Where |
|---|---|---|
| Reading an image down to scalar stats (brightness, contrast, R/G/B, saturation, hue) once per frame | **yes** | [`ImageAnalyzeNode`](../../../src/nodes/AnalyzeNodes.h), `Analyze()` in [`AnalyzeNodes.cpp:503`](../../../src/nodes/AnalyzeNodes.cpp) |
| A parametric color-grade transform (exposure, contrast, highlights/shadows, vibrance, hue, per-channel gain) | **no** | nothing in `src/nodes/` exposes these as named controls |
| A tone-curve LUT applied per-pixel on the GPU | **yes** | [`CurvesNode`](../../../src/nodes/CurvesNode.h) — Photoshop-style bezier points per channel, baked to a 256-entry LUT texture |
| A histogram of pixel values (not just mean/variance) | **no** | nothing in the codebase computes one |
| An online, cross-session, half-life-decayed statistics profile | **yes**, for params | [`MovementStats`](../../../src/core/MovementStats.h) — the exact shape this needs, but for knob positions, not pixels |
| A "Learn button that captures then builds a model" UI pattern | **yes** | [`PredictiveNotesNode::SetLearning`](../../../src/nodes/PredictiveNotesNode.h) |

Conclusion: the **plumbing** for "reduce an image to numbers, once a frame, without stalling the
GPU" already exists and should be reused wholesale (§2). The **color-grade math** does not exist
anywhere in Infinite and has to be built from the ground up (§3). This is why the node is scoped
as new rather than as an extension of `CurvesNode` — Curves has no named channels to predict
*onto*; a predicted "highlights" value has nowhere to go until this node creates that destination
itself.

## 1. The math fundamentals (research, not invention)

The user's list — brightness, contrast, saturation, hue, vibrance, exposure, black/white levels,
highlights, midtones, shadows, per-channel RGB — is exactly the parameter set of a standard
photographic tone-and-color pipeline (Lightroom/Resolve/Photoshop Camera Raw all converge on the
same operators because the underlying math is old and settled). None of this needs inventing;
it needs assembling correctly, in the correct order, operating on the correct color space at each
step:

| Operator | Space | Formula (per channel `c`, or on luma `Y`) |
|---|---|---|
| **Exposure** | linear RGB | `c' = c · 2^stops` — multiplicative gain, applied *before* the rest, because a stop is a linear-light doubling, not a gamma-space one |
| **Black / white point (levels)** | RGB or luma | `c' = clamp((c − black) / (white − black), 0, 1)` |
| **Contrast** | RGB or luma, around a pivot | `c' = (c − pivot) · k + pivot`, pivot = 0.5 (or the learned mean, §3) |
| **Highlights / Shadows** | luma-masked RGB | split by a smooth luma weight `w(Y)` (not a hard threshold — a hard split bands), then `c' = c + gain_hi · w_hi(Y) · (1−c) + gain_sh · w_sh(Y) · c`; `w_hi`/`w_sh` are complementary smoothstep windows over `Y` |
| **Midtones** | gamma on the masked middle | `c' = c ^ (1/γ)`, weighted by `1 − w_hi − w_sh` so the ends are untouched |
| **Whites / Blacks** | endpoint anchors | same shape as highlights/shadows but windows anchored at `Y≈1` / `Y≈0` with a tighter falloff — the "clip harder at the very end" control, distinct from the broader highlights/shadows zones |
| **Saturation** | HSV | `S' = S · k` — uniform scale of the `S` channel |
| **Vibrance** | HSV | `S' = S + k·(1−S)·S` — non-uniform: boosts low-saturation pixels more than already-saturated ones, so skin tones (naturally low-S) don't get crushed the way a flat saturation boost does |
| **Hue** | HSV | `H' = H + shift (mod 360°)` |
| **Per-channel RGB gain / white balance** | linear RGB | `R'=R·gR, G'=G·gG, B'=B·gB` — this *is* temperature/tint: temperature trades `gR` against `gB`, tint trades `gG` against `(gR+gB)/2` |

RGB↔HSV conversion is the standard piecewise formula (max/min/chroma); no new derivation needed.
Order matters and is fixed above: exposure and levels operate in (approximately) linear light
before the rest; contrast and the three tone zones operate on luma-derived weights; saturation,
vibrance and hue operate in HSV last, so they see the already-graded RGB. This ordering matches
every mainstream grading tool and is not a free design choice — reordering it (e.g. saturation
before levels) visibly shifts hues at the black/white points.

**What "predictive" adds on top of a plain color-grade node:** none of these 13 numbers are set by
the user. They are *fit* from a learned target, the way Drift fits `θ`/`σ` from a param's own
history instead of the user setting a wander speed by hand. §3 is the fitting procedure.

## 2. Reusing the existing reduction pipeline

`ImageAnalyzeNode::Analyze()` ([`AnalyzeNodes.cpp:235`](../../../src/nodes/AnalyzeNodes.cpp))
already solves "get numbers out of a live image without serializing CPU and GPU every frame":

1. Downsample the source texture to a small square (`sampleSize`, clamped 8–256) via a shader pass
   into an FBO (`kDownsampleFrag`, `AnalyzeNodes.cpp:29`).
2. `glReadPixels` that small FBO into a CPU buffer.
3. Compute moments on the CPU buffer (`sumR/G/B`, `sumLum`, `sumLum2`, `sumSat`, ...).
4. Rate-limit step 1–3 to `sampleRate` (default 30/s) against `Transport::Instance().Seconds()`, so
   a 240 fps render doesn't readback 240 times a second.

Predictive Coloring reuses this exact shape, with one change: instead of reducing the small buffer
to five scalars (brightness, contrast, R, G, B), it reduces it to a **histogram**: 32 bins each for
`R`, `G`, `B`, luma `Y`, `H`, and `S`. At a 64×64 downsample (4096 samples), a 32-bin histogram is
well-populated per readback; six histograms of 32 `uint32` bins is 768 B — trivial next to the
existing `mPixels` buffer.

This is the first place the design must depart from `ImageAnalyzeNode`: that node's moments (sum,
sum-of-squares) are sufficient for mean/variance but not for percentiles, and black/white-point
fitting (§3) needs percentiles (the 1st/99th percentile, not the min/max, so a single hot pixel
doesn't set the white point). A histogram is the minimum representation that supports both moment
and percentile queries cheaply.

## 3. The learned model

### 3.1 Two profiles, same shape as `MovementStats`

- **Live profile**: the current patch's own recent frames. A ring buffer of the last *N* seconds
  of histograms (or an EMA of the histogram bins — cheaper, and consistent with how `MovementStats`
  ages out old evidence). This is "what does *this* footage look like right now."
- **Learned target**: what Learn fits *toward*. Two modes, both useful, user picks which:
  - **Self-normalize** (default, needs no reference): the target is a *canonical* well-exposed
    histogram — black point at the learned 1st percentile, white point at the learned 99th,
    midtone pivot at the learned median, saturation/vibrance left at learned levels (this mode
    doesn't push saturation around, it only fixes levels/contrast/balance). This is auto-levels /
    auto-contrast, well-understood (Photoshop's "Auto Tone" is exactly this family of algorithm).
  - **Match a reference**: same target-fitting, but the target histogram comes from a *different*
    source — e.g. a still image or another patch's learned profile — and the transform solves for
    the RGB gains and tone-curve shape that move the live histogram's moments onto the reference
    histogram's moments. This is **statistical color transfer** (Reinhard, Ashikhmin, Gooch &
    Shirley, *"Color Transfer between Images"*, IEEE CG&A 2001) — match mean and standard
    deviation per channel (in a decorrelated space; the paper uses `lαβ`, a good option here too
    since it avoids RGB's channel correlation). This is the actual research citation behind "build
    a mathematical model" rather than a hand-tuned heuristic.

### 3.2 Fitting the 13 operator values from the histogram

Given a live histogram and a target (self-normalized or matched), each operator in §1's table has
a closed-form or near-closed-form fit, not a search:

| Operator | Fit from histogram |
|---|---|
| Black/white point | live 1st/99th percentile → target's own percentiles (or 0/1 for self-normalize) |
| Exposure | `log2(targetMeanY / liveMeanY)` |
| Contrast | `targetStdY / liveStdY`, pivot = live median |
| Highlights/Shadows/Whites/Blacks | compare live vs. target histogram mass in each luma window; gain = the ratio needed to move that window's mean onto the target's |
| Saturation/Vibrance | `targetMeanS / liveMeanS` (uniform vs. non-uniform split by how much the low-S tail differs from the target's) |
| Hue | circular mean of `H` (mean of unit vectors on the hue circle, not a linear mean — hue wraps at 360°); shift = target circular mean − live circular mean |
| Per-channel RGB gain | `targetMeanC / liveMeanC` per channel, after exposure/levels are already applied (fit sequentially, in the §1 order, on the *residual* histogram at each stage — fitting all 13 simultaneously from one histogram is over-determined and unstable) |

This sequential residual-fit (apply exposure, re-measure, fit contrast on what's left, re-measure,
fit the next stage...) is the same idea as `MovementStats::FitFollow`'s ridge regression being run
per-stage rather than as one giant joint fit — keep each fit low-dimensional and well-conditioned
rather than solving a 13-parameter system at once.

### 3.3 Live learning, cross-session persistence — the Drift precedent, not the Predictive Notes one

Two existing predictors persist their learned state two different ways, and this matters for
which one Predictive Coloring should copy:

- **Predictive Notes** learns *once per patch*: `model` is a captured-events string saved in that
  patch's own params ([`PredictiveNotesNode::model`](../../../src/nodes/PredictiveNotesNode.h)). A
  new patch starts cold.
- **Drift** learns from `MovementStats`, which is a **global, cross-session, cross-patch** profile
  keyed by `(nodeType, paramName)`, persisted independently of any one patch, with active-time
  half-life forgetting (`kHalfLifeActiveSec`, 2 weeks) so old evidence fades but never vanishes on
  session close.

The user's spec — "it takes on all existing data in the patch/sessions and future patches/sessions,
and learns live like Drift" — is explicitly the **Drift** shape, not the Predictive Notes shape.
So: the learned target histogram is **not** a per-patch saved param. It is a new pool in the same
place `MovementStats::Engine` already lives, keyed by something stable across patches (there is no
natural "same param" key for an image the way there is for `ParamKey` — the key here is closer to
"this owner's footage in general," so a single global profile per owner, not per-node, is the right
granularity — one learned "how I like things graded" target, reused by every Predictive Coloring
node the owner ever places). Live accumulation uses the same EMA-with-half-life shape
`MovementStats` already implements; this reuses the *pattern*, not the `ParamKey`-shaped code, so
it is new code in `MovementStats.*` or a sibling `ColorStats.*` — **decide which during
implementation**, based on whether the six histograms fit naturally next to `MovementStats::Engine`
or want their own translation unit. Leaning toward a sibling file: `MovementStats` is already 2600
lines and is about *params*, not *pixels* — mixing concerns there makes both harder to read.

### 3.4 The Learn button

Matches `PredictiveNotesNode`'s precedent exactly: `bool IsLearning()`, `SetLearning(bool)`. While
learning, the node accumulates histograms into the live profile at the §2 rate limit. Unlike
Predictive Notes (which learns a fixed capture then stops), Learn here **toggles accumulation into
the global cross-session profile** — turning it off doesn't discard anything, it just stops feeding
new evidence in, matching Drift's `frozen` semantics more than Predictive Notes' one-shot capture.

## 4. The node itself

**Params (`VisitParams`): exactly one.** `float mix = 0.0f; // 0 = pass-through, 1 = full graded`.
Plus the Learn toggle, which is UI state more than a "creative" param (compare `frozen` on Drift,
which is saved but not something you'd automate).

```
v.Float("mix", mix);
v.Bool("learning", mLearning); // whether accumulation is currently on; see §3.4
```

**`CookIfNeeded(frameId)`:**
1. Pull input texture.
2. Rate-limited: downsample + readback + histogram (§2). If `mLearning`, fold into the global
   profile (§3.3).
3. Re-fit the 13 operator values from (live histogram, target) whenever the target or live profile
   changed meaningfully (not every frame — the fit is cheap but pointless to rerun on an unchanged
   histogram; cache and compare a version counter, the same pattern `CurvesNode::mLutVersion` uses
   for its LUT rebuild).
4. Apply the §1 pipeline as a single fragment shader (one pass, all 13 operators folded into one
   shader program — this is a per-pixel GPU cost like `CurvesNode`'s LUT sample, not a CPU cost),
   blended against the untouched input by `mix`: `outColor = mix(inColor, gradedColor, mix)`.

This makes the shader itself unconditional and simple — always the same 13-operator pipeline,
values driven by the CPU-side fit each time it updates, `mix` as the shader's last line. No branchy
GLSL, no per-frame CPU color math beyond the periodic re-fit.

## 5. Traps

| Trap | Why |
|---|---|
| Linear vs. gamma-encoded exposure | Exposure and levels must operate on (approximately) linear light. Applying a `2^stops` gain directly to sRGB-encoded values shifts contrast and looks wrong — decode to linear (or use `pow(c, 2.2)` as the practical approximation Infinite likely already uses elsewhere in its pixel pipeline; **check what `FieldPixelNode`/existing shaders assume about gamma before picking one** — this file does not verify that assumption). |
| Hue as a linear mean | Averaging hue values directly is wrong at the wraparound (359° and 1° average to 180°, the opposite of "nearby"). Use the circular mean (average of `(cosH, sinH)`, then `atan2`). |
| Single global histogram hiding two different "looks" | If the owner grades warm indoor footage and cool outdoor footage in the same session, one EMA-blended target averages them into neither. Not solving this in v1 — flag it as a known limitation, the same way `MovementStats` flags patch-left-running-overnight skew, and revisit with something like §7c's session-map idea (a per-section target) only if it turns out to matter in practice. |
| Percentile from a coarse histogram | 32 bins gives ±3% percentile resolution — fine for black/white points, not for anything finer. Do not be tempted to fit whites/blacks from the same histogram at higher precision than it has. |
| Re-fitting every frame | The histogram changes a little every frame (video is not static); re-running the 13-stage sequential fit every frame is wasted work and can make `mix`-blended output visibly flicker as the target chases noise. Fit on a slower cadence than the histogram itself updates (e.g. re-fit at 2 Hz even if the histogram accumulates at 30 Hz), the same throttle-inside-a-throttle idea `ImageAnalyzeNode` already applies once. |
| Treating this as a `CurvesNode` extension | Curves has no named "highlights" or "vibrance" control to predict onto — it's raw bezier points. Do not try to retrofit predicted values onto `CurveShape::Point`s; build the 13-operator shader as this node's own thing. |

## 6. Test

No existing self-test harness name reserved yet — propose `INFINITE_PREDCOLORTEST`, following the
`INFINITE_<NAME>TEST` convention (`main.cpp:63880` and neighbors).

1. Feed a synthetic gradient image with a known histogram (e.g. a known mean/std per channel);
   verify the fitted exposure/contrast/black-white values match the closed-form §3.2 formulas
   within tolerance.
2. Self-normalize mode on a deliberately underexposed synthetic frame (`mean Y ≈ 0.2`) raises `mean
   Y` toward the target without clipping highlights that weren't there to begin with.
3. Match-reference mode: two different synthetic gradients, verify the fitted transform moves
   source's mean/std onto target's mean/std (the Reinhard-style transfer's own correctness
   condition).
4. `mix = 0` is bit-identical to the untouched input (pass-through has no shader error diffusion or
   rounding drift).
5. Learn toggled on then off across two save/load cycles: the global profile survives, matching
   `MovementStats`' `PREDBINDTEST`-style save/load coverage, not a per-patch round trip.

## 7. Exit criterion

`PREDCOLORTEST` passes; `node-ui-pillars` checklist clean in both themes (one knob, one toggle —
should be the easiest node in the category to get right); the owner has actually graded a real
image with it and confirms Learn + Mix behaves the way "it learned my taste" is supposed to feel,
per memory (`feedback_no_control_your_mac`) the owner verifies by hand, not by UI-scripting the
canvas.
