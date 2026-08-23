#include "AnalyzeNodes.h"

#if defined(__APPLE__)
#include <Accelerate/Accelerate.h>
#else
#include "dsp/PortableFft.h"
#endif
#include "gl3.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

#include "GLUtil.h"
#include "Transport.h"
#include "core/Expression.h"
#include "core/AudioTopologyRequest.h"
#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/ParamMailbox.h"
#include "audio/SampleSlot.h"

// =========================================================== Image Analyze

namespace
{
   const char* kDownsampleFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uSrc;\n"
      "void main() { fragColor = texture(uSrc, vUv); }\n";

   const char* kImageOutputNames[] = {
      "result", "bright", "contrast", "red", "green", "blue", "sat", "hue", "motion", "cx", "cy"
   };

   const std::vector<std::string> kSampleModeNames = {
      "Global Average", "Point Probe", "Region of Interest", "Center Weighted"
   };

   const std::vector<std::string> kMathOpNames = {
      "Custom Expression",
      "Red (R)",
      "Green (G)",
      "Blue (B)",
      "Alpha (A)",
      "Luminance (0.299R+0.587G+0.114B)",
      "Average ((R+G+B)/3)",
      "Sum (R+G+B)",
      "Product (R*G*B)",
      "Max (max(R,G,B))",
      "Min (min(R,G,B))",
      "Range (Max - Min)",
      "R - G",
      "R - B",
      "G - B",
      "|R - G| (Red/Green Diff)",
      "|R - B| (Red/Blue Diff)",
      "|G - B| (Green/Blue Diff)",
      "Saturation",
      "Hue",
      "Euclidean Norm",
      "Delta / Motion"
   };
}

const std::vector<std::string>& ImageAnalyzeNode::SampleModeNames()
{
   return kSampleModeNames;
}

const std::vector<std::string>& ImageAnalyzeNode::MathOpNames()
{
   return kMathOpNames;
}

ImageAnalyzeNode::ImageAnalyzeNode()
{
   for (int i = 0; i < kOutputCount; i++)
   {
      mTaps[i].owner = this;
      mTaps[i].index = i;
   }
}

ImageAnalyzeNode::~ImageAnalyzeNode()
{
   if (mSmallTex != 0)
      glDeleteTextures(1, &mSmallTex);
   if (mSmallFbo != 0)
      glDeleteFramebuffers(1, &mSmallFbo);
   if (mProgram != 0)
      glDeleteProgram(mProgram);
}

const char* ImageAnalyzeNode::OutputLabel(int index) const
{
   if (index < 0 || index >= kOutputCount)
      return "out";
   return kImageOutputNames[index];
}

IModulator* ImageAnalyzeNode::ModulatorOutput(int index)
{
   if (index < 0 || index >= kOutputCount)
      return nullptr;
   return &mTaps[index];
}

float ImageAnalyzeNode::Value(int index) const
{
   if (index < 0 || index >= kOutputCount)
      return 0.0f;
   return mValues[index];
}

