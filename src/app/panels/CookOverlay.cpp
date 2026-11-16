// Cook times overlay (View > Cook times). Each node gets its CPU cook time in ms; audio nodes show their share of the
// audio callback instead. Only the number is coloured, never the node: amber or red when the node costs a real share
// of its budget (see Level). Off by default and saved in settings, not in patches.
// While off nothing here runs and CookProbe / the audio thread do no timing.
#include "app/AppShared.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/Tokens.gen.h"
#include <unordered_map>
#include "audio/AudioEngine.h"
#include "audio/AudioNode.h"
#include "imgui_internal.h"

namespace app
{
namespace
{
   // A node's colour is its share of its own budget, never a comparison with other nodes (that painted the slowest node
   // of a cheap patch red at 0.09 ms). Video: CPU cook ms against a 60 fps frame. Audio: share of the audio callback,
   // stricter because an overrun is an audible click while a slow frame is only a dropped frame.
   //   video  amber >= 2 ms (12%)   red >= 5 ms (30%): three such nodes and the canvas drops frames
   //   audio  amber >= 10%          red >= 25%: four such nodes and the callback overruns
   // Both inputs are already smoothed; a level drops only below 80% of its threshold, so a value sitting on a line does
   // not flicker.
   constexpr float kFrameMs = 1000.0f / 60.0f;
   constexpr float kVideoAmber = 0.12f, kVideoRed = 0.30f, kAudioAmber = 0.10f, kAudioRed = 0.25f, kHold = 0.8f;

   int Level(float share, bool audio, int prev)
   {
      const float amber = audio ? kAudioAmber : kVideoAmber, red = audio ? kAudioRed : kVideoRed;
      if (share >= red || (prev == 2 && share >= red * kHold)) return 2;
      if (share >= amber || (prev >= 1 && share >= amber * kHold)) return 1;
      return 0;
   }
   bool sApplied = false;

   AudioNode* AudioOf(INode* n)
   {
      if (auto* s = dynamic_cast<IAudioSource*>(n)) return s->GetAudioNode();
      if (auto* s = dynamic_cast<INoteSource*>(n)) return s->GetAudioNode();
      return nullptr;
   }

