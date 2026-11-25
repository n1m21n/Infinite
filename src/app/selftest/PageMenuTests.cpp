// Page menu self-test (INFINITE_PAGEMENUTEST). Every paged panel (Drums, Mixer, Piano Roll, Curve, Field Patcher,
// Performance) shares one page strip (PagePicker). Through real mouse and key events this test, per panel:
//   a) right-clicks a chip and reads the menu: open, exactly Rename | Duplicate | Colour > | Move Left | Move Right |
//      --- | Delete Page, in that order;
//   b) runs Rename, Colour, Move Left, Move Right, Duplicate, Delete (and middle-click Close where the panel closes
//      pages), checks each result, then Undo (and Redo);
//   c) saves the patch, loads it back, and checks page order and colour came back.
// It also checks Cmd+R starts a rename on each strip and that the Piano Roll inspector colour undoes.
// Then it opens each 2D View More.. popover and checks it is one compact column (no wider than kPopoverW), and
// right-clicks free space and a control in each docked panel: one dock menu shape, never over a control.
// INFINITE_PAGEMENUSHOT=<dir>: one full-window shot per panel with the page menu open.
// Plan: internal/plans/ui-system/panel-consistency-final-prompt.md (Step 2).
#include "app/AppShared.h"
#include "app/ui/design/UiDensity.h"
#include "app/ui/design/components/PagePicker.h"
#include "app/ui/design/components/SettingsWindow.h"
#include "app/selftest/UiAudit.h"
#include "imgui_internal.h"

