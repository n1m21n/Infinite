#include "EnvironmentNode.h"

#include "gl3.h"
#include "BenchReport.h"
#include <algorithm>
#include <cmath>
#include <vector>

#include "AssetCache.h"
#include "Platform.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include <cstdio>
#include "stb_image.h"
#include "platform/AppPaths.h"

namespace
{
   // stb_image wants a FILE*-free path when STBI_NO_STDIO is set, but reading
   // the whole file into memory first is simpler than wiring its callback API
   // for a loader that only ever runs once per Load() call.
   bool ReadWholeFile(const std::string& path, std::vector<unsigned char>& out)
   {
      FILE* f = AppPaths::OpenFile(path, "rb");
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

   struct DecodedEnv
   {
      std::vector<float> pixels; // RGB, width*height*3
      int width = 0;
      int height = 0;
   };

   // Shared across every EnvironmentNode respawn in the process - see
   // AssetCache.h. Both the .hdr and .exr decode paths normalize into this
   // same RGB-float shape so one cache covers both.
   AssetCache<DecodedEnv>& GetEnvDecodeCache()
   {
      static AssetCache<DecodedEnv> cache(512ull * 1024 * 1024);
      return cache;
   }
}

EnvironmentNode::~EnvironmentNode()
{
   if (mTex != 0)
   {
      Bench::GpuMem::ReleaseTexture(mTex);
      glDeleteTextures(1, &mTex);
   }
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
   Bench::GpuMem::RecordTexture(mTex, Bench::GpuMemCategory::Textures, kSize, kSize, GL_RGBA8, false, "EnvironmentNode");

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

   // Sun extraction, in the same direction convention as Render 3D's dirToEquirect: texel row r
   // sits at v = (r + 0.5) / h, theta = pi * v (row 0 is the zenith); column x at phi = 2 pi (u - 0.5).
   mHasSun = false;
   mUploadedThreshold = sunThreshold;
   if (sunThreshold > 0.0f)
   {
      const double kPi = 3.14159265358979;
      double light[3] = { 0, 0, 0 }, dir[3] = { 0, 0, 0 };
      for (int r = 0; r < h; r++)
      {
         const double theta = kPi * (r + 0.5) / h;
         // Solid angle of one texel: rows near the poles cover less sky.
         const double omega = (2.0 * kPi / w) * (kPi / h) * std::sin(theta);
         for (int x = 0; x < w; x++)
         {
            float* p = &sanitized[((size_t)r * w + x) * 3];
            const float lum = 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
            if (lum <= sunThreshold)
               continue;
            const float keep = sunThreshold / lum;
            const double phi = 2.0 * kPi * ((x + 0.5) / w - 0.5);
            const double weight = (lum - sunThreshold) * omega;
            for (int k = 0; k < 3; k++)
            {
               light[k] += p[k] * (1.0f - keep) * omega;
               p[k] *= keep;
            }
            dir[0] += std::sin(theta) * std::cos(phi) * weight;
            dir[1] += std::cos(theta) * weight;
            dir[2] += std::sin(theta) * std::sin(phi) * weight;
         }
      }
      const double len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
      if (len > 0.0 && light[0] + light[1] + light[2] > 0.0)
      {
         mHasSun = true;
         for (int k = 0; k < 3; k++)
         {
            mSunDir[k] = (float)(dir[k] / len);
            mSunLight[k] = (float)light[k];
         }
      }
   }

   if (mTex == 0)
      glGenTextures(1, &mTex);
   glBindTexture(GL_TEXTURE_2D, mTex);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, w, h, 0, GL_RGB, GL_FLOAT, sanitized.data());
   glGenerateMipmap(GL_TEXTURE_2D);
   Bench::GpuMem::RecordTexture(mTex, Bench::GpuMemCategory::Textures, w, h, GL_RGB16F, true, "EnvironmentNode");
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

   auto& cache = GetEnvDecodeCache();
   const DecodedEnv* cached = cache.Get(path);
   DecodedEnv decoded;

   if (cached == nullptr)
   {
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
         decoded.pixels.assign(pixels, pixels + (size_t)w * (size_t)h * 3);
         decoded.width = w;
         decoded.height = h;
         stbi_image_free(pixels);
      }
      else if (ext == "exr")
      {
         // EXR: the OS decodes this natively (Ventura+), including values above
         // 1.0, as long as the bitmap context is told to keep float precision
         // and stay in an extended-range colour space rather than clamping to
         // 8-bit sRGB the way ImageSourceNode's loader does.
         std::string error;
         if (!Platform::LoadImageFloatRGB(path, decoded.pixels, decoded.width, decoded.height, error))
         {
            mLastError = error;
            return false;
         }
      }
      else
      {
         mLastError = "expected a .hdr or .exr equirectangular image";
         return false;
      }

      cache.Put(path, decoded, decoded.pixels.size() * sizeof(float));
      cached = &decoded;
   }

   Upload(cached->pixels.data(), cached->width, cached->height);

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

