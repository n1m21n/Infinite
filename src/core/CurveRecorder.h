// Curve Editor Record page (docs/plans/curve-editor/README.md §8.9): turns a stream of (position, value) samples
// into curve points. Pure, so MSEGTEST drives it with a synthetic CC take (M3) and the panel feeds it live.
//
//   Overwrite  writes every sample of one pass
//   Touch      writes while the value moves; lets go `touchHold` seconds after it stops
//   Latch      writes from the first move until the take stops
//
// Positions are 0..1 along the shape (Fixed length) or seconds since the take began (Auto length, scaled at
// Commit). A position that goes back (the pass wrapped) closes the run, so a later pass over the same x wins.
#pragma once

#include "core/CurveEdit.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace CurveRecorder
{
   enum Mode
   {
      kOverwrite = 0,
      kTouch,
      kLatch
   };

   struct Take
   {
      int mode = kOverwrite;
      float moveEps = 0.5f / 127.0f; // half a CC step: below it the hand is still
      double touchHold = 0.35;       // Touch: seconds of stillness before it lets go
      std::vector<std::vector<CurveEdit::XY>> runs;
      bool writing = false;
      bool open = false; // the last run still takes samples
      bool primed = false;
      float lastV = 0.0f, lastX = 0.0f;
      double lastMoveT = -1.0e9;

      void Begin(int m)
      {
         *this = Take();
         mode = m;
      }

      void Sample(double t, float x, float v)
      {
         const bool moved = primed && std::fabs(v - lastV) > moveEps;
         if (primed && x < lastX)
            open = false; // wrapped: a new run
         if (moved)
         {
            lastMoveT = t;
            lastV = v;
         }
         if (!primed)
         {
            lastV = v;
            primed = true;
         }
         switch (mode)
         {
         case kOverwrite: writing = true; break;
         case kLatch: writing = writing || moved; break;
         default: writing = (t - lastMoveT) <= touchHold; break;
         }
         if (writing)
         {
            if (!open)
            {
               runs.emplace_back();
               open = true;
            }
            runs.back().push_back({ x, v });
         }
         else
            open = false;
         lastX = x;
      }

      int Samples() const
      {
         int c = 0;
         for (const auto& r : runs)
            c += (int)r.size();
         return c;
      }

      // Write the runs into `pts`: each is scaled (Auto length), kept inside [a, b] (Overdub), cleaned, thinned
      // to `tolerance` (RDP) and, with grid > 0, snapped to it. Returns how many runs landed.
      int Commit(CurveEdit::Points& pts, float tolerance, int maxPoints, float scale = 1.0f, float a = 0.0f, float b = 1.0f,
                 float grid = 0.0f) const
      {
         int landed = 0;
         for (const auto& r : runs)
         {
            std::vector<CurveEdit::XY> run;
            run.reserve(r.size());
            for (CurveEdit::XY q : r)
            {
               q.x *= scale;
               if (q.x > 1.0f && q.x < 1.02f)
                  q.x = 1.0f; // the last frame of a pass lands a hair past its end
               if (q.x >= a && q.x <= b)
                  run.push_back(q);
            }
            run = CurveEdit::CleanStroke(run);
            if (run.size() < 2)
               continue;
            run = CurveEdit::Simplify(run, tolerance);
            if (grid > 0.0f)
            {
               for (CurveEdit::XY& q : run)
                  q.x = std::clamp(std::round(q.x / grid) * grid, a, b);
               run = CurveEdit::CleanStroke(run);
            }
            CurveEdit::ReplaceRange(pts, run, ModShapes::kInterpLinear, maxPoints);
            landed++;
         }
         return landed;
      }
   };
}
