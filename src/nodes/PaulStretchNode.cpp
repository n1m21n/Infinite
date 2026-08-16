#include "PaulStretchNode.h"

#include <Accelerate/Accelerate.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/AudioVoice.h"
#include "audio/MeterRing.h"
#include "audio/ParamMailbox.h"
#include "audio/SampleSlot.h"
#include "platform/Platform.h"
#include "Transport.h"
#include "core/AudioTopologyRequest.h"

const int PaulStretchNode::kWindowSizes[PaulStretchNode::kNumWindowSizes] = {
   2048, 4096, 8192, 16384, 32768, 65536, 131072
};

const char* const PaulStretchNode::kWindowSizeLabels[PaulStretchNode::kNumWindowSizes] = {
   "2048 (46ms)",
   "4096 (93ms)",
   "8192 (186ms)",
   "16384 (372ms)",
   "32768 (743ms)",
   "65536 (1.49s)",
   "131072 (2.97s)"
};

namespace
{
   constexpr float kTwoPi = 6.28318530717958647692f;
   constexpr int kMaxFFTSize = 131072;
   constexpr int kMaxLog2 = 17; // 1 << 17 == 131072
   constexpr int kMaxVoices = 8;
   constexpr int kMaxRecordSeconds = 30;
   constexpr int kMaxRecordSampleRate = 192000;

   // Fast, lock-free XorShift32 PRNG for audio-thread phase randomization
   struct FastRng
   {
      uint32_t state = 2463534242u;

      inline float NextFloat()
      {
         state ^= state << 13;
         state ^= state >> 17;
         state ^= state << 5;
         return (float)(state & 0x00FFFFFF) * (1.0f / 16777216.0f);
      }

      inline float NextAngle()
      {
         return NextFloat() * kTwoPi;
      }
   };

   // Overlap-add ring buffer for accumulating time-domain window segments
   struct OverlapAddBuffer
   {
      std::vector<float> data;
      int mask = 0;
      int readPos = 0;

      void Init(int size)
      {
         // Round up to power of two
         int p2 = 1;
         while (p2 < size) p2 <<= 1;
         data.assign(p2, 0.0f);
         mask = p2 - 1;
         readPos = 0;
      }

      void Add(const float* segment, int count, int offset)
      {
         for (int i = 0; i < count; ++i)
         {
            const int idx = (offset + i) & mask;
            data[idx] += segment[i];
         }
      }

      float ReadAndClear()
      {
         const float val = data[readPos & mask];
         data[readPos & mask] = 0.0f;
         readPos = (readPos + 1) & mask;
         return val;
      }

      void Clear()
      {
         std::fill(data.begin(), data.end(), 0.0f);
         readPos = 0;
      }
   };
}

// ------------------------------------------------------------- audio thread
class AudioPaulStretchNode : public AudioNode
{
public:
   AudioPaulStretchNode()
   {
      mFftSetup = vDSP_create_fftsetup(kMaxLog2, FFT_RADIX2);

      // Precalculate Hann windows for each supported window size
      mWindows.resize(PaulStretchNode::kNumWindowSizes);
      for (int w = 0; w < PaulStretchNode::kNumWindowSizes; ++w)
      {
         const int size = PaulStretchNode::kWindowSizes[w];
         mWindows[w].resize(size);
         for (int i = 0; i < size; ++i)
         {
            mWindows[w][i] = 0.5f * (1.0f - std::cos(kTwoPi * (float)i / (float)(size - 1)));
         }
      }

      // Preallocate audio processing work buffers
      mFftInput.resize(kMaxFFTSize, 0.0f);
      mFftOutput.resize(kMaxFFTSize, 0.0f);
      mSplitReal.resize(kMaxFFTSize / 2, 0.0f);
      mSplitImag.resize(kMaxFFTSize / 2, 0.0f);
      mMagnitudes.resize(kMaxFFTSize / 2 + 1, 0.0f);
      mPhases.resize(kMaxFFTSize / 2 + 1, 0.0f);
      mUnisonReal.resize(kMaxFFTSize / 2 + 1, 0.0f);
      mUnisonImag.resize(kMaxFFTSize / 2 + 1, 0.0f);

      mOutputBuffers[0].Init(kMaxFFTSize * 2);
      mOutputBuffers[1].Init(kMaxFFTSize * 2);
   }

