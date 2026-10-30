#include "InfiniteCli.h"

#include "AISkillContent.h"
#include "core/NodeFactory.h"
#include "Patch.h"
#include "PatchLayout.h"
#include "TermDocs.h"
#include "core/sketch/Sketch3DPresets.h"
#include "core/sketch/SketchPresets.h"
#include "json.hpp"
#include "nodes/FieldElementNode.h"
#include "nodes/FieldPixelNode.h"
#include "nodes/FieldPrimitiveNode.h"
#include "nodes/FieldSynthNode.h"
#include "nodes/FormulaNode.h"
#include "platform/AppPaths.h"
#include "platform/NetCompat.h"
#include "platform/Platform.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace app
{
   std::string BundledResourcePath(const char* relPath);
}

namespace app
{
   void RegisterNodes();
}

namespace InfiniteCli
{
namespace
{
   using json = nlohmann::json;
   using Args = std::vector<std::string>;

   constexpr int kExitFail = 1;
   constexpr int kExitUsage = 2;
   constexpr int kExitNoApp = 7;
   constexpr int kDefaultPort = 7777;

   struct Failure
   {
      std::string message;
      int code = kExitFail;
   };

   [[noreturn]] void Fail(const std::string& message, int code = kExitFail)
   {
      throw Failure{ message, code };
   }

   std::string ReadFile(const std::string& path)
   {
      std::ifstream in(AppPaths::FsPath(path), std::ios::binary);
      if (!in)
         Fail("cannot read " + path);
      std::stringstream text;
      text << in.rdbuf();
      return text.str();
   }

   void WriteFile(const std::string& path, const std::string& text)
   {
      std::ofstream out(AppPaths::FsPath(path), std::ios::binary);
      if (!out)
         Fail("cannot write " + path);
      out << text;
   }

   bool IsPatchFile(const std::string& arg)
   {
      return arg.size() > 4 && arg.compare(arg.size() - 4, 4, ".inf") == 0;
   }

   void Print(const json& value)
   {
      if (value.is_string())
         printf("%s\n", value.get<std::string>().c_str());
      else
         printf("%s\n", value.dump().c_str());
   }

   // Removes `--name value` from args. False when absent.
   bool TakeOption(Args& args, const std::string& name, std::string& value)
   {
      for (size_t i = 0; i + 1 < args.size(); i++)
         if (args[i] == name)
         {
            value = args[i + 1];
            args.erase(args.begin() + (std::ptrdiff_t)i, args.begin() + (std::ptrdiff_t)i + 2);
            return true;
         }
      return false;
   }

   bool TakeFlag(Args& args, const std::string& name)
   {
      const auto it = std::find(args.begin(), args.end(), name);
      if (it == args.end())
         return false;
      args.erase(it);
      return true;
   }

   // ---- This binary's headless flags --------------------------------------------------------

   struct Run
   {
      int code = 0;
      std::string out;
   };

   std::string SelfPath()
   {
      std::string exe = Platform::ExecutablePath();
#if defined(_WIN32)
      // The console front door, so the child's output and exit code reach us.
      if (const size_t dot = exe.find_last_of('.'); dot != std::string::npos)
         exe = exe.substr(0, dot) + ".com";
#endif
      return exe;
   }

   // Runs this binary with `args`. capture false lets its output through.
   Run RunSelf(const Args& args, bool capture)
   {
      Run result;
      const std::string exe = SelfPath();
      fflush(stdout);
#if defined(_WIN32)
      std::string line = "\"" + exe + "\"";
      for (const std::string& arg : args)
      {
         line += " \"";
         for (char c : arg)
            line += c == '"' ? std::string("\\\"") : std::string(1, c);
         line += "\"";
      }
      FILE* pipe = _wpopen(AppPaths::FsPath("\"" + line + "\"").c_str(), L"r");
      if (pipe == nullptr)
         Fail("cannot start " + exe);
      char chunk[4096];
      size_t got;
      while ((got = fread(chunk, 1, sizeof chunk, pipe)) > 0)
      {
         if (capture)
            result.out.append(chunk, got);
         else
            fwrite(chunk, 1, got, stdout);
      }
      result.code = _pclose(pipe);
#else
      int fds[2] = { -1, -1 };
      if (capture && pipe(fds) != 0)
         Fail("cannot open a pipe");
      const pid_t child = fork();
      if (child < 0)
         Fail("cannot start " + exe);
      if (child == 0)
      {
         if (capture)
         {
            dup2(fds[1], 1);
            close(fds[0]);
            close(fds[1]);
         }
         std::vector<char*> argv;
         argv.push_back(const_cast<char*>(exe.c_str()));
         for (const std::string& arg : args)
            argv.push_back(const_cast<char*>(arg.c_str()));
         argv.push_back(nullptr);
         execv(exe.c_str(), argv.data());
         _exit(127);
      }
      if (capture)
      {
         close(fds[1]);
         char chunk[4096];
         ssize_t got;
         while ((got = read(fds[0], chunk, sizeof chunk)) > 0)
            result.out.append(chunk, (size_t)got);
         close(fds[0]);
      }
      int status = 0;
      waitpid(child, &status, 0);
      result.code = WIFEXITED(status) ? WEXITSTATUS(status) : kExitFail;
#endif
      return result;
   }

   int PassThrough(const Args& args)
   {
      return RunSelf(args, false).code;
   }

   // The headless result: the last line printed is one JSON object.
   json LastJson(const std::string& out)
   {
      size_t end = out.find_last_not_of("\r\n");
      if (end == std::string::npos)
         return json::object();
      const size_t start = out.find_last_of('\n', end);
      const std::string line = out.substr(start == std::string::npos ? 0 : start + 1, end - (start == std::string::npos ? 0 : start + 1) + 1);
      return json::parse(line, nullptr, false);
   }

   // ---- The running app ---------------------------------------------------------------------

   class Live
   {
   public:
      // False when no app answers. Never fails hard: a headless route may still apply.
      bool Connect()
      {
         if (mFd >= 0)
            return true;
         int port = kDefaultPort;
         std::string token;
         const char* envPort = getenv("INFINITE_CONTROL_PORT");
         const char* envToken = getenv("INFINITE_CONTROL_TOKEN");
         if (envPort != nullptr)
            port = atoi(envPort);
         if (envToken != nullptr)
            token = envToken;
         else
         {
            std::ifstream in(AppPaths::FsPath(AppPaths::AppSupportDir() + "/control_token"));
            std::getline(in, token);
         }
         if (token.empty() || port <= 0)
            return false;
         const int fd = (int)socket(AF_INET, SOCK_STREAM, 0);
         if (fd < 0)
            return false;
         sockaddr_in addr{};
         addr.sin_family = AF_INET;
         addr.sin_port = htons((uint16_t)port);
         addr.sin_addr.s_addr = inet_addr("127.0.0.1");
         if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
         {
            NetCompat::NetClose(fd);
            return false;
         }
         mFd = fd;
         mToken = token;
         return true;
      }

