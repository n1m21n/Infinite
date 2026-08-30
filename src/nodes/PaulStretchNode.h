#pragma once

#include <memory>
#include <string>

#include "core/AudioCable.h"
#include "core/INode.h"

class AudioPaulStretchNode;
namespace Platform
{
   struct SampleBuffer;
}

// Extreme spectral time-stretcher and ambient texture synthesizer based on the
// PaulStretch algorithm. Transforms any audio file or live recording into
// lush, evolving ambient soundscapes through large-window FFT, phase
// randomization, spectral pitch/frequency shifting, and unison detuning.
//
// Provides:
// - Slot 0: Audio input pin ("record in") for sampling incoming audio directly
// - Stereo audio output
// - Interactive waveform display with loop bounds and scrub controls
// - Zero-allocation audio thread safety with hardware-accelerated vDSP FFT
class PaulStretchNode : public INode, public IAudioSource
{
public:
   static constexpr int kNumWindowSizes = 7;
   static const int kWindowSizes[kNumWindowSizes]; // 2048, 4096, 8192, 16384, 32768, 65536, 131072
   static const char* const kWindowSizeLabels[kNumWindowSizes];

   static INode* Create() { return new PaulStretchNode(); }
   PaulStretchNode();
   ~PaulStretchNode() override;

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

   void TriggerPreview(float frac);
   void StopPreview();
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

   // Parameters
   float stretch = 8.0f;          // 1.0x .. 1000.0x time stretch factor
   int windowSizeIndex = 4;       // default index 4 -> 32768 samples (~743ms @ 44.1k)
   float phaseRand = 1.0f;        // 0.0 .. 1.0 (phase randomization depth)
   float pitchShift = 0.0f;       // -24.0 .. +24.0 semitones
   float fineTune = 0.0f;         // -100.0 .. +100.0 cents
   float freqShift = 0.0f;        // -1000.0 .. +1000.0 Hz spectral shift
   int unison = 1;                // 1 .. 8 unison voices
   float detune = 10.0f;          // 0.0 .. 100.0 cents unison detune spread
   float volume = 0.8f;           // 0.0 .. 2.0 output volume
   float start = 0.0f;            // 0.0 .. 1.0 playback/loop start
   float end = 1.0f;              // 0.0 .. 1.0 playback/loop end
   float position = 0.0f;         // 0.0 .. 1.0 playhead/scrub position within [start, end]
   bool loop = true;              // loop playback within [start, end]

   AudioCable audioInput;         // record source

private:
   void FinishBuffer(Platform::SampleBuffer* decoded, const std::string& fileName,
                     const std::string& filePath, const std::string& status);

   std::unique_ptr<AudioPaulStretchNode> mAudioNode;
   int mLastCookFrame = -1;
   std::string mFilePath;
   std::string mFileName;
   std::string mStatus = "no sample loaded";
   float mPlayhead = 0.0f;
   bool mIsPlaying = false;
   bool mSelfOwnedByUser = false;
   bool mRecording = false;
};
