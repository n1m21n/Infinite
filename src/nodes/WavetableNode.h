#pragma once

#include <memory>

#include "audio/SynthModes.h"
#include "core/INode.h"
#include "core/NoteCable.h"

class AudioWavetableNode;

// The synth source. Replaces the P2 Oscillator entirely: two independent
// wavetable engines, each with its own tuning, unison stack, amplitude ADSR
// and pitch ADSR, mixed by one A/B control.
//
// Why two engines and not one with a second node cabled in: the two things a
// wavetable patch is built out of are a layer pair (a body and a top, detuned
// against each other) and an envelope pair shaping them differently over
// time. Both of those are *per engine* and both are useless if the engines
// can't share a note allocation - two separate nodes would each allocate
// their own voices from the same note stream and drift apart under voice
// stealing. So the pair lives in one node, over one voice allocator.
//
// Free-running vs note-driven is the same rule the Oscillator had (see
// AudioWavetableNode::ProcessBlock): with no note cable the node runs one
// permanently-open voice at `frequency` and both amplitude envelopes are
// bypassed, so the node makes sound the moment it is patched to an output.
// Connect a note cable and it becomes polyphonic and envelope-gated.
struct WavetableEngine
{
   // Tier 1 - what the engine is.
   int table = 0;           // index into the Wavetable bank
   float position = 0.0f;   // 0..1, morph across the table's frames
   float volume = 0.8f;     // 0..1
   float pan = 0.0f;        // -1..1

   // Unison stack.
   int unison = 1;          // 1..kMaxUnison detuned copies
   float detune = 12.0f;    // cents, total spread across the stack
   float stereoWidth = 0.4f;// 0..1, how far the stack fans across the field

   // Tuning.
   int octave = 0;          // +/- 4
   int semi = 0;            // +/- 12
   float fine = 0.0f;       // +/- 50 cents

   // Phase.
   float phase = 0.0f;      // 0..1 static start offset
   float phaseRandomize = 0.0f; // 0..1, spreads the unison stack's start phase

   // Shaping. One mode selector plus one depth, rather than a knob per
   // algorithm: the modes are alternatives, never combined, and fifteen
   // permanently-zero knobs is what a mode list exists to avoid. `warpRatio`
   // is only read by the modes SynthModes::WarpUsesRatio names.
   int warpMode = SynthModes::kWarpOff;
   float warpAmount = 0.0f; // 0..1 depth of whichever warp is selected
   float warpRatio = 1.0f;  // cross-mod operator / sync ratio, 0.25..16

   // Filter, per engine (see SynthModes::FilterType for why slope is part of
   // the type). Cutoff is offset by the filter envelope in octaves.
   int filterType = SynthModes::kFilterOff;
   float cutoff = 12000.0f;    // Hz
   float resonance = 0.15f;    // 0..1, mapped onto Q by the render path

   // Amplitude envelope (note-driven mode only).
   float ampAttack = 4.0f;      // ms
   float ampDecay = 300.0f;     // ms
   float ampSustain = 0.75f;    // 0..1
   float ampRelease = 260.0f;   // ms

   // Pitch envelope: `pitchAmount` semitones of offset, shaped by its own ADSR.
   float pitchAmount = 0.0f;    // -48..48 semitones at full envelope
   float pitchAttack = 0.0f;    // ms
   float pitchDecay = 120.0f;   // ms
   float pitchSustain = 0.0f;   // 0..1
   float pitchRelease = 120.0f; // ms

   // Filter envelope: `filterAmount` octaves of cutoff offset at full
   // envelope. Octaves rather than Hz because a fixed Hz sweep is a different
   // musical distance at every cutoff setting - 2000 Hz of movement is most of
   // the spectrum from 200 Hz and barely audible from 12 kHz.
   float filterAmount = 0.0f;    // -8..8 octaves
   float filterAttack = 4.0f;    // ms
   float filterDecay = 400.0f;   // ms
   float filterSustain = 0.4f;   // 0..1
   float filterRelease = 300.0f; // ms

   bool on = true;
};

class WavetableNode : public INode, public IAudioSource
{
public:
   static constexpr int kEngines = 2;
   static constexpr int kMaxUnison = 8;
   static constexpr int kMaxVoices = 8;

   static INode* Create() { return new WavetableNode(); }
   WavetableNode();
   ~WavetableNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;

   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }

   // Decimated output trace for the inline scope; drains the audio node's
   // MeterRing on the main thread. Returns the count written.
   int ReadScope(float* out, int capacity);

   // How many voices are sounding right now, published once per block by the
   // audio thread - drives the readout strip's voice count.
   int ActiveVoices() const;

   WavetableEngine engines[kEngines];

   float mix = 0.0f;         // 0 = engine A only, 1 = engine B only
   float volume = 0.8f;      // master
   float frequency = 220.0f; // free-running pitch (ignored when note-driven)
   float glide = 0.0f;       // portamento, seconds

   NoteCable noteInput;

   // UI-only scope cache - not a saved param, not touched by VisitParams.
   static constexpr int kScopeCacheCapacity = 128;
   float scopeCache[kScopeCacheCapacity] = {};
   int scopeCacheCount = 0;
   double scopeCacheTime = -1.0;

private:
   std::unique_ptr<AudioWavetableNode> mAudioNode;
   int mLastCookFrame = -1;
};