      json Call(const std::string& method, const json& params = json::object(), int attempt = 0)
      {
         if (!Connect())
            Fail("no running Infinite answered. Open the app, or give a patch file for the headless route", kExitNoApp);
         const std::string line = json{ { "jsonrpc", "2.0" }, { "id", ++mNextId }, { "method", method },
                                        { "params", params }, { "token", mToken } }.dump() + "\n";
         if (NetCompat::NetSend(mFd, line.data(), line.size(), 0) < 0)
            Fail("the app closed the connection");
         size_t newline;
         char chunk[65536];
         while ((newline = mBuffer.find('\n')) == std::string::npos)
         {
            const NetSsize got = NetCompat::NetRecv(mFd, chunk, sizeof chunk, 0);
            if (got <= 0)
               Fail("the app closed the connection");
            mBuffer.append(chunk, (size_t)got);
         }
         const json reply = json::parse(mBuffer.substr(0, newline));
         mBuffer.erase(0, newline + 1);
         if (reply.contains("error"))
         {
            const std::string message = reply["error"].value("message", std::string("the app refused the call"));
            if (attempt < 10 && message.find("retry on the next frame") != std::string::npos)
            {
               std::this_thread::sleep_for(std::chrono::milliseconds(50));
               return Call(method, params, attempt + 1);
            }
            Fail(message);
         }
         return reply.value("result", json::object());
      }

   private:
      int mFd = -1;
      int mNextId = 0;
      std::string mToken, mBuffer;
   };

   Live gLive;

   // A node by index, `selected`, or its title or type when exactly one node has it.
   int NodeRef(const std::string& ref)
   {
      if (!ref.empty() && std::all_of(ref.begin(), ref.end(), [](char c) { return c >= '0' && c <= '9'; }))
         return atoi(ref.c_str());
      if (ref == "selected" || ref == "sel")
      {
         const json nodes = gLive.Call("selection")["nodes"];
         if (nodes.empty())
            Fail("no node is selected");
         return nodes[0].value("index", -1);
      }
      std::vector<int> hits;
      const json nodes = gLive.Call("list_nodes")["nodes"];
      for (const json& n : nodes)
         if (n.value("title", std::string()) == ref)
            hits.push_back(n.value("index", -1));
      if (hits.empty())
         for (const json& n : nodes)
            if (n.value("type", std::string()) == ref)
               hits.push_back(n.value("index", -1));
      if (hits.size() == 1)
         return hits[0];
      Fail(hits.empty() ? "no node named '" + ref + "'" : "'" + ref + "' matches " + std::to_string(hits.size()) + " nodes; use the index");
   }

   // "4" or "4:1": a node and a pin number.
   void SplitPin(const std::string& text, std::string& node, int& pin, int fallback)
   {
      const size_t colon = text.rfind(':');
      node = colon == std::string::npos ? text : text.substr(0, colon);
      pin = colon == std::string::npos ? fallback : atoi(text.c_str() + colon + 1);
   }

   // ---- Code ---------------------------------------------------------------------------------

   enum class Output { Picture, Sound, Geometry, Notes };

   struct Language
   {
      const char* kind;      // `code new` and --type
      const char* type;      // node type
      const char* category;  // node category
      const char* key;       // the text control holding the code
      const char* extension;
      Output output;
      bool gpuOnly;          // compiles only with a GL context, so a check must draw a frame
      const char* starter;
   };

   const Language kLanguages[] = {
      { "field-pixel", "FieldPixel", "Source", "code", ".field", Output::Picture, false,
        "# Field Pixel: runs once per pixel. uv is 0..1, col is the output colour.\nparam float speed = 0.5 [0, 4]\ncol = vec3(uv.x, uv.y, 0.5 + 0.5 * sin(t * speed * 6.28))\n" },
      { "field-synth", "Field Synth", "Synths", "code", ".synth.field", Output::Sound, false,
        "# Field Synth: runs once per sample. out is the audio output.\nparam float pitch = 220 [20, 2000]\nstate float phase = 0\nphase = fract(phase + pitch / sr)\nout = sin(phase * 6.28318) * 0.3\n" },
      { "field-modifier", "Field Modifier", "3D", "code", ".modifier.field", Output::Geometry, false,
        "# Field Modifier: runs once per point. P is the position.\nparam float amount = 0.2 [0, 1]\nP.y += sin(P.x * 4 + t) * amount\n" },
      { "field-primitive", "Field Primitive", "3D", "code", ".primitive.field", Output::Geometry, false, "" },
      { "sketch", "Sketch", "Source", "code", ".sketch.js", Output::Picture, false,
        "// Sketch: draw(t) runs each frame. Every param() is a knob you can modulate.\nparam(\"size\", 240, 10, 600);\n\nfunction draw(t) {\n  background(0.04);\n  fill(hsl(t * 0.1, 0.7, 0.6));\n  noStroke();\n  circle(width / 2, height / 2, size * (1 + 0.2 * Math.sin(t * 2)));\n}\n" },
      { "sketch3d", "Sketch 3D", "3D", "code", ".sketch3d.js", Output::Geometry, false,
        "// Sketch 3D: draw(t) builds a mesh. Feed the node into Render 3D to see it.\nparam(\"count\", 12, 1, 48);\n\nfunction draw(t) {\n  for (let i = 0; i < count; i++) {\n    push();\n    rotateY(TAU * i / count + t * 0.3);\n    translate(1.2, 0, 0);\n    fill(hsl(i / count, 0.7, 0.6));\n    box(0.25);\n    pop();\n  }\n}\n" },
      { "shadertoy", "Shadertoy", "Source", "stImage", ".glsl", Output::Picture, true,
        "// Shadertoy: the Image pass. iTime, iResolution, iChannel0-3 come from the preamble.\nuniform float speed; // 0..4 = 1\n\nvoid mainImage(out vec4 fragColor, in vec2 fragCoord) {\n  vec2 uv = fragCoord / iResolution.xy;\n  fragColor = vec4(uv, 0.5 + 0.5 * sin(iTime * speed), 1.0);\n}\n" },
      { "formula", "Formula", "Source", "formula", ".formula", Output::Picture, true, "" },
      { "equation", "Equation Synth", "Synths", "formula", ".equation", Output::Sound, false, "" },
   };