float ImageAnalyzeNode::ComputeMathResult(float r, float g, float b, float a, float lum, float delta)
{
   float result = 0.0f;
   const float maxRGB = std::max(r, std::max(g, b));
   const float minRGB = std::min(r, std::min(g, b));
   const float sat = (maxRGB > 1e-5f) ? (maxRGB - minRGB) / maxRGB : 0.0f;

   float hue = 0.0f;
   if (maxRGB - minRGB > 1e-5f)
   {
      if (maxRGB == r)
      {
         hue = (g - b) / (maxRGB - minRGB);
         if (hue < 0.0f) hue += 6.0f;
      }
      else if (maxRGB == g)
      {
         hue = ((b - r) / (maxRGB - minRGB)) + 2.0f;
      }
      else
      {
         hue = ((r - g) / (maxRGB - minRGB)) + 4.0f;
      }
      hue /= 6.0f;
   }

   switch (mathOp)
   {
      case kCustomExpression:
      {
         std::string formula = customFormula;
         while (!formula.empty() && formula.front() == '=')
            formula.erase(formula.begin());

         std::map<std::string, float> vars;
         vars["r"] = r;
         vars["red"] = r;
         vars["g"] = g;
         vars["green"] = g;
         vars["b"] = b;
         vars["blue"] = b;
         vars["a"] = a;
         vars["alpha"] = a;
         vars["lum"] = lum;
         vars["l"] = lum;
         vars["bright"] = lum;
         vars["sat"] = sat;
         vars["s"] = sat;
         vars["hue"] = hue;
         vars["h"] = hue;
         vars["max"] = maxRGB;
         vars["min"] = minRGB;
         vars["delta"] = delta;
         vars["motion"] = delta;
         vars["u"] = probeU;
         vars["v"] = probeV;

         float evalVal = 0.0f;
         std::string evalErr;
         const double t = Transport::Instance().Seconds();
         if (Expression::Evaluate(formula, t, &vars, nullptr, evalVal, evalErr))
         {
            result = evalVal;
            mExprError.clear();
         }
         else
         {
            mExprError = evalErr;
            result = lum;
         }
         break;
      }
      case kRedOp: result = r; break;
      case kGreenOp: result = g; break;
      case kBlueOp: result = b; break;
      case kAlphaOp: result = a; break;
      case kLuminanceOp: result = lum; break;
      case kAverageOp: result = (r + g + b) / 3.0f; break;
      case kSumOp: result = (r + g + b) / 3.0f; break;
      case kProductOp: result = r * g * b; break;
      case kMaxOp: result = maxRGB; break;
      case kMinOp: result = minRGB; break;
      case kRangeOp: result = maxRGB - minRGB; break;
      case kRMinusG: result = (r - g + 1.0f) * 0.5f; break;
      case kRMinusB: result = (r - b + 1.0f) * 0.5f; break;
      case kGMinusB: result = (g - b + 1.0f) * 0.5f; break;
      case kAbsRMinusG: result = std::fabs(r - g); break;
      case kAbsRMinusB: result = std::fabs(r - b); break;
      case kAbsGMinusB: result = std::fabs(g - b); break;
      case kSaturationOp: result = sat; break;
      case kHueOp: result = hue; break;
      case kEuclideanNorm: result = std::sqrt(r * r + g * g + b * b) / 1.7320508f; break;
      case kDeltaMotion: result = delta; break;
      default: result = lum; break;
   }

   result = result * gain + offset;

   if (power > 0.001f && std::fabs(power - 1.0f) > 0.001f)
   {
      const float sign = result < 0.0f ? -1.0f : 1.0f;
      result = sign * std::pow(std::fabs(result), power);
   }

   if (invert)
      result = 1.0f - result;

   if (clamp01)
      result = std::clamp(result, 0.0f, 1.0f);

   return result;
}

