# Field Coder: a small local model that writes Infinite

Status: sections 1-7 signed off 2026-11-04. Speed probe done. Paused behind the agent collaboration layer (docs/plans/agent-collab), which it later plugs into.
Ships as an optional pack through the Extensions framework (`docs/plans/extensions/README.md`), like Tracking.
New UI (a prompt line in nodes) is shown to the owner and approved before it is built.

## 1. Goal

A specialist model, running on the user's machine, that turns a precise request into a working change in the open
patch: node code (Field, Sketch JS, Sketch 3D JS), a parameter edit, or a small graph edit. It is faster than a
frontier model at every step, iterations included. General agents (Claude Code in Infinite's terminal, or any other)
plan; Field Coder executes.

## 2. Problem

| Today | Cost |
|---|---|
| Claude Code in the terminal writes code and patches | 4-6 turns per task (agent-language pilot), 20-60 s, per-token billing, needs network |
| Frontier models do not know Field or the p5/three-style Sketch APIs | They call functions that do not exist, then repair over more turns |
| Every iteration ("more rings", "thinner") resends the whole context | Iteration, the thing users do most, is the slowest part |

## 3. Scope

In (v1):

| Task class | Example | Output |
|---|---|---|
| A. Param edit | "brick texture on this cube" | `Texture.type = Brick`, wire to material |
| B. Small graph edit (1-3 nodes) | "sculpt this cube into a sphere" | insert/swap nodes + params |
| C. Node code | "concentric circles around this circle" | Field Pixel body |
| D. Fix | compile error text | corrected code |
| E. Iterate | "8 rings, thinner, slower" | diff on the current code |

Out (v1): whole patches from scratch, taste-only prompts ("more ethereal"), arrangement, mixing. Those stay with
the general agent, which may call Field Coder for each piece.

Languages, in order: Field (all domains) → Sketch (JS, p5-style, quickjs) → Sketch 3D (JS, p5 WEBGL-style: box, sphere,
translate, beginShape). Real three.js is not supported (needs the CEF Web pack, not built).

## 4. Existing constraints

| Constraint | Effect |
|---|---|
| Main machine M2 8 GB | Model ≤ 1.5B at 4-bit; loaded on demand, unloaded when idle |
| Audio is top priority (perf initiative) | Model runs on its own thread, never the audio thread; GPU work yields to the frame |
| Core stays 15-25 MB | Runtime + weights only in the pack |
| Every code node already has a headless checker | Field compile (`--validate`), `SketchCheck`, `--frame`, `--audio-summary`: these are the oracles and the data filter |
| `infinite` command + RPC exist in the terminal (`docs/plans/terminal`) | Field Coder is exposed as a verb there, no second setup |
| Student candidates (Apache 2.0, ≤ 2.3B) | Qwen3.5-0.8B, Qwen3.5-2B (newest, same tokenizer: 0.8B can draft for 2B); Qwen2.5-Coder-1.5B as the control; not Qwen2.5 3B (research license) |
| Students ruled out | LFM3 / LFM2.5 (custom licence, read before any use); LFM2.5-8B-A1B and Gemma-4-E2B (fast per token but 4-5 GB of weights, too big beside a patch on 8 GB) |
| Teachers free (Apache 2.0) | Qwen3-Coder-Next (80B MoE), Qwen3.5-35B-A3B, Gemma-4-26B-A4B on Colab A100 80 GB; compiler-filtered |
| Seed data found | `ccore/ThreeJS-3D-Scenes` (Apache 2.0): geometry only, translated from three.js to Sketch 3D's p5 WEBGL calls and re-checked; materials and lights dropped; ShaderToy sets are mostly CC BY-NC-SA, so not used |

## 5. Hypothesis

A 0.5B-1.5B model fine-tuned only on compiler-verified Infinite tasks, decoding under a grammar, with the static
prompt pre-cached, matches a frontier model's pass rate on precise single-step tasks and beats it on wall-clock time,
and beats it by a wider margin on iterations.

Why iterations favour local: the node's current code and the system prompt are already in the model's KV cache, so
an iteration only reads the new sentence and writes a short diff. A remote model re-reads everything over the network
each time.

## 6. Baseline (recorded once, before any training)

Same 300-task suite (section 8), same machine (M2 8 GB, app pinned by checksum), recorded once:

| Arm | What runs |
|---|---|
| F1 | Frontier API, fast tier (Haiku class), one shot + one auto-fix, Infinite skill in the prompt |
| F2 | Frontier API, strong tier (Sonnet class), same |
| F3 | Claude Code in Infinite's terminal, normal agent loop |
| S0 | Stock Qwen2.5-Coder-1.5B, no fine-tune, same harness as the student |
| H | Headless engine alone: `--validate`, `--frame`, `--audio-summary` time per task, process start included; plus live RPC apply time |

Recorded per task: pass (oracle), time to first token, tokens out, end-to-end time (prompt → visible change),
retries, cost. H tells us how much of the end-to-end time is the engine, not the model.

## 7. Success metrics (the ship gate)

| Gate | Pass |
|---|---|
| Speed, first change | Median end-to-end ≤ F1 on the held-out tasks, every level |
| Speed, iteration | Median iteration ≤ 1 s for classes A, B, E; ≤ 2 s for C |
| Quality | Pass rate ≥ F1 on every level; ≥ 95 % on levels 0-2 |
| Fit | Running beside the reference patch: 0 audio dropouts, frame time within 1 ms of no-model |
| Size | Pack ≤ 1.1 GB; RAM while generating ≤ 1.4 GB |

Missing any one gate means it does not ship; the numbers are kept.

## 8. Methodology

### Task ladder: fundamentals first

Prompts use technical language from the start. Vague prompts come only after every lower level passes.

| Level | Visual | Sound | 3D |
|---|---|---|---|
| L0 Primitive | circle, square, line, triangle, gradient, solid colour | sine, square, saw, noise, silence | cube, sphere, plane, torus |
| L1 Param | radius 0.3, stroke 2 px, hue 200 | 440 Hz, -12 dB, pan left | scale 2, rotate 45° Y, brick texture |
| L2 Compose | concentric circles (n), grid, stripes, polar repeat | chord, ADSR envelope, LP filter at 800 Hz | cube → sphere, subdivide 3, array of 5 |
| L3 Motion | rotate at 0.5 Hz, pulse, scroll | LFO on cutoff, tremolo, arpeggio | spin, orbit camera |
| L4 Reactive | rings scale with bass band | sidechain duck from kick | mesh displaced by envelope |
| L5 Fix | given broken code + error | same | same |
| L6 Iterate | 3-5 follow-ups on an L2-L4 result | same | same |
| L7 Vague (later) | "more ethereal" | "warmer" | "softer" |

About 300 tasks across L0-L6: 200 dev, 100 held out, frozen before any training. Same split for every arm.

### Oracles: measured, not judged by eye

| Task | Predicate (from `--frame` / `--audio-summary`) |
|---|---|
| Circle r=0.3 | Radial profile edge at 0.3 ± 0.01 |
| Concentric circles n=8 | 8 peaks in the radial profile, even spacing ± 5 % |
| Brick texture | Texture node type = Brick, connected; 2D FFT shows the brick period |
| Cube → sphere | Vertex distance to centre: std/mean < 2 % |
| 440 Hz sine | Pitch 440 ± 1 Hz, THD < 1 % |
| LP at 800 Hz | −3 dB point 800 ± 10 % on white noise in |
| Iteration "thinner" | Ring width falls; ring count and colour unchanged |

Patches may differ from any reference; only the predicates count.

### Data

```
 task templates (ladder × params × phrasings)
        │
        ▼
 teachers (Qwen-Coder-32B, gpt-oss-20b; Colab)  ── many candidates per task
        │
        ▼
 headless checker + oracle   ── reject fails, keep passes
        │
        ▼
 dedupe, balance by level   ── ~2k pairs (gate 1) → 5-10k per language
        │
        ▼
 QLoRA on Qwen2.5-Coder 0.5B and 1.5B  (MLX on the M2, or Colab)
        │
        ▼
 4-bit GGUF  →  benchmark on the held-out 100
```

Iteration pairs come from chaining: result of task k plus a follow-up phrase, with the oracle checking both what
changed and what must not change.

### Speed engineering (part of the method, not an afterthought)

| Lever | Saves |
|---|---|
| Static prompt (node facts, API) prefilled once at load, KV cache kept | 1-3 s of prefill per request |
| Grammar-constrained decoding (GBNF per output class) | Retries for syntax errors: zero |
| Output is the smallest form: param set < graph diff < code diff < full code | Tokens out |
| Apply through the live RPC, not by writing and reopening a file | Process start (130-200 ms per shim call) |
| Router picks class A/B/C before generating | Param edits never write code |

### Benchmarks

| Bench | Measures |
|---|---|
| B-pass | Pass rate per level, per arm |
| B-latency | TTFT, tokens/s, end-to-end, per class |
| B-iterate | Median and p90 per follow-up over 5-step chains |
| B-engine | Headless and live engine time per task (arm H) |
| B-fit | Audio dropouts and frame time with model generating during the reference patch |
| B-orchestra | Claude Code in the terminal solving 10 multi-node tasks, with and without the Field Coder verb: turns, tokens, time |

## 9. Solution trajectories

| Trajectory | Pros | Cons | What if |
|---|---|---|---|
| T1 One 1.5B model, all classes | One pack, best quality | Slower per token | If L0-L2 pass with 0.5B, drop to T2 |
| T2 One 0.5B model | ~2× faster, 400 MB | Weaker on L3-L5 | If it fails L4, add T3 |
| T3 0.5B router + param/graph edits, 1.5B for code | Fast where it can be | Two models in the pack | If RAM is tight, load the 1.5B on demand only |
| T4 No model: templates + search over existing presets | Instant | No new code | Use it as the fallback when the pack is absent |

Start with T1 and T2 trained on the same data; decide from B-latency and B-pass.

### How it plugs in: multi-agent

```
 user ──► Claude Code / any agent in Infinite's terminal   (plans, taste, whole patches)
                 │  calls, many times per task
                 ▼
          infinite code "<precise request>" --node <id>     also MCP tool infinite_code
                 │
                 ▼
          Field Coder (in the app process, pack loaded)     (executes one step, ~1-2 s)
                 │ param set / graph diff / code
                 ▼
          live RPC apply ──► compiler + oracle check ──► result + error back to the caller

 user ──► prompt line in a node ──► Field Coder directly    (no agent needed)
```

### Node → terminal reference (owner request, 2026-11-04)

Every node's right-click menu gets **Reference in Terminal**. It types a reference into the active terminal tab's
input line (no Enter), so whichever agent runs there knows exactly what the user means:

```
right-click node ──► Reference in Terminal
                         │ TermSession::SendBytes (existing), no newline
                         ▼
 > make @node:12 rings thinner          ◄── user keeps typing around it
                         │
 agent reads @node:12 ──► infinite node show 12   (type, params, code, wires; live RPC)
                     └──► infinite code "..." --node 12
```

| Decision | Choice | Why |
|---|---|---|
| Token | `@node:<id>` plus type and title, e.g. `@node:12 (FieldPixel "rings")` | Readable for the user, exact for the agent, uses the id the `infinite` verbs already take |
| Several nodes selected | One token per selected node, space separated | Same action, no extra menu |
| No terminal open | Opens the panel and a tab in the patch folder first | No dead click |
| Ids after save/reopen | Agents re-resolve through `infinite node show`; a stale id returns a typed error | Never acts on the wrong node |
| Also the prompt for Field Coder | `infinite code` accepts the same token | One way to point at a node everywhere |

No second setup: the "Connect terminal & agent" button already registers the `infinite` command and MCP server;
`infinite code` appears there once the pack is installed, and the Infinite skill tells agents to use it.

## 10. Benchmarking against the baseline

Baseline arms not run yet. Results go here, linked to rows in sections 6 and 7.

### Speed probe, 2026-11-04 (step 2; not the baseline)

M2 8 GB, llama.cpp b12099 (Metal), Q4_K_M, stock models (no fine-tune, no grammar), ~1.5k-token static prompt
(first 6 KB of the field-language skill), Claude app busy in the background (~2 cores). Times only; outputs not
checked. Script: `probe.py` (scratchpad, not kept).

Raw speed (llama-bench):

| Model | Read prompt (tok/s) | Write (tok/s) |
|---|---|---|
| Qwen3.5-0.8B | 1,660 | 85 |
| Qwen2.5-Coder-1.5B | 690 | 58 |
| Qwen3.5-2B | 700 | 45 |

End-to-end, seconds (prompt sent → last token):

| Task | 0.8B | Coder-1.5B | 2B | 2B + 0.8B draft |
|---|---|---|---|---|
| First request (static prompt not cached yet) | 2.1 | 2.9 | 2.6 | 2.6 |
| A param edit (brick texture) | — | — | — | — |
| B graph edit (cube → sphere) | **0.8** | **0.6** | **0.7** | 0.8 |
| C code (circle + concentric rings) | 2.6 | 4.3 | 3.6 | 3.6 |
| E iteration 1 (thinner) | 3.6 | 3.9 | 3.8 | 3.8 |
| E iteration 2 (10 rings, pulse) | 4.4 | 3.2 | 4.0 | 3.8 |

(Row A ran as the cold first request, so its time is the first row.)

Reading:

| Finding | Effect on the plan |
|---|---|
| Static prompt costs ~2-3 s once, ~0.1 s after (cache works) | Prefill at pack load; users never pay it |
| Param and graph edits already ≤ 1 s with stock models | Gate met for A/B before any training |
| Stock models write long, full rewrites (all hit the 160-token cap) | C and E miss the ≤ 2 s gate. The fix is output size: fine-tune for short code and iterate by diff (~20-60 tokens), which at 0.8B speed is ~0.5-1 s |
| Speculative draft (0.8B for 2B) gave no gain | Dropped |
| 0.8B is fastest everywhere that matters | Train 0.8B first; 2B only if 0.8B fails quality gates |

Frontier arm F1 not measured yet (needs an API run); gates still compare against it.

## 11. Critique

| Risk | Answer |
|---|---|
| Oracles too weak, the model learns to game them | Each level has a "must not change" predicate; spot-check 5 % of passes by picture |
| Teachers share blind spots, data lacks variety | Two teacher families plus templated params; held-out tasks written by hand |
| Frontier speeds improve and the gate moves | Gate is against F1 measured at baseline time; re-baseline only with an owner decision |
| 8 GB too tight with heavy patches | B-fit uses the heaviest reference patch; fall back to T2 |
| Field changes break the model | Freeze Field during training; the data pipeline reruns from templates on any Field change |

## 12. Next steps

1. Owner signs off sections 1-7.
2. Speed probe (no training): llama.cpp bench of stock Qwen3.5-0.8B, Qwen3.5-2B (alone and with 0.8B drafting) and
   Qwen2.5-Coder-1.5B on the M2, plus arm H. Kill early if no stock model can reach the iteration gate even with
   prefix caching.
3. Write and freeze the 300-task ladder and oracles; run the baseline arms once.
4. Data generation on Colab, level by level, L0 first.
5. Train T1 and T2, benchmark, write section 10, decide ship / iterate / abandon.
