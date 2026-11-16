#include "audio/dsp/MixerMeter.h"
#include "plugin/engine/HostAudio.h"
#include "AudioNodes.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <sstream>

#include "audio/AudioBuffer.h"
#include "audio/AudioEngine.h"
#include "audio/AudioNode.h"
#include "audio/DspMath.h"
#include "audio/MeterRing.h"
#include "audio/ParamMailbox.h"
#include "platform/Platform.h"

namespace
{
   constexpr int kGainDbParam = 0;
   // The gain knobs' and faders' range (Gain, Mixer, Audio In).
   constexpr float kMinGainDb = -60.0f;
   constexpr float kMaxGainDb = 12.0f;
}

// ----------------------------------------------------------------------- Gain
class AudioGainNode : public AudioNode
{
public:
   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mMailbox.PrepareToPlay(sampleRate);
      mMailbox.SetImmediate(kGainDbParam, mGainDb.load(std::memory_order_relaxed));
   }

   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& buffer) override
   {
      const AudioBuffer* in = (numInputs > 0) ? inputs[0] : nullptr;
      float peak = 0.0f;
      for (int i = 0; i < buffer.numFrames; i++)
      {
         const float linear = DspMath::DbToLinear(mMailbox.SmoothedValue(kGainDbParam));
         for (int ch = 0; ch < buffer.numChannels; ch++)
         {
            const float s = (in != nullptr) ? in->channels[ch][i] : 0.0f;
            const float v = s * linear;
            buffer.channels[ch][i] = v;
            peak = std::max(peak, std::fabs(v));
         }
      }
      mMeter.Write(&peak, 1);
   }

   // Main thread only.
   void PushParams(float gainDb)
   {
      mGainDb.store(gainDb, std::memory_order_relaxed);
      mMailbox.Push(kGainDbParam, gainDb);
   }

   MeterRing& Meter() { return mMeter; }

private:
   ParamMailbox mMailbox;
   MeterRing mMeter;
   std::atomic<float> mGainDb { 0.0f };
};

GainNode::GainNode() = default;
GainNode::~GainNode() = default;

void GainNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioGainNode>();
   mAudioNode->PushParams(gainDb);

   float peak = 0.0f;
   if (mAudioNode->Meter().ReadLatest(peak))
      mLevel = peak;
}

void GainNode::VisitParams(ParamVisitor& v)
{
   v.Float("gainDb", gainDb);
   // A patch file, OSC or RPC set stores any finite float; 10^(dB/20) is inf
   // past ~770 dB and the knob binds only the UI. Hold it to the knob's range.
   gainDb = std::clamp(gainDb, kMinGainDb, kMaxGainDb);
}

AudioNode* GainNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioGainNode>();
   return mAudioNode.get();
}

// ---------------------------------------------------------------------- Mixer
namespace
{
   // Pan gains for one strip, unity at centre under every law; the law only
   // sets how far a hard-panned side rises above that (mixer-panel plan R5).
   // Law 1 is the exact expression older patches were mixed with.
   void MixerPanGains(float pan, int law, float& l, float& r)
   {
      const float p = std::clamp(pan, -1.0f, 1.0f);
      const float x = (p + 1.0f) * 0.5f; // 0 = hard left, 1 = hard right
      switch (law)
      {
      case 0: // 0 dB: balance - the far side fades, the near side stays at unity
         l = std::min(1.0f, 2.0f * (1.0f - x));
         r = std::min(1.0f, 2.0f * x);
         return;
      case 2: // +4.5 dB: geometric mean of equal power and linear
      {
         float el, er;
         DspMath::EqualPowerPan(p, el, er);
         const float centre = std::sqrt((float)M_SQRT1_2 * 0.5f);
         l = std::sqrt(std::max(0.0f, el * (1.0f - x))) / centre;
         r = std::sqrt(std::max(0.0f, er * x)) / centre;
         return;
      }
      case 3: // +6 dB: linear
         l = 2.0f * (1.0f - x);
         r = 2.0f * x;
         return;
      default: // +3 dB: equal power
         DspMath::EqualPowerPan(p, l, r);
         l *= (float)M_SQRT2;
         r *= (float)M_SQRT2;
         return;
      }
   }
}

