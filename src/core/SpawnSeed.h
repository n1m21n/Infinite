#pragma once

// Spawn counters: nodes without a saved seed take the next number of their kind, so two of them
// dropped on the canvas never move in lockstep. Headless resets them before each job, so a seedless
// patch renders the same whether it is the first job of a batch or the fiftieth.
namespace SpawnSeed
{
   enum Kind
   {
      LFO,
      Random,
      Drift,
      Moves,
      PredictiveModulator,
      ReactionDiffusion,
      kCount
   };

   inline int& Counter(Kind k)
   {
      static int sCounters[kCount] = {};
      return sCounters[k];
   }

   inline int Next(Kind k) { return ++Counter(k); }

   inline void ResetAll()
   {
      for (int i = 0; i < kCount; ++i)
         Counter((Kind)i) = 0;
   }
}
