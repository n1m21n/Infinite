#include "nodes/SpatialMixerNode.h"
#include "platform/HeadTracker.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>

#include "audio/AudioNode.h"
#include "audio/DspMath.h"
#include "audio/AudioEngine.h"
#include "audio/MeterRing.h"
#include "audio/dsp/BinauralKernel.h"
#include "audio/dsp/HrtfVoice.h"
#include "audio/dsp/SpatialMaster.h"

float SpatialMixerNode::DefaultAzimuth(int slot)
{
   // 0, +30, -30, +60, -60 ... then the rear half.
   const int k = (slot + 1) / 2;
   const float a = 30.0f * (float)k;
   const float az = (slot % 2 == 1) ? a : -a;
   return slot == 0 ? 0.0f : (az > 180.0f ? az - 360.0f : az);
}

namespace
{
   constexpr int kSlots = SpatialMixerNode::kMaxSlots;

   // One block's worth of parameters, copied out of the atomics once.
   struct BlockParams
   {
      float az[kSlots], el[kSlots], dist[kSlots], width[kSlots], gainDb[kSlots];
      bool mute[kSlots], solo[kSlots], locked[kSlots];
      bool anySolo = false;
      float outDb = 0.0f, room = 0.0f, bassHz = 0.0f;
      int renderMode = 0, hrtf = 0;
      bool limiter = true;
   };

   // Everything that turns the input buffers into one stereo mix. The node owns
   // two: the monitor (head-rotated) and, only while recording with tracking
   // on, the file render (always facing front).
   class SpatialRenderer
   {
   public:
      void Prepare(double sr, const Hrtf::Set* set)
      {
         mSr = sr;
         for (int s = 0; s < kSlots; s++)
            for (int v = 0; v < 2; v++)
            {
               mModel[s][v].Prepare(sr);
               mHrtf[s][v].Prepare(set, sr);
            }
         mRoom.Prepare(sr);
         mBass.Prepare(sr);
         mLimiter.Prepare(sr);
         mLimiter.SetCeilingDb(-1.0f);
         mLoud.Prepare(sr);
         mOutGain.SetTimeConstant(0.01f, sr);
         mOutGain.SetImmediate(1.0f);
         }

      void Reset()
      {
         for (int s = 0; s < kSlots; s++)
            for (int v = 0; v < 2; v++)
            {
               mModel[s][v].Reset();
               mHrtf[s][v].Reset();
            }
         mRoom.Reset();
         mBass.Reset();
         mLimiter.Reset();
         mLoud.Reset();
      }

      int Latency() const { return mLimiter.Latency(); }