class AudioMixerNode : public AudioNode
{
public:
   AudioMixerNode()
   {
      for (auto& bus : mSendDb)
         for (auto& v : bus)
            v.store(MixerNode::kSendOffDb, std::memory_order_relaxed);
   }

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mMailbox.PrepareToPlay(sampleRate);
      for (int s = 0; s < MixerNode::kMaxSlots; s++)
      {
         mMailbox.SetImmediate(s, mGainDb[s].load(std::memory_order_relaxed));
         mCachedDb[s] = std::nanf("");
      }
      mPanPrimed = false;
      mLoudness.Prepare(sampleRate);
      mWasMetering = false;
   }

   // Main out + four send buses (mixer-panel plan D5). The topology asks
   // before every rebuild, so a Mixer without sends keeps one output buffer.
   int AudioOutputCount() const override { return 1 + mBuses.load(std::memory_order_relaxed); }
   void SetBusCount(int buses) { mBuses.store(std::clamp(buses, 0, (int)MixerNode::kSendBuses), std::memory_order_relaxed); }

   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& output) override
   {
      AudioBuffer* outs[1] = { &output };
      ProcessBlockMulti(inputs, numInputs, outs, 1);
   }

   void ProcessBlockMulti(const AudioBuffer* const* inputs, int numInputs,
                          AudioBuffer* const* outputs, int numOutputs) override
   {
      constexpr int kBuses = MixerNode::kSendBuses;
      AudioBuffer* busOut[kBuses] = {};
      for (int b = 0; b < kBuses && b + 1 < numOutputs; b++)
      {
         busOut[b] = outputs[b + 1];
         if (busOut[b] != nullptr)
            for (int ch = 0; ch < busOut[b]->numChannels; ch++)
               std::fill(busOut[b]->channels[ch], busOut[b]->channels[ch] + busOut[b]->numFrames, 0.0f);
      }
      if (numOutputs < 1 || outputs[0] == nullptr)
         return;
      AudioBuffer& output = *outputs[0];
      const int activeChannels = std::clamp(mNumChannels.load(std::memory_order_relaxed), 0, (int)MixerNode::kMaxSlots);
      const int slots = std::min(numInputs, activeChannels);
      const int law = mPanLaw.load(std::memory_order_relaxed);

      bool anySolo = false;
      for (int s = 0; s < activeChannels; s++)
      {
         if (mSolo[s].load(std::memory_order_relaxed) != 0)
         {
            anySolo = true;
            break;
         }
      }

      // Pan, mute, solo and polarity are read once per block, not smoothed per
      // sample. Polarity rides on the gain pair, so a flip ramps through zero.
      float panL[MixerNode::kMaxSlots] = {}, panR[MixerNode::kMaxSlots] = {};
      for (int s = 0; s < activeChannels; s++)
      {
         const bool isMuted = mMute[s].load(std::memory_order_relaxed) != 0;
         const bool isSolo = mSolo[s].load(std::memory_order_relaxed) != 0;
         const bool isSafe = mSoloSafe[s].load(std::memory_order_relaxed) != 0;
         if (isMuted || (anySolo && !isSolo && !isSafe))
         {
            panL[s] = panR[s] = 0.0f;
            continue;
         }
         MixerPanGains(mPan[s].load(std::memory_order_relaxed), law, panL[s], panR[s]);
         if (mPhase[s].load(std::memory_order_relaxed) != 0)
         {
            panL[s] = -panL[s];
            panR[s] = -panR[s];
         }
      }

      // Sends, trim (for pre-fader sends) and master are block-rate values
      // ramped from last block's across this one, like the pan pair.
      float sendLin[kBuses][MixerNode::kMaxSlots] = {};
      bool busActive[kBuses] = {};
      for (int b = 0; b < kBuses; b++)
      {
         if (busOut[b] == nullptr)
            continue;
         for (int s = 0; s < activeChannels; s++)
         {
            const float db = mSendDb[b][s].load(std::memory_order_relaxed);
            sendLin[b][s] = db <= MixerNode::kSendOffDb ? 0.0f : DspMath::DbToLinear(db);
            if (sendLin[b][s] != 0.0f || mPrevSend[b][s] != 0.0f)
               busActive[b] = true;
         }
      }
      float trimLin[MixerNode::kMaxSlots];
      bool pre[MixerNode::kMaxSlots];
      for (int s = 0; s < activeChannels; s++)
      {
         trimLin[s] = DspMath::DbToLinear(mTrimDb[s].load(std::memory_order_relaxed));
         pre[s] = mSendPre[s].load(std::memory_order_relaxed) != 0;
      }
      const float masterLin = DspMath::DbToLinear(mMasterDb.load(std::memory_order_relaxed));

      if (!mPanPrimed)
      {
         for (int s = 0; s < MixerNode::kMaxSlots; s++)
         {
            mPrevPanL[s] = panL[s];
            mPrevPanR[s] = panR[s];
            mPrevTrim[s] = s < activeChannels ? trimLin[s] : 1.0f;
            for (int b = 0; b < kBuses; b++)
               mPrevSend[b][s] = s < activeChannels ? sendLin[b][s] : 0.0f;
         }
         mPrevMaster = masterLin;
         mPanPrimed = true;
      }
      const float invFrames = 1.0f / (float)std::max(1, output.numFrames);

      // Fader + trim reach the mailbox as one dB value. A strip whose value is
      // not moving gets its linear gain once per block; only a ramping strip
      // pays the per-sample powf (32 strips x powf per sample otherwise).
      bool ramping[MixerNode::kMaxSlots] = {};
      float linear[MixerNode::kMaxSlots];
      for (int s = 0; s < activeChannels; s++)
      {
         ramping[s] = !mMailbox.IsSettled(s);
         if (!ramping[s])
         {
            const float db = mMailbox.SmoothedValue(s);
            // A value that landed between the check and the read started a
            // ramp: follow it per sample from here instead.
            if (!mMailbox.IsSettled(s))
            {
               ramping[s] = true;
               continue;
            }
            if (!(db == mCachedDb[s]))
            {
               mCachedDb[s] = db;
               mCachedLinear[s] = DspMath::DbToLinear(db);
            }
            linear[s] = mCachedLinear[s];
         }
      }

      bool anySend = false;
      for (int b = 0; b < kBuses; b++)
         anySend = anySend || busActive[b];

      float channelPeak[MixerNode::kMaxSlots] = {};
      float peak = 0.0f;
      for (int i = 0; i < output.numFrames; i++)
      {
         float gL[MixerNode::kMaxSlots], gR[MixerNode::kMaxSlots];
         float tr[MixerNode::kMaxSlots], sg[kBuses][MixerNode::kMaxSlots];
         const float t = (float)(i + 1) * invFrames;
         for (int s = 0; s < activeChannels; s++)
         {
            if (ramping[s])
               linear[s] = DspMath::DbToLinear(mMailbox.SmoothedValue(s));
            gL[s] = mPrevPanL[s] + (panL[s] - mPrevPanL[s]) * t;
            gR[s] = mPrevPanR[s] + (panR[s] - mPrevPanR[s]) * t;
         }
         if (anySend)
         {
            for (int s = 0; s < activeChannels; s++)
               tr[s] = mPrevTrim[s] + (trimLin[s] - mPrevTrim[s]) * t;
            for (int b = 0; b < kBuses; b++)
               if (busActive[b])
                  for (int s = 0; s < activeChannels; s++)
                     sg[b][s] = mPrevSend[b][s] + (sendLin[b][s] - mPrevSend[b][s]) * t;
         }
         const float m = mPrevMaster + (masterLin - mPrevMaster) * t;

         for (int ch = 0; ch < output.numChannels; ch++)
         {
            // Channel 0 is left, 1 is right; anything beyond a stereo pair
            // gets the left law rather than being silently dropped.
            const bool right = ch == 1;
            float sum = 0.0f;
            for (int s = 0; s < slots; s++)
            {
               if (inputs[s] == nullptr)
                  continue;
               const float in = inputs[s]->channels[ch][i];
               const float g = right ? gR[s] : gL[s];
               const float v = in * linear[s] * g;
               sum += v;
               channelPeak[s] = std::max(channelPeak[s], std::fabs(v));
               if (anySend)
               {
                  const float sendSrc = pre[s] ? in * tr[s] * g : v;
                  for (int b = 0; b < kBuses; b++)
                     if (busActive[b] && ch < busOut[b]->numChannels)
                        busOut[b]->channels[ch][i] += sendSrc * sg[b][s];
               }
            }
            sum *= m;
            output.channels[ch][i] = sum;
            peak = std::max(peak, std::fabs(sum));
         }
      }
      for (int s = 0; s < activeChannels; s++)
         if (ramping[s])
            mCachedDb[s] = std::nanf("");

      for (int s = 0; s < MixerNode::kMaxSlots; s++)
      {
         mPrevPanL[s] = panL[s];
         mPrevPanR[s] = panR[s];
         if (s < activeChannels)
         {
            mPrevTrim[s] = trimLin[s];
            for (int b = 0; b < kBuses; b++)
               mPrevSend[b][s] = sendLin[b][s];
         }
      }
      mPrevMaster = masterLin;
      mMeter.Write(&peak, 1);
      // Loudness and true peak of the main sum: only while the Mixer panel shows this master.
      const bool metering = mMetering.load(std::memory_order_relaxed) != 0;
      if (metering)
      {
         if (!mWasMetering)
            mLoudness.Reset();
         mLoudness.Process(output.channels[0], output.numChannels > 1 ? output.channels[1] : nullptr, output.numFrames);
      }
      mWasMetering = metering;
      for (int s = 0; s < MixerNode::kMaxSlots; s++)
         mChannelPeak[s].store(s < activeChannels ? channelPeak[s] : 0.0f, std::memory_order_relaxed);
   }

   // Main thread only.
   void PushParams(const MixerNode& n)
   {
      mNumChannels.store(n.numChannels, std::memory_order_relaxed);
      mPanLaw.store(std::clamp(n.panLaw, 0, 3), std::memory_order_relaxed);
      mMasterDb.store(n.masterDb, std::memory_order_relaxed);
      SetBusCount(n.SendBuses());
      for (int s = 0; s < MixerNode::kMaxSlots; s++)
      {
         const float db = n.gainDb[s] + n.trimDb[s];
         mGainDb[s].store(db, std::memory_order_relaxed);
         mMailbox.Push(s, db);
         mTrimDb[s].store(n.trimDb[s], std::memory_order_relaxed);
         mSendPre[s].store(n.sendPre[s] ? 1 : 0, std::memory_order_relaxed);
         for (int b = 0; b < MixerNode::kSendBuses; b++)
            mSendDb[b][s].store(b < n.SendBuses() ? n.sendDb[b][s] : MixerNode::kSendOffDb, std::memory_order_relaxed);
         mPan[s].store(n.pan[s], std::memory_order_relaxed);
         mMute[s].store(n.mute[s] ? 1 : 0, std::memory_order_relaxed);
         mSolo[s].store(n.solo[s] ? 1 : 0, std::memory_order_relaxed);
         mPhase[s].store(n.phase[s] ? 1 : 0, std::memory_order_relaxed);
         mSoloSafe[s].store(n.soloSafe[s] ? 1 : 0, std::memory_order_relaxed);
      }
   }

   MeterRing& Meter() { return mMeter; }
   float ChannelPeak(int slot) const { return mChannelPeak[slot].load(std::memory_order_relaxed); }
   void SetMetering(bool on) { mMetering.store(on ? 1 : 0, std::memory_order_relaxed); }
   MixerMeter& Loudness() { return mLoudness; }

