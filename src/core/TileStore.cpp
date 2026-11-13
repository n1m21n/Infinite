#include "TileStore.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "stb_image.h"
#include "stb_image_write.h"

namespace TileStore
{
   namespace
   {
      std::mutex sLock;
      std::unordered_map<std::string, std::vector<uint8_t>>& Map()
      {
         static std::unordered_map<std::string, std::vector<uint8_t>> m;
         return m;
      }

      bool IsHex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }
   }

   bool IsKey(const char* s, size_t n)
   {
      if (n != 17 || s[0] != 't')
         return false;
      for (size_t i = 1; i < 17; i++)
         if (!IsHex(s[i]))
            return false;
      return true;
   }

   std::string Put(const uint8_t* rgba, int w, int h)
   {
      const size_t n = (size_t)w * (size_t)h * 4;
      bool empty = true;
      for (size_t i = 3; i < n; i += 4)
         if (rgba[i] != 0)
         {
            empty = false;
            break;
         }
      if (empty)
         return "-";
      unsigned long long hsh = 1469598103934665603ull;
      auto mix = [&](uint8_t b) { hsh = (hsh ^ b) * 1099511628211ull; };
      for (int k = 0; k < 4; k++)
      {
         mix((uint8_t)(w >> (k * 8)));
         mix((uint8_t)(h >> (k * 8)));
      }
      for (size_t i = 0; i < n; i++)
         mix(rgba[i]);
      char key[24];
      std::snprintf(key, sizeof(key), "t%016llx", hsh);
      {
         std::lock_guard<std::mutex> g(sLock);
         if (Map().count(key) != 0)
            return key;
      }
      std::vector<uint8_t> png;
      stbi_write_png_to_func(
         [](void* ctx, void* data, int size)
         {
            auto* out = static_cast<std::vector<uint8_t>*>(ctx);
            out->insert(out->end(), (uint8_t*)data, (uint8_t*)data + size);
         },
         &png, w, h, 4, rgba, w * 4);
      std::lock_guard<std::mutex> g(sLock);
      Map()[key] = std::move(png);
      return key;
   }

   void PutEncoded(const std::string& key, std::vector<uint8_t> png)
   {
      if (!IsKey(key.data(), key.size()) || png.empty())
         return;
      std::lock_guard<std::mutex> g(sLock);
      Map()[key] = std::move(png);
   }

   bool Get(const std::string& key, std::vector<uint8_t>& rgba, int& w, int& h)
   {
      std::vector<uint8_t> png = Encoded(key);
      if (png.empty())
         return false;
      int n = 0;
      unsigned char* px = stbi_load_from_memory(png.data(), (int)png.size(), &w, &h, &n, 4);
      if (px == nullptr)
         return false;
      rgba.assign(px, px + (size_t)w * (size_t)h * 4);
      stbi_image_free(px);
      return true;
   }

   std::vector<uint8_t> Encoded(const std::string& key)
   {
      std::lock_guard<std::mutex> g(sLock);
      auto it = Map().find(key);
      return it != Map().end() ? it->second : std::vector<uint8_t>();
   }

   bool Has(const std::string& key)
   {
      std::lock_guard<std::mutex> g(sLock);
      return Map().count(key) != 0;
   }

   void CollectKeys(const std::string& text, std::vector<std::string>& out)
   {
      std::unordered_set<std::string> seen(out.begin(), out.end());
      for (size_t i = 0; i + 17 <= text.size(); i++)
      {
         if (text[i] != 't' || (i > 0 && (std::isalnum((unsigned char)text[i - 1]) != 0)))
            continue;
         if (!IsKey(text.data() + i, 17) || (i + 17 < text.size() && std::isalnum((unsigned char)text[i + 17]) != 0))
            continue;
         std::string k = text.substr(i, 17);
         if (seen.count(k) == 0 && Has(k))
         {
            seen.insert(k);
            out.push_back(k);
         }
         i += 16;
      }
   }
}
