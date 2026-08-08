#include "PaletteNode.h"

#include <OpenGL/gl3.h>
#include <algorithm>
#include <cmath>

#include "Platform.h"
#include "Transport.h"

namespace
{
   const std::vector<std::string> kSortNames = { "Luminance", "Weight", "Hue" };
   const std::vector<std::string> kStripNames = { "Steps", "Smooth" };

   const int kStripW = 512;
   const int kStripH = 128;

   const char* kDownsampleFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uSrc;\n"
      "void main() { fragColor = texture(uSrc, vUv); }\n";

   // sRGB transfer function. The rest of the app stores colours the way the
   // picker shows them, so clustering has to linearise on the way in and
   // re-encode on the way out - averaging sRGB values directly pulls every
   // cluster centre towards the dark end.
   inline float SrgbToLinear(float c)
   {
      return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
   }

   inline float LinearToSrgb(float c)
   {
      c = std::max(0.0f, std::min(c, 1.0f));
      return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
   }

   // Oklab (Björn Ottosson). Chosen over CIELAB because its hue lines stay
   // straight under lightness changes, which is exactly what the shaping
   // controls below do to the extracted centres.
   void LinearToOklab(const float rgb[3], float outLab[3])
   {
      const float l = 0.4122214708f * rgb[0] + 0.5363325363f * rgb[1] + 0.0514459929f * rgb[2];
      const float m = 0.2119034982f * rgb[0] + 0.6806995451f * rgb[1] + 0.1073969566f * rgb[2];
      const float s = 0.0883024619f * rgb[0] + 0.2817188376f * rgb[1] + 0.6299787005f * rgb[2];

      const float l_ = std::cbrt(l), m_ = std::cbrt(m), s_ = std::cbrt(s);

      outLab[0] = 0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_;
      outLab[1] = 1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_;
      outLab[2] = 0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_;
   }

   void OklabToLinear(const float lab[3], float outRgb[3])
   {
      const float l_ = lab[0] + 0.3963377774f * lab[1] + 0.2158037573f * lab[2];
      const float m_ = lab[0] - 0.1055613458f * lab[1] - 0.0638541728f * lab[2];
      const float s_ = lab[0] - 0.0894841775f * lab[1] - 1.2914855480f * lab[2];

      const float l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;

      outRgb[0] = 4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
      outRgb[1] = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
      outRgb[2] = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;
   }

   void LabToDisplay(const float lab[3], float outRgb[3])
   {
      float linear[3];
      OklabToLinear(lab, linear);
      for (int i = 0; i < 3; i++)
         outRgb[i] = LinearToSrgb(linear[i]);
   }

   inline float LabDistance2(const float a[3], const float b[3])
   {
      const float dl = a[0] - b[0], da = a[1] - b[1], db = a[2] - b[2];
      return dl * dl + da * da + db * db;
   }

   // Deterministic, so the same photo and seed always give the same palette.
   // A palette that reshuffles itself every time the node re-cooks would make
   // every downstream binding unreliable.
   struct Rng
   {
      unsigned int state;
      explicit Rng(unsigned int s) : state(s == 0 ? 0x9E3779B9u : s) {}
      unsigned int Next()
      {
         state = state * 1664525u + 1013904223u;
         return state;
      }
      float Unit() { return (float)(Next() >> 8) / (float)(1 << 24); }
   };
}

const std::vector<std::string>& PaletteNode::SortNames() { return kSortNames; }
const std::vector<std::string>& PaletteNode::StripNames() { return kStripNames; }

PaletteNode::PaletteNode()
{
   // A palette that starts blank reads as broken. Seed a neutral ramp so the
   // node is immediately usable as a colour source and any binding made before
   // a reference is loaded still resolves to something.
   for (int i = 0; i < kMaxSwatches; i++)
   {
      const float t = (float)i / (float)(kMaxSwatches - 1);
      mRawLab[i][0] = 0.15f + t * 0.75f;
      mRawLab[i][1] = 0.0f;
      mRawLab[i][2] = 0.0f;
      mRawWeight[i] = 1.0f / (float)kMaxSwatches;
   }
   mActiveCount = swatchCount;
   ApplyShaping();
}

