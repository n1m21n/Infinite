// ChatPanel: the Terminal panel's chat face (docs/plans/agent-collab/README.md C2-C6). A line that the
// patch grammar reads fully runs at once through the same RPC handlers as `infinite`, as one undo step,
// and shows the CLI lines it equals; anything else is a turn of the agent (claude -p / codex exec), whose
// reply links every node and param it names. The terminal face resumes the same agent session.
#include "app/AppShared.h"
#include "app/ui/design/GlyphDraw.h"
#include "app/ui/design/UiAnim.h"
#include "app/ui/design/UiType.h"
#include "app/ui/design/components/BusyLine.h"
#include "app/ui/design/components/ChipButton.h"
#include "app/ui/design/components/CodeBlock.h"
#include "app/ui/design/components/EmptyState.h"
#include "app/ui/design/components/FieldWell.h"
#include "app/ui/design/components/GlyphToggle.h"
#include "core/CategoryColors.h"
#include "core/CliSetup.h"
#include "core/NodeFactory.h"
#include "core/chat/AgentRun.h"
#include "core/chat/PatchGrammar.h"
#include "core/chat/ReplyLinks.h"
#include "core/gl3.h"
#include "stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <random>

namespace app
{
   int gTerminalFace = 0;

   namespace
   {
      using json = nlohmann::json;

      struct Msg
      {
         enum class Kind { User, Agent, Change, Steps, Note, Error };
         Kind kind = Kind::Note;
         std::string text;                // User, Agent, Note, Error
         std::vector<std::string> lines;  // Change: the `infinite` lines; Steps: what the agent ran
         bool ok = true;
         bool open = false;    // Steps: unfolded
         bool undone = false;  // Change
         std::vector<chat::Block> blocks;
         uint64_t parsedFor = 0;  // catalog generation the blocks were linked against
      };

      struct Cand
      {
         std::string insert;  // replaces the word being typed
         std::string label, detail;
         ImU32 dot = 0;
         bool info = false;  // a line to read, not to pick
      };

      struct Texture
      {
         GLuint id = 0;
         int w = 0, h = 0;
      };

      std::vector<Msg> sMsgs;
      std::string sInput;
      std::vector<std::string> sSent;  // lines sent, for ↑ when no dropdown is open
      int sSentAt = -1;
      chat::Catalog sCat;
      uint64_t sCatGen = 0;
      double sCatTime = -1.0;
      std::shared_ptr<chat::AgentRun> sRun;
      chat::AgentKind sRunKind = chat::AgentKind::Claude;
      std::string sSession;         // the agent's conversation; empty = none yet
      bool sSessionLive = false;    // the agent knows the session (later turns resume it)
      std::string sResumedSession;  // the session the terminal face already opened
      bool sScrollToEnd = false;
      bool sRefocus = false;
      // Dropdown
      int sPick = 0;
      std::string sDropKey;      // the slot + partial it was last shown for; a change resets the pick
      std::string sDismissed;    // Esc closed it for this key
      bool sAcceptPending = false;
      int sNudge = 0;            // ↑↓ in a value slot with nothing to pick
      bool sCursorAtEnd = true;
      std::vector<Cand> sCands;  // what the dropdown shows this frame
      chat::Parse sParse;
      std::map<std::string, Texture> sTextures;

      bool Light() { return CategoryColors::IsThemeLight(); }

      ImU32 WithAlpha(ImU32 c, float a) { return (c & 0x00FFFFFFu) | ((ImU32)(std::clamp(a, 0.0f, 1.0f) * 255.0f) << 24); }

