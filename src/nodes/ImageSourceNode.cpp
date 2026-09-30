#include "ImageSourceNode.h"

#include <cstring>
#include "GltfImport.h"

#include "platform/OpenGLHeaders.h"
#include <vector>

#include "Platform.h"

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

namespace
{
   const char* const kGltfScheme = "gltf://";

   bool ParseGltfPseudoPath(const std::string& path, std::string& outRealPath, std::string& outSlot)
   {
      const size_t schemeLen = std::strlen(kGltfScheme);
      if (path.compare(0, schemeLen, kGltfScheme) != 0)
         return false;
      const size_t hash = path.find_last_of('#');
      if (hash == std::string::npos || hash < schemeLen)
         return false;
      outRealPath = path.substr(schemeLen, hash - schemeLen);
      outSlot = path.substr(hash + 1);
      return !outRealPath.empty() && !outSlot.empty();
   }

   const GltfImport::GltfDecodedImage* SlotImage(const GltfImport::GltfDecodePackage& pkg, const std::string& slot)
   {
      if (slot == "albedo") return &pkg.albedo;
      if (slot == "roughness") return &pkg.roughness;
      if (slot == "metallic") return &pkg.metallic;
      if (slot == "normal") return &pkg.normalMap;
      if (slot == "ao") return &pkg.occlusion;
      if (slot == "emission") return &pkg.emissive;
      return nullptr;
   }
}

bool ImageSourceNode::LoadFromDecoded(const std::vector<unsigned char>& pixels, int w, int h,
                                      const std::string& pseudoPath)
{
   if (pixels.empty() || w <= 0 || h <= 0)
   {
      mLastError = "no decoded pixels";
      return false;
   }
   UploadPixels(pixels, w, h);
   mLoadedPath = pseudoPath;
   pathInput = pseudoPath;
   return true;
}

bool ImageSourceNode::Load(const std::string& path)
{
   if (path.empty())
   {
      mLastError = "no file chosen";
      return false;
   }

   std::string gltfPath, gltfSlot;
   if (ParseGltfPseudoPath(path, gltfPath, gltfSlot))
   {
      std::string error;
      const GltfImport::GltfDecodePackage* pkg = GltfImport::DecodeCached(gltfPath, error);
      const GltfImport::GltfDecodedImage* img = pkg != nullptr ? SlotImage(*pkg, gltfSlot) : nullptr;
      if (img == nullptr || img->pixels.empty())
      {
         mLastError = error.empty() ? ("glTF has no " + gltfSlot + " map") : error;
         return false;
      }
      UploadPixels(img->pixels, img->width, img->height);
      mLoadedPath = path;
      pathInput = path;
      return true;
   }

   // Decoded by the OS rather than a bundled decoder, so anything Preview can
   // open (png/jpeg/tiff/heic/webp/raw/...) loads here too.
   std::vector<unsigned char> pixels;
   int w = 0, h = 0;
   std::string error;
   if (!Platform::LoadImageRGBA(path, pixels, w, h, error))
   {
      mLastError = error;
      return false;
   }
   UploadPixels(pixels, w, h);
   mLoadedPath = path;
   pathInput = path;
   return true;
}

void ImageSourceNode::UploadPixels(const std::vector<unsigned char>& pixels, int w, int h)
{
   if (mTex == 0)
      glGenTextures(1, &mTex);
   glBindTexture(GL_TEXTURE_2D, mTex);
   glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glBindTexture(GL_TEXTURE_2D, 0);

   mWidth = w;
   mHeight = h;
   mLastError.clear();
   mHasPlaceholder = false;
   mRevision = NextTextureRevision();
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
