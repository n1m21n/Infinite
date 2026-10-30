#pragma once
// A MIDI lane flattened for the audio thread (docs/plans/piano-roll/README.md §7):
// every note every clip box plays, on the timeline in beats, loops unrolled,
// sorted by on-beat. Built on the main thread, read lock-free by the Piano Roll
// node's audio half, never mutated once published.
#include "ArrangeModel.h"

#include <cstdint>
#include <vector>

namespace Arrange
{
   struct SchedExpr
   {
      double at = 0.0; // beats after the note's on
      float v = 0.0f;
   };

   struct SchedNote
   {
      double on = 0.0, off = 0.0; // timeline beats
      uint8_t pitch = 60;         // after clip transpose
      uint8_t vel = 100;          // after clip velScale, 1..127
      uint8_t relVel = 64;
      float chance = 1.0f;
      uint32_t key = 0;           // (clip, note, loop pass): seeds chance, finds held notes after an edit
      uint32_t expr[3] = { 0, 0, 0 };  // first index into SchedExprs per curve: bend, pressure, timbre
      uint16_t exprN[3] = { 0, 0, 0 };
   };

   struct NoteSchedule
   {
      uint64_t gen = 0; // set by the publisher
      std::vector<SchedNote> notes;
      std::vector<SchedExpr> exprs;
   };

   // Upper bound on unrolled notes: a 1-tick loop over a long clip must not
   // run the main thread out of memory.
   constexpr size_t kMaxScheduledNotes = 1u << 18;

   // The lane's enabled clips, as played. An empty schedule for a muted or
   // disabled lane, or one that is not MIDI.
   void BuildNoteSchedule(const Lane& lane, NoteSchedule& out);

   // Chance roll for one scheduled note: true = it plays. Deterministic in
   // (key, pass), so a render repeats; pass counts transport restarts.
   inline bool ChancePlays(uint32_t key, uint32_t pass, float chance)
   {
      if (chance >= 1.0f) return true;
      if (chance <= 0.0f) return false;
      uint32_t h = key * 0x9E3779B1u ^ (pass + 0x7F4A7C15u) * 0x85EBCA77u;
      h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
      return (float)(h >> 8) * (1.0f / 16777216.0f) < chance;
   }

   // A curve's value `at` beats after the note's on: linear between points,
   // flat before the first and after the last.
   inline float SampleExpr(const SchedExpr* pts, int n, double at)
   {
      if (n <= 0) return 0.0f;
      if (at <= pts[0].at) return pts[0].v;
      for (int i = 1; i < n; i++)
         if (at < pts[i].at)
         {
            const double span = pts[i].at - pts[i - 1].at;
            const double t = span > 0.0 ? (at - pts[i - 1].at) / span : 1.0;
            return (float)((double)pts[i - 1].v + ((double)pts[i].v - (double)pts[i - 1].v) * t);
         }
      return pts[n - 1].v;
   }
}
