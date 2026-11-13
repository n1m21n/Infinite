#include "Providers.h"

#include <cctype>

namespace chat
{
   const std::vector<ProviderDef>& Providers()
   {
      static const std::vector<ProviderDef> kProviders = {
         { "claude", "Claude", AgentKind::Claude, &ArgsClaude },
         { "codex", "ChatGPT", AgentKind::Codex, &ArgsCodex },
         { "antigravity", "Gemini", AgentKind::Antigravity, &ArgsAntigravity },
         { "custom", "Custom", AgentKind::Custom, &ArgsCustom },
      };
      return kProviders;
   }

   const ProviderDef* FindProvider(const std::string& id)
   {
      for (const ProviderDef& p : Providers())
         if (id == p.id)
            return &p;
      return nullptr;
   }

   const ProviderDef* FindProvider(AgentKind kind)
   {
      for (const ProviderDef& p : Providers())
         if (p.kind == kind)
            return &p;
      return nullptr;
   }

   const std::vector<CustomPreset>& CustomPresets()
   {
      // URLs and model names are hints, not checked against the services (see docs/custom-agent.md).
      static const std::vector<CustomPreset> k = {
         { "ollama", "Ollama (local)", "http://localhost:11434/v1", { "qwen3-coder", "gpt-oss:20b", "llama3.3" },
           "Ollama needs no key: leave the key empty. Pull the model first (ollama pull <name>); tools need a model that supports them." },
         { "deepseek", "DeepSeek", "https://api.deepseek.com", { "deepseek-chat", "deepseek-reasoner" },
           "Get the key at platform.deepseek.com. deepseek-chat supports tool calls." },
         { "openrouter", "OpenRouter", "https://openrouter.ai/api/v1", { "openai/gpt-5.5", "anthropic/claude-sonnet-5.5", "deepseek/deepseek-chat" },
           "One OpenRouter key opens many models; use the model's full id from openrouter.ai/models." },
      };
      return k;
   }

   bool ValidEnvName(const std::string& name)
   {
      if (name.empty() || std::isdigit((unsigned char)name[0]))
         return false;
      for (char c : name)
         if (!(std::isalnum((unsigned char)c) || c == '_'))
            return false;
      return true;
   }

   std::string CustomProblem(const std::string& baseUrl, const std::string& model, const std::string& keyEnv)
   {
      if (baseUrl.rfind("http://", 0) != 0 && baseUrl.rfind("https://", 0) != 0)
         return "Custom agent: set a Base URL (http:// or https://) in Settings > Agents & Terminal.";
      if (model.empty())
         return "Custom agent: set a Model ID in Settings > Agents & Terminal.";
      if (!keyEnv.empty() && !ValidEnvName(keyEnv))
         return "Custom agent: the key variable name may only use letters, digits and _ .";
      return {};
   }

   std::vector<std::string> ArgsCustom(const std::string&, const std::string&, bool, const std::vector<std::string>&, const TurnOptions&)
   {
      return {};  // no child process: the chat runs HttpAgent in-process
   }
}
