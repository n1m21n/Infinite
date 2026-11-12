#include "McpHttp.h"
#include "core/ForkIdentity.h"

#include "McpAgentDoc.h"
#include "RemoteControl.h"
#include "CliSetup.h"
#include "json.hpp"
#include "platform/AppPaths.h"
#include "platform/NetCompat.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstdlib>
#include <cctype>
#include <fstream>
#include <mutex>
#include <random>
#include <sys/stat.h>
#include <thread>

using json = nlohmann::json;

namespace McpHttp
{
   namespace
   {
      std::atomic<int> gListenFd{ -1 };
      std::atomic<int> gPort{ 0 };
      std::atomic<unsigned> gGeneration{ 0 };
      std::mutex gMutex;
      std::string gToken;  // the token the running server checks

      std::string ConfigPath() { return AppPaths::AppSupportDir() + "/mcp-http.json"; }

      bool SameToken(const std::string& a, const std::string& b)
      {
         if (a.size() != b.size() || a.empty())
            return false;
         unsigned char d = 0;
         for (size_t i = 0; i < a.size(); i++)
            d |= (unsigned char)(a[i] ^ b[i]);
         return d == 0;
      }

      // Every method infinite_rpc accepts: its parameters and a call that works. Keep in step with
      // src/app/graph/Rpc*.cpp (the parameter names are the ones those handlers read).
      struct RpcDoc { const char* method; const char* params; const char* example; };
      const RpcDoc kRpcDocs[] = {
         { "status", "none", "{}" },
         { "get_graph", "none. Returns nodes (index, type, params) and links (id, src, dst)", "{}" },
         { "list_nodes", "none", "{}" },
         { "list_node_types", "none. Valid type names by category", "{}" },
         { "describe", "type?: node type name EXACTLY as list_node_types prints it (e.g. \"Bloom\"); omit to list all types. Returns control keys with a type letter prefix (f/i/b/c/s), e.g. \"f uRotation\"", "{\"type\":\"Kaleidoscope\"}" },
         { "get_params", "index: int node index; full?: bool. Keys carry the type letter prefix: \"f uRotation\"", "{\"index\":5}" },
         { "create_node", "typeName: string; category?: string; x?: number; y?: number. Returns {index}", "{\"typeName\":\"Oscillator\",\"x\":100,\"y\":100}" },
         { "delete_node", "index: int", "{\"index\":5}" },
         { "connect", "srcIndex: int; srcOutput?: int (default 0); dstIndex: int; dstSlot?: int (default 0). Returns the link id", "{\"srcIndex\":4,\"srcOutput\":0,\"dstIndex\":5,\"dstSlot\":0}" },
         { "disconnect", "linkId: int (from get_graph)", "{\"linkId\":3}" },
         { "set_param", "index: int; name: string, with or without the type letter (\"uRotation\" or \"f uRotation\"); value: number | string | bool. A string starting with = is an expression, e.g. \"=sin(t)\"", "{\"index\":5,\"name\":\"uRotation\",\"value\":0.5}" },
         { "set_expr", "index: int; key: string (\"f uRotation\"); text: expression, with or without the leading =; \"\" clears it", "{\"index\":5,\"key\":\"f uRotation\",\"text\":\"sin(t)*0.3\"}" },
         { "bind_mod", "dst: int node index to modulate; key: string (\"f uRotation\"); src: int modulator node index; output?: int (default 0); lo?, hi?: number range. Flat integers: dst/src are NOT objects", "{\"dst\":5,\"key\":\"f uRotation\",\"src\":4,\"output\":0,\"lo\":0,\"hi\":1}" },
         { "unbind_mod", "index: int node index; key: string (\"f uRotation\")", "{\"index\":5,\"key\":\"f uRotation\"}" },
         { "set_bypass", "index: int; bypassed?: bool (default true)", "{\"index\":3,\"bypassed\":true}" },
         { "set_node_position", "index: int; x: number; y: number", "{\"index\":3,\"x\":200,\"y\":120}" },
         { "get_node_position", "index: int", "{\"index\":3}" },
         { "auto_wire_inputs", "index: int node whose inputs get wired from nodes selected before it", "{\"index\":3}" },
         { "expand_node", "index: int; advanced?: bool", "{\"index\":3}" },
         { "select", "nodes?: int[] ; all?: bool", "{\"nodes\":[3,4]}" },
         { "selection", "none", "{}" },
         { "attention", "none. What the user is looking at / touched", "{}" },
         { "edit", "action: string; nodes?: int[] (copy, paste, duplicate, group... see `infinite help`)", "{\"action\":\"duplicate\",\"nodes\":[3]}" },
         { "explain", "all?: bool", "{}" },
         { "code_errors", "none. Compile errors of code nodes", "{}" },
         { "batch", "calls: [{method, params}] run in order; stops at the first error", "{\"calls\":[{\"method\":\"create_node\",\"params\":{\"typeName\":\"Oscillator\"}}]}" },
         { "undo", "none", "{}" },
         { "redo", "none", "{}" },
         { "new_patch", "none. Clears the patch (undoable)", "{}" },
         { "save_patch", "path?: absolute file path ending .inf. Omit to write the open patch's own file (error if it has none: then ask the user, never invent a path). Missing folders are created. Returns {saved: real path} or an error with the reason", "{\"path\":\"/Users/me/song.inf\"}" },
         { "load_patch", "path: string path of a .inf file", "{\"path\":\"/Users/me/song.inf\"}" },
         { "load_patch_text", "text: string, the patch file contents", "{\"text\":\"...\"}" },
         { "validate_patch_text", "text: string; strict?: bool", "{\"text\":\"...\"}" },
         { "transport", "action: \"play\"|\"pause\"|\"stop\"|\"seek\"|\"bpm\"; bpm?: number; seconds?: number", "{\"action\":\"bpm\",\"bpm\":128}" },
         { "settings", "action?/name?/value?: read or change an app setting; see `infinite settings`", "{\"name\":\"bpm\"}" },
         { "view", "action: string; on?/show?/volume?/accent?: see `infinite help`", "{\"action\":\"fit\"}" },
      };

