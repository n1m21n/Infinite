#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/INode.h"
#include "core/NoteCable.h"

class AudioFMNode;

namespace FMDsp
{
   constexpr int kNumOps = 4;
   constexpr int kNumAlgorithms = 11;

   // One algorithm: which operators modulate each operator (bit j = op j+1
   // feeds it) and which operators are heard. A modulator always has a
   // higher number than what it feeds, so operators run 4 -> 1 in one pass.
   struct Algorithm
   {
      unsigned mods[kNumOps];
      unsigned carriers;
      const char* name;
   };

   inline const Algorithm& AlgorithmAt(int i)
   {
      static const Algorithm kAlgorithms[kNumAlgorithms] = {
         { { 0x2, 0x4, 0x8, 0 }, 0x1, "4>3>2>1" },
         { { 0x2, 0xC, 0, 0 }, 0x1, "(4+3)>2>1" },
         { { 0x6, 0, 0x8, 0 }, 0x1, "(4>3+2)>1" },
         { { 0x6, 0x8, 0, 0 }, 0x1, "(4>2+3)>1" },
         { { 0x2, 0, 0x8, 0 }, 0x5, "4>3, 2>1" },
         { { 0x8, 0x8, 0x8, 0 }, 0x7, "4>(3,2,1)" },
         { { 0, 0x4, 0x8, 0 }, 0x3, "4>3>2, 1" },
         { { 0xE, 0, 0, 0 }, 0x1, "(4+3+2)>1" },
         { { 0, 0, 0x8, 0 }, 0x7, "4>3, 2, 1" },
         { { 0x8, 0, 0, 0 }, 0x7, "4>1, 3, 2" },
         { { 0, 0, 0, 0 }, 0xF, "4, 3, 2, 1" },
      };
      return kAlgorithms[i < 0 ? 0 : (i >= kNumAlgorithms ? kNumAlgorithms - 1 : i)];
   }

   inline const std::vector<std::string>& AlgorithmList()
   {
      static const std::vector<std::string> kList = [] {
         std::vector<std::string> v;
         for (int i = 0; i < kNumAlgorithms; i++)
            v.push_back(AlgorithmAt(i).name);
         return v;
      }();
      return kList;
   }
}

// 4-operator FM synth: sine operators in phase modulation, wired by one of
// 11 algorithms. Each operator follows the note at a ratio or sits at a
// fixed frequency, and has its own level and ADSR. Operator 4 can feed
// itself. Note-driven, 8 voices, rendered at 8x and filtered down.
class FMNode : public INode, public IAudioSource
{
public:
   static constexpr int kMaxVoices = 8;
   static constexpr int kScopeCacheCapacity = 128;

   static INode* Create() { return new FMNode(); }
   FMNode();
   ~FMNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;

   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }

   std::vector<SweepParamPrereq> SweepPrerequisitesFor(const std::string& paramName) const override;
   // A modulator moves the timbre, not the level: a carrier's peak and RMS
   // stay put however hard it is bent, so the sweep reads the spectrum.
   bool SweepSpectralSignature() const override { return true; }
   int SweepMeasureBlocks() const override { return 2; }

   int ReadScope(float* out, int capacity);
   int ActiveVoices() const;

   struct Operator
   {
      float ratio = 1.0f;     // x the note's frequency
      bool fixed = false;     // play `freq` whatever the note
      float freq = 100.0f;    // Hz, when fixed
      float level = 1.0f;     // 0..1: loudness as a carrier, depth as a modulator
      float attack = 2.0f;    // ms
      float decay = 600.0f;   // ms
      float sustain = 0.6f;   // 0..1
      float release = 300.0f; // ms
   };

   int algorithm = 0;
   float feedback = 0.0f; // 0..1, operator 4 into itself
   float volume = 0.7f;
   Operator ops[FMDsp::kNumOps];

   NoteCable noteInput;

   float scopeCache[kScopeCacheCapacity] = {};
   int scopeCacheCount = 0;
   double scopeCacheTime = -1.0;

private:
   std::unique_ptr<AudioFMNode> mAudioNode;
   int mLastCookFrame = -1;
};
