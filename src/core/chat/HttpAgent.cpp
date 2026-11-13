#include "HttpAgent.h"

#include "core/McpHttp.h"
#include "platform/AgentNet.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>

using json = nlohmann::json;

namespace chat
{
   namespace
   {
      std::mutex gMemoryMutex;
      std::map<std::string, json> gMemory;  // session id -> messages array

      std::string TrimRight(std::string s)
      {
         while (!s.empty() && (s.back() == '/' || s.back() == ' '))
            s.pop_back();
         return s;
      }

      bool ValidName(const std::string& n)
      {
         if (n.empty() || std::isdigit((unsigned char)n[0]))
            return false;
         for (char c : n)
            if (!std::isalnum((unsigned char)c) && c != '_')
               return false;
         return true;
      }

      std::string ShortError(const std::string& body)
      {
         const json j = json::parse(body, nullptr, false);
         if (j.is_object())
         {
            if (j.contains("error") && j["error"].is_object() && j["error"].contains("message") && j["error"]["message"].is_string())
               return j["error"]["message"].get<std::string>();
            if (j.contains("error") && j["error"].is_string())
               return j["error"].get<std::string>();
            if (j.contains("message") && j["message"].is_string())
               return j["message"].get<std::string>();
         }
         return body.substr(0, 300);
      }

      bool MentionsTools(const std::string& body)
      {
         std::string l = body;
         for (char& c : l)
            c = (char)std::tolower((unsigned char)c);
         return l.find("tool") != std::string::npos || l.find("function") != std::string::npos;
      }
   }

   std::string ChatUrl(const std::string& baseUrl) { return TrimRight(baseUrl) + "/chat/completions"; }

   std::string MaskKey(const std::string& key)
   {
      if (key.empty())
         return {};
      if (key.size() <= 8)
         return "…";
      return key.substr(0, 3) + "…" + key.substr(key.size() - 4);
   }

   std::string ResolveKey(const std::string& keyEnv, std::string& source, bool askShell)
   {
      source.clear();
      std::string key;
      if (AgentNet::SecretGet(kKeyService, kKeyAccount, key) && !key.empty())
      {
         source = "Keychain";
         return key;
      }
      if (!ValidName(keyEnv))
         return {};
      if (const char* v = std::getenv(keyEnv.c_str()); v != nullptr && v[0] != '\0')
      {
         source = "$" + keyEnv;
         return v;
      }
      if (!askShell)
         return {};
#ifndef _WIN32
      // A GUI app does not see what ~/.zprofile exports: ask the login shell, as agent CLIs are started.
      const char* shell = std::getenv("SHELL");
      const std::string cmd = std::string(shell != nullptr && shell[0] != '\0' ? shell : "/bin/sh") + " -l -c 'printenv " + keyEnv + "' 2>/dev/null </dev/null";
      if (FILE* p = popen(cmd.c_str(), "r"))
      {
         char buf[512];
         std::string out;
         while (fgets(buf, sizeof buf, p) != nullptr)
            out += buf;
         pclose(p);
         while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
            out.pop_back();
         if (!out.empty())
         {
            source = "$" + keyEnv;
            return out;
         }
      }
#endif
      return {};
   }

   void SseChat::Feed(const char* data, size_t n)
   {
      mPending.append(data, n);
      size_t nl;
      while ((nl = mPending.find('\n')) != std::string::npos)
      {
         std::string line = mPending.substr(0, nl);
         mPending.erase(0, nl + 1);
         if (!line.empty() && line.back() == '\r')
            line.pop_back();
         Line(line);
      }
   }

   void SseChat::Line(const std::string& line)
   {
      if (line.rfind("data:", 0) != 0)
         return;  // comments (": keep-alive"), event: lines
      size_t s = 5;
      while (s < line.size() && line[s] == ' ')
         s++;
      const std::string payload = line.substr(s);
      if (payload == "[DONE]")
      {
         done = true;
         return;
      }
      const json j = json::parse(payload, nullptr, false);
      if (!j.is_object())
         return;
      if (j.contains("error"))
      {
         error = j["error"].is_object() ? j["error"].value("message", std::string("error")) : j["error"].dump();
         return;
      }
      if (j.contains("usage") && j["usage"].is_object())
      {
         const json& u = j["usage"];
         usage.input = u.value("prompt_tokens", 0LL);
         usage.output = u.value("completion_tokens", 0LL);
         if (u.contains("prompt_tokens_details") && u["prompt_tokens_details"].is_object())
         {
            usage.cached = u["prompt_tokens_details"].value("cached_tokens", 0LL);
            usage.input -= usage.cached;
         }
      }
      if (!j.contains("choices") || !j["choices"].is_array() || j["choices"].empty())
         return;
      const json& c = j["choices"][0];
      if (c.contains("finish_reason") && c["finish_reason"].is_string())
         finish = c["finish_reason"].get<std::string>();
      if (!c.contains("delta") || !c["delta"].is_object())
         return;
      const json& d = c["delta"];
      if (d.contains("content") && d["content"].is_string())
         text += d["content"].get<std::string>();
      if (d.contains("tool_calls") && d["tool_calls"].is_array())
         for (const json& t : d["tool_calls"])
         {
            const size_t idx = t.value("index", 0);
            if (idx > 32)
               continue;
            if (calls.size() <= idx)
               calls.resize(idx + 1);
            if (t.contains("id") && t["id"].is_string())
               calls[idx].id = t["id"].get<std::string>();
            if (t.contains("function") && t["function"].is_object())
            {
               const json& f = t["function"];
               if (f.contains("name") && f["name"].is_string())
                  calls[idx].name += f["name"].get<std::string>();
               if (f.contains("arguments") && f["arguments"].is_string())
                  calls[idx].args += f["arguments"].get<std::string>();
            }
         }
   }

