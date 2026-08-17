#pragma once

#include <chrono>
#include <memory>
#include <vector>
#include <string>

#include "core/AudioCable.h"
#include "audio/AudioNode.h"
#include "audio/MeterRing.h"
#include "core/INode.h"
#include "core/Mesh.h"
#include "nodes/Geometry3DNodes.h"

class AudioDisplaceAudioSink;

class AudioDisplacementNode : public INode, public IGeometrySource
{
public:
   enum Mode
   {
      kModeNormalWaveform = 0, // Pushes along vertex normal by oscilloscope waveform s(coord)
      kModeSpectralBands,      // Deforms along U/X axis based on real-time FFT frequency spectrum
      kModeAcousticRipples,    // Concentric radial acoustic sound ripples expanding from center
      kModeCymaticsChladni,    // Simulates 2D/3D Ernst Chladni standing wave modal patterns
      kModeDirectionalAxis,    // Pushes along local X/Y/Z axis
      kModeCount
   };

   static const std::vector<std::string>& ModeNames();

   static INode* Create() { return new AudioDisplacementNode(); }
   AudioDisplacementNode();
   ~AudioDisplacementNode() override;

   INode* BypassSource() override { return dynamic_cast<INode*>(input); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   AudioNode* GetAudioNode();
   AudioCable& GetAudioInput() { return mAudioInput; }
   AudioCable* AudioInputSlot(int slot) override { return slot == 1 ? &mAudioInput : nullptr; }
   bool RequiresAudioProcessing() const override { return mAudioInput.IsConnected(); }

   const Mesh& GetMesh() override;
   unsigned long long MeshRevision() override;
   Mat4 GetModelMatrix() const override
   {
      return input ? input->GetModelMatrix() : Mat4::Identity();
   }
   Material GetMaterial() const override;
   unsigned int GetSurfaceTexture() override;
   unsigned int GetMaterialTexture(int map) override
   {
      return input ? input->GetMaterialTexture(map) : 0;
   }
   unsigned long long SurfaceTextureRevision() const override
   {
      return input ? input->SurfaceTextureRevision() : 0;
   }
   MappingTransform GetMappingTransform() const override
   {
      return input ? input->GetMappingTransform() : MappingTransform();
   }

   IGeometrySource* input = nullptr;
   IGeometrySource** GeometryInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }

   const char* InputLabel(int slot) const override
   {
      if (slot == 0) return "geo";
      if (slot == 1) return "audio";
      return nullptr;
   }

   size_t TriangleCount() const { return mCache.indices.size() / 3; }

   // --- Parameters ---
   int mode = kModeNormalWaveform;
   float strength = 1.0f;
   float frequency = 2.0f;       // Spatial wave frequency / scaling
   float speed = 1.0f;           // Wave propagation / phase speed
   float damping = 1.0f;         // Spatial decay / edge attenuation
   float attack = 10.0f;         // ms ballistics smoothing
   float decay = 150.0f;         // ms ballistics smoothing
   float midlevel = 0.0f;        // Neutral zero offset
   int axis = 1;                 // 0: X, 1: Y, 2: Z (for Directional mode)
   int subdivide = 2;            // 0..4 real-time subdivision levels for high-res mesh ripples

   // Cymatics modal numbers
   int chladniM = 3;
   int chladniN = 5;

   bool flatShade = false;
   bool flipNormals = false;
   bool inheritMaterial = true;
   bool selectionOnly = false;

   // PBR Material (when not inheriting)
   float color[3] = { 0.15f, 0.75f, 0.95f };
   float metallic = 0.2f;
   float roughness = 0.35f;
   float opacity = 1.0f;
   int shading = 0;
   float emissionColor[3] = { 0.0f, 0.6f, 1.0f };
   float emission = 0.0f;
   float ior = 1.5f;
   float transmission = 0.0f;
   float transmissionRoughness = 0.0f;
   float specular = 0.5f;
   float clearcoat = 0.0f;
   float clearcoatRoughness = 0.03f;
   float subsurface = 0.0f;
   float subsurfaceColor[3] = { 0.1f, 0.5f, 1.0f };
   float subsurfaceRadius = 0.5f;