      void Render(const BlockParams& P, const AudioBuffer* const* inputs, int numInputs, int n, float yaw,
                  float* outL, float* outR, float* channelPeak)
      {
         std::fill(outL, outL + n, 0.0f);
         std::fill(outR, outR + n, 0.0f);
         std::fill(mSend, mSend + n, 0.0f);
         const bool binaural = P.renderMode == 0;
         const bool useHrtf = P.hrtf == 0;
         const int slots = std::min(numInputs, kSlots);
         for (int s = 0; s < slots; s++)
         {
            const AudioBuffer* in = inputs[s];
            if (in == nullptr || in->numChannels < 1)
               continue; // unconnected: ~0 CPU
            if (P.mute[s] || (P.anySolo && !P.solo[s]))
               continue;
            const float gain = DspMath::DbToLinear(P.gainDb[s]);
            const float az = P.locked[s] ? P.az[s] : P.az[s] - yaw;
            const float el = P.el[s], dist = P.dist[s];
            const float w = std::clamp(P.width[s], 0.0f, 1.0f);
            const bool stereo = in->numChannels >= 2 && w > 0.001f;

            if (!binaural)
            {
               // Stereo (speaker-safe): equal-power pan from azimuth, 1/r distance, no ear filtering.
               const float g = gain / std::max(dist, 1.0f);
               float pl, pr;
               DspMath::EqualPowerPan(std::clamp(std::sin(az * 0.017453292f) * std::cos(el * 0.017453292f), -1.0f, 1.0f), pl, pr);
               pl *= (float)M_SQRT2;
               pr *= (float)M_SQRT2;
               float pk = 0.0f;
               for (int i = 0; i < n; i++)
               {
                  float m = in->channels[0][i];
                  if (in->numChannels >= 2)
                     m = 0.5f * (m + in->channels[1][i]);
                  m *= g;
                  outL[i] += m * pl;
                  outR[i] += m * pr;
                  pk = std::max(pk, std::fabs(m));
               }
               channelPeak[s] = std::max(channelPeak[s], pk);
               continue;
            }

            if (useHrtf)
            {
               if (stereo)
               {
                  mHrtf[s][0].SetTarget(az - 30.0f * w, el, dist, n);
                  mHrtf[s][1].SetTarget(az + 30.0f * w, el, dist, n);
               }
               else
                  mHrtf[s][0].SetTarget(az, el, dist, n);
            }
            else if (stereo)
            {
               mModel[s][0].SetTarget(Binaural::ComputeTarget(az - 30.0f * w, el, dist, mSr));
               mModel[s][1].SetTarget(Binaural::ComputeTarget(az + 30.0f * w, el, dist, mSr));
            }
            else
               mModel[s][0].SetTarget(Binaural::ComputeTarget(az, el, dist, mSr));

            float peak = 0.0f;
            for (int i = 0; i < n; i++)
            {
               if (stereo)
               {
                  const float l = in->channels[0][i] * gain, r = in->channels[1][i] * gain;
                  if (useHrtf)
                  {
                     mHrtf[s][0].Process(l, outL[i], outR[i]);
                     mHrtf[s][1].Process(r, outL[i], outR[i]);
                  }
                  else
                  {
                     mModel[s][0].Process(l, outL[i], outR[i]);
                     mModel[s][1].Process(r, outL[i], outR[i]);
                  }
                  mSend[i] += 0.5f * (l + r);
                  peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
               }
               else
               {
                  float m = in->channels[0][i];
                  if (in->numChannels >= 2)
                     m = 0.5f * (m + in->channels[1][i]);
                  m *= gain;
                  if (useHrtf)
                     mHrtf[s][0].Process(m, outL[i], outR[i]);
                  else
                     mModel[s][0].Process(m, outL[i], outR[i]);
                  mSend[i] += m;
                  peak = std::max(peak, std::fabs(m));
               }
            }
            channelPeak[s] = std::max(channelPeak[s], peak);
         }

         // Master: room send (constant per source, so distance reads as a
         // higher room-to-direct ratio), bass mono, out gain, limiter, meters.
         mRoom.SetRoom(binaural ? P.room : 0.0f);
         mBass.SetHz(P.bassHz);
         mLimiter.BeginBlock();
         const float outLin = DspMath::DbToLinear(P.outDb);
         float tp = 0.0f;
         for (int i = 0; i < n; i++)
         {
            mRoom.Process(mSend[i] * 0.5f, outL[i], outR[i]);
            mBass.Process(outL[i], outR[i]);
            const float g = mOutGain.Process(outLin);
            outL[i] *= g;
            outR[i] *= g;
            mLimiter.Process(outL[i], outR[i], P.limiter);
            mLoud.Process(outL[i], outR[i]);
            // Post-limiter inter-sample peak estimate (cubic midpoint).
            const float x = std::max(std::fabs(outL[i]), std::fabs(outR[i]));
            tp = std::max(tp, x);
         }
         mBlockPeak = tp;
         mBlockReductionDb = mLimiter.BlockMinGainDb();
      }

      float BlockPeak() const { return mBlockPeak; }
      float BlockReductionDb() const { return mBlockReductionDb; }
      float Momentary() const { return mLoud.Momentary(); }
      float ShortTerm() const { return mLoud.ShortTerm(); }

   private:
      double mSr = 48000.0;
      Binaural::Voice mModel[kSlots][2];
      Hrtf::Voice mHrtf[kSlots][2];
      SpatialMaster::Room mRoom;
      SpatialMaster::BassMono mBass;
      SpatialMaster::Limiter mLimiter;
      SpatialMaster::Loudness mLoud;
      DspMath::OnePole mOutGain;
      float mSend[kAudioMaxBlockFrames] = {};
      float mBlockPeak = 0.0f, mBlockReductionDb = 0.0f;
   };
}

