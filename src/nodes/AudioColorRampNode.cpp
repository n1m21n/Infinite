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
      "void main() {\n"
      "   vec4 src = texture(uSrc, vUv);\n"
      "   float lum = dot(src.rgb, vec3(0.299, 0.587, 0.114));\n"
      "   vec3 ramped = texture(uLut, vec2(clamp(lum, 0.0, 1.0), 0.5)).rgb;\n"
      "   fragColor = vec4(ramped, src.a);\n"
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
      crossoverPos[i] = (float)(i + 1) / (float)bandCount;
   mLutDirty = true;
}

namespace
{
   constexpr float kBandAttackMs = 15.0f;
   constexpr float kBandDecayMs = 120.0f;
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

      const float tau = (mRawBandEnergies[b] > mBandEnergies[b]) ? (kBandAttackMs * 0.001f) : (kBandDecayMs * 0.001f);
      const float alpha = 1.0f - std::exp(-dt / std::max(0.001f, tau));
      mBandEnergies[b] += (mRawBandEnergies[b] - mBandEnergies[b]) * alpha;
      mBandEnergies[b] = std::clamp(mBandEnergies[b], 0.0f, 2.0f);
   }

   const float masterTau = (totalEnergy * gain > mMasterEnergy) ? (kBandAttackMs * 0.001f) : (kBandDecayMs * 0.001f);
   const float masterAlpha = 1.0f - std::exp(-dt / std::max(0.001f, masterTau));
   mMasterEnergy += ((totalEnergy * gain * 0.15f) - mMasterEnergy) * masterAlpha;
   mMasterEnergy = std::clamp(mMasterEnergy, 0.0f, 1.0f);
}

void AudioColorRampNode::EvaluateRamp(float t, float outRgb[3]) const
{
   t = std::clamp(t, 0.0f, 1.0f);
   const int count = std::clamp(bandCount, 2, kMaxBands);

   if (mode == kModeSpectrum)
   {
      // Bypass the discrete band/crossover logic - sample the continuous FFT
      // spectrum directly at ramp position t and blend the palette using it.
      const int specSize = (int)mSmoothedSpectrum.size();
      float mag = 0.0f;
      if (specSize > 1)
      {
         const float binF = t * (float)(specSize - 1);
         const int bin0 = std::clamp((int)binF, 0, specSize - 1);
         const int bin1 = std::clamp(bin0 + 1, 0, specSize - 1);
         const float frac = binF - (float)bin0;
         mag = mSmoothedSpectrum[bin0] * (1.0f - frac) + mSmoothedSpectrum[bin1] * frac;
      }
      const float glow = minBrightness + (1.0f - minBrightness) * std::clamp(mag * 4.0f, 0.0f, 1.0f);

      // Continuous palette blend across the full ramp (ignores discrete crossovers).
      float f = t * (float)(count - 1);
      int b0 = std::clamp((int)f, 0, count - 1);
      int b1 = std::clamp(b0 + 1, 0, count - 1);
      float bf = f - (float)b0;

      outRgb[0] = (bandColor[b0][0] + (bandColor[b1][0] - bandColor[b0][0]) * bf) * glow;
      outRgb[1] = (bandColor[b0][1] + (bandColor[b1][1] - bandColor[b0][1]) * bf) * glow;
      outRgb[2] = (bandColor[b0][2] + (bandColor[b1][2] - bandColor[b0][2]) * bf) * glow;
      return;
   }

   // Calculate band center positions and effective colors
   float centers[kMaxBands];
   float colors[kMaxBands][3];
   float boundaries[kMaxBands];

   if (mode == kModeExpansion)
   {
      // Louder bands claim more of the 0..1 ramp: scale each band's base width
      // by its energy, then renormalize so the boundaries still span 0..1.
      float widths[kMaxBands];
      float prevPos = 0.0f;
      float totalWidth = 0.0f;
      for (int b = 0; b < count; b++)
      {
         float next = (b == count - 1) ? 1.0f : crossoverPos[b];
         float baseWidth = std::max(1e-4f, next - prevPos);
         prevPos = next;
         widths[b] = baseWidth * (1.0f + 3.0f * std::clamp(mBandEnergies[b], 0.0f, 1.0f));
         totalWidth += widths[b];
      }
      float cum = 0.0f;
      for (int b = 0; b < count; b++)
      {
         cum += widths[b] / totalWidth;
         boundaries[b] = cum;
      }
   }
   else
   {
      for (int b = 0; b < count; b++)
         boundaries[b] = (b == count - 1) ? 1.0f : crossoverPos[b];
   }

   float prev = 0.0f;
   for (int b = 0; b < count; b++)
   {
      float next = boundaries[b];
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
         if (t < boundaries[b])
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

   // Recompile shader if input connection state changed
   const bool hasInput = mInput.IsConnected();
   if (hasInput != mHadInput)
   {
      if (mProgram != 0)
      {
         glDeleteProgram(mProgram);
         mProgram = 0;
      }
      mShaderTried = false;
      mHadInput = hasInput;
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
   v.Float("minBrightness", minBrightness);

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
