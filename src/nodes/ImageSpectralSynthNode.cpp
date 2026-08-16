#include "ImageSpectralSynthNode.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/AudioVoice.h"
#include "audio/DspMath.h"
#include "audio/MeterRing.h"
#include "audio/ParamMailbox.h"
#include "audio/dsp/SpectralAdditiveSynth.h"
#include "core/GLUtil.h"

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

namespace
{
   const std::vector<std::string> kScanModeNames = {
      "Loop (0->1)", "Ping-Pong", "One-Shot", "Free-Run Hz", "BPM Sync", "Manual Scrub"
   };

   const std::vector<std::string> kScaleNames = {
      "Logarithmic (Musical)", "Linear (Hz)", "Harmonic Series", "Mel Scale", "Chromatic (12-TET)"
   };

   const std::vector<std::string> kColorNames = {
      "Luma (Mono)", "Hue -> Stereo Pan", "RGB Split (L/R/Shimmer)", "Spectral Bands"
   };

   const std::vector<std::string> kPartialsNames = {
      "64 Partials", "128 Partials", "256 Partials"
   };

   const std::vector<std::string> kFilterNames = {
      "Off", "LP 12dB", "LP 24dB", "HP 12dB", "BP 12dB"
   };

   inline float MidiNoteToHz(int midiNote)
   {
      return 440.0f * powf(2.0f, ((float)midiNote - 69.0f) / 12.0f);
   }

   const char* kLaserVertSrc =
      "#version 150\n"
      "in vec2 aPos;\n"
      "void main() {\n"
      "   gl_Position = vec4(aPos.x * 2.0 - 1.0, aPos.y * 2.0 - 1.0, 0.0, 1.0);\n"
      "}\n";

   const char* kLaserFragSrc =
      "#version 150\n"
      "out vec4 fragColor;\n"
      "uniform vec4 uColor;\n"
      "void main() {\n"
      "   fragColor = uColor;\n"
      "}\n";
}

const std::vector<std::string>& ImageSpectralSynthNode::ScanModeNames() { return kScanModeNames; }
const std::vector<std::string>& ImageSpectralSynthNode::FrequencyScaleNames() { return kScaleNames; }
const std::vector<std::string>& ImageSpectralSynthNode::ColorModeNames() { return kColorNames; }
const std::vector<std::string>& ImageSpectralSynthNode::PartialsCountNames() { return kPartialsNames; }
const std::vector<std::string>& ImageSpectralSynthNode::FilterTypeNames() { return kFilterNames; }

// ===========================================================================
// AudioImageSpectralNode: Audio Thread Real-Time Additive DSP Engine
// ===========================================================================
class AudioImageSpectralNode : public AudioNode
{
public:
   static constexpr int kMaxVoices = ImageSpectralSynthNode::kMaxVoices;
   static constexpr int kMaxPartials = SpectralAdditiveDsp::kMaxPartials;

   AudioImageSpectralNode()
   {
      mMatrices[0] = std::make_shared<SpectralAdditiveDsp::SpectrogramMatrix>();
      mMatrices[1] = std::make_shared<SpectralAdditiveDsp::SpectrogramMatrix>();
      mMailbox.SetImmediate(kParamVolume, 0.8f);
      mMailbox.SetImmediate(kParamPan, 0.0f);
      mMailbox.SetImmediate(kParamStereoWidth, 1.0f);
      mMailbox.SetImmediate(kParamGlide, 0.0f);
      mMailbox.SetImmediate(kParamScanSpeed, 1.0f);
      mMailbox.SetImmediate(kParamPosition, 0.0f);
      mMailbox.SetImmediate(kParamMinFreq, 40.0f);
      mMailbox.SetImmediate(kParamMaxFreq, 16000.0f);
      mMailbox.SetImmediate(kParamRootFreq, 261.63f);
      mMailbox.SetImmediate(kParamCutoff, 18000.0f);
      mMailbox.SetImmediate(kParamResonance, 0.15f);
      mMailbox.SetImmediate(kParamDrive, 0.0f);
      mMailbox.SetImmediate(kParamFine, 0.0f);
      mMailbox.SetImmediate(kParamThreshold, 0.02f);
      mMailbox.SetImmediate(kParamContrast, 1.2f);
      mMailbox.SetImmediate(kParamBrightness, 1.0f);
   }

