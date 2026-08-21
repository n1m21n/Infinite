#include "AudioColorRampNode.h"
#include "audio/dsp/WaveTerrainDsp.h"
#include "Transport.h"

#include <algorithm>
#include <cmath>
#include <cstring>

static const std::vector<std::string> kModeNames = { "Intensity Glow", "Band Expansion", "Continuous Spectrum" };
static const std::vector<std::string> kInterpNames = { "Linear", "Constant", "Smooth" };
static const int kLutSize = 256;

const std::vector<std::string>& AudioColorRampNode::ModeNames() { return kModeNames; }
const std::vector<std::string>& AudioColorRampNode::InterpNames() { return kInterpNames; }

namespace
{
   const char* kPassthroughFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uLut;\n"
      "void main() {\n"
      "   vec3 c = texture(uLut, vec2(vUv.x, 0.5)).rgb;\n"
      "   fragColor = vec4(c, 1.0);\n"
      "}\n";

   const char* kGradeFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uSrc;\n"
      "uniform sampler2D uLut;\n"
      "uniform float uMix;\n"
      "void main() {\n"
      "   vec4 src = texture(uSrc, vUv);\n"
      "   float lum = dot(src.rgb, vec3(0.299, 0.587, 0.114));\n"
      "   vec3 ramped = texture(uLut, vec2(clamp(lum, 0.0, 1.0), 0.5)).rgb;\n"
      "   fragColor = vec4(mix(src.rgb, ramped, uMix), src.a);\n"
      "}\n";
}

// ---------------------------------------------------------------------------
// AudioColorRampAudioSink: Audio Thread Sink
// ---------------------------------------------------------------------------
class AudioColorRampAudioSink : public AudioNode
{
public:
   AudioColorRampAudioSink() = default;
   ~AudioColorRampAudioSink() override = default;

   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& output) override
   {
      const int numFrames = output.numFrames;
      if (numFrames <= 0)
         return;

      for (int ch = 0; ch < output.numChannels; ch++)
      {
         if (output.channels[ch])
            std::fill_n(output.channels[ch], numFrames, 0.0f);
      }

      const AudioBuffer* inBuf = nullptr;
      if (inputs != nullptr)
      {
         for (int s = 0; s < numInputs; s++)
         {
            if (inputs[s] != nullptr && inputs[s]->channels[0] != nullptr)
            {
               inBuf = inputs[s];
               break;
            }
         }
      }

      if (inBuf != nullptr)
      {
         const float* inL = inBuf->channels[0];
         const float* inR = (inBuf->numChannels > 1 && inBuf->channels[1] != nullptr) ? inBuf->channels[1] : inL;

         std::vector<float> mono(numFrames);
         for (int i = 0; i < numFrames; i++)
         {
            mono[i] = (inL[i] + inR[i]) * 0.5f;
            if (output.channels[0]) output.channels[0][i] = inL[i];
            if (output.numChannels > 1 && output.channels[1]) output.channels[1][i] = inR[i];
         }
         mMeterRing.Write(mono.data(), numFrames);
      }
   }

   int ReadSamples(float* out, int capacity)
   {
      return mMeterRing.Read(out, capacity);
   }

private:
   MeterRing mMeterRing;
};

// ---------------------------------------------------------------------------
// AudioColorRampNode Implementation
// ---------------------------------------------------------------------------
AudioColorRampNode::AudioColorRampNode()
{
   mAudioSink = std::make_unique<AudioColorRampAudioSink>();
   mAudioWindow.assign(1024, 0.0f);
   mSmoothedSpectrum.assign(512, 0.0f);

   for (int i = 0; i < kOutputCount; i++)
   {
      mTaps[i].owner = this;
      mTaps[i].index = i;
   }
}

AudioColorRampNode::~AudioColorRampNode()
{
   GLUtil::DestroyFbo(mOut);
   if (mLutTex != 0)
   {
      glDeleteTextures(1, &mLutTex);
      mLutTex = 0;
   }
   if (mProgram != 0)
   {
      glDeleteProgram(mProgram);
      mProgram = 0;
   }
}

