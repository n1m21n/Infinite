#pragma once

#include <cstdint>
#include <vector>

// Vibration modes of a triangle mesh: the lowest eigenpairs of the surface
// Laplacian, L phi = lambda M phi, with the cotangent stiffness matrix (Pinkall
// & Polthier 1993; Meyer et al. 2003) and a lumped (vertex-area) mass matrix.
// An open boundary is left free (Neumann, the natural boundary of the
// discrete form), like the edge of a free plate. Solved by shift-invert
// subspace iteration with Rayleigh-Ritz, preconditioned CG for the inner
// solves. Pure std, no mesh type: callable from a worker thread and from tests.
namespace MeshModalSolver
{
   constexpr int kMaxModes = 32;
   constexpr int kPickPoints = 64; // locations along the longest axis at which each mode is sampled

   struct Modes
   {
      int count = 0;
      // Eigenvalues lambda_i, ascending, in 1 / (mesh length unit)^2; the
      // zero mode(s) of rigid translation are not included.
      float lambda[kMaxModes] = {};
      // Mode shape sampled at kPickPoints vertices spread along the mesh's
      // longest bounding-box axis (rank order), scaled so each mode has
      // unit RMS over the surface. Sign is arbitrary but fixed per solve.
      float shape[kMaxModes][kPickPoints] = {};
      int weldedVertices = 0;
      int solverVertices = 0; // after the size cap
      bool valid = false;
   };

   // `positions`: 3 floats per vertex. `indices`: 3 per triangle.
   // `maxModes` is clamped to kMaxModes. `vertexCap` bounds the system size
   // (larger meshes are clustered down first).
   Modes Solve(const float* positions, int numVertices, const uint32_t* indices, int numIndices, int maxModes,
               int vertexCap = 3000);
}
