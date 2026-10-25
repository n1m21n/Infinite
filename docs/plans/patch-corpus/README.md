# Patch corpus: generating thousands of human-quality .inf patches

Status: design, written 2026-10-25. Nothing built yet. Decisions below are settled; change them only with a measured reason.

## Why this exists

Infinite's long-term position is a base layer that agents and tools depend on (pyramid: engine, language, agent loop, yardstick, library, instrument, brand). This plan is Level 5, the library: enough real, long, human-like `.inf` patches that models write the format natively and the library has depth.

## Problem statement

Produce 1,000+ patches that are valid, deterministic, long (12+ nodes, with modulation), diverse and structurally indistinguishable from patches a person made. Do it cheaply per patch, behind a measurable pass/fail gate, and publish them so agents and models can learn `.inf`.

```
                 GOAL: thousands of human-quality patches
                                 |
   +----------+----------+-------+----+-----------+------------+
   P1 SEEDS   P2 FORMAT  P3 GENERATE   P4 FILTER   P5 JUDGE     P6 PUBLISH
   real       lossless   structure +   drop broken is it good   scrub paths,
   taste      read/write parameters    and boring  and human?   licence,
              of every                                          distribution
              record
```

| # | Sub-problem | State on 2026-10-25 |
|---|---|---|
| P1 Seeds | about 100 complex human patches | thin, mostly one person's taste |
| P2 Format | Tongue reads 0 of them | needs a lossless reader/writer; `canonicalize` and `--explain` exist |
| P3 Generate | the T2 fuzzer made valid but dull 3 to 4 node chains | open |
| P4 Filter | strict validate, render stats, audio summary, determinism check exist | mostly built |
| P5 Judge | no definition of "human-like" | open, the hardest part |
| P6 Publish | library, `catalog.json`, `llms.txt` exist | needs scrubbing and licensing |

## Measured facts (2026-10-25)

Corpus census with `tools/agentlang/codec.py encode` over every `.inf` on the machine.

| Set | Unique patches | Median nodes | Largest | Use modulation | Tongue round-trips |
|---|---|---|---|---|---|
| All found | 628 (1,233 files) | | | | 24% |
| Personal (Downloads, Desktop, subfolders) | 56 | 27 | 92 | 100% | 0% |
| Community (website/assets/community) | 24 | 32 | 94 | 100% | 0% |
| Templates | 73 | 7 | 15 | 97% | 3% |
| Per-node demos | 307 | 3 | 7 | 13% | 0% (a bookkeeping `rev` record) |
| Patches with 12+ nodes | 50 personal | | | | 0 of 50 |

Why Tongue cannot read them (personal set, of 56): wire with extra fields 49, `mod` 45, `transport` 42, `glob` 40, Audio Out flags and `recordDirectory` 40. Many of these are saved state, not creative content.

Coverage if Tongue kept adding support, patches with 12+ nodes: mod/expr/glob 16%, plus rev/transport/flags 16%, plus unwritable keys and wire fields 36%. The remaining records (`stream`, `gesture`, `arrange`, `uid`) are a long tail. Conclusion: a sentence language over `.inf` is the wrong tool for this goal.

Recurring structure in the 50 complex personal patches (wire direction corrected: the first number in a `cable` record is the destination):

| Pattern | Patches |
|---|---|
| Note Sequencer to Note Transpose to Slicer | 10 |
| Note Sequencer or Note Stack to Oscillator | 7 each |
| color adjustments and Blend stacks | 4 to 8 |
| Image Source to Material to Transform or Displacement | 2 |

Only 42 of 317 distinct two-node pairs (13%) and 10 of 395 three-node chains (3%) appear in 3 or more patches, so counting alone will not mine good macros from 50 patches. Most used types: Audio Out 37, Reverb 36, Delay 33, Mixer 33, Sampler 29, Oscillator 28, LFO 27, PaulStretch 25, Random 24.

Tongue result, for the record (docs/plans/agent-language): on 10 held-out small tasks it used about 0.48x the fresh tokens of plain `.inf`, verdict KEEP-GOING for 2 of 3 models, T4 cold-write passed only after a SPEC fix. It stays as an optional small-edit dialect. It is not the generation approach.

## Structure

