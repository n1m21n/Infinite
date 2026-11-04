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
      enum class Kind { Other, SignIn, Limit, Offline, Missing, Terms };
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
   };
   // The fixed list for an agent ({} = the agent picks); Antigravity's comes from `agy models`.
   std::vector<Model> FixedModels(AgentKind kind);
   // `agy models` output: a header line, then `<id>\t<label>`.
   std::vector<Model> ParseAgyModels(const std::string& output);
   // True when a sign-in is waiting for the user to paste an authorization code (Gemini shows one in the browser).
   bool AsksForCode(const std::string& log);
}
