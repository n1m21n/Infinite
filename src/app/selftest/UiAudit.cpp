// UI audit, layers 2 and 3 (docs/plans/ui-system/ui-audit.md). Debug builds only: INFINITE_UIAUDIT=1 opens a
// 3D scene, steps the 3D View through each side tab (paging each one down), checks every item ImGui
// registered on the last frame, then drags each visible slider end to end and undoes it.
// tools/ui-audit/run.py launches it, writes the report and holds it against the baseline.
#include "app/AppShared.h"
#include "app/selftest/UiAudit.h"
#include "core/Notices.h"

#ifdef IMGUI_ENABLE_TEST_ENGINE
#include "imgui_internal.h"
#include <cmath>
#include <cstdarg>
#include <fstream>
#include <set>
#include <unordered_map>

namespace app { bool View3DAuditState(int tab); }

namespace
{
   struct Rec
   {
      ImGuiID id;
      ImRect bb, clip;
      std::string window, label;
      bool widget = false;     // ItemInfo came (a real control, not a bare ItemAdd)
      bool disabled = false;
      bool overlapOk = false;  // SetNextItemAllowOverlap: built to sit over or under another item
   };
   struct SliderNote { float frac; ImRect drawn; };

   int sFrame = -1;
   std::vector<Rec> sCur, sLast;
   std::unordered_map<ImGuiID, SliderNote> sSliders, sSlidersLast;

   void Roll(ImGuiContext* ctx)
   {
      if (ctx->FrameCount == sFrame)
         return;
      sFrame = ctx->FrameCount;
      sLast.swap(sCur);
      sCur.clear();
      sSlidersLast.swap(sSliders);
      sSliders.clear();
   }

   // "3D View/##v3dprops_1A2B3C4D" -> "3D View/##v3dprops": child names carry their parent's id.
   std::string BaseName(const char* name)
   {
      std::string s = name;
      const size_t u = s.rfind('_');
      if (u != std::string::npos && s.size() - u == 9 &&
          s.find_first_not_of("0123456789ABCDEF", u + 1) == std::string::npos)
         s.resize(u);
      return s;
   }
}

void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect& bb, const ImGuiLastItemData* item)
{
   if (item == nullptr || ctx->CurrentWindow == nullptr)
      return;
   Roll(ctx);
   Rec r;
   r.id = id;
   r.bb = bb;
   r.clip = ctx->CurrentWindow->ClipRect;
   r.window = BaseName(ctx->CurrentWindow->Name);
   r.disabled = (item->ItemFlags & ImGuiItemFlags_Disabled) != 0;
   r.overlapOk = (item->ItemFlags & ImGuiItemFlags_AllowOverlap) != 0;
   sCur.push_back(std::move(r));
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext* ctx, ImGuiID id, const char* label, ImGuiItemStatusFlags)
{
   Roll(ctx);
   for (auto it = sCur.rbegin(); it != sCur.rend() && it - sCur.rbegin() < 4; ++it)
      if (it->id == id)
      {
         it->widget = true;
         it->label = label != nullptr ? label : "";
         return;
      }
}

void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}

const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID id)
{
   for (const Rec& r : sLast)
      if (r.id == id && !r.label.empty())
         return r.label.c_str();
   return nullptr;
}

void UiAudit::NoteSlider(ImGuiID id, float frac, ImVec2 drawnMin, ImVec2 drawnMax)
{
   if (!GImGui->TestEngineHookItems)
      return;
   Roll(GImGui);
   sSliders[id] = { frac, ImRect(drawnMin, drawnMax) };
}

namespace app
{
namespace
{
   struct Finding { std::string check, state, window, label, detail; };
   std::vector<Finding> sFindings;
   std::set<std::string> sSeen;   // check|window|label: one row per control, the first state it showed in
   int sChecked = 0, sPoked = 0;

   void Add(const char* check, const std::string& state, const Rec& r, const char* fmt, ...)
   {
      const std::string key = std::string(check) + "|" + r.window + "|" + r.label;
      if (!sSeen.insert(key).second)
         return;
      char buf[256];
      va_list ap;
      va_start(ap, fmt);
      vsnprintf(buf, sizeof buf, fmt, ap);
      va_end(ap);
      sFindings.push_back({ check, state, r.window, r.label.empty() ? "(no label)" : r.label, buf });
   }

   bool Visible(const Rec& r) { return r.bb.Overlaps(r.clip) && r.bb.GetWidth() > 0 && r.bb.GetHeight() > 0; }
   bool FullyShown(const Rec& r) { return r.clip.Contains(r.bb); }