private:
   ParamMailbox mMailbox;
   MeterRing mMeter;
   MixerMeter mLoudness;            // audio thread; readouts are atomics
   std::atomic<int> mMetering { 0 };
   bool mWasMetering = false;       // audio thread only
   float mPrevPanL[MixerNode::kMaxSlots] = {}, mPrevPanR[MixerNode::kMaxSlots] = {}; // audio thread only
   float mCachedDb[MixerNode::kMaxSlots] = {}, mCachedLinear[MixerNode::kMaxSlots] = {}; // audio thread only
   bool mPanPrimed = false;
   float mPrevTrim[MixerNode::kMaxSlots] = {};                          // audio thread only
   float mPrevSend[MixerNode::kSendBuses][MixerNode::kMaxSlots] = {};   // audio thread only
   float mPrevMaster = 1.0f;                                            // audio thread only
   std::atomic<int> mBuses { 0 };
   std::atomic<float> mMasterDb { 0.0f };
   std::atomic<float> mTrimDb[MixerNode::kMaxSlots] {};
   std::atomic<int> mSendPre[MixerNode::kMaxSlots] {};
   std::atomic<float> mSendDb[MixerNode::kSendBuses][MixerNode::kMaxSlots] {};
   std::atomic<int> mNumChannels { 8 };
   std::atomic<int> mPanLaw { 1 };
   std::atomic<float> mGainDb[MixerNode::kMaxSlots] {};
   std::atomic<float> mPan[MixerNode::kMaxSlots] {};
   std::atomic<int> mMute[MixerNode::kMaxSlots] {};
   std::atomic<int> mSolo[MixerNode::kMaxSlots] {};
   std::atomic<int> mPhase[MixerNode::kMaxSlots] {};
   std::atomic<int> mSoloSafe[MixerNode::kMaxSlots] {};
   std::atomic<float> mChannelPeak[MixerNode::kMaxSlots] {};
};

MixerNode::MixerNode()
{
   for (auto& bus : sendDb)
      std::fill(std::begin(bus), std::end(bus), kSendOffDb);
}
MixerNode::~MixerNode() = default;

void MixerNode::ResetMeters()
{
   if (mAudioNode)
      mAudioNode->Loudness().Reset();
   mLoudM = mLoudS = mLoudI = mTruePeakDb = MixerMeter::kSilent;
}

// ---- Snapshots A-H (B6) ----
namespace
{
   MixerNode::Snapshot ReadConsole(const MixerNode& m)
   {
      MixerNode::Snapshot sn;
      sn.has = true;
      sn.count = m.numChannels;
      sn.master = m.masterDb;
      for (int i = 0; i < MixerNode::kMaxSlots; i++)
      {
         sn.gain[i] = m.gainDb[i]; sn.pan[i] = m.pan[i]; sn.trim[i] = m.trimDb[i];
         sn.mute[i] = m.mute[i]; sn.solo[i] = m.solo[i]; sn.phase[i] = m.phase[i];
         sn.safe[i] = m.soloSafe[i]; sn.pre[i] = m.sendPre[i];
         for (int b = 0; b < MixerNode::kSendBuses; b++)
            sn.send[b][i] = m.sendDb[b][i];
      }
      return sn;
   }

