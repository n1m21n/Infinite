// Settings > "Infinite as VST3/AU" tab (docs/plans/vst-au/README.md 8.11, 3.6): install the plugin
// bundles the app carries into the user's plugin folders. Approved 2026-11-07 (12.4).
#include "app/AppShared.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/Tokens.gen.h"
#include "app/ui/design/components/FormParts.h"
#include "core/DawPluginInstall.h"
#include "core/License.h"
#include "platform/Platform.h"

namespace app
{
   namespace
   {
      std::string gDawPluginError;
      std::string gDawPluginDone;
   }

   DawPlugin::Env DawPluginEnv()
   {
      return DawPlugin::CurrentEnv(Platform::ExecutablePath(), INFINITE_VERSION_STRING);
   }

   // App launch: an installed plugin follows the app's version and location.
   void DawPluginLaunchRecopy()
   {
      std::string err;
      if (!DawPlugin::RecopyIfStale(DawPluginEnv(), err))
         gDawPluginError = err;
   }

   void DrawDawPluginSection()
   {
      const DawPlugin::Env env = DawPluginEnv();
      const DawPlugin::Status st = DawPlugin::Describe(env);

      FormParts::Section(T("Install"));
      FormParts::Label(L("Status"));
      ImGui::AlignTextToFramePadding();
      if (!st.bundled)
         ImGui::TextDisabled("%s", T("Not in this build"));
      else if (st.current)
         ImGui::Text(T("Installed (%s)"), st.installedVersion.c_str());
      else if (st.installed)
         ImGui::Text(T("Installed (%s), update ready"), st.installedVersion.c_str());
      else
         ImGui::TextUnformatted(T("Not installed"));

      // One row per format, named by the format; the path is selectable text, so no Copy button is needed.
      for (const std::string& f : st.folders)
      {
         const bool au = f.find("Components") != std::string::npos;
         ImGui::PushID(f.c_str());
         FormParts::Label(au ? "AU" : "VST3");
         ImGui::AlignTextToFramePadding();
         ImGui::TextDisabled("%s", f.c_str());
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", T("Click to copy"));
         if (ImGui::IsItemClicked())
            ImGui::SetClipboardText(f.c_str());
         ImGui::PopID();
      }

      if (st.bundled)
      {
         FormParts::Label("");
         if (FormParts::Button(st.installed ? L("Reinstall") : L("Install")))
         {
            gDawPluginError.clear();
            gDawPluginDone.clear();
            std::string err;
            if (DawPlugin::Install(env, err))
               gDawPluginDone = T("Installed. Rescan plugins in your DAW.");
            else
               gDawPluginError = err;
         }
         if (st.installed)
         {
            FormParts::Inline();
            if (FormParts::Button(L("Uninstall")))
            {
               gDawPluginError.clear();
               gDawPluginDone.clear();
               std::string err;
               if (!DawPlugin::Uninstall(env, err))
                  gDawPluginError = err;
            }
            FormParts::Inline();
            if (FormParts::Button(L("Reveal")) && !st.bundles.empty())
               Platform::RevealInFileManager(st.bundles.front().target);
         }
      }
      if (!gDawPluginDone.empty())
      {
         FormParts::Label("");
         ImGui::TextUnformatted(gDawPluginDone.c_str());
      }
      if (!gDawPluginError.empty())
      {
         FormParts::Label("");
         FormParts::TintedText(tok::U32(tok::action_record, CategoryColors::IsThemeLight()),
                               [&] { ImGui::TextWrapped("%s", gDawPluginError.c_str()); });
      }

      // Disclosure: the plugin is paid, the app is free. Buy is a chip like every other action here.
      ImGui::Spacing();
      FormParts::Section(T("Licence"));
      FormParts::Label(L("Plugin"));
      ImGui::AlignTextToFramePadding();
      ImGui::TextWrapped("%s", T("Infinite as a VST3/AU plugin runs inside your DAW, with your patch saved in the project. The plugin is a one-time purchase for two computers, with updates for life. The standalone app stays free."));
      FormParts::Label("");   // its own row, like the install actions: a chip beside text sits off the text line
      if (FormParts::Button(L("Buy the plugin")))
         Platform::OpenExternalUrl(License::kBuyUrl);
   }
}
