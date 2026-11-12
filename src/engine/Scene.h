#pragma once
// The engine's scene description (docs/plans/realtime-engine). Plain data, no GPU types, so the app builds it from
// nodes, a test builds it by hand, and any backend renders it. R1 carries what Render 3D's raster path draws with
// default materials: meshes, instances with a base-colour/metal/rough material, up to 8 lights, a gradient sky.

#include <cstdint>
#include <string>
#include <vector>

namespace engine
{
   struct MeshData
   {
      std::vector<float> posNormal; // x y z nx ny nz per vertex
      std::vector<uint32_t> indices;
      std::vector<float> uv;        // u v per vertex (GL convention, v = 0 is the image's first row); empty = none
      // Row 4.3: the rays' mesh when it differs from the drawn one (a bump map tessellated and displaced into real
      // relief). Empty = the rays see the drawn mesh. Same layout as above; the BLAS and every ray-hit read use it.
      std::vector<float> tracePosNormal;
      std::vector<uint32_t> traceIndices;
      std::vector<float> traceUv;
      // Rows 4.4 / 3.3: curvature, cavity, attr per vertex (MeshWear::Stream), present only when a material program
      // reads one of them; empty = none (all read 0). traceWear: the same over the trace mesh, when there is one.
      std::vector<float> wear, traceWear;
   };

   struct Instance
   {
      int mesh = 0;
      float model[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };       // column-major, GL conventions
      float normalMatrix[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }; // inverse transpose of model
      float baseSrgb[3] = { 0.8f, 0.8f, 0.8f };
      float metallic = 0.0f;
      float roughness = 0.5f;
      float transmission = 0.0f; // glass: live = two refractions through the hit object, then the scene beyond
      float ior = 1.5f;
      float emission[3] = { 0.0f, 0.0f, 0.0f }; // linear light leaving the surface (colour * strength)
      // Material maps, as Render 3D's GL path reads them: indices into Frame::textures, -1 = none. Albedo is sRGB and
      // multiplies the base colour; roughness and metallic multiply by the red channel; normal is a tangent-space map
      // (or a grey height map with normalBump) through the screen-space cotangent frame. Sheen is the Charlie lobe.
      int mapAlbedo = -1, mapRoughness = -1, mapMetallic = -1, mapNormal = -1;
      int wrap = 0;              // 0 clamp, 1 repeat, 2 mirror (Material's wrapMode)
      float normalStrength = 1.0f;
      int normalBump = 0;
      float sheen = 0.0f, sheenColor[3] = { 1.0f, 1.0f, 1.0f }, sheenRoughness = 0.5f, sheenDir = 0.0f; // sheenColor as GL uses it (raw)
      // The rest of the Material's maps (red channel multiplies the value; emission is sRGB and multiplies the colour),
      // as GL reads them. Detail: the normal map read again at uv * detailScale (0 = off), blended over the first.
      int mapAo = -1, mapEmission = -1, mapSheen = -1, mapOpacity = -1, mapClearcoat = -1, mapTransmission = -1;
      float clearcoat = 0.0f, clearcoatRoughness = 0.03f;
      float specular = 0.5f;               // dielectric F0 = ((ior - 1) / (ior + 1))^2 * 2 * specular, as GL
      float opacity = 1.0f, alphaCutoff = 0.0f; // cutoff > 0: cut below it; else opacity is a stochastic coverage
      float detailScale = 0.0f, detailStrength = 1.0f;
      // Mapping (the Mapping node): the coordinate space (0 uv, 1 generated, 2 object), its transform, triplanar blend
      // and the mesh's own box for generated; breakup > 0 hides tiling (two offset reads blended by a noise).
      int mapSpace = 0;
      float mapScale[3] = { 1.0f, 1.0f, 1.0f }, mapRotate[3] = { 0.0f, 0.0f, 0.0f }, mapTranslate[3] = { 0.0f, 0.0f, 0.0f };
      float triplanar = 0.0f, breakup = 0.0f;
      float objLo[3] = { 0.0f, 0.0f, 0.0f }, objHi[3] = { 1.0f, 1.0f, 1.0f };
      // Hair (Render3DHair.h): hairRadial > 0 shades the Chiang fibre BSDF along the vertex normal (the strand axis),
      // with absorption hairSigma, cuticle tilt hairShift (radians), from triangle hairFrom on (a Fur mesh's strands; the
      // skin below keeps the Principled material).
      float hairSigma[3] = { 0.0f, 0.0f, 0.0f };
      float hairRadial = 0.0f, hairShift = 0.0f;
      int hairFrom = 0;
      int strandFrom = -1; // row 5.2: a two-sided ribbon mesh's first strand triangle (Fur), -1 none
      // Subsurface (weight, per-channel mean free path in scene units) and thin translucency (share of diffuse
      // passed to the back side).
      float subsurface = 0.0f, sssRadius[3] = { 0.0f, 0.0f, 0.0f };
      float translucency = 0.0f;
      float iridescence = 0.0f, iridescenceIor = 1.3f, iridescenceThickness = 400.0f; // thin film (Render3DThinFilm.h)
      bool castShadow = true; // false: drawn and seen by bounces, but shadow rays and the shadow map pass it by
      // Field material program (core/field/MaterialRegistry.h): the id the shader switches on, 0 = none. It runs after
      // the maps are read and replaces the outputs it writes. seeThrough: it writes opacity or mask.
      int fieldProgram = 0;
      bool fieldSeeThrough = false;
      int fieldImage[4] = { -1, -1, -1, -1 }; // row 2.4: what img(slot, uv) reads, indices into Frame::textures
      float fieldParams[16] = {};               // row 3.4: the program's live knobs (FldMat.prm0..3)
   };

