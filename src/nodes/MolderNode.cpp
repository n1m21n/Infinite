#include "MolderNode.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/AudioVoice.h"
#include "audio/MeterRing.h"
#include "audio/SampleSlot.h"
#include "core/AudioDecodeCache.h"
#include "platform/Platform.h"
#include "Transport.h"
#include "core/AudioTopologyRequest.h"
#include "audio/WavWriter.h"

namespace
{
   constexpr int kMaxRecordSeconds = 30;
   constexpr int kMaxRecordSampleRate = 192000;

   // channel is a source-buffer channel index (0 = left/mono, 1 = right);
   // channels are stored contiguous per Platform::SampleBuffer's convention
   // ("channel 0 then channel 1, each numFrames long" - Platform.h).
   float ReadSample(const Platform::SampleBuffer& buf, double pos, int channel = 0)
   {
      const float* chan = buf.channelData.data() + (size_t)channel * buf.numFrames;
      const int i0 = (int)pos;
      if (i0 < 0 || i0 >= buf.numFrames - 1)
         return (i0 >= 0 && i0 < buf.numFrames) ? chan[i0] : 0.0f;
      const float frac = (float)(pos - i0);
      const float a = chan[i0];
      const float b = chan[i0 + 1];
      return a + (b - a) * frac;
   }

   // Shared advance+edge-handling for the self lane's playback position -
   // copied from SamplerNode.cpp:52-80 rather than shared, since Molder has
   // only ever the one lane (no per-voice speedSign variation needed here,
   // so this call site always passes 1.0f for it). Returns true if the
   // voice hit a non-looping edge and should release.
   bool AdvanceVoicePosition(double& pos, int& dir, bool loop, bool pingpong, float rate, float speedSign,
                              double startPos, double endPos)
   {
      const float dirSign = (float)dir * speedSign;
      pos += rate * dirSign;

      const bool hitEnd = dirSign > 0.0f && pos >= endPos;
      const bool hitStart = dirSign < 0.0f && pos <= startPos;
      bool shouldRelease = false;
      if (hitEnd || hitStart)
      {
         if (loop && pingpong)
         {
            dir = -dir;
            pos = hitEnd ? endPos : startPos;
         }
         else if (loop)
         {
            pos = hitEnd ? startPos : endPos;
         }
         else
         {
            shouldRelease = true;
         }
      }

      // A handle drag can move the range under a voice already in flight -
      // clamp unconditionally rather than only in the loop/ping-pong branches.
      pos = std::clamp(pos, startPos, endPos);
      return shouldRelease;
   }
}