   double SnapshotNowMs()
   {
      using namespace std::chrono;
      return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
   }

   // One line: "count master" then per strip "gain pan trim sendA..D flags", flags = mute solo phase safe pre bits.
   std::string SnapshotText(const MixerNode::Snapshot& sn)
   {
      if (!sn.has)
         return std::string();
      std::string out;
      char buf[48];
      snprintf(buf, sizeof(buf), "%d %g", sn.count, sn.master);
      out += buf;
      for (int i = 0; i < sn.count && i < MixerNode::kMaxSlots; i++)
      {
         snprintf(buf, sizeof(buf), " %g %g %g", sn.gain[i], sn.pan[i], sn.trim[i]);
         out += buf;
         for (int b = 0; b < MixerNode::kSendBuses; b++)
         {
            snprintf(buf, sizeof(buf), " %g", sn.send[b][i]);
            out += buf;
         }
         const int flags = (sn.mute[i] ? 1 : 0) | (sn.solo[i] ? 2 : 0) | (sn.phase[i] ? 4 : 0) | (sn.safe[i] ? 8 : 0) | (sn.pre[i] ? 16 : 0);
         snprintf(buf, sizeof(buf), " %d", flags);
         out += buf;
      }
      return out;
   }

   float Finite(float v, float lo, float hi, float fallback)
   {
      return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
   }

   MixerNode::Snapshot SnapshotParse(const std::string& text)
   {
      MixerNode::Snapshot sn;
      std::istringstream in(text);
      int count = 0;
      float master = 0.0f;
      if (!(in >> count >> master))
         return sn;
      sn.count = std::clamp(count, 0, MixerNode::kMaxSlots);
      sn.master = Finite(master, kMinGainDb, kMaxGainDb, 0.0f);
      for (int i = 0; i < MixerNode::kMaxSlots; i++)
         for (int b = 0; b < MixerNode::kSendBuses; b++)
            sn.send[b][i] = MixerNode::kSendOffDb;
      for (int i = 0; i < sn.count; i++)
      {
         float g = 0.0f, p = 0.0f, t = 0.0f, send[MixerNode::kSendBuses];
         int flags = 0;
         if (!(in >> g >> p >> t))
            return MixerNode::Snapshot();
         for (float& v : send)
            if (!(in >> v))
               return MixerNode::Snapshot();
         if (!(in >> flags))
            return MixerNode::Snapshot();
         sn.gain[i] = Finite(g, kMinGainDb, kMaxGainDb, 0.0f);
         sn.pan[i] = Finite(p, -1.0f, 1.0f, 0.0f);
         sn.trim[i] = Finite(t, -24.0f, 24.0f, 0.0f);
         for (int b = 0; b < MixerNode::kSendBuses; b++)
            sn.send[b][i] = Finite(send[b], MixerNode::kSendOffDb, kMaxGainDb, MixerNode::kSendOffDb);
         sn.mute[i] = flags & 1; sn.solo[i] = flags & 2; sn.phase[i] = flags & 4; sn.safe[i] = flags & 8; sn.pre[i] = flags & 16;
      }
      sn.has = true;
      return sn;
   }
}

void MixerNode::SnapshotStore(int k)
{
   if (k < 0 || k >= kSnapshots)
      return;
   mSnap[k] = ReadConsole(*this);
   mSnapLast = k;
}

void MixerNode::SnapshotRecall(int k, float fadeMs)
{
   if (!SnapshotHas(k))
      return;
   mSnapLast = k;
   if (!(fadeMs > 0.0f))
   {
      mFadeK = -1;
      SnapshotApply(mSnap[k], 1.0f);
      return;
   }
   mFadeFrom = ReadConsole(*this);
   mFadeK = k;
   mFadeMs = fadeMs;
   mFadeAt = 0.0f;
   mFadeClock = SnapshotNowMs();
}

void MixerNode::SnapshotAdvance(float dtMs)
{
   if (mFadeK < 0)
      return;
   mFadeAt += std::max(0.0f, dtMs);
   const float t = mFadeMs > 0.0f ? std::min(1.0f, mFadeAt / mFadeMs) : 1.0f;
   SnapshotApply(mSnap[mFadeK], t);
   if (t >= 1.0f)
      mFadeK = -1;
}

void MixerNode::SnapshotApply(const Snapshot& to, float t)
{
   const Snapshot& from = mFadeK >= 0 ? mFadeFrom : to;
   auto mix = [t](float a, float b) { return a + (b - a) * t; };
   const bool late = t >= 0.5f; // G3: switches change at the midpoint
   masterDb = mix(from.master, to.master);
   for (int i = 0; i < to.count && i < kMaxSlots; i++)
   {
      gainDb[i] = mix(from.gain[i], to.gain[i]);
      pan[i] = mix(from.pan[i], to.pan[i]);
      trimDb[i] = mix(from.trim[i], to.trim[i]);
      for (int b = 0; b < kSendBuses; b++)
         sendDb[b][i] = mix(from.send[b][i], to.send[b][i]);
      mute[i] = late ? to.mute[i] : from.mute[i];
      solo[i] = late ? to.solo[i] : from.solo[i];
      phase[i] = late ? to.phase[i] : from.phase[i];
      soloSafe[i] = late ? to.safe[i] : from.safe[i];
      sendPre[i] = late ? to.pre[i] : from.pre[i];
   }
}

void MixerNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (mFadeK >= 0)
   {
      const double now = SnapshotNowMs();
      SnapshotAdvance((float)(now - mFadeClock));
      mFadeClock = now;
   }
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioMixerNode>();
   mAudioNode->PushParams(*this);
   // B4: the panel asks every frame it draws this master; a frame without the ask stops the meter.
   mAudioNode->SetMetering(mMeterRequested);
   mMetersRunning = mMeterRequested;
   mMeterRequested = false;
   if (mMetersRunning)
   {
      MixerMeter& lm = mAudioNode->Loudness();
      mLoudM = lm.Momentary();
      mLoudS = lm.ShortTerm();
      mLoudI = lm.Integrated();
      mTruePeakDb = lm.TruePeakDb();
   }

   float peak = 0.0f;
   if (mAudioNode->Meter().ReadLatest(peak))
      mLevel = peak;
   for (int s = 0; s < kMaxSlots; s++)
      mChannelLevel[s] = mAudioNode->ChannelPeak(s);
}

