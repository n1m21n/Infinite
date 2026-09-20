#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Shared base64 codec, promoted out of PredictionNodes.cpp's local anonymous-namespace copy
// (Step 8 item 5) rather than adding a third private copy alongside the one in NoteModel.cpp -
// any node that needs to stash a small binary blob (a learned model, a fitted matrix) in a
// std::string patch param can use this one instead of hand-rolling its own. PredictionNodes.cpp
// itself still carries its original copy (frozenProfile encode/decode); it was left alone here
// to avoid touching already-shipped, already-tested code for a refactor with no behavior change.
namespace Base64
{
   inline std::string Encode(const uint8_t* d, size_t n)
   {
      static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
      std::string out;
      for (size_t i = 0; i < n; i += 3)
      {
         const uint32_t v = (uint32_t)d[i] << 16 | (i + 1 < n ? (uint32_t)d[i + 1] << 8 : 0) |
                            (i + 2 < n ? d[i + 2] : 0);
         out += kAlphabet[(v >> 18) & 63];
         out += kAlphabet[(v >> 12) & 63];
         out += i + 1 < n ? kAlphabet[(v >> 6) & 63] : '=';
         out += i + 2 < n ? kAlphabet[v & 63] : '=';
      }
      return out;
   }

   inline bool Decode(const std::string& s, std::vector<uint8_t>& out)
   {
      static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
      out.clear();
      uint32_t acc = 0;
      int bits = 0;
      for (char c : s)
      {
         if (c == '=')
            break;
         const char* p = std::strchr(kAlphabet, c);
         if (p == nullptr || c == '\0')
            return false;
         acc = acc << 6 | (uint32_t)(p - kAlphabet);
         bits += 6;
         if (bits >= 8)
         {
            bits -= 8;
            out.push_back((uint8_t)(acc >> bits));
         }
      }
      return true;
   }
}
