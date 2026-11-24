#include "BrainSourceNode.h"

#include "core/Transport.h"
#include "core/bci/BciDspEngine.h"
#include "core/bci/BciRuntime.h"
#include "audio/AudioNode.h"
#include "gl3.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cmath>

namespace
{
   const char* kHexDigits = "0123456789abcdef";

   // Inferno-like map, same anchors as tools/bci/direct_pins.py
   void InfernoRgb(float v, uint8_t* rgb)
   {
      static const float kCm[8][3] = { { 0, 0, 4 }, { 40, 11, 84 }, { 101, 21, 110 }, { 159, 42, 99 },
                                       { 212, 72, 66 }, { 245, 125, 21 }, { 250, 193, 39 }, { 252, 255, 164 } };
      const float x = std::clamp(v, 0.0f, 1.0f) * 7.0f;
      const int i = std::min(6, (int)x);
      const float t = x - (float)i;
      for (int k = 0; k < 3; ++k)
         rgb[k] = (uint8_t)std::lround((1.0f - t) * kCm[i][k] + t * kCm[i + 1][k]);
   }

   int HexNibble(char c)
   {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
   }
}

// Audio half of Brain Source: plays the worker's audified EEG frames. Each channel is an
// oscillator whose phase advances 32x the EEG phase and whose level follows the EEG amplitude
// (over its eyes-open baseline), so 10 Hz alpha sounds at 320 Hz. AF7 + TP9 left, AF8 + TP10 right.
class BrainAudioNode : public AudioNode
{
public:
   static constexpr int kCap = 2048; // 8 s of EEG at 256 Hz

   // Worker thread only (single producer).
   void Push(const Bci::AudifyFrame* f, int n, double eegRateHz)
   {
      mEegRate.store(eegRateHz, std::memory_order_relaxed);
      unsigned w = mWrite.load(std::memory_order_relaxed);
      const unsigned r = mRead.load(std::memory_order_acquire);
      for (int i = 0; i < n && w - r < (unsigned)kCap; ++i, ++w)
         mRing[w % kCap] = f[i];
      mWrite.store(w, std::memory_order_release);
   }

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override { mSr = sampleRate > 0.0 ? sampleRate : 48000.0; }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& out) override
   {
      const double step = mEegRate.load(std::memory_order_relaxed) / mSr;
      for (int i = 0; i < out.numFrames; ++i)
      {
         mPos += step;
         while (mPos >= 1.0)
         {
            mPos -= 1.0;
            Advance();
         }
         float v[Bci::kChannelCount];
         for (int c = 0; c < Bci::kChannelCount; ++c)
         {
            const float a = mPrev.amp[c] + (mCur.amp[c] - mPrev.amp[c]) * (float)mPos;
            mOsc[c] += Bci::kAudifyMult * (double)mCur.dphase[c] * step;
            if (mOsc[c] > 1e6)
               mOsc[c] = std::fmod(mOsc[c], 6.283185307179586);
            v[c] = std::tanh(0.5f * a) * (float)std::cos(mOsc[c]);
         }
         const float l = 0.35f * (v[0] + v[2]);
         const float r = 0.35f * (v[1] + v[3]);
         if (out.numChannels >= 2)
         {
            out.channels[0][i] = l;
            out.channels[1][i] = r;
            for (int ch = 2; ch < out.numChannels; ++ch)
               out.channels[ch][i] = 0.0f;
         }
         else if (out.numChannels == 1)
            out.channels[0][i] = 0.5f * (l + r);
      }
   }

private:
   void Advance()
   {
      unsigned r = mRead.load(std::memory_order_relaxed);
      const unsigned w = mWrite.load(std::memory_order_acquire);
      if (w - r > 128) // more than 0.5 s behind: drop to ~1/8 s of lag
         r = w - 32;
      mPrev = mCur;
      if (r != w)
      {
         mCur = mRing[r % kCap];
         ++r;
      }
      else
      {
         for (int c = 0; c < Bci::kChannelCount; ++c)
            mCur.amp[c] *= 0.98f; // starved (source stopped): fade out instead of holding a tone
      }
      mRead.store(r, std::memory_order_release);
   }

   std::array<Bci::AudifyFrame, kCap> mRing{};
   std::atomic<unsigned> mWrite{ 0 }, mRead{ 0 };
   std::atomic<double> mEegRate{ Bci::kSampleRateHz };
   double mSr = 48000.0;
   double mPos = 0.0;
   double mOsc[Bci::kChannelCount] = {};
   Bci::AudifyFrame mPrev, mCur;
};

