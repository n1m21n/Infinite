// Page menu self-test (INFINITE_PAGEMENUTEST). Every paged panel (Drums, Mixer, Piano Roll, Curve, Field Patcher,
// Performance) shares one page strip (PagePicker). Through real mouse and key events this test, per panel:
//   a) right-clicks a chip and reads the menu: open, exactly Rename | Duplicate | Colour > | Move Left | Move Right |
//      --- | Delete Page, in that order;
//   b) runs Rename, Colour, Move Left, Move Right, Duplicate, Delete (and middle-click Close where the panel closes
//      pages), checks each result, then Undo (and Redo);
//   c) saves the patch, loads it back, and checks page order and colour came back.
// Then it opens each 2D View More.. popover and checks it is one compact column (no wider than kPopoverW).
// INFINITE_PAGEMENUSHOT=<dir>: one full-window shot per panel with the page menu open.
// Plan: internal/plans/ui-system/panel-consistency-final-prompt.md (Step 2).
#include "app/AppShared.h"
#include "app/ui/design/UiDensity.h"
#include "app/ui/design/components/PagePicker.h"
#include "app/selftest/UiAudit.h"

#include <GLFW/glfw3.h>
#include "imgui_impl_glfw.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace app
{
extern bool gCurveEditorOpen;
extern bool gFieldPatcherOpen;
extern std::string gShotNowPath;
extern int gShotNowRect[4];
int View2DAuditStateCount();
std::string View2DAuditState(int i);

#ifdef IMGUI_ENABLE_TEST_ENGINE
namespace
{
   bool sOk = true;
   void Check(const std::string& label, bool pass)
   {
      printf("  [%s] %s\n", pass ? "pass" : "FAIL", label.c_str());
      fflush(stdout);
      if (!pass)
         sOk = false;
   }

   struct PanelCase
   {
      const char* name;
      const char* strip;
      bool* open;
      bool undo;     // page edits are undoable (Field Patcher pages are session-only)
      bool close;    // middle-click closes a page
      bool saves;    // pages are in the patch
      const char* addItem; // + opens a menu: the row that adds a page
   };

   // One step runs each frame until it returns true; then the script waits `wait` frames. A step that never
   // finishes fails after kTimeout frames and the script moves on.
   struct Step
   {
      std::string what;
      std::function<bool()> fn;
      int wait = 1;
   };
   constexpr int kTimeout = 120;
   std::vector<Step> sSteps;
   size_t sAt = 0;
   int sNextFrame = 0, sStepFrom = -1;
   ImVec2 sMouse(-1.0f, -1.0f);
   int sPanel = -1;
   std::vector<PanelCase> sCases;
   std::string sTmp;

   void Do(const std::string& what, std::function<void()> fn, int wait = 1)
   {
      sSteps.push_back({ what, [fn] { fn(); return true; }, wait });
   }
   void Until(const std::string& what, std::function<bool()> pred, int wait = 1) { sSteps.push_back({ what, std::move(pred), wait }); }

   const PanelCase& Cur() { return sCases[sPanel]; }

   const PagePicker::Probe* P()
   {
      auto& m = PagePicker::Probes();
      auto it = m.find(Cur().strip);
      if (it == m.end() || it->second.frame < ImGui::GetFrameCount() - 2)
         return nullptr;
      return &it->second;
   }
   std::vector<std::string> Names()
   {
      std::vector<std::string> v;
      if (const PagePicker::Probe* p = P())
         for (const auto& e : p->entries)
            v.push_back(e.name);
      return v;
   }
   int Count()
   {
      const PagePicker::Probe* p = P();
      return p != nullptr ? (int)p->entries.size() : -1;
   }
   std::string Join(const std::vector<std::string>& v)
   {
      std::string s;
      for (size_t i = 0; i < v.size(); i++)
         s += (i ? ", " : "") + v[i];
      return "[" + s + "]";
   }
   ImVec2 Mid(ImVec2 a, ImVec2 b) { return ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f); }

   // Moves the pointer to `where` (once it resolves), then presses and releases `button` there.
   void Click(const std::string& what, std::function<bool(ImVec2&)> where, int button = 0)
   {
      Until(what + ": find", [where] { ImVec2 p; if (!where(p)) return false; sMouse = p; return true; }, 2);
      Do(what + ": press", [button] { ImGui::GetIO().AddMouseButtonEvent(button, true); }, 1);
      Do(what + ": release", [button] { ImGui::GetIO().AddMouseButtonEvent(button, false); }, 3);
   }
   std::function<bool(ImVec2&)> Chip(int i)
   {
      return [i](ImVec2& p) {
         const PagePicker::Probe* pr = P();
         if (pr == nullptr || i >= (int)pr->chipMn.size() || pr->chipMx[i].x <= pr->chipMn[i].x)
            return false;
         p = Mid(pr->chipMn[i], pr->chipMx[i]);
         return true;
      };
   }
   std::function<bool(ImVec2&)> Row(const char* label)
   {
      const std::string want = label;
      return [want](ImVec2& p) {
         const PagePicker::Probe* pr = P();
         if (pr == nullptr || !pr->menuOpen)
            return false;
         for (const auto& r : pr->rows)
            if (r.label == want && r.mx.x > r.mn.x)
            {
               p = Mid(r.mn, r.mx);
               return true;
            }
         return false;
      };
   }
   void Key(ImGuiKey k)
   {
      Do("key down", [k] { ImGui::GetIO().AddKeyEvent(k, true); }, 1);
      Do("key up", [k] { ImGui::GetIO().AddKeyEvent(k, false); }, 2);
   }
   void OpenMenu(int chip)
   {
      Click(std::string(Cur().name) + ": right-click chip " + std::to_string(chip), Chip(chip), 1);
      Until(std::string(Cur().name) + ": menu opens on chip " + std::to_string(chip), [chip] {
         const PagePicker::Probe* p = P();
         return p != nullptr && p->menuOpen && p->menuFor == chip && !p->rows.empty();
      });
   }
   void Expect(const std::string& what, std::function<bool()> pred)
   {
      // Waits for the panel to catch up; the check fails (with what it saw) if it never does.
      sSteps.push_back({ what, [=] {
                           if (!pred())
                              return false;
                           Check(std::string(Cur().name) + ": " + what, true);
                           return true;
                        }, 1 });
      sSteps.back().what = std::string("EXPECT|") + what;
   }

   void Rename(int chip, const std::string& to)
   {
      OpenMenu(chip);
      Click("Rename row", Row("Rename"));
      Until("rename field takes keys", [] { return ImGui::GetIO().WantTextInput; }, 2);
      Do("type", [to] { ImGui::GetIO().AddInputCharactersUTF8(to.c_str()); }, 2);
      Key(ImGuiKey_Enter);
      Expect("Rename chip " + std::to_string(chip) + " to " + to, [chip, to] { auto n = Names(); return chip < (int)n.size() && n[chip] == to; });
   }
   void UndoRedo(const std::string& what, std::function<bool()> undone, std::function<bool()> redone)
   {
      if (!Cur().undo)
         return;
      Do("undo", [] { Undo(); }, 3);
      Expect("Undo " + what, undone);
      Do("redo", [] { Redo(); }, 3);
      Expect("Redo " + what, redone);
   }
   bool NamesAre(std::vector<std::string> want) { return Names() == want; }

   void ScriptPanel(int k)
   {
      sPanel = k; // the names below are built now; the step sets it again when the script reaches this panel
      Do("pick panel", [k] { sPanel = k; }, 0);
      Do("set up", [] {
         NewPatch();
         gDrumPanelOpen = gMixerPanelOpen = gPianoRollOpen = gCurveEditorOpen = gFieldPatcherOpen = gPerfPanelOpen = false;
         gView2DOpen = gView3DOpen = gArrangePanelOpen = gSampleStudioOpen = false;
         *Cur().open = true;
         SpawnNode("Delay", "AudioEffects", -400.0f, 0.0f); // a patch with no nodes is refused on load
         if (Cur().open == &gDrumPanelOpen)
         {
            SpawnNode("Drum Sequencer", "Synths", 0.0f, 0.0f);
            SpawnNode("Drum Sequencer", "Synths", 0.0f, 300.0f);
         }
         if (Cur().open == &gMixerPanelOpen)
         {
            MixerPanelAddPage();
            MixerPanelAddPage();
         }
      }, 6);
      Until("strip drawn", [] { return P() != nullptr; }, 1);
      // Two pages through +, however many the panel starts with.
      for (int t = 0; t < 2; t++)
      {
         Until("seed check", [] { return P() != nullptr; }, 0);
         Click("+ (seed)", [](ImVec2& p) {
            const PagePicker::Probe* pr = P();
            if (pr == nullptr)
               return false;
            p = pr->entries.size() >= 2 ? ImVec2(-1.0f, -1.0f) : Mid(pr->addMn, pr->addMx);
            return true;
         });
         if (Cur().addItem != nullptr)
         {
            const std::string item = Cur().addItem;
            Click("add menu row", [item](ImVec2& p) {
               ImVec2 mn, mx;
               if (Count() >= 2 || !UiAudit::FindItem(item.c_str(), nullptr, 0, &mn, &mx))
               {
                  p = ImVec2(-1.0f, -1.0f); // nothing to add: click off everything
                  return true;
               }
               p = Mid(mn, mx);
               return true;
            });
         }
      }
      Expect("two pages to work on", [] { return Count() >= 2; });

      // a) The menu: open, seven rows in order.
      OpenMenu(0);
      Do("shot", [] {
         if (const char* dir = getenv("INFINITE_PAGEMENUSHOT"))
         {
            gShotNowPath = std::string(dir) + "/pagemenu_" + (Cur().strip + 2) + ".png";
            gShotNowRect[0] = gShotNowRect[1] = 0;
            gShotNowRect[2] = gShotNowRect[3] = 1 << 20;
         }
      }, 2);
      Do("read menu", [] {
         const PagePicker::Probe* p = P();
         std::vector<std::string> rows;
         if (p != nullptr)
            for (const auto& r : p->rows)
               rows.push_back(r.label);
         const std::vector<std::string> want = { "Rename", "Duplicate", "Colour >", "Move Left", "Move Right", "---", "Delete Page" };
         Check(std::string(Cur().name) + ": menu rows " + Join(rows), rows == want);
         const bool leftGrey = p != nullptr && p->rows.size() == 7 && !p->rows[3].enabled && p->rows[4].enabled;
         Check(std::string(Cur().name) + ": Move Left greyed (shown) on the first chip", leftGrey);
#ifdef __APPLE__
         const char* key = "Cmd+R";
#else
         const char* key = "Ctrl+R";
#endif
         Check(std::string(Cur().name) + ": Rename shows " + key + " on the current page",
               p != nullptr && !p->rows.empty() && (p->menuFor != p->current || p->rows[0].key == key));
      }, 1);
      Do("close menu", [] { ImGui::ClosePopupsExceptModals(); }, 2);
      Until("menu closed", [] { const PagePicker::Probe* p = P(); return p != nullptr && !p->menuOpen; }, 1);

      // b) Each row, then Undo / Redo.
      Rename(0, "PgA");
      Rename(1, "PgB");
      UndoRedo("rename", [] { auto n = Names(); return n.size() >= 2 && n[1] != "PgB"; }, [] { return NamesAre({ "PgA", "PgB" }); });

      OpenMenu(0);
      Until("hover Colour", [] { ImVec2 p; if (!Row("Colour >")(p)) return false; sMouse = p; return true; }, 4);
      Until("colour row opens", [] { const PagePicker::Probe* p = P(); return p != nullptr && p->colourOpen && p->swatchMn.size() == 8; }, 1);
      Do("to the swatch", [] {
         const PagePicker::Probe* p = P();
         // Across, then down: a straight line from the row to the third swatch can cross the next row and close the submenu.
         if (p != nullptr && p->swatchMn.size() == 8)
            sMouse = ImVec2(p->swatchMn[0].x + 2.0f, sMouse.y);
      }, 2);
      Click("third swatch", [](ImVec2& p) {
         const PagePicker::Probe* pr = P();
         if (pr == nullptr || pr->swatchMn.size() != 8)
            return false;
         p = Mid(pr->swatchMn[2], pr->swatchMx[2]);
         return true;
      });
      const unsigned int kYellow = 0xE8C547;
      Expect("Colour chip 0", [=] { const PagePicker::Probe* p = P(); return p != nullptr && !p->entries.empty() && p->entries[0].color == kYellow; });
      UndoRedo("colour", [] { const PagePicker::Probe* p = P(); return p != nullptr && !p->entries.empty() && p->entries[0].color == 0; },
               [=] { const PagePicker::Probe* p = P(); return p != nullptr && !p->entries.empty() && p->entries[0].color == kYellow; });

      OpenMenu(0);
      Click("Move Right row", Row("Move Right"));
      Expect("Move Right chip 0", [] { return NamesAre({ "PgB", "PgA" }); });
      UndoRedo("move right", [] { return NamesAre({ "PgA", "PgB" }); }, [] { return NamesAre({ "PgB", "PgA" }); });
      OpenMenu(1);
      Click("Move Left row", Row("Move Left"));
      Expect("Move Left chip 1", [] { return NamesAre({ "PgA", "PgB" }); });
      UndoRedo("move left", [] { return NamesAre({ "PgB", "PgA" }); }, [] { return NamesAre({ "PgA", "PgB" }); });
      // Leave the pages out of their first order, coloured, for the save below.
      OpenMenu(0);
      Click("Move Right row (for save)", Row("Move Right"));
      Expect("pages reordered for save", [] { return NamesAre({ "PgB", "PgA" }); });

      OpenMenu(0);
      Click("Duplicate row", Row("Duplicate"));
      Expect("Duplicate chip 0", [] { return Count() == 3; });
      if (Cur().undo)
      {
         Do("undo", [] { Undo(); }, 3);
         Expect("Undo duplicate", [] { return Count() == 2 && NamesAre({ "PgB", "PgA" }); });
      }
      // Delete the last chip; with Undo it comes back.
      static int sBefore = 0;
      Do("count before delete", [] { sBefore = Count(); }, 0);
      Click("open menu on last chip", [](ImVec2& p) { return Chip(std::max(0, Count() - 1))(p); }, 1);
      Until("menu opens on last chip", [] { const PagePicker::Probe* p = P(); return p != nullptr && p->menuOpen && p->menuFor == Count() - 1; });
      Click("Delete Page row", Row("Delete Page"));
      Expect("Delete Page", [] { return Count() == sBefore - 1; });
      if (Cur().undo)
      {
         Do("undo", [] { Undo(); }, 3);
         Expect("Undo delete", [] { return Count() == sBefore; });
      }

      // c) Save and load: order and colour come back.
      if (Cur().saves)
      {
         static std::vector<PagePicker::Entry> sSaved;
         Do("save", [] {
            const PagePicker::Probe* p = P();
            sSaved = p != nullptr ? p->entries : std::vector<PagePicker::Entry>();
            Check(std::string(Cur().name) + ": patch saves", SavePatchTo(sTmp));
         }, 2);
         Do("new patch", [] { NewPatch(); }, 3);
         Do("new patch clears", [] {
            const PagePicker::Probe* p = P();
            bool same = p != nullptr && p->entries.size() == sSaved.size();
            for (size_t i = 0; same && i < sSaved.size(); i++)
               same = p->entries[i].name == sSaved[i].name && p->entries[i].color == sSaved[i].color;
            Check(std::string(Cur().name) + ": New Patch clears the pages (now " + Join(Names()) + ")", !same);
         }, 1);
         Do("load", [] { Check(std::string(Cur().name) + ": patch loads", LoadPatchFrom(sTmp)); *Cur().open = true; }, 6);
         Expect("page order and colour survive save and load", [] {
            const PagePicker::Probe* p = P();
            if (p == nullptr || p->entries.size() != sSaved.size())
               return false;
            for (size_t i = 0; i < sSaved.size(); i++)
               if (p->entries[i].name != sSaved[i].name || p->entries[i].color != sSaved[i].color)
                  return false;
            return true;
         });
         Do("print pages", [] {
            auto fmt = [](const std::vector<PagePicker::Entry>& v) {
               std::string s;
               char b[32];
               for (const auto& e : v)
               {
                  std::snprintf(b, sizeof b, "#%06X", e.color);
                  s += (s.empty() ? "" : ", ") + e.name + b;
               }
               return "[" + s + "]";
            };
            const PagePicker::Probe* p = P();
            printf("    saved %s, loaded %s\n", fmt(sSaved).c_str(), p != nullptr ? fmt(p->entries).c_str() : "(no strip)");
         }, 1);
      }

      // Middle-click closes a page (a view action: the page's node stays).
      if (Cur().close)
      {
         Do("count before close", [] { sBefore = Count(); }, 0);
         Click("middle-click chip 0", Chip(0), 2);
         Expect("Middle-click Close", [] { return Count() == sBefore - 1; });
      }
      Do("panel done", [] { printf("  -- %s done\n", Cur().name); }, 1);
   }

   // The 2D View's More.. popovers: one compact column, kPopoverW wide at most.
   std::vector<std::string> sMoreDone;
   void ScriptMore()
   {
      Do("2D set up", [] {
         NewPatch();
         gDrumPanelOpen = gMixerPanelOpen = gPianoRollOpen = gCurveEditorOpen = gFieldPatcherOpen = gPerfPanelOpen = false;
         gView2DOpen = true;
      }, 4);
      static int sState = 0;
      static std::string sOpenId;
      Until("2D: walk the tool states", [] {
         // Find the next tool state whose bar shows a More.. chip not yet checked.
         static int sSetAt = -1;
         if (sSetAt >= 0 && ImGui::GetFrameCount() < sSetAt + 4)
            return false;
         if (sSetAt >= 0)
         {
            for (auto& [id, pr] : View2DMoreProbes())
               if (pr.frame >= ImGui::GetFrameCount() - 2 && std::find(sMoreDone.begin(), sMoreDone.end(), id) == sMoreDone.end())
               {
                  sOpenId = id;
                  sMouse = Mid(pr.chipMn, pr.chipMx);
                  sSetAt = -1;
                  return true;
               }
         }
         for (; sState < View2DAuditStateCount(); sState++)
         {
            const std::string name = View2DAuditState(sState);
            if (name.rfind("tool ", 0) == 0)
            {
               sState++;
               sSetAt = ImGui::GetFrameCount();
               return false;
            }
         }
         sOpenId.clear();
         return true;
      }, 2);
      Do("2D: press", [] { if (!sOpenId.empty()) ImGui::GetIO().AddMouseButtonEvent(0, true); }, 1);
      Do("2D: release", [] { if (!sOpenId.empty()) ImGui::GetIO().AddMouseButtonEvent(0, false); }, 6);
      Do("2D: read", [] {
         if (sOpenId.empty())
            return;
         const View2DMoreProbe& pr = View2DMoreProbes()[sOpenId];
         const float pad = 2.0f * ImGui::GetStyle().WindowPadding.x;
         char buf[256];
         std::snprintf(buf, sizeof buf, "2D More.. %s opens as one column (window %.0f, content %.0f, limit %.0f)", sOpenId.c_str(), pr.winW,
                       pr.contentW, UiDensity::kPopoverW);
         Check(buf, pr.open && pr.winW <= UiDensity::kPopoverW + 1.0f && pr.contentW <= UiDensity::kPopoverW - pad + 1.0f);
         sMoreDone.push_back(sOpenId);
      }, 1);
      Do("2D: close", [] { ImGui::ClosePopupsExceptModals(); }, 2);
   }
}