void MixerNode::VisitParams(ParamVisitor& v)
{
   v.Int("channels", numChannels);
   numChannels = std::clamp(numChannels, 0, kMaxSlots); // loaded unchecked; indexes inputs[]
   // Names are saved per strip as <prefix><index>; the four original families
   // keep their old spelling, the console additions are appended after them.
   struct Names
   {
      std::string gain[kMaxSlots], pan[kMaxSlots], mute[kMaxSlots], solo[kMaxSlots];
      std::string trim[kMaxSlots], phase[kMaxSlots], safe[kMaxSlots];
      Names()
      {
         for (int i = 0; i < kMaxSlots; i++)
         {
            const std::string n = std::to_string(i);
            gain[i] = "gain" + n;
            pan[i] = "pan" + n;
            mute[i] = "mute" + n;
            solo[i] = "solo" + n;
            trim[i] = "trim" + n;
            phase[i] = "phase" + n;
            safe[i] = "solosafe" + n;
         }
      }
   };
   static const Names kNames;
   for (int i = 0; i < kMaxSlots; i++)
   {
      v.Float(kNames.gain[i].c_str(), gainDb[i]);
      v.Float(kNames.pan[i].c_str(), pan[i]);
      v.Bool(kNames.mute[i].c_str(), mute[i]);
      v.Bool(kNames.solo[i].c_str(), solo[i]);
      gainDb[i] = std::clamp(gainDb[i], kMinGainDb, kMaxGainDb); // as GainNode
      pan[i] = std::clamp(pan[i], -1.0f, 1.0f);
   }
   v.Int("panlaw", panLaw);
   panLaw = std::clamp(panLaw, 0, 3);
   for (int i = 0; i < kMaxSlots; i++)
   {
      v.Float(kNames.trim[i].c_str(), trimDb[i]);
      v.Bool(kNames.phase[i].c_str(), phase[i]);
      v.Bool(kNames.safe[i].c_str(), soloSafe[i]);
      trimDb[i] = std::isfinite(trimDb[i]) ? std::clamp(trimDb[i], -24.0f, 24.0f) : 0.0f;
   }
   v.Float("master", masterDb);
   masterDb = std::isfinite(masterDb) ? std::clamp(masterDb, kMinGainDb, kMaxGainDb) : 0.0f;
   // Sends (plan D5): send<A..D><strip> in dB, sendpre<strip>, sendbuses.
   struct SendNames
   {
      std::string db[kSendBuses][kMaxSlots], pre[kMaxSlots];
      SendNames()
      {
         for (int i = 0; i < kMaxSlots; i++)
         {
            for (int b = 0; b < kSendBuses; b++)
               db[b][i] = std::string("send") + (char)('A' + b) + std::to_string(i);
            pre[i] = "sendpre" + std::to_string(i);
         }
      }
   };
   static const SendNames kSend;
   v.Int("sendbuses", sendBuses);
   sendBuses = std::clamp(sendBuses, 0, kSendBuses);
   for (int b = 0; b < kSendBuses; b++)
      for (int i = 0; i < kMaxSlots; i++)
      {
         v.Float(kSend.db[b][i].c_str(), sendDb[b][i]);
         sendDb[b][i] = std::isfinite(sendDb[b][i]) ? std::clamp(sendDb[b][i], kSendOffDb, kMaxGainDb) : kSendOffDb;
      }
   for (int i = 0; i < kMaxSlots; i++)
      v.Bool(kSend.pre[i].c_str(), sendPre[i]);
   // Snapshots (B6): one text line each, empty when the slot is unused. Text keeps them out of
   // sweeps and modulation; snapfade is text for the same reason.
   static const char* kSnapNames[kSnapshots] = { "snapA", "snapB", "snapC", "snapD", "snapE", "snapF", "snapG", "snapH" };
   for (int k = 0; k < kSnapshots; k++)
   {
      std::string text = SnapshotText(mSnap[k]);
      const std::string before = text;
      v.Text(kSnapNames[k], text);
      if (text != before)
         mSnap[k] = text.empty() ? Snapshot() : SnapshotParse(text);
   }
   for (int i = 0; i < kMaxSlots; i++)
   {
      static const std::string kNameKeys[kMaxSlots] = {
         "chname0", "chname1", "chname2", "chname3", "chname4", "chname5", "chname6", "chname7",
         "chname8", "chname9", "chname10", "chname11", "chname12", "chname13", "chname14", "chname15",
         "chname16", "chname17", "chname18", "chname19", "chname20", "chname21", "chname22", "chname23",
         "chname24", "chname25", "chname26", "chname27", "chname28", "chname29", "chname30", "chname31" };
      v.Text(kNameKeys[i].c_str(), chName[i]);
      if (chName[i].size() > 32)
         chName[i].resize(32);
      char hex[16];
      snprintf(hex, sizeof(hex), "%06X", chColor[i] & 0xFFFFFFu);
      std::string col = chColor[i] ? hex : "";
      v.Text(("chcolor" + std::to_string(i)).c_str(), col);
      chColor[i] = col.empty() ? 0u : (unsigned int)(strtoul(col.c_str(), nullptr, 16) & 0xFFFFFFu);
   }
   std::string fade = std::to_string(snapFadeMs);
   v.Text("snapfade", fade);
   snapFadeMs = std::clamp(atoi(fade.c_str()), 0, 10000);
}

AudioNode* MixerNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioMixerNode>();
   // The topology reads AudioOutputCount() before this Mixer cooks.
   mAudioNode->SetBusCount(SendBuses());
   return mAudioNode.get();
}

// ------------------------------------------------------------------- Blend
namespace
{
   constexpr int kBlendParam = 0;
}

class AudioBlendNode : public AudioNode
{
public:
   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mMailbox.PrepareToPlay(sampleRate);
      mMailbox.SetImmediate(kBlendParam, mBlend.load(std::memory_order_relaxed));
   }

   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& output) override
   {
      const AudioBuffer* a = (numInputs > 0) ? inputs[0] : nullptr;
      const AudioBuffer* b = (numInputs > 1) ? inputs[1] : nullptr;

      float peak = 0.0f;
      for (int i = 0; i < output.numFrames; i++)
      {
         const float blend = mMailbox.SmoothedValue(kBlendParam);
         float gainA, gainB;
         DspMath::EqualPowerPan(blend * 2.0f - 1.0f, gainA, gainB);

         for (int ch = 0; ch < output.numChannels; ch++)
         {
            const float sa = (a != nullptr) ? a->channels[ch][i] : 0.0f;
            const float sb = (b != nullptr) ? b->channels[ch][i] : 0.0f;
            const float v = sa * gainA + sb * gainB;
            output.channels[ch][i] = v;
            peak = std::max(peak, std::fabs(v));
         }
      }
      mMeter.Write(&peak, 1);
   }

   // Main thread only.
   void PushParams(float blend)
   {
      mBlend.store(blend, std::memory_order_relaxed);
      mMailbox.Push(kBlendParam, blend);
   }

   MeterRing& Meter() { return mMeter; }