void ImageAnalyzeNode::Analyze()
{
   const int size = std::clamp(sampleSize, 8, 256);

   if (mSmallSize != size)
   {
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
      mPrevPixels.clear();
   }

   // Downscale on the GPU first: reading a 4K frame back every sample would
   // stall the pipeline hard.
   GLUtil::Fbo wrapper;
   wrapper.fbo = mSmallFbo;
   wrapper.tex = mSmallTex;
   wrapper.w = size;
   wrapper.h = size;

   const unsigned int srcTex = mInput.GetSource() ? mInput.GetSource()->GetOutputTexture() : 0;
   if (srcTex == 0)
      return;

   GLUtil::RunShaderPass(wrapper, mProgram, [this, srcTex]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, srcTex);
      glUniform1i(glGetUniformLocation(mProgram, "uSrc"), 0);
   });

   mPixels.assign((size_t)size * size * 4, 0);
   GLint prevFbo = 0;
   glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
   glBindFramebuffer(GL_FRAMEBUFFER, mSmallFbo);
   glPixelStorei(GL_PACK_ALIGNMENT, 1);
   glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, mPixels.data());
   glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);

   const bool haveHistory = mPrevPixels.size() == mPixels.size();

   double sumR = 0, sumG = 0, sumB = 0, sumA = 0, sumLum = 0, sumLum2 = 0, sumSat = 0;
   double weightedX = 0, weightedY = 0, weightTotal = 0, motion = 0;
   double sampleWeightTotal = 0.0;
   const int count = size * size;

   const float pu = std::clamp(probeU, 0.0f, 1.0f);
   const float pv = std::clamp(probeV, 0.0f, 1.0f);
   const float pr = std::clamp(probeRadius, 0.01f, 1.0f);

   if (sampleMode == kPointProbe)
   {
      const int cx = std::clamp((int)(pu * (size - 1)), 0, size - 1);
      const int cy = std::clamp((int)(pv * (size - 1)), 0, size - 1);

      for (int dy = -1; dy <= 1; dy++)
      {
         for (int dx = -1; dx <= 1; dx++)
         {
            const int px = std::clamp(cx + dx, 0, size - 1);
            const int py = std::clamp(cy + dy, 0, size - 1);
            const size_t i = ((size_t)py * size + px) * 4;

            const double w = (dx == 0 && dy == 0) ? 2.0 : 1.0;
            const double r = mPixels[i] / 255.0;
            const double g = mPixels[i + 1] / 255.0;
            const double b = mPixels[i + 2] / 255.0;
            const double a = mPixels[i + 3] / 255.0;
            const double lum = 0.299 * r + 0.587 * g + 0.114 * b;

            sumR += r * w; sumG += g * w; sumB += b * w; sumA += a * w;
            sumLum += lum * w;
            sumLum2 += lum * lum * w;

            const double mx = std::max(r, std::max(g, b));
            const double mn = std::min(r, std::min(g, b));
            sumSat += ((mx > 1e-5) ? (mx - mn) / mx : 0.0) * w;

            sampleWeightTotal += w;

            if (haveHistory)
            {
               const double prevR = mPrevPixels[i] / 255.0;
               const double prevG = mPrevPixels[i + 1] / 255.0;
               const double prevB = mPrevPixels[i + 2] / 255.0;
               motion += (std::fabs(r - prevR) + std::fabs(g - prevG) + std::fabs(b - prevB)) * (w / 3.0);
            }
         }
      }
   }
   else if (sampleMode == kBoxRegion)
   {
      const int minX = std::clamp((int)((pu - pr) * size), 0, size - 1);
      const int maxX = std::clamp((int)((pu + pr) * size), 0, size - 1);
      const int minY = std::clamp((int)((pv - pr) * size), 0, size - 1);
      const int maxY = std::clamp((int)((pv + pr) * size), 0, size - 1);

      for (int y = minY; y <= maxY; y++)
      {
         for (int x = minX; x <= maxX; x++)
         {
            const size_t i = ((size_t)y * size + x) * 4;
            const double r = mPixels[i] / 255.0;
            const double g = mPixels[i + 1] / 255.0;
            const double b = mPixels[i + 2] / 255.0;
            const double a = mPixels[i + 3] / 255.0;
            const double lum = 0.299 * r + 0.587 * g + 0.114 * b;

            sumR += r; sumG += g; sumB += b; sumA += a;
            sumLum += lum;
            sumLum2 += lum * lum;

            const double mx = std::max(r, std::max(g, b));
            const double mn = std::min(r, std::min(g, b));
            sumSat += (mx > 1e-5) ? (mx - mn) / mx : 0.0;

            sampleWeightTotal += 1.0;

            if (haveHistory)
            {
               const double prevR = mPrevPixels[i] / 255.0;
               const double prevG = mPrevPixels[i + 1] / 255.0;
               const double prevB = mPrevPixels[i + 2] / 255.0;
               motion += (std::fabs(r - prevR) + std::fabs(g - prevG) + std::fabs(b - prevB)) / 3.0;
            }
         }
      }
   }
   else if (sampleMode == kCenterWeighted)
   {
      const double centerU = (double)pu;
      const double centerV = (double)pv;

      for (int y = 0; y < size; y++)
      {
         const double v = (double)y / (size - 1);
         const double dv = v - centerV;
         for (int x = 0; x < size; x++)
         {
            const double u = (double)x / (size - 1);
            const double du = u - centerU;
            const double distSq = du * du + dv * dv;
            const double w = std::exp(-distSq * 8.0);

            const size_t i = ((size_t)y * size + x) * 4;
            const double r = mPixels[i] / 255.0;
            const double g = mPixels[i + 1] / 255.0;
            const double b = mPixels[i + 2] / 255.0;
            const double a = mPixels[i + 3] / 255.0;
            const double lum = 0.299 * r + 0.587 * g + 0.114 * b;

            sumR += r * w; sumG += g * w; sumB += b * w; sumA += a * w;
            sumLum += lum * w;
            sumLum2 += lum * lum * w;

            const double mx = std::max(r, std::max(g, b));
            const double mn = std::min(r, std::min(g, b));
            sumSat += ((mx > 1e-5) ? (mx - mn) / mx : 0.0) * w;

            sampleWeightTotal += w;

            if (haveHistory)
            {
               const double prevR = mPrevPixels[i] / 255.0;
               const double prevG = mPrevPixels[i + 1] / 255.0;
               const double prevB = mPrevPixels[i + 2] / 255.0;
               motion += (std::fabs(r - prevR) + std::fabs(g - prevG) + std::fabs(b - prevB)) * (w / 3.0);
            }
         }
      }
   }
   else // kGlobalAverage
   {
      for (int y = 0; y < size; y++)
      {
         for (int x = 0; x < size; x++)
         {
            const size_t i = ((size_t)y * size + x) * 4;
            const double r = mPixels[i] / 255.0;
            const double g = mPixels[i + 1] / 255.0;
            const double b = mPixels[i + 2] / 255.0;
            const double a = mPixels[i + 3] / 255.0;
            const double lum = 0.299 * r + 0.587 * g + 0.114 * b;

            sumR += r; sumG += g; sumB += b; sumA += a;
            sumLum += lum;
            sumLum2 += lum * lum;

            const double mx = std::max(r, std::max(g, b));
            const double mn = std::min(r, std::min(g, b));
            sumSat += (mx > 1e-5) ? (mx - mn) / mx : 0.0;

            weightedX += x * lum;
            weightedY += y * lum;
            weightTotal += lum;

            sampleWeightTotal += 1.0;

            if (haveHistory)
               motion += std::fabs(lum - (0.299 * mPrevPixels[i] + 0.587 * mPrevPixels[i + 1] + 0.114 * mPrevPixels[i + 2]) / 255.0);
         }
      }
   }

   mPrevPixels = mPixels;

   const double invWeight = (sampleWeightTotal > 1e-6) ? (1.0 / sampleWeightTotal) : 1.0;
   mRawR = (float)(sumR * invWeight);
   mRawG = (float)(sumG * invWeight);
   mRawB = (float)(sumB * invWeight);
   mRawA = (float)(sumA * invWeight);
   mRawLum = (float)(sumLum * invWeight);
   mRawDelta = haveHistory ? (float)std::clamp(motion * invWeight * (sampleMode == kGlobalAverage ? 8.0 : 6.0), 0.0, 1.0) : 0.0f;

   const double meanLum = mRawLum;
   const double variance = std::max(0.0, sumLum2 * invWeight - meanLum * meanLum);

   const float maxRGB = std::max(mRawR, std::max(mRawG, mRawB));
   const float minRGB = std::min(mRawR, std::min(mRawG, mRawB));
   float rawHue = 0.0f;
   if (maxRGB - minRGB > 1e-5f)
   {
      if (maxRGB == mRawR)
      {
         rawHue = (mRawG - mRawB) / (maxRGB - minRGB);
         if (rawHue < 0.0f) rawHue += 6.0f;
      }
      else if (maxRGB == mRawG)
      {
         rawHue = ((mRawB - mRawR) / (maxRGB - minRGB)) + 2.0f;
      }
      else
      {
         rawHue = ((mRawR - mRawG) / (maxRGB - minRGB)) + 4.0f;
      }
      rawHue /= 6.0f;
   }

   const float mathRes = ComputeMathResult(mRawR, mRawG, mRawB, mRawA, mRawLum, mRawDelta);

   float target[kOutputCount];
   target[kResult] = mathRes;
   target[kBrightness] = (float)meanLum;
   target[kContrast] = (float)std::min(1.0, std::sqrt(variance) * 3.0);
   target[kRed] = mRawR;
   target[kGreen] = mRawG;
   target[kBlue] = mRawB;
   target[kSaturation] = (float)(sumSat * invWeight);
   target[kHue] = rawHue;
   target[kMotion] = mRawDelta;
   target[kCentroidX] = weightTotal > 1e-5 ? (float)(weightedX / weightTotal / (size - 1)) : 0.5f;
   target[kCentroidY] = weightTotal > 1e-5 ? (float)(weightedY / weightTotal / (size - 1)) : 0.5f;

   const float k = std::clamp(1.0f - smoothing, 0.01f, 1.0f);
   for (int i = 0; i < kOutputCount; i++)
      mValues[i] += (target[i] - mValues[i]) * k;
}

void ImageAnalyzeNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   if (!mInput.IsConnected())
      return;
   mInput.Pull(frameId);

   if (!mShaderTried)
   {
      mShaderTried = true;
      mProgram = GLUtil::CompileProgram(kDownsampleFrag);
   }
   if (mProgram == 0)
      return;

   // Rate-limit the readback; every frame would serialise CPU and GPU.
   const double now = Transport::Instance().Seconds();
   const double interval = 1.0 / std::max(1.0f, sampleRate);
   if (mLastSampleSeconds >= 0.0 && now >= mLastSampleSeconds && now - mLastSampleSeconds < interval)
      return;
   mLastSampleSeconds = now;

   Analyze();
}

// =========================================================== Audio Analyze

namespace
{
   const char* kAudioOutputNames[] = {
      "level", "low", "mid", "high", "onset",
      "b1", "b2", "b3", "b4", "b5", "b6", "b7", "b8"
   };
}

AudioAnalyzeNode::AudioAnalyzeNode()
{
   for (int i = 0; i < kOutputCount; i++)
   {
      mTaps[i].owner = this;
      mTaps[i].index = i;
   }
}

AudioAnalyzeNode::~AudioAnalyzeNode()
{
   // The audio tap is process-wide; leave it running for any other audio nodes.
}

const char* AudioAnalyzeNode::OutputLabel(int index) const
{
   if (index < 0 || index >= kOutputCount)
      return "out";
   return kAudioOutputNames[index];
}

IModulator* AudioAnalyzeNode::ModulatorOutput(int index)
{
   if (index < 0 || index >= kOutputCount)
      return nullptr;
   return &mTaps[index];
}

bool AudioAnalyzeNode::Start()
{
   std::string error;
   if (Platform::AudioStart(error))
   {
      mStatus = "listening to " + Platform::AudioDeviceName();
      return true;
   }
   mStatus = error;
   return false;
}

void AudioAnalyzeNode::Stop()
{
   Platform::AudioStop();
   mStatus = "stopped";
}

bool AudioAnalyzeNode::IsRunning() const
{
   if (fileSource != nullptr)
      return fileSource->IsLoaded();
   return Platform::AudioIsRunning();
}