const std::vector<std::string>& BrainSourceNode::DeviceNames()
{
   static const std::vector<std::string> names = {
      "Synthetic Cortex",
      "CSV Replay",
      "Muse 2",
      "Muse S",
      "OpenBCI Cyton",
      "OpenBCI Ganglion",
      "Neurosity Crown"
   };
   return names;
}

const std::vector<std::string>& BrainSourceNode::PresetNames()
{
   static const std::vector<std::string> names = {
      "Calm Alpha",
      "Deep Focus",
      "Theta Dream",
      "Micro-Gesture Perf"
   };
   return names;
}

const std::vector<std::string>& BrainSourceNode::BlinkModeNames()
{
   static const std::vector<std::string> names = {
      "Intentional / Double",
      "All Blinks",
      "Double Blink"
   };
   return names;
}

BrainSourceNode::BrainSourceNode()
   : mAudio(std::make_unique<BrainAudioNode>())
{
   for (int i = 0; i < kOutputCount; ++i)
   {
      mTaps[i].owner = this;
      mTaps[i].index = i;
      mPublished[i].store(i == kPresence ? 1.0f : (i == kGazeLR || i == kValence || i == kHeartPulse ? 0.5f : 0.0f),
                         std::memory_order_relaxed);
   }
}

BrainSourceNode::~BrainSourceNode()
{
   StopWorker();
   if (mTexture != 0)
      glDeleteTextures(1, &mTexture);
   GLUtil::DestroyFbo(mLightFbo);
   if (mLightProgram != 0)
      glDeleteProgram(mLightProgram);
}

AudioNode* BrainSourceNode::GetAudioNode()
{
   return mAudio.get();
}

namespace
{
   // Brain Light (docs/plans/bci/BRAIN_LIGHT.md): the same maths as Bci::BrainLightPixel, per pixel.
   const char* kLightFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform float uA[4];\n"
      "uniform float uHue[4];\n"
      "uniform float uVivid[4];\n"
      "uniform float uGrain[4];\n"
      "uniform float uPhase[4];\n"
      "uniform float uPres[4];\n"
      "uniform vec2 uPos[4];\n"
      "uniform float uState;\n"
      "uniform float uHOverW;\n"
      "uniform float uSeq;\n"
      "uniform vec2 uRes;\n"
      "uniform vec4 uK;\n" // slow, ring, eps, glow
      "uniform vec2 uK2;\n" // state weight, alpha hue
      "float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }\n"
      "vec3 hsv(float h, float s, float v) {\n"
      "   vec3 k = clamp(abs(mod(h / 1.0471976 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);\n"
      "   return v * mix(vec3(1.0), k, s);\n"
      "}\n"
      "void main() {\n"
      "   float u = vUv.x, v = 1.0 - vUv.y;\n"
      "   float w[4]; float d[4]; float wSum = 0.0;\n"
      "   for (int c = 0; c < 4; ++c) {\n"
      "      vec2 dd = vec2(u - uPos[c].x, (v - uPos[c].y) * uHOverW);\n"
      "      d[c] = length(dd);\n"
      "      w[c] = 1.0 / (d[c] * d[c] + uK.z);\n"
      "      wSum += w[c];\n"
      "   }\n"
      "   float m = 0.0, val = 0.0, grain = 0.0; vec2 vec = vec2(0.0);\n"
      "   for (int c = 0; c < 4; ++c) {\n"
      "      float wc = w[c] / wSum;\n"
      "      float pull = uK2.x * uState * uPres[c];\n"
      "      float mc = wc * (uA[c] + uK.w * uPres[c] + pull);\n"
      "      m += mc;\n"
      "      vec += wc * (uA[c] * uVivid[c] * vec2(cos(uHue[c]), sin(uHue[c])) + pull * vec2(cos(uK2.y), sin(uK2.y)));\n"
      "      val += mc * (0.6 + 0.4 * cos(uK.y * d[c] - uPhase[c] / uK.x));\n"
      "      grain += wc * uGrain[c];\n"
      "   }\n"
      "   vec2 px = floor(vUv * uRes);\n"
      "   float r1 = max(hash(px + vec2(uSeq * 0.37, 1.7)), 1e-6), r2 = hash(px.yx + vec2(5.3, uSeq * 0.61));\n"
      "   float noise = sqrt(-2.0 * log(r1)) * cos(6.2831853 * r2);\n"
      "   val = clamp(val + grain * 0.5 * noise, 0.0, 1.0);\n"
      "   float sat = clamp(length(vec) / (m + 1e-9), 0.0, 1.0);\n"
      "   float h = atan(vec.y, vec.x);\n"
      "   if (h < 0.0) h += 6.2831853;\n"
      "   fragColor = vec4(hsv(h, sat, val), 1.0);\n"
      "}\n";
}

