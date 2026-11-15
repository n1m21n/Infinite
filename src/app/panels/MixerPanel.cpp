// Mixer panel: the console view of the patch's Mixer nodes (internal/plans/mixer-panel).
// A3 is the shell: docks on four sides or floats, resizes from its seam, persists, and owns
// the "Enable Mixer Audio" chip that hands the device to the console (AudioMode::Mixer).
#include "app/ui/design/components/MenuParts.h"
#include "app/ui/design/components/EmptyState.h"
#include "app/ui/design/components/ChipButton.h"
#include "app/ui/design/components/PanelFrame.h"
#include "app/ui/design/UiType.h"
#include "app/AppShared.h"
#include "app/ui/design/TokenColors.h"
#include "audio/AudioEngine.h"
#include "nodes/AudioNodes.h"

namespace app
{
   bool  gMixerPanelOpen = false;
   int   gMixerPanelDock = 0;   // 0 bottom, 1 right, 2 left, 3 top, kDockFloating
   float gMixerPanelWidth = 520.0f;
   float gMixerPanelHeight = 300.0f;
   bool  gMixerClaimedKeys = false;
   ImVec2 gMixerPanelRectMin, gMixerPanelRectMax;

   // Mixer Audio: only the console reaches the device, the way Session audio takes it from the canvas.
   void SetMixerAudio(bool on)
   {
      if (on == (gAudioMode == AudioMode::Mixer))
         return;
      gAudioMode = on ? AudioMode::Mixer : AudioMode::Canvas;
      if (on && AudioEngine::Instance().SampleRate() <= 0.0)
      {
         gAudioStartError.clear();
         if (!StartAudioEngine(gAudioStartError))
            fprintf(stderr, "audio device: %s\n", gAudioStartError.c_str());
      }
      RebuildAudioTopology();
   }

   namespace
   {
      int CountMixers()
      {
         int n = 0;
         for (const auto& gn : gNodes)
            if (dynamic_cast<MixerNode*>(gn.node.get()) != nullptr)
               n++;
         return n;
      }

      void DrawMixerPanelContent()
      {
         const ImVec2 origin = ImGui::GetCursorScreenPos();
         const ImVec2 avail = ImGui::GetContentRegionAvail();

         // Header: the title left, Mixer Audio pinned right (the twin of Session's and the Timeline's chip).
         ImGui::AlignTextToFramePadding();
         {
            UiType::Scope hs(UiType::Size::Title, UiType::Weight::Semibold);
            ImGui::TextUnformatted(T("Mixer"));
         }
         {
            const bool mixerMode = gAudioMode == AudioMode::Mixer;
            const char* audioLabel = mixerMode ? T("Mixer Audio On") : T("Enable Mixer Audio");
            const float audioW = ImGui::CalcTextSize(audioLabel).x + 2.0f * tok::space_2;
            ImGui::SameLine();
            ImGui::SetCursorScreenPos(ImVec2(std::max(ImGui::GetCursorScreenPos().x, origin.x + avail.x - audioW), origin.y));
            if (ChipButton::Draw(audioLabel, mixerMode, tok::tile))
               SetMixerAudio(!mixerMode);
            if (ImGui::IsItemHovered())
               HelpTip("%s", mixerMode ? T("Only the console reaches the speakers. Click to hand audio back to the canvas.")
                                       : T("Hear only the console: other Audio Outs go silent."));
         }

         const ImVec2 bodyMin(origin.x, ImGui::GetCursorScreenPos().y + tok::space_2);
         const ImVec2 bodyMax(origin.x + avail.x, origin.y + avail.y);
         if (CountMixers() == 0 && bodyMax.y > bodyMin.y)
            EmptyState::DrawTo(ImGui::GetWindowDrawList(), bodyMin, bodyMax, T("No Mixer yet"),
                               T("Add a Mixer node to see its strips here."));

         // Right-click anywhere on the panel to move or close it (rect test, as the Mod Matrix does).
         const ImVec2 mouse = ImGui::GetIO().MousePos;
         const bool overPanel = mouse.x >= origin.x && mouse.x < origin.x + avail.x &&
                                mouse.y >= origin.y && mouse.y < origin.y + avail.y;
         gMixerClaimedKeys = overPanel && !ImGui::GetIO().WantTextInput;
         if (overPanel && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
            ImGui::OpenPopup("##mixerpanelctx");
         if (MenuParts::BeginPopup("##mixerpanelctx"))
         {
            static const char* kDockLabels[] = { I18N_KEY("Bottom"), I18N_KEY("Right"), I18N_KEY("Left"), I18N_KEY("Top"), I18N_KEY("Floating Window") };
            for (int i = 0; i < 5; i++)
               if (MenuParts::Item(L(kDockLabels[i]), nullptr, i == gMixerPanelDock))
                  gMixerPanelDock = i;
            MenuParts::Separator();
            if (MenuParts::Item(L("Close panel")))
               gMixerPanelOpen = false;
            MenuParts::EndPopup();
         }
      }
   }

