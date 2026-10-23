#pragma once

#include <string>
#include <vector>

#include "Mesh.h"

// Reads a .ply that is a point cloud (a scan, a photogrammetry dump, a
// Gaussian-splat export) as points rather than as a mesh. Plain C++ on the
// file's bytes, so it reads the same on every platform: the OS model loaders
// (ModelIO, the portable importer) only ever return triangles, and a PLY with
// no faces has none.
//
// Reads ascii, binary_little_endian and binary_big_endian bodies. Positions
// come from x/y/z; colour from red/green/blue (integer types are read as
// 0..max of their width, floats as 0..1), or from a splat's f_dc_0..2 (the
// zero-order spherical harmonic, 0.5 + 0.2821 * f_dc). Normals nx/ny/nz are
// kept as the particle's orientation hint. Every other property is skipped.
namespace PlyPoints
{
   enum class Kind
   {
      kPoints, // a vertex element and no faces: `points` is filled
      kMesh,   // the file has faces; read it as a mesh instead
      kError,  // not a PLY, truncated, or a layout this reader skips
   };

   struct Cloud
   {
      std::vector<Particle> points; // scale 1, alive; hasColor when the file had colour
      bool hasColor = false;
      bool hasNormals = false;
   };

   Kind Parse(const unsigned char* data, size_t size, Cloud& out, std::string& error);
   Kind Read(const std::string& path, Cloud& out, std::string& error);
}