#include <GLFW/glfw3.h>
#include "imgui_impl_glfw.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
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
      int timeout = 120; // kTimeout; a scan over a panel takes longer
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
   void Until(const std::string& what, std::function<bool()> pred, int wait = 1, int timeout = kTimeout)
   {
      sSteps.push_back({ what, std::move(pred), wait, timeout });
   }

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

      // Cmd+R over the strip starts a rename on the current page (Enter keeps the name).
      Until("pointer on the current chip", [] {
         const PagePicker::Probe* p = P();
         ImVec2 at;
         if (p == nullptr || !Chip(std::max(0, p->current))(at))
            return false;
         sMouse = at;
         return true;
      }, 3);
      Do("mod down", [] { ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, true); }, 1);
      Key(ImGuiKey_R);
      Do("mod up", [] { ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, false); }, 1);
      Expect("Cmd+R starts a rename", [] { const PagePicker::Probe* p = P(); return p != nullptr && p->renaming >= 0 && p->renaming == p->current; });
      Key(ImGuiKey_Enter);
      Until("rename ends", [] { const PagePicker::Probe* p = P(); return p != nullptr && p->renaming < 0; }, 2);

      // Piano Roll: the page colour set from the inspector undoes in one step.
      if (Cur().open == &gPianoRollOpen)
      {
         static unsigned int sColour = 0;
         static bool sChanged = false;
         Click("settings chip opens the inspector", [](ImVec2& p) { ImVec2 a, b; if (!UiAudit::FindItem("##pianoSettings", nullptr, 0, &a, &b)) return false; p = Mid(a, b); return true; });
         Do("colour before", [] { const PagePicker::Probe* p = P(); sColour = p != nullptr && p->current >= 0 ? p->entries[p->current].color : 0; }, 0);
         Click("inspector colour dot", [](ImVec2& p) { ImVec2 a, b; if (!UiAudit::FindItem("##pageColour", nullptr, 0, &a, &b)) return false; p = Mid(a, b); return true; });
         Click("picker square", [](ImVec2& p) { ImVec2 a, b; if (!UiAudit::FindItem("sv", nullptr, 0, &a, &b)) return false; p = ImVec2(a.x + (b.x - a.x) * 0.7f, a.y + (b.y - a.y) * 0.3f); return true; });
         Do("close picker", [] { ImGui::ClosePopupsExceptModals(); sMouse = ImVec2(-1.0f, -1.0f); }, 4);
         Expect("inspector colour changes the page", [] {
            const PagePicker::Probe* p = P();
            sChanged = p != nullptr && p->current >= 0 && p->entries[p->current].color != sColour;
            return sChanged;
         });
         Until("gesture ends", [] { return !gArrangeGestureOpen; }, 2);
         Do("undo", [] { if (sChanged) Undo(); }, 3); // only a real change: Undo would take back a page otherwise
         Expect("Undo inspector colour", [] { const PagePicker::Probe* p = P(); return sChanged && p != nullptr && p->current >= 0 && p->entries[p->current].color == sColour; });
         Click("settings chip closes the inspector", [](ImVec2& p) { ImVec2 a, b; if (!UiAudit::FindItem("##pianoSettings", nullptr, 0, &a, &b)) return false; p = Mid(a, b); return true; });
      }

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

   // Dock menus (fix groups A and F): a right-click on free space in each docked panel opens one menu with a
   // "Dock Position" submenu and a "Close <Panel>" row; a right-click on a control in the panel never opens it.
   struct DockCase
   {
      const char* name;
      const char* card; // the docked card's child window id
      bool* open;
      const char* close;          // the Close row; "A|B" when the label follows the panel's face
      const char* item = nullptr; // the control to right-click (its label); null: any control found by scanning
   };
   bool FindAny(const std::string& labels)
   {
      ImVec2 a, b;
      size_t from = 0;
      while (from <= labels.size())
      {
         const size_t bar = labels.find('|', from);
         const std::string one = labels.substr(from, bar == std::string::npos ? std::string::npos : bar - from);
         if (UiAudit::FindItem(one.c_str(), nullptr, 0, &a, &b))
            return true;
         if (bar == std::string::npos)
            break;
         from = bar + 1;
      }
      return false;
   }
   ImGuiWindow* CardWindow(const char* card)
   {
      ImGuiWindow* best = nullptr;
      for (ImGuiWindow* w : GImGui->Windows)
         if (w->WasActive && std::strstr(w->Name, card) != nullptr && (best == nullptr || std::strlen(w->Name) < std::strlen(best->Name)))
            best = w;
      return best;
   }
   bool OverCard(const char* card)
   {
      const ImGuiWindow* h = GImGui->HoveredWindow;
      return h != nullptr && std::strstr(h->Name, card) != nullptr;
   }
   bool DockMenuShown()
   {
      ImVec2 a, b;
      return UiAudit::FindItem("Dock Position", nullptr, 0, &a, &b);
   }
   // Walks points over the card (its header band first) until one is free space (`wantItem` false) or a control
   // (`wantItem` true), right-clicks it, and hands the result to `judge`; `judge` returns true when it is done.
   struct Scan
   {
      std::vector<ImVec2> pts;
      size_t i = 0;
      int sub = 0, wakeAt = 0, tries = 0;
   };
   void ScanClick(const DockCase& dc, bool wantItem, std::function<bool(Scan&)> judge, std::string what)
   {
      auto st = std::make_shared<Scan>();
      Until(what, [=] {
         const int f = ImGui::GetFrameCount();
         if (st->pts.empty())
         {
            const ImGuiWindow* w = CardWindow(dc.card);
            if (w == nullptr)
               return false;
            const ImVec2 o = w->Pos, sz = w->Size;
            ImVec2 a, b;
            if (wantItem && dc.item != nullptr)
            {
               if (!UiAudit::FindItem(dc.item, dc.card, 0, &a, &b))
                  return false;
               st->pts.push_back(Mid(a, b));
            }
            else
            {
               for (float y = 4.0f; y < std::min(40.0f, sz.y - 4.0f); y += 6.0f)
                  for (float x = 8.0f; x < sz.x - 8.0f; x += 18.0f)
                     st->pts.push_back(ImVec2(o.x + x, o.y + y));
               for (float y = 48.0f; y < sz.y - 8.0f; y += 36.0f)
                  for (float x = 12.0f; x < sz.x - 8.0f; x += 36.0f)
                     st->pts.push_back(ImVec2(o.x + x, o.y + y));
            }
         }
         if (f < st->wakeAt)
            return false;
         switch (st->sub)
         {
            case 0:
               if (st->i >= st->pts.size() || st->tries > 12)
               {
                  if (wantItem && st->tries == 0)
                     printf("  [skip] %s: %s (no control in the empty panel)\n", dc.name, what.c_str());
                  else
                     Check(std::string(dc.name) + ": " + what + " (no spot found)", false);
                  return true;
               }
               sMouse = st->pts[st->i];
               st->sub = 1;
               st->wakeAt = f + 2;
               return false;
            case 1:
            {
               const bool item = GImGui->HoveredId != 0 || GImGui->HoveredIdPreviousFrame != 0;
               if (OverCard(dc.card) && item == wantItem && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
               {
                  ImGui::GetIO().AddMouseButtonEvent(1, true);
                  st->sub = 2;
                  st->wakeAt = f + 1;
               }
               else
               {
                  if (dc.item != nullptr && wantItem)
                     printf("    %s: %s at (%.0f, %.0f) not taken: over %s, hovered id %08X\n", dc.name, dc.item, sMouse.x, sMouse.y,
                            GImGui->HoveredWindow != nullptr ? GImGui->HoveredWindow->Name : "(none)", GImGui->HoveredId);
                  st->i++;
                  st->sub = 0;
               }
               return false;
            }
            case 2:
               ImGui::GetIO().AddMouseButtonEvent(1, false);
               st->sub = 3;
               st->wakeAt = f + 4;
               return false;
            default:
            {
               const bool done = judge(*st);
               ImGui::ClosePopupsExceptModals();
               st->tries++;
               st->i += 3;
               st->sub = 0;
               st->wakeAt = f + 3;
               return done;
            }
         }
      }, 2, 4000);
   }
   void ScriptDock(const DockCase& dc)
   {
      bool* open = dc.open;
      Do(std::string(dc.name) + ": dock set up", [open] {
         NewPatch();
         gDrumPanelOpen = gMixerPanelOpen = gPianoRollOpen = gCurveEditorOpen = gFieldPatcherOpen = gPerfPanelOpen = false;
         gView2DOpen = gView3DOpen = gArrangePanelOpen = gSampleStudioOpen = gModMatrixOpen = gViewportPanelOpen = gTerminalOpen = false;
         *open = true;
         if (open == &gTerminalOpen)
            gTerminalFace = 1; // the tabs face, whatever the profile's face is
      }, 8);
      const std::string name = dc.name, close = dc.close;
      ScanClick(dc, false, [name, close](Scan&) {
         if (!DockMenuShown())
            return false; // this free spot is outside the panel's menu band: try another
         Check(name + ": free-space right-click opens Dock Position > | " + close, FindAny(close));
         return true;
      }, "dock menu on free space");
      const std::string on = dc.item != nullptr ? std::string("a ") + dc.item + " control" : "a control";
      ScanClick(dc, true, [name, on](Scan&) {
         Check(name + ": right-click on " + on + " opens no dock menu", !DockMenuShown());
         return true;
      }, "no dock menu over a control");
      Do(std::string(dc.name) + ": dock done", [open] { *open = false; sMouse = ImVec2(-1.0f, -1.0f); }, 2);
   }

   // The 2D View's More.. settings windows: one compact column, kPopoverW wide at most.
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
         const float lim = SettingsWindow::Width(2); // two strips side by side: a grid, not a tower
         std::snprintf(buf, sizeof buf, "2D More.. %s opens as a two-column grid (window %.0f, content %.0f, limit %.0f)", sOpenId.c_str(), pr.winW,
                       pr.contentW, lim);
         Check(buf, pr.open && pr.winW <= lim + 1.0f && pr.contentW <= lim - pad + 1.0f);
         sMoreDone.push_back(sOpenId);
      }, 1);
      Do("2D: close", [] { SettingsWindow::CloseAll(); }, 2);
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
      const char* only = getenv("INFINITE_PAGEMENUONLY");
      if (only == nullptr || std::string(only).find("Dock") != std::string::npos)
      {
         static const DockCase kDocks[] = {
            { "Mixer", "##mixerpanelcontent", &gMixerPanelOpen, "Close Mixer" },
            { "Mod Matrix", "##modmatrixpanelcontent", &gModMatrixOpen, "Close Mod Matrix" },
            { "Viewport", "##viewportpanelcontent", &gViewportPanelOpen, "Close Viewport" },
            { "Sample Studio", "##studiopanelcontent", &gSampleStudioOpen, "Close Sample Studio" },
            { "Terminal", "##terminalcontent", &gTerminalOpen, "Close Terminal|Close Chat", "##tab" },
            { "2D View", "##view2dcontent", &gView2DOpen, "Close 2D View" },
            { "3D View", "##view3dcontent", &gView3DOpen, "Close 3D View" },
            { "Timeline", "##arrangepanelinnercontent", &gArrangePanelOpen, "Close Timeline" },
         };
         for (const DockCase& dc : kDocks)
            ScriptDock(dc);
      }
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
   if (frameId - sStepFrom > st.timeout)
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
   printf("PAGEMENUTEST SKIP (dev-only: needs a Debug build; the ImGui test engine is compiled out of release)\n");
}
#endif
}
