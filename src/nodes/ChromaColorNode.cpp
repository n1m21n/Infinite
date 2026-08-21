#include "ChromaColorNode.h"
#include "audio/dsp/WaveTerrainDsp.h"
#include "Transport.h"

#include <algorithm>
#include <cmath>
#include <cstring>

static const std::vector<std::string> kPresetNames = {
   "Circle of Fifths", "Scriabin Prometheus", "Newton Prism", "Custom"
};

static const std::vector<std::string> kLayoutNames = {
   "Chromatic (C..B)", "Circle of Fifths", "Active Chord Notes"
};

static const char* kPitchNames[12] = {
   "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

static const int kCircleOfFifthsOrder[12] = {
   0, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10, 5 // C, G, D, A, E, B, F#, C#, G#, D#, A#, F
};

const std::vector<std::string>& ChromaColorNode::PresetNames() { return kPresetNames; }
const std::vector<std::string>& ChromaColorNode::LayoutNames() { return kLayoutNames; }
const char* ChromaColorNode::PitchName(int pitchIndex)
{
   if (pitchIndex < 0 || pitchIndex >= 12) return "";
   return kPitchNames[pitchIndex];
}
int ChromaColorNode::CircleOfFifthsOrder(int index)
{
   if (index < 0 || index >= 12) return 0;
   return kCircleOfFifthsOrder[index];
}

namespace
{
   const int kLutSize = 256;

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

   // Krumhansl-Schmuckler Key Profiles (12 values)
   const float kMajorProfile[12] = {
      6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f
   };
   const float kMinorProfile[12] = {
      6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f
   };
}

// ---------------------------------------------------------------------------
// ChromaColorAudioSink: Audio + Note Thread Sink
// ---------------------------------------------------------------------------
class ChromaColorAudioSink : public AudioNode
{
public:
   ChromaColorAudioSink() = default;
   ~ChromaColorAudioSink() override = default;

   void SetNoteInbox(NoteEventQueue* inbox, int cursor) override
   {
      mNoteInbox = inbox;
      mNoteCursor = cursor;
   }

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

      // Process Note Events from MIDI / Sequencer
      if (mNoteInbox != nullptr && mNoteCursor >= 0)
      {
         NoteEvent events[32];
         int count = mNoteInbox->Pop(mNoteCursor, events, 32);
         for (int i = 0; i < count; i++)
         {
            const auto& ev = events[i];
            int p = ev.note % 12;
            if (p < 0) p += 12;
            if (ev.isNoteOn && ev.velocity > 0.0f)
            {
               mNoteVelocities[p] = ev.velocity;
            }
            else if (!ev.isNoteOn)
            {
               mNoteVelocities[p] = 0.0f;
            }
         }
      }

      // Process Audio Buffer
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

   void GetNoteVelocities(float out[12])
   {
      for (int i = 0; i < 12; i++)
         out[i] = mNoteVelocities[i];
   }

private:
   MeterRing mMeterRing;
   NoteEventQueue* mNoteInbox = nullptr;
   int mNoteCursor = -1;
   float mNoteVelocities[12] = { 0.0f };
};

// ---------------------------------------------------------------------------
// ChromaColorNode Implementation
// ---------------------------------------------------------------------------
ChromaColorNode::ChromaColorNode()
{
   mAudioSink = std::make_unique<ChromaColorAudioSink>();
   mAudioWindow.assign(1024, 0.0f);

   for (int i = 0; i < kOutputCount; i++)
   {
      mTaps[i].owner = this;
      mTaps[i].index = i;
   }

   ApplyPreset(kPresetCircleOfFifths);
}

ChromaColorNode::~ChromaColorNode()
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

AudioNode* ChromaColorNode::GetAudioNode()
{
   if (!mAudioSink)
      mAudioSink = std::make_unique<ChromaColorAudioSink>();
   return mAudioSink.get();
}

const char* ChromaColorNode::OutputLabel(int index) const
{
   if (index == 0) return "root pitch";
   if (index == 1) return "centroid hue";
   if (index == 2) return "consonance";
   if (index == 3) return "total energy";
   if (index >= 4 && index < kOutputCount)
      return kPitchNames[index - 4];
   return "out";
}

IModulator* ChromaColorNode::ModulatorOutput(int index)
{
   if (index < 0 || index >= kOutputCount)
      return nullptr;
   return &mTaps[index];
}

float ChromaColorNode::ModulatorValue(int index) const
{
   switch (index)
   {
      case 0: return (float)mDetectedRoot / 12.0f;
      case 1: return mCentroidHue;
      case 2: return mConsonance;
      case 3: return std::clamp(mTotalEnergy, 0.0f, 1.0f);
      default:
      {
         const int p = index - 4;
         if (p >= 0 && p < 12)
            return std::clamp(mSmoothedChroma[p], 0.0f, 1.0f);
         break;
      }
   }
   return 0.0f;
}

void ChromaColorNode::ApplyPreset(int presetIndex)
{
   palettePreset = presetIndex;
   if (presetIndex == kPresetCircleOfFifths)
   {
      // Harmonically smooth Circle of Fifths color palette
      for (int i = 0; i < 12; i++)
      {
         float hue = (float)i / 12.0f; // in fifths space
         // Convert HSV (hue, 0.85, 0.95) to RGB
         float h = hue * 6.0f;
         int sector = (int)h % 6;
         float f = h - (float)sector;
         float p = 0.95f * (1.0f - 0.85f);
         float q = 0.95f * (1.0f - 0.85f * f);
         float t = 0.95f * (1.0f - 0.85f * (1.0f - f));
         float r = 0, g = 0, b = 0;
         switch (sector)
         {
            case 0: r = 0.95f; g = t;     b = p;     break;
            case 1: r = q;     g = 0.95f; b = p;     break;
            case 2: r = p;     g = 0.95f; b = t;     break;
            case 3: r = p;     g = q;     b = 0.95f; break;
            case 4: r = t;     g = p;     b = 0.95f; break;
            case 5: r = 0.95f; g = p;     b = q;     break;
         }
         int pitch = kCircleOfFifthsOrder[i];
         pitchColors[pitch][0] = r;
         pitchColors[pitch][1] = g;
         pitchColors[pitch][2] = b;
      }
   }
   else if (presetIndex == kPresetScriabin)
   {
      // Alexander Scriabin 1911 Prometheus Synesthesia Scale
      const float scriabin[12][3] = {
         { 0.95f, 0.15f, 0.15f }, // C: Red
         { 0.55f, 0.10f, 0.80f }, // C#: Deep Violet
         { 0.95f, 0.85f, 0.15f }, // D: Yellow
         { 0.60f, 0.65f, 0.75f }, // D#: Steel Glaze
         { 0.20f, 0.75f, 0.95f }, // E: Sky Blue
         { 0.75f, 0.10f, 0.20f }, // F: Deep Maroon
         { 0.15f, 0.45f, 1.00f }, // F#: Bright Blue
         { 0.95f, 0.45f, 0.20f }, // G: Orange-Rose
         { 0.65f, 0.20f, 0.85f }, // G#: Violet
         { 0.15f, 0.85f, 0.30f }, // A: Emerald Green
         { 0.70f, 0.75f, 0.85f }, // A#: Steel/Silver
         { 0.65f, 0.80f, 0.95f }  // B: Pearl Blue
      };
      for (int i = 0; i < 12; i++)
      {
         pitchColors[i][0] = scriabin[i][0];
         pitchColors[i][1] = scriabin[i][1];
         pitchColors[i][2] = scriabin[i][2];
      }
   }
   else if (presetIndex == kPresetNewton)
   {
      // Newton Optical Prism
      for (int i = 0; i < 12; i++)
      {
         float hue = (float)i / 12.0f;
         float h = hue * 6.0f;
         int sector = (int)h % 6;
         float f = h - (float)sector;
         float p = 0.95f * (1.0f - 0.85f);
         float q = 0.95f * (1.0f - 0.85f * f);
         float t = 0.95f * (1.0f - 0.85f * (1.0f - f));
         float r = 0, g = 0, b = 0;
         switch (sector)
         {
            case 0: r = 0.95f; g = t;     b = p;     break;
            case 1: r = q;     g = 0.95f; b = p;     break;
            case 2: r = p;     g = 0.95f; b = t;     break;
            case 3: r = p;     g = q;     b = 0.95f; break;
            case 4: r = t;     g = p;     b = 0.95f; break;
            case 5: r = 0.95f; g = p;     b = q;     break;
         }
         pitchColors[i][0] = r;
         pitchColors[i][1] = g;
         pitchColors[i][2] = b;
      }
   }
   mLutDirty = true;
}

void ChromaColorNode::ProcessChromagram()
{
   const double now = Transport::Instance().Seconds();
   float dt = (float)(mLastTime > 0.0 ? (now - mLastTime) : 0.016);
   dt = std::clamp(dt, 0.001f, 0.1f);
   mLastTime = now;

   // 1. Read Audio Buffer & FFT
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
      const float w = 0.5f * (1.0f - cosf(2.0f * 3.14159265358979323846f * (float)i / 1023.0f));
      re[i] = mAudioWindow[i] * w;
      im[i] = 0.0f;
   }

   WaveTerrainDsp::Radix2FFT::Instance().Forward(re, im);

   // 2. Accumulate 12-TET Chromagram Bins
   std::fill_n(mRawChroma, 12, 0.0f);
   const float sampleRate = 44100.0f;
   const float binWidth = sampleRate / 1024.0f;

   for (int i = 1; i < 512; i++)
   {
      const float freq = (float)i * binWidth;
      if (freq < 30.0f || freq > 4500.0f)
         continue;

      const float mag = std::sqrt(re[i] * re[i] + im[i] * im[i]) * (2.0f / 1024.0f) * gain;
      // Fractional MIDI note: 69 + 12 * log2(freq / 440)
      const float midiNote = 69.0f + 12.0f * (std::log(freq / 440.0f) / 0.69314718f);
      float chromaPos = std::fmod(midiNote, 12.0f);
      if (chromaPos < 0.0f) chromaPos += 12.0f;

      int p0 = (int)chromaPos;
      int p1 = (p0 + 1) % 12;
      float f = chromaPos - (float)p0;

      mRawChroma[p0] += mag * (1.0f - f);
      mRawChroma[p1] += mag * f;
   }

   // 3. Add MIDI Note input if present
   if (mAudioSink)
   {
      float noteVels[12];
      mAudioSink->GetNoteVelocities(noteVels);
      for (int p = 0; p < 12; p++)
      {
         if (noteVels[p] > 0.0f)
            mRawChroma[p] += noteVels[p] * 2.0f;
      }
   }

   // 4. Smooth Chromagram with Ballistics
   float sumEnergy = 0.0f;
   float sumCos = 0.0f;
   float sumSin = 0.0f;

   for (int p = 0; p < 12; p++)
   {
      const float tau = (mRawChroma[p] > mSmoothedChroma[p]) ? (attack * 0.001f) : (decay * 0.001f);
      const float alpha = 1.0f - std::exp(-dt / std::max(0.001f, tau));
      mSmoothedChroma[p] += (mRawChroma[p] - mSmoothedChroma[p]) * alpha;
      mSmoothedChroma[p] = std::clamp(mSmoothedChroma[p], 0.0f, 2.0f);

      sumEnergy += mSmoothedChroma[p];

      // Circular chroma projection for centroid hue & consonance
      float angle = (float)p * (2.0f * 3.1415926535f / 12.0f);
      sumCos += mSmoothedChroma[p] * std::cos(angle);
      sumSin += mSmoothedChroma[p] * std::sin(angle);
   }

   mTotalEnergy = std::clamp(sumEnergy * 0.25f, 0.0f, 1.0f);

   if (sumEnergy > 0.01f)
   {
      float hueAngle = std::atan2(sumSin, sumCos);
      if (hueAngle < 0.0f) hueAngle += 2.0f * 3.1415926535f;
      mCentroidHue = hueAngle / (2.0f * 3.1415926535f);

      float r = std::sqrt(sumCos * sumCos + sumSin * sumSin);
      mConsonance = std::clamp(r / sumEnergy, 0.0f, 1.0f);
   }

   // 5. Krumhansl-Schmuckler Key Profile Correlation
   float bestScore = -1e9f;
   int bestRoot = 0;
   bool bestMinor = false;

   for (int r = 0; r < 12; r++)
   {
      float scoreMaj = 0.0f;
      float scoreMin = 0.0f;
      for (int i = 0; i < 12; i++)
      {
         int pitch = (r + i) % 12;
         scoreMaj += mSmoothedChroma[pitch] * kMajorProfile[i];
         scoreMin += mSmoothedChroma[pitch] * kMinorProfile[i];
      }
      if (scoreMaj > bestScore)
      {
         bestScore = scoreMaj;
         bestRoot = r;
         bestMinor = false;
      }
      if (scoreMin > bestScore)
      {
         bestScore = scoreMin;
         bestRoot = r;
         bestMinor = true;
      }
   }

   mDetectedRoot = bestRoot;
   mDetectedMinor = bestMinor;
   mKeyConfidence = std::clamp(bestScore * 0.03f, 0.0f, 1.0f);
}

