#pragma once
// Files attached to a chat message (docs/plans/agent-collab/README.md C7): every kind the canvas takes
// on a drop, plus text and code. The agents get the paths in the prompt and read access to their
// folders; none of the file goes through the chat itself.
#include <string>
#include <vector>

namespace chat
{
   // "audio", "video", "image", "model", "midi", "vector", "patch", "field", "text", or "" when the chat
   // does not take it.
   std::string FileKind(const std::string& path);
   // The prompt with a line per file, which the agent reads with its own tools.
   std::string WithFiles(const std::string& prompt, const std::vector<std::string>& files);
   // The folders the files are in, each once, for the agents' read access.
   std::vector<std::string> FileDirs(const std::vector<std::string>& files);
   // Every extension FileKind takes, for the attach dialog.
   std::vector<std::string> AttachExtensions();
}