   const Language* LanguageByKind(const std::string& kind)
   {
      for (const Language& l : kLanguages)
         if (kind == l.kind)
            return &l;
      return nullptr;
   }

   const Language* LanguageByType(const std::string& type)
   {
      for (const Language& l : kLanguages)
         if (type == l.type)
            return &l;
      return nullptr;
   }

   const Language& LanguageOf(const std::string& path, const std::string& forced)
   {
      if (!forced.empty())
      {
         if (const Language* l = LanguageByKind(forced))
            return *l;
         Fail("unknown code kind '" + forced + "'; see `infinite code kinds`", kExitUsage);
      }
      // The longest match wins, so a.sketch3d.js is not read as plain JS.
      const Language* best = nullptr;
      for (const Language& l : kLanguages)
      {
         const std::string ext = l.extension;
         if (path.size() >= ext.size() && path.compare(path.size() - ext.size(), ext.size(), ext) == 0 &&
             (best == nullptr || ext.size() > std::string(best->extension).size()))
            best = &l;
      }
      if (best == nullptr)
         Fail("cannot tell the language of " + path + "; add --type <kind>", kExitUsage);
      return *best;
   }

   std::string Escape(const std::string& value)
   {
      std::string clean;
      for (char c : value)
      {
         if (c == '\\')
            clean += "\\\\";
         else if (c == '\n')
            clean += "\\n";
         else if (c != '\r')
            clean += c;
      }
      return clean;
   }

   std::string TempPath(const std::string& name)
   {
      std::error_code ec;
      const auto dir = std::filesystem::temp_directory_path(ec) / "infinite-cli";
      std::filesystem::create_directories(dir, ec);
#if defined(_WIN32)
      const int pid = _getpid();
#else
      const int pid = getpid();
#endif
      return (dir / (std::to_string(pid) + "-" + name)).u8string();
   }

   // A one-node patch around the code, so a check runs the same compile as the app.
   std::string WrapInPatch(const Language& l, const std::string& code, const std::string& passKey)
   {
      return "infinite-patch 1\nnode 1 " + std::string(l.category) + " " + l.type + "\n  s " +
             (passKey.empty() ? std::string(l.key) : passKey) + " " + Escape(code) + "\nend\n";
   }

   json IssueShape(const json& issue)
   {
      const std::string message = issue.value("message", std::string());
      json out = { { "code", issue.value("code", std::string()) }, { "message", message }, { "hint", issue.value("hint", std::string()) } };
      std::smatch m;
      static const std::regex kAt("(?:line |:)(\\d+)(?::(\\d+))?");
      if (std::regex_search(message, m, kAt))
      {
         out["line"] = atoi(m[1].str().c_str());
         out["col"] = m[2].matched ? atoi(m[2].str().c_str()) : 0;
      }
      else
      {
         out["line"] = 0;
         out["col"] = 0;
      }
      return out;
   }

   json CheckCode(const Language& l, const std::string& code, const std::string& passKey, bool look)
   {
      const std::string patch = TempPath("check.inf");
      const std::string png = TempPath("check.png");
      WriteFile(patch, WrapInPatch(l, code, passKey));
      json result = { { "ok", true }, { "language", l.kind }, { "errors", json::array() } };

      const auto absorb = [&](const Run& run)
      {
         const json reply = LastJson(run.out);
         if (run.code != 0 || !reply.value("ok", false))
         {
            result["ok"] = false;
            if (reply.is_object() && reply.contains("errors"))
               for (const json& issue : reply["errors"])
                  if (issue.value("code", std::string()).rfind("E_", 0) == 0 || result["errors"].empty())
                     result["errors"].push_back(IssueShape(issue));
         }
         return reply;
      };

      absorb(RunSelf({ "--validate", patch, "--lenient" }, true));
      if (result["ok"].get<bool>() && (l.gpuOnly || look) && l.output == Output::Picture)
      {
         const json reply = absorb(RunSelf({ "--frame", patch, "0.5", png, "--node", "1" }, true));
         if (reply.is_object() && reply.contains("frame_stats"))
            result["frame_stats"] = reply["frame_stats"];
         if (look && result["ok"].get<bool>())
            result["file"] = png;
      }
      std::error_code ec;
      std::filesystem::remove(AppPaths::FsPath(patch), ec);
      return result;
   }

   std::string PassKey(const std::string& pass)
   {
      static const std::map<std::string, std::string> keys = {
         { "image", "stImage" }, { "bufa", "stBufA" }, { "bufb", "stBufB" }, { "bufc", "stBufC" }, { "bufd", "stBufD" }, { "common", "stCommon" }
      };
      if (pass.empty())
         return std::string();
      const auto it = keys.find(pass);
      if (it == keys.end())
         Fail("unknown pass '" + pass + "' (image, bufa, bufb, bufc, bufd, common)", kExitUsage);
      return it->second;
   }

   struct Example
   {
      std::string name, code;
   };

   template <typename Node>
   void AddPresets(std::vector<Example>& out)
   {
      for (const auto& p : Node::Presets())
         out.push_back({ p.name, p.code });
   }

   std::vector<Example> Examples(const std::string& kind)
   {
      std::vector<Example> out;
      int n = 0;
      if (kind == "sketch")
      {
         const SketchPresets::Entry* e = SketchPresets::All(n);
         for (int i = 0; i < n; i++)
            out.push_back({ e[i].name, e[i].code });
      }
      else if (kind == "sketch3d")
      {
         const Sketch3DPresets::Entry* e = Sketch3DPresets::All(n);
         for (int i = 0; i < n; i++)
            out.push_back({ e[i].name, e[i].code });
      }
      else if (kind == "field-pixel")
         AddPresets<FieldPixelNode>(out);
      else if (kind == "field-synth")
         AddPresets<FieldSynthNode>(out);
      else if (kind == "field-modifier")
         AddPresets<FieldElementNode>(out);
      else if (kind == "field-primitive")
         AddPresets<FieldPrimitiveNode>(out);
      else if (kind == "formula")
         AddPresets<FormulaNode>(out);
      return out;
   }

