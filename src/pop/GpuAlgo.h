#pragma once

// Turbo 0.70 (P2): reusable GPU primitives for PointZ, as GL 4.3 compute passes
// over buffer objects (the same buffers PopData holds). Nothing here reads back
// to the CPU: counts stay on the GPU (a uint in a buffer) and later passes use
// them through indirect dispatch, so a chain of operators never waits (R3).
// Equivalence with plain CPU loops: the GPUALGOTEST self-test.
//
// Buffers are GL buffer names; sizes are in elements. All functions return
// false when compute shaders are unavailable (the caller keeps its CPU path).

#include <cstdint>
#include <memory>
#include <vector>

#include "PopData.h"

namespace pop
{
   namespace gpu
   {
      // True when the compute programs built (GL 4.3).
      bool Available();
      // Tests only: Available() answers false while on, so an operator cooks
      // its CPU path (reading a GPU-made input back) as the reference.
      void ForceCpuForTest(bool on);

      // flags[i] = src[i] > 0.5 ? 1 : 0 over `count` floats (a 0 / 1 attribute
      // a kernel wrote, e.g. "keep this point") into uints for CompactIndices.
      bool FloatToFlags(unsigned int src, int count, unsigned int flags);

      // out[i] = in[0] + ... + in[i-1] over `count` uints (out[0] = 0).
      // `total` (optional, 0 = none) receives the sum of all `count` values in
      // its first uint. `in` and `out` must be different buffers.
      bool ExclusiveScanU32(unsigned int in, unsigned int out, int count, unsigned int total = 0);

      // For every i < count with flags[i] != 0 (uints), writes i to
      // indices[k] in order (k = how many flagged before i); the number kept
      // goes to the first uint of `countBuf`. `indices` holds `count` uints.
      bool CompactIndices(unsigned int flags, int count, unsigned int indices, unsigned int countBuf);

      // dst row k = src row indices[k] (rows of `comps` floats), for
      // k < countBuf[0] (read on the GPU; maxCount bounds it). Rows of dst past
      // the count are left as they were.
      bool GatherRows(unsigned int src, int comps, unsigned int indices, unsigned int countBuf, int maxCount,
                      unsigned int dst);

      // dst row r = sum over k in [offsets[r], offsets[r+1]) of weights[k] *
      // src row indices[k] (rows of `comps` floats), summed in that order with
      // no fused multiply-add - the CPU PointBuilder::Weighted result, bit for
      // bit. A row with one contributor of weight 1 is a copy (PointBuilder::
      // Copy); with `normalizeN`, a 3-component row of more contributors is
      // normalised (what Weighted does to N). offsets: rows + 1 uints.
      bool WeightedRows(unsigned int src, int comps, unsigned int offsets, unsigned int indices, unsigned int weights,
                        int rows, unsigned int dst, bool normalizeN);

      // A fixed recipe of output rows from input rows, written like PointBuilder
      // (Copy / Lerp / Weighted, same order, same weights) and applied on the GPU
      // by WeightedRows: the same numbers as the PointBuilder it replaces. Built
      // once per input topology and reused while the points move.
      struct RowStencil
      {
         std::vector<uint32_t> offs = { 0 }, idx;
         std::vector<float> w;
         uint32_t Copy(uint32_t i);
         uint32_t Lerp(uint32_t i, uint32_t j, float t);
         uint32_t Weighted(const int* rows, const float* weights, int n); // n >= 2
         int Rows() const { return (int)offs.size() - 1; }
      };
      struct GpuStencil
      {
         BufferPtr offs, idx, w; // uint buffers are uploaded bit for bit through float buffers
         int rows = 0;
      };
      GpuStencil Upload(const RowStencil& s);
      // Every attribute of `in` through the stencil (N renormalised like
      // PointBuilder::Weighted); no topology. Null when a pass fails.
      std::shared_ptr<PopData> ApplyStencil(const PopData& in, const GpuStencil& s);

      // Every part's rows in order, for every attribute any part has, as the
      // CPU Concat: P first, then the order the attributes are met; a part
      // without the attribute gets its default. GPU copies only, no topology
      // (the caller sets it). When an attribute has different component counts
      // in two parts: null, or with `mismatchAsMissing` that part gets the
      // default instead.
      std::shared_ptr<PopData> ConcatRows(const std::vector<const PopData*>& parts, bool mismatchAsMissing = false);

      // Turbo 0.70 (P6): Line Resample by count. The layout comes from the
      // topology (built once per topology and count): line s is
      // pts[start[s] .. start[s] + len[s]) (a closed line repeats its first
      // point), output row r is number rowJ[r] of line rowStrip[r], spread over
      // den[s] = count - 1 steps. ResampleStencil sums each line's lengths from
      // P on the GPU in the CPU's order, finds each row's segment and lerp
      // factor and returns the 2-row stencil for ApplyStencil - no readback.
      // The GPU's sqrt and divide make it match the CPU to ~1e-6, not bit for bit.
      struct ResampleLayout
      {
         BufferPtr pts, start, len, den, rowStrip, rowJ, offs;
         int strips = 0, rows = 0, ptsCount = 0;
      };
      BufferPtr UintBuffer(const std::vector<uint32_t>& values);
      bool ResampleStencil(const PopData& in, const ResampleLayout& layout, GpuStencil& out);
   }
}
