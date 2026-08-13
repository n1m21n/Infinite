# P1 decisions — cook-rate, RTSan, PD patch-shape skim

Written during the P1 session that added `src/audio/DspMath.h` and
`src/audio/AudioVoice.h`/`.cpp`, closing out three items from
`docs/plans/optimization/research-implementation-map.md` §1/§2 that were
gated on `src/audio/` existing. This file is the "written somewhere a P2/P3
session will find it" home those items asked for.

## Audio/visual cook-rate decision (optimization doc 2.1)

**Decision: TouchDesigner-style, not Houdini-style.** An audio node that
needs a *visual* modulator's value (an LFO driving both a visual param and
an audio param, say) reads the **last value the visual graph cooked on the
main/render thread** — it does not trigger the visual modulator to cook
synchronously from the audio thread.

Mechanism: the main thread writes each frame into a plain `std::atomic<float>`
that the audio thread only ever reads, never writes. One frame of latency
(main-thread render-frame granularity, not audio-block granularity), but
trivially real-time-safe — it's just reading a value already sitting in
memory, no different in kind from `ParamMailbox::SmoothedValue`'s target
read.

**Why this and not the Houdini-style synchronous-cook option:** the
alternative — an audio node triggering its upstream visual modulator to cook
on demand from the audio thread — means arbitrary main-thread node code
(potentially allocating, locking, touching GL state) could run on the
real-time audio callback. That's not just harder to get right, it's
disallowed by the constraint list `AudioNode.h` states and this phase has
been enforcing since `AudioEngine`/`AudioNode` first existed: no allocation,
no locks with unbounded wait, no GL, no syscalls, no unbounded loops on the
render thread. A design that requires violating that list to work is not a
real option here.

**Where this lands for P3:** any future `IModulator`-implementing node
(`Modulation.h`) that wants to feed an audio param publishes its value into
one of these atomics alongside its existing `Value01()`, once P3 gives audio
nodes a concrete reference to a specific modulator's published slot (a small
registry or an index into a fixed-size array — exact shape is a P3 decision,
not this one). Nothing in P1 wires this up; this section only names the
mechanism so P3 doesn't have to re-derive it.

## RTSan (optimization doc 1.3) — verified go/no-go

**Status: blocked on this machine's toolchain, unchanged from the plan
prompt's own finding.** Re-verified during this session:

```
$ clang --version
Apple clang version 21.0.0 (clang-2100.3.27.1)
Target: arm64-apple-darwin27.0.0
InstalledDir: /Library/Developer/CommandLineTools/usr/bin

$ clang++ -fsanitize=realtime test.cpp -o test
clang++: error: unsupported option '-fsanitize=realtime' for target 'arm64-apple-darwin27.0.0'

$ brew list llvm
Error: No such keg: /opt/homebrew/Cellar/llvm
```

Apple Clang does not carry upstream RTSan; it needs a vanilla LLVM/Clang 19+
build, and no such toolchain is installed on this machine. This is not a
CMake flag away — it needs a second compiler entirely (a Homebrew-LLVM-based
build configuration, or CI on a different toolchain than local dev).

**Not available on this machine's toolchain; revisit if/when a
Homebrew-LLVM-based build config exists.** Do not attempt to wire up
`INFINITE_RTSAN` until then — there's nothing to point it at.

## Pure Data patch-shape skim (optimization doc 1.5)

Five-minute web skim of Islam, Eng & Hindle, "Opening the Valve on
Pure-Data: Usage Patterns and Programming Practices of a Data-Flow Based
Visual Programming Language" (MSR 2024), against
`docs/plans/audio/README.md` §3's 55→30 node consolidation.

**Headline finding (from the paper's own abstract/summary, via search — the
full PDF's fan-out-depth/feedback-prevalence/subpatch-nesting breakdown
specifically was not reachable through search snippets and would need a
direct PDF read to cite precisely):** across 6,534 public PD projects
mined from GitHub, most patch revisions are small and simple — fewer than
64 nodes, 51 connections, and 3 revisions per file; most projects have fewer
than 17 PD files, 31 commits, and a single author.

**Read against the plan's consolidation:** nothing here looks off. A
30-node palette is generous, not cramped, against patches that in practice
use well under 64 nodes total — the paper's real-world evidence points
toward *fewer* distinct node types mattering in practice, not more,
supporting consolidation rather than arguing against it. No change to
§3's node list is warranted from this skim.

**Caveat:** this is a headline-stats skim, not a read of the paper's actual
fan-out-depth/feedback-loop-prevalence/subpatch-nesting tables, which is
what the optimization doc's item 1.5 originally asked about. If a future
session wants the specific structural numbers (not just size), read
`softwareprocess.es/pubs/islam2024MSR-pure-data.pdf` directly before
locking in P2/P3's node graph shape further.
