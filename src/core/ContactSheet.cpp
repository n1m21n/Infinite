#include "ContactSheet.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ContactSheet
{
   namespace
   {
      constexpr int kGap = 8;
      constexpr int kGlyphW = 5, kGlyphH = 7, kGlyphScale = 3;
      const uint8_t kBackground[3] = { 24, 24, 24 };

      // 5x7 glyphs for the timestamp, one row per byte, bit 4 = left column.
      const uint8_t* Glyph(char c)
      {
         static const uint8_t digits[10][kGlyphH] = {
            { 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e },
            { 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e },
            { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f },
            { 0x1f, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0e },
            { 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 },
            { 0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e },
            { 0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e },
            { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
            { 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e },
            { 0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c },
         };
         static const uint8_t dot[kGlyphH] = { 0, 0, 0, 0, 0, 0x0c, 0x0c };
         static const uint8_t s[kGlyphH] = { 0, 0, 0x0f, 0x10, 0x0e, 0x01, 0x1e };
         static const uint8_t f[kGlyphH] = { 0x06, 0x09, 0x08, 0x1c, 0x08, 0x08, 0x08 };
         static const uint8_t blank[kGlyphH] = { 0, 0, 0, 0, 0, 0, 0 };
         if (c >= '0' && c <= '9')
            return digits[c - '0'];
         if (c == '.')
            return dot;
         if (c == 's')
            return s;
         if (c == 'f')
            return f;
         return blank;
      }

      uint32_t Crc32(const uint8_t* data, size_t size)
      {
         uint32_t crc = 0xffffffffu;
         for (size_t i = 0; i < size; i++)
         {
            crc ^= data[i];
            for (int b = 0; b < 8; b++)
               crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
         }
         return ~crc;
      }
   }

   void Builder::Add(const uint8_t* rgba, int width, int height, bool bottomUp, const std::string& label)
   {
      if (rgba == nullptr || width <= 0 || height <= 0)
         return;
      Cell cell;
      cell.label = label;
      cell.height = std::max(1, (int)std::lround((double)kCellWidth * (double)height / (double)width));
      cell.rgb.resize((size_t)kCellWidth * (size_t)cell.height * 3);
      // Box filter: each cell pixel is the mean of the source pixels it
      // covers (at least one, so a frame narrower than the cell is repeated
      // rather than left with holes).
      for (int y = 0; y < cell.height; y++)
      {
         const int sy0 = (int)((long long)y * height / cell.height);
         const int sy1 = std::max(sy0 + 1, (int)((long long)(y + 1) * height / cell.height));
         for (int x = 0; x < kCellWidth; x++)
         {
            const int sx0 = (int)((long long)x * width / kCellWidth);
            const int sx1 = std::max(sx0 + 1, (int)((long long)(x + 1) * width / kCellWidth));
            uint32_t sum[3] = { 0, 0, 0 };
            uint32_t n = 0;
            for (int sy = sy0; sy < sy1 && sy < height; sy++)
            {
               const int row = bottomUp ? height - 1 - sy : sy;
               const uint8_t* src = rgba + ((size_t)row * (size_t)width + (size_t)sx0) * 4;
               for (int sx = sx0; sx < sx1 && sx < width; sx++, src += 4, n++)
               {
                  sum[0] += src[0];
                  sum[1] += src[1];
                  sum[2] += src[2];
               }
            }
            uint8_t* dst = cell.rgb.data() + ((size_t)y * kCellWidth + (size_t)x) * 3;
            for (int c = 0; c < 3; c++)
               dst[c] = n > 0 ? (uint8_t)((sum[c] + n / 2) / n) : 0;
         }
      }
      mCells.push_back(std::move(cell));
   }

   Image Builder::Build() const
   {
      Image out;
      if (mCells.empty())
         return out;
      const int count = (int)mCells.size();
      const int cols = (int)std::ceil(std::sqrt((double)count));
      const int rows = (count + cols - 1) / cols;
      int cellH = 0;
      for (const Cell& c : mCells)
         cellH = std::max(cellH, c.height);

      out.width = cols * kCellWidth + (cols + 1) * kGap;
      out.height = rows * cellH + (rows + 1) * kGap;
      out.rgba.resize((size_t)out.width * (size_t)out.height * 4);
      for (size_t i = 0; i < out.rgba.size(); i += 4)
      {
         out.rgba[i + 0] = kBackground[0];
         out.rgba[i + 1] = kBackground[1];
         out.rgba[i + 2] = kBackground[2];
         out.rgba[i + 3] = 255;
      }
      auto put = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b)
      {
         if (x < 0 || y < 0 || x >= out.width || y >= out.height)
            return;
         uint8_t* p = out.rgba.data() + ((size_t)y * (size_t)out.width + (size_t)x) * 4;
         p[0] = r;
         p[1] = g;
         p[2] = b;
      };

      for (int i = 0; i < count; i++)
      {
         const Cell& cell = mCells[(size_t)i];
         const int ox = kGap + (i % cols) * (kCellWidth + kGap);
         const int oy = kGap + (i / cols) * (cellH + kGap);
         for (int y = 0; y < cell.height; y++)
            for (int x = 0; x < kCellWidth; x++)
            {
               const uint8_t* s = cell.rgb.data() + ((size_t)y * kCellWidth + (size_t)x) * 3;
               put(ox + x, oy + y, s[0], s[1], s[2]);
            }

         // Timestamp on a solid plate in the cell's corner, so it reads on
         // any picture.
         const int pad = 4;
         const int advance = (kGlyphW + 1) * kGlyphScale;
         const int plateW = (int)cell.label.size() * advance + pad * 2;
         const int plateH = kGlyphH * kGlyphScale + pad * 2;
         for (int y = 0; y < plateH && y < cell.height; y++)
            for (int x = 0; x < plateW && x < kCellWidth; x++)
               put(ox + x, oy + y, 0, 0, 0);
         for (size_t ci = 0; ci < cell.label.size(); ci++)
         {
            const uint8_t* g = Glyph(cell.label[ci]);
            for (int gy = 0; gy < kGlyphH; gy++)
               for (int gx = 0; gx < kGlyphW; gx++)
               {
                  if (!(g[gy] & (0x10 >> gx)))
                     continue;
                  for (int sy = 0; sy < kGlyphScale; sy++)
                     for (int sx = 0; sx < kGlyphScale; sx++)
                     {
                        const int x = pad + (int)ci * advance + gx * kGlyphScale + sx;
                        const int y = pad + gy * kGlyphScale + sy;
                        if (x < kCellWidth && y < cell.height)
                           put(ox + x, oy + y, 255, 255, 255);
                     }
               }
         }
      }
      return out;
   }

   std::vector<uint8_t> TagSrgb(const std::vector<uint8_t>& png)
   {
      static const uint8_t kSignature[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
      // Signature, then IHDR: 4 length + 4 type + 13 data + 4 crc.
      const size_t headerEnd = 8 + 25;
      if (png.size() < headerEnd || std::memcmp(png.data(), kSignature, 8) != 0 ||
          std::memcmp(png.data() + 12, "IHDR", 4) != 0)
         return png;
      for (size_t i = headerEnd; i + 8 <= png.size() && i < headerEnd + 64; i++)
         if (std::memcmp(png.data() + i, "sRGB", 4) == 0)
            return png;

      // One data byte: rendering intent 0 (perceptual).
      uint8_t chunk[13] = { 0, 0, 0, 1, 's', 'R', 'G', 'B', 0, 0, 0, 0, 0 };
      const uint32_t crc = Crc32(chunk + 4, 5);
      chunk[9] = (uint8_t)(crc >> 24);
      chunk[10] = (uint8_t)(crc >> 16);
      chunk[11] = (uint8_t)(crc >> 8);
      chunk[12] = (uint8_t)crc;

      std::vector<uint8_t> out;
      out.reserve(png.size() + sizeof(chunk));
      out.insert(out.end(), png.begin(), png.begin() + (std::ptrdiff_t)headerEnd);
      out.insert(out.end(), chunk, chunk + sizeof(chunk));
      out.insert(out.end(), png.begin() + (std::ptrdiff_t)headerEnd, png.end());
      return out;
   }
}