private:
   ParamMailbox mMailbox;
   MeterRing mMeter;
   std::atomic<float> mBlend { 0.5f };
};

BlendAudioNode::BlendAudioNode() = default;
BlendAudioNode::~BlendAudioNode() = default;

void BlendAudioNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioBlendNode>();
   mAudioNode->PushParams(blend);

   float peak = 0.0f;
   if (mAudioNode->Meter().ReadLatest(peak))
      mLevel = peak;
}

void BlendAudioNode::VisitParams(ParamVisitor& v)
{
   v.Float("blend", blend);
}

AudioNode* BlendAudioNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioBlendNode>();
   return mAudioNode.get();
}

// ------------------------------------------------------------------- Splitter
class AudioSplitterNode : public AudioNode
{
public:
   static constexpr int kMaxChannels = 8;
   static constexpr int kSplits = SplitterNode::kMaxBands - 1;

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      for (int s = 0; s < kSplits; s++)
         mHz[s] = mTargetHz[s].load(std::memory_order_relaxed);
      Reset();
   }

   void Reset() override
   {
      for (int ch = 0; ch < kMaxChannels; ch++)
      {
         for (int s = 0; s < kSplits; s++)
            for (DspMath::TptSvf& f : mSplit[ch][s])
               f.Reset();
         for (DspMath::TptSvf& f : mAllpass[ch])
            f.Reset();
      }
      mActiveBands = mBands.load(std::memory_order_relaxed);
      SetCutoffs();
   }

   int AudioOutputCount() const override { return SplitterNode::kMaxBands; }

   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& output) override
   {
      AudioBuffer* outs[1] = { &output };
      ProcessBlockMulti(inputs, numInputs, outs, 1);
   }

   void ProcessBlockMulti(const AudioBuffer* const* inputs, int numInputs,
                          AudioBuffer* const* outputs, int numOutputs) override
   {
      const AudioBuffer* in = (numInputs > 0) ? inputs[0] : nullptr;
      const int bands = mBands.load(std::memory_order_relaxed);
      if (bands != mActiveBands)
      {
         // A band count change re-wires the pins, so it is an edit, not a
         // gesture: start the new tree from silence rather than reuse states
         // that belonged to a different split.
         Reset();
         mActiveBands = bands;
      }
      AudioBuffer* out[SplitterNode::kMaxBands] = {};
      for (int o = 0; o < numOutputs && o < SplitterNode::kMaxBands; o++)
         out[o] = outputs[o];
      const AudioBuffer* ref = nullptr;
      for (AudioBuffer* o : out)
         if (o != nullptr) { ref = o; break; }
      if (ref == nullptr)
         return;

      if (bands <= 1)
      {
         if (out[0] != nullptr)
         {
            AudioBuffer& output = *out[0];
            for (int ch = 0; ch < output.numChannels; ch++)
            {
               for (int i = 0; i < output.numFrames; i++)
                  output.channels[ch][i] = (in != nullptr) ? in->channels[ch][i] : 0.0f;
            }
         }
         ZeroFrom(out, 1);
         return;
      }

      SmoothCutoffs(ref->numFrames);
      const float k = 1.41421356f; // Butterworth damping, 1/Q
      const int numCh = std::min(ref->numChannels, kMaxChannels);
      for (int ch = 0; ch < numCh; ch++)
      {
         DspMath::TptSvf* x1 = mSplit[ch][0];
         DspMath::TptSvf* x2 = mSplit[ch][1];
         DspMath::TptSvf* x3 = mSplit[ch][2];
         for (int i = 0; i < ref->numFrames; i++)
         {
            const float x = (in != nullptr && ch < in->numChannels) ? in->channels[ch][i] : 0.0f;
            float band[SplitterNode::kMaxBands] = {};
            // Tree: split at 1, the high side at 2, its high side at 3.
            float lo, hi;
            Lr4(x1, x, lo, hi);
            band[0] = lo;
            if (bands == 2)
               band[1] = hi;
            else
            {
               float lo2, hi2;
               Lr4(x2, hi, lo2, hi2);
               band[0] = Allpass(mAllpass[ch][0], band[0], k); // phase of split 2
               band[1] = lo2;
               if (bands == 3)
                  band[2] = hi2;
               else
               {
                  float lo3, hi3;
                  Lr4(x3, hi2, lo3, hi3);
                  band[0] = Allpass(mAllpass[ch][1], band[0], k); // phase of split 3
                  band[1] = Allpass(mAllpass[ch][2], band[1], k);
                  band[2] = lo3;
                  band[3] = hi3;
               }
            }
            for (int b = 0; b < bands; b++)
               if (out[b] != nullptr && ch < out[b]->numChannels)
                  out[b]->channels[ch][i] = band[b];
         }
      }
      for (int b = 0; b < SplitterNode::kMaxBands; b++)
      {
         if (out[b] == nullptr)
            continue;
         for (int ch = (b < bands ? numCh : 0); ch < out[b]->numChannels; ch++)
            std::fill(out[b]->channels[ch], out[b]->channels[ch] + out[b]->numFrames, 0.0f);
      }
   }

   // Main thread only.
   void PushParams(const SplitterNode& n)
   {
      mBands.store(n.Bands(), std::memory_order_relaxed);
      mTargetHz[0].store(n.split1, std::memory_order_relaxed);
      mTargetHz[1].store(n.split2, std::memory_order_relaxed);
      mTargetHz[2].store(n.split3, std::memory_order_relaxed);
   }