      json Tools();

      json Tools()
      {
         const json none = { { "type", "object" }, { "properties", json::object() } };
         return json::array({
            { { "name", "infinite_context" },
              { "description", "State of the open Infinite patch (nodes, cables, selection, hints). Call first and after every change." },
              { "inputSchema", none } },
            { { "name", "infinite_cli" },
              { "description", "Run one `infinite` command, e.g. [\"add\",\"Oscillator\"], [\"connect\",\"osc\",\"out\"], [\"set\",\"osc.freq\",\"440\"], [\"undo\"], [\"find\",\"reverb\"], [\"docs\",\"nodes\"], [\"help\"]. Output is the command's text." },
              { "inputSchema", { { "type", "object" },
                                 { "properties", { { "args", { { "type", "array" }, { "items", { { "type", "string" } } }, { "description", "argv after `infinite`" } } } } },
                                 { "required", json::array({ "args" }) } } } },
            { { "name", "infinite_rpc" },
              { "description", "Low-level JSON-RPC call to the running app (see infinite_rpc_methods). Use when infinite_cli has no command." },
              { "inputSchema", { { "type", "object" },
                                 { "properties", { { "method", { { "type", "string" } } }, { "params", { { "type", "object" } } } } },
                                 { "required", json::array({ "method" }) } } } },
            { { "name", "infinite_rpc_methods" }, { "description", "Every method infinite_rpc accepts, with its parameters and a working example. Read this before calling infinite_rpc." }, { "inputSchema", none } },
            { { "name", "infinite_rpc_help" }, { "description", "Parameters and example of one infinite_rpc method." },
              { "inputSchema", { { "type", "object" }, { "properties", { { "method", { { "type", "string" } } } } }, { "required", json::array({ "method" }) } } } },
         });
      }

      std::string ShellQuote(const std::string& s)
      {
         std::string o = "'";
         for (char c : s)
            o += c == '\'' ? std::string("'\\''") : std::string(1, c);
         return o + "'";
      }