   // The `s <key>` line of one node in patch text, escaped. Empty when absent.
   std::string PatchNodeCode(const std::string& patch, int index, const std::string& key)
   {
      std::istringstream in(patch);
      std::string line;
      bool inNode = false;
      const std::string head = "node " + std::to_string(index) + " ";
      while (std::getline(in, line))
      {
         const size_t first = line.find_first_not_of(" \t");
         const std::string body = first == std::string::npos ? std::string() : line.substr(first);
         if (body.rfind("node ", 0) == 0)
            inNode = body.rfind(head, 0) == 0;
         else if (inNode && body.rfind("s " + key + " ", 0) == 0)
            return body.substr(key.size() + 3);
      }
      return std::string();
   }

   std::string Unescape(const std::string& raw)
   {
      std::string out;
      for (size_t i = 0; i < raw.size(); i++)
      {
         if (raw[i] == '\\' && i + 1 < raw.size() && (raw[i + 1] == 'n' || raw[i + 1] == '\\'))
            out += raw[++i] == 'n' ? '\n' : '\\';
         else
            out += raw[i];
      }
      return out;
   }

   bool ReplacePatchNodeCode(std::string& patch, int index, const std::string& key, const std::string& code)
   {
      std::istringstream in(patch);
      std::string line, out;
      bool inNode = false, done = false;
      const std::string head = "node " + std::to_string(index) + " ";
      while (std::getline(in, line))
      {
         const size_t first = line.find_first_not_of(" \t");
         const std::string body = first == std::string::npos ? std::string() : line.substr(first);
         if (body.rfind("node ", 0) == 0)
            inNode = body.rfind(head, 0) == 0;
         if (inNode && !done && body == "end")
         {
            out += "  s " + key + " " + Escape(code) + "\n";
            done = true;
         }
         else if (inNode && !done && body.rfind("s " + key + " ", 0) == 0)
         {
            line = line.substr(0, first) + "s " + key + " " + Escape(code);
            done = true;
         }
         out += line + "\n";
      }
      if (done)
         patch = out;
      return done;
   }

   // The language of a live node, by its type.
   const Language& LiveLanguage(int index)
   {
      const json nodes = gLive.Call("list_nodes")["nodes"];
      for (const json& n : nodes)
         if (n.value("index", -1) == index)
         {
            if (const Language* l = LanguageByType(n.value("type", std::string())))
               return *l;
            Fail(n.value("type", std::string()) + " holds no code");
         }
      Fail("no node " + std::to_string(index));
   }

