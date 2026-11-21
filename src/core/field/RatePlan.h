#pragma once

#include "AliasAnalysis.h"
#include "SampleProgram.h"
#include <string>
#include <vector>

// Per-segment oversampling plans for Field sample programs (phase 2 of
// docs/plans/aliasing-aware-field). Main thread only; nothing here runs on
// the audio thread yet: phase 2a executes plans offline in the alias rig.
//
// A plan cuts the program into contiguous segments, each run at its own
// factor of the base rate. A value that crosses a cut goes down to the base
// rate and up again (a min-phase half-band chain each way), so every segment
// sees a band-limited input: that is what stops the spectrum of a chain of
// nonlinear stages from compounding, and why a chain can need far less than
// the factor the whole kernel needs.
//
// Values that are constant over a block (imm, params, sr, and arithmetic on
// them) never cross a cut: each segment recomputes them, so sr-relative
// coefficients mean the right thing at the segment's own rate. `in` is read
// once at the top and crosses like any other value, so a later segment's dry
// signal goes through the same filters as its wet one and stays in phase.
// State cells may not straddle a cut (a loop through a resampler would pick
// up its latency).
namespace Field
{
   struct RateSegment
   {
      SampleProgram prog;        // prologue (recomputed constants) + the segment's instructions
      std::vector<int> inRegs;   // set before each run, from the previous segment
      std::vector<int> outRegs;  // read after each run, to the next segment
      std::vector<int> stateMap; // segment cell -> cell of the original program
      int first = 0, end = 0;    // instruction range in the prepared program
      bool readsIn = false;      // segment 0 only: reads `in`
      int factor = 1;
   };

   // Cost per base-rate sample, in arbitrary units (the rig calibrates it in
   // ns). Index of up/down: 0..5 for factor 1, 2, 4, 8, 16, 32.
   struct RateCostModel
   {
      double run = 1.0;   // per program run, fixed part
      double instr = 1.0; // per instruction
      double up[6] = { 0, 1, 2, 3, 4, 5 };
      double down[6] = { 0, 1, 2, 3, 4, 5 };
   };

   struct RatePlanOptions
   {
      double f0Hz = 1000.0;
      float inAmplitude = 0.5f;
      bool usesInput = true;
      double baseRate = 48000.0;
      double audibleHz = 20000.0;
      double harmonicTolHz = 12.0;
      double targetDb = -80.0;
      int gridLog2 = 12;
      int maxCandidates = 8;
      int maxCuts = 3;
      int maxCrossing = 2;
      RateCostModel cost;
   };

   struct RatePlan
   {
      std::vector<int> cuts;
      std::vector<RateSegment> segs;
      std::vector<double> segDb; // predicted alias of each segment at its factor
      double predictedDb = 0.0;  // power sum of the segments
      double cost = 0.0;
      bool meetsTarget = false;
   };

   struct RatePlanReport
   {
      std::string status;
      int candidates = 0;
      int cutSets = 0;
      int analyses = 0;
      long long evaluations = 0;
   };

   int FactorIndex(int factor); // 1,2,4,8,16,32 -> 0..5, else -1

   // `in` read once into a fresh register at the top; every LoadIn becomes a
   // Move from it. inReg = -1 when the program does not read `in`.
   bool PrepareForSplit(const SampleProgram& prog, SampleProgram& out, int& inReg, std::string& why);

   // Cut positions (split before instruction p) next to a nonlinear op, with
   // at most maxCrossing values crossing and no state loop straddling.
   std::vector<int> CandidateCuts(const SampleProgram& prepared, int maxCrossing, int maxCandidates);

   bool SplitProgram(const SampleProgram& prepared, const std::vector<int>& cuts, std::vector<RateSegment>& segs,
                     std::string& why);

   double PlanCost(const std::vector<RateSegment>& segs, const RateCostModel& m);
   double WholeCost(const SampleProgram& prog, int factor, const RateCostModel& m);

   // Cheapest plan (factors 1..32 per segment, up to maxCuts cuts) whose
   // predicted alias meets targetDb; if none does, the plan with the lowest
   // predicted alias (meetsTarget false).
   bool ChooseRatePlan(const SampleProgram& prog, const RatePlanOptions& opt, RatePlan& best, RatePlanReport& rep);
}
