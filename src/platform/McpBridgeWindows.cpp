// Infinite-Turbo 0.45: MCP server (stdio) for Claude and other MCP clients.
//
//   Infinite-Turbo.exe --mcp
//
// Speaks the Model Context Protocol on stdin/stdout (newline-delimited
// JSON-RPC 2.0) and forwards every tool call to the running Infinite-Turbo
// over its local control port (RemoteControl: 127.0.0.1:7777, or
// INFINITE_CONTROL_PORT, with the token the app writes to
// %LOCALAPPDATA%\Infinite\control_token). If the app is not running, the
// first tool call starts it (set INFINITE_MCP_NO_LAUNCH=1 to turn that off).
//
// Nothing else in this process opens a window, an audio device or GL: this
// mode returns from main() before any of that.

#include "McpBridge.h"
#include "AISkillContent.h"

#include <chrono>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "json.hpp"
#include "platform/SettingsPaths.h"
#include "platform/SocketCompat.h"

#include <windows.h>

using json = nlohmann::json;

namespace
{
   const char* kServerVersion = "0.47.0";

   // ------------------------------------------------------------ stdio ---
   HANDLE gIn = INVALID_HANDLE_VALUE;
   HANDLE gOut = INVALID_HANDLE_VALUE;
   std::string gInBuffer;

   bool ReadLine(std::string& line)
   {
      for (;;)
      {
         const size_t nl = gInBuffer.find('\n');
         if (nl != std::string::npos)
         {
            line = gInBuffer.substr(0, nl);
            gInBuffer.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r')
               line.pop_back();
            return true;
         }
         char chunk[8192];
         DWORD got = 0;
         if (!ReadFile(gIn, chunk, sizeof(chunk), &got, nullptr) || got == 0)
            return false; // client closed stdin: we are done
         gInBuffer.append(chunk, got);
      }
   }

   void WriteMessage(const json& msg)
   {
      std::string out = msg.dump(-1, ' ', false, json::error_handler_t::replace);
      out += "\n";
      DWORD written = 0;
      WriteFile(gOut, out.data(), (DWORD)out.size(), &written, nullptr);
      FlushFileBuffers(gOut);
   }

   void Log(const std::string& text)
   {
      // stderr is the MCP client's log; stdout is reserved for protocol.
      HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
      if (err == INVALID_HANDLE_VALUE || err == nullptr)
         return;
      const std::string line = "[infinite-mcp] " + text + "\n";
      DWORD written = 0;
      WriteFile(err, line.data(), (DWORD)line.size(), &written, nullptr);
   }

   // ------------------------------------------------------ app connection ---
   InfiniteSocket gSock = kInfiniteInvalidSocket;
   std::string gSockBuffer;
   int gNextId = 1;

   int ControlPort()
   {
      if (const char* env = std::getenv("INFINITE_CONTROL_PORT"))
         if (int p = std::atoi(env); p > 0)
            return p;
      return 7777;
   }

   std::string ReadToken()
   {
      std::ifstream f(InfiniteSettingsDirectory() + "/control_token");
      std::string token;
      std::getline(f, token);
      return token;
   }

   void Disconnect()
   {
      InfiniteCloseSocket(gSock);
      gSock = kInfiniteInvalidSocket;
      gSockBuffer.clear();
   }

   bool Connect()
   {
      if (gSock != kInfiniteInvalidSocket)
         return true;
      if (!InfiniteSocketsReady())
         return false;
      InfiniteSocket s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      if (s == kInfiniteInvalidSocket)
         return false;
      sockaddr_in addr{};
      addr.sin_family = AF_INET;
      addr.sin_port = htons((u_short)ControlPort());
      addr.sin_addr.s_addr = inet_addr("127.0.0.1");
      if (connect(s, (sockaddr*)&addr, sizeof(addr)) != 0)
      {
         InfiniteCloseSocket(s);
         return false;
      }
      gSock = s;
      return true;
   }

