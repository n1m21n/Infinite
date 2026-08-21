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
   static constexpr int kOutputCount = 1 + kMaxBands; // Master + 8 bands

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

   // Modulator outputs: 0 = master level, 1..8 = band levels
   int OutputCount() const override { return kOutputCount; }
   const char* OutputLabel(int index) const override;
   IModulator* ModulatorOutput(int index) override;
   float ModulatorValue(int index) const;

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
   float attack = 15.0f;           // ms
   float decay = 120.0f;           // ms
   float minBrightness = 0.15f;    // Baseline color floor
   float rampMix = 1.0f;           // Blend when image is connected
   float saturationBoost = 1.2f;

   // Band split positions (log scale 0..1). For N bands, there are N-1 crossover dividers.
   float crossoverPos[kMaxBands - 1] = { 0.30f, 0.53f, 0.77f, 0.85f, 0.90f, 0.94f, 0.97f };

   // Band colors (RGB)
   float bandColor[kMaxBands][3] = {
      { 0.95f, 0.15f, 0.25f }, // Band 0: Deep Red/Crimson (Sub/Bass)
      { 1.00f, 0.55f, 0.10f }, // Band 1: Amber/Gold (Low-Mids)
      { 0.10f, 0.85f, 0.95f }, // Band 2: Electric Cyan (Mid-Highs)
      { 0.80f, 0.30f, 1.00f }, // Band 3: Neon Violet/Lavender (Presence/Air)
      { 0.20f, 0.95f, 0.40f }, // Band 4: Emerald Green
      { 1.00f, 0.90f, 0.20f }, // Band 5: Bright Yellow
      { 0.95f, 0.20f, 0.75f }, // Band 6: Hot Pink
      { 0.30f, 0.50f, 1.00f }  // Band 7: Royal Blue
   };

   // Band sensitivity multipliers
   float bandGain[kMaxBands] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

   void VisitParams(ParamVisitor& v) override;

private:
   struct ModTap : public IModulator
   {
      AudioColorRampNode* owner = nullptr;
      int index = 0;
      float Value01() override { return owner ? owner->ModulatorValue(index) : 0.0f; }
   };

   bool EnsureShader();
   void RebuildLut();
   void ProcessAudioFFT();

   std::unique_ptr<AudioColorRampAudioSink> mAudioSink;
   AudioCable mAudioInput;
   ImageCable mInput;

   ModTap mTaps[kOutputCount];
   GLUtil::Fbo mOut;
   unsigned int mProgram = 0;
   bool mShaderTried = false;
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
