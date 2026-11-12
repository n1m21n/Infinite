// The four Field Patcher starters (internal/plans/field-patcher/README.md §8.11). Plain data, so the headless
// field-patcher-check compiles each one in the domain of the node it becomes.
#pragma once

namespace Field
{
   struct PatcherStarter
   {
      const char* name;
      const char* domain;   // "notes" (Field Notes) or "element" (Field Primitive)
      bool valueMode;       // Field Primitive with its geometry hidden, read through publish
      const char* code;
   };

   inline constexpr PatcherStarter kPatcherStarters[] = {
      { "Skipping beat", "element", true,
        "step = floor(t * 4)\n"
        "hit = fract(t * 4) < 0.25\n"
        "skip = step % 3 < 0.5\n"
        "publish = hit != skip\n" },
      { "Melody from counting", "notes", false,
        "state float step = 0\n"
        "if (tick(1 / 8) > 0.5) {\n"
        "   note(deg(step % 8), 0.8, 1 / 8)\n"
        "   step = step + 1\n"
        "}\n" },
      { "Spiral", "element", false,
        "a = i * 2.39996\n"
        "r = sqrt(i / count)\n"
        "P = vec3(cos(a) * r, sin(a) * r, 0)\n" },
      { "Breathing light", "element", true,
        "publish = 0 + (sin(t * 2) - -1) / (1 - -1) * (1 - 0)\n" },
   };

   inline constexpr int kPatcherStarterCount = sizeof(kPatcherStarters) / sizeof(kPatcherStarters[0]);
}
