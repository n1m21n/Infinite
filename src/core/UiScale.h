#pragma once

#include <cmath>

// How the whole UI follows the monitor's DPI and the user's "UI Scale" slider.
//
// Every platform lays ImGui out in *points*, the way Retina macOS always has. One ImGui unit
// is `pointScale` window units, and the renderer multiplies back up through
// io.DisplayFramebufferScale. Node widths, literal ImVec2 sizes in node bodies, custom draw
// lists, the minimap and pins all grow together, so nothing has to be scaled by hand.
//
// glfwGetWindowContentScale() means two different things, told apart by the framebuffer:
//   - Retina macOS and Wayland: the window is already sized in points and the framebuffer
//     is a larger pixel buffer. The OS scales the picture; only the manual slider is ours.
//   - Windows and X11: window and framebuffer are both physical pixels and xscale is the
//     real DPI factor. We divide the window by xscale ourselves.
// The font is baked at (about) its final physical pixel size, so it is sharp, and
// FontGlobalScale brings it back to exactly 15 units.
namespace UiScale
{
   constexpr float kBaseFontSize = 15.0f;

   struct Result
   {
      float pointScale;      // window units per ImGui unit (backend divides DisplaySize/mouse)
      float bakeScale;       // physical pixels per ImGui unit
      float fontGlobalScale; // io.FontGlobalScale = kBaseFontSize / BakedFontPx(bakeScale)
   };

   // ImGui 1.90 truncates font sizes when it builds the atlas, so 150% (22.5 px) would bake
   // at 22 and every text-sized node would come out short. Bake at the next whole pixel up
   // instead; FontGlobalScale absorbs the difference. Whole-pixel sizes are unchanged.
   inline float BakedFontPx(float bakeScale)
   {
      return std::ceil(kBaseFontSize * bakeScale - 1.0e-3f);
   }

   inline Result Resolve(float xscale, int windowW, int framebufferW, float manualScale)
   {
      if (!(xscale >= 0.25f))
         xscale = 1.0f;
      if (!(manualScale >= 0.25f))
         manualScale = 1.0f;
      const bool platformScalesPicture = windowW > 0 && framebufferW > windowW;
      Result r;
      r.pointScale = (platformScalesPicture ? 1.0f : xscale) * manualScale;
      r.bakeScale = xscale * manualScale;
      r.fontGlobalScale = kBaseFontSize / BakedFontPx(r.bakeScale);
      return r;
   }

   // The resolved value for the main window, updated on startup and on every rescale.
   inline Result& Current()
   {
      static Result sCurrent { 1.0f, 1.0f, 1.0f };
      return sCurrent;
   }

   // Set by the content-scale callback (window moved to another monitor, OS scale changed)
   // and by the UI Scale slider; main.cpp consumes it between frames, never mid-frame.
   inline bool& RescaleRequested()
   {
      static bool sRequested = false;
      return sRequested;
   }
   inline void RequestRescale() { RescaleRequested() = true; }

   // Physical pixels per ImGui unit for the main window. Use this for anything that has to
   // talk to the framebuffer (glReadPixels crops, offscreen captures) - never for layout,
   // which is already in points.
   inline float Factor() { return Current().bakeScale; }

   inline bool Near(float a, float b) { return std::fabs(a - b) < 1.0e-4f; }

   // The decision table, as data. Returns the number of failing cases (0 = OK) and fills
   // `firstFailure` with the first one's name. Run by INFINITE_UISCALETEST.
   inline int SelfCheck(const char** firstFailure)
   {
      struct Case
      {
         const char* name;
         float xscale; int winW; int fbW; float manual;
         float wantPoint; float wantBake;
      };
      static const Case kCases[] = {
         { "macOS 1x",            1.0f, 1600, 1600, 1.0f, 1.0f,  1.0f  },
         { "macOS Retina 2x",     2.0f, 1600, 3200, 1.0f, 1.0f,  2.0f  },
         { "Windows 100%",        1.0f, 1600, 1600, 1.0f, 1.0f,  1.0f  },
         { "Windows 150%",        1.5f, 2400, 2400, 1.0f, 1.5f,  1.5f  },
         { "Windows 200%",        2.0f, 3200, 3200, 1.0f, 2.0f,  2.0f  },
         { "X11 hi-DPI 2x",       2.0f, 3200, 3200, 1.0f, 2.0f,  2.0f  },
         { "Wayland 2x",          2.0f, 1600, 3200, 1.0f, 1.0f,  2.0f  },
         { "Retina + slider 1.5", 2.0f, 1600, 3200, 1.5f, 1.5f,  3.0f  },
         { "Win 150% + slider",   1.5f, 2400, 2400, 1.5f, 2.25f, 2.25f },
         { "minimized window",    1.5f, 0,    0,    1.0f, 1.5f,  1.5f  },
         { "bogus xscale",        0.0f, 1600, 1600, 1.0f, 1.0f,  1.0f  },
      };
      int failures = 0;
      for (const Case& c : kCases)
      {
         const Result r = Resolve(c.xscale, c.winW, c.fbW, c.manual);
         const bool ok = Near(r.pointScale, c.wantPoint) && Near(r.bakeScale, c.wantBake) &&
                         Near(r.fontGlobalScale * BakedFontPx(r.bakeScale), kBaseFontSize) &&
                         BakedFontPx(r.bakeScale) == std::floor(BakedFontPx(r.bakeScale));
         if (!ok)
         {
            if (failures == 0 && firstFailure != nullptr)
               *firstFailure = c.name;
            failures++;
         }
      }
      return failures;
   }
}