```
 +-------------+   +--------------+   +----------------+
 | Human seeds |-->| P2 Lossless  |-->| Caption + tag  |
 | (~100)      |   | graph model  |   | (render + audio|
 +-------------+   | (all records)|   |  summary -> LLM)|
                   +------+-------+   +--------+-------+
                          | motifs             | descriptions
                          v                    v
                  +-----------------------------------+
                  | P3 GENERATORS                      |
                  |  A retrieve nearest + agent edits  |
                  |  B quality-diversity mutation      |
                  |  C agent from intent + motifs      |
                  +----------------+------------------+
                                   v
                  +-----------------------------------+
                  | P4 FILTER (automatic)              |
                  | validate, render, audible,         |
                  | determinism, dedupe by structure   |
                  +----------------+------------------+
                                   v
                  +-----------------------------------+
                  | P5 JUDGE                           |
                  | stats match, classifier fooled,    |
                  | owner rates a sample               |
                  +-----+-------------------+---------+
                    pass|                   | fail: adjust operators
                        v                   +--------> back to P3
                  +-----------------------------------+
                  | P6 PUBLISH                         |
                  | scrub paths, licence, library,     |
                  | catalog.json, llms.txt             |
                  +-----------------------------------+
```

## The patch universe (added 2026-10-25, owner's design)

Generator B is replaced by an indexed, typed grammar over the whole node vocabulary. Variety first, quality second: most sounds will be absurd, and that is fine. The universe is never listed, only indexed (code string <-> number), so it can be sampled uniformly and counted.

### Music orders

| Order | Name | Rule |
|---|---|---|
| M1 | Linear spine | N > S > E > E > ... > Out, no branches |
| M2 | Linear + modulation | an M1 spine where any param of any node may be modulated |
| M3 | Multi-spine | several spines joined at a common node (Mixer) |
| M4 | Multi-spine + modulation | modulation that crosses spines |
| M5+ | open | grow only after M4's stats are measured |

Visual orders are decided separately, then a combined order (X) where music and visuals interact. Not defined yet.

### Notation (one-way: it writes `.inf`, never reads it back)

```
N3.17>N9.2>S4.203>E7.9>E2.41>O ; L2.5@2.cutoff
```

| Part | Meaning |
|---|---|
| letter | pool: N note, S synth, E audio effect, L modulator, O Audio Out (X mixer for M3) |
| first number | node type, position in the pool's sorted list |
| number after the dot | param seed (0 = defaults) |
| `; Lm.s@k.key` | modulator m (seed s) drives param `key` of chain node k |

Why one-way: Tongue failed on the bookkeeping records (`flags`, `rev`, `transport`, `glob`) because it had to read every patch. A compiler that only writes never meets them. The string is also the genome for mutation and the atlas key.

### Funnel (cheapest first)

| Stage | Filter |
|---|---|
| F0 | grammar: pin types, merge rules (by construction) |
| F1 | static rules: param ranges, dead pairs from the atlas |
| F2 | probe render, a few seconds of audio: silent, NaN, clipped |
| F3 | full render and determinism re-render |
| F4 | learned filter: predicts F2/F3 from the code string, skips renders |
| F5 | judge: stats against the human range, owner rates 50 |

Absurd but valid is kept. Silent, NaN, runaway, non-deterministic or hanging output is dropped, and each class is a finding (engine bug or generator bug).