   // True when a floating window or popup (Settings, a dropdown...) sits above the canvas and contains `pt`. The overlay is
   // drawn on the foreground list, so without this its pills and tints paint over those windows. Same test as the cable trash icon.
   bool CoveredByWindow(const ImVec2& pt)
   {
      ImGuiContext& g = *ImGui::GetCurrentContext();
      const ImGuiWindow* canvasRoot = ImGui::GetCurrentWindow()->RootWindow;
      bool above = false;
      for (ImGuiWindow* w : g.Windows)
      {
         if (w->RootWindow == canvasRoot)
         {
            above = true;
            continue;
         }
         if (above && w->WasActive && !w->Hidden && (w->Flags & (ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_Tooltip)) == 0 &&
             w->Rect().Contains(pt))
            return true;
      }
      return false;
   }
}

void DrawCookOverlay()
{
   static const bool sEnv = std::getenv("INFINITE_COOKTIMES") != nullptr;   // review hook
   const bool on = sEnv || CategoryColors::GetCookTimes();
   if (on != sApplied)
   {
      sApplied = on;
      CookProbe::gOn.store(on, std::memory_order_relaxed);
      AudioEngine::Instance().SetNodeCostMeasure(on);
      if (!on) CookProbe::Clear();
   }
   if (!on || gNodes.empty())
      return;

   struct Row { const GraphNode* gn; float v; bool audio; };
   static std::vector<Row> rows;
   rows.clear();
   const float budget = std::max(0.001f, AudioEngine::Instance().BlockBudgetMs());
   for (const GraphNode& gn : gNodes)
   {
      if (gn.node == nullptr || gn.node->bypassed || dynamic_cast<GroupNode*>(gn.node.get()) != nullptr) continue;   // a group cooks nothing; its box is the size of its members
      if (AudioNode* an = AudioOf(gn.node.get()))
      {
         const float share = an->costMs.load(std::memory_order_relaxed) / budget;
         rows.push_back({ &gn, share, true });
      }
      else
      {
         const float ms = CookProbe::Ms(gn.node.get());
         if (ms < 0.0f) continue;
         rows.push_back({ &gn, ms, false });
      }
   }

   ImDrawList* dl = ImGui::GetForegroundDrawList();
   const ImVec2 br(gGraphScreenTL.x + gGraphScreenSize.x, gGraphScreenTL.y + gGraphScreenSize.y);
   dl->PushClipRect(gGraphScreenTL, br, true);
   // ed::GetCurrentZoom() is the node editor's InvScale (1 / zoom), so it must never scale a size: corners go through
   // CanvasToScreen, and the on-screen scale is its inverse.
   const float scale = 1.0f / std::max(0.001f, ed::GetCurrentZoom());
   const CategoryColors::UiTheme& t = CategoryColors::CurrentUiTheme();
   const ImU32 pillBg = ImGui::GetColorU32(ImVec4(t.panelBg.r, t.panelBg.g, t.panelBg.b, 0.92f));
   const ImU32 pillText = ImGui::GetColorU32(ImVec4(t.text.r, t.text.g, t.text.b, 1.0f));
   const bool light = CategoryColors::IsThemeLight();
   // A fixed size: the current font size here still carries the canvas zoom, so the number grew and shrank with it.
   ImFont* font = ImGui::GetFont();
   constexpr float kPillText = tok::type_caption;
   const ImU32 amber = tok::U32(tok::action_learn, light), red = tok::U32(tok::action_record, light);   // same roles as the node issue badge
   static std::unordered_map<const INode*, int> sLevel;   // last level per node, for the hold; rebuilt each frame
   std::unordered_map<const INode*, int> nextLevel;
   for (const Row& r : rows)
   {
      const ImVec2 cp = ed::GetNodePosition(r.gn->NodeId());
      const ImVec2 cs = ed::GetNodeSize(r.gn->NodeId());
      const ImVec2 p = ed::CanvasToScreen(cp);
      const ImVec2 p2 = ed::CanvasToScreen(ImVec2(cp.x + cs.x, cp.y + cs.y));
      if (p2.x < gGraphScreenTL.x || p.x > br.x || p2.y < gGraphScreenTL.y || p.y > br.y) continue;
      if (CoveredByWindow(ImVec2((p.x + p2.x) * 0.5f, (p.y + p2.y) * 0.5f))) continue;   // a window is on top of this node
      const INode* key = r.gn->node.get();
      const auto it = sLevel.find(key);
      const int level = Level(r.audio ? r.v : r.v / kFrameMs, r.audio, it == sLevel.end() ? 0 : it->second);
      nextLevel[key] = level;
      if (scale < 0.35f && level == 0) continue;   // zoomed far out only the amber and red numbers stay, so a slow node still shows
      char buf[24];
      if (r.audio) std::snprintf(buf, sizeof buf, "%.1f%% cb", r.v * 100.0f);
      else std::snprintf(buf, sizeof buf, r.v < 10.0f ? "%.2f ms" : "%.0f ms", r.v);
      const ImVec2 ts = font->CalcTextSizeA(kPillText, FLT_MAX, 0.0f, buf);
      const ImVec2 q0(p2.x - ts.x - 10.0f, p.y - ts.y - 8.0f), q1(p2.x, p.y - 2.0f);
      if (CoveredByWindow(ImVec2((q0.x + q1.x) * 0.5f, (q0.y + q1.y) * 0.5f))) continue;   // pill sits above the node, may be under a window the node is not
      dl->AddRectFilled(q0, q1, pillBg, tok::radius_control);
      dl->AddText(font, kPillText, ImVec2(q0.x + 5.0f, q0.y + 3.0f), level == 0 ? pillText : (level == 2 ? red : amber), buf);
   }
   dl->PopClipRect();
   for (const Row& r : rows)   // off-screen nodes keep their level
      nextLevel.emplace(r.gn->node.get(), sLevel.count(r.gn->node.get()) ? sLevel[r.gn->node.get()] : 0);
   sLevel.swap(nextLevel);
}
}
