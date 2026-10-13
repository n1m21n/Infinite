#pragma once

// Native HAP frame unpacker (Vidvox HAP spec: github.com/Vidvox/hap).
// Turns one HAP frame (the packet payload of a Hap1/Hap5/HapY/HapM/HapA/Hap7
// QuickTime track) into the GPU-compressed texture blocks it carries, ready
// for glCompressedTexSubImage2D. No colour work happens here: BC1/BC3/BC7
// are sampled by the GPU as-is and HAP Q's scaled YCoCg is decoded in a
// shader (core/VideoTexture.cpp). Includes a self-contained Snappy
// decompressor; chunked frames are decompressed in parallel.
//
// Portable C++17, no platform or FFmpeg dependency.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Hap
{
   enum class TexFormat : unsigned char
   {
      None = 0,
      BC1,       // RGB DXT1 (Hap)
      BC3,       // RGBA DXT5 (Hap Alpha)
      BC3YCoCg,  // scaled YCoCg DXT5 (Hap Q)
      BC7,       // RGBA BPTC (Hap R)
      BC4        // alpha RGTC1 (Hap Alpha-Only, second image of Hap Q Alpha)
   };

   struct Image
   {
      TexFormat format = TexFormat::None;
      std::vector<std::uint8_t> blocks;
   };

   // Bytes of one 4x4 block for a format (8 or 16), 0 for None.
   std::size_t BlockBytes(TexFormat f);
   // Total block bytes for a width x height picture (padded up to 4x4).
   std::size_t ImageBytes(TexFormat f, int width, int height);

   // Decodes a frame of the given picture size. Returns the number of images
   // written (1, or 2 for Hap Q Alpha: images[0] colour, images[1] alpha),
   // 0 when the frame is malformed or uses a variant this decoder lacks.
   int DecodeFrame(const std::uint8_t* data, std::size_t size, int width, int height,
                   Image images[2]);

   // Raw Snappy block format (no framing). Exposed for tests.
   bool SnappyUncompressedLength(const std::uint8_t* src, std::size_t n, std::size_t& outLen);
   bool SnappyUncompress(const std::uint8_t* src, std::size_t n, std::uint8_t* dst, std::size_t dstLen);
}