void RunPageMenuTest(int frameId, GLFWwindow* window)
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
      const char* tmp = getenv("TMPDIR");
      sTmp = std::string(tmp != nullptr ? tmp : "/tmp") + "/infinite_pagemenu.inf";
      sCases = {
         { "Drums", "##drumPage", &gDrumPanelOpen, true, false, true, nullptr },
         { "Mixer", "##mixerPage", &gMixerPanelOpen, true, false, true, nullptr },
         { "Piano Roll", "##pianoPage", &gPianoRollOpen, true, false, true, nullptr },
         { "Curve", "##cePages", &gCurveEditorOpen, true, true, true, nullptr },
         { "Field Patcher", "##fpPages", &gFieldPatcherOpen, false, true, false, "Empty page" },
         { "Performance", "##perfPages", &gPerfPanelOpen, true, false, true, nullptr },
      };
      if (const char* only = getenv("INFINITE_PAGEMENUONLY"))
      {
         std::vector<PanelCase> keep;
         for (const auto& c : sCases)
            if (std::string(only).find(c.name) != std::string::npos)
               keep.push_back(c);
         sCases = keep;
      }
      for (int k = 0; k < (int)sCases.size(); k++)
         ScriptPanel(k);
      if (getenv("INFINITE_PAGEMENUONLY") == nullptr)
         for (int i = 0; i < 3; i++)
            ScriptMore();
      sNextFrame = frameId;
   }

   // The pointer is ours every frame (the real cursor would win otherwise).
   GImGui->TestEngineHookItems = true;
   io.AddMousePosEvent(sMouse.x, sMouse.y);
   if (sMouse.x >= 0.0f)
      glfwSetCursorPos(window, (double)(sMouse.x * ImGui_ImplGlfw_GetPointScale()), (double)(sMouse.y * ImGui_ImplGlfw_GetPointScale()));

   if (sAt >= sSteps.size())
   {
      if (sAt == sSteps.size())
      {
         printf("PAGEMENUTEST %s\n", sOk ? "OK" : "FAIL");
         fflush(stdout);
         sAt++;
         glfwSetWindowShouldClose(window, GLFW_TRUE);
      }
      return;
   }
   if (frameId < sNextFrame)
      return;
   Step& st = sSteps[sAt];
   if (sStepFrom < 0)
      sStepFrom = frameId;
   if (st.fn())
   {
      sAt++;
      sNextFrame = frameId + st.wait;
      sStepFrom = -1;
      return;
   }
   if (frameId - sStepFrom > kTimeout)
   {
      const bool expect = st.what.rfind("EXPECT|", 0) == 0;
      const std::string what = expect ? st.what.substr(7) : st.what;
      Check(std::string(sPanel >= 0 && sPanel < (int)sCases.size() ? sCases[sPanel].name : "?") + ": " + what + " (timed out; pages " + Join(Names()) + ")",
            false);
      sAt++;
      sNextFrame = frameId + 1;
      sStepFrom = -1;
   }
}
#else
void RunPageMenuTest(int, GLFWwindow*)
{
   printf("PAGEMENUTEST FAIL (needs a dev build: the test engine hooks)\n");
}
#endif
}
