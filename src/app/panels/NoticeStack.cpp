// Draws Notices::Active() as small cards stacked up from the bottom-right of the canvas. One card: glyph (warning or
// error; info has none), what happened, what is safe and what to do next, an optional action, a close glyph.
#include "app/AppShared.h"
#include "app/ui/design/components/NoticeCard.h"
#include "app/ui/design/components/PanelFrame.h"
#include "core/Notices.h"

namespace app
{
// Device state is owned by the audio code; the notice follows it so the reason is stated once, in words.
static void FollowAudioDevice()
{
   static std::string sLast;
   if (gAudioStartError == sLast)
      return;
   sLast = gAudioStartError;
   if (sLast.empty())
      Notices::Dismiss("audio.device");
   else
      Notices::Post(Notices::Level::Error, "audio.device", "Audio isn't running",
                    sLast + ". Your patch is unaffected. Pick another output in Settings, then press the audio button.",
                    "Open audio settings", [] { gSettingsOpen = true; });
}

void DrawNotices()
{
   FollowAudioDevice();
   Notices::Tick(ImGui::GetTime());
   const std::vector<Notices::Notice> active = Notices::Active();   // copy: an action may post or dismiss
   if (active.empty())
      return;

   const ImGuiViewport* vp = ImGui::GetMainViewport();
   const float width = ImGui::GetFontSize() * 24.0f;
   float y = vp->WorkPos.y + vp->WorkSize.y - tok::space_4;
   const float x = vp->WorkPos.x + vp->WorkSize.x - width - tok::space_4;

   for (size_t i = active.size(); i-- > 0;)
   {
      const Notices::Notice& n = active[i];
      char id[48];
      snprintf(id, sizeof(id), "##notice%llu", (unsigned long long)n.id);

      ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
      ImGui::SetNextWindowSize(ImVec2(width, 0.0f));
      PanelFrame::PushFloatingStyle();
      ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav);
      switch (NoticeCard::Draw(n, width))
      {
      case NoticeCard::Result::Dismiss: Notices::DismissId(n.id); break;
      case NoticeCard::Result::Action: n.action(); Notices::DismissId(n.id); break;
      case NoticeCard::Result::None: break;
      }
      y -= ImGui::GetWindowHeight() + tok::space_2;
      ImGui::End();
      PanelFrame::PopFloatingStyle();
   }
}
}
