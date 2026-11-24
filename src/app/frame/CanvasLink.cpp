// Canvas Link: one pick state, one canvas hook, one set of words and glyphs (see CanvasLink.h).
#include "app/frame/CanvasLink.h"
#include "app/ui/design/Keymap.h"
#include "app/AppShared.h"
#include "app/selftest/UiAudit.h"
#include "app/ui/design/Glyphs.gen.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/components/ActionButton.h"
#include "app/ui/design/components/FormParts.h"
#include "app/ui/design/components/MenuParts.h"
#include "app/ui/design/components/ModeBanner.h"
#include "app/ui/design/components/PanelKit.h"
#include "app/ui/design/components/StateRing.h"
#include <imgui_node_editor.h>
#include <map>
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
      bool sBannerHovered = false; // last frame: the pointer is over the mode strip, so canvas clicks under it don't pick
      const std::string kNone;
      struct TestRect { uint64_t uid; ImVec2 mn, mx; };
      std::vector<TestRect> sTestRects; // screen rects of the last armed frame, for the self-test
      struct TestPin { int node, param; ImVec2 at; };
      std::vector<TestPin> sTestPins;

      std::map<std::string, std::string> sSynced; // site -> the owner key it armed

      // The pointer over the canvas you can see: inside the graph rect and not over a view docked on top of it.
      bool PointerOnCanvas()
      {
         const ImVec2 m = ed::CanvasToScreen(ImGui::GetMousePos());
         if (m.x < gGraphScreenTL.x || m.y < gGraphScreenTL.y || m.x > gGraphScreenTL.x + gGraphScreenSize.x ||
             m.y > gGraphScreenTL.y + gGraphScreenSize.y)
            return false;
         struct Over { bool open; ImVec2 mn, mx; };
         const Over overs[] = { { gPianoRollOpen, gPianoRollRectMin, gPianoRollRectMax }, { gView2DOpen, gView2DRectMin, gView2DRectMax },
                                { gView3DOpen, gView3DRectMin, gView3DRectMax } };
         for (const Over& o : overs)
            if (o.open && m.x >= o.mn.x && m.x <= o.mx.x && m.y >= o.mn.y && m.y <= o.mx.y)
               return false;
         return true;
      }

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

   int HoveredParam(const std::function<bool(int, int)>& accepts)
   {
      if (!PointerOnCanvas())
         return -1;
      const ImVec2 mp = ImGui::GetMousePos();
      int hit = -1;
      for (size_t i = 0; i < gParamPinScreenList.size(); i++)
      {
         const ParamPinScreenInfo& p = gParamPinScreenList[i];
         if (accepts && !accepts(p.nodeIndex, p.paramIndex))
            continue;
         const bool in = mp.x >= p.rowMin.x && mp.x <= p.rowMax.x && mp.y >= p.rowMin.y && mp.y <= p.rowMax.y;
         const float dx = mp.x - p.screenPos.x, dy = mp.y - p.screenPos.y;
         if (in || dx * dx + dy * dy < tok::canvas_link_hovered_param_ratio * 20.0f)
            hit = (int)i; // the last drawn wins, as before
      }
      return hit;
   }

   void RingParam(int i)
   {
      if (i < 0 || i >= (int)gParamPinScreenList.size())
         return;
      const ParamPinScreenInfo& p = gParamPinScreenList[i];
      ImDrawList* dl = ImGui::GetWindowDrawList();
      if (p.isCircle)
         StateRing::DrawCircle(dl, p.shapeCenter, p.shapeRadius, StateRing::Kind::Target, IsThemeLight());
      else
         StateRing::Draw(dl, p.rowMin, p.rowMax, StateRing::Kind::Target, IsThemeLight(), tok::radius_control);
   }

   void Sync(const char* site, const std::string& key, const std::function<Spec()>& make)
   {
      std::string& was = sSynced[site];
      if (key.empty())
      {
         if (!was.empty() && Armed(was.c_str()))
            Cancel();
         was.clear();
         return;
      }
      if (Armed(key.c_str()))
         return;
      if (!was.empty() && Armed(was.c_str()))
         sArmed.onCancel = nullptr; // re-keyed: the panel already holds the new key
      Spec spec = make();
      spec.owner = key;
      Arm(std::move(spec));
      was = key;
   }

   void CanvasHook()
   {
      if (!sIsArmed)
      {
         sBannerHovered = false;
         return;
      }
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 mp = ImGui::GetMousePos();
      const ImU32 dim = ImGui::GetColorU32(ImGuiCol_WindowBg, tok::canvas_link_hook_window_bg_alpha_2);
      const bool params = sArmed.target == Target::Param;
      const bool onCanvas = PointerOnCanvas();
      GraphNode* hovered = nullptr;
      int hoveredPin = params && !sBannerHovered ? HoveredParam(sArmed.acceptsParam) : -1;
      bool cancelNow = false;
      ImVec2 hp(0, 0), hs(0, 0);
      sTestRects.clear();
      sTestPins.clear();
      if (params)
         for (const ParamPinScreenInfo& pin : gParamPinScreenList)
            if (!sArmed.acceptsParam || sArmed.acceptsParam(pin.nodeIndex, pin.paramIndex))
               sTestPins.push_back({ pin.nodeIndex, pin.paramIndex,
                                     ed::CanvasToScreen(ImVec2((pin.rowMin.x + pin.rowMax.x) * 0.5f, (pin.rowMin.y + pin.rowMax.y) * 0.5f)) });
      for (GraphNode& gn : gNodes)
      {
         if (gn.hiddenFromCanvas)
            continue;
         const ImVec2 p = ed::GetNodePosition(gn.NodeId());
         const ImVec2 s = ed::GetNodeSize(gn.NodeId());
         if (s.x <= 0.0f)
            continue;
         sTestRects.push_back({ gn.uid, ed::CanvasToScreen(p), ed::CanvasToScreen(ImVec2(p.x + s.x, p.y + s.y)) });
         bool ok = true;
         if (params)
         {
            ok = false; // a node is lit when it has a control the pick takes
            for (const ParamPinScreenInfo& pin : gParamPinScreenList)
               if (pin.nodeIndex == gn.index && (!sArmed.acceptsParam || sArmed.acceptsParam(pin.nodeIndex, pin.paramIndex)))
               {
                  ok = true;
                  break;
               }
         }
         else
            ok = !sArmed.accepts || sArmed.accepts(gn);
         if (!ok)
         {
            dl->AddRectFilled(p, ImVec2(p.x + s.x, p.y + s.y), dim, tok::radius_container);
            continue;
         }
         if (!params && onCanvas && hovered == nullptr && !sBannerHovered && mp.x >= p.x && mp.x <= p.x + s.x && mp.y >= p.y && mp.y <= p.y + s.y)
         {
            hovered = &gn;
            hp = p;
            hs = s;
         }
      }

      ed::Suspend();
      // The prompt: the shared mode strip, top centre of the canvas you can see (under any docked view over it).
      {
         const float cx = gGraphScreenTL.x + gGraphScreenSize.x * 0.5f;
         float top = gGraphScreenTL.y;
         struct Over { bool open; ImVec2 mn, mx; };
         const Over overs[] = { { gPianoRollOpen, gPianoRollRectMin, gPianoRollRectMax }, { gView2DOpen, gView2DRectMin, gView2DRectMax },
                                { gView3DOpen, gView3DRectMin, gView3DRectMax } };
         for (const Over& o : overs)
            if (o.open && o.mx.y > o.mn.y && o.mn.y <= top + 1.0f && o.mx.y > top && o.mn.x <= cx && o.mx.x >= cx)
               top = o.mx.y;
         // Names what is under the pointer once there is something to take, else what to look for.
         std::string what = sArmed.kind.empty() ? std::string(T("node")) : sArmed.kind;
         const char* head = T("Click a");
         if (hovered != nullptr)
            what = NodeTitleWithInstance(*hovered), head = T("Pick");
         else if (hoveredPin >= 0)
            what = gParamPinScreenList[hoveredPin].nodeTitle + ": " + gParamPinScreenList[hoveredPin].paramName, head = T("Pick");
         if (ModeBanner::Float("##canvaslink.banner", ImVec2(cx, top + tok::space_3), head, what.c_str(), &sBannerHovered))
            cancelNow = true;
      }
      if ((hovered != nullptr || hoveredPin >= 0) && !sBannerHovered)
         ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
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
      if (hoveredPin >= 0)
      {
         RingParam(hoveredPin);
         if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
         {
            std::function<void(int, int)> picked = std::move(sArmed.onPickedParam);
            const int n = gParamPinScreenList[hoveredPin].nodeIndex, p = gParamPinScreenList[hoveredPin].paramIndex;
            sArmed = Spec();
            sIsArmed = false;
            if (picked)
               picked(n, p);
            return;
         }
      }
      if (cancelNow || Keymap::Cancel(true) || MenuParts::RightPressed())
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
      const bool hit = PanelKit::IconChip(id, Glyph(Verb::Pick), armed, armed ? ArmedLabel(spec).c_str() : Label(Verb::Pick));
      UiAudit::NoteKind("toggle", Label(Verb::Pick)); // after the chip, so the note lands on the chip and not on the field before it
      if (!hit)
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
      UiAudit::NoteKind("toggle", Label(Verb::Show));
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
      ImGui::SetNextItemWidth(std::max(chip, full - tok::canvas_link_field_ratio * (chip + gap)));
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

   bool ParamScreenPosForTest(int nodeIndex, ImVec2& at, int& paramIndex)
   {
      for (const TestPin& p : sTestPins)
         if (p.node == nodeIndex)
         {
            at = p.at;
            paramIndex = p.param;
            return true;
         }
      return false;
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