class AudioSpatialMixerNode : public AudioNode
{
public:
   explicit AudioSpatialMixerNode(AudioCaptureRing* ring) : mRing(ring) {}

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSr = sampleRate;
      mHrtfSet = Hrtf::Set::Get(sampleRate); // cached; main thread
      mMon.Prepare(sampleRate, mHrtfSet.get());
      mFile.Prepare(sampleRate, mHrtfSet.get());
   }

   void Reset() override
   {
      mMon.Reset();
      mFile.Reset();
   }

   int LatencySamples() const override { return mMon.Latency(); }

   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& output) override
   {
      const int n = std::min(output.numFrames, kAudioMaxBlockFrames);
      for (int ch = 0; ch < output.numChannels; ch++)
         std::fill(output.channels[ch], output.channels[ch] + output.numFrames, 0.0f);
      if (output.numChannels < 2)
         return;

      BlockParams P;
      for (int s = 0; s < kSlots; s++)
      {
         P.az[s] = mAz[s].load(std::memory_order_relaxed);
         P.el[s] = mEl[s].load(std::memory_order_relaxed);
         P.dist[s] = mDist[s].load(std::memory_order_relaxed);
         P.width[s] = mWidth[s].load(std::memory_order_relaxed);
         P.gainDb[s] = mGainDb[s].load(std::memory_order_relaxed);
         P.mute[s] = mMute[s].load(std::memory_order_relaxed) != 0;
         P.solo[s] = mSolo[s].load(std::memory_order_relaxed) != 0;
         P.locked[s] = mLocked[s].load(std::memory_order_relaxed) != 0;
         P.anySolo = P.anySolo || P.solo[s];
      }
      P.outDb = mOutDb.load(std::memory_order_relaxed);
      P.room = mRoomAmt.load(std::memory_order_relaxed);
      P.bassHz = mBassHz.load(std::memory_order_relaxed);
      P.renderMode = mRenderMode.load(std::memory_order_relaxed);
      P.hrtf = mHrtfChoice.load(std::memory_order_relaxed);
      P.limiter = mLimiterOn.load(std::memory_order_relaxed) != 0;
      const float yaw = mYaw.load(std::memory_order_relaxed);

      float channelPeak[kSlots] = {};
      mMon.Render(P, inputs, numInputs, n, yaw, output.channels[0], output.channels[1], channelPeak);

      // Export: the file never carries head movement. With tracking off it is
      // the monitor mix; with tracking on, a second render at yaw 0 runs for
      // as long as the recording does (its state is kept warm from the first
      // sample, so a head turn never changes what the file contains).
      if (mRing != nullptr && mRing->enabled.load(std::memory_order_relaxed))
      {
         const float* fl = output.channels[0];
         const float* fr = output.channels[1];
         if (mTracking.load(std::memory_order_relaxed) != 0)
         {
            float dummy[kSlots] = {};
            mFile.Render(P, inputs, numInputs, n, 0.0f, mFileL, mFileR, dummy);
            fl = mFileL;
            fr = mFileR;
         }
         for (int i = 0; i < n; i++)
         {
            mInterleave[(size_t)i * 2] = fl[i];
            mInterleave[(size_t)i * 2 + 1] = fr[i];
         }
         mRing->Write(mInterleave, n * 2);
      }

      const float blockPeak = mMon.BlockPeak();
      mMeter.Write(&blockPeak, 1);
      mLufsShort.store(mMon.ShortTerm(), std::memory_order_relaxed);
      mLufsMomentary.store(mMon.Momentary(), std::memory_order_relaxed);
      const float tpDb = 20.0f * std::log10(std::max(mMon.BlockPeak(), 1e-6f));
      if (mTpReset.exchange(0, std::memory_order_relaxed) != 0)
         mTruePeakDb.store(tpDb, std::memory_order_relaxed);
      else if (tpDb > mTruePeakDb.load(std::memory_order_relaxed))
         mTruePeakDb.store(tpDb, std::memory_order_relaxed);
      mReductionDb.store(mMon.BlockReductionDb(), std::memory_order_relaxed);
      for (int s = 0; s < kSlots; s++)
         mChannelPeak[s].store(channelPeak[s], std::memory_order_relaxed);
   }

   // Main thread only.
   void PushParams(const SpatialMixerNode& n)
   {
      for (int s = 0; s < kSlots; s++)
      {
         mAz[s].store(n.azimuth[s], std::memory_order_relaxed);
         mEl[s].store(n.elevation[s], std::memory_order_relaxed);
         mDist[s].store(n.distance[s], std::memory_order_relaxed);
         mWidth[s].store(n.width[s], std::memory_order_relaxed);
         mGainDb[s].store(n.gainDb[s], std::memory_order_relaxed);
         mMute[s].store(n.mute[s] ? 1 : 0, std::memory_order_relaxed);
         mSolo[s].store(n.solo[s] ? 1 : 0, std::memory_order_relaxed);
         mLocked[s].store(n.headLocked[s] ? 1 : 0, std::memory_order_relaxed);
      }
      mOutDb.store(n.outDb, std::memory_order_relaxed);
      mRenderMode.store(n.renderMode, std::memory_order_relaxed);
      mRoomAmt.store(n.room, std::memory_order_relaxed);
      mBassHz.store(n.bassHz, std::memory_order_relaxed);
      mHrtfChoice.store(n.hrtf, std::memory_order_relaxed);
      mLimiterOn.store(n.limiter ? 1 : 0, std::memory_order_relaxed);
   }
   void SetHead(float yawDeg, bool tracking)
   {
      mYaw.store(yawDeg, std::memory_order_relaxed);
      mTracking.store(tracking ? 1 : 0, std::memory_order_relaxed);
   }
   void ResetTruePeak() { mTpReset.store(1, std::memory_order_relaxed); }

   MeterRing& Meter() { return mMeter; }
   float ChannelPeak(int s) const { return mChannelPeak[s].load(std::memory_order_relaxed); }
   float LufsShort() const { return mLufsShort.load(std::memory_order_relaxed); }
   float LufsMomentary() const { return mLufsMomentary.load(std::memory_order_relaxed); }
   float TruePeakDb() const { return mTruePeakDb.load(std::memory_order_relaxed); }
   float ReductionDb() const { return mReductionDb.load(std::memory_order_relaxed); }

