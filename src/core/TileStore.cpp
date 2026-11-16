#include "TileStore.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "stb_image.h"
#include "stb_image_write.h"

namespace TileStore
{
   namespace
   {
      // A tile is kept raw until a worker has compressed it, so a stroke's end
      // never waits on PNG encoding; anything that needs the PNG (saving) gets
      // it encoded on the spot when the worker hasn't reached it yet.
      struct Entry
      {
         std::vector<uint8_t> raw;   // straight RGBA8 until encoded
         int w = 0, h = 0;
         std::vector<uint8_t> png;
      };
      std::mutex sLock;
      std::unordered_map<std::string, Entry>& Map()
      {
         static std::unordered_map<std::string, Entry> m;
         return m;
      }

      bool IsHex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }

      std::vector<uint8_t> EncodePng(const std::vector<uint8_t>& rgba, int w, int h)
      {
         std::vector<uint8_t> png;
         stbi_write_png_to_func(
            [](void* ctx, void* data, int size)
            {
               auto* out = static_cast<std::vector<uint8_t>*>(ctx);
               out->insert(out->end(), (uint8_t*)data, (uint8_t*)data + size);
            },
            &png, w, h, 4, rgba.data(), w * 4);
         return png;
      }

      // Encodes `key` if it is still raw. Called with sLock held; drops it while encoding.
      void EncodeLocked(std::unique_lock<std::mutex>& g, const std::string& key)
      {
         auto it = Map().find(key);
         if (it == Map().end() || !it->second.png.empty() || it->second.raw.empty())
            return;
         const std::vector<uint8_t> raw = it->second.raw;
         const int w = it->second.w, h = it->second.h;
         g.unlock();
         std::vector<uint8_t> png = EncodePng(raw, w, h);
         g.lock();
         it = Map().find(key);
         if (it != Map().end() && it->second.png.empty())
         {
            it->second.png = std::move(png);
            it->second.raw.clear();
            it->second.raw.shrink_to_fit();
         }
      }

      struct Worker
      {
         std::thread thread;
         std::condition_variable cv;
         std::deque<std::string> queue;
         bool stop = false;
         ~Worker()
         {
            {
               std::lock_guard<std::mutex> g(sLock);
               stop = true;
            }
            cv.notify_all();
            if (thread.joinable())
               thread.join();
         }
         void Run()
         {
            std::unique_lock<std::mutex> g(sLock);
            for (;;)
            {
               cv.wait(g, [&] { return stop || !queue.empty(); });
               if (stop)
                  return;
               const std::string key = std::move(queue.front());
               queue.pop_front();
               EncodeLocked(g, key);
            }
         }
      };
      // Made after Map() so it is torn down first.
      Worker& TheWorker()
      {
         static Worker w;
         static std::once_flag started;
         std::call_once(started, [] { w.thread = std::thread([] { w.Run(); }); });
         return w;
      }
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
      // Eight bytes per step: tiles are 256 KB and this runs on stroke end.
      size_t i = 0;
      for (; i + 8 <= n; i += 8)
      {
         unsigned long long v;
         std::memcpy(&v, rgba + i, 8);
         hsh = (hsh ^ v) * 1099511628211ull;
         hsh ^= hsh >> 29;
      }
      for (; i < n; i++)
         mix(rgba[i]);
      char key[24];
      std::snprintf(key, sizeof(key), "t%016llx", hsh);
      Map();
      Worker& worker = TheWorker();
      {
         std::lock_guard<std::mutex> g(sLock);
         if (Map().count(key) != 0)
            return key;
         Entry& e = Map()[key];
         e.raw.assign(rgba, rgba + n);
         e.w = w;
         e.h = h;
         worker.queue.push_back(key);
      }
      worker.cv.notify_one();
      return key;
   }

   void PutEncoded(const std::string& key, std::vector<uint8_t> png)
   {
      if (!IsKey(key.data(), key.size()) || png.empty())
         return;
      std::lock_guard<std::mutex> g(sLock);
      Entry& e = Map()[key];
      e.png = std::move(png);
      e.raw.clear();
   }

   bool Get(const std::string& key, std::vector<uint8_t>& rgba, int& w, int& h)
   {
      {
         std::lock_guard<std::mutex> g(sLock);
         auto it = Map().find(key);
         if (it != Map().end() && !it->second.raw.empty())
         {
            rgba = it->second.raw;
            w = it->second.w;
            h = it->second.h;
            return true;
         }
      }
      std::vector<uint8_t> png = Encoded(key);
      if (png.empty())
         return false;
      int n = 0;
      // Tiles are stored as they sit in the GL surface; never let another decoder's flip setting turn them over.
      stbi_set_flip_vertically_on_load_thread(0);
      unsigned char* px = stbi_load_from_memory(png.data(), (int)png.size(), &w, &h, &n, 4);
      if (px == nullptr)
         return false;
      rgba.assign(px, px + (size_t)w * (size_t)h * 4);
      stbi_image_free(px);
      return true;
   }

   std::vector<uint8_t> Encoded(const std::string& key)
   {
      std::unique_lock<std::mutex> g(sLock);
      EncodeLocked(g, key);
      auto it = Map().find(key);
      return it != Map().end() ? it->second.png : std::vector<uint8_t>();
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