// ------------------------------------------------------------- audio thread
// Single self lane playing back whatever the worker last rendered - no note
// input any more (Molder is a roll-and-audition sound designer, not a
// playable sampler), driven by the audition button, the transport free-run
// convention, or a click on the waveform. Supports the same start/end/loop/
// reverse/ping-pong range transport as SamplerNode.
class AudioMolderNode : public AudioNode
{
public:
   AudioMolderNode() = default;

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mSelfEnv.SetSampleRate(sampleRate);
      mSelfEnv.SetADSR(2.0f, 0.0f, 1.0f, 15.0f);
      if (mRecordBuffer.empty())
         mRecordBuffer.resize((size_t)kMaxRecordSeconds * kMaxRecordSampleRate);
   }

   SampleSlot& GetSampleSlot() { return mSampleSlot; }
   void PushBuffer(Platform::SampleBuffer* buf) { mSampleSlot.Push(buf); }

   void TriggerPreview(float frac)
   {
      mPreviewFrac.store(frac, std::memory_order_release);
   }
   void StopPreview() { mStopRequested.store(true, std::memory_order_release); }

   void StartRecording()
   {
      mRecordWritePos.store(0, std::memory_order_release);
      mRecordingActive.store(true, std::memory_order_release);
   }
   void StopRecording() { mRecordingActive.store(false, std::memory_order_release); }
   int RecordedFrames() const { return mRecordWritePos.load(std::memory_order_acquire); }
   double RecordSampleRate() const { return mSampleRate; }
   const float* RecordBufferData() const { return mRecordBuffer.data(); }

   std::atomic<float> mLevel { 0.8f };

   // Plain atomics, not ParamMailbox, matching mLevel above and
   // SamplerNode's own start/end/reverse handling (SamplerNode.cpp:518-522) -
   // these are transport state, not smoothed synthesis params.
   std::atomic<float> mStart { 0.0f };
   std::atomic<float> mEnd { 1.0f };
   std::atomic<bool> mLoop { false };
   std::atomic<bool> mReverse { false };
   std::atomic<bool> mPingpong { false };

   MeterRing& PlayheadRing() { return mPlayheadRing; }
   MeterRing& ActiveVoiceRing() { return mActiveVoiceRing; }

   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& buffer) override
   {
      if (mSampleSlot.SwapIn())
         mActiveBuffer = mSampleSlot.Active();

      // Slot 0 is this node's only input now that the note pin is gone.
      const AudioBuffer* recordSrc = (numInputs > 0) ? inputs[0] : nullptr;
      if (mRecordingActive.load(std::memory_order_relaxed) && recordSrc != nullptr && recordSrc->numChannels > 0)
      {
         int pos = mRecordWritePos.load(std::memory_order_relaxed);
         const int cap = (int)mRecordBuffer.size();
         for (int i = 0; i < recordSrc->numFrames && pos < cap; i++, pos++)
            mRecordBuffer[pos] = recordSrc->channels[0][i];
         mRecordWritePos.store(pos, std::memory_order_release);
      }

      for (int ch = 0; ch < buffer.numChannels; ch++)
         std::fill(buffer.channels[ch], buffer.channels[ch] + buffer.numFrames, 0.0f);

      const float startFrac = mStart.load(std::memory_order_relaxed);

      if (mActiveBuffer == nullptr || mActiveBuffer->numFrames <= 0)
      {
         const float zero = 0.0f;
         mActiveVoiceRing.Write(&zero, 1);
         mPlayheadRing.Write(&startFrac, 1);
         return;
      }

      if (mStopRequested.exchange(false, std::memory_order_acq_rel))
         mSelfEnv.NoteOff();

      const float endFrac = std::max(startFrac + 0.001f, mEnd.load(std::memory_order_relaxed));
      const bool loop = mLoop.load(std::memory_order_relaxed);
      const bool pingpong = mPingpong.load(std::memory_order_relaxed);
      const bool reverseOn = mReverse.load(std::memory_order_relaxed);
      const double startPos = (double)startFrac * mActiveBuffer->numFrames;
      const double endPos = (double)endFrac * mActiveBuffer->numFrames;

      const float previewFrac = mPreviewFrac.exchange(-1.0f, std::memory_order_acq_rel);
      if (previewFrac >= 0.0f)
      {
         // A click outside [start, end] starts from the range's start
         // rather than from the click point - same rule SamplerNode's
         // waveform click uses, for the same reason (a click past the
         // range would trigger a voice already past its own bound).
         const float target = (previewFrac < startFrac || previewFrac > endFrac) ? startFrac : previewFrac;
         mSelfPos = (double)target * mActiveBuffer->numFrames;
         mSelfEnv.NoteOn();
      }

      const bool transportPlaying = Transport::Instance().IsPlaying();
      if (transportPlaying && !mTransportWasPlaying)
      {
         mSelfPos = (reverseOn && !pingpong) ? endPos : startPos;
         mSelfEnv.NoteOn();
      }
      mTransportWasPlaying = transportPlaying;

      const float level = mLevel.load(std::memory_order_relaxed);

      const bool sourceStereo = mActiveBuffer->channels > 1;

      for (int i = 0; i < buffer.numFrames; i++)
      {
         float sampleL = 0.0f, sampleR = 0.0f;

         if (mSelfEnv.IsActive())
         {
            // Outside a ping-pong bounce, direction tracks the live reverse
            // toggle every sample - see SamplerNode.cpp:336-338's comment,
            // copied verbatim: without this, flipping "rev" mid-playback
            // does nothing until retrigger, and turning "p-p" off after a
            // bounce leaves the voice stuck playing backward forever.
            if (!pingpong)
               mSelfDir = reverseOn ? -1 : 1;

            const float env = mSelfEnv.Process();
            sampleL = ReadSample(*mActiveBuffer, mSelfPos, 0) * env;
            sampleR = sourceStereo ? ReadSample(*mActiveBuffer, mSelfPos, 1) * env : sampleL;

            if (AdvanceVoicePosition(mSelfPos, mSelfDir, loop, pingpong, 1.0f, 1.0f, startPos, endPos))
               mSelfEnv.NoteOff();
         }

         sampleL *= level;
         sampleR *= level;

         // Mono output bus: fold to a plain average rather than dropping the
         // right channel. Stereo-or-wider: left to channel 0, right to
         // channel 1 and every channel beyond that (matches the previous
         // mono-duplicated-to-all-channels behaviour when the source has no
         // width of its own).
         if (buffer.numChannels == 1)
            buffer.channels[0][i] = 0.5f * (sampleL + sampleR);
         else
         {
            if (buffer.numChannels > 0)
               buffer.channels[0][i] = sampleL;
            for (int ch = 1; ch < buffer.numChannels; ch++)
               buffer.channels[ch][i] = sampleR;
         }
      }

      // Park the published playhead on the start marker when nothing is
      // sounding, rather than freezing at the last position -
      // SamplerNode.cpp:363-366 does this and explains why: a frozen
      // playhead reads as "stuck", not "idle".
      const float ph = mSelfEnv.IsActive() ? (float)(mSelfPos / (double)mActiveBuffer->numFrames) : startFrac;
      mPlayheadRing.Write(&ph, 1);
      const float active = mSelfEnv.IsActive() ? 1.0f : 0.0f;
      mActiveVoiceRing.Write(&active, 1);
   }

