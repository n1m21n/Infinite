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
      { "Sphere shell", "element", false,
        "z = 1 - (i + 0.5) / count * 2\n"
        "r = sqrt(1 - z * z)\n"
        "a = i * 2.39996\n"
        "P = vec3(cos(a) * r, sin(a) * r, z)\n" },
      { "Wave grid", "element", false,
        "n = floor(sqrt(count))\n"
        "x = (i % n) / n * 2 - 1\n"
        "y = floor(i / n) / n * 2 - 1\n"
        "P = vec3(x, y, sin(x * 4 + t * 2) * cos(y * 4) * 0.2)\n" },
      { "Ring pulse", "element", false,
        "a = i / count * 6.28318\n"
        "r = 0.6 + sin(t * 3) * 0.1\n"
        "P = vec3(cos(a) * r, sin(a) * r, 0)\n" },
      { "Helix", "element", false,
        "a = i / count * 18.8496\n"
        "P = vec3(cos(a) * 0.5, sin(a) * 0.5, i / count * 2 - 1)\n" },
      { "Lissajous", "element", false,
        "a = i / count * 6.28318\n"
        "P = vec3(sin(a * 3 + t), sin(a * 2), cos(a * 5) * 0.4)\n" },
      { "Slow LFO", "element", true,
        "publish = 0.5 + 0.5 * sin(t * 0.5)\n" },
      { "Saw ramp", "element", true,
        "publish = fract(t * 0.5)\n" },
      { "Triangle LFO", "element", true,
        "publish = abs(fract(t * 0.5) * 2 - 1)\n" },
      { "Pulse train", "element", true,
        "publish = fract(t * 2) < 0.25\n" },
      { "Arpeggio", "notes", false,
        "state float step = 0\n"
        "if (tick(1 / 8) > 0.5) {\n"
        "   note(deg((step % 4) * 2), 0.7, 1 / 8)\n"
        "   step = step + 1\n"
        "}\n" },
      { "Bass pulse", "notes", false,
        "if (tick(1 / 4) > 0.5) {\n"
        "   note(deg(0) - 24, 0.9, 1 / 8)\n"
        "}\n" },
      { "Rising scale", "notes", false,
        "state float step = 0\n"
        "if (tick(1 / 4) > 0.5) {\n"
        "   note(deg(step % 7), 0.8, 1 / 4)\n"
        "   step = step + 1\n"
        "}\n" },
   };

   inline constexpr int kPatcherStarterCount = sizeof(kPatcherStarters) / sizeof(kPatcherStarters[0]);
}