private:
   double mSr = 48000.0;
   AudioCaptureRing* mRing = nullptr;
   std::shared_ptr<const Hrtf::Set> mHrtfSet;
   SpatialRenderer mMon, mFile;
   float mFileL[kAudioMaxBlockFrames] = {}, mFileR[kAudioMaxBlockFrames] = {};
   float mInterleave[kAudioMaxBlockFrames * 2] = {};
   MeterRing mMeter;
   std::atomic<float> mAz[kSlots] {}, mEl[kSlots] {}, mDist[kSlots] {}, mWidth[kSlots] {};
   std::atomic<float> mGainDb[kSlots] {};
   std::atomic<int> mMute[kSlots] {}, mSolo[kSlots] {}, mLocked[kSlots] {};
   std::atomic<float> mOutDb { 0.0f }, mRoomAmt { 0.0f }, mBassHz { 0.0f }, mYaw { 0.0f };
   std::atomic<int> mRenderMode { 0 }, mHrtfChoice { 0 }, mLimiterOn { 1 }, mTpReset { 0 }, mTracking { 0 };
   std::atomic<float> mChannelPeak[kSlots] {};
   std::atomic<float> mLufsShort { -70.0f }, mLufsMomentary { -70.0f }, mTruePeakDb { -120.0f }, mReductionDb { 0.0f };
};

SpatialMixerNode::SpatialMixerNode()
{
   for (int s = 0; s < kMaxSlots; s++)
   {
      azimuth[s] = DefaultAzimuth(s);
      distance[s] = 2.0f;
   }
}
SpatialMixerNode::~SpatialMixerNode()
{
   if (mTrackStarted != 0)
      HeadTracker::Stop((HeadTracker::Source)mTrackStarted);
   StopRecording(); // never leave a file with unpatched chunk sizes
}

bool SpatialMixerNode::StartRecording(const std::string& path)
{
   if (IsRecording())
      return false;
   const double sampleRate = AudioEngine::Instance().SampleRate();
   if (sampleRate <= 0.0 || !mWriter.Open(path, sampleRate, 2, AudioFileWriter::Format::Auto, bit24 ? 24 : 16))
      return false;
   mOpenSampleRate = sampleRate;
   mCaptureRing.overflowCount.store(0, std::memory_order_relaxed);
   mCaptureRing.enabled.store(true, std::memory_order_relaxed);
   return true;
}

void SpatialMixerNode::StopRecording()
{
   mCaptureRing.enabled.store(false, std::memory_order_relaxed);
   // Drain what the ring still holds, then close.
   if (mWriter.IsOpen())
   {
      float scratch[4096];
      int n;
      while ((n = mCaptureRing.Read(scratch, 4096)) > 0)
         mWriter.Append(scratch, n / 2);
   }
   mWriter.Close();
}

