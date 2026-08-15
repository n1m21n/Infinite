In /Users/namansoni/infinte, there's a request to change IModulator's
contract from unipolar 0..1 to unbounded/bipolar, to make sine LFOs and
inverters cleaner to express. Before implementing, read this whole prompt
— there are two viable approaches with very different risk, and you need
to pick one explicitly and say which you picked.

Verified current state: IModulator::Value01() (src/core/Modulation.h:14-21)
is documented as contractually 0..1. Every single implementer (18 classes,
listed below) already either clamps to 0..1 or is designed to produce
0..1 — LFONode, RandomNode, PatternNode, MathNode, CompareNode,
RangeToRangeNode, SmoothNode, ModDepthNode, ConstantNode,
NullModulatorNode, MacroKnobNode, MacroXYNode, PathNode (both outputs),
EnvelopeNode, NoteToCVNode, VibratoNode, PitchBendNode, MidiCCNode,
MidiTriggerNode, ImageAnalyzeNode::Tap, AudioAnalyzeNode::Tap — file:line
list available in the earlier verification pass, all in
src/nodes/ModulatorNodes.cpp, src/nodes/NoteNodes.cpp,
src/nodes/MacroNodes.cpp, src/nodes/PathNode.cpp, src/nodes/MidiNodes.h,
src/nodes/AnalyzeNodes.h.

The ONE exception: InvertNode::Value01() (ModulatorNodes.cpp:239-243,
`return low + high - v;`) is deliberately unclamped — its own comment
(ModulatorNodes.h:341-342) says this is intentional, to correctly mirror
an already-out-of-range value from an unclamped Math/RangeToRange node.
RangeToRangeNode already has a `clampOutput` toggle (ModulatorNodes.cpp:
204-212) that, when false, lets it emit values outside 0..1 today — i.e.
the 0..1 contract is already an opt-out, not absolute, and there's
already an "out of contract" amber-border UI warning for it
(main.cpp:28563-28571, `outOfContract = lo < -1e-4f || hi > 1.0f+1e-4f`).

The single production apply-loop that binds a modulator to a parameter
is main.cpp:28278-28282:
    const float v01 = std::min(1.0f, std::max(0.0f, modulator->Value01()));
    *ref.value = ref.minValue + (ref.maxValue - ref.minValue) * v01;
This clamps defensively before the affine map onto the destination
param's `ParamRef.minValue/maxValue` (Modulation.h:25-33).

Serialization (verified, src/core/Patch.h:1-33, main.cpp:13787-13789):
modulator bindings are stored as pure topology
(`mod <dstIndex> <dstParam> <srcIndex> <srcOutput>`) — no min/max/range
value is ever persisted in the binding record. A node's own low/high
fields (LFO, Random, Invert, MidiCC) ARE persisted via VisitParams as
ordinary `f low <val>` lines, format-unchanged by this refactor. So: no
patch file schema migration is needed, but changing the runtime contract
silently changes the physical *behavior* of every saved patch that has a
modulator binding, because the same stored low/high numbers will
produce different results once v01 isn't clamped to 0..1 before the
affine map.

Two implementation options — pick one and say which:

OPTION A (narrower, lower risk, addresses the actual stated pain point):
Leave IModulator::Value01() and its 0..1 contract exactly as-is for all
existing implementers. Instead, fix main.cpp:28281-28282 to stop
force-clamping v01 before the affine map — let a modulator that
deliberately produces an out-of-range value (via InvertNode, or
RangeToRangeNode with clampOutput=false) actually extrapolate past the
destination's nominal min/max, rather than being silently clamped away.
This solves "bipolar/out-of-range modulation gets clamped into
uselessness" without touching 18 call sites or the interface name. Also
update the "out of contract" warning at main.cpp:28563-28571 to describe
this as an intentional extrapolation feature, not a defect.

OPTION B (the original full ask, high risk): Rename Value01() to
Value(), change the documented contract to unbounded/bipolar float, and
update all 18 implementers plus main.cpp:28278-28282 (remove the
defensive 0..1 clamp, replace with the real bipolar-to-range mapping
logic) plus the three test/dev sites that assert 0..1
(main.cpp:24673-24676 Path fixture, main.cpp:28635-28641 AudioAnalyze
sweep assertion — both will start reporting spurious FAILs unless
updated) plus the two ImGui::ProgressBar(n->Value01(), ...) calls at
main.cpp:3784 and :3817 (a progress bar assumes 0..1 for its fill
fraction — these need remapping/clamping logic added at the display
site since the underlying value may no longer be 0..1).

Recommendation: implement Option A first as a separate, smaller, safer
change. Only proceed to Option B if Option A is confirmed insufficient
in practice (i.e. users actually need e.g. a raw -1..1 sine feeding
something like a bipolar pan control without any shift math) — Option B
is a one-way door once patches exist that depend on the new contract.

Whichever option, build with:
  cmake --build build -j"$(sysctl -n hw.ncpu)"
and run the full modulator-node self-test sweep in main.cpp (the fixture
functions at 23961-23976, 24673-24676, 28635-28641 reference above) to
confirm no regressions, plus manually test loading 2-3 existing saved
patches that use LFO/Invert modulator bindings and confirm their visual/
audio behavior is unchanged (Option A) or document exactly what changed
and why (Option B).
