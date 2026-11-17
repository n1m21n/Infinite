// Menu-alive self-test (INFINITE_MENUALIVETEST). Every right-click menu in the app, one table row each. Through real
// mouse events the test opens a menu, walks into every submenu and clicks every item, each on a fresh setup (the
// showcase template loaded again, or a small 3D scene for the 3D View). An item passes when its click changed
// something a person could see, measured against the same setup with the menu opened and closed without a click:
//   - the patch (nodes, timeline, session, everything the file holds), the clipboard, the menu's own extra state;
//   - what each window draws (a hash of its vertices), leaving out windows that change on their own;
//   - or a dialog / popup the item opened.
// Every menu also gets a "(no click)" control row that must come out the same; if it does not, the menu's results
// cannot be trusted and the menu fails. Greyed items are listed, not clicked; items that open a file dialog ("...")
// or start an export are skipped. A checked choice clicked again, or Select Random, may land where it started.
// A menu whose setup has nothing to right-click fails ("did not open"). A crash ends the run without the OK line.
//   INFINITE_MENUALIVE_MENU=<text>  only menus whose name holds it
//   INFINITE_MENUALIVE_ONLY=<text>  only click rows whose path holds it (submenus are still walked)
// Plan: internal/plans/ui-system/lint-plan.md (D10, Block E).
#include "app/AppShared.h"
#include "app/ui/design/components/MenuParts.h"
#include "core/Patch.h"
#include "core/Transport.h"
#include "nodes/Scene2DNode.h"
#include "imgui_internal.h"

#include <GLFW/glfw3.h>
#include "imgui_impl_glfw.h"