   ~AudioPaulStretchNode() override
   {
      if (mFftSetup != nullptr)
      {
         vDSP_destroy_fftsetup(mFftSetup);
         mFftSetup = nullptr;
      }
   }

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mSelfEnv.SetSampleRate(sampleRate);
      mSelfEnv.SetADSR(2.0f, 0.0f, 1.0f, 20.0f);

      mOutputBuffers[0].Clear();
      mOutputBuffers[1].Clear();
      mHopAccumulator = 0.0f;
      mSamplePlayPosition = 0.0;

      if (mRecordBuffer.empty())
         mRecordBuffer.resize((size_t)kMaxRecordSeconds * kMaxRecordSampleRate);
   }

   void Reset() override
   {
      mOutputBuffers[0].Clear();
      mOutputBuffers[1].Clear();
      mHopAccumulator = 0.0f;
   }

   SampleSlot& GetSampleSlot() { return mSampleSlot; }

   void PushBuffer(Platform::SampleBuffer* buf)
   {
      mSampleSlot.Push(buf);
   }

   void TriggerPreview(float frac)
   {
      mAuditionTriggerFrac.store(frac, std::memory_order_release);
      mAuditionTriggerPending.store(true, std::memory_order_release);
   }

   void StopPreview()
   {
      mAuditionStopPending.store(true, std::memory_order_release);
   }

   void StartRecording()
   {
      mRecordWritePos.store(0, std::memory_order_release);
      mRecordOverflowed.store(false, std::memory_order_release);
      mRecordingActive.store(true, std::memory_order_release);
   }

   int RecordedFrames() const
   {
      return mRecordWritePos.load(std::memory_order_acquire);
   }

   double RecordSampleRate() const
   {
      return mSampleRate;
   }

   const float* RecordBufferData() const
   {
      return mRecordBuffer.data();
   }

   void StopRecording()
   {
      mRecordingActive.store(false, std::memory_order_release);
   }

   MeterRing& PlayheadRing() { return mPlayheadRing; }
   MeterRing& ActiveVoiceRing() { return mActiveVoiceRing; }

   // Atomic parameters updated from UI/Cook
   std::atomic<float> mStretch { 8.0f };
   std::atomic<int> mWindowSizeIdx { 4 }; // 32768 default
   std::atomic<float> mPhaseRand { 1.0f };
   std::atomic<float> mPitchShift { 0.0f };
   std::atomic<float> mFineTune { 0.0f };
   std::atomic<float> mFreqShift { 0.0f };
   std::atomic<int> mUnison { 1 };
   std::atomic<float> mDetune { 10.0f };
   std::atomic<float> mVolume { 0.8f };
   std::atomic<float> mStart { 0.0f };
   std::atomic<float> mEnd { 1.0f };
   std::atomic<bool> mLoop { true };

   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& output) override
   {
      const int numFrames = output.numFrames;
      for (int ch = 0; ch < output.numChannels; ++ch)
         std::fill(output.channels[ch], output.channels[ch] + numFrames, 0.0f);

      // 1. Swap in newly loaded sample if ready
      if (mSampleSlot.SwapIn())
      {
         mSamplePlayPosition = 0.0;
         mHopAccumulator = 0.0f;
         mOutputBuffers[0].Clear();
         mOutputBuffers[1].Clear();
      }

      // 2. Handle recording from audio input slot (slot 0)
      const bool recording = mRecordingActive.load(std::memory_order_relaxed);
      if (recording && numInputs > 0 && inputs[0] != nullptr && inputs[0]->numChannels > 0)
      {
         const float* inData = inputs[0]->channels[0];
         const int writePos = mRecordWritePos.load(std::memory_order_relaxed);
         const int capacity = (int)mRecordBuffer.size();
         const int inFrames = inputs[0]->numFrames;
         if (writePos < capacity)
         {
            const int toCopy = std::min(inFrames, capacity - writePos);
            std::copy(inData, inData + toCopy, mRecordBuffer.begin() + writePos);
            mRecordWritePos.store(writePos + toCopy, std::memory_order_release);
            if (writePos + toCopy >= capacity)
               mRecordOverflowed.store(true, std::memory_order_release);
         }
      }

      Platform::SampleBuffer* activeSample = mSampleSlot.Active();
      const bool hasSample = (activeSample != nullptr && activeSample->numFrames > 0 && activeSample->channels > 0);

      // 3. Handle audition commands
      if (mAuditionTriggerPending.exchange(false, std::memory_order_acq_rel))
      {
         const float frac = mAuditionTriggerFrac.load(std::memory_order_relaxed);
         if (hasSample)
         {
            mSamplePlayPosition = (double)std::clamp(frac, 0.0f, 1.0f) * (double)activeSample->numFrames;
         }
         mAuditionActive = true;
         mSelfEnv.NoteOn();
      }
      if (mAuditionStopPending.exchange(false, std::memory_order_acq_rel))
      {
         mAuditionActive = false;
         mSelfEnv.NoteOff();
      }

      const bool transportRunning = Transport::Instance().IsPlaying();
      const bool freeRunning = transportRunning;

      const bool shouldSound = hasSample && (mAuditionActive || freeRunning || mSelfEnv.IsActive());

      if (!shouldSound)
      {
         const float zero = 0.0f;
         mActiveVoiceRing.Write(&zero, 1);
         return;
      }

      // 4. Read parameters
      const float stretchFactor = std::max(1.0f, mStretch.load(std::memory_order_relaxed));
      const int winIdx = std::clamp(mWindowSizeIdx.load(std::memory_order_relaxed), 0, PaulStretchNode::kNumWindowSizes - 1);
      const int currentFFTSize = PaulStretchNode::kWindowSizes[winIdx];
      const int log2N = 11 + winIdx; // 2048 -> 11, ..., 131072 -> 17
      const float phaseRand = std::clamp(mPhaseRand.load(std::memory_order_relaxed), 0.0f, 1.0f);
      const float userPitchShift = mPitchShift.load(std::memory_order_relaxed);
      const float fineTune = mFineTune.load(std::memory_order_relaxed);
      const float freqShiftHz = mFreqShift.load(std::memory_order_relaxed);
      const int unisonVoices = std::clamp(mUnison.load(std::memory_order_relaxed), 1, kMaxVoices);
      const float detuneCents = mDetune.load(std::memory_order_relaxed);
      const float volume = mVolume.load(std::memory_order_relaxed);
      const float startFrac = std::clamp(mStart.load(std::memory_order_relaxed), 0.0f, 1.0f);
      const float endFrac = std::clamp(mEnd.load(std::memory_order_relaxed), 0.0f, 1.0f);
      const bool loop = mLoop.load(std::memory_order_relaxed);

      const int sampleFrames = activeSample->numFrames;
      const int numSampleChannels = activeSample->channels;
      const float* ch0Data = activeSample->channelData.data();
      const float* ch1Data = (numSampleChannels >= 2) ? (ch0Data + sampleFrames) : ch0Data;

      double loopStart = (double)sampleFrames * std::min((double)startFrac, (double)endFrac);
      double loopEnd = (double)sampleFrames * std::max((double)startFrac, (double)endFrac);
      if (loopEnd <= loopStart + 1.0)
         loopEnd = std::min((double)sampleFrames, loopStart + (double)currentFFTSize);

      if (mSamplePlayPosition < loopStart || mSamplePlayPosition >= loopEnd)
         mSamplePlayPosition = loopStart;

      // 5. Overlap-add hop processing loop
      const int hopSize = currentFFTSize / 4; // 75% overlap
      mHopAccumulator += (float)numFrames;
      const float readStep = (float)hopSize / stretchFactor;

      const float* window = mWindows[winIdx].data();
      const int numSpectrumBins = currentFFTSize / 2; // split complex size

      while (mHopAccumulator >= (float)hopSize)
      {
         mHopAccumulator -= (float)hopSize;

         for (size_t ch = 0; ch < 2; ++ch)
         {
            const float* srcChannelData = (ch == 0) ? ch0Data : ch1Data;
            const int intPlayPos = (int)mSamplePlayPosition;

            // Extract input window with boundary / loop wrapping
            for (int i = 0; i < currentFFTSize; ++i)
            {
               int sampleIdx = intPlayPos + i;
               if ((double)sampleIdx >= loopEnd)
               {
                  if (loop && loopEnd > loopStart)
                  {
                     const int span = (int)(loopEnd - loopStart);
                     sampleIdx = (int)loopStart + ((sampleIdx - (int)loopEnd) % std::max(1, span));
                  }
                  else
                  {
                     sampleIdx = (int)loopEnd - 1;
                  }
               }
               if (sampleIdx < 0) sampleIdx = 0;
               if (sampleIdx >= sampleFrames) sampleIdx = sampleFrames - 1;

               mFftInput[i] = srcChannelData[sampleIdx] * window[i];
            }

            // Real-to-complex FFT via Apple vDSP
            DSPSplitComplex splitComplex;
            splitComplex.realp = mSplitReal.data();
            splitComplex.imagp = mSplitImag.data();

            vDSP_ctoz((const DSPComplex*)mFftInput.data(), 2, &splitComplex, 1, numSpectrumBins);
            vDSP_fft_zrip(mFftSetup, &splitComplex, 1, log2N, FFT_FORWARD);

            // Compute polar representation (magnitudes & original phases)
            // DC bin
            const float dcVal = splitComplex.realp[0] * 0.5f;
            mMagnitudes[0] = std::abs(dcVal);
            mPhases[0] = 0.0f;

            // Nyquist bin is packed in imagp[0]
            const float nyqVal = splitComplex.imagp[0] * 0.5f;
            mMagnitudes[numSpectrumBins] = std::abs(nyqVal);
            mPhases[numSpectrumBins] = 0.0f;

            for (int k = 1; k < numSpectrumBins; ++k)
            {
               const float re = splitComplex.realp[k] * 0.5f;
               const float im = splitComplex.imagp[k] * 0.5f;
               mMagnitudes[k] = std::sqrt(re * re + im * im);
               mPhases[k] = std::atan2(im, re);
            }

            // Spectral transformation: pitch shift, frequency shift, unison detune & phase randomization
            const int totalFreqBins = numSpectrumBins + 1;
            const float freqShiftBins = freqShiftHz * (float)currentFFTSize / (float)mSampleRate;
            const bool hasPitchOrUnison = (std::abs(userPitchShift) > 0.01f ||
                                           std::abs(fineTune) > 0.01f ||
                                           std::abs(freqShiftHz) > 0.01f ||
                                           unisonVoices > 1);

            std::fill(mUnisonReal.begin(), mUnisonReal.begin() + totalFreqBins, 0.0f);
            std::fill(mUnisonImag.begin(), mUnisonImag.begin() + totalFreqBins, 0.0f);

            for (int v = 0; v < unisonVoices; ++v)
            {
               const float voiceDetuneCents = (unisonVoices <= 1) ? 0.0f :
                  (-detuneCents + (2.0f * detuneCents * (float)v / (float)(unisonVoices - 1)));
               const float totalSemis = userPitchShift + (fineTune + voiceDetuneCents) / 100.0f;
               const float pitchRatio = std::pow(2.0f, -totalSemis / 12.0f);

               for (int k = 0; k < totalFreqBins; ++k)
               {
                  const float sourceK = (float)k * pitchRatio - freqShiftBins;
                  if (sourceK >= 0.0f && sourceK < (float)(totalFreqBins - 1))
                  {
                     const int k1 = (int)sourceK;
                     const int k2 = k1 + 1;
                     const float frac = sourceK - (float)k1;
                     const float mag = mMagnitudes[k1] * (1.0f - frac) + mMagnitudes[k2] * frac;

                     if (mag > 1e-7f)
                     {
                        const float srcPhase = mPhases[k1];
                        const float randPhase = mRng.NextAngle();
                        // Interpolate phase from exact preservation to complete random smear
                        const float finalPhase = srcPhase + phaseRand * (randPhase - srcPhase);
                        mUnisonReal[k] += mag * std::cos(finalPhase);
                        mUnisonImag[k] += mag * std::sin(finalPhase);
                     }
                  }
               }
            }

            // Normalize unison sum
            const float unisonScale = 1.0f / std::sqrt((float)unisonVoices);

            // Reconstruct split complex spectrum
            // DC & Nyquist
            splitComplex.realp[0] = mUnisonReal[0] * unisonScale;
            splitComplex.imagp[0] = mUnisonReal[numSpectrumBins] * unisonScale;

            for (int k = 1; k < numSpectrumBins; ++k)
            {
               if (!hasPitchOrUnison && phaseRand < 0.001f)
               {
                  // Bit-accurate passthrough reconstruction
                  splitComplex.realp[k] = splitComplex.realp[k] * 0.5f;
                  splitComplex.imagp[k] = splitComplex.imagp[k] * 0.5f;
               }
               else
               {
                  splitComplex.realp[k] = mUnisonReal[k] * unisonScale;
                  splitComplex.imagp[k] = mUnisonImag[k] * unisonScale;
               }
            }

            // Inverse FFT
            vDSP_fft_zrip(mFftSetup, &splitComplex, 1, log2N, FFT_INVERSE);
            vDSP_ztoc(&splitComplex, 1, (DSPComplex*)mFftOutput.data(), 2, numSpectrumBins);

            // Synthesis windowing and normalization
            // 75% overlap of Hann^2 window produces a constant sum of 1.5 * (FFT size / 4)
            // vDSP scaling gives 2.0x, so normalizer is (1.0 / (FFTSize * 1.5))
            const float normScale = 1.0f / ((float)currentFFTSize * 1.5f);
            for (int i = 0; i < currentFFTSize; ++i)
            {
               mFftOutput[i] *= window[i] * normScale;
            }

            const int writeOffset = mOutputBuffers[ch].readPos;
            mOutputBuffers[ch].Add(mFftOutput.data(), currentFFTSize, writeOffset);
         }

         // Advance playhead
         mSamplePlayPosition += (double)readStep;
         if (mSamplePlayPosition >= loopEnd)
         {
            if (loop && loopEnd > loopStart)
            {
               mSamplePlayPosition = loopStart + std::fmod(mSamplePlayPosition - loopEnd, loopEnd - loopStart);
            }
            else
            {
               mSamplePlayPosition = loopEnd - 1.0;
            }
         }
      }

      // 6. Stream from overlap-add output buffer to audio frame block
      float* outL = (output.numChannels > 0) ? output.channels[0] : nullptr;
      float* outR = (output.numChannels > 1) ? output.channels[1] : outL;

      for (int i = 0; i < numFrames; ++i)
      {
         const float envGain = (freeRunning && !mAuditionActive) ? 1.0f : mSelfEnv.Process();
         const float sampleL = mOutputBuffers[0].ReadAndClear() * volume * envGain;
         const float sampleR = mOutputBuffers[1].ReadAndClear() * volume * envGain;
         if (outL != nullptr)
            outL[i] = sampleL;
         if (outR != nullptr && outR != outL)
            outR[i] = sampleR;
      }

      // Publish playhead & active status
      if (sampleFrames > 0)
      {
         const float playheadFrac = (float)(mSamplePlayPosition / (double)sampleFrames);
         mPlayheadRing.Write(&playheadFrac, 1);
      }
      const float activeVal = (mAuditionActive || freeRunning || mSelfEnv.IsActive()) ? 1.0f : 0.0f;
      mActiveVoiceRing.Write(&activeVal, 1);
   }

