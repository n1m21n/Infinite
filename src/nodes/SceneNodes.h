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
   float azimuth = 34.3775f;
   float elevation = 22.9183f;
   float targetX = 0.0f, targetY = 0.0f, targetZ = 0.0f;
   float roll = 0.0f;
   float nearPlane = 0.05f;
   float farPlane = 100.0f;
   float orbitPerBeat = 0.0f; // free rotation, useful for turntables
   float shiftX = 0.0f, shiftY = 0.0f;   // lens shift, in widths of the sensor
   float sensorWidth = 36.0f;            // mm, what focal length is measured against
   float sensorHeight = 24.0f;           // mm, used when the sensor fits the frame's height
   int sensorFit = 0;                    // 0 auto (the longer side), 1 horizontal, 2 vertical
   // In the 3D View only: marker size, clip range and focus line, name and sensor gate in the camera view.
   float displaySize = 1.0f;
   bool showLimits = false, showName = false, showSensor = false, showMist = false;

   static const std::vector<std::string>& SensorFitNames();
   // The vertical view's sensor size in mm for a frame of `aspect` (width / height): focal length
   // and field of view convert through it, tan(fov / 2) = span / (2 * mm).
   float SensorSpan(float aspect) const;
   float FocalLength(float aspect) const;
   void SetFocalLength(float mm, float aspect);
   void VisitParams(ParamVisitor& v) override
   {
      v.Int("projection", projection); v.Float("fov", fov);
      v.Float("shiftX", shiftX); v.Float("shiftY", shiftY); v.Float("sensorWidth", sensorWidth);
      v.Float("sensorHeight", sensorHeight); v.Int("sensorFit", sensorFit);
      v.Float("displaySize", displaySize); v.Bool("showLimits", showLimits);
      v.Bool("showName", showName); v.Bool("showSensor", showSensor); v.Bool("showMist", showMist);
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
   // A black body's colour at `kelvin`, linear sRGB, brightest channel 1 (6500 K is about white).
   static void TemperatureColor(float kelvin, float out[3]);

   int type = 0; // directional / point / sun / ambient / spot / area
   float azimuth = 51.5662f;
   float elevation = 51.5662f;
   float distance = 4.0f;
   float color[3] = { 1.0f, 0.98f, 0.94f };
   float intensity = 1.2f;
   float falloff = 1.0f;
   float spotAngle = 45.0f;       // degrees half-angle for spot cone
   float spotPenumbra = 0.2f;    // 0..1 softness of the cone edge
   // Area: a one-sided rectangle at `distance`, facing the origin. Sizes in world units; intensity is
   // the brightness of the emitting face, so resizing it changes the soft look, not the exposure.
   float areaWidth = 1.0f;
   float areaHeight = 1.0f;
   bool normalize = true;          // area: off, a bigger panel gives more light
   // How much of each kind of shading this light does, 1 = all of it.
   float diffuse = 1.0f, specular = 1.0f, transmission = 1.0f;
   bool softFalloff = true;        // point, spot, area: no blow-up close to the light
   // Used when this light casts the scene's shadow.
   bool castShadow = true;
   float shadowRadius = 0.25f;     // size of the light: a bigger one softens the shadow edge
   float shadowFilter = 1.0f;      // extra blur on top of that
   int shadowRes = 0;              // 0 = the look's quality, 1 / 2 / 3 = 1024 / 2048 / 4096
   float shadowClip = 0.05f;       // nothing nearer the light than this casts
   bool useTemperature = false;    // tint the colour by a black body at `temperature`
   float temperature = 6500.0f;    // kelvin
   float exposure = 0.0f;          // stops on top of the strength
   bool useCutoff = false;         // point, spot, area: no light at all past `cutoffDistance`
   float cutoffDistance = 40.0f;
   bool showCone = false;          // spot: the 3D View draws its whole cone, out to the target or the cutoff

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("type", type); v.Float("azimuth", azimuth); v.Float("elevation", elevation);
      v.Float("distance", distance); v.Color("color", color);
      v.Float("intensity", intensity); v.Float("falloff", falloff);
      v.Float("spotAngle", spotAngle); v.Float("spotPenumbra", spotPenumbra);
      v.Float("orbitPerBeat", orbitPerBeat);
      v.Float("areaWidth", areaWidth); v.Float("areaHeight", areaHeight);
      v.Bool("normalize", normalize);
      v.Float("diffuse", diffuse); v.Float("specular", specular); v.Float("transmission", transmission);
      v.Bool("softFalloff", softFalloff);
      v.Bool("castShadow", castShadow); v.Float("shadowRadius", shadowRadius);
      v.Float("shadowFilter", shadowFilter); v.Int("shadowRes", shadowRes); v.Float("shadowClip", shadowClip);
      v.Bool("useTemperature", useTemperature); v.Float("temperature", temperature);
      v.Float("exposure", exposure);
      v.Bool("useCutoff", useCutoff); v.Float("cutoffDistance", cutoffDistance);
      v.Bool("showCone", showCone);
   }
   float orbitPerBeat = 0.0f;
};