#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace app
{
void View3DAddNode(GraphNode& gn);
void View3DTestOpenMenu(bool onItem);
void View3DTestPose();
std::string View3DTestFingerprint();
extern bool gCurveEditorOpen;
extern bool gFieldPatcherOpen;
void FieldPatcherOpenStarter(int index);
std::string ArrangeSessionTestFingerprint();
std::string FieldPatcherTestFingerprint();
void ArrangeSessionTestReset();
void FieldPatcherTestReset();
std::string View2DTestFingerprint();
void View2DTestReset();
std::string SampleStudioTestFingerprint();
void SampleStudioTestReset();
void SampleStudioStatus(bool& busy, bool& ready, std::string& name, double& seconds);
std::vector<std::string> SampleStudioParts();
bool SampleStudioGenerate(const std::string& part, int bars, std::string& err);

#ifdef IMGUI_ENABLE_TEST_ENGINE
namespace
{
   struct Menu
   {
      std::string name;   // "Timeline clip"
      std::string root;   // its popup id
      std::function<void()> setup;               // after the fixture: show the panel, add what the menu acts on
      std::function<void()> open;                // asks the panel to open the menu (default: MenuParts::TestOpen)
      std::function<std::string()> extra;        // state the patch and the drawing do not show
      std::function<bool()> ready;               // the setup has finished (a take rendered): the menu may open
      bool fixture = true;                       // false: the setup builds its own patch
   };
   std::vector<Menu> sMenus;

   struct Job
   {
      int menu = 0;
      std::vector<std::string> path;   // submenus to open, then (for a click job) the item
      bool click = false;
      bool checked = false;   // the row is the current choice: clicking it again may rightly change nothing
      bool control = false;   // "(no click)": open and close; must come out the same
   };
   // Rows whose effect is random and may land where it started.
   bool MayKeep(const std::string& name) { return name == "Select Random"; }
   // Rows that leave the app (export, render a file, reveal in Finder, open a web page): listed, not clicked.
   bool LeavesApp(const std::string& name)
   {
      static const char* kWords[] = { "Export", "Render", "Reveal", "Finder", "Explorer", "Browse", "Share", "Upload", "Website", "Docs", "Bounce" };
      for (const char* w : kWords)
         if (name.find(w) != std::string::npos)
            return true;
      return false;
   }

   std::deque<Job> sJobs;
   int sPhase = 0, sWaitUntil = 0, sStep = 0, sTries = 0;
   ImVec2 sMouse(-FLT_MAX, -FLT_MAX);
   int sPass = 0, sFail = 0, sGrey = 0, sSkip = 0;
   bool sDone = false, sActive = false;
   std::set<std::string> sBadMenus;
   // The setup is still as fresh as a reload would make it: the last job ran on this menu and changed nothing.
   int sCleanMenu = -1;

   // ---- what a person could see

   // Per window, a hash of the vertices it drew last frame (taken right after ImGui::Render, so every list is whole).
   std::map<std::string, uint64_t> sDrawn;
   uint64_t Mix(uint64_t h, uint64_t v) { return (h ^ v) * 1099511628211ull; }
   void HashDrawLists()
   {
      sDrawn.clear();
      for (ImGuiWindow* w : GImGui->Windows)
      {
         if (!w->WasActive || w->Hidden || w->DrawList == nullptr)
            continue;
         if (w->Flags & (ImGuiWindowFlags_Popup | ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_ChildMenu))
            continue;
         if (w->RootWindow != nullptr && (w->RootWindow->Flags & (ImGuiWindowFlags_Popup | ImGuiWindowFlags_Tooltip)))
            continue;
         uint64_t h = 1469598103934665603ull;
         const ImDrawList* dl = w->DrawList;
         for (const ImDrawVert& v : dl->VtxBuffer)
         {
            h = Mix(h, (uint64_t)(int64_t)(v.pos.x * 4.0f));
            h = Mix(h, (uint64_t)(int64_t)(v.pos.y * 4.0f));
            h = Mix(h, v.col);
         }
         for (const ImDrawCmd& c : dl->CmdBuffer)
            h = Mix(h, (uint64_t)(uintptr_t)c.TexRef._TexData ^ (uint64_t)c.TexRef._TexID);   // raw: GetTexID asserts on a texture not uploaded yet
         sDrawn[w->Name] = h;
      }
   }

   struct Snap
   {
      std::string state;
      std::map<std::string, uint64_t> drawn;
   };
   std::string PatchText()
   {
      std::string t;
      Patch::WriteText(BuildPatchData(), t);
      return t;
   }
   Snap Take(const Menu& m)
   {
      Snap s;
      s.state = PatchText();
      if (const char* clip = ImGui::GetClipboardText())
         s.state += std::string("\nclip:") + clip;
      // A text field that took the keys (an inline rename) counts as seen.
      s.state += GImGui->ActiveId != 0 ? "\nactive" : "";
      s.state += "\nrenaming:" + std::to_string(gArrangeRenamingClipId) + "," + std::to_string(gArrangeRenamingMarkerId);
      s.state += "\n" + ArrangeSessionTestFingerprint() + "\n" + FieldPatcherTestFingerprint() + "\n" + View2DTestFingerprint() + "\n" + SampleStudioTestFingerprint();
      if (m.extra)
         s.state += "\nextra:" + m.extra();
      s.drawn = sDrawn;
      return s;
   }
   // Windows that changed between two snaps, and whether the state did.
   std::vector<std::string> Differ(const Snap& a, const Snap& b, const std::set<std::string>& skip, bool* stateDiffers)
   {
      std::vector<std::string> out;
      std::set<std::string> names;
      for (const auto& [k, v] : a.drawn)
         names.insert(k);
      for (const auto& [k, v] : b.drawn)
         names.insert(k);
      for (const std::string& n : names)
      {
         if (skip.count(n))
            continue;
         auto ia = a.drawn.find(n), ib = b.drawn.find(n);
         if (ia == a.drawn.end() || ib == b.drawn.end() || ia->second != ib->second)
            out.push_back(n);
      }
      if (stateDiffers)
         *stateDiffers = a.state != b.state;
      return out;
   }

   // The first line two states differ on, for the report.
   std::string FirstDiff(const std::string& a, const std::string& b)
   {
      size_t i = 0;
      while (i < a.size() && i < b.size() && a[i] == b[i])
         i++;
      const size_t from = i > 40 ? i - 40 : 0;
      auto near = [from](const std::string& t) { std::string x = t.substr(from, 100); for (char& c : x) if (c == '\n') c = '|'; return x; };
      return " {" + near(a) + "} -> {" + near(b) + "}";
   }

   // State lines keyed by their text before the value plus which occurrence it is ("  f a.position#3"), so lines
   // that move on their own (an animated parameter, a playhead) can be left out of the comparison.
   std::map<std::string, std::string> Keyed(const std::string& t)
   {
      std::map<std::string, std::string> out;
      std::map<std::string, int> seen;
      size_t at = 0;
      while (at <= t.size())
      {
         size_t e = t.find('\n', at);
         if (e == std::string::npos)
            e = t.size();
         const std::string line = t.substr(at, e - at);
         // The indent, the type and the name ("  f a.position"): a value with spaces in it stays one key.
         size_t sp = line.find_first_not_of(' ');
         for (int w = 0; w < 2 && sp != std::string::npos; w++)
         {
            sp = line.find(' ', sp);
            if (sp != std::string::npos && w == 0)
               sp = line.find_first_not_of(' ', sp);
         }
         const std::string head = sp == std::string::npos ? line : line.substr(0, sp);
         out[head + "#" + std::to_string(seen[head]++)] = line;
         at = e + 1;
      }
      return out;
   }
   std::set<std::string> sNoisyLines;
   std::string Masked(const std::string& t)
   {
      std::string out;
      for (const auto& [k, v] : Keyed(t))
         if (!sNoisyLines.count(k))
            out += v + "\n";
      return out;
   }

   Snap sBase, sBase2;
   std::set<std::string> sNoisy;

   // ---- the menu's rows

   std::vector<MenuParts::ProbeRow> Rows(const std::string& root, int depth)
   {
      std::vector<MenuParts::ProbeRow> out;
      const auto& v = MenuParts::ProbeRows();
      int last = -1;
      for (const auto& r : v)
         if (r.root == root)
            last = std::max(last, r.frame);
      if (last < ImGui::GetFrameCount() - 3)
         return out;   // not drawn lately: the menu is closed
      for (const auto& r : v)
         if (r.root == root && r.frame == last && r.depth == depth)
            out.push_back(r);
      return out;
   }
   bool Find(const std::string& root, int depth, const std::string& label, bool sub, MenuParts::ProbeRow& out)
   {
      for (const auto& r : Rows(root, depth))
         if (r.label == label && r.sub == sub)
         {
            out = r;
            return true;
         }
      return false;
   }
   ImVec2 Mid(const MenuParts::ProbeRow& r) { return ImVec2((r.mn.x + r.mx.x) * 0.5f, (r.mn.y + r.mx.y) * 0.5f); }

   std::string Name(const std::string& label)
   {
      const size_t h = label.find("##");
      return h == std::string::npos ? label : label.substr(0, h);
   }
   std::string PathText(const Job& j)
   {
      std::string s = sMenus[(size_t)j.menu].name;
      for (const std::string& p : j.path)
         s += " > " + Name(p);
      if (j.control)
         s += " > (no click)";
      return s;
   }
   void Report(const Job& j, const std::string& what, bool pass)
   {
      printf("  [%s] %s%s\n", pass ? "pass" : "FAIL", PathText(j).c_str(), what.c_str());
      fflush(stdout);
      (pass ? sPass : sFail)++;
      if (!pass)
         sBadMenus.insert(sMenus[(size_t)j.menu].name);
   }

   // ---- setups

   void HideAll()
   {
      gDrumPanelOpen = gMixerPanelOpen = gPianoRollOpen = gCurveEditorOpen = gFieldPatcherOpen = gPerfPanelOpen = false;
      gView2DOpen = gView3DOpen = gArrangePanelOpen = gSessionPanelOpen = gSampleStudioOpen = gModMatrixOpen = false;
      gViewportPanelOpen = gTerminalOpen = false;
      gArrangeClipSettingsPanelOpen = false;   // the clip inspector a Timeline row opened
      gArrangeSettingsPanelTarget = 0;
   }
   // A fresh app holds nothing on any clipboard: a copy before this job must not make this job's copy look dead.
   void ResetClipboards()
   {
      ImGui::SetClipboardText("");
      ArrangeSessionTestReset();
      FieldPatcherTestReset();
      View2DTestReset();
      SampleStudioTestReset();
   }
   void LoadShowcase()
   {
      ImGui::ClosePopupsExceptModals();
      ResetClipboards();
      LoadPatchFrom(BundledResourcePath("templates/infinite-v1-showcase.inf"));
      HideAll();
   }
   void Show(bool& open) { open = true; }
   // A track group over the first two tracks, so the group header menu has a target.
   void EnsureTrackGroup()
   {
      if (gArrange.trackGroups.empty() && gArrange.lanes.size() >= 2)
         Arrange::AddTrackGroup(gArrange, { gArrange.lanes[0].id, gArrange.lanes[1].id });
   }
   void Build2D()
   {
      gView2DOpen = true;
      GraphNode* shape = SpawnNode("Shape", "Source", 0.0f, 2000.0f);
      GraphNode* text = SpawnNode("Text", "Source", 0.0f, 2200.0f);
      if (shape != nullptr)
         View2DAddNode(*shape);
      if (text != nullptr)
         View2DAddNode(*text);
      for (GraphNode& gn : gNodes)
         if (auto* doc = dynamic_cast<Scene2DNode*>(gn.node.get()))
         {
            doc->AddPaintLayer();
            break;
         }
   }
   // An MSEG on the canvas: the Curve Editor's page.
   void BuildCurve()
   {
      gCurveEditorOpen = true;
      SpawnNode("MSEG", "Modulators", 0.0f, 2000.0f);
   }
   // A starter graph (boxes and wires) in the Field Patcher.
   void BuildField() { FieldPatcherOpenStarter(0); }
   // A one-bar take in the Sample Studio, so the wave has something to right-click.
   void BuildStudio()
   {
      gSampleStudioOpen = true;
      const std::vector<std::string> parts = SampleStudioParts();
      std::string err;
      if (!parts.empty())
         SampleStudioGenerate(parts[0], 1, err);
   }
   void Build3D()
   {
      ImGui::ClosePopupsExceptModals();
      ResetClipboards();
      NewPatch();
      HideAll();
      gView3DOpen = true;
      // Two cubes and a sphere: Select All, Same Type and Linked each have work to do.
      if (GraphNode* cube = SpawnNode("Cube", "3D", 0.0f, 0.0f))
         View3DAddNode(*cube);
      if (GraphNode* cube = SpawnNode("Cube", "3D", 0.0f, 200.0f))
         View3DAddNode(*cube);
      if (GraphNode* ball = SpawnNode("Sphere", "3D", 0.0f, 400.0f))
         View3DAddNode(*ball);
   }

   void BuildTable()
   {
      auto add = [](std::string name, std::string root, std::function<void()> setup) {
         Menu m;
         m.name = std::move(name);
         m.root = std::move(root);
         m.setup = std::move(setup);
         sMenus.push_back(std::move(m));
      };
      // Canvas
      add("Canvas node", "##nodecontext", [] {});
      // Timeline (Ableton Arrangement / Premiere)
      // Stopped, the playhead inside the clip the menu opens on: Split at Playhead has something to cut.
      add("Timeline clip", "##arrangeclipctx", [] {
         Show(gArrangePanelOpen);
         Transport::Instance().SetPlaying(false);
         for (const Arrange::Lane& l : gArrange.lanes)
            if (!l.clips.empty())
            {
               ArrangeSeekTick(l.clips.front().start + l.clips.front().length / 2);
               break;
            }
      });
      add("Timeline track", "##arrangelanectx", [] { Show(gArrangePanelOpen); });
      add("Timeline group", "##arrangegroupctx", [] { Show(gArrangePanelOpen); EnsureTrackGroup(); });
      add("Timeline marker", "##arrangemarkerctx", [] { Show(gArrangePanelOpen); });
      add("Timeline empty space", "##arrangeemptyspacectx", [] { Show(gArrangePanelOpen); });
      add("Timeline monitor", "##arrangeviewportctx", [] { Show(gArrangePanelOpen); });
      add("Timeline dock", "##arrangeDockMenu", [] { Show(gArrangePanelOpen); });
      // Session (Ableton Session)
      add("Session clip", "##arrangeclipctx", [] { Show(gSessionPanelOpen); });
      add("Session empty slot", "##sessionslotctx", [] { Show(gSessionPanelOpen); });
      add("Session scene", "##sessionscenectx", [] { Show(gSessionPanelOpen); });
      add("Session track", "##sessiontrackctx", [] { Show(gSessionPanelOpen); });
      add("Session dock", "##sessiondockctx", [] { Show(gSessionPanelOpen); });
      // 2D View (Photoshop)
      add("2D View layer", "##rowmenu", [] { Build2D(); });
      add("2D View dock", "##v2ddockmenu", [] { Build2D(); });
      // 3D View (Blender): its own scene and opener, as in D10
      {
         Menu m;
         m.name = "3D View object";
         m.root = "##v3dcontext";
         m.fixture = false;
         m.setup = [] { Build3D(); };
         m.open = [] { View3DTestPose(); View3DTestOpenMenu(true); };
         m.extra = [] { return View3DTestFingerprint(); };
         sMenus.push_back(m);
         m.name = "3D View empty space";
         m.open = [] { View3DTestPose(); View3DTestOpenMenu(false); };
         sMenus.push_back(m);
         Menu d;
         d.name = "3D View dock";
         d.root = "##v3ddockmenu";
         d.fixture = false;
         d.setup = [] { Build3D(); };
         d.extra = [] { return View3DTestFingerprint(); };
         sMenus.push_back(d);
      }
      // The other docked panels
      add("Mixer", "##mixerpanelctx", [] { Show(gMixerPanelOpen); });
      add("Mod Matrix", "##modmatrixctx", [] { Show(gModMatrixOpen); });
      add("Viewport", "##viewportpanelctx", [] { Show(gViewportPanelOpen); });
      add("Curve Editor", "##curveMenu", [] { BuildCurve(); });
      add("Curve Editor dock", "##curveDockMenu", [] { BuildCurve(); });
      add("Sample Studio wave", "##studioWaveMenu", [] { BuildStudio(); });
      sMenus.back().ready = [] { bool busy = false, ready = false; std::string n; double sec = 0.0; SampleStudioStatus(busy, ready, n, sec); return ready; };
      add("Sample Studio dock", "##studioDockMenu", [] { BuildStudio(); });
      add("Piano Roll dock", "##pianoDockMenu", [] { Show(gPianoRollOpen); });
      add("Drums dock", "##drumDockMenu", [] { Show(gDrumPanelOpen); });
      add("Terminal dock", "##terminalDockMenu", [] { Show(gTerminalOpen); });
      add("Field Patcher box", "##fpNodeMenu", [] { BuildField(); });
      add("Field Patcher wire", "##fpLinkMenu", [] { BuildField(); });
      add("Field Patcher dock", "##fpDockMenu", [] { BuildField(); });
   }

   // ---- one job, a frame at a time. Returns true when the job is finished.

   void Park() { sMouse = ImVec2(-FLT_MAX, -FLT_MAX); }
   bool MenuShown(const Menu& m) { return !Rows(m.root, 0).empty(); }
   void RequestOpen(const Menu& m)
   {
      if (m.open)
         m.open();
      else
         MenuParts::TestOpenWant() = m.root;
   }

   bool RunJob(Job& j, int frame)
   {
      ImGuiIO& io = ImGui::GetIO();
      const Menu& m = sMenus[(size_t)j.menu];
      if (frame < sWaitUntil)
         return false;
      auto wait = [&](int n) { sWaitUntil = frame + n; };
      switch (sPhase)
      {
      case 0:   // a fresh setup
         Park();
         sPhase = 1;
         if (sCleanMenu == j.menu)
         {
            sTries = 0;
            ImGui::ClosePopupsExceptModals();
            wait(2);
            return false;
         }
         if (m.fixture)
            LoadShowcase();
         m.setup();
         sTries = 0;
         wait(m.fixture ? 15 : 8);
         return false;
      case 1:   // open the menu
         if (m.ready && !m.ready() && ++sTries < 900)
            return false;
         RequestOpen(m);
         sPhase = 2;
         sTries = 0;
         wait(3);
         return false;
      case 2:   // wait for it
         if (!MenuShown(m))
         {
            if (++sTries % 20 == 0 && sTries < 200)
               RequestOpen(m);   // a take still rendering, a page still loading: ask again
            if (sTries < 200)
               return false;
            MenuParts::TestOpenWant().clear();
            sCleanMenu = -1;
            Report(j, ": did not open (nothing to right-click in the setup?)", false);
            sPhase = 0;
            return true;
         }
         if (!j.click && !j.control)
         {
            sStep = 0;
            sPhase = 20;   // a list job: straight to the walk
            return false;
         }
         // Baseline: the same setup, the menu closed without a click.
         ImGui::ClosePopupsExceptModals();
         Park();
         sPhase = 3;
         wait(4);
         return false;
      case 3:
         sBase = Take(m);
         sPhase = 4;
         wait(8);
         return false;
      case 4:
      {
         sBase2 = Take(m);
         bool stateMoved = false;
         const std::vector<std::string> moved = Differ(sBase, sBase2, {}, &stateMoved);
         sNoisy = std::set<std::string>(moved.begin(), moved.end());
         // Kept for the whole run: a slow animated value missed by one baseline is caught by the next.
         if (stateMoved)
         {
            const auto a = Keyed(sBase.state), b = Keyed(sBase2.state);
            for (const auto& [k, v] : b)
            {
               auto it = a.find(k);
               if (it == a.end() || it->second != v)
                  sNoisyLines.insert(k);
            }
            for (const auto& [k, v] : a)
               if (!b.count(k))
                  sNoisyLines.insert(k);
         }
         sBase2.state = Masked(sBase2.state);
         RequestOpen(m);
         sPhase = 5;
         sTries = 0;
         wait(3);
         return false;
      }
      case 5:
         if (!MenuShown(m))
         {
            if (++sTries < 30)
               return false;
            sCleanMenu = -1;
            Report(j, ": did not open a second time", false);
            sPhase = 0;
            return true;
         }
         sStep = 0;
         sPhase = 20;
         return false;
      case 20:   // open the submenus on the path, one hover each
      {
         const size_t subs = j.click ? j.path.size() - 1 : j.path.size();
         if ((size_t)sStep < subs)
         {
            MenuParts::ProbeRow r;
            if (!Find(m.root, sStep, j.path[(size_t)sStep], true, r))
            {
               if (++sTries < 20)
                  return false;
               sCleanMenu = -1;
            Report(j, ": submenu not shown", false);
               sPhase = 0;
               return true;
            }
            sMouse = Mid(r);
            sStep++;
            sTries = 0;
            wait(6);
            return false;
         }
         if (j.control)
         {
            ImGui::ClosePopupsExceptModals();
            Park();
            sPhase = 30;
            wait(10);
            return false;
         }
         if (!j.click)
         {
            // List the open level: submenus become list jobs, items become click jobs.
            const std::vector<MenuParts::ProbeRow> rows = Rows(m.root, (int)j.path.size());
            if (rows.empty() && ++sTries < 20)
               return false;
            if (j.path.empty())
            {
               Job c{ j.menu, {}, false, false, true };
               sJobs.push_back(c);
            }
            for (const auto& r : rows)
            {
               Job n{ j.menu, j.path, !r.sub, r.checked, false };
               n.path.push_back(r.label);
               if (!r.enabled)
               {
                  printf("  [grey] %s\n", PathText(n).c_str());
                  sGrey++;
                  continue;
               }
               const std::string nm = Name(r.label);
               if (n.click && nm.size() >= 3 && nm.compare(nm.size() - 3, 3, "...") == 0)
               {
                  printf("  [skip] %s (opens a file dialog)\n", PathText(n).c_str());
                  sSkip++;
                  continue;
               }
               if (n.click && LeavesApp(nm))
               {
                  printf("  [skip] %s (leaves the app)\n", PathText(n).c_str());
                  sSkip++;
                  continue;
               }
               const char* only = getenv("INFINITE_MENUALIVE_ONLY");
               if (n.click && only != nullptr && PathText(n).find(only) == std::string::npos)
                  continue;
               sJobs.push_back(n);
            }
            ImGui::ClosePopupsExceptModals();
            sCleanMenu = j.menu;
            sPhase = 0;
            return true;
         }
         MenuParts::ProbeRow r;
         if (!Find(m.root, (int)j.path.size() - 1, j.path.back(), false, r))
         {
            if (++sTries < 20)
               return false;
            sCleanMenu = -1;
            Report(j, ": item not shown", false);
            sPhase = 0;
            return true;
         }
         sMouse = Mid(r);
         sPhase = 21;
         wait(3);
         return false;
      }
      case 21:
         io.AddMouseButtonEvent(0, true);
         sPhase = 22;
         wait(1);
         return false;
      case 22:
         io.AddMouseButtonEvent(0, false);
         sPhase = 23;
         wait(1);
         return false;
      case 23:
         Park();
         sPhase = 30;
         wait(8);
         return false;
      case 30:
      {
         // A popup or modal still open is the item's own (a rename, a confirm, a picker), unless it is the menu itself.
         bool ownPopup = false;
         for (const ImGuiPopupData& p : GImGui->OpenPopupStack)
            if (p.Window != nullptr && !(p.Window->Flags & ImGuiWindowFlags_ChildMenu) && !MenuShown(m))
               ownPopup = true;
         Snap after = Take(m);
         after.state = Masked(after.state);
         bool stateMoved = false;
         const std::vector<std::string> moved = Differ(sBase2, after, sNoisy, &stateMoved);
         const bool changed = stateMoved || !moved.empty() || ownPopup;
         sCleanMenu = changed ? -1 : j.menu;
         // A control drift once in its state can be the load still settling (a seed rolled late): reload, take a
         // fresh baseline and run the control once more. A second drift fails. Nothing is learned as noise here, so
         // a later row's real edit in the same line still shows.
         static bool sControlRetried = false;
         if (j.control && stateMoved && moved.empty() && !ownPopup && !sControlRetried)
         {
            sControlRetried = true;
            sCleanMenu = -1;
            ImGui::ClosePopupsExceptModals();
            sPhase = 0;
            return false;
         }
         sControlRetried = false;
         if (j.control)
         {
            std::string why;
            for (const std::string& w : moved)
               why += " [" + w + "]";
            Report(j, changed ? ": opening and closing changed" + why + (stateMoved ? " state" + FirstDiff(sBase2.state, after.state) : std::string()) + (ownPopup ? " popup" : "") : "", !changed);
         }
         else if (!changed && (j.checked || MayKeep(Name(j.path.back()))))
         {
            printf("  [same] %s (%s)\n", PathText(j).c_str(), j.checked ? "already the choice" : "random");
            sPass++;
         }
         else
            Report(j, "", changed);
         ImGui::ClosePopupsExceptModals();
         sPhase = 0;
         return true;
      }
      }
      return true;
   }
}

// Called right after ImGui::Render: every window's draw list is whole here.
void MenuAliveAfterRender()
{
   if (sActive)
      HashDrawLists();
}

void RunMenuAliveTest(int frameId, GLFWwindow* window)
{
   static bool sBuilt = false;
   ImGuiIO& io = ImGui::GetIO();
   if (!sBuilt)
   {
      if (frameId < 4)
         return;
      sBuilt = true;
      sActive = true;
      io.ConfigInputTrickleEventQueue = false;
      GImGui->TestEngineHookItems = true;
      BuildTable();
      const char* only = getenv("INFINITE_MENUALIVE_MENU");
      for (int i = 0; i < (int)sMenus.size(); i++)
         if (only == nullptr || sMenus[(size_t)i].name.find(only) != std::string::npos)
            sJobs.push_back({ i, {}, false, false, false });
   }
   GImGui->TestEngineHookItems = true;
   io.AddMousePosEvent(sMouse.x, sMouse.y);
   if (sMouse.x > -FLT_MAX)
      glfwSetCursorPos(window, (double)(sMouse.x * ImGui_ImplGlfw_GetPointScale()), (double)(sMouse.y * ImGui_ImplGlfw_GetPointScale()));
   if (sDone)
      return;
   if (sJobs.empty())
   {
      sDone = true;
      printf("MENUALIVE: %d alive, %d dead, %d greyed, %d skipped in %.0f s\n", sPass, sFail, sGrey, sSkip, ImGui::GetTime());
      for (const std::string& n : sBadMenus)
         printf("  menu with fails: %s\n", n.c_str());
      printf("MENUALIVETEST %s\n", sFail == 0 && sPass > 0 ? "OK" : "FAIL");
      fflush(stdout);
      glfwSetWindowShouldClose(window, GLFW_TRUE);
      return;
   }
   if (RunJob(sJobs.front(), frameId))
      sJobs.pop_front();
}
#else
void MenuAliveAfterRender() {}
void RunMenuAliveTest(int, GLFWwindow*)
{
   printf("MENUALIVETEST FAIL (needs a dev build: the test engine hooks)\n");
}
#endif
}