AudioNode* AudioColorRampNode::GetAudioNode()
{
   if (!mAudioSink)
      mAudioSink = std::make_unique<AudioColorRampAudioSink>();
   return mAudioSink.get();
}

const char* AudioColorRampNode::OutputLabel(int index) const
{
   if (index == 0) return "master";
   static const char* kBandLabels[] = { "b1 (sub)", "b2 (low)", "b3 (mid)", "b4 (high)", "b5", "b6", "b7", "b8" };
   if (index >= 1 && index <= kMaxBands)
      return kBandLabels[index - 1];
   return "out";
}

IModulator* AudioColorRampNode::ModulatorOutput(int index)
{
   if (index < 0 || index >= kOutputCount)
      return nullptr;
   return &mTaps[index];
}

float AudioColorRampNode::ModulatorValue(int index) const
{
   if (index == 0)
      return std::clamp(mMasterEnergy, 0.0f, 1.0f);
   const int b = index - 1;
   if (b >= 0 && b < kMaxBands)
      return std::clamp(mBandEnergies[b], 0.0f, 1.0f);
   return 0.0f;
}

float AudioColorRampNode::PosToFreq(float pos)
{
   pos = std::clamp(pos, 0.0f, 1.0f);
   return 20.0f * std::pow(1000.0f, pos);
}

float AudioColorRampNode::FreqToPos(float freq)
{
   freq = std::clamp(freq, 20.0f, 20000.0f);
   return std::log10(freq / 20.0f) / 3.0f;
}

void AudioColorRampNode::SetBandCount(int count)
{
   bandCount = std::clamp(count, 2, kMaxBands);
   for (int i = 0; i < bandCount - 1; i++)
   {
      float idealPos = (float)(i + 1) / (float)bandCount;
      if (crossoverPos[i] <= 0.01f || crossoverPos[i] >= 0.99f)
         crossoverPos[i] = idealPos;
   }
   mLutDirty = true;
}

void AudioColorRampNode::ProcessAudioFFT()
{
   const double now = Transport::Instance().Seconds();
   float dt = (float)(mLastTime > 0.0 ? (now - mLastTime) : 0.016);
   dt = std::clamp(dt, 0.001f, 0.1f);
   mLastTime = now;

   const int winSize = 1024;
   float tempBuf[1024];
   const int readCount = mAudioSink ? mAudioSink->ReadSamples(tempBuf, winSize) : 0;
   if (readCount > 0)
   {
      if (readCount >= winSize)
      {
         mAudioWindow.assign(tempBuf + (readCount - winSize), tempBuf + readCount);
      }
      else
      {
         const int keep = winSize - readCount;
         std::copy(mAudioWindow.begin() + readCount, mAudioWindow.end(), mAudioWindow.begin());
         std::copy(tempBuf, tempBuf + readCount, mAudioWindow.begin() + keep);
      }
   }

   float re[1024];
   float im[1024];
   for (int i = 0; i < 1024; i++)
   {
      // Hann window
      const float w = 0.5f * (1.0f - cosf(2.0f * 3.14159265358979323846f * (float)i / 1023.0f));
      re[i] = mAudioWindow[i] * w;
      im[i] = 0.0f;
   }

   WaveTerrainDsp::Radix2FFT::Instance().Forward(re, im);

   const int count = std::clamp(bandCount, 2, kMaxBands);
   float rawBands[kMaxBands] = { 0.0f };
   int bandBinCounts[kMaxBands] = { 0 };
   float totalEnergy = 0.0f;

   const float sampleRate = 44100.0f;
   const float binWidth = sampleRate / 1024.0f;

   for (int i = 1; i < 512; i++)
   {
      const float mag = std::sqrt(re[i] * re[i] + im[i] * im[i]) * (2.0f / 1024.0f) * gain;
      mSmoothedSpectrum[i] = mSmoothedSpectrum[i] * 0.7f + mag * 0.3f;
      totalEnergy += mag;

      const float freq = (float)i * binWidth;
      const float pos = FreqToPos(freq);

      int targetBand = count - 1;
      for (int b = 0; b < count - 1; b++)
      {
         if (pos < crossoverPos[b])
         {
            targetBand = b;
            break;
         }
      }

      rawBands[targetBand] += mag;
      bandBinCounts[targetBand]++;
   }

   // Normalize band energies
   for (int b = 0; b < count; b++)
   {
      float avg = (bandBinCounts[b] > 0) ? (rawBands[b] / std::sqrt((float)bandBinCounts[b])) : 0.0f;
      mRawBandEnergies[b] = avg * bandGain[b] * 3.5f;

      const float tau = (mRawBandEnergies[b] > mBandEnergies[b]) ? (attack * 0.001f) : (decay * 0.001f);
      const float alpha = 1.0f - std::exp(-dt / std::max(0.001f, tau));
      mBandEnergies[b] += (mRawBandEnergies[b] - mBandEnergies[b]) * alpha;
      mBandEnergies[b] = std::clamp(mBandEnergies[b], 0.0f, 2.0f);
   }

   const float masterTau = (totalEnergy * gain > mMasterEnergy) ? (attack * 0.001f) : (decay * 0.001f);
   const float masterAlpha = 1.0f - std::exp(-dt / std::max(0.001f, masterTau));
   mMasterEnergy += ((totalEnergy * gain * 0.15f) - mMasterEnergy) * masterAlpha;
   mMasterEnergy = std::clamp(mMasterEnergy, 0.0f, 1.0f);
}

