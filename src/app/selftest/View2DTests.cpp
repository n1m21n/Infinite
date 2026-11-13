// 2D View self-test (INFINITE_VIEW2DTEST): the Scene 2D document model, its save
// text, the composite it draws, and the plan's two Block A tasks (T1 poster, T2
// overlay) built from node and fill layers only. Plan: internal/plans/2d-view.
#include "app/AppShared.h"
#include "nodes/Scene2DNode.h"
#include "core/TileStore.h"
#include "gl3.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace app
{
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

   NewPatch();
   gView2DOpen = false;
   printf("VIEW2DTEST %s\n", ok ? "OK" : "FAIL");
}

} // namespace app
