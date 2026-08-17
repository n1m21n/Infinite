#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/INode.h"
#include "core/AudioCable.h"
#include "audio/AudioNode.h"
#include "audio/MeterRing.h"
#include "core/Mesh.h"
#include "nodes/Geometry3DNodes.h"

class AudioRibbonAudioSink;

class AudioRibbonNode : public INode, public IGeometrySource
{
public:
   static INode* Create() { return new AudioRibbonNode(); }
   AudioRibbonNode();
   ~AudioRibbonNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   AudioNode* GetAudioNode();
   AudioCable& GetAudioInput() { return mAudioInput; }
   AudioCable* AudioInputSlot(int slot) override { return slot == 0 ? &mAudioInput : nullptr; }
   bool RequiresAudioProcessing() const override { return mAudioInput.IsConnected(); }

   const Mesh& GetMesh() override;
   unsigned long long MeshRevision() override;
   Mat4 GetModelMatrix() const override;
   Material GetMaterial() const override;
   unsigned int GetSurfaceTexture() override { return 0; }

   size_t TriangleCount() const { return mMesh.indices.size() / 3; }

   void VisitParams(ParamVisitor& v) override
   {
      v.Float("width", width);
      v.Float("heightScale", heightScale);
      v.Int("segments", segments);
      v.Float("gain", gain);
      v.Float("smoothing", smoothing);

      v.Float("posX", posX); v.Float("posY", posY); v.Float("posZ", posZ);
      v.Float("rotX", rotX); v.Float("rotY", rotY); v.Float("rotZ", rotZ);
      v.Float("scaleX", scaleX); v.Float("scaleY", scaleY); v.Float("scaleZ", scaleZ);
      v.Float("uniformScale", uniformScale);

      v.Color("color", color);
      v.Float("metallic", metallic);
      v.Float("roughness", roughness);
      v.Float("opacity", opacity);
      v.Int("shading", shading);
      v.Color("emissionColor", emissionColor);
      v.Float("emission", emission);
      v.Float("ior", ior);
   }

   // Ribbon geometry parameters
   float width = 1.0f;
   float heightScale = 1.0f;
   int segments = 128;
   float gain = 1.0f;
   float smoothing = 0.0f;

   // Transform parameters
   float posX = 0.0f, posY = 0.0f, posZ = 0.0f;
   float rotX = 0.0f, rotY = 0.0f, rotZ = 0.0f;
   float scaleX = 1.0f, scaleY = 1.0f, scaleZ = 1.0f;
   float uniformScale = 1.0f;

   // Material parameters
   float color[3] = { 0.85f, 0.86f, 0.9f };
   float metallic = 0.1f;
   float roughness = 0.45f;
   float opacity = 1.0f;
   int shading = 0;
   float emissionColor[3] = { 1.0f, 0.85f, 0.6f };
   float emission = 0.0f;
   float ior = 1.5f;

private:
   void RebuildRibbon();

   std::unique_ptr<AudioRibbonAudioSink> mAudioSink;
   AudioCable mAudioInput;

   Mesh mMesh;
   unsigned long long mMeshRevision = 1;

   std::vector<float> mAudioWaveform;
   std::vector<float> mSmoothedWaveform;
   int mLastCookFrame = -1;

   // Parameter tracking to detect UI changes
   float mLastWidth = 0.0f;
   float mLastHeightScale = 0.0f;
   int mLastSegments = 0;
   float mLastGain = 0.0f;
   float mLastSmoothing = 0.0f;
};