   int CodeVerb(Args args)
   {
      if (args.empty())
         Fail("usage: infinite code kinds|new|check|push|pull|watch|errors|docs|examples", kExitUsage);
      const std::string verb = args[0];
      args.erase(args.begin());
      std::string forced, node, patch, pass;
      TakeOption(args, "--type", forced);
      TakeOption(args, "--node", node);
      TakeOption(args, "--patch", patch);
      TakeOption(args, "--pass", pass);
      const bool look = TakeFlag(args, "--look");
      const std::string passKey = PassKey(pass);

      if (verb == "kinds")
      {
         json kinds = json::array();
         for (const Language& l : kLanguages)
            kinds.push_back({ { "kind", l.kind }, { "node", l.type }, { "extension", l.extension } });
         Print(kinds);
         return 0;
      }
      if (verb == "new")
      {
         if (args.empty())
            Fail("usage: infinite code new <kind> [file]", kExitUsage);
         const Language* l = LanguageByKind(args[0]);
         if (l == nullptr)
            Fail("unknown code kind '" + args[0] + "'; see `infinite code kinds`", kExitUsage);
         std::string starter = l->starter;
         if (starter.empty())
         {
            const std::vector<Example> found = Examples(l->kind);
            if (found.empty())
               Fail(std::string("no starter for ") + l->kind);
            starter = found[0].code;
         }
         if (args.size() < 2)
         {
            fputs(starter.c_str(), stdout);
            return 0;
         }
         if (std::filesystem::exists(AppPaths::FsPath(args[1])))
            Fail(args[1] + " already exists");
         WriteFile(args[1], starter);
         Print(json{ { "file", args[1] }, { "kind", l->kind } });
         return 0;
      }
      if (verb == "check")
      {
         if (args.empty())
            Fail("usage: infinite code check <file> [--type kind] [--look] [--pass name]", kExitUsage);
         const json result = CheckCode(LanguageOf(args[0], forced), ReadFile(args[0]), passKey, look);
         Print(result);
         return result["ok"].get<bool>() ? 0 : 3;
      }
      if (verb == "docs")
      {
         const std::string language = args.empty() ? std::string() : args[0];
         if (language == "field")
            fputs(AISkillContent::kFieldLanguageMarkdown, stdout);
         else if (language == "expressions" || language == "expr")
            fputs(AISkillContent::kExpressionGlobalsMarkdown, stdout);
         else if (language == "sketch")
            fputs(TermDocs::kSketch, stdout);
         else if (language == "sketch3d")
            fputs(TermDocs::kSketch3D, stdout);
         else if (language == "glsl" || language == "shadertoy")
            fputs(TermDocs::kGlsl, stdout);
         else if (language == "patch")
            fputs(AISkillContent::kPatchAuthoringMarkdown, stdout);
         else
            Fail("usage: infinite code docs field|sketch|sketch3d|glsl|expressions|patch", kExitUsage);
         return 0;
      }
      if (verb == "examples")
      {
         if (args.empty())
            Fail("usage: infinite code examples <kind> [name]", kExitUsage);
         const std::vector<Example> found = Examples(args[0]);
         if (found.empty())
            Fail("no examples for '" + args[0] + "'");
         if (args.size() == 1)
         {
            for (const Example& e : found)
               printf("%s\n", e.name.c_str());
            return 0;
         }
         for (const Example& e : found)
            if (e.name == args[1])
            {
               fputs(e.code.c_str(), stdout);
               return 0;
            }
         Fail("no example '" + args[1] + "'");
      }
      if (verb == "errors")
      {
         if (!args.empty() && IsPatchFile(args[0]))
            return PassThrough({ "--validate", args[0], "--lenient" });
         Print(gLive.Call("code_errors"));
         return 0;
      }
      if (verb == "pull")
      {
         const std::string ref = !node.empty() ? node : !args.empty() ? args[0] : std::string("selected");
         const std::string file = args.size() >= (node.empty() ? 2u : 1u) ? args[node.empty() ? 1 : 0] : std::string();
         std::string code;
         std::string extension;
         if (!patch.empty())
         {
            const std::string text = ReadFile(patch);
            const int index = atoi(ref.c_str());
            for (const Language& l : kLanguages)
            {
               const std::string raw = PatchNodeCode(text, index, passKey.empty() ? l.key : passKey);
               if (!raw.empty())
               {
                  code = Unescape(raw);
                  extension = l.extension;
                  break;
               }
            }
            if (extension.empty())
               Fail("node " + ref + " of " + patch + " holds no code");
         }
         else
         {
            const int index = NodeRef(ref);
            const Language& l = LiveLanguage(index);
            const json params = gLive.Call("get_params", { { "index", index } });
            const std::string key = "s " + (passKey.empty() ? std::string(l.key) : passKey);
            if (!params.contains(key))
               Fail("node " + std::to_string(index) + " has no " + key);
            code = Unescape(params[key].get<std::string>());
            extension = l.extension;
         }
         if (file.empty())
            fputs(code.c_str(), stdout);
         else
         {
            WriteFile(file, code);
            Print(json{ { "file", file }, { "extension", extension } });
         }
         return 0;
      }
      if (verb == "push" || verb == "watch")
      {
         if (args.empty())
            Fail("usage: infinite code " + verb + " <file> --node <node> [--patch p.inf] [--pass name]", kExitUsage);
         const std::string file = args[0];
         const Language& l = LanguageOf(file, forced);
         const std::string key = passKey.empty() ? std::string(l.key) : passKey;
         const auto send = [&]() -> json
         {
            const std::string code = ReadFile(file);
            json result = CheckCode(l, code, passKey, false);
            if (!result["ok"].get<bool>())
            {
               result["pushed"] = false;
               return result;
            }
            if (!patch.empty())
            {
               std::string text = ReadFile(patch);
               if (!ReplacePatchNodeCode(text, atoi(node.c_str()), key, code))
                  Fail("no node " + node + " in " + patch);
               WriteFile(patch, text);
            }
            else
               gLive.Call("set_param", { { "index", NodeRef(node.empty() ? "selected" : node) }, { "name", key }, { "value", code } });
            result["pushed"] = true;
            return result;
         };
         if (verb == "push")
         {
            const json result = send();
            Print(result);
            return result["ok"].get<bool>() ? 0 : 3;
         }
         std::error_code ec;
         auto seen = std::filesystem::file_time_type::min();
         for (;;)
         {
            const auto now = std::filesystem::last_write_time(AppPaths::FsPath(file), ec);
            if (!ec && now != seen)
            {
               seen = now;
               Print(send());
               fflush(stdout);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
         }
      }
      Fail("unknown code verb '" + verb + "'", kExitUsage);
   }

   // ---- Patches ------------------------------------------------------------------------------

   std::vector<std::vector<std::string>> Recipes()
   {
      std::vector<std::vector<std::string>> out;
      std::istringstream in(ReadFile(app::BundledResourcePath("templates/index.txt")));
      std::string line;
      while (std::getline(in, line))
      {
         if (line.empty() || line[0] == '#')
            continue;
         std::vector<std::string> fields;
         std::istringstream cols(line);
         std::string field;
         while (std::getline(cols, field, '|'))
            fields.push_back(field);
         if (fields.size() >= 4)
            out.push_back(fields);
      }
      return out;
   }

   // Which nodes reach an Output or an Audio Out, from canonical patch text.
   json Unreachable(const std::string& text)
   {
      std::map<int, std::string> types;
      std::multimap<int, int> feeders;
      std::istringstream in(text);
      std::string line;
      while (std::getline(in, line))
      {
         std::istringstream words(line);
         std::string tag;
         words >> tag;
         if (tag == "node")
         {
            int index = -1;
            std::string category, type;
            words >> index >> category;
            std::getline(words, type);
            type.erase(0, type.find_first_not_of(' '));
            types[index] = type;
         }
         else if (tag == "cable")
         {
            int dst = -1, slot = 0, src = -1;
            words >> dst >> slot >> src;
            feeders.insert({ dst, src });
         }
         else if (tag == "geo" || tag == "aud" || tag == "note")
         {
            int dst = -1, src = -1;
            words >> dst >> src;
            feeders.insert({ dst, src });
         }
         else if (tag == "mod")
         {
            int dst = -1, param = 0, src = -1;
            words >> dst >> param >> src;
            feeders.insert({ dst, src });
         }
      }
      std::set<int> reached;
      std::vector<int> open;
      for (const auto& kv : types)
         if (kv.second == "Output" || kv.second == "Audio Out")
            open.push_back(kv.first);
      while (!open.empty())
      {
         const int at = open.back();
         open.pop_back();
         if (!reached.insert(at).second)
            continue;
         const auto range = feeders.equal_range(at);
         for (auto it = range.first; it != range.second; ++it)
            open.push_back(it->second);
      }
      json out = json::array();
      for (const auto& kv : types)
         if (reached.count(kv.first) == 0 && kv.second != "Comment")
            out.push_back({ { "node", kv.first }, { "type", kv.second } });
      return out;
   }

   int PatchCheck(Args args)
   {
      std::string want;
      TakeOption(args, "--want", want);
      if (args.empty())
         Fail("usage: infinite patch check <file.inf> [--want moving-picture,sound]", kExitUsage);
      const std::string file = args[0];
      json result = { { "ok", false }, { "file", file }, { "stages", json::array() } };
      const auto stage = [&](const std::string& name, bool ok, json detail) -> bool
      {
         detail["stage"] = name;
         detail["ok"] = ok;
         result["stages"].push_back(detail);
         return ok;
      };
      const auto finish = [&](bool ok)
      {
         result["ok"] = ok;
         Print(result);
         return ok ? 0 : 3;
      };

      const Run validate = RunSelf({ "--validate", file, "--for-render" }, true);
      const json report = LastJson(validate.out);
      json loads = json::object();
      if (report.is_object())
         for (const char* key : { "errors", "warnings" })
            if (report.contains(key))
               loads[key] = report[key];
      if (!stage("loads", validate.code == 0, loads))
         return finish(false);

      const std::string canon = TempPath("canon.inf");
      RunSelf({ "--canonicalize", file, canon }, true);
      const json dead = Unreachable(ReadFile(canon));
      std::error_code ec;
      std::filesystem::remove(AppPaths::FsPath(canon), ec);
      if (!stage("reachable", dead.empty(), { { "unreachable", dead } }))
         return finish(false);

      const bool wantPicture = want.find("moving-picture") != std::string::npos;
      const std::string dir = TempPath("frames");
      std::filesystem::create_directories(AppPaths::FsPath(dir), ec);
      const std::string sheet = dir + "/sheet.png";
      const Run frames = RunSelf({ "--frame", file, "0,1,2,3", dir + "/", "--contact-sheet", sheet }, true);
      const json frameReport = LastJson(frames.out);
      json picture = { { "contact_sheet", sheet } };
      bool pictureOk = frames.code == 0;
      if (frameReport.is_object() && frameReport.contains("frame_stats"))
      {
         picture["frame_stats"] = frameReport["frame_stats"];
         std::set<std::string> distinct;
         if (frameReport.contains("files"))
            for (const json& f : frameReport["files"])
               if (f.is_string() && f.get<std::string>() != sheet)
                  distinct.insert(ReadFile(f.get<std::string>()));
         picture["distinct_frames"] = distinct.size();
         if (wantPicture && distinct.size() < 2)
            pictureOk = false;
      }
      if (frames.code != 0 && frameReport.is_object() && frameReport.contains("errors"))
         picture["errors"] = frameReport["errors"];
      if (!stage("picture", pictureOk, picture))
         return finish(false);

      const bool wantSound = want.find("sound") != std::string::npos;
      const std::string summary = TempPath("audio.json");
      const Run audio = RunSelf({ "--audio-summary", file, summary, "--duration", "4" }, true);
      const json audioReport = LastJson(audio.out);
      json sound = audioReport.is_object() && audioReport.contains("audio_summary") ? json{ { "audio_summary", audioReport["audio_summary"] } } : json::object();
      bool soundOk = audio.code == 0 || !wantSound;
      if (wantSound && sound.contains("audio_summary"))
      {
         const json& s = sound["audio_summary"];
         if (s.value("silent", false) || s.value("clipping", false))
            soundOk = false;
      }
      return finish(stage("sound", soundOk, sound));
   }

   int PatchVerb(Args args)
   {
      if (args.empty())
         Fail("usage: infinite patch recipes|new|check|layout|apply", kExitUsage);
      const std::string verb = args[0];
      args.erase(args.begin());
      if (verb == "recipes")
      {
         for (const auto& r : Recipes())
            printf("%-20s %s: %s\n", r[0].c_str(), r[2].c_str(), r[3].c_str());
         return 0;
      }
      if (verb == "new")
      {
         std::string recipe = "first-image";
         TakeOption(args, "--recipe", recipe);
         if (args.empty())
            Fail("usage: infinite patch new --recipe <name> out.inf", kExitUsage);
         const std::string source = app::BundledResourcePath(("templates/" + recipe + ".inf").c_str());
         if (!std::filesystem::exists(AppPaths::FsPath(source)))
            Fail("no recipe '" + recipe + "'; see `infinite patch recipes`");
         if (std::filesystem::exists(AppPaths::FsPath(args[0])))
            Fail(args[0] + " already exists");
         WriteFile(args[0], ReadFile(source));
         Print(json{ { "file", args[0] }, { "recipe", recipe } });
         return 0;
      }
      if (verb == "check")
         return PatchCheck(args);
      if (verb == "layout")
      {
         if (args.empty())
            Fail("usage: infinite patch layout <file.inf> [out.inf]", kExitUsage);
         const std::string canon = TempPath("layout.inf");
         const Run run = RunSelf({ "--canonicalize", args[0], canon, "--keep-ids" }, true);
         if (run.code != 0)
         {
            fputs(run.out.c_str(), stdout);
            return run.code;
         }
         Patch::Data data;
         std::string error;
         if (!Patch::ReadText(ReadFile(canon), data, error))
            Fail(error);
         for (Patch::NodeRecord& n : data.nodes)
            n.hasPos = false;
         PatchLayout::Apply(data);
         std::string text;
         if (!Patch::WriteText(data, text))
            Fail("could not write the patch");
         WriteFile(args.size() > 1 ? args[1] : args[0], text);
         std::error_code ec;
         std::filesystem::remove(AppPaths::FsPath(canon), ec);
         Print(json{ { "file", args.size() > 1 ? args[1] : args[0] }, { "nodes", data.nodes.size() } });
         return 0;
      }
      if (verb == "apply")
      {
         if (args.empty())
            Fail("usage: infinite patch apply <file.inf>", kExitUsage);
         Print(gLive.Call("load_patch_text", { { "text", ReadFile(args[0]) } }));
         return 0;
      }
      Fail("unknown patch verb '" + verb + "'", kExitUsage);
   }

   // ---- The rest ------------------------------------------------------------------------------

   const char* kHelp = R"(infinite: drive Infinite from a terminal. Live when the app answers, headless given a .inf file.

Look     graph [p.inf]  explain [p.inf]  params <node>  selection  status  nodes  describe <type>
Edit     add <type> [x y]  remove <node>  connect <src[:out]> <dst[:slot]>  disconnect <dst[:slot]>
         set <node> <key> <value>  mod <src[:out]> <dst> <key> [lo hi]  mod clear <dst> <key>
         expr <node> <key> <text>  bypass <node> [on|off]  batch <file|->  undo  redo
Patch    new  open <p.inf>  save [p.inf]  validate <p.inf>  canonicalize <in> <out>  apply <p.inf>
Play     play  stop  rewind  seek <seconds>  tempo <bpm>  session scene <n>  session stop
See      shot <node> [out.png]  frame [p.inf] <t> <out.png>  render <p.inf> <out.mp4>  audio <p.inf> <out.json>
Code     code kinds|new|check|push|pull|watch|errors|docs|examples
Generate patch recipes|new|check|layout|apply
Learn    context  guide
Watch    watch
Terminal term open|list|read|send|run

<node> is an index, `selected`, or a title or type that only one node has.
)";

