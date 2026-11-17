// Menu-alive self-test (INFINITE_MENUALIVETEST). Through real mouse events it opens the 3D View's right-click menu,
// on an object and on empty space, walks into every submenu and clicks every item, each on a fresh scene (one cube,
// moved, turned and scaled off the origin, 3D cursor off the origin too). An item passes when its click changed
// something a person could see: the scene, the selection, the view, a mode, a copy buffer, a rename, a request to the
// canvas, or a dialog or popup it opened. Greyed items are listed, not clicked; items that open a file dialog ("...")
// are skipped. A checked choice clicked again, or Select Random, may land where it started. A crash or assert on a click ends the run without the OK line.
// Plan: internal/plans/ui-system/lint-plan.md (D10).
#include "app/AppShared.h"
#include "app/ui/design/components/MenuParts.h"
#include "imgui_internal.h"

#include <GLFW/glfw3.h>
#include "imgui_impl_glfw.h"

#include <cstdio>
#include <cstdlib>
#include <deque>
#include <string>
#include <vector>

namespace app
{
void View3DAddNode(GraphNode& gn);
void View3DTestOpenMenu(bool onItem);
void View3DTestPose();
std::string View3DTestFingerprint();

#ifdef IMGUI_ENABLE_TEST_ENGINE
namespace
{
   constexpr const char* kRoot = "##v3dcontext";

   struct Job
   {
      bool onItem = true;
      std::vector<std::string> path;   // submenus to open, then (for a click job) the item
      bool click = false;
      bool checked = false;   // the row is the current choice: clicking it again may rightly change nothing
   };
   // Rows whose effect is random and may land where it started.
   bool MayKeep(const std::string& name) { return name == "Select Random"; }

   std::deque<Job> sJobs;
   int sPhase = 0, sWaitUntil = 0, sStep = 0, sTries = 0;
   ImVec2 sMouse(-1.0f, -1.0f);
   std::string sBefore;
   int sPass = 0, sFail = 0, sGrey = 0, sSkip = 0;
   bool sDone = false;

   std::string Name(const std::string& label)
   {
      const size_t h = label.find("##");
      return h == std::string::npos ? label : label.substr(0, h);
   }
   std::string PathText(const Job& j)
   {
      std::string s = j.onItem ? "on object" : "on empty space";
      for (const std::string& p : j.path)
         s += " > " + Name(p);
      return s;
   }

   // The latest frame's rows of the 3D View menu at one depth.
   std::vector<MenuParts::ProbeRow> Rows(int depth)
   {
      std::vector<MenuParts::ProbeRow> out;
      const auto& v = MenuParts::ProbeRows();
      int last = -1;
      for (const auto& r : v)
         if (r.root == kRoot)
            last = std::max(last, r.frame);
      for (const auto& r : v)
         if (r.root == kRoot && r.frame == last && r.depth == depth)
            out.push_back(r);
      return out;
   }
   const MenuParts::ProbeRow* Find(int depth, const std::string& label, bool sub)
   {
      static std::vector<MenuParts::ProbeRow> rows;
      rows = Rows(depth);
      for (const auto& r : rows)
         if (r.label == label && r.sub == sub)
            return &r;
      return nullptr;
   }
   ImVec2 Mid(const MenuParts::ProbeRow& r) { return ImVec2((r.mn.x + r.mx.x) * 0.5f, (r.mn.y + r.mx.y) * 0.5f); }

   void Report(const std::string& what, bool pass)
   {
      printf("  [%s] %s\n", pass ? "pass" : "FAIL", what.c_str());
      fflush(stdout);
      (pass ? sPass : sFail)++;
   }

