#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "INode.h"
#include "GLUtil.h"
#include "BrushEngine.h"

// The 2D View's document: node outputs, fills and groups stacked into one
// picture at the document's size. A node layer only references its node (by
// uid) and places it, so the node keeps animating and is never written to; a
// deleted node leaves its layer as "source missing". No input pins: the
// layers are the inputs. Plan: internal/plans/2d-view/README.md.
class Scene2DNode : public INode
{
public:
   static INode* Create() { return new Scene2DNode(); }
   ~Scene2DNode() override;

   enum class Kind { Node = 0, Fill = 1, Group = 2, Paint = 3, Vector = 4, Text = 5 };
   static constexpr int kKindCount = 6;

   // One vector item: a pen path, a live shape or an imported SVG. Points are
   // the layer's own box pixels, y down.
   struct Shape
   {
      enum Type { kPath = 0, kRect, kEllipse, kPolygon, kStar, kLine, kSvg, kTypeCount };
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
   enum FxType { kFxShadow = 0, kFxInner, kFxGlow, kFxStroke, kFxOverlay, kFxCount };
   struct Fx
   {
      bool on = false;
      float c[4] = { 0.0f, 0.0f, 0.0f, 0.75f };   // colour and its opacity (display values)
      float angle = 120.0f;   // shadows: light from this angle, degrees (Photoshop: 90 = from the top)
      float dist = 10.0f;     // shadows: offset, px
      float size = 10.0f;     // blur (shadows, glow) or width (stroke), px
      float spread = 0.0f;    // shadows and glow: 0..1, how hard the edge is
      int pos = 0;            // stroke: 0 outside, 1 inside, 2 centre
   };
   static Fx FxDefault(int type);
   static const char* FxName(int type);

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
      bool open = true;             // a group's row is expanded in the Layers list
      // Placement, in document pixels, y down. (x, y) is where the anchor sits;
      // the anchor is a fraction of the layer's own box.
      float x = 0.0f, y = 0.0f;
      float sx = 1.0f, sy = 1.0f;
      float angle = 0.0f;           // degrees, clockwise on screen
      float ax = 0.5f, ay = 0.5f;
      bool flipX = false, flipY = false;
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
      int fillType = 0;             // 0 solid, 1 linear, 2 radial
      float colorA[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
      float colorB[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
      float fillAngle = 90.0f;      // linear: degrees, 90 = top to bottom
      // Paint layers: the surface's size and its tiles (TileStore keys, row
      // major from the bottom row, kTile px square).
      int pw = 0, ph = 0;
      std::vector<std::string> tiles;
      // Pixel mask (any layer but a group): grey, white shows. Sized to the layer's box.
      bool pmaskOn = true;
      int mw = 0, mh = 0;
      std::vector<std::string> mtiles;
      bool HasPixelMask() const { return mw > 0 && mh > 0; }
      // Vector layers: items bottom to top; pw/ph hold the box size.
      std::vector<Shape> shapes;
      // Vector mask (any layer but a group): inside the items shows. No items shows all.
      bool hasVMask = false, vmaskOn = true;
      std::vector<Shape> vmask;
      // Text layers. boxW 0 = point text (no wrap); else wraps at boxW px.
      std::string text, font;
      float fontSize = 96.0f, leading = 1.2f, tracking = 0.0f;
      int align = 0;                // left, centre, right, justified
      float textC[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
      float boxW = 0.0f;
      // 3D (After Effects model): the layer is a card the comp camera sees.
      // angle is the Z rotation; x, y place the anchor as in 2D, z pushes it away.
      bool is3d = false;
      float z = 0.0f, az = 0.0f;              // depth and anchor depth, px
      float rx = 0.0f, ry = 0.0f;             // X and Y rotation, degrees
      float orient[3] = { 0.0f, 0.0f, 0.0f }; // orientation X Y Z, degrees, under the rotations
      bool collapse = false;                  // a pass-through group: its 3D layers sort with the outside
      // Distort: each corner (top left, top right, bottom right, bottom left) moved by dx, dy px.
      bool warpOn = false;
      float warp[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
      Fx fx[kFxCount] = { FxDefault(0), FxDefault(1), FxDefault(2), FxDefault(3), FxDefault(4) };
      // Blend if (grey): this layer's and the underlying picture's black lo/hi and white lo/hi, 0..1.
      float bifT[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
      float bifU[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
      bool AnyFx() const
      {
         for (const Fx& f : fx)
            if (f.on)
               return true;
         return false;
      }
      bool HasBlendIf() const
      {
         return bifT[0] > 0.0f || bifT[1] > 0.0f || bifT[2] < 1.0f || bifT[3] < 1.0f || bifU[0] > 0.0f || bifU[1] > 0.0f ||
                bifU[2] < 1.0f || bifU[3] < 1.0f;
      }
   };

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
   };

   // Set once by the app: the live node for a uid, nullptr when it is gone.
   static std::function<INode*(uint64_t)> ResolveUid;

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
   // A new layer above `above` (0: top of the document), inside the same parent.
   Layer& AddLayer(Kind kind, uint64_t uid, int above = 0);
   void RemoveLayer(int id);         // a group takes its children with it
   void Commit();                    // call after changing a Layer or the Document

   // Tree helpers. Order in Layers() is bottom to top; children follow the
   // same order among themselves.
   std::vector<int> ChildrenOf(int parent);
   bool IsInside(int id, int group);   // id is group or sits somewhere under it
   int Depth(int id);
   // Moves `id` to sit just above `target` (or below when `below`), taking
   // target's parent; into = put it inside the group `target` at the top.
   void MoveLayer(int id, int target, bool below, bool into = false);
   int GroupLayers(const std::vector<int>& ids);   // returns the new group's id
   void Ungroup(int group);
   int Duplicate(int id);

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
   // The four corners in document pixels (top left, top right, bottom right, bottom left), after the crop.
   bool Corners(const Layer& l, float out[8]);
   // The document box a layer covers; a group: the union of its children.
   bool Bounds(int id, float lo[2], float hi[2]);
   // Whether the layer shows (its own eye and every parent's).
   bool Shown(const Layer& l);
   INode* Source(const Layer& l) const;       // node layers, through bypass; nullptr = missing
   INode* MaskSource(const Layer& l) const;

   // Document edits that keep every layer's picture where it is.
   void CanvasSize(int w, int h, int anchor);   // anchor 0..8, row major from top left
   void ImageSize(int w, int h);                // scales every layer with the document
   void CropTo(float x0, float y0, float x1, float y1);   // document pixels

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
   // Painting on a layer (target 0) or its pixel mask (1) or the quick mask (2).
   // Points are document pixels. EndPaint stores the touched tiles and commits.
   bool BeginPaint(int id, int target, const BrushEngine::Brush& b, float x, float y, float pressure);
   void PaintTo(float x, float y, float pressure);
   void EndPaint();
   bool Painting() const { return mStroke.Active(); }
   int PaintingLayer() const { return mPaintId; }
   // Bucket fill at a document point, within `tolerance` (0..255) of the
   // clicked colour; contiguous or everywhere; sampled from the layer or the picture.
   bool Bucket(int id, float x, float y, const float rgb[3], float opacity, int tolerance, bool contiguous, bool sampleAll);
   // Gradient from a to b (document px), colour A to B; radial when `radial`.
   bool Gradient(int id, float ax, float ay, float bx, float by, const float a[4], const float b[4], bool radial);
   // The picture's colour (display values) at a document point.
   bool Pick(float x, float y, float rgb[3]);
   // The paint surface's texture and size, 0 when not a paint layer (Convert to Draw node).
   bool ReadPaint(int id, std::vector<uint8_t>& rgba, int& w, int& h);
   // The paint surface's live texture (thumbnails), 0 when none.
   unsigned PaintTexture(int id);

   // Symmetry copies of a document point (the point itself first).
   std::vector<std::pair<float, float>> SymmetryPoints(float x, float y);
   // The document's mirrors as doc-pixel affines (x' = m0 x + m1 y + m2, y' = m3 x + m4 y + m5), identity excluded.
   std::vector<std::array<float, 6>> DocMirrors() const;

   // ---- vector and text (Block C) ---------------------------------------
   int AddVectorLayer(int above = 0);
   int AddTextLayer(const std::string& text, float x, float y, float boxW, int above = 0);
   // Document px <-> the layer's box px.
   void DocToBox(const Layer& l, float x, float y, float& bx, float& by);
   void BoxToDoc(const Layer& l, float bx, float by, float& x, float& y);
   // An item's outline as polylines in box px (anchors for paths), for the editor.
   static void Outline(const Shape& s, std::vector<std::vector<float>>& out);
   static bool ShapeBounds(const Shape& s, float lo[2], float hi[2]);
   // The layer's vector or text picture (and vector mask) as a texture; 0 = none.
   unsigned VectorTexture(int id);
   // The items as an SVG document (Convert to Sketch node, Copy SVG).
   std::string ToSvg(const Layer& l);
   // An SVG file's text as one item filling (or fitting) the document; false on a parse error.
   bool ImportSvg(int id, const std::string& svgText);
   // A text's raster size (the box a text layer has), cached.
   bool TextSize(const Layer& l, int& w, int& h);
   static std::string EscapeToken(const std::string& s);
   static std::string UnescapeToken(const std::string& s);

   // Selection: one per document, document-sized, not saved.
   enum SelOp { kSelNew = 0, kSelAdd, kSelSub, kSelIntersect };
   void SelectRect(float x0, float y0, float x1, float y1, int op, float feather, bool ellipse);
   void SelectPolygon(const std::vector<float>& xy, int op, float feather);
   void SelectWand(float x, float y, int tolerance, bool contiguous, int op, bool sampleAll, int layerId);
   void SelectAll();
   void Deselect();
   void InvertSelection();
   bool HasSelection() const { return mSelAny; }
   bool SelectionBounds(float lo[2], float hi[2]) const;
   float SelectionAt(int x, int y) const;   // 0..1, document px
   // Marching ants for the selection (or the red quick-mask tint), a document-sized overlay; 0 = none.
   unsigned SelectionOverlay(float time, bool quickMask);
   unsigned SelectionTexture();             // the selection as a grey texture, 0 = none
   bool quickMask = false;                  // Q: the selection shows red and the brush paints it

   bool panelOnly = false;           // lives in the 2D View panel until Convert to node
   int soloLayer = 0;                // panel only, not saved: Alt-click eye

   // INode
   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mOut); }
   int GetOutputWidth() const override { return mOut.w > 0 ? mOut.w : mDoc.w; }
   int GetOutputHeight() const override { return mOut.h > 0 ? mOut.h : mDoc.h; }
   void CookIfNeeded(int frameId) override;
   unsigned long long TextureRevision() const override { return mRevision; }
   void VisitParams(ParamVisitor& v) override { v.Text("doc", mText); }
   // No inputs: a bypassed Scene 2D is transparent (BypassSource nullptr).

   // Milliseconds of the last cook, for the status bar.
   float CookMs() const { return mCookMs; }

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
      int kind = 0;                 // 0 texture, 1 solid, 2 linear, 3 radial
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
      const Layer* fill = nullptr;
      const Layer* bif = nullptr;   // blend if
      bool card = false;            // a 3D card: tested against and written to the run's depth
   };
   void Pass(int depth, const PassArgs& a);

   // Paint surfaces, live on the GPU, filled from the tiles the text names.
   struct Surface
   {
      GLUtil::Fbo fbo;
      std::vector<std::string> loaded;   // keys the texture holds now
   };
   Surface* SyncSurface(std::map<int, Surface>& in, int id, int w, int h, const std::vector<std::string>& keys);
   void SyncSurfaces();
   void StoreTiles(Surface& s, std::vector<std::string>& keys, const int dirty[4]);
   bool DocToSurface(const Layer& l, int target, float x, float y, float& sx, float& sy, float& scale);
   std::vector<std::array<float, 6>> SurfaceMirrors(const Layer& l, int target);
   bool EnsurePaintShaders();
   void UploadSelection();
   void ApplyFill(Surface& s, int sw, int sh, const Layer& l, unsigned coverage, const float colA[4], const float colB[4],
                  int kind, const float p0[2], const float p1[2], float opacity);
   std::map<int, Surface> mSurfaces, mMasks;
   // CPU-rastered layers (vector, text) and vector masks, re-rastered when their key changes.
   struct Raster
   {
      unsigned tex = 0;
      int w = 0, h = 0;
      unsigned long long key = 0;
   };
   Raster* SyncRaster(std::map<int, Raster>& in, const Layer& l, bool mask);
   void SyncRasters();
   std::map<int, Raster> mVec, mVMasks;
   struct TextBox { unsigned long long key = 0; int w = 0, h = 0; std::vector<uint8_t> rgba; };
   std::map<int, TextBox> mTextBoxes;
   BrushEngine::Stroke mStroke;
   int mPaintId = 0, mPaintTarget = 0;
   std::vector<uint8_t> mSel;          // document px, bottom row first, 0..255
   bool mSelAny = false, mSelDirty = false;
   GLUtil::Fbo mSelTex, mSelOverlay, mSelScratch;
   unsigned mFillProgram = 0, mAntsProgram = 0, mResampleProgram = 0;
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
   GLUtil::Fbo mFxBlur[2], mFxOut;
   unsigned mBlurProgram = 0, mFxProgram = 0;
   bool mFxTried = false;
   // Bare geometry cards: a solo render per layer.
   std::map<int, std::shared_ptr<class NodeViewport>> mGeo;   // shared: the type stays incomplete here
   unsigned GeometryTexture(const Layer& l, int& w, int& h);
   GLUtil::Fbo mOut;
   unsigned int mPassProgram = 0, mFinishProgram = 0;
   bool mShaderTried = false;
   int mLastCookFrame = -1;
   unsigned long long mRevision = 1;
   unsigned long long mStamp = 0;    // what the last cook drew
   float mCookMs = 0.0f;
};