   int Context()
   {
      json brief = json::object();
      if (gLive.Connect())
      {
         brief["app"] = gLive.Call("status");
         brief["selection"] = gLive.Call("selection")["nodes"];
         brief["nodes"] = gLive.Call("list_nodes")["nodes"];
         brief["codeErrors"] = gLive.Call("code_errors")["errors"];
      }
      else
         brief["app"] = "not running; every verb that takes a .inf file still works";
      Print(brief);
      fputs(R"(
Loop: patch new, edit the .inf, patch check, fix, patch layout, patch apply.
Code loop: code new, edit, code check, code watch --node <n>, shot <n>.
Facts come from `describe <type>` and `code docs <language>`, never from memory.
`infinite` with no verb lists every verb; `infinite guide` prints the patch authoring guide.
)", stdout);
      return 0;
   }

   int Watch()
   {
      json last;
      for (;;)
      {
         json now = { { "status", gLive.Call("status") }, { "selection", gLive.Call("selection")["nodes"] }, { "nodes", gLive.Call("list_nodes")["nodes"] } };
         now["status"].erase("seconds");
         now["status"].erase("beats");
         if (now != last)
         {
            for (auto it = now.begin(); it != now.end(); ++it)
               if (last.is_null() || last[it.key()] != it.value())
                  Print(json{ { "changed", it.key() }, { "value", it.value() } });
            fflush(stdout);
            last = now;
         }
         std::this_thread::sleep_for(std::chrono::milliseconds(300));
      }
   }

