#pragma once
// fork: provider table (stage 6 of docs/plans/ux-roadmap.md). One row per chat agent instead of if/else on
// AgentKind scattered over the code: id, name, how its argv is built. "custom" has no CLI: it is the built-in
// OpenAI-compatible agent (HttpAgent.h) and its argv builder returns nothing.
#include "AgentStream.h"

#include <string>
#include <vector>

namespace chat
{
   using ArgvFn = std::vector<std::string> (*)(const std::string& prompt, const std::string& session, bool resume,
                                               const std::vector<std::string>& dirs, const TurnOptions& options);
   struct ProviderDef
   {
      const char* id;
      const char* name;
      AgentKind kind;
      ArgvFn argv;
   };
   const std::vector<ProviderDef>& Providers();
   const ProviderDef* FindProvider(const std::string& id);  // nullptr when unknown
   const ProviderDef* FindProvider(AgentKind kind);

   // The per-CLI argv builders (AgentStream.cpp) the table points at.
   std::vector<std::string> ArgsClaude(const std::string&, const std::string&, bool, const std::vector<std::string>&, const TurnOptions&);
   std::vector<std::string> ArgsCodex(const std::string&, const std::string&, bool, const std::vector<std::string>&, const TurnOptions&);
   std::vector<std::string> ArgsAntigravity(const std::string&, const std::string&, bool, const std::vector<std::string>&, const TurnOptions&);

   std::vector<std::string> ArgsCustom(const std::string&, const std::string&, bool, const std::vector<std::string>&, const TurnOptions&);

   // ---- the custom (OpenAI-compatible) provider ----
   struct CustomPreset
   {
      const char* id;
      const char* name;
      const char* baseUrl;
      std::vector<const char*> models;  // hints only
      const char* note;
   };
   const std::vector<CustomPreset>& CustomPresets();
   constexpr const char* kDefaultKeyEnv = "CUSTOM_LLM_API_KEY";
   bool ValidEnvName(const std::string& name);
   // "" when the form is complete, else why a turn cannot start (shown in the chat).
   std::string CustomProblem(const std::string& baseUrl, const std::string& model, const std::string& keyEnv);
}
