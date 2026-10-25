#pragma once

#include <memory>

#include "core/INode.h"
#include "core/NoteCable.h"

class AudioCircuitSynthNode;

// Circuit Synth (Turbo 0.73): makes its sound by simulating a small electrical
// circuit in real time (Modified Nodal Analysis, see audio/dsp/CircuitSynthDsp.h).
// A preset is a netlist; every knob scales one part (or a group of parts) of
// it. Notes play the circuit's generator, and C / L can follow the key so the
// circuit's own resonances move with the pitch. Stereo is built in: twin
// circuits with part tolerance (SPREAD), two probe points (PROBE A / B),
// mid/side WIDTH, POLY voices panned. Clean room, MIT: ships in both editions.
class CircuitSynthNode : public INode, public IAudioSource
{
public:
   static constexpr int kKnobs = 8;
   static constexpr int kScopeCapacity = 256; // interleaved L, R

   static INode* Create() { return new CircuitSynthNode(); }
   CircuitSynthNode();
   ~CircuitSynthNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;

   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }

   // Selects a preset and takes its play settings (wave, trigger, follow,
   // contact, attack, release, spread, probes); every part knob back to 0.
   void ApplyPreset(int index);
   // The body's TAP button: a C3 for 0.3 s, so a circuit can be heard unpatched.
   void Tap();

   int ReadScope(float* out, int capacity); // interleaved L, R pairs
   int ActiveVoices() const;
   int MaxVoices() const;

   // ------------------------------------------------ parameters (saved)
   int preset = 0;
   int wave = 0;    // CircuitDsp::Wave
   int mode = 0;    // 0 mono, 1 poly
   int trigger = 1; // 0 tap, 1 hold
   bool cFollow = true, lFollow = true;
   float pitch = 0.0f;     // semitones
   float glide = 0.0f;     // seconds
   float contactMs = 1.0f; // the source switch's edge
   float attack = 0.003f, release = 0.4f;
   float lfoRate = 1.0f, lfoDepth = 0.0f;
   int lfoShape = 0, lfoTarget = 0;
   int probeA = 0, probeB = 1;
   float width = 1.0f, spread = 1.5f, level = 0.8f;
   float knob[kKnobs] = {}; // -1..1: each part knob scales its parts (x1/8 .. x8 for R, C, L, volts)

   NoteCable noteInput;

   // UI scope cache (main thread)
   float scopeCache[kScopeCapacity] = {};
   int scopeCount = 0;
   double scopeTime = -1.0;

private:
   std::unique_ptr<AudioCircuitSynthNode> mAudioNode;
   int mLastCookFrame = -1;
};