   ~AudioImageSpectralNode() override = default;

   NoteEventQueue* NoteOutbox() override { return nullptr; }
   void SetNoteInbox(NoteEventQueue* inbox) override { mNoteInbox = inbox; }

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
      mMailbox.PrepareToPlay(mSampleRate);
      for (int v = 0; v < kMaxVoices; v++)
      {
         mVoices[v].ampEnv.SetSampleRate(mSampleRate);
         mVoices[v].filterL.SetSampleRate(mSampleRate);
         mVoices[v].filterR.SetSampleRate(mSampleRate);
      }
   }

   void SwapMatrix(const SpectralAdditiveDsp::SpectrogramMatrix& newMatrix)
   {
      const int nextIdx = 1 - mActiveMatrixIndex.load(std::memory_order_relaxed);
      *mMatrices[nextIdx] = newMatrix;
      mActiveMatrixIndex.store(nextIdx, std::memory_order_release);
   }

   enum ParamIndex
   {
      kParamVolume = 0,
      kParamPan,
      kParamStereoWidth,
      kParamGlide,
      kParamScanSpeed,
      kParamPosition,
      kParamMinFreq,
      kParamMaxFreq,
      kParamRootFreq,
      kParamCutoff,
      kParamResonance,
      kParamDrive,
      kParamFine,
      kParamThreshold,
      kParamContrast,
      kParamBrightness,
      kNumSmoothedParams
   };

   void PushParams(const ImageSpectralSynthNode& n)
   {
      mScanMode.store(n.scanMode, std::memory_order_relaxed);
      mRate.store(n.rate, std::memory_order_relaxed);
      mDirection.store(n.direction, std::memory_order_relaxed);
      mPartialsChoice.store(n.partialsChoice, std::memory_order_relaxed);
      mFreqScale.store(n.freqScale, std::memory_order_relaxed);
      mColorMode.store(n.colorMode, std::memory_order_relaxed);
      mInvert.store(n.invert != 0, std::memory_order_relaxed);
      mOctave.store(n.octave, std::memory_order_relaxed);
      mSemi.store(n.semi, std::memory_order_relaxed);
      mUnison.store(std::clamp(n.unison, 1, ImageSpectralSynthNode::kMaxUnison), std::memory_order_relaxed);
      mDetune.store(n.detune, std::memory_order_relaxed);
      mFilterType.store(n.filterType, std::memory_order_relaxed);

      mAmpAdsr[0].store(n.ampAttack, std::memory_order_relaxed);
      mAmpAdsr[1].store(n.ampDecay, std::memory_order_relaxed);
      mAmpAdsr[2].store(n.ampSustain, std::memory_order_relaxed);
      mAmpAdsr[3].store(n.ampRelease, std::memory_order_relaxed);

      const float values[kNumSmoothedParams] = {
         n.volume,
         n.pan,
         n.stereoWidth,
         n.glide,
         n.scanSpeed,
         n.position,
         n.minFreq,
         n.maxFreq,
         n.rootFreq,
         n.cutoff,
         n.resonance,
         n.drive,
         n.fine,
         n.threshold,
         n.contrast,
         n.brightness
      };

      for (int i = 0; i < kNumSmoothedParams; i++)
         mMailbox.Push(i, values[i]);
   }

   void TriggerScan()
   {
      mTriggerScan.store(true, std::memory_order_relaxed);
   }

   float GetPlayhead() const
   {
      return mCurrentPlayhead.load(std::memory_order_relaxed);
   }

   int ActiveVoices() const
   {
      int count = 0;
      for (int v = 0; v < kMaxVoices; v++)
      {
         if (mVoices[v].active) count++;
      }
      return count;
   }

   MeterRing& ScopeRing() { return mScopeRing; }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      const int numFrames = output.numFrames;
      float* outL = output.channels[0];
      float* outR = output.channels[1];
      std::fill_n(outL, numFrames, 0.0f);
      std::fill_n(outR, numFrames, 0.0f);

      const int matrixIdx = mActiveMatrixIndex.load(std::memory_order_acquire);
      const SpectralAdditiveDsp::SpectrogramMatrix& matrix = *mMatrices[matrixIdx];

      const int scanMode = mScanMode.load(std::memory_order_relaxed);
      const int rateDiv = mRate.load(std::memory_order_relaxed);
      const int direction = mDirection.load(std::memory_order_relaxed);
      const int partialsChoice = mPartialsChoice.load(std::memory_order_relaxed);
      const int numPartials = (partialsChoice == 0) ? 64 : (partialsChoice == 2 ? 256 : 128);
      const int freqScale = mFreqScale.load(std::memory_order_relaxed);
      const int colorMode = mColorMode.load(std::memory_order_relaxed);
      const bool invert = mInvert.load(std::memory_order_relaxed);
      const int octave = mOctave.load(std::memory_order_relaxed);
      const int semi = mSemi.load(std::memory_order_relaxed);
      const int unison = mUnison.load(std::memory_order_relaxed);
      const float detuneCents = mDetune.load(std::memory_order_relaxed);
      const int filterType = mFilterType.load(std::memory_order_relaxed);

      const float ampA = mAmpAdsr[0].load(std::memory_order_relaxed);
      const float ampD = mAmpAdsr[1].load(std::memory_order_relaxed);
      const float ampS = mAmpAdsr[2].load(std::memory_order_relaxed);
      const float ampR = mAmpAdsr[3].load(std::memory_order_relaxed);

      if (mTriggerScan.exchange(false, std::memory_order_relaxed))
      {
         mPlayheadPos = (direction == 0) ? 0.0 : 1.0;
         mPingPongDir = 1;
      }

      // 1. Process MIDI Note Events
      if (mNoteInbox != nullptr)
      {
         NoteEvent evts[64];
         const int numEvts = mNoteInbox->Pop(evts, 64);
         for (int i = 0; i < numEvts; i++)
         {
            const auto& event = evts[i];
            if (event.isNoteOn && event.velocity > 0.0f)
            {
               const int vIdx = AllocateVoice();
               Voice& v = mVoices[vIdx];
               v.active = true;
               v.held = true;
               v.note = event.note;
               v.voiceId = event.voiceId;
               v.velocity = event.velocity;
               v.age = ++mAgeCounter;
               v.ampEnv.SetADSR(ampA, ampD, ampS, ampR);
               v.ampEnv.NoteOn();
               v.playheadPos = (direction == 0) ? 0.0 : 1.0;
               v.pingPongDir = 1;

               const float baseHz = MidiNoteToHz(event.note + octave * 12 + semi);
               v.targetPitchRatio = baseHz / 261.63f; // relative to Middle C
               if (v.currentPitchRatio <= 0.0f)
                  v.currentPitchRatio = v.targetPitchRatio;
            }
            else if (!event.isNoteOn && !event.bendUpdate)
            {
               for (int v = 0; v < kMaxVoices; v++)
               {
                  if (mVoices[v].active && (mVoices[v].voiceId == event.voiceId || mVoices[v].note == event.note) && mVoices[v].held)
                  {
                     mVoices[v].held = false;
                     mVoices[v].ampEnv.NoteOff();
                  }
               }
            }
         }
      }

      // 2. Fetch Smoothed Parameters
      const float volume = mMailbox.SmoothedValue(kParamVolume);
      const float pan = mMailbox.SmoothedValue(kParamPan);
      const float stereoWidth = mMailbox.SmoothedValue(kParamStereoWidth);
      const float glide = mMailbox.SmoothedValue(kParamGlide);
      const float scanSpeed = mMailbox.SmoothedValue(kParamScanSpeed);
      const float manualPos = mMailbox.SmoothedValue(kParamPosition);
      const float minFreq = mMailbox.SmoothedValue(kParamMinFreq);
      const float maxFreq = mMailbox.SmoothedValue(kParamMaxFreq);
      const float rootFreq = mMailbox.SmoothedValue(kParamRootFreq);
      const float cutoff = mMailbox.SmoothedValue(kParamCutoff);
      const float resonance = mMailbox.SmoothedValue(kParamResonance);
      const float drive = mMailbox.SmoothedValue(kParamDrive);
      const float fine = mMailbox.SmoothedValue(kParamFine);
      const float threshold = mMailbox.SmoothedValue(kParamThreshold);
      const float contrast = mMailbox.SmoothedValue(kParamContrast);
      const float brightness = mMailbox.SmoothedValue(kParamBrightness);

      const float finePitchRatio = powf(2.0f, (fine + (float)(octave * 12 + semi) * 100.0f) / 1200.0f);

      // 3. Compute Playhead Position Increment
      double playheadInc = 0.0;
      if (scanMode == SpectralAdditiveDsp::kScanBpmSync)
      {
         const double bpm = std::max(20.0, (double)Transport::Instance().Tempo());
         const double beatsForDiv = std::max(0.0625, MusicTime::BeatsFor((MusicTime::RateDivision)rateDiv));
         const double cycleSeconds = beatsForDiv * (60.0 / bpm);
         playheadInc = (cycleSeconds > 0.0 && mSampleRate > 0.0) ? (1.0 / (cycleSeconds * mSampleRate)) * (double)scanSpeed : 0.0;
      }
      else if (scanMode == SpectralAdditiveDsp::kScanFreeRunHz)
      {
         playheadInc = (mSampleRate > 0.0) ? ((double)scanSpeed / mSampleRate) : 0.0;
      }
      else if (scanMode == SpectralAdditiveDsp::kScanForwardLoop || scanMode == SpectralAdditiveDsp::kScanPingPong || scanMode == SpectralAdditiveDsp::kScanOneShot)
      {
         playheadInc = (mSampleRate > 0.0) ? ((double)scanSpeed / mSampleRate) : 0.0;
      }

      // Update Global Playhead for UI / Drone
      double currentGlobalU = 0.0;
      if (scanMode == SpectralAdditiveDsp::kScanManual)
      {
         currentGlobalU = std::clamp(manualPos, 0.0f, 1.0f);
      }
      else if (scanMode == SpectralAdditiveDsp::kScanPingPong)
      {
         mPlayheadPos += playheadInc * (double)mPingPongDir;
         if (mPlayheadPos >= 1.0)
         {
            mPlayheadPos = 1.0;
            mPingPongDir = -1;
         }
         else if (mPlayheadPos <= 0.0)
         {
            mPlayheadPos = 0.0;
            mPingPongDir = 1;
         }
         currentGlobalU = mPlayheadPos;
      }
      else if (scanMode == SpectralAdditiveDsp::kScanOneShot)
      {
         if (direction == 0)
         {
            mPlayheadPos = std::min(1.0, mPlayheadPos + playheadInc);
         }
         else
         {
            mPlayheadPos = std::max(0.0, mPlayheadPos - playheadInc);
         }
         currentGlobalU = mPlayheadPos;
      }
      else
      {
         // Forward Loop / BPM / Free-run
         if (direction == 0)
         {
            mPlayheadPos += playheadInc * (double)numFrames;
            mPlayheadPos -= floor(mPlayheadPos);
         }
         else
         {
            mPlayheadPos -= playheadInc * (double)numFrames;
            mPlayheadPos -= floor(mPlayheadPos);
         }
         currentGlobalU = mPlayheadPos;
      }

      mCurrentPlayhead.store((float)currentGlobalU, std::memory_order_relaxed);

      // 4. Render Synth Audio (Polyphonic or Drone)
      const bool isNoteDriven = (mNoteInbox != nullptr);
      const auto& sineTable = SpectralAdditiveDsp::FastSineTable::Instance();

      std::vector<float> blockL(numFrames, 0.0f);
      std::vector<float> blockR(numFrames, 0.0f);

      const auto renderPartialsBank = [&](double uPos, float pitchMul, float voiceGain,
                                          Voice& vState, float* destL, float* destR) {
         if (voiceGain <= 1e-5f) return;

         // Extract partial target amplitudes and base frequencies from matrix
         float targetAmpsL[kMaxPartials];
         float targetAmpsR[kMaxPartials];
         float partialFreqs[kMaxPartials];

         for (int k = 0; k < numPartials; k++)
         {
            const float normY = (float)k / (float)(numPartials - 1);
            float r, g, b, a;
            matrix.SampleBilinear((float)uPos, normY, r, g, b, a);

            SpectralAdditiveDsp::EvaluatePartialColor(r, g, b, a, colorMode, threshold, contrast, brightness, invert,
                                                     targetAmpsL[k], targetAmpsR[k]);

            const float fBase = SpectralAdditiveDsp::ComputePartialFrequency(k, numPartials, freqScale, minFreq, maxFreq, rootFreq);
            partialFreqs[k] = fBase * pitchMul;
         }

         // Block interpolation & sine oscillator accumulation
         const float invFrames = 1.0f / (float)numFrames;
         const double nyquist = mSampleRate * 0.48;

         for (int k = 0; k < numPartials; k++)
         {
            const float f = partialFreqs[k];
            if (f >= nyquist || f < 5.0f)
               continue;

            const double inc = (double)f / mSampleRate;
            const float startAmpL = vState.partials[k].ampL;
            const float startAmpR = vState.partials[k].ampR;
            const float endAmpL = targetAmpsL[k] * voiceGain;
            const float endAmpR = targetAmpsR[k] * voiceGain;
            const float stepAmpL = (endAmpL - startAmpL) * invFrames;
            const float stepAmpR = (endAmpR - startAmpR) * invFrames;

            double phase = vState.partials[k].phase;
            float curAmpL = startAmpL;
            float curAmpR = startAmpR;

            // Unison / stereo spread phase shift
            const double phaseSpread = (double)k * 0.05 * (double)stereoWidth;

            for (int i = 0; i < numFrames; i++)
            {
               const float sL = sineTable.Lookup(phase);
               const float sR = sineTable.Lookup(phase + phaseSpread);

               destL[i] += sL * curAmpL;
               destR[i] += sR * curAmpR;

               curAmpL += stepAmpL;
               curAmpR += stepAmpR;
               phase += inc;
            }

            phase -= floor(phase);
            vState.partials[k].phase = phase;
            vState.partials[k].ampL = endAmpL;
            vState.partials[k].ampR = endAmpR;
         }
      };

      if (isNoteDriven)
      {
         for (int v = 0; v < kMaxVoices; v++)
         {
            Voice& voice = mVoices[v];
            if (!voice.active) continue;

            const float env = voice.ampEnv.Process();
            if (!voice.ampEnv.IsActive() && !voice.held)
            {
               voice.active = false;
               continue;
            }

            // Portamento glide
            if (glide > 0.001f)
            {
               const float coeff = expf(-1.0f / (glide * (float)mSampleRate * (float)numFrames));
               voice.currentPitchRatio = voice.targetPitchRatio + (voice.currentPitchRatio - voice.targetPitchRatio) * coeff;
            }
            else
            {
               voice.currentPitchRatio = voice.targetPitchRatio;
            }

            const float pitch = voice.currentPitchRatio * finePitchRatio;
            const float voiceGain = env * voice.velocity * (0.4f / sqrtf((float)numPartials));

            renderPartialsBank(currentGlobalU, pitch, voiceGain, voice, blockL.data(), blockR.data());
         }
      }
      else
      {
         // Drone / Free-running generator
         const float droneGain = volume * (0.5f / sqrtf((float)numPartials));
         renderPartialsBank(currentGlobalU, finePitchRatio, droneGain, mDroneVoice, blockL.data(), blockR.data());
      }

      // 5. Analog State-Variable Filter, Drive & Master Pan
      float panL = 1.0f, panR = 1.0f;
      DspMath::EqualPowerPan(pan, panL, panR);

      for (int i = 0; i < numFrames; i++)
      {
         float sL = blockL[i];
         float sR = blockR[i];

         // Saturation / Drive
         if (drive > 0.01f)
         {
            const float driveFactor = 1.0f + drive * 4.0f;
            sL = DspMath::FastTanh(sL * driveFactor);
            sR = DspMath::FastTanh(sR * driveFactor);
         }

         // Filter
         if (filterType != SpectralAdditiveDsp::kFilterOff)
         {
            mDroneVoice.filterL.SetCutoff(cutoff, 0.5f + resonance * 9.5f);
            mDroneVoice.filterR.SetCutoff(cutoff, 0.5f + resonance * 9.5f);
            auto outFltL = mDroneVoice.filterL.Process(sL);
            auto outFltR = mDroneVoice.filterR.Process(sR);

            switch (filterType)
            {
               case SpectralAdditiveDsp::kFilterLP12:
               case SpectralAdditiveDsp::kFilterLP24:
                  sL = outFltL.low; sR = outFltR.low;
                  break;
               case SpectralAdditiveDsp::kFilterHP12:
                  sL = outFltL.high; sR = outFltR.high;
                  break;
               case SpectralAdditiveDsp::kFilterBP12:
                  sL = outFltL.band; sR = outFltR.band;
                  break;
               default: break;
            }
         }

         sL *= volume * panL;
         sR *= volume * panR;

         outL[i] = sL;
         outR[i] = sR;
      }

      // Push to UI oscilloscope
      std::vector<float> monoScope(numFrames);
      for (int i = 0; i < numFrames; i++)
         monoScope[i] = 0.5f * (outL[i] + outR[i]);
      mScopeRing.Write(monoScope.data(), numFrames);
   }

