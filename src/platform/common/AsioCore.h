// ASIO host helpers that need no driver, no SDK header and no Windows: sample
// format conversion, the installed-driver list, and buffer-size negotiation.
// The Windows host (../win/AsioHostWin.cpp) uses these; keeping them free of
// the ASIO SDK means every platform compiles and self-tests them
// (INFINITE_ASIOTEST), so a conversion bug shows in CI without an interface.
//
// Sample type numbers are the ASIO ones (ASIOSampleType in Steinberg's
// asio.h). Only the little-endian types are handled: every Windows machine
// Infinite runs on is little-endian, and a driver that reports a big-endian
// type is refused at open.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace AsioCore
{
   enum SampleType : int
   {
      kInt16LSB = 16,
      kInt24LSB = 17,
      kInt32LSB = 18,
      kFloat32LSB = 19,
      kFloat64LSB = 20,
      kInt32LSB16 = 24,
      kInt32LSB18 = 25,
      kInt32LSB20 = 26,
      kInt32LSB24 = 27,
   };

   inline bool SampleTypeSupported(int type)
   {
      switch (type)
      {
         case kInt16LSB: case kInt24LSB: case kInt32LSB: case kFloat32LSB: case kFloat64LSB:
         case kInt32LSB16: case kInt32LSB18: case kInt32LSB20: case kInt32LSB24:
            return true;
         default:
            return false;
      }
   }

   inline int BytesPerSample(int type)
   {
      switch (type)
      {
         case kInt16LSB: return 2;
         case kInt24LSB: return 3;
         case kFloat64LSB: return 8;
         case kInt32LSB: case kFloat32LSB: case kInt32LSB16: case kInt32LSB18:
         case kInt32LSB20: case kInt32LSB24:
            return 4;
         default: return 0;
      }
   }

   // Significant bits of the 32-bit-container integer types (sign-extended in
   // the low bits of the word), 0 for everything else.
   inline int ContainerBits(int type)
   {
      switch (type)
      {
         case kInt32LSB16: return 16;
         case kInt32LSB18: return 18;
         case kInt32LSB20: return 20;
         case kInt32LSB24: return 24;
         default: return 0;
      }
   }

   inline int32_t SignExtend24(const uint8_t* p)
   {
      int32_t v = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16));
      return (v << 8) >> 8;
   }

   // A driver buffer of `n` samples (one channel) -> float in [-1, 1].
   inline void ToFloat(int type, const void* src, float* dst, int n)
   {
      if (n <= 0)
         return;
      switch (type)
      {
         case kInt16LSB:
         {
            const int16_t* s = static_cast<const int16_t*>(src);
            for (int i = 0; i < n; i++)
               dst[i] = (float)s[i] * (1.0f / 32768.0f);
            break;
         }
         case kInt24LSB:
         {
            const uint8_t* s = static_cast<const uint8_t*>(src);
            for (int i = 0; i < n; i++)
               dst[i] = (float)SignExtend24(s + (size_t)i * 3) * (1.0f / 8388608.0f);
            break;
         }
         case kInt32LSB:
         {
            const int32_t* s = static_cast<const int32_t*>(src);
            for (int i = 0; i < n; i++)
               dst[i] = (float)((double)s[i] * (1.0 / 2147483648.0));
            break;
         }
         case kFloat32LSB:
            std::memcpy(dst, src, (size_t)n * sizeof(float));
            break;
         case kFloat64LSB:
         {
            const double* s = static_cast<const double*>(src);
            for (int i = 0; i < n; i++)
               dst[i] = (float)s[i];
            break;
         }
         case kInt32LSB16: case kInt32LSB18: case kInt32LSB20: case kInt32LSB24:
         {
            const int32_t* s = static_cast<const int32_t*>(src);
            const float scale = 1.0f / (float)(1 << (ContainerBits(type) - 1));
            for (int i = 0; i < n; i++)
               dst[i] = (float)s[i] * scale;
            break;
         }
         default:
            std::fill(dst, dst + n, 0.0f);
            break;
      }
   }

   inline int32_t ClampToInt(float v, int bits)
   {
      const double hi = (double)((int64_t)1 << (bits - 1));
      double x = std::isnan(v) ? 0.0 : (double)v * hi;
      x = std::nearbyint(x);
      if (x > hi - 1.0)
         x = hi - 1.0;
      if (x < -hi)
         x = -hi;
      return (int32_t)x;
   }

   // Float in [-1, 1] -> a driver buffer of `n` samples. Out-of-range input
   // clips instead of wrapping; NaN becomes silence.
   inline void FromFloat(int type, const float* src, void* dst, int n)
   {
      if (n <= 0)
         return;
      switch (type)
      {
         case kInt16LSB:
         {
            int16_t* d = static_cast<int16_t*>(dst);
            for (int i = 0; i < n; i++)
               d[i] = (int16_t)ClampToInt(src[i], 16);
            break;
         }
         case kInt24LSB:
         {
            uint8_t* d = static_cast<uint8_t*>(dst);
            for (int i = 0; i < n; i++)
            {
               const int32_t v = ClampToInt(src[i], 24);
               d[(size_t)i * 3 + 0] = (uint8_t)(v & 0xFF);
               d[(size_t)i * 3 + 1] = (uint8_t)((v >> 8) & 0xFF);
               d[(size_t)i * 3 + 2] = (uint8_t)((v >> 16) & 0xFF);
            }
            break;
         }
         case kInt32LSB:
         {
            int32_t* d = static_cast<int32_t*>(dst);
            for (int i = 0; i < n; i++)
               d[i] = ClampToInt(src[i], 32);
            break;
         }
         case kFloat32LSB:
            std::memcpy(dst, src, (size_t)n * sizeof(float));
            break;
         case kFloat64LSB:
         {
            double* d = static_cast<double*>(dst);
            for (int i = 0; i < n; i++)
               d[i] = (double)src[i];
            break;
         }
         case kInt32LSB16: case kInt32LSB18: case kInt32LSB20: case kInt32LSB24:
         {
            int32_t* d = static_cast<int32_t*>(dst);
            const int bits = ContainerBits(type);
            for (int i = 0; i < n; i++)
               d[i] = ClampToInt(src[i], bits);
            break;
         }
         default:
            std::memset(dst, 0, (size_t)n * (size_t)std::max(1, BytesPerSample(type)));
            break;
      }
   }

   // ---- installed drivers ---------------------------------------------------
   // HKLM\SOFTWARE\ASIO\<driver name> has a CLSID value; the host creates the
   // driver with it.

   struct DriverEntry
   {
      std::string name;
      std::string clsid; // "{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}"
   };

   inline std::string Trim(const std::string& s)
   {
      size_t a = 0, b = s.size();
      while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n'))
         a++;
      while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n'))
         b--;
      return s.substr(a, b - a);
   }

   inline std::string Lower(std::string s)
   {
      for (char& c : s)
         if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
      return s;
   }

   // Accepts exactly the registry form {8-4-4-4-12} of hex digits.
   inline bool IsValidClsid(const std::string& s)
   {
      if (s.size() != 38 || s.front() != '{' || s.back() != '}')
         return false;
      for (size_t i = 1; i + 1 < s.size(); i++)
      {
         const bool dash = i == 9 || i == 14 || i == 19 || i == 24;
         const char c = s[i];
         if (dash ? c != '-' : !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
      }
      return true;
   }

   // (key name, CLSID value) pairs as read from the registry -> the list the
   // UI shows: bad rows dropped, duplicates (same name, any case) dropped,
   // sorted by name.
   inline std::vector<DriverEntry> BuildDriverList(const std::vector<std::pair<std::string, std::string>>& raw)
   {
      std::vector<DriverEntry> out;
      for (const auto& row : raw)
      {
         DriverEntry e;
         e.name = Trim(row.first);
         e.clsid = Trim(row.second);
         if (e.name.empty() || !IsValidClsid(e.clsid))
            continue;
         const std::string key = Lower(e.name);
         bool dup = false;
         for (const DriverEntry& have : out)
            if (Lower(have.name) == key)
               dup = true;
         if (!dup)
            out.push_back(std::move(e));
      }
      std::sort(out.begin(), out.end(), [](const DriverEntry& a, const DriverEntry& b) {
         return Lower(a.name) < Lower(b.name);
      });
      return out;
   }

   // The driver to open: `wanted` by name (any case), the first one when no
   // name is set, -1 when a name is set but not installed.
   inline int FindDriver(const std::vector<DriverEntry>& list, const std::string& wanted)
   {
      if (list.empty())
         return -1;
      if (wanted.empty())
         return 0;
      const std::string key = Lower(Trim(wanted));
      for (size_t i = 0; i < list.size(); i++)
         if (Lower(list[i].name) == key)
            return (int)i;
      return -1;
   }

   // ---- buffer size ---------------------------------------------------------
   // ASIOGetBufferSize gives min / max / preferred / granularity. Granularity
   // -1 means "powers of two from min", > 0 means "min plus a multiple".
   inline int PickBufferSize(int requested, int minSize, int maxSize, int preferred, int granularity)
   {
      if (minSize <= 0 || maxSize < minSize)
         return preferred > 0 ? preferred : 0;
      if (minSize == maxSize)
         return minSize;
      if (requested <= 0)
         return std::clamp(preferred, minSize, maxSize);
      const int r = std::clamp(requested, minSize, maxSize);
      if (granularity == -1)
      {
         int best = minSize;
         for (int s = minSize; s <= maxSize; s *= 2)
         {
            if (std::abs(s - r) <= std::abs(best - r))
               best = s;
            if (s > maxSize / 2)
               break;
         }
         return best;
      }
      const int step = granularity > 0 ? granularity : 1;
      const int k = (int)std::lround((double)(r - minSize) / step);
      return std::clamp(minSize + k * step, minSize, maxSize);
   }
}