   void MixerPanelDockCombo()
   {
      static const char* kDockLabels[] = { I18N_KEY("Bottom"), I18N_KEY("Right"), I18N_KEY("Left"), I18N_KEY("Top"), I18N_KEY("Floating Window") };
      gMixerPanelDock = std::clamp(gMixerPanelDock, 0, kDockFloating);
      if (MenuParts::BeginCombo("##mixerpaneldock", T(kDockLabels[gMixerPanelDock])))
      {
         for (int i = 0; i < 5; i++)
            if (MenuParts::Choice(L(kDockLabels[i]), i == gMixerPanelDock))
               gMixerPanelDock = i;
         MenuParts::EndCombo();
      }
   }

   // The outer frame at every dock: the grip strip on the canvas side, then the card (as DrawModMatrixDocked).
   void DrawMixerPanelDocked(const char* id, const ImVec2& size)
   {
      const float kGrip = PanelFrame::kGap;
      const int dock = gMixerPanelDock;
      const bool vertical = (dock == 1 || dock == 2);
      const bool gripFirst = (dock == 0 || dock == 1);

      ImGui::BeginChild(id, size, false);
      const ImVec2 inner = ImGui::GetContentRegionAvail();

      auto grip = [&]()
      {
         ImGui::InvisibleButton("##mixerpanelgrip",
                                vertical ? ImVec2(kGrip, std::max(1.0f, inner.y))
                                         : ImVec2(std::max(1.0f, inner.x), kGrip));
         DrawPanelSeam(vertical, gripFirst);
         if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(vertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
         if (ImGui::IsItemActive())
         {
            const ImVec2 d = ImGui::GetIO().MouseDelta;
            switch (dock)
            {
               case 0: gMixerPanelHeight -= d.y; break;
               case 1: gMixerPanelWidth -= d.x; break;
               case 2: gMixerPanelWidth += d.x; break;
               default: gMixerPanelHeight += d.y; break;
            }
            gMixerPanelWidth = std::max(kMixerPanelMinWidth, gMixerPanelWidth);
            gMixerPanelHeight = std::max(kMixerPanelMinHeight, gMixerPanelHeight);
         }
      };

      if (gripFirst)
      {
         grip();
         if (vertical)
            ImGui::SameLine();
      }

      const ImVec2 gap = ImGui::GetStyle().ItemSpacing;
      const ImVec2 cardSize = vertical ? ImVec2(std::max(1.0f, inner.x - kGrip - gap.x), inner.y)
                                       : ImVec2(std::max(1.0f, inner.x), std::max(1.0f, inner.y - kGrip - gap.y));
      PanelFrame::Insets in;
      switch (dock)
      {
         case 0: in.t = 0.0f; break;
         case 1: in.l = 0.0f; break;
         case 2: in.r = 0.0f; break;
         default: in.b = 0.0f; break;
      }
      PushDockedPanelStyle(/*isChild=*/true);
      PanelFrame::BeginCard("##mixerpanelcontent", cardSize, in);
      PopDockedPanelStyle();
      gMixerPanelRectMin = ImGui::GetWindowPos();
      gMixerPanelRectMax = ImVec2(gMixerPanelRectMin.x + ImGui::GetWindowWidth(), gMixerPanelRectMin.y + ImGui::GetWindowHeight());
      DrawMixerPanelContent();
      PanelFrame::EndCard();

      if (!gripFirst)
      {
         if (vertical)
            ImGui::SameLine();
         grip();
      }
      // Zeroed only around EndChild so no ItemSpacing strip shows between this panel and the next row.
      const ImVec2 savedSpacing = ImGui::GetStyle().ItemSpacing;
      ImGui::GetStyle().ItemSpacing = ImVec2(0.0f, 0.0f);
      ImGui::EndChild();
      ImGui::GetStyle().ItemSpacing = savedSpacing;
   }

   void DrawMixerPanelFloating()
   {
      bool open = true;
      if (BeginFloatingPanel((std::string(T("Mixer")) + "###mixerpanelfloat").c_str(), &open, ImVec2(760.0f, 360.0f)))
      {
         gMixerPanelRectMin = ImGui::GetWindowPos();
         gMixerPanelRectMax = ImVec2(gMixerPanelRectMin.x + ImGui::GetWindowWidth(), gMixerPanelRectMin.y + ImGui::GetWindowHeight());
         DrawMixerPanelContent();
      }
      EndFloatingPanel();
      if (!open)
         gMixerPanelOpen = false;
   }
}