      // The state of the open patch from the app's own RPC, no child process (the only way `infinite_context`
      // works where the CLI cannot be started, and a fallback elsewhere).
      std::string ContextViaRpc()
      {
         json out = json::object();
         std::string err;
         for (const char* m : { "status", "get_graph", "attention", "selection", "code_errors" })
         {
            json r;
            if (RemoteControl::Call(m, json::object(), r, err))
               out[m] = r;
            else
               out[m] = { { "error", err } };
         }
         out["next"] = "Edit with infinite_rpc (see infinite_rpc_methods for parameters) or infinite_cli; call this again to verify.";
         return out.dump();
      }

#ifdef _WIN32
      // NOT VERIFIED on Windows: `<this binary> --cli ...` through _popen, the control port and token passed in the
      // environment of this process (which the child inherits).
      std::string WinQuote(const std::string& a)
      {
         std::string o = "\"";
         for (char c : a)
            o += c == '"' ? std::string("\\\"") : std::string(1, c);
         return o + "\"";
      }
#endif

      // Runs `<this binary> --cli args...` with the control port and token in its environment.
      bool RunCli(const std::vector<std::string>& args, std::string& out)
      {
#ifdef _WIN32
         _putenv_s("INFINITE_CONTROL_PORT", std::to_string(RemoteControl::Port()).c_str());
         _putenv_s("INFINITE_CONTROL_TOKEN", RemoteControl::Token().c_str());
         std::string cmd = "\"" + WinQuote(CliSetup::RealExecutable()) + " --cli";
         for (const std::string& a : args)
            cmd += " " + WinQuote(a);
         cmd += " 2>&1\"";
         FILE* p = _popen(cmd.c_str(), "r");
         if (p == nullptr)
         {
            out = "could not start the infinite command";
            return false;
         }
         char buf[4096];
         size_t n;
         while ((n = fread(buf, 1, sizeof buf, p)) > 0 && out.size() < (1u << 20))
            out.append(buf, n);
         return _pclose(p) == 0;
#else
         std::string cmd = "INFINITE_CONTROL_PORT=" + std::to_string(RemoteControl::Port()) + " INFINITE_CONTROL_TOKEN=" + RemoteControl::Token() +
                           " " + ShellQuote(CliSetup::RealExecutable()) + " --cli";
         for (const std::string& a : args)
            cmd += " " + ShellQuote(a);
         cmd += " 2>&1 </dev/null";
         FILE* p = popen(cmd.c_str(), "r");
         if (p == nullptr)
         {
            out = "could not start the infinite command";
            return false;
         }
         char buf[4096];
         size_t n;
         while ((n = fread(buf, 1, sizeof buf, p)) > 0 && out.size() < (1u << 20))
            out.append(buf, n);
         return pclose(p) == 0;
#endif
      }

      json TextResult(const std::string& text, bool error)
      {
         return { { "content", json::array({ { { "type", "text" }, { "text", text } } }) }, { "isError", error } };
      }

      json CallTool(const std::string& name, const json& args)
      {
         if (name == "infinite_context" || name == "infinite_cli")
         {
            std::vector<std::string> a;
            if (name == "infinite_context")
               a = { "context" };
            else if (args.is_object() && args.contains("args") && args["args"].is_array())
               for (const json& x : args["args"])
                  a.push_back(x.is_string() ? x.get<std::string>() : x.dump());
            else
               return TextResult("infinite_cli needs args: an array of strings", true);
            std::string out;
            if (name == "infinite_context")
            {
#ifdef _WIN32
               return TextResult(ContextViaRpc(), false);  // no child process on Windows
#else
               const bool ok = RunCli(a, out);
               if (!ok || out.empty())
                  return TextResult(ContextViaRpc(), false);
               return TextResult(out, false);
#endif
            }
            const bool ok = RunCli(a, out);
            return TextResult(out.empty() ? (ok ? "ok" : "failed") : out, !ok);
         }
         if (name == "infinite_rpc_methods")
         {
            return TextResult(RpcHelp("").dump(), false);
         }
         if (name == "infinite_rpc_help")
         {
            const std::string mname = args.is_object() && args.contains("method") && args["method"].is_string() ? args["method"].get<std::string>() : std::string();
            const json h = RpcHelp(mname);
            return TextResult(h.dump(), h.is_null());
         }
         if (name == "infinite_rpc")
         {
            if (!args.is_object() || !args.value("method", std::string()).size())
               return TextResult("infinite_rpc needs a method", true);
            const std::string mname = args["method"].is_string() ? args["method"].get<std::string>() : std::string();
            if (RpcExample(mname).empty())
               return TextResult("unknown or not allowed method '" + mname + "'. Call infinite_rpc_methods for the list with parameters.", true);
            if (args.contains("params") && !args["params"].is_object())
               return TextResult("params must be an object. Example for " + mname + ": " + RpcExample(mname), true);
            json result;
            std::string err;
            if (!RemoteControl::Call(mname, args.value("params", json::object()), result, err))
               return TextResult(err + (err.find("Example:") == std::string::npos ? " Example: " + RpcExample(mname) : std::string()), true);
            return TextResult(result.dump(), false);
         }
         return TextResult("unknown tool " + name, true);
      }