void ChromaColorNode::EvaluateHarmonicRamp(float t, float outRgb[3]) const
{
   t = std::clamp(t, 0.0f, 1.0f);

   // Determine pitch order
   std::vector<int> order(12);
   if (layoutMode == kLayoutCircleOfFifths)
   {
      for (int i = 0; i < 12; i++)
         order[i] = kCircleOfFifthsOrder[i];
   }
   else if (layoutMode == kLayoutActiveChords)
   {
      // Collect active notes
      std::vector<std::pair<float, int>> active;
      for (int i = 0; i < 12; i++)
      {
         if (mSmoothedChroma[i] > pitchThreshold)
            active.push_back({ mSmoothedChroma[i], i });
      }
      if (active.empty())
      {
         active.push_back({ 1.0f, mDetectedRoot });
      }
      std::sort(active.rbegin(), active.rend());
      int count = (int)active.size();
      int seg = std::clamp((int)(t * count), 0, count - 1);
      int p = active[seg].second;
      float glow = minBrightness + (1.0f - minBrightness) * std::clamp(mSmoothedChroma[p], 0.0f, 1.0f);
      outRgb[0] = pitchColors[p][0] * glow;
      outRgb[1] = pitchColors[p][1] * glow;
      outRgb[2] = pitchColors[p][2] * glow;
      return;
   }
   else
   {
      for (int i = 0; i < 12; i++)
         order[i] = i;
   }

   // Continuous circular interpolation across 12 chromatic/harmonic stops
   float pos = t * 12.0f;
   int idx0 = ((int)pos) % 12;
   int idx1 = (idx0 + 1) % 12;
   float f = pos - (float)(int)pos;

   int p0 = order[idx0];
   int p1 = order[idx1];

   float glow0 = minBrightness + (1.0f - minBrightness) * std::clamp(mSmoothedChroma[p0], 0.0f, 1.0f);
   float glow1 = minBrightness + (1.0f - minBrightness) * std::clamp(mSmoothedChroma[p1], 0.0f, 1.0f);

   float c0[3] = { pitchColors[p0][0] * glow0, pitchColors[p0][1] * glow0, pitchColors[p0][2] * glow0 };
   float c1[3] = { pitchColors[p1][0] * glow1, pitchColors[p1][1] * glow1, pitchColors[p1][2] * glow1 };

   // Smoothstep
   f = f * f * (3.0f - 2.0f * f);

   outRgb[0] = c0[0] + (c1[0] - c0[0]) * f;
   outRgb[1] = c0[1] + (c1[1] - c0[1]) * f;
   outRgb[2] = c0[2] + (c1[2] - c0[2]) * f;
}

