// ChatPanel: the Terminal panel's chat face (docs/plans/agent-collab/README.md C2-C7). A line the patch
// grammar reads fully runs at once through the same RPC handlers as `infinite`, as one undo step; anything
// else is a turn of each agent it mentions (`@claude`, `@codex`, `@antigravity`, else the default one),
// run side by side. A turn shows only its result: a folded Working row, the agent's last answer, and one
// Changed card diffed from the patch before and after. Files attach by the + button or a drop. Every chat
// is saved and comes back from the history menu with its agent sessions.
#include "app/AppShared.h"
#include "app/ui/design/GlyphDraw.h"
#include "app/ui/design/UiAnim.h"
#include "app/ui/design/UiType.h"
#include "app/ui/design/components/BusyLine.h"
#include "app/ui/design/components/ChipButton.h"
#include "app/ui/design/components/CodeBlock.h"
#include "app/ui/design/components/EmptyState.h"
#include "app/ui/design/components/FormParts.h"
#include "app/ui/design/components/GlyphToggle.h"
#include "core/CategoryColors.h"
#include "core/CliSetup.h"
#include "core/GraphNode.h"
#include "core/NodeFactory.h"
#include "core/chat/AgentRun.h"
#include "core/chat/ChatDiff.h"
#include "core/chat/ChatFiles.h"
#include "core/chat/ChatThread.h"
#include "core/chat/PatchGrammar.h"
#include "core/chat/ReplyLinks.h"
#include "core/gl3.h"
#include "imgui_internal.h"
#include "platform/AppPaths.h"
#include "platform/Platform.h"
#include "stb_image.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <random>
#include <sstream>
#include <unordered_map>

namespace app
{
   int gTerminalFace = 0;

   namespace
   {
      using json = nlohmann::json;
      using chat::Entry;

      struct EntryUi
      {
         bool open = false;     // Steps: unfolded; Change: every line shown
         bool hovered = false;  // last frame, for the Copy action
      };

      struct Cand
      {
         std::string insert;  // replaces the word being typed
         std::string label, detail, tip;
         ImU32 dot = 0;
         bool info = false;  // a line to read, not to pick
      };

      struct Texture
      {
         GLuint id = 0;
         int w = 0, h = 0;
      };

      // One agent's part of a turn.
      struct Run
      {
         std::string agent;
         chat::AgentKind kind = chat::AgentKind::Claude;
         std::shared_ptr<chat::AgentRun> run;
         int steps = -1;  // its Working row in the thread
         std::string answer;
         bool error = false;
         std::string errorText;
         std::vector<std::string> denied;
      };

      // What the chat knows about each agent: can it take a turn, and a setup running in the background.
      struct AgentState
      {
         CliSetup::Readiness ready;
         std::shared_ptr<chat::AgentRun> setup;
         bool failed = false;
         bool signingIn = false;
      };

      chat::Thread sThread;
      std::vector<EntryUi> sUi;
      bool sLoaded = false;
      std::string sInput;
      std::vector<std::string> sAttach;
      chat::Catalog sCat;
      uint64_t sCatGen = 0;
      double sCatTime = -1.0;
      std::vector<Run> sRuns;
      chat::Snapshot sBefore;
      std::map<std::string, AgentState> sAgents;
      double sAgentsAt = -100.0;
      int sLastFrame = -100;
      bool sScrollToEnd = false;
      bool sRefocus = false;
      bool sCursorToEnd = false;
      std::vector<chat::ThreadInfo> sHistory;  // newest first; read from disk once, then kept by SaveNow
      bool sHistoryLoaded = false;
      char sHistoryFilter[96] = "";
      // Dropdown
      int sPick = 0;
      bool sPickByKey = false;
      std::string sDropKey;    // the slot + partial it was last shown for; a change resets the pick
      std::string sDismissed;  // Esc closed it for this key
      bool sDropHovered = false;
      bool sCursorAtEnd = true;
      int sPrevCursor = 0;
      std::vector<Cand> sCands;
      chat::Parse sParse;
      std::map<std::string, Texture> sTextures;
      std::unordered_map<std::string, std::vector<chat::Block>> sBlocks;
      uint64_t sBlocksGen = 0;

      bool Light() { return CategoryColors::IsThemeLight(); }

      ImU32 WithAlpha(ImU32 c, float a) { return (c & 0x00FFFFFFu) | ((ImU32)(std::clamp(a, 0.0f, 1.0f) * 255.0f) << 24); }

