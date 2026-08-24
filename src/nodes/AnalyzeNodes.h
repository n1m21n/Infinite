#pragma once

#include <memory>
#include <string>
#include <vector>

#include "INode.h"
#include "ImageCable.h"
#include "Modulation.h"
#include "Platform.h"
#include "core/AudioCable.h"

class AudioFilePlayerAudioNode; // defined in AnalyzeNodes.cpp - see SamplerNode.h's
                                 // forward-declare-in-header/define-in-cpp pattern
class AudioAnalyzerAudioNode;   // ditto

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
// Plays an audio file into the real-time DSP graph - not just a private
// analysis-only engine off to the side - so its output can be patched into
// Audio Analyze, Audio Displacement, a Mixer, an effect, or straight to
// Audio Out like any other audio source. Decodes once via
// Platform::DecodeAudioFileToBuffer (main thread) and hands the buffer to
// its own AudioFilePlayerAudioNode (defined in AnalyzeNodes.cpp), the same
// two-object split SamplerNode/AudioSamplerNode use.
//
// `monitor` and `volume` no longer mean "audible on the hardware output
// regardless of the patch" - this node's own AVAudioEngine is gone.
// Audibility now comes entirely from whether this node's output is patched
// (directly or through other audio nodes) into an Audio Out; `monitor`
// silences this node's own output buffer (still analysed - see Levels())
// the same way "silent but still analysed" always meant, just scoped to the
// graph instead of to the speakers.
class AudioFileNode : public INode, public IAudioSource
{
public:
   static INode* Create() { return new AudioFileNode(); }

   AudioFileNode(); // out-of-line: mAudioNode's pointee is forward-declared here
   ~AudioFileNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   AudioNode* GetAudioNode() override;

   // While a file is loaded this node must keep processing every block even
   // with no path to an Audio Out - both to advance its own playhead and so
   // its own Levels() meter keeps reading. Same pattern as SamplerNode's
   // IsRecording()-gated override.
   bool RequiresAudioProcessing() const override { return IsLoaded(); }

   bool OpenViaDialog();
   bool Open(const std::string& path);

   void Play();
   void Pause();
   void Restart();
   bool IsPlaying() const;
   bool IsLoaded() const { return mLoaded; }

   double Duration() const { return mDuration; }
   double Position() const;
   const std::string& FileName() const { return mFileName; }
   const std::string& FilePath() const { return mFilePath; }
   const std::string& Status() const { return mStatus; }
   const Platform::AudioLevels& Levels() const { return mLevels; }

   bool loop = true;
   bool followTransport = true; // play/pause with the global transport
   bool monitor = true;         // audible into the graph, or silent but still analysed
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
   std::unique_ptr<AudioFilePlayerAudioNode> mAudioNode;
   Platform::AudioLevels mLevels;
   std::string mFileName;
   std::string mFilePath;
   std::string mStatus = "no file loaded";
   bool mWasTransportPlaying = false;
   bool mLoaded = false;
   double mDuration = 0.0;
   int mLastCookFrame = -1;
};

// --- Audio Analyze ------------------------------------------------------
// Audio in, control values out. Makes every parameter in the graph
// audio-reactive, since any of these outputs can be patched into any slider.
//
// Three sources, in priority order:
//   1. Whatever is patched into its audio input pin - ANY IAudioSource, not
//      just an Audio File. Analysed by this node's own audio-thread half
//      (AudioAnalyzerAudioNode, AnalyzeNodes.cpp) running the same
//      SpectrumAnalyser the file player runs, over the buffer the cable
//      delivers. This replaced a bare `AudioFileNode* fileSource` pointer,
//      which could only ever reach a node that happened to publish
//      AudioLevels of its own - so a Filter, a Mixer, an Oscillator or Audio
//      In had nothing to offer it and the link was refused outright.
//   2. Failing that, the node's own Start/Stop live mic tap
//      (Platform::AudioStart/AudioRead), which is a separate AVAudioEngine
//      on the default *input* device and stays as the zero-patching default.
//
// Not an IAudioSource: it does pass its input through on the audio thread, but
// its graph-visible output is a set of modulator values, so it exposes its
// AudioNode via AudioNodeForNotePorts() (see INode.h's comment on exactly this
// distinction) rather than becoming cable-connectable as a source.
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

   // The audio source being analysed, when one is patched in.
   AudioCable input;
   AudioCable* AudioInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "audio" : nullptr; }

   // See the class comment: this node owns an AudioNode but is not itself an
   // audio source, so the topology builder finds it through here.
   AudioNode* AudioNodeForNotePorts() override;

   // A patched-in source must keep being analysed even with nothing
   // downstream of this node - there never is anything downstream, since its
   // real outputs are modulator values, not an audio buffer. Same pattern as
   // AudioFileNode's IsLoaded()-gated override.
   bool RequiresAudioProcessing() const override { return input.IsConnected(); }

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

   void EnsureAudioNode();

   Tap mTaps[kOutputCount];
   std::unique_ptr<AudioAnalyzerAudioNode> mAudioNode;
   Platform::AudioLevels mLevels;
   float mOnsetEnvelope = 0.0f;
   double mLastSeconds = 0.0;
   std::string mStatus = "press Start to listen";
   int mLastCookFrame = -1;
};
