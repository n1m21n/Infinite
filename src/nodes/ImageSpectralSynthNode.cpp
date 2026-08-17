#include "ImageSpectralSynthNode.h"

#include <GLFW/glfw3.h>
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
#include "audio/SampleSlot.h"
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

   constexpr float kVoicePhaseSeed[ImageSpectralSynthNode::kMaxUnison] = {
      0.0f, 0.5f, 0.25f, 0.75f, 0.125f, 0.625f, 0.375f, 0.875f
   };

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

   // Shared wrap/bounce/clamp logic for a scan position, factored out so the
   // global playhead and each voice's own per-note retrigger playhead (item
   // 16) can't drift out of sync by keeping two independent copies of it.
   inline double AdvancePlayhead(int scanMode, int direction, double playheadInc, int numFrames,
                                 float manualPos, double& pos, int& pingPongDir)
   {
      if (scanMode == SpectralAdditiveDsp::kScanManual)
      {
         pos = std::clamp((double)manualPos, 0.0, 1.0);
         return pos;
      }
      if (scanMode == SpectralAdditiveDsp::kScanPingPong)
      {
         pos += playheadInc * (double)numFrames * (double)pingPongDir;
         if (pos >= 1.0)
         {
            pos = 1.0;
            pingPongDir = -1;
         }
         else if (pos <= 0.0)
         {
            pos = 0.0;
            pingPongDir = 1;
         }
         return pos;
      }
      if (scanMode == SpectralAdditiveDsp::kScanOneShot)
      {
         if (direction == 0)
            pos = std::min(1.0, pos + playheadInc * (double)numFrames);
         else
            pos = std::max(0.0, pos - playheadInc * (double)numFrames);
         return pos;
      }
      // Forward Loop / BPM / Free-run
      if (direction == 0)
      {
         pos += playheadInc * (double)numFrames;
         pos -= floor(pos);
      }
      else
      {
         pos -= playheadInc * (double)numFrames;
         pos -= floor(pos);
      }
      return pos;
   }
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
   static constexpr int kMaxUnison = ImageSpectralSynthNode::kMaxUnison;

   AudioImageSpectralNode()
   {
      // Seed an initial matrix (the same procedural default pattern as
      // before) and adopt it immediately - the audio thread isn't running
      // yet at construction time, so SwapIn() can be called directly here
      // rather than waiting for the first ProcessBlock.
      mMatrixSlot.Push(new SpectralAdditiveDsp::SpectrogramMatrix());
      mMatrixSlot.SwapIn();
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

      mBlockL.resize(4096, 0.0f);
      mBlockR.resize(4096, 0.0f);
      mMonoScope.resize(4096, 0.0f);

      for (int k = 0; k < kMaxPartials; k++)
      {
         for (int u = 0; u < kMaxUnison; u++)
         {
            mDroneVoice.partials[k].phase[u] = (double)kVoicePhaseSeed[u] * 0.5;
         }
      }
   }

   ~AudioImageSpectralNode() override = default;

   NoteEventQueue* NoteOutbox() override { return nullptr; }
   void SetNoteInbox(NoteEventQueue* inbox, int cursor) override
   {
      mNoteInbox = inbox;
      mNoteCursor = cursor;
   }

   void PrepareToPlay(double sampleRate, int maxBlockSize) override
   {
      mSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
      mMailbox.PrepareToPlay(mSampleRate);
      for (int v = 0; v < kMaxVoices; v++)
      {
         mVoices[v].ampEnv.SetSampleRate(mSampleRate);
      }
      for (int stage = 0; stage < 2; stage++)
      {
         mFilterL[stage].SetSampleRate(mSampleRate);
         mFilterL[stage].Reset();
         mFilterR[stage].SetSampleRate(mSampleRate);
         mFilterR[stage].Reset();
      }

      const int allocSize = std::max(maxBlockSize, 4096);
      mBlockL.resize(allocSize, 0.0f);
      mBlockR.resize(allocSize, 0.0f);
      mMonoScope.resize(allocSize, 0.0f);
   }

   // Main thread only. See WaveTerrainNode's identical SwapBank comment and
   // SampleSlot.h: this replaces a hand-rolled two-slot double buffer where
   // a second main-thread swap inside one audio block could overwrite the
   // very matrix the audio thread was reading that block.
   void SwapMatrix(const SpectralAdditiveDsp::SpectrogramMatrix& newMatrix)
   {
      mMatrixSlot.Push(new SpectralAdditiveDsp::SpectrogramMatrix(newMatrix));
   }

   // Main thread only, once per CookIfNeeded.
   void DrainRetiredMatrices() { mMatrixSlot.DrainRetired(); }

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
      mPerVoiceScan.store(n.perVoiceScan, std::memory_order_relaxed);

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
      return mActiveVoices.load(std::memory_order_relaxed);
   }

   MeterRing& ScopeRing() { return mScopeRing; }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      const int numFrames = output.numFrames;
      float* outL = output.channels[0];
      float* outR = output.channels[1];
      std::fill_n(outL, numFrames, 0.0f);
      std::fill_n(outR, numFrames, 0.0f);

      // Adopt a freshly pushed matrix only here, at the top of the block
      // (never mid-block) - see SampleSlot.h's contract.
      mMatrixSlot.SwapIn();
      const SpectralAdditiveDsp::SpectrogramMatrix& matrix = *mMatrixSlot.Active();

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
      const int unison = std::clamp(mUnison.load(std::memory_order_relaxed), 1, kMaxUnison);
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
         const int numEvts = mNoteInbox->Pop(mNoteCursor, evts, 64);
         for (int i = 0; i < numEvts; i++)
         {
            const auto& event = evts[i];
            if (event.isNoteOn && event.velocity > 0.0f)
            {
               const int vIdx = AllocateVoice();
               Voice& v = mVoices[vIdx];
               // AllocateVoice() returns either a genuinely idle voice or
               // steals the oldest released-but-still-decaying one; either
               // way this note-on is a fresh allocation. Only a voice that
               // was still active (mid-decay from whatever it was playing
               // before) has a currentPitchRatio worth gliding from - a
               // never-triggered/fully-reset voice's ratio (1.0 = middle C)
               // is not a real previous pitch, so seed it immediately
               // instead of gliding away from it.
               const bool wasActive = v.active;
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
               mLastTriggeredVoice = vIdx;

               for (int k = 0; k < kMaxPartials; k++)
               {
                  for (int u = 0; u < kMaxUnison; u++)
                  {
                     v.partials[k].phase[u] = (double)kVoicePhaseSeed[u] * 0.5;
                  }
               }

               const float baseHz = MidiNoteToHz(event.note + octave * 12 + semi);
               v.targetPitchRatio = baseHz / 261.63f; // relative to Middle C
               if (!wasActive)
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
      // ParamMailbox::SmoothedValue advances its one-pole smoother by exactly
      // one sample per call. Calling it once per block (as this used to)
      // meant every smoothed param slewed numFrames times slower than
      // intended - a cutoff move could take seconds to arrive. Advance every
      // slot numFrames times so the smoother actually reaches (or nearly
      // reaches) its target within the block; kNumSmoothedParams (16) x
      // numFrames one-pole steps is negligible cost. `volume` additionally
      // keeps its block-start value so the output loop below can ramp it
      // linearly across the block instead of stepping it - the one param
      // where a once-per-block jump is audible as a click on a fast fade.
      float paramStart[kNumSmoothedParams];
      float paramEnd[kNumSmoothedParams];
      for (int i = 0; i < numFrames; i++)
      {
         for (int p = 0; p < kNumSmoothedParams; p++)
         {
            const float val = mMailbox.SmoothedValue(p);
            if (i == 0) paramStart[p] = val;
            paramEnd[p] = val;
         }
      }

      const float volume = paramEnd[kParamVolume];
      const float volumeStart = paramStart[kParamVolume];
      const float pan = paramEnd[kParamPan];
      const float stereoWidth = paramEnd[kParamStereoWidth];
      const float glide = paramEnd[kParamGlide];
      const float scanSpeed = paramEnd[kParamScanSpeed];
      const float manualPos = paramEnd[kParamPosition];
      const float minFreq = paramEnd[kParamMinFreq];
      const float maxFreq = paramEnd[kParamMaxFreq];
      const float rootFreq = paramEnd[kParamRootFreq];
      const float cutoff = paramEnd[kParamCutoff];
      const float resonance = paramEnd[kParamResonance];
      const float drive = paramEnd[kParamDrive];
      const float fine = paramEnd[kParamFine];
      const float threshold = paramEnd[kParamThreshold];
      const float contrast = paramEnd[kParamContrast];
      const float brightness = paramEnd[kParamBrightness];

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

      // Update Global Playhead for UI / Drone. Factored into AdvancePlayhead
      // (below) so the per-voice retrigger path added for item 16 shares the
      // exact same wrap/bounce/clamp logic instead of a second copy that
      // could drift out of sync with this one.
      const double currentGlobalU = AdvancePlayhead(scanMode, direction, playheadInc, numFrames,
                                                     manualPos, mPlayheadPos, mPingPongDir);

      const bool perVoiceScan = mPerVoiceScan.load(std::memory_order_relaxed);

      // 4. Render Synth Audio (Polyphonic or Drone)
      const bool isNoteDriven = (mNoteInbox != nullptr);
      const auto& sineTable = SpectralAdditiveDsp::FastSineTable::Instance();

      if (mBlockL.size() < (size_t)numFrames)
      {
         mBlockL.resize(numFrames, 0.0f);
         mBlockR.resize(numFrames, 0.0f);
         mMonoScope.resize(numFrames, 0.0f);
      }

      std::fill_n(mBlockL.data(), numFrames, 0.0f);
      std::fill_n(mBlockR.data(), numFrames, 0.0f);

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
         const float unisonNorm = (unison > 1) ? (1.0f / sqrtf((float)unison)) : 1.0f;

         for (int k = 0; k < numPartials; k++)
         {
            const float fBase = partialFreqs[k];
            const float startAmpL = vState.partials[k].ampL;
            const float startAmpR = vState.partials[k].ampR;
            const float endAmpL = targetAmpsL[k] * voiceGain;
            const float endAmpR = targetAmpsR[k] * voiceGain;
            const float stepAmpL = (endAmpL - startAmpL) * invFrames;
            const float stepAmpR = (endAmpR - startAmpR) * invFrames;

            // Stereo width phase offset per partial
            const double phaseSpread = (double)k * 0.05 * (double)stereoWidth;

            for (int u = 0; u < unison; u++)
            {
               const float spread = (unison > 1)
                  ? detuneCents * 0.5f * (2.0f * (float)u / (float)(unison - 1) - 1.0f)
                  : 0.0f;
               const float detunePitchRatio = (spread != 0.0f) ? powf(2.0f, spread / 1200.0f) : 1.0f;
               const float f = fBase * detunePitchRatio;

               if (f >= nyquist || f < 5.0f)
                  continue;

               const double inc = (double)f / mSampleRate;

               float uPanL = 1.0f, uPanR = 1.0f;
               if (unison > 1)
               {
                  const float spreadPan = stereoWidth * (2.0f * (float)u / (float)(unison - 1) - 1.0f);
                  DspMath::EqualPowerPan(std::clamp(spreadPan, -1.0f, 1.0f), uPanL, uPanR);
                  // EqualPowerPan at center is 0.7071, scale so center is 1.0
                  uPanL *= 1.41421356f;
                  uPanR *= 1.41421356f;
               }

               const float gainL = unisonNorm * uPanL;
               const float gainR = unisonNorm * uPanR;

               double phase = vState.partials[k].phase[u];
               float curAmpL = startAmpL;
               float curAmpR = startAmpR;

               for (int i = 0; i < numFrames; i++)
               {
                  const float sL = sineTable.Lookup(phase);
                  const float sR = sineTable.Lookup(phase + phaseSpread);

                  destL[i] += sL * curAmpL * gainL;
                  destR[i] += sR * curAmpR * gainR;

                  curAmpL += stepAmpL;
                  curAmpR += stepAmpR;
                  phase += inc;
               }

               phase -= floor(phase);
               vState.partials[k].phase[u] = phase;
            }

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

            // Envelope::Process() advances exactly one sample (see
            // Envelope::SegmentInc in AudioVoice.h). Calling it once per
            // block made a 10ms attack take ~441 blocks (~5s at 512/44.1k)
            // and made every ADSR time depend on the audio buffer size.
            // Advance it numFrames times so a block's worth of envelope
            // time actually elapses; the per-partial startAmp/endAmp ramp
            // already interpolates smoothly across the block using
            // whatever env value lands at the end, exactly as it did
            // before, just now advancing at the correct rate.
            float env = 0.0f;
            for (int i = 0; i < numFrames; i++)
               env = voice.ampEnv.Process();

            if (!voice.ampEnv.IsActive() && !voice.held)
            {
               voice.active = false;
               continue;
            }

            // Portamento glide. The coefficient is a per-block one-pole
            // decay: over `numFrames` samples the response should decay by
            // exp(-numFrames / (glide * sampleRate)) - numFrames belongs in
            // the numerator, not the denominator (the old formula divided
            // by it instead, which for any reasonable glide time evaluated
            // to ~1.0 per block, i.e. the pitch effectively never moved).
            if (glide > 0.001f)
            {
               const float coeff = expf(-(float)numFrames / (glide * (float)mSampleRate));
               voice.currentPitchRatio = voice.targetPitchRatio + (voice.currentPitchRatio - voice.targetPitchRatio) * coeff;
            }
            else
            {
               voice.currentPitchRatio = voice.targetPitchRatio;
            }

            const float pitch = voice.currentPitchRatio * finePitchRatio;
            const float voiceGain = env * voice.velocity * (0.4f / sqrtf((float)numPartials));

            // Per-voice scan retrigger (item 16): each voice was already
            // seeded with its own playheadPos/pingPongDir at note-on but
            // neither field was ever advanced or read - every voice rendered
            // at the shared currentGlobalU regardless, so a chord always
            // scanned the image in lockstep. When enabled, advance this
            // voice's own position through the same wrap/bounce/clamp logic
            // as the global playhead and read the image from there instead.
            double voiceU = currentGlobalU;
            if (perVoiceScan)
               voiceU = AdvancePlayhead(scanMode, direction, playheadInc, numFrames,
                                        manualPos, voice.playheadPos, voice.pingPongDir);

            renderPartialsBank(voiceU, pitch, voiceGain, voice, mBlockL.data(), mBlockR.data());
         }

         // The UI readout is only meaningful as "the" playhead when every
         // voice shares one; in per-voice mode there is no single playhead,
         // so report whichever voice was triggered most recently instead of
         // the (now voice-local) global counter.
         if (perVoiceScan && mLastTriggeredVoice >= 0 && mLastTriggeredVoice < kMaxVoices &&
             mVoices[mLastTriggeredVoice].active)
         {
            mCurrentPlayhead.store((float)mVoices[mLastTriggeredVoice].playheadPos, std::memory_order_relaxed);
         }
         else
         {
            mCurrentPlayhead.store((float)currentGlobalU, std::memory_order_relaxed);
         }
      }
      else
      {
         // Drone / Free-running generator
         const float droneGain = volume * (0.5f / sqrtf((float)numPartials));
         renderPartialsBank(currentGlobalU, finePitchRatio, droneGain, mDroneVoice, mBlockL.data(), mBlockR.data());
         mCurrentPlayhead.store((float)currentGlobalU, std::memory_order_relaxed);
      }

      // Publish the active-voice count once per block via an atomic rather
      // than letting the main thread's ActiveVoices() loop over mVoices[].active
      // directly - that was a plain bool shared mutable field read across the
      // two-object boundary with no synchronisation, which the audio-node
      // rules prohibit outright (Wave Terrain already does this correctly).
      {
         int count = isNoteDriven ? 0 : 1;
         if (isNoteDriven)
         {
            for (int v = 0; v < kMaxVoices; v++)
            {
               if (mVoices[v].active)
                  count++;
            }
         }
         mActiveVoices.store(count, std::memory_order_relaxed);
      }

      // 5. Analog State-Variable Filter, Drive & Master Pan
      float panL = 1.0f, panR = 1.0f;
      DspMath::EqualPowerPan(pan, panL, panR);

      if (filterType != SpectralAdditiveDsp::kFilterOff)
      {
         const float q = 0.5f + resonance * 9.5f;
         mFilterL[0].SetCutoff(cutoff, q);
         mFilterR[0].SetCutoff(cutoff, q);
         if (filterType == SpectralAdditiveDsp::kFilterLP24)
         {
            mFilterL[1].SetCutoff(cutoff, q);
            mFilterR[1].SetCutoff(cutoff, q);
         }
      }

      const float volumeStep = (volume - volumeStart) / (float)numFrames;
      float curVolume = volumeStart;

      for (int i = 0; i < numFrames; i++)
      {
         float sL = mBlockL[i];
         float sR = mBlockR[i];

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
            auto outFltL = mFilterL[0].Process(sL);
            auto outFltR = mFilterR[0].Process(sR);

            switch (filterType)
            {
               case SpectralAdditiveDsp::kFilterLP12:
                  sL = outFltL.low;
                  sR = outFltR.low;
                  break;
               case SpectralAdditiveDsp::kFilterLP24:
               {
                  auto outFltL2 = mFilterL[1].Process(outFltL.low);
                  auto outFltR2 = mFilterR[1].Process(outFltR.low);
                  sL = outFltL2.low;
                  sR = outFltR2.low;
                  break;
               }
               case SpectralAdditiveDsp::kFilterHP12:
                  sL = outFltL.high;
                  sR = outFltR.high;
                  break;
               case SpectralAdditiveDsp::kFilterBP12:
                  sL = outFltL.band;
                  sR = outFltR.band;
                  break;
               default: break;
            }
         }

         sL *= curVolume * panL;
         sR *= curVolume * panR;
         curVolume += volumeStep;

         outL[i] = sL;
         outR[i] = sR;
      }

      // Push to UI oscilloscope
      for (int i = 0; i < numFrames; i++)
         mMonoScope[i] = 0.5f * (outL[i] + outR[i]);
      mScopeRing.Write(mMonoScope.data(), numFrames);
   }

