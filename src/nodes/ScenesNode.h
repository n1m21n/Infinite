#pragma once

// Turbo 0.50: Scenes, a radio-button scene launcher for live sets. A grid of
// up to 8 scenes (rows) x 8 outputs (columns), plus an "off" row (the base
// state); each output is a modulator cable to any param (a mixer mute, a Note
// Switcher slot, a Drum Sequencer part, a Chord Progression restart...).
//
// Radio behaviour: entering a scene sets every on/off, choice and level
// output to that scene's row, so whatever was on in the previous scene and is
// off in the new one switches off by itself. Pressing the playing scene again
// (option "press again = off") goes to the off row. Pulse outputs fire a short
// trigger on entry when their cell is on. Changes can wait for the next beat
// or bar (quantize), off included.
//
// Main-thread only: Tick() runs once per frame (node body and main loop,
// frame-guarded) and the outputs are read by the modulation apply loop.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "INode.h"
#include "Modulation.h"
#include "Transport.h"

class ScenesNode : public INode, public IModulator
{
public:
   static constexpr int kMax = 8;
   static constexpr int kQuantCount = 5; // immediate, next beat, next bar, 2 bars, 4 bars
   static constexpr int kPulseFrames = 3; // long enough for a trigger to see a rising edge
   static constexpr int kOff = -1;        // "no scene": outputs follow the off row
   static constexpr int kNone = -2;       // no scene change waiting
   static constexpr int kOffRow = kMax;   // cell row that holds the off state
   static constexpr int kChoiceFallback = 4; // choice on auto with nothing discrete cabled
   // Saved values: 0 and 1 keep the first draft's meaning (value, pulse) so
   // its patches map over; 0 with steps 0/1 ("smooth") loads as level.
   enum Mode { kChoice = 0, kPulse = 1, kOnOff = 2, kLevel = 3 };

   static INode* Create() { return new ScenesNode(); }