   int TermVerb(Args args)
   {
      if (args.empty())
         Fail("usage: infinite term open|list|read|send|run", kExitUsage);
      const std::string verb = args[0];
      args.erase(args.begin());
      std::string tabText, timeoutText = "60";
      TakeOption(args, "--tab", tabText);
      TakeOption(args, "--timeout", timeoutText);
      json params = json::object();
      if (!tabText.empty())
         params["tab"] = atoi(tabText.c_str());
      if (verb == "open")
      {
         Print(gLive.Call("term_open"));
         return 0;
      }
      if (verb == "list")
      {
         Print(gLive.Call("term_list"));
         return 0;
      }
      if (verb == "read")
      {
         Print(gLive.Call("term_read", params));
         return 0;
      }
      if (args.empty())
         Fail("usage: infinite term " + verb + " <text> [--tab n]", kExitUsage);
      if (verb == "send")
      {
         params["text"] = args[0];
         Print(gLive.Call("term_send", params));
         return 0;
      }
      if (verb == "run")
      {
         if (const char* own = getenv("INFINITE_TERM_TAB"); own != nullptr && (tabText.empty() || tabText == own))
            Fail("term run from inside a tab needs --tab <another tab>");
         // The exit code travels as the same OSC 133 mark a shell integration would send.
         params["text"] = args[0] + "; printf '\\033]133;D;%s\\007' $?\n";
         gLive.Call("term_send", params);
         params.erase("text");
         const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(atoi(timeoutText.c_str()));
         std::this_thread::sleep_for(std::chrono::milliseconds(300));
         json read = gLive.Call("term_read", params);
         while (read.value("busy", false) && std::chrono::steady_clock::now() < end)
         {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            read = gLive.Call("term_read", params);
         }
         std::this_thread::sleep_for(std::chrono::milliseconds(100));
         const bool timedOut = read.value("busy", false);
         read = gLive.Call("term_read", params);
         read["timedOut"] = timedOut;
         Print(read);
         return timedOut ? kExitFail : read.value("exitCode", 0);
      }
      Fail("unknown term verb '" + verb + "'", kExitUsage);
   }

   json ParamValue(const std::string& text)
   {
      return json(text);
   }

   int Add(const Args& args)
   {
      if (args.empty())
         Fail("usage: infinite add <type> [x y]", kExitUsage);
      const json categories = gLive.Call("list_node_types");
      const json& table = categories.contains("categories") ? categories["categories"] : categories;
      std::string category;
      for (auto it = table.begin(); it != table.end(); ++it)
         if (it->is_array() && std::find(it->begin(), it->end(), json(args[0])) != it->end())
            category = it.key();
      if (category.empty())
         Fail("no node type '" + args[0] + "'; see `infinite nodes`");
      json params = { { "typeName", args[0] }, { "category", category } };
      if (args.size() >= 3)
      {
         params["x"] = atof(args[1].c_str());
         params["y"] = atof(args[2].c_str());
      }
      Print(gLive.Call("create_node", params));
      return 0;
   }

   int Disconnect(const Args& args)
   {
      if (args.empty())
         Fail("usage: infinite disconnect <dst[:slot]>", kExitUsage);
      std::string nodeText;
      int slot = 0;
      SplitPin(args[0], nodeText, slot, 0);
      const int dst = NodeRef(nodeText);
      const json links = gLive.Call("get_graph")["links"];
      for (const json& link : links)
         if (link.value("dstIndex", -1) == dst && link.value("dstSlot", -1) == slot)
         {
            Print(gLive.Call("disconnect", { { "linkId", link.value("id", -1) } }));
            return 0;
         }
      Fail("no cable into " + args[0]);
   }

