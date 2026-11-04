#pragma once
// Signing an agent in from the chat, and saying what went wrong in plain words (docs/plans/agent-collab/README.md C8).
// Every agent signs in through the browser without its terminal screen: `claude auth login`, `codex login`, and
// Antigravity's own first headless turn. Pure functions, so a test checks them with strings.
#include "AgentStream.h"

#include <string>
#include <vector>

namespace chat
{
   // argv of the sign-in: the CLI opens the browser itself and waits for the callback.
   std::vector<std::string> SignInArgs(AgentKind kind);
   // Seconds the CLI waits for the browser before it gives up (0 = it does not).
   int SignInWaitSeconds(AgentKind kind);

   // Terminal output without colour codes, carriage returns or other control bytes.
   std::string StripAnsi(const std::string& text);
   // The first https:// address in the text (the page to open when the CLI could not), or "".
   std::string FindUrl(const std::string& text);

   // What a failed turn or setup means for the user.
   struct Friendly
   {
      enum class Kind { Other, SignIn, Limit, Offline, Missing, Terms };
      Kind kind = Kind::Other;
      std::string text;  // one short sentence, no jargon
   };
   Friendly Explain(const std::string& agentName, const std::string& raw);

   // One row of a model menu.
   struct Model
   {
      std::string id, label;
   };
   // The fixed list for an agent ({} = the agent picks); Antigravity's comes from `agy models`.
   std::vector<Model> FixedModels(AgentKind kind);
   // `agy models` output: a header line, then `<id>\t<label>`.
   std::vector<Model> ParseAgyModels(const std::string& output);
}
