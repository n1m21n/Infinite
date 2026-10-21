#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/INode.h"
#include "core/NoteCable.h"

class AudioPartialsNode;

// What the audio thread plays from: one analysed sample as tracked
// partials over time plus its residual. Built on a worker thread from
// MolderDsp::Analyze, then handed over whole through a SampleSlotT.
struct PartialsModel
{
   static constexpr int kMaxPartials = 48;

   double sourceSR = 44100.0;
   int sourceLen = 0;
   int hop = 256;
   int numFrames = 0;
   int numPartials = 0;
   float f0 = 220.0f;
   // Per (frame, partial), flattened as frame * kMaxPartials + h.
   std::vector<float> amp;   // cosine amplitude
   std::vector<float> ratio; // measured frequency / f0
   std::vector<float> residual; // sourceLen samples: what the partials leave behind

   float DurationSec() const { return sourceSR > 0.0 ? (float)sourceLen / (float)sourceSR : 0.0f; }

   // Worker-thread only: allocates, runs the full analysis. Returns
   // nullptr when the sample has nothing to analyse.
   static PartialsModel* Build(const std::vector<float>& mono, double sr, const std::atomic<bool>* abort = nullptr);
};

// Playable resynthesis (sinusoids + residual, McAulay-Quatieri 1986,
// Serra 1989): a sample is analysed once into tracked partials, then each
// note plays it back additively. Pitch follows the keyboard with the
// sample's own formants kept in place (formant shifts them on purpose),
// partials thins the stack from the top, morph crossfades partial by
// partial into a second sample, speed walks the analysis faster or
// slower without changing pitch. Note-driven, 8 voices.
class PartialsNode : public INode, public IAudioSource
{
public:
   static constexpr int kMaxVoices = 8;
   static constexpr int kBarCount = PartialsModel::kMaxPartials;

   static INode* Create() { return new PartialsNode(); }
   PartialsNode();
   ~PartialsNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;

   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }

   void SweepPrepare() override;
   std::vector<SweepParamPrereq> SweepPrerequisitesFor(const std::string& paramName) const override
   {
      // loop and release only act past the end of the sample (the rig
      // never sends a note-off; a one-shot note releases itself there).
      if (paramName == "loop" || paramName == "release")
         return { { "speed", 4.0f } };
      return {};
   }
   bool SweepSpectralSignature() const override { return true; }
   int SweepMeasureBlocks() const override { return 16; }

   // which: 0 = the main sample, 1 = the morph target.
   bool LoadFile(int which, const std::string& path);
   void LoadMono(int which, std::vector<float> mono, double sr);
   void ReloadFromPath();
   void WaitIdle(); // tests: block until both analyses have landed
   bool IsAnalyzing() const;
   bool HasSample(int which) const { return mLoaders[which].f0 > 0.0f; }
   float AnalyzedF0(int which) const { return mLoaders[which].f0; }
   const std::string& FileName(int which) const { return mLoaders[which].name; }
   const std::string& Status() const { return mStatus; }
   int ActiveVoices() const;

   // The main sample's partial levels at its loudest frame, for the card.
   float bars[kBarCount] = {};
   int barCount = 0;

   std::string pathA;
   std::string pathB;

   float pitch = 0.0f;      // -24..24 st on top of the keyboard
   float formant = 0.0f;    // -12..12 st, moves the sample's spectral envelope
   float partials = 48.0f;  // 1..48, how many partials sound
   float morph = 0.0f;      // 0..1, main sample -> morph target
   float speed = 1.0f;      // 0.1..4, how fast a note walks through the analysis
   float noise = 1.0f;      // 0..2, residual level
   float release = 300.0f;  // ms
   float volume = 0.8f;
   bool loop = false;

   NoteCable noteInput;

private:
   struct Loader
   {
      std::thread thread;
      std::atomic<bool> working { false };
      std::atomic<bool> ready { false };
      std::atomic<bool> abort { false };
      PartialsModel* result = nullptr; // worker-owned until `ready`
      float f0 = 0.0f;
      std::string name;
   };
   void JoinIfDone(int which);
   void RebuildBars(const PartialsModel& m);

   std::unique_ptr<AudioPartialsNode> mAudioNode;
   int mLastCookFrame = -1;
   Loader mLoaders[2];
   std::string mStatus = "no sample loaded";
};