const std::vector<std::string>& BrainSourceNode::ImageModeNames()
{
   static const std::vector<std::string> kNames = { "Spectrogram", "Light" };
   return kNames;
}

bool BrainSourceNode::DrawLight(GLUtil::Fbo& out, unsigned int& program, const Bci::BrainLightFrame& f)
{
   if (program == 0)
      program = GLUtil::CompileProgram(kLightFrag);
   if (program == 0)
      return false;
   const int w = out.w > 0 ? out.w : kLightW, h = out.h > 0 ? out.h : kLightH;
   if (!GLUtil::EnsureFbo(out, w, h, GL_RGBA8, "BrainLight"))
      return false;
   float pos[Bci::kChannelCount * 2];
   for (int c = 0; c < Bci::kChannelCount; ++c)
   {
      pos[c * 2] = Bci::kLightPos[c][0];
      pos[c * 2 + 1] = Bci::kLightPos[c][1];
   }
   GLUtil::RunShaderPass(out, program, [&]()
   {
      auto loc = [&](const char* nm) { return GLUtil::UniformLoc(program, nm); };
      glUniform1fv(loc("uA"), 4, f.a);
      glUniform1fv(loc("uHue"), 4, f.hue);
      glUniform1fv(loc("uVivid"), 4, f.vivid);
      glUniform1fv(loc("uGrain"), 4, f.grain);
      glUniform1fv(loc("uPhase"), 4, f.phase);
      glUniform1fv(loc("uPres"), 4, f.pres);
      glUniform2fv(loc("uPos"), 4, pos);
      glUniform1f(loc("uState"), f.state);
      glUniform1f(loc("uHOverW"), (float)out.h / (float)out.w);
      glUniform1f(loc("uSeq"), (float)(f.seq % 4096u));
      glUniform2f(loc("uRes"), (float)out.w, (float)out.h);
      glUniform4f(loc("uK"), Bci::kLightSlow, Bci::kLightRing, Bci::kLightEps, Bci::kLightGlow);
      glUniform2f(loc("uK2"), Bci::kLightState, 280.0f * 3.14159265f / 180.0f * 9.0f / 39.0f);
   });
   return true;
}

void BrainSourceNode::CopyImagePixels(std::vector<uint8_t>& out) const
{
   std::lock_guard<std::mutex> lk(mPixMu);
   out = mPixels;
}

Bci::BrainLightFrame BrainSourceNode::LightFrame() const
{
   std::lock_guard<std::mutex> lk(mPixMu);
   return mLight;
}

