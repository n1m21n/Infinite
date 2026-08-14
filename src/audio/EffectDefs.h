#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "dsp/IEffectKernel.h"

// Declarative description of one AudioEffectNode "type", mirroring
// core/FilterDefs.h/FilterNode.h exactly: one C++ class (AudioEffectNode) is
// instantiated once per EffectDef, so adding effect eight is a table row plus
// a kernel, not a new node class - see docs/plans/audio/P3c-P3a2-design.md
// §0.4.
//
// What this table drives, concretely: a param's name (VisitParams' save/load
// key) and its default/range are declared exactly once, and AudioEffectNode
// walks this same list both to persist the param and to hand its raw value to
// the kernel - see AudioEffectNode.h's class comment for why the *layout*
// (Tier 1 vs Tier 2, which controls a dynamic selection shows) is deliberately
// NOT part of this table and stays a per-effect Draw*Body function instead.

// One param that must be set to `value` before `EffectParamDef` this attaches
// to can have any audible effect at all - e.g. Dynamics' `ratio` needs
// `mode` != limit, or Audio Filter's `gain` needs `type` pinned to a
// gain-using type (shelf/peak). Declared here (not inferred) so
// audio-node-sweep's generic, registry-driven param sweep can arrange for a
// gated param's prerequisites before testing it in isolation, rather than
// reporting every one of them FAIL - see
// .claude/skills/audio-node-sweep/SKILL.md "Blind spots", which is exactly
// the false-failure class this exists to remove.
struct EffectParamPrereq
{
   std::string paramName;
   float value = 0.0f;
};

struct EffectParamDef
{
   std::string name;
   float minVal = 0.0f;
   float maxVal = 1.0f;
   float defaultVal = 0.0f;
   // Set for a param with no audio-thread effect at all by design - the
   // sweep reports these `pass` with a note rather than trying every
   // alternate value and reporting FAIL.
   bool uiOnly = false;
   // Every entry must hold simultaneously for this param to be observable in
   // isolation. Empty = always live (most params).
   std::vector<EffectParamPrereq> prerequisites;
   // Explicit alternate values to try in the sweep's Check B, in order,
   // instead of the generic AlternateInts/AlternateFloats sequence. Empty
   // (the default) uses the generic sequence, which is right for most
   // params. Needed when a param's meaningful range is wider than the
   // generic small-number sequence reaches relative to the sweep rig's fixed
   // probe note (69) - Note Filter's rangeLow (0..127) is the first case.
   std::vector<float> testCandidates;
};

// Selects a node's visualizer draw function - the visualizer is the one
// thing §0.4 deliberately keeps OUT of the table (a frequency-response curve,
// a transfer curve and a goniometer share no drawing code), but *which one* a
// given EffectDef wants is still a single small enum rather than a second
// string comparison against `EffectDef::name` in the main.cpp dispatch ladder
// - see the `DrawAudioNodeBody` comment next to this enum's use.
enum class EffectVisualizerId
{
   kNone = 0,
   kFilterResponse,
   kDynamicsTransfer,
   kDelayTaps,
   kReverbDecay,
   kDriveCurve,
   kStereoGoniometer,
   kPitchShiftDisplay,
   kChorusScatter,
   kFlangerScatter,
   kPhaserScatter,
   kBitcrushWave,
   kTransientEnvelope,
   kStutterGrid,
   kRingModWave,
   kFormantVowel,
};

struct EffectDef
{
   std::string name;     // registered node type name, e.g. "Audio Filter"
   std::string category; // always "AudioEffects" today; kept for parity with FilterDef
   float bodyWidth = 440.0f;
   std::vector<EffectParamDef> params;
   std::function<std::unique_ptr<IEffectKernel>()> makeKernel;
   EffectVisualizerId visualizerId = EffectVisualizerId::kNone;
   // Starting value for the universal mix field (§0.5) - 1.0 (fully wet) is
   // right for Audio Filter/Dynamics, which sit in the main signal path, but
   // wrong for Delay, which §1.3 specifies at 0.3 wet by default so a freshly
   // spawned delay doesn't swallow the dry signal.
   float defaultMix = 1.0f;
   // True if this effect wants a second audio input (slot 1) for a sidechain
   // signal - Dynamics only today. AudioEffectNode only answers
   // AudioInputSlot(1) when this is set, so every other effect keeps its
   // single contiguous input slot.
   bool hasSidechain = false;
};

const std::vector<EffectDef>& GetEffectDefs();

// Looks up one param's declaration by node type name + param name - used by
// audio-node-sweep to find a gated param's prerequisites/uiOnly flag without
// hand-listing node names there. Returns nullptr if either isn't found (every
// non-AudioEffectNode node, or a param name typo).
const EffectParamDef* FindEffectParamDef(const std::string& nodeName, const std::string& paramName);
