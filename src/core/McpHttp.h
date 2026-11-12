#pragma once
// fork: the Infinite MCP server inside the running app, Streamable HTTP (single JSON responses) on
// 127.0.0.1:<fixed port> with a bearer token, so any MCP client connects by URL with nothing installed.
// Tools go to the same RemoteControl queue as the `infinite` command; `infinite_cli` runs the app's own
// `--cli` binary (the commands the chat's Briefing teaches). Off by default; Settings turns it on.
#include "json.hpp"

#include <string>
#include <vector>

namespace McpHttp
{
   struct Settings
   {
      bool enabled = false;
      int port = 7790;
      std::string token;   // 32 hex chars, made on first use; shown to the user, kept in <AppSupport>/mcp-http.json
   };
   Settings Load();                       // creates the token when missing (does not write)
   void Save(const Settings& s);          // file mode 0600
   std::string NewToken();
   // Applies `s`: stops a running server, starts one when enabled. Error text when the port cannot be bound.
   std::string Apply(const Settings& s);
   void StartFromSettings();              // at app start
   bool Running();
   int BoundPort();
   // A real request to our own port (initialize + tools/list): "" when it answers, else why not.
   std::string SelfCheck();
   // Number of tools / names, for tests and Settings.
   std::string Url(const Settings& s);
   // The text for "copy instructions for an agent": docs/connect-agent.md with real port, token and paths filled in.
   std::string AgentInstructions(const Settings& s, const std::string& exePath);
   // The JSON-RPC handler, exposed for tests: one request body in, one response body out ("" = no response).
   std::string Handle(const std::string& body);
   // The tool list and a tool call, for the built-in chat agent (same tools, same code path).
   nlohmann::json ToolsJson();
   nlohmann::json CallToolJson(const std::string& name, const nlohmann::json& args);
   nlohmann::json TextResultJson(const std::string& text, bool error);
   // Parameters and a working example of each infinite_rpc method ("" when unknown); the RPC error text quotes it.
   std::string RpcExample(const std::string& method);
   nlohmann::json RpcHelp(const std::string& method);   // one method, or all when empty
   std::vector<std::string> RpcMethodNames();
}
