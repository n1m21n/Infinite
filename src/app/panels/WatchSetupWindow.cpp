// "Set up watch app": the checklist window opened from the Apple Watch node (docs/plans/apple-watch/README.md,
// D-B2 to D-B4). Each step is green when the machine says so; the scan runs in the background every few seconds
// while the window is open. The install runs in the Terminal panel (tab "Watch app") so password prompts work;
// the card and the checklist read its progress from status.json, not from the terminal text.
#include "app/AppShared.h"
#include "app/ui/design/components/ActionButton.h"
#include "app/ui/design/components/FormParts.h"
#include "app/ui/design/components/SectionCard.h"
#include "platform/AppPaths.h"
#include "platform/Platform.h"
#include "platform/WatchSetup.h"

namespace app
{
namespace
{
constexpr const char* kGuideUrl = "https://infinitedevices.org/watch.html";

bool gReinstallPending = false;   // "Reinstall" pressed: scan, then install as soon as the steps are green
double gLastScan = -100.0, gLastPoll = -100.0;

WatchSetup::Checklist& List()
{
   static WatchSetup::Checklist list(
      WatchSetup::MakeSystemToolchain(BundledResourcePath("WatchRemote"), [](const std::string& tab, const std::string& cmd) {
         return TerminalRunInNewTab(tab, cmd);
      }),
      AppPaths::AppSupportDir() + "/WatchRemote/status.json");
   return list;
}
}

// What the node card shows under the setup button: the install progress or last error, else the expiry.
WatchSetupCardInfo WatchSetupCard()
{
   WatchSetup::Checklist& l = List();
   WatchSetupCardInfo c;
   const WatchSetup::InstallStatus s = l.Install();
   c.running = s.phase == WatchSetup::InstallPhase::kRunning;
   if (c.running)
      c.line = (s.total > 0 ? "Step " + std::to_string(s.step) + "/" + std::to_string(s.total) + ": " : std::string()) +
               (s.progress.empty() ? "working" : s.progress);
   else if (s.phase == WatchSetup::InstallPhase::kFailed)
      c.line = "Install failed: " + s.error;
   else
      c.line = l.ExpiryText();
   c.reinstallDue = l.ReinstallDue();
   return c;
}

// Called from the node card and the window: starts a pending reinstall once the scan says it can.
void WatchSetupTick()
{
   WatchSetup::Checklist& l = List();
   if (ImGui::GetTime() - gLastPoll > 0.5)
   {
      l.PollStatus();
      gLastPoll = ImGui::GetTime();
   }
   if (gReinstallPending && !l.Scanning() && l.Install().phase != WatchSetup::InstallPhase::kRunning && ImGui::GetTime() - gLastScan < 60.0)
   {
      if (l.AllReady())
         l.StartInstall();
      gReinstallPending = false;
   }
}

void WatchSetupRefreshSoon()
{
   WatchSetup::Checklist& l = List();
   if (l.Install().phase == WatchSetup::InstallPhase::kRunning)
      return;
   l.RefreshAsync();
   gLastScan = ImGui::GetTime();
}

void WatchSetupReinstall()
{
   gReinstallPending = true;
   WatchSetupRefreshSoon();
}

void DrawWatchSetupWindow(bool* open)
{
   WatchSetup::Checklist& list = List();
   if (ImGui::GetTime() - gLastScan > 3.0)
      WatchSetupRefreshSoon();
   WatchSetupTick();

   ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
   ImGui::SetNextWindowSize(ImVec2(520, 400), ImGuiCond_FirstUseEver);
   PushElevatedPanelStyle(/*isChild=*/false);
   FormParts::PushWindowPad();
   const bool visible = ImGui::Begin(L("Set up watch app"), open, ImGuiWindowFlags_NoCollapse);
   FormParts::PopWindowPad();
   if (!visible)
   {
      ImGui::End();
      PopElevatedPanelStyle();
      return;
   }
   SectionCard::BeginWindow();
   ImGui::TextDisabled("%s", T("Builds the Infinite Remote app with your own Apple ID and installs it on your watch."));
   ImGui::Spacing();

   static const char* const kTitle[] = { "Xcode", "Apple ID in Xcode", "iPhone and watch", "Install on my watch" };
   const std::vector<WatchSetup::StepInfo> steps = list.Steps();
   SectionCard::Begin(T("Steps"));
   for (size_t i = 0; i < steps.size(); i++)
   {
      const WatchSetup::StepInfo& s = steps[i];
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const float r = 6.0f;
      const ImU32 col = s.state == WatchSetup::StepState::kOk ? IM_COL32(76, 199, 115, 255)
                        : s.state == WatchSetup::StepState::kMissing ? IM_COL32(230, 160, 60, 255)
                                                                     : IM_COL32(130, 130, 140, 255);
      ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + r, p.y + ImGui::GetTextLineHeight() * 0.5f), r, col);
      ImGui::SetCursorScreenPos(ImVec2(p.x + r * 2 + 8.0f, p.y));
      ImGui::BeginGroup();
      ImGui::Text("%d  %s", (int)i + 1, T(kTitle[i]));
      ImGui::TextWrapped("%s", T(s.text.c_str()));
      if (s.canOpen && ActionButton::Draw((std::string(T("Open")) + "##step" + std::to_string(i)).c_str()))
         list.OpenStep(s.id);
      ImGui::EndGroup();
      ImGui::Spacing();
   }

   const WatchSetup::InstallStatus inst = list.Install();
   const bool running = inst.phase == WatchSetup::InstallPhase::kRunning;
   ImGui::BeginDisabled(!list.AllReady() || running);
   if (ActionButton::Draw(running ? "Working...##install" : "Install on my watch##install", ImVec2(180, 0), ActionButton::Kind::Primary))
      list.StartInstall();
   ImGui::EndDisabled();
   if (!list.ExpiryText().empty())
      ImGui::TextDisabled("%s", T(list.ExpiryText().c_str()));
   ImGui::Spacing();
   if (ActionButton::Draw("Guide with screenshots##guide"))
      Platform::OpenExternalUrl(kGuideUrl);

   SectionCard::End();
   SectionCard::EndWindow();
   ImGui::End();
   PopElevatedPanelStyle();
}
}
