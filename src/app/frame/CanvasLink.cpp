// Canvas Link: one pick state, one canvas hook, one set of words and glyphs (see CanvasLink.h).
#include "app/frame/CanvasLink.h"
#include "app/AppShared.h"
#include "app/selftest/UiAudit.h"
#include "app/ui/design/Glyphs.gen.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/components/ActionButton.h"
#include "app/ui/design/components/FormParts.h"
#include "app/ui/design/components/MenuParts.h"
#include "app/ui/design/components/PanelKit.h"
#include "app/ui/design/components/StateRing.h"
#include <imgui_node_editor.h>
#include <vector>

namespace ed = ax::NodeEditor;

namespace app
{
namespace CanvasLink
{
   namespace
   {
      Spec sArmed;
      bool sIsArmed = false;
      const std::string kNone;
      struct TestRect { uint64_t uid; ImVec2 mn, mx; };
      std::vector<TestRect> sTestRects; // screen rects of the last armed frame, for the self-test

      std::string ArmedLabel(const Spec& s)
      {
         return std::string(T("Click a")) + " " + (s.kind.empty() ? std::string(T("node")) : s.kind) + "...";
      }
   }

   const char* Label(Verb v)
   {
      switch (v)
      {
      case Verb::Pick: return T("Pick on Canvas");
      case Verb::Show: return T("Show on Canvas");
      case Verb::Add: return T("Add to Canvas");
      case Verb::Unlink: return T("Unlink");
      case Verb::Hide: return T("Hide from Canvas");
      }
      return "";
   }

   const char* Glyph(Verb v)
   {
      switch (v)
      {
      case Verb::Pick: return IconsInfinite::Pointer;
      case Verb::Show: return IconsInfinite::Zoom;
      case Verb::Add: return IconsInfinite::Plus;
      case Verb::Unlink: return IconsInfinite::Close;
      case Verb::Hide: return IconsInfinite::EyeOff;
      }
      return IconsInfinite::Placeholder;
   }

   void Cancel()
   {
      if (!sIsArmed)
         return;
      std::function<void()> cancel = std::move(sArmed.onCancel);
      sArmed = Spec();
      sIsArmed = false;
      if (cancel)
         cancel();
   }

   void Arm(Spec spec)
   {
      Cancel();
      sArmed = std::move(spec);
      sIsArmed = true;
   }

   bool Armed(const char* owner)
   {
      return sIsArmed && (owner == nullptr || sArmed.owner == owner);
   }

   const std::string& ArmedOwner()
   {
      return sIsArmed ? sArmed.owner : kNone;
   }

   void CanvasHook()
   {
      if (!sIsArmed)
         return;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 mp = ImGui::GetMousePos();
      const ImU32 dim = ImGui::GetColorU32(ImGuiCol_WindowBg, 0.62f);
      GraphNode* hovered = nullptr;
      ImVec2 hp(0, 0), hs(0, 0);
      sTestRects.clear();
      for (GraphNode& gn : gNodes)
      {
         if (gn.hiddenFromCanvas)
            continue;
         const ImVec2 p = ed::GetNodePosition(gn.NodeId());
         const ImVec2 s = ed::GetNodeSize(gn.NodeId());
         if (s.x <= 0.0f)
            continue;
         sTestRects.push_back({ gn.uid, ed::CanvasToScreen(p), ed::CanvasToScreen(ImVec2(p.x + s.x, p.y + s.y)) });
         const bool ok = !sArmed.accepts || sArmed.accepts(gn);
         if (!ok)
         {
            dl->AddRectFilled(p, ImVec2(p.x + s.x, p.y + s.y), dim, tok::radius_container);
            continue;
         }
         if (hovered == nullptr && mp.x >= p.x && mp.x <= p.x + s.x && mp.y >= p.y && mp.y <= p.y + s.y)
         {
            hovered = &gn;
            hp = p;
            hs = s;
         }
      }

      ed::Suspend();
      // The prompt, top centre of the canvas: what to click and how to stop.
      {
         const std::string msg = ArmedLabel(sArmed) + "   " + T("Esc cancels");
         const ImVec2 ts = ImGui::CalcTextSize(msg.c_str());
         // Top centre of the canvas you can see: under any docked view lying over it.
         const float cx = gGraphScreenTL.x + gGraphScreenSize.x * 0.5f;
         float top = gGraphScreenTL.y;
         struct Over { bool open; ImVec2 mn, mx; };
         const Over overs[] = { { gPianoRollOpen, gPianoRollRectMin, gPianoRollRectMax }, { gView2DOpen, gView2DRectMin, gView2DRectMax },
                                { gView3DOpen, gView3DRectMin, gView3DRectMax } };
         for (const Over& o : overs)
            if (o.open && o.mx.y > o.mn.y && o.mn.y <= top + 1.0f && o.mx.y > top && o.mn.x <= cx && o.mx.x >= cx)
               top = o.mx.y;
         const ImVec2 a(cx - ts.x * 0.5f - tok::space_3, top + tok::space_3);
         const ImVec2 b(a.x + ts.x + 2.0f * tok::space_3, a.y + ts.y + 2.0f * tok::space_2);
         ImDrawList* sdl = ImGui::GetWindowDrawList();
         sdl->AddRectFilled(a, b, ImGui::GetColorU32(ImGuiCol_PopupBg), tok::radius_control);
         StateRing::Draw(sdl, a, b, StateRing::Kind::Target, IsThemeLight());
         sdl->AddText(ImVec2(a.x + tok::space_3, a.y + tok::space_2), ImGui::GetColorU32(ImGuiCol_Text), msg.c_str());
      }
      if (hovered != nullptr)
      {
         ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
         ImGui::SetTooltip("%s", NodeTitleWithInstance(*hovered).c_str());
      }
      ed::Resume();

      if (hovered != nullptr)
      {
         StateRing::Draw(dl, hp, ImVec2(hp.x + hs.x, hp.y + hs.y), StateRing::Kind::Target, IsThemeLight());
         if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
         {
            std::function<void(uint64_t)> picked = std::move(sArmed.onPicked);
            const uint64_t uid = hovered->uid;
            sArmed = Spec();
            sIsArmed = false;
            if (picked)
               picked(uid);
            return;
         }
      }
      if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
         Cancel();
   }

