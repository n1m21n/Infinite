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
   const float* color;     // colorStride per point (rgb[a], sRGB), or null
   int colorStride;
   const float* radius;    // spheres: final radius per point
   const float* instColor; // instColorStride per instance (sRGB), or null
   int instColorStride;
   // Material, already linear.
   float3 base;
   float metal, rough, opacity, ior, trans;
   float3 emission;
   CUtexObject tex;        // surface colour (sRGB, normalized), 0 = none
};

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
};
