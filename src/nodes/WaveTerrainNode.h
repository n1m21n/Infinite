#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "audio/dsp/WaveTerrainDsp.h"
#include "core/INode.h"
#include "core/ImageCable.h"
#include "core/NoteCable.h"

class AudioWaveTerrainNode;

// Wave Terrain Synthesis Node.
// Converts arbitrary 2D textures/images (Noise, Draw, Video, Reaction-Diffusion, Shapes)
// into a continuous 3D topographic surface z = T(u, v) and samples closed orbital
// trajectories at audio rates. The resulting cycles are bandlimited via Real Discrete
// Fourier Transform into an exact 10-level mip pyramid to ensure alias-free sound.
class WaveTerrainNode : public INode, public IAudioSource
{
public:
   static constexpr int kMaxVoices = 8;
   static constexpr int kMaxUnison = 8;
   static constexpr int kScopeCapacity = 128;

   static INode* Create() { return new WaveTerrainNode(); }
   static const std::vector<std::string>& OrbitTypeNames();
   static const std::vector<std::string>& ChannelModeNames();
   static const std::vector<std::string>& FilterTypeNames();

   WaveTerrainNode();
   ~WaveTerrainNode() override;

   unsigned int GetOutputTexture() override { return mPreviewTex; }
   int GetOutputWidth() const override { return mPreviewSize; }
   int GetOutputHeight() const override { return mPreviewSize; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;

   const char* InputLabel(int slot) const override
   {
      if (slot == 0) return "texture";
      if (slot == 1) return "note";
      return nullptr;
   }

   ImageCable& TextureInput() { return mTextureInput; }
   NoteCable& NoteInput() { return mNoteInput; }

   NoteCable* NoteInputSlot(int slot) override { return slot == 1 ? &mNoteInput : nullptr; }

   // "detune"/"stereoWidth" only spread unison voices apart in RenderVoice -
   // both are a no-op while "unison" (default 1) isn't > 1. See INode's
   // SweepPrerequisitesFor comment.
   std::vector<SweepParamPrereq> SweepPrerequisitesFor(const std::string& paramName) const override
   {
      if (paramName == "detune" || paramName == "stereoWidth")
         return { { "unison", 2.0f } };
      return {};
   }

   int ReadScope(float* out, int capacity);
   int ActiveVoices() const;

   // ------------------------------------------------ Parameters
   // Terrain Orbit Parameters
   int orbitType = WaveTerrainDsp::kOrbitCircle;
   int channel = WaveTerrainDsp::kChanLuminance;
   float centerX = 0.5f;
   float centerY = 0.5f;
   float radiusX = 0.35f;
   float radiusY = 0.35f;
   float ratioA = 1.0f;
   float ratioB = 1.0f;
   float phaseOffset = 0.25f;
   float rotation = 0.0f;      // degrees
   float scanSpeed = 0.0f;     // continuous orbital drift rate
   float position = 0.0f;      // 0..1 morph position across 8 frames

   // Voice & Tuning
   float volume = 0.8f;
   float pan = 0.0f;
   float frequency = 220.0f;   // free-running Hz when no note cable is attached
   int octave = 0;             // +/- 4
   int semi = 0;               // +/- 12
   float fine = 0.0f;          // +/- 50 cents
   float glide = 0.0f;         // portamento seconds

   // Unison
   int unison = 1;             // 1..8 voices
   float detune = 12.0f;       // cents
   float stereoWidth = 0.5f;   // 0..1

   // Amp Envelope
   float ampAttack = 5.0f;     // ms
   float ampDecay = 250.0f;    // ms
   float ampSustain = 0.75f;   // 0..1
   float ampRelease = 200.0f;  // ms

   // Filter & Envelope
   int filterType = 1;         // 0: Off, 1: LP12, 2: LP24, 3: HP12, 4: BP
   float cutoff = 10000.0f;    // Hz
   float resonance = 0.2f;     // 0..1
   float filterAmount = 0.0f;  // -8..8 octaves
   float filterAttack = 5.0f;  // ms
   float filterDecay = 300.0f; // ms
   float filterSustain = 0.4f; // 0..1
   float filterRelease = 250.0f; // ms

   // Drive / Saturation
   float drive = 0.0f;         // 0..1 wave saturation

   // UI oscilloscope cache
   float scopeCache[kScopeCapacity] = {};
   int scopeCacheCount = 0;
   double scopeCacheTime = -1.0;

   float CurrentRotation() const { return mCurrentRotation; }

private:
   ImageCable mTextureInput;
   NoteCable mNoteInput;
   std::unique_ptr<AudioWaveTerrainNode> mAudioNode;

   // GPU readback & preview FBO
   unsigned int mFbo = 0;
   unsigned int mPreviewTex = 0;
   int mPreviewSize = 128;
   std::vector<uint8_t> mPixels;
   int mLastCookFrame = -1;
   float mCurrentRotation = 0.0f;
   unsigned long long mLastTexRev = 0;
   float mLastCenterX = -1.0f;
   float mLastCenterY = -1.0f;
   float mLastRadiusX = -1.0f;
   float mLastRadiusY = -1.0f;
   float mLastRatioA = -1.0f;
   float mLastRatioB = -1.0f;
   float mLastPhaseOffset = -1.0f;
   float mLastTotalRot = -999.0f;
   float mLastRotationParam = -999.0f;
   std::chrono::steady_clock::time_point mLastContinuousRebuild{};
   int mLastOrbitType = -1;
   int mLastChannel = -1;

   void EnsurePreviewResources(int size);
   void RenderPreview(int frameId);
};