   bool LaunchApp()
   {
      if (std::getenv("INFINITE_MCP_NO_LAUNCH") != nullptr)
         return false;
      wchar_t exe[MAX_PATH * 2] = {};
      if (GetModuleFileNameW(nullptr, exe, (DWORD)(sizeof(exe) / sizeof(exe[0]))) == 0)
         return false;
      std::wstring cmd = L"\"" + std::wstring(exe) + L"\"";
      std::wstring dir(exe);
      const size_t slash = dir.find_last_of(L"\\/");
      if (slash != std::wstring::npos)
         dir.resize(slash);
      STARTUPINFOW si{};
      si.cb = sizeof(si);
      PROCESS_INFORMATION pi{};
      std::vector<wchar_t> buf(cmd.begin(), cmd.end());
      buf.push_back(0);
      if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
                          nullptr, dir.c_str(), &si, &pi))
         return false;
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
      Log("started Infinite-Turbo, waiting for it to come up");
      return true;
   }

   bool SendAll(const std::string& data)
   {
      size_t sent = 0;
      while (sent < data.size())
      {
         const int n = send(gSock, data.data() + sent, (int)(data.size() - sent), 0);
         if (n <= 0)
            return false;
         sent += (size_t)n;
      }
      return true;
   }

   bool RecvLine(std::string& line, int timeoutMs)
   {
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
      for (;;)
      {
         const size_t nl = gSockBuffer.find('\n');
         if (nl != std::string::npos)
         {
            line = gSockBuffer.substr(0, nl);
            gSockBuffer.erase(0, nl + 1);
            return true;
         }
         const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
         if (left <= 0)
            return false;
         fd_set set;
         FD_ZERO(&set);
         FD_SET(gSock, &set);
         timeval tv{ (long)(left / 1000), (long)((left % 1000) * 1000) };
         if (select(0, &set, nullptr, nullptr, &tv) <= 0)
            continue;
         char chunk[65536];
         const int n = recv(gSock, chunk, (int)sizeof(chunk), 0);
         if (n <= 0)
            return false;
         gSockBuffer.append(chunk, (size_t)n);
      }
   }

   // Calls one RPC method on the app. Returns false with `error` set.
   bool CallApp(const std::string& method, const json& params, json& result, std::string& error)
   {
      static bool sLaunched = false;
      if (!Connect())
      {
         if (sLaunched || !LaunchApp())
         {
            error = "Infinite-Turbo is not running (or its control port is busy). Open Infinite-Turbo and try again.";
            return false;
         }
         sLaunched = true;
         for (int i = 0; i < 60 && !Connect(); i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
         if (gSock == kInfiniteInvalidSocket)
         {
            error = "Infinite-Turbo was started but its control port did not answer within 30 s.";
            return false;
         }
         std::this_thread::sleep_for(std::chrono::milliseconds(800)); // token file written at startup
      }
      for (int attempt = 0; attempt < 2; attempt++)
      {
         const int id = gNextId++;
         json req = { { "jsonrpc", "2.0" }, { "id", id }, { "method", method }, { "params", params },
                      { "token", ReadToken() } };
         if (!SendAll(req.dump(-1, ' ', false, json::error_handler_t::replace) + "\n"))
         {
            // Stale connection (the app restarted): nothing was sent, so
            // reconnecting and sending again cannot run it twice.
            Disconnect();
            if (!Connect())
               break;
            continue;
         }
         std::string line;
         if (!RecvLine(line, 120000))
         {
            // Sent but no answer: never resend (it may still run). Drop the
            // connection so a late reply cannot be read as the next one's.
            Disconnect();
            error = "Infinite-Turbo did not answer within 2 minutes (busy?). Check the app before retrying.";
            return false;
         }
         json reply;
         try
         {
            reply = json::parse(line);
         }
         catch (const std::exception& e)
         {
            error = std::string("bad reply from Infinite-Turbo: ") + e.what();
            return false;
         }
         if (reply.contains("error"))
         {
            error = reply["error"].value("message", std::string("error"));
            return false;
         }
         result = reply.value("result", json::object());
         return true;
      }
      error = "lost the connection to Infinite-Turbo";
      return false;
   }

   // --------------------------------------------------------------- tools ---
   struct Tool
   {
      const char* name;
      const char* description;
      const char* schema; // JSON
   };

   const char* kPatchFormat =
      "Infinite patch text (.inf), one statement per line, leading spaces ignored:\n"
      "  infinite-patch 1\n"
      "  node <index> <category> <type name>      (type names: describe)\n"
      "    pos <x> <y>                              (optional: nodes without pos are auto laid out)\n"
      "    f <key> <float> | i <key> <int> | b <key> <0|1> | c <key> <r> <g> <b> | s <key> <text>\n"
      "  end\n"
      "  cable <dstIndex> <dstSlot> <srcIndex> [srcOutput]   image cable\n"
      "  geo   <dstIndex> <dstSlot> <srcIndex>               geometry / camera / light / palette\n"
      "  aud   <dstIndex> <dstSlot> <srcIndex> [srcOutput]   audio cable\n"
      "  note  <dstIndex> <dstSlot> <srcIndex> [srcOutput]   note cable\n"
      "  mod   <dstIndex> <dstParam> <srcIndex> <srcOutput> [polarity depth centre]\n"
      "  expr  <dstIndex> <dstParam> <expression>\n"
      "  transport <bpm> <num> <den> <key> <scale>\n"
      "Keys after f/i/b/c/s are the node's settings (describe {type} lists them with defaults).\n"
      "Slots count from 0 in the order describe lists the inputs. dstParam (mod/expr) is the index of a\n"
      "drawn control, only known on a live node: prefer the modulate / set_expression tools by name.\n"
      "Minimal example (a shape into a blur into an output):\n"
      "  infinite-patch 1\n"
      "  node 1 Source Shape\n  end\n  node 2 Effects gaussianblur\n  end\n  node 3 Output Output\n  end\n"
      "  cable 2 0 1\n  cable 3 0 2\n";

   const Tool kTools[] = {
      { "ping", "Check that Infinite-Turbo is running and reachable (starts it if needed).",
        R"({"type":"object","properties":{}})" },
      { "describe", "Without arguments: every node type with its category and a one-line summary. With {type}: that "
                    "node's inputs (slot, label, kind), outputs (index, label, kind), settings (key, type, default) and help.",
        R"({"type":"object","properties":{"type":{"type":"string","description":"node type name, e.g. gaussianblur"}}})" },
      { "authoring_guide", "How to build good Infinite-Turbo patches with these tools: the loop, cable kinds, settings vs "
                           "params, useful nodes, recipes and pitfalls. Read it once before building.",
        R"({"type":"object","properties":{}})" },
      { "patch_format", "The patch text grammar (for load_patch_text / validate_patch_text) with an example.",
        R"({"type":"object","properties":{}})" },
      { "explain", "Reads back the live graph: every node (or one, with index) with its inputs and what feeds them, "
                   "outputs, drawn params (name, range, value, modulation, expression), settings and warnings; "
                   "plus transport and patch state.",
        R"({"type":"object","properties":{"index":{"type":"integer"}}})" },
      { "get_graph", "The live graph as JSON (patch records plus link ids for disconnect).",
        R"({"type":"object","properties":{}})" },
      { "get_patch_text", "The live graph as patch text (same as the saved .inf).",
        R"({"type":"object","properties":{}})" },
      { "validate_patch_text", "Checks patch text without changing anything: unknown types, bad indices, slots, settings.",
        R"({"type":"object","properties":{"text":{"type":"string"}},"required":["text"]})" },
      { "load_patch_text", "Replaces the canvas with the patch text (one undo step; keeps the open file name). "
                           "Refuses on errors unless force=true. Nodes without pos are auto laid out.",
        R"({"type":"object","properties":{"text":{"type":"string"},"force":{"type":"boolean"}},"required":["text"]})" },
      { "list_node_types", "Node types by category (names only; describe gives details).",
        R"({"type":"object","properties":{}})" },
      { "create_node", "Adds a node. Without x/y it is placed by the auto layout. settings = {key: value} applied "
                       "right away (keys from describe). Returns its index, inputs and outputs.",
        R"({"type":"object","properties":{"type":{"type":"string"},"x":{"type":"number"},"y":{"type":"number"},"settings":{"type":"object"},"show_params":{"type":"boolean"}},"required":["type"]})" },
      { "delete_node", "Removes a node.", R"({"type":"object","properties":{"index":{"type":"integer"}},"required":["index"]})" },
      { "connect", "Wires srcIndex's output (number or label) into dstIndex's input slot (number or label, e.g. \"B\", \"audio\").",
        R"({"type":"object","properties":{"srcIndex":{"type":"integer"},"srcOutput":{"type":["integer","string"]},"dstIndex":{"type":"integer"},"dstSlot":{"type":["integer","string"]}},"required":["srcIndex","dstIndex"]})" },
      { "disconnect", "Removes a cable by link id (get_graph lists them).",
        R"({"type":"object","properties":{"linkId":{"type":"integer"}},"required":["linkId"]})" },
      { "get_params", "A node's settings as saved (key -> value).",
        R"({"type":"object","properties":{"index":{"type":"integer"}},"required":["index"]})" },
      { "set_param", "Sets one setting by key (case/spaces ignored). Colors as [r,g,b] 0..1, bools true/false.",
        R"({"type":"object","properties":{"index":{"type":"integer"},"name":{"type":"string"},"value":{}},"required":["index","name","value"]})" },
      { "modulate", "Drives a param (by its drawn name, e.g. \"amount\", or index) from a modulator output. polarity "
                    "absolute (replaces the value) or bipolar (moves around it, depth). range = [inMin,inMax,outMin,outMax].",
        R"({"type":"object","properties":{"index":{"type":"integer"},"param":{"type":["string","integer"]},"srcIndex":{"type":"integer"},"srcOutput":{"type":["integer","string"]},"polarity":{"type":"string","enum":["absolute","bipolar"]},"depth":{"type":"number"},"range":{"type":"array","items":{"type":"number"}}},"required":["index","param","srcIndex"]})" },
      { "set_expression", "Drives a param with an expression (e.g. \"sin(t)*0.5+0.5\"); empty text removes it.",
        R"({"type":"object","properties":{"index":{"type":"integer"},"param":{"type":["string","integer"]},"expression":{"type":"string"}},"required":["index","param","expression"]})" },
      { "unmodulate", "Removes modulation and expression from a param.",
        R"({"type":"object","properties":{"index":{"type":"integer"},"param":{"type":["string","integer"]}},"required":["index","param"]})" },
      { "batch", "Runs several commands as one undo step, all or nothing. commands = [{method, params}]; a string "
                 "\"$N\" in params is replaced by command N's result index (\"$N.field\" for another field).",
        R"({"type":"object","properties":{"commands":{"type":"array","items":{"type":"object","properties":{"method":{"type":"string"},"params":{"type":"object"}},"required":["method"]}}},"required":["commands"]})" },
      { "screenshot_node", "Looks at a node's image (any image output; output by number or label). Returns a picture "
                           "(JPEG by default, max_size px on the long side, default 768).",
        R"({"type":"object","properties":{"index":{"type":"integer"},"output":{"type":["integer","string"]},"max_size":{"type":"integer"},"format":{"type":"string","enum":["jpeg","png"]}},"required":["index"]})" },
      { "render_frame", "Looks at the live Output (the first one, or index) as the projector shows it; path also saves "
                        "it at full size (.png / .jpg).",
        R"({"type":"object","properties":{"index":{"type":"integer"},"max_size":{"type":"integer"},"format":{"type":"string","enum":["jpeg","png"]},"path":{"type":"string"}}})" },
      { "clip_matrix", "Plays a Clip Matrix (session view): launch / release a cell (row, col), stop_row, scene (col), "
                       "stop_all, or state (what plays and what is queued per row). Launches wait for the quantize grid.",
        R"({"type":"object","properties":{"index":{"type":"integer"},"action":{"type":"string","enum":["launch","release","stop_row","scene","stop_all","state"]},"row":{"type":"integer"},"col":{"type":"integer"}},"required":["index"]})" },
      { "pads", "Hits a pad (0-15) of an MPC (samples) or VMPC (video clips): hit, down / up (gate pads), stop_all (VMPC).",
        R"({"type":"object","properties":{"index":{"type":"integer"},"pad":{"type":"integer"},"action":{"type":"string","enum":["hit","down","up","stop_all"]},"velocity":{"type":"number"}},"required":["index","pad"]})" },
      { "looper", "Drives a Looper: record, stop_record, play, stop, overdub, stop_overdub, clear, undo, redo, state.",
        R"({"type":"object","properties":{"index":{"type":"integer"},"action":{"type":"string"}},"required":["index","action"]})" },
      { "drum_pattern", "Drum Sequencer pattern library (Turbo): without pattern, lists the 141 grooves (rock, funk, "
                        "breaks, hip hop, electronic, latin, Brazil, Middle East, India, Africa, jazz; category filters) "
                        "and the lane roles. With index + pattern (number or name) + part (A verse, B bridge, C chorus), "
                        "fills that node's grid, steps, rate and swing, and loads the bundled kit into empty lanes "
                        "(kit:false skips it). Does not change the tempo. Use A / B / C for song sections.",
        R"({"type":"object","properties":{"index":{"type":"integer"},"pattern":{"type":["integer","string"]},"part":{"type":["string","integer"]},"category":{"type":"string"},"kit":{"type":"boolean"}}})" },
      { "perf_list", "The Performance Mode panel: pages and every control with what it drives.",
        R"({"type":"object","properties":{}})" },
      { "perf_add", "Puts a parameter on the Performance Mode panel (by its drawn name, as explain lists it). kind: knob, "
                    "fader, slider, toggle, xy (param + param_y), trigger, numbox, selector, bipolar, stepgate; default "
                    "follows the param (toggle for checkboxes, selector for dropdowns). Optional label and page.",
        R"({"type":"object","properties":{"index":{"type":"integer"},"param":{"type":["string","integer"]},"param_y":{"type":["string","integer"]},"kind":{"type":["string","integer"]},"label":{"type":"string"},"page":{"type":"integer"}},"required":["index","param"]})" },
      { "perf_remove", "Removes a control from the Performance Mode panel (element number from perf_list).",
        R"({"type":"object","properties":{"element":{"type":"integer"}},"required":["element"]})" },
      { "perf_show", "Opens / closes the Performance Mode panel, switches Perform (true) / Edit mode, page, dock side.",
        R"({"type":"object","properties":{"open":{"type":"boolean"},"perform":{"type":"boolean"},"page":{"type":"integer"},"dock":{"type":"string","enum":["bottom","top","left","right"]}}})" },
      { "auto_layout", "Lays the graph out left to right by signal flow (all nodes, or the given indices).",
        R"({"type":"object","properties":{"nodes":{"type":"array","items":{"type":"integer"}},"fit":{"type":"boolean"}}})" },
      { "set_node_position", "Moves a node on the canvas.",
        R"({"type":"object","properties":{"index":{"type":"integer"},"x":{"type":"number"},"y":{"type":"number"}},"required":["index","x","y"]})" },
      { "get_node_position", "A node's canvas position.",
        R"({"type":"object","properties":{"index":{"type":"integer"}},"required":["index"]})" },
      { "fit_view", "Frames every node in the canvas.", R"({"type":"object","properties":{}})" },
      { "transport", "Play / stop, tempo, seek (beats) and audio engine on/off. Returns the transport state.",
        R"({"type":"object","properties":{"play":{"type":"boolean"},"bpm":{"type":"number"},"seek_beats":{"type":"number"},"audio":{"type":"boolean"}}})" },
      { "new_patch", "Clears the canvas (a new document).", R"({"type":"object","properties":{}})" },
      { "load_patch", "Opens a .inf file by path.",
        R"({"type":"object","properties":{"path":{"type":"string"}},"required":["path"]})" },
      { "save_patch", "Saves the canvas to a .inf path.",
        R"({"type":"object","properties":{"path":{"type":"string"}},"required":["path"]})" },
      { "undo", "Undo one step.", R"({"type":"object","properties":{}})" },
      { "redo", "Redo one step.", R"({"type":"object","properties":{}})" },
   };

   const char* kInstructions =
      "Infinite-Turbo is a node-based realtime video / audio / 3D environment (Windows). This server edits the "
      "patch open in the running app. Typical flow: describe (node types) -> describe {type} (inputs, outputs, "
      "settings) -> build with create_node + connect (or batch, with $N references), or write patch text "
      "(patch_format) and validate_patch_text before load_patch_text -> explain to check the result. An image "
      "chain needs an Output node at the end to be seen. Params that modulate / set_expression take are the "
      "drawn control names explain lists. render_frame / screenshot_node show the picture. Turbo-only tools: "
      "clip_matrix, pads (MPC / VMPC), looper, drum_pattern (Drum Sequencer grooves), perf_* (Performance Mode). Read authoring_guide once before "
      "building. Everything is undoable (undo).";

   json ToolList()
   {
      json tools = json::array();
      for (const Tool& t : kTools)
      {
         json schema;
         try
         {
            schema = json::parse(t.schema);
         }
         catch (...)
         {
            schema = { { "type", "object" } };
         }
         tools.push_back({ { "name", t.name }, { "description", t.description }, { "inputSchema", schema } });
      }
      return tools;
   }

   json ToolResult(const std::string& text, bool isError)
   {
      json r = { { "content", json::array({ { { "type", "text" }, { "text", text } } }) } };
      if (isError)
         r["isError"] = true;
      return r;
   }

   json CallTool(const std::string& name, const json& args)
   {
      if (name == "patch_format")
         return ToolResult(kPatchFormat, false);
      if (name == "authoring_guide")
      {
         std::string guide = AISkillContent::kPatchSkillMarkdown;
         const size_t end = guide.find("\n---\n", 4); // drop the skill front matter
         if (guide.rfind("---", 0) == 0 && end != std::string::npos)
            guide = guide.substr(end + 5);
         return ToolResult(guide, false);
      }
      bool known = false;
      for (const Tool& t : kTools)
         known = known || name == t.name;
      if (!known)
         return ToolResult("unknown tool '" + name + "'", true);
      json result;
      std::string error;
      if (!CallApp(name, args.is_object() ? args : json::object(), result, error))
         return ToolResult(error, true);
      // Turbo 0.46: an image reply becomes MCP image content plus its details.
      if (result.is_object() && result.contains("image_base64") && result["image_base64"].is_string())
      {
         const std::string data = result["image_base64"].get<std::string>();
         const std::string mime = result.value("mime", std::string("image/jpeg"));
         result.erase("image_base64");
         result.erase("mime");
         json r;
         r["content"] = json::array({ { { "type", "image" }, { "data", data }, { "mimeType", mime } },
                                      { { "type", "text" },
                                        { "text", result.dump(1, ' ', false, json::error_handler_t::replace) } } });
         return r;
      }
      std::string text;
      if (result.is_object() && result.size() == 1 && result.contains("text") && result["text"].is_string())
         text = result["text"].get<std::string>(); // patch text: hand it over verbatim
      else
         text = result.dump(1, ' ', false, json::error_handler_t::replace);
      return ToolResult(text.empty() || text == "{}" ? "ok" : text, false);
   }
}

