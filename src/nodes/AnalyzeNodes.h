#pragma once

#include <string>
#include <vector>

#include "INode.h"
#include "ImageCable.h"
#include "Modulation.h"
#include "Platform.h"

// --- Image Analyze ------------------------------------------------------
// Reduces an image or video to control values and modulation channels.
// Provides 4 sampling modes (Global average, Point probe, ROI box, Center weighted),
// 22 mathematical/color operations and custom algebraic formulas via Expression::Evaluate,
// and outputs multiple modulation signals for driving parameters across Infinite.
class ImageAnalyzeNode : public INode
{
public:
   enum Output
   {
      kResult = 0,
      kBrightness,
      kContrast,
      kRed,
      kGreen,
      kBlue,
      kSaturation,
      kHue,
      kMotion,
      kCentroidX,
      kCentroidY,
      kOutputCount
   };

   enum SampleMode
   {
      kGlobalAverage = 0,
      kPointProbe,
      kBoxRegion,
      kCenterWeighted,
      kSampleModeCount
   };

   enum MathOp
   {
      kCustomExpression = 0,
      kRedOp,
      kGreenOp,
      kBlueOp,
      kAlphaOp,
      kLuminanceOp,
      kAverageOp,
      kSumOp,
      kProductOp,
      kMaxOp,
      kMinOp,
      kRangeOp,
      kRMinusG,
      kRMinusB,
      kGMinusB,
      kAbsRMinusG,
      kAbsRMinusB,
      kAbsGMinusB,
      kSaturationOp,
      kHueOp,
      kEuclideanNorm,
      kDeltaMotion,
      kMathOpCount
   };

   static INode* Create() { return new ImageAnalyzeNode(); }
   static const std::vector<std::string>& SampleModeNames();
   static const std::vector<std::string>& MathOpNames();

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

   float RawR() const { return mRawR; }
   float RawG() const { return mRawG; }
   float RawB() const { return mRawB; }
   float RawA() const { return mRawA; }
   float RawLum() const { return mRawLum; }
   float RawDelta() const { return mRawDelta; }

   const std::string& ExpressionError() const { return mExprError; }

   // Configuration & parameters
   int sampleMode = kGlobalAverage;
   float probeU = 0.5f;
   float probeV = 0.5f;
   float probeRadius = 0.1f;

   int mathOp = kLuminanceOp;
   std::string customFormula = "r * 0.5 + g * 0.5";

   float gain = 1.0f;
   float offset = 0.0f;
   float power = 1.0f;
   bool invert = false;
   bool clamp01 = true;

   float smoothing = 0.35f;
   float sampleRate = 30.0f; // readbacks per second
   int sampleSize = 64;      // readback resolution

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("sampleMode", sampleMode);
      v.Float("probeU", probeU);
      v.Float("probeV", probeV);
      v.Float("probeRadius", probeRadius);
      v.Int("mathOp", mathOp);
      v.Text("customFormula", customFormula);
      v.Float("gain", gain);
      v.Float("offset", offset);
      v.Float("power", power);
      v.Bool("invert", invert);
      v.Bool("clamp01", clamp01);
      v.Float("smoothing", smoothing);
      v.Float("sampleRate", sampleRate);
      v.Int("sampleSize", sampleSize);
   }

private:
   struct Tap : public IModulator
   {
      ImageAnalyzeNode* owner = nullptr;
      int index = 0;
      float Value01() override { return owner ? owner->Value(index) : 0.0f; }
   };

   void Analyze();
   float ComputeMathResult(float r, float g, float b, float a, float lum, float delta);

   ImageCable mInput;
   Tap mTaps[kOutputCount];
   float mValues[kOutputCount] = { 0 };

   float mRawR = 0.0f;
   float mRawG = 0.0f;
   float mRawB = 0.0f;
   float mRawA = 1.0f;
   float mRawLum = 0.0f;
   float mRawDelta = 0.0f;

   std::string mExprError;

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
   const std::string& FilePath() const { return mFilePath; }
   const std::string& Status() const { return mStatus; }
   const Platform::AudioLevels& Levels() const { return mLevels; }

   bool loop = true;
   bool followTransport = true; // play/pause with the global transport
   bool monitor = true;         // audible, or silent but still analysed
   float volume = 0.8f;
   float gain = 1.0f;
   float attack = 0.5f;
   float release = 0.12f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Text("path", mFilePath);
      v.Bool("loop", loop); v.Bool("followTransport", followTransport);
      v.Bool("monitor", monitor); v.Float("volume", volume); v.Float("gain", gain);
      v.Float("attack", attack); v.Float("release", release);
   }

   // Reloads from whatever path a patch restored. Called after loading.
   void ReloadFromPath()
   {
      if (!mFilePath.empty())
      {
         const std::string p = mFilePath;
         Open(p);
      }
   }

private:
   Platform::AudioPlayerHandle* mHandle = nullptr;
   Platform::AudioLevels mLevels;
   std::string mFileName;
   std::string mFilePath;
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

   void VisitParams(ParamVisitor& v) override
   {
      v.Float("gain", gain); v.Float("attack", attack);
      v.Float("release", release); v.Float("onsetHold", onsetHold);
   }

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
