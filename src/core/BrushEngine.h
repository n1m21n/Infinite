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
   enum Tip { kRound = 0, kSoftRound, kHardRound, kSquare, kSpray, kChalk, kInkBleed,
               // Creative and texture tips, all procedural: no bitmaps to ship.
               kBristle, kGrain, kHalftone, kWatercolor, kMetal, kCloth, kLeaf, kGrass, kSparkle, kSponge, kTipCount };
   enum Mode { kNormal = 0, kErase, kBehind, kClearMode, kMultiply, kScreen,
               // Retouch: the stroke reworks the pixels already there instead of laying colour.
               kBlur, kSharpen, kDodge, kBurn, kClone, kHeal,
               // Drags the colour under the brush along the stroke (paints straight onto the surface).
               kSmudge,
               // Relief (height in alpha): Raise is Normal; Stack adds the stroke's height on top; Press dents.
               kStack, kPress,
               // Marbling: the paint floats. A drop pushes it apart, a comb drags tines through it, a swirl turns it.
               kDrop, kComb, kSwirl,
               // Liquify: the same warp on any layer, no bath. Push drags, Twirl turns, Bloat swells, Pinch draws in.
               kPush, kTwirl, kBloat, kPinch,
               // More retouch (Affinity's brush tools): saturate / desaturate, median, paint back an earlier state,
               // replace a sampled colour, erase a sampled colour, red eye, and inpaint (filled on the CPU at the end).
               kSpongeMode, kMedian, kUndo, kColourReplace, kBgErase, kRedEye, kInpaint };
   inline bool RetouchMode(int m) { return (m >= kBlur && m <= kHeal) || m >= kSpongeMode; }
   inline bool MarbleMode(int m) { return m == kDrop || m == kComb || m == kSwirl; }
   inline bool LiquifyMode(int m) { return m == kPush || m == kTwirl || m == kBloat || m == kPinch; }
   inline bool WarpMode(int m) { return MarbleMode(m) || LiquifyMode(m); }
   inline bool DirectMode(int m) { return m == kSmudge || WarpMode(m); }

   const std::vector<std::string>& TipNames();
   const std::vector<std::string>& MaterialNames();
   // Copies `tex` (w x h) into `out`, so a stroke keeps the picture a node had when it started.
   bool Snapshot(unsigned tex, int w, int h, GLUtil::Fbo& out);

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
      int stabiliser = 0;          // 0 off, else the brush follows a weighted average of this many pen samples
      bool pencil = false;         // hard pixels, no anti-aliasing
      bool pressureSize = true;
      bool pressureOpacity = false;
      int mode = kNormal;
      bool lockAlpha = false;
      bool follow = false;         // the tip turns with the stroke's direction
      float taperIn = 0.0f;        // px of stroke over which the tip grows from a point
      float taperOut = 0.0f;       // px at the end over which it shrinks back (the last dabs land when the pen lifts)
      float sizeJitter = 0.0f;     // 0..1, each dab's size drops by up to this share
      float opacityJitter = 0.0f;  // 0..1, each dab's flow drops by up to this share
      float hueJitter = 0.0f;      // 0..1 of a half turn, per dab
      float satJitter = 0.0f;      // 0..1, per dab
      float valJitter = 0.0f;      // 0..1, per dab
      int range = 1;               // dodge / burn: 0 shadows, 1 midtones, 2 highlights
      bool sampleAll = false;      // clone / heal: copy from the whole picture (the surface owner fills sourceTex)
      unsigned sourceTex = 0;      // clone / heal: read from this texture (surface-sized) instead of the surface
      int relief = -1;             // relief preset: the material the layer takes (0 plasticine .. 3 ink stipple)
      int tines = 5;               // comb: tines across the brush's width
      // Material: the stroke carries a height across its width, lit while painting and baked into the colour.
      int material = -1;           // -1 flat colour; 0 plasticine, 1 metal, 2 chrome, 3 stipple, 4 satin, 5 pearl
      int profile = 1;             // height across the stroke: 0 flat, 1 round, 2 ridge, 3 ribbon
      float depth = 1.0f;          // how steep the height reads, 0..3
      float lightAz = 135.0f;      // degrees, the light's turn
      float lightEl = 45.0f;       // degrees above the page
      // Canvas texture: a grain fixed to the surface, not to each dab.
      int grain = 0;               // 0 off, 1 paper, 2 weave, 3 noise
      float grainScale = 1.0f;
      float grainDepth = 0.6f;
      int grainMode = 0;           // 0 multiply, 1 subtract, 2 height
      // Dual brush: a second tip masks the first.
      int dualTip = -1;
      float dualScale = 0.5f;      // of the main tip
      float dualScatter = 0.5f;    // of the main tip's size
      // Colour along the stroke: from `color` to `colorB`.
      int colorAlong = 0;          // 0 off, 1 along (repeats every colorPeriod px), 2 across
      float colorB[3] = { 1.0f, 1.0f, 1.0f };
      float colorPeriod = 200.0f;
      // Wet mix: each dab picks up a share of the paint under it and refills from `color` as it goes.
      float wet = 0.0f;            // 0..1 pick-up
      float refill = 0.15f;        // 0..1 back toward the brush colour per dab
      // Node-fed: a canvas node's picture as the tip, the canvas texture or the colour (0 = built in).
      unsigned tipTex = 0;         // luma x alpha is the mask, in the tip's frame (angle, roundness, follow)
      unsigned grainTex = 0;       // tiled at its own pixel size x grainScale, fixed to the canvas
      int grainTexW = 0, grainTexH = 0;
      unsigned colorTex = 0;       // stretched over the surface: each pixel takes the colour under it
      float color[3] = { 0.0f, 0.0f, 0.0f };
      float sourceOffset[2] = { 0.0f, 0.0f };   // clone / heal: where to copy from, surface pixels from the dab
      // Clone / heal source transform (Affinity's Rotation, Scale, Flip), about the stroke's first point; the
      // surface owner fills sourceAnchor (surface pixels) from the document point.
      float sourceRotate = 0.0f;   // degrees
      float sourceScale = 1.0f;
      bool sourceFlipX = false, sourceFlipY = false;
      float sourceAnchor[2] = { 0.0f, 0.0f };
      int sourceBlend = 0;         // clone: 0 normal, 1 multiply, 2 screen, 3 overlay, 4 darken, 5 lighten, 6 difference
      bool protectHue = false;     // dodge / burn / sponge: the hue stays put
      int sharpMode = 1;           // sharpen: 0 clarity, 1 unsharp mask, 2 harsh
      int spongeMode = 0;          // sponge: 0 saturate, 1 desaturate, 2 vibrance (dull colours gain most)
      float key[3] = { 0.0f, 0.0f, 0.0f };   // colour replace / background erase: the colour sampled where the stroke began
      float keyTolerance = 0.25f;  // 0..1 of the RGB distance
      bool wetEdges = false;       // the stroke's edge holds more paint than its middle
      float accumulation = 1.0f;   // 0..1, how much overlapping dabs build up inside one stroke
      float accumJitter = 0.0f;    // 0..1, per dab
      float hardJitter = 0.0f;     // 0..1, per dab
      float angleJitter = 0.0f;    // 0..1 of a half turn, per dab
      float roundJitter = 0.0f;    // 0..1 (shape jitter), per dab
      float scatterY = -1.0f;      // across-stroke scatter, of the diameter; -1 = the same as jitter
   };

   struct Preset
   {
      const char* name;
      Brush brush;
      const char* group = nullptr;   // "Basic", "Creative", "Texture", "Eraser": the preset menu's headers
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
      // Where the pen will be `aheadMs` after sample b, carried on from sample a ({x, y, ms} each), at most
      // maxPx past b. False when the two samples give no direction or speed.
      static bool Predict(const float a[3], const float b[3], float aheadMs, float maxPx, float out[2]);
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
      // The last stroke's coverage (alpha = how much of the stroke landed), kept until the next Begin.
      const GLUtil::Fbo& Coverage() const { return mCoverage; }

      bool Active() const { return mActive; }
      int DabCount() const { return mDabCount; }
      const Brush& CurrentBrush() const { return mBrush; }
      // A modulated brush mid-stroke: the next dabs take the new size, opacity and flow (sound as a paint input).
      // Live node sources: the next dabs read these textures (0 = built in).
      void SetSources(unsigned tip, unsigned grain, int grainW, int grainH, unsigned color)
      {
         mBrush.tipTex = tip;
         mBrush.grainTex = grain;
         mBrush.grainTexW = grainW;
         mBrush.grainTexH = grainH;
         mBrush.colorTex = color;
      }
      void Follow(float size, float opacity, float flow)
      {
         mBrush.size = size;
         mBrush.opacity = opacity;
         mBrush.flow = flow;
      }

   private:
      struct Pending
      {
         float x, y, pressure, seed, dir;
         float dist;           // stroke length when the dab was placed (taper out)
         float size, flow;     // jitter factors
         float hard = 1, rot = 0, round = 1;   // hardness, rotation (radians) and roundness jitter
         float rgb[3];         // the dab's colour (colour jitter)
         float fromX, fromY;   // smudge: where the previous dab sat
      };
      void DrawDab(const Pending& q, float taper);
      void SmudgeDab(const Pending& q, float r, float strength);
      void WarpDab(const Pending& q, float r, float strength);
      void Walk(float x, float y, float pressure);
      void Merge(const GLUtil::Fbo& surface, const GLUtil::Fbo& out);
      bool EnsurePrograms();

      Brush mBrush;
      bool mActive = false;
      float mLastX = 0, mLastY = 0, mLastP = 1;   // where the last dab went
      float mPenX = 0, mPenY = 0, mPenP = 1;      // the real pen (smoothing)
      std::vector<std::array<float, 3>> mStab;    // the stabiliser's last pen samples (x, y, pressure), oldest first
      void StabWalk(float x, float y, float p);
      float mCarry = 0;                           // distance walked since the last dab
      float mDabX = 0, mDabY = 0, mDir = 0;       // the last dab's place and the stroke's heading (radians)
      float mDist = 0;                            // stroke length walked so far, px
      float mPrevX = 0, mPrevY = 0;               // the last placed dab (smudge pick-up)
      float mSize = 1, mFlow = 1, mRgb[3] = { 0, 0, 0 };   // the dab being placed: jitter and taper
      float mHard = 1, mRot = 0, mRound = 1;
      bool mEnding = false;                       // the pen lifted: taper-out dabs land now
      uint32_t mRng = 1;
      int mDabCount = 0;
      int mW = 0, mH = 0;
      unsigned mSelection = 0;
      float mDirty[4] = { 0, 0, 0, 0 };
      bool mAny = false;
      std::vector<Pending> mQueue;
      std::vector<std::array<float, 6>> mMirrors;
      void Queue(float x, float y, float p, float seed, float dir, float fx, float fy);
      const GLUtil::Fbo* mSurface = nullptr;      // smudge paints into it directly
      GLUtil::Fbo mCoverage, mDisplay, mMerge;
      GLUtil::Fbo mWetCarry[2];                      // wet mix: the colour the brush carries (1x1, ping-pong)
      int mCarryIdx = 0;
      void UpdateCarry(const Pending& q, float r);
      bool mDisplayStale = true;
      bool mFinal = false;                        // the merge that lands (End), not the live display
   };
}