   int Dispatch(Args args)
   {
      if (args.empty() || args[0] == "help" || args[0] == "--help" || args[0] == "-h")
      {
         fputs(kHelp, stdout);
         return 0;
      }
      const std::string verb = args[0];
      args.erase(args.begin());
      const auto need = [&](size_t n, const char* usage)
      {
         if (args.size() < n)
            Fail(std::string("usage: infinite ") + usage, kExitUsage);
      };
      const auto call = [&](const std::string& method, const json& params = json::object())
      {
         Print(gLive.Call(method, params));
         return 0;
      };
      const auto transport = [&](const char* action, json params = json::object())
      {
         params["action"] = action;
         return call("transport", params);
      };

      if (verb == "--version" || verb == "version")
         return PassThrough({ "--version" });

      // Look
      if (verb == "graph" || verb == "explain")
      {
         if (!args.empty() && IsPatchFile(args[0]))
            return PassThrough({ "--explain", args[0], "--json" });
         if (verb == "graph")
            return call("get_graph");
         Print(gLive.Call("explain")["text"]);
         return 0;
      }
      if (verb == "params")
      {
         need(1, "params <node>");
         return call("get_params", { { "index", NodeRef(args[0]) } });
      }
      if (verb == "selection")
         return call("selection");
      if (verb == "status")
         return call("status");
      if (verb == "nodes")
      {
         app::RegisterNodes();
         json table = json::object();
         for (const std::string& category : NodeFactory::Instance().GetCategories())
            table[category] = NodeFactory::Instance().GetNodesInCategory(category);
         Print(table);
         return 0;
      }
      if (verb == "describe")
      {
         need(1, "describe <type>");
         return PassThrough({ "--describe", args[0] });
      }

      // Edit
      if (verb == "add")
         return Add(args);
      if (verb == "remove")
      {
         need(1, "remove <node>");
         return call("delete_node", { { "index", NodeRef(args[0]) } });
      }
      if (verb == "connect")
      {
         need(2, "connect <src[:output]> <dst[:slot]>");
         std::string src, dst;
         int output = 0, slot = 0;
         SplitPin(args[0], src, output, 0);
         SplitPin(args[1], dst, slot, 0);
         return call("connect", { { "srcIndex", NodeRef(src) }, { "srcOutput", output }, { "dstIndex", NodeRef(dst) }, { "dstSlot", slot } });
      }
      if (verb == "disconnect")
         return Disconnect(args);
      if (verb == "set")
      {
         need(3, "set <node> <key> <value>");
         return call("set_param", { { "index", NodeRef(args[0]) }, { "name", args[1] }, { "value", ParamValue(args[2]) } });
      }
      if (verb == "mod")
      {
         if (!args.empty() && args[0] == "clear")
         {
            need(3, "mod clear <dst> <key>");
            return call("unbind_mod", { { "index", NodeRef(args[1]) }, { "key", args[2] } });
         }
         need(3, "mod <src[:output]> <dst> <key> [lo hi]");
         std::string src;
         int output = 0;
         SplitPin(args[0], src, output, 0);
         json params = { { "src", NodeRef(src) }, { "output", output }, { "dst", NodeRef(args[1]) }, { "key", args[2] } };
         if (args.size() >= 5)
         {
            params["lo"] = atof(args[3].c_str());
            params["hi"] = atof(args[4].c_str());
         }
         return call("bind_mod", params);
      }
      if (verb == "expr")
      {
         need(3, "expr <node> <key> <text>");
         return call("set_expr", { { "index", NodeRef(args[0]) }, { "key", args[1] }, { "text", args[2] } });
      }
      if (verb == "bypass")
      {
         need(1, "bypass <node> [on|off]");
         return call("set_bypass", { { "index", NodeRef(args[0]) }, { "bypassed", args.size() < 2 || args[1] != "off" } });
      }
      if (verb == "batch")
      {
         need(1, "batch <file|->");
         std::string text;
         if (args[0] == "-")
         {
            std::stringstream in;
            in << std::cin.rdbuf();
            text = in.str();
         }
         else
            text = ReadFile(args[0]);
         const json calls = json::parse(text, nullptr, false);
         if (!calls.is_array())
            Fail("batch wants a JSON array of {method, params}");
         return call("batch", { { "calls", calls } });
      }
      if (verb == "undo" || verb == "redo")
         return call(verb);

      // Patch
      if (verb == "new")
         return call("new_patch");
      if (verb == "open")
      {
         need(1, "open <file.inf>");
         return call("load_patch", { { "path", std::filesystem::absolute(AppPaths::FsPath(args[0])).u8string() } });
      }
      if (verb == "save")
         return call("save_patch", args.empty() ? json::object() : json{ { "path", std::filesystem::absolute(AppPaths::FsPath(args[0])).u8string() } });
      if (verb == "validate")
      {
         need(1, "validate <file.inf>");
         Args flags = { "--validate" };
         flags.insert(flags.end(), args.begin(), args.end());
         return PassThrough(flags);
      }
      if (verb == "canonicalize")
      {
         need(2, "canonicalize <in.inf> <out.inf>");
         return PassThrough({ "--canonicalize", args[0], args[1] });
      }
      if (verb == "apply")
         return PatchVerb({ "apply", args.empty() ? std::string() : args[0] });

      // Play
      if (verb == "play" || verb == "stop" || verb == "rewind")
         return transport(verb.c_str());
      if (verb == "seek")
      {
         need(1, "seek <seconds>");
         return transport("seek", { { "seconds", atof(args[0].c_str()) } });
      }
      if (verb == "tempo")
      {
         need(1, "tempo <bpm>");
         return transport("tempo", { { "bpm", atof(args[0].c_str()) } });
      }
      if (verb == "session")
      {
         need(1, "session scene <n> | session stop");
         if (args[0] == "stop")
            return call("session", { { "action", "stop" } });
         need(2, "session scene <n>");
         return call("session", { { "action", "scene" }, { "scene", atoi(args[1].c_str()) } });
      }

      // See and hear
      if (verb == "shot")
      {
         need(1, "shot <node> [out.png]");
         const std::string path = args.size() > 1 ? std::filesystem::absolute(AppPaths::FsPath(args[1])).u8string() : TempPath("shot.png");
         return call("screenshot_node", { { "index", NodeRef(args[0]) }, { "path", path } });
      }
      if (verb == "frame")
      {
         if (!args.empty() && IsPatchFile(args[0]))
         {
            need(3, "frame <file.inf> <times> <out.png>");
            Args flags = { "--frame" };
            flags.insert(flags.end(), args.begin(), args.end());
            return PassThrough(flags);
         }
         need(1, "frame <out.png>");
         return call("render_frame", { { "path", std::filesystem::absolute(AppPaths::FsPath(args.back())).u8string() } });
      }
      if (verb == "render" || verb == "audio")
      {
         need(2, verb == "render" ? "render <file.inf> <out.mp4> [options]" : "audio <file.inf> <out.json> [options]");
         Args flags = { verb == "render" ? "--render" : "--audio-summary" };
         flags.insert(flags.end(), args.begin(), args.end());
         return PassThrough(flags);
      }

      if (verb == "code")
         return CodeVerb(args);
      if (verb == "patch")
         return PatchVerb(args);
      if (verb == "context")
         return Context();
      if (verb == "guide")
      {
         fputs(kHelp, stdout);
         fputs(AISkillContent::kPatchAuthoringMarkdown, stdout);
         return 0;
      }
      if (verb == "watch")
         return Watch();
      if (verb == "term")
         return TermVerb(args);
      Fail("unknown verb '" + verb + "'; `infinite` lists them", kExitUsage);
   }
}

int Run(int argc, char** argv)
{
#if defined(_WIN32)
   Platform::AttachConsoleForHeadless();
#endif
   try
   {
      return Dispatch(Args(argv, argv + argc));
   }
   catch (const Failure& failure)
   {
      fprintf(stderr, "infinite: %s\n", failure.message.c_str());
      return failure.code;
   }
   catch (const std::exception& e)
   {
      fprintf(stderr, "infinite: %s\n", e.what());
      return kExitFail;
   }
}
}
