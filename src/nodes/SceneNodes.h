#pragma once

#include <string>
#include <vector>

#include "INode.h"
#include "Mesh.h"

// Camera and lights are separate nodes so they can be animated and modulated
// independently, shared between renders, or swapped without touching the
// Render node. Render 3D falls back to its own built-in values when nothing is
// patched in, so existing patches keep working.
class CameraNode : public INode
{
public:
   static INode* Create() { return new CameraNode(); }
   static const std::vector<std::string>& ProjectionNames();

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}

   void ComputeEye(float outEye[3]) const;
   Mat4 ViewMatrix() const;
   Mat4 ProjectionMatrix(float aspect) const;

   int projection = 0;
   float fov = 45.0f;
   float orthoHeight = 1.5f;
   float distance = 3.0f;
   float azimuth = 0.6f;
   float elevation = 0.4f;
   float targetX = 0.0f, targetY = 0.0f, targetZ = 0.0f;
   float roll = 0.0f;
   float nearPlane = 0.05f;
   float farPlane = 100.0f;
   float orbitPerBeat = 0.0f; // free rotation, useful for turntables
   void VisitParams(ParamVisitor& v) override
   {
      v.Int("projection", projection); v.Float("fov", fov);
      v.Float("orthoHeight", orthoHeight); v.Float("distance", distance);
      v.Float("azimuth", azimuth); v.Float("elevation", elevation);
      v.Float("targetX", targetX); v.Float("targetY", targetY); v.Float("targetZ", targetZ);
      v.Float("roll", roll); v.Float("near", nearPlane); v.Float("far", farPlane);
      v.Float("orbitPerBeat", orbitPerBeat);
   }
};

class LightNode : public INode
{
public:
   static INode* Create() { return new LightNode(); }
   static const std::vector<std::string>& TypeNames();

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}

   // Direction for a directional light, world position for a point light.
   void ComputeVector(float out[3]) const;

   int type = 0; // directional / point
   float azimuth = 0.9f;
   float elevation = 0.9f;
   float distance = 4.0f;
   float color[3] = { 1.0f, 0.98f, 0.94f };
   float intensity = 1.2f;
   float falloff = 1.0f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("type", type); v.Float("azimuth", azimuth); v.Float("elevation", elevation);
      v.Float("distance", distance); v.Color("color", color);
      v.Float("intensity", intensity); v.Float("falloff", falloff);
      v.Float("orbitPerBeat", orbitPerBeat);
   }
   float orbitPerBeat = 0.0f;
};