private:
   struct PartialVoiceState
   {
      double phase = 0.0;
      float ampL = 0.0f;
      float ampR = 0.0f;
   };

   struct Voice
   {
      bool active = false;
      bool held = false;
      int note = 60;
      int voiceId = 0;
      float velocity = 1.0f;
      uint64_t age = 0;
      float targetPitchRatio = 1.0f;
      float currentPitchRatio = 1.0f;
      double playheadPos = 0.0;
      int pingPongDir = 1;
      Envelope ampEnv;
      DspMath::TptSvf filterL;
      DspMath::TptSvf filterR;
      PartialVoiceState partials[kMaxPartials] = {};
   };

   int AllocateVoice()
   {
      for (int v = 0; v < kMaxVoices; v++)
      {
         if (!mVoices[v].active)
            return v;
      }
      uint64_t oldestAge = UINT64_MAX;
      int oldestIdx = 0;
      for (int v = 0; v < kMaxVoices; v++)
      {
         if (!mVoices[v].held && mVoices[v].age < oldestAge)
         {
            oldestAge = mVoices[v].age;
            oldestIdx = v;
         }
      }
      return oldestIdx;
   }

   double mSampleRate = 44100.0;
   NoteEventQueue* mNoteInbox = nullptr;
   ParamMailbox mMailbox;
   MeterRing mScopeRing;

   std::shared_ptr<SpectralAdditiveDsp::SpectrogramMatrix> mMatrices[2];
   std::atomic<int> mActiveMatrixIndex { 0 };

   // Parameter atomics
   std::atomic<int> mScanMode { SpectralAdditiveDsp::kScanBpmSync };
   std::atomic<int> mRate { MusicTime::k1Bar };
   std::atomic<int> mDirection { 0 };
   std::atomic<int> mPartialsChoice { 1 };
   std::atomic<int> mFreqScale { SpectralAdditiveDsp::kScaleLogarithmic };
   std::atomic<int> mColorMode { SpectralAdditiveDsp::kColorHuePan };
   std::atomic<bool> mInvert { false };
   std::atomic<int> mOctave { 0 };
   std::atomic<int> mSemi { 0 };
   std::atomic<int> mUnison { 1 };
   std::atomic<float> mDetune { 8.0f };
   std::atomic<int> mFilterType { SpectralAdditiveDsp::kFilterLP12 };
   std::atomic<bool> mTriggerScan { false };

   std::atomic<float> mAmpAdsr[4] { 10.0f, 200.0f, 0.85f, 250.0f };

   // Running playhead state
   double mPlayheadPos = 0.0;
   int mPingPongDir = 1;
   std::atomic<float> mCurrentPlayhead { 0.0f };

   Voice mVoices[kMaxVoices];
   Voice mDroneVoice;
   uint64_t mAgeCounter = 0;
};

