// fork: Linux half of AgentNet.h: not implemented. The built-in chat agent says so instead of failing silently.
#include "AgentNet.h"

namespace AgentNet
{
   bool Post(const Request&, const std::function<bool(const char*, size_t)>&, int& status, std::string& error)
   {
      status = 0;
      error = "The built-in agent is not available on Linux yet (macOS and Windows only).";
      return false;
   }
   bool SecretSet(const std::string&, const std::string&, const std::string&) { return false; }
   bool SecretGet(const std::string&, const std::string&, std::string&) { return false; }
   bool SecretDelete(const std::string&, const std::string&) { return false; }
   bool SecretsAvailable() { return false; }
}