   bool PickButton(const Spec& spec)
   {
      const bool armed = Armed(spec.owner.c_str());
      const std::string label = (armed ? ArmedLabel(spec) : std::string(Label(Verb::Pick))) + "##cl." + spec.owner;
      UiAudit::NoteKind("toggle", Label(Verb::Pick));
      if (!ActionButton::Draw(label.c_str(), ImVec2(-1.0f, 0.0f), armed ? ActionButton::Kind::Selected : ActionButton::Kind::Plain,
                              Glyph(Verb::Pick)))
         return false;
      if (armed)
         Cancel();
      else
         Arm(spec);
      return true;
   }

   bool PickChip(const char* id, const Spec& spec)
   {
      const bool armed = Armed(spec.owner.c_str());
      UiAudit::NoteKind("toggle", Label(Verb::Pick));
      if (!PanelKit::IconChip(id, Glyph(Verb::Pick), armed, armed ? ArmedLabel(spec).c_str() : Label(Verb::Pick)))
         return false;
      if (armed)
         Cancel();
      else
         Arm(spec);
      return true;
   }

   bool PickMenuItem(const Spec& spec)
   {
      const bool armed = Armed(spec.owner.c_str());
      if (!MenuParts::Item(Label(Verb::Pick), nullptr, armed))
         return false;
      if (armed)
         Cancel();
      else
         Arm(spec);
      return true;
   }

   void Show(std::initializer_list<uint64_t> uids)
   {
      RevealSpawned(uids);
   }

   void Show(uint64_t uid)
   {
      RevealSpawned({ uid });
   }

   bool ShowChip(const char* id, uint64_t uid)
   {
      const bool live = uid != 0 && FindNodeByUid(uid) != nullptr;
      ImGui::BeginDisabled(!live);
      const bool clicked = PanelKit::IconChip(id, Glyph(Verb::Show), false, Label(Verb::Show));
      ImGui::EndDisabled();
      if (clicked && live)
         Show(uid);
      return clicked && live;
   }

   bool ShowMenuItem(uint64_t uid)
   {
      const bool live = uid != 0 && FindNodeByUid(uid) != nullptr;
      if (!MenuParts::Item(Label(Verb::Show), nullptr, false, live))
         return false;
      Show(uid);
      return true;
   }

   bool Field(const char* id, uint64_t uid, const Spec& spec, bool allowNone, float w)
   {
      ImGui::PushID(id);
      const float chip = ImGui::GetFrameHeight();
      const float gap = tok::space_1;
      const float full = w > 0.0f ? w : ImGui::GetContentRegionAvail().x;
      const GraphNode* cur = uid != 0 ? FindNodeByUid(uid) : nullptr;
      const std::string preview = cur != nullptr ? NodeTitleWithInstance(*cur) : uid != 0 ? std::string(T("Missing")) : std::string(T("None"));
      bool changed = false;
      uint64_t chosen = uid;
      ImGui::SetNextItemWidth(std::max(chip, full - 2.0f * (chip + gap)));
      if (MenuParts::BeginCombo("##cl.field", preview.c_str()))
      {
         if (allowNone && MenuParts::Choice(T("None"), uid == 0) && uid != 0)
            chosen = 0, changed = true;
         for (GraphNode& gn : gNodes)
         {
            if (spec.accepts && !spec.accepts(gn))
               continue;
            const std::string name = NodeTitleWithInstance(gn) + "##n" + std::to_string(gn.uid);
            if (MenuParts::Choice(name.c_str(), gn.uid == uid) && gn.uid != uid)
               chosen = gn.uid, changed = true;
         }
         MenuParts::EndCombo();
      }
      FormParts::Inline(0.0f, gap);
      PickChip("##cl.pick", spec);
      FormParts::Inline(0.0f, gap);
      ShowChip("##cl.show", uid);
      ImGui::PopID();
      if (changed && spec.onPicked)
         spec.onPicked(chosen);
      return changed;
   }

   bool ScreenRectForTest(uint64_t uid, ImVec2& mn, ImVec2& mx)
   {
      for (const TestRect& r : sTestRects)
         if (r.uid == uid)
         {
            mn = r.mn, mx = r.mx;
            return true;
         }
      return false;
   }

   GraphNode* Add(const char* type, const char* category)
   {
      const ImVec2 at = FindFreeSpawnPosition(gViewCenterCanvas); // beside what is on screen, never on top of it
      GraphNode* gn = SpawnNode(type, category, at.x, at.y);
      if (gn != nullptr)
         Show(gn->uid);
      return gn;
   }
}
}