// ===========================================================================
// ImageSpectralSynthNode (Main Thread GUI / Graph Node)
// ===========================================================================
ImageSpectralSynthNode::ImageSpectralSynthNode()
{
   mAudioNode = std::make_unique<AudioImageSpectralNode>();
   mAudioNode->PushParams(*this);
}

ImageSpectralSynthNode::~ImageSpectralSynthNode()
{
   if (mPreviewTex != 0) { glDeleteTextures(1, &mPreviewTex); mPreviewTex = 0; }
   if (mFbo != 0) { glDeleteFramebuffers(1, &mFbo); mFbo = 0; }
}

AudioNode* ImageSpectralSynthNode::GetAudioNode()
{
   if (!mAudioNode)
   {
      mAudioNode = std::make_unique<AudioImageSpectralNode>();
      mAudioNode->PushParams(*this);
   }
   return mAudioNode.get();
}

int ImageSpectralSynthNode::ReadScope(float* out, int capacity)
{
   return mAudioNode ? mAudioNode->ScopeRing().Read(out, capacity) : 0;
}

int ImageSpectralSynthNode::ActiveVoices() const
{
   return mAudioNode ? mAudioNode->ActiveVoices() : 0;
}

float ImageSpectralSynthNode::Playhead() const
{
   return mAudioNode ? mAudioNode->GetPlayhead() : 0.0f;
}

