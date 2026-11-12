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

      const char* kRpcMethods[] = { "attention", "auto_wire_inputs", "batch", "bind_mod", "code_errors", "connect", "create_node", "delete_node",
                                    "describe", "disconnect", "edit", "expand_node", "explain", "get_graph", "get_node_position", "get_params",
                                    "list_node_types", "list_nodes", "load_patch", "load_patch_text", "new_patch", "redo", "save_patch", "select",
                                    "selection", "set_bypass", "set_expr", "set_node_position", "set_param", "settings", "status", "transport",
                                    "undo", "unbind_mod", "validate_patch_text", "view" };

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
            { { "name", "infinite_rpc_methods" }, { "description", "Method names infinite_rpc accepts." }, { "inputSchema", none } },
         });
      }

      std::string ShellQuote(const std::string& s)
      {
         std::string o = "'";
         for (char c : s)
            o += c == '\'' ? std::string("'\\''") : std::string(1, c);
         return o + "'";
      }

      // Runs `<this binary> --cli args...` with the control port and token in its environment.
      bool RunCli(const std::vector<std::string>& args, std::string& out)
      {
#ifdef _WIN32
         (void)args;
         out = "infinite_cli is not available on Windows yet (not verified); use infinite_rpc.";
         return false;
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
         const int rc = pclose(p);
         return rc == 0;
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
            const bool ok = RunCli(a, out);
            return TextResult(out.empty() ? (ok ? "ok" : "failed") : out, !ok);
         }
         if (name == "infinite_rpc_methods")
         {
            json m = json::array();
            for (const char* x : kRpcMethods)
               m.push_back(x);
            return TextResult(m.dump(), false);
         }
         if (name == "infinite_rpc")
         {
            if (!args.is_object() || !args.value("method", std::string()).size())
               return TextResult("infinite_rpc needs a method", true);
            bool known = false;
            for (const char* x : kRpcMethods)
               known = known || args["method"] == x;
            if (!known)
               return TextResult("unknown or not allowed method; see infinite_rpc_methods", true);
            json result;
            std::string err;
            if (!RemoteControl::Call(args["method"], args.value("params", json::object()), result, err))
               return TextResult(err, true);
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
