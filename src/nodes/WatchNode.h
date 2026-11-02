#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "INode.h"
#include "Modulation.h"
#include "core/WatchPacket.h"

// --- Apple Watch --------------------------------------------------------
// Your wrist as 22 modulators: tilt (roll, pitch, yaw), turn and acceleration, energy, Digital Crown, touch pad,
// taps, four gestures (flick left/right, shake, strike) and heart rate. The watch runs Infinite Remote and sends
// packets over Bluetooth (platform/WatchLink.h); the gestures are found here, from the raw stream, so tuning
// ships with Infinite. docs/plans/apple-watch/README.md section 5 is the spec. macOS only for now.
class WatchNode : public INode
{
public:
   enum Output
   {
      kConnected = 0,
      kRoll, kPitch, kYaw,
      kRotX, kRotY, kRotZ,       // turn speed, signed, 0.5 = still
      kAccelX, kAccelY, kAccelZ, // acceleration without gravity, signed
      kEnergy,                   // how hard you move
      kCrown, kCrownSpeed,
      kTouchX, kTouchY, kTouching,
      kTap,                      // double tap or a pad
      kFlickLeft, kFlickRight, kShake, kStrike,   // triggers: 1 for 80 ms
      kHeart,
      kOutputCount
   };

   static INode* Create() { return new WatchNode(); }

   WatchNode();
   ~WatchNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   int OutputCount() const override { return kOutputCount; }
   const char* OutputLabel(int index) const override;
   IModulator* ModulatorOutput(int index) override;

   std::string watch;        // "" = the first connected watch
   float smooth = 0.5f;      // 0 = raw, 1 = very smooth (One-Euro on the continuous outputs)
   float sens = 1.0f;        // gesture sensitivity, 0.5 (needs a harder move) .. 2 (a lighter one)
   float range = 90.0f;      // degrees from the recentred pose to the end of roll, pitch and yaw

   void VisitParams(ParamVisitor& v) override
   {
      v.Text("watch", watch);
      v.Float("smooth", smooth);
      v.Float("sens", sens);
      v.Float("range", range);
   }

   // Makes the current pose (and crown position) the middle: roll, pitch and yaw read 0.5. Not a saved param.
   void Recenter();

   float Value(int index);
   // How many times a trigger output has fired since the node was made (tests, and the matrix's activity hint).
   uint32_t FireCount(int index)
   {
      Update();
      return (index >= 0 && index < kOutputCount) ? mFires[index] : 0;
   }
   // Roll, pitch and yaw relative to the recentred pose, degrees, before mapping (the card's meters).
   void Angles(float& rollDeg, float& pitchDeg, float& yawDeg) const;

   // "1 watch connected", "Bluetooth is off ...", plus a dropped-packet count when it matters.
   std::string Status() const;
   // Watch names for the card's dropdown ("Any watch" first); the saved name is kept even when it is not around.
   std::vector<std::string> WatchChoices(int& current) const;

private:
   struct Tap : public IModulator
   {
      WatchNode* owner = nullptr;
      int index = 0;
      float Value01() override { return owner ? owner->Value(index) : 0.0f; }
   };

   void Update();
   void Feed(const WatchPacket::Sample& s);
   void Fire(Output o, uint32_t tMs);
   void LogSample(const WatchPacket::Sample& s);
   void Smoothed(Output o, float x, double t);

   Tap mTaps[kOutputCount];
   int mLastCookFrame = -1;
   bool mStarted = false;

   // link
   uint64_t mCursor = UINT64_MAX;   // "nothing read yet": Poll resyncs to the end, so a backlog never replays
   bool mConnected = false;
   bool mNeedsUpdate = false;
   bool mFirst = true;                 // next sample is the first of a connection: recenter on it
   bool mRecenterPending = false;
   std::vector<WatchPacket::Sample> mFresh;

   // state
   WatchPacket::Sample mLast;
   bool mHaveLast = false;
   float mRef[4] = { 1, 0, 0, 0 };     // conj-applied reference attitude
   float mCrownRef = 0;
   float mAngles[3] = {};
   float mVals[kOutputCount] = {};
   double mFiredAt[kOutputCount] = {};   // wall clock, for the 80 ms pulse
   uint32_t mFires[kOutputCount] = {};

   // smoothing and envelopes (watch time)
   struct Euro { float x = 0, dx = 0; double t = -1; };
   Euro mEuro[kOutputCount];
   float mEnergyEnv = 0;
   uint32_t mPrevMs = 0;
   float mPrevCrown = 0;
   float mCrownSpeed = 0;
   uint16_t mPrevFlags = 0;

   // gestures (watch time, ms)
   uint32_t mFlickBlockedUntil = 0, mShakeBlockedUntil = 0, mStrikeBlockedUntil = 0;
   int mShakeSign = 0;
   uint32_t mShakeTimes[8] = {};
   int mShakeCount = 0;
   bool mStrikeArmed = false;
   bool mStrikeSpent = false;   // a push that lasted too long: wait for it to end before arming again
   uint32_t mStrikeAt = 0;

   // INFINITE_WATCH_LOG=<path>: raw samples and the arrival rate as CSV, for tuning the gestures from real numbers.
   bool mLogChecked = false;
   FILE* mLog = nullptr;
   double mRateT0 = 0;
   int mRateCount = 0;
   int mRateDrops = 0;
   uint8_t mPrevSeq = 0;
   bool mHaveSeq = false;
};