      ImU32 TextTint(float a)
      {
         const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
         return ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, a));
      }

      ImU32 CategoryU32(const std::string& category)
      {
         const CategoryColors::Color& c = CategoryColors::ColorFor(category);
         return ImGui::GetColorU32(ImVec4(c.r, c.g, c.b, 1.0f));
      }

      // ---------------------------------------------------------------- catalog

      void RefreshCatalog(bool force)
      {
         const double now = ImGui::GetTime();
         if (!force && now - sCatTime < 0.5)
            return;
         sCatTime = now;
         chat::Catalog c;
         for (const GraphNode& gn : gNodes)
         {
            chat::CatalogNode n;
            n.index = gn.index;
            n.type = gn.typeName;
            n.title = NodeTitle(gn);
            n.category = NodeFactory::Instance().CategoryOf(gn.typeName);
            n.modulator = n.category == "Modulators" || n.category == "Prediction";
            json r;
            std::string e;
            if (HandleRpcCommand("get_params", { { "index", gn.index }, { "full", true } }, r, e) && r.contains("params"))
               for (auto it = r["params"].begin(); it != r["params"].end(); ++it)
               {
                  const json& v = it.value();
                  chat::CatalogParam p;
                  p.key = it.key();
                  p.value = v.value("value", std::string());
                  p.label = v.value("label", std::string());
                  p.kind = v.value("kind", std::string());
                  p.min = v.value("min", 0.0f);
                  p.max = v.value("max", 1.0f);
                  if (v.contains("options") && v["options"].is_array())
                     for (const json& o : v["options"])
                        if (o.is_string())
                           p.options.push_back(o.get<std::string>());
                  p.modulatable = v.value("modulatable", false);
                  p.modulated = v.value("modulated", false);
                  p.expression = v.value("expression", false);
                  n.params.push_back(p);
               }
            c.nodes.push_back(n);
         }
         if (sCat.types.empty())
            for (const std::string& cat : NodeFactory::Instance().GetCategories())
               for (const std::string& t : NodeFactory::Instance().GetNodesInCategory(cat))
                  c.types.push_back({ t, cat });
         else
            c.types = sCat.types;
         json sel;
         std::string e;
         if (HandleTerminalRpc("selection", json::object(), sel, e) && sel.contains("nodes"))
            for (const json& n : sel["nodes"])
               c.selected.push_back(n.value("index", -1));
         // Only a change of what the chat can name re-links the replies.
         bool same = c.nodes.size() == sCat.nodes.size();
         for (size_t i = 0; same && i < c.nodes.size(); i++)
            same = c.nodes[i].index == sCat.nodes[i].index && c.nodes[i].title == sCat.nodes[i].title && c.nodes[i].params.size() == sCat.nodes[i].params.size();
         sCat = std::move(c);
         if (!same)
            sCatGen++;
      }

      // The shortest `@` text that names exactly this node.
      std::string RefText(const chat::CatalogNode& n)
      {
         int same = 0;
         for (const chat::CatalogNode& o : sCat.nodes)
            same += chat::Lower(o.title) == chat::Lower(n.title);
         for (const chat::CatalogNode& o : sCat.nodes)
            if (&o != &n && same == 1 && chat::Lower(o.type) == chat::Lower(n.title) && o.title != n.title)
               same++;  // a type of that name would win the lookup
         return "@" + n.title + (same == 1 ? "" : "#" + std::to_string(n.index));
      }

      // `@title` chips the user typed become `@title#index`, so the agent reads one node per name.
      std::string ExpandRefs(const std::string& line, const chat::Parse& p)
      {
         std::string out;
         size_t at = 0;
         for (const chat::Span& s : p.spans)
            if (s.kind == chat::SpanKind::Node && s.node >= 0 && s.begin >= at)
               if (const chat::CatalogNode* n = sCat.Node(s.node))
               {
                  const std::string written = line.substr(s.begin, s.end - s.begin);
                  if (written.find('#') != std::string::npos)
                     continue;
                  size_t pin = written.find(':');
                  out += line.substr(at, s.begin - at) + "@" + n->title + "#" + std::to_string(n->index) + (pin != std::string::npos ? written.substr(pin) : "");
                  at = s.end;
               }
         return out + line.substr(at);
      }

      // ---------------------------------------------------------------- sending

      void Push(Msg::Kind kind, const std::string& text)
      {
         Msg m;
         m.kind = kind;
         m.text = text;
         sMsgs.push_back(m);
         sScrollToEnd = true;
      }

      void RunDirect(const chat::Parse& p)
      {
         json res;
         std::string err;
         bool ok;
         if (p.calls.size() == 1)
            ok = HandleRpcCommand(p.calls[0]["method"], p.calls[0].value("params", json::object()), res, err);
         else
            ok = HandleRpcCommand("batch", { { "calls", p.calls } }, res, err);
         Msg m;
         m.kind = Msg::Kind::Change;
         m.lines = p.cli;
         m.ok = ok && err.empty();
         m.text = err;
         sMsgs.push_back(m);
         sScrollToEnd = true;
         RefreshCatalog(true);
      }

      std::string NewSessionId()
      {
         std::random_device rd;
         std::mt19937_64 g(((uint64_t)rd() << 32) ^ rd());
         char buf[40];
         const uint64_t a = g(), b = g();
         std::snprintf(buf, sizeof buf, "%08x-%04x-4%03x-%04x-%012llx", (unsigned)(a >> 32), (unsigned)(a >> 16) & 0xFFFF, (unsigned)a & 0xFFF,
                       (unsigned)((b >> 48) & 0x3FFF) | 0x8000, (unsigned long long)(b & 0xFFFFFFFFFFFFull));
         return buf;
      }

      const CliSetup::Agent* ChatAgent() { return CliSetup::FindAgent(CliSetup::Prefs().agent); }

      bool AgentKindOf(const CliSetup::Agent* a, chat::AgentKind& kind)
      {
         if (a == nullptr)
            return false;
         const std::string id = a->id;
         kind = id == "codex" ? chat::AgentKind::Codex : chat::AgentKind::Claude;
         return id == "claude" || id == "codex";
      }

      void SendToAgent(const std::string& prompt)
      {
         const CliSetup::Agent* agent = ChatAgent();
         chat::AgentKind kind;
         if (!AgentKindOf(agent, kind))
         {
            char note[160];
            std::snprintf(note, sizeof note, T("%s works in the terminal."), agent != nullptr ? agent->name : "This agent");
            Push(Msg::Kind::Note, note);
            return;
         }
         if (!CliSetup::AgentInstalledHere(*agent))
         {
            char note[160];
            std::snprintf(note, sizeof note, T("%s is not installed."), agent->name);
            Push(Msg::Kind::Error, note);
            return;
         }
         if (sRunKind != kind)
         {
            sSession.clear();
            sSessionLive = false;
         }
         if (sSession.empty() && kind == chat::AgentKind::Claude)
            sSession = NewSessionId();
         chat::AgentRun::Options o;
         o.kind = kind;
         o.argv = chat::AgentArgs(kind, prompt, sSession, sSessionLive);
         TerminalCommandEnv(o.env, o.cwd);
         sRunKind = kind;
         sRun = chat::AgentRun::Start(o);
      }

      void Send()
      {
         std::string line = sInput;
         while (!line.empty() && std::isspace((unsigned char)line.back()))
            line.pop_back();
         if (line.empty() || sRun != nullptr)
            return;
         RefreshCatalog(true);
         const chat::Parse p = chat::ParseLine(line, sCat);
         Push(Msg::Kind::User, line);
         sSent.push_back(line);
         sSentAt = -1;
         sInput.clear();
         sDismissed.clear();
         if (p.direct)
            RunDirect(p);
         else
         {
            std::string prompt = ExpandRefs(line, p);
            if (prompt.rfind("@agent", 0) == 0)
               prompt = prompt.substr(6);
            SendToAgent(prompt);
         }
      }

      void PollAgent()
      {
         if (sRun == nullptr)
            return;
         std::vector<chat::AgentEvent> events;
         sRun->Poll(events);
         for (const chat::AgentEvent& e : events)
         {
            sScrollToEnd = true;
            switch (e.kind)
            {
               case chat::AgentEvent::Kind::Session:
                  sSession = e.text;
                  sSessionLive = true;
                  break;
               case chat::AgentEvent::Kind::Text:
                  Push(Msg::Kind::Agent, e.text);
                  break;
               case chat::AgentEvent::Kind::Tool:
                  if (sMsgs.empty() || sMsgs.back().kind != Msg::Kind::Steps)
                     Push(Msg::Kind::Steps, "");
                  sMsgs.back().lines.push_back(e.text);
                  break;
               case chat::AgentEvent::Kind::ToolResult:
                  if (e.error && !sMsgs.empty() && sMsgs.back().kind == Msg::Kind::Steps)
                     sMsgs.back().ok = false;
                  break;
               case chat::AgentEvent::Kind::End:
                  if (e.error)
                     Push(Msg::Kind::Error, e.text.empty() ? T("The agent stopped with an error.") : e.text);
                  for (const std::string& d : e.denied)
                     Push(Msg::Kind::Note, std::string(T("Not allowed:")) + " " + d);
                  break;
            }
         }
         if (sRun->Done())
         {
            sRun = nullptr;
            RefreshCatalog(true);
         }
      }

      // ---------------------------------------------------------------- dropdown

      // 0 best; -1 no match. `want` is lower case.
      int Score(const std::string& text, const std::string& want)
      {
         if (want.empty())
            return 1;
         const std::string t = chat::Lower(text);
         if (t.rfind(want, 0) == 0)
            return 0;
         const size_t at = t.find(want);
         if (at == std::string::npos)
            return -1;
         return at > 0 && !std::isalnum((unsigned char)t[at - 1]) ? 1 : 2;
      }

      std::string ValueText(const chat::CatalogParam& p)
      {
         if (p.kind == "enum")
         {
            const int i = std::atoi(p.value.c_str());
            if (i >= 0 && i < (int)p.options.size())
               return p.options[(size_t)i];
         }
         if (p.kind == "bool")
            return p.value == "1" ? "on" : "off";
         return p.value;
      }

      std::string Num(float v, bool integer)
      {
         char buf[32];
         if (integer)
            std::snprintf(buf, sizeof buf, "%d", (int)std::lround(v));
         else
            std::snprintf(buf, sizeof buf, "%.4g", v);
         return buf;
      }

      void AddNodes(std::vector<Cand>& out, const std::string& want, bool modulatorsFirst, int skip)
      {
         std::vector<std::pair<int, Cand>> ranked;
         for (const chat::CatalogNode& n : sCat.nodes)
         {
            if (n.index == skip)
               continue;
            int s = Score(n.title, want);
            if (s < 0)
               s = Score(n.type, want) >= 0 ? 3 : Score(std::to_string(n.index), want) == 0 ? 3 : -1;
            if (s < 0)
               continue;
            const bool sel = std::find(sCat.selected.begin(), sCat.selected.end(), n.index) != sCat.selected.end();
            const int rank = s * 4 + (modulatorsFirst && !n.modulator ? 2 : 0) + (sel ? 0 : 1);
            Cand c;
            c.insert = RefText(n) + " ";
            c.label = n.title;
            c.detail = (n.type != n.title ? n.type + " " : std::string()) + "#" + std::to_string(n.index);
            c.dot = CategoryU32(n.category);
            ranked.push_back({ rank, c });
         }
         std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
         for (auto& r : ranked)
            out.push_back(r.second);
      }

      void AddParams(std::vector<Cand>& out, const chat::CatalogNode& n, const std::string& want)
      {
         std::vector<std::pair<int, Cand>> ranked;
         for (const chat::CatalogParam& p : n.params)
         {
            int s = std::max(Score(p.key, want), Score(p.label, want));
            if (Score(p.key, want) >= 0 && Score(p.label, want) >= 0)
               s = std::min(Score(p.key, want), Score(p.label, want));
            if (s < 0)
               continue;
            Cand c;
            c.insert = p.key + " ";
            c.label = p.label.empty() || chat::Lower(p.label) == p.key ? p.key : p.label + "  " + p.key;
            c.detail = ValueText(p);
            if (p.modulated)
               c.dot = tok::U32(tok::pin_mod, Light());
            else if (p.expression)
               c.dot = tok::U32(tok::pin_expr, Light());
            ranked.push_back({ s * 2 + (p.kind.empty() ? 1 : 0), c });
         }
         std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
         for (auto& r : ranked)
            out.push_back(r.second);
      }

      void AddWords(std::vector<Cand>& out, const std::vector<std::string>& words, const std::string& want)
      {
         for (const std::string& w : words)
            if (Score(w, want) >= 0 && chat::Lower(w) != want)
               out.push_back({ w + " ", w, "", 0, false });
      }

      std::vector<Cand> Candidates(const chat::Parse& p)
      {
         std::vector<Cand> out;
         std::string want = chat::Lower(p.partial);
         if (!want.empty() && want[0] == '@')
            want = want.substr(1);
         const chat::CatalogNode* node = sCat.Node(p.slotNode);
         switch (p.slot)
         {
            case chat::Slot::Start:
               if (!want.empty())
                  AddWords(out, { "add", "connect", "remove", "bypass", "unbypass", "undo", "redo" }, want);
               break;
            case chat::Slot::Node:
               AddNodes(out, want, false, -1);
               if (Score("agent", want) >= 0)
                  out.push_back({ "@agent ", "agent", T("ask the agent"), 0, false });
               break;
            case chat::Slot::Source:
               AddNodes(out, want, true, p.slotNode);
               break;
            case chat::Slot::AfterNode:
               if (node != nullptr)
                  AddParams(out, *node, want);
               AddWords(out, p.words.empty() ? std::vector<std::string>{ "modulate", "set", "to" } : p.words, want);
               break;
            case chat::Slot::Param:
               if (node != nullptr)
                  AddParams(out, *node, want);
               break;
            case chat::Slot::Value:
               if (node != nullptr)
                  if (const chat::CatalogParam* prm = node->Find(p.slotKey))
                  {
                     if (prm->kind == "enum")
                        for (const std::string& o : prm->options)
                        {
                           if (Score(o, want) >= 0)
                              out.push_back({ chat::Lower(o).find(' ') == std::string::npos ? chat::Lower(o) + " " : o + " ", o, "", 0, false });
                        }
                     else if (prm->kind == "bool")
                        AddWords(out, { "on", "off" }, want);
                     else if (!prm->kind.empty())
                        out.push_back({ "", Num(prm->min, prm->kind == "int") + " – " + Num(prm->max, prm->kind == "int"), std::string(T("now")) + " " + prm->value, 0, true });
                  }
               break;
            case chat::Slot::Joiner:
               AddWords(out, p.words, want);
               break;
            case chat::Slot::Type:
            {
               std::vector<std::pair<int, Cand>> ranked;
               for (const chat::CatalogType& t : sCat.types)
                  if (const int s = Score(t.name, want); s >= 0 && !want.empty())
                     ranked.push_back({ s, { t.name + " ", t.name, t.category, CategoryU32(t.category), false } });
               std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
               for (size_t i = 0; i < ranked.size() && i < 40; i++)
                  out.push_back(ranked[i].second);
               break;
            }
            default:
               break;
         }
         if (out.size() > 40)
            out.resize(40);
         return out;
      }

      bool Pickable()
      {
         for (const Cand& c : sCands)
            if (!c.info)
               return true;
         return false;
      }

      bool DropdownOpen() { return !sCands.empty() && sCursorAtEnd && sDismissed != sDropKey; }

      int InputCallback(ImGuiInputTextCallbackData* data)
      {
         if (data->EventFlag == ImGuiInputTextFlags_CallbackAlways)
         {
            sCursorAtEnd = data->CursorPos == data->BufTextLen;
            const bool picking = sAcceptPending && DropdownOpen() && Pickable();
            sAcceptPending = false;
            if (picking)
            {
               const Cand& c = sCands[(size_t)std::clamp(sPick, 0, (int)sCands.size() - 1)];
               if (!c.info)
               {
                  const int begin = (int)std::min(sParse.partialBegin, (size_t)data->BufTextLen);
                  data->DeleteChars(begin, data->BufTextLen - begin);
                  data->InsertChars(begin, c.insert.c_str());
                  data->CursorPos = data->BufTextLen;
               }
            }
            if (sNudge != 0)
            {
               // ↑↓ on a number in a value slot steps it.
               const chat::CatalogNode* n = sCat.Node(sParse.slotNode);
               const chat::CatalogParam* p = n != nullptr ? n->Find(sParse.slotKey) : nullptr;
               char* end = nullptr;
               const std::string word = sParse.partial;
               const float v = word.empty() ? 0.0f : std::strtof(word.c_str(), &end);
               if (p != nullptr && (p->kind == "float" || p->kind == "int") && end != nullptr && *end == '\0')
               {
                  const bool integer = p->kind == "int";
                  const float step = integer ? 1.0f : std::max(1e-4f, (p->max - p->min) / 100.0f);
                  const std::string next = Num(std::clamp(v + step * (float)sNudge, p->min, p->max), integer);
                  const int begin = (int)sParse.partialBegin;
                  data->DeleteChars(begin, data->BufTextLen - begin);
                  data->InsertChars(begin, next.c_str());
               }
               sNudge = 0;
            }
         }
         else if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion)
            sAcceptPending = true;  // applied by the next Always callback, after this frame's candidates
         else if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory)
         {
            const int dir = data->EventKey == ImGuiKey_UpArrow ? -1 : 1;
            if (DropdownOpen() && Pickable())
               sPick = (sPick + dir + (int)sCands.size()) % (int)sCands.size();
            else if (sParse.slot == chat::Slot::Value && !sParse.partial.empty())
               sNudge = -dir;
            else if (!sSent.empty())
            {
               sSentAt = sSentAt < 0 ? (dir < 0 ? (int)sSent.size() - 1 : -1) : sSentAt + dir;
               if (sSentAt >= (int)sSent.size())
                  sSentAt = -1;
               sSentAt = std::max(-1, sSentAt);
               data->DeleteChars(0, data->BufTextLen);
               if (sSentAt >= 0)
                  data->InsertChars(0, sSent[(size_t)sSentAt].c_str());
            }
         }
         return 0;
      }

      // ---------------------------------------------------------------- drawing

      void Focus(int node)
      {
         json r;
         std::string e;
         HandleRpcCommand("focus", { { "index", node } }, r, e);
      }

      std::string ParamTip(int node, const std::string& key)
      {
         const chat::CatalogNode* n = sCat.Node(node);
         const chat::CatalogParam* p = n != nullptr ? n->Find(key) : nullptr;
         if (p == nullptr)
            return key;
         std::string tip = p->key + " = " + ValueText(*p);
         if (p->kind == "float" || p->kind == "int")
            tip += "   " + Num(p->min, p->kind == "int") + " – " + Num(p->max, p->kind == "int");
         if (p->modulated)
            tip += std::string("   ") + T("modulated");
         if (p->expression)
            tip += std::string("   ") + T("expression");
         return tip;
      }

      // A link chip: a quiet well in the node's colour; click frames the node on the canvas.
      void DrawChip(ImDrawList* dl, ImVec2 at, const chat::Seg& s, float lineH, int id)
      {
         const ImVec2 ts = ImGui::CalcTextSize(s.text.c_str());
         const ImVec2 sz(ts.x + 2.0f * tok::space_1 + 2.0f, lineH);
         const chat::CatalogNode* n = sCat.Node(s.node);
         ImGui::SetCursorScreenPos(at);
         ImGui::PushID(id);
         const bool clicked = ImGui::InvisibleButton("##chip", sz) && n != nullptr;
         ImGui::PopID();
         const float hv = UiAnim::Hover(ImGui::GetItemID(), ImGui::IsItemHovered(), tok::motion_hover_in, tok::motion_hover_out);
         if (s.kind == chat::Seg::Kind::Missing)
         {
            const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
            dl->AddText(ImVec2(at.x + tok::space_1, at.y + (lineH - ts.y) * 0.5f), dim, s.text.c_str());
            dl->AddLine(ImVec2(at.x + tok::space_1, at.y + lineH * 0.5f), ImVec2(at.x + tok::space_1 + ts.x, at.y + lineH * 0.5f), dim, 1.0f);
            if (ImGui::IsItemHovered())
               ImGui::SetTooltip("%s", T("Not in the patch"));
            return;
         }
         const ImU32 cat = n != nullptr ? CategoryU32(n->category) : TextTint(1.0f);
         dl->AddRectFilled(at, ImVec2(at.x + sz.x, at.y + sz.y), WithAlpha(cat, 0.16f + 0.10f * hv), tok::radius_cell);
         if (s.kind == chat::Seg::Kind::Param)
         {
            const chat::CatalogParam* p = n != nullptr ? n->Find(s.key) : nullptr;
            if (p != nullptr && (p->modulated || p->expression))
               dl->AddCircleFilled(ImVec2(at.x + sz.x - 2.5f, at.y + 2.5f), 2.0f, tok::U32(p->modulated ? tok::pin_mod : tok::pin_expr, Light()));
         }
         dl->AddText(ImVec2(at.x + tok::space_1 + 1.0f, at.y + (lineH - ts.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), s.text.c_str());
         if (ImGui::IsItemHovered())
         {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (s.kind == chat::Seg::Kind::Param)
               ImGui::SetTooltip("%s", ParamTip(s.node, s.key).c_str());
            else if (n != nullptr)
               ImGui::SetTooltip("%s  #%d", n->type.c_str(), n->index);
         }
         if (clicked)
            Focus(s.node);
      }

      // Words and chips laid out in `width`, wrapping between words. Returns the height used.
      float DrawFlow(const std::vector<chat::Seg>& segs, ImVec2 origin, float width, int idBase)
      {
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const float lineH = std::floor(ImGui::GetTextLineHeight() + tok::space_1 + 1.0f);
         const float space = ImGui::CalcTextSize(" ").x;
         float x = 0.0f, y = 0.0f;
         int chip = 0;
         for (const chat::Seg& s : segs)
         {
            const bool link = s.kind == chat::Seg::Kind::Node || s.kind == chat::Seg::Kind::Param || s.kind == chat::Seg::Kind::Missing;
            if (link)
            {
               const float w = ImGui::CalcTextSize(s.text.c_str()).x + 2.0f * tok::space_1 + 2.0f;
               if (x > 0.0f && x + w > width)
                  x = 0.0f, y += lineH;
               DrawChip(dl, ImVec2(origin.x + x, origin.y + y), s, lineH - 2.0f, idBase * 1000 + chip++);
               x += w;
               continue;
            }
            ImFont* font = s.kind == chat::Seg::Kind::Bold ? UiType::Font(UiType::Weight::Semibold) : ImGui::GetFont();
            const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
            size_t i = 0;
            while (i < s.text.size())
            {
               if (s.text[i] == '\n')
               {
                  x = 0.0f, y += lineH, i++;
                  continue;
               }
               size_t e = i;
               while (e < s.text.size() && s.text[e] != ' ' && s.text[e] != '\n')
                  e++;
               const std::string word = s.text.substr(i, e - i);
               const float w = font->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0.0f, word.c_str()).x;
               if (x > 0.0f && x + w > width)
                  x = 0.0f, y += lineH;
               const ImVec2 at(origin.x + x, origin.y + y + (lineH - ImGui::GetFontSize()) * 0.5f - 1.0f);
               if (s.kind == chat::Seg::Kind::Code)
                  dl->AddRectFilled(ImVec2(at.x - 2.0f, origin.y + y + 1.0f), ImVec2(at.x + w + 2.0f, origin.y + y + lineH - 2.0f), TextTint(0.08f), tok::radius_cell);
               dl->AddText(font, ImGui::GetFontSize(), at, col, word.c_str());
               x += w;
               while (e < s.text.size() && s.text[e] == ' ')
                  x += space, e++;
               i = e;
            }
         }
         return y + lineH;
      }

      Texture* TextureFor(const std::string& rawPath)
      {
         std::string path = rawPath;
         if (!path.empty() && path[0] == '~')
            if (const char* home = std::getenv("HOME"))
               path = home + path.substr(1);
         auto it = sTextures.find(path);
         if (it != sTextures.end())
            return it->second.id != 0 ? &it->second : nullptr;
         Texture t;
         int ch = 0;
         // stb is built with STBI_NO_STDIO here (EnvironmentNode.cpp): decode from memory.
         std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
         const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
         if (unsigned char* px = bytes.empty() ? nullptr : stbi_load_from_memory(bytes.data(), (int)bytes.size(), &t.w, &t.h, &ch, 4))
         {
            glGenTextures(1, &t.id);
            glBindTexture(GL_TEXTURE_2D, t.id);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, t.w, t.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            stbi_image_free(px);
         }
         sTextures[path] = t;
         return t.id != 0 ? &sTextures[path] : nullptr;
      }

      void DrawImage(const std::string& path, float width)
      {
         Texture* t = TextureFor(path);
         if (t == nullptr)
         {
            ImGui::TextDisabled("%s", path.c_str());
            return;
         }
         const float w = std::min(width, (float)t->w);
         float h = w * (float)t->h / (float)std::max(1, t->w);
         const float cap = 320.0f;
         const float s = h > cap ? cap / h : 1.0f;
         const ImVec2 at = ImGui::GetCursorScreenPos();
         ImGui::Image((ImTextureID)(intptr_t)t->id, ImVec2(w * s, h * s));
         ImGui::GetWindowDrawList()->AddRect(at, ImVec2(at.x + w * s, at.y + h * s), TextTint(0.12f), tok::radius_field);
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", path.c_str());
         h *= s;
      }

      void DrawAgentText(Msg& m, float width, int id)
      {
         if (m.parsedFor != sCatGen || m.blocks.empty())
         {
            m.blocks = chat::ParseReply(m.text, sCat);
            m.parsedFor = sCatGen;
         }
         int k = 0;
         for (const chat::Block& b : m.blocks)
         {
            const int bid = id * 64 + k++;
            switch (b.kind)
            {
               case chat::Block::Kind::Code:
               {
                  char cid[32];
                  std::snprintf(cid, sizeof cid, "##code%d", bid);
                  CodeBlock::Draw(b.text.c_str(), cid);
                  break;
               }
               case chat::Block::Kind::Image:
                  DrawImage(b.text, width);
                  ImGui::Dummy(ImVec2(0.0f, tok::space_1));
                  break;
               default:
               {
                  const ImVec2 at = ImGui::GetCursorScreenPos();
                  float indent = 0.0f;
                  if (b.kind == chat::Block::Kind::Bullet)
                  {
                     indent = tok::space_4;
                     ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(at.x + 5.0f, at.y + std::floor(ImGui::GetTextLineHeight() * 0.5f + 2.0f)), 2.0f, TextTint(0.6f));
                  }
                  float h;
                  if (b.kind == chat::Block::Kind::Heading)
                  {
                     UiType::Scope scope(UiType::Size::Body, UiType::Weight::Semibold);
                     h = DrawFlow(b.segs, ImVec2(at.x + indent, at.y), width - indent, bid);
                  }
                  else
                     h = DrawFlow(b.segs, ImVec2(at.x + indent, at.y), width - indent, bid);
                  ImGui::SetCursorScreenPos(at);
                  ImGui::Dummy(ImVec2(width, h + (b.kind == chat::Block::Kind::Bullet ? 0.0f : tok::space_1)));
                  break;
               }
            }
         }
      }

      // A direct change: what ran, as the `infinite` lines it equals, and Undo while it is the last one.
      void DrawChange(Msg& m, float width, int id, bool last)
      {
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const ImVec2 at = ImGui::GetCursorScreenPos();
         const float lineH = ImGui::GetTextLineHeight() + 2.0f;
         const float pad = tok::space_2;
         const float h = pad * 2.0f + lineH * (float)std::max<size_t>(1, m.lines.size() + (m.ok ? 0 : 1));
         dl->AddRectFilled(at, ImVec2(at.x + width, at.y + h), TextTint(0.05f), tok::radius_tile);
         const ImU32 accent = ImGui::GetColorU32(m.ok ? AccentEmphasisSelected() : ImGui::ColorConvertU32ToFloat4(tok::U32(tok::action_record, Light())));
         glyph::Draw(dl, ImVec2(at.x + pad + 7.0f, at.y + pad + lineH * 0.5f), tok::icon_sm, m.undone ? ImGui::GetColorU32(ImGuiCol_TextDisabled) : accent, IconsInfinite::Bolt);
         float y = at.y + pad;
         const ImU32 code = m.undone ? ImGui::GetColorU32(ImGuiCol_TextDisabled) : TextTint(0.75f);
         for (const std::string& l : m.lines)
         {
            dl->AddText(ImVec2(at.x + pad + 20.0f, y + 1.0f), code, l.c_str());
            if (m.undone)
               dl->AddLine(ImVec2(at.x + pad + 20.0f, y + lineH * 0.5f), ImVec2(at.x + pad + 20.0f + ImGui::CalcTextSize(l.c_str()).x, y + lineH * 0.5f), code, 1.0f);
            y += lineH;
         }
         if (!m.ok)
            dl->AddText(ImVec2(at.x + pad + 20.0f, y + 1.0f), accent, m.text.c_str());
         if (last && m.ok && !m.undone)
         {
            const char* label = T("Undo");
            const float bw = ImGui::CalcTextSize(label).x + 2.0f * tok::space_2;
            ImGui::SetCursorScreenPos(ImVec2(at.x + width - bw - tok::space_1, at.y + (h - 22.0f) * 0.5f));
            ImGui::PushID(id);
            if (ChipButton::Draw(label, false))
            {
               json r;
               std::string e;
               if (HandleRpcCommand("undo", json::object(), r, e))
                  m.undone = true;
               RefreshCatalog(true);
            }
            ImGui::PopID();
         }
         ImGui::SetCursorScreenPos(at);
         ImGui::Dummy(ImVec2(width, h));
      }

      // What the agent ran, folded to one row.
      void DrawSteps(Msg& m, float width, int id, bool running)
      {
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const ImVec2 at = ImGui::GetCursorScreenPos();
         const float rowH = ImGui::GetTextLineHeight() + tok::space_1;
         ImGui::PushID(id);
         if (ImGui::InvisibleButton("##steps", ImVec2(width, rowH)))
            m.open = !m.open;
         ImGui::PopID();
         const bool hot = ImGui::IsItemHovered();
         const ImU32 dim = hot ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled);
         glyph::Draw(dl, ImVec2(at.x + 7.0f, at.y + rowH * 0.5f), tok::icon_sm, dim, m.open ? IconsInfinite::ChevronDown : IconsInfinite::ChevronRight);
         char label[64];
         std::snprintf(label, sizeof label, m.lines.size() == 1 ? T("%d step") : T("%d steps"), (int)m.lines.size());
         dl->AddText(ImVec2(at.x + 18.0f, at.y + (rowH - ImGui::GetFontSize()) * 0.5f), dim, label);
         if (!m.ok)
            dl->AddCircleFilled(ImVec2(at.x + 22.0f + ImGui::CalcTextSize(label).x + 4.0f, at.y + rowH * 0.5f), 2.5f, tok::U32(tok::action_record, Light()));
         if (running)
         {
            ImGui::SetCursorScreenPos(ImVec2(at.x + 26.0f + ImGui::CalcTextSize(label).x + 8.0f, at.y + (rowH - BusyLine::kSpinner) * 0.5f));
            BusyLine::Spinner();
         }
         ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + rowH));
         if (m.open)
         {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
            for (const std::string& l : m.lines)
            {
               ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f);
               ImGui::TextDisabled("%s", l.c_str());
            }
            ImGui::PopTextWrapPos();
         }
         ImGui::Dummy(ImVec2(width, 2.0f));
      }

      void DrawUser(const Msg& m, float width, int id)
      {
         // The line as typed, in a quiet well on the right, its node names as chips.
         const chat::Parse p = chat::ParseLine(m.text, sCat);
         std::vector<chat::Seg> segs;
         size_t at = 0;
         for (const chat::Span& s : p.spans)
         {
            if (s.kind != chat::SpanKind::Node || s.node < 0 || s.begin < at)
               continue;
            if (s.begin > at)
               segs.push_back({ chat::Seg::Kind::Text, m.text.substr(at, s.begin - at), -1, "" });
            const chat::CatalogNode* n = sCat.Node(s.node);
            segs.push_back({ n != nullptr ? chat::Seg::Kind::Node : chat::Seg::Kind::Missing, n != nullptr ? n->title : m.text.substr(s.begin, s.end - s.begin), s.node, "" });
            at = s.end;
         }
         if (at < m.text.size())
            segs.push_back({ chat::Seg::Kind::Text, m.text.substr(at), -1, "" });
         const float maxW = std::max(80.0f, width * 0.85f);
         float textW = 0.0f;
         for (const chat::Seg& s : segs)
            textW += ImGui::CalcTextSize(s.text.c_str()).x + (s.kind == chat::Seg::Kind::Text ? 0.0f : 2.0f * tok::space_1 + 2.0f);
         const float w = std::min(maxW, textW + 2.0f * tok::space_2 + 2.0f);
         const ImVec2 origin = ImGui::GetCursorScreenPos();
         const ImVec2 box(origin.x + width - w, origin.y);
         ImDrawList* dl = ImGui::GetWindowDrawList();
         dl->ChannelsSplit(2);
         dl->ChannelsSetCurrent(1);
         const float h = DrawFlow(segs, ImVec2(box.x + tok::space_2, box.y + tok::space_1 + 1.0f), w - 2.0f * tok::space_2, id);
         dl->ChannelsSetCurrent(0);
         dl->AddRectFilled(box, ImVec2(box.x + w, box.y + h + 2.0f * tok::space_1 + 2.0f), TextTint(0.08f), tok::radius_tile);
         dl->ChannelsMerge();
         ImGui::SetCursorScreenPos(origin);
         ImGui::Dummy(ImVec2(width, h + 2.0f * tok::space_1 + 2.0f));
      }

      void DrawMessages(float height)
      {
         ImGui::BeginChild("##chatlog", ImVec2(0.0f, height), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollWithMouse * 0);
         const float width = ImGui::GetContentRegionAvail().x - tok::space_1;
         if (sMsgs.empty() && sRun == nullptr)
         {
            const ImVec2 mn = ImGui::GetWindowPos(), mx(mn.x + ImGui::GetWindowWidth(), mn.y + ImGui::GetWindowHeight());
            EmptyState::DrawCaption(mn, mx, T("Type @ to command a node, or ask anything."));
         }
         int lastChange = -1;
         for (int i = 0; i < (int)sMsgs.size(); i++)
            if (sMsgs[(size_t)i].kind == Msg::Kind::Change)
               lastChange = i;
         for (int i = 0; i < (int)sMsgs.size(); i++)
         {
            Msg& m = sMsgs[(size_t)i];
            if (i > 0 && (m.kind == Msg::Kind::User || sMsgs[(size_t)i - 1].kind == Msg::Kind::User))
               ImGui::Dummy(ImVec2(0.0f, tok::space_2));
            switch (m.kind)
            {
               case Msg::Kind::User:
                  DrawUser(m, width, i);
                  break;
               case Msg::Kind::Agent:
                  DrawAgentText(m, width, i);
                  break;
               case Msg::Kind::Change:
                  DrawChange(m, width, i, i == lastChange);
                  break;
               case Msg::Kind::Steps:
                  DrawSteps(m, width, i, sRun != nullptr && i == (int)sMsgs.size() - 1);
                  break;
               case Msg::Kind::Note:
               case Msg::Kind::Error:
               {
                  ImGui::PushStyleColor(ImGuiCol_Text, m.kind == Msg::Kind::Error ? ImGui::ColorConvertU32ToFloat4(tok::U32(tok::action_record, Light())) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
                  ImGui::TextUnformatted(m.text.c_str());
                  ImGui::PopTextWrapPos();
                  ImGui::PopStyleColor();
                  break;
               }
            }
            ImGui::Dummy(ImVec2(0.0f, 2.0f));
         }
         if (sRun != nullptr && (sMsgs.empty() || sMsgs.back().kind != Msg::Kind::Steps))
         {
            BusyLine::Spinner();
         }
         if (sScrollToEnd)
         {
            ImGui::SetScrollHereY(1.0f);
            sScrollToEnd = false;
         }
         ImGui::EndChild();
      }

      ImU32 SpanColor(const chat::Span& s)
      {
         switch (s.kind)
         {
            case chat::SpanKind::Verb:
            case chat::SpanKind::Joiner:
               return ImGui::GetColorU32(ImGuiCol_TextDisabled);
            case chat::SpanKind::Formula:
               return tok::U32(tok::pin_expr, Light());
            case chat::SpanKind::Agent:
               return tok::U32(tok::pin_pred, Light());
            case chat::SpanKind::Bad:
               return tok::U32(tok::action_record, Light());
            default:
               return ImGui::GetColorU32(ImGuiCol_Text);
         }
      }

      // The typed line drawn over the field: node chips in their colour, params on a quiet well,
      // grammar words dim, a word that does not fit underlined red. Only while the line fits (no scroll).
      void DrawLineOverlay(ImVec2 textAt, float maxW)
      {
         if (sInput.empty() || ImGui::CalcTextSize(sInput.c_str()).x > maxW)
            return;
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const float fh = ImGui::GetFontSize();
         auto xAt = [&](size_t i) { return textAt.x + ImGui::CalcTextSize(sInput.c_str(), sInput.c_str() + std::min(i, sInput.size())).x; };
         size_t at = 0;
         const ImU32 plain = ImGui::GetColorU32(ImGuiCol_Text);
         auto run = [&](size_t a, size_t b, ImU32 col)
         {
            if (b > a)
               dl->AddText(ImVec2(xAt(a), textAt.y), col, sInput.c_str() + a, sInput.c_str() + b);
         };
         std::vector<chat::Span> spans = sParse.spans;
         std::sort(spans.begin(), spans.end(), [](const chat::Span& a, const chat::Span& b) { return a.begin < b.begin; });
         for (const chat::Span& s : spans)
         {
            if (s.begin < at || s.end > sInput.size())
               continue;
            run(at, s.begin, plain);
            const float x0 = xAt(s.begin), x1 = xAt(s.end);
            if (s.kind == chat::SpanKind::Node)
            {
               const chat::CatalogNode* n = sCat.Node(s.node);
               const ImU32 cat = n != nullptr ? CategoryU32(n->category) : plain;
               dl->AddRectFilled(ImVec2(x0 - 2.0f, textAt.y - 1.0f), ImVec2(x1 + 2.0f, textAt.y + fh + 1.0f), WithAlpha(cat, 0.22f), tok::radius_cell);
            }
            else if (s.kind == chat::SpanKind::Param)
               dl->AddRectFilled(ImVec2(x0 - 2.0f, textAt.y - 1.0f), ImVec2(x1 + 2.0f, textAt.y + fh + 1.0f), TextTint(0.10f), tok::radius_cell);
            run(s.begin, s.end, SpanColor(s));
            if (s.kind == chat::SpanKind::Bad)
               dl->AddLine(ImVec2(x0, textAt.y + fh + 1.0f), ImVec2(x1, textAt.y + fh + 1.0f), SpanColor(s), 1.0f);
            at = s.end;
         }
         if (!sParse.error.empty() && sParse.errorEnd > sParse.errorBegin && sParse.errorEnd <= sInput.size() && sParse.errorBegin >= at)
         {
            run(at, sParse.errorBegin, plain);
            const ImU32 red = tok::U32(tok::action_record, Light());
            run(sParse.errorBegin, sParse.errorEnd, red);
            dl->AddLine(ImVec2(xAt(sParse.errorBegin), textAt.y + fh + 1.0f), ImVec2(xAt(sParse.errorEnd), textAt.y + fh + 1.0f), red, 1.0f);
            at = sParse.errorEnd;
         }
         run(at, sInput.size(), plain);
      }

      void DrawDropdown(ImVec2 anchor, float width)
      {
         if (!DropdownOpen())
            return;
         const float rowH = 22.0f;
         const int shown = std::min<int>((int)sCands.size(), 8);
         const float h = rowH * (float)shown + 2.0f * tok::space_1;
         ImGui::SetNextWindowPos(ImVec2(anchor.x, anchor.y - tok::space_1), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
         ImGui::SetNextWindowSize(ImVec2(width, h));
         ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(tok::space_1, tok::space_1));
         ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, tok::radius_tile);
         const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                        ImGuiWindowFlags_NoNav;
         if (ImGui::Begin("##chatdropdown", nullptr, flags))
         {
            ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
            sPick = std::clamp(sPick, 0, (int)sCands.size() - 1);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float rowW = ImGui::GetContentRegionAvail().x;
            for (int i = 0; i < (int)sCands.size(); i++)
            {
               const Cand& c = sCands[(size_t)i];
               const ImVec2 at = ImGui::GetCursorScreenPos();
               ImGui::PushID(i);
               const bool clicked = ImGui::InvisibleButton("##cand", ImVec2(rowW, rowH)) && !c.info;
               ImGui::PopID();
               if (ImGui::IsItemHovered() && !c.info && ImGui::GetIO().MouseDelta.x * ImGui::GetIO().MouseDelta.x + ImGui::GetIO().MouseDelta.y * ImGui::GetIO().MouseDelta.y > 0.0f)
                  sPick = i;
               if (i == sPick && !c.info)
               {
                  dl->AddRectFilled(at, ImVec2(at.x + rowW, at.y + rowH), TextTint(0.10f), tok::radius_field);
                  ImGui::SetScrollHereY(0.5f * 0 + 0.5f);
               }
               float x = at.x + tok::space_2;
               if (c.dot != 0)
               {
                  dl->AddCircleFilled(ImVec2(x + 3.0f, at.y + rowH * 0.5f), 3.0f, c.dot);
                  x += 12.0f;
               }
               const float ty = at.y + (rowH - ImGui::GetFontSize()) * 0.5f;
               dl->AddText(ImVec2(x, ty), c.info ? ImGui::GetColorU32(ImGuiCol_TextDisabled) : ImGui::GetColorU32(ImGuiCol_Text), c.label.c_str());
               if (!c.detail.empty())
               {
                  const float dw = ImGui::CalcTextSize(c.detail.c_str()).x;
                  dl->AddText(ImVec2(at.x + rowW - tok::space_2 - dw, ty), ImGui::GetColorU32(ImGuiCol_TextDisabled), c.detail.c_str());
               }
               if (clicked)
               {
                  sPick = i;
                  sAcceptPending = true;
                  sRefocus = true;
               }
            }
         }
         ImGui::End();
         ImGui::PopStyleVar(2);
      }

      void DrawComposer(float width)
      {
         const float h = 30.0f;
         const float sendW = 26.0f;
         const ImVec2 at = ImGui::GetCursorScreenPos();
         const float fieldW = width - sendW - tok::space_1;
         sParse = chat::ParseLine(sInput, sCat);
         sCands = Candidates(sParse);
         const std::string key = std::to_string((int)sParse.slot) + "|" + std::to_string(sParse.partialBegin) + "|" + sParse.partial;
         if (key != sDropKey)
         {
            sDropKey = key;
            sPick = 0;
            while (sPick < (int)sCands.size() && sCands[(size_t)sPick].info)
               sPick++;
         }
         if (sInput.empty())
            sDismissed.clear();

         // Field
         ImDrawList* dl = ImGui::GetWindowDrawList();
         ImGui::SetCursorScreenPos(at);
         ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(tok::space_2, (h - ImGui::GetFontSize()) * 0.5f));
         FieldWell::PushStyle();
         const bool overlay = !sInput.empty() && ImGui::CalcTextSize(sInput.c_str()).x <= fieldW - 2.0f * tok::space_2 - 22.0f;
         if (overlay)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
         ImGui::SetNextItemWidth(fieldW);
         if (sRefocus)
         {
            ImGui::SetKeyboardFocusHere();
            sRefocus = false;
         }
         const bool open = DropdownOpen() && Pickable();
         const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackAlways |
                                           ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory;
         const bool enter = ImGui::InputTextWithHint("##chatinput", T("Message, or @node"), &sInput, flags, InputCallback);
         const bool active = ImGui::IsItemActive();
         if (overlay)
            ImGui::PopStyleColor();
         FieldWell::PopStyle();
         ImGui::PopStyleVar();
         if (overlay)
            DrawLineOverlay(ImVec2(at.x + tok::space_2, at.y + (h - ImGui::GetFontSize()) * 0.5f), fieldW - 2.0f * tok::space_2);
         if (enter)
         {
            if (open)
               sAcceptPending = true;
            else
               Send();
            sRefocus = true;
         }
         if ((active || sRefocus) && ImGui::IsKeyPressed(ImGuiKey_Escape) && DropdownOpen())
         {
            sDismissed = sDropKey;
            sRefocus = true;
         }

         // Route badge: bolt = runs here at once, spark = goes to the agent.
         if (!sInput.empty())
         {
            const bool direct = sParse.direct;
            const ImVec2 c(at.x + fieldW - 13.0f, at.y + h * 0.5f);
            const ImU32 col = direct ? ImGui::GetColorU32(AccentEmphasisSelected()) : tok::U32(tok::pin_pred, Light());
            glyph::Draw(dl, c, tok::icon_sm, col, direct ? IconsInfinite::Bolt : IconsInfinite::Spark);
            if (ImGui::IsMouseHoveringRect(ImVec2(c.x - 9, c.y - 9), ImVec2(c.x + 9, c.y + 9)))
               ImGui::SetTooltip("%s", direct ? T("Runs now") : T("Goes to the agent"));
         }

         // Send
         ImGui::SetCursorScreenPos(ImVec2(at.x + fieldW + tok::space_1, at.y + (h - 22.0f) * 0.5f));
         const bool canSend = !sInput.empty() && sRun == nullptr;
         if (sRun != nullptr)
         {
            ImGui::InvisibleButton("##chatstop", ImVec2(sendW, 22.0f));
            const ImVec2 c = ImVec2((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5f, (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f);
            glyph::Draw(dl, c, tok::icon_md, ImGui::IsItemHovered() ? ImGui::GetColorU32(ImGuiCol_Text) : TextTint(0.6f), IconsInfinite::StopFill);
            if (ImGui::IsItemHovered())
               ImGui::SetTooltip("%s", T("Stop"));
            if (ImGui::IsItemClicked())
               sRun->Stop();
         }
         else
         {
            const bool clicked = ImGui::InvisibleButton("##chatsend", ImVec2(sendW, 22.0f));
            const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
            const ImVec2 c((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
            if (canSend)
               dl->AddCircleFilled(c, 10.0f, ImGui::GetColorU32(ImGui::IsItemHovered() ? AccentEmphasisHover() : AccentEmphasisSelected()), 24);
            glyph::Draw(dl, c, tok::icon_sm, canSend ? IM_COL32(255, 255, 255, 255) : TextTint(0.35f), IconsInfinite::Send);
            if (clicked && canSend)
            {
               Send();
               sRefocus = true;
            }
         }
         ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + h));
         ImGui::Dummy(ImVec2(width, 0.0f));
         if (active || ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) || sAcceptPending)
            DrawDropdown(at, std::min(width, 360.0f));
      }

      void DrawHeader(float width)
      {
         const ImVec2 at = ImGui::GetCursorScreenPos();
         const float rowH = 22.0f;
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const CliSetup::Agent* agent = ChatAgent();
         glyph::Draw(dl, ImVec2(at.x + 9.0f, at.y + rowH * 0.5f), tok::icon_sm, TextTint(0.6f), IconsInfinite::Chat);
         const char* name = agent != nullptr ? agent->name : T("Chat");
         dl->AddText(ImVec2(at.x + 22.0f, at.y + (rowH - ImGui::GetFontSize()) * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), name);
         // New chat, then the terminal face.
         ImGui::SetCursorScreenPos(ImVec2(at.x + width - 2.0f * 24.0f, at.y + 2.0f));
         if (GlyphToggle::Draw("##chatnew", IconsInfinite::Plus, IconsInfinite::Plus, false) && sRun == nullptr)
         {
            sMsgs.clear();
            sSession.clear();
            sSessionLive = false;
         }
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", T("New chat"));
         ImGui::SameLine(0.0f, 2.0f);
         if (GlyphToggle::Draw("##chatterm", IconsInfinite::Terminal, IconsInfinite::Terminal, false))
         {
            gTerminalFace = 1;
            chat::AgentKind kind;
            if (sSessionLive && sResumedSession != sSession && AgentKindOf(agent, kind))
            {
               sResumedSession = sSession;
               TerminalRunInNewTab(chat::TerminalResumeLine(kind, sSession));
            }
         }
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", T("Terminal"));
         ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + rowH + tok::space_1));
      }
   }

   void DrawChatPanel()
   {
      if (ImGui::GetCurrentWindow()->SkipItems)
         return;
      PollAgent();
      RefreshCatalog(false);
      const float width = ImGui::GetContentRegionAvail().x;
      DrawHeader(width);
      if (DrawTerminalWelcome())
         return;
      const float composerH = 30.0f + tok::space_2;
      DrawMessages(std::max(40.0f, ImGui::GetContentRegionAvail().y - composerH));
      ImGui::Dummy(ImVec2(0.0f, tok::space_1));
      DrawComposer(width);
   }

   bool ChatBusy() { return sRun != nullptr; }

   void ChatShutdown()
   {
      if (sRun != nullptr)
         sRun->Stop();
      sRun = nullptr;
   }
}
