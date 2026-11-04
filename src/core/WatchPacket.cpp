#include "core/WatchPacket.h"

#include <algorithm>
#include <cmath>

namespace WatchPacket
{
   namespace
   {
      uint16_t U16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
      int16_t S16(const uint8_t* p) { return (int16_t)U16(p); }
      uint32_t U32(const uint8_t* p) { return (uint32_t)U16(p) | ((uint32_t)U16(p + 2) << 16); }

      void PutU16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v & 0xff); p[1] = (uint8_t)(v >> 8); }
      void PutS16(uint8_t* p, int16_t v) { PutU16(p, (uint16_t)v); }
      void PutU32(uint8_t* p, uint32_t v) { PutU16(p, (uint16_t)(v & 0xffff)); PutU16(p + 2, (uint16_t)(v >> 16)); }

      // value * scale rounded and saturated to int16
      int16_t Q(float v, float scale)
      {
         const float r = std::nearbyint(v * scale);
         return (int16_t)std::max(-32768.f, std::min(32767.f, r));
      }
   }

   Result Decode(const uint8_t* d, size_t len, Sample& out)
   {
      if (!d || len < 3)
         return Result::kShort;
      if (d[0] != kMagic0 || d[1] != kMagic1)
         return Result::kBadMagic;
      if (d[2] > kVersion)
         return Result::kNewerVersion;   // checked before the length: a v2 packet may be any size
      if (len < kSize)
         return Result::kShort;

      Sample s;
      s.seq = d[3];
      s.timeMs = U32(d + 4);
      s.flags = U16(d + 8);
      for (int i = 0; i < 4; i++)
         s.q[i] = (float)S16(d + 10 + i * 2) / 32768.f;
      const float n = std::sqrt(s.q[0] * s.q[0] + s.q[1] * s.q[1] + s.q[2] * s.q[2] + s.q[3] * s.q[3]);
      if (n > 1e-4f)
         for (float& c : s.q) c /= n;
      else
      {
         s.q[0] = 1; s.q[1] = s.q[2] = s.q[3] = 0;
      }
      for (int i = 0; i < 3; i++)
      {
         s.rot[i] = (float)S16(d + 18 + i * 2) * 0.1f;
         s.accel[i] = (float)S16(d + 24 + i * 2) * 0.001f;
         s.gravity[i] = (float)S16(d + 30 + i * 2) * 0.001f;
      }
      s.crown = (float)S16(d + 36) * 0.001f;
      s.touchX = (float)U16(d + 38) / 65535.f;
      s.touchY = (float)U16(d + 40) / 65535.f;
      s.heart = d[42];
      s.battery = d[43];
      out = s;
      return Result::kOk;
   }

   void Encode(const Sample& s, uint8_t out[kSize])
   {
      out[0] = kMagic0; out[1] = kMagic1; out[2] = kVersion; out[3] = s.seq;
      PutU32(out + 4, s.timeMs);
      PutU16(out + 8, s.flags);
      for (int i = 0; i < 4; i++)
         PutS16(out + 10 + i * 2, Q(s.q[i], 32768.f));
      for (int i = 0; i < 3; i++)
      {
         PutS16(out + 18 + i * 2, Q(s.rot[i], 10.f));
         PutS16(out + 24 + i * 2, Q(s.accel[i], 1000.f));
         PutS16(out + 30 + i * 2, Q(s.gravity[i], 1000.f));
      }
      PutS16(out + 36, Q(s.crown, 1000.f));
      PutU16(out + 38, (uint16_t)std::max(0.f, std::min(65535.f, std::nearbyint(s.touchX * 65535.f))));
      PutU16(out + 40, (uint16_t)std::max(0.f, std::min(65535.f, std::nearbyint(s.touchY * 65535.f))));
      out[42] = s.heart;
      out[43] = s.battery;
   }
}