   json OpenAiTools()
   {
      json out = json::array();
      for (const json& t : McpHttp::ToolsJson())
         out.push_back({ { "type", "function" },
                         { "function", { { "name", t["name"] }, { "description", t["description"] }, { "parameters", t["inputSchema"] } } } });
      return out;
   }

   std::string DescribeCall(const std::string& name, const json& args)
   {
      if (name == "infinite_cli" && args.is_object() && args.contains("args") && args["args"].is_array())
      {
         std::string s = "infinite";
         for (const json& a : args["args"])
            s += " " + (a.is_string() ? a.get<std::string>() : a.dump());
         return s;
      }
      if (name == "infinite_context")
         return "infinite context";
      return name + (args.is_object() && !args.empty() ? " " + args.dump() : std::string());
   }

   void RunHttpTurn(const HttpAgentConfig& cfg, const std::string& session, bool resume, const std::string& prompt,
                    const std::function<void(const AgentEvent&)>& emit, const std::function<bool()>& cancelled)
   {
      auto end = [&](bool error, const std::string& text, const TurnUsage& u = TurnUsage())
      {
         AgentEvent e;
         e.kind = AgentEvent::Kind::End;
         e.error = error;
         e.text = text;
         e.usage = u;
         emit(e);
      };
      AgentEvent se;
      se.kind = AgentEvent::Kind::Session;
      se.text = session;
      emit(se);

      if (cfg.baseUrl.rfind("http://", 0) != 0 && cfg.baseUrl.rfind("https://", 0) != 0)
         return end(true, "Set a Base URL (http:// or https://) in Settings > Agents & Terminal.");
      if (cfg.model.empty())
         return end(true, "Set a Model ID in Settings > Agents & Terminal.");

      std::string source;
      const std::string key = ResolveKey(cfg.keyEnv, source);
      json messages = json::array();
      if (resume)
      {
         std::lock_guard<std::mutex> g(gMemoryMutex);
         if (gMemory.count(session))
            messages = gMemory[session];
      }
      messages.push_back({ { "role", "user" }, { "content", prompt } });

      bool useTools = cfg.tools;
      bool toldNoTools = false;
      TurnUsage total;
      for (int step = 0; step < cfg.maxSteps; step++)
      {
         if (cancelled())
            return end(true, "stopped");
         json body = { { "model", cfg.model }, { "messages", messages }, { "stream", true }, { "stream_options", { { "include_usage", true } } } };
         if (useTools)
            body["tools"] = OpenAiTools();
         AgentNet::Request rq;
         rq.url = ChatUrl(cfg.baseUrl);
         rq.headers = { { "Content-Type", "application/json" }, { "Accept", "text/event-stream" } };
         if (!key.empty())
            rq.headers.push_back({ "Authorization", "Bearer " + key });
         rq.body = body.dump();
         rq.timeoutSeconds = 180;
         SseChat sse;
         std::string raw;  // the body of a non-SSE answer (an error)
         int status = 0;
         std::string err;
         const bool ok = AgentNet::Post(rq, [&](const char* p, size_t n)
                                        {
                                           if (cancelled())
                                              return false;
                                           if (raw.size() < 65536)
                                              raw.append(p, n);
                                           sse.Feed(p, n);
                                           return true;
                                        }, status, err);
         if (cancelled())
            return end(true, "stopped");
         if (!ok)
            return end(true, "Could not reach " + cfg.baseUrl + ": " + err);
         if (status < 200 || status >= 300)
         {
            const std::string why = ShortError(raw);
            if (useTools && (status == 400 || status == 404 || status == 422) && MentionsTools(raw))
            {
               useTools = false;  // this model or server has no tool calling: chat only
               if (!toldNoTools)
               {
                  toldNoTools = true;
                  AgentEvent t;
                  t.kind = AgentEvent::Kind::Text;
                  t.text = "(This model does not support tools, so it can talk but cannot change the patch.)";
                  emit(t);
               }
               step--;
               continue;
            }
            if (status == 401 || status == 403)
               return end(true, "The server refused the API key (" + std::to_string(status) + "): " + why + (source.empty() ? " No key is set: add one in Settings." : ""));
            return end(true, "The server answered " + std::to_string(status) + ": " + why);
         }
         if (!sse.error.empty())
            return end(true, sse.error);
         total.input += sse.usage.input;
         total.cached += sse.usage.cached;
         total.output += sse.usage.output;

         // A server that ignored stream:true answers one JSON object: take its message.
         if (sse.text.empty() && sse.calls.empty() && !raw.empty() && raw[0] == '{')
         {
            const json j = json::parse(raw, nullptr, false);
            if (j.is_object() && j.contains("choices") && j["choices"].is_array() && !j["choices"].empty())
            {
               const json& m = j["choices"][0].value("message", json::object());
               if (m.contains("content") && m["content"].is_string())
                  sse.text = m["content"].get<std::string>();
               if (m.contains("tool_calls") && m["tool_calls"].is_array())
                  for (const json& t : m["tool_calls"])
                  {
                     SseChat::Call c;
                     c.id = t.value("id", std::string());
                     c.name = t["function"].value("name", std::string());
                     c.args = t["function"].value("arguments", std::string());
                     sse.calls.push_back(c);
                  }
            }
         }

         if (sse.calls.empty())
         {
            messages.push_back({ { "role", "assistant" }, { "content", sse.text } });
            {
               std::lock_guard<std::mutex> g(gMemoryMutex);
               gMemory[session] = messages;
            }
            AgentEvent t;
            t.kind = AgentEvent::Kind::Text;
            t.text = sse.text.empty() ? "(no answer)" : sse.text;
            emit(t);
            return end(false, "", total);
         }

         json calls = json::array();
         for (size_t i = 0; i < sse.calls.size(); i++)
         {
            if (sse.calls[i].id.empty())
               sse.calls[i].id = "call_" + std::to_string(step) + "_" + std::to_string(i);
            calls.push_back({ { "id", sse.calls[i].id }, { "type", "function" },
                              { "function", { { "name", sse.calls[i].name }, { "arguments", sse.calls[i].args.empty() ? "{}" : sse.calls[i].args } } } });
         }
         messages.push_back({ { "role", "assistant" }, { "content", sse.text.empty() ? json(nullptr) : json(sse.text) }, { "tool_calls", calls } });
         for (const SseChat::Call& c : sse.calls)
         {
            const json args = json::parse(c.args.empty() ? "{}" : c.args, nullptr, false);
            AgentEvent te;
            te.kind = AgentEvent::Kind::Tool;
            te.text = DescribeCall(c.name, args.is_discarded() ? json::object() : args);
            emit(te);
            json r = args.is_discarded() ? McpHttp::TextResultJson("The arguments were not valid JSON.", true) : McpHttp::CallToolJson(c.name, args);
            const bool isErr = r.value("isError", false);
            const std::string out = r["content"][0].value("text", std::string());
            AgentEvent re;
            re.kind = AgentEvent::Kind::ToolResult;
            re.text = out;
            re.error = isErr;
            emit(re);
            messages.push_back({ { "role", "tool" }, { "tool_call_id", c.id }, { "content", out.size() > 60000 ? out.substr(0, 60000) + "\n…(cut)" : out } });
         }
      }
      {
         std::lock_guard<std::mutex> g(gMemoryMutex);
         gMemory[session] = messages;
      }
      end(true, "Stopped after " + std::to_string(cfg.maxSteps) + " steps without a final answer.", total);
   }