void BrainSourceNode::FeedDirect(const Bci::TelemetrySnapshot& snap, unsigned& lastImgSeq,
                                 const std::vector<Bci::AudifyFrame>& frames, double eegRateHz)
{
   // The wave's own phase per sensor (same Hilbert phase the audio pin plays), kept bounded:
   // the rings use phase / kLightSlow, so wrapping at 2 pi * kLightSlow is seamless.
   const double wrap = 2.0 * 3.141592653589793 * (double)Bci::kLightSlow;
   for (const auto& fr : frames)
      for (int c = 0; c < Bci::kChannelCount; ++c)
         mLightPhase[c] = std::fmod(mLightPhase[c] + (double)fr.dphase[c], wrap);
   const int newCols = std::min(4, (int)(snap.imageSeq - lastImgSeq));
   for (int k = 0; k < newCols; ++k)
      AppendImageColumn(snap);
   if (newCols > 0)
   {
      float ph[Bci::kChannelCount];
      for (int c = 0; c < Bci::kChannelCount; ++c)
         ph[c] = (float)mLightPhase[c];
      Bci::BrainLightFrame f;
      Bci::BrainLightFeatures(snap.imageZ, ph, snap.imageState, f);
      std::lock_guard<std::mutex> lk(mPixMu);
      f.seq = mLight.seq + (unsigned)newCols;
      mLight = f;
      ++mLightRevision;
   }
   lastImgSeq = snap.imageSeq;
   if (!frames.empty())
      mAudio->Push(frames.data(), (int)frames.size(), eegRateHz);
}

void BrainSourceNode::AppendImageColumn(const Bci::TelemetrySnapshot& snap)
{
   std::lock_guard<std::mutex> lk(mPixMu);
   if (mPixels.empty())
      mPixels.assign((size_t)kImgW * kImgH * 4, 0);
   // scroll one column left
   for (int y = 0; y < kImgH; ++y)
   {
      uint8_t* row = mPixels.data() + (size_t)y * kImgW * 4;
      std::memmove(row, row + 4, (size_t)(kImgW - 1) * 4);
   }
   auto put = [&](int y, const uint8_t* rgb)
   {
      uint8_t* p = mPixels.data() + ((size_t)y * kImgW + (kImgW - 1)) * 4;
      p[0] = rgb[0]; p[1] = rgb[1]; p[2] = rgb[2]; p[3] = 255;
   };
   uint8_t rgb[3] = {};
   const uint8_t grey[3] = { 96, 96, 96 }, gap[3] = { 0, 0, 0 };
   if (snap.imageState >= 0.0f)
      InfernoRgb(snap.imageState, rgb);
   for (int y = 0; y < kImgStrip; ++y)
      put(y, snap.imageState >= 0.0f ? rgb : grey);
   int y = kImgStrip;
   for (int c = 0; c < Bci::kChannelCount; ++c)
   {
      for (int g = 0; g < kImgGap; ++g)
         put(y++, gap);
      for (int k = Bci::kImageBins - 1; k >= 0; --k) // 40 Hz at the top of each row, 1 Hz at the bottom
      {
         InfernoRgb(snap.imageCol[c * Bci::kImageBins + k], rgb);
         put(y++, rgb);
      }
   }
   ++mPixRevision;
}

const char* BrainSourceNode::OutputLabel(int index) const
{
   static const char* kLabels[kOutputCount] = {
      "presence",
      "arousal",
      "valence",
      "entropy",
      "coherence",
      "density",
      "smoothness",
      "motion",
      "space",
      "tension",
      "drift",
      "surge",
      "stillness",
      "delta",
      "theta",
      "alpha",
      "beta",
      "gamma",
      "blink",
      "gaze L/R",
      "brow",
      "jaw L",
      "jaw R",
      "heart"
   };
   if (index == kImagePin)
      return "image";
   if (index == kAudioPin)
      return "audio";
   if (index < 0 || index >= kOutputCount)
      return "out";
   return kLabels[index];
}

IModulator* BrainSourceNode::ModulatorOutput(int index)
{
   if (index < 0 || index >= kOutputCount)
      return nullptr;
   return &mTaps[index];
}

float BrainSourceNode::Value(int index) const
{
   if (index < 0 || index >= kOutputCount)
      return 0.0f;
   if (playTake && !mRecordingTake && !mTakeFrames.empty())
      return SampleTake(index, Transport::Instance().Beats());
   return std::clamp(mPublished[index].load(std::memory_order_relaxed), 0.0f, 1.0f);
}

void BrainSourceNode::StartWorker()
{
   std::lock_guard<std::mutex> lk(mMu);
   if (!mThread.joinable())
   {
      mStop = false;
      mThread = std::thread([this] { WorkerLoop(); });
   }
}

