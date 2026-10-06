#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>

// Turbo 0.50: quantized restart shared by the transport-locked sequencers
// (Chord Progression, Note Sequencer, Arpeggiator, MIDI File). The main
// thread calls Request() (a "restart" trigger) and SetQuant(); the audio half
// calls Update() once per block with the block START beat and then fires the
// restart when ArmBeat() falls inside the block (Due / Fire). The grid is
// transport-beat based: the next beat, bar, 2 or 4 bars line at or after the
// block start; "immediate" fires at the block start.
//
// Lock free: a serial counter carries the request (two presses between two
// blocks read as one), the rest is audio-thread state plus one readout.
class QuantizedRestart
{
public:
   enum Quant
   {
      kNow = 0,
      kBeat,
      kBar,
      k2Bars,
      k4Bars,
      kNumQuants
   };

   static const std::vector<std::string>& Names()
   {
      static const std::vector<std::string> names = { "immediate", "next beat", "next bar", "next 2 bars",
                                                      "next 4 bars" };
      return names;
   }

   static double GridBeats(int quant, double beatsPerBar)
   {
      switch (quant)
      {
         case kBeat: return 1.0;
         case kBar: return beatsPerBar;
         case k2Bars: return 2.0 * beatsPerBar;
         case k4Bars: return 4.0 * beatsPerBar;
         default: return 0.0;
      }
   }

   // ---- main thread
   void Request() { mRequest.fetch_add(1, std::memory_order_relaxed); }
   void SetQuant(int quant) { mQuant.store(std::clamp(quant, 0, kNumQuants - 1), std::memory_order_relaxed); }
   bool Armed() const { return mArmedReadout.load(std::memory_order_relaxed); }

   // ---- audio thread
   // `playing` false: an armed line belongs to the old position, so the
   // request goes back to waiting and is placed again when play resumes.
   void Update(bool playing, double beats0, double beatsPerBar)
   {
      const int req = mRequest.load(std::memory_order_relaxed);
      if (req != mSeen)
      {
         mSeen = req;
         mPending = true;
         mArmed = false;
      }
      if (!playing)
      {
         if (mArmed)
         {
            mArmed = false;
            mPending = true;
         }
         Publish();
         return;
      }
      if (mPending || mArmed)
      {
         const double grid = GridBeats(mQuant.load(std::memory_order_relaxed), std::max(0.25, beatsPerBar));
         // A transport jump that leaves the line behind, or more than one
         // grid ahead, re-measures it.
         const bool stale = mArmed && (mArmBeat < beats0 - 1.0e-9 || mArmBeat > beats0 + grid + 1.0e-9);
         if (mPending || stale)
         {
            mArmBeat = grid > 0.0 ? std::max(beats0, std::ceil(beats0 / grid - 1.0e-9) * grid) : beats0;
            mArmed = true;
            mPending = false;
         }
      }
      Publish();
   }

   bool IsArmed() const { return mArmed; }
   double ArmBeat() const { return mArmBeat; }
   bool Due(double beats1) const { return mArmed && mArmBeat < beats1; }
   void Fire()
   {
      mArmed = false;
      Publish();
   }

private:
   void Publish() { mArmedReadout.store(mArmed || mPending, std::memory_order_relaxed); }

   std::atomic<int> mRequest { 0 };
   std::atomic<int> mQuant { kBar };
   std::atomic<bool> mArmedReadout { false };

   int mSeen = 0;
   bool mPending = false;
   bool mArmed = false;
   double mArmBeat = 0.0;
};
