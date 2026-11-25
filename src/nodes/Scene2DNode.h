#pragma once
#include <future>
#include <memory>

#include <algorithm>
#include <cmath>
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <iosfwd>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "INode.h"
#include "GLUtil.h"
#include "BrushEngine.h"
#include "core/SelectMaths.h"

// The 2D View's document: node outputs, fills and groups stacked into one
// picture at the document's size. A node layer only references its node (by
// uid) and places it, so the node keeps animating and is never written to; a
// deleted node leaves its layer as "source missing". No input pins: the
// layers are the inputs. Plan: internal/plans/2d-view/README.md.
class ImageCable;
class Scene2DNode : public INode
{
public:
   static INode* Create() { return new Scene2DNode(); }
   ~Scene2DNode() override;

   enum class Kind { Node = 0, Fill = 1, Group = 2, Paint = 3, Vector = 4, Text = 5, Adjust = 6 };
   static constexpr int kKindCount = 7;
   // Adjustment layers: the picture below, changed in place (masks and clip limit where).
   enum AdjType { kAdjPixelate = 0, kAdjHalftone, kAdjDither, kAdjThreshold, kAdjGradientMap, kAdjPosterize,
                  kAdjBrightness, kAdjContrast, kAdjHue, kAdjSaturation, kAdjExposure, kAdjInvert, kAdjLevels, kAdjCurves,
                  kAdjColorBalance, kAdjVibrance, kAdjSelective, kAdjMixer, kAdjBlackWhite, kAdjPhotoFilter,
                  kAdjShadowsHighlights, kAdjLookup, kAdjWhiteBalance, kAdjSplitToning, kAdjRecolour,
                  kAdjNormals, kAdjToneCompression, kAdjToneStretch, kAdjNoise, kAdjTexture, kAdjLensBlur, kAdjCount };
   // adjP layout per type (AdjParamCount): Colour Balance 9 (shadows, mids, highlights rgb, -1..1); Selective
   // Colour 36 (reds, yellows, greens, cyans, blues, magentas, whites, neutrals, blacks x CMYK, -1..1); Channel
   // Mixer 10 (rows R, G, B of R G B, then mono 0/1); Black & White 6 (reds..magentas, -2..3); Photo Filter 1
   // (keep luminosity 0/1); Shadows / Highlights 2 (shadows, highlights 0..1); White Balance 2 (temperature, tint -1..1);
   // Split Toning 5 (highlight hue deg, saturation, shadow hue deg, saturation, balance 0..1); Recolour 3 (hue deg,
   // saturation, lightness -1..1). The last three are Affinity's, measured: tools/re-survey/affinity_probe.py adjust.
   static void WhiteBalanceMatrix(float temperature, float tint, float m[9]);   // linear sRGB, row-major
   // From a picked pixel (display sRGB, with the adjustment already on it): the White Balance temperature / tint that
   // make it neutral, and Levels' or Curves' black (1), grey (2) or white (3) point set from it.
   static void WhiteBalanceFromPick(const float disp[3], float amt, float& temp, float& tint);
   static void PointFromPick(int adjType, float adjAmt, float levels[4], float levelsCh[3][4], std::vector<float>& adjP,
                             std::vector<float>& curve, std::vector<float> curveCh[3], const float disp[3], int point);
   static int AdjParamCount(int type);
   static std::vector<float> AdjParamDefault(int type);
   static const char* AdjGroup(int type);   // "Adjust", "Colour", "Stylize": the effect list's sections
   static const char* AdjName(int type);
   static float AdjDefault(int type);
   // A smooth curve through the points that never overshoots between them, sampled n times over 0..1.
   static void CurveLut(const std::vector<float>& pts, float* lut, int n);
   static void AdjRange(int type, float& lo, float& hi);
   static constexpr int kDrives = 4;   // Drive A..D: modulatable 0..1 knobs adjustment layers follow

   // A gradient's colour stops (fill layers, shape fills, the gradient overlay). Empty: the two colours the owner
   // already has, start to end. Otherwise 2..kMaxStops stops, sorted by t (0..1), colours in display values.
   struct GradStop
   {
      float t = 0.0f;
      float c[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
      bool operator==(const GradStop& o) const { return t == o.t && std::equal(c, c + 4, o.c); }
   };
   static constexpr int kMaxStops = 8;
   static constexpr int kPickSizeMax = 101;   // the largest eyedropper average (101 by 101)
   static constexpr int kPickBoxMax = 4096;   // a far zoomed out box is capped here
   static void SortStops(std::vector<GradStop>& st);
   static void GradAt(const std::vector<GradStop>& st, const float a[4], const float b[4], float t, float out[4]);
   static void WriteStops(std::ostream& o, const std::vector<GradStop>& st);   // " n t r g b a ..."
   static void ReadStops(std::istream& s, std::vector<GradStop>& st);
   // GLSL: uniforms uStopN, uStopT[8], uStopC[8] and vec4 gradAt(float t, vec4 a, vec4 b). StopUniforms sets them,
   // each colour through `conv` (display to working space) when given.
   static const char* kStopGlsl;
   static void StopUniforms(unsigned prog, const std::vector<GradStop>& st, float (*conv)(float, bool) = nullptr, bool lin = false);

