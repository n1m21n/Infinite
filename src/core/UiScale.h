#pragma once

// How the UI font and widget metrics follow the monitor's DPI.
//
// glfwGetWindowContentScale() means two different things:
//   - Retina macOS and Wayland: the window is sized in points and the framebuffer is a
//     larger pixel buffer. The OS already enlarges the picture, so xscale only says how
//     sharp the baked glyphs should be; on-screen size stays baseSize.
//   - Windows and X11: window and framebuffer are both physical pixels, and xscale is the
//     real DPI factor. Text and widgets must actually grow.
// The two cases are told apart by the framebuffer, not by the OS: if the framebuffer is
// wider than the window, the platform is already scaling the picture.
namespace UiScale
{
   struct Result
   {
      float bakeScale;     // multiplier on the font's baked pixel size
      float displayScale;  // io.FontGlobalScale
      float styleScale;    // ImGuiStyle::ScaleAllSizes factor (1 = leave alone)
   };

   inline Result Resolve(float xscale, int windowW, int framebufferW, float manualScale)
   {
      if (xscale < 0.25f)
         xscale = 1.0f;
      const bool platformScalesPicture = windowW > 0 && framebufferW > windowW;
      Result r;
      r.bakeScale = xscale * manualScale;
      if (platformScalesPicture)
      {
         r.displayScale = 1.0f / xscale;
         r.styleScale = manualScale;
      }
      else
      {
         r.displayScale = 1.0f;
         r.styleScale = xscale * manualScale;
      }
      return r;
   }
}
