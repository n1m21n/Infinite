#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "INode.h"
#include "Modulation.h"

// Drift: a musical random walk (Turbo port of upstream's Drift dynamics).
//
// Upstream's Drift learns each destination's "landscape" from how you move
// that knob by hand (the MovementLog / MovementStats engine, which Turbo does
// not have). This keeps the same physics - an Ornstein-Uhlenbeck walk with
// momentum, reflected inside a range - but around a fixed home point instead
// of a learned histogram:
//
//    v  *= exp(-dt / momentum)                         carry
//    x  += v dt - speed * theta * (x - home) dt        pull home
//          + sqrt(2 * speed * theta * k * stray) dW    wander (stray = temperature)
//    reflect x into [lo, hi]
//
// then optional tempo sample-and-hold (quantize) and an EMA (smoothing), and
// depth scales the swing around the centre. One output, 0..1.
class DriftNode : public INode, public IModulator
{
public:
   static INode* Create() { return new DriftNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   float Value01() override;

   float speed = 1.0f;     // 0.1..4
   float stray = 1.0f;     // 0.25..4 (temperature: how far it wanders from home)
   float momentum = 1.0f;  // seconds 0..4 (how long a push carries)
   float home = 0.5f;      // 0..1 centre of the well
   float rangeLo = 0.0f;
   float rangeHi = 1.0f;
   float depth = 1.0f;     // 0..1 scales the swing around 0.5
   int quantizeRate = 0;   // 0 off, else MusicTime::RateDivision + 1
   float smoothness = 0.0f; // 0..0.95 EMA
   int seed = 1;

   void VisitParams(ParamVisitor& v) override
   {
      v.Float("speed", speed); v.Float("stray", stray); v.Float("momentum", momentum);
      v.Float("home", home); v.Float("rangeLo", rangeLo); v.Float("rangeHi", rangeHi);
      v.Float("depth", depth); v.Int("quantizeRate", quantizeRate);
      v.Float("smoothness", smoothness); v.Int("seed", seed);
   }

   float RawPos() const { return mX; }

private:
   void Tick(double dt);
   float Gauss();

   float mX = 0.5f, mV = 0.0f;
   float mHeld = 0.5f, mSmoothed = 0.5f;
   long long mHeldIdx = -1;
   uint64_t mRng = 0;
   int mSeededWith = -1;
   int mLastCookFrame = -1;
   bool mHaveTime = false;
   std::chrono::steady_clock::time_point mLastTime;
};