void AudioColorRampNode::EvaluateRamp(float t, float outRgb[3]) const
{
   t = std::clamp(t, 0.0f, 1.0f);
   const int count = std::clamp(bandCount, 2, kMaxBands);

   // Calculate band center positions and effective colors
   float centers[kMaxBands];
   float colors[kMaxBands][3];

   float prev = 0.0f;
   for (int b = 0; b < count; b++)
   {
      float next = (b == count - 1) ? 1.0f : crossoverPos[b];
      centers[b] = (prev + next) * 0.5f;
      prev = next;

      float energy = mBandEnergies[b];
      float glow = minBrightness + (1.0f - minBrightness) * std::clamp(energy, 0.0f, 1.0f);
      if (mode == kModeIntensity)
      {
         colors[b][0] = bandColor[b][0] * glow;
         colors[b][1] = bandColor[b][1] * glow;
         colors[b][2] = bandColor[b][2] * glow;
      }
      else
      {
         colors[b][0] = bandColor[b][0];
         colors[b][1] = bandColor[b][1];
         colors[b][2] = bandColor[b][2];
      }
   }

   if (interpMode == kInterpConstant)
   {
      int activeBand = count - 1;
      for (int b = 0; b < count - 1; b++)
      {
         if (t < crossoverPos[b])
         {
            activeBand = b;
            break;
         }
      }
      outRgb[0] = colors[activeBand][0];
      outRgb[1] = colors[activeBand][1];
      outRgb[2] = colors[activeBand][2];
      return;
   }

   // Linear / Smooth Interpolation across centers
   if (t <= centers[0])
   {
      outRgb[0] = colors[0][0];
      outRgb[1] = colors[0][1];
      outRgb[2] = colors[0][2];
      return;
   }
   if (t >= centers[count - 1])
   {
      outRgb[0] = colors[count - 1][0];
      outRgb[1] = colors[count - 1][1];
      outRgb[2] = colors[count - 1][2];
      return;
   }

   for (int b = 0; b < count - 1; b++)
   {
      if (t >= centers[b] && t <= centers[b + 1])
      {
         float span = std::max(1e-5f, centers[b + 1] - centers[b]);
         float f = (t - centers[b]) / span;
         if (interpMode == kInterpSmooth)
            f = f * f * (3.0f - 2.0f * f); // Smoothstep

         outRgb[0] = colors[b][0] + (colors[b + 1][0] - colors[b][0]) * f;
         outRgb[1] = colors[b][1] + (colors[b + 1][1] - colors[b][1]) * f;
         outRgb[2] = colors[b][2] + (colors[b + 1][2] - colors[b][2]) * f;
         return;
      }
   }

   outRgb[0] = colors[count - 1][0];
   outRgb[1] = colors[count - 1][1];
   outRgb[2] = colors[count - 1][2];
}

