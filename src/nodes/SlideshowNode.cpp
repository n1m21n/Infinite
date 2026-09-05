#include "SlideshowNode.h"

#include "gl3.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

#include "MediaExtensions.h"
#include "Platform.h"
#include "Transport.h"

namespace
{
   const std::vector<std::string> kFitModeNames = {
      "Native Size", "Best Fit", "Proportional Fit"
   };

   const std::vector<std::string> kTransitionNames = {
      "Fade", "Slide Left", "Slide Right", "Wipe Left", "Wipe Right", "Zoom Fade"
   };

   const char* kFragSrc =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uA;\n"
      "uniform sampler2D uB;\n"
      "uniform vec2 uScaleA;\n"
      "uniform vec2 uScaleB;\n"
      "uniform float uProgress;\n"
      "uniform float uWipeFeather;\n"
      "uniform int uTransition;\n"
      "vec4 sampleFrame(sampler2D tex, vec2 screenUv, vec2 scale) {\n"
      "   if (screenUv.x < 0.0 || screenUv.x > 1.0 || screenUv.y < 0.0 || screenUv.y > 1.0)\n"
      "      return vec4(0.0);\n"
      "   vec2 uv = (screenUv - 0.5) * scale + 0.5;\n"
      "   if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)\n"
      "      return vec4(0.0);\n"
      "   return texture(tex, uv);\n"
      "}\n"
      "vec4 over(vec4 back, vec4 front) {\n"
      "   float alpha = front.a + back.a * (1.0 - front.a);\n"
      "   if (alpha <= 0.00001) return vec4(0.0);\n"
      "   vec3 rgb = (front.rgb * front.a + back.rgb * back.a * (1.0 - front.a)) / alpha;\n"
      "   return vec4(rgb, alpha);\n"
      "}\n"
      "void main() {\n"
      "   float t = smoothstep(0.0, 1.0, uProgress);\n"
      "   vec4 a;\n"
      "   vec4 b;\n"
      "   if (uTransition == 1) {\n"
      "      a = sampleFrame(uA, vUv + vec2(t, 0.0), uScaleA);\n"
      "      b = sampleFrame(uB, vUv - vec2(1.0 - t, 0.0), uScaleB);\n"
      "      fragColor = over(a, b);\n"
      "   } else if (uTransition == 2) {\n"
      "      a = sampleFrame(uA, vUv - vec2(t, 0.0), uScaleA);\n"
      "      b = sampleFrame(uB, vUv + vec2(1.0 - t, 0.0), uScaleB);\n"
      "      fragColor = over(a, b);\n"
      "   } else if (uTransition == 3) {\n"
      "      a = sampleFrame(uA, vUv, uScaleA);\n"
      "      b = sampleFrame(uB, vUv, uScaleB);\n"
      "      float mask = 1.0 - smoothstep(t - uWipeFeather, t + uWipeFeather, vUv.x);\n"
      "      fragColor = mix(a, b, mask);\n"
      "   } else if (uTransition == 4) {\n"
      "      a = sampleFrame(uA, vUv, uScaleA);\n"
      "      b = sampleFrame(uB, vUv, uScaleB);\n"
      "      float edge = 1.0 - t;\n"
      "      float mask = smoothstep(edge - uWipeFeather, edge + uWipeFeather, vUv.x);\n"
      "      fragColor = mix(a, b, mask);\n"
      "   } else if (uTransition == 5) {\n"
      "      vec2 uvA = (vUv - 0.5) * (1.0 - 0.12 * t) + 0.5;\n"
      "      vec2 uvB = (vUv - 0.5) * (1.12 - 0.12 * t) + 0.5;\n"
      "      a = sampleFrame(uA, uvA, uScaleA);\n"
      "      b = sampleFrame(uB, uvB, uScaleB);\n"
      "      fragColor = mix(a, b, t);\n"
      "   } else {\n"
      "      a = sampleFrame(uA, vUv, uScaleA);\n"
      "      b = sampleFrame(uB, vUv, uScaleB);\n"
      "      fragColor = mix(a, b, t);\n"
      "   }\n"
      "}\n";

   std::string Lower(std::string value)
   {
      std::transform(value.begin(), value.end(), value.begin(),
                     [](unsigned char c) { return (char)std::tolower(c); });
      return value;
   }

   bool IsSupportedImage(const std::filesystem::path& path)
   {
      std::string ext = Lower(path.extension().string());
      if (!ext.empty() && ext[0] == '.')
         ext.erase(0, 1);
      const auto& supported = MediaExtensions::Image();
      return std::find(supported.begin(), supported.end(), ext) != supported.end();
   }

   int PositiveModulo(long long value, int modulus)
   {
      const long long result = value % modulus;
      return (int)(result < 0 ? result + modulus : result);
   }