void BrainSourceNode::StopWorker()
{
   {
      std::lock_guard<std::mutex> lk(mMu);
      mStop = true;
   }
   mCv.notify_all();
   if (mThread.joinable())
      mThread.join();
}

void BrainSourceNode::StartCalibration()
{
   StartWorker();
   {
      std::lock_guard<std::mutex> lk(mMu);
      mCalibStartRequested = true;
   }
   mCv.notify_one();
}

void BrainSourceNode::CancelCalibration()
{
   {
      std::lock_guard<std::mutex> lk(mMu);
      mCalibCancelRequested = true;
   }
   mCv.notify_one();
}

void BrainSourceNode::TriggerSyntheticGesture(Bci::GestureKind kind)
{
   StartWorker();
   {
      std::lock_guard<std::mutex> lk(mMu);
      mPendingGestures.emplace_back(kind, false);
   }
   mCv.notify_one();
}

void BrainSourceNode::TriggerSyntheticVegetativeBlink()
{
   StartWorker();
   {
      std::lock_guard<std::mutex> lk(mMu);
      mPendingGestures.emplace_back(Bci::GestureKind::Blink, true);
   }
   mCv.notify_one();
}

void BrainSourceNode::StartRecordingTake()
{
   mRecordingTake = true;
   playTake = false;
   mTakeFrames.clear();
   mTakeRecStartBeats = Transport::Instance().Beats();
}

void BrainSourceNode::StopRecordingTake()
{
   if (!mRecordingTake)
      return;
   mRecordingTake = false;
   if (!mTakeFrames.empty())
      playTake = true;
}

void BrainSourceNode::ClearTake()
{
   mRecordingTake = false;
   playTake = false;
   mTakeFrames.clear();
}

void BrainSourceNode::RecordTakeStep(double beats)
{
   double rel = beats - mTakeRecStartBeats;
   if (rel < 0.0)
   {
      mTakeRecStartBeats = beats;
      mTakeFrames.clear();
      rel = 0.0;
   }
   const int targetStep = std::min(kTakeMaxSteps - 1, std::max(0, (int)std::floor(rel * kTakeSamplesPerBeat)));
   const int curSteps = (int)mTakeFrames.size() / kOutputCount;

   uint8_t curSample[kOutputCount];
   for (int i = 0; i < kOutputCount; ++i)
   {
      const float v = std::clamp(mPublished[i].load(std::memory_order_relaxed), 0.0f, 1.0f);
      curSample[i] = (uint8_t)std::lround(v * 255.0f);
   }

   while ((int)mTakeFrames.size() / kOutputCount <= targetStep &&
          (int)mTakeFrames.size() / kOutputCount < kTakeMaxSteps)
   {
      if (curSteps > 0 && (int)mTakeFrames.size() / kOutputCount < targetStep)
      {
         // Forward-fill missed steps with previous sample before writing latest
         const size_t prevOff = mTakeFrames.size() - (size_t)kOutputCount;
         for (int i = 0; i < kOutputCount; ++i)
            mTakeFrames.push_back(mTakeFrames[prevOff + (size_t)i]);
      }
      else
      {
         mTakeFrames.insert(mTakeFrames.end(), curSample, curSample + kOutputCount);
      }
   }
}

float BrainSourceNode::SampleTake(int index, double beats) const
{
   const int steps = (int)mTakeFrames.size() / kOutputCount;
   if (steps <= 0 || index < 0 || index >= kOutputCount)
      return 0.0f;
   const double pos = std::fmod(std::max(0.0, beats) * (double)kTakeSamplesPerBeat, (double)steps);
   const int i0 = ((int)std::floor(pos)) % steps;
   const int i1 = (i0 + 1) % steps;
   const float frac = (float)(pos - std::floor(pos));
   const float v0 = (float)mTakeFrames[(size_t)i0 * kOutputCount + (size_t)index] / 255.0f;
   const float v1 = (float)mTakeFrames[(size_t)i1 * kOutputCount + (size_t)index] / 255.0f;
   // Discrete blink pin does not interpolate mid-pulse
   if (index == kBlink)
      return v0 > 0.5f ? 1.0f : 0.0f;
   return v0 + (v1 - v0) * frac;
}

void BrainSourceNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   if (!(playTake && !mRecordingTake && !mTakeFrames.empty()))
      StartWorker();

   if (mRecordingTake)
      RecordTakeStep(Transport::Instance().Beats());

   float vals[kOutputCount];
   for (int i = 0; i < kOutputCount; ++i)
      vals[i] = Value(i);
   Bci::PublishBrainGlobals(vals, !bypassed);

   if (imageMode == kImageLight)
   {
      Bci::BrainLightFrame f;
      {
         std::lock_guard<std::mutex> lk(mPixMu);
         if (mLightRevision == mLightDrawn && mLightFbo.tex != 0)
            return;
         mLightDrawn = mLightRevision;
         f = mLight;
      }
      if (DrawLight(mLightFbo, mLightProgram, f))
         ++mTexRevision;
      return;
   }

   // Image pin upload (GL thread): only when the worker added a column.
   std::vector<uint8_t> flipped;
   {
      std::lock_guard<std::mutex> lk(mPixMu);
      if (mPixRevision == mUploadedRevision || mPixels.empty())
         return;
      mUploadedRevision = mPixRevision;
      flipped.resize(mPixels.size());
      const size_t rowBytes = (size_t)kImgW * 4;
      for (int y = 0; y < kImgH; ++y) // GL row 0 is the bottom
         std::copy_n(mPixels.data() + (size_t)y * rowBytes, rowBytes, flipped.data() + (size_t)(kImgH - 1 - y) * rowBytes);
   }
   if (mTexture == 0)
   {
      glGenTextures(1, &mTexture);
      glBindTexture(GL_TEXTURE_2D, mTexture);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kImgW, kImgH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
   }
   glBindTexture(GL_TEXTURE_2D, mTexture);
   glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
   glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kImgW, kImgH, GL_RGBA, GL_UNSIGNED_BYTE, flipped.data());
   glBindTexture(GL_TEXTURE_2D, 0);
   ++mTexRevision;
}

void BrainSourceNode::GetTelemetry(Bci::TelemetrySnapshot& out) const
{
   std::lock_guard<std::mutex> lk(mTelemetryMu);
   out = mTelemetry;
}

std::string BrainSourceNode::Status() const
{
   std::lock_guard<std::mutex> lk(mTelemetryMu);
   return mStatus;
}

void BrainSourceNode::VisitParams(ParamVisitor& v)
{
   v.Int("device", device);
   v.Int("syntheticPreset", syntheticPreset);
   v.Int("blinkMode", blinkMode);
   v.Int("imageMode", imageMode);
   imageMode = std::clamp(imageMode, (int)kImageSpectrogram, (int)kImageLight);
   v.Float("smoothing", smoothing);
   v.Float("holdMs", holdMs);
   v.Bool("artifactBlanking", artifactBlanking);
   v.Bool("startleGuard", startleGuard);
   v.Bool("playTake", playTake);
   v.Text("serialPort", serialPort);
   v.Text("csvPath", csvPath);

   std::string hex;
   hex.reserve(mTakeFrames.size() * 2);
   for (uint8_t b : mTakeFrames)
   {
      hex.push_back(kHexDigits[b >> 4]);
      hex.push_back(kHexDigits[b & 15]);
   }
   const std::string before = hex;
   v.Text("takeData", hex);
   if (hex != before && !mRecordingTake)
   {
      mTakeFrames.clear();
      const size_t maxBytes = (size_t)kTakeMaxSteps * (size_t)kOutputCount;
      for (size_t i = 0; i + 1 < hex.size() && mTakeFrames.size() < maxBytes; i += 2)
      {
         const int hi = HexNibble(hex[i]), lo = HexNibble(hex[i + 1]);
         if (hi < 0 || lo < 0)
            break;
         mTakeFrames.push_back((uint8_t)(hi * 16 + lo));
      }
      // Trim partial trailing frame if corrupted
      const size_t rem = mTakeFrames.size() % (size_t)kOutputCount;
      if (rem != 0)
         mTakeFrames.resize(mTakeFrames.size() - rem);
   }
}