   // One vector item: a pen path, a live shape or an imported SVG. Points are
   // the layer's own box pixels, y down.
   struct Shape
   {
      enum Type { kPath = 0, kRect, kEllipse, kPolygon, kStar, kLine, kSvg, kTriangle, kTypeCount };   // appended: saved as numbers
      enum Op { kAdd = 0, kSubtract, kIntersect, kXor };
      int type = kPath;
      float x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // live shapes and SVG: the box; a line: its ends
      float radius = 0.0f;                    // rectangle corners
      int sides = 5;                          // polygon and star points
      float inner = 0.5f;                     // star inner radius, fraction of the outer
      std::vector<float> pts;                 // path: per anchor x y inX inY outX outY
      bool closed = false;
      bool fillOn = true;
      float fillC[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
      // 0 solid (fillC), 1 linear, 2 radial (fits the box), 3 reflected, 4 conical (sweeps round the centre from
      // fillAngle), 5 bitmap (fillImage), 6 mesh (meshC at the box's corners, blended between them)
      int fillType = 0;
      float fillAngle = 90.0f;                // linear, reflected, conical: degrees, 90 = top to bottom
      std::vector<GradStop> fillStops;        // gradient fills: 2..kMaxStops
      std::string fillImage;                  // bitmap fill: an image file
      int imageMode = 0;                      // bitmap fill: 0 stretch to the box, 1 fill (crop), 2 fit, 3 tile
      float meshC[4][4] = { { 1, 0, 0, 1 }, { 1, 1, 0, 1 }, { 0, 0, 1, 1 }, { 0, 1, 0, 1 } };   // TL, TR, BR, BL
      float noise = 0.0f;                     // 0..1: grain mixed into the fill's alpha
      float cornerR[4] = { -1, -1, -1, -1 };  // rectangle: TL, TR, BR, BL radius; -1 = `radius`
      int cornerType[4] = { 0, 0, 0, 0 };     // rectangle: 0 round, 1 straight (chamfer), 2 concave, 3 cut-out
      float miter = 10.0f;                    // miter joins: longest spike, in widths
      float dashPhase = 0.0f;                 // px the dash pattern starts into
      bool balanced = false;                  // dashes stretched so a closed outline ends on a whole dash
      int arrowS = 0, arrowE = 0;             // 0 none, 1 arrow, 2 triangle, 3 circle, 4 square, 5 bar
      float arrowSize = 3.0f;                 // arrowheads: widths
      float arrowConcave = 0.0f;              // triangle head: back pulled in (+) or out (-), whole % of its length, -50..50
      static constexpr float kArrowConcaveMax = 50.0f;   // Photoshop's Concavity field range, +-
      float taperS = 0.0f, taperE = 0.0f;     // pressure: 0..1 of the length the stroke thins over at each end
      float pressMin = 0.0f;                  // pressure: the width the tapers reach, fraction of `width`
      bool behind = false;                    // stroke drawn under the fill
      bool scaleStroke = true;                // stroke width follows the layer's scale
      float CornerR(int i) const { return cornerR[i] >= 0.0f ? cornerR[i] : radius; }
      bool strokeOn = false;
      float strokeC[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
      float width = 4.0f;
      int cap = 0, join = 0;                  // butt/round/square, miter/round/bevel
      int align = 0;                          // centre, inside, outside
      float dash = 0.0f, gap = 0.0f;          // 0 dash = solid
      int op = kAdd;                          // with the items below it in the layer
      std::string svg;                        // kSvg: the document text
      int Anchors() const { return (int)pts.size() / 6; }
   };
   enum Fit { kFitNone = 0, kFitFit, kFitFill, kFitStretch };
   static const std::vector<std::string>& FitNames();
   static constexpr int kPassThrough = -1;   // a group's blend: its children blend straight into what is below
   enum Lock : unsigned { kLockAlpha = 1, kLockPixels = 2, kLockPosition = 4, kLockAll = 8 };

   // Layer fx (Photoshop order): drop shadow and outer glow under the pixels,
   // colour overlay and inner shadow on them, stroke on top.
   enum FxType { kFxShadow = 0, kFxInner, kFxGlow, kFxStroke, kFxOverlay, kFxInnerGlow, kFxBevel, kFxGradient, kFxSatin, kFxPattern,
                 kFxCount };
   static constexpr int kFxPatternCount = 8;   // checker, dots, lines, grid, noise, perlin, voronoi, weave
   struct Fx
   {
      bool on = false;
      float c[4] = { 0.0f, 0.0f, 0.0f, 0.75f };   // colour and its opacity (display values)
      float angle = 120.0f;   // shadows: light from this angle, degrees (Photoshop: 90 = from the top)
      float dist = 10.0f;     // shadows: offset, px
      float size = 10.0f;     // blur (shadows, glow) or width (stroke), px
      float spread = 0.0f;    // shadows and glow: 0..1, how hard the edge is
      int pos = 0;            // stroke: 0 outside, 1 inside, 2 centre
      float c2[4] = { 1.0f, 1.0f, 1.0f, 1.0f };   // bevel: shadow colour; gradient overlay: end colour; pattern: second colour
      int pattern = 0;        // pattern overlay: kFxPatternCount kinds; size is the cell, angle turns it
      int material = -1;      // bevel: -1 plain light, else the relief materials (0 plasticine, 1 metal, 2 chrome, 3 stipple)
      int blend = 0;          // per-style blend: 0 normal, 1 multiply, 2 screen, 3 overlay, 4 soft light, 5 add, 6 colour
      int style = 0;          // inner glow: 0 edge / 1 centre; bevel: 0 up / 1 down; gradient: 0 lin / 1 rad / 2 refl / 3 diam; satin: 0 norm / 1 inv
      float contour[4] = { 0.25f, 0.25f, 0.75f, 0.75f };   // shadow, glow, bevel, satin: the two inner points of a 0..1 curve
      std::vector<GradStop> stops;   // gradient overlay: more than two colours (empty: c to c2)
      bool ContourIsLinear() const { return contour[0] == contour[1] && contour[2] == contour[3]; }
   };
   static bool FxSame(const Fx& a, const Fx& b);
   static const char* FxPatternName(int p);
   static Fx FxDefault(int type);
   static const char* FxName(int type);

   // One entry in a layer's effect stack (Photoshop's smart filters): a native adjustment, or a node the stack owns
   // (kept off the canvas until "Show on canvas") that runs on the layer's picture. Runs before blend and opacity.
   struct Effect
   {
      bool on = true;
      uint64_t uid = 0;          // node effect: its node; 0 = native
      bool shown = false;        // node effect: its node is on the canvas
      bool animated = false;     // node effect that moves by itself (time, simulations): redraws every frame
      bool toMask = false;       // node effect whose picture masks the layer instead of replacing it
      std::string entry;         // node effect: the list entry it came from ("Gaussian Blur", "Remove background")
      float opacity = 1.0f;
      int blend = 0;             // 0 normal, 1 multiply, 2 screen, 3 overlay, 4 luminosity
      // Native: the same settings as an adjustment layer.
      int adjType = 0;
      float adjAmt = 0.0f;
      bool adjMap = false;
      float colorA[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
      float colorB[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
      float levels[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
      float levelsCh[3][4] = { { 0, 1, 0, 1 }, { 0, 1, 0, 1 }, { 0, 1, 0, 1 } };
      std::vector<float> curve = { 0.0f, 0.0f, 1.0f, 1.0f };
      std::vector<float> curveCh[3] = { { 0, 0, 1, 1 }, { 0, 0, 1, 1 }, { 0, 0, 1, 1 } };
      std::vector<float> adjP;
      std::vector<GradStop> stops;
      std::string adjFile;
   };
   static const char* const kEffBlendNames[5];

   struct Layer
   {
      int id = 0;
      Kind kind = Kind::Node;
      int parent = 0;               // group layer id, 0 = the document
      uint64_t uid = 0;             // node layers: the canvas node
      std::string name;             // empty: the node's own name
      bool visible = true;
      int blend = 0;                // BlendModes index; kPassThrough for a group that does not isolate
      float opacity = 1.0f;         // the whole layer
      float fill = 1.0f;            // pixels only (layer fx stay at full, Block D)
      unsigned lock = 0;            // Lock bits
      int label = 0;                // colour label, 0 = none, 1..7
      bool clip = false;            // clipped to the nearest unclipped layer below
      bool output = false;          // also a node output: this layer's own picture
      bool below = false;           // effect: a node layer whose first image input reads the picture under it
      bool open = true;             // a group's row is expanded in the Layers list
      // Placement, in document pixels, y down. (x, y) is where the anchor sits;
      // the anchor is a fraction of the layer's own box.
      float x = 0.0f, y = 0.0f;
      float sx = 1.0f, sy = 1.0f;
      float angle = 0.0f;           // degrees, clockwise on screen
      float ax = 0.5f, ay = 0.5f;
      bool flipX = false, flipY = false;
      float shear = 0.0f;           // degrees, x leans with y in the layer's own box (2D layers only)
      int fit = kFitFit;            // node layers: how the node's picture is sized before the transform
      bool nearest = false;         // node layers: sampling
      // Layer crop, in the layer's own box pixels, from each side.
      bool cropOn = false;
      float crop[4] = { 0, 0, 0, 0 };   // left, top, right, bottom
      // Node mask: another node's picture shows or hides this layer, live.
      uint64_t maskUid = 0;
      bool maskOn = true, maskInvert = false;
      int maskChannel = 0;          // 0 luminance, 1 alpha
      float maskDensity = 1.0f;
      // Fill layers.
      int fillType = 0;             // 0 solid, 1 linear, 2 radial, 3 reflected, 4 diamond, 5 conical, 6 bitmap
      float fillNoise = 0.0f;       // grain: each pixel keeps a random share of the fill's alpha, 0..1
      std::string fillImage;        // bitmap: picture file, stretched over the document
      float colorA[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
      float colorB[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
      float fillAngle = 90.0f;      // linear: degrees, 90 = top to bottom
      std::vector<GradStop> stops;  // gradients with more than two colours (empty: colorA to colorB)
      // Adjustment layers. adjDrive 1..4 moves the amount from adjAmt to adjTo with Drive A..D.
      // adjMap: after the effect, dark to colorA, light to colorB.
      int adjType = 0;
      float adjAmt = 16.0f, adjTo = 16.0f;
      int adjDrive = 0;
      bool adjMap = false;
      // Levels (amount = gamma): input black, input white, output black, output white, 0..1.
      float levels[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
      // Curves (amount = strength): x, y pairs in 0..1, sorted by x, ends included.
      std::vector<float> curve = { 0.0f, 0.0f, 1.0f, 1.0f };
      // Per channel R, G, B on top of the master: levels (in black, in white, out black, out white) and curves.
      float levelsCh[3][4] = { { 0, 1, 0, 1 }, { 0, 1, 0, 1 }, { 0, 1, 0, 1 } };
      std::vector<float> curveCh[3] = { { 0, 0, 1, 1 }, { 0, 0, 1, 1 }, { 0, 0, 1, 1 } };
      std::vector<float> adjP;          // the new adjustments' settings (AdjParamCount)
      std::string adjFile;              // Colour Lookup: the .cube file
      // Effect stack and its mask (a node's picture: white keeps the effects, black shows the layer as it was).
      std::vector<Effect> effects;
      uint64_t effMaskUid = 0;
      bool HasEffects() const
      {
         if (kind == Kind::Adjust)
            return false;
         for (const Effect& e : effects)
            if (e.on)
               return true;
         return false;
      }
      // Paint layers: the surface's size and its tiles (TileStore keys, row
      // major from the bottom row, kTile px square).
      int pw = 0, ph = 0;
      std::vector<std::string> tiles;
      // Relief (paint layers): alpha is height, lit by a light that turns. reliefMat: 0 plasticine, 1 metal,
      // 2 chrome, 3 ink stipple. reliefLight: degrees the light comes from (90 = top); reliefElev: its height,
      // degrees; reliefDepth: px of height at full alpha.
      bool relief = false;
      int reliefMat = 0;
      float reliefLight = 120.0f, reliefElev = 40.0f, reliefDepth = 20.0f;
      // Pixel mask (any layer but a group): grey, white shows. Sized to the layer's box.
      bool pmaskOn = true;
      int mw = 0, mh = 0;
      std::vector<std::string> mtiles;
      bool HasPixelMask() const { return mw > 0 && mh > 0; }
      // Filter mask (a layer with an effect stack): grey at document size, white keeps the effects, black shows the
      // layer as it was. Painted as target 3; applies on top of the node Mask-from when both are set.
      int fmw = 0, fmh = 0;
      std::vector<std::string> fmtiles;
      bool HasFilterMask() const { return fmw > 0 && fmh > 0; }
      // Node layers with a 3D object: a painted skin laid over its surface by UV (pw x ph, same tiles).
      // skinMirror: bit 0/1/2 mirror strokes across the object's X / Y / Z.
      bool HasSkin() const { return kind == Kind::Node && pw > 0 && ph > 0 && !tiles.empty(); }
      int skinMirror = 0;
      // Vector layers: items bottom to top; pw/ph hold the box size.
      std::vector<Shape> shapes;
      // Vector mask (any layer but a group): inside the items shows. No items shows all.
      bool hasVMask = false, vmaskOn = true, vmaskInvert = false;
      float vmaskFeather = 0.0f;              // Gaussian sigma, px (Photoshop's mask Feather)
      float vmaskDensity = 1.0f;              // 0..1: beyond the path the layer shows at 1 - density (Photoshop's Density)
      std::vector<Shape> vmask;
      // Text layers. boxW 0 = point text (no wrap); else wraps at boxW px.
      std::string text, font;
      float fontSize = 96.0f, leading = 1.2f, tracking = 0.0f;
      int align = 0;                // left, centre, right, justified
      bool bold = false, italic = false, underline = false, caps = false;   // text style; caps = ALL CAPS
      float textC[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
      float boxW = 0.0f;
      // Character: strike, highlight band, super/subscript (0 none, 1 super, 2 sub), baseline shift (px, up),
      // pair kerning, width and height scale.
      bool strike = false, highlight = false, kerning = true;
      float highlightC[4] = { 1.0f, 0.9f, 0.2f, 1.0f };
      int script = 0;
      float baseShift = 0.0f, textSX = 1.0f, textSY = 1.0f;
      // Line colours (own = not the text colour), no break (never wraps), horizontal offset (px),
      // optical alignment (leading punctuation hangs), OpenType language tag, outline (centred, px).
      bool ulOwn = false, stOwn = false, noBreak = false, optical = false, outline = false;
      float ulC[4] = { 0.0f, 0.0f, 0.0f, 1.0f }, stC[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
      float offsetX = 0.0f, outlineW = 2.0f;
      float outlineC[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
      std::string lang;
      // Paragraph (a new line starts one; box text): indents and spacing, px.
      float indentL = 0.0f, indentR = 0.0f, indentFirst = 0.0f, spaceBefore = 0.0f, spaceAfter = 0.0f;
      // 3D (After Effects model): the layer is a card the comp camera sees.
      // angle is the Z rotation; x, y place the anchor as in 2D, z pushes it away.
      bool is3d = false;
      float z = 0.0f, az = 0.0f;              // depth and anchor depth, px
      float rx = 0.0f, ry = 0.0f;             // X and Y rotation, degrees
      float sz = 1.0f;                        // depth scale of a 3D object (on top of its width and height scale)
      float orient[3] = { 0.0f, 0.0f, 0.0f }; // orientation X Y Z, degrees, under the rotations
      bool collapse = false;                  // a pass-through group: its 3D layers sort with the outside
      // Distort: each corner (top left, top right, bottom right, bottom left) moved by dx, dy px.
      bool warpOn = false;
      float warp[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
      Fx fx[kFxCount] = { FxDefault(0), FxDefault(1), FxDefault(2), FxDefault(3), FxDefault(4), FxDefault(5), FxDefault(6), FxDefault(7),
                          FxDefault(8), FxDefault(9) };
      std::vector<std::pair<int, Fx>> fxMore;   // more of one kind (two strokes, three shadows): type and settings, drawn in order
      // Blend if (grey): this layer's and the underlying picture's black lo/hi and white lo/hi, 0..1.
      float bifT[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
      float bifU[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
      float blendGamma = 2.2f;      // the gamma the layer blends at (2.2 = the document's own)
      int aaMode = 0;               // 0 default, 1 on, 2 off (hard edges: shapes, text, nearest sampling)
      bool noExport = false;        // drawn in the view, left out of exported pictures and layered files
      bool AnyFx() const
      {
         for (const Fx& f : fx)
            if (f.on)
               return true;
         for (const auto& m : fxMore)
            if (m.second.on)
               return true;
         return false;
      }
      bool HasBlendIf() const
      {
         return bifT[0] > 0.0f || bifT[1] > 0.0f || bifT[2] < 1.0f || bifT[3] < 1.0f || bifU[0] > 0.0f || bifU[1] > 0.0f ||
                bifU[2] < 1.0f || bifU[3] < 1.0f;
      }
   };

   // Motion-first layers: a layer number that a modulator, expression, recorded gesture or MSEG can drive. A
   // field gets a slot (its param index on this node) the first time it is bound; slots save with the document,
   // so a binding keeps pointing at the same layer number across saves and undo.
   enum LayerField { kLfOpacity = 0, kLfFill, kLfX, kLfY, kLfScaleX, kLfScaleY, kLfAngle, kLfZ, kLfRotX, kLfRotY,
                     kLfAdjust, kLfFontSize, kLfTracking, kLfLeading, kLfMaskDensity, kLfFillAngle, kLfFxBase,
                     kLfFxFields = 4, kLfCount = kLfFxBase + kFxCount * kLfFxFields };   // per fx: size, distance, angle, opacity
   struct ModSlot { int layer = 0, field = 0, slot = 0; };
   static constexpr int kSlotBase = 8, kSlotMax = 399;   // param indices (Drive A..D 0..3, brush 4..7; discrete params start at 400)

   struct Document
   {
      int w = 1920, h = 1080;
      int background = 0;           // 0 white, 1 black, 2 colour, 3 transparent
      float bgColor[3] = { 1.0f, 1.0f, 1.0f };
      bool ratioLock = false;
      bool rulers = false, grid = false, snap = true, guidesOn = true;
      float gridSize = 100.0f;
      int gridSubdiv = 4;
      std::vector<float> guidesX, guidesY;   // document pixels
      float fps = 60.0f, seconds = 10.0f;    // Render Animation
      bool linear = true, headroom = true;   // composite in linear light, light above 1.0 survives
      uint64_t camUid = 0;                   // a Camera node for 3D layers; 0 = the default 50 mm camera
      int symmetry = 0;                      // painting: 0 off, 1 left-right, 2 top-bottom, 3 both, 4 radial
      int symN = 6;                          // radial copies
      uint64_t brushSrc[3] = { 0, 0, 0 };    // canvas nodes feeding the brush: tip, grain, colour (0 = built in)
      bool brushLive = false;                // sources follow the node mid-stroke; off = frozen when the stroke starts
      std::vector<ModSlot> slots;            // bound layer numbers (motion-first layers)
   };

   // Set once by the app: the live node for a uid, nullptr when it is gone.
   static std::function<INode*(uint64_t)> ResolveUid;
   // A node's viewport orbit (azimuth, elevation in degrees), so a 3D object layer is seen as its node card shows it.
   static std::function<bool(uint64_t, float&, float&)> NodeViewOf;
   // The first image input of the node with this uid (the app knows every node's cables), for effect layers.
   static std::function<ImageCable*(uint64_t)> ImageInputOf;
   bool CanTakeBelow(const Layer& l) const { return l.kind == Kind::Node && l.uid != 0 && ImageInputOf && ImageInputOf(l.uid) != nullptr; }

   // The model. Edits go through these so the saved text follows.
   std::vector<Layer>& Layers()
   {
      if (mText != mParsedText)
         Parse();
      return mLayers;
   }
   Document& Doc()
   {
      if (mText != mParsedText)
         Parse();
      return mDoc;
   }
   Layer* FindLayer(int id);
   // A layer's own locks with every enclosing group's: a locked group locks what is inside (Photoshop).
   unsigned EffectiveLock(const Layer& l)
   {
      unsigned lock = l.lock;
      for (int p = l.parent, guard = 0; p != 0 && guard < 64; guard++)
      {
         const Layer* g = FindLayer(p);
         if (g == nullptr)
            break;
         lock |= g->lock;
         p = g->parent;
      }
      return lock;
   }
   static const char* LayerFieldName(int field);           // "Opacity", "Shadow size"
   static float* LayerFieldPtr(Layer& l, int field);
   static bool LayerFieldApplies(const Layer& l, int field); // the field means something on this layer kind
   void LayerFieldRange(const Layer& l, int field, float& lo, float& hi);
   // The param index a layer number is bound through; -1 when it has none (make: give it one).
   int SlotFor(int layer, int field, bool make);
   // A new layer above `above` (0: top of the document), inside the same parent.
   Layer& AddLayer(Kind kind, uint64_t uid, int above = 0);
   void RemoveLayer(int id, bool settleClips = true);   // a group takes its children with it; false: a scratch layer goes
   void Commit();                    // call after changing a Layer or the Document

   // Tree helpers. Order in Layers() is bottom to top; children follow the
   // same order among themselves.
   std::vector<int> ChildrenOf(int parent);
   bool IsInside(int id, int group);   // id is group or sits somewhere under it
   int Depth(int id);
   // Moves `id` to sit just above `target` (or below when `below`), taking
   // target's parent; into = put it inside the group `target` at the top.
   void MoveLayer(int id, int target, bool below, bool into = false, bool settleClips = true);
   int GroupLayers(const std::vector<int>& ids);   // returns the new group's id
   void Ungroup(int group);
   int Duplicate(int id);
   // Copy and paste: the layers (with everything inside a copied group) as a free-standing list, and back in
   // with fresh ids above `above` (0: the top), nudged by (dx, dy). Returns the new top-level ids.
   std::vector<Layer> CopyLayers(const std::vector<int>& ids);
   std::vector<int> PasteLayers(const std::vector<Layer>& clip, int above, float dx, float dy);

   // The layer's own box before the transform (node: after Fit), in pixels.
   bool LayerBox(const Layer& l, float& w, float& h);
   // Box-pixel -> document-pixel affine, row-major 2x3: x' = m0 x + m1 y + m2, y' = m3 x + m4 y + m5.
   void LayerMatrix(const Layer& l, float m[6]);
   // Box-pixel -> document-pixel homography, row-major 3x3 (3D cards and distort
   // make it projective). False when the card is edge-on or behind the camera.
   bool LayerHomography(const Layer& l, float h[9]);
   // The comp camera: zoom (px), position, and its right/down/forward rows.
   struct CompCam
   {
      float zoom = 1.0f;
      float pos[3] = { 0, 0, 0 };
      float rot[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
      float az = 0.0f, el = 0.0f;   // the orbit, degrees (geometry cards turn with it)
   };
   CompCam Camera();
   // A 3D layer's depth from the camera at its box centre (sort key, far first).
   float CardDepth(const Layer& l);
   // A world point (document px, z away from the camera) to document pixels; false behind the camera.
   bool ProjectPoint(const float p[3], float& x, float& y, float* depth = nullptr);
   // The 3D gizmo's frame: the world axis the X, Y and Z rotations turn about, and the card's right and down.
   void CardAxes(const Layer& l, float rot[3][3], float card[2][3]);
   // The four corners in document pixels (top left, top right, bottom right, bottom left), after the crop.
   bool Corners(const Layer& l, float out[8]);
   // The document box a layer covers; a group: the union of its children.
   bool Bounds(int id, float lo[2], float hi[2]);
   // Whether the layer shows (its own eye and every parent's).
   bool Shown(const Layer& l);
   INode* Source(const Layer& l) const;       // node layers, through bypass; nullptr = missing
   INode* MaskSource(const Layer& l) const;
   INode* BrushSource(int slot) const;        // the doc's brush tip/grain/colour node, through bypass
   INode* NodeThrough(uint64_t uid) const;    // any uid, through bypass; nullptr = gone
   // Effect stacks as text (presets, .inf2dfx): every effect of the layer; parsing replaces them.
   static std::string EffectsText(const Layer& l);
   static bool ParseEffects(const std::string& text, Layer& l);
   // A native effect's settings as a layer, for the adjustment shader.
   static Layer AdjLayerOf(const Effect& e);
   // The effect stack and the adjustment extras as text lines for layer `id`; parsing takes one line's key and the
   // rest after the id, false for a key it does not know.
   static void WriteEffectLines(std::ostream& o, const Layer& l, int id);
   // A layer's styles (fx and their extras) as a preset's text ("inf2dstyle" first), and back; Parse replaces them all.
   static std::string StyleText(const Layer& l);
   static bool ParseStyle(const std::string& text, Layer& l);
   static bool ParseEffectLine(const std::string& key, std::istream& s, Layer& l);
   unsigned long long mEffDraws = 0;   // effect stacks drawn (tests: an unchanged stack is not redrawn)
   unsigned long long mBatched = 0, mPasses = 0;   // layers drawn in a batch / single passes (tests)
   // Colour Lookup: a .cube file's size and RGB table (size^3 rows, red fastest); false when it cannot be read.
   static bool ReadCube(const std::string& path, int& size, std::vector<float>& rgb);
   // Levels' Auto: per channel black and white at the 0.1 % tails of the picture the adjustment works on: id 0 the
   // document, an adjustment layer the picture under it, effect >= 0 that layer alone with effects from there on off.
   bool AutoLevels(int id, float out[3][4], int effect = -1);
   // A small luminance histogram (bins, peak 1) of the same picture: the panel draws it in Levels and Curves.
   bool Histogram(int id, std::vector<float>& bins, int count = 64, int effect = -1);
   // The picture the adjustment at (id, effect) works on, top-down RGBA8 at document size (see AutoLevels).
   bool PictureUnder(int id, int effect, int frameId, std::vector<uint8_t>& rgba);
   // Every layer under this one (it and the layers over it hidden), composited and finished.
   bool RenderBelow(int id, int frameId, std::vector<uint8_t>& rgba);
   // Only these layers (and what they hold) drawn as they look, the groups holding them passed through.
   bool RenderSet(const std::vector<int>& set, int frameId, std::vector<uint8_t>& rgba);
   // The box (document px) of what these layers show as they look: masks, crops and empty pixels left out. False
   // when nothing shows.
   bool VisibleBounds(const std::vector<int>& set, int frameId, float lo[2], float hi[2]);
   // Photoshop's Cmd-click on a layer thumbnail: what the layer shows (masks included) becomes the selection, by op.
   bool SelectLayerPixels(int id, int op, int frameId);
   bool SelectMaskPixels(int id, int op);   // the pixel mask as the selection
   // The selection `was` (bottom row first) carried along by a layer moving from `before` to `after`: the marquee
   // follows the pixels it lifted.
   void CarrySelection(const std::vector<uint8_t>& was, const Layer& before, const Layer& after);

   // Document edits that keep every layer's picture where it is.
   void CanvasSize(int w, int h, int anchor);   // anchor 0..8, row major from top left
   void ImageSize(int w, int h);                // scales every layer with the document
   void CropTo(float x0, float y0, float x1, float y1);   // document pixels
   // A turned crop (Photoshop's crop at an angle, Straighten): the picture turns by -deg (degrees, the box's turn
   // clockwise on screen) about the box centre (cx, cy), then the document is cropped to the w x h box. One step.
   void CropRotated(float cx, float cy, float w, float h, float deg);
   // Perspective Crop: the quad q (TL TR BR BL, document px) becomes the whole w x h document, every layer
   // carried by the same projective map (a corner warp, so it stays editable). The size a side gets when it is
   // 0: both 0 is the rectangle the quad shows in perspective (PhotoCraft's free size), one 0 keeps the quad's
   // mean-edge proportions. False, with nothing changed, when q is not convex or a layer reaches past the horizon.
   static bool PerspectiveCropSize(const float q[8], float cx, float cy, int w, int h, int& outW, int& outH);
   bool PerspectiveCrop(const float q[8], int w = 0, int h = 0);

   // The whole document as saved, for duplicating and copying it.
   const std::string& DocText() const { return mText; }
   void SetDocText(const std::string& text) { mText = text; }

   // ---- paint (Block B) ------------------------------------------------
   // A new empty paint layer at the document's size, above `above`.
   int AddPaintLayer(int above = 0);
   // Adds a white (all shown) pixel mask; false when the layer cannot have one.
   bool AddPixelMask(int id);
   // The selection made into the layer's pixel mask (all shown without one).
   bool MaskFromSelection(int id);
   void RemovePixelMask(int id);
   // Layer > Layer Mask > Apply: a paint layer's alpha times its pixel mask (soft values kept as they show), then the
   // mask goes. A disabled mask hides nothing: it is only removed. False: no mask, not a paint layer, or the mask and
   // the pixels are not the same size.
   bool ApplyPixelMask(int id);
   // Invert with the mask targeted: white becomes black across the whole mask.
   bool InvertPixelMask(int id);
   // Mask view (backslash): the layer's pixel mask over the picture as Photoshop's rubylith, document size: clear
   // where the mask shows, half red where it hides. A view only: no pixel changes. 0 = no mask.
   unsigned MaskViewTexture(int id);
   // The layer's pixel mask through its box at document size (bottom row first, 0..255).
   bool MaskInDoc(int id, std::vector<uint8_t>& out);
   // Filter mask: added white (the effects everywhere) on a layer with an effect stack; false otherwise.
   bool AddFilterMask(int id);
   void RemoveFilterMask(int id);
   // The filter mask's grey, document size, bottom row first (0..255).
   bool FilterMaskPixels(int id, std::vector<uint8_t>& out);
   // Clear / Fill with the filter mask targeted: grey (0..1) laid in the selection (soft edges kept), or everywhere
   // without one. One commit.
   bool FillFilterMask(int id, float grey);
   // Gaussian blur of the filter mask (sigma px), inside the selection when there is one: feathers its edges.
   bool BlurFilterMask(int id, float sigma);
   // Copy with the filter mask targeted: a new paint layer of the mask's grey (opaque), cut to the selection. The
   // layer mask is never part of it. 0 = no filter mask.
   int FilterMaskToLayer(int id, int above);
   // Painting on a layer (target 0) or its pixel mask (1) or the quick mask (2) or its filter mask (3).
   // Points are document pixels. EndPaint stores the touched tiles and commits.
   bool BeginPaint(int id, int target, const BrushEngine::Brush& b, float x, float y, float pressure);
   void PaintTo(float x, float y, float pressure);
   void SetPaintMeta(float tiltX, float tiltY, float rotDeg, double timestamp) { mStroke.SetPenMeta(tiltX, tiltY, rotDeg, timestamp); }
   void PaintTick(float dt) { mStroke.Tick(dt); }
   // The brush may be moving (a modulator on its size / opacity / flow): the stroke follows it from the next dab.
   void PaintFollow(const BrushEngine::Brush& b);
   void EndPaint();
   bool Painting() const { return mStroke.Active(); }
   int PaintingLayer() const { return mPaintId; }
   // Bucket fill at a document point, within `tolerance` (0..255) of the
   // clicked colour; contiguous or everywhere; sampled from the layer or the picture.
   bool Bucket(int id, float x, float y, const float rgb[3], float opacity, int tolerance, bool contiguous, bool sampleAll);
   // ---- Remove tool (ai-tools.md 1.8) ----
   // One stroke of the Remove tool: its points in document px (top row first) and its own brush radius.
   struct RemoveStroke
   {
      std::vector<float> xy;   // x0 y0 x1 y1 ...
      float size = 20.0f;      // the brush's diameter, document px
      float Radius() const { return std::max(0.5f, size * 0.5f); }
      size_t Points() const { return xy.size() / 2; }
      float X(size_t k) const { return xy[k * 2]; }
      float Y(size_t k) const { return xy[k * 2 + 1]; }
      // Adds a point unless it repeats the last one.
      void Add(float x, float y)
      {
         if (xy.empty() || X(Points() - 1) != x || Y(Points() - 1) != y)
         {
            xy.push_back(x);
            xy.push_back(y);
         }
      }
   };
   // Removes what the strokes cover, all of them as one job: a stroke that closes on itself takes in what it
   // encloses, the hole grows 2 px past the object's halo, the selection (if any) limits it, and the pixels are
   // rebuilt from around it (a membrane fill for the colour, patches from nearby for the texture). It reads the
   // layer `id`, or the picture with sampleAll; it writes into `id`, or with newLayer into a new paint layer above
   // `id` named "Remove Tool Edits" (then " 1", " 2": the first free name). Returns the layer written, 0 when
   // nothing changed (an empty layer, a hole the selection hides, a locked or transformed layer).
   int Remove(int id, const std::vector<RemoveStroke>& strokes, bool sampleAll, bool newLayer);
   // The name the next new Remove layer takes.
   std::string RemoveLayerName() const;
   // Gradient from a to b (document px), colour A to B; radial when `radial`.
   // kind: 1 linear, 2 radial, 3 angle, 4 reflected, 5 diamond.
   // extend: past the ends 0 holds the end colour, 1 repeats, 2 reflects; dither breaks up 8-bit banding.
   bool Gradient(int id, float ax, float ay, float bx, float by, const float a[4], const float b[4], int kind, float opacity = 1.0f,
                 int extend = 0, bool dither = false);
   // The picture's colour (display values) at a document point.
   // size: 1, 3 or 5 px square, averaged. layer: read that paint layer alone (0 = the whole picture).
   // size is the N of an N by N average (1 = point). zoom > 0 and <= 1 sizes the box as Photoshop does:
   // (N+1) screen pixels, so at 50 % a 3 by 3 covers 8 x 8 document pixels; above 100 % (or zoom 0) it is
   // the centred N x N square.
   bool Pick(float x, float y, float rgb[3], int size = 1, int layer = 0, float zoom = 0.0f);
   // The eyedropper's box in document pixels for pixel (px, py), clipped to w x h; false when empty.
   static bool PickBox(int px, int py, int size, float zoom, int w, int h, int& x0, int& y0, int& x1, int& y1);
   // ---- retouch (Affinity's photo tools) ---------------------------------
   // The Undo brush paints back the layer as it was when first shown (or when SetUndoSource last ran).
   void SetUndoSource(int id);
   // Rebuilds the masked pixels (mask 0..255, w x h) from around them; `off` px to a clean patch, or the best nearby
   // one when autoOff. Rows and offset in the same frame as px.
   static bool RemoveRegion(std::vector<uint8_t>& px, int w, int h, const std::vector<uint8_t>& mask);
   static bool HealRegion(std::vector<uint8_t>& px, int w, int h, const std::vector<uint8_t>& mask, int ox, int oy, bool autoOff);
   // Patch: the selected pixels healed from the place (dx, dy) document px away.
   // Patch: the selection mended from where it was dragged to; selIsSource sends its own pixels there instead;
   // transparent keeps half of what was there.
   bool PatchSelection(int id, float dx, float dy, bool selIsSource = false, bool transparent = false);
   // Content-aware move / extend: the selection's content placed (dx, dy) document px away (scaled and turned
   // about its centre when asked), its edge blended into what is there; move also fills the old place.
   // patch: the selection is mended from the content (dx, dy) away instead (the Patch tool's Content-Aware mode).
   struct CamParams
   {
      float dx = 0, dy = 0;
      bool extend = false;
      int structure = 4;   // 1..7: how much of the content's edge is rebuilt (1 = the most)
      int color = 0;       // 0..10: how far the content's colour follows its new place (above 5)
      float scaleX = 100, scaleY = 100, rotate = 0;   // percent, degrees clockwise
      bool sampleAll = false;
      int target = 0;      // 0 pixels, 1 layer mask, 3 filter mask
      bool patch = false;
   };
   // The solve itself, on a top-down RGBA picture and selection (both W x H): out is the result, write the
   // pixels it changed (255), selOut where the selection goes. Returns "" or why it was refused.
   static std::string ContentAwareSolve(const std::vector<uint8_t>& src, int W, int H, const std::vector<uint8_t>& sel,
                                        const CamParams& p, std::vector<uint8_t>& out, std::vector<uint8_t>& write,
                                        std::vector<uint8_t>& selOut);
   bool ContentAwareMove(int id, const CamParams& p, std::string* err = nullptr);
   // The same solve, the document untouched, on a copy scaled so the selection and its landing are at most maxArea
   // px (0 = full size): rgba (top-down, pw x ph) covers the document rect r (x0, y0, x1, y1), clear outside the
   // changed pixels.
   bool ContentAwarePreview(int id, const CamParams& p, int maxArea, std::vector<uint8_t>& rgba, int& pw, int& ph, float r[4],
                            std::string* err = nullptr);
   // Blemish removal: a disc (document px) healed from the best clean place nearby.
   bool Blemish(int id, float x, float y, float radius);
   // Flood erase: the region the wand would pick at (x, y) cleared; the selection stays as it was.
   bool FloodErase(int id, float x, float y, int tolerance, bool contiguous, bool sampleAll);
   // Object selection: the thing inside a box (document px) told from what is round its edge, as a selection.
   void SelectObject(float x0, float y0, float x1, float y1, int op, bool sampleAll, int layerId);
   // The same with the "select" pack's model (SAM 2.1): a click (x0 == x1, y0 == y1) takes the whole object under it,
   // a box the thing inside it. The picture is analysed once on a worker (about 1.5 s); until then the prompt waits and
   // AiSelectPoll applies it. Returns 0 when the pack is missing (call SelectObject), 1 applied, 2 waiting.
   int SelectObjectAi(float x0, float y0, float x1, float y1, int op, bool sampleAll, int layerId);
   // Select Subject: the main subject of the picture. The system's matting (Vision on macOS, u2netp elsewhere) finds it
   // on a worker; with the "select" pack SAM then redraws it from its box (sharper edges, COCO median IoU 0.77 -> 0.85),
   // keeping the matting's own mask when the two disagree. False when the picture can't be read.
   bool SelectSubject(int op, bool sampleAll, int layerId);
   // Once a frame: applies a waiting prompt when its picture is ready. True when the selection changed.
   bool AiSelectPoll();
   bool AiSelectWaiting() const { return mAiWait.on || mSubjectJob.valid(); }
   // A region (document px, bottom row first, 0..255) joined to the selection by op (kSelNew, Add, Sub, Intersect).
   void CombineSelection(const std::vector<uint8_t>& region, int op);
   // Live selection dialogs (Select and Mask, Color Range; ai-tools.md 1.5-1.6). LiveSelBegin keeps the selection as it
   // was and reads the picture once (the layer, or everything with sampleAll); each preview redraws the selection from
   // that copy without an undo step; LiveSelEnd keeps the result (one stored selection) or puts the copy back.
   bool LiveSelBegin(bool sampleAll, int layerId);
   bool LiveSelActive() const { return !mLivePic.empty(); }
   // The live dialog's picture at a document point (top row first), 0..1: Color Range's eyedropper.
   bool LivePixel(float x, float y, float rgb[3]) const;
   void LiveSelEnd(bool keep);
   // Select and Mask: the kept selection's edge refined against the picture (needs a selection).
   void RefinePreview(const SelectMaths::RefineParams& p);
   // Select and Mask's outputs, after LiveSelEnd(true): 1 the layer's pixel mask, 2 a copy of the layer masked by
   // the selection with its edge colours decontaminated by `decon` (0..1). Returns the layer that took it (0 = none).
   int RefineOutput(int output, int layerId, float decon);
   // Color Range: each pixel's share of the range (samples in document px, top row first) joined by op. With a
   // selection and op New the range is looked for only inside it, as in Photoshop. invert flips the range first.
   void ColorRangePreview(const std::vector<SelectMaths::Sample>& s, const SelectMaths::RangeParams& p, bool invert, int op);
   // Depth Select (ai-tools.md 1.7): the live picture's depth map from Depth Anything, asked for once per dialog.
   // LiveDepthPoll: 1 the map is here, 0 still analysing, -1 no depth model (or it failed).
   int LiveDepthPoll();
   bool LiveDepthReady() const { return !mLiveDepth.empty(); }
   // The map's value (0 far .. 1 near) at a document point, top row first; < 0 when there is none.
   float LiveDepthAt(float x, float y) const;
   // Full between lo and hi (0 far .. 1 near), falling off over soft, joined by op as Color Range is.
   void DepthPreview(float lo, float hi, float soft, bool invert, int op);
   // Focus Area (ledger-gaps Block A): the in-focus part of the live picture (SelectMaths::FocusMap / FocusSelect,
   // the map made once per noise level), joined by op as Color Range is. range and noise 0..1.
   void FocusPreview(float range, float noise, bool invert, int op);
   // Select > Grow (contiguous) / Similar (everywhere): the selection plus every pixel within the Magic Wand's
   // tolerance of a selected colour (SelectMaths::GrowMatch). Reads the layer, or everything with sampleAll.
   // false = no selection or nothing to read.
   bool GrowSelection(bool similar, int tolerance, bool sampleAll, int layerId);
   // Tests: a made-up map (document px, top row first) in place of the model's.
   void SetLiveDepth(const std::vector<float>& topDown);
   // Quick Select with edges (the selection brush's Edges mode; ai-tools.md 1.6): each dab is the brush disc grown to
   // the edges round it. The picture it reads: the active paint layer, or everything with sampleAll, an adjustment
   // or a non-pixel layer; an active group without sampleAll has nothing to read, so a dab is the disc alone.
   bool QuickSelectBegin(bool sampleAll, int layerId);
   void QuickSelectDab(float x, float y, float radius, bool subtract);   // document px, top row first
   void QuickSelectEnd();
   // Lens Blur: true once a depth map asked for by a Lens Blur layer has landed (the canvas redraws then).
   bool LensDepthPoll();
   // Forget a Lens Blur layer's depth so the picture below is analysed again (after it changed).
   void LensDepthReset(int layerId);
   // Lens Blur's state for its rows: 1 depth ready, 0 analysing, -1 no depth model (the blur is even everywhere).
   int LensDepthState(int layerId) const;
   // Tests: a made-up depth (document px, top row first, 0 far .. 1 near) for a Lens Blur layer.
   void SetLensDepth(int layerId, const std::vector<float>& topDown);
   // One call for the agent and tests: Begin, preview, End(true).
   bool SelectColorRange(const std::vector<SelectMaths::Sample>& s, const SelectMaths::RangeParams& p, bool invert, int op,
                         bool sampleAll, int layerId);
   // Mesh warp grid: lines at rest fractions us (columns) and vs (rows) of the grid's box, both 0 .. 1 with the
   // ends included; pts the points' document positions, row by row from the top left (us.size() per row).
   // The Default grid is 4 x 4 at the thirds (Photoshop's one patch with its thirds); `deflt` stays true until a
   // split changes the lines. Points move by bilinear pieces, so a new line through the grid changes nothing.
   struct WarpGrid
   {
      std::vector<float> us { 0.0f, 1.0f / 3.0f, 2.0f / 3.0f, 1.0f }, vs { 0.0f, 1.0f / 3.0f, 2.0f / 3.0f, 1.0f };
      std::vector<float> pts;
      bool deflt = true;
      int Nx() const { return (int)us.size(); }
      int Ny() const { return (int)vs.size(); }
      int Count() const { return Nx() * Ny(); }
   };
   static constexpr int kWarpLinesMax = 12;   // grid lines per axis (the shader's arrays)
   enum WarpSplit { kWarpSplitCross, kWarpSplitH, kWarpSplitV, kWarpSplitRemove };
   // The grid's points laid at rest between the box corners (TL TR BR BL, document px) on its lines.
   static void WarpGridRest(WarpGrid& g, const float corners[8]);
   // Where document point (x, y) sits on the moved grid, as line fractions (s along us, t along vs).
   static bool WarpGridParam(const WarpGrid& g, float x, float y, float& s, float& t);
   // Photoshop's Split Warp Crosswise / Horizontally / Vertically at (s, t), or Remove Warp Split nearest to it,
   // applied to the moved grid and its rest grid alike. A split of the Default grid keeps the thirds across its
   // line and drops the ones it replaces, when that loses no moved point; removing the only split of such a grid
   // gives the Default grid back.
   static bool SplitWarpGrid(WarpGrid& moved, WarpGrid& rest, float s, float t, int how);
   // Mesh warp: a paint layer's pixels pulled through the grid. `from` is the layer's tiles when the warp began;
   // `moved` the grid as pulled; `rest` where its points sat when the warp began (null = the whole surface).
   bool MeshWarp(int id, const std::vector<std::string>& from, const WarpGrid& moved, bool store, bool nearest = false,
                 const WarpGrid* rest = nullptr);
   // The Default grid's rest positions (document px) for a paint layer.
   bool MeshRest(int id, WarpGrid& out);
   // Style picker: blend, opacity, fill and layer effects copied from one layer to another.
   // Style picker parts: which of the style travels.
   enum StylePart { kStStroke = 1, kStFill = 2, kStOpacity = 4, kStEffects = 8, kStCharacter = 16, kStParagraph = 32, kStObject = 64,
                    kStAll = 127 };
   bool CopyLayerStyle(int from, int to, int parts = kStOpacity | kStEffects);
   bool wandAntialias = true;   // the wand's and bucket's soft edge (panel only, not saved)

   // ---- 3D surface paint ---------------------------------------------------
   // A node layer whose source is a 3D object can be painted on its surface: a stroke is picked by ray through the
   // layer's view onto the mesh, lands by UV in the layer's skin, and stays on the object when it turns.
   struct SurfacePoint
   {
      float uv[2] = { 0, 0 };
      float pos[3] = { 0, 0, 0 };   // object space
      float facing = 0;             // 1 face-on .. 0 edge-on
      float texelsPerPx = 0;        // skin texels under one document pixel here
   };
   bool CanSurfacePaint(int id);
   bool IsObjectLayer(const Layer& l);   // a layer that shows a real 3D object
   bool HasGeometryView(int id) const { auto it = mGeo.find(id); return it != mGeo.end() && it->second != nullptr; }
   // The width of a 3D object layer's last render (it follows the document), 0 before one.
   int GeometryRenderWidth(int id) const;
   // The surface under a document point, false when the ray misses the object.
   bool SurfaceAt(int id, float x, float y, SurfacePoint& out);
   // Starts a stroke (brush.size = document px on screen). Makes the skin (side x side) on first use.
   bool BeginSurfacePaint(int id, const BrushEngine::Brush& b, float x, float y, float pressure, int side = 1024);
   void SurfacePaintTo(float x, float y, float pressure);
   // The painted colour (straight RGBA, 0..1) at a document point; false off the object or unpainted.
   bool SurfaceColor(int id, float x, float y, float rgba[4]);
   // The skin's colour at a UV (straight RGBA, 0..1).
   bool SkinAt(int id, float u, float v, float rgba[4]);
   void ClearSkin(int id);
   int SkinSide(int id);
   // The paint surface's texture and size, 0 when not a paint layer (Convert to Draw node).
   bool ReadPaint(int id, std::vector<uint8_t>& rgba, int& w, int& h);
   // The composited picture as last cooked (bottom row first); false before the first cook.
   bool ReadOutput(std::vector<uint8_t>& rgba, int& w, int& h);
   // ---- layered files (Block 8) ---------------------------------------------
   // Photoshop .psd (8/16/32-bit RGB or grey in; 8-bit RGB out) and OpenRaster .ora. Loading replaces the
   // document: its size, then every layer as a paint layer with name, opacity, blend, visibility, clipping,
   // groups and (PSD) pixel masks. Saving cooks first; non-paint layers go out drawn alone. Adjustment layers stay out.
   bool LoadPsd(const std::vector<uint8_t>& bytes, std::string* err = nullptr);
   // note (optional): what Photoshop will not get live (adjustments left out, gradients and styles as pixels).
   bool SavePsd(std::vector<uint8_t>& out, int frameId, std::string* note = nullptr);
   bool LoadOra(const std::vector<uint8_t>& bytes, std::string* err = nullptr);
   bool SaveOra(std::vector<uint8_t>& out, int frameId);
   // One layer drawn alone on transparent at full opacity and Normal, rows top-down (after a cook).
   bool RenderLayerAlone(int id, int frameId, std::vector<uint8_t>& rgba);
   // The paint surface's live texture (thumbnails), 0 when none.
   unsigned PaintTexture(int id);
   // The layer's pixel mask as a texture (grey, white shows), 0 when none.
   unsigned PixelMaskTexture(int id);

   // Symmetry copies of a document point (the point itself first).
   std::vector<std::pair<float, float>> SymmetryPoints(float x, float y);
   // The document's mirrors as doc-pixel affines (x' = m0 x + m1 y + m2, y' = m3 x + m4 y + m5), identity excluded.
   std::vector<std::array<float, 6>> DocMirrors() const;
   static std::string UpperText(const std::string& in);
   // Text layer -> a vector layer of its glyph outlines, same place and colour; returns the new layer's id (0 = failed).
   int ConvertTextToShapes(int id);

   // ---- vector and text (Block C) ---------------------------------------
   int AddVectorLayer(int above = 0);
   // A text or vector layer becomes a paint layer holding the same picture in the same place.
   bool RasterizeLayer(int id);
   // An image / node layer as a paint layer in its place (the layer drawn alone at document size); the new id, 0 when it cannot.
   int RasterizeNodeLayer(int id, int frameId);
   // Sibling layers drawn together become one Normal paint layer in the lowest one's place (Merge Down / Visible,
   // Flatten); hidden ones in the set are dropped. The new id, 0 when it cannot (layers at different levels).
   int MergeLayers(const std::vector<int>& ids, int frameId);
   // A lifted piece put back down (Photoshop's floating selection): `from` drawn plain (no masks, styles or effects,
   // Normal, full strength) into paint layer `into`'s own pixels, then removed. `into` keeps its place, mask, blend
   // and effects. False (nothing changed) when `into` is not an unlocked paint layer.
   bool StampLayerInto(int from, int into, int frameId);
   int AddTextLayer(const std::string& text, float x, float y, float boxW, int above = 0);
   // Document px <-> the layer's box px.
   void DocToBox(const Layer& l, float x, float y, float& bx, float& by);
   void BoxToDoc(const Layer& l, float bx, float by, float& x, float& y);
   // An item's outline as polylines in box px (anchors for paths), for the editor.
   static void Outline(const Shape& s, std::vector<std::vector<float>>& out);
   static bool ShapeBounds(const Shape& s, float lo[2], float hi[2]);
   // True when (x, y) is on the item itself: inside its fill, or within `pad` of its outline or stroke.
   static bool ShapeHit(const Shape& s, float x, float y, float pad);
   // The layer's vector or text picture (and vector mask) as a texture; 0 = none.
   unsigned VectorTexture(int id);
   unsigned VectorMaskTexture(int id);
   // Convert a live shape to an editable cubic Bezier path, split a segment, subdivide, or deform points.
   static bool ShapeToPath(Shape& s);
   static int SplitSegment(Shape& s, int seg, float t);
   static void SubdividePath(Shape& s);
   static void DeformShape(Shape& s, int mode, float amount, float freq);
   // Layer > Combine Shapes with 2+ vector layers: the lower layers' items move into the top one (mapped through both
   // layers' transforms, taking its fill and stroke); the bottom layer's items keep their ops, every item above takes
   // `op`. Non-vector ids are ignored. Returns the kept layer's id, or 0 with nothing changed (under two vector layers,
   // a locked one, a transform that cannot be inverted, an SVG item that would need a turn).
   int CombineShapeLayers(const std::vector<int>& ids, int op);
   static void CopyShapeStyle(const Shape& from, Shape& to);   // fill and stroke, not geometry or op
   // The items as an SVG document (Convert to Sketch node, Copy SVG).
   std::string ToSvg(const Layer& l);
   // An SVG file's text as one item filling (or fitting) the document; false on a parse error.
   bool ImportSvg(int id, const std::string& svgText);
   // A text's raster size (the box a text layer has), cached.
   bool TextSize(const Layer& l, int& w, int& h);
   static std::string EscapeToken(const std::string& s);
   static std::string UnescapeToken(const std::string& s);

   // Selection: one per document, document-sized, kept in the doc text (undo and save carry it).
   enum SelOp { kSelNew = 0, kSelAdd, kSelSub, kSelIntersect };
   void SelectRect(float x0, float y0, float x1, float y1, int op, float feather, bool ellipse, bool aa = false);
   void SelectPolygon(const std::vector<float>& xy, int op, float feather, bool aa = false);
   void SelectEllipse(float x0, float y0, float x1, float y1, int op, float feather, bool aa);   // Photoshop's measured edge   // aa: 1 px coverage edge (Photoshop's Anti-alias)
   void SelectWand(float x, float y, int tolerance, bool contiguous, int op, bool sampleAll, int layerId);
   void SelectAll();
   void Deselect();
   void InvertSelection();
   void ShiftSelection(float dx, float dy);   // the marquee moves by whole pixels
   // 0 expand, 1 contract, 2 smooth (rounds corners, drops specks), 3 border (a ring px wide on the edge).
   void ModifySelection(int op, int px);   // 0 expand, 1 contract, 2 smooth, 3 border, 4 feather (soft edges)
   bool HasSelection() const { return mSelAny; }
   // A text layer's auto name: its first line with any text, cut to at most `maxBytes` on a UTF-8 character
   // boundary; `cut` says whether it was shortened. Empty when the text has no visible line.
   static std::string TextName(const std::string& text, size_t maxBytes, bool* cut = nullptr)
   {
      size_t at = 0;
      std::string line;
      while (at <= text.size())
      {
         const size_t nl = text.find('\n', at);
         line = text.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
         if (line.find_first_not_of(" \t\r") != std::string::npos || nl == std::string::npos)
            break;
         at = nl + 1;
      }
      if (line.find_first_not_of(" \t\r") == std::string::npos)
         line.clear();
      if (cut != nullptr)
         *cut = line.size() > maxBytes;
      if (line.size() > maxBytes)
      {
         size_t n = maxBytes;
         while (n > 0 && ((unsigned char)line[n] & 0xC0) == 0x80)   // never split a multi-byte character
            n--;
         line.resize(n);
      }
      return line;
   }
   // Photoshop's duplicate names: "Layer" -> "Layer copy" -> "Layer copy 2" -> "Layer copy 3".
   static std::string CopyName(const std::string& name)
   {
      const std::string tag = " copy";
      if (name.size() >= tag.size() && name.compare(name.size() - tag.size(), tag.size(), tag) == 0)
         return name + " 2";
      const size_t sp = name.find_last_of(' ');
      if (sp != std::string::npos && sp + 1 < name.size() && sp >= tag.size() &&
          name.compare(sp - tag.size(), tag.size(), tag) == 0 &&
          name.find_first_not_of("0123456789", sp + 1) == std::string::npos && name.size() - sp - 1 < 9)
         return name.substr(0, sp + 1) + std::to_string(std::stoi(name.substr(sp + 1)) + 1);
      return name + tag;
   }
   // The selection as the CPU holds it (document px, bottom row first, 0..255), and whether a selection-brush stroke
   // is live on the GPU (then the CPU copy is behind until the stroke ends).
   const std::vector<uint8_t>& SelectionPixels() const { return mSel; }
   bool SelectionStrokeLive() const { return mStroke.Active() && mPaintTarget == 2; }
   // Delete / fill: the selected pixels of a paint layer (all of it with no selection) cleared, or covered with a colour.
   bool ClearPixels(int id);
   bool FillPixels(int id, const float rgb[3], float opacity);
   // Quick mask: Bucket and Gradient aimed at the selection itself (values 0..1).
   bool BucketSelection(float x, float y, float value, int tolerance, bool contiguous);
   bool GradientSelection(float ax, float ay, float bx, float by, float va, float vb, int kind);
   // Saved selections (named, kept in the document).
   std::vector<std::string> SavedSelections() const;
   void SaveSelection(const std::string& name);
   bool LoadSelection(const std::string& name, int op);
   void DeleteSavedSelection(const std::string& name);
   // Live textures this document holds (leak check).
   int LiveTextures() const;
   bool SelectionBounds(float lo[2], float hi[2]) const;
   // The whole selection's box in document px (x1/y1 past the end), the part moved off the canvas too.
   bool SelectionExtent(int& x0, int& y0, int& x1, int& y1) const;
   float SelectionAt(int x, int y) const;   // 0..1, document px
   // Marching ants for the selection (or the red quick-mask tint), a document-sized overlay; 0 = none.
   unsigned SelectionOverlay(float time, bool quickMask);
   unsigned SelectionTexture();             // the selection as a grey texture, 0 = none
   bool quickMask = false;                  // Q: the selection shows red and the brush paints it

   bool panelOnly = false;           // lives in the 2D View panel until Convert to node
   int soloLayer = 0;                // panel only, not saved: Alt-click eye

   // INode
   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mOut); }
   // Output 0 is the comp; then one output per layer marked output, in id order.
   unsigned int GetOutputTexture(int index) override;
   int OutputCount() const override;
   const char* OutputLabel(int index) const override;
   std::vector<int> OutputLayers() const;
   int GetOutputWidth() const override { return mOut.w > 0 ? mOut.w : mDoc.w; }
   int GetOutputHeight() const override { return mOut.h > 0 ? mOut.h : mDoc.h; }
   void CookIfNeeded(int frameId) override;
   unsigned long long TextureRevision() const override { return mRevision; }
   void VisitParams(ParamVisitor& v) override
   {
      v.Text("doc", mText);
      static const char* kKeys[kDrives] = { "driveA", "driveB", "driveC", "driveD" };
      for (int i = 0; i < kDrives; i++)
         v.Float(kKeys[i], drive[i]);
   }
   float drive[kDrives] = { 0, 0, 0, 0 };   // node knobs, modulatable
   float AdjAmount(const Layer& l) const;
   // No inputs: a bypassed Scene 2D is transparent (BypassSource nullptr).

   // Milliseconds of the last cook, for the status bar.
   float CookMs() const { return mCookMs; }

   // A pixel layer from top-down RGBA at (x, y), under `parent` or above layer `above`; its id.
   int AddFileLayer(int parent, const std::string& name, int x, int y, int w, int h, const std::vector<uint8_t>& rgba, int above = 0);
private:
   void Parse();
   void Serialize();
   const Layer* FindConst(int id) const;
   bool EnsureShaders();
   struct Pair
   {
      GLUtil::Fbo f[2];
      int cur = 0;
      unsigned int Tex() const { return f[cur].tex; }
   };
   Pair& PairAt(int depth);
   void Clear(Pair& p, const float rgba[4]);
   // Composites `l` (any kind) onto `dst`; depth = dst's pool level; clipMode keeps dst's alpha.
   void DrawLayer(const Layer& l, int depth, int frameId, float opacityMul, bool clipMode);
   // A layer with fx: drawn alone, the fx built round it, then blended as one.
   void DrawFx(const Layer& l, int depth, int frameId, float opacityMul, bool clipMode);
   bool EnsureFxShaders();
   // 3D runs: the pair at `depth` gets a depth buffer; layers then hide behind nearer ones.
   bool BeginDepth(int depth);
   void EndDepth(int depth);
   std::vector<const Layer*> ListOf(int parent);   // collapsed groups open in place
   void DrawList(int parent, int depth, int frameId, float opacityMul);
   // One pass: tex (or a fill) placed by `inv` (document px -> box uv), blended onto PairAt(depth).
   struct PassArgs
   {
      unsigned int tex = 0;
      int texW = 0, texH = 0;
      int kind = 0;                 // 0 texture, 1 solid, 2 linear, 3 radial, 4 3D object (document size, premultiplied)
      float inv[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };   // document px -> box px (homogeneous)
      float box[2] = { 1, 1 };
      float crop[4] = { 0, 0, 1, 1 };        // visible part of the box, as uv (left, top, right, bottom)
      int blend = 0;
      float opacity = 1.0f;
      bool clipMode = false;
      bool nearest = false;
      bool srgb = true;             // the texture holds display colours (decode in linear mode)
      unsigned int mask = 0;
      int maskChannel = 0;
      bool maskInvert = false;
      float maskDensity = 1.0f;
      unsigned int pmask = 0;       // pixel mask, sampled with the layer
      unsigned int vmask = 0;       // vector mask, alpha
      bool vmaskInvert = false;
      float vmaskDensity = 1.0f;
      const Layer* fill = nullptr;
      const Layer* bif = nullptr;   // blend if
      float gamma = 1.0f;           // blend gamma / 2.2: colours raised to this while blending
      bool card = false;            // a 3D card: tested against and written to the run's depth
      unsigned int geoDepth = 0;    // kind 4 (a 3D object at document size): its hardware depth
      float geoNF[2] = { 1, 2 };    // and the near / far that depth was drawn with, document px
      float geoUv[4] = { 0, 0, 1, 1 }; // and the document uv box its picture covers (only the object's box is drawn)
      int rect[4] = { 0, 0, 0, 0 }; // pixels the layer can touch (x0, y0, x1, y1, GL rows); empty = all
      bool stable = false;          // tex holds still for the whole frame (a node's output): the pass may wait and batch
   };
   bool LayerRect(const float h[9], PassArgs& a) const;   // false: the layer is off the page
   static float BlendGammaOf(const Layer& l) { return std::fabs(l.blendGamma - 2.2f) < 1e-4f ? 1.0f : std::clamp(l.blendGamma, 0.1f, 10.0f) / 2.2f; }
   void Pass(int depth, const PassArgs& a);
   // Plain full-frame layers wait in a queue and draw up to kBatch at a time in one pass (the picture is read and
   // written once instead of once per layer). Any other use of that level's buffers (PairAt) draws the queue first.
   static constexpr int kBatch = 15;   // 16 fragment texture units (the GL 3.2 minimum), one for the picture
   bool Batchable(int depth, const PassArgs& a) const;
   void FlushQueue();
   std::vector<PassArgs> mQueue;
   int mQueueDepth = -1;
   unsigned int mBatchProgram = 0;
   bool mBatchTried = false;

   // Paint surfaces, live on the GPU, filled from the tiles the text names.
   struct Surface
   {
      GLUtil::Fbo fbo;
      std::vector<std::string> loaded;   // keys the texture holds now
   };
   Surface* SyncSurface(std::map<int, Surface>& in, int id, int w, int h, const std::vector<std::string>& keys);
   void SyncSurfaces();
   void StoreTiles(Surface& s, std::vector<std::string>& keys, const int dirty[4]);
   void ResetForFile(int w, int h);
   bool DocToSurface(const Layer& l, int target, float x, float y, float& sx, float& sy, float& scale);
   bool CamSource(int id, const CamParams& p, std::vector<uint8_t>& src, std::vector<uint8_t>& own, std::string* err);
   std::vector<std::array<float, 6>> SurfaceMirrors(const Layer& l, int target);
   bool EnsurePaintShaders();
   void UploadSelection();
   void ApplyFill(Surface& s, int sw, int sh, const Layer& l, unsigned coverage, const float colA[4], const float colB[4],
                  int kind, const float p0[2], const float p1[2], float opacity);
   std::map<int, Surface> mSurfaces, mMasks, mFxMasks;
   bool StoreFilterMask(int id, const std::vector<uint8_t>& was, std::vector<uint8_t> next);
   std::map<int, std::vector<std::string>> mUndoTiles;   // the Undo brush's source per paint layer
   std::map<int, Surface> mUndoSurf;
   unsigned mWarpProgram = 0;
   // CPU-rastered layers (vector, text) and vector masks, re-rastered when their key changes.
   struct Raster
   {
      unsigned tex = 0;
      int w = 0, h = 0;
      unsigned long long key = 0;
      // While one shape is being drawn: every shape under it, rasterised once (plutovg ARGB), and the
      // picture so far; each frame only the last shape's box is redrawn and uploaded.
      std::vector<uint8_t> base, work;
      unsigned long long baseKey = 0;
      int dirty[4] = { 0, 0, 0, 0 };
   };
   Raster* SyncRaster(std::map<int, Raster>& in, const Layer& l, bool mask);
   void SyncRasters();
   std::map<int, Raster> mVec, mVMasks;
   struct TextBox { unsigned long long key = 0; int w = 0, h = 0; std::vector<uint8_t> rgba; };
   std::map<int, TextBox> mTextBoxes;
   BrushEngine::Stroke mStroke;
   int mPaintId = 0, mPaintTarget = 0;
   struct SkinStroke
   {
      bool on = false;
      float lx = 0, ly = 0, carry = 0, screen = 20.0f, spacing = 0.15f, texels = 1.0f;
      float seed = 1.0f;
   } mSkinStroke;
   void SkinDab(const Layer& l, float x, float y, float pressure, bool skipMain = false);
   unsigned long long SkinRevision(const Layer& l) const;
   std::vector<uint8_t> mSel;          // document px, bottom row first, 0..255
   std::vector<uint8_t> mLiveSel, mLivePic;   // a live dialog's selection before it and picture (bottom row first)
   bool mLiveSelAny = false;
   bool mQuickDisc = false;           // Quick Select has no picture to read: dabs are the disc alone
   std::vector<float> mLiveDepth;     // the live picture's depth (bottom row first), empty until it arrives
   std::vector<float> mLiveFocus;     // Focus Area's sharpness map of the live picture, made for mLiveFocusNoise
   float mLiveFocusNoise = -1.0f;
   GLUtil::Fbo mMaskView;             // MaskViewTexture's overlay, made for mMaskViewKey
   size_t mMaskViewKey = 0;
   // The document-sized picture a selection command reads (bottom row first): the paint layer, or the composite.
   bool ReadSelectPicture(bool sampleAll, int layerId, std::vector<uint8_t>& px);
   uint64_t mLiveDepthKey = 0;        // 0 = not asked yet
   void LiveCombine(std::vector<uint8_t>& region, bool invert, int op);
   struct AiWait
   {
      bool on = false;
      uint64_t key = 0;
      int w = 0, h = 0, op = 0;
      bool box = false;
      float p[4] = { 0, 0, 0, 0 };
      std::vector<uint8_t> guide;      // Select Subject: the matting mask (bottom row first) SAM must agree with
   } mAiWait;                          // a SelectObjectAi prompt waiting for its picture's analysis
   struct SubjectResult { bool ok = false; std::vector<uint8_t> mask; };
   std::future<SubjectResult> mSubjectJob;
   int mSubjectW = 0, mSubjectH = 0, mSubjectOp = 0;
   std::shared_ptr<const std::vector<uint8_t>> mSubjectPx;   // the picture it ran on, bottom row first
   bool AiDecodeApply(uint64_t key, int w, int h, const float p[4], bool box, int op, const std::vector<uint8_t>* guide = nullptr);
   bool SubjectLanded();
   // The selection as tile keys, written into the doc text so undo and save carry it ("-" empty, "+" full).
   struct SelStore { std::string name; int w = 0, h = 0; std::vector<std::string> keys; };
   SelStore mSelStore;
   std::vector<SelStore> mSelSaved;
   // Off-canvas selection (PhotoCraft selection_off_canvas): a marquee moved past the edge keeps the part outside,
   // so moving it back brings it back; Add and Subtract keep that part, New and Intersect drop it. The apron holds
   // the whole selection top down in document px (x0/y0 may be negative) and mSel stays its on-canvas crop. It
   // holds only while that crop is mSel: any other change to the selection drops it. Kept within one canvas of
   // the edge on each side.
   struct SelApron { int x0 = 0, y0 = 0, w = 0, h = 0; std::vector<uint8_t> a; };
   struct ApronStore { int x0 = 0, y0 = 0, w = 0, h = 0; std::vector<std::string> keys; };
   SelApron mSelApron;
   ApronStore mSelApronStore;
   bool ApronMatches() const;
   SelApron ApronOrCanvas() const;           // the apron, or the on-canvas selection as one
   void SetSelectionFromApron(SelApron ap);  // trims it, crops mSel from it, drops it when it fits the canvas
   void StoreSelection();
   // The document changed size: the selection moves by (dx, dy) into a w x h frame (Canvas Size, Crop), or goes.
   void ReframeSelection(int w, int h, int dx, int dy);
   void ReleaseOrphanClips();
   // Clip groups follow layers as they are added, moved and deleted (Photoshop, measured in PhotoCraft
   // clip_group.rs): read before the move, settled after.
   struct ClipsBefore
   {
      std::vector<int> moving, clipped, left;
      std::vector<std::pair<int, std::vector<int>>> groups;
   };
   std::vector<int> ClipGroupOf(int id);
   ClipsBefore ReadClips(const std::vector<int>& moving);
   void SettleClips(const std::vector<int>& placed, const ClipsBefore& before);
   void RestoreSelection();
   bool mSelAny = false, mSelDirty = false;
   GLUtil::Fbo mSelTex, mSelOverlay, mSelScratch, mCloneSrc;
   unsigned long long mSelTexGen = 0, mSelOverlayGen = 0;
   unsigned mSelOverlaySrc = 0;
   bool mSelOverlayQuick = false;
   GLUtil::Fbo mBrushFrozen[3];   // node brush sources as they were when the stroke started
   GLUtil::Fbo mEff[2];           // effect stack ping-pong, document size
   unsigned mEffMixProgram = 0, mLutTex = 0;
   std::string mLutPath;
   int mLutSize = 0;
   GLUtil::Fbo mShBlur[2];         // Shadows / Highlights: the blurred luminance
   void DrawEffects(const Layer& l, int depth, int frameId, float opacityMul, bool clipMode);
   bool EffMix(int ping, unsigned base, unsigned fx, unsigned mask, float opacity, int blend, bool toMask);
   unsigned long long EffectsHash(const Layer& l, int frameId);
   unsigned mLumaBlurProgram = 0;
   bool BindLookup(const std::string& path);
   unsigned BlurLuma(unsigned src, float radius);
   void ApplyBrushSources(BrushEngine::Brush& brush, bool snapshot);
   unsigned mFillProgram = 0, mAntsProgram = 0, mResampleProgram = 0;
   int mGradExtend = 0;          // the next ApplyFill's gradient extend and dither (Gradient sets, then clears)
   bool mGradDither = false;
   bool mPaintTried = false;
   unsigned long long mPaintEdits = 0;   // bumps when a surface changes outside a stroke

   std::vector<Layer> mLayers;
   Document mDoc;
   int mNextId = 1;
   std::string mText;
   std::string mParsedText;

   std::deque<Pair> mPool;   // deque: a deeper level never moves the ones in use
   // Depth buffers per pool level (attachment 1 of both FBOs), and the level a 3D run is on.
   struct DepthBufs
   {
      unsigned tex[2] = { 0, 0 };
      int w = 0, h = 0;
   };
   std::map<int, DepthBufs> mDepth;
   int mDepthLevel = -1;
   // Layer fx scratch: blurred alpha (two ping-pong), the finished layer.
   GLUtil::Fbo mFxBlur[2], mFxBlur2[2], mFxOut, mFxExtra, mFxMore[2];
   unsigned mBlurProgram = 0, mFxProgram = 0, mFxCombineProgram = 0;
   bool mFxTried = false;
   // Adjustment layers: the picture below, changed, ready for the layer's normal pass.
   GLUtil::Fbo mAdjFbo;
   // Lens Blur's depth of the picture below each Lens Blur layer (ai-tools.md 1.7), by layer id. Asked for once
   // (Depth Anything on the AiSelect worker); the layer draws nothing new until it lands. Rows bottom first.
   struct LensDepth
   {
      uint64_t key = 0;
      unsigned tex = 0;
      int w = 0, h = 0;
      bool pending = false, failed = false;
   };
   std::map<int, LensDepth> mLensDepth;
   unsigned LensDepthTex(const Layer& l, unsigned src, bool& wait);
   // Relief paint layers, lit; one per layer, redone each cook.
   std::map<int, GLUtil::Fbo> mRelief;
   unsigned mReliefProgram = 0;
   bool mReliefTried = false;
   unsigned ShadeRelief(const Layer& l, unsigned tex, int w, int h);
   unsigned mAdjProgram = 0;
   bool mAdjTried = false, mAdjReady = false;
   bool RunAdjust(const Layer& l, unsigned src);
   bool CookOnBelow(const Layer& l, INode* src, unsigned below);
   // 3D object layers: the object drawn by the 3D View's renderer straight through the comp camera at document size,
   // with its depth, so objects and cards hide each other per pixel. Scene2D3D.cpp.
   std::map<int, std::shared_ptr<struct Scene2DGeo>> mGeo;   // shared: the type stays incomplete here
   // The layer's object -> comp camera: GL view and projection at document size, the eye in the object's world,
   // near / far in document px. One place, so the render, the pick and the brush agree.
   struct GeoCam;
   bool GeoPlace(const Layer& l, IGeometrySource* geo, GeoCam& out);
   // A 3D object layer's outline on screen: its projected box (a turned object does not foreshorten like a card).
   bool GeometryCorners(const Layer& l, float out[8], bool needAll = false);   // needAll: false unless all 8 corners are in front of the camera
   // Renders the object; colour (premultiplied) and hardware depth at document size. False: nothing to draw.
   bool GeometryRender(const Layer& l, unsigned& color, unsigned& depthTex, float nf[2], float uv[4], int px[4]);
   GLUtil::Fbo mOut;
   std::map<int, GLUtil::Fbo> mLayerOut;          // output layers, by id
   mutable std::map<int, std::string> mOutLabels;
   bool mIsolating = false;                       // drawing one output layer: no below cache
   bool mAccOpaque = false;                       // the top-level picture is opaque: Normal layers blend in place
   unsigned int mPassProgram = 0, mFinishProgram = 0;
   bool mShaderTried = false;
   int mLastCookFrame = -1;
   unsigned long long mRevision = 1;
   unsigned long long mStamp = 0;    // what the last cook drew
   // Below cache: the top-level composite as it stood at the start of top-level entry mBelowAt, so an edit to one
   // layer redraws only it and what sits above. mKidHash is last cook's per-entry change stamp.
   GLUtil::Fbo mBelow;
   struct FillPic { unsigned tex = 0; int w = 0, h = 0; };
   std::map<std::string, FillPic> mFillPics;
   size_t mBelowAt = 0;
   unsigned long long mBelowKey = 0;
   unsigned long long mGlobalHash = 0;
   std::vector<unsigned long long> mKidHash;
   void TopHashes(const std::vector<const Layer*>& kids, std::vector<unsigned long long>& out, unsigned long long& global);
   // TopHashes' parse of mText (per-layer line hashes + the document hash), reused until mText
   // or the layer count changes: the text is re-scanned only on an edit, not every composite frame.
   std::string mTopHashText;
   size_t mTopHashLayerCount = (size_t)-1;
   std::unordered_map<int, unsigned long long> mTopHashLines;
   unsigned long long mTopHashDoc = 0;
   unsigned long long BelowKey(const std::vector<const Layer*>& kids, const std::vector<unsigned long long>& hashes, size_t at,
                               unsigned long long global);
public:
   static bool sBelowCache;          // tests turn it off to compare pictures
   static bool sLensDepthAuto;       // Lens Blur asks the depth model on its own (tests turn it off: even blur)
   static bool sBatch;               // batched layer passes (tests turn it off to compare pictures)
   static bool sExport;              // drawing for an export: layers marked noExport stay out
   unsigned FillTexture(const std::string& path, int& w, int& h);   // bitmap fill layers: the picture, cached
private:
   float mCookMs = 0.0f;
};
