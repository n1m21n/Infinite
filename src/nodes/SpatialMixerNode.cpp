#include "nodes/SpatialMixerNode.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>

#include "audio/AudioNode.h"
#include "audio/DspMath.h"
#include "audio/AudioEngine.h"
#include "audio/MeterRing.h"
#include "audio/dsp/BinauralKernel.h"

float SpatialMixerNode::DefaultAzimuth(int slot)
{
   // 0, +30, -30, +60, -60 ... then the rear half.
   const int k = (slot + 1) / 2;
   const float a = 30.0f * (float)k;
   const float az = (slot % 2 == 1) ? a : -a;
   return slot == 0 ? 0.0f : (az > 180.0f ? az - 360.0f : az);
}

class AudioSpatialMixerNode : public AudioNode
{
public:
   static constexpr int kSlots = SpatialMixerNode::kMaxSlots;

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSr = sampleRate;
      for (int s = 0; s < kSlots; s++)
         for (int v = 0; v < 2; v++)
            mVoice[s][v].Prepare(sampleRate);
      mOutGain.SetTimeConstant(0.01f, sampleRate);
      mOutGain.SetImmediate(DspMath::DbToLinear(mOutDb.load(std::memory_order_relaxed)));
   }

   void Reset() override
   {
      for (int s = 0; s < kSlots; s++)
         for (int v = 0; v < 2; v++)
            mVoice[s][v].Reset();
   }

   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& output) override
   {
      const int n = output.numFrames;
      for (int ch = 0; ch < output.numChannels; ch++)
         std::fill(output.channels[ch], output.channels[ch] + n, 0.0f);
      if (output.numChannels < 2)
         return;
      float* outL = output.channels[0];
      float* outR = output.channels[1];

      bool anySolo = false;
      for (int s = 0; s < kSlots; s++)
         anySolo = anySolo || mSolo[s].load(std::memory_order_relaxed) != 0;

      float channelPeak[kSlots] = {};
      const int slots = std::min(numInputs, kSlots);
      for (int s = 0; s < slots; s++)
      {
         const AudioBuffer* in = inputs[s];
         if (in == nullptr || in->numChannels < 1)
            continue; // unconnected: ~0 CPU
         const bool muted = mMute[s].load(std::memory_order_relaxed) != 0 ||
                            (anySolo && mSolo[s].load(std::memory_order_relaxed) == 0);
         if (muted)
            continue;

         const float gain = DspMath::DbToLinear(mGainDb[s].load(std::memory_order_relaxed));
         const float az = mAz[s].load(std::memory_order_relaxed);
         const float el = mEl[s].load(std::memory_order_relaxed);
         const float dist = mDist[s].load(std::memory_order_relaxed);
         const float w = std::clamp(mWidth[s].load(std::memory_order_relaxed), 0.0f, 1.0f);
         const bool stereo = in->numChannels >= 2 && w > 0.001f;

         if (mRenderMode.load(std::memory_order_relaxed) == 1)
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
            channelPeak[s] = pk;
            continue;
         }
         if (stereo)
         {
            mVoice[s][0].SetTarget(Binaural::ComputeTarget(az - 30.0f * w, el, dist, mSr));
            mVoice[s][1].SetTarget(Binaural::ComputeTarget(az + 30.0f * w, el, dist, mSr));
         }
         else
            mVoice[s][0].SetTarget(Binaural::ComputeTarget(az, el, dist, mSr));

         float peak = 0.0f;
         for (int i = 0; i < n; i++)
         {
            if (stereo)
            {
               const float l = in->channels[0][i] * gain, r = in->channels[1][i] * gain;
               mVoice[s][0].Process(l, outL[i], outR[i]);
               mVoice[s][1].Process(r, outL[i], outR[i]);
               peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
            }
            else
            {
               float m = in->channels[0][i];
               if (in->numChannels >= 2)
                  m = 0.5f * (m + in->channels[1][i]);
               m *= gain;
               mVoice[s][0].Process(m, outL[i], outR[i]);
               peak = std::max(peak, std::fabs(m));
            }
         }
         channelPeak[s] = peak;
      }

      float peak = 0.0f;
      for (int i = 0; i < n; i++)
      {
         const float g = mOutGain.Process(DspMath::DbToLinear(mOutDb.load(std::memory_order_relaxed)));
         outL[i] *= g;
         outR[i] *= g;
         peak = std::max(peak, std::max(std::fabs(outL[i]), std::fabs(outR[i])));
      }
      mMeter.Write(&peak, 1);
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
      }
      mOutDb.store(n.outDb, std::memory_order_relaxed);
      mRenderMode.store(n.renderMode, std::memory_order_relaxed);
   }

   MeterRing& Meter() { return mMeter; }
   float ChannelPeak(int s) const { return mChannelPeak[s].load(std::memory_order_relaxed); }

private:
   double mSr = 48000.0;
   Binaural::Voice mVoice[kSlots][2];
   DspMath::OnePole mOutGain;
   MeterRing mMeter;
   std::atomic<float> mAz[kSlots] {}, mEl[kSlots] {}, mDist[kSlots] {}, mWidth[kSlots] {};
   std::atomic<float> mGainDb[kSlots] {};
   std::atomic<int> mMute[kSlots] {}, mSolo[kSlots] {};
   std::atomic<float> mOutDb { 0.0f };
   std::atomic<int> mRenderMode { 0 };
   std::atomic<float> mChannelPeak[kSlots] {};
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
   StopRecording(); // never leave a file with unpatched chunk sizes
}

bool SpatialMixerNode::StartRecording(const std::string& path)
{
   if (IsRecording())
      return false;
   const double sampleRate = AudioEngine::Instance().SampleRate();
   if (sampleRate <= 0.0 || !mWriter.Open(path, sampleRate, 2))
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

void SpatialMixerNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioSpatialMixerNode>();
   mAudioNode->PushParams(*this);
   if (mWriter.IsOpen()) // drained every frame so a scrolled-off node does not stall its recording
   {
      float scratch[4096];
      int n;
      while ((n = mCaptureRing.Read(scratch, 4096)) > 0)
         mWriter.Append(scratch, n / 2);
   }
   float peak = 0.0f;
   if (mAudioNode->Meter().ReadLatest(peak))
      mLevel = peak;
   for (int s = 0; s < kMaxSlots; s++)
      mChannelLevel[s] = mAudioNode->ChannelPeak(s);
}

void SpatialMixerNode::VisitParams(ParamVisitor& v)
{
   static char names[7][kMaxSlots][12];
   static bool init = false;
   if (!init)
   {
      static const char* kBase[7] = { "az", "el", "dist", "width", "gain", "mute", "solo" };
      for (int k = 0; k < 7; k++)
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
   }
   v.Float("out", outDb);
   v.Int("renderMode", renderMode);
   v.Int("formatIndex", formatIndex);
   v.Bool("live", live);
   v.Text("recordDirectory", recordDirectory);
}

AudioNode* SpatialMixerNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioSpatialMixerNode>();
   return mAudioNode.get();
}
