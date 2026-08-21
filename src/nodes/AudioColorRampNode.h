#pragma once

#include <memory>
#include <string>
#include <vector>
#include <OpenGL/gl3.h>

#include "INode.h"
#include "AudioCable.h"
#include "ImageCable.h"
#include "GLUtil.h"
#include "Modulation.h"
#include "audio/AudioNode.h"
#include "audio/MeterRing.h"

class AudioColorRampAudioSink;

class AudioColorRampNode : public INode
{
public:
   static constexpr int kMaxBands = 8;
   static constexpr int kDefaultBands = 4;

   enum Mode
   {
      kModeIntensity = 0, // Band energy modulates color intensity / glow
      kModeExpansion,     // Band energy expands the width of each color stop
      kModeSpectrum,      // Direct continuous FFT spectrum mapped to palette
      kModeCount
   };

   enum Interp
   {
      kInterpLinear = 0,
      kInterpConstant,
      kInterpSmooth,
      kInterpCount
   };

   static const std::vector<std::string>& ModeNames();
   static const std::vector<std::string>& InterpNames();

   static INode* Create() { return new AudioColorRampNode(); }
   AudioColorRampNode();
   ~AudioColorRampNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mOut); }
   int GetOutputWidth() const override { return mOut.w > 0 ? mOut.w : 256; }
   int GetOutputHeight() const override { return mOut.h > 0 ? mOut.h : 256; }
   unsigned long long TextureRevision() const override { return mRevision; }
   void CookIfNeeded(int frameId) override;

   INode* BypassSource() override { return mInput.GetSource(); }

   // Slots: 0 = optional upstream Image, 1 = Audio stream
   ImageCable& Input() { return mInput; }
   AudioCable& GetAudioInput() { return mAudioInput; }
   AudioCable* AudioInputSlot(int slot) override { return slot == 1 ? &mAudioInput : nullptr; }
   bool RequiresAudioProcessing() const override { return mAudioInput.IsConnected(); }

   const char* InputLabel(int slot) const override
   {
      if (slot == 0) return "img";
      if (slot == 1) return "audio";
      return nullptr;
   }

   AudioNode* GetAudioNode();

   // Frequency and position math (Logarithmic 20Hz - 20kHz)
   static float PosToFreq(float pos);
   static float FreqToPos(float freq);

   // UI helper methods
   int GetBandCount() const { return bandCount; }
   void SetBandCount(int count);
   void EvaluateRamp(float t, float outRgb[3]) const;
   const std::vector<float>& GetSmoothedSpectrum() const { return mSmoothedSpectrum; }
   const float* GetBandEnergies() const { return mBandEnergies; }
   float GetMasterEnergy() const { return mMasterEnergy; }

   void MarkDirty() { mLutDirty = true; }

   // --- Parameters ---
   int mode = kModeIntensity;
   int interpMode = kInterpLinear;
   int bandCount = kDefaultBands;
   float gain = 1.0f;
   float minBrightness = 0.15f;    // Baseline color floor

   // Band split positions (log scale 0..1). For N bands, there are N-1 crossover dividers.
   float crossoverPos[kMaxBands - 1] = { 0.30f, 0.53f, 0.77f, 0.85f, 0.90f, 0.94f, 0.97f };

   // Band colors (RGB), low frequency to high - VIBGYOR read high to low.
   float bandColor[kMaxBands][3] = {
      { 0.95f, 0.15f, 0.15f }, // Band 0: Red (Sub/Bass)
      { 1.00f, 0.55f, 0.10f }, // Band 1: Orange (Low-Mids)
      { 1.00f, 0.90f, 0.20f }, // Band 2: Yellow (Mids)
      { 0.20f, 0.95f, 0.40f }, // Band 3: Green (Presence)
      { 0.10f, 0.85f, 0.85f }, // Band 4: Cyan
      { 0.20f, 0.45f, 1.00f }, // Band 5: Blue
      { 0.45f, 0.20f, 0.95f }, // Band 6: Indigo
      { 0.75f, 0.20f, 1.00f }  // Band 7: Violet (Air/Highs)
   };

   // Band sensitivity multipliers
   float bandGain[kMaxBands] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

   void VisitParams(ParamVisitor& v) override;

private:
   bool EnsureShader();
   void RebuildLut();
   void ProcessAudioFFT();

   std::unique_ptr<AudioColorRampAudioSink> mAudioSink;
   AudioCable mAudioInput;
   ImageCable mInput;

   GLUtil::Fbo mOut;
   unsigned int mProgram = 0;
   bool mShaderTried = false;
   bool mHadInput = false;
   unsigned int mLutTex = 0;
   bool mLutDirty = true;
   unsigned long long mRevision = 1;
   int mLastCookFrame = -1;

   std::vector<float> mAudioWindow;
   std::vector<float> mSmoothedSpectrum;
   float mBandEnergies[kMaxBands] = { 0.0f };
   float mRawBandEnergies[kMaxBands] = { 0.0f };
   float mMasterEnergy = 0.0f;
   double mLastTime = 0.0;
};
