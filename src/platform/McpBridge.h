#pragma once

// Infinite-Turbo 0.45: `Infinite-Turbo.exe --mcp` runs this instead of the
// app: an MCP (Model Context Protocol) server on stdin/stdout that forwards
// tool calls to the running Infinite-Turbo over its local control port.
// Returns the process exit code.
int RunMcpBridge();

#include <string>
// Turbo 0.59: `--mcp-http <port> <token>`, the same server over Streamable
// HTTP on 127.0.0.1 at /mcp/<token>, for ChatGPT and other remote MCP clients
// (reached through an HTTPS tunnel, see McpShare). Runs until killed.
int RunMcpHttp(int port, const std::string& token);

// `--mcp-install`: registers the exe in Claude Desktop's config (setup-mcp.bat).
int InstallMcpConfig();
