#pragma once
// One look for the plugin view's own screens (licence sheet, Starting, Engine stopped) on every OS:
// Infinite's Midnight brand (docs/brand/BRAND.md), so the first thing a buyer sees in their DAW is
// Infinite, not plain system controls.
//
//   Midnight ground
//     ┌ card (surface-1, radius 24) ───────────┐
//     │ Infinite                    title, ink  │
//     │ Enter your key              message,ink2│
//     │ ┌ key well (deep, radius 10, mono) ───┐ │
//     │ └─────────────────────────────────────┘ │
//     │ [Paste key]  [Buy]   Ember / surface-2  │
//     │ □ Weekly refund check            ink-3  │
//     └─────────────────────────────────────────┘
#include <cstdint>
#include <string>

namespace InfinitePlugin::Sheet
{
   // 0xRRGGBB
   constexpr uint32_t kGround = 0x151930;  // Midnight
   constexpr uint32_t kDeep = 0x0D1020;    // key well
   constexpr uint32_t kCard = 0x20263F;    // surface-1
   constexpr uint32_t kRaised = 0x2A3052;  // surface-2: secondary button, card edge
   constexpr uint32_t kLine = 0x343B5E;    // surface-3: well edge
   constexpr uint32_t kInk = 0xEEF1FA;     // Moon
   constexpr uint32_t kInk2 = 0xA3AACB;
   constexpr uint32_t kInk3 = 0x8890B1;
   constexpr uint32_t kEmber = 0xFF6B35;   // primary button; its label is Midnight, never white
   constexpr uint32_t kEmberDown = 0xD9430E;

   constexpr int kCardW = 400;     // narrower when the view is
   constexpr int kCardR = 24;
   constexpr int kPad = 28;        // card padding
   constexpr int kWellH = 36;
   constexpr int kWellR = 10;
   constexpr int kButtonH = 32;
   constexpr int kButtonR = 10;
   constexpr int kButtonPadX = 16;
   constexpr int kGap = 12;
   constexpr int kTitleSize = 20, kTextSize = 13, kMonoSize = 12, kSmallSize = 12;

   constexpr const char* kTitle = "Infinite";

   inline float R(uint32_t c) { return float((c >> 16) & 0xFF) / 255.0f; }
   inline float G(uint32_t c) { return float((c >> 8) & 0xFF) / 255.0f; }
   inline float B(uint32_t c) { return float(c & 0xFF) / 255.0f; }

   // The engine app's fonts folder (Inter, Geist Mono), or "" when no engine is found: the views
   // fall back to system fonts. Defined in Shell.cpp, beside FindEngine.
   std::string EngineFontsDir();
}
