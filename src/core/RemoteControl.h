#pragma once

// Embedded local control server: lets an external process (the Infinite MCP
// server, or any other local tool) drive the running app over a plain TCP
// socket using newline-delimited JSON-RPC 2.0. Bound to 127.0.0.1 only - this
// is a local dev/automation hook, not a network service, and must never be
// exposed beyond loopback.
//
// Threading: RemoteControl owns its own accept/read thread(s) and never
// touches app state directly. Parsed requests are queued; DrainPending() must
// be called once per frame from the main thread (right after
// glfwPollEvents(), before any ed:: drawing) so every mutation lands between
// frames rather than mid-draw. The handler runs synchronously on the main
// thread and its return value is written back to the client by the network
// thread that queued the request.

#include <functional>
#include <string>

#include "json.hpp"

namespace RemoteControl
{
   // Returns false, err set, on error (result is a JSON-RPC "result" payload,
   // not the whole envelope - RemoteControl wraps it).
   using Handler = std::function<bool(const std::string& method, const nlohmann::json& params,
                                       nlohmann::json& outResult, std::string& outError)>;

   // Starts the accept thread bound to 127.0.0.1:port. Writes a random auth
   // token to ~/Library/Application Support/Infinite/control_token (creating
   // the directory if needed). When the port is taken, as by a second running
   // app, it takes the next free one and leaves the token file alone. Safe to
   // call once at startup; no-op if a server is already running.
   void Start(int port);

   // The port actually bound (0 = no server) and this app's token, for the
   // sessions of its Terminal panel.
   int Port();
   // Where each running app leaves <pid>.json = {pid, port, token}; removed when it quits.
   std::string InstancesDir();
   const std::string& Token();

   // Runs `handler` for every request received since the last call, in
   // receipt order, on the calling (main) thread. Must be called every frame.
   void DrainPending(const Handler& handler);

   // fork: run one request from any thread (the in-app MCP over HTTP, core/McpHttp.cpp) through the same queue
   // and handler as a socket client. Waits for the main thread up to `timeoutMs`. False, error set, on failure.
   bool Call(const std::string& method, const nlohmann::json& params, nlohmann::json& outResult, std::string& outError,
             int timeoutMs = 30000);
}