   void FitScale(int mode, int srcW, int srcH, int dstW, int dstH, float& scaleX, float& scaleY)
   {
      scaleX = 1.0f;
      scaleY = 1.0f;
      if (mode == (int)SlideshowNode::FitMode::Native)
      {
         scaleX = (float)dstW / (float)std::max(1, srcW);
         scaleY = (float)dstH / (float)std::max(1, srcH);
      }
      else if (mode == (int)SlideshowNode::FitMode::ProportionalFit)
      {
         const float srcAspect = (float)std::max(1, srcW) / (float)std::max(1, srcH);
         const float dstAspect = (float)dstW / (float)dstH;
         if (dstAspect > srcAspect)
            scaleX = dstAspect / srcAspect;
         else
            scaleY = srcAspect / dstAspect;
      }
      // Best Fit deliberately leaves both scales at 1:1, stretching the whole
      // source into the node's output resolution without cropping or bars.
   }
}

const std::vector<std::string>& SlideshowNode::FitModeNames()
{
   return kFitModeNames;
}

const std::vector<std::string>& SlideshowNode::TransitionNames()
{
   return kTransitionNames;
}

bool SlideshowNode::Signature::operator==(const Signature& other) const
{
   return folderGeneration == other.folderGeneration &&
          imageA == other.imageA && imageB == other.imageB &&
          sourceWidthA == other.sourceWidthA && sourceHeightA == other.sourceHeightA &&
          sourceWidthB == other.sourceWidthB && sourceHeightB == other.sourceHeightB &&
          outputWidth == other.outputWidth && outputHeight == other.outputHeight &&
          fit == other.fit && transitionType == other.transitionType &&
          progress == other.progress;
}

SlideshowNode::~SlideshowNode()
{
   GLUtil::DestroyFbo(mOut);
   if (mProgram != 0)
      glDeleteProgram(mProgram);
}

bool SlideshowNode::EnsureShader()
{
   if (mShaderTried)
      return mProgram != 0;
   mShaderTried = true;
   mProgram = GLUtil::CompileProgram(kFragSrc);
   return mProgram != 0;
}

bool SlideshowNode::LoadFolder(const std::string& path)
{
   if (path.empty())
   {
      mLastError = "no folder chosen";
      return false;
   }

   namespace fs = std::filesystem;
   const fs::path root = fs::u8path(path);
   std::error_code ec;
   if (!fs::is_directory(root, ec) || ec)
   {
      mLastError = "folder is not available";
      return false;
   }

   std::vector<std::string> found;
   fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
   const fs::directory_iterator end;
   if (ec)
   {
      mLastError = "could not read folder";
      return false;
   }

   while (it != end)
   {
      std::error_code entryError;
      if (it->is_regular_file(entryError) && !entryError && IsSupportedImage(it->path()))
         found.push_back(it->path().u8string());
      it.increment(ec);
      if (ec)
      {
         mLastError = "could not finish reading folder";
         return false;
      }
   }

   std::sort(found.begin(), found.end(), [](const std::string& a, const std::string& b)
   {
      return Lower(std::filesystem::u8path(a).filename().u8string()) <
             Lower(std::filesystem::u8path(b).filename().u8string());
   });

   if (found.empty())
   {
      mLastError = "folder contains no supported images";
      return false;
   }

   mFolderPath = path;
   mFiles = std::move(found);
   mPlayableIndices.clear();
   mPlayableIndices.reserve(mFiles.size());
   for (int i = 0; i < (int)mFiles.size(); ++i)
      mPlayableIndices.push_back(i);
   mLoadedIndices[0] = -1;
   mLoadedIndices[1] = -1;
   mCurrentIndex = -1;
   mLastError.clear();
   mHasBuilt = false;
   ++mFolderGeneration;
   return true;
}

bool SlideshowNode::LoadViaDialog()
{
   const std::string path = Platform::OpenFolderDialog("Choose slideshow folder", mFolderPath);
   return path.empty() ? false : LoadFolder(path);
}

void SlideshowNode::ReloadFromFolder()
{
   if (!mFolderPath.empty())
   {
      const std::string path = mFolderPath;
      LoadFolder(path);
   }
}

int SlideshowNode::CurrentImageNumber() const
{
   return mCurrentIndex >= 0 ? mCurrentIndex + 1 : 0;
}

std::string SlideshowNode::CurrentFileName() const
{
   if (mCurrentIndex < 0 || mCurrentIndex >= (int)mFiles.size())
      return std::string();
   return std::filesystem::u8path(mFiles[mCurrentIndex]).filename().u8string();
}

bool SlideshowNode::LoadSlot(int slot, int fileIndex)
{
   if (slot < 0 || slot > 1 || fileIndex < 0 || fileIndex >= (int)mFiles.size())
      return false;
   if (mLoadedIndices[slot] == fileIndex)
      return true;

   if (!mSources[slot].Load(mFiles[fileIndex]))
   {
      mPlayableIndices.erase(std::remove(mPlayableIndices.begin(), mPlayableIndices.end(), fileIndex),
                             mPlayableIndices.end());
      mLastError = "skipped " + std::filesystem::u8path(mFiles[fileIndex]).filename().u8string() +
                   ": " + mSources[slot].LastError();
      return false;
   }

   mLoadedIndices[slot] = fileIndex;
   return true;
}