   // Layer 2: every item of one frame, no input.
   void Snapshot(const std::string& state)
   {
      std::unordered_map<ImGuiID, const Rec*> first;
      std::unordered_map<std::string, std::vector<const Rec*>> byWindow;
      for (const Rec& r : sLast)
      {
         if (!r.widget || !Visible(r))
            continue;
         sChecked++;
         if (auto [it, fresh] = first.emplace(r.id, &r); !fresh)
         {
            const Rec& o = *it->second;
            if (std::fabs(o.bb.Min.x - r.bb.Min.x) > 0.5f || std::fabs(o.bb.Min.y - r.bb.Min.y) > 0.5f)
               Add("duplicate id", state, r, "also at %.0f,%.0f", o.bb.Min.x, o.bb.Min.y);
         }
         if (r.bb.Min.x < r.clip.Min.x - 0.5f || r.bb.Max.x > r.clip.Max.x + 0.5f)
            Add("clipped", state, r, "%.0f px of %.0f cut", std::max(0.0f, r.clip.Min.x - r.bb.Min.x) +
                std::max(0.0f, r.bb.Max.x - r.clip.Max.x), r.bb.GetWidth());
         if (!r.disabled && (r.bb.GetWidth() < 12.0f || r.bb.GetHeight() < 12.0f))
            Add("tiny target", state, r, "%.0f x %.0f", r.bb.GetWidth(), r.bb.GetHeight());
         if (auto s = sSlidersLast.find(r.id); s != sSlidersLast.end())
         {
            const ImRect& d = s->second.drawn;
            const float ix = std::max(0.0f, std::min(r.bb.Max.x, d.Max.x) - std::max(r.bb.Min.x, d.Min.x));
            const float iy = std::max(0.0f, std::min(r.bb.Max.y, d.Max.y) - std::max(r.bb.Min.y, d.Min.y));
            const float cover = d.GetArea() > 0 ? ix * iy / d.GetArea() : 1.0f;
            if (cover < 0.95f)
               Add("hit smaller than drawn", state, r, "hit covers %.0f%%", cover * 100.0f);
         }
         if (!r.disabled)
            byWindow[r.window].push_back(&r);
      }
      for (auto& [w, items] : byWindow)
         for (size_t i = 0; i < items.size(); i++)
            for (size_t j = i + 1; j < items.size(); j++)
            {
               const ImRect& a = items[i]->bb;
               const ImRect& b = items[j]->bb;
               const float ox = std::min(a.Max.x, b.Max.x) - std::max(a.Min.x, b.Min.x);
               const float oy = std::min(a.Max.y, b.Max.y) - std::max(a.Min.y, b.Min.y);
               if (ox > 2.0f && oy > 2.0f && !a.Contains(b) && !b.Contains(a) && items[i]->id != items[j]->id &&
                   !items[i]->overlapOk && !items[j]->overlapOk)
                  Add("overlap", state, *items[j], "with %s, %.0f x %.0f", items[i]->label.c_str(), ox, oy);
            }
   }

   // The 3D View's settings column, or the side child around it when that is what scrolls.
   ImGuiWindow* PropsScroller()
   {
      for (ImGuiWindow* w : GImGui->Windows)
      {
         const std::string n = BaseName(w->Name);
         if (w->WasActive && n.size() >= 10 && n.compare(n.size() - 10, 10, "##v3dprops") == 0)
            return w->ScrollMax.y <= 0.0f && w->ParentWindow != nullptr && w->ParentWindow->ScrollMax.y > 0.0f
                   ? w->ParentWindow : w;
      }
      return nullptr;
   }

   // Known-bad controls: each check must fire on its row, or the audit is not looking.
   void DrawCanary()
   {
      ImGui::SetNextWindowPos(ImVec2(60, 120));
      ImGui::SetNextWindowSize(ImVec2(220, 160));
      ImGui::Begin("UI audit canary", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
      ImGui::Button("Same##canary");
      ImGui::SameLine();
      ImGui::Button("Same##canary");
      ImGui::InvisibleButton("tiny", ImVec2(6, 6));
      const ImVec2 p = ImGui::GetCursorPos();
      ImGui::Button("Over A", ImVec2(70, 20));
      ImGui::SetCursorPos(ImVec2(p.x + 30, p.y + 6));
      ImGui::Button("Over B", ImVec2(70, 20));
      ImGui::SetNextItemWidth(60);
      static float v = 0.5f;
      const ImVec2 mn = ImGui::GetCursorScreenPos();
      ImGui::SliderFloat("##canaryslider", &v, 0, 1);
      UiAudit::NoteSlider(ImGui::GetItemID(), v, mn, ImVec2(mn.x + 120, mn.y + ImGui::GetFrameHeight()));
      ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 30);
      ImGui::Button("Cut off", ImVec2(80, 0));
      ImGui::End();
   }

