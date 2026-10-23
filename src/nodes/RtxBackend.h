#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Geometry3DNodes.h"

// Turbo 0.62: Render 3D's RTX mode - hardware ray tracing (NVIDIA OptiX on the
// GPU's RT cores) behind this interface, so the tracer lives in an optional
// module (src/rtx, INFINITE_ENABLE_RTX, not in the MIT build). Render 3D
// gathers the scene as GPU handles (GL buffers of PointZ data and instance
// matrices, CPU meshes by pointer) and never downloads per-frame data; the
// backend copies what it needs GPU to GPU, builds one acceleration structure
// per geometry (rebuilt only when its key changes) plus one over the
// instances every frame, traces 1+ samples per pixel, denoises and returns a
// GL texture Render 3D finishes like the raster image.
class RtxBackend
{
public:
   struct Geometry
   {
      enum Kind { kMesh = 0, kSpheres = 1 };
      Kind kind = kMesh;
      // Identity + revision of the vertex data: the backend rebuilds this
      // geometry's acceleration structure only when it changes.
      uint64_t key = 0;
      // Which source this is, across frames: animated geometry with the same
      // point and primitive counts is refitted in place instead of rebuilt.
      uint64_t slot = 0;

      // Triangles, either a CPU mesh ...
      const Mesh* mesh = nullptr;
      // ... or GL buffers (PointZ): tightly packed floats, `posComps` per
      // point; uint32 index buffer of triCount * 3 indices.
      unsigned int posBuf = 0, normalBuf = 0, uvBuf = 0, colorBuf = 0, indexBuf = 0;
      int posComps = 3, normalComps = 3, uvComps = 2, colorComps = 4;
      int pointCount = 0, triCount = 0;

      // Spheres: centres in posBuf (or cpuCentres), radius = radiusScale *
      // (radiusBuf value, or 1 without one), colour from colorBuf / cpuColors.
      unsigned int radiusBuf = 0;
      float radiusScale = 1.0f;
      std::vector<float> cpuCentres; // xyzr per sphere, world space (legacy clouds)
      std::vector<float> cpuColors;  // rgb per sphere

      // Placement: one model matrix, or instances - GL column buffers
      // (pop::InstanceMatrices, vec4 each, model already applied) or CPU
      // matrices - with optional per-instance colour.
      Mat4 model;
      unsigned int instCols[4] = { 0, 0, 0, 0 };
      int instCount = 0;
      std::vector<Mat4> instCpu;
      unsigned int instColorBuf = 0;
      int instColorComps = 4;

      Material material;
      unsigned int surfaceTex = 0;
      // Turbo 0.70: Instance PointZ's per-copy textures (PopData::InstanceLook,
      // as the raster draws them): mode 0 off, 1 pick by index, 2 atlas grid;
      // index from the TexIndex attribute (instTexIdxBuf), the copy's number
      // or a hash of it. Replaces surfaceTex when on.
      int instTexMode = 0, instTexFrom = 0, instTexCount = 0, atlasCols = 1, atlasRows = 1;
      unsigned int instTex[4] = { 0, 0, 0, 0 };
      unsigned int instTexIdxBuf = 0;
      int instTexIdxComps = 1;
      // Turbo 0.70 (X1): PartId per copy (instanced) or per sphere, 0 = none -
      // the motion vectors follow a particle by it through births and deaths.
      unsigned int partIdBuf = 0;
      int partIdComps = 1;
   };

   struct Light
   {
      int type = 0; // Render 3D's: 0 / 2 directional, 1 point, 3 ambient, 4 spot
      float dir[3] = { 0.0f, 1.0f, 0.0f }; // direction to the light, or position
      float color[3] = { 1.0f, 1.0f, 1.0f }; // sRGB
      float intensity = 1.0f;
      float spotAngle = 30.0f;
   };

   struct Env
   {
      unsigned int tex = 0; // equirect HDRI (GL), 0 = gradient sky
      int texW = 0, texH = 0;
      uint64_t texKey = 0;  // changes when the HDRI's pixels do
      float rotation = 0.0f;
      float intensity = 1.0f;
      float sky[3] = {}, horizon[3] = {}, ground[3] = {}; // sRGB
      float ambient[3] = {};                               // sRGB
      // Turbo 0.70: Render 3D's rim term, drawn the raster's way (parity).
      float rim = 0.0f;
   };

   struct Frame
   {
      int width = 0, height = 0;
      Mat4 view, viewProj;
      float eye[3] = { 0, 0, 0 };
      int panorama = 0; // Render 3D's: 0 off, 1 equirect, 2 cubemap 3x2, 3 dome
      float domeAngle = 180.0f, domeTilt = 0.0f;
      int bounces = 4;
      int spp = 1;            // samples per pixel this frame
      // Turbo 0.70: > 0 picks the samples per frame instead (1..32) so the
      // trace takes about this long, from the last frames' time per sample. A
      // moving scene restarts every frame: one sample left the denoiser too
      // little to work with while the trace cost well under a millisecond.
      float traceBudgetMs = 0.0f;
      int maxSamples = 256;   // accumulation stops here while nothing changes
      bool restart = false;   // something moved: drop the accumulation
      int denoise = 0; // 0 auto (fast while moving, AI when still), 1 AI, 2 fast, 3 off
      float exposure = 1.0f;
      // Turbo 0.70: how long the shutter stays open, in frames (0 = no motion
      // blur): each pixel is blurred along its own motion since last frame.
      float motionBlur = 0.0f;
      // Turbo 0.70: the AI denoiser also doubles the size (OptiX's 2x model);
      // OutputScale() says whether it did.
      bool upscale2x = false;
   };

   virtual ~RtxBackend() {}
   // Replaces the scene (cheap when the keys match the last call's).
   virtual void SetScene(const std::vector<Geometry>& geometry, const std::vector<Light>& lights, const Env& env) = 0;
   // Traces and returns an RGBA32F GL texture (alpha = coverage), or 0.
   virtual unsigned int Render(const Frame& frame) = 0;
   virtual int Samples() const = 0;
   // Turbo 0.70: the last Render's texture is this many times the frame's size.
   virtual int OutputScale() const { return 1; }
   virtual double LastMs() const = 0;
   virtual const std::string& Error() const = 0;
};

// The OptiX backend, or nullptr with the reason in `why` (module not built,
// no NVIDIA RTX GPU / driver, initialisation failed).
std::unique_ptr<RtxBackend> CreateRtxBackend(std::string& why);
// True when this build has the RTX module at all (the mode is listed).
bool RtxModuleBuilt();
