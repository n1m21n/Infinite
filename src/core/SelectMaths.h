#pragma once

// Selection maths for the 2D View (internal/plans/2d-view/ai-tools.md, Block 1 steps 1.5-1.7): Select and Mask's
// edge refine and edge sliders, Decontaminate Colours, Color Range and Depth Select. Pure CPU functions on plain
// buffers so VIEW2DTEST can prove each one on a made-up picture; they run on the UI thread only at preview size
// (the long side capped by the caller) or once on OK.
//
// Buffers: rgba is w x h x 4 (0..255), a mask is w x h (0..255). Rows may run either way, as long as the picture
// and the mask agree.

#include <cstdint>
#include <vector>

namespace SelectMaths
{
   // Mean over a (2r + 1)^2 window, edges clamped. Running sums: cost does not grow with r.
   void BoxMean(const std::vector<float>& in, std::vector<float>& out, int w, int h, int r);

   // ---- Select and Mask ----------------------------------------------------------------------------------------
   struct RefineParams
   {
      float radius = 0.0f;     // px: the band round the edge the picture redraws (0 = off)
      bool smart = false;      // Smart Radius: a thin band on clean edges, the full one where the edge is busy
      float smooth = 0.0f;     // 0..100: rounds jagged edges
      float feather = 0.0f;    // px: Gaussian sigma
      float contrast = 0.0f;   // 0..100 %: sharpens a soft edge
      float shift = 0.0f;      // -100..100 %: moves a soft edge out (+) or in (-)
   };
   // Edge detection: a colour guided filter (He, Sun and Tang 2013) of the mask, steered by the picture, inside the
   // band within `radius` px of the edge; outside the band the mask is kept. Long sides above 1024 px run the fast
   // guided filter (coefficients at a lower size, applied at full size), so hair keeps full-size detail.
   void GuidedEdge(const uint8_t* rgba, std::vector<uint8_t>& mask, int w, int h, float radius, bool smart);
   // The four edge sliders in Photoshop's order: Smooth, Feather, Contrast, Shift Edge.
   void AdjustEdge(std::vector<uint8_t>& mask, int w, int h, float smooth, float feather, float contrast, float shift);
   // Both, in order.
   void Refine(const uint8_t* rgba, std::vector<uint8_t>& mask, int w, int h, const RefineParams& p);
   // Decontaminate Colours: in the soft edge (0 < mask < 250) the colour is pulled toward the mean of the solidly
   // selected pixels nearby: in full at half selected or less, less toward solid. amount 0..1. Pixels with mask 0
   // or >= 250 are untouched.
   void Decontaminate(uint8_t* rgba, const std::vector<uint8_t>& mask, int w, int h, float amount);

   // ---- Color Range --------------------------------------------------------------------------------------------
   enum class Range
   {
      Sampled, Reds, Yellows, Greens, Cyans, Blues, Magentas, Highlights, Midtones, Shadows, Skin
   };
   struct Sample
   {
      float rgb[3] = { 0, 0, 0 };   // 0..1, display values
      float x = 0, y = 0;           // where it was picked (Localized), same frame as the picture
      bool subtract = false;        // a minus eyedropper click
   };
   struct RangeParams
   {
      Range range = Range::Sampled;
      float fuzziness = 40.0f;   // 0..200: how far (CIE76 Delta E) from a sample a colour still counts, falling to 0
      bool localized = false;
      float localRange = 100.0f; // %: Localized: share of the picture's long side a sample reaches
      float tonalFuzz = 20.0f;   // %: Highlights / Midtones / Shadows: the fall-off past the range
      float lo = -1.0f, hi = -1.0f;   // 0..255 tonal range; < 0 = Photoshop's default for the range
   };
   // The share of each pixel in the range (0..255). Samples apply in click order: an add takes the larger, a
   // subtract cuts what came before it, so a later add can win back what an earlier subtract removed.
   void ColorRange(const uint8_t* rgba, int w, int h, const std::vector<Sample>& samples, const RangeParams& p,
                   std::vector<uint8_t>& out);
   // One colour's share in a non-sampled range (0..1): Reds..Magentas by hue (weighted by chroma, so dull and dark
   // colours count less, as in Photoshop), Highlights..Shadows by lightness, Skin by a YCbCr skin locus.
   float RangeWeight(Range r, const float rgb[3], const RangeParams& p);
   // CIE L*a*b* (D65) of a display colour 0..1.
   void Lab(const float rgb[3], float lab[3]);

