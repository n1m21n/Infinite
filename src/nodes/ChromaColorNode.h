#pragma once

#include <memory>
#include <string>
#include <vector>
#include <OpenGL/gl3.h>

#include "INode.h"
#include "AudioCable.h"
#include "NoteCable.h"
#include "ImageCable.h"
#include "GLUtil.h"
#include "Modulation.h"
#include "audio/AudioNode.h"
#include "audio/MeterRing.h"

class ChromaColorAudioSink;

class ChromaColorNode : public INode
{
public:
   static constexpr int kNumPitches = 12;
   static constexpr int kOutputCount = 4 + kNumPitches; // Root, Hue, Consonance, Energy + 12 Pitches

   enum PalettePreset
   {
      kPresetCircleOfFifths = 0,
      kPresetScriabin,
      kPresetNewton,
      kPresetCustom,
      kPresetCount
   };

   enum LayoutMode
   {
      kLayoutChromatic = 0, // C, C#, D, D# ... B
      kLayoutCircleOfFifths, // C, G, D, A ... F
      kLayoutActiveChords,   // Only currently sounding notes sorted by loudness
      kLayoutCount
   };

   static const std::vector<std::string>& PresetNames();
   static const std::vector<std::string>& LayoutNames();
   static const char* PitchName(int pitchIndex);
   static int CircleOfFifthsOrder(int index);

   static INode* Create() { return new ChromaColorNode(); }
   ChromaColorNode();
   ~ChromaColorNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mOut); }
   int GetOutputWidth() const override { return mOut.w > 0 ? mOut.w : 256; }
   int GetOutputHeight() const override { return mOut.h > 0 ? mOut.h : 256; }
   unsigned long long TextureRevision() const override { return mRevision; }
   void CookIfNeeded(int frameId) override;

   INode* BypassSource() override { return mInput.GetSource(); }

   ImageCable& Input() { return mInput; }
   AudioCable& GetAudioInput() { return mAudioInput; }
   NoteCable& GetNoteInput() { return mNoteInput; }

   AudioCable* AudioInputSlot(int slot) override { return slot == 1 ? &mAudioInput : nullptr; }
   NoteCable* NoteInputSlot(int slot) override { return slot == 2 ? &mNoteInput : nullptr; }
   bool RequiresAudioProcessing() const override { return mAudioInput.IsConnected() || mNoteInput.IsConnected(); }

   const char* InputLabel(int slot) const override
   {
      if (slot == 0) return "img";
      if (slot == 1) return "audio";
      if (slot == 2) return "note";
      return nullptr;
   }

   AudioNode* GetAudioNode();

   // Modulator outputs
   int OutputCount() const override { return kOutputCount; }
   const char* OutputLabel(int index) const override;
   IModulator* ModulatorOutput(int index) override;
   float ModulatorValue(int index) const;

   // UI helper inspection
   float GetPitchEnergy(int pitchIndex) const { return (pitchIndex >= 0 && pitchIndex < 12) ? mSmoothedChroma[pitchIndex] : 0.0f; }
   float GetRootPitchNorm() const { return (float)mDetectedRoot / 12.0f; }
   int GetDetectedRoot() const { return mDetectedRoot; }
   bool IsDetectedMinor() const { return mDetectedMinor; }
   float GetKeyConfidence() const { return mKeyConfidence; }
   float GetCentroidHue() const { return mCentroidHue; }
   float GetConsonance() const { return mConsonance; }
   float GetTotalEnergy() const { return mTotalEnergy; }
   void EvaluateHarmonicRamp(float t, float outRgb[3]) const;

   void ApplyPreset(int presetIndex);
   void MarkDirty() { mLutDirty = true; }

   // --- Parameters ---
   int palettePreset = kPresetCircleOfFifths;
   int layoutMode = kLayoutCircleOfFifths;
   float gain = 1.2f;
   float attack = 20.0f;           // ms
   float decay = 250.0f;          // ms
   float minBrightness = 0.10f;
   float saturation = 1.0f;
   float rampMix = 1.0f;
   float pitchThreshold = 0.05f;

   // 12 Chromatic Pitch Colors (RGB)
   float pitchColors[kNumPitches][3] = {
      { 0.95f, 0.15f, 0.20f }, // 0: C  (Red)
      { 0.85f, 0.10f, 0.55f }, // 1: C# (Magenta/Crimson)
      { 1.00f, 0.55f, 0.05f }, // 2: D  (Yellow-Orange)
      { 0.70f, 0.20f, 0.90f }, // 3: D# (Purple/Violet)
      { 0.95f, 0.90f, 0.10f }, // 4: E  (Bright Yellow)
      { 0.85f, 0.10f, 0.15f }, // 5: F  (Deep Ruby)
      { 0.10f, 0.90f, 0.85f }, // 6: F# (Cyan/Teal)
      { 1.00f, 0.35f, 0.10f }, // 7: G  (Warm Orange)
      { 0.50f, 0.20f, 0.85f }, // 8: G# (Indigo/Deep Blue)
      { 0.20f, 0.85f, 0.25f }, // 9: A  (Emerald Green)
      { 0.15f, 0.60f, 0.95f }, // 10: A# (Sky Blue)
      { 0.60f, 0.90f, 0.20f }  // 11: B  (Lime/Chartreuse)
   };

   void VisitParams(ParamVisitor& v) override;

private:
   struct ModTap : public IModulator
   {
      ChromaColorNode* owner = nullptr;
      int index = 0;
      float Value01() override { return owner ? owner->ModulatorValue(index) : 0.0f; }
   };

   bool EnsureShader();
   void RebuildLut();
   void ProcessChromagram();

   std::unique_ptr<ChromaColorAudioSink> mAudioSink;
   AudioCable mAudioInput;
   NoteCable mNoteInput;
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
   float mRawChroma[kNumPitches] = { 0.0f };
   float mSmoothedChroma[kNumPitches] = { 0.0f };
   float mNoteActive[kNumPitches] = { 0.0f };

   int mDetectedRoot = 0;
   bool mDetectedMinor = false;
   float mKeyConfidence = 0.0f;
   float mCentroidHue = 0.0f;
   float mConsonance = 0.0f;
   float mTotalEnergy = 0.0f;
   double mLastTime = 0.0;
};
