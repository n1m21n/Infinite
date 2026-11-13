#pragma once

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

   enum class Kind { Node = 0, Fill = 1, Group = 2, Paint = 3 };
   enum Fit { kFitNone = 0, kFitFit, kFitFill, kFitStretch };
   static const std::vector<std::string>& FitNames();
   static constexpr int kPassThrough = -1;   // a group's blend: its children blend straight into what is below
   enum Lock : unsigned { kLockAlpha = 1, kLockPixels = 2, kLockPosition = 4, kLockAll = 8 };

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
   void DrawList(int parent, int depth, int frameId, float opacityMul);
   // One pass: tex (or a fill) placed by `inv` (document px -> box uv), blended onto PairAt(depth).
   struct PassArgs
   {
      unsigned int tex = 0;
      int texW = 0, texH = 0;
      int kind = 0;                 // 0 texture, 1 solid, 2 linear, 3 radial
      float inv[6] = { 1, 0, 0, 0, 1, 0 };   // document px -> box px
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
      const Layer* fill = nullptr;
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
   bool EnsurePaintShaders();
   void UploadSelection();
   void ApplyFill(Surface& s, int sw, int sh, const Layer& l, unsigned coverage, const float colA[4], const float colB[4],
                  int kind, const float p0[2], const float p1[2], float opacity);
   std::map<int, Surface> mSurfaces, mMasks;
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

   std::deque<Pair> mPool;            // deque: a deeper level never moves the ones in use
   GLUtil::Fbo mOut;
   unsigned int mPassProgram = 0, mFinishProgram = 0;
   bool mShaderTried = false;
   int mLastCookFrame = -1;
   unsigned long long mRevision = 1;
   unsigned long long mStamp = 0;    // what the last cook drew
   float mCookMs = 0.0f;
};
