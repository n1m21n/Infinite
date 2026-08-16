#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "core/AudioCable.h"
#include "core/INode.h"

class AudioGranularNode;
namespace Platform
{
   struct SampleBuffer;
}

// Visual snapshot of active grains for real-time viewport particle rendering
struct GrainVisualSnapshot
{
   static constexpr int kMaxVisualGrains = 64;
   struct Item
   {
      float position = 0.0f;    // 0..1 normalized buffer position
      float amp = 0.0f;         // 0..1 current envelope amplitude
      float pan = 0.0f;         // -1..+1 stereo position
      float pitchRatio = 1.0f;  // playback speed ratio
   };
   Item grains[kMaxVisualGrains];
   int count = 0;
   float scanPos = 0.0f;        // Current base scan playhead 0..1
};

// Real-time Granular Synthesizer and Texture Generator.
// Dissects loaded audio samples or live recorded audio into micro-sound
// grains with stochastic/periodic emission, position spray, pitch jitter,
// flexible windowing, spatial stereo diffusion, and real-time visualization.
//
// Provides:
// - Slot 0: Audio input pin ("record in") for live sampling
// - Stereo audio output
// - Interactive waveform display with start/end trim handles and real-time grain dots
// - Lock-free, zero-allocation audio DSP engine
class GranularNode : public INode, public IAudioSource
{
public:
   enum WindowShape
   {
      kWindowHann = 0,
      kWindowGaussian,
      kWindowTriangle,
      kWindowExpDecay,
      kWindowTrapezoid,
      kNumWindowShapes
   };
   static const char* const kWindowShapeNames[kNumWindowShapes];

   static INode* Create() { return new GranularNode(); }
   GranularNode();
   ~GranularNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;
   AudioCable* AudioInputSlot(int slot) override { return slot == 0 ? &audioInput : nullptr; }
   const char* InputLabel(int slot) const override
   {
      return slot == 0 ? "record in" : nullptr;
   }

   bool LoadFile(const std::string& path);
   void ReloadFromPath();

   void StartRecording();
   void StopRecording();
   bool IsRecording() const { return mRecording; }
   bool RequiresAudioProcessing() const override { return IsRecording(); }

   void Seek(float frac);
   bool IsPlaying() const { return mIsPlaying; }

   const std::string& FilePath() const { return mFilePath; }
   const std::string& FileName() const { return mFileName; }
   const std::string& Status() const { return mStatus; }

   static constexpr int kWaveformCacheSize = 256;
   float waveformMin[kWaveformCacheSize] = {};
   float waveformMax[kWaveformCacheSize] = {};
   int waveformCacheCount = 0;

   float Playhead() const { return mPlayhead; }
   const GrainVisualSnapshot& VisualSnapshot() const { return mLatestVisualSnapshot; }

   // ------------------------------------------------ Parameters
   // Position & Time
   float position = 0.0f;         // 0.0 .. 1.0 base position in sample
   float scan = 0.0f;             // -4.0 .. +4.0 traversal scan speed (0.0 = static / manual pos)
   float grainLength = 80.0f;     // 5.0 .. 1000.0 ms grain duration
   float density = 25.0f;         // 1.0 .. 100.0 grains / second
   bool freeze = false;           // Freeze scan position

   // Chaos & Randomization
   float randomPos = 0.05f;       // 0.0 .. 1.0 position spray / jitter
   float randomLength = 0.1f;     // 0.0 .. 1.0 duration randomization
   float randomPitch = 0.0f;      // 0.0 .. 24.0 semitones pitch jitter
   float randomPan = 0.5f;        // 0.0 .. 1.0 stereo pan spread
   float reverseProb = 0.0f;      // 0.0 .. 1.0 reverse grain probability

   // Pitch & Tuning
   float pitchShift = 0.0f;       // -24.0 .. +24.0 semitones
   float fineTune = 0.0f;         // -100.0 .. +100.0 cents
   int windowShape = kWindowHann; // Window envelope function

   // Stereo & Mix
   float pan = 0.0f;              // -1.0 .. +1.0 master pan
   float width = 1.0f;            // 0.0 .. 2.0 stereo width expansion
   float mix = 1.0f;              // 0.0 .. 1.0 dry/wet mix
   float volume = 0.8f;           // 0.0 .. 2.0 output volume

   // Bounds & Loop
   float start = 0.0f;            // 0.0 .. 1.0 trim start
   float end = 1.0f;              // 0.0 .. 1.0 trim end
   bool loop = true;              // Loop within [start, end]

   AudioCable audioInput;         // Record source

private:
   void FinishBuffer(Platform::SampleBuffer* decoded, const std::string& fileName,
                     const std::string& filePath, const std::string& status);

   std::unique_ptr<AudioGranularNode> mAudioNode;
   int mLastCookFrame = -1;
   std::string mFilePath;
   std::string mFileName;
   std::string mStatus = "no sample loaded";
   float mPlayhead = 0.0f;
   bool mIsPlaying = false;
   bool mRecording = false;

   GrainVisualSnapshot mLatestVisualSnapshot;
};
