// 2D View self-test (INFINITE_VIEW2DTEST): the Scene 2D document model, its save
// text, the composite it draws, and the plan's two Block A tasks (T1 poster, T2
// overlay) built from node and fill layers only. Plan: internal/plans/2d-view.
#include <cstdlib>
#include "app/AppShared.h"
#include "nodes/Geometry3DNodes.h"
#include "nodes/Scene2DNode.h"
#include "nodes/SceneNodes.h"
#include "core/TileStore.h"
#include "gl3.h"
#include "stb_image_write.h"

extern bool gScene2DBounds;   // Scene2DNode.cpp: bounded layer passes

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <cstdio>
#include <cstring>
#include <sstream>
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

namespace
{
   bool sDockOk = true;
}

// Sets each of the five docks and reads the rect the view was drawn in, two frames later.
bool View2DDockTestStep(int frameId)
{
   static const char* kNames[5] = { "bottom", "right", "left", "top", "fill" };
   static const int kOrder[5] = { 0, 3, 1, 2, 4 };
   static float sTopY = 0.0f;
   static int sStep = -1, sSetAt = 0;
   auto Check = [&](const std::string& label, bool pass)
   {
      printf("  [%s] dock: %s\n", pass ? "pass" : "FAIL", label.c_str());
      if (!pass)
         sDockOk = false;
   };
   if (sStep >= 5)
      return true;
   if (sStep < 0)
   {
      gView2DOpen = true;
      gView3DOpen = gArrangePanelOpen = gSessionPanelOpen = gSampleStudioOpen = gPianoRollOpen = false;
      gViewportPanelOpen = gModMatrixOpen = gPerfPanelOpen = gTerminalOpen = gHistoryOpen = false;
      gView2DHeight = 400.0f;
      gView2DWidth = 500.0f;
      sStep = 0;
      SetViewDock(2, kOrder[0]);
      sSetAt = frameId;
      return false;
   }
   if (frameId < sSetAt + 3)
      return false;
   const int dock = kOrder[sStep];
   const ImVec2 mn = gView2DRectMin, mx = gView2DRectMax;
   const ImVec2 disp = ImGui::GetIO().DisplaySize;
   const float w = mx.x - mn.x, h = mx.y - mn.y;
   printf("  dock %s: rect %.0f,%.0f .. %.0f,%.0f in a %.0f x %.0f window\n", kNames[dock], mn.x, mn.y, mx.x, mx.y, disp.x, disp.y);
   switch (dock)
   {
      case 0: Check("bottom sits on the window's bottom edge at its height", std::fabs(mx.y - disp.y) < 2.0f && std::fabs(h - 400.0f) < 2.0f && w > disp.x * 0.9f); break;
      case 3:
         sTopY = mn.y;
         Check("top sits under the menu bar at its height", mn.y < 80.0f && std::fabs(h - 400.0f) < 2.0f && w > disp.x * 0.9f);
         break;
      case 1: Check("right is a column at its width, right of the canvas", std::fabs(w - 500.0f) < 2.0f && mn.x > gGraphScreenTL.x + 100.0f && std::fabs(mn.y - sTopY) < 2.0f); break;
      case 2: Check("left is a column at its width, on the window's left edge", std::fabs(w - 500.0f) < 2.0f && mn.x < 2.0f && gGraphScreenTL.x >= mx.x - 1.0f); break;
      default:
         Check("fill is the window less the menu bar", std::fabs(mn.y - sTopY) < 2.0f && std::fabs(mx.y - disp.y) < 2.0f && w > disp.x * 0.9f);
         Check("fill hides the canvas below the window", gGraphScreenTL.y > disp.y);
         break;
   }
   if (++sStep < 5)
   {
      SetViewDock(2, kOrder[sStep]);
      sSetAt = frameId;
      return false;
   }
   // Fill remembers where it came from; one view fills at a time; the setting survives its save text.
   Check("fill remembers the dock it came from", gView2DDock == 4 && gView2DDockBack == 2);
   SetViewDock(3, 4);
   Check("a second view on Fill sends the first back", gView3DDock == 4 && gView2DDock == 2);
   ViewDockToggleFill(3);
   Check("the toggle leaves Fill for the old dock", gView3DDock == 0);
   SetViewDock(2, 4);
   gView2DWidth = 777.0f;
   std::ostringstream saved;
   ViewDockWrite(saved);
   gView2DDock = 0; gView2DDockBack = 0; gView2DWidth = 400.0f;
   std::istringstream in(saved.str());
   for (std::string line; std::getline(in, line);)
   {
      const size_t eq = line.find('=');
      if (eq != std::string::npos)
         ViewDockReadKey(line.substr(0, eq), line.substr(eq + 1));
   }
   Check("dock, width and the way back survive save and load", gView2DDock == 4 && gView2DDockBack == 2 && gView2DWidth == 777.0f);
   SetViewDock(2, 0);
   gView3DOpen = false;
   return true;
}

