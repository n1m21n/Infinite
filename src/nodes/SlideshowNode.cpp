#include "SlideshowNode.h"

#include "platform/OpenGLHeaders.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

#include "ImageTransition.h"
#include "MediaExtensions.h"
#include "Platform.h"
#include "Transport.h"

namespace
{
   const std::vector<std::string> kFitModeNames = {
      "Native Size", "Best Fit", "Proportional Fit"
   };

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
   return ImageTransition::Names();
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
   mProgram = GLUtil::CompileProgram(ImageTransition::kFragSrc);
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

bool SlideshowNode::ResolveFrames(long long ordinal, int direction, int& slotA, int& slotB,
                                  int& indexA, int& indexB)
{
   const size_t maxAttempts = mFiles.size();
   for (size_t attempt = 0; attempt < maxAttempts; ++attempt)
   {
      if (mPlayableIndices.empty())
         return false;

      const int position = PositiveModulo(ordinal, (int)mPlayableIndices.size());
      indexA = mPlayableIndices[position];
      indexB = mPlayableIndices[PositiveModulo((long long)position + (direction < 0 ? -1 : 1),
                                               (int)mPlayableIndices.size())];

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

void SlideshowNode::AutoPhase(double now, double hold, double fade, double step,
                              long long& ordinal, float& progress) const
{
   const double position = (now - mTimeOrigin) / step;
   const long long base = (long long)std::floor(position);
   const double phaseSeconds = (position - (double)base) * step;
   ordinal = base + mOrdinalOffset;
   progress = fade > 0.0 && phaseSeconds > hold ? (float)((phaseSeconds - hold) / fade) : 0.0f;
   progress = std::max(0.0f, std::min(progress, 1.0f));
}

// Turbo 0.50: restart / next / prev. Main thread, inside the cook, so the
// current position is read from the same clock the frame is drawn with.
void SlideshowNode::ApplyRequest(double now, double hold, double fade, double step)
{
   const Request request = mRequest;
   mRequest = Request::None;
   if (request == Request::None)
      return;

   if (request == Request::Restart)
   {
      mManual = false;
      mOrdinalOffset = 0;
      mTimeOrigin = now;
      return;
   }

   // The image on screen: mid-transition, the one we are heading to once
   // past halfway, so a press always moves one image away from what is seen.
   long long current = 0;
   if (mManual)
      current = mManualTo;
   else
   {
      float progress = 0.0f;
      AutoPhase(now, hold, fade, step, current, progress);
      if (progress >= 0.5f)
         ++current;
   }
   const long long target = current + (request == Request::Next ? 1 : -1);
   if (fade <= 0.0)
   {
      mManual = false;
      mOrdinalOffset = target;
      mTimeOrigin = now;
      return;
   }
   mManual = true;
   mManualFrom = current;
   mManualTo = target;
   mManualStart = std::chrono::steady_clock::now();
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
   const double now = Transport::Instance().Seconds();
   // A rewind behind the last restart / step goes back to the plain
   // transport timing (image 1 at 0 s), as before 0.50.
   if (!mManual && now < mTimeOrigin - 1.0e-6)
   {
      mTimeOrigin = 0.0;
      mOrdinalOffset = 0;
   }
   ApplyRequest(now, hold, fade, step);

   long long ordinal = 0;
   int direction = 1;
   float progress = 0.0f;
   if (mManual)
   {
      const double elapsed =
         std::chrono::duration<double>(std::chrono::steady_clock::now() - mManualStart).count();
      if (fade <= 0.0 || elapsed >= fade)
      {
         // Landed: the hold of the new image starts now.
         mManual = false;
         mOrdinalOffset = mManualTo;
         mTimeOrigin = now;
      }
      else
      {
         ordinal = mManualFrom;
         direction = mManualTo < mManualFrom ? -1 : 1;
         progress = (float)std::max(0.0, std::min(elapsed / fade, 1.0));
      }
   }
   if (!mManual)
      AutoPhase(now, hold, fade, step, ordinal, progress);

   int slotA = 0;
   int slotB = 0;
   int indexA = -1;
   int indexB = -1;
   if (!ResolveFrames(ordinal, direction, slotA, slotB, indexA, indexB))
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
      ImageTransition::SetUniforms(mProgram, texA, texB, scaleAX, scaleAY, scaleBX, scaleBY,
                                   sig.progress, sig.transitionType, dstW);
   });

   mBuilt = sig;
   mHasBuilt = true;
   mRevision = NextTextureRevision();
}