private:
   double mSampleRate = 44100.0;
   FastRng mRng;
   FFTSetup mFftSetup = nullptr;
   std::vector<std::vector<float>> mWindows;

   std::vector<float> mFftInput;
   std::vector<float> mFftOutput;
   std::vector<float> mSplitReal;
   std::vector<float> mSplitImag;
   std::vector<float> mMagnitudes;
   std::vector<float> mPhases;
   std::vector<float> mUnisonReal;
   std::vector<float> mUnisonImag;

   OverlapAddBuffer mOutputBuffers[2];
   float mHopAccumulator = 0.0f;
   double mSamplePlayPosition = 0.0;

   SampleSlot mSampleSlot;
   Envelope mSelfEnv;

   bool mAuditionActive = false;

   std::atomic<bool> mAuditionTriggerPending { false };
   std::atomic<float> mAuditionTriggerFrac { 0.0f };
   std::atomic<bool> mAuditionStopPending { false };

   std::atomic<bool> mRecordingActive { false };
   std::atomic<int> mRecordWritePos { 0 };
   std::atomic<bool> mRecordOverflowed { false };
   std::vector<float> mRecordBuffer;

   MeterRing mPlayheadRing;
   MeterRing mActiveVoiceRing;
};

// ------------------------------------------------------------- main thread
PaulStretchNode::PaulStretchNode() = default;
PaulStretchNode::~PaulStretchNode() = default;

void PaulStretchNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   if (mAudioNode == nullptr)
      mAudioNode = std::make_unique<AudioPaulStretchNode>();

   mAudioNode->GetSampleSlot().DrainRetired();

   // Push parameters to audio thread
   mAudioNode->mStretch.store(stretch, std::memory_order_relaxed);
   mAudioNode->mWindowSizeIdx.store(windowSizeIndex, std::memory_order_relaxed);
   mAudioNode->mPhaseRand.store(phaseRand, std::memory_order_relaxed);
   mAudioNode->mPitchShift.store(pitchShift, std::memory_order_relaxed);
   mAudioNode->mFineTune.store(fineTune, std::memory_order_relaxed);
   mAudioNode->mFreqShift.store(freqShift, std::memory_order_relaxed);
   mAudioNode->mUnison.store(unison, std::memory_order_relaxed);
   mAudioNode->mDetune.store(detune, std::memory_order_relaxed);
   mAudioNode->mVolume.store(volume, std::memory_order_relaxed);
   mAudioNode->mStart.store(start, std::memory_order_relaxed);
   mAudioNode->mEnd.store(end, std::memory_order_relaxed);
   mAudioNode->mLoop.store(loop, std::memory_order_relaxed);

   float ph = 0.0f;
   if (mAudioNode->PlayheadRing().ReadLatest(ph))
      mPlayhead = ph;

   float active = 0.0f;
   if (mAudioNode->ActiveVoiceRing().ReadLatest(active))
      mIsPlaying = (active > 0.5f);
}