   ConnectionResult TestConnection(const HttpAgentConfig& cfg)
   {
      ConnectionResult r;
      if (cfg.baseUrl.rfind("http://", 0) != 0 && cfg.baseUrl.rfind("https://", 0) != 0)
      {
         r.message = "Base URL must start with http:// or https://";
         return r;
      }
      if (cfg.model.empty())
      {
         r.message = "Set a Model ID first.";
         return r;
      }
      std::string source;
      const std::string key = ResolveKey(cfg.keyEnv, source);
      AgentNet::Request rq;
      rq.url = ChatUrl(cfg.baseUrl);
      rq.headers = { { "Content-Type", "application/json" } };
      if (!key.empty())
         rq.headers.push_back({ "Authorization", "Bearer " + key });
      rq.body = json{ { "model", cfg.model }, { "max_tokens", 8 }, { "messages", json::array({ { { "role", "user" }, { "content", "Reply with: ok" } } }) } }.dump();
      rq.timeoutSeconds = 30;
      std::string raw, err;
      int status = 0;
      if (!AgentNet::Post(rq, [&](const char* p, size_t n) { if (raw.size() < 65536) raw.append(p, n); return true; }, status, err))
      {
         r.message = "Cannot reach the server: " + err;
         return r;
      }
      if (status >= 200 && status < 300)
      {
         r.ok = true;
         r.message = "Connected. Model answered" + std::string(source.empty() ? " (no API key sent)." : " (key from " + source + ").");
         return r;
      }
      r.message = "Server answered " + std::to_string(status) + ": " + ShortError(raw);
      if ((status == 401 || status == 403) && source.empty())
         r.message += " No API key is set.";
      return r;
   }
}
