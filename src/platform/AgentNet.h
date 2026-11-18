#pragma once
// fork: what the built-in chat agent needs from the OS - an HTTPS/HTTP POST whose body is read as it arrives
// (Server-Sent Events), and a place for the API key that is not a file (Keychain / Credential Manager).
// macOS: NSURLSession + Security.framework. Windows: WinHTTP + CredWrite (NOT VERIFIED on Windows).
// Linux: stubs that say so.
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace AgentNet
{
   struct Request
   {
      std::string url;
      std::vector<std::pair<std::string, std::string>> headers;
      std::string body;
      int timeoutSeconds = 120;  // no data at all for this long ends the request
   };
   // Blocking; call from a worker thread. `onData` gets each piece of the response body as it arrives; return
   // false to cancel. `status` is the HTTP status (0 when none came). False, `error` set, on a transport failure
   // or cancel; a non-2xx status still returns true so the caller can read the error body it streamed.
   bool Post(const Request& request, const std::function<bool(const char*, size_t)>& onData, int& status, std::string& error);

   bool SecretSet(const std::string& service, const std::string& account, const std::string& value);
   bool SecretGet(const std::string& service, const std::string& account, std::string& value);  // false when absent
   bool SecretDelete(const std::string& service, const std::string& account);
   bool SecretsAvailable();
}
