#include "HapDecode.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#if defined(_MSC_VER)
#include <execution>
#endif
#include <numeric>

namespace Hap
{
   namespace
   {
      std::uint32_t LE32(const std::uint8_t* p)
      {
         return (std::uint32_t)p[0] | ((std::uint32_t)p[1] << 8) |
                ((std::uint32_t)p[2] << 16) | ((std::uint32_t)p[3] << 24);
      }

      // Section header: 3-byte little-endian length + 1-byte type, or a zero
      // length followed by a 4-byte length for sections over 16 MB.
      bool ReadSection(const std::uint8_t* p, std::size_t avail,
                       std::size_t& headerLen, std::size_t& bodyLen, std::uint8_t& type)
      {
         if (avail < 4)
            return false;
         bodyLen = (std::size_t)p[0] | ((std::size_t)p[1] << 8) | ((std::size_t)p[2] << 16);
         type = p[3];
         headerLen = 4;
         if (bodyLen == 0)
         {
            if (avail < 8)
               return false;
            bodyLen = LE32(p + 4);
            headerLen = 8;
         }
         return headerLen + bodyLen <= avail;
      }

      TexFormat FormatFromNibble(std::uint8_t nibble)
      {
         switch (nibble)
         {
            case 0x0B: return TexFormat::BC1;
            case 0x0E: return TexFormat::BC3;
            case 0x0F: return TexFormat::BC3YCoCg;
            case 0x0C: return TexFormat::BC7;
            case 0x01: return TexFormat::BC4;
            default: return TexFormat::None;
         }
      }

      enum : std::uint8_t
      {
         kCompressorNone = 0xA0,
         kCompressorSnappy = 0xB0,
         kCompressorComplex = 0xC0,
         kChunkNone = 0x0A,
         kChunkSnappy = 0x0B,
         kSectionDecodeInstructions = 0x01,
         kSectionChunkCompressors = 0x02,
         kSectionChunkSizes = 0x03,
         kSectionChunkOffsets = 0x04,
         kSectionMultipleImages = 0x0D
      };

      struct Chunk
      {
         const std::uint8_t* src = nullptr;
         std::size_t srcLen = 0;
         std::size_t dstOffset = 0;
         std::size_t dstLen = 0;
         std::uint8_t compressor = kChunkNone;
      };

      bool DecodeChunked(const std::uint8_t* body, std::size_t bodyLen, std::vector<std::uint8_t>& out)
      {
         std::size_t hdr = 0, len = 0;
         std::uint8_t type = 0;
         if (!ReadSection(body, bodyLen, hdr, len, type) || type != kSectionDecodeInstructions)
            return false;
         const std::uint8_t* instr = body + hdr;
         const std::size_t instrLen = len;
         const std::uint8_t* data = body + hdr + len;
         const std::size_t dataLen = bodyLen - hdr - len;

         const std::uint8_t* compressors = nullptr;
         const std::uint8_t* sizes = nullptr;
         const std::uint8_t* offsets = nullptr;
         std::size_t chunkCount = 0;
         for (std::size_t pos = 0; pos < instrLen;)
         {
            if (!ReadSection(instr + pos, instrLen - pos, hdr, len, type))
               return false;
            const std::uint8_t* sec = instr + pos + hdr;
            if (type == kSectionChunkCompressors)
            {
               compressors = sec;
               chunkCount = len;
            }
            else if (type == kSectionChunkSizes)
            {
               sizes = sec;
               if (chunkCount == 0)
                  chunkCount = len / 4;
            }
            else if (type == kSectionChunkOffsets)
               offsets = sec;
            pos += hdr + len;
         }
         if (compressors == nullptr || sizes == nullptr || chunkCount == 0)
            return false;

         std::vector<Chunk> chunks(chunkCount);
         std::size_t srcRunning = 0;
         std::size_t dstRunning = 0;
         for (std::size_t i = 0; i < chunkCount; ++i)
         {
            Chunk& c = chunks[i];
            c.compressor = compressors[i];
            c.srcLen = LE32(sizes + i * 4);
            const std::size_t srcOff = offsets != nullptr ? LE32(offsets + i * 4) : srcRunning;
            srcRunning = srcOff + c.srcLen;
            if (srcOff + c.srcLen > dataLen)
               return false;
            c.src = data + srcOff;
            if (c.compressor == kChunkSnappy)
            {
               if (!SnappyUncompressedLength(c.src, c.srcLen, c.dstLen))
                  return false;
            }
            else if (c.compressor == kChunkNone)
               c.dstLen = c.srcLen;
            else
               return false;
            c.dstOffset = dstRunning;
            dstRunning += c.dstLen;
         }

         out.resize(dstRunning);
         std::atomic<bool> ok { true };
         auto work = [&](const Chunk& c)
         {
            if (c.compressor == kChunkNone)
               std::memcpy(out.data() + c.dstOffset, c.src, c.dstLen);
            else if (!SnappyUncompress(c.src, c.srcLen, out.data() + c.dstOffset, c.dstLen))
               ok.store(false, std::memory_order_relaxed);
         };
#if defined(_MSC_VER)
         if (chunks.size() > 1)
            std::for_each(std::execution::par, chunks.begin(), chunks.end(), work);
         else
#endif
            std::for_each(chunks.begin(), chunks.end(), work);
         return ok.load();
      }