PaletteNode::~PaletteNode()
{
   GLUtil::DestroyFbo(mStrip);
   if (mOwnTex != 0)
      glDeleteTextures(1, &mOwnTex);
   if (mSmallTex != 0)
      glDeleteTextures(1, &mSmallTex);
   if (mSmallFbo != 0)
      glDeleteFramebuffers(1, &mSmallFbo);
   if (mDownsampleProgram != 0)
      glDeleteProgram(mDownsampleProgram);
}

void PaletteNode::GetSwatch(int index, float outRgb[3]) const
{
   const int count = std::max(1, mActiveCount);
   // Wrapped rather than clamped: lowering the swatch count should redistribute
   // the bindings around the smaller palette, not pile them all onto the last
   // colour and flatten the patch.
   const int i = ((index % count) + count) % count;
   outRgb[0] = mSwatch[i][0];
   outRgb[1] = mSwatch[i][1];
   outRgb[2] = mSwatch[i][2];
}

float PaletteNode::SwatchWeight(int index) const
{
   if (index < 0 || index >= mActiveCount)
      return 0.0f;
   return mWeight[index];
}

bool PaletteNode::Load(const std::string& path)
{
   if (path.empty())
   {
      mLastError = "no file chosen";
      return false;
   }

   std::vector<unsigned char> pixels;
   int w = 0, h = 0;
   std::string error;
   if (!Platform::LoadImageRGBA(path, pixels, w, h, error))
   {
      mLastError = error;
      return false;
   }

   if (mOwnTex == 0)
      glGenTextures(1, &mOwnTex);
   glBindTexture(GL_TEXTURE_2D, mOwnTex);
   glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glBindTexture(GL_TEXTURE_2D, 0);

   mOwnW = w;
   mOwnH = h;
   mLoadedPath = path;
   mLastError.clear();
   mForceExtract = true;
   return true;
}

bool PaletteNode::LoadViaDialog()
{
   std::string path = Platform::OpenImageDialog();
   if (path.empty())
      return false; // cancelled - not an error
   return Load(path);
}

unsigned int PaletteNode::SourceTexture(int frameId)
{
   if (mInput.IsConnected())
   {
      const unsigned int tex = mInput.Pull(frameId);
      if (tex != 0)
         return tex;
   }
   return mOwnTex;
}

bool PaletteNode::EnsureDownsample()
{
   if (!mShaderTried)
   {
      mShaderTried = true;
      mDownsampleProgram = GLUtil::CompileProgram(kDownsampleFrag);
   }
   if (mDownsampleProgram == 0)
      return false;

   const int size = std::max(16, std::min(sampleSize, 256));
   if (mSmallSize == size)
      return true;

   if (mSmallTex != 0)
      glDeleteTextures(1, &mSmallTex);
   if (mSmallFbo != 0)
      glDeleteFramebuffers(1, &mSmallFbo);

   glGenTextures(1, &mSmallTex);
   glBindTexture(GL_TEXTURE_2D, mSmallTex);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glGenFramebuffers(1, &mSmallFbo);
   glBindFramebuffer(GL_FRAMEBUFFER, mSmallFbo);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mSmallTex, 0);
   glBindFramebuffer(GL_FRAMEBUFFER, 0);
   glBindTexture(GL_TEXTURE_2D, 0);
   mSmallSize = size;
   return true;
}

