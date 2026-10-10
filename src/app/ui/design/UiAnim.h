// The only animation timing code in the UI (docs/plans/iconography/README.md 4b).
// Chrome eases; values never do. Under 200 ms, reduce-motion = instant, idle costs one map lookup.
#pragma once
#include "imgui.h"

namespace UiAnim
{
   // Eases a per-ID value toward `target` over `durationMs` (linear in time, ease-out shaped).
   // First sight of an ID snaps to `target` (no animation on appear). Returns the current value.
   float Value(ImGuiID id, float target, float durationMs);

   // Same, picking the duration by direction: rising uses upMs, falling uses downMs.
   float Hover(ImGuiID id, bool on, float upMs, float downMs);

   // Call once per frame (after NewFrame): drops IDs not touched last frame.
   void EndFrame();

   // True if any value moved this frame (a future idle throttle must keep drawing while this is true).
   bool Moving();

   void SetReduceMotion(bool reduce);
   bool ReduceMotion();
}
