#pragma once
// The engine's scene description (docs/plans/realtime-engine). Plain data, no GPU types, so the app builds it from
// nodes, a test builds it by hand, and any backend renders it. R1 carries what Render 3D's raster path draws with
// default materials: meshes, instances with a base-colour/metal/rough material, up to 8 lights, a gradient sky.

#include <cstdint>
#include <vector>

namespace engine
{
   struct MeshData
   {
      std::vector<float> posNormal; // x y z nx ny nz per vertex
      std::vector<uint32_t> indices;
   };

   struct Instance
   {
      int mesh = 0;
      float model[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };       // column-major, GL conventions
      float normalMatrix[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }; // inverse transpose of model
      float baseSrgb[3] = { 0.8f, 0.8f, 0.8f };
      float metallic = 0.0f;
      float roughness = 0.5f;
   };

   enum class LightType { Directional = 0, Point = 1, Sun = 2, Ambient = 3, Spot = 4 };

   struct Light
   {
      LightType type = LightType::Directional;
      float vec[3] = { 0, 1, 0 };   // direction toward the light, or its position (point, spot)
      float aim[3] = { 0, -1, 0 };  // spot axis
      float colorSrgb[3] = { 1, 1, 1 };
      float intensity = 1.0f;
      float spotAngleDeg = 45.0f, spotPenumbra = 0.2f;
      float diffuse = 1.0f, specular = 1.0f;
      float falloff = 1.0f; // k of 1 / (1 + d^2 k); negative = inverse square
      float cutoff = 0.0f;  // 0 = none
   };

   // One shadow-casting light (the first), a depth map fitted to the scene bounds. Spot and directional/sun only.
   struct Shadow
   {
      bool enabled = false;
      int size = 2048;
      float bias = 0.0025f;     // distance in the scene, as Render 3D's shadowBias
      float strength = 0.8f;
      float softness = 1.0f;    // PCF kernel in texels (<= 2 supported)
      float clipNear = 0.05f;
   };

   struct Frame
   {
      std::vector<Instance> instances;
      std::vector<Light> lights; // up to kMaxLights
      float viewProj[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }; // GL clip space, z in -1..1
      float eye[3] = { 0, 0, 5 };
      float exposure = 1.0f;
      float rim = 0.35f;
      int tonemap = 1; // 0 none, 1 ACES
      float ambientSrgb[3] = { 0.28f, 0.32f, 0.42f };
      float envSkySrgb[3] = { 0.35f, 0.5f, 0.75f }, envHorizonSrgb[3] = { 0.7f, 0.75f, 0.8f },
            envGroundSrgb[3] = { 0.2f, 0.2f, 0.22f };
      float envIntensity = 1.0f;
      float clearSrgb[3] = { 0.04f, 0.04f, 0.06f };
      Shadow shadow;
   };

   constexpr int kMaxLights = 8;
}
