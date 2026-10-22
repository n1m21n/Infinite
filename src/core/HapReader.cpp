#include "HapReader.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

#if defined(__APPLE__) || defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace Hap
{
   namespace
   {
      constexpr uint32_t Fourcc(char a, char b, char c, char d)
      {
         return ((uint32_t)(uint8_t)a << 24) | ((uint32_t)(uint8_t)b << 16) | ((uint32_t)(uint8_t)c << 8) |
                (uint32_t)(uint8_t)d;
      }

      uint32_t Be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
      uint64_t Be64(const uint8_t* p) { return ((uint64_t)Be32(p) << 32) | Be32(p + 4); }
      uint32_t Be16(const uint8_t* p) { return ((uint32_t)p[0] << 8) | p[1]; }
      uint32_t Le32(const uint8_t* p) { return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0]; }

      struct Atom
      {
         uint32_t type = 0;
         size_t body = 0; // payload start within the buffer
         size_t end = 0;  // one past the payload
      };

      // Next child atom of [pos, end) in `buf`; false when none is left or the
      // next one is malformed.
      bool NextAtom(const std::vector<uint8_t>& buf, size_t& pos, size_t end, Atom& a)
      {
         if (pos + 8 > end)
            return false;
         uint64_t size = Be32(&buf[pos]);
         a.type = Be32(&buf[pos + 4]);
         size_t hdr = 8;
         if (size == 1)
         {
            if (pos + 16 > end)
               return false;
            size = Be64(&buf[pos + 8]);
            hdr = 16;
         }
         else if (size == 0)
         {
            size = end - pos;
         }
         // `size` can be 64-bit: compare against what is left, so pos + size
         // cannot wrap and send pos backwards (an endless loop on open).
         if (size < hdr || size > end - pos)
            return false;
         a.body = pos + hdr;
         a.end = pos + (size_t)size;
         pos = a.end;
         return true;
      }

      bool FindChild(const std::vector<uint8_t>& buf, const Atom& parent, uint32_t type, Atom& out)
      {
         size_t pos = parent.body;
         Atom a;
         while (NextAtom(buf, pos, parent.end, a))
         {
            if (a.type == type)
            {
               out = a;
               return true;
            }
         }
         return false;
      }

      struct Parsed
      {
         int width = 0;
         int height = 0;
         double duration = 0.0;
         Format format = Format::None;
         struct S
         {
            uint64_t offset;
            uint32_t size;
            double time;
         };
         std::vector<S> samples;
      };

      // Fills `p` from one video trak. False when it is not a supported HAP
      // track (wrong handler, codec, or a malformed sample table).
      bool ParseTrak(const std::vector<uint8_t>& b, const Atom& trak, Parsed& p)
      {
         Atom mdia, hdlr, mdhd, minf, stbl, stsd, stts, stsz, stsc, stco;
         if (!FindChild(b, trak, Fourcc('m', 'd', 'i', 'a'), mdia) || !FindChild(b, mdia, Fourcc('h', 'd', 'l', 'r'), hdlr) ||
             !FindChild(b, mdia, Fourcc('m', 'd', 'h', 'd'), mdhd) || !FindChild(b, mdia, Fourcc('m', 'i', 'n', 'f'), minf) ||
             !FindChild(b, minf, Fourcc('s', 't', 'b', 'l'), stbl))
            return false;
         if (hdlr.end - hdlr.body < 12 || Be32(&b[hdlr.body + 8]) != Fourcc('v', 'i', 'd', 'e'))
            return false;

         uint32_t timescale = 0;
         if (mdhd.end - mdhd.body < 24)
            return false;
         timescale = (b[mdhd.body] == 1) ? Be32(&b[mdhd.body + 20]) : Be32(&b[mdhd.body + 12]);
         if (timescale == 0)
            return false;

         if (!FindChild(b, stbl, Fourcc('s', 't', 's', 'd'), stsd) || !FindChild(b, stbl, Fourcc('s', 't', 't', 's'), stts) ||
             !FindChild(b, stbl, Fourcc('s', 't', 's', 'z'), stsz) || !FindChild(b, stbl, Fourcc('s', 't', 's', 'c'), stsc))
            return false;
         const bool co64 = !FindChild(b, stbl, Fourcc('s', 't', 'c', 'o'), stco);
         if (co64 && !FindChild(b, stbl, Fourcc('c', 'o', '6', '4'), stco))
            return false;

         // Sample entry: only the first, and only the HAP codecs.
         if (stsd.end - stsd.body < 8 + 36 || Be32(&b[stsd.body + 4]) < 1)
            return false;
         const size_t entry = stsd.body + 8;
         const uint32_t codec = Be32(&b[entry + 4]);
         if (codec == Fourcc('H', 'a', 'p', '1'))
            p.format = Format::Dxt1;
         else if (codec == Fourcc('H', 'a', 'p', '5'))
            p.format = Format::Dxt5;
         else if (codec == Fourcc('H', 'a', 'p', 'Y'))
            p.format = Format::YCoCg5;
         else
            return false;
         p.width = (int)Be16(&b[entry + 32]);
         p.height = (int)Be16(&b[entry + 34]);
         if (p.width <= 0 || p.height <= 0)
            return false;

         // Sample sizes.
         if (stsz.end - stsz.body < 12)
            return false;
         const uint32_t fixedSize = Be32(&b[stsz.body + 4]);
         const uint32_t count = Be32(&b[stsz.body + 8]);
         if (count == 0 || (fixedSize == 0 && stsz.end - stsz.body < 12 + (size_t)count * 4))
            return false;
         std::vector<uint32_t> sizes(count);
         for (uint32_t i = 0; i < count; i++)
            sizes[i] = fixedSize != 0 ? fixedSize : Be32(&b[stsz.body + 12 + (size_t)i * 4]);

         // Durations.
         if (stts.end - stts.body < 8)
            return false;
         const uint32_t sttsCount = Be32(&b[stts.body + 4]);
         if (stts.end - stts.body < 8 + (size_t)sttsCount * 8)
            return false;
         std::vector<double> times;
         times.reserve(count);
         uint64_t ticks = 0;
         for (uint32_t e = 0; e < sttsCount && times.size() < count; e++)
         {
            const uint32_t n = Be32(&b[stts.body + 8 + (size_t)e * 8]);
            const uint32_t d = Be32(&b[stts.body + 8 + (size_t)e * 8 + 4]);
            for (uint32_t i = 0; i < n && times.size() < count; i++)
            {
               times.push_back((double)ticks / timescale);
               ticks += d;
            }
         }
         if (times.size() != count)
            return false;
         p.duration = (double)ticks / timescale;

         // Chunk offsets and the sample-to-chunk map.
         if (stco.end - stco.body < 8 || stsc.end - stsc.body < 8)
            return false;
         const uint32_t chunkCount = Be32(&b[stco.body + 4]);
         const size_t entrySize = co64 ? 8 : 4;
         if (stco.end - stco.body < 8 + (size_t)chunkCount * entrySize)
            return false;
         const uint32_t stscCount = Be32(&b[stsc.body + 4]);
         if (stscCount == 0 || stsc.end - stsc.body < 8 + (size_t)stscCount * 12)
            return false;

         p.samples.reserve(count);
         uint32_t sample = 0;
         for (uint32_t c = 0; c < chunkCount && sample < count; c++)
         {
            // Last stsc entry whose firstChunk <= this chunk (1-based).
            uint32_t perChunk = 0;
            for (uint32_t e = 0; e < stscCount; e++)
            {
               const size_t at = stsc.body + 8 + (size_t)e * 12;
               if (Be32(&b[at]) <= c + 1)
                  perChunk = Be32(&b[at + 4]);
               else
                  break;
            }
            uint64_t off = co64 ? Be64(&b[stco.body + 8 + (size_t)c * 8]) : Be32(&b[stco.body + 8 + (size_t)c * 4]);
            for (uint32_t i = 0; i < perChunk && sample < count; i++, sample++)
            {
               p.samples.push_back({ off, sizes[sample], times[sample] });
               off += sizes[sample];
            }
         }
         return p.samples.size() == count;
      }

      bool LoadMovie(std::ifstream& f, Parsed& p)
      {
         f.clear();
         f.seekg(0, std::ios::end);
         const uint64_t fileSize = (uint64_t)f.tellg();
         uint64_t pos = 0;
         while (pos + 8 <= fileSize)
         {
            uint8_t h[16];
            f.clear();
            f.seekg((std::streamoff)pos);
            f.read((char*)h, 16);
            const size_t got = (size_t)f.gcount();
            if (got < 8)
               return false;
            uint64_t size = Be32(h);
            const uint32_t type = Be32(h + 4);
            uint64_t hdr = 8;
            if (size == 1 && got >= 16)
            {
               size = Be64(h + 8);
               hdr = 16;
            }
            else if (size == 0)
            {
               size = fileSize - pos;
            }
            if (size < hdr || size > fileSize - pos) // as in NextAtom: no wrap
               return false;
            if (type == Fourcc('m', 'o', 'o', 'v'))
            {
               if (size - hdr > (64ull << 20))
                  return false; // an implausibly large index
               std::vector<uint8_t> moov((size_t)(size - hdr));
               f.clear();
               f.seekg((std::streamoff)(pos + hdr));
               f.read((char*)moov.data(), (std::streamsize)moov.size());
               if ((size_t)f.gcount() != moov.size())
                  return false;
               Atom root;
               root.body = 0;
               root.end = moov.size();
               size_t at = 0;
               Atom trak;
               while (NextAtom(moov, at, root.end, trak))
               {
                  if (trak.type == Fourcc('t', 'r', 'a', 'k'))
                  {
                     Parsed tmp;
                     if (ParseTrak(moov, trak, tmp))
                     {
                        p = std::move(tmp);
                        return true;
                     }
                  }
               }
               return false;
            }
            pos += size;
         }
         return false;
      }

      bool OpenFile(std::ifstream& f, const std::string& path)
      {
         f.open(std::filesystem::u8path(path), std::ios::binary);
         return f.is_open();
      }

      // Section header: 3-byte LE length + type byte, or a zero length and a
      // 4-byte LE length after the type byte.
      bool ReadSectionHeader(const uint8_t* d, size_t size, size_t& hdr, size_t& len, uint8_t& type)
      {
         if (size < 4)
            return false;
         len = (size_t)d[0] | ((size_t)d[1] << 8) | ((size_t)d[2] << 16);
         type = d[3];
         hdr = 4;
         if (len == 0)
         {
            if (size < 8)
               return false;
            len = Le32(d + 4);
            hdr = 8;
         }
         return hdr + len <= size;
      }

      Format FormatFromNibble(uint8_t n)
      {
         switch (n)
         {
            case 0x0B: return Format::Dxt1;
            case 0x0E: return Format::Dxt5;
            case 0x0F: return Format::YCoCg5;
            default: return Format::None;
         }
      }
   }

   bool SnappyDecompress(const uint8_t* in, size_t inSize, uint8_t* out, size_t expectedSize)
   {
      size_t i = 0;
      uint64_t total = 0;
      int shift = 0;
      while (true)
      {
         if (i >= inSize || shift > 32)
            return false;
         const uint8_t b = in[i++];
         total |= (uint64_t)(b & 0x7F) << shift;
         if ((b & 0x80) == 0)
            break;
         shift += 7;
      }
      if (total != expectedSize)
         return false;

      size_t o = 0;
      while (i < inSize)
      {
         const uint8_t tag = in[i++];
         size_t len = 0;
         size_t offset = 0;
         switch (tag & 3)
         {
            case 0:
            {
               len = (tag >> 2);
               if (len >= 60)
               {
                  const size_t extra = len - 59;
                  if (i + extra > inSize)
                     return false;
                  len = 0;
                  for (size_t k = 0; k < extra; k++)
                     len |= (size_t)in[i + k] << (8 * k);
                  i += extra;
               }
               len += 1;
               if (i + len > inSize || o + len > expectedSize)
                  return false;
               std::memcpy(out + o, in + i, len);
               i += len;
               o += len;
               continue;
            }
            case 1:
               if (i + 1 > inSize)
                  return false;
               len = 4 + ((tag >> 2) & 7);
               offset = ((size_t)(tag >> 5) << 8) | in[i++];
               break;
            case 2:
               if (i + 2 > inSize)
                  return false;
               len = 1 + (tag >> 2);
               offset = (size_t)in[i] | ((size_t)in[i + 1] << 8);
               i += 2;
               break;
            default:
               if (i + 4 > inSize)
                  return false;
               len = 1 + (tag >> 2);
               offset = Le32(in + i);
               i += 4;
               break;
         }
         if (offset == 0 || offset > o || o + len > expectedSize)
            return false;
         for (size_t k = 0; k < len; k++, o++)
            out[o] = out[o - offset];
      }
      return o == expectedSize;
   }

   size_t TextureBytes(Format fmt, int w, int h)
   {
      const size_t blocks = (size_t)((w + 3) / 4) * (size_t)((h + 3) / 4);
      return blocks * (fmt == Format::Dxt1 ? 8 : 16);
   }

   // Expands one section's payload into `out` (exactly `outSize` bytes).
   static bool ExpandSimple(uint8_t compressor, const uint8_t* d, size_t n, uint8_t* out, size_t outSize)
   {
      if (compressor == 0xA0)
      {
         if (n != outSize)
            return false;
         std::memcpy(out, d, n);
         return true;
      }
      if (compressor == 0xB0)
         return SnappyDecompress(d, n, out, outSize);
      return false;
   }

   bool DecodeFrame(const uint8_t* data, size_t size, Format& fmt, std::vector<uint8_t>& out, std::string& error)
   {
      size_t hdr = 0, len = 0;
      uint8_t type = 0;
      if (!ReadSectionHeader(data, size, hdr, len, type))
      {
         error = "HAP frame header is damaged";
         return false;
      }
      fmt = FormatFromNibble(type & 0x0F);
      if (fmt == Format::None)
      {
         error = "HAP frame uses an unsupported texture format";
         return false;
      }
      const uint8_t compressor = type & 0xF0;
      const uint8_t* payload = data + hdr;

      // The caller sizes `out`; DecodeFrame needs the expected byte count, so
      // it is taken from the vector's current size when set, else derived by
      // the Reader. A bare call with an empty vector decodes at the size the
      // stream itself declares (snappy preamble / chunk tables).
      size_t expected = out.size();

      if (compressor == 0xC0)
      {
         size_t ihdr = 0, ilen = 0;
         uint8_t itype = 0;
         if (!ReadSectionHeader(payload, len, ihdr, ilen, itype) || itype != 0x01)
         {
            error = "HAP chunk table is missing";
            return false;
         }
         const uint8_t* ins = payload + ihdr;
         const uint8_t* compressors = nullptr;
         const uint8_t* sizes = nullptr;
         const uint8_t* offsets = nullptr;
         size_t compressorsN = 0, sizesN = 0;
         size_t at = 0;
         while (at < ilen)
         {
            size_t shdr = 0, slen = 0;
            uint8_t stype = 0;
            if (!ReadSectionHeader(ins + at, ilen - at, shdr, slen, stype))
               break;
            if (stype == 0x02)
            {
               compressors = ins + at + shdr;
               compressorsN = slen;
            }
            else if (stype == 0x03)
            {
               sizes = ins + at + shdr;
               sizesN = slen;
            }
            else if (stype == 0x04)
            {
               offsets = ins + at + shdr;
            }
            at += shdr + slen;
         }
         const size_t chunks = compressorsN;
         if (chunks == 0 || sizes == nullptr || sizesN != chunks * 4)
         {
            error = "HAP chunk table is damaged";
            return false;
         }
         const uint8_t* chunkData = payload + ihdr + ilen;
         const size_t chunkBytes = len - (ihdr + ilen);

         // First pass: each chunk's output size.
         std::vector<size_t> outSizes(chunks);
         std::vector<size_t> inOffsets(chunks);
         size_t inPos = 0;
         size_t total = 0;
         for (size_t c = 0; c < chunks; c++)
         {
            const size_t csize = Le32(sizes + c * 4);
            const size_t coff = offsets != nullptr ? Le32(offsets + c * 4) : inPos;
            if (coff + csize > chunkBytes)
            {
               error = "HAP chunk runs past the frame";
               return false;
            }
            inOffsets[c] = coff;
            inPos = coff + csize;
            if (compressors[c] == 0x0B)
            {
               // Snappy preamble holds the expanded length.
               size_t i = 0;
               uint64_t v = 0;
               int shift = 0;
               while (i < csize && shift <= 32)
               {
                  const uint8_t bt = chunkData[coff + i++];
                  v |= (uint64_t)(bt & 0x7F) << shift;
                  if ((bt & 0x80) == 0)
                     break;
                  shift += 7;
               }
               outSizes[c] = (size_t)v;
            }
            else
            {
               outSizes[c] = csize;
            }
            total += outSizes[c];
         }
         if (expected != 0 && total != expected)
         {
            error = "HAP frame size does not match the texture";
            return false;
         }
         out.resize(total);
         size_t o = 0;
         for (size_t c = 0; c < chunks; c++)
         {
            const size_t csize = Le32(sizes + c * 4);
            const uint8_t* cd = chunkData + inOffsets[c];
            if (compressors[c] == 0x0A)
            {
               std::memcpy(out.data() + o, cd, csize);
            }
            else if (compressors[c] == 0x0B)
            {
               if (!SnappyDecompress(cd, csize, out.data() + o, outSizes[c]))
               {
                  error = "HAP frame failed to decompress";
                  return false;
               }
            }
            else
            {
               error = "HAP chunk uses an unknown compressor";
               return false;
            }
            o += outSizes[c];
         }
         return true;
      }

      if (compressor == 0xA0)
      {
         if (expected != 0 && len != expected)
         {
            error = "HAP frame size does not match the texture";
            return false;
         }
         out.assign(payload, payload + len);
         return true;
      }
      if (compressor == 0xB0)
      {
         if (expected == 0)
         {
            // Read the declared length from the snappy preamble.
            size_t i = 0;
            uint64_t v = 0;
            int shift = 0;
            while (i < len && shift <= 32)
            {
               const uint8_t bt = payload[i++];
               v |= (uint64_t)(bt & 0x7F) << shift;
               if ((bt & 0x80) == 0)
                  break;
               shift += 7;
            }
            expected = (size_t)v;
         }
         out.resize(expected);
         if (!ExpandSimple(compressor, payload, len, out.data(), expected))
         {
            error = "HAP frame failed to decompress";
            return false;
         }
         return true;
      }
      error = "HAP frame uses an unknown compressor";
      return false;
   }

   bool Reader::Probe(const std::string& path)
   {
      std::ifstream f;
      if (!OpenFile(f, path))
         return false;
      Parsed p;
      return LoadMovie(f, p);
   }

   bool Reader::Open(const std::string& path, std::string& error)
   {
      Close();
      if (!OpenFile(mFile, path))
      {
         error = "could not open file";
         return false;
      }
      Parsed p;
      if (!LoadMovie(mFile, p))
      {
         mFile.close();
         error = "not a HAP movie";
         return false;
      }
      mPath = path;
      mWidth = p.width;
      mHeight = p.height;
      mDuration = p.duration;
      mFormat = p.format;
      mSamples.clear();
      mSamples.reserve(p.samples.size());
      for (const auto& s : p.samples)
         mSamples.push_back({ s.offset, s.size, s.time });
      mFps = mDuration > 0.0 ? (double)mSamples.size() / mDuration : 30.0;
      return true;
   }

   void Reader::Prefetch(int fromIndex, int count)
   {
#if defined(__APPLE__) || defined(__linux__)
      if (mSamples.empty())
         return;
      if (mAdviseFd < 0)
         mAdviseFd = ::open(mPath.c_str(), O_RDONLY);
      if (mAdviseFd < 0)
         return;
      const int n = (int)mSamples.size();
      for (int k = 0; k < count; k++)
      {
         const Sample& s = mSamples[(size_t)(((fromIndex + k) % n + n) % n)];
#if defined(__APPLE__)
         struct radvisory ra;
         ra.ra_offset = (off_t)s.offset;
         ra.ra_count = (int)s.size;
         fcntl(mAdviseFd, F_RDADVISE, &ra);
#else
         posix_fadvise(mAdviseFd, (off_t)s.offset, (off_t)s.size, POSIX_FADV_WILLNEED);
#endif
      }
#else
      (void)fromIndex; (void)count;
#endif
   }

   void Reader::Close()
   {
#if defined(__APPLE__) || defined(__linux__)
      if (mAdviseFd >= 0)
      {
         ::close(mAdviseFd);
         mAdviseFd = -1;
      }
#endif
      if (mFile.is_open())
         mFile.close();
      mSamples.clear();
      mRaw.clear();
      mWidth = mHeight = 0;
      mDuration = mFps = 0.0;
      mFormat = Format::None;
   }

   int Reader::FrameIndexAt(double seconds) const
   {
      if (mSamples.empty())
         return 0;
      auto it = std::upper_bound(mSamples.begin(), mSamples.end(), seconds,
                                 [](double t, const Sample& s) { return t < s.time; });
      int idx = (int)(it - mSamples.begin()) - 1;
      return std::clamp(idx, 0, (int)mSamples.size() - 1);
   }

   double Reader::FrameTime(int index) const
   {
      if (index < 0 || index >= (int)mSamples.size())
         return 0.0;
      return mSamples[(size_t)index].time;
   }

   bool Reader::ReadFrame(int index, std::vector<uint8_t>& out, std::string& error)
   {
      if (!mFile.is_open() || index < 0 || index >= (int)mSamples.size())
      {
         error = "frame out of range";
         return false;
      }
      const Sample& s = mSamples[(size_t)index];
      mRaw.resize(s.size);
      mFile.clear();
      mFile.seekg((std::streamoff)s.offset);
      mFile.read((char*)mRaw.data(), (std::streamsize)s.size);
      if ((size_t)mFile.gcount() != s.size)
      {
         error = "short read";
         return false;
      }
      Format fmt = Format::None;
      out.resize(TextureBytes(mFormat, mWidth, mHeight));
      if (!DecodeFrame(mRaw.data(), mRaw.size(), fmt, out, error))
         return false;
      if (fmt != mFormat)
      {
         error = "HAP frame format changed mid-clip";
         return false;
      }
      return true;
   }
}