   // While the audit runs its mouse is the only mouse: the backend's events (the real cursor) are dropped each
   // frame before ImGui reads them, and the audit's position is held until it moves it.
   ImVec2 sMouse(-10000.0f, -10000.0f);
   int sButton = -1;   // -1 nothing to send, else 0 up / 1 down
   void OwnMouse(ImGuiContext* ctx, ImGuiContextHook*)
   {
      if (!ctx->TestEngineHookItems)
         return;
      auto& q = ctx->InputEventsQueue;
      for (int i = q.Size - 1; i >= 0; i--)
         if (q[i].Type == ImGuiInputEventType_MousePos || q[i].Type == ImGuiInputEventType_MouseButton ||
             q[i].Type == ImGuiInputEventType_MouseWheel)
            q.erase(q.Data + i);
      ctx->IO.AddMousePosEvent(sMouse.x, sMouse.y);
      if (sButton >= 0)
         ctx->IO.AddMouseButtonEvent(0, sButton == 1);
      sButton = -1;
   }

   // Layer 3: press at 25%, drag to each end, release, undo.
   struct Poke
   {
      ImGuiID id = 0;
      ImRect bb;
      std::string label, window, state;
      float before = 0, at25 = 0, atMax = 0, atMin = 0;
      size_t depth = 0;
      bool undoable = false, took = false;
      std::string over;   // the window under the press
      int step = 0;
   };
   std::vector<Poke> sQueue;
   Poke sPoke;
   bool sPoking = false;

   float SliderFrac(ImGuiID id, float fallback)
   {
      auto it = sSlidersLast.find(id);
      return it != sSlidersLast.end() ? it->second.frac : fallback;
   }

   void Judge(const Poke& k)
   {
      sPoked++;
      Rec r;
      r.window = k.window;
      r.label = k.label;
      if (!k.took)
      {
         Add("press not taken", k.state, r, "under %s", k.over == k.window ? "its own window" : k.over.c_str());
         return;
      }
      // ImGui maps the value over the bar minus the grab: 25% of the bar lands a little under 25% of the range.
      if (std::fabs(k.at25 - 0.25f) > 0.06f)
         Add("press lands off", k.state, r, "press at 25%% of the bar gave %.0f%%", k.at25 * 100.0f);
      if (k.atMax < 0.99f)
         Add("drag misses max", k.state, r, "right end gave %.0f%%", k.atMax * 100.0f);
      if (k.atMin > 0.01f)
         Add("drag misses min", k.state, r, "left end gave %.0f%%", k.atMin * 100.0f);
      if (!k.undoable)
         Add("no undo step", k.state, r, "the drag pushed no undo checkpoint");
      else if (std::fabs(SliderFrac(k.id, -1.0f) - k.before) > 1e-4f)
         Add("undo does not restore", k.state, r, "%.3f before, %.3f after undo", k.before, SliderFrac(k.id, -1.0f));
   }

   // One step a frame: queued input lands on the next frame, its value is read the frame after.
   bool StepPoke()
   {
      if (!sPoking)
      {
         if (sQueue.empty())
            return false;
         sPoke = sQueue.back();
         sQueue.pop_back();
         sPoking = true;
      }
      Poke& k = sPoke;
      const float y = k.bb.GetCenter().y;
      switch (k.step++)
      {
      case 0:
         k.before = SliderFrac(k.id, 0);
         k.depth = gUndoStack.size();
         sMouse = ImVec2(k.bb.Min.x + k.bb.GetWidth() * 0.25f, y);
         break;
      case 1: sButton = 1; break;
      case 3:
         // The press landed last frame; the panel draws after this runs, so its ActiveId is from then.
         k.took = GImGui->ActiveId == k.id;
         k.over = GImGui->HoveredWindow != nullptr ? BaseName(GImGui->HoveredWindow->Name) : "nothing";
         k.at25 = SliderFrac(k.id, -1); sMouse = ImVec2(k.bb.Max.x - 1.0f, y); break;
      case 5: k.atMax = SliderFrac(k.id, -1); sMouse = ImVec2(k.bb.Min.x + 1.0f, y); break;
      case 7: k.atMin = SliderFrac(k.id, -1); sButton = 0; break;
      case 8: sMouse = ImVec2(-10000.0f, -10000.0f); break;
      case 9:
         // Undo only this edit's own step; without one, undoing would take back something else.
         k.undoable = gUndoStack.size() > k.depth;
         if (k.undoable)
            Undo();
         break;
      case 11: Judge(k); sPoking = false; break;
      default: break;
      }
      return true;
   }