void PaulStretchNode::VisitParams(ParamVisitor& v)
{
   v.Text("path", mFilePath);
   v.Float("stretch", stretch);
   v.Int("window_size", windowSizeIndex);
   v.Float("phase_rand", phaseRand);
   v.Float("pitch_shift", pitchShift);
   v.Float("fine_tune", fineTune);
   v.Float("freq_shift", freqShift);
   v.Int("unison", unison);
   v.Float("detune", detune);
   v.Float("volume", volume);
   v.Float("start", start);
   v.Float("end", end);
   v.Bool("loop", loop);
}

AudioNode* PaulStretchNode::GetAudioNode()
{
   if (mAudioNode == nullptr)
      mAudioNode = std::make_unique<AudioPaulStretchNode>();
   return mAudioNode.get();
}

void PaulStretchNode::TriggerPreview(float frac)
{
   if (mAudioNode == nullptr)
      mAudioNode = std::make_unique<AudioPaulStretchNode>();
   mAudioNode->TriggerPreview(frac);
   mSelfOwnedByUser = true;
}

void PaulStretchNode::StopPreview()
{
   if (mAudioNode != nullptr)
      mAudioNode->StopPreview();
   mSelfOwnedByUser = false;
}

void PaulStretchNode::StartRecording()
{
   if (mAudioNode == nullptr)
      mAudioNode = std::make_unique<AudioPaulStretchNode>();
   mRecording = true;
   mStatus = "recording...";
   mAudioNode->StartRecording();
   AudioTopologyRequest::Request();
}

