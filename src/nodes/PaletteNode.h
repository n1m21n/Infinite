#pragma once

#include <string>
#include <vector>

#include "GLUtil.h"
#include "ImageCable.h"
#include "INode.h"
#include "Palette.h"

// --- Palette from Image -------------------------------------------------
// Drop in a reference photo and the patch's colours come from it.
//
// The point is to compose by mood rather than by RGB numbers: instead of
// hand-picking twenty swatches scattered across ramp stops, material albedo
// and curve endpoints until they happen to agree, you point at a frame you
// like and cable its palette into all of them at once. Swapping the reference
// re-grades the whole graph.
//
// Clustering runs in Oklab rather than RGB. RGB k-means groups by how numbers
// look to a computer - it will happily merge a saturated teal and a mid grey
// because their coordinates are close - where Oklab distance approximates how
// far apart two colours look, which is the only definition of "the palette of
// this photo" that matches what someone sees in it.
//
// The reference can be a still (loaded here) or any node's output cabled in,
// which makes a video's palette a live thing: with Live on, the graph re-grades
// itself as the footage changes.
class PaletteNode : public INode, public IPaletteSource
{
public:
   static constexpr int kMaxSwatches = 8;

   // Dark-to-light, dominant-first, or around the hue wheel. Which order you
   // want depends entirely on the destination: ramp stops read best sorted by
   // luminance, a set of material albedos by weight.
   enum SortMode { kSortLuminance = 0, kSortWeight, kSortHue, kSortModeCount };
   enum StripMode { kStripSteps = 0, kStripSmooth, kStripModeCount };

   static INode* Create() { return new PaletteNode(); }
   static const std::vector<std::string>& SortNames();
   static const std::vector<std::string>& StripNames();

   PaletteNode();
   ~PaletteNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mStrip); }
   int GetOutputWidth() const override { return mStrip.w; }
   int GetOutputHeight() const override { return mStrip.h; }
   void CookIfNeeded(int frameId) override;

   // --- IPaletteSource ---
   int SwatchCount() const override { return mActiveCount; }
   void GetSwatch(int index, float outRgb[3]) const override;

   ImageCable& Input() { return mInput; }
   const char* InputLabel(int) const override { return "ref"; }

   // A palette is not a picture, so it must not act as a pass-through when
   // bypassed - handing downstream nodes the reference photo itself would be a
   // surprising thing for the power button to do.
   INode* BypassSource() override { return nullptr; }

   // Loads a still reference. Returns false and fills LastError() on failure.
   bool Load(const std::string& path);
   bool LoadViaDialog();
   const std::string& LastError() const { return mLastError; }
   const std::string& LoadedPath() const { return mLoadedPath; }
   void ReloadFromPath()
   {
      if (!mLoadedPath.empty())
      {
         const std::string p = mLoadedPath;
         Load(p);
      }
   }

   // Re-run extraction on the next cook regardless of the Live setting.
   void RequestExtract() { mForceExtract = true; }
   bool HasPalette() const { return mHasPalette; }
   // Share of the reference each swatch covers, 0..1, in the same order as the
   // swatches. Shown as a weight bar so a one-percent accent is not mistaken
   // for a dominant colour.
   float SwatchWeight(int index) const;

   int swatchCount = 5; // 5 by default: exactly a Ramp's stop count
   int sortMode = kSortLuminance;
   int stripMode = kStripSteps;

   // Photographs are mostly near-neutral, so an unfiltered clustering returns
   // five greys and calls it a palette. Dropping low-chroma pixels first is
   // what surfaces the colours someone would actually name.
   float minChroma = 0.04f;
   bool includeNeutrals = false;

   // Post-shaping. These are the "make it a mood" controls: the extracted
   // palette is the starting point, not the verdict.
   float hueShift = 0.0f;   // turns, 0..1
   float saturation = 1.0f; // chroma multiplier
   float brightness = 0.0f; // lightness offset
   float spread = 1.0f;     // lightness contrast between swatches

   bool live = false;       // re-extract continuously (for video references)
   float sampleRate = 8.0f; // extractions per second when live
   int sampleSize = 96;     // reference is downsampled to this square first
   float seed = 1.0f;       // k-means++ seeding; same seed, same palette

   void VisitParams(ParamVisitor& v) override
   {
      v.Text("path", mLoadedPath);
      v.Int("swatchCount", swatchCount); v.Int("sortMode", sortMode);
      v.Int("stripMode", stripMode);
      v.Float("minChroma", minChroma); v.Bool("includeNeutrals", includeNeutrals);
      v.Float("hueShift", hueShift); v.Float("saturation", saturation);
      v.Float("brightness", brightness); v.Float("spread", spread);
      v.Bool("live", live); v.Float("sampleRate", sampleRate);
      v.Int("sampleSize", sampleSize); v.Float("seed", seed);
   }

private:
   // Which texture the palette is read from: a cabled node wins over the
   // loaded still, so patching a video in takes over without clearing the file.
   unsigned int SourceTexture(int frameId);
   bool EnsureDownsample();
   void Extract(unsigned int srcTex);
   void ApplyShaping();
   void RebuildStrip();

   ImageCable mInput;

   float mSwatch[kMaxSwatches][3] = { { 0 } }; // display-space, as the picker shows them
   float mWeight[kMaxSwatches] = { 0 };
   int mActiveCount = 0;
   bool mHasPalette = false;

   // The reference still, when one is loaded.
   unsigned int mOwnTex = 0;
   int mOwnW = 0, mOwnH = 0;
   std::string mLoadedPath;
   std::string mLastError;

   // GPU downsample target, read back on the CPU for clustering.
   unsigned int mSmallFbo = 0;
   unsigned int mSmallTex = 0;
   int mSmallSize = 0;
   unsigned int mDownsampleProgram = 0;
   bool mShaderTried = false;
   std::vector<unsigned char> mPixels;

   GLUtil::Fbo mStrip;

   // Extraction is expensive and its inputs rarely change, so it re-runs only
   // when something it depends on actually moved.
   bool mForceExtract = true;
   unsigned int mExtractedFrom = 0;
   int mExtractedCount = -1;
   float mExtractedChroma = -1.0f;
   bool mExtractedNeutrals = false;
   float mExtractedSeed = -1.0f;
   int mExtractedSize = -1;
   double mLastSampleSeconds = -1.0;

   // Shaping is applied after clustering, so changing it must not re-cluster -
   // the raw cluster centres are kept in Oklab and re-shaped on demand.
   float mRawLab[kMaxSwatches][3] = { { 0 } };
   float mRawWeight[kMaxSwatches] = { 0 };
   float mShapedHue = 1e9f, mShapedSat = 1e9f, mShapedBright = 1e9f, mShapedSpread = 1e9f;
   int mShapedSort = -1, mShapedStrip = -1;

   int mLastCookFrame = -1;
};
