#pragma once

// Noise built-ins (OPUS_PLAN row 2.1, FINISH_PLAN 6.14): plain Field functions over a point, the same numbers on the
// CPU (FieldVM, element VM) and on the GPU (GLSL pixel and element kernels, MSL). The hash is PCG3D / PCG4D
// (Jarzynski and Olano 2020, "Hash Functions for GPU Rendering"): unsigned 32-bit integer only, so every backend
// agrees bit for bit on the lattice values; the float math after it agrees to rounding.
//
//   noise3(p)                       gradient noise, about -1..1, 0 at lattice points
//   noise4(p, w)                    the same in 4D (w: a fourth coordinate, e.g. t for evolving noise)
//   fbm(p, octaves, lacunarity, gain)  sum of noise3 octaves, normalised by the amplitude sum
//   voronoi3(p)                     vec3(F1, F2, cell id): distances to the nearest and second feature point,
//                                   and a 0..1 id of the nearest cell
//   wave(p)                         bands along x, period 1, 0..1
//   brick(p, mortar)                1 on a brick, 0 in the mortar; bricks 1 x 0.5 in xy, odd rows offset by half
//   checker(p)                      0 / 1 unit-cube checker
//   gradient(p)                     linear ramp clamp(p.x, 0, 1)
//   white(p)                        0..1 hash per unit cell of p
//
// p is always a vec3; every other argument a scalar.

#include <string>

namespace Field
{
   // True for the nine names above. arity: argument count; resultLanes: 1, or 3 for voronoi3.
   bool IsNoiseBuiltin(const std::string& name, int* arity = nullptr, int* resultLanes = nullptr);
   // Row 2.4: img(slot, uv) reads one of a material's four field images (raw values), imgc() the same decoded from
   // sRGB. Material domain only: the host shader owns the textures.
   inline bool IsImageBuiltin(const std::string& name) { return name == "img" || name == "imgc"; }
   // flat: p.x, p.y, p.z, then the scalar arguments. Writes resultLanes values to out; false for an unknown name.
   bool EvalNoiseBuiltin(const std::string& name, const double* flat, int count, double out[3]);
   // GLSL 150 helpers fld_noise3 ... fld_white (MSL gets them through NoiseMsl()).
   const char* NoiseGlsl();
   std::string NoiseMsl();
}