float AudioAnalyzeNode::Value(int index) const
{
   float v = 0.0f;
   switch (index)
   {
      case kLevel: v = mLevels.rms; break;
      case kLow: v = mLevels.low; break;
      case kMid: v = mLevels.mid; break;
      case kHigh: v = mLevels.high; break;
      case kOnset: v = mOnsetEnvelope; break;
      default:
      {
         const int band = index - kBand0;
         if (band >= 0 && band < 8)
         {
            // 16 analysis bands folded down to the 8 exposed outputs
            v = std::max(mLevels.bands[band * 2], mLevels.bands[band * 2 + 1]);
         }
         break;
      }
   }
   return std::min(1.0f, std::max(0.0f, v * gain));
}

void AudioAnalyzeNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   Platform::AudioSetSmoothing(attack, release);
   Platform::AudioSetGain(1.0f);

   Platform::AudioLevels levels;
   bool running = false;
   if (fileSource != nullptr)
   {
      fileSource->CookIfNeeded(frameId);
      levels = fileSource->Levels();
      running = fileSource->IsLoaded();
   }
   else
   {
      running = Platform::AudioRead(levels);
   }

   const double now = Transport::Instance().Seconds();
   const double dt = std::max(0.0, std::min(0.25, now - mLastSeconds));
   mLastSeconds = now;

   if (running)
   {
      mLevels = levels;
      // Onset is a one-shot flag; stretch it into a decaying envelope so it is
      // actually usable as a modulation source.
      if (levels.onset)
         mOnsetEnvelope = 1.0f;
      else if (onsetHold > 0.0f)
         mOnsetEnvelope = std::max(0.0f, mOnsetEnvelope - (float)(dt / onsetHold));
      else
         mOnsetEnvelope = 0.0f;
   }
   else
   {
      mOnsetEnvelope = 0.0f;
   }
}

// ============================================================== Audio File
//
// AudioFilePlayerAudioNode is AudioFileNode's audio-thread half - the same
// two-object split SamplerNode/AudioSamplerNode use (see SamplerNode.h's
// class comment). It owns a decoded Platform::SampleBuffer (handed over
// through the same lock-free SampleSlot mailbox SamplerNode uses) and plays
// it back at a fixed rate 1.0 (no pitch/speed controls on this node), while
// independently running the same FFT-based level/band/onset analysis
// Platform.mm's live-input Analyser used to do, so Levels() keeps working
// unchanged for AudioAnalyzeNode's fileSource path and DrawAudioFileParams.
// Everything here obeys the AudioNode real-time contract: every buffer is
// fixed-size and preallocated as a member, no allocation/lock/std::string
// touches the audio thread.
namespace
{
   constexpr int kFileVolumeParam = 0;
   constexpr int kFileGainParam = 1;

   constexpr int kFileFftLog2 = 10; // 1024-point FFT, same size Platform.mm's live analyser used
   constexpr int kFileFftSize = 1 << kFileFftLog2;
   constexpr int kFileSpectrumSize = kFileFftSize / 2;
}

class AudioFilePlayerAudioNode : public AudioNode
{
public:
   AudioFilePlayerAudioNode()
   {
      // FFT setup allocates - main thread only, at construction, never on
      // the audio thread (mirrors PaulStretchNode's mFftSetup lifetime).
#if defined(__APPLE__)
      mFftSetup = vDSP_create_fftsetup(kFileFftLog2, FFT_RADIX2);
      vDSP_hann_window(mWindow, kFileFftSize, vDSP_HANN_NORM);
#else
      mFft.Prepare(kFileFftLog2);
      PortableFft::HannWindowNorm(mWindow, kFileFftSize);
#endif
   }