int RunMcpBridge()
{
   gIn = GetStdHandle(STD_INPUT_HANDLE);
   gOut = GetStdHandle(STD_OUTPUT_HANDLE);
   if (gIn == INVALID_HANDLE_VALUE || gIn == nullptr || gOut == INVALID_HANDLE_VALUE || gOut == nullptr)
      return 2;
   Log(std::string("Infinite-Turbo MCP bridge ") + kServerVersion + ", control port " + std::to_string(ControlPort()));

   std::string line;
   while (ReadLine(line))
   {
      if (line.empty())
         continue;
      json msg;
      try
      {
         msg = json::parse(line);
      }
      catch (const std::exception& e)
      {
         WriteMessage({ { "jsonrpc", "2.0" }, { "id", nullptr },
                        { "error", { { "code", -32700 }, { "message", std::string("parse error: ") + e.what() } } } });
         continue;
      }
      if (!msg.is_object())
         continue; // batches / junk: not used by MCP clients
      const std::string method = msg.contains("method") && msg["method"].is_string() ? msg["method"].get<std::string>() : "";
      const bool isRequest = msg.contains("id") && !method.empty();
      if (!isRequest)
         continue; // notifications (initialized, cancelled, ...) and stray responses
      const json id = msg["id"];
      const json params = msg.contains("params") && msg["params"].is_object() ? msg["params"] : json::object();
      json reply = { { "jsonrpc", "2.0" }, { "id", id } };
      try
      {

      if (method == "initialize")
      {
         std::string version = params.contains("protocolVersion") && params["protocolVersion"].is_string()
                                  ? params["protocolVersion"].get<std::string>() : std::string("2025-06-18");
         if (version != "2024-11-05" && version != "2025-03-26" && version != "2025-06-18")
            version = "2025-06-18";
         reply["result"] = { { "protocolVersion", version },
                             { "capabilities", { { "tools", { { "listChanged", false } } }, { "prompts", { { "listChanged", false } } } } },
                             { "serverInfo", { { "name", "infinite-turbo" }, { "version", kServerVersion } } },
                             { "instructions", kInstructions } };
      }
      else if (method == "ping")
         reply["result"] = json::object();
      else if (method == "tools/list")
         reply["result"] = { { "tools", ToolList() } };
      else if (method == "tools/call")
      {
         const std::string name = params.contains("name") && params["name"].is_string() ? params["name"].get<std::string>() : "";
         const json args = params.contains("arguments") ? params["arguments"] : json::object();
         reply["result"] = CallTool(name, args);
      }
      else if (method == "resources/list")
         reply["result"] = { { "resources", json::array() } };
      else if (method == "prompts/list")
         reply["result"] = { { "prompts", json::array({ { { "name", "build_patch" },
                                                          { "description", "Build or change an Infinite-Turbo patch from a description" },
                                                          { "arguments", json::array({ { { "name", "idea" },
                                                                                         { "description", "what the patch should do" },
                                                                                         { "required", true } } }) } } }) } };
      else if (method == "prompts/get")
      {
         const json args = params.contains("arguments") && params["arguments"].is_object() ? params["arguments"] : json::object();
         const std::string idea = args.contains("idea") && args["idea"].is_string() ? args["idea"].get<std::string>() : "";
         const std::string text = std::string("Build this in Infinite-Turbo with the infinite-turbo tools: ") + idea +
                                  "\n\nFollow this guide:\n\n" + AISkillContent::kPatchSkillMarkdown;
         reply["result"] = { { "description", "Build an Infinite-Turbo patch" },
                             { "messages", json::array({ { { "role", "user" },
                                                           { "content", { { "type", "text" }, { "text", text } } } } }) } };
      }
      else
         reply["error"] = { { "code", -32601 }, { "message", "method not found: " + method } };
      }
      catch (const std::exception& e)
      {
         reply.erase("result");
         reply["error"] = { { "code", -32603 }, { "message", std::string("internal error: ") + e.what() } };
      }
      WriteMessage(reply);
   }
   Disconnect();
   return 0;
}