int SpatialMixerNode::ConnectedCount() const
{
   int c = 0;
   for (int s = 0; s < kMaxSlots; s++)
      c += inputs[s].IsConnected() ? 1 : 0;
   return c;
}

int SpatialMixerNode::PinCount() const
{
   int last = -1;
   for (int s = 0; s < kMaxSlots; s++)
      if (inputs[s].IsConnected())
         last = s;
   return std::clamp(last + 2, 1, kMaxSlots);
}

const char* SpatialMixerNode::InputLabel(int slot) const
{
   static const char* kLabels[kMaxSlots] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12" };
   if (slot < 0 || slot >= PinCount())
      return nullptr;
   return inputs[slot].IsConnected() ? kLabels[slot] : "+";
}

void SpatialMixerNode::SyncTracker()
{
   const int want = (trackMode == 1 && HeadTracker::Supported(HeadTracker::kHeadphones)) ? 1 : 0;
   if (want == mTrackStarted)
      return;
   if (mTrackStarted != 0)
      HeadTracker::Stop((HeadTracker::Source)mTrackStarted);
   mTrackStarted = want;
   if (want != 0)
      HeadTracker::Start((HeadTracker::Source)want);
}

void SpatialMixerNode::RecenterHead()
{
   HeadTracker::Recenter();
}

void SpatialMixerNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioSpatialMixerNode>(&mCaptureRing);
   mAudioNode->PushParams(*this);
   if (mWriter.IsOpen()) // drained every frame so a scrolled-off node does not stall its recording
   {
      float scratch[4096];
      int n;
      while ((n = mCaptureRing.Read(scratch, 4096)) > 0)
         mWriter.Append(scratch, n / 2);
   }
   if (mYawForced)
   {
      mAudioNode->SetHead(mHeadYaw, trackMode != 0);
   }
   else
   {
      SyncTracker();
      mHeadFresh = false;
      if (mTrackStarted != 0)
         mHeadFresh = HeadTracker::ReadYaw(mHeadYaw);
      if (!mHeadFresh)
         mHeadYaw = 0.0f; // sensor lost: settle to facing front rather than freeze turned
      mAudioNode->SetHead(mHeadYaw, mTrackStarted != 0);
   }
   float peak = 0.0f;
   if (mAudioNode->Meter().ReadLatest(peak))
      mLevel = peak;
   mLufsShort = mAudioNode->LufsShort();
   mLufsMomentary = mAudioNode->LufsMomentary();
   mTruePeakDb = mAudioNode->TruePeakDb();
   mReductionDb = mAudioNode->ReductionDb();
   for (int s = 0; s < kMaxSlots; s++)
      mChannelLevel[s] = mAudioNode->ChannelPeak(s);
}

void SpatialMixerNode::VisitParams(ParamVisitor& v)
{
   static char names[8][kMaxSlots][12];
   static bool init = false;
   if (!init)
   {
      static const char* kBase[8] = { "az", "el", "dist", "width", "gain", "mute", "solo", "lock" };
      for (int k = 0; k < 8; k++)
         for (int s = 0; s < kMaxSlots; s++)
            snprintf(names[k][s], sizeof(names[k][s]), "%s%d", kBase[k], s);
      init = true;
   }
   for (int s = 0; s < kMaxSlots; s++)
   {
      v.Float(names[0][s], azimuth[s]);
      v.Float(names[1][s], elevation[s]);
      v.Float(names[2][s], distance[s]);
      v.Float(names[3][s], width[s]);
      v.Float(names[4][s], gainDb[s]);
      v.Bool(names[5][s], mute[s]);
      v.Bool(names[6][s], solo[s]);
      v.Bool(names[7][s], headLocked[s]);
   }
   v.Float("out", outDb);
   v.Float("room", room);
   v.Float("bassHz", bassHz);
   v.Int("hrtf", hrtf);
   v.Bool("limiter", limiter);
   v.Int("trackMode", trackMode);
   v.Bool("bit24", bit24);
   v.Int("renderMode", renderMode);
   v.Int("formatIndex", formatIndex);
   v.Bool("live", live);
   v.Text("recordDirectory", recordDirectory);
}

void SpatialMixerNode::ResetPeak()
{
   if (mAudioNode)
      mAudioNode->ResetTruePeak();
}

AudioNode* SpatialMixerNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioSpatialMixerNode>(&mCaptureRing);
   return mAudioNode.get();
}