      std::string Reply(const json& id, const json& result)
      {
         return json{ { "jsonrpc", "2.0" }, { "id", id }, { "result", result } }.dump();
      }
      std::string Error(const json& id, int code, const std::string& msg)
      {
         return json{ { "jsonrpc", "2.0" }, { "id", id }, { "error", { { "code", code }, { "message", msg } } } }.dump();
      }

      std::string One(const json& req)
      {
         if (!req.is_object())
            return Error(nullptr, -32600, "invalid request");
         const std::string method = req.value("method", std::string());
         const bool hasId = req.contains("id");
         const json id = hasId ? req["id"] : json(nullptr);
         if (!hasId)
            return {};  // a notification
         if (method == "initialize")
         {
            std::string v = "2025-06-18";
            if (req.contains("params") && req["params"].is_object())
            {
               const std::string c = req["params"].value("protocolVersion", std::string());
               if (c == "2024-11-05" || c == "2025-03-26" || c == "2025-06-18")
                  v = c;
            }
            return Reply(id, { { "protocolVersion", v },
                               { "capabilities", { { "tools", { { "listChanged", false } } } } },
                               { "serverInfo", { { "name", "infinite" }, { "version", "1" } } },
                               { "instructions", "Drives the open Infinite patch. Start with infinite_context; edit with infinite_cli; verify with infinite_context." } });
         }
         if (method == "ping")
            return Reply(id, json::object());
         if (method == "tools/list")
            return Reply(id, { { "tools", Tools() } });
         if (method == "tools/call")
         {
            const json p = req.value("params", json::object());
            return Reply(id, CallTool(p.value("name", std::string()), p.value("arguments", json::object())));
         }
         return Error(id, -32601, "method not found: " + method);
      }

      std::string Lower(std::string s)
      {
         for (char& c : s)
            c = (char)std::tolower((unsigned char)c);
         return s;
      }

      void Respond(int fd, int code, const char* text, const std::string& body, const char* extra = "")
      {
         std::string r = "HTTP/1.1 " + std::to_string(code) + " " + text + "\r\nContent-Type: application/json\r\nContent-Length: " +
                         std::to_string(body.size()) + "\r\nConnection: keep-alive\r\n" + extra + "\r\n" + body;
         NetCompat::NetSend(fd, r.data(), r.size(), 0);
      }