      ImU32 TextTint(float a)
      {
         const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
         return ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, a));
      }

      ImU32 Dim() { return ImGui::GetColorU32(ImGuiCol_TextDisabled); }
      ImU32 Red() { return tok::U32(tok::action_record, Light()); }

      ImU32 CategoryU32(const std::string& category)
      {
         const CategoryColors::Color& c = CategoryColors::ColorFor(category);
         return ImGui::GetColorU32(ImVec4(c.r, c.g, c.b, 1.0f));
      }

      std::string ChatsDir() { return AppPaths::AppSupportDir() + "/chats"; }

      std::string ReadAll(const std::string& path)
      {
         std::ifstream f(AppPaths::FsPath(path), std::ios::binary);
         std::stringstream ss;
         ss << f.rdbuf();
         return ss.str();
      }

      std::string NewId()
      {
         std::random_device rd;
         std::mt19937_64 g(((uint64_t)rd() << 32) ^ rd());
         char buf[40];
         const uint64_t a = g(), b = g();
         std::snprintf(buf, sizeof buf, "%08x-%04x-4%03x-%04x-%012llx", (unsigned)(a >> 32), (unsigned)(a >> 16) & 0xFFFF, (unsigned)a & 0xFFF,
                       (unsigned)((b >> 48) & 0x3FFF) | 0x8000, (unsigned long long)(b & 0xFFFFFFFFFFFFull));
         return buf;
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

      // The patch as the Changed card compares it: every node and param, and every cable.
      chat::Snapshot Snap()
      {
         RefreshCatalog(true);
         chat::Snapshot s;
         s.catalog = sCat;
         for (const LinkInfo& l : gLinks)
         {
            chat::CatalogLink c;
            c.src = GraphNode::NodeIndexFromPin(l.srcPin);
            c.srcOutput = GraphNode::IsOutputPin(l.srcPin) ? GraphNode::OutputIndexFromPin(l.srcPin) : 0;
            c.dst = GraphNode::NodeIndexFromPin(l.dstPin);
            const int off = GraphNode::OffsetFromPin(l.dstPin);
            if (GraphNode::IsParamPin(l.dstPin))
               c.dstPin = "p" + std::to_string(off - GraphNode::kParamBase);
            else if (GraphNode::IsColorPin(l.dstPin))
               c.dstPin = "c" + std::to_string(GraphNode::ColorIndexFromPin(l.dstPin));
            else
               c.dstSlot = off - 1;
            s.links.push_back(c);
         }
         return s;
      }

      const std::vector<chat::Block>& BlocksFor(const std::string& text)
      {
         if (sBlocksGen != sCatGen)
         {
            sBlocks.clear();
            sBlocksGen = sCatGen;
         }
         auto it = sBlocks.find(text);
         if (it == sBlocks.end())
            it = sBlocks.emplace(text, chat::ParseReply(text, sCat)).first;
         return it->second;
      }

      // ---------------------------------------------------------------- agents

      bool KindOf(const std::string& id, chat::AgentKind& kind)
      {
         if (id == "claude")
            kind = chat::AgentKind::Claude;
         else if (id == "codex")
            kind = chat::AgentKind::Codex;
         else if (id == "antigravity")
            kind = chat::AgentKind::Antigravity;
         else
            return false;
         return true;
      }

      const char* AgentName(const std::string& id)
      {
         const CliSetup::Agent* a = CliSetup::FindAgent(id);
         return a != nullptr ? a->name : id.c_str();
      }

      // Each agent's own colour (a node family's, so it follows the theme) and initial, so a reply
      // shows who wrote it at a glance when several agents answer in one chat.
      ImU32 AgentTint(const std::string& id)
      {
         static const std::map<std::string, const char*> kFamily = { { "claude", "AudioEffects" }, { "codex", "3D" }, { "antigravity", "Notes" } };
         const auto it = kFamily.find(id);
         return CategoryU32(it != kFamily.end() ? it->second : "Utility");
      }

      void DrawAvatar(ImDrawList* dl, ImVec2 centre, float radius, const std::string& id)
      {
         const ImU32 tint = AgentTint(id);
         dl->AddCircleFilled(centre, radius, WithAlpha(tint, 0.22f));
         dl->AddCircle(centre, radius, WithAlpha(tint, 0.70f), 0, 1.0f);
         const char name[2] = { (char)std::toupper((unsigned char)AgentName(id)[0]), 0 };
         UiType::Scope cap(UiType::Size::Caption, UiType::Weight::Semibold);
         const ImVec2 ts = ImGui::CalcTextSize(name);
         dl->AddText(ImVec2(std::floor(centre.x - ts.x * 0.5f), std::floor(centre.y - ts.y * 0.5f)), tint, name);
      }

      bool Ready(const std::string& id)
      {
         auto it = sAgents.find(id);
         return it != sAgents.end() && it->second.ready.Ready();
      }

      bool AnyReady()
      {
         for (const auto& [id, s] : sAgents)
            if (s.ready.Ready())
               return true;
         return false;
      }

      // The agent a message with no mention goes to: the Settings choice when it can take a turn, else the
      // first one that can.
      std::string DefaultAgent()
      {
         const std::string pref = CliSetup::Prefs().agent;
         if (Ready(pref))
            return pref;
         for (const CliSetup::Agent& a : CliSetup::Agents())
            if (Ready(a.id))
               return a.id;
         return std::string();
      }

      // File probes only; on open, when a setup ends, and every 2 s while the setup card shows.
      void CheckAgents(bool force)
      {
         const double now = ImGui::GetTime();
         bool waiting = !AnyReady();
         for (const auto& [id, s] : sAgents)
            waiting = waiting || s.signingIn;
         if (!force && (!waiting || now - sAgentsAt < 2.0))
            return;
         sAgentsAt = now;
         for (const CliSetup::Agent& a : CliSetup::Agents())
         {
            chat::AgentKind k;
            if (!KindOf(a.id, k))
               continue;
            AgentState& s = sAgents[a.id];
            s.ready = CliSetup::AgentReadiness(a);
            if (s.ready.signedIn)
               s.signingIn = false;
         }
      }

      // Install if missing and connect, with no Terminal tab: the setup line runs as a child of the login
      // shell, and its status.json says how far it got.
      void SetUpInBackground(const CliSetup::Agent& agent)
      {
         using namespace CliSetup;
         AgentState& s = sAgents[agent.id];
         if (s.setup != nullptr)
            return;
         s.failed = false;
         const Os os = CurrentOs();
         const Paths paths = PathsFor(AppPaths::AppSupportDir(), os);
         std::string err;
         const bool ran = Perform(Action::kSetupAgent, os, agent, RealExecutable(), paths,
                                  [&](const std::string&, const std::string& line)
                                  {
                                     chat::AgentRun::Options o;
                                     o.argv = os == Os::kWindows ? std::vector<std::string>{ "powershell", "-NoProfile", "-Command", line }
                                                                 : std::vector<std::string>{ "sh", "-c", line };
                                     TerminalCommandEnv(o.env, o.cwd);
                                     s.setup = chat::AgentRun::Start(o);
                                  },
                                  &err);
         if (!ran || s.setup == nullptr)
            s.failed = true;
      }

      void OpenInTerminal(const CliSetup::Agent& agent)
      {
         using namespace CliSetup;
         const Os os = CurrentOs();
         std::string err;
         Perform(Action::kConnectAll, os, agent, RealExecutable(), PathsFor(AppPaths::AppSupportDir(), os),
                 [](const std::string&, const std::string& line) { TerminalRunInNewTab(line); }, &err);
      }

      void PollSetups()
      {
         for (auto& [id, s] : sAgents)
         {
            if (s.setup == nullptr)
               continue;
            std::vector<chat::AgentEvent> drop;
            s.setup->Poll(drop);
            if (!s.setup->Done())
               continue;
            s.setup = nullptr;
            CheckAgents(true);
            s.failed = !s.ready.connected;
         }
      }

      // ---------------------------------------------------------------- thread

      void SaveNow()
      {
         if (sThread.entries.empty())
            return;
         sThread.updated = (long long)std::time(nullptr);
         chat::SaveThread(ChatsDir(), sThread);
         // Keep the history list current without reading every chat again.
         if (sHistoryLoaded)
         {
            sHistory.erase(std::remove_if(sHistory.begin(), sHistory.end(), [](const chat::ThreadInfo& t) { return t.id == sThread.id; }), sHistory.end());
            sHistory.insert(sHistory.begin(), chat::InfoOf(sThread, ChatsDir() + "/" + sThread.id + ".json"));
         }
      }

      void Push(Entry e)
      {
         sThread.entries.push_back(std::move(e));
         sUi.emplace_back();
         sScrollToEnd = true;
      }

      void InsertAt(int at, Entry e)
      {
         sThread.entries.insert(sThread.entries.begin() + at, std::move(e));
         sUi.insert(sUi.begin() + at, EntryUi());
         for (Run& r : sRuns)
            if (r.steps >= at)
               r.steps++;
         sScrollToEnd = true;
      }

      Entry Make(Entry::Kind kind, const std::string& text, const std::string& agent = std::string())
      {
         Entry e;
         e.kind = kind;
         e.text = text;
         e.agent = agent;
         return e;
      }

      void NewChat()
      {
         sThread = chat::Thread();
         sThread.id = NewId();
         sUi.clear();
         sAttach.clear();
      }

      void OpenThread(const std::string& path)
      {
         chat::Thread t;
         if (!chat::LoadThread(path, t))
            return;
         sThread = std::move(t);
         sUi.assign(sThread.entries.size(), EntryUi());
         sScrollToEnd = true;
      }

      // ---------------------------------------------------------------- sending

      // `@title` the user typed becomes `@title#index`, so the agent reads one node per name; agent
      // mentions only route the message and leave it.
      std::string PromptFor(const std::string& line, const chat::Parse& p)
      {
         std::vector<chat::Span> spans = p.spans;
         std::sort(spans.begin(), spans.end(), [](const chat::Span& a, const chat::Span& b) { return a.begin < b.begin; });
         std::string out;
         size_t at = 0;
         for (const chat::Span& s : spans)
         {
            if (s.begin < at || s.end > line.size())
               continue;
            if (s.kind == chat::SpanKind::Agent)
            {
               out += line.substr(at, s.begin - at);
               at = s.end;
               if (at < line.size() && line[at] == ' ')
                  at++;
               continue;
            }
            if (s.kind != chat::SpanKind::Node || s.node < 0)
               continue;
            const chat::CatalogNode* n = sCat.Node(s.node);
            const std::string written = line.substr(s.begin, s.end - s.begin);
            if (n == nullptr || written.find('#') != std::string::npos)
               continue;
            const size_t pin = written.find(':');
            out += line.substr(at, s.begin - at) + "@" + n->title + "#" + std::to_string(n->index) + (pin != std::string::npos ? written.substr(pin) : "");
            at = s.end;
         }
         out += line.substr(at);
         while (!out.empty() && std::isspace((unsigned char)out.back()))
            out.pop_back();
         return out;
      }

      void RunDirect(const chat::Parse& p)
      {
         const chat::Snapshot before = Snap();
         json res;
         std::string err;
         bool ok;
         if (p.calls.size() == 1)
            ok = HandleRpcCommand(p.calls[0]["method"], p.calls[0].value("params", json::object()), res, err);
         else
            ok = HandleRpcCommand("batch", { { "calls", p.calls } }, res, err);
         Entry e = Make(Entry::Kind::Change, err);
         e.ok = ok && err.empty();
         e.changes = chat::Diff(before, Snap());
         if (e.changes.empty())
            e.lines = p.cli;
         Push(std::move(e));
      }

      void StartRun(const std::string& agent, const std::string& prompt, const std::vector<std::string>& dirs)
      {
         Run r;
         r.agent = agent;
         KindOf(agent, r.kind);
         chat::Session& s = sThread.sessions[agent];
         if (s.id.empty() && r.kind == chat::AgentKind::Claude)
            s.id = NewId();
         chat::AgentRun::Options o;
         o.kind = r.kind;
         o.argv = chat::AgentArgs(r.kind, prompt, s.id, s.live, dirs);
         TerminalCommandEnv(o.env, o.cwd);
         r.run = chat::AgentRun::Start(o);
         r.steps = (int)sThread.entries.size();
         Push(Make(Entry::Kind::Steps, "", agent));
         sRuns.push_back(std::move(r));
      }

      void Send()
      {
         std::string line = sInput;
         while (!line.empty() && std::isspace((unsigned char)line.back()))
            line.pop_back();
         if ((line.empty() && sAttach.empty()) || !sRuns.empty())
            return;
         RefreshCatalog(true);
         const chat::Parse p = chat::ParseLine(line, sCat);
         Entry user = Make(Entry::Kind::User, line);
         user.files = sAttach;
         if (sThread.title.empty())
            sThread.title = chat::TitleFor(!line.empty() ? line : std::filesystem::path(AppPaths::FsPath(sAttach[0])).filename().u8string());
         Push(std::move(user));
         const std::vector<std::string> files = sAttach;
         sInput.clear();
         sAttach.clear();
         sDismissed.clear();
         if (p.direct && files.empty())
         {
            RunDirect(p);
            SaveNow();
            return;
         }
         std::vector<std::string> agents;
         for (const chat::Span& s : p.spans)
            if (s.kind == chat::SpanKind::Agent)
            {
               const std::string id = s.key == "agent" ? DefaultAgent() : s.key;
               if (!id.empty() && std::find(agents.begin(), agents.end(), id) == agents.end())
                  agents.push_back(id);
            }
         if (agents.empty())
         {
            const std::string id = DefaultAgent();
            if (id.empty())
            {
               Push(Make(Entry::Kind::Error, T("Set up an agent first.")));
               SaveNow();
               return;
            }
            agents.push_back(id);
         }
         std::string prompt = PromptFor(line, p);
         if (prompt.empty())
            prompt = "Look at the attached files.";  // to the agent, not the user
         prompt = chat::WithFiles(prompt, files);
         const std::vector<std::string> dirs = chat::FileDirs(files);
         sBefore = Snap();
         for (const std::string& id : agents)
         {
            if (!Ready(id))
            {
               char note[160];
               std::snprintf(note, sizeof note, T("%s is not set up."), AgentName(id));
               Push(Make(Entry::Kind::Error, note, id));
               continue;
            }
            StartRun(id, prompt, dirs);
         }
         SaveNow();
      }

      // One agent's turn ended: its answer replaces its Working row when it ran nothing, else goes under it.
      void FinishRun(Run& r)
      {
         Entry& steps = sThread.entries[(size_t)r.steps];
         int at = r.steps + 1;
         if (steps.lines.empty())
         {
            sThread.entries.erase(sThread.entries.begin() + r.steps);
            sUi.erase(sUi.begin() + r.steps);
            for (Run& o : sRuns)
               if (o.steps > r.steps)
                  o.steps--;
            at = r.steps;
         }
         if (!r.answer.empty())
            InsertAt(at++, Make(Entry::Kind::Agent, r.answer, r.agent));
         if (r.error)
         {
            char note[200];
            std::snprintf(note, sizeof note, "%s: %s", AgentName(r.agent), r.errorText.empty() ? T("stopped with an error.") : r.errorText.c_str());
            InsertAt(at++, Make(Entry::Kind::Error, note, r.agent));
         }
         for (const std::string& d : r.denied)
            InsertAt(at++, Make(Entry::Kind::Note, std::string(T("Not allowed:")) + " " + d, r.agent));
      }

      void PollRuns()
      {
         if (sRuns.empty())
            return;
         for (Run& r : sRuns)
         {
            if (r.run == nullptr)
               continue;
            std::vector<chat::AgentEvent> events;
            r.run->Poll(events);
            Entry& steps = sThread.entries[(size_t)r.steps];
            for (const chat::AgentEvent& e : events)
               switch (e.kind)
               {
                  case chat::AgentEvent::Kind::Session:
                     sThread.sessions[r.agent] = { e.text, true };
                     break;
                  case chat::AgentEvent::Kind::Text:
                     // Only the last text is the answer; what came before is part of the work.
                     if (!r.answer.empty())
                        steps.lines.push_back(r.answer.substr(0, r.answer.find('\n')));
                     r.answer = e.text;
                     break;
                  case chat::AgentEvent::Kind::Tool:
                     steps.lines.push_back(e.text);
                     break;
                  case chat::AgentEvent::Kind::ToolResult:
                     if (e.error)
                        steps.ok = false;
                     break;
                  case chat::AgentEvent::Kind::End:
                     r.error = e.error;
                     r.errorText = e.text;
                     r.denied = e.denied;
                     break;
               }
            if (r.run->Done())
            {
               r.run = nullptr;
               FinishRun(r);
            }
         }
         bool all = true;
         for (const Run& r : sRuns)
            all = all && r.run == nullptr;
         if (!all)
            return;
         sRuns.clear();
         Entry change = Make(Entry::Kind::Change, "");
         change.changes = chat::Diff(sBefore, Snap());
         if (!change.changes.empty())
            Push(std::move(change));
         SaveNow();
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

      std::string ParamTip(const chat::CatalogParam& p)
      {
         std::string tip = p.label.empty() ? p.key : p.label;
         if (p.kind == "float" || p.kind == "int")
            tip += "   " + Num(p.min, p.kind == "int") + " – " + Num(p.max, p.kind == "int");
         if (p.modulated)
            tip += std::string("   ") + T("modulated");
         if (p.expression)
            tip += std::string("   ") + T("formula");
         return tip;
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
            Cand c;
            c.insert = RefText(n) + " ";
            c.label = n.title;
            c.detail = (n.type != n.title ? n.type + "  " : std::string()) + "#" + std::to_string(n.index);
            c.dot = CategoryU32(n.category);
            ranked.push_back({ s * 4 + (modulatorsFirst && !n.modulator ? 2 : 0) + (sel ? 0 : 1), c });
         }
         std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
         for (auto& r : ranked)
            out.push_back(r.second);
      }

      void AddAgents(std::vector<Cand>& out, const std::string& want)
      {
         for (const CliSetup::Agent& a : CliSetup::Agents())
            if (Ready(a.id) && (Score(a.id, want) >= 0 || Score(a.name, want) >= 0))
               out.push_back({ std::string("@") + a.id + " ", a.id, a.name, "", tok::U32(tok::pin_pred, Light()), false });
      }

      // Rows read key then value; the label is in the tooltip, so nothing wraps or crowds.
      void AddParams(std::vector<Cand>& out, const chat::CatalogNode& n, const std::string& want)
      {
         std::vector<std::pair<int, Cand>> ranked;
         for (const chat::CatalogParam& p : n.params)
         {
            const int a = Score(p.key, want), b = Score(p.label, want);
            const int s = a >= 0 && b >= 0 ? std::min(a, b) : std::max(a, b);
            if (s < 0)
               continue;
            Cand c;
            c.insert = p.key + " ";
            c.label = p.key;
            c.detail = ValueText(p);
            c.tip = ParamTip(p);
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
               out.push_back({ w + " ", w, "", "", 0, false });
      }

      std::vector<Cand> Candidates(const chat::Parse& p)
      {
         std::vector<Cand> out;
         std::string want = chat::Lower(p.partial);
         const bool at = !want.empty() && want[0] == '@';
         if (at)
            want = want.substr(1);
         const chat::CatalogNode* node = sCat.Node(p.slotNode);
         switch (p.slot)
         {
            case chat::Slot::Start:
               if (at)
               {
                  AddNodes(out, want, false, -1);
                  AddAgents(out, want);
               }
               else if (!want.empty())
                  AddWords(out, { "add", "connect", "remove", "bypass", "unbypass", "undo", "redo" }, want);
               break;
            case chat::Slot::Node:
               AddNodes(out, want, false, -1);
               AddAgents(out, want);
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
                              out.push_back({ chat::Lower(o).find(' ') == std::string::npos ? chat::Lower(o) + " " : o + " ", o, "", "", 0, false });
                        }
                     else if (prm->kind == "bool")
                        AddWords(out, { "on", "off" }, want);
                     else if (!prm->kind.empty())
                        out.push_back({ "", Num(prm->min, prm->kind == "int") + " – " + Num(prm->max, prm->kind == "int"), std::string(T("now")) + " " + prm->value, "", 0, true });
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
                     ranked.push_back({ s, { t.name + " ", t.name, t.category, "", CategoryU32(t.category), false } });
               std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
               for (size_t i = 0; i < ranked.size() && i < 40; i++)
                  out.push_back(ranked[i].second);
               break;
            }
            default:
               // Mid-sentence `@`: any node or agent.
               if (at)
               {
                  AddNodes(out, want, false, -1);
                  AddAgents(out, want);
               }
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

      const Cand* Picked()
      {
         if (!DropdownOpen() || !Pickable())
            return nullptr;
         const Cand& c = sCands[(size_t)std::clamp(sPick, 0, (int)sCands.size() - 1)];
         return c.info ? nullptr : &c;
      }

      void MovePick(int dir)
      {
         const int n = (int)sCands.size();
         for (int step = 0; step < n; step++)
         {
            sPick = (sPick + dir + n) % n;
            if (!sCands[(size_t)sPick].info)
               break;
         }
         sPickByKey = true;
      }

      // The field is not active (Enter or a click took it): change the string, then focus it again.
      void AcceptInactive()
      {
         if (const Cand* c = Picked())
         {
            sInput = sInput.substr(0, std::min(sParse.partialBegin, sInput.size())) + c->insert;
            sRefocus = true;
            sCursorToEnd = true;
         }
      }

      int InputCallback(ImGuiInputTextCallbackData* data)
      {
         if (data->EventFlag == ImGuiInputTextFlags_CallbackAlways)
         {
            if (sCursorToEnd)
            {
               data->CursorPos = data->SelectionStart = data->SelectionEnd = data->BufTextLen;
               sCursorToEnd = false;
            }
            // ↑↓ pick in the dropdown instead of moving the cursor.
            if (DropdownOpen() && Pickable() && (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow)))
            {
               MovePick(ImGui::IsKeyPressed(ImGuiKey_UpArrow) ? -1 : 1);
               data->CursorPos = data->SelectionStart = data->SelectionEnd = std::min(sPrevCursor, data->BufTextLen);
            }
            sCursorAtEnd = data->CursorPos == data->BufTextLen;
            sPrevCursor = data->CursorPos;
         }
         else if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion)
         {
            if (const Cand* c = Picked())
            {
               const int begin = (int)std::min(sParse.partialBegin, (size_t)data->BufTextLen);
               data->DeleteChars(begin, data->BufTextLen - begin);
               data->InsertChars(begin, c->insert.c_str());
               data->CursorPos = data->SelectionStart = data->SelectionEnd = data->BufTextLen;
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

      // The buttons an agent offers (`[label](verb:arg)`): each runs one RPC, and add, set and
      // delete are one undo step like any edit.
      std::set<std::string> sActed;  // buttons already pressed, by message and place

      const char* ActionGlyph(const chat::Seg& s)
      {
         if (s.verb == "add")
            return IconsInfinite::Plus;
         if (s.verb == "set")
            return IconsInfinite::Edit;
         if (s.verb == "delete")
            return IconsInfinite::Erase;
         if (s.verb == "undo")
            return IconsInfinite::History;
         if (s.verb == "go")
            return IconsInfinite::Zoom;
         static const std::map<std::string, const char*> kPanels = {
            { "viewport", IconsInfinite::Viewport }, { "modmatrix", IconsInfinite::ModMatrix }, { "perf", IconsInfinite::Perform },
            { "timeline", IconsInfinite::Arrange }, { "session", IconsInfinite::Session }, { "samplestudio", IconsInfinite::Waveform },
            { "pianoroll", IconsInfinite::Piano }, { "view3d", IconsInfinite::View3d }, { "terminal", IconsInfinite::Terminal },
            { "history", IconsInfinite::History }, { "settings", IconsInfinite::Settings }, { "templates", IconsInfinite::Library },
         };
         const auto it = kPanels.find(s.arg);
         return it != kPanels.end() ? it->second : IconsInfinite::Browser;
      }

      bool RunAction(const chat::Seg& s)
      {
         json r;
         std::string err;
         bool ok = false;
         if (s.verb == "add")
         {
            std::string category;
            if (HandleRpcCommand("list_node_types", json::object(), r, err))
            {
               const json& table = r.contains("categories") ? r["categories"] : r;
               for (auto it = table.begin(); it != table.end(); ++it)
                  if (it->is_array() && std::find(it->begin(), it->end(), json(s.arg)) != it->end())
                     category = it.key();
            }
            json made;
            ok = !category.empty() && HandleRpcCommand("create_node", { { "typeName", s.arg }, { "category", category } }, made, err);
            if (ok && made.contains("index"))
               Focus(made["index"].get<int>());
         }
         else if (s.verb == "set")
            ok = HandleRpcCommand("set_param", { { "index", s.node }, { "name", s.key }, { "value", s.arg } }, r, err);
         else if (s.verb == "delete")
            ok = HandleRpcCommand("delete_node", { { "index", s.node } }, r, err);
         else if (s.verb == "undo")
            ok = HandleRpcCommand("undo", json::object(), r, err);
         else if (s.verb == "go")
         {
            Focus(s.node);
            ok = true;
         }
         else if (s.verb == "open")
            ok = HandleRpcCommand("panel", { { "name", s.arg }, { "open", true } }, r, err) && err.empty();
         RefreshCatalog(true);
         return ok;
      }

      // A button chip: glyph and label in an outlined pill; it dims with a tick once pressed, except
      // go and open, which stay pressable.
      float ActionWidth(const chat::Seg& s) { return ImGui::CalcTextSize(s.text.c_str()).x + tok::icon_sm + 3.0f * tok::space_1 + 2.0f; }

      void DrawAction(ImDrawList* dl, ImVec2 at, const chat::Seg& s, float lineH, const std::string& id)
      {
         const ImVec2 ts = ImGui::CalcTextSize(s.text.c_str());
         const ImVec2 sz(ActionWidth(s), lineH);
         const bool once = s.verb != "go" && s.verb != "open";
         const bool done = once && sActed.count(id) != 0;
         const bool live = s.verb == "add" || s.verb == "undo" || s.verb == "open" || sCat.Node(s.node) != nullptr;
         ImGui::SetCursorScreenPos(at);
         ImGui::PushID(id.c_str());
         const bool clicked = ImGui::InvisibleButton("##act", sz) && !done && live;
         ImGui::PopID();
         const bool hovered = ImGui::IsItemHovered() && !done && live;
         const float hv = UiAnim::Hover(ImGui::GetItemID(), hovered, tok::motion_hover_in, tok::motion_hover_out);
         const ImU32 accent = ImGui::GetColorU32(AccentEmphasisSelected());
         const ImU32 ink = done || !live ? Dim() : ImGui::GetColorU32(ImGuiCol_Text);
         dl->AddRectFilled(at, ImVec2(at.x + sz.x, at.y + sz.y), done || !live ? TextTint(0.05f) : WithAlpha(accent, 0.10f + 0.12f * hv), sz.y * 0.5f);
         if (!done && live)
            dl->AddRect(at, ImVec2(at.x + sz.x, at.y + sz.y), WithAlpha(accent, 0.45f + 0.35f * hv), sz.y * 0.5f);
         glyph::Draw(dl, ImVec2(at.x + tok::space_1 + tok::icon_sm * 0.5f + 1.0f, at.y + sz.y * 0.5f), tok::icon_sm, done ? Dim() : accent,
                     done ? IconsInfinite::Snap : ActionGlyph(s));
         dl->AddText(ImVec2(at.x + 2.0f * tok::space_1 + tok::icon_sm + 1.0f, at.y + (lineH - ts.y) * 0.5f), ink, s.text.c_str());
         if (hovered)
         {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (s.verb == "set")
               ImGui::SetTooltip("%s.%s = %s", sCat.Node(s.node)->title.c_str(), s.key.c_str(), s.arg.c_str());
         }
         else if (!live && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", T("Not in the patch"));
         if (clicked && RunAction(s) && once)
            sActed.insert(id);
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
            dl->AddText(ImVec2(at.x + tok::space_1, at.y + (lineH - ts.y) * 0.5f), Dim(), s.text.c_str());
            dl->AddLine(ImVec2(at.x + tok::space_1, at.y + lineH * 0.5f), ImVec2(at.x + tok::space_1 + ts.x, at.y + lineH * 0.5f), Dim(), 1.0f);
            if (ImGui::IsItemHovered())
               ImGui::SetTooltip("%s", T("Not in the patch"));
            return;
         }
         const ImU32 cat = n != nullptr ? CategoryU32(n->category) : TextTint(1.0f);
         dl->AddRectFilled(at, ImVec2(at.x + sz.x, at.y + sz.y), WithAlpha(cat, 0.16f + 0.10f * hv), tok::radius_cell);
         const chat::CatalogParam* p = s.kind == chat::Seg::Kind::Param && n != nullptr ? n->Find(s.key) : nullptr;
         if (p != nullptr && (p->modulated || p->expression))
            dl->AddCircleFilled(ImVec2(at.x + sz.x - 2.5f, at.y + 2.5f), 2.0f, tok::U32(p->modulated ? tok::pin_mod : tok::pin_expr, Light()));
         dl->AddText(ImVec2(at.x + tok::space_1 + 1.0f, at.y + (lineH - ts.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), s.text.c_str());
         if (ImGui::IsItemHovered())
         {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (p != nullptr)
               ImGui::SetTooltip("%s = %s   %s", p->key.c_str(), ValueText(*p).c_str(), ParamTip(*p).c_str());
            else if (n != nullptr)
               ImGui::SetTooltip("%s  #%d", n->type.c_str(), n->index);
         }
         if (clicked)
            Focus(s.node);
      }

      // Words and chips laid out in `width`, wrapping between words. Returns the height used.
      float DrawFlow(const std::vector<chat::Seg>& segs, ImVec2 origin, float width, int idBase, ImU32 col)
      {
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const float lineH = std::floor(ImGui::GetTextLineHeight() + tok::space_1 + 1.0f);
         const float space = ImGui::CalcTextSize(" ").x;
         float x = 0.0f, y = 0.0f;
         int chip = 0;
         for (const chat::Seg& s : segs)
         {
            if (s.kind == chat::Seg::Kind::Action)
            {
               const float w = ActionWidth(s);
               if (x > 0.0f && x + w > width)
                  x = 0.0f, y += lineH;
               DrawAction(dl, ImVec2(origin.x + x, origin.y + y + 1.0f), s, lineH - 2.0f, sThread.id + "/" + std::to_string(idBase) + "/" + std::to_string(chip++) + s.verb + s.arg);
               x += w + tok::space_1;
               continue;
            }
            const bool link = s.kind == chat::Seg::Kind::Node || s.kind == chat::Seg::Kind::Param || s.kind == chat::Seg::Kind::Missing;
            if (link)
            {
               const float w = ImGui::CalcTextSize(s.text.c_str()).x + 2.0f * tok::space_1 + 2.0f;
               if (x > 0.0f && x + w > width)
                  x = 0.0f, y += lineH;
               DrawChip(dl, ImVec2(origin.x + x, origin.y + y + 1.0f), s, lineH - 2.0f, idBase * 1000 + chip++);
               x += w + 1.0f;
               continue;
            }
            ImFont* font = s.kind == chat::Seg::Kind::Bold ? UiType::Font(UiType::Weight::Semibold) : ImGui::GetFont();
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
               const ImVec2 p(origin.x + x, origin.y + y + (lineH - ImGui::GetFontSize()) * 0.5f);
               if (s.kind == chat::Seg::Kind::Code)
                  dl->AddRectFilled(ImVec2(p.x - 2.0f, origin.y + y + 1.0f), ImVec2(p.x + w + 2.0f, origin.y + y + lineH - 1.0f), TextTint(0.08f), tok::radius_cell);
               dl->AddText(font, ImGui::GetFontSize(), p, col, word.c_str());
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
         std::ifstream f(AppPaths::FsPath(path), std::ios::binary);
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
         const float h = w * (float)t->h / (float)std::max(1, t->w);
         const float s = h > 320.0f ? 320.0f / h : 1.0f;
         const ImVec2 at = ImGui::GetCursorScreenPos();
         ImGui::Image((ImTextureID)(intptr_t)t->id, ImVec2(w * s, h * s));
         ImGui::GetWindowDrawList()->AddRect(at, ImVec2(at.x + w * s, at.y + h * s), TextTint(0.12f), tok::radius_field);
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", path.c_str());
      }

      void DrawBlocks(const std::vector<chat::Block>& blocks, float width, int id)
      {
         int k = 0;
         for (const chat::Block& b : blocks)
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
                     h = DrawFlow(b.segs, ImVec2(at.x + indent, at.y), width - indent, bid, ImGui::GetColorU32(ImGuiCol_Text));
                  }
                  else
                     h = DrawFlow(b.segs, ImVec2(at.x + indent, at.y), width - indent, bid, ImGui::GetColorU32(ImGuiCol_Text));
                  ImGui::SetCursorScreenPos(at);
                  ImGui::Dummy(ImVec2(width, h + (b.kind == chat::Block::Kind::Bullet ? 0.0f : tok::space_1)));
                  break;
               }
            }
         }
      }

      // A small grey line above an agent's rows: its name, and Copy while the answer is hovered.
      void DrawAgentLabel(const Entry& e, float width, int id, bool copy)
      {
         const ImVec2 at = ImGui::GetCursorScreenPos();
         ImDrawList* dl = ImGui::GetWindowDrawList();
         float h = 0.0f;
         {
            UiType::Scope cap(UiType::Size::Caption, UiType::Weight::Medium);
            h = std::max(ImGui::GetTextLineHeight(), 16.0f);
         }
         DrawAvatar(dl, ImVec2(at.x + 8.0f, at.y + h * 0.5f), 8.0f, e.agent);
         UiType::Scope cap(UiType::Size::Caption, UiType::Weight::Medium);
         dl->AddText(ImVec2(at.x + 22.0f, at.y + (h - ImGui::GetTextLineHeight()) * 0.5f), AgentTint(e.agent), AgentName(e.agent));
         if (copy)
         {
            const char* label = T("Copy");
            const float w = ImGui::CalcTextSize(label).x;
            ImGui::SetCursorScreenPos(ImVec2(at.x + width - w, at.y));
            ImGui::PushID(id);
            const bool clicked = ImGui::InvisibleButton("##copy", ImVec2(w, h));
            ImGui::PopID();
            dl->AddText(ImVec2(at.x + width - w, at.y), ImGui::IsItemHovered() ? ImGui::GetColorU32(ImGuiCol_Text) : Dim(), label);
            if (clicked)
               ImGui::SetClipboardText(e.text.c_str());
         }
         ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + h + 4.0f));
      }

      void DrawAgent(Entry& e, EntryUi& ui, float width, int id, bool label)
      {
         const ImVec2 top = ImGui::GetCursorScreenPos();
         if (label)
            DrawAgentLabel(e, width, id, ui.hovered);
         DrawBlocks(BlocksFor(e.text), width, id);
         ui.hovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(top, ImVec2(top.x + width, ImGui::GetCursorScreenPos().y));
      }

      // What changed in the patch: one line per change, links to each node and param, code as a block.
      void DrawChange(Entry& e, EntryUi& ui, float width, int id, bool undoable)
      {
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const ImVec2 at = ImGui::GetCursorScreenPos();
         const float pad = tok::space_2;
         dl->ChannelsSplit(2);
         dl->ChannelsSetCurrent(1);
         ImGui::SetCursorScreenPos(ImVec2(at.x + pad, at.y + pad));
         ImGui::BeginGroup();
         const float inner = width - 2.0f * pad;
         const float rowH = ImGui::GetTextLineHeight();
         {
            UiType::Scope cap(UiType::Size::Caption, UiType::Weight::Medium);
            const ImVec2 h = ImGui::GetCursorScreenPos();
            const ImU32 accent = e.undone ? Dim() : (e.ok ? ImGui::GetColorU32(AccentEmphasisSelected()) : Red());
            glyph::Draw(dl, ImVec2(h.x + 6.0f, h.y + rowH * 0.5f), tok::icon_sm, accent, IconsInfinite::Bolt);
            dl->AddText(ImVec2(h.x + 16.0f, h.y), Dim(), e.undone ? T("Undone") : (e.ok ? T("Changed") : T("Did not run")));
            ImGui::Dummy(ImVec2(inner, rowH + 4.0f));
         }
         const ImU32 col = e.undone ? Dim() : ImGui::GetColorU32(ImGuiCol_Text);
         const size_t cap = 8;
         const size_t shown = ui.open ? e.changes.size() : std::min(e.changes.size(), cap);
         for (size_t i = 0; i < shown; i++)
         {
            const chat::ChangeLine& l = e.changes[i];
            const std::vector<chat::Block>& blocks = BlocksFor(l.text);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float h = blocks.empty() ? rowH : DrawFlow(blocks[0].segs, p, inner, id * 100 + (int)i, col);
            ImGui::SetCursorScreenPos(p);
            ImGui::Dummy(ImVec2(inner, h));
            if (!l.code.empty() && !e.undone)
            {
               char cid[40];
               std::snprintf(cid, sizeof cid, "##chcode%d_%d", id, (int)i);
               CodeBlock::Draw(l.code.c_str(), cid);
            }
         }
         if (e.changes.size() > shown)
         {
            char more[48];
            std::snprintf(more, sizeof more, T("%d more"), (int)(e.changes.size() - shown));
            ImGui::PushID(id);
            if (ChipButton::Draw(more, false))
               ui.open = true;
            ImGui::PopID();
         }
         for (const std::string& l : e.lines)
         {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(e.undone ? Dim() : TextTint(0.75f)));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + inner);
            ImGui::TextUnformatted(l.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
         }
         if (!e.ok && !e.text.empty())
         {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Red()));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + inner);
            ImGui::TextUnformatted(e.text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
         }
         ImGui::EndGroup();
         const float h = ImGui::GetItemRectMax().y - at.y + pad;
         if (undoable && e.ok && !e.undone)
         {
            const char* label = T("Undo");
            const float bw = ImGui::CalcTextSize(label).x + 2.0f * tok::space_2;
            ImGui::SetCursorScreenPos(ImVec2(at.x + width - bw - tok::space_1, at.y + tok::space_1));
            ImGui::PushID(id);
            if (ChipButton::Draw(label, false))
            {
               json r;
               std::string err;
               if (HandleRpcCommand("undo", json::object(), r, err))
                  e.undone = true;
               RefreshCatalog(true);
               SaveNow();
            }
            ImGui::PopID();
         }
         dl->ChannelsSetCurrent(0);
         dl->AddRectFilled(at, ImVec2(at.x + width, at.y + h), TextTint(0.05f), tok::radius_tile);
         dl->ChannelsMerge();
         ImGui::SetCursorScreenPos(at);
         ImGui::Dummy(ImVec2(width, h));
      }

      // What the agent did, folded to one row: Working… while it runs, the step count after.
      void DrawSteps(Entry& e, EntryUi& ui, float width, int id, bool running, bool named)
      {
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const ImVec2 at = ImGui::GetCursorScreenPos();
         const float rowH = ImGui::GetTextLineHeight() + tok::space_1;
         ImGui::PushID(id);
         if (ImGui::InvisibleButton("##steps", ImVec2(width, rowH)) && !e.lines.empty())
            ui.open = !ui.open;
         ImGui::PopID();
         const bool hot = ImGui::IsItemHovered() && !e.lines.empty();
         const ImU32 dim = hot ? ImGui::GetColorU32(ImGuiCol_Text) : Dim();
         float x = at.x;
         if (running)
         {
            ImGui::SetCursorScreenPos(ImVec2(x + 1.0f, at.y + (rowH - BusyLine::kSpinner) * 0.5f));
            BusyLine::Spinner();
            x += BusyLine::kSpinner + tok::space_1 + 2.0f;
         }
         else
         {
            glyph::Draw(dl, ImVec2(x + 7.0f, at.y + rowH * 0.5f), tok::icon_sm, dim, ui.open ? IconsInfinite::ChevronDown : IconsInfinite::ChevronRight);
            x += 18.0f;
         }
         std::string label = named ? std::string(AgentName(e.agent)) + "  " : std::string();
         char count[64];
         if (running)
            std::snprintf(count, sizeof count, "%s", T("Working…"));
         else
            std::snprintf(count, sizeof count, e.lines.size() == 1 ? T("%d step") : T("%d steps"), (int)e.lines.size());
         label += count;
         dl->AddText(ImVec2(x, at.y + (rowH - ImGui::GetFontSize()) * 0.5f), dim, label.c_str());
         if (!e.ok)
            dl->AddCircleFilled(ImVec2(x + ImGui::CalcTextSize(label.c_str()).x + 8.0f, at.y + rowH * 0.5f), 2.5f, Red());
         ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + rowH));
         if (ui.open || (running && !e.lines.empty()))
         {
            // While it runs, the latest step only; unfolded, every step.
            const size_t from = ui.open ? 0 : e.lines.size() - 1;
            UiType::Scope cap(UiType::Size::Caption);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
            for (size_t i = from; i < e.lines.size(); i++)
            {
               ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f);
               ImGui::TextDisabled("%s", e.lines[i].c_str());
            }
            ImGui::PopTextWrapPos();
         }
         ImGui::Dummy(ImVec2(width, 2.0f));
      }

      void DrawUser(const Entry& e, float width, int id)
      {
         // The line as typed, in a grey bubble on the right, its node names as chips; files under it.
         const chat::Parse p = chat::ParseLine(e.text, sCat);
         std::vector<chat::Seg> segs;
         size_t at = 0;
         std::vector<chat::Span> spans = p.spans;
         std::sort(spans.begin(), spans.end(), [](const chat::Span& a, const chat::Span& b) { return a.begin < b.begin; });
         for (const chat::Span& s : spans)
         {
            if (s.kind != chat::SpanKind::Node || s.node < 0 || s.begin < at)
               continue;
            if (s.begin > at)
               segs.push_back({ chat::Seg::Kind::Text, e.text.substr(at, s.begin - at), -1, "" });
            const chat::CatalogNode* n = sCat.Node(s.node);
            segs.push_back({ n != nullptr ? chat::Seg::Kind::Node : chat::Seg::Kind::Missing, n != nullptr ? n->title : e.text.substr(s.begin, s.end - s.begin), s.node, "" });
            at = s.end;
         }
         if (at < e.text.size())
            segs.push_back({ chat::Seg::Kind::Text, e.text.substr(at), -1, "" });
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const ImVec2 origin = ImGui::GetCursorScreenPos();
         float y = origin.y;
         if (!segs.empty())
         {
            const float maxW = std::max(80.0f, width * 0.8f);
            float textW = 0.0f;
            for (const chat::Seg& s : segs)
               textW += ImGui::CalcTextSize(s.text.c_str()).x + (s.kind == chat::Seg::Kind::Text ? 0.0f : 2.0f * tok::space_1 + 3.0f);
            const float w = std::min(maxW, textW + 2.0f * tok::space_2 + 2.0f);
            const ImVec2 box(origin.x + width - w, origin.y);
            dl->ChannelsSplit(2);
            dl->ChannelsSetCurrent(1);
            const float h = DrawFlow(segs, ImVec2(box.x + tok::space_2, box.y + tok::space_1 + 1.0f), w - 2.0f * tok::space_2, id, ImGui::GetColorU32(ImGuiCol_Text));
            dl->ChannelsSetCurrent(0);
            dl->AddRectFilled(box, ImVec2(box.x + w, box.y + h + 2.0f * tok::space_1 + 2.0f), TextTint(0.09f), tok::radius_tile);
            dl->ChannelsMerge();
            y += h + 2.0f * tok::space_1 + 2.0f;
         }
         // Attached files: right-aligned chips, the path in the tooltip.
         float x = origin.x + width;
         for (size_t i = e.files.size(); i-- > 0;)
         {
            const std::string name = std::filesystem::path(AppPaths::FsPath(e.files[i])).filename().u8string();
            const float w = ImGui::CalcTextSize(name.c_str()).x + 2.0f * tok::space_2;
            if (x - w < origin.x + width * 0.2f && x < origin.x + width)
               x = origin.x + width, y += 24.0f;
            x -= w;
            const ImVec2 p(x, y + 3.0f);
            dl->AddRect(p, ImVec2(p.x + w, p.y + 20.0f), TextTint(0.16f), tok::radius_tile);
            dl->AddText(ImVec2(p.x + tok::space_2, p.y + (20.0f - ImGui::GetFontSize()) * 0.5f), TextTint(0.8f), name.c_str());
            if (ImGui::IsMouseHoveringRect(p, ImVec2(p.x + w, p.y + 20.0f)) && ImGui::IsWindowHovered())
               ImGui::SetTooltip("%s", e.files[i].c_str());
            x -= tok::space_1;
         }
         if (!e.files.empty())
            y += 24.0f;
         ImGui::SetCursorScreenPos(origin);
         ImGui::Dummy(ImVec2(width, y - origin.y));
      }

      // While no agent can take a turn: one row per agent, each set up in the background.
      bool DrawSetup(float width)
      {
         if (AnyReady())
            return false;
         using namespace CliSetup;
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
         {
            UiType::Scope s(UiType::Size::Body, UiType::Weight::Semibold);
            ImGui::TextUnformatted(T("Connect an agent"));
         }
         ImGui::PopTextWrapPos();
         ImGui::Dummy(ImVec2(0.0f, tok::space_1));
         const Paths paths = PathsFor(AppPaths::AppSupportDir(), CurrentOs());
         const float rowH = 30.0f;
         for (const Agent& a : Agents())
         {
            auto it = sAgents.find(a.id);
            if (it == sAgents.end())
               continue;
            AgentState& s = it->second;
            ImGui::PushID(a.id);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(at, ImVec2(at.x + width, at.y + rowH), TextTint(0.05f), tok::radius_tile);
            const float ty = at.y + (rowH - ImGui::GetFontSize()) * 0.5f;
            dl->AddText(ImVec2(at.x + tok::space_2, ty), ImGui::GetColorU32(ImGuiCol_Text), a.name);
            auto right = [&](const char* label, bool primary) -> bool
            {
               const float w = ImGui::CalcTextSize(label).x + 2.0f * tok::space_2;
               ImGui::SetCursorScreenPos(ImVec2(at.x + width - w - tok::space_1, at.y + (rowH - ChipButton::kHeight) * 0.5f));
               return ChipButton::Draw(label, primary);
            };
            if (s.setup != nullptr)
            {
               const Status st = ParseStatus(ReadAll(paths.status), (long long)std::time(nullptr));
               char prog[48];
               std::snprintf(prog, sizeof prog, T("Setting up… %d of %d"), st.GreenCount(), kStepCount);
               const float w = ImGui::CalcTextSize(prog).x;
               dl->AddText(ImVec2(at.x + width - w - tok::space_2, ty), Dim(), prog);
               ImGui::SetCursorScreenPos(ImVec2(at.x + width - w - tok::space_2 - BusyLine::kSpinner - tok::space_1, at.y + (rowH - BusyLine::kSpinner) * 0.5f));
               BusyLine::Spinner();
            }
            else if (s.failed)
            {
               if (right(T("Open in Terminal"), false))
                  OpenInTerminal(a);
               dl->AddText(ImVec2(at.x + tok::space_2 + ImGui::CalcTextSize(a.name).x + tok::space_2, ty), Red(), T("Failed"));
            }
            else if (s.ready.installed && s.ready.connected && !s.ready.signedIn)
            {
               if (right(T("Sign in"), true))
               {
                  s.signingIn = true;
                  TerminalRunInNewTab(a.launch);
               }
            }
            else if (right(T("Set up"), true))
               SetUpInBackground(a);
            ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + rowH + tok::space_1));
            ImGui::PopID();
         }
         return true;
      }

      void DrawEmpty(float width)
      {
         const ImVec2 mn = ImGui::GetWindowPos();
         const float h = ImGui::GetWindowHeight();
         const char* hello = T("What should we make?");
         {
            UiType::Scope s(UiType::Size::Title, UiType::Weight::Medium);
            const ImVec2 ts = ImGui::CalcTextSize(hello);
            ImGui::GetWindowDrawList()->AddText(ImVec2(mn.x + (ImGui::GetWindowWidth() - ts.x) * 0.5f, mn.y + h * 0.38f), TextTint(0.85f), hello);
         }
         // Starting points: each fills the composer, nothing is sent until Enter.
         const char* ideas[] = { T("Add a Noise and wire it to the output"), T("Make the colours drift slowly"), T("Explain this patch") };
         float y = mn.y + h * 0.38f + 40.0f;
         for (int i = 0; i < 3; i++)
         {
            const float w = std::min(width, ImGui::CalcTextSize(ideas[i]).x + 2.0f * tok::space_3);
            ImGui::SetCursorScreenPos(ImVec2(mn.x + (ImGui::GetWindowWidth() - w) * 0.5f, y));
            ImGui::PushID(i);
            if (ChipButton::Draw(ideas[i], false, 26.0f, w, true))
            {
               sInput = ideas[i];
               sRefocus = true;
               sCursorToEnd = true;
            }
            ImGui::PopID();
            y += 30.0f;
         }
      }

      void DrawMessages(float height)
      {
         ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(tok::space_3, tok::space_2));
         ImGui::BeginChild("##chatlog", ImVec2(0.0f, height), ImGuiChildFlags_AlwaysUseWindowPadding);
         ImGui::PopStyleVar();
         const float width = ImGui::GetContentRegionAvail().x;
         const bool setup = DrawSetup(width);
         if (setup && !sThread.entries.empty())
            ImGui::Dummy(ImVec2(0.0f, tok::space_3));
         if (!setup && sThread.entries.empty())
            DrawEmpty(width);
         if (sUi.size() != sThread.entries.size())
            sUi.resize(sThread.entries.size());
         int lastChange = -1;
         for (int i = 0; i < (int)sThread.entries.size(); i++)
            if (sThread.entries[(size_t)i].kind == Entry::Kind::Change)
               lastChange = i;
         int agents = 1;
         for (const auto& [id, s] : sThread.sessions)
            agents = std::max(agents, (int)sThread.sessions.size());
         for (int i = 0; i < (int)sThread.entries.size(); i++)
         {
            Entry& e = sThread.entries[(size_t)i];
            EntryUi& ui = sUi[(size_t)i];
            const Entry* prev = i > 0 ? &sThread.entries[(size_t)i - 1] : nullptr;
            if (prev != nullptr)
               ImGui::Dummy(ImVec2(0.0f, e.kind == Entry::Kind::User || prev->kind == Entry::Kind::User ? tok::space_4 : tok::space_1));
            // An agent's name sits over the first of its rows in a run of them.
            const bool label = prev == nullptr || prev->agent != e.agent || prev->kind == Entry::Kind::User;
            bool running = false;
            for (const Run& r : sRuns)
               running = running || r.steps == i;
            switch (e.kind)
            {
               case Entry::Kind::User:
                  DrawUser(e, width, i);
                  break;
               case Entry::Kind::Agent:
                  DrawAgent(e, ui, width, i, label);
                  break;
               case Entry::Kind::Change:
                  DrawChange(e, ui, width, i, i == lastChange && e.lines.size() + e.changes.size() > 0 && sRuns.empty() &&
                                                 (prev != nullptr && prev->kind == Entry::Kind::User));
                  break;
               case Entry::Kind::Steps:
                  DrawSteps(e, ui, width, i, running, agents > 1);
                  break;
               case Entry::Kind::Note:
               case Entry::Kind::Error:
               {
                  ImGui::PushStyleColor(ImGuiCol_Text, e.kind == Entry::Kind::Error ? ImGui::ColorConvertU32ToFloat4(Red()) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
                  ImGui::TextUnformatted(e.text.c_str());
                  ImGui::PopTextWrapPos();
                  ImGui::PopStyleColor();
                  break;
               }
            }
         }
         ImGui::Dummy(ImVec2(0.0f, tok::space_2));
         if (sScrollToEnd)
         {
            ImGui::SetScrollHereY(1.0f);
            sScrollToEnd = false;
         }
         ImGui::EndChild();
      }

      // The tint behind each exact name in the composer: a node in its colour, a param on a quiet well, an
      // agent in its own. Laid out the way the field wraps it; skipped once the field scrolls.
      void DrawTints(ImVec2 textAt, float wrapW)
      {
         if (sInput.empty())
            return;
         ImDrawList* dl = ImGui::GetWindowDrawList();
         ImFont* font = ImGui::GetFont();
         const float fs = ImGui::GetFontSize();
         const char* buf = sInput.c_str();
         const char* end = buf + sInput.size();
         std::vector<std::pair<size_t, size_t>> lines;
         for (const char* s = buf; s < end; s = (*s == '\n') ? s + 1 : s)
         {
            const char* e = ImFontCalcWordWrapPositionEx(font, fs, s, end, wrapW, ImDrawTextFlags_WrapKeepBlanks);
            lines.push_back({ (size_t)(s - buf), (size_t)(e - buf) });
            s = e;
         }
         for (const chat::Span& sp : sParse.spans)
         {
            ImU32 col = 0;
            if (sp.kind == chat::SpanKind::Node && sp.node >= 0)
            {
               const chat::CatalogNode* n = sCat.Node(sp.node);
               col = n != nullptr ? WithAlpha(CategoryU32(n->category), 0.24f) : 0;
            }
            else if (sp.kind == chat::SpanKind::Param && sp.node >= 0)
               col = TextTint(0.12f);
            else if (sp.kind == chat::SpanKind::Agent && (sp.key == "agent" || Ready(sp.key)))
               col = WithAlpha(tok::U32(tok::pin_pred, Light()), 0.24f);
            if (col == 0 || sp.end > sInput.size())
               continue;
            for (size_t li = 0; li < lines.size(); li++)
            {
               const size_t a = std::max(sp.begin, lines[li].first), b = std::min(sp.end, lines[li].second);
               if (a >= b)
                  continue;
               const float x0 = textAt.x + font->CalcTextSizeA(fs, FLT_MAX, 0.0f, buf + lines[li].first, buf + a).x;
               const float x1 = textAt.x + font->CalcTextSizeA(fs, FLT_MAX, 0.0f, buf + lines[li].first, buf + b).x;
               const float y = textAt.y + fs * (float)li;
               dl->AddRectFilled(ImVec2(x0 - 2.0f, y - 1.0f), ImVec2(x1 + 2.0f, y + fs + 1.0f), col, tok::radius_cell);
            }
         }
      }

      int VisualLines(float wrapW)
      {
         if (sInput.empty())
            return 1;
         const char* buf = sInput.c_str();
         const char* end = buf + sInput.size();
         int n = 0;
         for (const char* s = buf; s < end; s = (*s == '\n') ? s + 1 : s)
         {
            n++;
            s = ImFontCalcWordWrapPositionEx(ImGui::GetFont(), ImGui::GetFontSize(), s, end, wrapW, ImDrawTextFlags_WrapKeepBlanks);
         }
         return n + (sInput.back() == '\n' ? 1 : 0);
      }

      void DrawDropdown(ImVec2 anchor, float width)
      {
         if (!DropdownOpen())
         {
            sDropHovered = false;
            return;
         }
         const float rowH = 24.0f;
         const int shown = std::min<int>((int)sCands.size(), 8);
         const float h = rowH * (float)shown + 2.0f * tok::space_1;
         ImGui::SetNextWindowPos(ImVec2(anchor.x, anchor.y - tok::space_1), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
         ImGui::SetNextWindowSize(ImVec2(width, h));
         ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(tok::space_1, tok::space_1));
         ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, tok::radius_tile);
         ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
         const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus;
         if (ImGui::Begin("##chatdropdown", nullptr, flags))
         {
            ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
            sDropHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
            sPick = std::clamp(sPick, 0, (int)sCands.size() - 1);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float rowW = ImGui::GetContentRegionAvail().x;
            const ImVec2 mouse = ImGui::GetIO().MouseDelta;
            for (int i = 0; i < (int)sCands.size(); i++)
            {
               const Cand& c = sCands[(size_t)i];
               const ImVec2 at = ImGui::GetCursorScreenPos();
               ImGui::PushID(i);
               // Pressed on mouse down: the field loses the keyboard on that same click.
               const bool clicked = ImGui::InvisibleButton("##row", ImVec2(rowW, rowH), ImGuiButtonFlags_PressedOnClick) && !c.info;
               ImGui::PopID();
               const bool hovered = ImGui::IsItemHovered();
               if (hovered && !c.info && (mouse.x != 0.0f || mouse.y != 0.0f))
                  sPick = i, sPickByKey = false;
               if (i == sPick && !c.info)
               {
                  dl->AddRectFilled(at, ImVec2(at.x + rowW, at.y + rowH), TextTint(0.10f), tok::radius_field);
                  if (sPickByKey && (at.y < ImGui::GetWindowPos().y || at.y + rowH > ImGui::GetWindowPos().y + ImGui::GetWindowHeight()))
                  {
                     ImGui::SetScrollHereY(at.y < ImGui::GetWindowPos().y ? 0.0f : 1.0f);
                     sPickByKey = false;
                  }
               }
               float x = at.x + tok::space_2;
               if (c.dot != 0)
               {
                  dl->AddCircleFilled(ImVec2(x + 3.0f, at.y + rowH * 0.5f), 3.0f, c.dot);
                  x += 12.0f;
               }
               const float ty = at.y + (rowH - ImGui::GetFontSize()) * 0.5f;
               const float right = at.x + rowW - tok::space_2;
               // The value gets at most 45% of the row; the key keeps the rest, cut with an ellipsis.
               float dw = c.detail.empty() ? 0.0f : std::min(ImGui::CalcTextSize(c.detail.c_str()).x, rowW * 0.45f);
               const float labelMax = right - (dw > 0.0f ? dw + tok::space_3 : 0.0f);
               ImGui::PushStyleColor(ImGuiCol_Text, c.info ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled) : ImGui::GetStyleColorVec4(ImGuiCol_Text));
               ImGui::RenderTextEllipsis(dl, ImVec2(x, ty), ImVec2(labelMax, ty + ImGui::GetFontSize()), labelMax, c.label.c_str(), nullptr, nullptr);
               ImGui::PopStyleColor();
               if (dw > 0.0f)
               {
                  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                  ImGui::RenderTextEllipsis(dl, ImVec2(right - dw, ty), ImVec2(right, ty + ImGui::GetFontSize()), right, c.detail.c_str(), nullptr, nullptr);
                  ImGui::PopStyleColor();
               }
               if (hovered && !c.tip.empty())
                  ImGui::SetTooltip("%s", c.tip.c_str());
               if (clicked)
               {
                  sPick = i;
                  AcceptInactive();
               }
            }
         }
         ImGui::End();
         ImGui::PopStyleVar(3);
      }

      void TakeDrops()
      {
         // Files dropped on the panel (StageLayout hands them to the Terminal) attach when the chat shows.
         for (const std::string& f : gTerminalDropPending)
            if (!chat::FileKind(f).empty() && std::find(sAttach.begin(), sAttach.end(), f) == sAttach.end())
               sAttach.push_back(f);
         gTerminalDropPending.clear();
      }

      // The agent a message with no mention goes to; the menu sets it up or switches it.
      void DrawAgentPicker(ImVec2 at, float h)
      {
         const std::string cur = DefaultAgent();
         const char* name = cur.empty() ? T("No agent") : AgentName(cur);
         const float w = ImGui::CalcTextSize(name).x + 2.0f * tok::space_2;
         ImGui::SetCursorScreenPos(ImVec2(at.x - w, at.y));
         const bool clicked = ImGui::InvisibleButton("##chatagent", ImVec2(w, h));
         const bool hot = ImGui::IsItemHovered();
         ImDrawList* dl = ImGui::GetWindowDrawList();
         if (hot)
            dl->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), TextTint(0.06f), tok::radius_tile);
         dl->AddText(ImVec2(at.x - w + tok::space_2, at.y + (h - ImGui::GetFontSize()) * 0.5f), hot ? ImGui::GetColorU32(ImGuiCol_Text) : Dim(), name);
         if (clicked)
            ImGui::OpenPopup("##chatagents");
         if (ImGui::BeginPopup("##chatagents"))
         {
            for (const CliSetup::Agent& a : CliSetup::Agents())
            {
               auto it = sAgents.find(a.id);
               if (it == sAgents.end())
                  continue;
               AgentState& s = it->second;
               ImGui::PushID(a.id);
               if (s.ready.Ready())
               {
                  if (ImGui::Selectable(a.name, a.id == cur))
                  {
                     CliSetup::Prefs().agent = a.id;
                     CliSetup::SaveConfig(CliSetup::PathsFor(AppPaths::AppSupportDir(), CliSetup::CurrentOs()).config, CliSetup::Prefs());
                  }
               }
               else
               {
                  const char* state = s.setup != nullptr ? T("Setting up…") : s.ready.connected ? T("Sign in") : T("Set up");
                  const std::string label = std::string(a.name) + "  ·  " + state;
                  if (ImGui::Selectable(label.c_str(), false, s.setup != nullptr ? ImGuiSelectableFlags_Disabled : 0))
                  {
                     if (s.ready.installed && s.ready.connected)
                     {
                        s.signingIn = true;
                        TerminalRunInNewTab(a.launch);
                     }
                     else
                        SetUpInBackground(a);
                  }
               }
               ImGui::PopID();
            }
            ImGui::EndPopup();
         }
      }

      void DrawComposer(float width)
      {
         TakeDrops();
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

         const float pad = tok::space_2;
         const float fs = ImGui::GetFontSize();
         const float padY = 2.0f;
         const float fieldW = width - 2.0f * pad;
         const float wrapW = std::max(1.0f, fieldW - ImGui::GetStyle().ScrollbarSize);
         const int lines = VisualLines(wrapW);
         const int rows = std::clamp(lines, 1, 6);
         const float textH = fs * (float)rows + 2.0f * padY;
         const float chipsH = sAttach.empty() ? 0.0f : 26.0f;
         const float barH = 26.0f;
         const ImVec2 at = ImGui::GetCursorScreenPos();
         const float wellH = pad + chipsH + textH + barH + tok::space_1;
         ImDrawList* dl = ImGui::GetWindowDrawList();
         dl->AddRectFilled(at, ImVec2(at.x + width, at.y + wellH), TextTint(0.07f), tok::radius_tile * 2.0f);
         dl->AddRect(at, ImVec2(at.x + width, at.y + wellH), TextTint(0.10f), tok::radius_tile * 2.0f);

         // Attached files, each with a remove mark.
         float x = at.x + pad;
         int remove = -1;
         for (size_t i = 0; i < sAttach.size(); i++)
         {
            const std::string name = std::filesystem::path(AppPaths::FsPath(sAttach[i])).filename().u8string();
            const float w = std::min(ImGui::CalcTextSize(name.c_str()).x, 160.0f) + tok::space_2 + 20.0f;
            if (x + w > at.x + width - pad)
               break;
            const ImVec2 p(x, at.y + pad);
            dl->AddRectFilled(p, ImVec2(p.x + w, p.y + 22.0f), TextTint(0.08f), tok::radius_tile);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
            ImGui::RenderTextEllipsis(dl, ImVec2(p.x + tok::space_2, p.y + (22.0f - fs) * 0.5f), ImVec2(p.x + w - 20.0f, p.y + 22.0f), p.x + w - 20.0f, name.c_str(), nullptr, nullptr);
            ImGui::PopStyleColor();
            ImGui::SetCursorScreenPos(ImVec2(p.x + w - 20.0f, p.y + 1.0f));
            ImGui::PushID((int)i);
            if (ImGui::InvisibleButton("##unattach", ImVec2(20.0f, 20.0f)))
               remove = (int)i;
            const ImU32 mark = ImGui::IsItemHovered() ? ImGui::GetColorU32(ImGuiCol_Text) : Dim();
            ImGui::PopID();
            glyph::Draw(dl, ImVec2(p.x + w - 10.0f, p.y + 11.0f), tok::icon_sm, mark, IconsInfinite::Close);
            if (ImGui::IsMouseHoveringRect(p, ImVec2(p.x + w - 20.0f, p.y + 22.0f)))
               ImGui::SetTooltip("%s", sAttach[i].c_str());
            x += w + tok::space_1;
         }
         if (remove >= 0)
            sAttach.erase(sAttach.begin() + remove);

         // The text: wraps, grows to six lines, then scrolls. Enter sends, Shift+Enter breaks the line.
         const ImVec2 fieldAt(at.x + pad, at.y + pad + chipsH);
         if (lines <= 6)
            DrawTints(ImVec2(fieldAt.x, fieldAt.y + padY), wrapW);
         if (sInput.empty())
            dl->AddText(ImVec2(fieldAt.x, fieldAt.y + padY), Dim(), sRuns.empty() ? T("Ask, or type @ to name a node") : T("Working…"));
         ImGui::SetCursorScreenPos(fieldAt);
         ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, padY));
         ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
         ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
         if (sRefocus)
         {
            ImGui::SetKeyboardFocusHere();
            sRefocus = false;
         }
         const Cand* picked = Picked();
         const ImGuiInputTextFlags flags = ImGuiInputTextFlags_WordWrap | ImGuiInputTextFlags_CtrlEnterForNewLine | ImGuiInputTextFlags_EnterReturnsTrue |
                                           ImGuiInputTextFlags_CallbackAlways | ImGuiInputTextFlags_CallbackCompletion;
         const bool enter = ImGui::InputTextMultiline("##chatinput", &sInput, ImVec2(fieldW, textH), flags, InputCallback);
         const bool active = ImGui::IsItemActive();
         ImGui::PopStyleColor();
         ImGui::PopStyleVar(2);
         if (enter)
         {
            if (picked != nullptr)
               AcceptInactive();
            else
               Send();
            sRefocus = true;
         }
         if (active && ImGui::IsKeyPressed(ImGuiKey_Escape) && DropdownOpen())
         {
            sDismissed = sDropKey;
            sRefocus = true;
            sCursorToEnd = true;
         }

         // Tool row: attach on the left, the agent and Send/Stop on the right.
         const float barY = at.y + pad + chipsH + textH;
         ImGui::SetCursorScreenPos(ImVec2(at.x + pad - 4.0f, barY + (barH - 22.0f) * 0.5f));
         if (GlyphToggle::Draw("##chatattach", IconsInfinite::Plus, IconsInfinite::Plus, false))
         {
            for (const std::string& f : Platform::OpenFilesDialog(T("Attach files"), chat::AttachExtensions()))
               if (std::find(sAttach.begin(), sAttach.end(), f) == sAttach.end())
                  sAttach.push_back(f);
            sRefocus = true;
         }
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", T("Attach files"));
         const float sendW = 26.0f;
         const ImVec2 sc(at.x + width - pad - sendW * 0.5f + 2.0f, barY + barH * 0.5f);
         ImGui::SetCursorScreenPos(ImVec2(sc.x - sendW * 0.5f, sc.y - 11.0f));
         if (!sRuns.empty())
         {
            const bool stop = ImGui::InvisibleButton("##chatstop", ImVec2(sendW, 22.0f));
            dl->AddCircleFilled(sc, 11.0f, TextTint(ImGui::IsItemHovered() ? 0.9f : 0.75f), 24);
            glyph::Draw(dl, sc, tok::icon_sm, ImGui::GetColorU32(ImGuiCol_WindowBg), IconsInfinite::StopFill);
            if (ImGui::IsItemHovered())
               ImGui::SetTooltip("%s", T("Stop"));
            if (stop)
               for (Run& r : sRuns)
                  if (r.run != nullptr)
                     r.run->Stop();
         }
         else
         {
            const bool canSend = !sInput.empty() || !sAttach.empty();
            const bool clicked = ImGui::InvisibleButton("##chatsend", ImVec2(sendW, 22.0f));
            if (canSend)
               dl->AddCircleFilled(sc, 11.0f, ImGui::GetColorU32(ImGui::IsItemHovered() ? AccentEmphasisHover() : AccentEmphasisSelected()), 24);
            glyph::Draw(dl, sc, tok::icon_sm, canSend ? IM_COL32(255, 255, 255, 255) : TextTint(0.35f), IconsInfinite::Send);
            if (clicked && canSend)
            {
               Send();
               sRefocus = true;
            }
         }
         DrawAgentPicker(ImVec2(sc.x - sendW * 0.5f - tok::space_1, barY + (barH - 22.0f) * 0.5f), 22.0f);
         ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + wellH));
         ImGui::Dummy(ImVec2(width, 0.0f));
         if (active || sDropHovered)
            DrawDropdown(ImVec2(at.x, at.y), std::min(width, 420.0f));
         else
            sDropHovered = false;
      }

      std::string Age(long long t)
      {
         const long long d = std::max(0LL, (long long)std::time(nullptr) - t);
         char buf[24];
         if (d < 60)
            return T("now");
         if (d < 3600)
            std::snprintf(buf, sizeof buf, "%lldm", d / 60);
         else if (d < 86400)
            std::snprintf(buf, sizeof buf, "%lldh", d / 3600);
         else
            std::snprintf(buf, sizeof buf, "%lldd", d / 86400);
         return buf;
      }

      void DrawHeader(float width)
      {
         const ImVec2 at = ImGui::GetCursorScreenPos();
         const float rowH = 26.0f;
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const char* title = sThread.title.empty() ? T("New chat") : sThread.title.c_str();
         const float right = at.x + width - 2.0f * 24.0f - tok::space_1;
         {
            UiType::Scope s(UiType::Size::Body, UiType::Weight::Medium);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
            ImGui::RenderTextEllipsis(dl, ImVec2(at.x + tok::space_3, at.y + (rowH - ImGui::GetFontSize()) * 0.5f), ImVec2(right, at.y + rowH), right,
                                      title, nullptr, nullptr);
            ImGui::PopStyleColor();
         }
         ImGui::SetCursorScreenPos(ImVec2(at.x + width - 2.0f * 24.0f, at.y + (rowH - 22.0f) * 0.5f));
         if (GlyphToggle::Draw("##chathistory", IconsInfinite::History, IconsInfinite::History, false))
         {
            if (!sHistoryLoaded)
               sHistory = chat::ListThreads(ChatsDir()), sHistoryLoaded = true;
            sHistoryFilter[0] = '\0';
            ImGui::OpenPopup("##chathistorymenu");
         }
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", T("Chats"));
         ImGui::SameLine(0.0f, 2.0f);
         if (GlyphToggle::Draw("##chatnew", IconsInfinite::Plus, IconsInfinite::Plus, false) && sRuns.empty())
            NewChat();
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", T("New chat"));

         ImGui::SetNextWindowSizeConstraints(ImVec2(260.0f, 0.0f), ImVec2(360.0f, 420.0f));
         if (ImGui::BeginPopup("##chathistorymenu"))
         {
            if (sHistory.empty())
               ImGui::TextDisabled("%s", T("No saved chats"));
            if (sHistory.size() > 6)
            {
               if (ImGui::IsWindowAppearing())
                  ImGui::SetKeyboardFocusHere();
               FormParts::SearchInput("##chatfind", T("Search chats"), sHistoryFilter, sizeof sHistoryFilter, std::max(240.0f, ImGui::GetContentRegionAvail().x));
            }
            std::string needle = sHistoryFilter;
            std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            std::string removeId;
            const chat::ThreadInfo* first = nullptr;
            for (const chat::ThreadInfo& t : sHistory)
            {
               if (!needle.empty())
               {
                  std::string title = t.title;
                  std::transform(title.begin(), title.end(), title.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                  if (title.find(needle) == std::string::npos)
                     continue;
               }
               if (first == nullptr)
                  first = &t;
               ImGui::PushID(t.id.c_str());
               const ImVec2 p = ImGui::GetCursorScreenPos();
               const float w = std::max(240.0f, ImGui::GetContentRegionAvail().x);
               const float h = 24.0f;
               const bool open = ImGui::InvisibleButton("##open", ImVec2(w - 22.0f, h));
               const bool hot = ImGui::IsMouseHoveringRect(p, ImVec2(p.x + w, p.y + h));
               ImDrawList* pd = ImGui::GetWindowDrawList();
               if (hot || t.id == sThread.id)
                  pd->AddRectFilled(p, ImVec2(p.x + w, p.y + h), TextTint(t.id == sThread.id ? 0.10f : 0.06f), tok::radius_field);
               const std::string age = Age(t.updated);
               const float aw = ImGui::CalcTextSize(age.c_str()).x;
               const float ty = p.y + (h - ImGui::GetFontSize()) * 0.5f;
               // who answered: one small avatar each, before the age
               const float dots = (float)std::min<size_t>(t.agents.size(), 3) * 13.0f;
               for (size_t k = 0; k < t.agents.size() && k < 3; k++)
                  DrawAvatar(pd, ImVec2(p.x + w - aw - 22.0f - tok::space_2 - dots + 6.0f + 13.0f * (float)k, p.y + h * 0.5f), 5.5f, t.agents[k]);
               const float tmax = p.x + w - aw - 22.0f - tok::space_2 - dots - (dots > 0.0f ? tok::space_1 : 0.0f);
               ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
               ImGui::RenderTextEllipsis(pd, ImVec2(p.x + tok::space_1, ty), ImVec2(tmax, p.y + h), tmax, t.title.empty() ? "…" : t.title.c_str(), nullptr, nullptr);
               ImGui::PopStyleColor();
               pd->AddText(ImVec2(p.x + w - aw - 22.0f - tok::space_1, ty), Dim(), age.c_str());
               ImGui::SameLine(0.0f, 0.0f);
               if (ImGui::InvisibleButton("##delete", ImVec2(22.0f, h)))
                  removeId = t.id;
               if (hot)
                  glyph::Draw(pd, ImVec2(p.x + w - 11.0f, p.y + h * 0.5f), tok::icon_sm, ImGui::IsItemHovered() ? ImGui::GetColorU32(ImGuiCol_Text) : Dim(), IconsInfinite::Close);
               if (ImGui::IsItemHovered())
                  ImGui::SetTooltip("%s", T("Delete"));
               if (open && sRuns.empty())
               {
                  OpenThread(t.path);
                  ImGui::CloseCurrentPopup();
               }
               ImGui::PopID();
            }
            // Enter opens the first match.
            if (first != nullptr && !needle.empty() && ImGui::IsKeyPressed(ImGuiKey_Enter) && sRuns.empty())
            {
               OpenThread(first->path);
               ImGui::CloseCurrentPopup();
            }
            if (!removeId.empty())
            {
               chat::DeleteThread(ChatsDir(), removeId);
               sHistory.erase(std::remove_if(sHistory.begin(), sHistory.end(), [&](const chat::ThreadInfo& t) { return t.id == removeId; }), sHistory.end());
               if (removeId == sThread.id)
                  NewChat();
            }
            ImGui::EndPopup();
         }
         ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + rowH));
         dl->AddLine(ImVec2(at.x, at.y + rowH - 0.5f), ImVec2(at.x + width, at.y + rowH - 0.5f), TextTint(0.08f));
      }
   }

   void DrawChatPanel()
   {
      if (ImGui::GetCurrentWindow()->SkipItems)
         return;
      if (!sLoaded)
      {
         // The last chat comes back as it was left.
         sLoaded = true;
         const std::vector<chat::ThreadInfo> all = chat::ListThreads(ChatsDir());
         if (!all.empty())
            OpenThread(all[0].path);
         else
            NewChat();
      }
      // Opening the panel checks the agents again (a sign-in may have happened outside).
      const int frame = ImGui::GetFrameCount();
      CheckAgents(frame - sLastFrame > 1);
      sLastFrame = frame;
      PollSetups();
      PollRuns();
      RefreshCatalog(false);
      const ImVec2 mn = ImGui::GetCursorScreenPos();
      const ImVec2 avail = ImGui::GetContentRegionAvail();
      ImGui::GetWindowDrawList()->AddRectFilled(mn, ImVec2(mn.x + avail.x, mn.y + avail.y), tok::U32(tok::terminal_bg, Light()), tok::radius_field);
      const float width = avail.x;
      DrawHeader(width);
      const float composerW = std::min(width - 2.0f * tok::space_3, 760.0f);
      const float fs = ImGui::GetFontSize();
      // The composer's height is known before it draws: the log takes the rest.
      const float wrapW = std::max(1.0f, composerW - 2.0f * tok::space_2 - ImGui::GetStyle().ScrollbarSize);
      const float textH = fs * (float)std::clamp(VisualLines(wrapW), 1, 6) + 4.0f;
      const float composerH = tok::space_2 + (sAttach.empty() ? 0.0f : 26.0f) + textH + 26.0f + tok::space_1;
      DrawMessages(std::max(40.0f, ImGui::GetContentRegionAvail().y - composerH - tok::space_3));
      const ImVec2 c = ImGui::GetCursorScreenPos();
      ImGui::SetCursorScreenPos(ImVec2(mn.x + (width - composerW) * 0.5f, c.y + tok::space_1));
      DrawComposer(composerW);
   }

   bool ChatBusy() { return !sRuns.empty(); }

   int ChatSavedCount()
   {
      if (!sHistoryLoaded)
         sHistory = chat::ListThreads(ChatsDir()), sHistoryLoaded = true;
      return (int)sHistory.size();
   }

   void ChatClearHistory()
   {
      if (ChatBusy())
         return;
      for (const chat::ThreadInfo& t : chat::ListThreads(ChatsDir()))
         chat::DeleteThread(ChatsDir(), t.id);
      sHistory.clear();
      sHistoryLoaded = true;
      NewChat();
   }

   void ChatDrawAvatar(ImDrawList* dl, ImVec2 centre, float radius, const std::string& agent) { DrawAvatar(dl, centre, radius, agent); }

   void ChatShutdown()
   {
      for (Run& r : sRuns)
         if (r.run != nullptr)
            r.run->Stop();
      sRuns.clear();
      for (auto& [id, s] : sAgents)
         if (s.setup != nullptr)
            s.setup->Stop();
      SaveNow();
   }
}
