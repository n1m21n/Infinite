// UI audit, layers 2 and 3 (docs/plans/ui-system/ui-audit.md). Debug builds only: INFINITE_UIAUDIT=1 opens a
// 3D scene, steps the 3D View through each side tab (paging each one down), then the other panels, then one node
// of every registered type on an empty canvas; in the dark and the light theme. Each state's items (what ImGui
// registered on the last frame) are checked, then poked: sliders dragged end to end and undone, dropdowns and
// colour dots opened and closed with Esc, checkboxes clicked twice, every control hovered and wheel-scrolled.
// tools/ui-audit/run.py launches it, writes the report and holds it against the baseline.
#include "app/AppShared.h"
#include "app/selftest/UiAudit.h"
#include "app/panels/CurveEditorPanel.h"
#include "core/Notices.h"

#ifdef IMGUI_ENABLE_TEST_ENGINE
#include "imgui_internal.h"
#include <cmath>
#include <cstdarg>
#include <fstream>
#include <set>
#include <unordered_map>
#include "core/CategoryColors.h"
#include "core/NodeFactory.h"
#include "app/ui/design/Tokens.gen.h"

namespace app
{
   bool View3DAuditState(int tab);
   extern std::string gShotNowPath;
   extern int gShotNowRect[4];
   int View2DAuditStateCount();
   std::string View2DAuditState(int i);
   std::string View2DAuditPanelState();
   std::string View2DAuditRoundTrip();
   uint64_t View2DAuditRenderHash();
   std::string View2DAuditRanges();
   void View2DAuditRestore();
}

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
      bool checkable = false;  // ImGui's own checkbox/radio (the label sits inside the item's rect)
      bool inputable = false;  // takes typed text (drags and sliders after a double-click, input fields)
      float labelW = 0;        // visible label text
      ImVec4 textCol, bgCol;   // ImGuiCol_Text and WindowBg when the item was added
   };
   struct SliderNote { float frac; ImRect drawn; float textW; };
   struct ItemNote { const char* kind = nullptr; float textW = 0, availW = 0; bool hasFill = false; ImVec4 fill; };
   struct NodeBox { ImRect box; ImVec4 bg; };
   struct RowBox { float x0, y0, rowH, cellW; int count; };
   struct Ink { int kind; ImRect r; float size; std::string tip, glyph, window; ImGuiID item; ImRect itemBb; };

   int sFrame = -1;
   std::vector<Rec> sCur, sLast;
   std::unordered_map<ImGuiID, SliderNote> sSliders, sSlidersLast;
   std::unordered_map<ImGuiID, ItemNote> sNotes, sNotesLast;
   std::vector<NodeBox> sNodes, sNodesLast;
   std::vector<RowBox> sRows, sRowsLast;
   std::vector<Ink> sInks, sInksLast;
   std::set<ImGuiID> sOn, sOnLast;

   void Roll(ImGuiContext* ctx)
   {
      if (ctx->FrameCount == sFrame)
         return;
      sFrame = ctx->FrameCount;
      sLast.swap(sCur);
      sCur.clear();
      sSlidersLast.swap(sSliders);
      sSliders.clear();
      sNotesLast.swap(sNotes);
      sNotes.clear();
      sNodesLast.swap(sNodes);
      sNodes.clear();
      sRowsLast.swap(sRows);
      sRows.clear();
      sInksLast.swap(sInks);
      sInks.clear();
      sOnLast.swap(sOn);
      sOn.clear();
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
   r.textCol = ImGui::GetStyleColorVec4(ImGuiCol_Text);
   r.bgCol = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
   sCur.push_back(std::move(r));
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext* ctx, ImGuiID id, const char* label, ImGuiItemStatusFlags flags)
{
   Roll(ctx);
   for (auto it = sCur.rbegin(); it != sCur.rend() && it - sCur.rbegin() < 4; ++it)
      if (it->id == id)
      {
         it->widget = true;
         it->label = label != nullptr ? label : "";
         it->checkable = (flags & ImGuiItemStatusFlags_Checkable) != 0;
         it->inputable = (flags & ImGuiItemStatusFlags_Inputable) != 0;
         it->labelW = label != nullptr ? ImGui::CalcTextSize(label, nullptr, true).x : 0.0f;
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

void UiAudit::NoteSlider(ImGuiID id, float frac, ImVec2 drawnMin, ImVec2 drawnMax, float valueTextW)
{
   if (!GImGui->TestEngineHookItems)
      return;
   Roll(GImGui);
   sSliders[id] = { frac, ImRect(drawnMin, drawnMax), valueTextW };
}

void UiAudit::NoteKind(const char* kind, const char* label, float textW, float availW)
{
   if (!GImGui->TestEngineHookItems)
      return;
   Roll(GImGui);
   const ImGuiID id = GImGui->LastItemData.ID;
   ItemNote& n = sNotes[id];
   n.kind = kind;
   n.textW = textW;
   n.availW = availW;
   // InvisibleButton registers an item but no item info: promote it to a control here.
   for (auto it = sCur.rbegin(); it != sCur.rend() && it - sCur.rbegin() < 4; ++it)
      if (it->id == id)
      {
         it->widget = true;
         if (it->label.empty() && label != nullptr)
            it->label = label;
         it->labelW = textW;
         break;
      }
}

void UiAudit::NoteOn()
{
   Roll(GImGui);
   sOn.insert(GImGui->LastItemData.ID);
}

void UiAudit::NoteFill(ImVec4 fill)
{
   if (!GImGui->TestEngineHookItems)
      return;
   Roll(GImGui);
   ItemNote& n = sNotes[GImGui->LastItemData.ID];
   n.hasFill = true;
   n.fill = fill;
}

void UiAudit::NoteNode(ImVec2 mn, ImVec2 mx, ImVec4 bg)
{
   if (!GImGui->TestEngineHookItems)
      return;
   Roll(GImGui);
   sNodes.push_back({ ImRect(mn, mx), bg });
}

void UiAudit::NoteRow(float x0, float y0, float rowH, float cellW, int count)
{
   if (!GImGui->TestEngineHookItems)
      return;
   Roll(GImGui);
   sRows.push_back({ x0, y0, rowH, cellW, count });
}

void UiAudit::NoteInk(int kind, ImVec2 mn, ImVec2 mx, float size, const char* tip, const char* glyph)
{
   if (!GImGui->TestEngineHookItems || GImGui->CurrentWindow == nullptr)
      return;
   Roll(GImGui);
   Ink k;
   k.kind = kind;
   k.r = ImRect(mn, mx);
   k.size = size;
   k.tip = tip != nullptr ? tip : "";
   k.glyph = glyph != nullptr ? glyph : "";
   k.window = BaseName(GImGui->CurrentWindow->Name);
   k.item = kind <= 1 || kind == 5 ? GImGui->LastItemData.ID : 0;
   k.itemBb = GImGui->LastItemData.Rect;
   sInks.push_back(std::move(k));
}

namespace app
{
namespace
{
   struct Finding { std::string check, state, window, label, detail; };
   std::vector<Finding> sFindings;
   std::set<std::string> sSeen;   // check|window|label: one row per control, the first state it showed in
   int sChecked = 0, sPoked = 0;
   std::string sThemeTag;         // " (light)" while the light theme runs: contrast findings are per theme

   void RegFinding(const std::string& window, const std::string& label);
   void Add(const char* check, const std::string& state, const Rec& r, const char* fmt, ...)
   {
      RegFinding(r.window, r.label);
      const std::string win = r.window + (strncmp(check, "contrast", 8) == 0 ? sThemeTag : std::string());
      const std::string key = std::string(check) + "|" + win + "|" + r.label;
      if (!sSeen.insert(key).second)
         return;
      char buf[256];
      va_list ap;
      va_start(ap, fmt);
      vsnprintf(buf, sizeof buf, fmt, ap);
      va_end(ap);
      sFindings.push_back({ check, state, win, r.label.empty() ? "(no label)" : r.label, buf });
   }

   // --- 2D View mode (INFINITE_UIAUDIT_VIEW2D) ---
   bool sOnlyV2D = false;
   bool IsV2D(const std::string& w) { return w.find("view2d") != std::string::npos || w.find("v2d") != std::string::npos; }
   bool IsPopupWin(const std::string& w)
   {
      return w.rfind("##Popup_", 0) == 0 || w.rfind("##Combo_", 0) == 0 || w.rfind("##Menu_", 0) == 0 || w.rfind("##Tooltip", 0) == 0;
   }
   bool InScope(const std::string& w) { return !sOnlyV2D || IsV2D(w) || IsPopupWin(w); }
   // The last path piece of a window name, without ImGui's id suffix: "Main/##view2dcontent_1A2B/##v2dside" -> "##v2dside".
   std::string ShortWin(const std::string& w)
   {
      const size_t at = w.rfind('/');
      return at == std::string::npos ? w : w.substr(at + 1);
   }
   // The tip an icon chip shows, by item id, from this frame's ink notes.
   std::string TipOf(ImGuiID id)
   {
      for (const Ink& k : sInksLast)
         if (k.item == id && !k.tip.empty())
            return k.tip;
      return std::string();
   }
   std::string GlyphOf(ImGuiID id)
   {
      for (const Ink& k : sInksLast)
         if (k.item == id && k.kind == 1)
            return k.glyph;
      return std::string();
   }
   bool HasInk(ImGuiID id)
   {
      for (const Ink& k : sInksLast)
         if (k.item == id && (k.kind == 0 || k.kind == 1 || k.kind == 5))
            return true;
      return false;
   }

   // The registry: every control the 2D View drew, where, and what testing it gave.
   struct Entry
   {
      std::string key, window, label, tip, glyph, kind, state, status = "untested", reason;
      ImRect bb;
      int findings = 0;
      bool poked = false;
   };
   std::vector<Entry> sReg;
   std::unordered_map<std::string, size_t> sRegAt;
   std::set<std::string> sV2DPoked;   // registry keys already queued for a poke
   std::vector<std::pair<std::string, std::string>> sSkips;   // (label or tip substring, reason)

   std::string KeyOf(const std::string& window, const std::string& label) { return ShortWin(window) + "|" + label; }
   Entry* RegFind(const std::string& key)
   {
      auto it = sRegAt.find(key);
      return it == sRegAt.end() ? nullptr : &sReg[it->second];
   }
   void RegFinding(const std::string& window, const std::string& label)
   {
      if (Entry* e = RegFind(KeyOf(window, label)))
         e->findings++;
   }
   const char* SkipReason(const std::string& label, const std::string& tip)
   {
      for (const auto& [pat, why] : sSkips)
         if ((!label.empty() && label.find(pat) != std::string::npos) || (!tip.empty() && tip.find(pat) != std::string::npos))
            return why.c_str();
      return nullptr;
   }

   // UiInteract items carry a key ("lib.modules", "##grip") as their ImGui label; it is never drawn.
   bool IdLike(const std::string& l) { return l.rfind("##", 0) == 0 || (l.find('.') != std::string::npos && l.find(' ') == std::string::npos); }

   // A control in an AudioKnobRow cell: its caption is drawn by the row and cut with an ellipsis, never spilled.
   bool InRow(const Rec& r)
   {
      const ImVec2 c = r.bb.GetCenter();
      for (const RowBox& row : sRowsLast)
         if (c.y >= row.y0 && c.y <= row.y0 + row.rowH && c.x >= row.x0 && c.x <= row.x0 + row.cellW * row.count)
            return true;
      return false;
   }

   bool Visible(const Rec& r) { return r.bb.Overlaps(r.clip) && r.bb.GetWidth() > 0 && r.bb.GetHeight() > 0; }
   bool FullyShown(const Rec& r) { return r.clip.Contains(r.bb); }

   float Luma(const ImVec4& c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }
   ImVec4 Over(const ImVec4& fg, const ImVec4& bg)
   {
      return ImVec4(fg.x * fg.w + bg.x * (1 - fg.w), fg.y * fg.w + bg.y * (1 - fg.w), fg.z * fg.w + bg.z * (1 - fg.w), 1.0f);
   }
   float Ratio(const ImVec4& a, const ImVec4& b)
   {
      return CategoryColors::ContrastRatio({ a.x, a.y, a.z }, { b.x, b.y, b.z });
   }

   // The canvas window also holds the app's own menu bar and transport: only what sits on the node counts.
   bool InsideNode(const Rec& r, const ImRect& box)
   {
      ImRect b = box;
      b.Expand(30.0f);
      return b.Contains(r.bb.GetCenter());
   }

   // Layer 2: every item of one frame, no input. `scope` is a node type name while one node sits alone on the
   // canvas: its items are then told apart from every other window's by that name.
   void Snapshot(const std::string& state, const std::string& scope = std::string())
   {
      std::vector<Rec> recs = sLast;
      const NodeBox* nb = nullptr;
      if (!scope.empty())
      {
         if (sNodesLast.empty())
            return;
         nb = &sNodesLast.back();
         std::unordered_map<std::string, int> votes;
         for (const Rec& r : recs)
            if (r.bb.Overlaps(nb->box))
               votes[r.window]++;
         std::string canvas;
         int best = 0;
         for (auto& [w, n] : votes)
            if (n > best) { best = n; canvas = w; }
         std::vector<Rec> keep;
         for (Rec& r : recs)
            if (r.window == canvas && InsideNode(r, nb->box))
            {
               r.window = "node " + scope;
               keep.push_back(std::move(r));
            }
         recs.swap(keep);
      }
      const bool light = CategoryColors::IsThemeLight();
      std::unordered_map<ImGuiID, const Rec*> first;
      std::unordered_map<std::string, std::vector<const Rec*>> byWindow;
      for (const Rec& r : recs)
      {
         if (!r.widget || !Visible(r) || !InScope(r.window))
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
         // A splitter grip is long and thin by design: hit along its length, so only its short side is small.
         const bool grip = std::max(r.bb.GetWidth(), r.bb.GetHeight()) >= 100.0f && std::min(r.bb.GetWidth(), r.bb.GetHeight()) >= 6.0f;
         if (!r.disabled && !grip && (r.bb.GetWidth() < 12.0f || r.bb.GetHeight() < 12.0f))
            Add("tiny target", state, r, "%.0f x %.0f", r.bb.GetWidth(), r.bb.GetHeight());
         const SliderNote* sn = nullptr;
         if (auto s = sSlidersLast.find(r.id); s != sSlidersLast.end())
         {
            sn = &s->second;
            const ImRect& d = s->second.drawn;
            const float ix = std::max(0.0f, std::min(r.bb.Max.x, d.Max.x) - std::max(r.bb.Min.x, d.Min.x));
            const float iy = std::max(0.0f, std::min(r.bb.Max.y, d.Max.y) - std::max(r.bb.Min.y, d.Min.y));
            const float cover = d.GetArea() > 0 ? ix * iy / d.GetArea() : 1.0f;
            if (cover < 0.95f)
               Add("hit smaller than drawn", state, r, "hit covers %.0f%%", cover * 100.0f);
         }
         const ItemNote* in = nullptr;
         if (auto n = sNotesLast.find(r.id); n != sNotesLast.end())
            in = &n->second;

         // Text overflow: the text a control draws is wider than the room it draws it in.
         if (in != nullptr && in->availW > 0.0f && in->textW > in->availW + 0.5f)
            Add("text overflow", state, r, "%.0f px of text in %.0f", in->textW, in->availW);
         else if (sn != nullptr && sn->textW > r.bb.GetWidth() - 4.0f)
            Add("text overflow", state, r, "value %.0f px in %.0f", sn->textW, r.bb.GetWidth());
         else if (in == nullptr && sn == nullptr && !r.checkable && !IdLike(r.label) && !InRow(r) && r.labelW > r.bb.GetWidth() + 0.5f)
            Add("text overflow", state, r, "label %.0f px in %.0f", r.labelW, r.bb.GetWidth());
         // ... or past the node's own edge.
         if (nb != nullptr)
         {
            const float out = std::max({ nb->box.Min.x - r.bb.Min.x, r.bb.Max.x - nb->box.Max.x,
                                         nb->box.Min.y - r.bb.Min.y, r.bb.Max.y - nb->box.Max.y });
            if (out > 0.5f)
               Add("text overflow", state, r, "%.0f px past the node edge", out);
         }

         // P1: a control in an AudioKnobRow stays inside its cell.
         if (nb != nullptr)
            for (const RowBox& row : sRowsLast)
            {
               const ImVec2 c = r.bb.GetCenter();
               if (c.y < row.y0 || c.y > row.y0 + row.rowH || c.x < row.x0 || c.x > row.x0 + row.cellW * row.count)
                  continue;
               const int cell = std::clamp((int)((c.x - row.x0) / row.cellW), 0, row.count - 1);
               const float lo = row.x0 + cell * row.cellW, hi = lo + row.cellW;
               const float over = std::max(lo - r.bb.Min.x, r.bb.Max.x - hi);
               if (over > 1.0f)
                  Add("off grid", state, r, "%.0f px outside cell %d of %d (%.0f wide)", over, cell + 1, row.count, row.cellW);
               break;
            }

         // P10: text keeps 4.5:1 on what is behind it; in dark, a control's resting fill stays within 0.06 luminance of the body.
         if (!r.disabled)
         {
            const ImVec4 behind = nb != nullptr ? ImVec4(nb->bg.x, nb->bg.y, nb->bg.z, 1.0f) : ImVec4(r.bgCol.x, r.bgCol.y, r.bgCol.z, 1.0f);
            if (r.labelW > 0.0f || (in != nullptr && in->textW > 0.0f))
            {
               const float ratio = Ratio(Over(r.textCol, behind), behind);
               if (ratio < 4.5f)
                  Add("contrast text", state, r, "%.1f:1 against the body", ratio);
            }
            if (!light && in != nullptr && in->hasFill)
            {
               const float d = std::fabs(Luma(Over(in->fill, behind)) - Luma(behind));
               if (d > 0.06f)
                  Add("contrast fill", state, r, "fill %.2f luminance from the body", d);
            }
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


   // --- 2D View layout rules (1 px tolerance) ---
   float Mode(std::vector<float> v)
   {
      if (v.empty())
         return 0.0f;
      std::unordered_map<int, int> n;
      int best = 0;
      float at = v[0];
      for (float x : v)
      {
         const int k = (int)std::lround(x * 2.0f);
         if (++n[k] > best || (n[k] == best && x < at))
         {
            best = n[k];
            at = k * 0.5f;
         }
      }
      return at;
   }
   float Median(std::vector<float> v)
   {
      if (v.empty())
         return 0.0f;
      std::sort(v.begin(), v.end());
      return v[v.size() / 2];
   }
   Rec Pseudo(const std::string& window, const std::string& label)
   {
      Rec r;
      r.window = window;
      r.label = label;
      return r;
   }
   std::unordered_map<std::string, std::string> sTipGlyph;   // the icon each tip was first seen with

   void V2DLayout(const std::string& state)
   {
      constexpr float tol = 1.0f;
      const float divider = 2.0f * tok::space_2 + 1.0f;
      struct It { const Rec* r; const Ink* text = nullptr; const Ink* icon = nullptr; };
      std::unordered_map<std::string, std::vector<It>> byWin;
      std::set<ImGuiID> seen;
      for (const Rec& r : sLast)
      {
         if (!r.widget || !Visible(r) || !FullyShown(r) || !IsV2D(r.window) || !seen.insert(r.id).second)
            continue;
         It it{ &r };
         for (const Ink& k : sInksLast)
            if (k.item == r.id)
            {
               if (k.kind == 0 || k.kind == 5)
                  it.text = &k;
               else if (k.kind == 1)
                  it.icon = &k;
            }
         byWin[r.window].push_back(it);
         // Off the panel: a control drawn outside the 2D View's own rectangle.
         const ImRect panel(gView2DRectMin, gView2DRectMax);
         if (panel.GetWidth() > 0 && !panel.Contains(r.bb))
            Add("off panel", state, r, "%.0f,%.0f-%.0f,%.0f outside the panel", r.bb.Min.x, r.bb.Min.y, r.bb.Max.x, r.bb.Max.y);
      }
      for (auto& [w, items] : byWin)
      {
         // Text and icon centred in their chip; text inside it.
         for (It& it : items)
         {
            const ImVec2 c = it.r->bb.GetCenter();
            if (it.text != nullptr)
            {
               const ImVec2 tc = it.text->r.GetCenter();
               if (std::fabs(tc.y - c.y) > tol)
                  Add("text off centre", state, *it.r, "text centre %+.1f px from the chip's", tc.y - c.y);
               if (it.text->kind == 0 && std::fabs(tc.x - c.x) > tol)
                  Add("text off centre", state, *it.r, "text %+.1f px across", tc.x - c.x);
               if (it.text->r.Min.x < it.r->bb.Min.x - 0.5f || it.text->r.Max.x > it.r->bb.Max.x + 0.5f)
                  Add("text clipped", state, *it.r, "%.0f px of text in a %.0f px chip", it.text->r.GetWidth(), it.r->bb.GetWidth());
            }
            if (it.icon != nullptr)
            {
               const ImVec2 ic = it.icon->r.GetCenter();
               if (it.text == nullptr && (std::fabs(ic.x - c.x) > tol || std::fabs(ic.y - c.y) > tol))
                  Add("icon off centre", state, *it.r, "ink centre %+.1f, %+.1f px", ic.x - c.x, ic.y - c.y);
               // The same action has the same icon everywhere.
               if (!it.icon->tip.empty())
               {
                  auto [g, fresh] = sTipGlyph.emplace(it.icon->tip, it.icon->glyph);
                  if (!fresh && g->second != it.icon->glyph)
                     Add("icon differs", state, *it.r, "\"%s\" drawn with another icon elsewhere", it.icon->tip.c_str());
               }
            }
         }
         // Rows of chips: one height, one centre line, one icon size, near one ink size, even gaps.
         std::vector<It*> chips;
         for (It& it : items)
            if (it.text != nullptr || it.icon != nullptr)
               chips.push_back(&it);
         std::sort(chips.begin(), chips.end(), [](It* a, It* b) { return a->r->bb.GetCenter().y < b->r->bb.GetCenter().y; });
         std::vector<std::vector<It*>> rows;
         for (It* it : chips)
         {
            if (!rows.empty() && std::fabs(rows.back().front()->r->bb.GetCenter().y - it->r->bb.GetCenter().y) <= 3.0f)
               rows.back().push_back(it);
            else
               rows.push_back({ it });
         }
         for (auto& row : rows)
         {
            if (row.size() < 2)
               continue;
            std::sort(row.begin(), row.end(), [](It* a, It* b) { return a->r->bb.Min.x < b->r->bb.Min.x; });
            std::vector<float> hs, cys, iws, isz, ink, gaps;
            for (It* it : row)
            {
               hs.push_back(it->r->bb.GetHeight());
               cys.push_back(it->r->bb.GetCenter().y);
               if (it->icon != nullptr && it->text == nullptr)
               {
                  iws.push_back(it->r->bb.GetWidth());
                  isz.push_back(it->icon->size);
                  ink.push_back(std::max(it->icon->r.GetWidth(), it->icon->r.GetHeight()));
               }
            }
            for (size_t i = 1; i < row.size(); i++)
               gaps.push_back(row[i]->r->bb.Min.x - row[i - 1]->r->bb.Max.x);
            const float mh = Mode(hs), mc = Mode(cys), mw = Mode(iws), ms = Mode(isz), mi = Median(ink), mg = Mode(gaps);
            size_t ii = 0;
            for (size_t i = 0; i < row.size(); i++)
            {
               const Rec& r = *row[i]->r;
               if (std::fabs(r.bb.GetHeight() - mh) > tol)
                  Add("chip height", state, r, "%.0f px in a row of %.0f", r.bb.GetHeight(), mh);
               if (std::fabs(r.bb.GetCenter().y - mc) > tol)
                  Add("row centre", state, r, "centre %+.1f px off the row's", r.bb.GetCenter().y - mc);
               if (row[i]->icon != nullptr && row[i]->text == nullptr)
               {
                  if (std::fabs(r.bb.GetWidth() - mw) > tol)
                     Add("chip width", state, r, "%.0f px among %.0f px icon chips", r.bb.GetWidth(), mw);
                  if (std::fabs(isz[ii] - ms) > 0.5f)
                     Add("icon size", state, r, "%.0f px icon among %.0f px", isz[ii], ms);
                  if (std::fabs(ink[ii] - mi) > 2.0f)
                     Add("ink size", state, r, "ink %.0f px, the row's median %.0f", ink[ii], mi);
                  ii++;
               }
               if (i > 0)
               {
                  const float g = gaps[i - 1];
                  if (g < -0.5f)
                     Add("overlap", state, r, "%.0f px over the chip before it", -g);
                  else if (std::fabs(g - mg) > tol && g < divider - tol && std::fabs(g - tok::space_1) > tol)
                     Add("uneven gap", state, r, "%.1f px gap, the row's is %.1f", g, mg);
               }
            }
         }
      }
      // Form rows: labels share a left edge, fields a right edge, and a label sits on its field's centre line.
      std::unordered_map<std::string, std::vector<const Ink*>> labels, checks;
      for (const Ink& k : sInksLast)
         if (IsV2D(k.window) && (k.kind == 3 || k.kind == 4))
            (k.kind == 3 ? labels : checks)[k.window].push_back(&k);
      for (auto& [w, ls] : labels)
      {
         std::vector<float> lx, fx;
         std::vector<const Rec*> field(ls.size(), nullptr);
         for (size_t i = 0; i < ls.size(); i++)
         {
            lx.push_back(ls[i]->r.Min.x);
            for (const Rec& r : sLast)
               if (r.widget && r.window == w && r.bb.Min.x >= ls[i]->r.Max.x - 1.0f && r.bb.Min.y <= ls[i]->r.GetCenter().y &&
                   r.bb.Max.y >= ls[i]->r.GetCenter().y && (field[i] == nullptr || r.bb.Max.x > field[i]->bb.Max.x))
                  field[i] = &r;
            if (field[i] != nullptr)
               fx.push_back(field[i]->bb.Max.x);
         }
         const float ml = Mode(lx), mf = Mode(fx);
         for (size_t i = 0; i < ls.size(); i++)
         {
            const Rec pr = Pseudo(w, ls[i]->tip);
            const float dx = ls[i]->r.Min.x - ml;
            if (std::fabs(dx) > tol && std::fabs(dx) < 40.0f)
               Add("label edge", state, pr, "left edge %+.0f px from the column's", dx);
            if (field[i] != nullptr)
            {
               const float df = field[i]->bb.Max.x - mf;
               if (std::fabs(df) > tol && std::fabs(df) < 60.0f)
                  Add("field edge", state, pr, "field ends %+.0f px from the column's", df);
               const float dy = ls[i]->r.GetCenter().y - field[i]->bb.GetCenter().y;
               if (std::fabs(dy) > tol)
                  Add("label off row", state, pr, "label centre %+.1f px from its field's", dy);
            }
         }
      }
      for (auto& [w, cs] : checks)
      {
         std::vector<float> gaps;
         std::vector<std::pair<const Ink*, const Rec*>> pairs;
         for (const Ink* k : cs)
         {
            const Rec* box = nullptr;
            for (const Rec& r : sLast)
               if (r.widget && r.window == w && r.bb.Max.x <= k->r.Min.x + 0.5f && r.bb.Min.y <= k->r.GetCenter().y + 2 &&
                   r.bb.Max.y >= k->r.GetCenter().y - 2 && (box == nullptr || r.bb.Max.x > box->bb.Max.x))
                  box = &r;
            if (box == nullptr)
               continue;
            pairs.push_back({ k, box });
            gaps.push_back(k->r.Min.x - box->bb.Max.x);
         }
         const float mg = Mode(gaps);
         for (auto& [k, box] : pairs)
         {
            const Rec pr = Pseudo(w, k->tip);
            if (std::fabs(k->r.Min.x - box->bb.Max.x - mg) > tol)
               Add("checkbox gap", state, pr, "%.0f px from its box, the others %.0f", k->r.Min.x - box->bb.Max.x, mg);
            const float dy = k->r.GetCenter().y - box->bb.GetCenter().y;
            if (std::fabs(dy) > tol)
               Add("checkbox label off centre", state, pr, "%+.1f px from the box's centre", dy);
         }
      }
      // Loose text on a tile row sits on the row's centre line.
      for (const Ink& k : sInksLast)
      {
         if (k.kind != 2 || !IsV2D(k.window))
            continue;
         std::vector<float> cys;
         for (const Rec& r : sLast)
            if (r.widget && r.window == k.window && HasInk(r.id) && r.bb.Min.y <= k.r.GetCenter().y && r.bb.Max.y >= k.r.GetCenter().y)
               cys.push_back(r.bb.GetCenter().y);
         if (!cys.empty() && std::fabs(k.r.GetCenter().y - Median(cys)) > tol)
            Add("text off row", state, Pseudo(k.window, k.tip), "%+.1f px from the chips' centre line", k.r.GetCenter().y - Median(cys));
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
      ImGui::SetNextWindowSize(ImVec2(220, 200));
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
      // Text wider than its button; text nearly the colour of the window behind it.
      ImGui::Button("A label far too long for its button", ImVec2(60, 0));
      ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
      ImGui::Button("Faint##canary");
      ImGui::PopStyleColor();
      ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 30);
      ImGui::Button("Cut off", ImVec2(80, 0));
      ImGui::End();
   }

   // While the audit runs its mouse is the only mouse: the backend's events (the real cursor) are dropped each
   // frame before ImGui reads them, and the audit's position is held until it moves it.
   ImVec2 sMouse(-10000.0f, -10000.0f);
   int sButton = -1;   // -1 nothing to send, else 0 up / 1 down
   float sWheel = 0.0f;
   int sKey = -1;      // -1 nothing to send, else 0 up / 1 down (sKeyName, Escape unless a poke set another)
   ImGuiKey sKeyName = ImGuiKey_Escape;
   bool sKeyCmd = false;   // hold the shortcut modifier with the key
   std::string sChars;     // characters to type this frame
   bool sAllowDouble = false;
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
      // Every press is a single click: pokes come frames apart, and a press near the last one would count as a
      // double-click (a slider then opens its number field instead of dragging).
      if (sButton == 1 && !sAllowDouble)
         ctx->IO.MouseClickedTime[0] = -FLT_MAX;
      if (sButton >= 0)
         ctx->IO.AddMouseButtonEvent(0, sButton == 1);
      if (sWheel != 0.0f)
         ctx->IO.AddMouseWheelEvent(0.0f, sWheel);
      if (sKey >= 0)
      {
         if (sKeyCmd)
            ctx->IO.AddKeyEvent(ImGuiMod_Ctrl, sKey == 1);
         ctx->IO.AddKeyEvent(sKeyName, sKey == 1);
      }
      for (char c : sChars)
         ctx->IO.AddInputCharacter((unsigned)c);
      sChars.clear();
      sButton = -1;
      sKey = -1;
      sKeyName = ImGuiKey_Escape;
      sKeyCmd = false;
      sWheel = 0.0f;
   }

   // The node canvas in screen pixels: refreshed each frame after the editor has ended (the one place CanvasToScreen is safe).
   ImVec2 sCanvasOrigin(0, 0);
   float sCanvasScale = 1.0f;
   void ReadCanvas()
   {
      if (gEditor == nullptr)
         return;
      ed::EditorContext* prev = ed::GetCurrentEditor();
      ed::SetCurrentEditor(gEditor);
      const ImVec2 o = ed::CanvasToScreen(ImVec2(0, 0)), u = ed::CanvasToScreen(ImVec2(1, 1));
      sCanvasOrigin = o;
      sCanvasScale = u.x - o.x;
      ed::SetCurrentEditor(prev);
   }

   // Every value in the patch, as text, for "this poke changed nothing". The canvas view is not a value.
   std::string Fingerprint()
   {
      Patch::Data d = BuildPatchData();
      d.viewport = {};
      return PatchJson::ToJson(d).dump();
   }

   // Where two fingerprints first differ, as a short quote of the old and the new text.
   std::string FirstDiff(const std::string& a, const std::string& b)
   {
      size_t i = 0;
      while (i < a.size() && i < b.size() && a[i] == b[i])
         i++;
      const size_t from = i > 20 ? i - 20 : 0;
      auto quote = [&](const std::string& s) {
         std::string q = from < s.size() ? s.substr(from, 48) : std::string();
         for (char& c : q)
            if (c == '\t' || c == '\n' || c == '|')
               c = ' ';
         return q;
      };
      return quote(a) + " -> " + quote(b);
   }

   // Layer 3. Kinds: slider (press at 25%, drag to each end, release, undo), hover (wheel over it, nothing changes),
   // popup (dropdown, colour dot: one popup opens, Esc closes it, nothing changes), toggle (click twice).
   enum class Kind { Slider, Hover, Popup, Toggle, Click, Combo, Typed };
   struct Poke
   {
      Kind kind = Kind::Slider;
      ImGuiID id = 0;
      ImRect bb;
      std::string label, window, state;
      bool inNode = false;
      float before = 0, at25 = 0, atMax = 0, atMin = 0;
      size_t depth = 0;
      bool undoable = false, took = false, gone = false, on = false;
      std::string over, fp0, fp1;   // the window under the press; patch text before, after the first click
      int popups = 0, popupsAfter = 0, step = 0;
      // 2D View pokes: panel state and picture before/after, and the dropdown's options as found when it opened.
      std::string ps0, ps1, tip, rangeBad, typedText;
      uint64_t rh0 = 0, rh1 = 0;
      std::vector<std::pair<ImGuiID, ImRect>> opts;
      size_t opt = 0;
      int optsDead = 0, typedTook = 0;
      bool popupWin = false;   // the control lives in a popup the poke has to open first (not used yet)
   };
   std::vector<Poke> sQueue;
   Poke sPoke;
   bool sPoking = false;

   ImVec2 Scr(const Poke& k, ImVec2 c) { return k.inNode ? ImVec2(sCanvasOrigin.x + c.x * sCanvasScale, sCanvasOrigin.y + c.y * sCanvasScale) : c; }
   const Rec* Find(ImGuiID id)
   {
      for (const Rec& r : sLast)
         if (r.id == id && r.widget)
            return &r;
      return nullptr;
   }

   float SliderFrac(ImGuiID id, float fallback)
   {
      auto it = sSlidersLast.find(id);
      return it != sSlidersLast.end() ? it->second.frac : fallback;
   }

   Rec PokeRec(const Poke& k)
   {
      Rec r;
      r.window = k.window;
      r.label = k.label;
      return r;
   }

   void Judge(const Poke& k)
   {
      sPoked++;
      Rec r = PokeRec(k);
      if (k.gone)
      {
         sV2DPoked.erase(KeyOf(k.window, k.label));   // an earlier poke took it away (crop Apply ends Cancel): a later state retries
         return;
      }
      if (Entry* e = RegFind(KeyOf(k.window, k.label)))
         e->poked = true;
      if (k.kind == Kind::Click)
      {
         const bool doc = k.fp1 != k.fp0, panel = k.ps1 != k.ps0, pic = k.rh1 != k.rh0;
         if (!doc && !panel && !pic && k.popups == 0 && k.typedTook == 0 && !k.on)
            Add("click does nothing", k.state, r, "%s", k.tip.empty() ? "no value, panel state, picture or popup changed" : k.tip.c_str());
         if (k.popups > 1)
            Add("more than one popup", k.state, r, "%d popups open after one click", k.popups);
         if (doc && !k.undoable)
            Add("no undo step", k.state, r, "changed the document with no undo step, near: %s", FirstDiff(k.fp0, k.fp1).c_str());
         else if (doc && k.fp0 != k.fp1 && !k.over.empty())
            Add("undo does not restore", k.state, r, "near: %s", k.over.c_str());
         if (!k.rangeBad.empty())
            Add("value out of range", k.state, r, "%s", k.rangeBad.c_str());
         return;
      }
      if (k.kind == Kind::Combo)
      {
         if (k.opts.empty())
            Add("dropdown empty", k.state, r, "the menu showed no options");
         else if (k.optsDead >= 2)
            Add("option changes nothing", k.state, r, "%d of %zu options changed nothing", k.optsDead, k.opts.size());
         if (!k.rangeBad.empty())
            Add("value out of range", k.state, r, "%s", k.rangeBad.c_str());
         if (!k.over.empty())
            Add("undo does not restore", k.state, r, "near: %s", k.over.c_str());
         return;
      }
      if (k.kind == Kind::Typed)
      {
         if (k.typedTook == 0)
            return;   // a double-click opens no text field here; the slider poke covers it
         if (!k.rangeBad.empty())
            Add("typed value not clamped", k.state, r, "typed %s: %s", k.typedText.c_str(), k.rangeBad.c_str());
         if (k.fp1 != k.fp0 && !k.undoable)
            Add("no undo step", k.state, r, "typed %s with no undo step", k.typedText.c_str());
         else if (!k.over.empty())
            Add("undo does not restore", k.state, r, "near: %s", k.over.c_str());
         return;
      }
      if (k.kind == Kind::Hover)
      {
         if (Fingerprint() != k.fp0)
            Add("hover/scroll changes value", k.state, r, "near: %s", FirstDiff(k.fp0, Fingerprint()).c_str());
         return;
      }
      if (k.kind == Kind::Popup)
      {
         if (k.popups == 0)
            Add("popup did not open", k.state, r, "click opened nothing");
         else if (k.popups > 1)
            Add("more than one popup", k.state, r, "%d popups open after one click", k.popups);
         if (k.popupsAfter > 0)
            Add("Esc does not close", k.state, r, "%d popups still open after Esc", k.popupsAfter);
         if (Fingerprint() != k.fp0)
            Add("popup changes value", k.state, r, "near: %s", FirstDiff(k.fp0, Fingerprint()).c_str());
         return;
      }
      if (k.kind == Kind::Toggle)
      {
         if (k.fp1 == k.fp0 && k.ps1 == k.ps0)
            Add("click does not toggle", k.state, r, "first click changed nothing");
         if (k.popups > 0)
            Add("more than one popup", k.state, r, "a checkbox click opened %d popups", k.popups);
         if (Fingerprint() != k.fp0 || (sOnlyV2D && app::View2DAuditPanelState() != k.ps0))
            Add("toggle does not return", k.state, r, "near: %s", FirstDiff(k.fp0, Fingerprint()).c_str());
         return;
      }
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
      // A tool option (brush size) is not the document's: no undo step is right when the drag left the document alone.
      if (!k.undoable && k.fp1 != k.fp0)
         Add("no undo step", k.state, r, "the drag pushed no undo checkpoint");
      else if (k.undoable && std::fabs(SliderFrac(k.id, -1.0f) - k.before) > 1e-4f)
         Add("undo does not restore", k.state, r, "%.3f before, %.3f after undo", k.before, SliderFrac(k.id, -1.0f));
   }

   int OpenPopups() { return GImGui->OpenPopupStack.Size; }
   void CloseAllPopups()
   {
      if (GImGui->OpenPopupStack.Size > 0)
         ImGui::ClosePopupToLevel(0, true);
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
         CloseAllPopups();
      }
      Poke& k = sPoke;
      if (k.step == 0)
      {
         // Where the control is now: an earlier poke may have scrolled the panel.
         const Rec* r = Find(k.id);
         if (r == nullptr || !FullyShown(*r))
         {
            k.gone = true;
            Judge(k);
            sPoking = false;
            return true;
         }
         k.bb = r->bb;
      }
      const ImVec2 c = Scr(k, k.bb.GetCenter());
      switch (k.kind)
      {
      case Kind::Hover:
         switch (k.step++)
         {
         case 0: k.fp0 = Fingerprint(); sMouse = c; break;
         case 2: sWheel = -1.0f; break;
         case 3: sWheel = 1.0f; break;
         case 5: sWheel = 1.0f; break;
         case 6: sWheel = -1.0f; break;
         case 8: sMouse = ImVec2(-10000.0f, -10000.0f); break;
         case 10: Judge(k); sPoking = false; break;
         default: break;
         }
         return true;
      case Kind::Popup:
         switch (k.step++)
         {
         case 0: k.fp0 = Fingerprint(); sMouse = c; break;
         case 1: sButton = 1; break;
         case 2: sButton = 0; break;
         case 6: k.popups = OpenPopups(); break;
         case 7: sKey = 1; break;
         case 8: sKey = 0; break;
         case 11: k.popupsAfter = OpenPopups(); CloseAllPopups(); sMouse = ImVec2(-10000.0f, -10000.0f); break;
         case 13: Judge(k); sPoking = false; break;
         default: break;
         }
         return true;
      case Kind::Toggle:
         switch (k.step++)
         {
         case 0: k.fp0 = Fingerprint(); if (sOnlyV2D) k.ps0 = app::View2DAuditPanelState(); sMouse = c; break;
         case 1: sButton = 1; break;
         case 2: sButton = 0; break;
         case 5: k.fp1 = Fingerprint(); if (sOnlyV2D) k.ps1 = app::View2DAuditPanelState(); k.popups = OpenPopups(); CloseAllPopups(); sButton = 1; break;
         case 6: sButton = 0; break;
         case 9: sMouse = ImVec2(-10000.0f, -10000.0f); break;
         case 11: Judge(k); sPoking = false; break;
         default: break;
         }
         return true;
      case Kind::Click:
         switch (k.step++)
         {
         case 0:
            k.fp0 = Fingerprint(); k.ps0 = app::View2DAuditPanelState(); k.rh0 = app::View2DAuditRenderHash();
            k.depth = gUndoStack.size(); sMouse = c; break;
         case 1: sButton = 1; break;
         case 2: sButton = 0; break;
         case 6:
            k.fp1 = Fingerprint(); k.ps1 = app::View2DAuditPanelState(); k.rh1 = app::View2DAuditRenderHash();
            k.popups = OpenPopups(); k.typedTook = GImGui->ActiveId != 0 && ImGui::TempInputIsActive(GImGui->ActiveId) ? 1 : 0;
            k.rangeBad = app::View2DAuditRanges();
            if (k.popups > 0)
            {
               // The chip opens a menu: walk its options as a dropdown would.
               Poke m = k;
               m.kind = Kind::Combo;
               m.step = 0;
               m.opts.clear();
               sQueue.push_back(m);
            }
            sKey = 1; break;
         case 7: sKey = 0; break;
         case 9:
            CloseAllPopups(); sMouse = ImVec2(-10000.0f, -10000.0f);
            k.undoable = gUndoStack.size() > k.depth;
            while (gUndoStack.size() > k.depth)
               Undo();
            break;
         case 11:
            if (k.fp1 != k.fp0 && k.undoable && Fingerprint() != k.fp0)
               k.over = FirstDiff(k.fp0, Fingerprint());
            Judge(k); app::View2DAuditRestore(); sPoking = false; break;
         default: break;
         }
         return true;
      case Kind::Combo:
      {
         // Open, gather the options of the top popup, click option opt, reopen for the next; Esc and undo at the end.
         const int s = k.step++;
         if (s == 0) { k.fp0 = Fingerprint(); k.ps0 = app::View2DAuditPanelState(); k.rh0 = app::View2DAuditRenderHash(); k.depth = gUndoStack.size(); sMouse = c; }
         else if (s == 1) sButton = 1;
         else if (s == 2) sButton = 0;
         else if (s == 5)
         {
            if (OpenPopups() == 0) { k.step = 100; return true; }
            if (k.opts.empty())
            {
               ImGuiWindow* pw = GImGui->OpenPopupStack.back().Window;
               const std::string pn = pw != nullptr ? BaseName(pw->Name) : std::string();
               std::set<ImGuiID> got;
               for (const Rec& r : sLast)
                  if (r.widget && !r.disabled && r.window == pn && sOnLast.count(r.id) == 0 && got.insert(r.id).second && r.bb.GetWidth() > 2 && !IdLike(r.label))
                     k.opts.push_back({ r.id, r.bb });
               if (k.opts.size() > 24)
                  k.opts.resize(24);
            }
            if (k.opt >= k.opts.size()) { k.step = 100; return true; }
            k.fp1 = Fingerprint(); k.ps1 = app::View2DAuditPanelState(); k.rh1 = app::View2DAuditRenderHash();
            sMouse = k.opts[k.opt].second.GetCenter();
         }
         else if (s == 6) sButton = 1;
         else if (s == 7) sButton = 0;
         else if (s == 10)
         {
            if (Fingerprint() == k.fp1 && app::View2DAuditPanelState() == k.ps1 && app::View2DAuditRenderHash() == k.rh1)
               k.optsDead++;
            if (k.rangeBad.empty())
               k.rangeBad = app::View2DAuditRanges();
            CloseAllPopups();
            k.opt++;
            sMouse = c;
            k.step = k.opt < k.opts.size() ? 1 : 100;
         }
         else if (s == 100)
         {
            CloseAllPopups(); sMouse = ImVec2(-10000.0f, -10000.0f);
            while (gUndoStack.size() > k.depth)
               Undo();
         }
         else if (s == 102)
         {
            if (Fingerprint() != k.fp0)
               k.over = FirstDiff(k.fp0, Fingerprint());
            Judge(k); app::View2DAuditRestore(); sPoking = false;
         }
         return true;
      }
      case Kind::Typed:
         switch (k.step++)
         {
         case 0: k.fp0 = Fingerprint(); k.depth = gUndoStack.size(); sMouse = c; sAllowDouble = true; break;
         case 1: sButton = 1; break;
         case 2: sButton = 0; break;
         case 3: sButton = 1; break;
         case 4: sButton = 0; break;
         case 7:
            sAllowDouble = false;
            if (GImGui->ActiveId != 0 && ImGui::TempInputIsActive(GImGui->ActiveId))
            {
               k.typedTook = 1;
               sKeyName = ImGuiKey_A; sKeyCmd = true; sKey = 1;
            }
            else if (GImGui->ActiveId != 0 && (GImGui->InputTextState.ID == GImGui->ActiveId))
            {
               k.typedTook = 1;
               sKeyName = ImGuiKey_A; sKeyCmd = true; sKey = 1;
            }
            else
               k.step = 20;
            break;
         case 8: sKeyName = ImGuiKey_A; sKeyCmd = true; sKey = 0; break;
         case 9: sChars = k.typedText; break;
         case 11: sKeyName = ImGuiKey_Enter; sKey = 1; break;
         case 12: sKeyName = ImGuiKey_Enter; sKey = 0; break;
         case 13:
            // A multi-line field keeps focus on Enter; a person clicks away before they undo.
            if (GImGui->ActiveId != 0 && GImGui->InputTextState.ID == GImGui->ActiveId)
               ImGui::ClearActiveID();
            break;
         case 15:
            k.fp1 = Fingerprint(); k.rangeBad = app::View2DAuditRanges(); sMouse = ImVec2(-10000.0f, -10000.0f);
            k.undoable = gUndoStack.size() > k.depth;
            while (gUndoStack.size() > k.depth)
               Undo();
            break;
         case 17:
            if (k.fp1 != k.fp0 && k.undoable && Fingerprint() != k.fp0)
               k.over = FirstDiff(k.fp0, Fingerprint());
            break;
         case 21:
            sAllowDouble = false; ImGui::ClearActiveID(); CloseAllPopups();
            Judge(k); app::View2DAuditRestore(); sPoking = false; break;
         default: break;
         }
         return true;
      case Kind::Slider:
         break;
      }
      const float y = k.bb.GetCenter().y;
      const ImVec2 left = Scr(k, ImVec2(k.bb.Min.x, y)), right = Scr(k, ImVec2(k.bb.Max.x, y));
      const float w = right.x - left.x;
      switch (k.step++)
      {
      case 0:
         k.before = SliderFrac(k.id, 0);
         k.depth = gUndoStack.size();
         k.fp0 = Fingerprint();
         sMouse = ImVec2(left.x + w * 0.25f, left.y);
         break;
      case 1: sButton = 1; break;
      case 3:
         // The press landed last frame; the panel draws after this runs, so its ActiveId is from then.
         k.took = GImGui->ActiveId == k.id;
         k.over = GImGui->HoveredWindow != nullptr ? BaseName(GImGui->HoveredWindow->Name) : "nothing";
         k.at25 = SliderFrac(k.id, -1); sMouse = ImVec2(right.x - 1.0f, right.y); break;
      case 5: k.atMax = SliderFrac(k.id, -1); k.fp1 = Fingerprint(); sMouse = ImVec2(left.x + 1.0f, left.y); break;
      case 7: k.atMin = SliderFrac(k.id, -1); sButton = 0; break;
      case 8: sMouse = ImVec2(-10000.0f, -10000.0f); break;
      case 9:
         // Undo only this edit's own step; without one, undoing would take back something else.
         k.undoable = gUndoStack.size() > k.depth;
         if (k.undoable)
            Undo();
         break;
      case 11:
         Judge(k);
         if (sOnlyV2D)
            app::View2DAuditRestore();
         sPoking = false;
         break;
      default: break;
      }
      return true;
   }

   // 2D View: register every control drawn in this state, then poke each control once across all states.
   std::string KindOf(const Rec& r)
   {
      const auto note = sNotesLast.find(r.id);
      if (note != sNotesLast.end() && note->second.kind != nullptr)
         return note->second.kind;
      if (sSlidersLast.count(r.id) != 0)
         return "slider";
      if (r.checkable)
         return "checkbox";
      if (r.inputable)
         return "field";
      if (!GlyphOf(r.id).empty())
         return "icon";
      if (HasInk(r.id))
         return "button";
      return "item";
   }
   void V2DRegisterAndPoke(const std::string& state)
   {
      std::set<ImGuiID> seen;
      int queued = 0;
      for (const Rec& r : sLast)
      {
         if (!r.widget || !IsV2D(r.window) || !Visible(r) || !seen.insert(r.id).second)
            continue;
         const std::string key = KeyOf(r.window, r.label);
         Entry* e = RegFind(key);
         if (e == nullptr)
         {
            sRegAt[key] = sReg.size();
            sReg.push_back({});
            e = &sReg.back();
            e->key = key;
            e->window = ShortWin(r.window);
            e->label = r.label;
            e->tip = TipOf(r.id);
            e->glyph = GlyphOf(r.id);
            e->kind = KindOf(r);
            e->state = state;
            e->bb = r.bb;
         }
         if (r.disabled || !FullyShown(r) || sV2DPoked.count(key) != 0)
            continue;
         if (const char* why = SkipReason(r.label, e->tip))
         {
            if (e->reason.empty())
               e->reason = why;
            continue;
         }
         Poke k;
         k.id = r.id;
         k.bb = r.bb;
         k.label = r.label;
         k.window = r.window;
         k.state = state;
         k.tip = e->tip;
         k.on = sOnLast.count(r.id) != 0;
         const std::string& kind = e->kind;
         bool any = true;
         if (kind == "slider")
         {
            k.kind = Kind::Slider;
            sQueue.insert(sQueue.begin(), k);
            if (r.inputable)
            {
               k.kind = Kind::Typed; k.typedText = "99999"; sQueue.insert(sQueue.begin(), k);
               k.typedText = "-99999"; sQueue.insert(sQueue.begin(), k);
            }
         }
         else if (kind == "dropdown")
         {
            k.kind = Kind::Combo;
            sQueue.insert(sQueue.begin(), k);
         }
         else if (kind == "colour")
         {
            k.kind = Kind::Popup;
            sQueue.insert(sQueue.begin(), k);
         }
         else if (kind == "checkbox")
         {
            k.kind = Kind::Toggle;
            sQueue.insert(sQueue.begin(), k);
         }
         else if (r.inputable)
         {
            k.kind = Kind::Typed; k.typedText = "99999"; sQueue.insert(sQueue.begin(), k);
            k.typedText = "-99999"; sQueue.insert(sQueue.begin(), k);
         }
         else if (kind == "icon" || kind == "button" || kind == "item" || HasInk(r.id))
         {
            // Ink-less items (a layer row, its eye, a colour dot, the anchor grid) are clicked too: each must
            // change something, open something, or carry a reason in the skip file.
            k.kind = Kind::Click;
            sQueue.insert(sQueue.begin(), k);
         }
         else
            any = false;
         if (any)
         {
            sV2DPoked.insert(key);
            queued++;
         }
      }
      // Per state: the document survives its save text and holds no out-of-range value.
      const std::string rt = app::View2DAuditRoundTrip();
      if (!rt.empty())
         Add("save/load differs", state, Pseudo("##view2dcontent", "document"), "%s", rt.c_str());
      const std::string rg = app::View2DAuditRanges();
      if (!rg.empty())
         Add("value out of range", state, Pseudo("##view2dcontent", "document"), "%s", rg.c_str());
      printf("UIAUDIT v2d %s: %zu items, %zu registered, %d queued\n", state.c_str(), sLast.size(), sReg.size(), queued);
   }

   std::string JsonEsc(const std::string& s)
   {
      std::string o;
      for (char c : s)
      {
         if (c == '"' || c == '\\') { o += '\\'; o += c; }
         else if ((unsigned char)c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
         else o += c;
      }
      return o;
   }

   void WriteRegistry(const char* dir)
   {
      int pass = 0, fail = 0, skipped = 0, untested = 0;
      std::ofstream j(std::string(dir) + "/registry.json"), t(std::string(dir) + "/coverage.tsv");
      t << "status\tkind\twindow\tlabel\ttip\tstate\treason\n";
      j << "[\n";
      for (size_t i = 0; i < sReg.size(); i++)
      {
         Entry& e = sReg[i];
         if (e.findings > 0) { e.status = "fail"; fail++; }
         else if (e.poked) { e.status = "pass"; pass++; }
         else if (!e.reason.empty()) { e.status = "skipped"; skipped++; }
         else { e.status = "untested"; untested++; }
         t << e.status << '\t' << e.kind << '\t' << e.window << '\t' << e.label << '\t' << e.tip << '\t' << e.state << '\t' << e.reason << '\n';
         j << "  {\"key\": \"" << JsonEsc(e.key) << "\", \"kind\": \"" << e.kind << "\", \"window\": \"" << JsonEsc(e.window)
           << "\", \"label\": \"" << JsonEsc(e.label) << "\", \"tip\": \"" << JsonEsc(e.tip) << "\", \"glyph_bytes\": " << e.glyph.size()
           << ", \"state\": \"" << JsonEsc(e.state) << "\", \"status\": \"" << e.status << "\", \"reason\": \"" << JsonEsc(e.reason)
           << "\", \"findings\": " << e.findings << ", \"bb\": [" << e.bb.Min.x << ", " << e.bb.Min.y << ", " << e.bb.Max.x << ", " << e.bb.Max.y
           << "]}" << (i + 1 < sReg.size() ? "," : "") << "\n";
      }
      j << "]\n";
      t << "#\tpass " << pass << "\tfail " << fail << "\tskipped " << skipped << "\tuntested " << untested << '\n';
      printf("UIAUDIT v2d registry: %zu controls, pass %d fail %d skipped %d untested %d\n", sReg.size(), pass, fail, skipped, untested);
   }

   void LoadSkips()
   {
      const char* path = getenv("INFINITE_UIAUDIT_V2D_SKIP");
      if (path == nullptr)
         return;
      std::ifstream f(path);
      std::string line;
      while (std::getline(f, line))
      {
         if (line.empty() || (line[0] == '#' && line.compare(0, 2, "##") != 0))   // "# note"; "##id" is a pattern
            continue;
         const size_t tab = line.find('\t');
         if (tab != std::string::npos)
            sSkips.emplace_back(line.substr(0, tab), line.substr(tab + 1));
      }
      printf("UIAUDIT v2d skips: %zu from %s\n", sSkips.size(), path);
   }

   // Sliders only in the 3D View's column and on node bodies; popups, toggles and hover for every control.
   void QueuePokes(const std::string& state, const std::string& scope, int hoverCap)
   {
      const size_t had = sQueue.size();
      std::string canvas;
      if (!scope.empty() && !sNodesLast.empty())
      {
         std::unordered_map<std::string, int> votes;
         int best = 0;
         for (const Rec& r : sLast)
            if (r.bb.Overlaps(sNodesLast.back().box))
               best = std::max(best, ++votes[r.window]);
         for (auto& [w, n] : votes)
            if (n == best) canvas = w;
      }
      int sliders = 0, hovers = 0;
      std::set<ImGuiID> seen;
      for (const Rec& r : sLast)
      {
         if (!r.widget || r.disabled || !FullyShown(r) || !Visible(r) || !seen.insert(r.id).second)
            continue;
         const bool inNode = !scope.empty();
         if (inNode && (r.window != canvas || !InsideNode(r, sNodesLast.back().box)))
            continue;
         Poke k;
         k.id = r.id;
         k.bb = r.bb;
         k.label = r.label;
         k.window = inNode ? "node " + scope : r.window;
         k.state = state;
         k.inNode = inNode;
         const auto note = sNotesLast.find(r.id);
         const char* kind = note != sNotesLast.end() ? note->second.kind : nullptr;
         if (sSlidersLast.count(r.id) != 0 && (inNode || r.window.find("v3dprops") != std::string::npos))
         {
            k.kind = Kind::Slider;
            sliders++;
            sQueue.insert(sQueue.begin(), k);
         }
         else if (kind != nullptr && (strcmp(kind, "dropdown") == 0 || strcmp(kind, "colour") == 0))
         {
            k.kind = Kind::Popup;
            sQueue.insert(sQueue.begin(), k);
         }
         else if (kind != nullptr && strcmp(kind, "checkbox") == 0 || r.checkable)
         {
            k.kind = Kind::Toggle;
            sQueue.insert(sQueue.begin(), k);
         }
         if (hovers < hoverCap && r.bb.GetWidth() >= 4.0f)
         {
            k.kind = Kind::Hover;
            hovers++;
            sQueue.insert(sQueue.begin(), k);
         }
      }
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

   // --- the driver: themes x (3D View tabs, other panels, one node of every type) ---
   enum class Phase { Start, Tabs, Panels, ClearNodes, Nodes, NextTheme, View2D, Done };
   struct PanelFlag { const char* name; bool* flag; };

   std::string ThemeName(int t) { return t == 0 ? "dark" : "light"; }

   void SetTheme(int t)
   {
      int pick = 0;
      for (int i = 0; i < (int)CategoryColors::PresetNames().size(); i++)
         if ((Luma(ImVec4(CategoryColors::UiThemeForPreset(i).windowBg.r, CategoryColors::UiThemeForPreset(i).windowBg.g,
                          CategoryColors::UiThemeForPreset(i).windowBg.b, 1.0f)) > 0.5f) == (t == 1))
         {
            pick = i;
            break;
         }
      CategoryColors::SetPresetTransient(pick);
      ApplyTheme();
      sThemeTag = t == 1 ? " (light)" : "";
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
   static int sTabAt = -1, sPage = 0, sWait = 0, sTheme = 0;
   static Phase sPhase = Phase::Start;
   static std::vector<std::pair<std::string, std::string>> sTypes;   // (category, type)
   static size_t sType = 0, sPanel = 0;
   static int sNodeStep = 0;
   static std::string sOnly;
   static bool sDone = false;
   if (sDone)
      return;
   if (frameId < 3)
      return;
   if (sPhase != Phase::Start && GImGui->TestEngineHookItems)
      ReadCanvas();
   if (frameId == 3)
   {
      GImGui->TestEngineHookItems = true;
      static ImGuiContextHook hook;
      hook.Type = ImGuiContextHookType_NewFramePre;
      hook.Callback = OwnMouse;
      ImGui::AddContextHook(GImGui, &hook);
      SetCanvasSwapInterval(0);
      if (const char* only = getenv("INFINITE_UIAUDIT_NODES"))
         sOnly = only;
      for (const std::string& cat : NodeFactory::Instance().GetCategories())
         for (const std::string& n : NodeFactory::Instance().GetNodesInCategory(cat))
            if (sOnly.empty() || sOnly == "none" ? sOnly.empty() : (sOnly + ",").find(n + ",") != std::string::npos)
               sTypes.emplace_back(cat, n);
      const char* patch = getenv("INFINITE_UIAUDIT_PATCH");
      const bool opened = patch != nullptr && LoadPatchFrom(patch);
      printf("UIAUDIT patch %s opened=%d, %zu node types\n", patch != nullptr ? patch : "(none)", opened ? 1 : 0, sTypes.size());
      if (getenv("INFINITE_UIAUDIT_VIEW2D") != nullptr)
      {
         sOnlyV2D = true;
         LoadSkips();
         sPhase = Phase::View2D;
         sWait = 40;
         return;
      }
      gView3DOpen = true;
      sTabAt = 0;
      sWait = 30;   // the scene, its previews and the panel settle
      sPhase = Phase::Tabs;
      return;
   }
   if (StepPoke())
      return;
   const std::string tn = ThemeName(sTheme);

   if (sPhase == Phase::Tabs)
   {
      const bool canary = kTabs[sTabAt] == 8;
      if (canary && sTheme == 0)
         DrawCanary();
      if (canary && sTheme == 1)
      {
         sTabAt++;
         sPhase = Phase::Panels;
         gView3DOpen = false;
         sPanel = 0;
         sWait = 20;
         return;
      }
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
      const std::string state = tn + " " + kNames[sTabAt] + (sPage > 0 ? " p" + std::to_string(sPage + 1) : "");
      Snapshot(state);
      if (!canary)
         QueuePokes(state, "", 6);
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
         sTabAt++;
         if (kTabs[sTabAt - 1] == 8 || sTabAt == (int)(sizeof kTabs / sizeof kTabs[0]))
         {
            sPhase = Phase::Panels;
            gView3DOpen = false;
            sPanel = 0;
            sWait = 20;
            return;
         }
      }
      sWait = 1;
      return;
   }

   if (sPhase == Phase::Panels)
   {
      const PanelFlag panels[] = { { "Settings", &gSettingsOpen }, { "Help", &gHelpOpen }, { "Shortcuts", &gShortcutsOpen },
                                   { "Templates", &gTemplatesOpen }, { "History", &gHistoryOpen }, { "Piano Roll", &gPianoRollOpen },
                                   { "Curve Editor", &gCurveEditorOpen },
                                   { "Sample Studio", &gSampleStudioOpen }, { "Show Set", &gShowSetOpen },
                                   { "Node Panel", &gNodePanelOpen }, { "Viewport", &gViewportPanelOpen } };
      const size_t n = sizeof panels / sizeof panels[0];
      if (sWait > 0) { sWait--; return; }
      if (sPanel > 0 && sPanel <= n)
      {
         const std::string state = tn + " " + panels[sPanel - 1].name;
         Snapshot(state);
         QueuePokes(state, "", 12);
         *panels[sPanel - 1].flag = false;
         CloseAllPopups();
         sWait = 4;
      }
      if (sPanel >= n)
      {
         sPhase = Phase::ClearNodes;
         return;
      }
      *panels[sPanel].flag = true;
      sPanel++;
      sWait = 25;
      return;
   }

   if (sPhase == Phase::ClearNodes)
   {
      std::vector<int> ids;
      for (GraphNode& gn : gNodes)
         ids.push_back(gn.index);
      for (int id : ids)
         RemoveNodeByIndex(id);
      sType = 0;
      sNodeStep = 0;
      sWait = 5;
      sPhase = Phase::Nodes;
      return;
   }

   if (sPhase == Phase::Nodes)
   {
      if (sWait > 0) { sWait--; return; }
      if (sType >= sTypes.size() && sNodeStep == 0)
      {
         sPhase = Phase::NextTheme;
         return;
      }
      const auto& [cat, type] = sTypes[sType];
      switch (sNodeStep)
      {
      case 0:
         SpawnNode(type, cat, 300.0f, 200.0f);
         sNodeStep = 1;
         sWait = 4;
         return;
      case 1:
         {
            ed::EditorContext* prev = ed::GetCurrentEditor();
            ed::SetCurrentEditor(gEditor);
            ed::NavigateToContent(0.0f);
            ed::SetCurrentEditor(prev);
         }
         sNodeStep = 2;
         sWait = 8;
         return;
      case 2:
         Snapshot(tn + " node " + type, type);
         QueuePokes(tn + " node " + type, type, 40);
         sNodeStep = 3;
         return;
      default:
         // Pokes are done (StepPoke returned false): the node goes, the next one comes.
         {
            std::vector<int> ids;
            for (GraphNode& gn : gNodes)
               ids.push_back(gn.index);
            for (int id : ids)
               RemoveNodeByIndex(id);
         }
         CloseAllPopups();
         sType++;
         sNodeStep = 0;
         sWait = 2;
         return;
      }
   }

   if (sPhase == Phase::View2D)
   {
      static int sState = -1, sPage2 = 0;
      static std::string sName;
      if (sWait > 0)
      {
         if (--sWait == 0)
         {
            for (const Notices::Notice& n : Notices::Active())
               Notices::DismissId(n.id);
            CloseAllPopups();
            if (sPage2 == 0)
            {
               sState++;
               sName = app::View2DAuditState(sState);
               if (sName.empty())
               {
                  if (sTheme == 0 && getenv("INFINITE_UIAUDIT_DARK_ONLY") == nullptr)
                  {
                     sTheme = 1;
                     SetTheme(1);
                     sState = -1;
                     sV2DPoked.clear();   // the light theme gets the layout checks, not a second round of pokes
                     sWait = 20;
                     return;
                  }
                  const char* out = getenv("INFINITE_UIAUDIT_OUT");
                  Write(out != nullptr ? out : ".");
                  WriteRegistry(out != nullptr ? out : ".");
                  printf("UIAUDIT done findings=%zu checked=%d poked=%d\n", sFindings.size(), sChecked, sPoked);
                  sDone = true;
                  GImGui->TestEngineHookItems = false;
                  glfwSetWindowShouldClose(window, GLFW_TRUE);
                  return;
               }
            }
            else
               app::View2DAuditRestore();
            for (const char* nm : { "##v2dprops", "##v2dlayers" })
               for (ImGuiWindow* w : GImGui->Windows)
                  if ((w->Active || w->WasActive) && BaseName(w->Name).find(nm) != std::string::npos)
                     ImGui::SetScrollY(w, w->InnerRect.GetHeight() * 0.8f * (float)sPage2);
            sWait = -4;
         }
         return;
      }
      if (sWait < 0 && ++sWait < 0)
      {
         // Golden shots: the panel's pixels for each state's first page, written as this frame ends.
         static const char* shots = getenv("INFINITE_UIAUDIT_V2D_SHOTS");
         if (sWait == -1 && sPage2 == 0 && shots != nullptr)
         {
            std::string file = tn + "_" + sName;
            for (char& c : file)
               if (!std::isalnum((unsigned char)c))
                  c = '_';
            const ImVec2 s = ImGui::GetIO().DisplayFramebufferScale;
            gShotNowPath = std::string(shots) + "/" + file + ".png";
            gShotNowRect[0] = (int)std::lround(gView2DRectMin.x * s.x);
            gShotNowRect[1] = (int)std::lround(gView2DRectMin.y * s.y);
            gShotNowRect[2] = (int)std::lround(gView2DRectMax.x * s.x);
            gShotNowRect[3] = (int)std::lround(gView2DRectMax.y * s.y);
         }
         return;
      }
      const std::string state = tn + " " + sName + (sPage2 > 0 ? " p" + std::to_string(sPage2 + 1) : "");
      Snapshot(state);
      V2DLayout(state);
      if (sTheme == 0)
         V2DRegisterAndPoke(state);
      float maxY = 0, y = 0;
      for (ImGuiWindow* w : GImGui->Windows)
         if ((w->Active || w->WasActive) && BaseName(w->Name).find("##v2dprops") != std::string::npos)   // Active is cleared until the panel draws again
         {
            maxY = std::max(maxY, w->ScrollMax.y);
            y = w->Scroll.y;
         }
      if (y < maxY - 1.0f && sPage2 < 8)
         sPage2++;
      else
         sPage2 = 0;
      sWait = 2;
      return;
   }

   if (sPhase == Phase::NextTheme)
   {
      if (sTheme == 0 && getenv("INFINITE_UIAUDIT_DARK_ONLY") == nullptr)
      {
         sTheme = 1;
         SetTheme(1);
         const char* patch = getenv("INFINITE_UIAUDIT_PATCH");
         if (patch != nullptr)
            LoadPatchFrom(patch);
         gView3DOpen = true;
         sTabAt = 0;
         sPage = 0;
         sWait = 30;
         sPhase = Phase::Tabs;
         return;
      }
      const char* out = getenv("INFINITE_UIAUDIT_OUT");
      Write(out != nullptr ? out : ".");
      printf("UIAUDIT done findings=%zu checked=%d poked=%d\n", sFindings.size(), sChecked, sPoked);
      sDone = true;
      GImGui->TestEngineHookItems = false;
      glfwSetWindowShouldClose(window, GLFW_TRUE);
   }
}
}

#else
namespace app
{
void FrameTest_UIAUDIT(int, GLFWwindow*) {}
}
#endif
