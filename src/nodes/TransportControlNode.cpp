#include "TransportControlNode.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "audio/AudioBuffer.h"
#include "audio/AudioEngine.h"
#include "audio/AudioNode.h"
#include "audio/Metronome.h"
#include "audio/MusicTime.h"
#include "core/Transport.h"
#include "platform/Platform.h"

namespace
{
   double NowSeconds()
   {
      using namespace std::chrono;
      return duration_cast<duration<double>>(steady_clock::now().time_since_epoch()).count();
   }
}

// ------------------------------------------------------------- click pin
// The metronome on the node's audio output (index 4), for patching into a
// mixer / Audio Out like any source. Silent while the click is off.
class AudioTransportClickNode : public AudioNode
{
public:
   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mGen.Reset();
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      for (int ch = 0; ch < output.numChannels; ch++)
         std::fill(output.channels[ch], output.channels[ch] + output.numFrames, 0.0f);
      Transport& transport = Transport::Instance();
      if (!mOn.load(std::memory_order_relaxed) || !transport.IsPlaying() || mSampleRate <= 0.0)
      {
         mGen.Reset();
         return;
      }
      const double spb = mSampleRate * 60.0 / std::max(1.0, (double)transport.Tempo());
      mGen.RenderAdd(output.channels, output.numChannels, output.numFrames, transport.Beats(), spb,
                     transport.BeatsPerBar(), mSampleRate, mVolume.load(std::memory_order_relaxed),
                     mAccent.load(std::memory_order_relaxed));
   }

   void Push(bool on, float volume, bool accent)
   {
      mVolume.store(volume, std::memory_order_relaxed);
      mAccent.store(accent, std::memory_order_relaxed);
      mOn.store(on, std::memory_order_relaxed);
   }

private:
   double mSampleRate = 48000.0;
   MetronomeClick mGen;
   std::atomic<bool> mOn { false };
   std::atomic<float> mVolume { 0.5f };
   std::atomic<bool> mAccent { true };
};

namespace
{
   // The direct click belongs to the Transport Control that switched it on
   // last. Undo/reload rebuild nodes and destroy the old ones a frame later,
   // so only the owner may switch it off again.
   const void* gDirectClickOwner = nullptr;
}

TransportControlNode::TransportControlNode() = default;

TransportControlNode::~TransportControlNode()
{
   if (gDirectClickOwner == this)
   {
      AudioEngine::Instance().SetDirectClick(false, clickVolume, clickAccent);
      gDirectClickOwner = nullptr;
   }
}

AudioNode* TransportControlNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioTransportClickNode>();
   return mAudioNode.get();
}

float TransportControlNode::OutTap::Value01()
{
   Transport& t = Transport::Instance();
   switch (index)
   {
      case 0: { const double b = t.Beats(); return (float)(b - std::floor(b)); }
      case 1: { const double b = t.Bars(); return (float)(b - std::floor(b)); }
      case 2: return std::clamp((t.Tempo() - 20.0f) / 280.0f, 0.0f, 1.0f);
      default: return t.IsPlaying() ? 1.0f : 0.0f;
   }
}

const char* TransportControlNode::OutputLabel(int index) const
{
   static const char* const kLabels[5] = { "beat", "bar", "bpm", "play", "click" };
   return kLabels[std::clamp(index, 0, 4)];
}

IModulator* TransportControlNode::ModulatorOutput(int index)
{
   if (!mTapsBound)
   {
      for (int i = 0; i < 4; i++)
         mOutTaps[i].index = i;
      mTapsBound = true;
   }
   return (index >= 0 && index < 4) ? &mOutTaps[index] : nullptr;
}

int TransportControlNode::MeterDen(int index)
{
   static const int kDen[4] = { 2, 4, 8, 16 };
   return kDen[std::clamp(index, 0, 3)];
}

bool TransportControlNode::ClockPresent() const
{
   return Platform::MidiClockIsPresent() && Platform::MidiClockBpm() > 0.0f;
}

void TransportControlNode::Play()
{
   Transport::Instance().SetPlaying(true);
}

void TransportControlNode::Stop()
{
   Transport::Instance().SetPlaying(false);
}

void TransportControlNode::Rewind()
{
   Transport::Instance().Rewind();
}

void TransportControlNode::Tap()
{
   const double now = NowSeconds();
   // A pause longer than 2 s starts a new tap run.
   if (mNumTaps > 0 && now - mTaps[mNumTaps - 1] > 2.0)
      mNumTaps = 0;
   if (mNumTaps == 8)
   {
      for (int i = 1; i < 8; i++)
         mTaps[i - 1] = mTaps[i];
      mNumTaps = 7;
   }
   mTaps[mNumTaps++] = now;
   if (mNumTaps < 2)
      return;
   const double mean = (mTaps[mNumTaps - 1] - mTaps[0]) / (double)(mNumTaps - 1);
   if (mean <= 0.0)
      return;
   bpm = std::clamp((float)(60.0 / mean), 20.0f, 300.0f);
   if (!driveTempo)
      Transport::Instance().SetTempo(bpm); // not driving: the tap still sets the tempo once
   else
      mAppliedBpm = bpm;                   // a tap is a jump, not a glide
}

void TransportControlNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;

   Transport& transport = Transport::Instance();
   const double now = NowSeconds();

   // ---- metronome
   clickVolume = std::clamp(clickVolume, 0.0f, 1.0f);
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioTransportClickNode>();
   mAudioNode->Push(click, clickVolume, clickAccent);
   {
      // Only write the engine when this node's direct state changes, so two
      // Transport Controls don't fight over it every frame.
      const bool direct = click && clickDirect;
      if (direct != mDirectSent || (direct && (clickVolume != mDirectVolSent || clickAccent != mDirectAccentSent)))
      {
         if (direct)
         {
            AudioEngine::Instance().SetDirectClick(true, clickVolume, clickAccent);
            gDirectClickOwner = this;
         }
         else if (gDirectClickOwner == this || gDirectClickOwner == nullptr)
         {
            AudioEngine::Instance().SetDirectClick(false, clickVolume, clickAccent);
            gDirectClickOwner = nullptr;
         }
         mDirectSent = direct;
         mDirectVolSent = clickVolume;
         mDirectAccentSent = clickAccent;
      }
   }
   const double dt = (mLastCookTime < 0.0) ? 0.0 : std::clamp(now - mLastCookTime, 0.0, 0.25);
   mLastCookTime = now;

   // ---- tempo
   bpm = std::clamp(bpm, 20.0f, 300.0f);
   const bool clock = followClock && ClockPresent();
   if (driveTempo || clock)
   {
      const float target = clock ? std::clamp(Platform::MidiClockBpm(), 20.0f, 300.0f) : bpm;
      if (mAppliedBpm < 0.0f)
         mAppliedBpm = transport.Tempo();
      if (clock || glide <= 0.0f || dt <= 0.0)
         mAppliedBpm = target;
      else
      {
         // Exponential approach, ~98% of the way after `glide` seconds.
         const float k = 1.0f - expf(-4.0f * (float)dt / std::max(0.01f, glide));
         mAppliedBpm += (target - mAppliedBpm) * k;
         if (std::fabs(target - mAppliedBpm) < 0.01f)
            mAppliedBpm = target;
      }
      if (std::fabs(transport.Tempo() - mAppliedBpm) > 0.005f)
         transport.SetTempo(mAppliedBpm);
   }
   else
   {
      mAppliedBpm = transport.Tempo();
      bpm = std::clamp(mAppliedBpm, 20.0f, 300.0f); // mirror while not driving
   }

   // ---- key / scale
   key = std::clamp(key, 0, 11);
   scale = std::clamp(scale, 0, MusicTime::kNumScaleTypes - 1);
   if (driveKey && (transport.Key() != key || transport.Scale() != scale))
   {
      const long long bar = (long long)std::floor(transport.Bars());
      if (!keyOnBar || !transport.IsPlaying())
      {
         transport.SetKey(key);
         transport.SetScale(scale);
         mKeyPending = false;
      }
      else if (!mKeyPending)
      {
         mKeyPending = true;
         mPendingBar = bar;
      }
      else if (bar != mPendingBar)
      {
         transport.SetKey(key);
         transport.SetScale(scale);
         mKeyPending = false;
      }
   }
   else if (!driveKey)
   {
      mKeyPending = false;
      key = transport.Key(); // mirror while not driving
      scale = std::clamp(transport.Scale(), 0, MusicTime::kNumScaleTypes - 1);
   }
   else
   {
      mKeyPending = false;
   }

   // ---- meter
   meterNum = std::clamp(meterNum, 1, 16);
   meterDenIndex = std::clamp(meterDenIndex, 0, 3);
   if (driveMeter)
   {
      if (transport.TimeSigNumerator() != meterNum || transport.TimeSigDenominator() != MeterDen(meterDenIndex))
         transport.SetTimeSignature(meterNum, MeterDen(meterDenIndex));
   }
   else
   {
      meterNum = transport.TimeSigNumerator(); // mirror while not driving
      const int den = transport.TimeSigDenominator();
      meterDenIndex = den <= 2 ? 0 : den <= 4 ? 1 : den <= 8 ? 2 : 3;
   }
}

void TransportControlNode::VisitParams(ParamVisitor& v)
{
   v.Bool("driveTempo", driveTempo);
   v.Float("bpm", bpm);
   v.Float("glide", glide);
   v.Bool("followClock", followClock);
   v.Bool("driveKey", driveKey);
   v.Int("key", key);
   v.Int("scale", scale);
   v.Bool("keyOnBar", keyOnBar);
   v.Bool("audioOnOpen", audioOnOpen);
   v.Bool("click", click);
   v.Float("clickVolume", clickVolume);
   v.Bool("clickDirect", clickDirect);
   v.Bool("clickAccent", clickAccent);
   v.Bool("driveMeter", driveMeter);
   v.Int("meterNum", meterNum);
   v.Int("meterDenIndex", meterDenIndex);
}
