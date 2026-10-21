#pragma once

#include <cstdint>
#include <vector>

#include "Geometry3DNodes.h"

// Turbo 0.61: Render 3D's "path traced" mode - a progressive GPU path tracer
// over the same meshes, materials, lights and environment the raster path
// draws. The scene's triangles go into a BVH (binned SAH, built on the CPU when
// the geometry moves) stored in texture buffers; a fullscreen pass traces one
// sample per pixel per frame (GGX + Lambert, next-event estimation for the
// lights, transmission for glass, the HDRI or sky on a miss) and averages it
// into an RGBA32F target, restarting whenever anything in the scene changes.
// The average goes through Render 3D's normal HDR finish (Render3DPost).
//
// Not covered (raster only): point sprites, PointZ GPU-only sources, lines,
// sheen / clearcoat / iridescence (approximated by the base lobes), textures
// other than the surface (colour) texture.
class Render3DPathTracer
{
public:
   struct Source
   {
      const Mesh* mesh = nullptr;
      Mat4 model;                  // used when instances is empty
      std::vector<Mat4> instances; // world matrices, one copy each
      Material material;
      unsigned int surfaceTex = 0; // 0 = none
   };

   struct Light
   {
      int type = 0; // Render 3D's: 0 / 2 directional, 1 point, 3 ambient, 4 spot
      float dir[3] = { 0.0f, 1.0f, 0.0f }; // direction to the light, or position
      float color[3] = { 1.0f, 1.0f, 1.0f }; // sRGB, as the raster path
      float intensity = 1.0f;
      float spotAngle = 30.0f;
   };

   struct Env
   {
      unsigned int tex = 0; // equirect HDRI, 0 = gradient sky
      float rotation = 0.0f;
      float intensity = 1.0f;
      float sky[3] = {}, horizon[3] = {}, ground[3] = {}; // sRGB
      float ambient[3] = {};                               // sRGB
   };

   ~Render3DPathTracer();

   // Rebuilds the BVH when geometryKey changes. Returns false with nothing to trace.
   bool SetScene(const std::vector<Source>& sources, uint64_t geometryKey);
   // Restarts the average (camera, light, material or parameter change).
   void Restart() { mSamples = 0; }
   // Traces `spp` more samples per pixel (up to maxSamples) and returns the
   // averaged HDR texture (w x h), or 0 on failure.
   unsigned int Trace(int w, int h, const Mat4& viewProj, const float eye[3], const std::vector<Light>& lights,
                      const Env& env, float exposure, int bounces, int maxSamples, int spp, int panorama,
                      const Mat4& view);
   int Samples() const { return mSamples; }
   int Triangles() const { return mTriCount; }

private:
   bool EnsureProgram();
   void EnsureTargets(int w, int h);
   void ReleaseScene();

   unsigned int mProgram = 0;
   bool mProgramFailed = false;

   // Texture buffers: BVH nodes, triangle positions, normals + uv, materials.
   unsigned int mBuf[4] = {}, mTbo[4] = {};
   unsigned int mTexArray = 0; // surface textures, one layer each (1024 x 1024)
   int mTexLayers = 0;
   uint64_t mGeometryKey = ~0ull;
   int mTriCount = 0;

   unsigned int mAccum[2] = {}, mAccumFbo[2] = {};
   int mFront = 0;
   int mWidth = 0, mHeight = 0;
   int mSamples = 0;
   unsigned int mFrameSeed = 1;
};