   void VisitParams(ParamVisitor& v) override;

   // Real-time audio waveform / spectrum buffer for UI visualization
   int ReadAudioScope(float* out, int capacity);
   const std::vector<float>& GetSmoothedSpectrum() const { return mSmoothedSpectrum; }

private:
   AudioCable mAudioInput;
   std::unique_ptr<AudioDisplaceAudioSink> mAudioSink;

   struct Signature
   {
      int mode = 0;
      float strength = 0.0f;
      float frequency = 0.0f;
      float speed = 0.0f;
      float damping = 0.0f;
      float midlevel = 0.0f;
      int axis = 0;
      int subdivide = 0;
      int chladniM = 0;
      int chladniN = 0;
      bool flat = false;
      bool flip = false;
      bool selectionOnly = false;
      const IGeometrySource* upstream = nullptr;
      unsigned long long upstreamRevision = 0;
      // Quantised (not raw float) so float jitter doesn't defeat the cache -
      // replaces a previous `audioFrameId` counter that advanced on every
      // audio frame that produced samples, *including silence* (silence
      // still produces samples), which meant the cache invalidated and the
      // full subdivide+weld+displace+recalc-normals pipeline reran on every
      // single frame audio was connected, deformed or not. It also meant
      // that disconnecting the audio cable (RequiresAudioProcessing() then
      // false, the ring goes empty, the counter freezes) left the mesh
      // permanently locked in its last deformed pose even as mCurrentEnergy
      // decayed to zero underneath it. Keying on the energy itself instead
      // means the signature naturally tracks the decay (rebuilding while it
      // audibly settles) and naturally stabilises at rest once it's done -
      // and, combined with mBaseMeshCache below, the *expensive* part
      // (subdivide/weld) still only reruns when topology actually changes.
      int energyQuant = 0;

      bool operator==(const Signature& o) const
      {
         return mode == o.mode && strength == o.strength && frequency == o.frequency &&
                speed == o.speed && damping == o.damping && midlevel == o.midlevel &&
                axis == o.axis && subdivide == o.subdivide && chladniM == o.chladniM && chladniN == o.chladniN &&
                flat == o.flat && flip == o.flip && selectionOnly == o.selectionOnly &&
                upstream == o.upstream && upstreamRevision == o.upstreamRevision &&
                energyQuant == o.energyQuant;
      }
   };

   Signature CurrentSignature() const;

   Mesh mCache;
   Signature mBuilt;
   bool mHasBuilt = false;
   unsigned long long mMeshRevision = 1;
   int mLastCookFrame = -1;
   std::chrono::steady_clock::time_point mLastCookTime{};
   float mPhaseAccum = 0.0f;
   float mCurrentEnergy = 0.0f;
   std::vector<float> mSmoothedSpectrum;
   std::vector<float> mAudioWaveform;
   uint64_t mAudioFrameCounter = 0;

   // Separate cache for the subdivided base mesh + weld map + selection
   // mask, keyed only on (upstream, upstream revision, subdivide level) -
   // independent of the full Signature above, which also varies with the
   // (fast-changing, while audio plays) energy. Subdivide (up to 256x the
   // triangle count at level 4) + BuildWeldMap only need to rerun when the
   // topology actually changes; only the displacement pass and
   // RecalculateNormals need to run per audio frame.
   Mesh mBaseMeshCache;
   std::vector<unsigned int> mWeldMapCache;
   std::vector<unsigned char> mVertexSelectionCache;
   const IGeometrySource* mBaseCachedUpstream = nullptr;
   unsigned long long mBaseCachedUpstreamRevision = 0;
   int mBaseCachedSubdivide = -1;
   bool mBaseHasCache = false;
};