   ~AudioFilePlayerAudioNode() override
   {
#if defined(__APPLE__)
      if (mFftSetup != nullptr)
         vDSP_destroy_fftsetup(mFftSetup);
#endif
   }

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mMailbox.PrepareToPlay(sampleRate);
      mMailbox.SetImmediate(kFileVolumeParam, mVolume.load(std::memory_order_relaxed));
      mMailbox.SetImmediate(kFileGainParam, mGain.load(std::memory_order_relaxed));
   }

   // Main thread only. Hands over ownership of a freshly decoded buffer.
   void PushBuffer(Platform::SampleBuffer* buf) { mSampleSlot.Push(buf); }

   // Main thread only, once per frame from CookIfNeeded.
   void DrainRetired() { mSampleSlot.DrainRetired(); }

   void PushParams(float volume, float gain, float attack, float release, bool loop, bool monitor)
   {
      mVolume.store(volume, std::memory_order_relaxed);
      mGain.store(gain, std::memory_order_relaxed);
      mMailbox.Push(kFileVolumeParam, volume);
      mMailbox.Push(kFileGainParam, gain);
      mAttack.store(std::clamp(attack, 0.01f, 1.0f), std::memory_order_relaxed);
      mRelease.store(std::clamp(release, 0.005f, 1.0f), std::memory_order_relaxed);
      mLoop.store(loop, std::memory_order_relaxed);
      mMonitor.store(monitor, std::memory_order_relaxed);
   }

   // Main thread. Transport control - plain atomics, no mailbox needed since
   // these are one-shot requests/flags rather than smoothed audio params.
   void RequestPlay() { mPlaying.store(true, std::memory_order_relaxed); }
   void RequestPause() { mPlaying.store(false, std::memory_order_relaxed); }
   void RequestRestart() { mRestartRequested.store(true, std::memory_order_release); }
   bool IsPlaying() const { return mPlaying.load(std::memory_order_relaxed); }

   // Main thread. mFramePos is only ever written by the audio thread and
   // read here - a 64-bit atomic is lock-free on every platform this ships
   // on, so this never blocks the audio thread.
   double PositionSeconds() const
   {
      return mSampleRate > 0.0 ? (double)mFramePos.load(std::memory_order_relaxed) / mSampleRate : 0.0;
   }

   // Main thread. Latest published analysis snapshot - see the double-buffer
   // comment on mLevelsPublish below for why this is lock-free-safe to read
   // concurrently with the audio thread publishing a new one.
   Platform::AudioLevels ReadLevels() const
   {
      Platform::AudioLevels out = mLevelsPublish[mLevelsReady.load(std::memory_order_acquire)];
      // Onset is a one-shot flag: read-and-clear here (rather than letting
      // the audio thread clear it after publishing) so a fast analysis
      // cadence relative to CookIfNeeded's once-a-frame reads can never
      // drop one - matches Platform.mm's old ReadFrom() contract exactly.
      out.onset = mOnsetPending.exchange(false, std::memory_order_acq_rel);
      return out;
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& buffer) override
   {
      // Adopt a newly loaded buffer, if any, at the top of the block - never
      // mid-block. A fresh load always restarts from the top (matches the
      // old AudioFileOpen/ScheduleFile, which always began at framePosition
      // 0) and resets the analysis window so a previous file's spectrum
      // doesn't bleed into the new one's first readings.
      if (mSampleSlot.SwapIn())
      {
         mActiveBuffer = mSampleSlot.Active();
         mPos = 0.0;
         mFramePos.store(0, std::memory_order_relaxed);
         mRingWrite = 0;
         mRingCount = 0;
         mPrevFlux = 0.0f;
         std::memset(mPrevMagnitude, 0, sizeof(mPrevMagnitude));
         mSmoothed = Platform::AudioLevels();
      }

      for (int ch = 0; ch < buffer.numChannels; ch++)
         std::fill(buffer.channels[ch], buffer.channels[ch] + buffer.numFrames, 0.0f);

      if (mRestartRequested.exchange(false, std::memory_order_acq_rel))
      {
         mPos = 0.0;
         mFramePos.store(0, std::memory_order_relaxed);
      }

      const bool loop = mLoop.load(std::memory_order_relaxed);
      const bool monitor = mMonitor.load(std::memory_order_relaxed);
      const bool hasBuffer = mActiveBuffer != nullptr && mActiveBuffer->numFrames > 0;

      for (int i = 0; i < buffer.numFrames; i++)
      {
         const float volume = mMailbox.SmoothedValue(kFileVolumeParam);
         const float gain = mMailbox.SmoothedValue(kFileGainParam);

         float raw = 0.0f;
         if (hasBuffer && mPlaying.load(std::memory_order_relaxed))
         {
            raw = ReadSample(*mActiveBuffer, mPos);
            mPos += 1.0;
            if (mPos >= mActiveBuffer->numFrames)
            {
               if (loop)
                  mPos -= mActiveBuffer->numFrames;
               else
               {
                  mPos = (double)mActiveBuffer->numFrames;
                  mPlaying.store(false, std::memory_order_relaxed);
               }
            }
         }

         const float outSample = raw * volume;
         // Still analysed while `monitor` is off - "silent but still
         // analysed" always meant this node's own output, just scoped to
         // the graph now instead of to the hardware mixer.
         PushAnalysisSample(outSample * gain);
         for (int ch = 0; ch < buffer.numChannels; ch++)
            buffer.channels[ch][i] = monitor ? outSample : 0.0f;
      }

      mFramePos.store((int64_t)mPos, std::memory_order_relaxed);
      RunAnalysisIfWindowFull();
   }