void ImageSpectralSynthNode::TriggerScan()
{
   if (mAudioNode)
      mAudioNode->TriggerScan();
}

void ImageSpectralSynthNode::EnsurePreviewResources(int size)
{
   if (mFbo != 0 && mPreviewSize == size)
      return;

   if (mPreviewTex != 0) glDeleteTextures(1, &mPreviewTex);
   if (mFbo != 0) glDeleteFramebuffers(1, &mFbo);

   glGenTextures(1, &mPreviewTex);
   glBindTexture(GL_TEXTURE_2D, mPreviewTex);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

   glGenFramebuffers(1, &mFbo);
   glBindFramebuffer(GL_FRAMEBUFFER, mFbo);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mPreviewTex, 0);
   glBindFramebuffer(GL_FRAMEBUFFER, 0);
   glBindTexture(GL_TEXTURE_2D, 0);

   mPreviewSize = size;
   mPixels.assign((size_t)size * size * 4, 0);
}

void ImageSpectralSynthNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;

   if (mTextureInput.IsConnected())
      mTextureInput.Pull(frameId);

   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioImageSpectralNode>();

   RenderPreview(frameId);
   mAudioNode->PushParams(*this);
}

void ImageSpectralSynthNode::RenderPreview(int /*frameId*/)
{
   const int size = 256;
   EnsurePreviewResources(size);

   const unsigned int srcTex = mTextureInput.GetSource() ? mTextureInput.GetSource()->GetOutputTexture() : 0;
   const unsigned long long currentTexRev = mTextureInput.GetSource() ? mTextureInput.GetSource()->TextureRevision() : 0;

   const bool texChanged = (currentTexRev != mLastTexRev) || (mPixels.empty());

   GLint prevFbo = 0;
   glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

   if (texChanged)
   {
      if (srcTex != 0)
      {
         GLUtil::Fbo wrapper;
         wrapper.fbo = mFbo;
         wrapper.tex = mPreviewTex;
         wrapper.w = size;
         wrapper.h = size;

         static unsigned int sBlitProg = 0;
         if (sBlitProg == 0)
         {
            const char* fs =
               "#version 150\n"
               "in vec2 vUv;\n"
               "out vec4 fragColor;\n"
               "uniform sampler2D uSrc;\n"
               "void main() {\n"
               "   fragColor = texture(uSrc, vUv);\n"
               "}\n";
            sBlitProg = GLUtil::CompileProgram(fs);
         }

         GLUtil::RunShaderPass(wrapper, sBlitProg, [srcTex]() {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, srcTex);
            glUniform1i(glGetUniformLocation(sBlitProg, "uSrc"), 0);
         });

         glBindFramebuffer(GL_FRAMEBUFFER, mFbo);
         glPixelStorei(GL_PACK_ALIGNMENT, 1);
         glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, mPixels.data());

         SpectralAdditiveDsp::SpectrogramMatrix mat(size, size);
         mat.rgba = mPixels;
         mAudioNode->SwapMatrix(mat);
      }
      else
      {
         // Render default procedurally generated spectrogram to texture & audio
         SpectralAdditiveDsp::SpectrogramMatrix defaultMat;
         glBindTexture(GL_TEXTURE_2D, mPreviewTex);
         glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, defaultMat.width, defaultMat.height,
                         GL_RGBA, GL_UNSIGNED_BYTE, defaultMat.rgba.data());
         glBindTexture(GL_TEXTURE_2D, 0);
         mPixels = defaultMat.rgba;
         mAudioNode->SwapMatrix(defaultMat);
      }
      mLastTexRev = currentTexRev;
   }

   glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
}

