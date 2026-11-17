# Quality system

One registry, one row format and one scoreboard for every measurement of Infinite. Started 2026-10-13
with Synths as the pilot. Owner skill: `quality-system`.

## Why

Four efforts measure Infinite, and before this each had its own format, tracker and verdict:

| Effort | Question it answers | Lives in |
|---|---|---|
| Node audits | Is each node correct, by physics or by its own spec? | `docs/plans/node-audits/` |
| Re-survey | What does "good" look like in the apps people compare Infinite to? | `docs/plans/re-survey/`, `tools/re-survey/` |
| Benchmark suite | Is it fast enough on the base M2 8 GB laptop? | `docs/plans/perf/`, `tools/bench.py` |
| Driver checks and sweep skills | Did anything break? How do I investigate this area? | `.claude/skills/run-infinite-hygiene/driver.sh`, `*-sweep` skills |

They stay separate tools. What they share now is the metric: the same id, the same row, one place for targets.

## The loop

```
re-survey ──► competitor numbers set the TARGET
    │
    ▼
registry.tsv (id, pillar, area, unit, better, target, gate, source, exceptions)
    │
    ├── node audits ─┐
    ├── bench suite ─┼──► QUALITY rows ──► quality.py ingest ──► results.jsonl ──► SCOREBOARD.md
    └── probes ──────┘
                         QUALITY rows ──► quality.py check ──► driver.sh fails on a release-gated miss
                                                                │
                                                                ▼
                                       the sweep skill for that area says how to investigate
```

## Files

| File | What | Edited by |
|---|---|---|
| `registry.tsv` | One row per metric. Owns target, gate and exceptions | Hand, one row per new metric |
| `results.jsonl` | Worst value per (metric, subject) per recorded run, with UTC time, commit and machine | `quality.py ingest` only |
| `SCOREBOARD.md` | Status per pillar and metric; Infinite's worst against the best other app | `quality.py board` only |
| `tools/quality/quality.py` | `check`, `ingest`, `board`, `--self-test` | Code |

## The row

```
QUALITY id=synth.alias_db subject="node:Analog" value=-66.2000
```

- `id`: a registry id. An unknown id fails `check` and `ingest`, so the registry stays the one owner.
- `subject`: `node:<Name>` for Infinite's nodes, the tool's own label for anything else (`Vital Audio: Vital`).
- A producer may print many rows for one (id, subject), for example one per note or sample rate. The tool keeps
  the worst by the metric's `better` direction.

| Producer | How it emits |
|---|---|
| SYNTHAUDIT (`src/app/selftest/SynthAudit.cpp`) | `Quality(id, node, value)` next to each measurement, on stdout |
| `tools/re-survey/synth_probe.py` | `quality.txt` beside `results.json` (helper `common.quality_line`) |
| Benchmark suite (`tools/bench.py quality <results.jsonl>`) | Reads `BENCH_JSON` lines; `run_all.sh` writes `<run>.quality.txt` beside each run; `bench.py b9` prints rows after its report |

### Benchmark rows and the trust rule

Bench subjects are `bench:<B>:<variant>` (`bench:B3:s`, `bench:B1:buf256`, `bench:B8:2x1080+3w:w0`); the
board counts them as Infinite's own, like `node:`. The perf README's trust rule says an untrusted run
(unfocused, unpaced, overlapping windows) can only look better than the real thing, so its value inside a
limit is "unproven", never a pass. `bench.py quality` therefore **prints trusted rows only**: a row whose
variant carries `unfocused=1`, `unpaced=1` or `overlap=1` (the marks `scripts/bench/compare.py` owns), or a
B6 `vsync=0` row (pure work time, a lower bound), is skipped with a note on stderr. A metric whose only runs
are untrusted stays **unmeasured** on the board (B7 today). An untrusted miss is not lost: `compare.py`
still prints it. `--untrusted` adds those rows under a `:untrusted` subject suffix for reading; never ingest
that output.

Bench metrics are not run by `driver.sh`. Check a whole-suite run with `check --source bench`; a partial run
(`run_all.sh --only ...`) is checked without `--source`, since the benches it skipped emit nothing.

Two long runs are opt-in and stay out of a default run. ROBUSTNESS.md R5 is `run_all.sh --r5 --only B6`: B6 at
4000 nodes, the usual pan/zoom/drag/dropdown script plus `mode=select` (select-all and clear alternate) with one
delete-all through the Delete-key path (`INFINITE_BENCH_B6DELETE=1`). Rows above 1000 nodes go to `bench.r5_*`,
never to the 200-400 node `bench.b6_pan_*` targets. R8 is `run_all.sh --soak-long --only B7` (B7 for 480 min);
its rows are the B7 metrics under subject `bench:B7:s:480min`.

## Gates

- `release`: the driver runs `quality.py check --source <CHECK>` on every check whose log has QUALITY rows. A miss
  fails the check unless the subject is listed in `exceptions` with a finding id. It also fails when a release
  metric owned by that check produced no rows at all, so an audit cannot go quiet unnoticed.
- `report`: shown on the scoreboard as `behind`; never fails a run. Competitor comparisons, CPU timings and anything
  still above its target start here and move to `release` once Infinite meets the target, so it stays met.
- A FLAG line inside an audit is that audit's own pointer for investigation. The registry target is the gate.

## Day to day

```bash
python3 tools/quality/quality.py check --source SYNTHAUDIT /tmp/infinite_test_SYNTHAUDIT.log
python3 tools/quality/quality.py ingest /tmp/infinite_test_SYNTHAUDIT.log --note "after F11"
python3 tools/quality/quality.py board
```

Record (`ingest`) after a fix, a new competitor run or a release, not after every run.

## Design lints that read a generated registry (not QUALITY rows)

`tools/design/density_lint.py` (panel row pitch, controls per row, control height) is not a quality row
producer and does not feed `registry.tsv`. Its input, `ui-audit-view2d/registry.json`, is written by
`tools/ui-audit/run.py --view2d`, which runs the debug app (`scripts/ibuild.py build --mode dev`) and
is not committed. Without that file the lint exits 2 and prints the command to make it. Do not
hand-write the registry: its rows are the app's own control boxes.

## Rollout

| Step | Area | State |
|---|---|---|
| 1 | Synths: SYNTHAUDIT + `synth_probe` (Diva, Serum 2, Vital) | done 2026-10-13 |
| 2 | Benchmark suite (`tools/bench.py quality`, trusted rows only): 26 P8 metrics, baseline `bench/baselines/m2-8gb.jsonl` ingested (18 met, 2 behind: B8 heavy windows); B7 and R5 unmeasured until a focused run; R5/R8 behind `run_all.sh --r5` / `--soak-long` | done 2026-10-13 |
| 3 | Re-survey areas: each block ends in registered metrics plus a short dossier | after 2 |
| 4 | Node audits: FXAUDIT, NOTEAUDIT, IOAUDIT, GEOAUDIT, COMPAUDIT emit `Quality(...)` rows (16 metrics, first rows 2026-10-13); more are added as each audit grows | started 2026-10-13 |