private:
   struct PartialVoiceState
   {
      double phase[ImageSpectralSynthNode::kMaxUnison] = {};
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
   int mNoteCursor = -1;
   ParamMailbox mMailbox;
   MeterRing mScopeRing;

   SampleSlotT<SpectralAdditiveDsp::SpectrogramMatrix> mMatrixSlot;

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
   std::atomic<bool> mPerVoiceScan { false };
   int mLastTriggeredVoice = -1;

   std::atomic<float> mAmpAdsr[4] { 10.0f, 200.0f, 0.85f, 250.0f };

   // Running playhead state
   double mPlayheadPos = 0.0;
   int mPingPongDir = 1;
   std::atomic<float> mCurrentPlayhead { 0.0f };

   Voice mVoices[kMaxVoices];
   Voice mDroneVoice;
   uint64_t mAgeCounter = 0;
   std::atomic<int> mActiveVoices { 0 };

   DspMath::TptSvf mFilterL[2];
   DspMath::TptSvf mFilterR[2];

   std::vector<float> mBlockL;
   std::vector<float> mBlockR;
   std::vector<float> mMonoScope;
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

void ImageSpectralSynthNode::PushParams()
{
   if (mAudioNode)
      mAudioNode->PushParams(*this);
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

   mAudioNode->DrainRetiredMatrices();

   RenderPreview(frameId);
   mAudioNode->PushParams(*this);
}

void ImageSpectralSynthNode::RenderPreview(int /*frameId*/)
{
   // No GL context in headless test/sweep runs (no window created) - all GL
   // calls below (including EnsurePreviewResources's glGenTextures/
   // glGenFramebuffers, and the glReadPixels/RunShaderPass calls further
   // down) are unsafe without one. The node still produces audio: the
   // SpectrogramMatrix default constructor already generates a procedural
   // pattern for both bank slots, so a headless run is silent-but-sane
   // rather than uninitialised.
   if (glfwGetCurrentContext() == nullptr)
      return;

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
   v.Bool("perVoiceScan", perVoiceScan);

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