// Row 5.5: Preetham, Shirley, Smits, "A Practical Analytic Model for Daylight" (SIGGRAPH 1999). Sky luminance Y and
// chromaticity x, y each follow Perez: F(theta, gamma) = (1 + A e^(B / cos theta)) (1 + C e^(D gamma) + E cos^2 gamma),
// theta the view's zenith angle, gamma its angle to the sun, scaled so the zenith equals the paper's zenith fits.
// Below the horizon theta is held at the horizon while gamma still follows the view. Y in kcd/m2 * 0.06 (the scale Blender's Preetham sky shows).
void EnvironmentNode::MakeSky()
{
   const double kPi = 3.14159265358979, T = std::max(1.0f, turbidity);
   const double el = std::clamp((double)skyElevation, 0.0, 90.0) * kPi / 180.0, az = skyAzimuth * kPi / 180.0;
   const double ts = kPi / 2 - el;                      // the sun's zenith angle
   const double sd[3] = { std::cos(el) * std::cos(az), std::sin(el), std::cos(el) * std::sin(az) };
   const double cY[5] = { 0.1787 * T - 1.4630, -0.3554 * T + 0.4275, -0.0227 * T + 5.3251, 0.1206 * T - 2.5771, -0.0670 * T + 0.3703 };
   const double cx[5] = { -0.0193 * T - 0.2592, -0.0665 * T + 0.0008, -0.0004 * T + 0.2125, -0.0641 * T - 0.8989, -0.0033 * T + 0.0452 };
   const double cy[5] = { -0.0167 * T - 0.2608, -0.0950 * T + 0.0092, -0.0079 * T + 0.2102, -0.0441 * T - 1.6537, -0.0109 * T + 0.0529 };
   auto perez = [](const double* c, double th, double g)
   { return (1.0 + c[0] * std::exp(c[1] / std::cos(th))) * (1.0 + c[2] * std::exp(c[3] * g) + c[4] * std::cos(g) * std::cos(g)); };
   const double chi = (4.0 / 9.0 - T / 120.0) * (kPi - 2.0 * ts);
   const double Yz = ((4.0453 * T - 4.9710) * std::tan(chi) - 0.2155 * T + 2.4192) * 0.06;
   const double t2 = ts * ts, t3 = t2 * ts, T2 = T * T;
   const double xz = (0.00166 * t3 - 0.00375 * t2 + 0.00209 * ts) * T2 + (-0.02903 * t3 + 0.06377 * t2 - 0.03202 * ts + 0.00394) * T +
                     (0.11693 * t3 - 0.21196 * t2 + 0.06052 * ts + 0.25886);
   const double yz = (0.00275 * t3 - 0.00610 * t2 + 0.00317 * ts) * T2 + (-0.04214 * t3 + 0.08970 * t2 - 0.04153 * ts + 0.00516) * T +
                     (0.15346 * t3 - 0.26756 * t2 + 0.06670 * ts + 0.26688);
   const double nY = Yz / perez(cY, 0.0, ts), nx = xz / perez(cx, 0.0, ts), ny = yz / perez(cy, 0.0, ts);
   const int w = 512, h = 256;
   std::vector<float> px((size_t)w * h * 3);
   for (int r = 0; r < h; r++)
   {
      const double th = kPi * (r + 0.5) / h;             // row 0 the zenith, as dirToEquirect
      const double thc = std::min(th, kPi / 2 - 0.001);
      for (int c = 0; c < w; c++)
      {
         const double ph = 2.0 * kPi * ((c + 0.5) / w - 0.5);
         const double d[3] = { std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph) }; // gamma from the true view
         const double g = std::acos(std::clamp(d[0] * sd[0] + d[1] * sd[1] + d[2] * sd[2], -1.0, 1.0));
         const double Y = nY * perez(cY, thc, g), x = nx * perez(cx, thc, g), y = std::max(ny * perez(cy, thc, g), 1e-6);
         const double X = x * Y / y, Z = (1.0 - x - y) * Y / y;
         float* o = &px[((size_t)r * w + c) * 3];
         o[0] = (float)std::max(0.0, 3.2406 * X - 1.5372 * Y - 0.4986 * Z); // XYZ to linear Rec.709
         o[1] = (float)std::max(0.0, -0.9689 * X + 1.8758 * Y + 0.0415 * Z);
         o[2] = (float)std::max(0.0, 0.0557 * X - 0.2040 * Y + 1.0570 * Z);
      }
   }
   Upload(px.data(), w, h);
   mHasImage = true;
   mSkyMade[0] = skyElevation; mSkyMade[1] = skyAzimuth; mSkyMade[2] = turbidity;
}

bool EnvironmentNode::Sun(float dir[3], float light[3]) const
{
   if (!mHasImage || !mHasSun || sunThreshold <= 0.0f)
      return false;
   for (int k = 0; k < 3; k++)
   {
      dir[k] = mSunDir[k];
      light[k] = mSunLight[k];
   }
   return true;
}

unsigned int EnvironmentNode::GetEnvironmentTexture()
{
   return mHasImage ? mTex : 0;
}

void EnvironmentNode::CookIfNeeded(int /*frameId*/)
{
   if (source == 1)
   {
      if (!mHasImage || skyElevation != mSkyMade[0] || skyAzimuth != mSkyMade[1] || turbidity != mSkyMade[2] || sunThreshold != mUploadedThreshold)
         MakeSky();
      return;
   }
   if (mSkyMade[0] > -1e8f) // back from the sky to the file (or to nothing)
   {
      mSkyMade[0] = -1e9f; mHasImage = false;
      ReloadFromPath();
   }
   if (!mHasImage)
      EnsurePlaceholder();
   else if (sunThreshold != mUploadedThreshold)
      ReloadFromPath();   // the decode is cached, so this is just the cut and the upload again
}
