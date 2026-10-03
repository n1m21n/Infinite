#pragma once

// Infinite-Turbo 0.47: the in-app Claude chat runs the Claude Code CLI the
// user already has installed and logged in (`claude -p`, so it uses their
// Claude subscription, no API key), with this app's own MCP server
// (Infinite-Turbo.exe --mcp) as its only tool source. One process per turn;
// the conversation continues with --resume <session id>.
//
// Thread model: Send() starts the process and a reader thread; the main
// thread calls Drain() once a frame to collect what arrived. The MCP bridge
// the CLI spawns talks to this app over the control port, whose requests the
// main loop serves every frame, so nothing here may block the main thread.

#include <string>
#include <vector>

namespace ClaudeChat
{
   struct Event
   {
      enum Kind
      {
         kText,      // assistant text (a whole block)
         kTool,      // a tool call, "create_node {...}"
         kToolError, // a tool returned an error
         kSession,   // text = session id, for --resume
         kInfo,      // status line (model, MCP connected...)
         kError,     // CLI or turn failure, shown in red
         kDone       // the process ended; text = "" or a reason
      };
      Kind kind = kInfo;
      std::string text;
   };

   struct Options
   {
      std::string claudePath; // claude.exe / claude.cmd; empty = FindClaude()
      std::string exePath;    // Infinite-Turbo.exe (its --mcp is the MCP server)
      std::string workDir;    // cwd of the CLI and where the MCP config is written
      std::string sessionId;  // continue this conversation; empty = new one
      std::string model;      // empty = the CLI's default
      std::string systemPrompt;
   };

   // Path of the Claude Code CLI, or empty when it is not installed:
   // INFINITE_CLAUDE_PATH, %USERPROFILE%\.local\bin\claude.exe (native
   // installer), then claude.exe / claude.cmd on PATH, then %APPDATA%\npm.
   std::string FindClaude();

   bool Send(const std::string& prompt, const Options& options, std::string& error);
   bool Busy();
   void Cancel(); // kills the CLI and everything it started
   void Drain(std::vector<Event>& out);
   void Discard(); // drops whatever arrived and was not drained yet
}