private:
   // LR4: low = LP(LP(x)), high = HP(HP(x)), sharing the first section.
   static void Lr4(DspMath::TptSvf* f, float x, float& lo, float& hi)
   {
      const DspMath::TptSvf::Outputs a = f[0].Process(x);
      lo = f[1].Process(a.low).low;
      hi = f[2].Process(a.high).high;
   }

   // LP^2 + HP^2 of the LR4 above: x - 2k band.
   static float Allpass(DspMath::TptSvf& f, float x, float k)
   {
      return x - 2.0f * k * f.Process(x).band;
   }

   static void ZeroFrom(AudioBuffer* const* out, int first)
   {
      for (int b = first; b < SplitterNode::kMaxBands; b++)
      {
         if (out[b] == nullptr)
            continue;
         for (int ch = 0; ch < out[b]->numChannels; ch++)
            std::fill(out[b]->channels[ch], out[b]->channels[ch] + out[b]->numFrames, 0.0f);
      }
   }

   // Splits in order, a third of an octave apart at least, below Nyquist.
   void OrderedHz(float hz[kSplits]) const
   {
      const float top = (float)(0.45 * mSampleRate);
      float floorHz = 20.0f;
      for (int s = 0; s < kSplits; s++)
      {
         hz[s] = std::clamp(mHz[s], floorHz, std::max(floorHz, top));
         floorHz = hz[s] * 1.26f;
      }
   }

   void SetCutoffs()
   {
      float hz[kSplits];
      OrderedHz(hz);
      for (int ch = 0; ch < kMaxChannels; ch++)
      {
         for (int s = 0; s < kSplits; s++)
            for (DspMath::TptSvf& f : mSplit[ch][s])
            {
               f.SetSampleRate(mSampleRate);
               f.SetCutoff(hz[s], 0.70710678f);
            }
         const float apHz[3] = { hz[1], hz[2], hz[2] };
         for (int a = 0; a < 3; a++)
         {
            mAllpass[ch][a].SetSampleRate(mSampleRate);
            mAllpass[ch][a].SetCutoff(apHz[a], 0.70710678f);
         }
      }
   }

   // Per block, in octaves, ~20 ms: a split knob sweeps instead of stepping.
   void SmoothCutoffs(int frames)
   {
      const float a = 1.0f - std::exp(-(float)frames / (0.02f * (float)mSampleRate));
      bool moved = false;
      for (int s = 0; s < kSplits; s++)
      {
         const float target = std::clamp(mTargetHz[s].load(std::memory_order_relaxed), 20.0f, 20000.0f);
         if (target == mHz[s])
            continue;
         const float next = mHz[s] * std::pow(target / mHz[s], a);
         mHz[s] = std::fabs(next - target) < 0.01f ? target : next;
         moved = true;
      }
      if (moved)
         SetCutoffs();
   }

   std::atomic<int> mBands { 1 };
   std::atomic<float> mTargetHz[kSplits] { { 250.0f }, { 2500.0f }, { 8000.0f } };
   double mSampleRate = 48000.0;
   int mActiveBands = 1;
   float mHz[kSplits] = { 250.0f, 2500.0f, 8000.0f };
   DspMath::TptSvf mSplit[kMaxChannels][kSplits][3];
   DspMath::TptSvf mAllpass[kMaxChannels][3]; // band 1 at split 2, band 1 at split 3, band 2 at split 3
};

SplitterNode::SplitterNode() = default;
SplitterNode::~SplitterNode() = default;

void SplitterNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioSplitterNode>();
   mAudioNode->PushParams(*this);
}

void SplitterNode::VisitParams(ParamVisitor& v)
{
   v.Int("bands", bands);
   v.Float("split1", split1);
   v.Float("split2", split2);
   v.Float("split3", split3);
}

AudioNode* SplitterNode::GetAudioNode()
{
   if (!mAudioNode)
   {
      mAudioNode = std::make_unique<AudioSplitterNode>();
      mAudioNode->PushParams(*this);
   }
   return mAudioNode.get();
}

// ----------------------------------------------------------------------- In
class AudioCaptureNode : public AudioNode
{
public:
   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mMailbox.PrepareToPlay(sampleRate);
      mMailbox.SetImmediate(kGainDbParam, mGainDb.load(std::memory_order_relaxed));
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& buffer) override
   {
      // Platform::AudioInputCaptureRead is lock-free and zero-fills on
      // underrun, so this is safe to call unconditionally even before the
      // tap has produced its first block (or if mic permission was denied).
      //
      // Captured into a private scratch pair rather than straight into
      // `buffer`: on a mono output buffer both entries of a
      // buffer.channels-derived array point at the same memory, so the
      // right-channel read overwrote the left one it had just filled and
      // drained ring 0 into nothing. Scratch also keeps the gain loop below
      // reading source samples rather than samples it has already scaled.
      const int frames = std::min(buffer.numFrames, kMaxCaptureFrames);
      float* chans[2] = { mScratch[0], mScratch[1] };
      const int mode = mChannelMode.load(std::memory_order_relaxed);
      const int channelOffset = (mode <= 0) ? 0 : (mode - 1);
      const bool isMono = (mode > 0);
      // Inside the plugin the host feeds Audio In; decided here so it holds from the first block,
      // before the main thread has cooked the node.
      const int bus = Platform::PluginModeActive() ? (mSource.load(std::memory_order_relaxed) == AudioInputNode::kSourceHostSidechain ? 1 : 0) : -1;
      const int captured = bus >= 0 ? PluginEngine::ReadHostInput(bus, chans, frames, channelOffset, isMono)
                                    : Platform::AudioInputCaptureRead(chans, frames, 2, mReaderCursor, channelOffset, isMono);

      float peak = 0.0f;
      for (int i = 0; i < buffer.numFrames; i++)
      {
         const float linear = DspMath::DbToLinear(mMailbox.SmoothedValue(kGainDbParam));
         for (int ch = 0; ch < buffer.numChannels; ch++)
         {
            const float s = (captured > 0 && i < frames) ? mScratch[std::min(ch, 1)][i] : 0.0f;
            const float v = s * linear;
            buffer.channels[ch][i] = v;
            peak = std::max(peak, std::fabs(v));
         }
      }
      mMeter.Write(&peak, 1);
   }

   // Main thread only.
   void PushParams(float gainDb, int channelMode, int source)
   {
      mGainDb.store(gainDb, std::memory_order_relaxed);
      mChannelMode.store(channelMode, std::memory_order_relaxed);
      mSource.store(source, std::memory_order_relaxed);
      mMailbox.Push(kGainDbParam, gainDb);
   }

   MeterRing& Meter() { return mMeter; }