private:
   static float ReadSample(const Platform::SampleBuffer& buf, double pos)
   {
      // Rate is always exactly 1.0 (no pitch/speed on this node), so pos is
      // always integral - a direct bounds-checked index is enough, no
      // interpolation needed. Channel 0 only, same simplification
      // SamplerNode's ReadSample makes for a multi-channel file.
      const int i = (int)pos;
      return (i >= 0 && i < buf.numFrames) ? buf.channelData[i] : 0.0f;
   }

   void PushAnalysisSample(float s)
   {
      mRing[mRingWrite] = s;
      mRingWrite = (mRingWrite + 1) % kFileFftSize;
      if (mRingCount < kFileFftSize)
         mRingCount++;
   }

   // Runs the same FFT-based analysis Platform.mm's ProcessInto used to run
   // on the tap callback, over the most recent kFileFftSize samples, once
   // enough have arrived. Every buffer here is a fixed-size member - no
   // allocation on this, the audio thread.
   void RunAnalysisIfWindowFull()
   {
      if (mRingCount < kFileFftSize)
         return;

      for (int i = 0; i < kFileFftSize; i++)
         mLinear[i] = mRing[(mRingWrite + i) % kFileFftSize];

      float rms = 0.0f, peak = 0.0f;
      for (int i = 0; i < kFileFftSize; i++)
      {
         const float v = mLinear[i];
         rms += v * v;
         peak = std::max(peak, std::fabs(v));
      }
      rms = std::sqrt(rms / (float)kFileFftSize);

#if defined(__APPLE__)
      vDSP_vmul(mLinear, 1, mWindow, 1, mWindowedScratch, 1, kFileFftSize);

      DSPSplitComplex split = { mReal, mImag };
      vDSP_ctoz((const DSPComplex*)mWindowedScratch, 2, &split, 1, kFileSpectrumSize);
      vDSP_fft_zrip(mFftSetup, &split, 1, kFileFftLog2, FFT_FORWARD);

      vDSP_zvabs(&split, 1, mMagnitude, 1, kFileSpectrumSize);
#else
      for (int i = 0; i < kFileFftSize; i++)
         mWindowedScratch[i] = mLinear[i] * mWindow[i];
      mFft.Forward(mWindowedScratch, kFileFftLog2, mReal, mImag);
      for (int i = 0; i < kFileSpectrumSize; i++)
         mMagnitude[i] = std::sqrt(mReal[i] * mReal[i] + mImag[i] * mImag[i]);
#endif
      const float norm = 2.0f / (float)kFileFftSize;
      for (int i = 0; i < kFileSpectrumSize; i++)
         mMagnitude[i] *= norm;

      float flux = 0.0f;
      for (int i = 0; i < kFileSpectrumSize; i++)
         flux += std::max(0.0f, mMagnitude[i] - mPrevMagnitude[i]);
      const bool onset = flux > mPrevFlux * 1.6f && flux > 0.02f;
      mPrevFlux = mPrevFlux * 0.7f + flux * 0.3f;
      std::memcpy(mPrevMagnitude, mMagnitude, sizeof(mMagnitude));
      if (onset)
         mOnsetPending.store(true, std::memory_order_relaxed);

      const double nyquist = mSampleRate * 0.5;
      auto rangeEnergy = [this, nyquist](double fromHz, double toHz) {
         const int lo = std::max(1, (int)(fromHz / nyquist * kFileSpectrumSize));
         const int hi = std::min(kFileSpectrumSize - 1, (int)(toHz / nyquist * kFileSpectrumSize));
         float sum = 0.0f; int count = 0;
         for (int i = lo; i <= hi; i++) { sum += mMagnitude[i]; count++; }
         return count > 0 ? sum / count : 0.0f;
      };

      float bands[Platform::kAudioBands] = { 0 };
      for (int b = 0; b < Platform::kAudioBands; b++)
      {
         const double loHz = 20.0 * std::pow(nyquist / 20.0, (double)b / Platform::kAudioBands);
         const double hiHz = 20.0 * std::pow(nyquist / 20.0, (double)(b + 1) / Platform::kAudioBands);
         bands[b] = rangeEnergy(loHz, hiHz);
      }

      auto shape = [](float v) { return std::min(1.0f, std::sqrt(v * 12.0f)); };

      const float attack = mAttack.load(std::memory_order_relaxed);
      const float release = mRelease.load(std::memory_order_relaxed);
      auto smooth = [attack, release](float prev, float target) {
         const float rate = (target > prev) ? attack : release;
         return prev + (target - prev) * rate;
      };

      mSmoothed.rms = smooth(mSmoothed.rms, std::min(1.0f, rms * 3.0f));
      mSmoothed.peak = smooth(mSmoothed.peak, std::min(1.0f, peak));
      mSmoothed.low = smooth(mSmoothed.low, shape((float)rangeEnergy(20.0, 250.0)));
      mSmoothed.mid = smooth(mSmoothed.mid, shape((float)rangeEnergy(250.0, 2000.0)));
      mSmoothed.high = smooth(mSmoothed.high, shape((float)rangeEnergy(2000.0, 16000.0)));
      for (int b = 0; b < Platform::kAudioBands; b++)
         mSmoothed.bands[b] = smooth(mSmoothed.bands[b], shape(bands[b]));

      // Double-buffer publish: the audio thread always writes the buffer
      // NOT currently marked ready, then flips the ready index - the same
      // lock-free-ish handoff Platform.h documents for AudioLevels
      // elsewhere (a torn read is, at worst, one stale frame of meter data).
      const int next = 1 - mLevelsReady.load(std::memory_order_relaxed);
      mLevelsPublish[next] = mSmoothed;
      mLevelsReady.store(next, std::memory_order_release);
   }

   double mSampleRate = 44100.0;
   ParamMailbox mMailbox;

   std::atomic<float> mVolume { 0.8f };
   std::atomic<float> mGain { 1.0f };
   std::atomic<float> mAttack { 0.5f };
   std::atomic<float> mRelease { 0.12f };
   std::atomic<bool> mLoop { true };
   std::atomic<bool> mMonitor { true };

   std::atomic<bool> mPlaying { false };
   std::atomic<bool> mRestartRequested { false };
   std::atomic<int64_t> mFramePos { 0 };
   double mPos = 0.0; // audio-thread-only playback cursor, in frames

   Platform::SampleBuffer* mActiveBuffer = nullptr;
   SampleSlot mSampleSlot;

