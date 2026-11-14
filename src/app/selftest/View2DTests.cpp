// 2D View self-test (INFINITE_VIEW2DTEST): the Scene 2D document model, its save
// text, the composite it draws, and the plan's two Block A tasks (T1 poster, T2
// overlay) built from node and fill layers only. Plan: internal/plans/2d-view.
#include "app/AppShared.h"
#include "nodes/Geometry3DNodes.h"
#include "nodes/Scene2DNode.h"
#include "nodes/SceneNodes.h"
#include "core/TileStore.h"
#include "gl3.h"

extern bool gScene2DBounds;   // Scene2DNode.cpp: bounded layer passes

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace app
{
int View2DPanelSelfCheck(Scene2DNode* doc); // View2DPanel.cpp
namespace
{
   using Layer = Scene2DNode::Layer;
   using Kind = Scene2DNode::Kind;

   Scene2DNode* Doc()
   {
      for (GraphNode& gn : gNodes)
         if (auto* s = dynamic_cast<Scene2DNode*>(gn.node.get()))
            return s;
      return nullptr;
   }

   // One pixel of the cooked document, y down like the document.
   std::vector<float> Pixel(Scene2DNode* doc, int x, int y)
   {
      const int w = doc->GetOutputWidth(), h = doc->GetOutputHeight();
      std::vector<float> px((size_t)w * h * 4);
      glBindTexture(GL_TEXTURE_2D, doc->GetOutputTexture());
      glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, px.data());
      glBindTexture(GL_TEXTURE_2D, 0);
      const size_t at = ((size_t)(h - 1 - y) * w + x) * 4;
      return { px[at], px[at + 1], px[at + 2], px[at + 3] };
   }

   int sFrame = 500000;
   void Cook(Scene2DNode* doc) { doc->CookIfNeeded(++sFrame); }
}

