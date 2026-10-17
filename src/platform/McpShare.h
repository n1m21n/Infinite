#pragma once

// Infinite-Turbo 0.59: sharing this app's MCP server with remote MCP clients
// (ChatGPT developer mode connectors and the like), which only talk to a
// public HTTPS URL. Start() runs `Infinite-Turbo.exe --mcp-http <port> <token>`
// (Streamable HTTP on 127.0.0.1) and, when cloudflared is installed, a free
// Cloudflare quick tunnel to it, then reports the public URL. Both child
// processes live in a job object, so closing the app ends them.

#include <string>

namespace McpShare
{
   enum State { kOff = 0, kStarting, kLocalOnly, kPublic, kFailed };

   int Port();
   // The secret path segment (/mcp/<token>), kept in the settings folder so it
   // survives restarts; NewToken() replaces it (old URLs stop working).
   std::string Token();
   void NewToken();
   std::string FindCloudflared();

   bool Start(std::string& error);
   void Stop();
   State Current();
   std::string LocalUrl();  // http://127.0.0.1:<port>/mcp/<token>
   std::string PublicUrl(); // https://<name>.trycloudflare.com/mcp/<token>, once known
   std::string LastError();
}
