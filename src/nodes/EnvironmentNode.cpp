#include "EnvironmentNode.h"

#include "gl3.h"
#include <algorithm>
#include <cmath>
#include <vector>

#include "Platform.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include <cstdio>
#include "stb_image.h"

namespace
{
   // stb_image wants a FILE*-free path when STBI_NO_STDIO is set, but reading
   // the whole file into memory first is simpler than wiring its callback API
   // for a loader that only ever runs once per Load() call.
   bool ReadWholeFile(const std::string& path, std::vector<unsigned char>& out)
   {
      FILE* f = fopen(path.c_str(), "rb");
      if (f == nullptr)
         return false;
      fseek(f, 0, SEEK_END);
      const long size = ftell(f);
      fseek(f, 0, SEEK_SET);
      if (size <= 0)
      {
         fclose(f);
         return false;
      }
      out.resize((size_t)size);
      const size_t read = fread(out.data(), 1, (size_t)size, f);
      fclose(f);
      return read == (size_t)size;
   }
}

EnvironmentNode::~EnvironmentNode()
{
   if (mTex != 0)
      glDeleteTextures(1, &mTex);
}

void EnvironmentNode::EnsurePlaceholder()
{
   if (mTex != 0)
      return;

   // A small vertical gradient rather than a checker: this node's preview
   // thumbnail is standing in for a sky, and a checker there reads as "no
   // image loaded" for every source node except this one.
   const int kSize = 4;
   std::vector<unsigned char> pixels(kSize * kSize * 4);
   for (int y = 0; y < kSize; y++)
   {
      const float t = (float)y / (float)(kSize - 1);
      const unsigned char r = (unsigned char)(40 + t * 60);
      const unsigned char g = (unsigned char)(50 + t * 70);
      const unsigned char b = (unsigned char)(70 + t * 110);
      for (int x = 0; x < kSize; x++)
      {
         const int i = (y * kSize + x) * 4;
         pixels[i + 0] = r; pixels[i + 1] = g; pixels[i + 2] = b; pixels[i + 3] = 255;
      }
   }

   glGenTextures(1, &mTex);
   glBindTexture(GL_TEXTURE_2D, mTex);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glBindTexture(GL_TEXTURE_2D, 0);

   mWidth = kSize;
   mHeight = kSize;
   mRevision = NextTextureRevision();
}

void EnvironmentNode::Upload(const float* pixels, int w, int h)
{
   // Real-world HDRIs routinely carry a handful of non-finite texels - dead
   // pixels, or a sun encoded as +Inf. They are invisible at mip 0 (a dozen
   // texels out of millions), which is why the preview thumbnail and the
   // background quad both look perfect. But glGenerateMipmap box-averages its
   // way up the chain, and NaN/Inf is contagious under averaging: a single bad
   // texel spreads until the topmost 1x1 mip is entirely NaN. The lit-geometry
   // path always reads that top mip for diffuse irradiance (sampleEnv(n, 1.0)
   // in Geometry3DNodes.cpp), so the NaN propagates through the whole shading
   // result and the geometry renders solid black or a flat garbage colour.
   //
   // Two ways a texel goes bad, so both are handled here:
   //   - already non-finite in the source file -> drop to 0
   //   - finite but above half-float range -> becomes +Inf on conversion into
   //     the 16F texture, so clamp to the largest representable half
   const float kHalfMax = 65504.0f;
   std::vector<float> sanitized((size_t)w * (size_t)h * 3);
   size_t badTexels = 0;
   for (size_t i = 0, n = (size_t)w * (size_t)h * 3; i < n; i++)
   {
      const float v = pixels[i];
      if (!std::isfinite(v))
      {
         sanitized[i] = 0.0f;
         badTexels++;
      }
      else
      {
         sanitized[i] = std::min(std::max(v, -kHalfMax), kHalfMax);
      }
   }

   if (mTex == 0)
      glGenTextures(1, &mTex);
   glBindTexture(GL_TEXTURE_2D, mTex);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, w, h, 0, GL_RGB, GL_FLOAT, sanitized.data());
   glGenerateMipmap(GL_TEXTURE_2D);
   const GLenum err = glGetError();
   if (err != GL_NO_ERROR)
      fprintf(stderr, "EnvironmentNode::Upload: GL error 0x%x uploading %dx%d HDRI\n", err, w, h);
   if (badTexels != 0)
      fprintf(stderr, "EnvironmentNode::Upload: replaced %zu non-finite component(s) in %dx%d HDRI\n",
              badTexels, w, h);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   // Longitude wraps all the way around; latitude does not - clamping the
   // poles avoids a seam where the top/bottom row would otherwise repeat.
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glBindTexture(GL_TEXTURE_2D, 0);

   mWidth = w;
   mHeight = h;
   mMaxLod = std::floor(std::log2((float)std::max(w, h)));
   mRevision = NextTextureRevision();
}

bool EnvironmentNode::Load(const std::string& path)
{
   if (path.empty())
   {
      mLastError = "no file chosen";
      return false;
   }

   std::string ext;
   const size_t dot = path.find_last_of('.');
   if (dot != std::string::npos)
      ext = path.substr(dot + 1);
   for (char& c : ext)
      c = (char)tolower((unsigned char)c);

   if (ext == "hdr")
   {
      // Radiance HDR: ImageIO does not read this format, so it goes through
      // stb_image's own decoder instead.
      std::vector<unsigned char> file;
      if (!ReadWholeFile(path, file))
      {
         mLastError = "could not read file";
         return false;
      }

      int w = 0, h = 0, channels = 0;
      float* pixels = stbi_loadf_from_memory(file.data(), (int)file.size(), &w, &h, &channels, 3);
      if (pixels == nullptr)
      {
         mLastError = stbi_failure_reason() ? stbi_failure_reason() : "could not decode HDR image";
         return false;
      }
      Upload(pixels, w, h);
      stbi_image_free(pixels);
   }
   else if (ext == "exr")
   {
      // EXR: the OS decodes this natively (Ventura+), including values above
      // 1.0, as long as the bitmap context is told to keep float precision
      // and stay in an extended-range colour space rather than clamping to
      // 8-bit sRGB the way ImageSourceNode's loader does.
      std::vector<float> pixels;
      int w = 0, h = 0;
      std::string error;
      if (!Platform::LoadImageFloatRGB(path, pixels, w, h, error))
      {
         mLastError = error;
         return false;
      }
      Upload(pixels.data(), w, h);
   }
   else
   {
      mLastError = "expected a .hdr or .exr equirectangular image";
      return false;
   }

   mLoadedPath = path;
   pathInput = path;
   mLastError.clear();
   mHasImage = true;
   return true;
}

bool EnvironmentNode::LoadViaDialog()
{
   std::string path = Platform::OpenHdrDialog();
   if (path.empty())
      return false; // cancelled - not an error
   return Load(path);
}

unsigned int EnvironmentNode::GetOutputTexture()
{
   if (mHasImage)
      return mTex;
   EnsurePlaceholder();
   return mTex;
}

unsigned int EnvironmentNode::GetEnvironmentTexture()
{
   return mHasImage ? mTex : 0;
}

void EnvironmentNode::CookIfNeeded(int /*frameId*/)
{
   if (!mHasImage)
      EnsurePlaceholder();
}