void PaletteNode::Extract(unsigned int srcTex)
{
   if (!EnsureDownsample())
      return;

   const int size = mSmallSize;

   // Downscale on the GPU first: reading a 4K frame back would stall the
   // pipeline hard, and a palette does not need the pixels it throws away.
   GLUtil::Fbo wrapper;
   wrapper.fbo = mSmallFbo;
   wrapper.tex = mSmallTex;
   wrapper.w = size;
   wrapper.h = size;

   GLUtil::RunShaderPass(wrapper, mDownsampleProgram, [this, srcTex]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, srcTex);
      glUniform1i(glGetUniformLocation(mDownsampleProgram, "uSrc"), 0);
   });

   mPixels.assign((size_t)size * size * 4, 0);
   GLint prevFbo = 0;
   glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
   glBindFramebuffer(GL_FRAMEBUFFER, mSmallFbo);
   glPixelStorei(GL_PACK_ALIGNMENT, 1);
   glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, mPixels.data());
   glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);

   // --- to Oklab, dropping fully transparent and (optionally) near-neutral ---
   std::vector<float> samples; // flat Lab triples
   std::vector<float> neutralSamples;
   samples.reserve((size_t)size * size * 3);
   const int pixelCount = size * size;
   for (int i = 0; i < pixelCount; i++)
   {
      const size_t p = (size_t)i * 4;
      if (mPixels[p + 3] < 8)
         continue; // a cut-out's transparent surround is not part of its palette

      float linear[3];
      for (int c = 0; c < 3; c++)
         linear[c] = SrgbToLinear(mPixels[p + c] / 255.0f);

      float lab[3];
      LinearToOklab(linear, lab);

      const float chroma = std::sqrt(lab[1] * lab[1] + lab[2] * lab[2]);
      if (!includeNeutrals && chroma < minChroma)
      {
         neutralSamples.push_back(lab[0]);
         neutralSamples.push_back(lab[1]);
         neutralSamples.push_back(lab[2]);
         continue;
      }
      samples.push_back(lab[0]);
      samples.push_back(lab[1]);
      samples.push_back(lab[2]);
   }

   // A genuinely monochrome reference (fog, snow, a black-and-white photo) has
   // no chromatic pixels to cluster. Falling back to the neutrals it does have
   // is far more useful than reporting no palette at all.
   if (samples.size() < 3 * 8)
      samples.insert(samples.end(), neutralSamples.begin(), neutralSamples.end());

   const int n = (int)(samples.size() / 3);
   if (n == 0)
      return;

   const int k = std::max(1, std::min(swatchCount, std::min(kMaxSwatches, n)));

   // --- k-means++ seeding ---
   Rng rng((unsigned int)(std::fabs(seed) * 2654435761.0f) + 1u);
   float centers[kMaxSwatches][3];
   std::vector<float> nearest((size_t)n, 3.4e38f);

   const int first = (int)(rng.Unit() * (float)n) % n;
   for (int c = 0; c < 3; c++)
      centers[0][c] = samples[(size_t)first * 3 + c];

   for (int chosen = 1; chosen < k; chosen++)
   {
      double total = 0.0;
      for (int i = 0; i < n; i++)
      {
         const float d2 = LabDistance2(&samples[(size_t)i * 3], centers[chosen - 1]);
         nearest[i] = std::min(nearest[i], d2);
         total += nearest[i];
      }
      // All remaining points sit on top of an existing centre - the reference
      // has fewer distinct colours than swatches asked for.
      if (total <= 1e-9)
      {
         for (int c = 0; c < 3; c++)
            centers[chosen][c] = centers[chosen - 1][c];
         continue;
      }
      double target = rng.Unit() * total;
      int pick = n - 1;
      for (int i = 0; i < n; i++)
      {
         target -= nearest[i];
         if (target <= 0.0)
         {
            pick = i;
            break;
         }
      }
      for (int c = 0; c < 3; c++)
         centers[chosen][c] = samples[(size_t)pick * 3 + c];
   }

   // --- Lloyd iterations ---
   std::vector<int> assign((size_t)n, 0);
   const int kMaxIterations = 16;
   for (int iter = 0; iter < kMaxIterations; iter++)
   {
      bool moved = false;
      for (int i = 0; i < n; i++)
      {
         int best = 0;
         float bestD = 3.4e38f;
         for (int c = 0; c < k; c++)
         {
            const float d = LabDistance2(&samples[(size_t)i * 3], centers[c]);
            if (d < bestD)
            {
               bestD = d;
               best = c;
            }
         }
         if (assign[i] != best)
         {
            assign[i] = best;
            moved = true;
         }
      }

      double sum[kMaxSwatches][3] = { { 0 } };
      int count[kMaxSwatches] = { 0 };
      for (int i = 0; i < n; i++)
      {
         const int c = assign[i];
         for (int ch = 0; ch < 3; ch++)
            sum[c][ch] += samples[(size_t)i * 3 + ch];
         count[c]++;
      }

      for (int c = 0; c < k; c++)
      {
         if (count[c] > 0)
         {
            for (int ch = 0; ch < 3; ch++)
               centers[c][ch] = (float)(sum[c][ch] / count[c]);
            continue;
         }
         // An empty cluster contributes nothing and leaves a swatch stuck on a
         // stale colour. Re-seed it onto whichever pixel is currently worst
         // served, which is where an extra centre is worth the most.
         int worst = 0;
         float worstD = -1.0f;
         for (int i = 0; i < n; i++)
         {
            const float d = LabDistance2(&samples[(size_t)i * 3], centers[assign[i]]);
            if (d > worstD)
            {
               worstD = d;
               worst = i;
            }
         }
         for (int ch = 0; ch < 3; ch++)
            centers[c][ch] = samples[(size_t)worst * 3 + ch];
         moved = true;
      }

      if (!moved && iter > 0)
         break;
   }

   int finalCount[kMaxSwatches] = { 0 };
   for (int i = 0; i < n; i++)
      finalCount[assign[i]]++;

   for (int c = 0; c < k; c++)
   {
      for (int ch = 0; ch < 3; ch++)
         mRawLab[c][ch] = centers[c][ch];
      mRawWeight[c] = (float)finalCount[c] / (float)n;
   }

   mActiveCount = k;
   mHasPalette = true;
   // Force the shaping pass: the centres moved even if no shaping control did.
   mShapedSort = -1;
}