      bool DecodeTextureSection(std::uint8_t type, const std::uint8_t* body, std::size_t bodyLen,
                                int width, int height, Image& img)
      {
         img.format = FormatFromNibble(type & 0x0F);
         if (img.format == TexFormat::None)
            return false;
         const std::uint8_t compressor = type & 0xF0;
         bool ok = false;
         if (compressor == kCompressorNone)
         {
            img.blocks.assign(body, body + bodyLen);
            ok = true;
         }
         else if (compressor == kCompressorSnappy)
         {
            std::size_t n = 0;
            if (SnappyUncompressedLength(body, bodyLen, n))
            {
               img.blocks.resize(n);
               ok = SnappyUncompress(body, bodyLen, img.blocks.data(), n);
            }
         }
         else if (compressor == kCompressorComplex)
            ok = DecodeChunked(body, bodyLen, img.blocks);
         return ok && img.blocks.size() >= ImageBytes(img.format, width, height);
      }
   }

   std::size_t BlockBytes(TexFormat f)
   {
      switch (f)
      {
         case TexFormat::BC1:
         case TexFormat::BC4: return 8;
         case TexFormat::BC3:
         case TexFormat::BC3YCoCg:
         case TexFormat::BC7: return 16;
         default: return 0;
      }
   }

   std::size_t ImageBytes(TexFormat f, int width, int height)
   {
      const std::size_t bw = (std::size_t)(width + 3) / 4;
      const std::size_t bh = (std::size_t)(height + 3) / 4;
      return bw * bh * BlockBytes(f);
   }

   int DecodeFrame(const std::uint8_t* data, std::size_t size, int width, int height, Image images[2])
   {
      std::size_t hdr = 0, len = 0;
      std::uint8_t type = 0;
      if (data == nullptr || width <= 0 || height <= 0 || !ReadSection(data, size, hdr, len, type))
         return 0;
      if (type != kSectionMultipleImages)
         return DecodeTextureSection(type, data + hdr, len, width, height, images[0]) ? 1 : 0;

      // Hap Q Alpha: a colour image and an alpha image, in either order.
      const std::uint8_t* body = data + hdr;
      int count = 0;
      Image tmp[2];
      for (std::size_t pos = 0; pos < len && count < 2;)
      {
         std::size_t h2 = 0, l2 = 0;
         std::uint8_t t2 = 0;
         if (!ReadSection(body + pos, len - pos, h2, l2, t2) ||
             !DecodeTextureSection(t2, body + pos + h2, l2, width, height, tmp[count]))
            return 0;
         ++count;
         pos += h2 + l2;
      }
      if (count != 2)
         return 0;
      const int alpha = tmp[0].format == TexFormat::BC4 ? 0 : 1;
      if (tmp[alpha].format != TexFormat::BC4 || tmp[1 - alpha].format != TexFormat::BC3YCoCg)
         return 0;
      images[0] = std::move(tmp[1 - alpha]);
      images[1] = std::move(tmp[alpha]);
      return 2;
   }

   bool SnappyUncompressedLength(const std::uint8_t* src, std::size_t n, std::size_t& outLen)
   {
      std::uint64_t v = 0;
      for (std::size_t i = 0, shift = 0; i < n && i < 10; ++i, shift += 7)
      {
         v |= (std::uint64_t)(src[i] & 0x7F) << shift;
         if ((src[i] & 0x80) == 0)
         {
            outLen = (std::size_t)v;
            return true;
         }
      }
      return false;
   }

   bool SnappyUncompress(const std::uint8_t* src, std::size_t n, std::uint8_t* dst, std::size_t dstLen)
   {
      std::size_t ip = 0;
      while (ip < n && (src[ip] & 0x80))
         ++ip;
      ++ip; // past the length varint
      std::size_t op = 0;
      while (ip < n)
      {
         const std::uint8_t tag = src[ip++];
         const unsigned kind = tag & 3u;
         if (kind == 0)
         {
            std::size_t litLen = (std::size_t)(tag >> 2);
            if (litLen >= 60)
            {
               const std::size_t extra = litLen - 59;
               if (ip + extra > n)
                  return false;
               litLen = 0;
               for (std::size_t b = 0; b < extra; ++b)
                  litLen |= (std::size_t)src[ip + b] << (8 * b);
               ip += extra;
            }
            litLen += 1;
            if (ip + litLen > n || op + litLen > dstLen)
               return false;
            std::memcpy(dst + op, src + ip, litLen);
            ip += litLen;
            op += litLen;
            continue;
         }

         std::size_t copyLen = 0;
         std::size_t offset = 0;
         if (kind == 1)
         {
            if (ip + 1 > n)
               return false;
            copyLen = 4 + ((tag >> 2) & 7u);
            offset = ((std::size_t)(tag >> 5) << 8) | src[ip];
            ip += 1;
         }
         else if (kind == 2)
         {
            if (ip + 2 > n)
               return false;
            copyLen = 1 + (tag >> 2);
            offset = (std::size_t)src[ip] | ((std::size_t)src[ip + 1] << 8);
            ip += 2;
         }
         else
         {
            if (ip + 4 > n)
               return false;
            copyLen = 1 + (tag >> 2);
            offset = LE32(src + ip);
            ip += 4;
         }
         if (offset == 0 || offset > op || op + copyLen > dstLen)
            return false;
         std::uint8_t* d = dst + op;
         const std::uint8_t* s = d - offset;
         if (offset >= copyLen)
            std::memcpy(d, s, copyLen);
         else
            for (std::size_t i = 0; i < copyLen; ++i) // overlapping run
               d[i] = s[i];
         op += copyLen;
      }
      return op == dstLen;
   }
}