   // ---- Quick Select ---------------------------------------------------------------------------------------------
   // One dab of an edge-seeking Quick Select: the brush disc, grown into the pixels round it that look like the
   // disc (CIE76 Delta E to the disc's mean within a tolerance set by the disc's own spread) and stopped where the
   // colour jumps between neighbours, out to `reach` x the radius. rgba null = the disc alone (nothing to read).
   // out: box[2] x box[3] (0 or 255), box = x, y, w, h in the picture.
   void QuickRegion(const uint8_t* rgba, int w, int h, float cx, float cy, float r, float reach, std::vector<uint8_t>& out,
                    int box[4]);

   // ---- Remove tool's hole ---------------------------------------------------------------------------------------
   // A stroke that closes on itself takes in what it encloses: holes no background can reach from the picture's
   // border (4-neighbour flood, so a loop open at one pixel encloses nothing). mask 0..255, > 0 = in the stroke.
   void CloseRings(std::vector<uint8_t>& mask, int w, int h);
   // The hole's radius: its thickest part, the largest distance (px, Euclidean) from a hole pixel to the outside.
   float HoleRadius(const std::vector<uint8_t>& mask, int w, int h);
   // Grows the hole by r px (a disc). Sizes that do not match return the input.
   void Dilate(std::vector<uint8_t>& mask, int w, int h, float r);

   // A membrane fill: the hole's pixels solve Laplace's equation with the known pixels round it as the boundary, so
   // a smooth gradient comes back exact and a thin scratch takes the colours on each side of it. img is w x h x ch
   // floats; hole[i] != 0 marks the unknowns. A coarse-to-fine cascade (each level half the size) starts each level
   // from the one below, so wide holes cost a few sweeps per level, not thousands.
   void HarmonicFill(std::vector<float>& img, int ch, const std::vector<uint8_t>& hole, int w, int h);

   // ---- Grow / Similar ------------------------------------------------------------------------------------------
   // Select > Grow (contiguous) and Select > Similar (everywhere): pixels whose colour is within the Magic Wand's
   // tolerance of ANY selected pixel's colour (RGBA distance <= tol * sqrt 3, two clear pixels always alike), so two
   // selected colours never take in the colours between them. Grow floods 4-neighbour from the selected pixels.
   // sel and out are w x h (selected = >= 128); out is the old selection with the matches added (255).
   void GrowMatch(const uint8_t* rgba, int w, int h, const std::vector<uint8_t>& sel, int tol, bool contiguous,
                  std::vector<uint8_t>& out);

   // ---- Focus Area -----------------------------------------------------------------------------------------------
   // How sharp each pixel's neighbourhood is, 0 (soft) .. 1 (the sharpest detail in the picture): the local mean of
   // the luma Laplacian's size, less `noise` (0..1, of the picture's own detail) so grain does not read as focus.
   void FocusMap(const uint8_t* rgba, int w, int h, float noise, std::vector<float>& out);
   // The in-focus part of a FocusMap: range 0..1 (Photoshop's In-Focus Range: more takes in softer parts), with a
   // short soft edge.
   void FocusSelect(const std::vector<float>& map, int w, int h, float range, std::vector<uint8_t>& out);

   // ---- Depth --------------------------------------------------------------------------------------------------
   // A selection from a depth map (0 far .. 1 near): full between lo and hi, falling to 0 over `soft` beyond them.
   void DepthSelect(const std::vector<float>& depth, int w, int h, float lo, float hi, float soft, std::vector<uint8_t>& out);
}
