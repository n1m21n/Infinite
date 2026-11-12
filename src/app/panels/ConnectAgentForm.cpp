// fork: Settings > Agents & Terminal > "Connect Infinite to an outside agent". Turns the in-app MCP server
// (core/McpHttp.cpp) on and off, shows URL and token, and copies ready configs / instructions for an agent.
#include "app/AppShared.h"
#include "app/ui/design/components/ChipButton.h"
#include "app/ui/design/components/FormParts.h"
#include "core/CliSetup.h"
#include "core/McpHttp.h"

#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>

namespace app
{
   void DrawConnectAgentForm()
   {
      static McpHttp::Settings s;
      static bool loaded = false;
      static std::string error, check, copied;
      static bool revealed = false;
      static double checkedAt = -1e9;
      if (!loaded)
      {
         loaded = true;
         s = McpHttp::Load();
      }
      FormParts::Section(T("Connect Infinite to an outside agent"));
      bool apply = false;
      FormParts::Label(T("MCP server (HTTP)"));
      if (ChipButton::Draw(s.enabled ? T("Turn off") : T("Turn on"), !s.enabled, ImGui::GetFrameHeight(), 110.0f))
      {
         s.enabled = !s.enabled;
         apply = true;
      }
      FormParts::Label(T("Port"));
      static char port[16];
      static bool portInit = false;
      if (!portInit)
      {
         portInit = true;
         std::snprintf(port, sizeof port, "%d", s.port);
      }
      if (FormParts::SearchInput("##mcpPort", "7790", port, sizeof port, 100.0f))
      {
         const int p = std::atoi(port);
         if (p >= 1024 && p <= 65535)
         {
            s.port = p;
            apply = true;
         }
      }
      FormParts::Label(T("Access token"));
      const std::string shown = revealed ? s.token : std::string(s.token.size(), '*');
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(shown.c_str());
      FormParts::Label("");
      if (ChipButton::Draw(revealed ? T("Hide") : T("Show"), false, ImGui::GetFrameHeight(), 70.0f))
         revealed = !revealed;
      ImGui::SameLine();
      if (ChipButton::Draw(T("New token"), false, ImGui::GetFrameHeight(), 100.0f))
      {
         s.token = McpHttp::NewToken();
         apply = true;
      }
      if (apply)
      {
         McpHttp::Save(s);
         error = McpHttp::Apply(s);
         checkedAt = -1e9;
      }
      const double now = ImGui::GetTime();
      static double retriedAt = -1e9;
      if (s.enabled && !McpHttp::Running() && error.empty() && now - retriedAt > 5.0)  // e.g. the port was busy at start
      {
         retriedAt = now;
         error = McpHttp::Apply(s);
      }
      if (s.enabled && now - checkedAt > 3.0)
      {
         checkedAt = now;
         // On a worker thread: the check opens a socket to our own server and must never stall a frame.
         static std::shared_ptr<std::string> result = std::make_shared<std::string>("checking…");
         static std::shared_ptr<std::mutex> mu = std::make_shared<std::mutex>();
         static std::shared_ptr<std::atomic<bool>> busy = std::make_shared<std::atomic<bool>>(false);
         if (!busy->exchange(true))
            std::thread([] { const std::string r = McpHttp::SelfCheck(); { std::lock_guard<std::mutex> g(*mu); *result = r; } busy->store(false); }).detach();
         std::lock_guard<std::mutex> g(*mu);
         check = *result;
      }
      FormParts::Label(T("Status"));
      std::string status = !s.enabled ? T("Off") : !error.empty() ? error : check.empty() ? std::string(T("Available at ")) + McpHttp::Url(s) : std::string(T("Not answering: ")) + check;
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(status.c_str());

      const std::string url = McpHttp::Url(s);
      const std::string exe = CliSetup::RealExecutable();
      auto copy = [&](const char* label, const std::string& text)
      {
         FormParts::Label("");
         if (ChipButton::Draw(label, false, ImGui::GetFrameHeight(), 260.0f))
         {
            ImGui::SetClipboardText(text.c_str());
            copied = label;
         }
         if (copied == label)
         {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", T("Copied"));
         }
      };
      copy(T("Copy instructions for an agent"), McpHttp::AgentInstructions(s, exe));
      copy(T("Copy Claude Code command"), "claude mcp add --transport http infinite " + url + " --header \"Authorization: Bearer " + s.token + "\"");
      copy(T("Copy JSON config (mcpServers)"), "{ \"mcpServers\": { \"infinite\": { \"url\": \"" + url + "\", \"headers\": { \"Authorization\": \"Bearer " + s.token + "\" } } } }");
      copy(T("Copy Codex config.toml"), "[mcp_servers.infinite]\nurl = \"" + url + "\"\nhttp_headers = { Authorization = \"Bearer " + s.token + "\" }\n");
      FormParts::Label("");
      FormParts::TintedText(ImGui::GetColorU32(ImGuiCol_TextDisabled),
                            [&] { ImGui::TextWrapped("%s", T("Loopback only. The token is a secret: paste it only into your own agent's config. Paste the instructions into any agent and it connects by itself.")); });
   }
}
