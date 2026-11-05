#pragma once
// What the chat says on its own, and what it tells an agent before the first turn (docs/plans/agent-collab/README.md C9).
// Slash commands that need no agent answer from here; every answer points at a source the agents can read
// themselves (`infinite docs`, the website, the repository), so nothing is written twice.
#include <string>
#include <vector>

namespace chat
{
   struct SlashCommand
   {
      const char* name;     // without the slash
      const char* summary;  // one short line
      bool takesArg;
   };
   const std::vector<SlashCommand>& SlashCommands();

   // `/name rest` -> true with name (lower case) and rest; a path such as `/Users/x` is not a command.
   bool ParseSlash(const std::string& line, std::string& name, std::string& arg);

   // The commands whose name starts with `prefix` (without the slash), for the composer's list.
   std::vector<const SlashCommand*> MatchSlash(const std::string& prefix);

   // Reply text in the chat's reply grammar (buttons included) for a command that needs no agent;
   // "" when the command is not a built-in answer (the chat handles /new, /agent, /model, or sends it on).
   std::string SlashAnswer(const std::string& name, const std::string& arg);

   // For a command an agent answers: the prompt it gets, with where to look. "" when not one.
   std::string SlashPrompt(const std::string& name, const std::string& arg);

   // The first message of a chat: how it works in four steps, with buttons.
   std::string Welcome();

   // Put before the first prompt of an agent's session: what Infinite is, how to answer from the source, how to reply.
   std::string Briefing();
}