private:
   static constexpr int kMaxCaptureFrames = kAudioMaxBlockFrames;

   ParamMailbox mMailbox;
   MeterRing mMeter;
   std::atomic<float> mGainDb { 0.0f };
   std::atomic<int> mChannelMode { 0 };
   std::atomic<int> mSource { 0 }; // AudioInputNode::Source
   uint64_t mReaderCursor = 0;
   float mScratch[2][kMaxCaptureFrames] = {};
};

// The device tap is only held while the node reads the device: inside the plugin it reads the
// host and never asks for the microphone.
AudioInputNode::AudioInputNode() = default;

AudioInputNode::~AudioInputNode()
{
   if (mDeviceRef)
      Platform::AudioInputCaptureRemoveRef();
}

bool AudioInputNode::ReadsHost() const { return Platform::PluginModeActive(); }

void AudioInputNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioCaptureNode>();
   const bool host = ReadsHost();
   mAudioNode->PushParams(gainDb, channelMode, source);
   if (host)
   {
      if (mDeviceRef)
      {
         Platform::AudioInputCaptureRemoveRef();
         mDeviceRef = false;
      }
      mStatus.clear();
      float peak = 0.0f;
      if (mAudioNode->Meter().ReadLatest(peak))
         mLevel = peak;
      return;
   }
   if (!mDeviceRef)
   {
      Platform::AudioInputCaptureAddRef();
      mDeviceRef = true;
   }

   if (deviceId != 0)
      Platform::AudioInputCaptureSetDevice((uint32_t)deviceId);

   // The pump's error used to be collected into a local and dropped on the
   // floor, so every reason the tap can fail to install - permission denied,
   // permission not answered yet, no input device - looked identical to
   // "listening, but the room is quiet". Keep it for the body's status line.
   std::string error;
   Platform::AudioInputCapturePump(error); // no-op once the tap is already live
   mStatus = Platform::AudioInputCaptureIsRunning() ? std::string() : error;

   float peak = 0.0f;
   if (mAudioNode->Meter().ReadLatest(peak))
      mLevel = peak;
}

void AudioInputNode::VisitParams(ParamVisitor& v)
{
   v.Float("gainDb", gainDb);
   gainDb = std::clamp(gainDb, kMinGainDb, kMaxGainDb); // as GainNode
   v.Int("channelMode", channelMode);
   v.Int("deviceId", deviceId);
   v.Text("deviceName", deviceName);
   v.Int("source", source);
   source = std::clamp(source, (int)kSourceDefault, (int)kSourceHostSidechain);
}

AudioNode* AudioInputNode::GetAudioNode()
{
   if (!mAudioNode)
   {
      mAudioNode = std::make_unique<AudioCaptureNode>();
      mAudioNode->PushParams(gainDb, channelMode, source);
   }
   return mAudioNode.get();
}

// ------------------------------------------------------------------- Output

AudioOutputNode::~AudioOutputNode()
{
   StopRecording(); // never leave a WAV with unpatched chunk sizes
}

void AudioOutputNode::CookIfNeeded(int /*frameId*/)
{
   if (!mWriter.IsOpen())
      return;
   // Interleaved stereo, matching AudioEngine::RunTopology's capture write.
   float scratch[4096];
   int n;
   while ((n = mCaptureRing.Read(scratch, 4096)) > 0)
   {
      int frames = n / 2;
      const float* src = scratch;
      if (mTakeSkip > 0)
      {
         const int drop = (int)std::min<int64_t>(mTakeSkip, frames);
         mTakeSkip -= drop;
         src += (size_t)drop * 2;
         frames -= drop;
      }
      if (mTakeLimit > 0)
         frames = (int)std::min<int64_t>(frames, std::max<int64_t>(0, mTakeLimit - mTakeFrames));
      if (frames <= 0)
         continue;
      for (int i = 0; i < frames * 2; i++)
         mTakePeak = std::max(mTakePeak, std::fabs(src[i]));
      mWriter.Append(src, frames);
      MeterTake(src, frames);
      mTakeFrames += frames;
   }
}

void AudioOutputNode::VisitParams(ParamVisitor& v)
{
   v.Int("formatIndex", formatIndex);
   v.Text("recordDirectory", recordDirectory);
   v.Bool("live", live);
}

// The take's own loudness and true peak, measured on the samples that reach the file (plan G2).
void AudioOutputNode::MeterTake(const float* interleaved, int frames)
{
   if (!mTakeMeter)
      return;
   float l[2048], r[2048];
   for (int done = 0; done < frames;)
   {
      const int n = std::min(2048, frames - done);
      for (int i = 0; i < n; i++)
      {
         l[i] = interleaved[(size_t)(done + i) * 2];
         r[i] = interleaved[(size_t)(done + i) * 2 + 1];
      }
      mTakeMeter->Process(l, r, n);
      done += n;
   }
}

float AudioOutputNode::TakeLufs() const { return mTakeMeter ? mTakeMeter->Integrated() : MixerMeter::kSilent; }
float AudioOutputNode::TakeTruePeakDb() const { return mTakeMeter ? mTakeMeter->TruePeakDb() : MixerMeter::kSilent; }

bool AudioOutputNode::StartRecording(const std::string& path, double sampleRate, int bitDepth)
{
   if (IsRecording())
      return false;
   if (sampleRate <= 0.0)
      sampleRate = AudioEngine::Instance().SampleRate();
   if (sampleRate <= 0.0)
      return false;
   if (!mWriter.Open(path, sampleRate, 2, AudioFileWriter::Format::Auto, bitDepth == 24 ? 24 : 16))
      return false;
   mOpenSampleRate = sampleRate;
   mTakePeak = 0.0f;
   if (!mTakeMeter)
      mTakeMeter = std::make_unique<MixerMeter>();
   mTakeMeter->Prepare(sampleRate);
   mTakeSkip = mTakeLimit = mTakeFrames = 0;
   mCaptureRing.overflowCount.store(0, std::memory_order_relaxed);
   mCaptureRing.enabled.store(true, std::memory_order_relaxed);
   return true;
}

void AudioOutputNode::StopRecording()
{
   mCaptureRing.enabled.store(false, std::memory_order_relaxed);
   // Drain whatever the ring still has queued before closing, so the last
   // fraction of a second isn't silently dropped.
   CookIfNeeded(0);
   mWriter.Close();
}
