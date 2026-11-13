#pragma once

#include <cstdint>
#include <string>
#include <array>
#include <vector>

#include "GLUtil.h"

// The one brush every paint surface uses (Draw node, 2D View paint layers and
// masks). A stroke builds a coverage buffer from dabs; the coverage is merged
// onto the surface at display time and again, for good, when the stroke ends,
// so flow builds up inside a stroke but never past its opacity. Coordinates are
// the surface's pixels, origin bottom left (texture space). Surfaces hold
// straight (not premultiplied) RGBA.
namespace BrushEngine
{
   enum Tip { kRound = 0, kSoftRound, kHardRound, kSquare, kSpray, kChalk, kInkBleed, kTipCount };
   enum Mode { kNormal = 0, kErase, kBehind, kClearMode, kMultiply, kScreen };

   const std::vector<std::string>& TipNames();

   struct Brush
   {
      int tip = kRound;
      float size = 20.0f;          // diameter, px
      float hardness = 0.8f;       // 0 soft .. 1 hard
      float opacity = 1.0f;        // the stroke's ceiling
      float flow = 1.0f;           // each dab's share
      float spacing = 0.15f;       // of the diameter
      float jitter = 0.0f;         // position scatter, of the diameter
      float angle = 0.0f;          // degrees
      float roundness = 1.0f;      // 1 round .. 0.05 flat
      float smoothing = 0.0f;      // 0..1, lazy-mouse string
      bool pencil = false;         // hard pixels, no anti-aliasing
      bool pressureSize = true;
      bool pressureOpacity = false;
      int mode = kNormal;
      bool lockAlpha = false;
      float color[3] = { 0.0f, 0.0f, 0.0f };
   };

   struct Preset
   {
      const char* name;
      Brush brush;
   };
   const std::vector<Preset>& Presets();

   // One stroke on one surface at a time. Owns the coverage, display and merge
   // buffers, sized to the surface on Begin.
   class Stroke
   {
   public:
      ~Stroke();

      // `selection` (optional) is an R/alpha texture at the surface's size
      // limiting where paint lands.
      void Begin(const GLUtil::Fbo& surface, const Brush& b, float x, float y, float pressure, unsigned selection = 0);
      void MoveTo(float x, float y, float pressure);
      // A dab exactly here, no spacing walk (recorded replays).
      void Dab(float x, float y, float pressure, float seed);
      // Symmetry: every dab is also placed through each of these surface-pixel
      // affines (x' = m0 x + m1 y + m2, y' = m3 x + m4 y + m5). Kept until set again.
      void SetMirrors(const std::vector<std::array<float, 6>>& m) { mMirrors = m; }
      // Renders queued dabs into the coverage buffer.
      void Flush();
      // The surface with the stroke on it, for showing while the stroke lasts.
      unsigned DisplayTexture(const GLUtil::Fbo& surface);
      // Merges the stroke into `surface` for good (its handle stays). Returns
      // the touched rectangle (x0, y0, x1, y1, surface px), empty when nothing landed.
      void End(GLUtil::Fbo& surface, int dirty[4]);
      void Cancel();

      bool Active() const { return mActive; }
      int DabCount() const { return mDabCount; }
      const Brush& CurrentBrush() const { return mBrush; }

   private:
      struct Pending
      {
         float x, y, pressure, seed;
      };
      void Walk(float x, float y, float pressure);
      void Merge(const GLUtil::Fbo& surface, const GLUtil::Fbo& out);
      bool EnsurePrograms();

      Brush mBrush;
      bool mActive = false;
      float mLastX = 0, mLastY = 0, mLastP = 1;   // where the last dab went
      float mPenX = 0, mPenY = 0, mPenP = 1;      // the real pen (smoothing)
      float mCarry = 0;                           // distance walked since the last dab
      uint32_t mRng = 1;
      int mDabCount = 0;
      int mW = 0, mH = 0;
      unsigned mSelection = 0;
      float mDirty[4] = { 0, 0, 0, 0 };
      bool mAny = false;
      std::vector<Pending> mQueue;
      std::vector<std::array<float, 6>> mMirrors;
      void Queue(float x, float y, float p, float seed);
      GLUtil::Fbo mCoverage, mDisplay, mMerge;
      bool mDisplayStale = true;
   };
}
