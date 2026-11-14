#pragma once
// fork: the built-in chat agent - OpenAI-compatible /chat/completions with stream:true, and a tool-calling loop
// over Infinite's own tools (core/McpHttp.cpp: infinite_context, infinite_cli, infinite_rpc). No external CLI.
// The API key is read from the Keychain / Credential Manager, else from an environment variable; Infinite never
// writes it to a file or a log.
#include "AgentStream.h"

#include "json.hpp"

#include <functional>
#include <string>
#include <vector>

namespace chat
{
   struct HttpAgentConfig
   {
      std::string baseUrl;   // "https://openrouter.ai/api/v1"; "/chat/completions" is added
      std::string model;
      std::string keyEnv;    // fallback: the environment variable holding the key
      bool tools = true;     // offer Infinite's tools to the model
      int maxSteps = 12;     // model calls per turn
   };

   constexpr const char* kKeyService = "InfiniteTU"  /* fork: */;
   constexpr const char* kKeyAccount = "custom-agent";
   // The key and where it came from ("Keychain", "$NAME", or "" = none: fine for Ollama).
   // `askShell` = also try the login shell (slow, worker threads only; never from the UI thread).
   std::string ResolveKey(const std::string& keyEnv, std::string& source, bool askShell = true);
   std::string MaskKey(const std::string& key);  // "sk-…a1b2"
   std::string ChatUrl(const std::string& baseUrl);

   // Reads chat.completion.chunk lines (Server-Sent Events) into a reply, tool calls, usage.
   class SseChat
   {
   public:
      void Feed(const char* data, size_t n);
      std::string text;
      struct Call { std::string id, name, args; };
      std::vector<Call> calls;
      std::string finish;
      TurnUsage usage;
      std::string error;  // an error object inside the stream
      bool done = false;
   private:
      std::string mPending;
      void Line(const std::string& line);
   };

   // The OpenAI `tools` array for Infinite's tools.
   nlohmann::json OpenAiTools();
   // The one-line "what it ran" for a tool call, as the chat shows it.
   std::string DescribeCall(const std::string& name, const nlohmann::json& args);

   // One turn: messages are kept per `session` in memory; events go to `emit`. Returns when the turn ended.
   void RunHttpTurn(const HttpAgentConfig& cfg, const std::string& session, bool resume, const std::string& prompt,
                    const std::function<void(const AgentEvent&)>& emit, const std::function<bool()>& cancelled);

   struct ConnectionResult { bool ok = false; std::string message; };
   ConnectionResult TestConnection(const HttpAgentConfig& cfg);
}
