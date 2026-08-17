#pragma once

#include <memory>

#include "WavetableNode.h"
#include "core/INode.h"
#include "core/NoteCable.h"

class AudioWavetableNode;

// Single-oscillator synth node. Reuses AudioWavetableNode's DSP engine with one
// active engine and an interactive amplitude envelope.
class OscillatorNode : public INode, public IAudioSource
{
public:
   static constexpr int kMaxUnison = WavetableNode::kMaxUnison;
   static constexpr int kMaxVoices = WavetableNode::kMaxVoices;

   static INode* Create() { return new OscillatorNode(); }
   OscillatorNode();
   ~OscillatorNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;

   // Test-only observability: sample rate last received by the mailbox.
   double DebugMailboxSampleRate() const;

   // Test-only observability: fmMode is a plain atomic, pushed outside the
   // smoothed ParamMailbox - see WavetableNode::DebugFmMode's comment.
   int DebugFmMode() const;

   AudioCable* AudioInputSlot(int slot) override { return slot == 1 ? &fmInput : nullptr; }
   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override
   {
      if (slot == 0) return "notes";
      if (slot == 1) return "fm in";
      return nullptr;
   }

   // Decimated output trace for inline scope visualizer.
   int ReadScope(float* out, int capacity);

   // Active sounding voices count published by audio thread.
   int ActiveVoices() const;

   enum Waveform
   {
      kSine = 0,
      kTriangle,
      kSaw,
      kSquare,
      kNumWaveforms
   };

   WavetableEngine engine;
   int waveform = kSaw;

   float frequency = 220.0f; // free-running pitch (ignored when note-driven)
   float glide = 0.0f;       // portamento, seconds
   float pitchBend = 0.0f;   // -2..2 semitones global pitch bend
   float fmDepth = 0.0f;     // audio-rate FM / PM depth
   int fmMode = 0;           // 0 = Phase Modulation, 1 = Exponential FM

   NoteCable noteInput;
   AudioCable fmInput;

   // UI-only scope cache.
   static constexpr int kScopeCacheCapacity = 128;
   float scopeCache[kScopeCacheCapacity] = {};
   int scopeCacheCount = 0;
   double scopeCacheTime = -1.0;

private:
   std::unique_ptr<AudioWavetableNode> mAudioNode;
   int mLastCookFrame = -1;
};