void PaulStretchNode::StopRecording()
{
   if (!mRecording || mAudioNode == nullptr)
      return;
   mRecording = false;
   AudioTopologyRequest::Request();

   mAudioNode->StopRecording();
   const int frames = mAudioNode->RecordedFrames();
   if (frames <= 0)
   {
      mStatus = "recording was empty";
      return;
   }

   auto* decoded = new Platform::SampleBuffer();
   decoded->channels = 1;
   decoded->numFrames = frames;
   decoded->sampleRate = mAudioNode->RecordSampleRate();
   decoded->channelData.assign(mAudioNode->RecordBufferData(), mAudioNode->RecordBufferData() + frames);

   FinishBuffer(decoded, "recording", "", "recorded buffer");
}

bool PaulStretchNode::LoadFile(const std::string& path)
{
   if (path.empty())
      return false;

   auto* decoded = new Platform::SampleBuffer();
   std::string error;
   if (!Platform::DecodeAudioFileToBuffer(path, *decoded, error))
   {
      delete decoded;
      mStatus = error.empty() ? "failed to load" : error;
      return false;
   }

   const size_t slash = path.find_last_of('/');
   const std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
   FinishBuffer(decoded, name, path, "loaded");
   return true;
}

void PaulStretchNode::ReloadFromPath()
{
   if (!mFilePath.empty())
      LoadFile(mFilePath);
}