#if defined(__APPLE__)
   FFTSetup mFftSetup = nullptr;
#else
   PortableFft::RealFft mFft;
#endif
   float mWindow[kFileFftSize] = {};
   float mRing[kFileFftSize] = {};
   int mRingWrite = 0;
   int mRingCount = 0;
   float mLinear[kFileFftSize] = {};
   float mWindowedScratch[kFileFftSize] = {};
   float mReal[kFileSpectrumSize] = {};
   float mImag[kFileSpectrumSize] = {};
   float mMagnitude[kFileSpectrumSize] = {};
   float mPrevMagnitude[kFileSpectrumSize] = {};
   float mPrevFlux = 0.0f;

   Platform::AudioLevels mSmoothed;
   Platform::AudioLevels mLevelsPublish[2];
   std::atomic<int> mLevelsReady { 0 };
   mutable std::atomic<bool> mOnsetPending { false };
};

AudioFileNode::AudioFileNode() = default;
AudioFileNode::~AudioFileNode() = default;

AudioNode* AudioFileNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioFilePlayerAudioNode>();
   return mAudioNode.get();
}

bool AudioFileNode::Open(const std::string& path)
{
   auto* decoded = new Platform::SampleBuffer();
   std::string error;
   if (!Platform::DecodeAudioFileToBuffer(path, *decoded, error))
   {
      delete decoded;
      mStatus = error.empty() ? "failed to load" : error;
      return false;
   }

   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioFilePlayerAudioNode>();

   mDuration = decoded->sampleRate > 0.0 ? (double)decoded->numFrames / decoded->sampleRate : 0.0;
   mAudioNode->PushBuffer(decoded);
   mAudioNode->PushParams(volume, gain, attack, release, loop, monitor);
   mAudioNode->RequestRestart();

   const size_t slash = path.find_last_of('/');
   mFileName = (slash == std::string::npos) ? path : path.substr(slash + 1);
   mFilePath = path;
   mStatus = "loaded";
   mLoaded = true;

   // RequiresAudioProcessing() now answers true - see its comment in
   // AnalyzeNodes.h for why this node needs an AudioTopologyEntry even with
   // no path to an Audio Out.
   AudioTopologyRequest::Request();
   return true;
}

bool AudioFileNode::OpenViaDialog()
{
   const std::string path = Platform::OpenAudioDialog();
   if (path.empty())
      return false; // cancelled
   return Open(path);
}

void AudioFileNode::Play()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioFilePlayerAudioNode>();
   mAudioNode->RequestPlay();
}

void AudioFileNode::Pause()
{
   if (mAudioNode)
      mAudioNode->RequestPause();
}

void AudioFileNode::Restart()
{
   if (mAudioNode)
      mAudioNode->RequestRestart();
}

bool AudioFileNode::IsPlaying() const
{
   return mAudioNode && mAudioNode->IsPlaying();
}

double AudioFileNode::Position() const
{
   return mAudioNode ? mAudioNode->PositionSeconds() : 0.0;
}

void AudioFileNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioFilePlayerAudioNode>();

   mAudioNode->PushParams(volume, gain, attack, release, loop, monitor);
   mAudioNode->DrainRetired();

   // Following the transport keeps the track locked to the same play/pause the
   // rest of the patch obeys, so a recording lines up with the audio.
   if (followTransport)
   {
      const bool transportPlaying = Transport::Instance().IsPlaying();
      if (transportPlaying != mWasTransportPlaying)
      {
         if (transportPlaying)
            Play();
         else
            Pause();
         mWasTransportPlaying = transportPlaying;
      }
   }

   mLevels = mAudioNode->ReadLevels();
}
