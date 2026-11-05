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
   // How long the chat waits for a browser sign-in before it stops it and says so.
   constexpr double kSignInSeconds = 180.0;

   // The agent's own "am I signed in" check, run in the background; a file probe alone goes stale (Antigravity
   // keeps its login in the keychain, Claude leaves its account in ~/.claude.json after a logout).
   // On macOS Antigravity's check first asks the keychain whether its item exists (no secret is read) and runs
   // `agy models` only then, so a background check never starts a browser sign-in.
   std::vector<std::string> StatusArgs(AgentKind kind, bool mac);
   enum class Auth { Unknown, In, Out };
   // What StatusArgs printed and whether it exited 0. Unknown when the answer says neither.
   Auth SignedInFrom(AgentKind kind, bool exitOk, const std::string& output);

   // argv that clears the agent's stored login ({} = not supported on this system). Antigravity has no
   // logout command: on macOS its login is the keychain item service "gemini", account "antigravity", with
   // ~/.gemini/jetski-standalone-oauth-token as a file fallback. Its Linux and Windows keyring entries are
   // not verified, so there it is {}.
   std::vector<std::string> SignOutArgs(AgentKind kind, bool mac);

   // Terminal output without colour codes, carriage returns or other control bytes.
   std::string StripAnsi(const std::string& text);
   // The first https:// address in the text (the page to open when the CLI could not), or "".
   std::string FindUrl(const std::string& text);

   // What a failed turn or setup means for the user.
   struct Friendly
   {
      // Plan: the account needs a paid plan. Disk, Policy (Windows blocks scripts), Password (a step needs sudo)
      // and NoBrowser come from a setup or sign-in; their text says the one next thing.
      enum class Kind { Other, SignIn, Limit, Offline, Missing, Terms, Plan, Disk, Policy, Password, NoBrowser };
      Kind kind = Kind::Other;
      std::string text;  // one short sentence, no jargon
   };
   Friendly Explain(const std::string& agentName, const std::string& raw);
   // True when a chat turn stopped to ask for a sign-in (an address to open, a code to paste) in plain text
   // outside its stream-json: nobody can answer it there, so the chat ends the turn and shows Sign in.
   bool WaitsForSignIn(const std::string& log);

   // One row of a model menu.
   struct Model
   {
      std::string id, label;
      std::string full;  // the agent's own name when `label` is shortened for the picker
   };
   // "Gemini 3.8 Flash (Medium)" -> "Gemini 3.8": drop the effort in brackets and the tier word, but only while every
   // name in the list stays different; otherwise keep as much as is needed to tell them apart.
   void ShortenModelLabels(std::vector<Model>& models);
   // The fixed list for an agent ({} = the agent picks); Antigravity's comes from `agy models`.
   std::vector<Model> FixedModels(AgentKind kind);
   // `agy models` output: a header line, then `<id>\t<label>`.
   std::vector<Model> ParseAgyModels(const std::string& output);
   // Codex keeps the models its account may use in $CODEX_HOME/models_cache.json: the listed ones, by priority.
   std::vector<Model> ParseCodexModels(const std::string& json);
   // Where an agent keeps the model it uses when none is picked: Claude's and Antigravity's settings.json, Codex's
   // config.toml ($CODEX_HOME when set).
   std::string ModelSettingsPath(AgentKind kind, const std::string& home, const std::string& codexHome);
   // That file's model ("sonnet", "gpt-6.1-sol", "Gemini 3.8 Flash (Medium)"); "" when it names none.
   std::string ConfiguredModel(AgentKind kind, const std::string& text);
   // True when a sign-in is waiting for the user to paste an authorization code (Gemini shows one in the browser).
   bool AsksForCode(const std::string& log);
}