void PaletteNode::ApplyShaping()
{
   const int k = std::max(1, std::min(mActiveCount, kMaxSwatches));

   float lab[kMaxSwatches][3];
   float weight[kMaxSwatches];

   float meanL = 0.0f;
   for (int i = 0; i < k; i++)
      meanL += mRawLab[i][0];
   meanL /= (float)k;

   const float hueRadians = hueShift * 6.28318530718f;
   const float cosH = std::cos(hueRadians), sinH = std::sin(hueRadians);

   for (int i = 0; i < k; i++)
   {
      // Hue is a rotation of the a/b plane and chroma a scale of it, which is
      // the whole reason for working in Oklab: both are one operation here and
      // neither disturbs perceived lightness.
      const float a = mRawLab[i][1], b = mRawLab[i][2];
      lab[i][1] = (a * cosH - b * sinH) * saturation;
      lab[i][2] = (a * sinH + b * cosH) * saturation;
      lab[i][0] = std::max(0.0f, std::min(meanL + (mRawLab[i][0] - meanL) * spread + brightness, 1.0f));
      weight[i] = mRawWeight[i];
   }

   int order[kMaxSwatches];
   for (int i = 0; i < k; i++)
      order[i] = i;

   auto keyFor = [&](int i) -> float
   {
      if (sortMode == kSortWeight)
         return -weight[i]; // dominant first
      if (sortMode == kSortHue)
         return std::atan2(lab[i][2], lab[i][1]);
      return lab[i][0]; // dark to light
   };

   std::stable_sort(order, order + k, [&](int a, int b) { return keyFor(a) < keyFor(b); });

   for (int i = 0; i < k; i++)
   {
      LabToDisplay(lab[order[i]], mSwatch[i]);
      mWeight[i] = weight[order[i]];
   }

   mShapedHue = hueShift;
   mShapedSat = saturation;
   mShapedBright = brightness;
   mShapedSpread = spread;
   mShapedSort = sortMode;
}