private:
   double mSampleRate = 44100.0;
   SampleSlot mSampleSlot;
   Platform::SampleBuffer* mActiveBuffer = nullptr;
   int mSelfDir = 1;

   Envelope mSelfEnv;
   double mSelfPos = 0.0;
   bool mTransportWasPlaying = false;

   std::atomic<float> mPreviewFrac { -1.0f };
   std::atomic<bool> mStopRequested { false };

   std::atomic<bool> mRecordingActive { false };
   std::atomic<int> mRecordWritePos { 0 };
   std::vector<float> mRecordBuffer;

   MeterRing mPlayheadRing;
   MeterRing mActiveVoiceRing;
};

// ------------------------------------------------------------- main thread
MolderNode::MolderNode() = default;

MolderNode::~MolderNode()
{
   mAbort.store(true, std::memory_order_release);
   if (mWorkerThread.joinable())
      mWorkerThread.join();
}

AudioNode* MolderNode::GetAudioNode()
{
   if (mAudioNode == nullptr)
      mAudioNode = std::make_unique<AudioMolderNode>();
   return mAudioNode.get();
}

MolderDsp::Genome MolderNode::BuildDesiredGenome() const
{
   MolderDsp::Genome g;
   {
      MolderDsp::Rng rng(mSeed);
      for (int i = 0; i < mGeneration; ++i)
         MolderDsp::Mutate(g, chaos, rng);
   }

   g.tonalAmount *= 2.0f * tone;
   g.noiseAmount *= 2.0f * air;
   g.transientAmount *= 2.0f * snap;

   const float stretchKnobFactor = 0.8f + stretch * 0.4f; // 0..1 -> 0.8..1.2, default 0.5 -> 1.0
   g.harmonicStretch *= stretchKnobFactor;
   g.inharmonicity += std::max(0.0f, (stretch - 0.5f)) * 3.0f;

   const float warp = (time - 0.5f) * 2.0f;
   g.attackScale *= powf(2.0f, -warp);
   g.decayScale *= powf(2.0f, warp);

   g.pitchShiftSemitones += pitch;

   return g;
}