void BrainSourceNode::WorkerLoop()
{
   Bci::BciRuntime runtime;
   Bci::BciDspEngine dsp;
   std::vector<Bci::SampleFrame> batch;
   Bci::TelemetrySnapshot snap;
   float pins[kOutputCount] = {};
   unsigned lastImgSeq = 0;

   for (;;)
   {
      int wantDevice = kDeviceSynthetic;
      int wantPreset = 0;
      int wantBlinkMode = 0;
      float wantSmooth = 0.45f;
      bool wantBlanking = true;
      bool wantStartle = true;
      std::string wantSerial;
      std::string wantCsv;
      bool startCal = false;
      bool cancelCal = false;
      std::vector<std::pair<Bci::GestureKind, bool>> gestures;

      {
         std::unique_lock<std::mutex> lk(mMu);
         mCv.wait_for(lk, std::chrono::milliseconds(16), [this]
         {
            return mStop || mReopenRequested || mCalibStartRequested || mCalibCancelRequested || !mPendingGestures.empty();
         });
         if (mStop)
            break;

         wantDevice = device;
         wantPreset = syntheticPreset;
         wantBlinkMode = blinkMode;
         wantSmooth = smoothing;
         wantBlanking = artifactBlanking;
         wantStartle = startleGuard;
         wantSerial = serialPort;
         wantCsv = csvPath;
         startCal = mCalibStartRequested;
         cancelCal = mCalibCancelRequested;
         mCalibStartRequested = false;
         mCalibCancelRequested = false;
         mReopenRequested = false;
         gestures.swap(mPendingGestures);
      }

      if (wantDevice != mActiveDevice || wantSerial != mActiveSerial || wantCsv != mActiveCsv)
      {
         mActiveDevice = wantDevice;
         mActiveSerial = wantSerial;
         mActiveCsv = wantCsv;
         std::string status;
         runtime.Open(static_cast<Bci::BoardKind>(std::clamp(wantDevice, 0, 6)), wantSerial, wantCsv, status);
         mPackMissing.store(runtime.IsPackMissing(), std::memory_order_relaxed);
         dsp.SetSampleRate(runtime.SampleRateHz());
         std::lock_guard<std::mutex> tlk(mTelemetryMu);
         mStatus = status;
      }

      if (wantPreset != mActivePreset)
      {
         mActivePreset = wantPreset;
         runtime.Synth().SetPreset(static_cast<Bci::SyntheticPreset>(std::clamp(wantPreset, 0, 3)));
      }

      for (const auto& g : gestures)
      {
         if (g.second)
            runtime.Synth().TriggerVegetativeBlink();
         else
            runtime.Synth().TriggerGesture(g.first);
      }

      if (startCal)
         dsp.StartCalibration();
      if (cancelCal)
         dsp.CancelCalibration();

      dsp.SetSmoothing(wantSmooth);
      dsp.SetArtifactBlanking(wantBlanking);
      dsp.SetStartleGuard(wantStartle);
      dsp.SetBlinkMode(static_cast<Bci::BlinkMode>(std::clamp(wantBlinkMode, 0, 2)));

      // 4 samples @ 256 Hz per ~15.6 ms cycle (~64 Hz fast somatic update)
      runtime.PollSamples(4, batch);
      if (!batch.empty())
      {
         dsp.Process(batch.data(), (int)batch.size(), pins, snap);
         snap.packMissing = runtime.IsPackMissing();
         {
            std::lock_guard<std::mutex> tlk(mTelemetryMu);
            std::snprintf(snap.statusText, sizeof(snap.statusText), "%s",
                          snap.stateCalibFailed ? "calibration: no clean signal, check contact" : mStatus.c_str());
            mTelemetry = snap;
         }
         for (int i = 0; i < kOutputCount; ++i)
            mPublished[i].store(std::clamp(pins[i], 0.0f, 1.0f), std::memory_order_relaxed);

         FeedDirect(snap, lastImgSeq, dsp.Audify(), runtime.SampleRateHz());
      }
   }

   runtime.Close();
}
