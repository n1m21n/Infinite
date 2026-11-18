// fork: Settings > Agents & Terminal > "Custom agent (OpenAI-compatible)" (stage 6, docs/custom-agent.md).
// Base URL, Model ID and the NAME of the environment variable holding the key. The key itself is never
// asked for, shown or stored by Infinite.
#include "app/AppShared.h"
#include "app/ui/design/components/FormParts.h"
#include "core/CliSetup.h"
#include "core/chat/HttpAgent.h"
#include "core/chat/Providers.h"
#include "platform/AgentNet.h"
#include "platform/AppPaths.h"

#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>

namespace app
{
   void DrawCustomAgentForm()
   {
      using namespace CliSetup;
      Config& prefs = Prefs();
      const std::string cfg = PathsFor(AppPaths::AppSupportDir(), CurrentOs()).config;
      static char url[512], model[256], keyEnv[128];
      static bool loaded = false;
      if (!loaded)
      {
         loaded = true;
         std::snprintf(url, sizeof url, "%s", prefs.customBaseUrl.c_str());
         std::snprintf(model, sizeof model, "%s", prefs.models["custom"].c_str());
         std::snprintf(keyEnv, sizeof keyEnv, "%s", prefs.customKeyEnv.c_str());
      }
      bool dirty = false;

      FormParts::Section(T("Custom agent (OpenAI-compatible)"));
      static char apiKey[512];
      static std::string keySource, keyMask, testResult;
      static double keyAt = -1e9;
      static std::shared_ptr<std::atomic<int>> testState;  // 0 idle, 1 running, 2 ok, 3 failed
      static std::shared_ptr<std::string> testText = std::make_shared<std::string>();
      static std::shared_ptr<std::mutex> testMutex = std::make_shared<std::mutex>();
      if (ImGui::GetTime() - keyAt > 2.0)  // the Keychain is asked rarely, never every frame
      {
         keyAt = ImGui::GetTime();
         const std::string k = chat::ResolveKey(prefs.customKeyEnv, keySource, false);
         keyMask = chat::MaskKey(k);
      }
      // Presets fill only the URL; model names are hints in the chat's model menu.
      const auto& presets = chat::CustomPresets();
      int cur = 0;
      std::string items = std::string(T("None")) + '\0';
      for (size_t i = 0; i < presets.size(); i++)
      {
         items += std::string(presets[i].name) + '\0';
         if (prefs.customPreset == presets[i].id)
            cur = (int)i + 1;
      }
      items.push_back('\0');
      if (FormParts::Combo(T("Preset"), &cur, items.c_str()))
      {
         prefs.customPreset = cur == 0 ? std::string() : presets[(size_t)cur - 1].id;
         if (cur != 0)
         {
            std::snprintf(url, sizeof url, "%s", presets[(size_t)cur - 1].baseUrl);
            prefs.customBaseUrl = url;
         }
         dirty = true;
      }
      FormParts::Label(T("Base URL"));
      if (FormParts::SearchInput("##customUrl", "http://localhost:11434/v1", url, sizeof url, FormParts::ControlW()))
      {
         prefs.customBaseUrl = url;
         dirty = true;
      }
      FormParts::Label(T("Model ID"));
      if (FormParts::SearchInput("##customModel", "qwen3-coder", model, sizeof model, FormParts::ControlW()))
      {
         prefs.models["custom"] = model;
         dirty = true;
      }
      if (dirty)
         SaveConfig(cfg, prefs);

      // The key: typed here, kept in the system keychain, never written to Infinite's files or logs.
      FormParts::Label(T("API key"));
      {
         ImGui::SetNextItemWidth(FormParts::ControlW());
         ImGui::InputTextWithHint("##customKey", keyMask.empty() ? T("paste the key (stored in the keychain)") : (keySource + "  " + keyMask).c_str(), apiKey, sizeof apiKey,
                                  ImGuiInputTextFlags_Password);
         FormParts::Label("");
         if (ChipButton::Draw(T("Save key"), true, ImGui::GetFrameHeight(), 90.0f) && apiKey[0] != '\0')
         {
            AgentNet::SecretSet(chat::kKeyService, chat::kKeyAccount, apiKey);
            std::memset(apiKey, 0, sizeof apiKey);
            keyAt = -1e9;
         }
         ImGui::SameLine();
         if (ChipButton::Draw(T("Remove"), false, ImGui::GetFrameHeight(), 90.0f))
         {
            AgentNet::SecretDelete(chat::kKeyService, chat::kKeyAccount);
            keyAt = -1e9;
         }
      }
      FormParts::Label(T("or env variable"));
      if (FormParts::SearchInput("##customKeyEnv", chat::kDefaultKeyEnv, keyEnv, sizeof keyEnv, FormParts::ControlW()))
      {
         prefs.customKeyEnv = keyEnv;
         SaveConfig(cfg, prefs);
         keyAt = -1e9;
      }
      FormParts::Label("");
      if (ChipButton::Draw(T("Test connection"), false, ImGui::GetFrameHeight(), 160.0f) && (!testState || testState->load() != 1))
      {
         testState = std::make_shared<std::atomic<int>>(1);
         chat::HttpAgentConfig hc;
         hc.baseUrl = prefs.customBaseUrl;
         hc.model = prefs.models["custom"];
         hc.keyEnv = prefs.customKeyEnv;
         auto state = testState;
         auto text = testText;
         auto mu = testMutex;
         std::thread([hc, state, text, mu]
                     {
                        const chat::ConnectionResult r = chat::TestConnection(hc);
                        {
                           std::lock_guard<std::mutex> g(*mu);
                           *text = r.message;
                        }
                        state->store(r.ok ? 2 : 3);
                     }).detach();
      }
      if (testState)
      {
         ImGui::SameLine();
         std::string line;
         {
            std::lock_guard<std::mutex> g(*testMutex);
            line = testState->load() == 1 ? std::string(T("Testing…")) : *testText;
         }
         ImGui::AlignTextToFramePadding();
         ImGui::TextWrapped("%s", line.c_str());
      }

      FormParts::Label("");
      std::string note = T("The key is stored in your system keychain (macOS Keychain / Windows Credential Manager), never in Infinite's files. Ollama needs no key. The model must support tool calls to change the patch; otherwise the chat talks only.");
      for (const chat::CustomPreset& p : presets)
         if (prefs.customPreset == p.id)
            note += std::string("\n") + p.note;
      const std::string why = chat::CustomProblem(prefs.customBaseUrl, prefs.models["custom"], prefs.customKeyEnv);
      if (!why.empty())
         note += "\n" + why;
      FormParts::TintedText(ImGui::GetColorU32(ImGuiCol_TextDisabled), [&] { ImGui::TextWrapped("%s", note.c_str()); });
   }
}