void ImageSpectralSynthNode::VisitParams(ParamVisitor& v)
{
   v.Int("scanMode", scanMode);
   v.Int("rate", rate);
   v.Float("scanSpeed", scanSpeed);
   v.Float("position", position);
   v.Int("direction", direction);

   v.Int("partialsChoice", partialsChoice);
   v.Int("freqScale", freqScale);
   v.Float("minFreq", minFreq);
   v.Float("maxFreq", maxFreq);
   v.Float("rootFreq", rootFreq);

   v.Int("octave", octave);
   v.Int("semi", semi);
   v.Float("fine", fine);
   v.Float("glide", glide);

   v.Int("colorMode", colorMode);
   v.Float("threshold", threshold);
   v.Float("contrast", contrast);
   v.Float("brightness", brightness);
   v.Int("invert", invert);

   v.Float("volume", volume);
   v.Float("pan", pan);
   v.Float("stereoWidth", stereoWidth);
   v.Int("unison", unison);
   v.Float("detune", detune);

   v.Int("filterType", filterType);
   v.Float("cutoff", cutoff);
   v.Float("resonance", resonance);
   v.Float("drive", drive);

   v.Float("ampAttack", ampAttack);
   v.Float("ampDecay", ampDecay);
   v.Float("ampSustain", ampSustain);
   v.Float("ampRelease", ampRelease);
}