   // One job, a frame at a time. Returns true when the job is finished.
   bool RunJob(Job& j, int frame)
   {
      ImGuiIO& io = ImGui::GetIO();
      if (frame < sWaitUntil)
         return false;
      auto wait = [&](int n) { sWaitUntil = frame + n; };
      switch (sPhase)
      {
      case 0:   // a fresh scene: one cube, selected, off the origin
         ImGui::ClosePopupsExceptModals();
         NewPatch();
         gView3DOpen = true;
         sMouse = ImVec2(-1.0f, -1.0f);
         // Two cubes and a sphere: Select All, Same Type and Linked (the cubes are grouped) each have work to do.
         if (GraphNode* cube = SpawnNode("Cube", "3D", 0.0f, 0.0f))
            View3DAddNode(*cube);
         if (GraphNode* cube = SpawnNode("Cube", "3D", 0.0f, 200.0f))
            View3DAddNode(*cube);
         if (GraphNode* ball = SpawnNode("Sphere", "3D", 0.0f, 400.0f))
            View3DAddNode(*ball);
         sPhase = 1;
         wait(8);
         return false;
      case 1:
         View3DTestPose();
         View3DTestOpenMenu(j.onItem);
         sPhase = 2;
         sStep = 0;
         sTries = 0;
         wait(4);
         return false;
      case 2:   // open the submenus on the path, one hover each
      {
         const size_t subs = j.click ? j.path.size() - 1 : j.path.size();
         if ((size_t)sStep < subs)
         {
            const MenuParts::ProbeRow* r = Find(sStep, j.path[sStep], true);
            if (r == nullptr)
            {
               if (++sTries < 20)
                  return false;
               Report(PathText(j) + ": submenu not shown", false);
               sPhase = 0;
               return true;
            }
            sMouse = Mid(*r);
            sStep++;
            sTries = 0;
            wait(6);
            return false;
         }
         if (!j.click)
         {
            // List the open level: submenus become list jobs, items become click jobs.
            const std::vector<MenuParts::ProbeRow> rows = Rows((int)j.path.size());
            if (rows.empty() && ++sTries < 20)
               return false;
            for (const auto& r : rows)
            {
               Job n{ j.onItem, j.path, !r.sub, r.checked };
               n.path.push_back(r.label);
               if (!r.enabled)
               {
                  printf("  [grey] %s\n", PathText(n).c_str());
                  sGrey++;
                  continue;
               }
               const std::string nm = Name(r.label);
               if (nm.size() >= 3 && nm.compare(nm.size() - 3, 3, "...") == 0)
               {
                  printf("  [skip] %s (opens a file dialog)\n", PathText(n).c_str());
                  sSkip++;
                  continue;
               }
               // INFINITE_MENUALIVE_ONLY=<text>: click only rows whose path holds it (submenus are still walked).
               const char* only = getenv("INFINITE_MENUALIVE_ONLY");
               if (n.click && only != nullptr && PathText(n).find(only) == std::string::npos)
                  continue;
               sJobs.push_back(n);
            }
            sPhase = 0;
            return true;
         }
         const MenuParts::ProbeRow* r = Find((int)j.path.size() - 1, j.path.back(), false);
         if (r == nullptr)
         {
            if (++sTries < 20)
               return false;
            Report(PathText(j) + ": item not shown", false);
            sPhase = 0;
            return true;
         }
         sMouse = Mid(*r);
         sBefore = View3DTestFingerprint();
         sPhase = 3;
         wait(3);
         return false;
      }
      case 3:
         io.AddMouseButtonEvent(0, true);
         sPhase = 4;
         wait(1);
         return false;
      case 4:
         io.AddMouseButtonEvent(0, false);
         sPhase = 5;
         wait(6);
         return false;
      case 5:
      {
         // A popup or modal still open after the menu closed is the item's own (a rename, a confirm, a picker).
         bool ownPopup = false;
         for (const ImGuiPopupData& p : GImGui->OpenPopupStack)
            if (p.Window != nullptr && std::string(p.Window->Name).find("v3dcontext") == std::string::npos &&
                !(p.Window->Flags & ImGuiWindowFlags_ChildMenu))
               ownPopup = true;
         const std::string after = View3DTestFingerprint();
         const bool changed = after != sBefore || ownPopup;
         if (!changed && (j.checked || MayKeep(Name(j.path.back()))))
         {
            printf("  [same] %s (%s)\n", PathText(j).c_str(), j.checked ? "already the choice" : "random");
            sPass++;
         }
         else
            Report(PathText(j), changed);
         sPhase = 0;
         return true;
      }
      }
      return true;
   }
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
      io.ConfigInputTrickleEventQueue = false;
      GImGui->TestEngineHookItems = true;
      sJobs.push_back({ true, {}, false });
      sJobs.push_back({ false, {}, false });
   }
   GImGui->TestEngineHookItems = true;
   io.AddMousePosEvent(sMouse.x, sMouse.y);
   if (sMouse.x >= 0.0f)
      glfwSetCursorPos(window, (double)(sMouse.x * ImGui_ImplGlfw_GetPointScale()), (double)(sMouse.y * ImGui_ImplGlfw_GetPointScale()));
   if (sDone)
      return;
   if (sJobs.empty())
   {
      sDone = true;
      printf("MENUALIVE 3D View: %d alive, %d dead, %d greyed, %d skipped\n", sPass, sFail, sGrey, sSkip);
      printf("MENUALIVETEST %s\n", sFail == 0 && sPass > 0 ? "OK" : "FAIL");
      fflush(stdout);
      glfwSetWindowShouldClose(window, GLFW_TRUE);
      return;
   }
   if (RunJob(sJobs.front(), frameId))
      sJobs.pop_front();
}
#else
void RunMenuAliveTest(int, GLFWwindow*)
{
   printf("MENUALIVETEST FAIL (needs a dev build: the test engine hooks)\n");
}
#endif
}