void RunView2DTest()
{
   bool ok = sDockOk;
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

   // ---- S2 tool settings: each one changes what a stroke leaves ----
   {
      Scene2DNode pic;
      pic.Layers();
      pic.Doc().w = 300;
      pic.Doc().h = 200;
      pic.Doc().background = 1;   // black
      pic.Commit();
      BrushEngine::Brush hard = BrushEngine::Presets()[5].brush;   // Hard round
      hard.size = 20.0f;
      hard.pressureSize = false;
      const auto Line = [&](int id, float x0, float x1, float y, const BrushEngine::Brush& use)
      {
         pic.BeginPaint(id, 0, use, x0, y, 1.0f);
         for (float x = x0; x <= x1; x += 7.0f)
            pic.PaintTo(x, y, 1.0f);
         pic.EndPaint();
      };
      const auto Colour = [](BrushEngine::Brush b, float r, float g, float bl) { b.color[0] = r; b.color[1] = g; b.color[2] = bl; return b; };
      const auto Fresh = [&]() { while (!pic.Layers().empty()) pic.Layers().pop_back(); pic.Commit(); return pic.AddPaintLayer(); };

      // Mode: multiply and screen mix with what the layer holds; behind paints only where it is empty.
      int L = Fresh();
      Line(L, 20.0f, 280.0f, 100.0f, Colour(hard, 1, 0, 0));
      BrushEngine::Brush m = Colour(hard, 0, 1, 0);
      m.mode = BrushEngine::kMultiply;
      Line(L, 20.0f, 280.0f, 100.0f, m);
      Cook(&pic);
      Check("S2: multiply green on red leaves black", Pixel(&pic, 150, 100)[0] < 0.05f && Pixel(&pic, 150, 100)[1] < 0.05f);
      L = Fresh();
      Line(L, 20.0f, 280.0f, 100.0f, Colour(hard, 1, 0, 0));
      m.mode = BrushEngine::kScreen;
      Line(L, 20.0f, 280.0f, 100.0f, m);
      Cook(&pic);
      Check("S2: screen green on red makes yellow", Pixel(&pic, 150, 100)[0] > 0.9f && Pixel(&pic, 150, 100)[1] > 0.9f);
      L = Fresh();
      Line(L, 20.0f, 280.0f, 100.0f, Colour(hard, 1, 0, 0));
      BrushEngine::Brush behind = Colour(hard, 0, 0, 1);
      behind.mode = BrushEngine::kBehind;
      pic.BeginPaint(L, 0, behind, 150.0f, 40.0f, 1.0f);
      for (float y = 40.0f; y <= 160.0f; y += 5.0f)
         pic.PaintTo(150.0f, y, 1.0f);
      pic.EndPaint();
      Cook(&pic);
      Check("S2: behind keeps the red, fills the empty", Pixel(&pic, 150, 100)[0] > 0.9f && Pixel(&pic, 150, 100)[2] < 0.05f
                                                          && Pixel(&pic, 150, 50)[2] > 0.9f);

      // Smudge drags the red past where it ended.
      L = Fresh();
      Line(L, 20.0f, 150.0f, 100.0f, Colour(hard, 1, 0, 0));
      Cook(&pic);
      const float before = Pixel(&pic, 185, 100)[0];
      BrushEngine::Brush smudge = hard;
      smudge.mode = BrushEngine::kSmudge;
      smudge.spacing = 0.05f;
      Line(L, 140.0f, 260.0f, 100.0f, smudge);
      Cook(&pic);
      Check("S2: smudge drags the paint along", before < 0.02f && Pixel(&pic, 185, 100)[0] > 0.3f);

      // Taper in and out: the ends of the stroke are thin.
      L = Fresh();
      Line(L, 20.0f, 280.0f, 100.0f, Colour(hard, 1, 1, 1));
      Cook(&pic);
      const float fullIn = Pixel(&pic, 26, 107)[0], fullOut = Pixel(&pic, 268, 107)[0];
      L = Fresh();
      BrushEngine::Brush taper = Colour(hard, 1, 1, 1);
      taper.taperIn = taper.taperOut = 100.0f;
      Line(L, 20.0f, 280.0f, 100.0f, taper);
      Cook(&pic);
      Check("S2: taper in thins the start", fullIn > 0.9f && Pixel(&pic, 26, 107)[0] < 0.1f && Pixel(&pic, 150, 107)[0] > 0.9f);
      Check("S2: taper out thins the end", fullOut > 0.9f && Pixel(&pic, 268, 107)[0] < 0.1f);

      // Jitter: dabs set apart, each one's size, strength and colour drawn afresh.
      BrushEngine::Brush apart = Colour(hard, 1, 0, 0);
      apart.spacing = 1.5f;
      const auto Dabs = [&](const BrushEngine::Brush& b, int& smallDabs, float& minR, float& maxGB)
      {
         const int id = Fresh();
         Line(id, 20.0f, 280.0f, 100.0f, b);
         Cook(&pic);
         smallDabs = 0;
         minR = 1.0f;
         maxGB = 0.0f;
         for (int x = 20; x <= 260; x += 30)   // dab centres
         {
            const std::vector<float> c = Pixel(&pic, x, 100), e = Pixel(&pic, x, 107);
            smallDabs += e[0] + e[1] + e[2] < 0.3f ? 1 : 0;
            minR = std::min(minR, std::max(c[0], std::max(c[1], c[2])));
            maxGB = std::max(maxGB, std::max(c[1], c[2]));
         }
      };
      int small0 = 0, small1 = 0, unusedI = 0;
      float minR0 = 0, minR1 = 0, gb0 = 0, gb1 = 0, unusedF = 0;
      Dabs(apart, small0, minR0, gb0);
      BrushEngine::Brush j = apart;
      j.sizeJitter = 1.0f;
      Dabs(j, small1, unusedF, unusedF);
      Check("S2: size jitter shrinks some dabs", small0 == 0 && small1 > 0);
      j = apart;
      j.opacityJitter = 1.0f;
      Dabs(j, unusedI, minR1, unusedF);
      Check("S2: opacity jitter fades some dabs", minR0 > 0.9f && minR1 < 0.8f);
      j = apart;
      j.hueJitter = 1.0f;
      Dabs(j, unusedI, unusedF, gb1);
      Check("S2: hue jitter turns some dabs off red", gb0 < 0.05f && gb1 > 0.2f);

      // Dodge range: shadows lift the dark line only, highlights the light line only.
      const auto Greys = [&]()
      {
         const int id = Fresh();
         Line(id, 20.0f, 280.0f, 60.0f, Colour(hard, 0.2f, 0.2f, 0.2f));
         Line(id, 20.0f, 280.0f, 140.0f, Colour(hard, 0.8f, 0.8f, 0.8f));
         return id;
      };
      L = Greys();
      Cook(&pic);
      const float dark0 = Pixel(&pic, 150, 60)[0], light0 = Pixel(&pic, 150, 140)[0];
      BrushEngine::Brush dodge = hard;
      dodge.mode = BrushEngine::kDodge;
      dodge.size = 200.0f;
      dodge.range = 0;
      pic.BeginPaint(L, 0, dodge, 150.0f, 100.0f, 1.0f);
      pic.EndPaint();
      Cook(&pic);
      const float darkS = Pixel(&pic, 150, 60)[0], lightS = Pixel(&pic, 150, 140)[0];
      L = Greys();
      dodge.range = 2;
      pic.BeginPaint(L, 0, dodge, 150.0f, 100.0f, 1.0f);
      pic.EndPaint();
      Cook(&pic);
      const float darkH = Pixel(&pic, 150, 60)[0], lightH = Pixel(&pic, 150, 140)[0];
      printf("  dodge: dark %.3f -> shadows %.3f highlights %.3f, light %.3f -> %.3f %.3f\n", dark0, darkS, darkH, light0, lightS, lightH);
      Check("S2: dodge shadows lifts the dark, not the light", darkS > dark0 + 0.04f && std::fabs(lightS - light0) < 0.01f);
      Check("S2: dodge highlights lifts the light, not the dark", lightH > light0 + 0.02f && std::fabs(darkH - dark0) < 0.01f);

      // Clone all layers: an empty layer copies red from the layer under it.
      L = Fresh();
      Line(L, 20.0f, 280.0f, 50.0f, Colour(hard, 1, 0, 0));
      const int top = pic.AddPaintLayer(L);
      BrushEngine::Brush clone = hard;
      clone.mode = BrushEngine::kClone;
      clone.opacity = 1.0f;
      clone.sourceOffset[0] = 0.0f;
      clone.sourceOffset[1] = -100.0f;   // copy from 100 px above
      Cook(&pic);
      Line(top, 60.0f, 240.0f, 150.0f, clone);
      Cook(&pic);
      const float own = Pixel(&pic, 150, 150)[0];
      clone.sampleAll = true;
      Line(top, 60.0f, 240.0f, 150.0f, clone);
      Cook(&pic);
      Check("S2: clone all layers copies from the layer below", own < 0.02f && Pixel(&pic, 150, 150)[0] > 0.9f);

      // Gradient kinds and opacity, black to white along a drag from (50,100) to (250,100).
      const float k0[4] = { 0, 0, 0, 1 }, k1[4] = { 1, 1, 1, 1 };
      float g[6][2];
      for (int kind = 1; kind <= 5; kind++)
      {
         L = Fresh();
         pic.Gradient(L, 50.0f, 100.0f, 250.0f, 100.0f, k0, k1, kind);
         Cook(&pic);
         g[kind][0] = Pixel(&pic, 20, 100)[0];
         g[kind][1] = Pixel(&pic, 120, 170)[0];
      }
      L = Fresh();
      pic.Gradient(L, 50.0f, 100.0f, 250.0f, 100.0f, k0, k1, 3);
      Cook(&pic);
      const float above = Pixel(&pic, 150, 95)[0], below = Pixel(&pic, 150, 105)[0];
      Check("S2: reflected mirrors before the start", g[1][0] < 0.02f && g[4][0] > 0.1f);
      Check("S2: diamond reaches further on the diagonal than radial", g[5][1] > g[2][1] + 0.05f);
      Check("S2: angle wraps round the start", above > 0.8f && below < 0.2f);
      L = Fresh();
      pic.Gradient(L, 50.0f, 100.0f, 250.0f, 100.0f, k1, k1, 1, 0.5f);
      Cook(&pic);
      Check("S2: gradient opacity", Pixel(&pic, 150, 100)[0] > 0.3f && Pixel(&pic, 150, 100)[0] < 0.9f);

      // Eyedropper: a 3x3 white dot reads white at a point, grey over 5x5; one layer reads through the one above.
      L = Fresh();
      BrushEngine::Brush dot = Colour(hard, 1, 1, 1);
      dot.size = 3.0f;
      dot.pencil = true;
      pic.BeginPaint(L, 0, dot, 100.5f, 100.5f, 1.0f);
      pic.EndPaint();
      Cook(&pic);
      float p1[3] = { 0, 0, 0 }, p5[3] = { 0, 0, 0 };
      pic.Pick(100.5f, 100.5f, p1, 1);
      pic.Pick(100.5f, 100.5f, p5, 5);
      Check("S2: eyedropper 5x5 averages", p1[0] > 0.9f && p5[0] < 0.6f && p5[0] > 0.1f);
      const int over = pic.AddPaintLayer(L);
      Line(L, 20.0f, 280.0f, 150.0f, Colour(hard, 1, 0, 0));
      Line(over, 20.0f, 280.0f, 150.0f, Colour(hard, 0, 0, 1));
      Cook(&pic);
      float all[3] = { 0, 0, 0 }, one[3] = { 0, 0, 0 };
      pic.Pick(150.0f, 150.0f, all, 1, 0);
      pic.Pick(150.0f, 150.0f, one, 1, L);
      Check("S2: eyedropper current layer reads under the top", all[2] > 0.9f && one[0] > 0.9f && one[2] < 0.05f);

      // Fill layer reflected and diamond.
      while (!pic.Layers().empty())
         pic.Layers().pop_back();
      Layer& f = pic.AddLayer(Kind::Fill, 0);
      f.colorA[0] = f.colorA[1] = f.colorA[2] = 0.0f;
      f.colorA[3] = 1.0f;
      f.colorB[0] = f.colorB[1] = f.colorB[2] = 1.0f;
      f.colorB[3] = 1.0f;
      f.fillAngle = 0.0f;
      f.fillType = 1;
      pic.Commit();
      Cook(&pic);
      const float linL = Pixel(&pic, 10, 100)[0], linR = Pixel(&pic, 290, 100)[0];
      pic.Layers()[0].fillType = 3;
      pic.Commit();
      Cook(&pic);
      const float refL = Pixel(&pic, 10, 100)[0], refR = Pixel(&pic, 290, 100)[0], refC = Pixel(&pic, 150, 100)[0];
      pic.Layers()[0].fillType = 4;
      pic.Commit();
      Cook(&pic);
      const float diaC = Pixel(&pic, 150, 100)[0], diaCorner = Pixel(&pic, 290, 190)[0];
      Check("S2: reflected fill is dark in the middle, light at both ends", linL < 0.1f && linR > 0.9f && refC < 0.1f && refL > 0.8f && refR > 0.8f);
      Check("S2: diamond fill grows from the centre", diaC < 0.1f && diaCorner > 0.9f);
   }

   // ---- S3 new paint: Levels, Curves, relief, marbling ----
   {
      Scene2DNode pic;
      pic.Layers();
      pic.Doc().w = 300;
      pic.Doc().h = 200;
      pic.Doc().background = 1;   // black
      pic.Commit();
      const auto Clear = [&]() { while (!pic.Layers().empty()) pic.Layers().pop_back(); pic.Commit(); };
      const auto Grey = [&](float v)
      {
         Layer& f = pic.AddLayer(Kind::Fill, 0);
         f.colorA[0] = f.colorA[1] = f.colorA[2] = v;
         f.colorA[3] = 1.0f;
         pic.Commit();
      };
      // Levels: input 0.25..0.75 stretches 0.6 to 0.7; gamma 2 lifts it further; output 0.2..0.8 squeezes it.
      Clear();
      Grey(0.6f);
      Cook(&pic);
      const float plain = Pixel(&pic, 150, 100)[0];
      {
         Layer& a = pic.AddLayer(Kind::Adjust, 0, pic.Layers()[0].id);
         a.adjType = Scene2DNode::kAdjLevels;
         a.adjAmt = a.adjTo = 1.0f;
         a.levels[0] = 0.25f;
         a.levels[1] = 0.75f;
      }
      pic.Commit();
      Cook(&pic);
      const float stretched = Pixel(&pic, 150, 100)[0];
      pic.Layers()[1].adjAmt = 2.0f;
      pic.Commit();
      Cook(&pic);
      const float gamma = Pixel(&pic, 150, 100)[0];
      pic.Layers()[1].adjAmt = 1.0f;
      pic.Layers()[1].levels[0] = 0.0f;
      pic.Layers()[1].levels[1] = 1.0f;
      pic.Layers()[1].levels[2] = 0.5f;
      pic.Layers()[1].levels[3] = 0.5f;
      pic.Commit();
      Cook(&pic);
      const float flat = Pixel(&pic, 150, 100)[0];
      printf("  levels: plain %.3f stretched %.3f gamma2 %.3f out 0.5..0.5 %.3f\n", plain, stretched, gamma, flat);
      Check("S3: levels input range stretches", std::fabs(plain - 0.6f) < 0.02f && std::fabs(stretched - 0.7f) < 0.03f);
      Check("S3: levels gamma lifts the mids", gamma > stretched + 0.05f);
      Check("S3: levels output range", std::fabs(flat - 0.5f) < 0.02f);
      // Curves: a point at (0.5, 0.8) lifts mid grey to 0.8; half strength lands between.
      Clear();
      Grey(0.5f);
      {
         Layer& a = pic.AddLayer(Kind::Adjust, 0, pic.Layers()[0].id);
         a.adjType = Scene2DNode::kAdjCurves;
         a.adjAmt = a.adjTo = 1.0f;
         a.curve = { 0.0f, 0.0f, 0.5f, 0.8f, 1.0f, 1.0f };
      }
      pic.Commit();
      Cook(&pic);
      const float curved = Pixel(&pic, 150, 100)[0];
      pic.Layers()[1].adjAmt = 0.5f;
      pic.Commit();
      Cook(&pic);
      const float halfCurved = Pixel(&pic, 150, 100)[0];
      printf("  curves: 0.5 -> %.3f (half strength %.3f)\n", curved, halfCurved);
      Check("S3: curves lift mid grey to the point", std::fabs(curved - 0.8f) < 0.03f && std::fabs(halfCurved - 0.65f) < 0.03f);
      float lut[64];
      Scene2DNode::CurveLut({ 0.0f, 0.0f, 0.3f, 0.9f, 0.6f, 0.95f, 1.0f, 1.0f }, lut, 64);
      bool rising = true;
      for (int i = 1; i < 64; i++)
         rising = rising && lut[i] >= lut[i - 1] - 1e-5f && lut[i] <= 1.0f;
      Check("S3: a rising curve never dips or overshoots", rising);
      {
         Scene2DNode copy;
         copy.SetDocText(pic.DocText());
         const Layer& c = copy.Layers()[1];
         Check("S3: curve and levels save", c.adjType == Scene2DNode::kAdjCurves && c.curve.size() == 6 && std::fabs(c.curve[3] - 0.8f) < 1e-5f);
      }

      BrushEngine::Brush hard = BrushEngine::Presets()[5].brush;   // Hard round
      hard.pressureSize = false;
      const auto Line = [&](int id, float x0, float y0, float x1, float y1, const BrushEngine::Brush& use)
      {
         pic.BeginPaint(id, 0, use, x0, y0, 1.0f);
         const float len = std::hypot(x1 - x0, y1 - y0);
         for (float t = 0.0f; t <= len; t += 4.0f)
            pic.PaintTo(x0 + (x1 - x0) * t / len, y0 + (y1 - y0) * t / len, 1.0f);
         pic.PaintTo(x1, y1, 1.0f);
         pic.EndPaint();
      };
      const auto Colour = [](BrushEngine::Brush b, float r, float g, float bl) { b.color[0] = r; b.color[1] = g; b.color[2] = bl; return b; };

      // Relief modes on height (seen as alpha over black): stack adds, raise builds softly, press dents.
      Clear();
      int L = pic.AddPaintLayer();
      BrushEngine::Brush h = Colour(hard, 1, 1, 1);
      h.size = 30.0f;
      h.opacity = 0.4f;
      Line(L, 20, 60, 280, 60, h);
      Line(L, 20, 60, 280, 60, h);
      h.mode = BrushEngine::kStack;
      Line(L, 20, 140, 280, 140, h);
      Line(L, 20, 140, 280, 140, h);
      Cook(&pic);
      const float raised = Pixel(&pic, 150, 60)[0], stacked = Pixel(&pic, 150, 140)[0];
      h.mode = BrushEngine::kPress;
      Line(L, 20, 140, 280, 140, h);
      Cook(&pic);
      const float pressed = Pixel(&pic, 150, 140)[0];
      printf("  relief height: raise x2 %.3f, stack x2 %.3f, pressed %.3f\n", raised, stacked, pressed);
      Check("S3: stack builds higher than raise", stacked > raised + 0.03f);
      Check("S3: press dents", pressed < stacked - 0.05f);

      // A relief stroke relights when the light turns: its upper flank is bright lit from above, dark from below.
      Clear();
      L = pic.AddPaintLayer();
      BrushEngine::Brush clay = BrushEngine::Presets()[0].brush;
      for (const BrushEngine::Preset& pr : BrushEngine::Presets())
         if (std::string(pr.name) == "Plasticine")
            clay = pr.brush;
      clay = Colour(clay, 0.8f, 0.5f, 0.3f);
      clay.size = 40.0f;
      clay.pressureSize = false;
      pic.FindLayer(L)->relief = true;
      pic.FindLayer(L)->reliefMat = 0;
      pic.Commit();
      Line(L, 30, 100, 270, 100, clay);
      float flank[4][2];
      for (int mat = 0; mat < 4; mat++)
      {
         pic.FindLayer(L)->reliefMat = mat;
         for (int k = 0; k < 2; k++)
         {
            pic.FindLayer(L)->reliefLight = k == 0 ? 90.0f : -90.0f;
            pic.Commit();
            Cook(&pic);
            float sum = 0.0f;
            for (int x = 140; x < 160; x++)   // stipple is dots: average a run
               sum += Pixel(&pic, x, 90)[0];
            flank[mat][k] = sum / 20.0f;
         }
      }
      printf("  relief upper flank lit from top / bottom: clay %.3f/%.3f metal %.3f/%.3f chrome %.3f/%.3f stipple %.3f/%.3f\n",
             flank[0][0], flank[0][1], flank[1][0], flank[1][1], flank[2][0], flank[2][1], flank[3][0], flank[3][1]);
      Check("S3: a relief stroke relights when the light turns", flank[0][0] > flank[0][1] + 0.15f);
      Check("S3: metal, chrome and stipple relight too", std::fabs(flank[1][0] - flank[1][1]) > 0.1f && std::fabs(flank[2][0] - flank[2][1]) > 0.1f
                                                         && std::fabs(flank[3][0] - flank[3][1]) > 0.05f);
      {
         Scene2DNode copy;
         copy.SetDocText(pic.DocText());
         const Layer* c = copy.FindLayer(L);
         Check("S3: relief saves", c != nullptr && c->relief && c->reliefMat == 3 && std::fabs(c->reliefLight + 90.0f) < 1e-4f);
      }

      // Marbling: a drop pushes a red line aside and takes its place.
      const auto IsRed = [&](int x, int y) { const std::vector<float> c = Pixel(&pic, x, y); return c[0] > 0.4f && c[1] < 0.2f && c[2] < 0.2f; };
      Clear();
      L = pic.AddPaintLayer();
      BrushEngine::Brush red = Colour(hard, 1, 0, 0);
      red.size = 6.0f;
      Line(L, 150, 10, 150, 190, red);
      BrushEngine::Brush drop = Colour(hard, 0, 0, 1);
      drop.mode = BrushEngine::kDrop;
      drop.size = 50.0f;
      pic.BeginPaint(L, 0, drop, 130.0f, 100.0f, 1.0f);
      pic.EndPaint();
      Cook(&pic);
      int pushedTo = 0;
      for (int x = 151; x < 200 && pushedTo == 0; x++)
         if (IsRed(x, 100))
            pushedTo = x;
      printf("  drop: red line at 150 pushed to %d (expected ~162)\n", pushedTo);
      Check("S3: a drop pushes paint aside", Pixel(&pic, 150, 100)[2] > 0.9f && pushedTo >= 158 && pushedTo <= 167 && IsRed(150, 10));
      // Comb: a tine drawn down through a red line drags it down.
      Clear();
      L = pic.AddPaintLayer();
      BrushEngine::Brush band = red;
      band.size = 16.0f;
      Line(L, 10, 60, 290, 60, band);
      BrushEngine::Brush comb = hard;
      comb.mode = BrushEngine::kComb;
      comb.size = 40.0f;
      comb.tines = 1;
      comb.spacing = 0.25f;
      Line(L, 150, 30, 150, 160, comb);
      Cook(&pic);
      int dragged = 0;
      for (int y = 70; y < 170; y++)
         dragged += IsRed(150, y) ? 1 : 0;
      {
         std::string col;
         for (int y = 50; y < 170; y += 6)
         {
            const std::vector<float> c = Pixel(&pic, 150, y);
            char b[48];
            std::snprintf(b, sizeof(b), " %d:%.2f/%.2f", y, c[0], c[1]);
            col += b;
         }
         printf("  comb column x=150:%s\n", col.c_str());
      }
      Check("S3: a comb drags the paint along", !IsRed(150, 60) && dragged > 10 && IsRed(40, 60));
      // Swirl: turning round a point on the line bends it off its column.
      Clear();
      L = pic.AddPaintLayer();
      Line(L, 150, 10, 150, 190, red);
      BrushEngine::Brush swirl = hard;
      swirl.mode = BrushEngine::kSwirl;
      swirl.size = 80.0f;
      swirl.spacing = 0.05f;
      Line(L, 150, 100, 162, 100, swirl);
      Cook(&pic);
      int off = 0;
      for (int y = 75; y < 125; y++)
         for (int x = 120; x < 180; x++)
            if (std::abs(x - 150) > 6 && IsRed(x, y))
               off++;
      printf("  swirl: %d red px off the column\n", off);
      Check("S3: a swirl turns the paint", off > 20 && IsRed(150, 15));
      // Liquify: Bloat swells a red dot, Pinch draws it in; both leave paint outside the brush alone.
      const auto RedArea = [&]() {
         Cook(&pic);
         int n = 0;
         for (int y = 60; y < 140; y++)
            for (int x = 110; x < 190; x++)
               n += IsRed(x, y) ? 1 : 0;
         return n;
      };
      int liquified[2] = {};
      for (int k = 0; k < 2; k++)
      {
         Clear();
         L = pic.AddPaintLayer();
         BrushEngine::Brush dot = red;
         dot.size = 20.0f;
         Line(L, 150, 100, 150.5f, 100, dot);
         Line(L, 20, 20, 20.5f, 20, dot);
         const int before = RedArea();
         BrushEngine::Brush lq = hard;
         lq.mode = k == 0 ? BrushEngine::kBloat : BrushEngine::kPinch;
         lq.size = 70.0f;
         lq.flow = 0.6f;
         lq.spacing = 0.02f;
         Line(L, 148, 100, 152, 100, lq);
         liquified[k] = RedArea() - before;
         printf("  %s: red area %d -> %+d\n", k == 0 ? "bloat" : "pinch", before, liquified[k]);
      }
      Check("S3: Liquify Bloat swells the paint", liquified[0] > 40);
      Check("S3: Liquify Pinch draws the paint in", liquified[1] < -40);
      Check("S3: Liquify leaves paint outside the brush", IsRed(20, 20));
      bool presets = false;
      for (const auto& pr : BrushEngine::Presets())
         presets |= pr.group != nullptr && std::string(pr.group) == "Liquify" && pr.brush.mode == BrushEngine::kPinch;
      Check("S3: the Liquify presets are listed", presets);

      // ---- Block 5: material brushes (the section-3 done-checks) ----
      const auto Find = [](const char* name) {
         for (const auto& pr : BrushEngine::Presets())
            if (std::string(pr.name) == name)
               return pr.brush;
         return BrushEngine::Brush{};
      };
      const auto Lum = [&](int x0, int x1, int y0, int y1) {
         float sum = 0.0f;
         int n = 0;
         for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++)
            {
               const std::vector<float> c = Pixel(&pic, x, y);
               sum += 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
               n++;
            }
         return n > 0 ? sum / n : 0.0f;
      };
      // Brushed steel: turning the light 180 degrees moves the bright edge to the other side of the stroke.
      float side[2] = {};
      for (int k = 0; k < 2; k++)
      {
         Clear();
         L = pic.AddPaintLayer();
         BrushEngine::Brush steel = Find("Brushed steel");
         steel.lightAz = k == 0 ? 90.0f : -90.0f;
         Line(L, 40, 100, 260, 100, steel);
         Cook(&pic);
         side[k] = Lum(130, 170, 84, 92) - Lum(130, 170, 108, 116);
      }
      printf("  brushed steel: top-minus-bottom %.3f, light turned %.3f\n", side[0], side[1]);
      Check("B5: Brushed steel's bright edge follows the light", side[0] * side[1] < 0.0f && std::fabs(side[0]) > 0.03f && std::fabs(side[1]) > 0.03f);
      // Grain: a canvas texture lands at the same canvas spot under two crossing strokes.
      std::vector<float> cross[2];
      for (int k = 0; k < 2; k++)
      {
         Clear();
         L = pic.AddPaintLayer();
         BrushEngine::Brush gb = Colour(hard, 1, 1, 1);
         gb.size = 90.0f;
         gb.grain = 1;
         gb.grainMode = 1;
         gb.grainDepth = 1.0f;
         gb.grainScale = 1.5f;
         if (k == 0)
            Line(L, 30, 100, 270, 100, gb);
         else
            Line(L, 150, 10, 150, 190, gb);
         Cook(&pic);
         for (int y = 85; y < 115; y++)
            for (int x = 135; x < 165; x++)
               cross[k].push_back(Pixel(&pic, x, y)[0]);
      }
      const auto Corr = [](const std::vector<float>& a, const std::vector<float>& b) {
         double ma = 0, mb = 0;
         for (size_t i = 0; i < a.size(); i++) { ma += a[i]; mb += b[i]; }
         ma /= a.size(); mb /= b.size();
         double sab = 0, saa = 0, sbb = 0;
         for (size_t i = 0; i < a.size(); i++) { sab += (a[i] - ma) * (b[i] - mb); saa += (a[i] - ma) * (a[i] - ma); sbb += (b[i] - mb) * (b[i] - mb); }
         return saa > 1e-9 && sbb > 1e-9 ? sab / std::sqrt(saa * sbb) : 0.0;
      };
      const float spread = *std::max_element(cross[0].begin(), cross[0].end()) - *std::min_element(cross[0].begin(), cross[0].end());
      const double corr = Corr(cross[0], cross[1]);
      printf("  grain: spread %.2f, crossing strokes correlate %.3f\n", spread, corr);
      Check("B5: the grain stays put under crossing strokes", spread > 0.2f && corr > 0.8);
      // Wet gouache: white drawn over red picks the red up, so its first 20 px are pink.
      Clear();
      L = pic.AddPaintLayer();
      BrushEngine::Brush redBand = Colour(hard, 1, 0, 0);
      redBand.size = 80.0f;
      Line(L, 0, 100, 300, 100, redBand);
      Line(L, 60, 100, 280, 100, Find("Wet gouache"));
      Cook(&pic);
      const std::vector<float> wet = Pixel(&pic, 70, 100);
      printf("  wet gouache at x=70: %.2f %.2f %.2f\n", wet[0], wet[1], wet[2]);
      Check("B5: Wet gouache over red leaves pink at the start", wet[0] > 0.8f && wet[1] > 0.25f && wet[1] < 0.85f && std::fabs(wet[1] - wet[2]) < 0.1f);
      int materials = 0;
      for (const auto& pr : BrushEngine::Presets())
         materials += pr.group != nullptr && std::string(pr.group) == "Material" ? 1 : 0;
      Check("B5: ten Material presets are listed", materials == 10);

      // ---- Block 6: node-fed brushes (the section-4 done-check) ----
      // A Voronoi Texture node as the tip paints cells; its scale changed mid-stroke leaves the stroke as it began
      // (frozen) and reaches the next stroke; with Live on it reaches the stroke straight away.
      GraphNode* texGn = SpawnNode("Texture", "Source", 0.0f, 900.0f);
      auto* texNode = texGn != nullptr ? dynamic_cast<TextureNode*>(texGn->node.get()) : nullptr;
      if (texNode == nullptr)
         Check("B6: a Texture node spawns", false);
      else
      {
         const uint64_t texUid = texGn->uid;
         const int texIndex = texGn->index;
         texNode->textureType = 0;      // Voronoi
         texNode->voronoiFeature = 3;   // distance to edge: bright cells, dark borders
         texNode->width = texNode->height = 256.0f;
         texNode->scale = 4.0f;
         texNode->CookIfNeeded(++sFrame);
         pic.Doc().brushSrc[0] = texUid;
         pic.Commit();
         BrushEngine::Brush tipB = Colour(hard, 1, 1, 1);
         tipB.size = 120.0f;
         tipB.spacing = 1.0f;
         // Crossings of the row's mean along y = 100 across the second dab: more cells, more crossings.
         const auto Crossings = [&]() {
            Cook(&pic);
            std::vector<float> row;
            for (int x = 108; x <= 212; x++)   // the dab at x = 160, drawn after the change
               row.push_back(Pixel(&pic, x, 100)[0]);
            float mean = 0.0f;
            for (float v : row) mean += v;
            mean /= (float)row.size();
            int n = 0, side = 0;
            for (float v : row)
            {
               const int now = v > mean + 0.04f ? 1 : v < mean - 0.04f ? -1 : side;
               if (side != 0 && now != side) n++;
               side = now;
            }
            return n;
         };
         const auto Stroke = [&](float midScale) {
            Clear();
            const int id = pic.AddPaintLayer();
            pic.BeginPaint(id, 0, tipB, 40.0f, 100.0f, 1.0f);
            pic.PaintTo(60.0f, 100.0f, 1.0f);
            if (midScale > 0.0f)
            {
               texNode->scale = midScale;
               texNode->CookIfNeeded(++sFrame);
               Cook(&pic);
            }
            for (float x = 60.0f; x <= 240.0f; x += 6.0f)
               pic.PaintTo(x, 100.0f, 1.0f);
            pic.EndPaint();
            return Crossings();
         };
         const int c0 = Stroke(0.0f);           // scale 4 throughout
         const int c1 = Stroke(16.0f);          // 4, turned to 16 mid-stroke: frozen keeps 4
         Cook(&pic);
         const int c2 = Stroke(0.0f);           // the next stroke sees 16
         texNode->scale = 4.0f;
         texNode->CookIfNeeded(++sFrame);
         pic.Doc().brushLive = true;
         pic.Commit();
         const int c3 = Stroke(16.0f);          // live: 16 reaches the stroke at once
         printf("  node tip crossings: scale 4 %d, frozen after change %d, next stroke %d, live %d\n", c0, c1, c2, c3);
         Check("B6: a Voronoi node as the tip paints cells", c0 >= 2);
         Check("B6: changing the node mid-stroke leaves the stroke frozen", std::abs(c1 - c0) <= 1);
         Check("B6: the next stroke takes the node's new scale", c2 >= c0 + 3);
         Check("B6: Live takes the change straight away", c3 >= c0 + 3);
         {
            Scene2DNode copy;
            copy.SetDocText(pic.DocText());
            copy.Layers();
            Check("B6: brush sources save", copy.Doc().brushSrc[0] == texUid && copy.Doc().brushSrc[1] == 0 && copy.Doc().brushLive);
         }
         pic.Doc().brushSrc[0] = 0;
         pic.Doc().brushLive = false;
         pic.Commit();
         if (GraphNode* g = FindNodeByIndex(texIndex); g != nullptr && g->uid == texUid)
            RemoveNodeByIndex(texIndex);
      }
   }

   // ---- Block 7: effect stacks (the section-5 done-check) ----
   // Gaussian Blur added to a layer's stack blurs it with no node on the canvas; an unchanged stack is not redrawn,
   // a changed radius redraws it once; the stack saves, reloads and goes through a preset.
   {
      Scene2DNode pic;
      pic.Layers();
      pic.Doc().w = 300;
      pic.Doc().h = 200;
      pic.Doc().background = 1;   // black
      pic.Commit();
      const int L = pic.AddPaintLayer();
      BrushEngine::Brush br = BrushEngine::Presets()[5].brush;   // Hard round
      br.size = 20.0f;
      br.color[0] = 1.0f;
      br.color[1] = br.color[2] = 0.1f;
      pic.BeginPaint(L, 0, br, 150.0f, 20.0f, 1.0f);
      for (float y = 20.0f; y <= 180.0f; y += 4.0f)
         pic.PaintTo(150.0f, y, 1.0f);
      pic.EndPaint();
      Cook(&pic);
      const float before = Pixel(&pic, 166, 100)[0];
      GraphNode* blurGn = SpawnNode("gaussianblur", "Effects", 0.0f, 1200.0f);
      if (blurGn == nullptr)
         Check("B7: a Gaussian Blur node spawns", false);
      else
      {
         const uint64_t blurUid = blurGn->uid;
         const int blurIndex = blurGn->index;
         INode* blur = blurGn->node.get();
         struct SetRadius : ParamVisitor
         {
            float to = 0.0f;
            void Float(const char* name, float& v) override { if (std::strstr(name, "adius") != nullptr) v = to; }
            void Int(const char* name, int& v) override { if (std::strstr(name, "uality") != nullptr) v = 1; }
            void Bool(const char*, bool&) override {}
            void Text(const char*, std::string&) override {}
            void Color(const char*, float*) override {}
         } set;
         set.to = 6.0f;
         blur->VisitParams(set);
         Scene2DNode::Effect e;
         e.uid = blurUid;
         e.entry = "Gaussian Blur";
         pic.FindLayer(L)->effects.push_back(e);
         pic.Commit();
         HideEffectNodes(pic);
         Check("B7: the stack's node is off the canvas", FindNodeByUid(blurUid) != nullptr && FindNodeByUid(blurUid)->hiddenFromCanvas);
         Cook(&pic);
         const float after = Pixel(&pic, 166, 100)[0];
         const unsigned long long d0 = pic.mEffDraws;
         Cook(&pic);
         Cook(&pic);
         const unsigned long long d1 = pic.mEffDraws;
         set.to = 14.0f;
         blur->VisitParams(set);
         Cook(&pic);
         const unsigned long long d2 = pic.mEffDraws;
         const float wider = Pixel(&pic, 166, 100)[0];
         Cook(&pic);
         const unsigned long long d3 = pic.mEffDraws;
         printf("  blur stack: edge+6 px %.3f -> %.3f -> %.3f, draws %llu %llu %llu %llu\n", before, after, wider, d0, d1, d2, d3);
         Check("B7: Gaussian Blur in the stack blurs the layer", after > before + 0.02f);
         Check("B7: an unchanged stack is not redrawn", d1 == d0);
         Check("B7: a changed radius redraws once", d2 == d1 + 1 && d3 == d2 && wider != after);
         // A native Black & White after it: the red turns grey.
         Scene2DNode::Effect bw;
         bw.adjType = Scene2DNode::kAdjBlackWhite;
         bw.adjAmt = 1.0f;
         bw.adjP = Scene2DNode::AdjParamDefault(bw.adjType);
         pic.FindLayer(L)->effects.push_back(bw);
         pic.Commit();
         Cook(&pic);
         const std::vector<float> grey = Pixel(&pic, 150, 100);
         printf("  black & white: %.2f %.2f %.2f\n", grey[0], grey[1], grey[2]);
         Check("B7: Black & White in the stack makes the red grey", std::fabs(grey[0] - grey[1]) < 0.04f && std::fabs(grey[1] - grey[2]) < 0.04f && grey[0] > 0.05f);
         {
            Scene2DNode copy;
            copy.SetDocText(pic.DocText());
            copy.Layers();
            const Layer* cl = copy.FindLayer(L);
            Check("B7: the stack saves and reloads", cl != nullptr && cl->effects.size() == 2 && cl->effects[0].uid == blurUid &&
                                                     cl->effects[0].entry == "Gaussian Blur" && cl->effects[1].adjType == Scene2DNode::kAdjBlackWhite &&
                                                     cl->effects[1].adjP.size() == 6);
            Layer fresh;
            Check("B7: the stack goes through a preset", Scene2DNode::ParseEffects(Scene2DNode::EffectsText(*pic.FindLayer(L)), fresh) &&
                                                         fresh.effects.size() == 2 && fresh.effects[1].adjType == Scene2DNode::kAdjBlackWhite);
         }
         if (GraphNode* g = FindNodeByIndex(blurIndex); g != nullptr && g->uid == blurUid)
            RemoveNodeByIndex(blurIndex);
      }
   }

   // ---- B8: layered files - PSD and ORA out and back in keep the layers ----
   {
      Scene2DNode pic;
      pic.Layers();
      pic.Doc().w = 300;
      pic.Doc().h = 200;
      pic.Doc().background = 3;
      pic.Commit();
      BrushEngine::Brush br = BrushEngine::Presets()[5].brush;   // Hard round
      br.size = 30.0f;
      br.pressureSize = false;
      auto line = [&](int id, int target, float y, float r, float g, float b) {
         BrushEngine::Brush c = br;
         c.color[0] = r; c.color[1] = g; c.color[2] = b;
         pic.BeginPaint(id, target, c, 20.0f, y, 1.0f);
         for (float x = 20.0f; x <= 280.0f; x += 6.0f)
            pic.PaintTo(x, y, 1.0f);
         pic.EndPaint();
      };
      const int A = pic.AddPaintLayer();
      line(A, 0, 60.0f, 1, 0, 0);
      pic.AddPixelMask(A);
      line(A, 1, 60.0f, 0, 0, 0);   // the mask hides the line
      Layer& wash = pic.AddLayer(Kind::Fill, 0);
      (void)wash.id;
      wash.colorA[0] = 0.2f; wash.colorA[1] = 0.4f; wash.colorA[2] = 0.9f; wash.colorA[3] = 1.0f;
      wash.name = "Wash";
      wash.blend = 11;   // Overlay
      wash.opacity = 0.5f;
      const int G = pic.AddLayer(Kind::Group, 0).id;
      const int B = pic.AddPaintLayer();
      pic.FindLayer(B)->parent = G;
      line(B, 0, 140.0f, 0, 1, 0);
      {
         Layer& a = *pic.FindLayer(A);
         a.name = "Ink";
         a.blend = 2;   // Multiply
         a.opacity = 0.6f;
         Layer& g = *pic.FindLayer(G);
         g.name = "Folder";
         g.blend = 7;   // Screen
         Layer& b = *pic.FindLayer(B);
         b.name = "H\xC3\xB6he \xE2\x9C\x93";   // a name past ASCII
         b.visible = false;
      }
      pic.Commit();
      Cook(&pic);
      std::vector<uint8_t> want;
      int ww = 0, wh = 0;
      pic.ReadOutput(want, ww, wh);
      auto worst = [&](Scene2DNode& other) {
         Cook(&other);
         std::vector<uint8_t> got;
         int gw = 0, gh = 0;
         if (!other.ReadOutput(got, gw, gh) || gw != ww || gh != wh)
            return 255;
         int d = 0;
         for (size_t i = 0; i < got.size(); i++)
            d = std::max(d, std::abs((int)got[i] - (int)want[i]));
         return d;
      };
      auto layersKept = [&](Scene2DNode& q, bool maskKept) {
         const auto& ls = q.Layers();
         if (ls.size() != 4)
            return false;
         const Layer &a = ls[0], &w = ls[1], &g = ls[2], &b = ls[3];
         return a.name == "Ink" && a.blend == 2 && std::fabs(a.opacity - 0.6f) < 0.01f && a.kind == Kind::Paint
                && (a.HasPixelMask() == maskKept) && w.name == "Wash" && w.blend == 11 && std::fabs(w.opacity - 0.5f) < 0.01f
                && g.kind == Kind::Group && g.name == "Folder" && g.blend == 7 && b.parent == g.id && b.name == "H\xC3\xB6he \xE2\x9C\x93"
                && !b.visible && b.kind == Kind::Paint;
      };

      std::vector<uint8_t> psd;
      Check("B8: a PSD is written", pic.SavePsd(psd, ++sFrame) && psd.size() > 26 && std::memcmp(psd.data(), "8BPS", 4) == 0);
      Scene2DNode fromPsd;
      std::string err;
      const bool psdOk = fromPsd.LoadPsd(psd, &err);
      Check("B8: the PSD opens", psdOk);
      Check("B8: the PSD keeps layers, names, blend, opacity, visibility, group and mask", psdOk && layersKept(fromPsd, true));
      if (psdOk && fromPsd.Layers().size() == 4)
      {
         std::vector<uint8_t> mask;
         int mw = 0, mh = 0;
         const Layer& a = fromPsd.Layers()[0];
         bool dark = false;
         std::vector<uint8_t> tile;
         for (const std::string& k : a.mtiles)
            if (k != "-" && TileStore::Get(k, tile, mw, mh))
               for (size_t i = 0; i < tile.size() && !dark; i += 4)
                  dark = tile[i] < 40;
         Check("B8: the PSD mask keeps what it hid", dark);
      }
      const int dPsd = psdOk ? worst(fromPsd) : 255;
      printf("  B8 PSD round trip: worst pixel difference %d / 255\n", dPsd);
      Check("B8: the PSD composites as it was", dPsd <= 4);

      std::vector<uint8_t> ora;
      Check("B8: an ORA is written", pic.SaveOra(ora, ++sFrame) && ora.size() > 40);
      Scene2DNode fromOra;
      const bool oraOk = fromOra.LoadOra(ora, &err);
      Check("B8: the ORA opens", oraOk);
      Check("B8: the ORA keeps layers, names, blend, opacity, visibility and group", oraOk && layersKept(fromOra, false));
      const int dOra = oraOk ? worst(fromOra) : 255;
      printf("  B8 ORA round trip: worst pixel difference %d / 255\n", dOra);
      Check("B8: the ORA composites as it was", dOra <= 4);

      // A flat 2 x 1 PSD (no layers, raw): it opens as one Background layer.
      const uint8_t flat[] = { '8', 'B', 'P', 'S', 0, 1, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 1, 0, 0, 0, 2, 0, 8, 0, 3,
                               0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                               255, 0, 0, 0, 0, 255 };   // R plane, G plane, B plane: red, blue
      Scene2DNode flatDoc;
      const bool flatOk = flatDoc.LoadPsd(std::vector<uint8_t>(flat, flat + sizeof flat), &err);
      bool flatPx = false;
      if (flatOk && flatDoc.Layers().size() == 1)
      {
         const Layer& l = flatDoc.Layers()[0];
         std::vector<uint8_t> t;
         int tw = 0, th = 0;
         flatPx = l.name == "Background" && l.pw == 2 && l.ph == 1 && TileStore::Get(l.tiles[0], t, tw, th) && t[0] == 255
                  && t[2] == 0 && t[4] == 0 && t[6] == 255 && t[7] == 255;
      }
      Check("B8: a flat PSD opens as one Background layer", flatOk && flatDoc.Doc().w == 2 && flatPx);
      Check("B8: a damaged file says why", !flatDoc.LoadPsd(std::vector<uint8_t>(flat, flat + 10), &err) && !err.empty());
   }

   // ---- B8: stabiliser and predicted points ----
   {
      Scene2DNode pic;
      pic.Layers();
      pic.Doc().w = 300;
      pic.Doc().h = 200;
      pic.Doc().background = 1;   // black
      pic.Commit();
      BrushEngine::Brush pen = BrushEngine::Presets()[5].brush;   // Hard round
      pen.size = 4.0f;
      pen.pressureSize = false;
      pen.color[0] = pen.color[1] = pen.color[2] = 1.0f;
      // A shaky hand: a line along y = 100 that jumps 12 px up and down every 6 px, ending on the line.
      const auto Shaky = [&](const BrushEngine::Brush& use)
      {
         while (!pic.Layers().empty())
            pic.Layers().pop_back();
         pic.Commit();
         const int L = pic.AddPaintLayer();
         pic.BeginPaint(L, 0, use, 20.0f, 100.0f, 1.0f);
         int k = 0;
         for (float x = 26.0f; x < 280.0f; x += 6.0f, k++)
            pic.PaintTo(x, 100.0f + (k % 2 == 0 ? 12.0f : -12.0f), 1.0f);
         pic.PaintTo(280.0f, 100.0f, 1.0f);
         pic.EndPaint();
         Cook(&pic);
         const int w = pic.GetOutputWidth(), h = pic.GetOutputHeight();
         std::vector<float> px((size_t)w * h * 4);
         glBindTexture(GL_TEXTURE_2D, pic.GetOutputTexture());
         glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, px.data());
         glBindTexture(GL_TEXTURE_2D, 0);
         return px;
      };
      const auto Lit = [](const std::vector<float>& px, int x, int y) { return px[((size_t)(199 - y) * 300 + x) * 4] > 0.5f; };
      // Pixels lit 8+ px off the line between x 60 and 240: the shake that reached the picture.
      const auto Wobble = [&](const std::vector<float>& px)
      {
         int n = 0;
         for (int x = 60; x < 240; x++)
            for (int y = 60; y < 140; y++)
               if (std::abs(y - 100) >= 8 && Lit(px, x, y))
                  n++;
         return n;
      };
      const std::vector<float> raw = Shaky(pen);
      BrushEngine::Brush steady = pen;
      steady.stabiliser = 12;
      const std::vector<float> calm = Shaky(steady);
      const int wRaw = Wobble(raw), wCalm = Wobble(calm);
      printf("  B8 stabiliser: %d px off the line raw, %d stabilised\n", wRaw, wCalm);
      Check("B8: the stabiliser takes the shake out of a line", wRaw > 200 && wCalm * 10 < wRaw);
      Check("B8: a stabilised stroke catches up to where the pen lifted", Lit(calm, 279, 100) && Lit(raw, 279, 100));
      Check("B8: a stabilised stroke still starts where the pen went down", Lit(calm, 21, 100));

      float to[2] = { 0, 0 };
      const float a[3] = { 0.0f, 0.0f, 0.0f }, b[3] = { 10.0f, 5.0f, 16.0f };
      Check("B8: prediction carries the last two samples 8 ms on",
            BrushEngine::Stroke::Predict(a, b, 8.0f, 100.0f, to) && std::fabs(to[0] - 15.0f) < 1e-4f && std::fabs(to[1] - 7.5f) < 1e-4f);
      const float c[3] = { 0.0f, 0.0f, 0.0f }, d[3] = { 40.0f, 0.0f, 4.0f };
      Check("B8: prediction stops at its reach", BrushEngine::Stroke::Predict(c, d, 8.0f, 6.0f, to) && std::fabs(to[0] - 46.0f) < 1e-4f);
      const float e[3] = { 10.0f, 5.0f, 20.0f };
      Check("B8: no prediction from a still pen or one time", !BrushEngine::Stroke::Predict(b, e, 8.0f, 100.0f, to)
                                                              && !BrushEngine::Stroke::Predict(a, a, 8.0f, 100.0f, to));
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
      {
         // Text style and Convert to shapes, on a third copy so the checks below see the same document.
         Scene2DNode conv;
         conv.SetDocText(pic.DocText());
         Cook(&conv);
         const int inkText = Ink(&conv, 20, 180, 220, 236, 1);
         int rw = 0, rh = 0, bw2 = 0, bh2 = 0;
         conv.TextSize(*conv.FindLayer(pt), rw, rh);
         conv.FindLayer(pt)->bold = true;
         conv.FindLayer(pt)->caps = true;
         conv.TextSize(*conv.FindLayer(pt), bw2, bh2);
         conv.FindLayer(pt)->bold = false;
         conv.FindLayer(pt)->caps = false;
         printf("  T-C bold text: %dx%d -> %dx%d\n", rw, rh, bw2, bh2);
         Check("T-C: bold text draws wider", bw2 > rw);
         const int vs = conv.ConvertTextToShapes(pt);
         Cook(&conv);
         const Layer* sl = conv.FindLayer(vs);
         const int inkShapes = Ink(&conv, 20, 180, 220, 236, 1);
         printf("  T-C convert to shapes: %d items, ink %d text -> %d shapes\n", sl != nullptr ? (int)sl->shapes.size() : -1, inkText, inkShapes);
         Check("T-C: text converts to shapes in the same place", sl != nullptr && sl->kind == Kind::Vector && sl->shapes.size() >= 8
                                                                && !conv.FindLayer(pt)->visible && inkShapes > inkText / 2
                                                                && inkShapes < inkText * 2);
         // Round letters keep the font's curves: few anchors, with handles.
         const int bog = conv.AddTextLayer("Bog", 40.0f, 40.0f, 0.0f);
         conv.FindLayer(bog)->fontSize = 60.0f;
         const int bs = conv.ConvertTextToShapes(bog);
         int anchors = 0, curved = 0;
         if (const Layer* bl = conv.FindLayer(bs))
            for (const Shape& sh : bl->shapes)
               for (int i = 0; i < sh.Anchors(); i++, anchors++)
               {
                  const float* q = &sh.pts[(size_t)i * 6];
                  if (std::fabs(q[2] - q[0]) + std::fabs(q[3] - q[1]) + std::fabs(q[4] - q[0]) + std::fabs(q[5] - q[1]) > 0.01f)
                     curved++;
               }
         printf("  T-C \"Bog\" as shapes: %d anchors, %d with handles\n", anchors, curved);
#if defined(__APPLE__) || defined(__linux__)
         Check("T-C: converted letters keep the font's curves (few anchors, with handles)", anchors > 10 && anchors < 120 && curved > 6);
#endif
      }
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

      // I: inner glow, bevel, gradient overlay on a grey square.
      {
         Scene2DNode pic;
         NewPic(pic, 400, 300);
         const int a = Card(pic, 200, 150, 160, 160, 0.5f, 0.5f, 0.5f);
         Layer* l = pic.FindLayer(a);
         Scene2DNode::Fx& g = l->fx[Scene2DNode::kFxGradient];
         g.on = true;
         g.angle = 0.0f;   // black on the left, white on the right
         pic.Commit();
         Cook(&pic);
         const float gl = Pixel(&pic, 130, 150)[0], gr = Pixel(&pic, 270, 150)[0];
         Check("I: gradient overlay runs start to end along its angle", gl < gr - 0.1f);
         l = pic.FindLayer(a);
         l->fx[Scene2DNode::kFxGradient].on = false;
         Scene2DNode::Fx& b = l->fx[Scene2DNode::kFxBevel];
         b.on = true;
         b.angle = 90.0f;   // light from the top
         b.size = 10.0f;
         b.c[3] = 1.0f;
         pic.Commit();
         Cook(&pic);
         const float bt = Pixel(&pic, 200, 74)[0], bb = Pixel(&pic, 200, 226)[0], bc = Pixel(&pic, 200, 150)[0];
         Check("I: bevel lights the top edge and shades the bottom", bt > bc + 0.1f && bb < bc - 0.1f);
         l = pic.FindLayer(a);
         l->fx[Scene2DNode::kFxBevel].on = false;
         Scene2DNode::Fx& ig = l->fx[Scene2DNode::kFxInnerGlow];
         ig.on = true;
         ig.c[0] = 1.0f; ig.c[1] = 0.0f; ig.c[2] = 0.0f; ig.c[3] = 1.0f;
         ig.size = 20.0f;
         pic.Commit();
         Cook(&pic);
         const std::vector<float> e = Pixel(&pic, 122, 150);
         const std::vector<float> m = Pixel(&pic, 200, 150);
         Check("I: inner glow tints the inside edge, not the middle", e[0] > e[1] + 0.15f && Near(m[0], m[1], 0.03f));
         std::printf("  I fx: gradient %.2f..%.2f bevel top %.2f mid %.2f bottom %.2f glow edge %.2f/%.2f\n", gl, gr, bt, bc, bb, e[0], e[1]);
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

      // Save: shear, blend gamma, antialias, export visibility and the fill layer's noise and bitmap round-trip.
      {
         Scene2DNode pic;
         NewPic(pic, 400, 300);
         const int a = Card(pic, 200, 150, 100, 100, 1, 0, 0);
         Layer* l = pic.FindLayer(a);
         l->shear = 12.5f; l->blendGamma = 1.0f; l->aaMode = 2; l->noExport = true;
         Layer& f = pic.AddLayer(Scene2DNode::Kind::Fill, 0);
         f.fillType = 6; f.fillNoise = 0.3f; f.fillImage = "/tmp/a b.png";
         pic.Commit();
         Scene2DNode copy;
         copy.SetDocText(pic.DocText());
         const Layer* c = nullptr;
         const Layer* g = nullptr;
         for (const Layer& x : copy.Layers())
            (x.kind == Scene2DNode::Kind::Fill ? g : c) = &x;
         Check("Parity C: shear, blend gamma, antialias, in export, fill noise and bitmap save",
               c != nullptr && g != nullptr && c->shear == 12.5f && c->blendGamma == 1.0f && c->aaMode == 2 && c->noExport
                  && g->fillType == 6 && Near(g->fillNoise, 0.3f, 1e-4f) && g->fillImage == "/tmp/a b.png");
      }

      // Text character: line colours, no break, offset X, optical, language and outline save; outline, offset and
      // no break change the box.
      {
         Scene2DNode pic;
         NewPic(pic, 800, 300);
         const int t = pic.AddTextLayer("one two three four", 400.0f, 150.0f, 120.0f);
         Layer* l = pic.FindLayer(t);
         l->fontSize = 40.0f;
         int w0 = 0, h0 = 0, w1 = 0, h1 = 0, w2 = 0, h2 = 0, w3 = 0, h3 = 0;
         pic.TextSize(*l, w0, h0);
         l->noBreak = true;
         pic.TextSize(*l, w1, h1);
         l->offsetX = 30.0f;
         pic.TextSize(*l, w2, h2);
         l->outline = true; l->outlineW = 6.0f; l->outlineC[0] = 1.0f;
         pic.TextSize(*l, w3, h3);
         l->ulOwn = true; l->ulC[2] = 1.0f; l->stOwn = true; l->stC[1] = 0.5f; l->optical = true; l->lang = "tr";
         pic.Commit();
         Scene2DNode copy;
         copy.SetDocText(pic.DocText());
         const Layer& c = copy.Layers().back();
         Check("Text character: line colours, no break, offset, optical, language and outline save",
               c.ulOwn && c.ulC[2] == 1.0f && c.stOwn && c.stC[1] == 0.5f && c.noBreak && c.optical && c.lang == "tr"
                  && c.outline && c.outlineW == 6.0f && c.outlineC[0] == 1.0f && c.offsetX == 30.0f);
         std::printf("VIEW2DTEST text char boxes %dx%d %dx%d %d %dx%d\n", w0, h0, w1, h1, w2, w3, h3);
         Check("Text character: no break is one line, offset and outline widen the box",
               h1 < h0 && w1 > w0 && w2 >= w1 + 59 && w3 > w2 && h3 > h1);
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
         std::vector<double> ms, cpuMs;
         using Clock = std::chrono::steady_clock;
         for (int f = 0; f < 40; f++)
         {
            pic.Layers()[0].opacity = 0.8f + 0.001f * (f % 2);
            pic.Commit();
            glFinish();
            const auto t0 = Clock::now();
            Cook(&pic);
            cpuMs.push_back(std::chrono::duration<double, std::milli>(Clock::now() - t0).count());
            glFinish();
            ms.push_back(std::chrono::duration<double, std::milli>(Clock::now() - t0).count());
         }
         std::sort(cpuMs.begin(), cpuMs.end());
         printf("  M3 cpu submit median %.2f ms; batched layers %llu, single passes %llu\n", cpuMs[cpuMs.size() / 2], pic.mBatched, pic.mPasses);
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
         // Batched passes against one pass per layer.
         Scene2DNode::sBatch = false;
         pic.Layers()[12].opacity = 0.55f;
         pic.Commit();
         Cook(&pic);
         pic.Layers()[12].opacity = 0.5f;
         pic.Commit();
         Cook(&pic);
         const std::vector<float> single = grab();
         Scene2DNode::sBatch = true;
         double worstBatch = 0;
         for (size_t k = 0; k < full.size(); k++)
            worstBatch = std::max(worstBatch, (double)std::fabs(full[k] - single[k]));
         printf("  M3c batched vs one pass per layer: worst difference %.6f\n", worstBatch);
         Check("M3c: batched passes match one pass per layer", worstBatch < 2e-3);
         // Batches split at the cache point on one side and not the other, so the two round through half floats a
         // little differently; anything past that is a cache bug.
         double worstCache = cached.size() == full.size() ? 0.0 : 1.0;
         for (size_t k = 0; k < full.size() && k < cached.size(); k++)
            worstCache = std::max(worstCache, (double)std::fabs(full[k] - cached[k]));
         printf("  M3b below cache vs full redraw: worst difference %.6f\n", worstCache);
         Check("M3b: below cache matches a full redraw", worstCache < 2e-3);
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

   // M6: a turning cube keeps its size on the page (only its outline width changes with the turn), and a turning
   // object plus a stack of animated layers stays inside a frame.
   {
      using Clock = std::chrono::steady_clock;
      NewPatch();
      GraphNode* cubeGn = SpawnNode("Cube", "3D", 0.0f, 0.0f);
      auto* cube = cubeGn != nullptr ? dynamic_cast<GeometryNode*>(cubeGn->node.get()) : nullptr;
      // The page shows the node card's view; this check is about size, so look straight on from 22.9 up.
      if (cubeGn != nullptr)
         gNodeCameras[cubeGn->index] = SharedViewportCamera{ 90.0f, 22.9183f };
      Scene2DNode pic;
      pic.SetDocText("doc 1920 1080 0 0 0 0 0 0 0 0 1 32 4 30 10 1 1\n");
      pic.AddLayer(Kind::Node, cubeGn != nullptr ? cubeGn->uid : 0);
      pic.Commit();
      auto box = [&](float& w, float& h)
      {
         std::vector<float> px((size_t)1920 * 1080 * 4);
         glBindTexture(GL_TEXTURE_2D, pic.GetOutputTexture());
         glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, px.data());
         glBindTexture(GL_TEXTURE_2D, 0);
         // The page background is opaque, so the cube is wherever the colour leaves the corner pixel's.
         int x0 = 1920, y0 = 1080, x1 = -1, y1 = -1;
         for (int y = 0; y < 1080; y++)
            for (int x = 0; x < 1920; x++)
            {
               const float* q = &px[((size_t)y * 1920 + x) * 4];
               if (std::fabs(q[0] - px[0]) + std::fabs(q[1] - px[1]) + std::fabs(q[2] - px[2]) > 0.06f)
               {
                  x0 = std::min(x0, x); x1 = std::max(x1, x);
                  y0 = std::min(y0, y); y1 = std::max(y1, y);
               }
            }
         w = (float)std::max(0, x1 - x0 + 1);
         h = (float)std::max(0, y1 - y0 + 1);
         // INFINITE_V2D_DUMP=<dir>: the frame as a PNG, to look at the shading by eye.
         if (const char* dir = getenv("INFINITE_V2D_DUMP"))
         {
            static int n = 0;
            std::vector<unsigned char> rgb((size_t)1920 * 1080 * 3);
            for (int y = 0; y < 1080; y++)
               for (int x = 0; x < 1920; x++)
                  for (int c = 0; c < 3; c++)
                     rgb[((size_t)(1079 - y) * 1920 + x) * 3 + c] =
                        (unsigned char)std::lround(std::clamp(px[((size_t)y * 1920 + x) * 4 + c], 0.0f, 1.0f) * 255.0f);
            const std::string path = std::string(dir) + "/m6_" + std::to_string(n++) + ".png";
            stbi_write_png(path.c_str(), 1920, 1080, 3, rgb.data(), 1920 * 3);
         }
      };
      float w0 = 0, h0 = 0, w45 = 0, h45 = 0;
      if (cube != nullptr)
      {
         cube->rotY = 0.0f;
         Cook(&pic);
         box(w0, h0);
         cube->rotY = 45.0f;
         Cook(&pic);
         box(w45, h45);
      }
      printf("  M6 cube turn: 0deg %.0fx%.0f px, 45deg %.0fx%.0f px\n", w0, h0, w45, h45);
      // Seen from 22.9 degrees up, a turn stretches only the top face's depth: height (cos t + sqrt2 sin t)/(cos t + sin t)
      // at 45 degrees, about 1.12. Anything else is the object itself scaling.
      const float tl = 22.9183f * 3.14159265f / 180.0f;
      const float want = (std::cos(tl) + std::sqrt(2.0f) * std::sin(tl)) / (std::cos(tl) + std::sin(tl));
      Check("M6: a cube turning about its up axis keeps its size on the page", h0 > 50 && std::fabs(h45 / h0 / want - 1.0f) < 0.04f);
      auto timeIt = [&](auto&& step)
      {
         std::vector<double> t;
         for (int f = 0; f < 30; f++)
         {
            step(f);
            glFinish();
            const auto t0 = Clock::now();
            Cook(&pic);
            glFinish();
            t.push_back(std::chrono::duration<double, std::milli>(Clock::now() - t0).count());
         }
         std::sort(t.begin(), t.end());
         return t[t.size() / 2];
      };
      const double still = timeIt([&](int) {});
      const double turning = timeIt([&](int f) { if (cube) cube->rotY = 3.0f * f; });
      // Eight animated noise layers under the turning cube, each half the page.
      for (int i = 0; i < 8; i++)
      {
         GraphNode* nz = SpawnNode("Noise", "Source", 300.0f, 200.0f * i);
         Layer& l = pic.AddLayer(Kind::Node, nz != nullptr ? nz->uid : 0);
         l.opacity = 0.6f;
         l.blend = i % 3;
         l.x = 200.0f + 150.0f * i;
         l.y = 300.0f;
      }
      pic.Commit();
      Cook(&pic);
      const double stack = timeIt([&](int f) { if (cube) cube->rotY = 3.0f * f; });
      printf("  M6 timing: cube still %.2f ms, turning %.2f ms, turning + 8 animated noise layers %.2f ms\n", still, turning, stack);
      Check("M6: a turning cube over 8 animated layers renders inside a 60 fps frame", stack < 16.7);
   }

   // ---- B: real 3D. Objects draw through the comp camera at document size with their own depth: two intersecting
   // cubes hide each other per pixel whatever the layer order, a tilted card passes through a cube. ----
   {
      NewPatch();
      GraphNode* ca = SpawnNode("Cube", "3D", 0.0f, 0.0f);
      GraphNode* cb = SpawnNode("Cube", "3D", 0.0f, 300.0f);
      for (GraphNode* gn : { ca, cb })
         if (gn != nullptr)
            gNodeCameras[gn->index] = SharedViewportCamera{ 90.0f, 22.9183f };
      GraphNode* nz = SpawnNode("Noise", "Source", 0.0f, 600.0f);
      auto paint = [](GraphNode* g, float r, float gg, float b) {
         if (auto* n = g != nullptr ? dynamic_cast<GeometryNode*>(g->node.get()) : nullptr)
         {
            n->color[0] = n->color[1] = n->color[2] = 0.0f;
            n->emissionColor[0] = r;
            n->emissionColor[1] = gg;
            n->emissionColor[2] = b;
            n->emission = 1.0f;
         }
      };
      paint(ca, 1, 0, 0);
      paint(cb, 0, 0, 1);
      Scene2DNode pic;
      pic.SetDocText("doc 512 512 0 0 0 0 0 0 0 0 1 32 4 30 10 1 1\n");
      pic.Doc().background = 1;
      Layer& la = pic.AddLayer(Kind::Node, ca != nullptr ? ca->uid : 0);
      la.is3d = true;
      Layer& lb = pic.AddLayer(Kind::Node, cb != nullptr ? cb->uid : 0);
      lb.is3d = true;
      lb.ry = 45.0f;
      const int ia = la.id, ib = lb.id;
      pic.Commit();
      Cook(&pic);
      Cook(&pic);   // the first render resolves depth
      // Half the cube's side in px: the bounding sphere is 0.8 of the 512 box, the cube's half side 1/sqrt(3) of it.
      const int hs = (int)(0.4f * 512.0f / std::sqrt(3.0f));
      auto red = [&](int x, int y) { auto p = Pixel(&pic, x, y); return p[0] > p[2] + 0.2f; };
      auto blue = [&](int x, int y) { auto p = Pixel(&pic, x, y); return p[2] > p[0] + 0.2f; };
      const int c = 256, side = c + (int)(0.6f * hs);
      const bool order1 = blue(c, c) && red(side, c) && red(512 - side, c);
      pic.MoveLayer(ia, ib, false);   // red on top now
      pic.Commit();
      Cook(&pic);
      Cook(&pic);
      const bool order2 = blue(c, c) && red(side, c) && red(512 - side, c);
      printf("  B occlusion: order 1 %d, order 2 %d (half side %d px)\n", order1 ? 1 : 0, order2 ? 1 : 0, hs);
      Check("B: two intersecting objects hide each other per pixel, either layer order", order1 && order2);
      int rw = 0;   // ids may change on a restack: ask every layer
      for (const Layer& l : pic.Layers())
         rw = std::max(rw, pic.GeometryRenderWidth(l.id));
      printf("  B render width %d\n", rw);
      Check("B: an object renders only its box, inside the document", rw > 0 && rw <= 512);
      // A tilted card through the red cube: one side in front of the cube, the other behind it.
      pic.RemoveLayer(ib);
      Layer& card = pic.AddLayer(Kind::Node, nz != nullptr ? nz->uid : 0);
      card.is3d = true;
      card.ry = 70.0f;   // full size: it leaves the cube's front face one side of the centre
      pic.Commit();
      Cook(&pic);
      Cook(&pic);
      const int off = (int)(0.5f * hs);
      const bool l = red(c - off, c), r = red(c + off, c);
      printf("  B card through cube: left red %d, right red %d\n", l ? 1 : 0, r ? 1 : 0);
      Check("B: a card passes through an object (one side in front, one behind)", l != r);
      Check("B: the cube shows at the card's centre line", red(c, c - hs / 2) || red(c, c + hs / 2));
      pic.SetDocText("doc 320 200 0 0 0 0 0 0 0 0 1 32 4 30 10 1 1\n");
      Layer& small = pic.AddLayer(Kind::Node, ca != nullptr ? ca->uid : 0);
      small.is3d = true;
      pic.Commit();
      Cook(&pic);
      const int sw = pic.GeometryRenderWidth(small.id);
      Check("B: the render follows a new document size", sw > 0 && sw <= 320);
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
   // ---- Block 4: the bug-hunt rows, each with the check that would have caught it ----
   {
      using Shape = Scene2DNode::Shape;
      Scene2DNode pic;
      pic.Layers();
      pic.Doc().w = 320;
      pic.Doc().h = 240;
      pic.Doc().background = 0;
      pic.Commit();
      const int p = pic.AddPaintLayer();
      const float red[3] = { 1.0f, 0.0f, 0.0f };
      Cook(&pic);
      Check("B4: Fill covers the layer", pic.FillPixels(p, red, 1.0f));
      Cook(&pic);
      const std::vector<float> full = Pixel(&pic, 40, 40);
      Check("B4: the fill is in the picture", full[0] > 0.9f && full[1] < 0.1f);

      // #4 the selection is part of the document text, so undo and redo carry it.
      const std::string t0 = pic.DocText();
      pic.SelectRect(20, 20, 120, 100, Scene2DNode::kSelNew, 0.0f, false);
      pic.Commit();
      const std::string t1 = pic.DocText();
      Check("B4 #4: a selection changes the document text", t1 != t0 && t1.find("\nsel ") != std::string::npos);
      pic.SetDocText(t0);
      pic.Layers();
      Check("B4 #4: undo drops the selection", !pic.HasSelection());
      pic.SetDocText(t1);
      pic.Layers();
      float lo[2] = { 0, 0 }, hi[2] = { 0, 0 };
      Check("B4 #4: redo brings it back in place", pic.HasSelection() && pic.SelectionBounds(lo, hi) && std::fabs(lo[0] - 20.0f) <= 1.0f &&
                                                   std::fabs(hi[0] - 120.0f) <= 1.0f && std::fabs(lo[1] - 20.0f) <= 1.0f && std::fabs(hi[1] - 100.0f) <= 1.0f);
      pic.ModifySelection(0, 4);
      pic.Commit();
      Check("B4 #4: Expand changes the text too (its undo step is not empty)", pic.DocText() != t1);
      pic.SetDocText(t1);
      pic.Layers();

      // #2 Delete with a selection clears the pixels inside it and keeps the layer.
      Check("B4 #2: clear runs", pic.ClearPixels(p));
      Cook(&pic);
      const std::vector<float> in = Pixel(&pic, 60, 60), out = Pixel(&pic, 200, 200);
      printf("  B4 clear: inside %.2f %.2f %.2f, outside %.2f %.2f %.2f\n", in[0], in[1], in[2], out[0], out[1], out[2]);
      Check("B4 #2: the selected pixels are gone, the rest and the layer stay", pic.FindLayer(p) != nullptr && in[1] > 0.9f && out[0] > 0.9f && out[1] < 0.1f);

      // Saved selections.
      pic.SaveSelection("Sky");
      pic.Deselect();
      Check("B4: a saved selection loads back", !pic.HasSelection() && pic.LoadSelection("Sky", Scene2DNode::kSelNew) && pic.HasSelection() &&
                                                pic.SelectionAt(60, 60) > 0.9f && pic.SelectionAt(200, 200) < 0.1f);
      pic.Commit();
      {
         Scene2DNode copy;
         copy.SetDocText(pic.DocText());
         copy.Layers();
         Check("B4: saved selections save with the document", copy.SavedSelections().size() == 1 && copy.SavedSelections()[0] == "Sky" && copy.HasSelection());
         copy.Commit();
         Check("B4: save, load, save gives the same text", copy.DocText() == pic.DocText());
      }
      pic.DeleteSavedSelection("Sky");
      Check("B4: a saved selection can be deleted", pic.SavedSelections().empty());

      // #8 quick mask: the bucket and the gradient reach the mask.
      pic.Deselect();
      pic.quickMask = true;
      Check("B4 #8: the bucket fills the quick mask", pic.BucketSelection(10, 10, 1.0f, 32, true) && pic.HasSelection() && pic.SelectionAt(160, 120) > 0.9f);
      Check("B4 #8: the gradient writes the quick mask", pic.GradientSelection(0, 0, 320, 0, 0.0f, 1.0f, 1) && pic.SelectionAt(10, 120) < 0.2f &&
                                                         pic.SelectionAt(310, 120) > 0.8f);
      pic.Commit();
      {
         Scene2DNode copy;
         copy.SetDocText(pic.DocText());
         copy.Layers();
         Check("B4 #14: quick mask is in the text, so its switch can be undone", copy.quickMask);
      }
      pic.quickMask = false;
      pic.Deselect();
      pic.Commit();

      // #1 #12 #24 a hand-edited or damaged file loads clamped.
      {
         std::string bad = pic.DocText();
         Scene2DNode::Document& d = pic.Doc();
         d.gridSubdiv = 0;
         const auto AddAdj = [](Scene2DNode& n, int type) { Layer& a = n.AddLayer(Kind::Adjust, 0); a.adjType = type; return a.id; };
         const int lv = AddAdj(pic, Scene2DNode::kAdjLevels), cv = AddAdj(pic, Scene2DNode::kAdjCurves);
         Layer* L = pic.FindLayer(lv);
         L->opacity = 7.0f;
         L->blend = 999;
         L->levels[0] = -3.0f;
         L->levels[1] = 9.0f;
         Layer* C = pic.FindLayer(cv);
         C->curve.clear();
         for (int i = 0; i < 32; i++)
         {
            C->curve.push_back(1.0f - (float)i / 31.0f);   // unsorted and too many
            C->curve.push_back(2.0f);
         }
         pic.Commit();
         Scene2DNode copy;
         copy.SetDocText(pic.DocText());
         copy.Layers();
         const Layer* c = copy.FindLayer(cv);
         const Layer* cl = copy.FindLayer(lv);
         bool sorted = c != nullptr;
         for (size_t i = 2; c != nullptr && i < c->curve.size(); i += 2)
            sorted = sorted && c->curve[i] >= c->curve[i - 2];
         Check("B4 #1: grid subdivisions of 0 load as 1", copy.Doc().gridSubdiv == 1);
         Check("B4 #12: opacity, blend and levels load inside their ranges", cl != nullptr && cl->opacity <= 1.0f && cl->blend <= 34 && cl->levels[0] >= 0.0f &&
                                                                       cl->levels[1] <= 1.0f);
         Check("B4 #24: a curve loads with at most 16 points, sorted, inside 0..1", c != nullptr && c->curve.size() <= 32 && sorted &&
                                                                               std::all_of(c->curve.begin(), c->curve.end(), [](float v) { return v >= 0.0f && v <= 1.0f; }));
         pic.SetDocText(bad);
         pic.Layers();
      }
      // #12 two layers with one id load as two layers with different ids.
      {
         const int q = pic.AddPaintLayer();
         pic.Commit();
         std::string twin = pic.DocText();
         const std::string from = "\nlayer " + std::to_string(q) + " ", to = "\nlayer " + std::to_string(p) + " ";
         const size_t at = twin.find(from);
         if (at != std::string::npos)
            twin.replace(at, from.size(), to);
         Scene2DNode copy;
         copy.SetDocText(twin);
         std::vector<int> ids;
         for (const Layer& l : copy.Layers())
            ids.push_back(l.id);
         std::sort(ids.begin(), ids.end());
         Check("B4 #12: duplicate layer ids are renumbered on load", at != std::string::npos && ids.size() == pic.Layers().size() &&
                                                                  std::adjacent_find(ids.begin(), ids.end()) == ids.end());
         pic.RemoveLayer(q);
         pic.Commit();
      }

      // #19 items are hit on their path, not their box.
      {
         Shape ring;
         ring.type = Shape::kEllipse;
         ring.x0 = 0; ring.y0 = 0; ring.x1 = 100; ring.y1 = 100;
         ring.fillOn = false;
         ring.strokeOn = true;
         ring.width = 6.0f;
         Shape line;
         line.type = Shape::kLine;
         line.x0 = 0; line.y0 = 0; line.x1 = 100; line.y1 = 100;
         Shape disc = ring;
         disc.fillOn = true;
         Check("B4 #19: a ring is hit on its stroke, not in its hole or its box corner",
               Scene2DNode::ShapeHit(ring, 50, 1, 2) && !Scene2DNode::ShapeHit(ring, 50, 50, 2) && !Scene2DNode::ShapeHit(ring, 3, 3, 2));
         Check("B4 #19: a line is hit along itself only", Scene2DNode::ShapeHit(line, 50, 50, 2) && !Scene2DNode::ShapeHit(line, 90, 10, 2));
         Check("B4 #19: a filled shape is hit inside", Scene2DNode::ShapeHit(disc, 50, 50, 2) && !Scene2DNode::ShapeHit(disc, 3, 3, 2));
      }

      // #13 Rasterize: the same picture, now pixels.
      {
         const int v = pic.AddVectorLayer();
         Shape box;
         box.type = Shape::kRect;
         box.x0 = 200; box.y0 = 20; box.x1 = 300; box.y1 = 90;
         box.fillC[0] = 0.0f; box.fillC[1] = 0.0f; box.fillC[2] = 1.0f;
         pic.FindLayer(v)->shapes.push_back(box);
         pic.Commit();
         Cook(&pic);
         const std::vector<float> a0 = Pixel(&pic, 250, 50), b0 = Pixel(&pic, 250, 150);
         Check("B4 #13: rasterize runs", pic.RasterizeLayer(v));
         Cook(&pic);
         const std::vector<float> a1 = Pixel(&pic, 250, 50), b1 = Pixel(&pic, 250, 150);
         printf("  B4 rasterize: in box %.2f %.2f %.2f -> %.2f %.2f %.2f\n", a0[0], a0[1], a0[2], a1[0], a1[1], a1[2]);
         auto same = [](const std::vector<float>& x, const std::vector<float>& y) {
            return std::fabs(x[0] - y[0]) < 0.02f && std::fabs(x[1] - y[1]) < 0.02f && std::fabs(x[2] - y[2]) < 0.02f;
         };
         Check("B4 #13: a rasterized layer is a paint layer showing the same picture in the same place",
               pic.FindLayer(v)->kind == Kind::Paint && a0[2] > 0.9f && a0[0] < 0.1f && same(a0, a1) && same(b0, b1));
         pic.RemoveLayer(v);
         pic.Commit();
      }

      // #5 #6 GL leak check: 50 layers added, drawn and removed leave no textures behind.
      {
         Cook(&pic);
         const int live0 = pic.LiveTextures();
         std::vector<int> made;
         for (int i = 0; i < 50; i++)
         {
            const int id = pic.AddPaintLayer();
            made.push_back(id);
            if (i % 5 == 0)
            {
               pic.FindLayer(id)->relief = true;
               pic.AddPixelMask(id);
            }
            pic.FillPixels(id, red, 0.5f);
         }
         pic.Commit();
         Cook(&pic);
         const int liveMid = pic.LiveTextures();
         for (int id : made)
            pic.RemoveLayer(id);
         pic.Commit();
         Cook(&pic);
         Cook(&pic);
         printf("  B4 textures: %d before, %d with 50 layers, %d after\n", live0, liveMid, pic.LiveTextures());
         Check("B4 #6: removing 50 layers frees every texture they made", liveMid > live0 && pic.LiveTextures() <= live0);
      }

      // Standing sweep: every layer number, written and saved, loads back to the same text (save, load, save).
      {
         unsigned seed = 12345u;
         auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (float)((seed >> 8) & 0xFFFF) / 65535.0f; };
         bool fixed = true;
         std::string firstBad;
         for (int round = 0; round < 12 && fixed; round++)
         {
            Scene2DNode a;
            a.Layers();
            a.Doc().w = 64 + (int)(rnd() * 400);
            a.Doc().h = 64 + (int)(rnd() * 400);
            a.Doc().gridSubdiv = 1 + (int)(rnd() * 15);
            a.Commit();
            const int n = 1 + (int)(rnd() * 5);
            for (int i = 0; i < n; i++)
            {
               const float pickKind = rnd();
               const int id = pickKind < 0.3f ? a.AddPaintLayer() : pickKind < 0.6f ? a.AddVectorLayer()
                              : [&]() { Layer& n = a.AddLayer(Kind::Adjust, 0); n.adjType = pickKind < 0.8f ? Scene2DNode::kAdjLevels : Scene2DNode::kAdjCurves; return n.id; }();
               Layer& l = *a.FindLayer(id);
               for (int k = 0; k < Scene2DNode::kLfCount; k++)
                  if (float* f = Scene2DNode::LayerFieldPtr(l, k); f != nullptr && Scene2DNode::LayerFieldApplies(l, k))
                     *f = std::round(rnd() * 100.0f) / 100.0f;
               l.blend = (int)(rnd() * 20);
               l.opacity = std::round(rnd() * 100.0f) / 100.0f;
               for (float& v : l.levels)
                  v = std::round(rnd() * 100.0f) / 100.0f;
               l.curve = { 0.0f, std::round(rnd() * 100.0f) / 100.0f, 0.5f, std::round(rnd() * 100.0f) / 100.0f, 1.0f, 1.0f };
               l.relief = l.kind == Kind::Paint && rnd() < 0.5f;
               for (Scene2DNode::Fx& fx : l.fx)
               {
                  fx.on = rnd() < 0.4f;
                  fx.size = std::round(rnd() * 40.0f);
                  fx.dist = std::round(rnd() * 40.0f);
               }
            }
            a.Commit();
            const std::string ta = a.DocText();
            Scene2DNode b;
            b.SetDocText(ta);
            b.Layers();
            b.Commit();
            if (b.DocText() != ta)
            {
               fixed = false;
               // The first line that differs, for the log.
               std::istringstream sa(ta), sb(b.DocText());
               for (std::string la, lb; std::getline(sa, la) && std::getline(sb, lb);)
                  if (la != lb)
                  {
                     firstBad = la + "  ->  " + lb;
                     break;
                  }
            }
         }
         if (!fixed)
            printf("  B4 fuzz: first differing line: %s\n", firstBad.c_str());
         Check("B4 sweep: a random document saves, loads and saves to the same text", fixed);
      }
   }
   // Sweep: every layer setting the panel writes (relief, levels, curve, layer fx, adjustment) survives an undo
   // round trip: change it, undo, and the document's text is what it was. The History panel names the step.
   {
      doc = Doc();
      int paint = 0, adj = 0;
      if (doc != nullptr)
      {
         paint = doc->AddPaintLayer();
         adj = doc->AddLayer(Kind::Adjust, 0).id;
         doc->Commit();
      }
      struct Write
      {
         const char* what;
         std::function<void(Layer&, Layer&)> change;
      };
      const std::vector<Write> writes = {
         { "relief on", [](Layer& p, Layer&) { p.relief = true; } },
         { "relief material", [](Layer& p, Layer&) { p.relief = true; p.reliefMat = 2; } },
         { "relief light", [](Layer& p, Layer&) { p.relief = true; p.reliefLight = -30.0f; p.reliefElev = 70.0f; p.reliefDepth = 5.0f; } },
         { "levels", [](Layer&, Layer& a) { a.adjType = Scene2DNode::kAdjLevels; a.levels[0] = 0.2f; a.levels[3] = 0.8f; } },
         { "curve", [](Layer&, Layer& a) { a.adjType = Scene2DNode::kAdjCurves; a.curve = { 0.0f, 0.1f, 0.5f, 0.7f, 1.0f, 0.9f }; } },
         { "adjustment", [](Layer&, Layer& a) { a.adjType = Scene2DNode::kAdjPosterize; a.adjAmt = 3.0f; a.adjMap = true; } },
         { "layer fx", [](Layer& p, Layer&) { p.fx[0].on = true; p.fx[0].dist = 33.0f; p.fx[0].c[0] = 0.5f; } },
         { "fill and opacity", [](Layer& p, Layer&) { p.fill = 0.4f; p.opacity = 0.6f; p.blend = 3; } },
      };
      int bad = 0;
      for (const Write& w : writes)
      {
         doc = Doc();
         Layer* p = doc != nullptr ? doc->FindLayer(paint) : nullptr;
         Layer* a = doc != nullptr ? doc->FindLayer(adj) : nullptr;
         if (p == nullptr || a == nullptr)
         {
            printf("  undo round trip: %s found no layers (doc=%d paint=%d adj=%d)\n", w.what, doc != nullptr, paint, adj);
            bad++;
            break;
         }
         const std::string before = doc->DocText();
         PushUndoCheckpoint(w.what);
         w.change(*p, *a);
         doc->Commit();
         const bool changed = doc->DocText() != before, named = UndoLabelAt(0) == w.what;
         Undo();
         doc = Doc();
         const bool back = doc != nullptr && doc->DocText() == before;
         if (!changed || !named || !back)
         {
            printf("  undo round trip: %s changed=%d named=%d back=%d\n", w.what, changed, named, back);
            bad++;
         }
      }
      Check("Sweep: every layer setting undoes back to the same document", bad == 0);
      doc = Doc();
   }
   if (doc != nullptr && View2DPanelSelfCheck(doc) > 0)
      ok = false;
   printf("VIEW2DTEST %s\n", ok ? "OK" : "FAIL");
   // Done: quit with the result (no teardown), so a runner needs no timeout kill.
   fflush(stdout);
   std::_Exit(ok ? 0 : 1);
}

} // namespace app