bool SlideshowNode::ResolveFrames(long long ordinal, int& slotA, int& slotB,
                                  int& indexA, int& indexB)
{
   const size_t maxAttempts = mFiles.size();
   for (size_t attempt = 0; attempt < maxAttempts; ++attempt)
   {
      if (mPlayableIndices.empty())
         return false;

      const int position = PositiveModulo(ordinal, (int)mPlayableIndices.size());
      indexA = mPlayableIndices[position];
      indexB = mPlayableIndices[(position + 1) % mPlayableIndices.size()];

      slotA = mLoadedIndices[0] == indexA ? 0 : (mLoadedIndices[1] == indexA ? 1 : -1);
      if (slotA < 0)
      {
         slotA = mLoadedIndices[0] == indexB ? 1 : 0;
         if (!LoadSlot(slotA, indexA))
            continue;
      }

      if (indexB == indexA)
      {
         slotB = slotA;
         return true;
      }

      slotB = mLoadedIndices[0] == indexB ? 0 : (mLoadedIndices[1] == indexB ? 1 : -1);
      if (slotB < 0)
      {
         slotB = 1 - slotA;
         if (!LoadSlot(slotB, indexB))
            continue;
      }
      return true;
   }
   return false;
}

void SlideshowNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   const int dstW = std::max(4, (int)width);
   const int dstH = std::max(4, (int)height);
   const double hold = std::max(0.05f, holdDuration);
   const double fade = std::max(0.0f, transitionDuration);
   const double step = hold + fade;
   const double position = Transport::Instance().Seconds() / step;
   const long long ordinal = (long long)std::floor(position);
   const double phaseSeconds = (position - (double)ordinal) * step;
   float progress = fade > 0.0 && phaseSeconds > hold
                       ? (float)((phaseSeconds - hold) / fade)
                       : 0.0f;
   progress = std::max(0.0f, std::min(progress, 1.0f));

   int slotA = 0;
   int slotB = 0;
   int indexA = -1;
   int indexB = -1;
   if (!ResolveFrames(ordinal, slotA, slotB, indexA, indexB))
   {
      // A fresh/failed node still renders the same visible checker used by
      // Image Source, while LastError explains why no folder image is shown.
      mSources[0].GetOutputTexture();
      slotA = slotB = 0;
      progress = 0.0f;
   }
   mCurrentIndex = indexA;

   const unsigned int texA = mSources[slotA].GetOutputTexture();
   const unsigned int texB = mSources[slotB].GetOutputTexture();
   const int srcWA = std::max(1, mSources[slotA].GetOutputWidth());
   const int srcHA = std::max(1, mSources[slotA].GetOutputHeight());
   const int srcWB = std::max(1, mSources[slotB].GetOutputWidth());
   const int srcHB = std::max(1, mSources[slotB].GetOutputHeight());

   if (!EnsureShader())
      return;
   if (!GLUtil::EnsureFbo(mOut, dstW, dstH))
      return;

   Signature sig;
   sig.folderGeneration = mFolderGeneration;
   sig.imageA = indexA;
   sig.imageB = indexB;
   sig.sourceWidthA = srcWA;
   sig.sourceHeightA = srcHA;
   sig.sourceWidthB = srcWB;
   sig.sourceHeightB = srcHB;
   sig.outputWidth = dstW;
   sig.outputHeight = dstH;
   sig.fit = std::max(0, std::min(fitMode, (int)FitMode::ProportionalFit));
   sig.transitionType = std::max(0, std::min(transition, (int)Transition::ZoomFade));
   sig.progress = (indexA == indexB) ? 0.0f : progress;
   if (mHasBuilt && sig == mBuilt)
      return;

   float scaleAX = 1.0f;
   float scaleAY = 1.0f;
   float scaleBX = 1.0f;
   float scaleBY = 1.0f;
   FitScale(sig.fit, srcWA, srcHA, dstW, dstH, scaleAX, scaleAY);
   FitScale(sig.fit, srcWB, srcHB, dstW, dstH, scaleBX, scaleBY);

   NodeWorkCounter()++;
   GLUtil::RunShaderPass(mOut, mProgram,
      [this, texA, texB, scaleAX, scaleAY, scaleBX, scaleBY, sig, dstW]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, texA);
      glUniform1i(glGetUniformLocation(mProgram, "uA"), 0);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, texB);
      glUniform1i(glGetUniformLocation(mProgram, "uB"), 1);
      glUniform2f(glGetUniformLocation(mProgram, "uScaleA"), scaleAX, scaleAY);
      glUniform2f(glGetUniformLocation(mProgram, "uScaleB"), scaleBX, scaleBY);
      glUniform1f(glGetUniformLocation(mProgram, "uProgress"), sig.progress);
      glUniform1f(glGetUniformLocation(mProgram, "uWipeFeather"), 2.0f / (float)dstW);
      glUniform1i(glGetUniformLocation(mProgram, "uTransition"), sig.transitionType);
   });

   mBuilt = sig;
   mHasBuilt = true;
   mRevision = NextTextureRevision();
}
