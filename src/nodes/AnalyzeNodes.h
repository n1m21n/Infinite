#pragma once

#include <string>
#include <vector>

#include "INode.h"
#include "ImageCable.h"
#include "Modulation.h"
#include "Platform.h"

// --- Image Analyze ------------------------------------------------------
// Reduces an image to control values, which closes the loop in the graph:
// until now data only flowed modulators -> images, so a video could not drive
// a blur. This makes an image a legitimate modulation source.
//
// Reading pixels back off the GPU stalls the pipeline, so the readback is done
// at a reduced resolution and rate-limited rather than every frame.
class ImageAnalyzeNode : public INode
{
public:
   enum Output
   {
      kBrightness = 0,
      kContrast,
      kRed,
      kGreen,
      kBlue,
      kSaturation,
      kMotion,
      kCentroidX,
      kCentroidY,
      kOutputCount
   };

   static INode* Create() { return new ImageAnalyzeNode(); }

   ImageAnalyzeNode();
   ~ImageAnalyzeNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   int OutputCount() const override { return kOutputCount; }
   const char* OutputLabel(int index) const override;
   IModulator* ModulatorOutput(int index) override;

   ImageCable& Input() { return mInput; }
   float Value(int index) const;

   float smoothing = 0.35f;
   float gain = 1.0f;
   float sampleRate = 30.0f; // readbacks per second
   int sampleSize = 64;      // readback resolution

private:
   struct Tap : public IModulator
   {
      ImageAnalyzeNode* owner = nullptr;
      int index = 0;
      float Value01() override { return owner ? owner->Value(index) : 0.0f; }
   };

   void Analyze();

   ImageCable mInput;
   Tap mTaps[kOutputCount];
   float mValues[kOutputCount] = { 0 };
   std::vector<unsigned char> mPixels;
   std::vector<unsigned char> mPrevPixels;
   unsigned int mSmallFbo = 0;
   unsigned int mSmallTex = 0;
   int mSmallSize = 0;
   unsigned int mProgram = 0;
   bool mShaderTried = false;
   double mLastSampleSeconds = -1.0;
   int mLastCookFrame = -1;
};

// --- Audio File ---------------------------------------------------------
// Plays an audio file and analyses it independently of the live input, so a
// backing track can drive the visuals. Patch its output into Audio Analyze, or
// read it directly - it exposes the same set of taps.
class AudioFileNode : public INode
{
public:
   static INode* Create() { return new AudioFileNode(); }

   ~AudioFileNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   bool OpenViaDialog();
   bool Open(const std::string& path);

   void Play();
   void Pause();
   void Restart();
   bool IsPlaying() const;
   bool IsLoaded() const { return mHandle != nullptr; }

   double Duration() const;
   double Position() const;
   const std::string& FileName() const { return mFileName; }
   const std::string& Status() const { return mStatus; }
   const Platform::AudioLevels& Levels() const { return mLevels; }

   bool loop = true;
   bool followTransport = true; // play/pause with the global transport
   bool monitor = true;         // audible, or silent but still analysed
   float volume = 0.8f;
   float gain = 1.0f;
   float attack = 0.5f;
   float release = 0.12f;

private:
   Platform::AudioPlayerHandle* mHandle = nullptr;
   Platform::AudioLevels mLevels;
   std::string mFileName;
   std::string mStatus = "no file loaded";
   bool mWasTransportPlaying = false;
   int mLastCookFrame = -1;
};

// --- Audio Analyze ------------------------------------------------------
// Live audio in, control values out. Makes every parameter in the graph
// audio-reactive, since any of these outputs can be patched into any slider.
class AudioAnalyzeNode : public INode
{
public:
   enum Output
   {
      kLevel = 0,
      kLow,
      kMid,
      kHigh,
      kOnset,
      kBand0,
      kOutputCount = kBand0 + 8
   };

   static INode* Create() { return new AudioAnalyzeNode(); }

   AudioAnalyzeNode();
   ~AudioAnalyzeNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   int OutputCount() const override { return kOutputCount; }
   const char* OutputLabel(int index) const override;
   IModulator* ModulatorOutput(int index) override;

   float Value(int index) const;
   const Platform::AudioLevels& Levels() const { return mLevels; }

   // When a file node is patched in, it is analysed instead of the live input.
   AudioFileNode* fileSource = nullptr;

   bool Start();
   void Stop();
   bool IsRunning() const;
   const std::string& Status() const { return mStatus; }

   float gain = 1.0f;
   float attack = 0.5f;
   float release = 0.12f;
   float onsetHold = 0.12f; // seconds the onset output stays high

private:
   struct Tap : public IModulator
   {
      AudioAnalyzeNode* owner = nullptr;
      int index = 0;
      float Value01() override { return owner ? owner->Value(index) : 0.0f; }
   };

   Tap mTaps[kOutputCount];
   Platform::AudioLevels mLevels;
   float mOnsetEnvelope = 0.0f;
   double mLastSeconds = 0.0;
   std::string mStatus = "press Start to listen";
   int mLastCookFrame = -1;
};