void RunView2DTest()
{
   bool ok = true;
   auto Check = [&](const char* label, bool pass)
   {
      printf("  [%s] %s\n", pass ? "pass" : "FAIL", label);
      if (!pass)
         ok = false;
   };

   NewPatch();
   gNodes.shrink_to_fit();
   const int shape = SpawnNode("Shape", "Source", 0.0f, 0.0f)->index;
   const int text = SpawnNode("Text", "Source", 0.0f, 200.0f)->index;
   const int noise = SpawnNode("Noise", "Source", 0.0f, 400.0f)->index;
   gNodes.shrink_to_fit();
   const size_t before = gNodes.size();

   // ---- A1/A3: add, off the canvas, not addable itself ----
   GraphNode* gn = FindNodeByIndex(shape);
   Check("a Shape can be a layer", gn != nullptr && View2DCanAdd(*gn));
   View2DAddNode(*gn);
   Scene2DNode* doc = Doc();
   Check("the first add makes one document", doc != nullptr && gNodes.size() == before + 1);
   Check("one layer after one add", doc != nullptr && doc->Layers().size() == 1);
   if (doc == nullptr)
   {
      printf("VIEW2DTEST FAIL\n");
      return;
   }
   GraphNode* docGn = nullptr;
   for (GraphNode& g : gNodes)
      if (g.node.get() == doc)
         docGn = &g;
   Check("the document stays off the canvas", docGn != nullptr && docGn->hiddenFromCanvas && doc->panelOnly);
   Check("a Scene 2D is not addable", docGn != nullptr && !View2DCanAdd(*docGn));
   Check("the 2D View opened", gView2DOpen);
   View2DAddNode(*FindNodeByIndex(text));
   View2DAddNode(*FindNodeByIndex(noise));
   Check("three adds, still one document", Doc() == doc && doc->Layers().size() == 3 && gNodes.size() == before + 1);
   Check("a new layer lands on top", doc->Layers().back().uid == FindNodeByIndex(noise)->uid);

   // ---- save text round trip, every field family ----
   {
      Layer& l = doc->Layers()[0];
      l.x = 123.5f; l.y = 45.0f; l.sx = 1.5f; l.sy = 0.5f; l.angle = 8.0f; l.ax = 0.0f; l.ay = 1.0f;
      l.flipX = true; l.blend = 3; l.opacity = 0.4f; l.fill = 0.7f; l.lock = Scene2DNode::kLockPosition; l.label = 5;
      l.clip = true; l.name = "Title card"; l.cropOn = true; l.crop[0] = 4; l.crop[3] = 9; l.nearest = true; l.fit = Scene2DNode::kFitStretch;
      l.maskUid = FindNodeByIndex(noise)->uid; l.maskInvert = true; l.maskChannel = 1; l.maskDensity = 0.25f;
      doc->Doc().guidesX = { 10.0f, 540.0f };
      doc->Doc().background = 3;
      doc->Commit();
      Scene2DNode copy;
      copy.SetDocText(doc->DocText());
      const Layer& c = copy.Layers()[0];
      Check("transform saves", c.x == 123.5f && c.y == 45.0f && c.sx == 1.5f && c.sy == 0.5f && c.angle == 8.0f && c.ax == 0.0f
                                  && c.ay == 1.0f && c.flipX && !c.flipY);
      Check("blend, opacity, fill, lock, label save", c.blend == 3 && std::fabs(c.opacity - 0.4f) < 1e-5f
                                                         && std::fabs(c.fill - 0.7f) < 1e-5f && c.lock == Scene2DNode::kLockPosition && c.label == 5);
      Check("clip, name, crop, sampling, fit save", c.clip && c.name == "Title card" && c.cropOn && c.crop[0] == 4 && c.crop[3] == 9
                                                       && c.nearest && c.fit == Scene2DNode::kFitStretch);
      Check("mask saves", c.maskUid == l.maskUid && c.maskInvert && c.maskChannel == 1 && std::fabs(c.maskDensity - 0.25f) < 1e-5f);
      Check("guides and background save", copy.Doc().guidesX.size() == 2 && copy.Doc().guidesX[1] == 540.0f && copy.Doc().background == 3);
      Check("panel-only saves", copy.panelOnly == doc->panelOnly);
   }

   // ---- A6: groups ----
   {
      const int a = doc->Layers()[1].id, b = doc->Layers()[2].id;
      const int g = doc->GroupLayers({ a, b });
      Check("grouping makes a pass-through group", doc->FindLayer(g) != nullptr && doc->FindLayer(g)->kind == Kind::Group
                                                      && doc->FindLayer(g)->blend == Scene2DNode::kPassThrough);
      Check("both layers sit in it", doc->FindLayer(a)->parent == g && doc->FindLayer(b)->parent == g && doc->ChildrenOf(g).size() == 2);
      Scene2DNode copy;
      copy.SetDocText(doc->DocText());
      Check("the group saves", copy.FindLayer(a) != nullptr && copy.FindLayer(a)->parent == g);
      doc->Ungroup(g);
      Check("ungroup puts them back", doc->FindLayer(g) == nullptr && doc->FindLayer(a)->parent == 0 && doc->Layers().size() == 3);
      const int d = doc->Duplicate(a);
      Check("duplicate copies the layer", doc->FindLayer(d) != nullptr && doc->FindLayer(d)->uid == doc->FindLayer(a)->uid);
      doc->RemoveLayer(d);
   }

   // ---- A8: canvas size keeps pictures where they are ----
   {
      doc->Layers();
      doc->CanvasSize(100, 100, 4);
      Layer& l = doc->Layers()[1];
      l.x = 50.0f; l.y = 50.0f;
      doc->Commit();
      doc->CanvasSize(200, 100, 3);   // middle left: grows to the right
      const float leftX = doc->Layers()[1].x;
      doc->CanvasSize(100, 100, 5);   // middle right: shrinks from the left
      const float rightX = doc->Layers()[1].x;
      Check("canvas size from the left keeps x", std::fabs(leftX - 50.0f) < 1e-3f);
      Check("canvas size from the right moves x", std::fabs(rightX + 50.0f) < 1e-3f);
   }

   // ---- composite: background, fill, opacity, blend, clip, source missing ----
   {
      Scene2DNode pic;
      pic.Layers();
      pic.Doc().w = 32;
      pic.Doc().h = 32;
      pic.Doc().background = 1;   // black
      pic.Commit();
      Cook(&pic);
      const std::vector<float> black = Pixel(&pic, 16, 16);
      Check("a black background cooks black", pic.GetOutputTexture() != 0 && black[0] < 0.01f && black[3] > 0.99f);
      Layer& f = pic.AddLayer(Kind::Fill, 0);
      f.colorA[0] = 1.0f; f.colorA[1] = 0.0f; f.colorA[2] = 0.0f; f.colorA[3] = 1.0f;
      pic.Commit();
      Cook(&pic);
      const std::vector<float> red = Pixel(&pic, 16, 16);
      Check("a red fill covers the document", red[0] > 0.98f && red[1] < 0.02f);
      pic.Layers()[0].opacity = 0.5f;
      pic.Commit();
      Cook(&pic);
      const std::vector<float> half = Pixel(&pic, 16, 16);
      // In linear light half red over black encodes to about 0.73.
      Check("half opacity mixes in linear light", half[0] > 0.6f && half[0] < 0.85f);
      printf("  half red over black: %.3f\n", half[0]);
      pic.Layers()[0].opacity = 1.0f;
      Layer& g = pic.AddLayer(Kind::Fill, 0);
      g.colorA[0] = 0.0f; g.colorA[1] = 0.0f; g.colorA[2] = 1.0f; g.colorA[3] = 1.0f;
      g.blend = 0;
      g.visible = false;
      pic.Commit();
      Cook(&pic);
      Check("a hidden layer draws nothing", Pixel(&pic, 16, 16)[2] < 0.02f);
      pic.Layers()[1].visible = true;
      pic.soloLayer = pic.Layers()[0].id;
      Cook(&pic);
      Check("solo shows only that layer", Pixel(&pic, 16, 16)[0] > 0.98f && Pixel(&pic, 16, 16)[2] < 0.02f);
      pic.soloLayer = 0;
      // A missing node layer: kept, draws nothing, the rest still cooks.
      Layer& m = pic.AddLayer(Kind::Node, 0xDEADBEEFull, 0);
      const int missingId = m.id;
      pic.Commit();
      Cook(&pic);
      Check("a missing source keeps its layer", pic.FindLayer(missingId) != nullptr && pic.Source(*pic.FindLayer(missingId)) == nullptr);
      Check("the rest cooks around it", Pixel(&pic, 16, 16)[2] > 0.98f);
      // Clip: a blue fill clipped to a transparent layer shows nothing.
      pic.RemoveLayer(missingId);
      pic.Layers()[0].colorA[3] = 0.0f;   // the red base is now empty
      pic.Layers()[1].clip = true;
      pic.Commit();
      Cook(&pic);
      Check("clipped to an empty layer shows nothing", Pixel(&pic, 16, 16)[2] < 0.02f);
   }

   // ---- T1 poster: 1080x1350, a moving picture, two texts, a shape, a title at 8 degrees ----
   {
      NewPatch();
      gNodes.shrink_to_fit();
      const int vid = SpawnNode("Noise", "Source", 0.0f, 0.0f)->index;   // stands in for the video: a moving picture
      const int t1 = SpawnNode("Text", "Source", 0.0f, 200.0f)->index;
      const int t2 = SpawnNode("Text", "Source", 0.0f, 400.0f)->index;
      const int sh = SpawnNode("Shape", "Source", 0.0f, 600.0f)->index;
      gNodes.shrink_to_fit();
      const size_t nodes0 = gNodes.size();
      const uint64_t vidUid = FindNodeByIndex(vid)->uid, t1Uid = FindNodeByIndex(t1)->uid;
      const uint64_t t2Uid = FindNodeByIndex(t2)->uid, shUid = FindNodeByIndex(sh)->uid;
      View2DAddNode(*FindNodeByIndex(vid));
      Scene2DNode* p = Doc();
      p->CanvasSize(1080, 1350, 4);
      View2DAddNode(*FindNodeByIndex(sh));
      View2DAddNode(*FindNodeByIndex(t2));
      View2DAddNode(*FindNodeByIndex(t1));
      Layer* title = nullptr;
      for (Layer& l : p->Layers())
         if (l.uid == t1Uid)
            title = &l;
      if (title != nullptr)
      {
         title->angle = 8.0f;
         title->y = 300.0f;
      }
      p->Commit();
      Cook(p);
      bool order = p->Layers().size() == 4 && p->Layers()[0].uid == vidUid && p->Layers()[1].uid == shUid
                   && p->Layers()[2].uid == t2Uid && p->Layers()[3].uid == t1Uid;
      Check("T1: four layers in the order added", order);
      Check("T1: 1080 x 1350 picture", p->GetOutputWidth() == 1080 && p->GetOutputHeight() == 1350);
      Check("T1: only the document was added", gNodes.size() == nodes0 + 1);
      float c[8] = {};
      Check("T1: the title is turned 8 degrees", title != nullptr && p->Corners(*p->FindLayer(title->id), c) && c[3] > c[1]);
   }

   // ---- T2 overlay: a logo top right over a shader picture ----
   {
      NewPatch();
      gNodes.shrink_to_fit();
      const int bg = SpawnNode("Noise", "Source", 0.0f, 0.0f)->index;
      const int logo = SpawnNode("Shape", "Source", 0.0f, 200.0f)->index;
      gNodes.shrink_to_fit();
      View2DAddNode(*FindNodeByIndex(bg));
      View2DAddNode(*FindNodeByIndex(logo));
      Scene2DNode* p = Doc();
      Layer& l = p->Layers().back();
      l.sx = l.sy = 0.2f;
      // Top right, 40 px in: anchor the box's top-right corner.
      l.ax = 1.0f;
      l.ay = 0.0f;
      l.x = p->Doc().w - 40.0f;
      l.y = 40.0f;
      p->Commit();
      Cook(p);
      float lo[2], hi[2];
      Check("T2: the logo sits top right", p->Bounds(p->Layers().back().id, lo, hi) && std::fabs(hi[0] - (p->Doc().w - 40.0f)) < 0.5f
                                             && std::fabs(lo[1] - 40.0f) < 0.5f);
      Check("T2: it cooks at the document size", p->GetOutputWidth() == 1920 && p->GetOutputHeight() == 1080);
   }

   // ---- T3 sketch: two paint layers, erase on one; tiles save, undo restores ----
   {
      Scene2DNode pic;
      pic.Layers();
      pic.Doc().w = 300;
      pic.Doc().h = 200;
      pic.Doc().background = 1;   // black
      pic.Commit();
      const int a = pic.AddPaintLayer();
      const int b = pic.AddPaintLayer();
      Check("T3: two paint layers", pic.Layers().size() == 2 && pic.FindLayer(a)->kind == Kind::Paint
                                      && pic.FindLayer(a)->tiles.size() == 2);
      BrushEngine::Brush br = BrushEngine::Presets()[5].brush;   // Hard round
      br.size = 20.0f;
      br.color[0] = br.color[1] = br.color[2] = 1.0f;
      const auto Line = [&](int id, float x0, float x1, float y, const BrushEngine::Brush& use)
      {
         pic.BeginPaint(id, 0, use, x0, y, 1.0f);
         for (float x = x0; x <= x1; x += 7.0f)
            pic.PaintTo(x, y, 1.0f);
         pic.EndPaint();
      };
      const std::string blank = pic.DocText();
      Line(a, 20.0f, 280.0f, 50.0f, br);   // white across the top, crossing the tile seam at 256
      Cook(&pic);
      Check("T3: a stroke lands", Pixel(&pic, 100, 50)[0] > 0.95f && Pixel(&pic, 270, 50)[0] > 0.95f && Pixel(&pic, 100, 150)[0] < 0.02f);
      BrushEngine::Brush red = br;
      red.color[1] = red.color[2] = 0.0f;
      Line(b, 20.0f, 280.0f, 150.0f, red);
      Cook(&pic);
      Check("T3: the second layer paints too", Pixel(&pic, 100, 150)[0] > 0.95f && Pixel(&pic, 100, 150)[1] < 0.02f);
      const std::string twoStrokes = pic.DocText();
      BrushEngine::Brush er = BrushEngine::Presets()[12].brush;   // Eraser hard
      er.size = 30.0f;
      pic.BeginPaint(a, 0, er, 100.0f, 50.0f, 1.0f);
      pic.EndPaint();
      Cook(&pic);
      Check("T3: the eraser clears one layer", Pixel(&pic, 100, 50)[0] < 0.05f && Pixel(&pic, 150, 50)[0] > 0.95f
                                                  && Pixel(&pic, 100, 150)[0] > 0.95f);
      // Save and reopen: the text names the tiles, the store holds them.
      Scene2DNode copy;
      copy.SetDocText(pic.DocText());
      Cook(&copy);
      Check("T3: reopened, the pixels are back", Pixel(&copy, 150, 50)[0] > 0.95f && Pixel(&copy, 100, 50)[0] < 0.05f
                                                    && Pixel(&copy, 100, 150)[0] > 0.95f);
      // A blob line in a patch decodes to the same tile.
      std::vector<std::string> keys;
      TileStore::CollectKeys(pic.DocText(), keys);
      bool blobs = !keys.empty();
      for (const std::string& k : keys)
      {
         std::vector<uint8_t> rgba;
         int tw = 0, th = 0;
         blobs = blobs && !TileStore::Encoded(k).empty() && TileStore::Get(k, rgba, tw, th) && TileStore::Put(rgba.data(), tw, th) == k;
      }
      Check("T3: every tile round-trips as a blob", blobs);
      // Undo is the text from before the eraser.
      pic.SetDocText(twoStrokes);
      Cook(&pic);
      Check("T3: undo brings the erased stroke back", Pixel(&pic, 100, 50)[0] > 0.95f);
      pic.SetDocText(blank);
      Cook(&pic);
      Check("T3: undo to blank clears both", Pixel(&pic, 100, 50)[0] < 0.02f && Pixel(&pic, 100, 150)[0] < 0.02f);
      // A selection keeps paint inside it.
      pic.SelectRect(0.0f, 0.0f, 150.0f, 200.0f, Scene2DNode::kSelNew, 0.0f, false);
      Line(a, 20.0f, 280.0f, 100.0f, br);
      Cook(&pic);
      Check("T3: paint stays inside the selection", Pixel(&pic, 100, 100)[0] > 0.95f && Pixel(&pic, 200, 100)[0] < 0.02f);
      pic.InvertSelection();
      Check("T3: invert flips the selection", pic.SelectionAt(100, 100) < 0.01f && pic.SelectionAt(200, 100) > 0.99f);
      pic.Deselect();
      // Bucket fills the black region of layer b's empty area, picked from the picture.
      const float blue[3] = { 0.0f, 0.0f, 1.0f };
      const bool filled = pic.Bucket(b, 200.0f, 180.0f, blue, 1.0f, 10, true, true);
      Cook(&pic);
      float got[3] = {};
      const bool picked = pic.Pick(200.0f, 180.0f, got);
      Check("T3: bucket fills the open area, not the stroke", filled && picked && got[2] > 0.95f && got[0] < 0.05f
                                                                 && Pixel(&pic, 100, 100)[1] > 0.95f);
      // A pixel mask from a selection hides the rest of the layer.
      pic.SelectRect(0.0f, 0.0f, 300.0f, 120.0f, Scene2DNode::kSelNew, 0.0f, false);
      pic.MaskFromSelection(b);
      pic.Deselect();
      Cook(&pic);
      Check("T3: selection to mask hides outside it", pic.FindLayer(b)->HasPixelMask() && Pixel(&pic, 200, 180)[2] < 0.05f
                                                         && Pixel(&pic, 100, 100)[0] > 0.95f);
      Scene2DNode masked;
      masked.SetDocText(pic.DocText());
      Cook(&masked);
      Check("T3: the mask saves", masked.FindLayer(b)->HasPixelMask() && Pixel(&masked, 200, 180)[2] < 0.05f);
      printf("  T3 tiles in store after test: %zu keys\n", keys.size());
   }

   // ---- T-C: a poster with a vector logo and text, all editable after reopen ----
   {
      using Shape = Scene2DNode::Shape;
      Scene2DNode pic;
      pic.Layers();
      pic.Doc().w = 400;
      pic.Doc().h = 300;
      pic.Commit();
      const int v = pic.AddVectorLayer();
      {
         Shape box;
         box.type = Shape::kRect;
         box.x0 = 20; box.y0 = 20; box.x1 = 160; box.y1 = 160;
         box.radius = 12.0f;
         box.fillC[0] = 1.0f;
         Shape hole;
         hole.type = Shape::kEllipse;
         hole.x0 = 60; hole.y0 = 60; hole.x1 = 120; hole.y1 = 120;
         hole.op = Shape::kSubtract;
         Shape star;
         star.type = Shape::kStar;
         star.x0 = 240; star.y0 = 20; star.x1 = 380; star.y1 = 140;
         star.fillOn = false;
         star.strokeOn = true;
         star.strokeC[1] = 1.0f;
         star.width = 6.0f;
         star.join = 1;
         star.cap = 1;
         star.dash = 14.0f;
         star.gap = 8.0f;
         Shape pen;
         pen.type = Shape::kPath;
         pen.pts = { 20, 280, 20, 280, 60, 240, 160, 280, 120, 240, 160, 280 };
         pen.fillOn = false;
         pen.strokeOn = true;
         pen.width = 4.0f;
         pen.align = 0;
         pic.FindLayer(v)->shapes = { box, hole, star, pen };
         pic.Commit();
      }
      const bool imported = pic.ImportSvg(v, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\">"
                                             "<rect width=\"20\" height=\"20\" fill=\"#0000ff\"/></svg>");
      const int pt = pic.AddTextLayer("INFINITE", 20.0f, 180.0f, 0.0f);
      const int bx = pic.AddTextLayer("a poster set in a box that wraps", 230.0f, 170.0f, 150.0f);
      pic.FindLayer(pt)->fontSize = 40.0f;
      pic.FindLayer(bx)->fontSize = 22.0f;
      pic.Commit();
      Cook(&pic);
      Check("T-C: SVG imports into the vector layer", imported && pic.FindLayer(v)->shapes.size() == 5
                                                        && pic.FindLayer(v)->shapes.back().type == Shape::kSvg);
      Check("T-C: rectangle fills, the subtracted ellipse is a hole", Pixel(&pic, 30, 30)[1] < 0.05f && Pixel(&pic, 90, 90)[1] > 0.95f
                                                                        && Pixel(&pic, 150, 90)[1] < 0.05f);
      Check("T-C: the imported SVG draws centred", Pixel(&pic, 200, 150)[2] > 0.95f && Pixel(&pic, 200, 150)[0] < 0.05f);
      auto Ink = [](Scene2DNode* d, int x0, int y0, int x1, int y1, int ch)
      {
         int n = 0;
         for (int y = y0; y < y1; y += 2)
            for (int x = x0; x < x1; x += 2)
               if (Pixel(d, x, y)[ch] < 0.5f)   // ink on the white page
                  n++;
         return n;
      };
      Check("T-C: the dashed star stroke draws", Ink(&pic, 240, 20, 380, 140, 0) > 20 && Ink(&pic, 240, 20, 380, 140, 1) == 0);
      int tw = 0, th = 0, bw = 0, bh = 0;
      const bool sized = pic.TextSize(*pic.FindLayer(pt), tw, th) && pic.TextSize(*pic.FindLayer(bx), bw, bh);
      printf("  T-C text boxes: point %dx%d, box %dx%d\n", tw, th, bw, bh);
      Check("T-C: point text sizes to its ink, box text keeps its width", sized && tw > 60 && th > 10 && bw == 150 && bh > 40);
      Check("T-C: the text draws", Ink(&pic, 20, 180, 220, 236, 1) > 10 && Ink(&pic, 230, 170, 380, 290, 1) > 10);
      const std::string svg = pic.ToSvg(*pic.FindLayer(v));
      Check("T-C: the layer writes as SVG", svg.find("<svg") != std::string::npos && svg.find("mask") != std::string::npos
                                              && svg.find("stroke-dasharray") != std::string::npos);
      // Reopen: every item and every text field comes back and draws the same.
      Scene2DNode copy;
      copy.SetDocText(pic.DocText());
      Cook(&copy);
      const Layer* cv = copy.FindLayer(v);
      const Layer* cb = copy.FindLayer(bx);
      bool same = cv != nullptr && cb != nullptr && cv->shapes.size() == 5;
      if (same)
      {
         const Layer* ov = pic.FindLayer(v);
         for (size_t i = 0; i < 5; i++)
         {
            const Shape& a = ov->shapes[i];
            const Shape& b = cv->shapes[i];
            same = same && a.type == b.type && a.op == b.op && a.pts == b.pts && a.svg == b.svg && a.dash == b.dash
                   && a.gap == b.gap && a.cap == b.cap && a.join == b.join && a.radius == b.radius && a.strokeOn == b.strokeOn;
         }
         same = same && cb->text == "a poster set in a box that wraps" && cb->boxW == 150.0f && cb->fontSize == 22.0f;
      }
      Check("T-C: reopened, shapes and text are still editable", same);
      bool picture = true;
      for (int y = 5; y < 300; y += 10)
         for (int x = 5; x < 400; x += 10)
         {
            const std::vector<float> p0 = Pixel(&pic, x, y), p1 = Pixel(&copy, x, y);
            for (int c = 0; c < 4; c++)
               picture = picture && std::fabs(p0[c] - p1[c]) < 0.02f;
         }
      Check("T-C: reopened, the picture is the same", picture);
      // A vector mask keeps only the left part of the logo.
      Layer* mv = copy.FindLayer(v);
      Shape keep;
      keep.type = Shape::kRect;
      keep.x0 = 0; keep.y0 = 0; keep.x1 = 50; keep.y1 = 300;
      keep.fillC[0] = keep.fillC[1] = keep.fillC[2] = 1.0f;
      mv->hasVMask = true;
      mv->vmask = { keep };
      copy.Commit();
      Cook(&copy);
      Check("T-C: the vector mask hides outside its shape", Pixel(&copy, 30, 30)[1] < 0.05f && Pixel(&copy, 150, 30)[1] > 0.95f);
      // Editing after reopen redraws: the hole moves.
      copy.FindLayer(v)->vmask.clear();
      copy.FindLayer(v)->hasVMask = false;
      copy.FindLayer(v)->shapes[1].x0 = 100;
      copy.FindLayer(v)->shapes[1].x1 = 150;
      copy.Commit();
      Cook(&copy);
      Check("T-C: an edit after reopen redraws", Pixel(&copy, 70, 90)[1] < 0.05f && Pixel(&copy, 125, 90)[1] > 0.95f);
   }

   // ---- T-D: 3D cards, camera, fx, blend if, distort, symmetry ----
   {
      using Shape = Scene2DNode::Shape;
      auto NewPic = [](Scene2DNode& pic, int w, int h)
      {
         pic.Layers();
         pic.Doc().w = w;
         pic.Doc().h = h;
         pic.Commit();
      };
      // A vector layer of w x h px filled with one colour, centred on (x, y).
      auto Card = [](Scene2DNode& pic, float x, float y, float w, float h, float r, float g, float b)
      {
         const int id = pic.AddVectorLayer();
         Layer* l = pic.FindLayer(id);
         l->pw = (int)w;
         l->ph = (int)h;
         l->x = x;
         l->y = y;
         Shape sh;
         sh.type = Shape::kRect;
         sh.x0 = 0; sh.y0 = 0; sh.x1 = w; sh.y1 = h;
         sh.fillC[0] = r; sh.fillC[1] = g; sh.fillC[2] = b; sh.fillC[3] = 1.0f;
         l->shapes = { sh };
         pic.Commit();
         return id;
      };
      auto Near = [](float a, float b, float tol) { return std::fabs(a - b) <= tol; };

      // T4: a Render 3D output tilted 30 degrees in depth behind a 2D title, no other node.
      {
         GraphNode* cubeGn = SpawnNode("Cube", "3D", 0.0f, 600.0f);
         GraphNode* renderGn = SpawnNode("Render 3D", "3D", 300.0f, 600.0f);
         const uint64_t renderUid = renderGn != nullptr ? renderGn->uid : 0;
         if (cubeGn != nullptr && renderGn != nullptr)
            if (auto* r3 = dynamic_cast<Render3DNode*>(renderGn->node.get()))
               r3->geometry[0] = dynamic_cast<IGeometrySource*>(cubeGn->node.get());
         Scene2DNode pic;
         NewPic(pic, 640, 360);
         Layer& card = pic.AddLayer(Kind::Node, renderUid);
         card.is3d = true;
         card.fit = Scene2DNode::kFitFit;
         card.sx = card.sy = 0.6f;
         card.ry = 30.0f;
         card.z = 150.0f;
         const int cardId = card.id;
         const int title = pic.AddTextLayer("TITLE", 250.0f, 150.0f, 0.0f);
         pic.FindLayer(title)->fontSize = 48.0f;
         pic.FindLayer(title)->textC[1] = 1.0f;   // pure green, nothing a render makes
         pic.Commit();
         Cook(&pic);
         float c[8] = { 0 };
         const bool front = pic.Corners(*pic.FindLayer(cardId), c);
         const float left = c[7] - c[1], right = c[5] - c[3];
         printf("  T4 card corners: left edge %.1f px, right edge %.1f px\n", left, right);
         Check("T4: the tilted card is a trapezoid", front && left > 10.0f && right > 10.0f && std::fabs(left - right) > 8.0f);
         Check("T4: pushed back, it is smaller than flat", std::max(left, right) < 360.0f * 0.6f);
         int ink = 0, drawn = 0;
         for (int y = 150; y < 210; y += 2)
            for (int x = 250; x < 400; x += 2)
            {
               const std::vector<float> px = Pixel(&pic, x, y);
               ink += px[1] > 0.9f && px[0] < 0.1f && px[2] < 0.1f ? 1 : 0;
            }
         for (int y = 40; y < 320; y += 8)
            for (int x = 120; x < 520; x += 8)
               drawn += Pixel(&pic, x, y)[0] < 0.95f ? 1 : 0;
         Check("T4: the Render 3D card draws", drawn > 20);
         Check("T4: the 2D title draws over the card", ink > 20);
         Check("T4: no node was added for it", pic.Layers().size() == 2);
      }

      // The default camera keeps Z = 0 exactly 2D.
      {
         Scene2DNode pic;
         NewPic(pic, 400, 300);
         const int a = Card(pic, 200, 150, 120, 80, 1, 0, 0);
         float flat[8], d3[8];
         pic.Corners(*pic.FindLayer(a), flat);
         pic.FindLayer(a)->is3d = true;
         pic.FindLayer(a)->angle = 0.0f;
         pic.Commit();
         pic.Corners(*pic.FindLayer(a), d3);
         bool same = true;
         for (int i = 0; i < 8; i++)
            same = same && Near(flat[i], d3[i], 0.05f);
         Check("T-D: a 3D layer at Z 0 sits where its 2D self did", same);
         // A Camera node at its defaults with a 50 mm lens, no orbit: the same view.
         GraphNode* camGn = SpawnNode("Camera", "3D", 600.0f, 600.0f);
         if (auto* cam = camGn != nullptr ? dynamic_cast<CameraNode*>(camGn->node.get()) : nullptr)
         {
            cam->azimuth = 0.0f;
            cam->elevation = 0.0f;
            cam->SetFocalLength(50.0f, 400.0f / 300.0f);
            pic.Doc().camUid = camGn->uid;
            pic.Commit();
            float withCam[8];
            pic.Corners(*pic.FindLayer(a), withCam);
            bool camSame = true;
            for (int i = 0; i < 8; i++)
               camSame = camSame && Near(withCam[i], d3[i], 0.5f);
            Check("T-D: a 50 mm Camera node matches the default view", camSame);
            cam->azimuth = 25.0f;
            float turned[8];
            pic.Corners(*pic.FindLayer(a), turned);
            Check("T-D: orbiting the Camera node moves the card", !Near(turned[0], d3[0], 2.0f));
         }
         else
            Check("T-D: a Camera node spawns", false);
      }

      // Intersections: two cards crossing in depth; each shows where it is nearer, whatever the order.
      {
         auto Draw = [&](bool swap, float out[2][3])
         {
            Scene2DNode pic;
            NewPic(pic, 400, 300);
            const int first = Card(pic, 200, 150, 240, 120, swap ? 0.0f : 1.0f, 0, swap ? 1.0f : 0.0f);
            const int second = Card(pic, 200, 150, 240, 120, swap ? 1.0f : 0.0f, 0, swap ? 0.0f : 1.0f);
            const int a = swap ? second : first, b = swap ? first : second;   // a red, b blue
            pic.FindLayer(a)->is3d = pic.FindLayer(b)->is3d = true;
            pic.FindLayer(a)->ry = 40.0f;
            pic.FindLayer(b)->ry = -40.0f;
            pic.Commit();
            Cook(&pic);
            for (int k = 0; k < 2; k++)
            {
               const std::vector<float> px = Pixel(&pic, k == 0 ? 140 : 260, 150);
               for (int i = 0; i < 3; i++)
                  out[k][i] = px[i];
            }
         };
         float p0[2][3], p1[2][3];
         Draw(false, p0);
         Draw(true, p1);
         const bool lr = (p0[0][0] > 0.5f && p0[1][2] > 0.5f) || (p0[0][2] > 0.5f && p0[1][0] > 0.5f);
         Check("T-D: crossing cards each show where they are nearer", lr);
         bool same = true;
         for (int k = 0; k < 2; k++)
            for (int i = 0; i < 3; i++)
               same = same && Near(p0[k][i], p1[k][i], 0.02f);
         Check("T-D: the crossing does not depend on layer order", same);
      }

      // Layer fx: drop shadow, stroke, colour overlay.
      {
         Scene2DNode pic;
         NewPic(pic, 400, 300);
         const int a = Card(pic, 200, 150, 100, 100, 1, 0, 0);
         Layer* l = pic.FindLayer(a);
         l->fx[Scene2DNode::kFxShadow].on = true;
         l->fx[Scene2DNode::kFxShadow].angle = 90.0f;   // light from the top, the shadow falls down
         l->fx[Scene2DNode::kFxShadow].dist = 20.0f;
         l->fx[Scene2DNode::kFxShadow].size = 0.0f;
         l->fx[Scene2DNode::kFxShadow].c[3] = 1.0f;
         l->fx[Scene2DNode::kFxStroke].on = true;
         l->fx[Scene2DNode::kFxStroke].size = 6.0f;
         l->fx[Scene2DNode::kFxStroke].c[1] = 1.0f;   // green, outside
         pic.Commit();
         Cook(&pic);
         Check("T-D: the drop shadow falls below", Pixel(&pic, 200, 212)[0] < 0.1f && Pixel(&pic, 200, 88)[0] > 0.9f);
         Check("T-D: the stroke rings the outside", Pixel(&pic, 253, 150)[1] > 0.5f && Pixel(&pic, 200, 150)[0] > 0.9f);
         l = pic.FindLayer(a);
         l->fx[Scene2DNode::kFxOverlay].on = true;
         l->fx[Scene2DNode::kFxOverlay].c[0] = 0.0f;
         l->fx[Scene2DNode::kFxOverlay].c[2] = 1.0f;
         pic.Commit();
         Cook(&pic);
         Check("T-D: the colour overlay recolours the pixels", Pixel(&pic, 200, 150)[2] > 0.9f && Pixel(&pic, 200, 150)[0] < 0.1f);
      }

      // Blend if: a red layer shows only over the light half of what is below.
      {
         Scene2DNode pic;
         NewPic(pic, 400, 300);
         Card(pic, 100, 150, 200, 300, 0, 0, 0);
         const int top = Card(pic, 200, 150, 400, 300, 1, 0, 0);
         pic.FindLayer(top)->bifU[0] = pic.FindLayer(top)->bifU[1] = 0.5f;
         pic.Commit();
         Cook(&pic);
         Check("T-D: blend if hides the layer over dark", Pixel(&pic, 100, 150)[0] < 0.05f && Pixel(&pic, 300, 150)[0] > 0.95f
                                                           && Pixel(&pic, 300, 150)[1] < 0.05f);
      }

      // Distort: the top-right corner pulled out; the picture follows.
      {
         Scene2DNode pic;
         NewPic(pic, 400, 300);
         const int a = Card(pic, 200, 150, 200, 100, 1, 0, 0);
         Layer* l = pic.FindLayer(a);
         l->warpOn = true;
         l->warp[2] = 80.0f;
         l->warp[3] = -40.0f;
         pic.Commit();
         Cook(&pic);
         float c[8];
         pic.Corners(*pic.FindLayer(a), c);
         Check("T-D: distort moves the corner", Near(c[2], 380.0f, 0.5f) && Near(c[3], 60.0f, 0.5f) && Near(c[0], 100.0f, 0.5f));
         Check("T-D: the pixels follow the corner", Pixel(&pic, 340, 90)[1] < 0.05f && Pixel(&pic, 120, 190)[1] < 0.05f
                                                       && Pixel(&pic, 120, 60)[1] > 0.95f);
      }

      // Symmetry: a stroke on the left lands on the right too.
      {
         Scene2DNode pic;
         NewPic(pic, 400, 300);
         const int id = pic.AddPaintLayer();
         pic.Doc().symmetry = 1;
         pic.Commit();
         BrushEngine::Brush br = BrushEngine::Presets()[1].brush;
         br.size = 16.0f;
         br.color[0] = br.color[1] = br.color[2] = 0.0f;
         pic.BeginPaint(id, 0, br, 60.0f, 100.0f, 1.0f);
         pic.PaintTo(60.0f, 200.0f, 1.0f);
         pic.EndPaint();
         Cook(&pic);
         Check("T-D: symmetry mirrors the stroke", Pixel(&pic, 60, 150)[1] < 0.2f && Pixel(&pic, 340, 150)[1] < 0.2f
                                                      && Pixel(&pic, 200, 150)[1] > 0.9f);
         pic.Doc().symmetry = 4;
         pic.Doc().symN = 4;
         const auto pts = pic.SymmetryPoints(300.0f, 150.0f);
         Check("T-D: radial symmetry makes N copies", pts.size() == 4 && Near(pts[2].first, 100.0f, 0.01f) && Near(pts[2].second, 150.0f, 0.01f));
      }

      // Save: every new line round-trips.
      {
         Scene2DNode pic;
         NewPic(pic, 400, 300);
         const int a = Card(pic, 200, 150, 100, 100, 1, 0, 0);
         Layer* l = pic.FindLayer(a);
         l->is3d = true; l->z = 12.5f; l->az = 3; l->rx = 10; l->ry = -20; l->orient[2] = 45; l->collapse = true;
         l->warpOn = true; l->warp[5] = 7;
         l->fx[Scene2DNode::kFxGlow].on = true; l->fx[Scene2DNode::kFxGlow].size = 22;
         l->bifT[1] = 0.25f; l->bifU[2] = 0.75f;
         pic.Doc().camUid = 77; pic.Doc().symmetry = 4; pic.Doc().symN = 8;
         pic.Commit();
         Scene2DNode copy;
         copy.SetDocText(pic.DocText());
         const Layer& c = copy.Layers()[0];
         Check("T-D: 3D, distort, fx, blend if, camera and symmetry save",
               c.is3d && c.z == 12.5f && c.az == 3 && c.rx == 10 && c.ry == -20 && c.orient[2] == 45 && c.collapse && c.warpOn
                  && c.warp[5] == 7 && c.fx[Scene2DNode::kFxGlow].on && c.fx[Scene2DNode::kFxGlow].size == 22
                  && !c.fx[Scene2DNode::kFxShadow].on && c.bifT[1] == 0.25f && c.bifU[2] == 0.75f && copy.Doc().camUid == 77
                  && copy.Doc().symmetry == 4 && copy.Doc().symN == 8);
      }

      // M3: 20 layers at 1080p (10 node, 6 paint, 4 vector/text) composite in under 4 ms.
      {
         Scene2DNode pic;
         NewPic(pic, 1920, 1080);
         for (int i = 0; i < 10; i++)
         {
            Layer& l = pic.AddLayer(Kind::Node, FindNodeByIndex(i % 2 == 0 ? shape : text)->uid);
            l.opacity = 0.8f;
            l.blend = i % 3;
         }
         for (int i = 0; i < 6; i++)
         {
            const int id = pic.AddPaintLayer();
            BrushEngine::Brush br = BrushEngine::Presets()[1].brush;
            br.size = 60.0f;
            pic.BeginPaint(id, 0, br, 100.0f + i * 200.0f, 200.0f, 1.0f);
            pic.PaintTo(300.0f + i * 200.0f, 800.0f, 1.0f);
            pic.EndPaint();
         }
         Card(pic, 500, 500, 600, 400, 0.2f, 0.4f, 0.8f);
         Card(pic, 1400, 500, 600, 400, 0.8f, 0.4f, 0.2f);
         pic.AddTextLayer("INFINITE", 200.0f, 900.0f, 0.0f);
         pic.AddTextLayer("a 2D view", 1200.0f, 900.0f, 0.0f);
         Cook(&pic);
         glFinish();
         std::vector<double> ms;
         using Clock = std::chrono::steady_clock;
         for (int f = 0; f < 40; f++)
         {
            pic.Layers()[0].opacity = 0.8f + 0.001f * (f % 2);
            pic.Commit();
            glFinish();
            const auto t0 = Clock::now();
            Cook(&pic);
            glFinish();
            ms.push_back(std::chrono::duration<double, std::milli>(Clock::now() - t0).count());
         }
         std::sort(ms.begin(), ms.end());
         const double median = ms[ms.size() / 2];
         printf("  M3 20 layers at 1080p: median %.2f ms (cook + finish, %d layers)\n", median, (int)pic.Layers().size());
         Check("M3: 20 layers at 1080p composite in under 4 ms", pic.Layers().size() == 20 && median < 4.0);

         // Below cache: editing an upper layer redraws only it and what sits above, and the picture matches a
         // full redraw exactly.
         auto timeEdit = [&](int index)
         {
            std::vector<double> t;
            for (int f = 0; f < 30; f++)
            {
               pic.Layers()[index].opacity = 0.7f + 0.001f * (f % 2);
               pic.Commit();
               glFinish();
               const auto t0 = Clock::now();
               Cook(&pic);
               glFinish();
               t.push_back(std::chrono::duration<double, std::milli>(Clock::now() - t0).count());
            }
            std::sort(t.begin(), t.end());
            return t[t.size() / 2];
         };
         const double topMs = timeEdit(19), midMs = timeEdit(8);
         printf("  M3b edit: top layer %.2f ms, middle layer %.2f ms (bottom %.2f ms)\n", topMs, midMs, median);
         auto grab = [&]()
         {
            std::vector<float> px((size_t)1920 * 1080 * 4);
            glBindTexture(GL_TEXTURE_2D, pic.GetOutputTexture());
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, px.data());
            glBindTexture(GL_TEXTURE_2D, 0);
            return px;
         };
         pic.Layers()[12].opacity = 0.55f;
         pic.Commit();
         Cook(&pic);
         pic.Layers()[12].opacity = 0.5f;
         pic.Commit();
         Cook(&pic);
         const std::vector<float> cached = grab();
         Scene2DNode::sBelowCache = false;
         pic.Layers()[12].opacity = 0.55f;
         pic.Commit();
         Cook(&pic);
         pic.Layers()[12].opacity = 0.5f;
         pic.Commit();
         Cook(&pic);
         const std::vector<float> full = grab();
         // Bounded passes against full-frame ones: the same picture (full passes round the untouched pixels
         // through the blend; bounded ones leave them as they are).
         gScene2DBounds = false;
         pic.Layers()[12].opacity = 0.55f;
         pic.Commit();
         Cook(&pic);
         pic.Layers()[12].opacity = 0.5f;
         pic.Commit();
         Cook(&pic);
         const std::vector<float> unbounded = grab();
         gScene2DBounds = true;
         Scene2DNode::sBelowCache = true;
         double worst = 0;
         for (size_t k = 0; k < full.size(); k += 4)
         {
            const float a0 = full[k + 3], a1 = unbounded[k + 3];
            worst = std::max(worst, (double)std::fabs(a0 - a1));
            for (int c = 0; c < 3; c++)
               worst = std::max(worst, (double)std::fabs(full[k + c] * a0 - unbounded[k + c] * a1));
         }
         printf("  M3b bounded vs full passes: worst difference %.6f\n", worst);
         Check("M3b: bounded passes match full-frame passes", worst < 1e-3);
         Check("M3b: below cache matches a full redraw exactly", cached == full);
         Check("M3b: an upper-layer edit is faster than a bottom one", topMs < median && midMs < median);
      }
   }

   // ---- M4: a brush stroke at 1080p stays inside one frame ----
   {
      Scene2DNode pic;
      pic.Layers();
      pic.Doc().w = 1920;
      pic.Doc().h = 1080;
      pic.Commit();
      const int id = pic.AddPaintLayer();
      BrushEngine::Brush br = BrushEngine::Presets()[1].brush;
      br.size = 80.0f;
      Cook(&pic);
      using Clock = std::chrono::steady_clock;
      std::vector<double> ms;
      pic.BeginPaint(id, 0, br, 100.0f, 540.0f, 1.0f);
      for (int f = 0; f < 120; f++)
      {
         const auto t0 = Clock::now();
         // Each frame the pen moves ~14 px, as a fast hand does at 60 fps.
         const float x = 100.0f + f * 14.0f, y = 540.0f + 300.0f * std::sin(f * 0.08f);
         pic.PaintTo(x, y, 0.5f + 0.5f * std::sin(f * 0.2f));
         Cook(&pic);
         glFinish();
         ms.push_back(std::chrono::duration<double, std::milli>(Clock::now() - t0).count());
      }
      const auto e0 = Clock::now();
      pic.EndPaint();
      glFinish();
      const double endMs = std::chrono::duration<double, std::milli>(Clock::now() - e0).count();
      std::vector<double> sorted = ms;
      std::sort(sorted.begin(), sorted.end());
      const double median = sorted[sorted.size() / 2], p95 = sorted[sorted.size() * 95 / 100];
      printf("  M4 1080p stroke: median %.2f ms, p95 %.2f ms, end %.2f ms\n", median, p95, endMs);
      Check("M4: a 1080p brush frame is under 16.7 ms", p95 < 16.7);
   }

   // M5: a setting change (blend, opacity, move) on a doc with a 3D card, a cube layer and a noise layer must
   // land the next frame: undo checkpoint + commit + cook, each timed.
   {
      using Clock = std::chrono::steady_clock;
      NewPatch();
      GraphNode* cubeGn = SpawnNode("Cube", "3D", 0.0f, 0.0f);
      GraphNode* renderGn = SpawnNode("Render 3D", "3D", 300.0f, 0.0f);
      GraphNode* noiseGn = SpawnNode("Noise", "Source", 0.0f, 300.0f);
      if (cubeGn != nullptr && renderGn != nullptr)
         if (auto* r3 = dynamic_cast<Render3DNode*>(renderGn->node.get()))
            r3->geometry[0] = dynamic_cast<IGeometrySource*>(cubeGn->node.get());
      Scene2DNode pic;
      pic.SetDocText("doc 1920 1080 0 0 0 0 0 0 0 0 1 32 4 30 10 1 1\n");
      pic.AddLayer(Kind::Node, noiseGn != nullptr ? noiseGn->uid : 0);
      Layer& card = pic.AddLayer(Kind::Node, renderGn != nullptr ? renderGn->uid : 0);
      card.is3d = true;
      card.ry = 25.0f;
      pic.AddLayer(Kind::Node, cubeGn != nullptr ? cubeGn->uid : 0);
      pic.Commit();
      Cook(&pic);
      glFinish();
      double undoMax = 0, commitMax = 0, cookMax = 0;
      for (int i = 0; i < 24; i++)
      {
         Layer& l = pic.Layers()[i % 3];
         auto t0 = Clock::now();
         PushUndoCheckpoint("Blend mode");
         auto t1 = Clock::now();
         if (i % 3 == 0) l.blend = (l.blend + 1) % 12;
         else if (i % 3 == 1) l.opacity = 0.5f + 0.02f * i;
         else l.x += 3.0f;
         pic.Commit();
         auto t2 = Clock::now();
         Cook(&pic);
         glFinish();
         auto t3 = Clock::now();
         auto ms = [](Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
         undoMax = std::max(undoMax, ms(t0, t1));
         commitMax = std::max(commitMax, ms(t1, t2));
         cookMax = std::max(cookMax, ms(t2, t3));
      }
      printf("  M5 setting change: undo max %.2f ms, commit max %.2f ms, cook max %.2f ms\n", undoMax, commitMax, cookMax);
      Check("M5: a setting change lands within a frame or two", undoMax + commitMax + cookMax < 50.0);
   }

   // ---- H2 procedural layers: a Field Pixel is a layer and a mask at any document size; a Field Modifier is a 3D card ----
   {
      GraphNode* fp = SpawnNode("FieldPixel", "Source", 0.0f, 600.0f);
      const uint64_t fpUid = fp != nullptr ? fp->uid : 0;
      Check("H2: a Field Pixel can be a layer", fp != nullptr && View2DCanAdd(*fp));
      GraphNode* fm = SpawnNode("Field Modifier", "3D", 0.0f, 800.0f);
      Check("H2: a Field Modifier can be a layer", fm != nullptr && View2DCanAdd(*fm));
      for (int size : { 64, 1920 })
      {
         Scene2DNode pic;
         pic.Layers();
         pic.Doc().w = size;
         pic.Doc().h = size;
         pic.Doc().background = 1;
         pic.AddLayer(Kind::Node, fpUid, 0);
         pic.Commit();
         if (GraphNode* g = FindNodeByUid(fpUid))
            g->node->CookIfNeeded(++sFrame);
         Cook(&pic);
         std::vector<uint8_t> src;
         int w = 0, h = 0;
         const bool read = pic.ReadOutput(src, w, h);
         long lit = 0;
         for (size_t k = 0; k + 3 < src.size(); k += 4)
            lit += src[k] + src[k + 1] + src[k + 2] > 30 ? 1 : 0;
         printf("  H2 Field Pixel layer at %d: %ld of %d px lit\n", size, lit, w * h);
         Check(size == 64 ? "H2: a Field Pixel layer draws (64 px)" : "H2: a Field Pixel layer draws (1920 px)",
               read && w == size && lit > (long)w * h / 20);
         // As a mask: a white fill masked by the Field Pixel differs from the bare fill.
         Scene2DNode m;
         m.Layers();
         m.Doc().w = size;
         m.Doc().h = size;
         m.Doc().background = 1;
         Layer& f = m.AddLayer(Kind::Fill, 0);
         for (int c = 0; c < 4; c++)
            f.colorA[c] = 1.0f;
         m.Commit();
         Cook(&m);
         std::vector<uint8_t> bare, masked;
         m.ReadOutput(bare, w, h);
         m.Layers()[0].maskUid = fpUid;
         m.Layers()[0].maskOn = true;
         m.Commit();
         Cook(&m);
         m.ReadOutput(masked, w, h);
         Check(size == 64 ? "H2: a Field Pixel masks a layer (64 px)" : "H2: a Field Pixel masks a layer (1920 px)",
               !bare.empty() && bare.size() == masked.size() && bare != masked);
      }
   }

   // ---- H3 recipes: an early setting changed after ten later edits still re-renders, and going back is lossless ----
   {
      Scene2DNode pic;
      pic.Layers();
      pic.Doc().w = 96;
      pic.Doc().h = 96;
      pic.Doc().background = 1;
      Layer& base = pic.AddLayer(Kind::Fill, 0);
      base.colorA[0] = 1.0f; base.colorA[1] = 0.2f; base.colorA[2] = 0.1f; base.colorA[3] = 1.0f;
      const int baseId = base.id;
      pic.Commit();
      Cook(&pic);
      std::vector<uint8_t> first;
      int w = 0, h = 0;
      pic.ReadOutput(first, w, h);
      // Ten later edits: adjustments, a text, a second fill at half opacity, moves, blend changes.
      const int adj = pic.AddLayer(Kind::Adjust, 0).id;
      pic.FindLayer(adj)->adjType = Scene2DNode::kAdjPosterize;
      pic.FindLayer(adj)->adjAmt = 6.0f;
      const int txt = pic.AddTextLayer("Recipe", 10.0f, 10.0f, 80.0f);
      Layer& over = pic.AddLayer(Kind::Fill, 0);
      over.colorA[2] = 1.0f; over.colorA[3] = 1.0f; over.opacity = 0.3f;
      const int overId = over.id;
      pic.FindLayer(txt)->x += 4.0f;
      pic.FindLayer(overId)->blend = 2;
      pic.FindLayer(adj)->adjAmt = 4.0f;
      pic.FindLayer(txt)->fontSize = 18.0f;
      pic.FindLayer(overId)->opacity = 0.25f;
      pic.FindLayer(txt)->angle = 5.0f;
      pic.Commit();
      Cook(&pic);
      std::vector<uint8_t> before;
      pic.ReadOutput(before, w, h);
      // Change the first layer's colour: everything above re-renders over it.
      pic.FindLayer(baseId)->colorA[1] = 0.9f;
      pic.Commit();
      Cook(&pic);
      std::vector<uint8_t> changed;
      pic.ReadOutput(changed, w, h);
      Check("H3: an early setting re-renders after ten later edits", !before.empty() && before != changed);
      pic.FindLayer(baseId)->colorA[1] = 0.2f;
      pic.Commit();
      Cook(&pic);
      std::vector<uint8_t> back;
      pic.ReadOutput(back, w, h);
      Check("H3: setting it back gives the same pixels (nothing baked)", back == before);
      Check("H3: the first state was not the last (the edits show)", first != before);
   }

   // The panel checks get a fresh document of their own: NewPatch frees the one above.
   NewPatch();
   gView2DOpen = false;
   if (GraphNode* seed = SpawnNode("Noise", "Source", 0.0f, 0.0f))
      View2DAddNode(*seed);
   doc = Doc();
   Check("a fresh document for the panel checks", doc != nullptr);

   // ---- H1 motion-first layers: a layer number bound to a modulator saves its slot and moves the picture ----
   if (doc != nullptr)
   {
      GraphNode* dg = nullptr;
      for (GraphNode& g : gNodes)
         if (g.node.get() == doc)
            dg = &g;
      doc->Doc().background = 1;   // black
      Layer& f = doc->AddLayer(Kind::Fill, 0);
      f.colorA[0] = 1.0f; f.colorA[1] = 0.0f; f.colorA[2] = 0.0f; f.colorA[3] = 1.0f;
      const int fid = f.id;
      for (Layer& o : doc->Layers())
         o.visible = o.id == fid;   // the seed layer under it would show through
      doc->Commit();
      const int slot = doc->SlotFor(fid, Scene2DNode::kLfOpacity, true);
      Check("H1: a layer number gets a slot", slot >= Scene2DNode::kSlotBase && doc->SlotFor(fid, Scene2DNode::kLfOpacity, false) == slot);
      Check("H1: another number gets the next slot", doc->SlotFor(fid, Scene2DNode::kLfX, true) == slot + 1);
      Check("H1: an unbound number has none", doc->SlotFor(fid, Scene2DNode::kLfY, false) == -1);
      {
         Scene2DNode copy;
         copy.SetDocText(doc->DocText());
         Check("H1: slots save", copy.Doc().slots.size() == 2 && copy.SlotFor(fid, Scene2DNode::kLfOpacity, false) == slot);
      }
      Check("H1: every field has a name and a pointer", [&]() {
         Layer& l = *doc->FindLayer(fid);
         for (int k = 0; k < Scene2DNode::kLfCount; k++)
            if (Scene2DNode::LayerFieldName(k)[0] == 0 || Scene2DNode::LayerFieldPtr(l, k) == nullptr)
               return false;
         return true;
      }());
      const int cx = doc->Doc().w / 2, cy = doc->Doc().h / 2;
      Cook(doc);
      const float lit = Pixel(doc, cx, cy)[0];
      Modulation::Instance().ClearFrameParams();
      if (dg != nullptr)
         RegisterScene2DParams(*dg);
      bool registered = false;
      for (const ParamRef& r : Modulation::Instance().FrameParams())
         registered = registered || (dg != nullptr && r.nodeIndex == dg->index && r.paramIndex == slot && r.value == &doc->FindLayer(fid)->opacity);
      Check("H1: a 2D-View-only document registers its bound numbers", registered);
      if (dg != nullptr)
         Modulation::Instance().SetExpression(dg->index, slot, "0");
      ApplyModulationAndPalette(++sFrame);
      Cook(doc);
      const float dark = Pixel(doc, cx, cy)[0];
      printf("  H1 red before %.3f, after the expression drives opacity to 0: %.3f (opacity %.3f)\n", lit, dark, doc->FindLayer(fid)->opacity);
      Check("H1: an expression drives the layer's opacity in the picture", lit > 0.9f && dark < 0.05f);
      if (dg != nullptr)
         Modulation::Instance().ClearExpression(dg->index, slot);
      Modulation::Instance().ClearFrameParams();
      doc->RemoveLayer(fid);
      for (Layer& o : doc->Layers())
         o.visible = true;
      doc->Doc().slots.clear();
      doc->Doc().background = 0;
      doc->Commit();
   }
   if (doc != nullptr && View2DPanelSelfCheck(doc) > 0)
      ok = false;
   printf("VIEW2DTEST %s\n", ok ? "OK" : "FAIL");
}

} // namespace app
