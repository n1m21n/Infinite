// The Field Patcher starters (internal/plans/field-patcher/README.md §8.11). Plain data, so the headless
// field-patcher-check compiles each one in the domain of the node it becomes.
#pragma once

namespace Field
{
   struct PatcherStarter
   {
      const char* name;
      const char* domain;   // "element" (Field Primitive), "notes", "pixel" (Field Pixel), "sample" (Field Effect)
      bool valueMode;       // element only: a Field Modulator (no geometry, `publish` is the value)
      const char* code;
   };

   // Four of each kind, grouped in the order the pick grid shows them: Primitive, Modulator, Notes, Pixel, Effect.
   inline constexpr PatcherStarter kPatcherStarters[] = {
      { "Spiral", "element", false,
        "a = i * 2.39996\n"
        "r = sqrt(i / count)\n"
        "P = vec3(cos(a) * r, sin(a) * r, 0)\n" },
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
      { "Lissajous", "element", false,
        "a = i / count * 6.28318\n"
        "P = vec3(sin(a * 3 + t), sin(a * 2), cos(a * 5) * 0.4)\n" },

      { "Slow LFO", "element", true,
        "publish = 0.5 + 0.5 * sin(t * 0.5)\n" },
      { "Triangle LFO", "element", true,
        "publish = abs(fract(t * 0.5) * 2 - 1)\n" },
      { "Pulse train", "element", true,
        "publish = fract(t * 2) < 0.25\n" },
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

      { "Gradient", "pixel", false,
        "col = vec3(uv.x, uv.y, 0.5 + 0.5 * sin(t))\n" },
      { "Rings", "pixel", false,
        "d = length(uv - vec2(0.5, 0.5))\n"
        "w = 0.5 + 0.5 * sin(d * 40 - t * 3)\n"
        "col = vec3(w, w * 0.6, 1 - w)\n" },
      { "Stripes", "pixel", false,
        "s = step(0.5, fract(uv.x * 10 + t * 0.5))\n"
        "col = vec3(s, s * 0.5, 1 - s)\n" },
      { "Plasma", "pixel", false,
        "v = sin(uv.x * 10 + t) + sin(uv.y * 10 - t)\n"
        "col = vec3(0.5 + 0.25 * v, 0.3, 0.5 - 0.25 * v)\n" },

      { "Soft drive", "sample", false,
        "x = in * 2.5\n"
        "out = x / (1 + abs(x))\n" },
      { "Tremolo", "sample", false,
        "state float ph = 0\n"
        "ph = ph + 5 / sr\n"
        "ph = ph - floor(ph)\n"
        "out = in * (0.6 + 0.4 * sin(ph * 6.28318))\n" },
      { "Smoother", "sample", false,
        "state float y = 0\n"
        "y = y + (in - y) * 0.1\n"
        "out = y\n" },
      { "Bit crush", "sample", false,
        "out = floor(in * 8) / 8\n" },
   };

   inline constexpr int kPatcherStarterCount = sizeof(kPatcherStarters) / sizeof(kPatcherStarters[0]);
}
