#pragma once

// Infinite-Turbo 0.45: `Infinite-Turbo.exe --mcp` runs this instead of the
// app: an MCP (Model Context Protocol) server on stdin/stdout that forwards
// tool calls to the running Infinite-Turbo over its local control port.
// Returns the process exit code.
int RunMcpBridge();

// `--mcp-install`: registers the exe in Claude Desktop's config (setup-mcp.bat).
int InstallMcpConfig();