void PaletteNode::RebuildStrip()
{
   if (!GLUtil::EnsureFbo(mStrip, kStripW, kStripH))
      return;

   const int k = std::max(1, mActiveCount);
   std::vector<unsigned char> row((size_t)kStripW * 4);
   for (int x = 0; x < kStripW; x++)
   {
      const float t = (float)x / (float)(kStripW - 1);
      float rgb[3];
      if (stripMode == kStripSmooth && k > 1)
      {
         const float f = t * (float)(k - 1);
         const int i = std::min((int)f, k - 2);
         const float frac = f - (float)i;
         for (int c = 0; c < 3; c++)
            rgb[c] = mSwatch[i][c] + (mSwatch[i + 1][c] - mSwatch[i][c]) * frac;
      }
      else
      {
         const int i = std::min((int)(t * (float)k), k - 1);
         for (int c = 0; c < 3; c++)
            rgb[c] = mSwatch[i][c];
      }
      for (int c = 0; c < 3; c++)
         row[(size_t)x * 4 + c] = (unsigned char)(std::max(0.0f, std::min(rgb[c], 1.0f)) * 255.0f + 0.5f);
      row[(size_t)x * 4 + 3] = 255;
   }

   std::vector<unsigned char> image((size_t)kStripW * kStripH * 4);
   for (int y = 0; y < kStripH; y++)
      std::copy(row.begin(), row.end(), image.begin() + (size_t)y * kStripW * 4);

   glBindTexture(GL_TEXTURE_2D, mStrip.tex);
   glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
   glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kStripW, kStripH, GL_RGBA, GL_UNSIGNED_BYTE, image.data());
   glBindTexture(GL_TEXTURE_2D, 0);

   mShapedStrip = stripMode;
}

void PaletteNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   const unsigned int srcTex = SourceTexture(frameId);

   // Clustering is expensive next to everything else a node does per frame, so
   // it re-runs only when one of its own inputs actually moved. Shaping and
   // sorting are cheap and are checked separately, which is what lets the
   // saturation slider feel live without re-clustering the photo behind it.
   const bool inputsChanged =
      srcTex != mExtractedFrom ||
      swatchCount != mExtractedCount ||
      minChroma != mExtractedChroma ||
      includeNeutrals != mExtractedNeutrals ||
      seed != mExtractedSeed ||
      sampleSize != mExtractedSize;

   bool wantExtract = mForceExtract || inputsChanged;
   if (live && srcTex != 0)
   {
      const double now = Transport::Instance().Seconds();
      const double interval = 1.0 / std::max(1.0f, sampleRate);
      if (mLastSampleSeconds < 0.0 || now < mLastSampleSeconds ||
          now - mLastSampleSeconds >= interval)
      {
         wantExtract = true;
         mLastSampleSeconds = now;
      }
   }

   if (wantExtract && srcTex != 0)
   {
      Extract(srcTex);
      mForceExtract = false;
      mExtractedFrom = srcTex;
      mExtractedCount = swatchCount;
      mExtractedChroma = minChroma;
      mExtractedNeutrals = includeNeutrals;
      mExtractedSeed = seed;
      mExtractedSize = sampleSize;
   }
   else if (srcTex == 0 && swatchCount != mActiveCount && !mHasPalette)
   {
      // No reference yet: still honour the swatch count so the built-in neutral
      // ramp matches what the destination expects.
      mActiveCount = std::max(1, std::min(swatchCount, kMaxSwatches));
      mShapedSort = -1;
   }

   if (hueShift != mShapedHue || saturation != mShapedSat || brightness != mShapedBright ||
       spread != mShapedSpread || sortMode != mShapedSort)
   {
      ApplyShaping();
      mShapedStrip = -1; // the strip is built from the shaped swatches
   }

   if (stripMode != mShapedStrip)
      RebuildStrip();
}
