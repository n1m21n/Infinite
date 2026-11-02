#include "WatchNode.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>

#include "platform/WatchLink.h"

namespace
{
   constexpr float kRad2Deg = 57.29577951f;
   constexpr double kPulseSec = 0.080;         // how long a trigger output reads 1
   constexpr uint32_t kRefractoryMs = 250;     // gap after a gesture before the same one can fire again
   constexpr uint32_t kShakeWindowMs = 600;
   constexpr uint32_t kStrikeWindowMs = 120;

   const char* kLabels[WatchNode::kOutputCount] = {
      "connected", "roll", "pitch", "yaw",
      "rot x", "rot y", "rot z",
      "accel x", "accel y", "accel z",
      "energy", "crown", "crown speed",
      "touch x", "touch y", "touching",
      "tap", "flick left", "flick right", "shake", "strike",
      "heart"
   };

   double NowSec()
   {
      return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
   }

   float Clamp01(float v) { return std::min(1.f, std::max(0.f, v)); }
   float Signed01(float v, float fullScale) { return 0.5f + std::min(1.f, std::max(-1.f, v / fullScale)) * 0.5f; }

   // One-Euro filter (Casiez et al.), as the Track nodes use it: smooth when still, responsive when fast.
   float EuroStep(float x, float& prev, float& dprev, double& tprev, double t, float minCutoff, float beta)
   {
      if (tprev < 0) { prev = x; dprev = 0; tprev = t; return x; }
      const float dt = (float)std::max(1e-3, t - tprev);
      tprev = t;
      auto alpha = [dt](float cutoff)
      {
         const float tau = 1.f / (2.f * 3.14159265f * cutoff);
         return 1.f / (1.f + tau / dt);
      };
      const float dx = (x - prev) / dt;
      dprev += alpha(1.0f) * (dx - dprev);
      const float cutoff = minCutoff + beta * std::fabs(dprev);
      prev += alpha(cutoff) * (x - prev);
      return prev;
   }

   // conj(a) * b
   void ConjMul(const float a[4], const float b[4], float out[4])
   {
      const float aw = a[0], ax = -a[1], ay = -a[2], az = -a[3];
      out[0] = aw * b[0] - ax * b[1] - ay * b[2] - az * b[3];
      out[1] = aw * b[1] + ax * b[0] + ay * b[3] - az * b[2];
      out[2] = aw * b[2] - ax * b[3] + ay * b[0] + az * b[1];
      out[3] = aw * b[3] + ax * b[2] - ay * b[1] + az * b[0];
   }

   // Roll about x, pitch about y, yaw about z (aerospace order), degrees. No gimbal trouble within +-90 pitch.
   void Euler(const float q[4], float& roll, float& pitch, float& yaw)
   {
      const float w = q[0], x = q[1], y = q[2], z = q[3];
      roll = std::atan2(2.f * (w * x + y * z), 1.f - 2.f * (x * x + y * y)) * kRad2Deg;
      pitch = std::asin(std::min(1.f, std::max(-1.f, 2.f * (w * y - z * x)))) * kRad2Deg;
      yaw = std::atan2(2.f * (w * z + x * y), 1.f - 2.f * (y * y + z * z)) * kRad2Deg;
   }
}

WatchNode::WatchNode()
{
   for (int i = 0; i < kOutputCount; i++)
   {
      mTaps[i].owner = this;
      mTaps[i].index = i;
      mFiredAt[i] = -1e9;
   }
   for (int i : { kRotX, kRotY, kRotZ, kAccelX, kAccelY, kAccelZ, kRoll, kPitch, kYaw, kCrown, kCrownSpeed })
      mVals[i] = 0.5f;
}

WatchNode::~WatchNode()
{
   if (mLog)
      std::fclose(mLog);
   if (mStarted)
      WatchLink::Stop();
}

const char* WatchNode::OutputLabel(int index) const
{
   return (index < 0 || index >= kOutputCount) ? "out" : kLabels[index];
}

IModulator* WatchNode::ModulatorOutput(int index)
{
   return (index < 0 || index >= kOutputCount) ? nullptr : &mTaps[index];
}

void WatchNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId) return;
   mLastCookFrame = frameId;
   Update();
}

float WatchNode::Value(int index)
{
   if (index < 0 || index >= kOutputCount) return 0.f;
   Update();   // cheap and idempotent: it only consumes samples that arrived since the last call
   if ((index >= kFlickLeft && index <= kStrike) || index == kTap)
      return (NowSec() - mFiredAt[index]) < kPulseSec ? 1.f : 0.f;
   if (index == kConnected)
      return mConnected ? 1.f : 0.f;
   return Clamp01(mVals[index]);
}

void WatchNode::Angles(float& r, float& p, float& y) const
{
   r = mAngles[0]; p = mAngles[1]; y = mAngles[2];
}

void WatchNode::Recenter()
{
   if (!mHaveLast)
   {
      mRecenterPending = true;
      return;
   }
   std::copy(mLast.q, mLast.q + 4, mRef);
   mCrownRef = mLast.crown;
   for (Euro& e : mEuro) e.t = -1;   // snap to the new middle instead of gliding there
   // The outputs read the middle at once; the next sample refines them.
   mAngles[0] = mAngles[1] = mAngles[2] = 0;
   mVals[kRoll] = mVals[kPitch] = mVals[kYaw] = mVals[kCrown] = 0.5f;
}

void WatchNode::Fire(Output o, uint32_t)
{
   mFiredAt[o] = NowSec();
   mFires[o]++;
}

void WatchNode::Smoothed(Output o, float x, double t)
{
   const float sm = Clamp01(smooth);
   if (sm <= 0.f) { mEuro[o].t = -1; mVals[o] = x; return; }
   const float minCut = 8.f * std::pow(0.5f / 8.f, sm);
   mVals[o] = EuroStep(x, mEuro[o].x, mEuro[o].dx, mEuro[o].t, t, minCut, 1.5f);
}

void WatchNode::Update()
{
   if (!mStarted)
   {
      mStarted = true;
      WatchLink::Start();
   }
   mFresh.clear();
   const WatchLink::State st = WatchLink::Poll(watch, mCursor, mFresh);
   mNeedsUpdate = st == WatchLink::State::kNeedsUpdate;
   const bool now = st == WatchLink::State::kConnected;
   if (!now && mConnected)
      mFirst = true;   // whatever comes next is a new connection: recenter, forget the gesture state
   mConnected = now;
   for (const WatchPacket::Sample& s : mFresh)
      Feed(s);
}

// Opt-in raw capture. Lines: "S,t,seq,q0..3,rot0..2,accel0..2,grav0..2,crown,flags" per sample and
// "R,wallSec,packetsInLastSecond" once a second, so a BLE drop shows as a rate below 50.
void WatchNode::LogSample(const WatchPacket::Sample& s)
{
   if (!mLogChecked)
   {
      mLogChecked = true;
      if (const char* path = std::getenv("INFINITE_WATCH_LOG"); path != nullptr && path[0] != '\0')
      {
         mLog = std::fopen(path, "a");
         if (mLog)
            std::fprintf(mLog, "# S,t_ms,seq,qw,qx,qy,qz,rotx,roty,rotz,accx,accy,accz,gx,gy,gz,crown,flags  |  R,wall_s,packets_per_s\n");
      }
   }
   if (!mLog)
      return;
   std::fprintf(mLog, "S,%u,%u,%.5f,%.5f,%.5f,%.5f,%.2f,%.2f,%.2f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%u\n", s.timeMs, s.seq,
                s.q[0], s.q[1], s.q[2], s.q[3], s.rot[0], s.rot[1], s.rot[2], s.accel[0], s.accel[1], s.accel[2],
                s.gravity[0], s.gravity[1], s.gravity[2], s.crown, s.flags);
   const double wall = NowSec();
   if (mRateT0 == 0)
      mRateT0 = wall;
   mRateCount++;
   if (wall - mRateT0 >= 1.0)
   {
      std::fprintf(mLog, "R,%.2f,%d\n", wall, mRateCount);
      std::fflush(mLog);
      mRateT0 = wall;
      mRateCount = 0;
   }
}