Built: `tools/universe/` (`universe.py`: pools, compiler, probe runner; `describe_all.json`: every node type's params, ranges and pins from `Infinite --describe`).

## Generators (earlier plan, superseded by the universe for B)

```
A RETRIEVE + EDIT            B QUALITY-DIVERSITY           C AGENT FROM INTENT
 "dub techno sampler"         seed = a real patch           "build X" + motif library
        |                            |                               |
 find nearest 3 patches       mutate on the real graph      agent composes motifs
        |                     (insert effect, swap node,    and sets params
 agent edits the graph         add LFO to param, duplicate          |
        |                      subgraph, merge two patches)         |
        |                            |                               |
        |                     keep a grid cell if valid             |
        |                     AND new in behaviour                  |
        +--------------+-------------+-------------------------------+
                       v
                one shared filter
```

| Generator | Cost per patch | Gives | Weakness |
|---|---|---|---|
| A Retrieve + edit | one LLM call | long, coherent structure | stays near the seeds |
| B Quality-diversity | almost free (CPU) | thousands of varied valid patches | varied is not good |
| C Agent from intent | one larger LLM call | intent-driven novelty | drifts to short dull chains without motifs |

Decision: build A and B first; C only after the judge works.

Quality-diversity details: the behaviour grid is described by features from `--audio-summary` and `--frame` stats (loudness, spectral centroid, luma variance, node count), later by an automatic embedding. Mutation acts on the full graph, so every record (`mod`, `glob`, `transport`, `flags`, `stream`) is preserved untouched. No new language words are needed.

## The judge

```
                      +-- structural stats: node count, depth, % modulated,
generated ----------->|   type mix, motif counts, compared to the human set
                      +-- classifier: human vs generated, held-out patches
                      +-- render/audio: not blank, not silent, not clipped
                      +-- owner rates 50 random ones: keep / meh / bin
                                      |
                  ALL must pass ------+--> done; otherwise adjust and rerun
```

| Gate | Pass |
|---|---|
| Validity | 100% pass `--validate` |
| Non-degenerate | renders, audible, not clipped |
| Determinism | same output on re-render (`tools/determinism-check.py`) |
| Complexity match | node count, depth and modulation share inside the human range |
| Classifier | no better than about 60% at telling generated from human |
| Owner sample | at least 60% of 50 rated keep |
| Diversity | deduplicated by graph structure |

If the gates cannot be reached after real effort, stop and keep the numbers.

## Approaches considered

| Approach | Solves | Does not solve | Decision |
|---|---|---|---|
| Extend Tongue | cheap agent edits | the long tail of records (36% ceiling) | keep for small edits only |
| MatFormer-style model | high-quality graph generation | needs far more data than about 100 patches | later, when the corpus grows |
| Library learning (Stitch) | reusable macros | needs a graph encoding; 50 patches are sparse | run the motif census first, mine later |
| A + B + C on a lossless graph | all of P3 | P5 still needs the judge | chosen |

Prior art: [MatFormer](https://arxiv.org/pdf/2207.01044) (Adobe, SIGGRAPH 2022: staged node, parameter, edge generation that keeps graphs valid), [Stitch](https://arxiv.org/abs/2211.16605v2) (POPL 2023, corpus-guided library learning), [Sound Innovation Engine](https://www.uio.no/ritmo/english/people/postdoctoral-fellows/bthj/publications/sound-innovation-engine-1.0.html) (MAP-Elites over CPPN+DSP graphs) and [automatic behaviour spaces for quality-diversity in sound](https://arxiv.org/pdf/2512.02783). These are read from abstracts and snippets only.

## Decisions

| Question | Decision |
|---|---|
| Representation | the real `.inf` graph, read and written losslessly; Tongue is not used |
| Seeds, in priority order | community patches, then personal patches the owner marks public, then templates |
| Patches that need local samples | excluded from v1 of the published set unless rebuilt with built-in synths or bundled CC0 audio |
| Private paths | scrubbed (`recordDirectory`, sample paths, user names in comments) before anything is published |
| Licence | generated patches CC0; seed patches published only with the owner's per-file OK |
| Provenance | every published patch is labelled generated or human in `catalog.json` |
| First target | 200 patches that pass every gate, then 1,000+ |

## Blocks (three, one branch each)

| Block | Work | Done when |
|---|---|---|
| 1 Corpus and judge | lossless graph load/save, path scrubber, captions, stats and classifier gate | the gate separates a random-fuzzer set from the human set |
| 2 Generators | the universe (M1 to M4) with the F0 to F5 funnel, plus A (retrieve + edit) | 200 patches pass the filter, coverage counted per order |
| 3 Gate and publish | tune until the classifier is fooled, owner rates a sample, publish 1,000+ | all gates pass, `catalog.json` updated |

## Risks

| Risk | Detail |
|---|---|
| Private paths and samples | Sampler and PaulStretch appear in many seeds (PaulStretch in 25 of 50); their audio files are local |
| One person's taste | 56 of about 100 seeds are the owner's, so "human-like" means "owner-like"; community patches add variety |
| Rights | publish only what the owner or the community author has the right to |
| "Models learn natively" | needs a public, permissively licensed, crawlable corpus at volume; documentation and few-shot examples in the MCP docs help immediately regardless |
| Judge gaming | a generator can fool a weak classifier; the owner sample is the backstop |

## Parked

Extending Tongue to modulation and other records. Training a generative model. Hosted or paid generation.