// `Infinite-Turbo.exe --mcp-install` (setup-mcp.bat): registers this exe as
// the "infinite-turbo" MCP server in Claude Desktop's config (every config
// found: the classic %APPDATA%\Claude one and the Microsoft Store package's),
// keeping every other entry and a .bak of the old file. Reports in a box.
int InstallMcpConfig()
{
   namespace fs = std::filesystem;
   wchar_t exeW[MAX_PATH * 2] = {};
   GetModuleFileNameW(nullptr, exeW, (DWORD)(sizeof(exeW) / sizeof(exeW[0])));
   const std::string exe = fs::path(exeW).u8string();

   std::vector<fs::path> dirs;
   std::error_code ec;
   if (const char* appData = std::getenv("APPDATA"))
      dirs.push_back(fs::u8path(appData) / "Claude");
   if (const char* local = std::getenv("LOCALAPPDATA"))
   {
      const fs::path packages = fs::u8path(local) / "Packages";
      for (fs::directory_iterator it(packages, ec), end; !ec && it != end; it.increment(ec))
         if (it->path().filename().u8string().rfind("Claude", 0) == 0)
         {
            const fs::path roaming = it->path() / "LocalCache" / "Roaming" / "Claude";
            if (fs::exists(roaming, ec))
               dirs.push_back(roaming);
         }
   }

   std::string report;
   int written = 0;
   for (size_t i = 0; i < dirs.size(); i++)
   {
      const fs::path& dir = dirs[i];
      // The classic location is created when Claude Desktop is installed the
      // classic way; only write where Claude actually lives.
      if (!fs::exists(dir, ec))
         continue;
      const fs::path file = dir / "claude_desktop_config.json";
      json config = json::object();
      if (fs::exists(file, ec))
      {
         std::ifstream in(file);
         try
         {
            in >> config;
         }
         catch (...)
         {
            report += "SKIPPED (not valid JSON, fix it by hand): " + file.u8string() + "\n";
            continue;
         }
         fs::copy_file(file, fs::path(file.u8string() + ".bak"), fs::copy_options::overwrite_existing, ec);
      }
      if (!config.is_object())
         config = json::object();
      if (!config.contains("mcpServers") || !config["mcpServers"].is_object())
         config["mcpServers"] = json::object();
      config["mcpServers"]["infinite-turbo"] = { { "command", exe }, { "args", json::array({ "--mcp" }) } };
      std::ofstream out(file, std::ios::trunc);
      out << config.dump(2) << "\n";
      if (out.good())
      {
         written++;
         report += "OK: " + file.u8string() + "\n";
      }
      else
         report += "FAILED to write: " + file.u8string() + "\n";
   }
   std::string text;
   if (written > 0)
      text = "Infinite-Turbo is now an MCP server for Claude Desktop.\n\n" + report +
             "\nQuit Claude Desktop completely (tray icon > Quit) and open it again.\n"
             "Then ask Claude, for example: \"build me a patch in Infinite with a shape into a blur\".";
   else
      text = "Claude Desktop's config folder was not found.\n\n" + report +
             "\nInstall / open Claude Desktop once, then run setup-mcp.bat again.";
   text += "\n\nClaude Code (terminal):\n  claude mcp add infinite-turbo -- \"" + exe + "\" --mcp";
   std::wstring wtext = fs::u8path(text).wstring();
   MessageBoxW(nullptr, wtext.c_str(), L"Infinite-Turbo MCP", written > 0 ? MB_ICONINFORMATION : MB_ICONWARNING);
   return written > 0 ? 0 : 1;
}
