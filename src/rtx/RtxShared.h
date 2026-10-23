#pragma once

// Turbo 0.62: data shared by the RTX module's host code (RtxOptix.cpp) and its
// OptiX device programs (RtxDevice.cu). Plain structs only; CUDA vector types
// come from the toolkit headers on both sides.

#include <cuda.h>
#include <optix_types.h>
#include <vector_types.h>

#define RTX_MAX_LIGHTS 8

// One traceable thing (a hit group record per geometry).
struct RtxGeomData
{
   int kind;               // 0 triangles, 1 spheres
   const float* pos;       // posStride floats per point (object space)
   int posStride;
   const unsigned int* idx; // 3 per triangle (triangles only)
   const float* normal;    // normalStride per point, or null
   int normalStride;
   const float* uv;        // uvStride per point, or null
   int uvStride;
   const float* color;     // colorStride per point (rgb[a]), or null
   int colorStride;
   const float* radius;    // spheres: final radius per point
   // Turbo 0.70: both colours multiply the linear albedo as they are - what
   // the raster does (vVertexColor, vInstanceColor).
   const float* instColor; // instColorStride per instance, or null
   int instColorStride;
   // Material, already linear.
   float3 base;
   float metal, rough, opacity, ior, trans;
   float3 emission;
   CUtexObject tex;        // surface colour (sRGB, normalized), 0 = none
   // Turbo 0.70: Instance PointZ's per-copy textures, as the raster draws
   // them (instanceLook): mode 0 off, 1 pick instTex[idx % count], 2 atlas
   // cell idx of instTex[0]; idx from the TexIndex attribute (from 0), the
   // copy's number (1) or a hash of it (2). Replaces `tex` when on.
   CUtexObject instTex[4];
   int instTexMode, instTexFrom, instTexCount, atlasCols, atlasRows;
   const float* instTexIdx; // instTexIdxStride per instance, or null
   int instTexIdxStride;
   // Turbo 0.70 (X1): PartId per copy (instanced) or per sphere, or null - the
   // motion vectors find a particle's last-frame place by it (RtxParams::prevRing)
   // when births and deaths reorder the list; ringKey tells geometries apart.
   const float* partId;
   int partIdStride;
   unsigned int ringKey;
};

// Turbo 0.70 (X1): one particle's place last frame, by PartId: an instance's
// object-to-world transform (row-major 3x4), or a sphere's world centre in
// t[3], t[7], t[11]. id = PartId + 1 (0 = empty).
struct RtxPrevSlot
{
   float t[12];
   unsigned int id;
   unsigned int key;
   unsigned int pad[2];
};

__host__ __device__ inline unsigned int RtxRingSlot(unsigned int id, unsigned int key, unsigned int mask)
{
   return ((id * 2654435761u) ^ (key * 40503u + 0x9e3779b9u)) & mask;
}

struct RtxLight
{
   int type;
   float3 dir;   // direction to the light, or position
   float3 color; // linear, times intensity
   float spotCos0, spotCos1;
};

struct RtxParams
{
   // Output (width * height float4 each).
   float4* accum;   // running sum (rgb) and coverage (a)
   float4* average; // accum / samples, what the denoiser reads
   float4* albedo;  // first-hit albedo, averaged
   float4* normal;  // first-hit normal (camera space), averaged
   int width, height;
   int samples;     // already in accum
   int spp;
   unsigned int seed;
   int bounces;

   // Camera.
   float invViewProj[16]; // column-major
   float invViewRot[9];   // camera space -> world, column-major
   float viewRot[9];      // world -> camera
   float3 eye;
   int panorama;
   float domeAngle, domeTilt;

   OptixTraversableHandle scene;

   RtxLight lights[RTX_MAX_LIGHTS];
   int lightCount;
   float3 ambient; // linear, from ambient lights (added to diffuse)

   CUtexObject envTex; // equirect HDRI (linear float), 0 = gradient
   float envRotation, envIntensity;
   float3 sky, horizon, ground; // linear

   // Turbo 0.70 (X1): what the temporal accumulation of a moving scene needs,
   // from each pixel's first sample: where its first hit was last frame (in
   // pixels, x < 0 = nowhere) and that hit's world normal + distance (w < 0 =
   // background). The previous frame's instances (same order) give a moving
   // copy its own motion; 0 = only the camera's.
   float2* motion;
   float4* gbuf;
   const OptixInstance* prevInst;
   unsigned int prevInstCount;
   float prevViewProj[16];
   int hasPrev;
   const RtxPrevSlot* prevRing; // by PartId (null = none), ringMask + 1 slots
   unsigned int ringMask;

   // Turbo 0.70: the raster's look on the same scene. Its image-based diffuse
   // is the environment times ambient * 3 (Render 3D's ambient colour), here
   // the environment reached by a diffuse bounce; its rim adds
   // base * (1 - n.v)^3 * rim at the first hit.
   float3 ambientTint; // linear
   float rim;
};

// Payload carried by radiance rays (pointer split over two registers).
struct RtxHit
{
   int hit;
   float t;
   float3 n;      // world shading normal
   float3 ng;     // world geometric normal
   float3 base;   // linear albedo after vertex / instance colour and texture
   float metal, rough, opacity, ior, trans;
   float3 emission;
   float3 prevP;  // Turbo 0.70 (X1): the hit point where it was last frame (world)
};
