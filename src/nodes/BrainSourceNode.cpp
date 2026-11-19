#include "BrainSourceNode.h"

#include "core/Transport.h"
#include "core/bci/BciDspEngine.h"
#include "core/bci/BciRuntime.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace
{
   const char* kHexDigits = "0123456789abcdef";

   int HexNibble(char c)
   {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
   }
}

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
      "All Blinks"
   };
   return names;
}

BrainSourceNode::BrainSourceNode()
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
      dsp.SetBlinkMode(static_cast<Bci::BlinkMode>(std::clamp(wantBlinkMode, 0, 1)));

      // 4 samples @ 256 Hz per ~15.6 ms cycle (~64 Hz fast somatic update)
      runtime.PollSamples(4, batch);
      if (!batch.empty())
      {
         dsp.Process(batch.data(), (int)batch.size(), pins, snap);
         snap.packMissing = runtime.IsPackMissing();
         {
            std::lock_guard<std::mutex> tlk(mTelemetryMu);
            std::snprintf(snap.statusText, sizeof(snap.statusText), "%s", mStatus.c_str());
            mTelemetry = snap;
         }
         for (int i = 0; i < kOutputCount; ++i)
            mPublished[i].store(std::clamp(pins[i], 0.0f, 1.0f), std::memory_order_relaxed);
      }
   }

   runtime.Close();
}
