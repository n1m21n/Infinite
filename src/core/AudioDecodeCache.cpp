#include "AudioDecodeCache.h"

#include "AssetCache.h"

namespace AudioDecodeCache
{
   namespace
   {
      size_t BufferBytes(const Platform::SampleBuffer& buf)
      {
         return buf.channelData.size() * sizeof(float);
      }

      AssetCache<Platform::SampleBuffer>& GetCache()
      {
         // Sample libraries run bigger than image/model assets (a drum kit's
         // worth of long one-shots), so this budget is generous relative to
         // the image/model caches.
         static AssetCache<Platform::SampleBuffer> cache(1024ull * 1024 * 1024);
         return cache;
      }
   }

   bool DecodeCached(const std::string& path, Platform::SampleBuffer& outBuffer, std::string& outError)
   {
      auto& cache = GetCache();
      if (const Platform::SampleBuffer* hit = cache.Get(path))
      {
         outBuffer = *hit;
         return true;
      }

      if (!Platform::DecodeAudioFileToBuffer(path, outBuffer, outError))
         return false;

      cache.Put(path, outBuffer, BufferBytes(outBuffer));
      return true;
   }
}