void ChromaColorNode::RebuildLut()
{
   std::vector<unsigned char> lut(kLutSize * 3, 0);
   for (int i = 0; i < kLutSize; i++)
   {
      const float t = (float)i / (float)(kLutSize - 1);
      float rgb[3];
      EvaluateHarmonicRamp(t, rgb);
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

bool ChromaColorNode::EnsureShader()
{
   if (mShaderTried)
      return mProgram != 0;
   mShaderTried = true;
   mProgram = GLUtil::CompileProgram(mInput.IsConnected() ? kGradeFrag : kPassthroughFrag);
   return mProgram != 0;
}

void ChromaColorNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   if (mAudioInput.IsConnected() || mNoteInput.IsConnected())
   {
      ProcessChromagram();
      mLutDirty = true;
   }

   if (mLutDirty || mLutTex == 0)
      RebuildLut();

   unsigned int srcTex = mInput.Pull(frameId);
   int targetW = (srcTex != 0) ? mInput.Width() : 256;
   int targetH = (srcTex != 0) ? mInput.Height() : 256;

   if (!GLUtil::EnsureFbo(mOut, targetW, targetH))
      return;

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

void ChromaColorNode::VisitParams(ParamVisitor& v)
{
   v.Int("palettePreset", palettePreset);
   v.Int("layoutMode", layoutMode);
   v.Float("gain", gain);
   v.Float("attack", attack);
   v.Float("decay", decay);
   v.Float("minBrightness", minBrightness);
   v.Float("saturation", saturation);
   v.Float("rampMix", rampMix);
   v.Float("pitchThreshold", pitchThreshold);

   char key[32];
   for (int i = 0; i < kNumPitches; i++)
   {
      snprintf(key, sizeof(key), "pitchColor%d", i);
      v.Color(key, pitchColors[i]);
   }
   mLutDirty = true;
}