      void Serve(int fd, unsigned generation)
      {
         std::string buf;
         char chunk[8192];
         for (;;)
         {
            size_t he;
            while ((he = buf.find("\r\n\r\n")) == std::string::npos)
            {
               NetSsize n = NetCompat::NetRecv(fd, chunk, sizeof chunk, 0);
               if (n <= 0 || buf.size() > (1u << 20))
                  goto done;
               buf.append(chunk, (size_t)n);
            }
            {
               const std::string head = buf.substr(0, he);
               std::string line1 = head.substr(0, head.find("\r\n"));
               size_t clen = 0;
               std::string auth, origin, ctype;
               bool closeAfter = false;
               size_t pos = 0;
               while ((pos = head.find("\r\n", pos)) != std::string::npos)
               {
                  pos += 2;
                  const size_t e = head.find("\r\n", pos);
                  const std::string h = head.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
                  const size_t c = h.find(':');
                  if (c == std::string::npos)
                     continue;
                  const std::string k = Lower(h.substr(0, c));
                  std::string v = h.substr(c + 1);
                  while (!v.empty() && v[0] == ' ')
                     v.erase(0, 1);
                  if (k == "content-length")
                     clen = (size_t)std::strtoul(v.c_str(), nullptr, 10);
                  else if (k == "authorization")
                     auth = v;
                  else if (k == "origin")
                     origin = v;
                  else if (k == "connection")
                     closeAfter = Lower(v).find("close") != std::string::npos;
               }
               if (clen > (4u << 20))
                  goto done;
               while (buf.size() < he + 4 + clen)
               {
                  NetSsize n = NetCompat::NetRecv(fd, chunk, sizeof chunk, 0);
                  if (n <= 0)
                     goto done;
                  buf.append(chunk, (size_t)n);
               }
               const std::string body = buf.substr(he + 4, clen);
               buf.erase(0, he + 4 + clen);

               std::string token;
               {
                  std::lock_guard<std::mutex> lock(gMutex);
                  token = gToken;
               }
               if (generation != gGeneration.load())
                  goto done;  // the server was restarted or switched off
               // DNS-rebinding guard: a browser page has an Origin; a local MCP client has none.
               if (!origin.empty() && origin.find("://localhost") == std::string::npos && origin.find("://127.0.0.1") == std::string::npos)
               {
                  Respond(fd, 403, "Forbidden", "{\"error\":\"origin not allowed\"}");
                  continue;
               }
               const std::string want = "Bearer " + token;
               bool authed = SameToken(auth, want);
               if (!authed)
               {
                  const size_t q = line1.find("token=");  // ?token= for clients that cannot set headers
                  if (q != std::string::npos)
                  {
                     std::string t = line1.substr(q + 6);
                     t = t.substr(0, t.find_first_of(" &"));
                     authed = SameToken(t, token);
                  }
               }
               if (!authed)
               {
                  Respond(fd, 401, "Unauthorized", "{\"error\":\"missing or wrong token\"}", "WWW-Authenticate: Bearer\r\n");
                  continue;
               }
               if (line1.rfind("POST ", 0) != 0)
               {
                  Respond(fd, line1.rfind("DELETE ", 0) == 0 ? 200 : 405, line1.rfind("DELETE ", 0) == 0 ? "OK" : "Method Not Allowed", "", "Allow: POST\r\n");
                  continue;
               }
               const std::string out = Handle(body);
               if (out.empty())
                  Respond(fd, 202, "Accepted", "");
               else
                  Respond(fd, 200, "OK", out);
               if (closeAfter)
                  goto done;
            }
         }
      done:
         NetCompat::NetClose(fd);
      }

      void AcceptLoop(int listenFd, unsigned generation)
      {
         for (;;)
         {
            const int c = accept(listenFd, nullptr, nullptr);
            if (c < 0 || generation != gGeneration.load())
            {
               if (c >= 0)
                  NetCompat::NetClose(c);
               return;
            }
            std::thread(Serve, c, generation).detach();
         }
      }
   }

   std::string RpcExample(const std::string& method)
   {
      for (const RpcDoc& d : kRpcDocs)
         if (method == d.method)
            return d.example;
      return {};
   }

   std::vector<std::string> RpcMethodNames()
   {
      std::vector<std::string> out;
      for (const RpcDoc& d : kRpcDocs)
         out.push_back(d.method);
      return out;
   }

   nlohmann::json RpcHelp(const std::string& method)
   {
      auto one = [](const RpcDoc& d) { return json{ { "method", d.method }, { "params", d.params }, { "example", json::parse(d.example, nullptr, false) } }; };
      if (method.empty())
      {
         json all = json::array();
         for (const RpcDoc& d : kRpcDocs)
            all.push_back(one(d));
         return all;
      }
      for (const RpcDoc& d : kRpcDocs)
         if (method == d.method)
            return one(d);
      return json();
   }

   nlohmann::json ToolsJson() { return Tools(); }
   nlohmann::json CallToolJson(const std::string& name, const nlohmann::json& args) { return CallTool(name, args); }
   nlohmann::json TextResultJson(const std::string& text, bool error) { return TextResult(text, error); }

   std::string Handle(const std::string& body)
   {
      json req = json::parse(body, nullptr, false);
      if (req.is_discarded())
         return Error(nullptr, -32700, "parse error");
      if (req.is_array())
      {
         json out = json::array();
         for (const json& r : req)
         {
            const std::string s = One(r);
            if (!s.empty())
               out.push_back(json::parse(s));
         }
         return out.empty() ? std::string() : out.dump();
      }
      return One(req);
   }

   std::string NewToken()
   {
      std::random_device rd;
      std::mt19937_64 gen(((uint64_t)rd() << 32) ^ rd());
      static const char kHex[] = "0123456789abcdef";
      std::string t;
      for (int i = 0; i < 32; i++)
         t += kHex[gen() % 16];
      return t;
   }