void AudioColorRampNode::RebuildLut()
{
   std::vector<unsigned char> lut(kLutSize * 3, 0);
   for (int i = 0; i < kLutSize; i++)
   {
      const float t = (float)i / (float)(kLutSize - 1);
      float rgb[3];
      EvaluateRamp(t, rgb);
      lut[i * 3 + 0] = (unsigned char)(std::clamp(rgb[0], 0.0f, 1.0f) * 255.0f + 0.5f);
      lut[i * 3 + 1] = (unsigned char)(std::clamp(rgb[1], 0.0f, 1.0f) * 255.0f + 0.5f);
      lut[i * 3 + 2] = (unsigned char)(std::clamp(rgb[2], 0.0f, 1.0f) * 255.0f + 0.5f);
   }

   if (mLutTex == 0)
      glGenTextures(1, &mLutTex);
   glBindTexture(GL_TEXTURE_2D, mLutTex);
   glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, kLutSize, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, lut.data());
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glBindTexture(GL_TEXTURE_2D, 0);

   mLutDirty = false;
   mRevision++;
}

bool AudioColorRampNode::EnsureShader()
{
   if (mShaderTried)
      return mProgram != 0;
   mShaderTried = true;
   mProgram = GLUtil::CompileProgram(mInput.IsConnected() ? kGradeFrag : kPassthroughFrag);
   return mProgram != 0;
}

void AudioColorRampNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   if (mAudioInput.IsConnected())
   {
      ProcessAudioFFT();
      mLutDirty = true;
   }

   if (mLutDirty || mLutTex == 0)
      RebuildLut();

   unsigned int srcTex = mInput.Pull(frameId);
   int targetW = (srcTex != 0) ? mInput.Width() : 256;
   int targetH = (srcTex != 0) ? mInput.Height() : 256;

   if (!GLUtil::EnsureFbo(mOut, targetW, targetH))
      return;

   // Recompile shader if input connection changed
   if (mProgram != 0)
   {
      glDeleteProgram(mProgram);
      mProgram = 0;
      mShaderTried = false;
   }
   if (!EnsureShader())
      return;

   GLUtil::RunShaderPass(mOut, mProgram, [this, srcTex]()
   {
      if (srcTex != 0)
      {
         glActiveTexture(GL_TEXTURE0);
         glBindTexture(GL_TEXTURE_2D, srcTex);
         glUniform1i(glGetUniformLocation(mProgram, "uSrc"), 0);

         glActiveTexture(GL_TEXTURE1);
         glBindTexture(GL_TEXTURE_2D, mLutTex);
         glUniform1i(glGetUniformLocation(mProgram, "uLut"), 1);

         glUniform1f(glGetUniformLocation(mProgram, "uMix"), rampMix);
      }
      else
      {
         glActiveTexture(GL_TEXTURE0);
         glBindTexture(GL_TEXTURE_2D, mLutTex);
         glUniform1i(glGetUniformLocation(mProgram, "uLut"), 0);
      }
   });
}

void AudioColorRampNode::VisitParams(ParamVisitor& v)
{
   v.Int("mode", mode);
   v.Int("interpMode", interpMode);
   v.Int("bandCount", bandCount);
   v.Float("gain", gain);
   v.Float("attack", attack);
   v.Float("decay", decay);
   v.Float("minBrightness", minBrightness);
   v.Float("rampMix", rampMix);
   v.Float("saturationBoost", saturationBoost);

   char key[32];
   for (int i = 0; i < kMaxBands - 1; i++)
   {
      snprintf(key, sizeof(key), "cross%d", i);
      v.Float(key, crossoverPos[i]);
   }
   for (int i = 0; i < kMaxBands; i++)
   {
      snprintf(key, sizeof(key), "color%d", i);
      v.Color(key, bandColor[i]);
      snprintf(key, sizeof(key), "gain%d", i);
      v.Float(key, bandGain[i]);
   }
   mLutDirty = true;
}