   ScenesNode()
   {
      for (int i = 0; i < kMax; i++)
      {
         mOut[i].owner = this;
         mOut[i].index = i;
         mode[i] = kOnOff;
         steps[i] = -1;
         mSceneParam[i] = -1;
      }
      for (int r = 0; r <= kMax; r++)
         for (int o = 0; o < kMax; o++)
            cell[r][o] = 0.0f;
   }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}

   int OutputCount() const override { return Outputs(); }
   const char* OutputLabel(int index) const override
   {
      static const char* kDefault[kMax] = { "1", "2", "3", "4", "5", "6", "7", "8" };
      if (index < 0 || index >= kMax)
         return "out";
      return label[index].empty() ? kDefault[index] : label[index].c_str();
   }
   float Value01() override { return OutputValue(0); }
   IModulator* ModulatorOutput(int index) override
   {
      return index >= 0 && index < kMax ? &mOut[index] : nullptr;
   }

   static const char* QuantName(int q)
   {
      static const char* k[kQuantCount] = { "immediate", "next beat", "next bar", "2 bars", "4 bars" };
      return k[std::clamp(q, 0, kQuantCount - 1)];
   }
   static double QuantBeats(int q, double beatsPerBar)
   {
      switch (std::clamp(q, 0, kQuantCount - 1))
      {
         case 1: return 1.0;
         case 2: return beatsPerBar;
         case 3: return 2.0 * beatsPerBar;
         case 4: return 4.0 * beatsPerBar;
         default: return 0.0;
      }
   }
   static const char* ModeName(int m)
   {
      switch (m)
      {
         case kChoice: return "choice";
         case kPulse: return "pulse";
         case kLevel: return "level";
         default: return "on/off";
      }
   }
   static int SanitizeMode(int m) { return m >= kChoice && m <= kLevel ? m : kOnOff; }

   int Scenes() const { return std::clamp(scenes, 1, kMax); }
   int Outputs() const { return std::clamp(outputs, 1, kMax); }
   // The playing scene, or kOff.
   int Current() const { return current < 0 ? kOff : std::min(current, Scenes() - 1); }
   bool IsOff() const { return Current() == kOff; }
   // The waiting scene (kOff = waiting to go off), or kNone.
   int Pending() const { return mPending; }
   bool HasPending() const { return mPending != kNone; }
   double PendingBeat() const { return mPendingBeat; }
   int EnterCount() const { return mEnterCount; }
   bool PulseHigh(int o) const { return o >= 0 && o < kMax && mPulse[o] >= 1 && mPulse[o] <= kPulseFrames; }
   static int Row(int scene) { return scene < 0 ? kOffRow : std::min(scene, kMax - 1); }

   // Choices of a choice output: fixed N, or (steps -1, auto) the cabled
   // param's options, resolved by the UI every frame.
   int ChoiceCount(int o) const
   {
      if (o < 0 || o >= kMax)
         return kChoiceFallback;
      if (steps[o] >= 2)
         return steps[o];
      return mResolvedSteps[o] >= 2 ? mResolvedSteps[o] : kChoiceFallback;
   }
   bool ChoiceAutoResolved(int o) const { return o >= 0 && o < kMax && steps[o] < 0 && mResolvedSteps[o] >= 2; }
   void SetResolvedSteps(int o, int n)
   {
      if (o >= 0 && o < kMax)
         mResolvedSteps[o] = n >= 2 ? std::min(n, 128) : 0;
   }
   static float Snap(float v, int n)
   {
      v = std::clamp(v, 0.0f, 1.0f);
      return n >= 2 ? std::round(v * (float)(n - 1)) / (float)(n - 1) : v;
   }

   // A cell as the user thinks of it (scene kOff = the off row): 0/1 for
   // on/off and pulse, a 0-based choice index, or the 0..1 level.
   float CellUser(int scene, int o) const
   {
      if (scene < kOff || scene >= kMax || o < 0 || o >= kMax)
         return 0.0f;
      const float v = std::clamp(cell[Row(scene)][o], 0.0f, 1.0f);
      switch (mode[o])
      {
         case kChoice: return std::round(v * (float)(ChoiceCount(o) - 1));
         case kLevel: return v;
         default: return v >= 0.5f ? 1.0f : 0.0f;
      }
   }
   void SetCellUser(int scene, int o, float v)
   {
      if (scene < kOff || scene >= kMax || o < 0 || o >= kMax || !std::isfinite(v))
         return;
      float& c = cell[Row(scene)][o];
      switch (mode[o])
      {
         case kChoice:
         {
            const int n = ChoiceCount(o);
            c = std::clamp(std::round(v), 0.0f, (float)(n - 1)) / (float)(n - 1);
            break;
         }
         case kLevel: c = std::clamp(v, 0.0f, 1.0f); break;
         default: c = v >= 0.5f ? 1.0f : 0.0f; break;
      }
   }
   bool CellOn(int scene, int o) const { return CellUser(scene, o) >= 0.5f; }

   float OutputValue(int o) const
   {
      if (o < 0 || o >= kMax)
         return 0.0f;
      const float v = std::clamp(cell[Row(Current())][o], 0.0f, 1.0f);
      switch (mode[o])
      {
         case kPulse: return PulseHigh(o) ? 1.0f : 0.0f;
         case kChoice: return Snap(v, ChoiceCount(o));
         case kLevel: return v;
         default: return v >= 0.5f ? 1.0f : 0.0f;
      }
   }

   // Asks for a scene (kOff = the off row). retrigger: asking for the scene
   // already playing enters it again (pulses fire again); without it that
   // request just cancels a pending change. now: skip the quantize grid.
   void Request(int scene, bool retrigger, bool now = false)
   {
      scene = std::clamp(scene, kOff, Scenes() - 1);
      Transport& t = Transport::Instance();
      const double q = QuantBeats(quantize, t.BeatsPerBar());
      if (now || q <= 0.0 || !t.IsPlaying())
      {
         mPending = kNone;
         if (scene != Current() || retrigger)
            Enter(scene);
         return;
      }
      if (scene == Current() && !retrigger)
      {
         mPending = kNone;
         return;
      }
      if (scene == mPending)
         return;
      const double beat = t.Beats();
      // Pressed just after a line (a human is a little late): go now.
      const double intoGrid = beat - std::floor(beat / q) * q;
      if (intoGrid < std::min(0.08, q * 0.1))
      {
         mPending = kNone;
         Enter(scene);
         return;
      }
      mPending = scene;
      mRequestBeat = beat;
      mPendingBeat = std::ceil(beat / q - 1e-7) * q;
   }

   // A scene button press (mouse, MIDI, CV, Performance Mode). Radio
   // buttons: another scene goes there; the playing scene goes off (or, with
   // pressAgainOff off, restarts). While a change waits, pressing the waiting
   // scene or the playing one calls the change back.
   void Press(int scene)
   {
      scene = std::clamp(scene, 0, Scenes() - 1);
      if (HasPending() && (scene == mPending || scene == Current()))
      {
         mPending = kNone;
         return;
      }
      if (scene == Current())
      {
         if (pressAgainOff)
            Request(kOff, false);
         else
            Request(scene, true);
         return;
      }
      Request(scene, true);
   }
   // "all off": the off row (quantized like any change).
   void AllOff() { Request(kOff, false); }
   // prev / next walk the scenes (not the off row); from off, next is the
   // first scene and prev the last.
   void Step(int delta)
   {
      const int n = Scenes();
      const int base = HasPending() ? mPending : Current();
      int to;
      if (base < 0)
         to = delta >= 0 ? 0 : n - 1;
      else
         to = ((base + delta) % n + n) % n;
      Request(to, true);
   }
   void CancelPending() { mPending = kNone; }

   // Once per frame (guarded by frameId): pulses count down, a pending scene
   // lands on its grid line (or at once if the transport stopped or jumped back).
   void Tick(int frameId)
   {
      if (frameId == mLastTickFrame)
         return;
      mLastTickFrame = frameId;
      for (int o = 0; o < kMax; o++)
         if (mPulse[o] > 0)
            mPulse[o]--;
      if (!HasPending())
         return;
      Transport& t = Transport::Instance();
      const double beat = t.Beats();
      if (!t.IsPlaying() || quantize <= 0 || beat >= mPendingBeat - 1e-6 || beat < mRequestBeat - 1e-3)
      {
         const int s = mPending;
         mPending = kNone;
         Enter(s);
      }
   }

   // Lit state for the Performance panel: the UI records which hashed
   // discrete param index each scene button (and "all off") got, so a
   // Performance trigger bound to it can light while that scene plays.
   void SetSceneParam(int scene, int paramIndex)
   {
      if (scene >= 0 && scene < kMax)
         mSceneParam[scene] = paramIndex;
      else if (scene == kOff)
         mAllOffParam = paramIndex;
   }
   bool ParamLit(int paramIndex, bool& lit) const
   {
      if (paramIndex < 0)
         return false;
      for (int s = 0; s < Scenes(); s++)
         if (mSceneParam[s] == paramIndex)
         {
            lit = Current() == s;
            return true;
         }
      if (mAllOffParam == paramIndex)
      {
         lit = IsOff();
         return true;
      }
      return false;
   }

   // ---- saved settings ----
   int scenes = 4;
   int outputs = 4;
   int quantize = 0;
   int current = kOff; // a new node starts on the off row
   std::string label[kMax];
   int mode[kMax];
   int steps[kMax]; // choice only: -1 auto, 2..128 fixed
   std::string sceneName[kMax];
   float cell[kMax + 1][kMax]; // [scene, or kOffRow][output], 0..1 (choice: index / (N - 1))
   bool pressAgainOff = true;

   // Not released before 0.50: later additions still go at the END.
   void VisitParams(ParamVisitor& v) override
   {
      v.Int("scenes", scenes);
      v.Int("outputs", outputs);
      v.Int("quantize", quantize);
      v.Int("current", current);
      char key[32];
      for (int o = 0; o < kMax; o++)
      {
         snprintf(key, sizeof(key), "o%d_label", o); v.Text(key, label[o]);
         snprintf(key, sizeof(key), "o%d_mode", o); v.Int(key, mode[o]);
         snprintf(key, sizeof(key), "o%d_steps", o); v.Int(key, steps[o]);
      }
      for (int s = 0; s < kMax; s++)
      {
         snprintf(key, sizeof(key), "s%d_name", s); v.Text(key, sceneName[s]);
         for (int o = 0; o < kMax; o++)
         {
            snprintf(key, sizeof(key), "c%d_%d", s, o);
            v.Float(key, cell[s][o]);
         }
      }
      v.Bool("press_off", pressAgainOff);
      for (int o = 0; o < kMax; o++)
      {
         snprintf(key, sizeof(key), "off_%d", o);
         v.Float(key, cell[kOffRow][o]);
      }
      scenes = std::clamp(scenes, 1, kMax);
      outputs = std::clamp(outputs, 1, kMax);
      quantize = std::clamp(quantize, 0, kQuantCount - 1);
      current = std::clamp(current, kOff, kMax - 1);
      for (int o = 0; o < kMax; o++)
      {
         const int m = SanitizeMode(mode[o]);
         // First draft: value + smooth (steps 0/1) is a level now.
         mode[o] = (m == kChoice && steps[o] >= 0 && steps[o] < 2) ? kLevel : m;
         steps[o] = steps[o] >= 2 ? std::min(steps[o], 128) : -1;
      }
      for (int r = 0; r <= kMax; r++)
         for (int o = 0; o < kMax; o++)
            cell[r][o] = std::isfinite(cell[r][o]) ? std::clamp(cell[r][o], 0.0f, 1.0f) : 0.0f;
   }

private:
   void Enter(int scene)
   {
      current = std::clamp(scene, kOff, kMax - 1);
      mEnterCount++;
      const int row = Row(current);
      for (int o = 0; o < Outputs(); o++)
         if (mode[o] == kPulse && cell[row][o] >= 0.5f)
            // Already high: one low frame first so the target sees a new edge.
            mPulse[o] = PulseHigh(o) ? kPulseFrames + 1 : kPulseFrames;
   }

   struct Out : public IModulator
   {
      ScenesNode* owner = nullptr;
      int index = 0;
      float Value01() override { return owner ? owner->OutputValue(index) : 0.0f; }
   };

   Out mOut[kMax];
   int mResolvedSteps[kMax] = {};
   int mPulse[kMax] = {};
   int mSceneParam[kMax];
   int mAllOffParam = -1;
   int mPending = kNone;
   double mPendingBeat = 0.0;
   double mRequestBeat = 0.0;
   int mLastTickFrame = -1;
   int mEnterCount = 0;
};