void PaulStretchNode::FinishBuffer(Platform::SampleBuffer* decoded, const std::string& fileName,
                                   const std::string& filePath, const std::string& status)
{
   if (mAudioNode == nullptr)
      mAudioNode = std::make_unique<AudioPaulStretchNode>();

   mFileName = fileName;
   mFilePath = filePath;

   const int totalFrames = decoded->numFrames;
   const float durationSec = (decoded->sampleRate > 0.0) ? (float)totalFrames / (float)decoded->sampleRate : 0.0f;
   char stat[128];
   snprintf(stat, sizeof(stat), "%.2fs  -  %dch %dHz", durationSec, decoded->channels, (int)decoded->sampleRate);
   mStatus = stat;

   // Build min/max waveform cache for GUI from channel 0
   waveformCacheCount = std::min(kWaveformCacheSize, totalFrames);
   if (waveformCacheCount > 0)
   {
      const int framesPerBucket = std::max(1, totalFrames / waveformCacheCount);
      for (int b = 0; b < waveformCacheCount; ++b)
      {
         float mn = 0.0f, mx = 0.0f;
         const int bucketStart = b * framesPerBucket;
         const int bucketEnd = std::min(totalFrames, bucketStart + framesPerBucket);
         for (int i = bucketStart; i < bucketEnd; ++i)
         {
            mn = std::min(mn, decoded->channelData[i]);
            mx = std::max(mx, decoded->channelData[i]);
         }
         waveformMin[b] = mn;
         waveformMax[b] = mx;
      }
   }

   mAudioNode->PushBuffer(decoded);
}
