#pragma once

#include <vector>

#include "Mesh.h"

// The 3D noise behind Displacement's Noise source. One definition in two
// languages: the C++ below displaces meshes and CPU point clouds, and kGlsl
// is the same maths for the GPU point pass. Both are written in float, with
// integer hashing, so a point lands in the same place on either path (they
// differ only by float rounding in sin/floor). Change one, change the other;
// GPUPOINTSTEST compares them.
//
// The noise is sampled at each vertex/point position times `frequency`, and
// evolves in place over time: two noise layers, one per whole step of
// `time * speed`, faded into each other.
namespace DisplaceNoise
{
   enum Type { kSmooth = 0, kFractal, kCurl, kRidged, kCells };
   enum Affects { kPosition = 0, kColour, kSize };

   struct Params
   {
      int type = kCurl;
      float frequency = 1.0f;
      float w = 0.0f; // time * speed, the evolution coordinate
      int octaves = 3;
      float roughness = 0.5f;
      unsigned int seed = 0;
   };

   // Roughly -1..1. Curl has no scalar form; it returns its x.
   float Scalar(const Params& p, float x, float y, float z);
   // Three decorrelated scalars, or the curl flow (divergence free, so
   // points swirl instead of bunching).
   void Vector(const Params& p, float x, float y, float z, float out[3]);

   // Scalar mode moves a vertex along its normal; Vector (and Curl, always)
   // by the vector. Seams stay closed as in MeshOps::Displace.
   Mesh DisplaceMesh(const Mesh& in, const Params& p, int mode, float strength, bool flat, bool flip,
                     const std::vector<unsigned char>* vertexMask = nullptr);
   // Points have no normal to push along: Scalar moves +Z (as the texture
   // source does). Colour scales Cd by 1 + n * strength (Vector adds a
   // tint); Size scales the point by max(0, 1 + n * strength).
   void DisplacePoints(std::vector<Particle>& points, const Params& p, int mode, float strength, int affects);

   // GLSL 150 functions: float DnScalar(vec3 P), vec3 DnVector(vec3 P) and
   // void DnApply(inout vec3 P, inout vec3 Cd, inout float W), reading the
   // uniforms uDnType, uDnFreq, uDnW, uDnOctaves, uDnRough, uDnSeed (uint),
   // uDnMode, uDnStrength, uDnAffects.
   extern const char* const kGlsl;
}
