#include "ImageSourceNode.h"

#include "gl3.h"
#include <vector>

#include "AssetCache.h"
#include "Platform.h"

namespace
{
   struct DecodedImage
   {
      std::vector<unsigned char> pixels;
      int width = 0;
      int height = 0;
   };

   // Shared across every ImageSourceNode/EnvironmentNode-style respawn in the
   // process - see AssetCache.h. 512MB is generous enough for a heavy scene's
   // worth of source images without letting a long session grow unbounded.
   AssetCache<DecodedImage>& GetImageDecodeCache()
   {
      static AssetCache<DecodedImage> cache(512ull * 1024 * 1024);
      return cache;
   }
}

ImageSourceNode::~ImageSourceNode()
{
   if (mTex != 0)
      glDeleteTextures(1, &mTex);
}

void ImageSourceNode::EnsurePlaceholder()
{
   if (mTex != 0)
      return;

   const int kSize = 256;
   std::vector<unsigned char> pixels(kSize * kSize * 4);
   for (int y = 0; y < kSize; y++)
   {
      for (int x = 0; x < kSize; x++)
      {
         bool checker = ((x / 32) + (y / 32)) % 2 == 0;
         unsigned char v = checker ? 90 : 50;
         int i = (y * kSize + x) * 4;
         pixels[i + 0] = v;
         pixels[i + 1] = v;
         pixels[i + 2] = (unsigned char)(v + 25);
         pixels[i + 3] = 255;
      }
   }

   glGenTextures(1, &mTex);
   glBindTexture(GL_TEXTURE_2D, mTex);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glBindTexture(GL_TEXTURE_2D, 0);

   mWidth = kSize;
   mHeight = kSize;
   mHasPlaceholder = true;
   mRevision = NextTextureRevision();
}

bool ImageSourceNode::Load(const std::string& path)
{
   if (path.empty())
   {
      mLastError = "no file chosen";
      return false;
   }

   // Decoded by the OS rather than a bundled decoder, so anything Preview can
   // open (png/jpeg/tiff/heic/webp/raw/...) loads here too. Cached by path
   // (mtime+size validated) so respawning this node from an undo/redo
   // snapshot doesn't re-run the OS decoder on bytes we've already decoded -
   // see AssetCache.h.
   auto& cache = GetImageDecodeCache();
   int w = 0, h = 0;
   const std::vector<unsigned char>* cachedPixels = nullptr;
   std::vector<unsigned char> pixels;
   if (const DecodedImage* hit = cache.Get(path))
   {
      w = hit->width;
      h = hit->height;
      cachedPixels = &hit->pixels;
   }
   else
   {
      std::string error;
      if (!Platform::LoadImageRGBA(path, pixels, w, h, error))
      {
         mLastError = error;
         return false;
      }
      cache.Put(path, DecodedImage{ pixels, w, h }, pixels.size());
      cachedPixels = &pixels;
   }

   if (mTex == 0)
      glGenTextures(1, &mTex);
   glBindTexture(GL_TEXTURE_2D, mTex);
   glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, cachedPixels->data());
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glBindTexture(GL_TEXTURE_2D, 0);

   mWidth = w;
   mHeight = h;
   mLoadedPath = path;
   pathInput = path;
   mLastError.clear();
   mHasPlaceholder = false;
   mRevision = NextTextureRevision();
   return true;
}

bool ImageSourceNode::LoadViaDialog()
{
   std::string path = Platform::OpenImageDialog();
   if (path.empty())
      return false; // cancelled - not an error
   return Load(path);
}

unsigned int ImageSourceNode::GetOutputTexture()
{
   EnsurePlaceholder();
   return mTex;
}

void ImageSourceNode::CookIfNeeded(int /*frameId*/)
{
   EnsurePlaceholder();
}
