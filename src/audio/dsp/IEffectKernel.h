#pragma once

#include "audio/AudioBuffer.h"
#include "audio/MeterRing.h"

class AudioEffectNode;

// The seven P3c effects share no DSP - a reverb and a compressor have nothing
// in common at the sample level - so this interface is deliberately thin.
// What every kernel *does* share is declared once at the call sites, not
// here: dry/wet mix, latency compensation and denormal-guarding the final
// output all live in AudioEffectNode's own runtime (see AudioEffectNode.cpp),
// which wraps every kernel's ProcessBlock identically - see
// docs/plans/audio/P3c-P3a2-design.md §0.5.
class IEffectKernel
{
public:
   virtual ~IEffectKernel() {}

   virtual void PrepareToPlay(double sampleRate, int maxBlockSize) = 0;
   virtual void Reset() = 0;

   // Main thread only, called once per AudioEffectNode::CookIfNeeded. Reads
   // whatever fields it needs off `node` (AudioEffectNode::Param) and pushes
   // its own internal representation into whatever mailbox/atomics it keeps
   // privately - which does not have to be 1:1 with the node's raw params.
   // Audio Filter pushes precomputed biquad/SVF coefficients rather than
   // freq/Q, specifically so the audio thread below never runs tan() - see
   // §1.1's "coefficients computed main-thread ... pushed as coefficients".
   virtual void PushParams(const AudioEffectNode& node, double sampleRate) = 0;

   // Audio thread only. Writes the *wet* signal only - AudioEffectNode's
   // runtime crossfades it against the dry input per the node's mix param,
   // so a kernel here never has to know its own mix value. `sidechain` is
   // non-null only for a kernel whose EffectDef sets `hasSidechain` (Dynamics
   // today) and only when that second input pin is actually cabled - every
   // other kernel ignores the parameter and keeps its two-argument shape by
   // convention (the base declares it so a single virtual call site in
   // AudioEffectRuntime::ProcessBlock covers every kernel, table-driven or
   // not - see §0.5).
   virtual void ProcessBlock(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out) = 0;

   // Audio thread only. The dry half of the mix, for a kernel whose wet path
   // is not delayed by the same whole number of samples at every frequency
   // (minimum-phase oversampling: Drive, Wavetable Shaper). Such a kernel
   // writes `in` as its own signal path passes it with the processing taken
   // out, and returns true; the runtime then mixes against that instead of
   // against `in` delayed by LatencySamples(), so wet and dry stay in phase
   // across the whole spectrum at any mix. Called once per block, after
   // ProcessBlock, with the same `in`. Default: not handled.
   virtual bool ProcessDry(const AudioBuffer& /*in*/, AudioBuffer& /*dry*/) { return false; }

   // Samples of latency this kernel's processing adds (lookahead, oversample,
   // FFT window, ...). 0 for a kernel with none - Audio Filter's biquad/SVF
   // cascade has none. The engine compensates using whatever a kernel reports
   // here; see §0.5.
   virtual int LatencySamples() const { return 0; }

   // The most LatencySamples() can ever return for this kernel (a kernel
   // whose latency depends on a mode). AudioEffectRuntime sizes the dry
   // path's delay to this once, on the main thread; when the reported
   // latency changes, PollAudioLatencyChanges() re-runs PDC. Defaults to the
   // fixed latency.
   virtual int MaxLatencySamples() const { return LatencySamples(); }

   // Optional second meter a kernel publishes beyond AudioEffectRuntime's own
   // generic post-mix peak (AudioEffectNode::Level()) - Dynamics' live gain-
   // reduction dB, read by its visualizer's operating-point dot/GR bar.
   // nullptr (the default) for every kernel with nothing extra to publish.
   // Written from ProcessBlock (audio thread), read from CookIfNeeded (main
   // thread) via AudioEffectNode::ExtraMeterValue() - same MeterRing
   // discipline as every other audio->main readback in this tree.
   virtual MeterRing* ExtraMeter() { return nullptr; }

   // Main thread only. True while the kernel is still building what it needs
   // to sound (Shape Resonator's background mode solve). An offline render
   // (--audio-summary / --render) waits on this before its first sample, so a
   // render that runs faster than real time does not start into silence.
   virtual bool Preparing() const { return false; }
};
