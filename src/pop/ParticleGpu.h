#pragma once

// Turbo 0.70 (P3): the GPU engine of Particle PointZ ("engine" = GPU). The
// particles live in GPU buffers from birth to death: integration, ageing,
// size / colour over life run as kernels, the dead are removed by GPU stream
// compaction (GpuAlgo), births gather their source rows on the GPU. The CPU only
// draws the random numbers (same generator and order as the CPU engine, so a
// seed gives the same births) and counts the living by birth cohort with the
// same float operations the kernels use - no readback, no per-frame upload of
// the particles.

namespace pop
{
   struct CookCtx;

   // False when compute shaders are unavailable: the caller runs the CPU engine.
   bool CookParticleGpu(CookCtx& c);
}
