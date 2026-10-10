#include "app/ui/design/UiAnim.h"
#include <cmath>
#include <unordered_map>

namespace UiAnim
{
   namespace
   {
      struct Slot { float value; float from; float to; float t; float dur; int touched; };
      std::unordered_map<ImGuiID, Slot> sSlots;
      bool sMoving = false;
      bool sReduce = false;
      int sFrame = 0;
      float EaseOut(float x) { const float u = 1.0f - x; return 1.0f - u * u * u; }
   }

   float Value(ImGuiID id, float target, float durationMs)
   {
      auto it = sSlots.find(id);
      if (it == sSlots.end())
      {
         sSlots[id] = Slot{ target, target, target, 1.0f, durationMs, sFrame };
         return target;
      }
      Slot& s = it->second;
      s.touched = sFrame;
      if (sReduce || durationMs <= 0.0f)
      {
         s.value = s.from = s.to = target; s.t = 1.0f;
         return target;
      }
      if (target != s.to)
      {
         s.from = s.value; s.to = target; s.t = 0.0f; s.dur = durationMs;
      }
      if (s.t < 1.0f)
      {
         s.t = std::fmin(1.0f, s.t + ImGui::GetIO().DeltaTime * 1000.0f / s.dur);
         s.value = s.from + (s.to - s.from) * EaseOut(s.t);
         sMoving = true;
      }
      return s.value;
   }

   float Hover(ImGuiID id, bool on, float upMs, float downMs)
   {
      auto it = sSlots.find(id);
      const float cur = it == sSlots.end() ? (on ? 1.0f : 0.0f) : it->second.to;
      const bool rising = on && cur < 1.0f;
      return Value(id, on ? 1.0f : 0.0f, rising ? upMs : downMs);
   }

   void EndFrame()
   {
      for (auto it = sSlots.begin(); it != sSlots.end();)
         it = (it->second.touched < sFrame) ? sSlots.erase(it) : std::next(it);
      sMoving = false;
      sFrame++;
   }

   bool Moving() { return sMoving; }
   void SetReduceMotion(bool reduce) { sReduce = reduce; }
   bool ReduceMotion() { return sReduce; }
}