void MolderNode::LaunchJob(Job job, std::vector<float> sourceOverride, double sourceOverrideSR,
                           bool isOriginalSource)
{
   if (mWorking.exchange(true, std::memory_order_acq_rel))
      return; // one job at a time
   if (mWorkerThread.joinable())
      mWorkerThread.join();

   mCurrentJob = job;
   mAbort.store(false, std::memory_order_release);
   mResultReady.store(false, std::memory_order_relaxed);

   const MolderDsp::Analysis analysisCopy = mAnalysis;
   const std::vector<float> sourceCopy = sourceOverride.empty() ? mOriginalMono : std::move(sourceOverride);
   const double srCopy = sourceOverride.empty() ? mOriginalSR : sourceOverrideSR;
   const MolderDsp::Genome genome = BuildDesiredGenome();
   const bool doAnalyze = (job == Job::AnalyzeThenRender);
   std::atomic<bool>* abortPtr = &mAbort;
   std::atomic<bool>* readyPtr = &mResultReady;
   PendingResult* pending = &mPendingResult;

   mLastDispatched.seed = mSeed;
   mLastDispatched.generation = mGeneration;
   mLastDispatched.chaos = chaos;
   mLastDispatched.pitch = pitch;
   mLastDispatched.tone = tone;
   mLastDispatched.air = air;
   mLastDispatched.snap = snap;
   mLastDispatched.stretchP = stretch;
   mLastDispatched.timeP = time;

   std::atomic<bool>* workingPtr = &mWorking;

   mWorkerThread = std::thread([pending, analysisCopy, sourceCopy, srCopy, genome, doAnalyze, isOriginalSource,
                                abortPtr, readyPtr, workingPtr]() mutable
   {
      MolderDsp::Analysis analysis = analysisCopy;
      bool didAnalyze = false;
      if (doAnalyze)
      {
         MolderDsp::Analyze(sourceCopy.data(), (int)sourceCopy.size(), srCopy, analysis, abortPtr);
         didAnalyze = true;
      }

      if (abortPtr->load(std::memory_order_relaxed))
      {
         workingPtr->store(false, std::memory_order_release);
         return;
      }

      std::vector<float> rendered;
      std::vector<float> renderedR;
      if (analysis.valid)
         MolderDsp::Render(analysis, genome, rendered, abortPtr, &renderedR);

      if (abortPtr->load(std::memory_order_relaxed))
      {
         workingPtr->store(false, std::memory_order_release);
         return;
      }

      pending->analysisValid = analysis.valid;
      pending->analysis = std::move(analysis);
      pending->rendered = std::move(rendered);
      pending->renderedR = std::move(renderedR);
      pending->renderedSR = srCopy;
      pending->genomeUsed = genome;
      pending->didAnalyze = didAnalyze;
      pending->isOriginalSource = isOriginalSource;

      readyPtr->store(true, std::memory_order_release);
      workingPtr->store(false, std::memory_order_release);
   });
}

void MolderNode::JoinWorkerIfDone()
{
   if (!mResultReady.load(std::memory_order_acquire))
      return;

   if (mPendingResult.didAnalyze)
   {
      mAnalysis = mPendingResult.analysis;
      RebuildPartialCache();
      // Only a genuine new source (LoadFile/StopRecording) redefines "the
      // original" Reset() goes back to - an Iterate() re-analysis must
      // never touch these, or generation 0 gets permanently redefined as
      // whatever got iterated and Reset can no longer reach the real file.
      if (mPendingResult.isOriginalSource)
      {
         mOriginalAnalysis = mPendingResult.analysis;
         // mOriginalMono/mOriginalSR were already set synchronously by
         // LoadFile()/StopRecording() before dispatch - nothing to do here.
      }
   }

   if (mPendingResult.analysisValid && !mPendingResult.rendered.empty())
   {
      const int numFrames = (int)mPendingResult.rendered.size();
      const bool hasR = mPendingResult.renderedR.size() == mPendingResult.rendered.size();

      auto* buf = new Platform::SampleBuffer();
      buf->channels = hasR ? 2 : 1;
      buf->numFrames = numFrames;
      buf->sampleRate = mPendingResult.renderedSR;
      buf->channelData.resize((size_t)numFrames * buf->channels);
      std::copy(mPendingResult.rendered.begin(), mPendingResult.rendered.end(), buf->channelData.begin());
      if (hasR)
         std::copy(mPendingResult.renderedR.begin(), mPendingResult.renderedR.end(),
                    buf->channelData.begin() + numFrames);

      RebuildWaveformCache(mPendingResult.rendered);

      if (mAudioNode == nullptr)
         mAudioNode = std::make_unique<AudioMolderNode>();
      mAudioNode->PushBuffer(buf);

      // Whatever just rendered is what Iterate() re-analyzes next - kept
      // separate from mOriginalMono/mOriginalSR (see MolderNode.h's
      // comment) so Reset() always has the real loaded file to go back to.
      mLastRenderedMono = mPendingResult.rendered;
      mLastRenderedSR = mPendingResult.renderedSR;

      char stat[160];
      const float durSec = mLastRenderedSR > 0.0 ? (float)buf->numFrames / (float)mLastRenderedSR : 0.0f;
      snprintf(stat, sizeof(stat), "f0 %.1f Hz  -  gen %d  -  %.2fs", mAnalysis.globalF0, mGeneration, durSec);
      mStatus = stat;
   }
   else if (mPendingResult.didAnalyze)
   {
      mStatus = "analysis failed";
   }

   mResultReady.store(false, std::memory_order_relaxed);
}

void MolderNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   if (mAudioNode == nullptr)
      mAudioNode = std::make_unique<AudioMolderNode>();

   mAudioNode->GetSampleSlot().DrainRetired();
   mAudioNode->mLevel.store(level, std::memory_order_relaxed);
   mAudioNode->mStart.store(start, std::memory_order_relaxed);
   mAudioNode->mEnd.store(end, std::memory_order_relaxed);
   mAudioNode->mLoop.store(loop, std::memory_order_relaxed);
   mAudioNode->mReverse.store(reverse, std::memory_order_relaxed);
   mAudioNode->mPingpong.store(pingpong, std::memory_order_relaxed);

   JoinWorkerIfDone();

   float ph = 0.0f;
   if (mAudioNode->PlayheadRing().ReadLatest(ph))
      mPlayhead = ph;
   float active = 0.0f;
   if (mAudioNode->ActiveVoiceRing().ReadLatest(active))
      mIsPlaying = (active > 0.5f);

   // Live-knob re-render, debounced: only when idle and something the
   // rendered genome actually depends on has changed since the last
   // dispatch. This whole check is a handful of scalar compares - the
   // expensive Mutate-replay + full-buffer Render only happen inside the
   // worker job this triggers, never here.
   if (mCooldownFrames > 0)
      --mCooldownFrames;
   if (!mWorking.load(std::memory_order_relaxed) && mCooldownFrames == 0 && mAnalysis.valid)
   {
      DispatchSnapshot now;
      now.seed = mSeed;
      now.generation = mGeneration;
      now.chaos = chaos;
      now.pitch = pitch;
      now.tone = tone;
      now.air = air;
      now.snap = snap;
      now.stretchP = stretch;
      now.timeP = time;
      if (!(now == mLastDispatched))
      {
         LaunchJob(Job::RenderOnly);
         mCooldownFrames = 3; // ~50ms at 60fps - enough to keep a dragged slider from spamming threads
      }
   }
}

void MolderNode::VisitParams(ParamVisitor& v)
{
   v.Text("path", mFilePath);
   v.Float("chaos", chaos);
   v.Float("pitch", pitch);
   v.Float("tone", tone);
   v.Float("air", air);
   v.Float("snap", snap);
   v.Float("stretch", stretch);
   v.Float("time", time);
   v.Float("level", level);
   v.Float("start", start);
   v.Float("end", end);
   v.Bool("loop", loop);
   v.Bool("reverse", reverse);
   v.Bool("pingpong", pingpong);
   int seedInt = (int)mSeed;
   v.Int("seed", seedInt);
   mSeed = (uint32_t)seedInt;
   v.Int("generation", mGeneration);
}

float MolderNode::HarmonicityPercent() const
{
   if (!mAnalysis.valid || mAnalysis.numPartials <= 0)
      return -1.0f;
   float err = 0.0f;
   int n = 0;
   for (int h = 0; h < mAnalysis.numPartials; ++h)
   {
      const float harmonic = (float)(h + 1);
      if (mAnalysis.envelopeAmp[h] < 1e-5f)
         continue;
      err += std::fabs(mAnalysis.measuredRatio[h] - harmonic) / harmonic;
      ++n;
   }
   // No partial had any measurable energy (an unpitched source) - "nothing
   // to measure", not "perfectly harmonic". See HarmonicityPercent's header
   // comment for the -1 sentinel the UI checks for.
   if (n == 0)
      return -1.0f;
   const float avgErr = err / (float)n;
   return std::clamp(100.0f * (1.0f - avgErr), 0.0f, 100.0f);
}

void MolderNode::RebuildWaveformCache(const std::vector<float>& mono)
{
   const int total = (int)mono.size();
   waveformCacheCount = std::min(kWaveformCacheSize, total);
   if (waveformCacheCount <= 0)
      return;
   const int perBucket = std::max(1, total / waveformCacheCount);
   for (int b = 0; b < waveformCacheCount; ++b)
   {
      float mn = 0.0f, mx = 0.0f;
      const int start = b * perBucket;
      const int end = std::min(total, start + perBucket);
      for (int i = start; i < end; ++i)
      {
         mn = std::min(mn, mono[i]);
         mx = std::max(mx, mono[i]);
      }
      waveformMin[b] = mn;
      waveformMax[b] = mx;
   }
}

void MolderNode::RebuildPartialCache()
{
   partialBarCount = std::min(kPartialBarCount, mAnalysis.numPartials);
   for (int i = 0; i < partialBarCount; ++i)
      partialBars[i] = mAnalysis.valid ? mAnalysis.envelopeAmp[i] : 0.0f;
}