   Settings Load()
   {
      Settings s;
      std::ifstream f(AppPaths::FsPath(ConfigPath()));
      const json j = f.good() ? json::parse(f, nullptr, false) : json();
      if (j.is_object())
      {
         s.enabled = j.value("enabled", false);
         s.port = j.value("port", ForkIdentity::kMcpPort);
         s.token = j.value("token", std::string());
      }
      if (s.port < 1024 || s.port > 65535)
         s.port = ForkIdentity::kMcpPort;
      if (s.token.size() < 16)
         s.token = NewToken();
      return s;
   }

   void Save(const Settings& s)
   {
      const std::string path = ConfigPath();
      std::ofstream(AppPaths::FsPath(path)) << json{ { "enabled", s.enabled }, { "port", s.port }, { "token", s.token } }.dump(2) << "\n";
#ifndef _WIN32
      chmod(path.c_str(), 0600);
#endif
   }

   bool Running() { return gListenFd.load() >= 0; }
   int BoundPort() { return gPort.load(); }
   std::string Url(const Settings& s) { return "http://127.0.0.1:" + std::to_string(s.port) + "/mcp"; }

   std::string Apply(const Settings& s)
   {
      gGeneration++;
      const int old = gListenFd.exchange(-1);
      if (old >= 0)
      {
         // Wake the blocked accept(): connect once, then close.
         NetCompat::NetClose(old);
      }
      gPort = 0;
      {
         std::lock_guard<std::mutex> lock(gMutex);
         gToken = s.token;
      }
      if (!s.enabled)
         return {};
      const int fd = socket(AF_INET, SOCK_STREAM, 0);
      if (fd < 0)
         return "could not create a socket";
      int yes = 1;
      NetCompat::NetSetSockOpt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
      sockaddr_in addr{};
      addr.sin_family = AF_INET;
      addr.sin_port = htons((uint16_t)s.port);
      addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      if (bind(fd, (sockaddr*)&addr, sizeof addr) != 0 || listen(fd, 8) != 0)
      {
         NetCompat::NetClose(fd);
         return "port " + std::to_string(s.port) + " is taken; pick another";
      }
      gListenFd = fd;
      gPort = s.port;
      std::thread(AcceptLoop, fd, gGeneration.load()).detach();
      return {};
   }

   void StartFromSettings()
   {
      const Settings s = Load();
      if (s.enabled)
         Apply(s);
   }

   std::string SelfCheck()
   {
      if (!Running())
         return "off";
      const int fd = socket(AF_INET, SOCK_STREAM, 0);
      sockaddr_in addr{};
      addr.sin_family = AF_INET;
      addr.sin_port = htons((uint16_t)gPort.load());
      addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      if (fd < 0 || connect(fd, (sockaddr*)&addr, sizeof addr) != 0)
      {
         if (fd >= 0)
            NetCompat::NetClose(fd);
         return "cannot connect";
      }
      std::string token;
      {
         std::lock_guard<std::mutex> lock(gMutex);
         token = gToken;
      }
      const std::string body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}";
      const std::string req = "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nAuthorization: Bearer " + token + "\r\nContent-Length: " +
                              std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
      timeval tv{ 2, 0 };  // never wait longer than this for the answer
      NetCompat::NetSetSockOpt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
      NetCompat::NetSend(fd, req.data(), req.size(), 0);
      std::string resp;
      char c[4096];
      NetSsize n;
      while ((n = NetCompat::NetRecv(fd, c, sizeof c, 0)) > 0)
         resp.append(c, (size_t)n);
      NetCompat::NetClose(fd);
      return resp.find("infinite_context") != std::string::npos ? std::string() : "no answer";
   }

   std::string AgentInstructions(const Settings& s, const std::string& exePath)
   {
      std::string t = kAgentDoc;
      auto rep = [&](const std::string& k, const std::string& v)
      {
         for (size_t p = 0; (p = t.find(k, p)) != std::string::npos; p += v.size())
            t.replace(p, k.size(), v);
      };
      rep("{URL}", Url(s));
      rep("{TOKEN}", s.token);
      rep("{EXE}", exePath);
      rep("{CONTROL_PORT}", std::to_string(RemoteControl::Port()));
      return t;
   }
}
