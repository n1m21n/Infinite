#include "NoteSchedule.h"

#include <algorithm>
#include <cmath>

namespace Arrange
{
namespace
{
   // Clip swing: the off 16th of each 8th moves late, 1 = a full triplet
   // feel (2:1). A piecewise-linear warp of the 8th, so notes between grid
   // lines keep their order.
   Tick Swung(Tick t, float swing)
   {
      if (swing <= 0.0f) return t;
      const Tick pair = kPPQ / 2, mid = kPPQ / 4;
      const double shifted = (double)mid + (double)swing * (double)kPPQ / 12.0;
      const Tick base = (t / pair) * pair, p = t - base;
      const double w = p < mid ? (double)p * shifted / (double)mid
                               : shifted + (double)(p - mid) * ((double)pair - shifted) / (double)(pair - mid);
      return base + (Tick)std::llround(w);
   }

   uint32_t Key(uint64_t clipId, uint32_t noteId, uint32_t pass)
   {
      uint64_t h = clipId * 0x9E3779B97F4A7C15ull ^ ((uint64_t)noteId << 20) ^ pass;
      h ^= h >> 31; h *= 0xBF58476D1CE4E5B9ull; h ^= h >> 29;
      return (uint32_t)h;
   }
}

void BuildNoteSchedule(const Lane& lane, NoteSchedule& out)
{
   out.notes.clear();
   out.exprs.clear();
   if (lane.type != kLaneMidi || !lane.enabled || lane.mute)
      return;
   for (const Clip& c : lane.clips)
   {
      if (!c.enabled || c.notes.empty() || c.length <= 0)
         continue;
      const bool loop = c.midiLoop && c.loopLength > 0;
      const Tick loopEnd = c.loopStart + c.loopLength;
      // Walk the clip box in segments of contiguous content: up to the loop
      // end, then one loop pass per segment. A note plays when its start is
      // inside the segment and is cut where the segment ends (the loop wrap
      // or the clip's right edge).
      Tick dt = 0;
      uint32_t pass = 0;
      while (dt < c.length && out.notes.size() < kMaxScheduledNotes)
      {
         const Tick q = MidiContentTick(c, dt);
         const Tick segLen = loop ? std::min(c.length - dt, std::max<Tick>(1, loopEnd - q)) : c.length - dt;
         const Tick segStart = c.start + dt, segEnd = segStart + segLen;
         auto it = std::lower_bound(c.notes.begin(), c.notes.end(), q,
                                    [](const Note& n, Tick t) { return n.start < t; });
         for (; it != c.notes.end() && it->start < q + segLen && out.notes.size() < kMaxScheduledNotes; ++it)
         {
            const Note& n = *it;
            if (n.mute)
               continue;
            const Tick on = Swung(segStart + (n.start - q), c.swing);
            const Tick off = std::min(segEnd, on + n.length);
            if (off <= on)
               continue;
            SchedNote s;
            s.on = (double)on / (double)kPPQ;
            s.off = (double)off / (double)kPPQ;
            s.pitch = (uint8_t)std::clamp((int)n.pitch + c.transpose, 0, 127);
            s.vel = (uint8_t)std::clamp((int)std::lround((double)n.vel * (double)c.velScale), 1, 127);
            s.relVel = n.relVel;
            s.chance = std::clamp(n.chance * c.chanceScale, 0.0f, 1.0f);
            s.key = Key(c.id, n.id, pass);
            const std::vector<ExprPoint>* curves[3] = { &n.bend, &n.pressure, &n.timbre };
            for (int k = 0; k < 3; k++)
            {
               s.expr[k] = (uint32_t)out.exprs.size();
               s.exprN[k] = (uint16_t)std::min<size_t>(curves[k]->size(), 0xFFFF);
               for (size_t j = 0; j < s.exprN[k]; j++)
                  out.exprs.push_back({ (double)(*curves[k])[j].at / (double)kPPQ, (*curves[k])[j].v });
            }
            out.notes.push_back(s);
         }
         dt += segLen;
         pass++;
      }
   }
   // Clips never overlap on a lane, but swing can reorder across a clip
   // edge by a few ticks: keep the audio thread's binary search honest.
   std::stable_sort(out.notes.begin(), out.notes.end(),
                    [](const SchedNote& a, const SchedNote& b) { return a.on < b.on; });
}
}