bool MolderNode::LoadFile(const std::string& path)
{
   if (path.empty())
      return false;

   auto* decoded = new Platform::SampleBuffer();
   std::string error;
   if (!AudioDecodeCache::DecodeCached(path, *decoded, error))
   {
      delete decoded;
      mStatus = error.empty() ? "failed to load" : error;
      return false;
   }

   const size_t slash = path.find_last_of('/');
   const std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);

   const double sr = decoded->sampleRate;
   std::vector<float> mono(decoded->numFrames, 0.0f);
   const int channels = std::max(1, decoded->channels);
   for (int ch = 0; ch < channels; ++ch)
   {
      const float* chData = decoded->channelData.data() + (size_t)ch * decoded->numFrames;
      for (int i = 0; i < decoded->numFrames; ++i)
         mono[i] += chData[i] / (float)channels;
   }
   delete decoded;

   mFileName = name;
   mFilePath = path;
   mStatus = "analyzing...";
   mOriginalMono = mono;
   mOriginalSR = sr;

   std::random_device rd;
   mSeed = rd();
   mGeneration = 0;

   LaunchJob(Job::AnalyzeThenRender, mono, sr, /*isOriginalSource=*/true);
   return true;
}

void MolderNode::ReloadFromPath()
{
   if (!mFilePath.empty())
      LoadFile(mFilePath);
}

void MolderNode::StartRecording()
{
   if (mAudioNode == nullptr)
      mAudioNode = std::make_unique<AudioMolderNode>();
   mRecording = true;
   mStatus = "recording...";
   mAudioNode->StartRecording();
   AudioTopologyRequest::Request();
}

void MolderNode::StopRecording()
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

   const double sr = mAudioNode->RecordSampleRate();
   const float* data = mAudioNode->RecordBufferData();

   const std::string wavPath = AudioRecordings::GenerateFilePath("molder");
   AudioRecordings::WriteWav(wavPath, data, frames, sr, 1);

   std::vector<float> mono(data, data + frames);
   mFileName = "recording";
   mFilePath = wavPath;
   mStatus = "analyzing...";
   mOriginalMono = mono;
   mOriginalSR = sr;

   std::random_device rd;
   mSeed = rd();
   mGeneration = 0;

   LaunchJob(Job::AnalyzeThenRender, mono, sr, /*isOriginalSource=*/true);
}

void MolderNode::Iterate()
{
   if (!mAnalysis.valid || mLastRenderedMono.empty() || mWorking.load(std::memory_order_relaxed))
      return;
   mStatus = "iterating...";
   LaunchJob(Job::AnalyzeThenRender, mLastRenderedMono, mLastRenderedSR, /*isOriginalSource=*/false);
}

void MolderNode::Roll()
{
   ++mGeneration;
   if (mAnalysis.valid && !mWorking.load(std::memory_order_relaxed))
      LaunchJob(Job::RenderOnly);
}

void MolderNode::Reset()
{
   mGeneration = 0;
   // Restore the six knobs BuildDesiredGenome() layers on top of the
   // (now generation-0) genome - otherwise "generation 0" only sounds like
   // the original source when those also happen to already be neutral.
   // chaos is left alone (it only affects the *next* roll, never the
   // current render) and so is level (output gain, not part of the genome).
   tone = air = snap = stretch = time = 0.5f;
   pitch = 0.0f;
   if (mOriginalAnalysis.valid)
   {
      mAnalysis = mOriginalAnalysis;
      RebuildPartialCache();
   }
   // No !mWorking guard: the state change above must happen regardless of
   // whether a job is in flight. LaunchJob no-ops safely on a busy worker
   // via its own mWorking.exchange(true) guard; CookIfNeeded's dirty-check
   // notices the mismatch and re-dispatches once idle.
   LaunchJob(Job::RenderOnly);
}

void MolderNode::RollBack()
{
   mGeneration = std::max(0, mGeneration - 1);
   if (mAnalysis.valid && !mWorking.load(std::memory_order_relaxed))
      LaunchJob(Job::RenderOnly);
}

void MolderNode::TriggerPreview(float frac)
{
   if (mAudioNode == nullptr)
      mAudioNode = std::make_unique<AudioMolderNode>();
   mAudioNode->TriggerPreview(frac);
   mSelfOwnedByUser = true;
}

void MolderNode::StopPreview()
{
   if (mAudioNode != nullptr)
      mAudioNode->StopPreview();
   mSelfOwnedByUser = false;
}