void WatchNode::Feed(const WatchPacket::Sample& s)
{
   LogSample(s);
   const uint32_t t = s.timeMs;
   const double ts = (double)t * 0.001;
   const float k = 1.0f / std::max(0.25f, std::min(4.0f, sens));   // thresholds scale down as sens goes up

   const bool right = (s.flags & WatchPacket::kRightWrist) != 0;
   const float mirror = right ? -1.f : 1.f;
   float dt = mFirst ? 0.02f : (float)(int32_t)(t - mPrevMs) * 0.001f;
   dt = std::min(0.5f, std::max(0.001f, dt));

   if (mFirst || mRecenterPending)
   {
      std::copy(s.q, s.q + 4, mRef);
      mCrownRef = s.crown;
      for (Euro& e : mEuro) e.t = -1;
      mEnergyEnv = 0;
      mCrownSpeed = 0;
      mPrevCrown = s.crown;
      mPrevFlags = s.flags;
      mShakeSign = 0; mShakeCount = 0; mStrikeArmed = mStrikeSpent = false;
      mFlickBlockedUntil = mShakeBlockedUntil = mStrikeBlockedUntil = t;
      mFirst = mRecenterPending = false;
   }
   mLast = s;
   mHaveLast = true;
   mPrevMs = t;

   // Orientation relative to the recentred pose.
   float rel[4];
   ConjMul(mRef, s.q, rel);
   float roll, pitch, yaw;
   Euler(rel, roll, pitch, yaw);
   mAngles[0] = roll; mAngles[1] = pitch; mAngles[2] = yaw;
   const float span = 2.f * std::max(1.f, range);
   Smoothed(kRoll, Clamp01(0.5f + mirror * roll / span), ts);
   Smoothed(kPitch, Clamp01(0.5f + pitch / span), ts);
   Smoothed(kYaw, Clamp01(0.5f + mirror * yaw / span), ts);

   // Turn speed and acceleration, signed around 0.5.
   const float full[3] = { 720.f, 720.f, 720.f };
   for (int i = 0; i < 3; i++)
   {
      Smoothed((Output)(kRotX + i), Signed01(s.rot[i], full[i]), ts);
      Smoothed((Output)(kAccelX + i), Signed01(s.accel[i], 4.f), ts);
   }

   // Energy: |user acceleration| through an attack 20 ms / release 300 ms envelope, 0..1.5 g -> 0..1.
   const float mag = std::sqrt(s.accel[0] * s.accel[0] + s.accel[1] * s.accel[1] + s.accel[2] * s.accel[2]);
   const float tau = mag > mEnergyEnv ? 0.020f : 0.300f;
   mEnergyEnv += (mag - mEnergyEnv) * (1.f - std::exp(-dt / tau));
   mVals[kEnergy] = Clamp01(mEnergyEnv / 1.5f);

   // Crown: turns since Recenter, one turn = 0.25; speed in turns/s (2 per second = full).
   const float crownSign = (s.flags & WatchPacket::kCrownLeft) ? -1.f : 1.f;
   Smoothed(kCrown, Clamp01(0.5f + crownSign * (s.crown - mCrownRef) * 0.25f), ts);
   const float v = crownSign * (s.crown - mPrevCrown) / dt;
   mCrownSpeed += (v - mCrownSpeed) * (1.f - std::exp(-dt / 0.060f));
   mVals[kCrownSpeed] = Clamp01(Signed01(mCrownSpeed, 2.f));
   mPrevCrown = s.crown;

   // Touch pad holds the last position when the finger lifts.
   if (s.flags & WatchPacket::kTouching)
   {
      Smoothed(kTouchX, Clamp01(s.touchX), ts);
      Smoothed(kTouchY, Clamp01(s.touchY), ts);
   }
   mVals[kTouching] = (s.flags & WatchPacket::kTouching) ? 1.f : 0.f;

   mVals[kHeart] = s.heart == 0 ? 0.f : Clamp01(((float)s.heart - 40.f) / 160.f);

   // Tap: a rising edge of double tap or any pad.
   const uint16_t tapBits = WatchPacket::kDoubleTap | WatchPacket::kAnyPad;
   if ((s.flags & tapBits & ~mPrevFlags) != 0)
      Fire(kTap, t);
   mPrevFlags = s.flags;

   // ---- gestures. Each has its own gap (250 ms) after it fires. ----
   // Flick: a fast turn about z; the sign picks the side (mirrored on the right wrist). Left and right share the
   // gap, so the backswing of a flick does not read as the opposite flick.
   const float rz = s.rot[2] * mirror;
   if (std::fabs(rz) > 400.f * k && (int32_t)(t - mFlickBlockedUntil) >= 0)
   {
      Fire(rz > 0 ? kFlickRight : kFlickLeft, t);
      mFlickBlockedUntil = t + kRefractoryMs;
   }

   // Shake: 4 or more sign changes of acceleration along x, each side beyond 1.2 g, inside 600 ms.
   const float ax = s.accel[0];
   if (std::fabs(ax) > 1.2f * k)
   {
      const int sign = ax > 0 ? 1 : -1;
      if (mShakeSign != 0 && sign != mShakeSign)
      {
         if (mShakeCount == 8) { std::copy(mShakeTimes + 1, mShakeTimes + 8, mShakeTimes); mShakeCount--; }
         mShakeTimes[mShakeCount++] = t;
      }
      mShakeSign = sign;
   }
   int keep = 0;
   for (int i = 0; i < mShakeCount; i++)
      if (t - mShakeTimes[i] <= kShakeWindowMs) mShakeTimes[keep++] = mShakeTimes[i];
   mShakeCount = keep;
   if (mShakeCount >= 4 && (int32_t)(t - mShakeBlockedUntil) >= 0)
   {
      Fire(kShake, t);
      mShakeBlockedUntil = t + kRefractoryMs;
      mShakeCount = 0;
      mShakeSign = 0;
      mStrikeArmed = false;
      mStrikeBlockedUntil = std::max(mStrikeBlockedUntil, t + kRefractoryMs);
   }

   // Strike: a hard push along the forearm (x beyond 2.5 g) that stops within 120 ms. A shake's own peaks do not count.
   if (mStrikeSpent)
   {
      if (ax < 1.0f) mStrikeSpent = false;
   }
   else if (!mStrikeArmed)
   {
      if (ax > 2.5f * k)
      {
         mStrikeArmed = true;
         mStrikeAt = t;
      }
   }
   else if (ax < 1.0f)
   {
      if (t - mStrikeAt <= kStrikeWindowMs && mShakeCount < 2 && (int32_t)(t - mStrikeBlockedUntil) >= 0)
      {
         Fire(kStrike, t);
         mStrikeBlockedUntil = t + kRefractoryMs;
      }
      mStrikeArmed = false;
   }
   else if (t - mStrikeAt > kStrikeWindowMs)
   {
      mStrikeArmed = false;   // a push that lasts is not a strike
      mStrikeSpent = true;
   }
}

std::string WatchNode::Status() const
{
   std::string s = WatchLink::Status();
   const uint32_t drops = WatchLink::Dropped(watch);
   if (mConnected && drops > 20)
      s += ", " + std::to_string(drops) + " packets lost";
   return s;
}

std::vector<std::string> WatchNode::WatchChoices(int& current) const
{
   std::vector<std::string> names = { "Any watch" };
   current = 0;
   for (const WatchLink::WatchInfo& w : WatchLink::ListWatches())
      names.push_back(w.connected ? w.name : w.name + " (not connected)");
   if (!watch.empty())
   {
      current = -1;
      for (size_t i = 1; i < names.size(); i++)
         if (names[i] == watch || names[i] == watch + " (not connected)") current = (int)i;
      if (current < 0)
      {
         names.push_back(watch + " (not connected)");
         current = (int)names.size() - 1;
      }
   }
   return names;
}