   void QueuePokes(const std::string& state)
   {
      const size_t had = sQueue.size();
      for (const Rec& r : sLast)
         if (r.widget && !r.disabled && FullyShown(r) && sSlidersLast.count(r.id) != 0 &&
             r.window.find("v3dprops") != std::string::npos)
         {
            Poke k;
            k.id = r.id;
            k.bb = r.bb;
            k.label = r.label;
            k.window = r.window;
            k.state = state;
            sQueue.insert(sQueue.begin(), k);
         }
      int sliders = 0;
      for (const Rec& r : sLast)
         sliders += sSlidersLast.count(r.id) != 0 && r.window.find("v3dprops") != std::string::npos;
      printf("UIAUDIT state %s: %zu items, %d sliders, %zu poked\n", state.c_str(), sLast.size(), sliders, sQueue.size() - had);
   }

   void Write(const char* dir)
   {
      std::ofstream f(std::string(dir) + "/report.tsv");
      f << "check\tstate\twindow\tlabel\tdetail\n";
      for (const Finding& x : sFindings)
         f << x.check << '\t' << x.state << '\t' << x.window << '\t' << x.label << '\t' << x.detail << '\n';
      f << "#\tchecked " << sChecked << "\tpoked " << sPoked << '\n';
   }
}

// 3D View side tabs in strip order; 8 is the canary.
void FrameTest_UIAUDIT(int frameId, GLFWwindow* window)
{
   static const char* run = getenv("INFINITE_UIAUDIT");
   if (run == nullptr)
      return;
   static const int kTabs[] = { 6, 0, 1, 2, 3, 4, 7, 5, 8 };
   static const char* kNames[] = { "Scene", "Render", "Output", "World", "Object", "Light", "Material", "Viewport", "canary" };
   static int sTabAt = -1, sPage = 0, sWait = 0;
   static bool sDone = false;
   if (sDone)
      return;
   if (frameId == 3)
   {
      GImGui->TestEngineHookItems = true;
      static ImGuiContextHook hook;
      hook.Type = ImGuiContextHookType_NewFramePre;
      hook.Callback = OwnMouse;
      ImGui::AddContextHook(GImGui, &hook);
      const char* patch = getenv("INFINITE_UIAUDIT_PATCH");
      const bool opened = patch != nullptr && LoadPatchFrom(patch);
      printf("UIAUDIT patch %s opened=%d\n", patch != nullptr ? patch : "(none)", opened ? 1 : 0);
      gView3DOpen = true;
      sTabAt = 0;
      sWait = 30;   // the scene, its previews and the panel settle
      return;
   }
   if (sTabAt < 0)
      return;
   const bool canary = kTabs[sTabAt] == 8;
   if (canary)
      DrawCanary();
   if (StepPoke())
      return;
   if (sWait > 0)
   {
      if (--sWait == 0 && !canary)
      {
         // Notices float over the panel's lower rows; the audit is about the panel.
         for (const Notices::Notice& n : Notices::Active())
            Notices::DismissId(n.id);
         if (sPage == 0 && !View3DAuditState(kTabs[sTabAt]))
            printf("UIAUDIT %s: nothing to select\n", kNames[sTabAt]);
         if (ImGuiWindow* c = PropsScroller())
            ImGui::SetScrollY(c, c->InnerRect.GetHeight() * 0.8f * (float)sPage);
         sWait = -3;   // three frames for the tab (and Add Material) to draw
      }
      return;
   }
   if (sWait < 0 && ++sWait < 0)
      return;
   const std::string state = std::string(kNames[sTabAt]) + (sPage > 0 ? " p" + std::to_string(sPage + 1) : "");
   Snapshot(state);
   if (!canary)
      QueuePokes(state);
   // The next page while this tab still scrolls, else the next tab.
   float maxY = 0, y = 0;
   if (ImGuiWindow* c = PropsScroller())
   {
      maxY = c->ScrollMax.y;
      y = c->Scroll.y;
   }
   if (!canary && y < maxY - 1.0f && sPage < 12)
      sPage++;
   else
   {
      sPage = 0;
      if (++sTabAt == (int)(sizeof kTabs / sizeof kTabs[0]))
      {
         const char* out = getenv("INFINITE_UIAUDIT_OUT");
         Write(out != nullptr ? out : ".");
         printf("UIAUDIT done findings=%zu checked=%d poked=%d\n", sFindings.size(), sChecked, sPoked);
         sDone = true;
         GImGui->TestEngineHookItems = false;
         glfwSetWindowShouldClose(window, GLFW_TRUE);
         return;
      }
   }
   sWait = 1;
}
}

#else
namespace app
{
void FrameTest_UIAUDIT(int, GLFWwindow*) {}
}
#endif