   // RGBA8 pixels of a material map (row 0 = GL's first row). key changes when the pixels do; the backend keeps the
   // GPU copy between frames by key.
   struct TextureData
   {
      const uint8_t* rgba = nullptr;
      int w = 0, h = 0;
      unsigned long long key = 0;
   };
   constexpr int kMaxTextures = 16;

   // Many copies of one mesh drawn with one instanced call (R5). Raster only: batches are not in the ray-traced
   // structure (no RT shadows/reflections/GI on them) and cast no shadow-map shadow. Uniform scale per copy.
   struct InstanceBatch
   {
      int mesh = 0;
      std::vector<float> models; // 16 floats per copy, column-major
      float baseSrgb[3] = { 0.8f, 0.8f, 0.8f };
      float metallic = 0.0f;
      float roughness = 0.5f;
      size_t Count() const { return models.size() / 16; }
   };

   enum class LightType { Directional = 0, Point = 1, Sun = 2, Ambient = 3, Spot = 4, Area = 5 };

   struct Light
   {
      LightType type = LightType::Directional;
      float vec[3] = { 0, 1, 0 };   // direction toward the light, or its position (point, spot)
      float aim[3] = { 0, -1, 0 };  // spot axis
      float colorSrgb[3] = { 1, 1, 1 };
      float intensity = 1.0f;
      float spotAngleDeg = 45.0f, spotPenumbra = 0.2f;
      float areaW = 1.0f, areaH = 1.0f; // area: a rectangle facing aim; traced as one, lit live from its centre
      float diffuse = 1.0f, specular = 1.0f;
      float falloff = 1.0f; // k of 1 / (1 + d^2 k); negative = inverse square
      float cutoff = 0.0f;  // 0 = none
      bool castShadow = true; // false: this light is never blocked
      float sunAngleDeg = 0.0f; // directional / sun: degrees across its disc (0 = a point); softens its highlight and settled shadow
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
      std::vector<TextureData> textures; // up to kMaxTextures; maps past that fall back to the plain material
      std::vector<InstanceBatch> batches;
      std::vector<Light> lights; // up to kMaxLights
      float viewProj[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }; // GL clip space, z in -1..1
      float eye[3] = { 0, 0, 5 };
      float exposure = 1.0f;
      float rim = 0.35f;
      int tonemap = 1; // 0 none, 1 ACES, 2 Reinhard, 3 AgX, 4 Neutral
      float ambientSrgb[3] = { 0.28f, 0.32f, 0.42f };
      float envSkySrgb[3] = { 0.35f, 0.5f, 0.75f }, envHorizonSrgb[3] = { 0.7f, 0.75f, 0.8f },
            envGroundSrgb[3] = { 0.2f, 0.2f, 0.22f };
      float envIntensity = 1.0f;
      float clearSrgb[3] = { 0.04f, 0.04f, 0.06f };
      // Equirectangular HDRI (Render 3D's Environment input): linear RGB floats, row 0 = the top. Replaces the gradient
      // sky everywhere the env is read; envRev changes when the pixels do. envBaseLod: the GL mip the pixels came from,
      // so roughness picks the same mip as Render 3D (lod = rough * envMaxLod, in GL's levels).
      const float* envPixels = nullptr;
      int envW = 0, envH = 0;
      unsigned long long envRev = 0;
      float envRotationRad = 0.0f, envMaxLod = 0.0f, envBaseLod = 0.0f;
      bool envBackground = false; // draw the HDRI behind the scene instead of clearSrgb
      Shadow shadow;
      bool rtShadows = false; // ray-traced hard shadows for every light (point and spot too); replaces the shadow map
      bool gi = false;            // one-bounce diffuse GI: cosine-sampled rays, 3x3 + temporal accumulation (needs the TLAS)
      int giRays = 1;
      int giDenoise = 4;          // a-trous passes over the GI (steps 1, 2, 4, 8); 0 = the 3x3 gather alone
      float giStrength = 1.0f;
      float giBlend = 0.15f;      // temporal blend toward the new frame when the camera is still
      unsigned frameIndex = 0;
      bool reuseShadowMask = false; // same camera, scene and lights as the last Render: keep its half-res RT shadow mask
      bool rtReflections = false; // one ray along the reflection vector for glossy surfaces (hit = flat shade, miss = env)
      float rayBias = 0.003f; // origin offset along the normal, scene units
      // Depth of field (settle's path tracer): a thin lens of this radius focused at focusDist along camFwd; bokeh =
      // blades (below 3 round), their turn in radians, tall/wide ratio, as Render 3D's lens. 0 radius = pinhole.
      float lensRadius = 0.0f, focusDist = 4.0f;
      // Row 5.3: one homogeneous medium in the box fogMin..fogMax (settle only): extinction per unit, scattered share, HG g.
      float fogDensity = 0.0f, fogAnisotropy = 0.0f;
      float fogColor[3] = { 0.9f, 0.9f, 0.9f }, fogMin[3] = { -1000, -1000, -1000 }, fogMax[3] = { 1000, 1000, 1000 };
      float dofAperture = 0.0f; // live: Render 3D's depth blur, a circle of dofAperture x 24 px across 1080 rows at |d - focus| / d
      float bokeh[3] = { 0.0f, 0.0f, 1.0f };
      float camRight[3] = { 1, 0, 0 }, camUp[3] = { 0, 1, 0 }, camFwd[3] = { 0, 0, -1 };
      // The MSL library for the instances' Field material programs (MaterialRegistry BuildMaterialLibrary), empty when
      // none; the backend rebuilds its pipelines when fieldMslKey changes. fieldTime is the programs' t.
      std::string fieldMsl;
      unsigned long long fieldMslKey = 0;
      float fieldTime = 0.0f;
   };

   constexpr int kMaxLights = 8;
}
