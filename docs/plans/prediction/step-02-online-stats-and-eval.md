# Prediction step 2: online statistics and the evaluation gate

Builds the per-param statistics the Drift model reads (README §3.3), and the offline evaluator that
decides whether Drift is worth building (README §4.3). **No node, no UI, no change to modulation.**

Line numbers are from commit `35221c1`; re-grep the symbol if one has drifted.

## Start

Prereq: step 1 merged (`MOVELOGTEST` green).

```bash
git switch main && git pull --ff-only
git switch -c feature/prediction-step-02-online-stats
```

Skills: `codebase-navigation`, `invariant-interaction-audit`.

## Files to read first

| File / symbol | Why |
|---|---|
| [README.md](README.md) §3.3, §4.1–§4.3, §5 (weights), §6 (readiness table) | the maths this step implements |
| `src/core/MovementLog.h` (step 1) `Capture`, `Source`, `Flags`, `ReadFile` | input stream, live and offline |
| `src/core/Transport.h` `IsPlaying()` | "playing time" for forgetting |

## What to build

### 2.1 `src/core/MovementStats.h/.cpp` (new pair, add to `CMakeLists.txt`)

One `ParamStats` per key `(uid, paramIndex)`, plus one per profile key `(nodeType, paramIndex, name)`
for step 5.

```cpp
struct ParamStats {
   // AR(1) sufficient statistics on the 10 Hz grid, exponentially forgotten
   double W, Sx, Sy, Sxx, Sxy, Syy;
   float hist[64];            // dwell landscape, same forgetting
   float releaseVel;          // last hand release velocity (pos/s), from the last 100 ms
   double nEff;               // sum of weights = confidence
   double activeSeconds;      // playing time seen
};
```

- **Grid tick:** every 100 ms of *active* time (transport playing, or any hand move in the last 30 s),
  for each key seen this session, take the current pos (sample-and-hold) and update with weight `w`
  from README §5 using the source that owned the key in that 100 ms. Weight 0 for untouched
  predictions.
- **Forgetting:** multiply all sums by `λ = 2^(−Δt_active / H)`, H = 2 weeks of active time. Apply it
  lazily at read time from `activeSeconds`, not per tick for every key.
- **Derived values (pure functions):**
  `φ = cov(x,y)/var(x)` clamped to [0.5, 0.999]; `θ = −ln φ / dt`; `μ = c/(1−φ)`;
  `σ² = var(e)·2θ/(1−φ²)`; `p̂` = hist smoothed with a Gaussian kernel (σ = 2 bins), normalised;
  observed range = 2nd/98th percentile of `p̂`.
- **Enum/bool keys** are skipped.

**Where it runs:** `MovementLog::Capture` forwards the same pos/source stream to
`MovementStats::Observe`, all on the main thread. O(1) per key per tick; keep it in the step-1 perf
budget.

### 2.2 Persistence

`AppSupportDir()/movement-log/stats.bin`: every `ParamStats` plus profile stats, written by the step-1
writer thread every 60 s and on `Stop()` (the main thread hands it a copy). Versioned header. A
corrupt file means start empty; never crash on it.

**Retention gate (closes a step-1 gap).** Step 1's `EnforceRetention` deletes the oldest `.mlog.z`
files once the folder passes its cap, without knowing whether their data reached `stats.bin`. Fix it
here: `stats.bin` records the name of the newest session file whose rows it has absorbed
(`lastConsumed`); `EnforceRetention` may delete only files at or older than that. Files newer than it
are never deleted, even if the folder is over the cap. Test: with the cap at 1 MB, a file not yet
consumed survives, and the same file is deleted once `lastConsumed` has passed it. `stats.bin` and
the step-1 test folder are still never pruned.

### 2.3 Offline evaluator: `tools/prediction/eval.py`

Reads `.mlog` files (a Python port of `ReadFile`, or call the step-1 dump flag). For every key with
≥ 5 min active hand time:

1. Split by time: first 80% train, last 20% test.
2. Fit the stats from 2.1 on the train part (same maths, reimplemented in numpy).
3. For each test step, score the NLL of the value 1 s ahead under:
   - **B0 hold:** Gaussian at the current value, variance = the training set's empirical 1 s change variance;
   - **B1 range:** uniform over the observed range;
   - **Drift:** 64 simulated paths of README §4.1 for 1 s → Gaussian KDE → density at the truth.
4. Print one table per key and a summary: the share of keys where Drift beats both baselines.

## Traps

| Trap | Why |
|---|---|
| Counting dwell twice | Grid samples are already time-weighted; do not also weight the histogram by dwell (README §4.2 Home). |
| φ → 1 | Knob data is mostly still, and μ = c/(1−φ) explodes. Clamp φ. |
| Forgetting wall-clock time | A laptop closed for a month must not erase everything. Use active time. |
| Train/test leakage | Split by time, never by random rows (neighbouring rows are near-identical). |

## Test: `INFINITE_MOVESTATSTEST`

Feed a synthetic OU stream (known θ = 0.5 /s, μ = 0.3, σ = 0.1) through `Observe` for 20 simulated
minutes. Assert the recovered θ is within ±20%, μ within ±0.05, and σ within ±25%. Feed a two-spot
dwell pattern and assert `p̂` has two peaks at the right bins. Add the test to `GROUP_MODULATION`.

## Exit criterion (a go/no-go gate for the whole plan)

`MOVESTATSTEST` passes, and `eval.py` has been run on **at least two weeks of the owner's own logs**.
If Drift beats both baselines on most hand-moved keys, continue to step 3. If not, stop and revisit
README §4 before writing any node.
