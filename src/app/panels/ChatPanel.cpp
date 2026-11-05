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
#include "app/ui/design/components/Switch.h"
#include "core/CategoryColors.h"
#include "core/CliSetup.h"
#include "core/GraphNode.h"
#include "core/NodeFactory.h"
#include "audio/AudioEngine.h"
#include "core/chat/AgentAuth.h"
#include "core/chat/AgentRun.h"
#include "core/chat/ChatDiff.h"
#include "core/chat/ChatFiles.h"
#include "core/chat/ChatGuide.h"
#include "core/chat/ChatThread.h"
#include "core/chat/PatchGrammar.h"
#include "core/chat/ReplyLinks.h"
#include "core/gl3.h"
#include "imgui_internal.h"
#include "platform/AppPaths.h"
#include "platform/Platform.h"
#include "stb_image.h"
#include "lunasvg.h"
#include "app/ui/design/AgentLogos.gen.h"

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
   bool gChatSideTerminal = false;
   int gChatSelectionFrame = -100;

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
         unsigned long long bytes = 0;  // output seen at the last check
         double activeAt = -1.0;        // when output last grew
         double checkedAt = -1.0;
         std::string cut;               // why the chat ended the turn itself ("" = it did not)
         double startAt = -1.0;         // when the turn began, for the Working row's clock
      };

      // A turn with no output at all for this long is stuck (a long render still prints its tool call first).
      constexpr double kTurnSilentSeconds = 15.0 * 60.0;

      // What the chat knows about each agent: can it take a turn, and a setup running in the background.
      struct AgentState
      {
         CliSetup::Readiness ready;
         std::shared_ptr<chat::AgentRun> setup;
         bool failed = false;
         bool signingIn = false;
         std::shared_ptr<chat::AgentRun> login;  // the browser sign-in, running without a terminal
         std::string loginUrl;                   // the page to open by hand when the CLI could not
         std::string loginError;                 // plain words after a failed sign-in
         std::string loginLog;                   // raw output, behind Details
         bool chain = false;                     // Connect pressed: after setup, sign in without another click
         bool authBroken = false;                // a turn said "sign in": overrides the file probe
         double loginAt = 0.0;
         bool needsCode = false;                 // the sign-in page shows a code to paste back (Gemini)
         std::string codeInput;
         unsigned long long codeBytes = 0;       // output length when a code was sent: only a new ask shows the field again
         bool loginStopped = false;              // Cancel or the timeout ended the sign-in, not the CLI
         bool loginTimedOut = false;
         chat::Auth auth = chat::Auth::Unknown;  // the agent's own status check; wins over the file probe
         bool probe = false;                     // the file probe's last answer, to notice a sign-in made elsewhere
         bool everIn = false;                    // was signed in: a later failure asks to sign in again
         bool signedOutHere = false;             // Sign out ran: a login file left behind is not a stale session
         std::shared_ptr<chat::AgentRun> verify; // that status check, running
         double verifyAt = -1.0;
         std::shared_ptr<chat::AgentRun> logout; // Sign out, running
         bool signInAfterLogout = false;         // Sign in again that clears the old login first (Gemini)
      };

      // The one action each agent offers, the same in Settings, the agent picker, the onboarding card and an
      // error in the thread.
      enum class Act { None, SetUp, TryAgain, SignIn, SignInAgain, SignOut, Cancel };
      struct AgentView
      {
         std::string status;
         Act act = Act::None;
         bool ready = false, busy = false;
      };

      const char* ActLabel(Act a)
      {
         switch (a)
         {
            case Act::SetUp: return T("Set up");
            case Act::TryAgain: return T("Try again");
            case Act::SignIn: return T("Sign in");
            case Act::SignInAgain: return T("Sign in again");
            case Act::SignOut: return T("Sign out");
            case Act::Cancel: return T("Cancel");
            default: return "";
         }
      }

      bool OnMac() { return CliSetup::CurrentOs() == CliSetup::Os::kMac; }
      constexpr double kStatusCheckSeconds = 30.0;    // a status check that takes longer is stopped
      constexpr double kStatusRecheckSeconds = 300.0;  // a signed-in agent is checked again this often

      chat::Thread sThread;
      std::vector<EntryUi> sUi;
      bool sLoaded = false;
      std::string sInput;
      std::vector<std::string> sAttach;
      chat::Catalog sCat;
      uint64_t sCatGen = 0;
      double sCatTime = -1.0;
      std::vector<Run> sRuns;
      bool sRail = false;  // the chats list is open on the left
      chat::Snapshot sBefore;
      std::map<std::string, AgentState> sAgents;
      std::map<std::string, double> sModelsFetchAt;  // when each agent's model list was last asked for
      std::map<std::string, std::vector<chat::Model>> sModels;  // each agent's model menu
      double sAgentsAt = -100.0;
      int sLastFrame = -100;
      bool sScrollToEnd = false;
      bool sRefocus = false;
      int sPendingRef = -1;  // a node the canvas menu asked to name in the composer
      // Messages sent while an agent works wait here and go, in order, when the turn ends. Stop puts
      // them back in the box instead, so nothing is sent that the person did not mean to send now.
      struct Queued
      {
         std::string text;
         std::vector<std::string> files;
      };
      std::vector<Queued> sQueue;
      bool sQueueHold = false;
      constexpr float kQueueRowH = 24.0f;
      bool sCursorToEnd = false;
      // Selecting an agent's words: every word and chip DrawFlow lays out this frame, with where it
      // sits, so a drag can be hit-tested, highlighted and copied as the text it reads.
      struct Piece
      {
         int entry = -1;
         ImVec2 min, max;
         std::string text;
         ImFont* font = nullptr;
         float size = 0.0f;
         bool spaceAfter = false;
         bool breakBefore = false;  // the first word of a block or after a line break
      };
      struct SelPos
      {
         int piece = 0;  // within the entry's pieces
         int ch = 0;     // byte offset in the piece's text
      };
      std::vector<Piece> sPieces;
      std::vector<std::pair<int, ImRect>> sEntryRects;  // agent rows drawn this frame
      int sFlowEntry = -1;       // the agent row DrawFlow records for, or -1
      bool sFlowBreak = false;
      int sSelEntry = -1;
      SelPos sSelA, sSelB;
      bool sSelDrag = false;
      std::vector<chat::ThreadInfo> sHistory;  // newest first; read from disk once, then kept by SaveNow
      bool sHistoryLoaded = false;
      char sHistoryFilter[96] = "";
      // Dropdown
      int sPick = 0;
      bool sPickByKey = false;
      bool sPickMoved = false;  // ↑↓ moved the pick in this list: Enter takes it instead of sending
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
            n.uid = gn.uid;
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

      void SaveCliConfig()
      {
         CliSetup::SaveConfig(CliSetup::PathsFor(AppPaths::AppSupportDir(), CliSetup::CurrentOs()).config, CliSetup::Prefs());
      }

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

      // The chat reads at the app's base size (15): names, headers and the words share it, in a heavier
      // weight where they lead; only subtext (step rows, details) steps down to the body token.
      struct Weight
      {
         explicit Weight(UiType::Weight w) { ImGui::PushFont(UiType::Font(w), ImGui::GetFontSize()); }
         ~Weight() { ImGui::PopFont(); }
         Weight(const Weight&) = delete;
         Weight& operator=(const Weight&) = delete;
      };

      // Each agent's own colour (a node family's, so it follows the theme) and initial, so a reply
      // shows who wrote it at a glance when several agents answer in one chat.
      ImU32 AgentTint(const std::string& id)
      {
         static const std::map<std::string, const char*> kFamily = { { "claude", "AudioEffects" }, { "codex", "3D" }, { "antigravity", "Notes" } };
         const auto it = kFamily.find(id);
         return CategoryU32(it != kFamily.end() ? it->second : "Utility");
      }

      // An agent's official logo (art/brand/agents) rasterised once per pixel size and theme. The vendor
      // file is drawn unchanged, fitted by its own outline so each mark keeps the same clear space.
      ImTextureID AgentLogoTexture(const std::string& id, int px, bool light)
      {
         const unsigned char* data = nullptr;
         size_t size = 0;
         if (id == "claude")
            data = agentlogos::kClaude, size = sizeof(agentlogos::kClaude);
         else if (id == "codex")
            data = light ? agentlogos::kOpenAiBlack : agentlogos::kOpenAiWhite, size = sizeof(agentlogos::kOpenAiBlack);
         const bool png = id == "antigravity";  // Google's spark is a 512 px picture, not vectors
         if (png)
            data = agentlogos::kGeminiPng, size = sizeof(agentlogos::kGeminiPng);
         if (data == nullptr || px <= 0)
            return ImTextureID{};
         static std::map<std::string, GLuint> sTex;
         const std::string key = png ? id : id + (light ? "/l/" : "/d/") + std::to_string(px);
         const auto it = sTex.find(key);
         if (it != sTex.end())
            return (ImTextureID)(intptr_t)it->second;
         GLuint tex = 0;
         auto upload = [&](const unsigned char* rgba, int w, int h, int rowPx, bool mips)
         {
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, rowPx);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            if (mips)
               glGenerateMipmap(GL_TEXTURE_2D);  // a 512 px picture shrunk to ~20 px stays smooth
         };
         if (png)
         {
            // One texture for every size: the picture's own transparent margin is cropped so the spark
            // fills its box like the vector marks do.
            int w = 0, h = 0, n = 0;
            unsigned char* rgba = stbi_load_from_memory(data, (int)size, &w, &h, &n, 4);
            if (rgba != nullptr)
            {
               int x0 = w, y0 = h, x1 = -1, y1 = -1;
               for (int y = 0; y < h; y++)
                  for (int x = 0; x < w; x++)
                     if (rgba[(y * w + x) * 4 + 3] > 8)
                        x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
               const int side = x1 >= x0 ? std::max(x1 - x0, y1 - y0) + 1 : 0;
               if (side > 0)
                  upload(rgba + (y0 * w + x0) * 4, side, side, w, true);
               stbi_image_free(rgba);
            }
            sTex[key] = tex;
            return (ImTextureID)(intptr_t)tex;
         }
         const std::unique_ptr<lunasvg::Document> doc = lunasvg::Document::loadFromData((const char*)data, size);
         if (doc != nullptr)
         {
            const lunasvg::Box b = doc->boundingBox();
            const float fit = (float)px / std::max(1e-3f, std::max(b.w, b.h));
            lunasvg::Bitmap bmp(px, px);
            bmp.clear(0x00000000);
            doc->render(bmp, lunasvg::Matrix(fit, 0, 0, fit, (px - b.w * fit) * 0.5f - b.x * fit, (px - b.h * fit) * 0.5f - b.y * fit));
            bmp.convertToRGBA();
            upload(bmp.data(), px, px, bmp.stride() / 4, false);
         }
         sTex[key] = tex;
         return (ImTextureID)(intptr_t)tex;
      }

      // Each agent's official logo on a quiet disc. The mark fills at most 62% of the disc, so it keeps
      // clear space on every side as the vendors ask; an agent without a logo shows its tinted initial.
      void DrawAvatar(ImDrawList* dl, ImVec2 c, float r, const std::string& id)
      {
         dl->AddCircleFilled(c, r, TextTint(0.07f));
         dl->AddCircle(c, r, TextTint(0.14f), 0, 1.0f);
         const float half = std::round(r * 0.62f);
         const float scale = std::max(1.0f, ImGui::GetIO().DisplayFramebufferScale.x);
         const ImTextureID tex = AgentLogoTexture(id, (int)std::lround(half * 2.0f * scale), IsThemeLight());
         if (tex != ImTextureID{})
         {
            const ImVec2 mn(std::round(c.x - half), std::round(c.y - half));
            dl->AddImage(tex, mn, ImVec2(mn.x + half * 2.0f, mn.y + half * 2.0f));
         }
         else
         {
            const char name[2] = { (char)std::toupper((unsigned char)AgentName(id)[0]), 0 };
            UiType::Scope cap(UiType::Size::Caption, UiType::Weight::Semibold);
            const ImVec2 ts = ImGui::CalcTextSize(name);
            dl->AddText(ImVec2(std::floor(c.x - ts.x * 0.5f), std::floor(c.y - ts.y * 0.5f)), AgentTint(id), name);
         }
      }

      bool Ready(const std::string& id)
      {
         auto it = sAgents.find(id);
         return it != sAgents.end() && it->second.ready.Ready() && !it->second.authBroken;
      }

      bool AnyReady()
      {
         for (const auto& [id, s] : sAgents)
            if (s.ready.Ready() && !s.authBroken)
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

      void StartVerify(const std::string& id, chat::AgentKind kind)
      {
         AgentState& s = sAgents[id];
         if (s.verify != nullptr || s.login != nullptr || s.logout != nullptr || s.setup != nullptr)
            return;  // asked again once that ends
         s.verifyAt = ImGui::GetTime();
         chat::AgentRun::Options o;
         o.kind = kind;
         o.raw = true;
         o.argv = chat::StatusArgs(kind, OnMac());
         TerminalCommandEnv(o.env, o.cwd);
         s.verify = chat::AgentRun::Start(o);
      }

      // File probes, then the agents' own checks; on open, when a setup ends, and every 2 s while the setup card shows.
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
            const bool probeChanged = s.ready.signedIn != s.probe;
            s.probe = s.ready.signedIn;
            if (s.auth != chat::Auth::Unknown)
               s.ready.signedIn = s.auth == chat::Auth::In;
            if (s.ready.signedIn && s.login == nullptr)
               s.signingIn = false;
            // The agent's own check settles it: first look, a login file that came or went, now and then
            // while signed in. Never while signed out, so it cannot start a sign-in by itself.
            if (s.ready.installed && (s.verifyAt < 0.0 || probeChanged || (s.auth == chat::Auth::In && now - s.verifyAt > kStatusRecheckSeconds)))
               StartVerify(a.id, k);
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

      // Browser sign-in with no terminal: the CLI opens the browser itself and waits for the callback.
      void StartSignIn(const CliSetup::Agent& agent)
      {
         AgentState& s = sAgents[agent.id];
         chat::AgentKind kind;
         if (s.login != nullptr || s.logout != nullptr || !KindOf(agent.id, kind))
            return;
         if (s.verify != nullptr)
         {
            s.verify->Stop();
            s.verify = nullptr;
         }
         chat::AgentRun::Options o;
         o.kind = kind;
         o.raw = true;
         o.argv = chat::SignInArgs(kind);
         TerminalCommandEnv(o.env, o.cwd);
         s.loginError.clear();
         s.loginUrl.clear();
         s.loginLog.clear();
         s.needsCode = false;
         s.codeInput.clear();
         s.codeBytes = 0;
         s.loginStopped = false;
         s.loginTimedOut = false;
         s.signingIn = true;
         s.loginAt = ImGui::GetTime();
         s.login = chat::AgentRun::Start(o);
      }

      // The paste field for a sign-in that ends with a code shown in the browser: field + Submit on one line.
      void DrawCodeEntry(AgentState& s, float width)
      {
         if (s.login == nullptr || !s.needsCode)
            return;
         const float btnW = ImGui::CalcTextSize(T("Submit")).x + 2.0f * tok::space_2;
         ImGui::SetNextItemWidth(std::max(80.0f, width - btnW - tok::space_1));
         const bool enter = ImGui::InputTextWithHint("##signincode", T("Paste the code from the browser"), &s.codeInput, ImGuiInputTextFlags_EnterReturnsTrue);
         ImGui::SameLine(0.0f, tok::space_1);
         const bool press = ChipButton::Draw(T("Submit"), true, 26.0f);
         if (!(enter || press))
            return;
         std::string code = s.codeInput;
         while (!code.empty() && std::isspace((unsigned char)code.back()))
            code.pop_back();
         size_t b = 0;
         while (b < code.size() && std::isspace((unsigned char)code[b]))
            b++;
         code = code.substr(b);
         if (code.empty())
            return;
         s.codeBytes = s.login->Bytes();  // the ask already shown does not bring the field back
         s.login->Write(code + "\r");
         s.codeInput.clear();
         s.needsCode = false;
      }

      void PollLogins()
      {
         for (auto& [id, s] : sAgents)
         {
            if (s.login == nullptr)
               continue;
            std::vector<chat::AgentEvent> ev;
            s.login->Poll(ev);
            const std::string raw = s.login->Log();
            const std::string log = chat::StripAnsi(raw);
            if (s.loginUrl.empty())
               s.loginUrl = chat::FindUrl(log);
            // Only output after the last code sent counts: the old ask stays in the log.
            const unsigned long long fresh = s.login->Bytes() - s.codeBytes;
            const std::string recent = s.codeBytes == 0 ? raw : raw.substr(raw.size() - (size_t)std::min<unsigned long long>(raw.size(), fresh));
            s.needsCode = chat::AsksForCode(recent);
            if (!s.login->Done())
            {
               if (!s.loginStopped && ImGui::GetTime() - s.loginAt > chat::kSignInSeconds)
               {
                  s.loginStopped = s.loginTimedOut = true;
                  s.login->Stop();
               }
               continue;
            }
            bool error = false;
            for (const chat::AgentEvent& e : ev)
               if (e.kind == chat::AgentEvent::Kind::End)
                  error = e.error;
            error = error || s.loginStopped;  // a stopped run ends quietly, but it did not sign in
            s.loginLog = log;
            s.login = nullptr;
            s.signingIn = false;
            s.needsCode = false;
            if (!error)
            {
               s.authBroken = false;
               s.auth = chat::Auth::In;  // the CLI said it signed in; its status check confirms next
               s.everIn = true;
               s.signedOutHere = false;
               s.verifyAt = -1.0;
               sModelsFetchAt.erase(id);  // signed in: its model list can be asked for now
            }
            else if (s.loginTimedOut)
               s.loginError = T("Sign-in timed out. Try again.");
            else if (!s.loginStopped)
            {
               const chat::Friendly f = chat::Explain(AgentName(id), log);
               s.loginError = f.kind == chat::Friendly::Kind::Other ? std::string(T("Sign-in did not finish. Try again.")) : f.text;
            }
            CheckAgents(true);
         }
      }

      void OpenInTerminal(const CliSetup::Agent& agent)
      {
         using namespace CliSetup;
         const Os os = CurrentOs();
         std::string err;
         Perform(Action::kConnectAll, os, agent, RealExecutable(), PathsFor(AppPaths::AppSupportDir(), os),
                 [](const std::string&, const std::string& line) { TerminalRunInNewTab(line); }, &err);
      }

      void PollVerifies()
      {
         for (auto& [id, s] : sAgents)
         {
            if (s.verify == nullptr)
               continue;
            chat::AgentKind kind;
            if (!KindOf(id, kind))
               continue;
            std::vector<chat::AgentEvent> ev;
            s.verify->Poll(ev);
            const bool late = ImGui::GetTime() - s.verifyAt > kStatusCheckSeconds;
            if (!s.verify->Done())
            {
               if (late || chat::WaitsForSignIn(s.verify->Log()))
                  s.verify->Stop();  // never leave a check waiting for a browser
               continue;
            }
            bool exitOk = true;
            for (const chat::AgentEvent& e : ev)
               if (e.kind == chat::AgentEvent::Kind::End)
                  exitOk = !e.error;
            const std::string log = s.verify->Log();
            s.verify = nullptr;
            if (late)
               continue;  // too slow to tell: keep what was known
            const chat::Auth a = chat::SignedInFrom(kind, exitOk, log);
            if (a == chat::Auth::Unknown)
               continue;
            s.auth = a;
            s.ready.signedIn = a == chat::Auth::In;
            if (a == chat::Auth::In)
            {
               s.everIn = true;
               s.signedOutHere = false;
               if (kind == chat::AgentKind::Antigravity)
               {
                  std::vector<chat::Model> list = chat::ParseAgyModels(chat::StripAnsi(log));
                  if (!list.empty())
                     sModels[id] = std::move(list);
               }
            }
         }
      }

      void StartSignOut(const std::string& id)
      {
         AgentState& s = sAgents[id];
         chat::AgentKind kind;
         if (s.logout != nullptr || s.login != nullptr || !KindOf(id, kind))
            return;
         const std::vector<std::string> argv = chat::SignOutArgs(kind, OnMac());
         if (argv.empty())
            return;
         if (s.verify != nullptr)
         {
            s.verify->Stop();
            s.verify = nullptr;
         }
         chat::AgentRun::Options o;
         o.kind = kind;
         o.raw = true;
         o.argv = argv;
         TerminalCommandEnv(o.env, o.cwd);
         s.loginError.clear();
         s.logout = chat::AgentRun::Start(o);
      }

      void PollLogouts()
      {
         for (auto& [id, s] : sAgents)
         {
            if (s.logout == nullptr)
               continue;
            std::vector<chat::AgentEvent> drop;
            s.logout->Poll(drop);
            if (!s.logout->Done())
               continue;
            s.logout = nullptr;
            s.auth = chat::Auth::Out;
            s.ready.signedIn = false;
            s.everIn = false;
            s.authBroken = false;
            s.signedOutHere = true;
            sThread.sessions.erase(id);  // a conversation of the old login does not carry over
            sModels.erase(id);
            s.verifyAt = -1.0;           // its own check confirms
            CheckAgents(true);
            if (s.signInAfterLogout)
            {
               s.signInAfterLogout = false;
               if (const CliSetup::Agent* a = CliSetup::FindAgent(id))
                  StartSignIn(*a);
            }
         }
      }

      // Where an agent stands and the one thing to press.
      AgentView ViewOf(const std::string& id)
      {
         AgentView v;
         const auto it = sAgents.find(id);
         if (it == sAgents.end())
         {
            v.status = T("Checking…");
            v.busy = true;
            return v;
         }
         const AgentState& s = it->second;
         chat::AgentKind kind = chat::AgentKind::Claude;
         KindOf(id, kind);
         const bool canSignOut = !chat::SignOutArgs(kind, OnMac()).empty();
         const bool stale = s.authBroken || (s.auth == chat::Auth::Out && s.probe && !s.signedOutHere) || (s.everIn && !s.ready.signedIn);
         if (s.setup != nullptr)
            v.status = T("Setting up…"), v.busy = true;
         else if (s.logout != nullptr)
            v.status = T("Signing out…"), v.busy = true;
         else if (s.login != nullptr || s.signingIn)
            v.status = T("Waiting for sign-in…"), v.busy = true, v.act = Act::Cancel;
         else if (s.failed)
            v.status = T("Setup did not finish"), v.act = Act::TryAgain;
         else if (!s.ready.installed)
            v.status = T("Not installed"), v.act = Act::SetUp;
         else if (!s.ready.connected)
            v.status = T("Needs setup"), v.act = Act::SetUp;
         else if (s.ready.signedIn && !s.authBroken)
            v.status = T("Ready"), v.ready = true, v.act = canSignOut ? Act::SignOut : Act::None;
         else if (s.verify != nullptr && s.auth == chat::Auth::Unknown)
            v.status = T("Checking…"), v.busy = true;
         else if (stale)
            v.status = T("Signed out"), v.act = Act::SignInAgain;
         else
            v.status = T("Not signed in"), v.act = Act::SignIn;
         return v;
      }

      void Do(const std::string& id, Act act)
      {
         const CliSetup::Agent* a = CliSetup::FindAgent(id);
         if (a == nullptr)
            return;
         AgentState& s = sAgents[id];
         chat::AgentKind kind = chat::AgentKind::Claude;
         KindOf(id, kind);
         switch (act)
         {
            case Act::SetUp:
            case Act::TryAgain:
               s.failed = false;
               s.loginError.clear();
               s.chain = true;  // sign in right after, without another click
               SetUpInBackground(*a);
               break;
            case Act::SignIn:
               s.loginError.clear();
               StartSignIn(*a);
               break;
            case Act::SignInAgain:
               s.loginError.clear();
               // agy has no login command: its sign-in turn only asks when no login is stored, so clear it first.
               if (kind == chat::AgentKind::Antigravity && !chat::SignOutArgs(kind, OnMac()).empty())
               {
                  s.signInAfterLogout = true;
                  StartSignOut(id);
               }
               else
                  StartSignIn(*a);
               break;
            case Act::SignOut:
               StartSignOut(id);
               break;
            case Act::Cancel:
               if (s.login != nullptr)
               {
                  s.loginStopped = true;
                  s.login->Stop();
               }
               break;
            default:
               break;
         }
      }

      void PollSetups()
      {
         PollLogins();
         PollVerifies();
         PollLogouts();
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
            if (!s.failed && s.chain && (!s.ready.signedIn || s.authBroken))
            {
               s.chain = false;
               if (const CliSetup::Agent* ag = CliSetup::FindAgent(id))
                  StartSignIn(*ag);
            }
            s.chain = false;
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

      // The code pane beside the chat (see DrawCodeChip).
      struct CodeView
      {
         bool open = false;
         std::string title;
         std::string code;
         std::vector<std::string> lines;
         double copiedAt = -10.0;
      } sCodeView;

      void NewChat()
      {
         sThread = chat::Thread();
         sThread.id = NewId();
         sUi.clear();
         sAttach.clear();
         sCodeView.open = false;
      }

      void OpenThread(const std::string& path)
      {
         chat::Thread t;
         if (!chat::LoadThread(path, t))
            return;
         sThread = std::move(t);
         sUi.assign(sThread.entries.size(), EntryUi());
         sScrollToEnd = true;
         sCodeView.open = false;
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
         chat::TurnOptions to;
         {
            const auto& prefs = CliSetup::Prefs();
            const auto m = prefs.models.find(agent);
            if (m != prefs.models.end())
               to.model = m->second;
            to.folderAccess = prefs.folderAccess;
         }
         o.argv = chat::AgentArgs(r.kind, s.live ? prompt : chat::Briefing() + prompt, s.id, s.live, dirs, to);
         TerminalCommandEnv(o.env, o.cwd);
         if (!CliSetup::Prefs().workspace.empty() && std::filesystem::is_directory(CliSetup::Prefs().workspace))
            o.cwd = CliSetup::Prefs().workspace;
         r.run = chat::AgentRun::Start(o);
         r.steps = (int)sThread.entries.size();
         Push(Make(Entry::Kind::Steps, "", agent));
         r.startAt = ImGui::GetTime();
         sRuns.push_back(std::move(r));
      }

      // `/command`: most answer here with no agent; /explain, /check and /docs go to one with where to look.
      // True when it was handled completely.
      bool HandleSlash(const std::string& line, const std::string& name, const std::string& arg, std::string& agentPrompt)
      {
         const auto say = [&](const std::string& text)
         {
            if (sThread.title.empty())
               sThread.title = chat::TitleFor(line);
            Push(Make(Entry::Kind::User, line));
            if (!text.empty())
               Push(Make(Entry::Kind::Note, text));
            sInput.clear();
            SaveNow();
         };
         bool known = false;
         for (const chat::SlashCommand& c : chat::SlashCommands())
            known = known || name == c.name;
         if (!known)
         {
            say("");
            Push(Make(Entry::Kind::Error, std::string(T("Not a command: /")) + name + ". " + T("Type /help to see them.")));
            return true;
         }
         if (name == "new")
         {
            sInput.clear();
            SaveNow();
            NewChat();
            return true;
         }
         if (name == "agent")
         {
            std::string id = chat::Lower(arg);
            for (const CliSetup::Agent& a : CliSetup::Agents())
               if (chat::Lower(a.name) == id)
                  id = a.id;
            if (!Ready(id))
            {
               say("");
               Push(Make(Entry::Kind::Error, arg.empty() ? std::string(T("Which agent? claude, codex or antigravity.")) : std::string(T("Not ready: ")) + arg));
               return true;
            }
            CliSetup::Prefs().agent = id;
            SaveCliConfig();
            say(std::string(T("Now talking to ")) + AgentName(id) + ".");
            return true;
         }
         if (name == "model")
         {
            const std::string cur = DefaultAgent();
            chat::AgentKind kind;
            if (cur.empty() || !KindOf(cur, kind))
            {
               say("");
               Push(Make(Entry::Kind::Error, T("Set up an agent first.")));
               return true;
            }
            std::string id = arg;
            if (arg.empty() || chat::Lower(arg) == "default")
               CliSetup::Prefs().models.erase(cur);
            else
            {
               for (const chat::Model& m : chat::FixedModels(kind))
                  if (chat::Lower(m.label) == chat::Lower(arg) || m.id == chat::Lower(arg))
                     id = m.id;
               CliSetup::Prefs().models[cur] = id;
            }
            SaveCliConfig();
            say(std::string(AgentName(cur)) + T(" will use ") + (arg.empty() ? std::string(T("its default model")) : id) + ".");
            return true;
         }
         if (name == "undo")
            return false;  // runs as the direct command `undo`
         const std::string answer = chat::SlashAnswer(name, arg);
         if (!answer.empty())
         {
            say(answer);
            return true;
         }
         agentPrompt = chat::SlashPrompt(name, arg);
         if (agentPrompt.empty())
         {
            say("");
            Push(Make(Entry::Kind::Error, std::string(T("Add a question: /")) + name + " …"));
            return true;
         }
         return false;
      }

      void Send()
      {
         std::string line = sInput;
         while (!line.empty() && std::isspace((unsigned char)line.back()))
            line.pop_back();
         if (line.empty() && sAttach.empty())
            return;
         if (!sRuns.empty())
         {
            sQueue.push_back({ line, sAttach });
            sInput.clear();
            sAttach.clear();
            return;
         }
         RefreshCatalog(true);
         std::string slashPrompt;
         {
            std::string sn, sa;
            if (sAttach.empty() && chat::ParseSlash(line, sn, sa))
            {
               if (HandleSlash(line, sn, sa, slashPrompt))
                  return;
               if (slashPrompt.empty() && sn == "undo")
                  line = "undo";
            }
         }
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
         std::string prompt = slashPrompt.empty() ? PromptFor(line, p) : slashPrompt;
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
            // Plain words for what went wrong; a "sign in" error flips the agent back to Sign in.
            const chat::Friendly f = chat::Explain(AgentName(r.agent), r.errorText);
            if (f.kind == chat::Friendly::Kind::SignIn || f.kind == chat::Friendly::Kind::Terms)
               sAgents[r.agent].authBroken = true;
            char note[300];
            const std::string why = f.kind != chat::Friendly::Kind::Other ? f.text : r.errorText.empty() ? std::string(T("stopped with an error.")) : r.errorText;
            std::snprintf(note, sizeof note, "%s: %s", AgentName(r.agent), why.c_str());
            Entry err = Make(Entry::Kind::Error, note, r.agent);
            if (f.kind == chat::Friendly::Kind::Other || r.errorText.size() > why.size())
               err.detail = chat::StripAnsi(r.errorText);
            InsertAt(at++, std::move(err));
         }
         for (const std::string& d : r.denied)
            InsertAt(at++, Make(Entry::Kind::Note, std::string(T("Not allowed:")) + " " + d, r.agent));
      }

      void SendQueued();

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
            // Once a second: a turn that asks for a sign-in waits for input nobody can type, and one that has
            // printed nothing for a long time is stuck. Either is ended here and said plainly.
            const double now = ImGui::GetTime();
            if (r.activeAt < 0.0)
               r.activeAt = now;
            if (r.cut.empty() && now - r.checkedAt >= 1.0)
            {
               r.checkedAt = now;
               const unsigned long long b = r.run->Bytes();
               if (b != r.bytes)
                  r.bytes = b, r.activeAt = now;
               if (chat::WaitsForSignIn(r.run->Log()))
                  r.cut = "not signed in";
               else if (now - r.activeAt > kTurnSilentSeconds)
                  r.cut = T("It stopped responding, so the chat ended the turn. Try again.");
               if (!r.cut.empty())
                  r.run->Stop();
            }
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
                     {
                        steps.marks.resize(steps.lines.size(), chat::kStepRan);
                        steps.lines.push_back(r.answer.substr(0, r.answer.find('\n')));
                        steps.marks.push_back(chat::kStepNote);
                     }
                     r.answer = e.text;
                     break;
                  case chat::AgentEvent::Kind::Tool:
                     steps.marks.resize(steps.lines.size(), chat::kStepRan);
                     steps.lines.push_back(e.text);
                     steps.marks.push_back(chat::kStepRan);
                     break;
                  case chat::AgentEvent::Kind::ToolResult:
                     if (e.error)
                     {
                        steps.ok = false;
                        // The result belongs to the last command started.
                        steps.marks.resize(steps.lines.size(), chat::kStepRan);
                        for (size_t k = steps.marks.size(); k-- > 0;)
                           if (steps.marks[k] != chat::kStepNote)
                           {
                              steps.marks[k] = chat::kStepFailed;
                              break;
                           }
                     }
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
               if (!r.cut.empty())
                  r.error = true, r.errorText = r.cut;
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
         SendQueued();
      }

      // The turn ended: the next queued message goes now; after Stop, all of them go back to the box.
      void SendQueued()
      {
         if (sQueue.empty())
         {
            sQueueHold = false;
            return;
         }
         if (sQueueHold)
         {
            std::string back;
            for (const Queued& q : sQueue)
            {
               back += (back.empty() ? "" : "\n") + q.text;
               for (const std::string& f : q.files)
                  if (std::find(sAttach.begin(), sAttach.end(), f) == sAttach.end())
                     sAttach.push_back(f);
            }
            sInput = sInput.empty() ? back : back + "\n" + sInput;
            sQueue.clear();
            sQueueHold = false;
            sRefocus = sCursorToEnd = true;
            return;
         }
         Queued next = std::move(sQueue.front());
         sQueue.erase(sQueue.begin());
         std::string keep = std::move(sInput);
         std::vector<std::string> keepFiles = std::move(sAttach);
         sInput = next.text;
         sAttach = next.files;
         Send();
         sInput = std::move(keep);
         sAttach = std::move(keepFiles);
      }

      // Queued messages, one quiet row each over the composer: click to take it back into the box, × to drop it.
      void DrawQueue(float width)
      {
         if (sQueue.empty())
            return;
         ImDrawList* dl = ImGui::GetWindowDrawList();
         int take = -1, drop = -1;
         for (size_t i = 0; i < sQueue.size(); i++)
         {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::PushID((int)i);
            const bool row = ImGui::InvisibleButton("##queued", ImVec2(width - kQueueRowH, kQueueRowH));
            const bool hot = ImGui::IsItemHovered();
            ImGui::SameLine(0.0f, 0.0f);
            const bool x = ImGui::InvisibleButton("##unqueue", ImVec2(kQueueRowH, kQueueRowH));
            const bool xHot = ImGui::IsItemHovered();
            ImGui::PopID();
            if (row)
               take = (int)i;
            if (x)
               drop = (int)i;
            if (hot || xHot)
               dl->AddRectFilled(p, ImVec2(p.x + width, p.y + kQueueRowH), TextTint(0.05f), tok::radius_tile);
            const float ty = p.y + (kQueueRowH - ImGui::GetFontSize()) * 0.5f;
            const char* tag = T("Queued");
            dl->AddText(ImVec2(p.x + tok::space_2, ty), Dim(), tag);
            const float tx = p.x + tok::space_2 + ImGui::CalcTextSize(tag).x + tok::space_2;
            std::string one = sQueue[i].text.substr(0, sQueue[i].text.find('\n'));
            if (!sQueue[i].files.empty())
               one += (one.empty() ? "" : "  ") + std::string("+") + std::to_string(sQueue[i].files.size());
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(hot ? ImGui::GetColorU32(ImGuiCol_Text) : TextTint(0.75f)));
            ImGui::RenderTextEllipsis(dl, ImVec2(tx, ty), ImVec2(p.x + width - kQueueRowH, p.y + kQueueRowH), p.x + width - kQueueRowH - tok::space_1, one.c_str(), nullptr, nullptr);
            ImGui::PopStyleColor();
            glyph::Draw(dl, ImVec2(p.x + width - kQueueRowH * 0.5f, p.y + kQueueRowH * 0.5f), tok::icon_sm, xHot ? ImGui::GetColorU32(ImGuiCol_Text) : Dim(), IconsInfinite::Close);
            if (hot)
               ImGui::SetTooltip("%s", T("Edit"));
            else if (xHot)
               ImGui::SetTooltip("%s", T("Remove"));
         }
         if (take >= 0)
         {
            Queued q = std::move(sQueue[(size_t)take]);
            sQueue.erase(sQueue.begin() + take);
            sInput = sInput.empty() ? q.text : q.text + "\n" + sInput;
            for (const std::string& f : q.files)
               if (std::find(sAttach.begin(), sAttach.end(), f) == sAttach.end())
                  sAttach.push_back(f);
            sRefocus = sCursorToEnd = true;
         }
         else if (drop >= 0)
            sQueue.erase(sQueue.begin() + drop);
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

      std::string ValueText(const chat::CatalogParam& p) { return chat::ShownValue(p); }

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
         {
            // Offered by the name people use: @gemini, @chatgpt; the tool's own id still matches.
            const std::string handle = chat::Lower(AgentName(a.id));
            if (Ready(a.id) && (Score(handle, want) >= 0 || Score(a.id, want) >= 0 || Score(a.name, want) >= 0))
               out.push_back({ "@" + handle + " ", handle, a.name, "", tok::U32(tok::pin_pred, Light()), false });
         }
      }

      // Rows read key then value; the label is in the tooltip, so nothing wraps or crowds.
      void AddParams(std::vector<Cand>& out, const chat::CatalogNode& n, const std::string& want)
      {
         std::vector<std::pair<int, Cand>> ranked;
         for (const chat::CatalogParam& p : n.params)
         {
            if (chat::IsInternal(p))
               continue;
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

      // What each word does, said plainly beside it in the list.
      const char* WordHint(const std::string& w)
      {
         static const std::map<std::string, const char*> kHint = {
            { "add", "put a new node on the canvas" },     { "connect", "wire one node into another" },
            { "remove", "delete a node" },                 { "bypass", "skip a node, keep its place" },
            { "unbypass", "turn a skipped node back on" }, { "undo", "take back the last change" },
            { "redo", "bring it back" },                   { "to", "into this node" },
            { "modulate", "let a source move a control" }, { "set", "give a control a value" },
            { "on", "" },                                  { "off", "" } };
         const auto it = kHint.find(chat::Lower(w));
         return it != kHint.end() ? it->second : "";
      }

      void AddWords(std::vector<Cand>& out, const std::vector<std::string>& words, const std::string& want)
      {
         for (const std::string& w : words)
            if (Score(w, want) >= 0 && chat::Lower(w) != want)
               out.push_back({ w + " ", w, WordHint(w), "", 0, false });
      }

      // `/name` being typed as the whole first word: the completion replaces from the start.
      bool SlashTyping() { return !sInput.empty() && sInput[0] == '/' && sInput.find_first_of(" \t\n") == std::string::npos; }

      size_t ReplaceFrom() { return SlashTyping() ? 0 : sParse.partialBegin; }

      std::vector<Cand> Candidates(const chat::Parse& p)
      {
         std::vector<Cand> out;
         if (SlashTyping())
         {
            for (const chat::SlashCommand* c : chat::MatchSlash(sInput.substr(1)))
               out.push_back({ "/" + std::string(c->name) + " ", "/" + std::string(c->name), c->summary, "", 0, false });
            return out;
         }
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
         sPickMoved = true;
      }

      // Enter sends what was typed. It takes the highlighted suggestion only when ↑↓ moved to it, or for an
      // `@name` / `/command` still being typed, where sending the half word would mean nothing.
      bool EnterPicks()
      {
         const Cand* c = Picked();
         if (c == nullptr)
            return false;
         if (sPickMoved)
            return true;
         const std::string typed = chat::Lower(SlashTyping() ? sInput : sParse.partial);
         const bool menu = SlashTyping() || (!typed.empty() && typed[0] == '@');
         std::string insert = chat::Lower(c->insert);
         while (!insert.empty() && insert.back() == ' ')
            insert.pop_back();
         return menu && insert != typed;
      }

      // The field is not active (Enter or a click took it): change the string, then focus it again.
      void AcceptInactive()
      {
         if (const Cand* c = Picked())
         {
            sInput = sInput.substr(0, std::min(ReplaceFrom(), sInput.size())) + c->insert;
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
               const int begin = (int)std::min(ReplaceFrom(), (size_t)data->BufTextLen);
               data->DeleteChars(begin, data->BufTextLen - begin);
               data->InsertChars(begin, c->insert.c_str());
               data->CursorPos = data->SelectionStart = data->SelectionEnd = data->BufTextLen;
            }
         }
         return 0;
      }

      // ---------------------------------------------------------------- drawing

      // Selects and frames the node; with a key it also flashes that control on the node.
      void Focus(int node, const std::string& key = {})
      {
         json r;
         std::string e;
         json args = { { "index", node } };
         if (!key.empty())
            args["key"] = key;
         HandleRpcCommand("focus", args, r, e);
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
         if (s.verb == "web")
            return IconsInfinite::Browser;
         if (s.verb == "say")
            return IconsInfinite::Edit;
         if (s.verb == "reveal")
            return IconsInfinite::Folder;
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
            Focus(s.node, s.key);
            ok = true;
         }
         else if (s.verb == "web")
         {
            Platform::OpenExternalUrl(s.arg);
            ok = true;
         }
         else if (s.verb == "say")
         {
            sInput = s.arg;
            sRefocus = true;
            sCursorToEnd = true;
            ok = true;
         }
         else if (s.verb == "open")
            ok = HandleRpcCommand("panel", { { "name", s.arg }, { "open", true } }, r, err) && err.empty();
         else if (s.verb == "reveal")
            ok = HandleRpcCommand("reveal", { { "path", s.arg } }, r, err) && err.empty();
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
         const bool once = s.verb != "go" && s.verb != "open" && s.verb != "web" && s.verb != "say" && s.verb != "reveal";
         const bool done = once && sActed.count(id) != 0;
         const bool live = s.verb == "add" || s.verb == "undo" || s.verb == "open" || s.verb == "web" || s.verb == "say" || s.verb == "reveal" || sCat.Node(s.node) != nullptr;
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
            Focus(s.node, s.kind == chat::Seg::Kind::Param ? s.key : std::string());
      }

      // Words and chips laid out in `width`, wrapping between words. Returns the height used.
      float DrawFlow(const std::vector<chat::Seg>& segs, ImVec2 origin, float width, int idBase, ImU32 col)
      {
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const float lineH = std::floor(ImGui::GetTextLineHeight() + tok::space_1 + 1.0f);
         const float space = ImGui::CalcTextSize(" ").x;
         float x = 0.0f, y = 0.0f;
         int chip = 0;
         sFlowBreak = true;
         const auto record = [&](const std::string& text, ImVec2 mn, float w, ImFont* font)
         {
            if (sFlowEntry < 0)
               return;
            Piece pc;
            pc.entry = sFlowEntry;
            pc.min = mn;
            pc.max = ImVec2(mn.x + w, mn.y + lineH);
            pc.text = text;
            pc.font = font;
            pc.size = ImGui::GetFontSize();
            pc.breakBefore = sFlowBreak;
            sFlowBreak = false;
            sPieces.push_back(std::move(pc));
         };
         for (const chat::Seg& s : segs)
         {
            if (s.kind == chat::Seg::Kind::Action)
            {
               const std::string aid = sThread.id + "/" + std::to_string(idBase) + "/" + std::to_string(chip++) + s.verb + s.arg;
               if (s.verb == "undo" && sActed.count(aid) != 0)
                  continue;  // an undo that happened leaves nothing to press
               const float w = ActionWidth(s);
               if (x > 0.0f && x + w > width)
                  x = 0.0f, y += lineH;
               DrawAction(dl, ImVec2(origin.x + x, origin.y + y + 1.0f), s, lineH - 2.0f, aid);
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
               record(s.text, ImVec2(origin.x + x + tok::space_1 + 1.0f, origin.y + y), w - 2.0f * tok::space_1 - 2.0f, ImGui::GetFont());
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
                  sFlowBreak = true;
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
               dl->AddText(font, ImGui::GetFontSize(), p, s.dim ? Dim() : col, word.c_str());
               if (!word.empty())
                  record(word, ImVec2(origin.x + x, origin.y + y), w, font);
               x += w;
               if (e < s.text.size() && s.text[e] == ' ' && sFlowEntry >= 0 && !sPieces.empty() && sPieces.back().entry == sFlowEntry)
                  sPieces.back().spaceAfter = true;
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

      // A sound the agent made: one row, play / stop on the same preview player the Library uses.
      void DrawAudio(const std::string& rawPath, float width)
      {
         std::string path = rawPath;
         if (!path.empty() && path[0] == '~')
         {
            const char* home = std::getenv("HOME");
            if (home != nullptr)
               path = std::string(home) + path.substr(1);
         }
         static std::string sError;
         const bool exists = std::filesystem::exists(path);
         const bool playing = gPreviewingSamplePath == path && AudioEngine::Instance().Preview().IsPlaying();
         const float rowH = 30.0f;
         const ImVec2 at = ImGui::GetCursorScreenPos();
         ImGui::PushID(rawPath.c_str());
         const bool clicked = ImGui::InvisibleButton("##aud", ImVec2(width, rowH)) && exists;
         ImGui::PopID();
         ImDrawList* dl = ImGui::GetWindowDrawList();
         dl->AddRectFilled(at, ImVec2(at.x + width, at.y + rowH), TextTint(ImGui::IsItemHovered() ? 0.09f : 0.05f), tok::radius_tile);
         const ImVec2 c(at.x + rowH * 0.5f, at.y + rowH * 0.5f);
         const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
         if (playing)
            glyph::DrawPlayerPause(dl, c, rowH * 0.5f, col);
         else
            glyph::DrawPlayerPlay(dl, c, rowH * 0.5f, col, true);
         const std::string name = std::filesystem::path(path).filename().string();
         const std::string shown = exists ? name : name + "  " + T("(missing)");
         dl->AddText(ImVec2(at.x + rowH, at.y + (rowH - ImGui::GetFontSize()) * 0.5f), exists ? col : Dim(), shown.c_str());
         if (clicked)
         {
            SamplePreviewPlayer& preview = AudioEngine::Instance().Preview();
            preview.Stop();
            if (playing)
               gPreviewingSamplePath.clear();
            else
            {
               auto* decoded = new Platform::SampleBuffer();
               std::string error;
               if (Platform::DecodeAudioFileToBuffer(path, *decoded, error))
               {
                  preview.Play(decoded);
                  gPreviewingSamplePath = path;
               }
               else
                  delete decoded;
            }
         }
      }

      std::string ExpandHome(const std::string& raw)
      {
         if (!raw.empty() && raw[0] == '~')
            if (const char* home = std::getenv("HOME"))
               return std::string(home) + raw.substr(1);
         return raw;
      }

      // A small "Show in folder" under a file the agent made, so it can be kept.
      void DrawReveal(const std::string& rawPath)
      {
         const std::string path = ExpandHome(rawPath);
         if (!std::filesystem::exists(path))
            return;
         UiType::Scope cap(UiType::Size::Body, UiType::Weight::Medium);
         const char* label = T("Show in folder");
         const ImVec2 at = ImGui::GetCursorScreenPos();
         const ImVec2 sz = ImGui::CalcTextSize(label);
         ImGui::PushID(rawPath.c_str());
         const bool clicked = ImGui::InvisibleButton("##reveal", ImVec2(sz.x, sz.y + 2.0f));
         ImGui::PopID();
         const bool hot = ImGui::IsItemHovered();
         ImGui::GetWindowDrawList()->AddText(at, hot ? ImGui::GetColorU32(ImGuiCol_Text) : Dim(), label);
         if (hot)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
         if (clicked)
            Platform::RevealInFileManager(path);
      }

      // Any other file an agent made: its name, and where it is.
      void DrawFile(const std::string& rawPath, float width)
      {
         const std::string path = ExpandHome(rawPath);
         const bool exists = std::filesystem::exists(path);
         const float rowH = 30.0f;
         const ImVec2 at = ImGui::GetCursorScreenPos();
         ImGui::PushID(rawPath.c_str());
         const bool clicked = ImGui::InvisibleButton("##file", ImVec2(width, rowH)) && exists;
         ImGui::PopID();
         ImDrawList* dl = ImGui::GetWindowDrawList();
         dl->AddRectFilled(at, ImVec2(at.x + width, at.y + rowH), TextTint(ImGui::IsItemHovered() ? 0.09f : 0.05f), tok::radius_tile);
         glyph::Draw(dl, ImVec2(at.x + rowH * 0.5f, at.y + rowH * 0.5f), tok::icon_sm, Dim(), IconsInfinite::Library);
         const std::string name = std::filesystem::path(path).filename().string();
         const std::string shown = exists ? name : name + "  " + T("(missing)");
         dl->AddText(ImVec2(at.x + rowH, at.y + (rowH - ImGui::GetFontSize()) * 0.5f), exists ? ImGui::GetColorU32(ImGuiCol_Text) : Dim(), shown.c_str());
         if (exists)
         {
            const char* hint = T("Show in folder");
            const float w = ImGui::CalcTextSize(hint).x;
            dl->AddText(ImVec2(at.x + width - w - tok::space_2, at.y + (rowH - ImGui::GetFontSize()) * 0.5f), Dim(), hint);
         }
         if (clicked)
            Platform::RevealInFileManager(path);
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

      // Code a turn wrote (a code param, a long block in a reply) folds to a chip: the code glyph and its line
      // count. The chip opens the whole code in a pane beside the chat, monospace with line numbers.
      int LineCount(const std::string& code)
      {
         int n = code.empty() ? 0 : 1;
         for (size_t i = 0; i + 1 < code.size(); i++)
            n += code[i] == '\n' ? 1 : 0;
         return n;
      }

      void OpenCode(const std::string& title, const std::string& code)
      {
         if (sCodeView.open && sCodeView.code == code)
         {
            sCodeView.open = false;  // the chip toggles its own pane
            return;
         }
         sCodeView.open = true;
         sCodeView.title = title;
         sCodeView.code = code;
         sCodeView.lines.clear();
         size_t at = 0;
         while (at <= code.size())
         {
            const size_t nl = code.find('\n', at);
            const size_t end = nl == std::string::npos ? code.size() : nl;
            std::string line = code.substr(at, end - at);
            for (size_t t; (t = line.find('\t')) != std::string::npos;)
               line.replace(t, 1, "   ");
            sCodeView.lines.push_back(line);
            if (nl == std::string::npos)
               break;
            at = nl + 1;
         }
         while (sCodeView.lines.size() > 1 && sCodeView.lines.back().empty())
            sCodeView.lines.pop_back();
      }

      void DrawCodeChip(const std::string& title, const std::string& code, const char* id)
      {
         UiType::Scope s(UiType::Size::Body, UiType::Weight::Medium);
         const int n = LineCount(code);
         char label[48];
         std::snprintf(label, sizeof label, n == 1 ? T("%d line") : T("%d lines"), n);
         const float icon = tok::icon_sm;
         const float pad = tok::space_2;
         const ImVec2 ts = ImGui::CalcTextSize(label);
         const ImVec2 sz(pad + icon + 6.0f + ts.x + pad, ChipButton::kHeight);
         const ImVec2 p = ImGui::GetCursorScreenPos();
         if (ImGui::InvisibleButton(id, sz))
            OpenCode(title, code);
         const bool hot = ImGui::IsItemHovered();
         const bool on = sCodeView.open && sCodeView.code == code;
         if (hot)
            ImGui::SetTooltip("%s", on ? T("Close code") : T("Open code"));
         ImDrawList* dl = ImGui::GetWindowDrawList();
         dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), TextTint(hot ? 0.10f : 0.06f), tok::radius_field);
         if (on)
            dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y), ImGui::GetColorU32(AccentEmphasisSelected()), tok::radius_field);
         const ImU32 fg = on ? ImGui::GetColorU32(AccentEmphasisSelected()) : (hot ? ImGui::GetColorU32(ImGuiCol_Text) : Dim());
         glyph::Draw(dl, ImVec2(p.x + pad + icon * 0.5f, p.y + sz.y * 0.5f), icon, fg, IconsInfinite::Code);
         dl->AddText(ImVec2(p.x + pad + icon + 6.0f, std::round(p.y + (sz.y - ImGui::GetFontSize()) * 0.5f)), fg, label);
      }

      // Long code in a reply folds to the chip; a short snippet stays inline, in the monospace face.
      constexpr int kInlineCodeLines = 12;

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
                  if (LineCount(b.text) > kInlineCodeLines)
                  {
                     DrawCodeChip(T("Code"), b.text, cid);
                     ImGui::Dummy(ImVec2(0.0f, tok::space_1));
                     break;
                  }
                  CodeBlock::Draw(b.text.c_str(), cid);
                  break;
               }
               case chat::Block::Kind::Audio:
                  DrawAudio(b.text, width);
                  DrawReveal(b.text);
                  ImGui::Dummy(ImVec2(0.0f, tok::space_1));
                  break;
               case chat::Block::Kind::Image:
                  DrawImage(b.text, width);
                  DrawReveal(b.text);
                  ImGui::Dummy(ImVec2(0.0f, tok::space_1));
                  break;
               case chat::Block::Kind::File:
                  DrawFile(b.text, width);
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
                     Weight scope(UiType::Weight::Semibold);
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
            Weight cap(UiType::Weight::Medium);
            h = std::max(ImGui::GetTextLineHeight(), 16.0f);
         }
         DrawAvatar(dl, ImVec2(at.x + 8.0f, at.y + h * 0.5f), 8.0f, e.agent);
         Weight cap(UiType::Weight::Medium);
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
         const ImVec2 body = ImGui::GetCursorScreenPos();
         sFlowEntry = id;
         DrawBlocks(BlocksFor(e.text), width, id);
         sFlowEntry = -1;
         sEntryRects.push_back({ id, ImRect(body, ImVec2(body.x + width, ImGui::GetCursorScreenPos().y)) });
         ui.hovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(top, ImVec2(top.x + width, ImGui::GetCursorScreenPos().y));
      }

      // What changed in the patch: one line per change, links to each node and param, code as a block.
      // The code pane is named after the param the code lives in ("sketch 3d · Code").
      std::string CodeTitle(const chat::ChangeLine& l)
      {
         const std::vector<chat::Block>& blocks = BlocksFor(l.text);
         if (!blocks.empty())
            for (const chat::Seg& s : blocks[0].segs)
               if (s.kind == chat::Seg::Kind::Param || s.kind == chat::Seg::Kind::Node)
               {
                  // A param chip reads "node.label"; the label may hold dots itself (p.count).
                  const chat::CatalogNode* n = s.kind == chat::Seg::Kind::Param ? sCat.Node(s.node) : nullptr;
                  const chat::CatalogParam* p = n != nullptr ? n->Find(s.key) : nullptr;
                  const std::string label = p == nullptr ? std::string() : "." + (p->label.empty() ? p->key : p->label);
                  if (!label.empty() && s.text.size() > label.size() && s.text.compare(s.text.size() - label.size(), label.size(), label) == 0)
                     return s.text.substr(0, s.text.size() - label.size()) + " · " + label.substr(1);
                  return s.text;
               }
         return T("Code");
      }

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
            Weight cap(UiType::Weight::Medium);
            // The heading row is as tall as the Undo chip, so the mark, the word and the chip share one centre line.
            const ImVec2 h = ImGui::GetCursorScreenPos();
            const float headH = ChipButton::kHeight;
            const ImU32 accent = e.undone ? Dim() : (e.ok ? ImGui::GetColorU32(AccentEmphasisSelected()) : Red());
            glyph::Draw(dl, ImVec2(h.x + 6.0f, h.y + headH * 0.5f), tok::icon_sm, accent, IconsInfinite::Bolt);
            dl->AddText(ImVec2(h.x + 16.0f, std::round(h.y + (headH - ImGui::GetFontSize()) * 0.5f)), Dim(), e.undone ? T("Undone") : (e.ok ? T("Changed") : T("Did not run")));
            ImGui::Dummy(ImVec2(inner, headH + tok::space_1));
         }
         const ImU32 col = e.undone ? Dim() : ImGui::GetColorU32(ImGuiCol_Text);
         const size_t cap = 8;
         const size_t shown = ui.open ? e.changes.size() : std::min(e.changes.size(), cap);
         for (size_t i = 0; i < shown; i++)
         {
            const chat::ChangeLine& l = e.changes[i];
            const std::vector<chat::Block>& blocks = BlocksFor(l.text);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            // Moved since the turn (an edit, randomize, a cable): say what the canvas shows now, dimmed.
            const std::string now = e.undone ? std::string() : chat::Drift(l, sCat);
            std::vector<chat::Seg> segs = blocks.empty() ? std::vector<chat::Seg>() : blocks[0].segs;
            if (!now.empty())
            {
               chat::Seg note;
               note.text = std::string(" · ") + T("now") + " " + now;
               note.dim = true;
               segs.push_back(note);
            }
            const float h = segs.empty() ? rowH : DrawFlow(segs, p, inner, id * 100 + (int)i, col);
            ImGui::SetCursorScreenPos(p);
            ImGui::Dummy(ImVec2(inner, h));
            if (!l.code.empty() && !e.undone)
            {
               char cid[40];
               std::snprintf(cid, sizeof cid, "##chcode%d_%d", id, (int)i);
               ImGui::Dummy(ImVec2(0.0f, 2.0f));
               DrawCodeChip(CodeTitle(l), l.code, cid);
               ImGui::Dummy(ImVec2(0.0f, 2.0f));
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
            ImGui::SetCursorScreenPos(ImVec2(at.x + width - bw - pad, at.y + pad));
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

      // What the agent did, folded to one quiet row: "Made 3 changes · 1 failed", or "Working… 12s" with the
      // latest step under it while it runs. Unfolded, one row per step in plain words ("Added Noise"), the
      // command itself in the tooltip; a command with no plain words shows as itself, in monospace.
      void DrawSteps(Entry& e, EntryUi& ui, float width, int id, bool running, bool named, double since)
      {
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const ImVec2 at = ImGui::GetCursorScreenPos();
         const chat::StepView v = chat::ViewSteps(e.lines, e.marks);
         const bool any = !v.rows.empty();
         // One size ladder: the header reads at body size, like the agent's name and words; the steps under it are captions.
         Weight body(UiType::Weight::Medium);
         const float rowH = std::max(ImGui::GetTextLineHeight(), 16.0f) + tok::space_1;
         const float gutter = 18.0f;
         ImGui::PushID(id);
         if (ImGui::InvisibleButton("##steps", ImVec2(width, rowH)) && any && !running)
            ui.open = !ui.open;
         ImGui::PopID();
         const bool hot = ImGui::IsItemHovered() && any && !running;
         const ImU32 dim = hot ? ImGui::GetColorU32(ImGuiCol_Text) : Dim();
         const float cy = at.y + rowH * 0.5f;
         if (running)
         {
            ImGui::SetCursorScreenPos(ImVec2(at.x + (gutter - BusyLine::kSpinner) * 0.5f, cy - BusyLine::kSpinner * 0.5f));
            BusyLine::Spinner();
         }
         else if (any)
            glyph::Draw(dl, ImVec2(at.x + gutter * 0.5f, cy), tok::icon_sm, dim, ui.open ? IconsInfinite::ChevronDown : IconsInfinite::ChevronRight);
         std::string label;
         float tx = at.x + gutter;
         if (named)
         {
            DrawAvatar(dl, ImVec2(tx + 8.0f, cy), 8.0f, e.agent);
            tx += 22.0f;
            const char* name = AgentName(e.agent);
            dl->AddText(ImVec2(tx, at.y + (rowH - ImGui::GetFontSize()) * 0.5f), AgentTint(e.agent), name);
            tx += ImGui::CalcTextSize(name).x;
            label = "  ·  ";
         }
         char count[64];
         if (running)
         {
            const int secs = since >= 0.0 ? (int)(ImGui::GetTime() - since) : 0;
            if (secs >= 60)
               std::snprintf(count, sizeof count, "%s  %d:%02d", T("Working…"), secs / 60, secs % 60);
            else if (secs >= 3)
               std::snprintf(count, sizeof count, "%s  %ds", T("Working…"), secs);
            else
               std::snprintf(count, sizeof count, "%s", T("Working…"));
         }
         else if (v.edits > 0)
            std::snprintf(count, sizeof count, v.edits == 1 ? T("Made %d change") : T("Made %d changes"), v.edits);
         else if (v.commands > 0)
            std::snprintf(count, sizeof count, "%s", T("Looked at the patch"));
         else
            std::snprintf(count, sizeof count, v.rows.size() == 1 ? T("%d step") : T("%d steps"), (int)v.rows.size());
         label += count;
         const float ty = at.y + (rowH - ImGui::GetFontSize()) * 0.5f;
         dl->AddText(ImVec2(tx, ty), dim, label.c_str());
         if (!running && v.failed > 0)
         {
            char failed[48];
            std::snprintf(failed, sizeof failed, "  ·  %s", v.failed == 1 ? T("1 failed") : (std::to_string(v.failed) + " " + T("failed")).c_str());
            dl->AddText(ImVec2(tx + ImGui::CalcTextSize(label.c_str()).x, ty), Red(), failed);
         }
         ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + rowH));
         UiType::Scope sub(UiType::Size::Body);
         // While it runs, the latest line only; unfolded, every line.
         if ((ui.open && !running) || (running && any))
         {
            ImFont* mono = TerminalMonoFont();
            const size_t from = running ? v.rows.size() - 1 : 0;
            const float lineH = ImGui::GetFontSize() + tok::space_1;
            for (size_t i = from; i < v.rows.size(); i++)
            {
               const chat::StepRow& r = v.rows[i];
               const ImVec2 p = ImGui::GetCursorScreenPos();
               const float x0 = p.x + gutter, x1 = p.x + width;
               if (r.failed)
                  glyph::Draw(dl, ImVec2(p.x + gutter * 0.5f, p.y + lineH * 0.5f), tok::icon_sm, Red(), IconsInfinite::Close);
               const ImU32 col = r.failed ? Red() : (r.note ? TextTint(0.7f) : Dim());
               const bool words = !r.phrase.empty();
               std::string one = words ? r.phrase : r.text.substr(0, r.text.find('\n'));
               if (words && r.times > 1)
                  one += "  \xC3\x97" + std::to_string(r.times);
               const bool monoRow = mono != nullptr && !r.note && !words;
               if (monoRow)
                  ImGui::PushFont(mono, ImGui::GetFontSize());
               ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(col));
               ImGui::RenderTextEllipsis(dl, ImVec2(x0, p.y + (lineH - ImGui::GetFontSize()) * 0.5f), ImVec2(x1, p.y + lineH), x1, one.c_str(), nullptr, nullptr);
               ImGui::PopStyleColor();
               if (monoRow)
                  ImGui::PopFont();
               ImGui::Dummy(ImVec2(width, lineH));
               if (ImGui::IsItemHovered() && (words || ImGui::CalcTextSize(one.c_str()).x > width - gutter || one.size() != r.text.size()))
                  ImGui::SetTooltip("%s", r.text.c_str());
            }
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

      // A glass card: the same well and hairline the composer uses.
      void GlassCard(ImVec2 at, float width, float height)
      {
         ImDrawList* dl = ImGui::GetWindowDrawList();
         dl->AddRectFilled(at, ImVec2(at.x + width, at.y + height), TextTint(0.045f), tok::radius_tile + 4.0f);
         dl->AddRect(at, ImVec2(at.x + width, at.y + height), TextTint(0.10f), tok::radius_tile + 4.0f, 0, 1.0f);
      }

      // First run, one card: pick an agent, press Connect. Install, MCP, skills and the browser sign-in
      // follow by themselves; the card says only where it is.
      bool DrawSetup(float width)
      {
         if (AnyReady())
            return false;
         using namespace CliSetup;
         const Paths paths = PathsFor(AppPaths::AppSupportDir(), CurrentOs());
         const float pad = tok::space_3, rowH = 38.0f, titleH = 30.0f;
         int rows = 0;
         for (const Agent& a : Agents())
            rows += sAgents.count(a.id) ? 1 : 0;
         float extra = 0.0f;
         for (const Agent& a : Agents())
         {
            const auto it = sAgents.find(a.id);
            if (it != sAgents.end() && (it->second.login != nullptr && !it->second.loginUrl.empty()))
               extra += 26.0f;
            if (it != sAgents.end() && it->second.login != nullptr && it->second.needsCode)
               extra += 34.0f;
            if (it != sAgents.end() && (!it->second.loginError.empty() || it->second.failed))
               extra += 34.0f;
         }
         const float height = pad * 2.0f + titleH + rows * (rowH + tok::space_1) + extra;
         const ImVec2 at = ImGui::GetCursorScreenPos();
         GlassCard(at, width, height);
         ImDrawList* dl = ImGui::GetWindowDrawList();
         {
            Weight s(UiType::Weight::Semibold);
            dl->AddText(ImVec2(at.x + pad, at.y + pad), ImGui::GetColorU32(ImGuiCol_Text), T("Connect an agent"));
         }
         float y = at.y + pad + titleH;
         for (const Agent& a : Agents())
         {
            auto it = sAgents.find(a.id);
            if (it == sAgents.end())
               continue;
            AgentState& s = it->second;
            ImGui::PushID(a.id);
            DrawAvatar(dl, ImVec2(at.x + pad + 11.0f, y + rowH * 0.5f), 11.0f, a.id);
            const float ty = y + (rowH - ImGui::GetFontSize()) * 0.5f;
            dl->AddText(ImVec2(at.x + pad + 30.0f, ty), ImGui::GetColorU32(ImGuiCol_Text), a.name);
            const float rx = at.x + width - pad;
            auto button = [&](const char* label, bool primary) -> bool
            {
               const float w = ImGui::CalcTextSize(label).x + 2.0f * tok::space_2;
               ImGui::SetCursorScreenPos(ImVec2(rx - w, y + (rowH - 26.0f) * 0.5f));
               return ChipButton::Draw(label, primary, 26.0f);
            };
            auto status = [&](const char* label, bool spin, ImU32 col)
            {
               const float w = ImGui::CalcTextSize(label).x;
               dl->AddText(ImVec2(rx - w, ty), col, label);
               if (spin)
               {
                  ImGui::SetCursorScreenPos(ImVec2(rx - w - BusyLine::kSpinner - tok::space_1, y + (rowH - BusyLine::kSpinner) * 0.5f));
                  BusyLine::Spinner();
               }
            };
            float used = 0.0f;
            if (s.setup != nullptr)
            {
               const Status st = ParseStatus(ReadAll(paths.status), (long long)std::time(nullptr));
               char prog[48];
               std::snprintf(prog, sizeof prog, T("Setting up… %d of %d"), st.GreenCount(), kStepCount);
               status(prog, true, Dim());
            }
            else if (s.login != nullptr)
            {
               const char* cancel = T("Cancel");
               if (button(cancel, false))
                  Do(a.id, Act::Cancel);
               {
                  const char* label = T("Finish in your browser…");
                  const float bw = ImGui::CalcTextSize(cancel).x + 2.0f * tok::space_2 + tok::space_2;
                  const float w = ImGui::CalcTextSize(label).x;
                  dl->AddText(ImVec2(rx - bw - w, ty), Dim(), label);
                  ImGui::SetCursorScreenPos(ImVec2(rx - bw - w - BusyLine::kSpinner - tok::space_1, y + (rowH - BusyLine::kSpinner) * 0.5f));
                  BusyLine::Spinner();
               }
               if (!s.loginUrl.empty())
               {
                  ImGui::SetCursorScreenPos(ImVec2(at.x + pad + 30.0f, y + rowH));
                  if (ChipButton::Draw(T("Open sign-in page"), false, 22.0f, 0.0f, true))
                     Platform::OpenExternalUrl(s.loginUrl);
                  used = 26.0f;
               }
               if (s.needsCode)
               {
                  ImGui::SetCursorScreenPos(ImVec2(at.x + pad + 30.0f, y + rowH + used));
                  DrawCodeEntry(s, rx - (at.x + pad + 30.0f));
                  used += 34.0f;
               }
            }
            else
            {
               const AgentView v = ViewOf(a.id);
               if (v.act == Act::None || v.act == Act::SignOut || v.busy)
                  status(v.status.c_str(), v.busy, Dim());
               else if (button(ActLabel(v.act), true))
                  Do(a.id, v.act);
               std::string msg = s.failed ? std::string(T("Setup did not finish.")) + (s.ready.why.empty() ? std::string() : " " + s.ready.why) : s.loginError;
               if (!msg.empty())
               {
                  UiType::Scope cap(UiType::Size::Caption);
                  const float tw = rx - (at.x + pad + 30.0f);
                  const ImVec2 sz = ImGui::CalcTextSize(msg.c_str(), nullptr, false, tw);
                  dl->AddText(nullptr, 0.0f, ImVec2(at.x + pad + 30.0f, y + rowH - 4.0f), Red(), msg.c_str(), nullptr, tw);
                  used = 34.0f;
                  (void)sz;
               }
            }
            y += rowH + tok::space_1 + used;
            ImGui::PopID();
         }
         ImGui::SetCursorScreenPos(at);
         ImGui::Dummy(ImVec2(width, height));
         return true;
      }

      // After the first agent connects: where it works and whether it may edit files, then one intro line.
      bool DrawFolderStep(float width)
      {
         CliSetup::Config& prefs = CliSetup::Prefs();
         if (!AnyReady() || prefs.introduced)
            return false;
         const float pad = tok::space_3, rowH = 34.0f;
         const float height = pad * 2.0f + 28.0f + rowH * 2.0f + 34.0f;
         const ImVec2 at = ImGui::GetCursorScreenPos();
         GlassCard(at, width, height);
         ImDrawList* dl = ImGui::GetWindowDrawList();
         {
            Weight s(UiType::Weight::Semibold);
            dl->AddText(ImVec2(at.x + pad, at.y + pad), ImGui::GetColorU32(ImGuiCol_Text), T("Connected"));
         }
         float y = at.y + pad + 28.0f;
         // Folder
         std::string folder = prefs.workspace;
         if (folder.empty())
            folder = T("The open patch's folder");
         else if (const size_t sl = folder.find_last_of("/\\"); sl != std::string::npos && sl + 1 < folder.size())
            folder = folder.substr(sl + 1);
         dl->AddText(ImVec2(at.x + pad, y + (rowH - ImGui::GetFontSize()) * 0.5f), Dim(), T("Folder"));
         {
            const float w = ImGui::CalcTextSize(T("Choose…")).x + 2.0f * tok::space_2;
            ImGui::SetCursorScreenPos(ImVec2(at.x + width - pad - w, y + (rowH - 24.0f) * 0.5f));
            if (ChipButton::Draw(T("Choose…"), false, 24.0f))
            {
               const std::string dir = Platform::OpenFolderDialog("Folder for agents", prefs.workspace);
               if (!dir.empty())
               {
                  prefs.workspace = dir;
                  SaveCliConfig();
               }
            }
            const float fw = ImGui::CalcTextSize(folder.c_str()).x;
            dl->AddText(ImVec2(at.x + width - pad - w - tok::space_2 - fw, y + (rowH - ImGui::GetFontSize()) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), folder.c_str());
         }
         y += rowH;
         // Access
         dl->AddText(ImVec2(at.x + pad, y + (rowH - ImGui::GetFontSize()) * 0.5f), Dim(), T("Edit files in it"));
         ImGui::SetCursorScreenPos(ImVec2(at.x + width - pad - 36.0f, y + (rowH - 20.0f) * 0.5f));
         if (Switch::Box("##access", &prefs.folderAccess))
            SaveCliConfig();
         y += rowH + 4.0f;
         {
            const float w = ImGui::CalcTextSize(T("Start")).x + 4.0f * tok::space_2;
            ImGui::SetCursorScreenPos(ImVec2(at.x + width - pad - w, y));
            if (ChipButton::Draw(T("Start"), true, 26.0f, w))
            {
               prefs.introduced = true;
               SaveCliConfig();
               Push(Make(Entry::Kind::Note, chat::Welcome()));
               SaveNow();
            }
         }
         ImGui::SetCursorScreenPos(at);
         ImGui::Dummy(ImVec2(width, height));
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

      // ── Selecting an agent's words ──────────────────────────────────────────
      std::vector<int> PiecesOf(int entry)
      {
         std::vector<int> out;
         for (int i = 0; i < (int)sPieces.size(); i++)
            if (sPieces[(size_t)i].entry == entry)
               out.push_back(i);
         return out;
      }

      float XOf(const Piece& p, int ch)
      {
         ch = std::clamp(ch, 0, (int)p.text.size());
         return p.min.x + p.font->CalcTextSizeA(p.size, FLT_MAX, 0.0f, p.text.c_str(), p.text.c_str() + ch).x;
      }

      // The byte offset in `p` nearest the mouse's x, on a character boundary.
      int CharAt(const Piece& p, float mx)
      {
         if (mx <= p.min.x)
            return 0;
         if (mx >= p.max.x)
            return (int)p.text.size();
         const char* b = p.text.c_str();
         const char* end = b + p.text.size();
         const char* c = b;
         while (c < end)
         {
            unsigned int cp = 0;
            const int n = std::max(1, ImTextCharFromUtf8(&cp, c, end));
            const float l = XOf(p, (int)(c - b)), r = XOf(p, (int)(c - b) + n);
            if (mx < (l + r) * 0.5f)
               return (int)(c - b);
            c += n;
         }
         return (int)p.text.size();
      }

      // The nearest piece by line first, then along it.
      SelPos HitTest(const std::vector<int>& idx, ImVec2 m)
      {
         SelPos best;
         float bestKey = FLT_MAX;
         for (int k = 0; k < (int)idx.size(); k++)
         {
            const Piece& p = sPieces[(size_t)idx[(size_t)k]];
            const float dy = m.y < p.min.y ? p.min.y - m.y : (m.y > p.max.y ? m.y - p.max.y : 0.0f);
            const float dx = m.x < p.min.x ? p.min.x - m.x : (m.x > p.max.x ? m.x - p.max.x : 0.0f);
            const float key = dy * 10000.0f + dx;
            if (key < bestKey)
               bestKey = key, best.piece = k, best.ch = CharAt(p, m.x);
         }
         return best;
      }

      bool Before(SelPos a, SelPos b) { return a.piece != b.piece ? a.piece < b.piece : a.ch < b.ch; }

      std::string SelectedText(const std::vector<int>& idx, SelPos lo, SelPos hi)
      {
         std::string out;
         for (int k = lo.piece; k <= hi.piece && k < (int)idx.size(); k++)
         {
            const Piece& p = sPieces[(size_t)idx[(size_t)k]];
            if (k > lo.piece)
               out += p.breakBefore ? "\n" : (sPieces[(size_t)idx[(size_t)k - 1]].spaceAfter ? " " : "");
            const int a = k == lo.piece ? lo.ch : 0;
            const int b = k == hi.piece ? hi.ch : (int)p.text.size();
            if (b > a)
               out += p.text.substr((size_t)a, (size_t)(b - a));
         }
         return out;
      }

      // Reply: the words as a quote at the top of the composer, the cursor after them.
      void QuoteInComposer(const std::string& text)
      {
         std::string q;
         for (size_t i = 0;;)
         {
            const size_t e = text.find('\n', i);
            q += "> " + text.substr(i, e == std::string::npos ? std::string::npos : e - i) + "\n";
            if (e == std::string::npos)
               break;
            i = e + 1;
         }
         sInput = q + "\n" + sInput;
         sRefocus = sCursorToEnd = true;
      }

      void ClearSelection()
      {
         sSelEntry = -1;
         sSelDrag = false;
      }

      // After the rows: highlight, the Copy / Reply pill, and the mouse. Inside ##chatlog.
      void DrawSelection()
      {
         ImGuiWindow* win = ImGui::GetCurrentWindow();
         const ImGuiID selId = ImGui::GetID("##chatsel");
         const ImVec2 m = ImGui::GetMousePos();
         std::vector<int> idx = sSelEntry >= 0 ? PiecesOf(sSelEntry) : std::vector<int>();
         if (sSelEntry >= 0 && idx.empty())
            ClearSelection();
         // The drag follows the mouse, scrolling the log when it leaves the top or bottom.
         if (sSelDrag)
         {
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
               sSelDrag = false;
               if (ImGui::GetActiveID() == selId)
                  ImGui::ClearActiveID();
               if (sSelA.piece == sSelB.piece && sSelA.ch == sSelB.ch)
                  ClearSelection();
            }
            else
            {
               ImGui::KeepAliveID(selId);
               sSelB = HitTest(idx, m);
               const float top = win->InnerRect.Min.y, bottom = win->InnerRect.Max.y;
               if (m.y < top)
                  ImGui::SetScrollY(ImGui::GetScrollY() - std::min(24.0f, (top - m.y) * 0.5f + 2.0f));
               else if (m.y > bottom)
                  ImGui::SetScrollY(ImGui::GetScrollY() + std::min(24.0f, (m.y - bottom) * 0.5f + 2.0f));
            }
         }
         const bool has = sSelEntry >= 0 && !idx.empty() && (sSelA.piece != sSelB.piece || sSelA.ch != sSelB.ch);
         SelPos lo = sSelA, hi = sSelB;
         if (Before(hi, lo))
            std::swap(lo, hi);
         lo.piece = std::min(lo.piece, (int)idx.size() - 1);
         hi.piece = std::min(hi.piece, (int)idx.size() - 1);
         if (has)
         {
            gChatSelectionFrame = ImGui::GetFrameCount();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImU32 col = ImGui::GetColorU32(ImGuiCol_TextSelectedBg);
            for (int k = lo.piece; k <= hi.piece; k++)
            {
               const Piece& p = sPieces[(size_t)idx[(size_t)k]];
               float x0 = XOf(p, k == lo.piece ? lo.ch : 0);
               float x1 = XOf(p, k == hi.piece ? hi.ch : (int)p.text.size());
               // The space after a word is part of the run, up to the next word on the same line.
               if (k < hi.piece && p.spaceAfter)
               {
                  const Piece& n = sPieces[(size_t)idx[(size_t)k + 1]];
                  if (!n.breakBefore && n.min.y == p.min.y)
                     x1 = n.min.x;
               }
               if (x1 > x0)
                  dl->AddRectFilled(ImVec2(x0, p.min.y + 1.0f), ImVec2(x1, p.max.y - 1.0f), col);
            }
            if (!sSelDrag)
            {
               const Piece& hp = sPieces[(size_t)idx[(size_t)hi.piece]];
               const Piece& lp = sPieces[(size_t)idx[(size_t)lo.piece]];
               const float h = ChipButton::kHeight, pad = 3.0f;
               const float w = ImGui::CalcTextSize(T("Copy")).x + ImGui::CalcTextSize(T("Reply")).x + 4.0f * tok::space_2 + 3.0f * pad;
               ImVec2 at(XOf(hp, hi.ch) - w * 0.5f, hp.max.y + tok::space_1);
               if (at.y + h + 2.0f * pad > win->InnerRect.Max.y)
                  at.y = lp.min.y - h - 2.0f * pad - tok::space_1;
               at.x = std::clamp(at.x, win->InnerRect.Min.x, win->InnerRect.Max.x - w);
               const ImVec2 bg1(at.x + w, at.y + h + 2.0f * pad);
               dl->AddRectFilled(at, bg1, ImGui::GetColorU32(ImGuiCol_PopupBg), tok::radius_tile + pad);
               dl->AddRect(at, bg1, ImGui::GetColorU32(ImGuiCol_Border), tok::radius_tile + pad);
               const ImVec2 back = ImGui::GetCursorScreenPos();
               ImGui::SetCursorScreenPos(ImVec2(at.x + pad, at.y + pad));
               const bool copy = ChipButton::Draw((std::string(T("Copy")) + "##selcopy").c_str(), false, h);
               ImGui::SameLine(0.0f, pad);
               const bool reply = ChipButton::Draw((std::string(T("Reply")) + "##selreply").c_str(), false, h);
               ImGui::SetCursorScreenPos(back);
               if (copy || reply)
               {
                  const std::string text = SelectedText(idx, lo, hi);
                  if (copy)
                     ImGui::SetClipboardText(text.c_str());
                  else
                     QuoteInComposer(text);
                  ClearSelection();
                  return;
               }
            }
            const bool typing = ImGui::GetIO().WantTextInput;
            if (!typing && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C))
               ImGui::SetClipboardText(SelectedText(idx, lo, hi).c_str());
            if (!typing && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
               ClearSelection();
         }
         // A press on an agent's words starts a selection; a double click takes the whole word.
         const bool here = ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered();
         if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !sSelDrag)
         {
            int entry = -1;
            for (const auto& [id, r] : sEntryRects)
               if (r.Contains(m))
                  entry = id;
            if (here && entry >= 0)
            {
               std::vector<int> ix = PiecesOf(entry);
               if (!ix.empty())
               {
                  sSelEntry = entry;
                  sSelA = sSelB = HitTest(ix, m);
                  if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                  {
                     sSelA.ch = 0;
                     sSelB.ch = (int)sPieces[(size_t)ix[(size_t)sSelB.piece]].text.size();
                  }
                  else
                  {
                     sSelDrag = true;
                     ImGui::SetActiveID(selId, win);   // the drag is ours: no window move, no canvas pan
                     ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
                  }
               }
            }
            else if (here || !ImGui::IsWindowHovered())
               ClearSelection();
         }
         if (here)
            for (const auto& [id, r] : sEntryRects)
               if (r.Contains(m))
                  ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
      }

      void DrawMessages(float height)
      {
         ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(tok::space_3, tok::space_2));
         ImGui::BeginChild("##chatlog", ImVec2(0.0f, height), ImGuiChildFlags_AlwaysUseWindowPadding);
         ImGui::PopStyleVar();
         sPieces.clear();
         sEntryRects.clear();
         const float width = ImGui::GetContentRegionAvail().x;
         const bool setup = DrawSetup(width) || DrawFolderStep(width);
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
            double since = -1.0;
            for (const Run& r : sRuns)
               if (r.steps == i)
                  running = true, since = r.startAt;
            ImGui::PushID(i);  // chips and buttons inside each row get ids of their own
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
                  DrawSteps(e, ui, width, i, running, agents > 1, since);
                  break;
               case Entry::Kind::Note:
               case Entry::Kind::Error:
               {
                  if (e.kind == Entry::Kind::Note && e.text.find("](") != std::string::npos)
                  {
                     DrawBlocks(BlocksFor(e.text), width, i);  // guide answers carry buttons
                     break;
                  }
                  ImGui::PushStyleColor(ImGuiCol_Text, e.kind == Entry::Kind::Error ? ImGui::ColorConvertU32ToFloat4(Red()) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
                  ImGui::TextUnformatted(e.text.c_str());
                  ImGui::PopTextWrapPos();
                  ImGui::PopStyleColor();
                  if (e.kind == Entry::Kind::Error)
                  {
                     ImGui::PushID(i);
                     const auto ag = sAgents.find(e.agent);
                     if (ag != sAgents.end() && ag->second.authBroken && ag->second.login == nullptr && ag->second.logout == nullptr)
                     {
                        const Act act = ViewOf(e.agent).act;
                        if (act == Act::SignIn || act == Act::SignInAgain || act == Act::SetUp || act == Act::TryAgain)
                           if (ChipButton::Draw(ActLabel(act), true))
                              Do(e.agent, act);
                     }
                     else if (ag != sAgents.end() && ag->second.login != nullptr)
                     {
                        ImGui::TextDisabled("%s", T("Finish in your browser…"));
                        DrawCodeEntry(ag->second, std::min(width, 360.0f));
                     }
                     if (!e.detail.empty())
                     {
                        if (ChipButton::Draw(ui.open ? T("Hide details") : T("Details"), false, ChipButton::kHeight, 0.0f, true))
                           ui.open = !ui.open;
                        if (ui.open)
                        {
                           UiType::Scope cap(UiType::Size::Caption);
                           ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
                           ImGui::TextDisabled("%s", e.detail.c_str());
                           ImGui::PopTextWrapPos();
                        }
                     }
                     ImGui::PopID();
                  }
                  break;
               }
            }
            ImGui::PopID();
         }
         DrawSelection();
         // Two or more agents at work: their avatars side by side, with a short count.
         if (sRuns.size() > 1)
         {
            ImGui::Dummy(ImVec2(0.0f, tok::space_2));
            ImDrawList* wd = ImGui::GetWindowDrawList();
            const ImVec2 wat = ImGui::GetCursorScreenPos();
            const float r = 8.0f, step = r * 2.0f + tok::space_1;
            float x = wat.x + r + 1.0f;
            for (const Run& run : sRuns)
            {
               DrawAvatar(wd, ImVec2(x, wat.y + r + 1.0f), r, run.agent);
               x += step;
            }
            char buf[64];
            std::snprintf(buf, sizeof buf, T("%d working"), (int)sRuns.size());
            wd->AddText(ImVec2(x + tok::space_1, wat.y + (r * 2.0f + 2.0f - ImGui::GetFontSize()) * 0.5f), Dim(), buf);
            ImGui::Dummy(ImVec2(width, r * 2.0f + 2.0f));
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
                  dl->AddRectFilled(at, ImVec2(at.x + rowW, at.y + rowH), TextTint(EnterPicks() ? 0.10f : 0.05f), tok::radius_field);
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
         // A node's "Reference in chat": its `@` name joins the line, and the cursor waits after it.
         if (sPendingRef >= 0)
         {
            RefreshCatalog(true);
            for (const chat::CatalogNode& n : sCat.nodes)
               if (n.index == sPendingRef)
               {
                  if (!sInput.empty() && sInput.back() != ' ' && sInput.back() != '\n')
                     sInput += ' ';
                  sInput += RefText(n) + " ";
                  sRefocus = sCursorToEnd = true;
               }
            sPendingRef = -1;
         }
      }

      // ---- menus: one look for every popup in the chat (glass well, 28 px rows, a check on the chosen one)

      void BeginMenuStyle()
      {
         ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(tok::space_2, tok::space_2));
         ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, tok::radius_group);
         ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 2.0f));
         ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
      }

      void EndMenuStyle() { ImGui::PopStyleVar(4); }

      void MenuLabel(const char* text, float w)
      {
         UiType::Scope cap(UiType::Size::Caption, UiType::Weight::Semibold);
         const ImVec2 p = ImGui::GetCursorScreenPos();
         ImGui::Dummy(ImVec2(w, ImGui::GetTextLineHeight() + tok::space_2));
         ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + tok::space_2, p.y + tok::space_1), Dim(), text);
      }

      // One row: [avatar] label ........ sub  [check]. True when pressed (and enabled).
      bool MenuRow(const char* id, float w, const char* label, const char* sub, bool chosen, bool enabled, const std::string& avatar)
      {
         const float h = 28.0f;
         const ImVec2 p = ImGui::GetCursorScreenPos();
         const bool pressed = ImGui::InvisibleButton(id, ImVec2(w, h)) && enabled;
         const bool hot = ImGui::IsItemHovered() && enabled;
         ImDrawList* dl = ImGui::GetWindowDrawList();
         if (hot)
            dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), TextTint(0.08f), tok::radius_tile);
         float x = p.x + tok::space_2;
         if (!avatar.empty())
         {
            DrawAvatar(dl, ImVec2(x + 9.0f, p.y + h * 0.5f), 9.0f, avatar);
            x += 18.0f + tok::space_2;
         }
         const float ty = p.y + (h - ImGui::GetFontSize()) * 0.5f;
         const float right = p.x + w - tok::space_2 - 14.0f;
         float subW = 0.0f;
         if (sub != nullptr && sub[0] != '\0')
         {
            subW = ImGui::CalcTextSize(sub).x;
            dl->AddText(ImVec2(right - subW, ty), Dim(), sub);
         }
         const float lmax = right - (subW > 0.0f ? subW + tok::space_2 : 0.0f);
         ImGui::PushStyleColor(ImGuiCol_Text, enabled ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
         ImGui::RenderTextEllipsis(dl, ImVec2(x, ty), ImVec2(lmax, ty + ImGui::GetFontSize()), lmax, label, nullptr, nullptr);
         ImGui::PopStyleColor();
         if (chosen)
         {
            const ImVec2 c(p.x + w - tok::space_2 - 6.0f, p.y + h * 0.5f);
            const ImU32 col = ImGui::GetColorU32(AccentEmphasisSelected());
            dl->AddLine(ImVec2(c.x - 4.0f, c.y), ImVec2(c.x - 1.0f, c.y + 3.0f), col, 1.6f);
            dl->AddLine(ImVec2(c.x - 1.0f, c.y + 3.0f), ImVec2(c.x + 4.5f, c.y - 3.5f), col, 1.6f);
         }
         return pressed;
      }

      std::vector<chat::Model>& ModelsOf(const std::string& agent)
      {
         static std::map<std::string, std::shared_ptr<chat::AgentRun>> sFetch;
         chat::AgentKind kind;
         std::vector<chat::Model>& list = sModels[agent];
         if (!KindOf(agent, kind))
            return list;
         if (list.empty())
            list = chat::FixedModels(kind);
         // One fetch at a time, only for an installed agent, and an empty answer (signed out, offline) waits a
         // minute before the next try: this runs every frame the picker or Settings shows.
         const auto st = sAgents.find(agent);
         // The status check of a signed-in agy fills this list itself.
         const bool installed = st != sAgents.end() && st->second.ready.installed && st->second.auth != chat::Auth::Out && st->second.verify == nullptr;
         const double now = ImGui::GetTime();
         const auto at = sModelsFetchAt.find(agent);
         const bool due = at == sModelsFetchAt.end() || now - at->second >= 60.0;
         if (list.empty() && kind == chat::AgentKind::Antigravity && sFetch[agent] == nullptr && installed && due)
         {
            sModelsFetchAt[agent] = now;
            chat::AgentRun::Options o;
            o.kind = kind;
            o.raw = true;
            o.argv = { "agy", "models" };
            TerminalCommandEnv(o.env, o.cwd);
            sFetch[agent] = chat::AgentRun::Start(o);
         }
         // Codex's list is a file it keeps: read it whenever it is there, no process, no sign-in state needed.
         if (list.empty() && kind == chat::AgentKind::Codex && due)
         {
            sModelsFetchAt[agent] = now;
            const char* codexHome = std::getenv("CODEX_HOME");
            const char* home = std::getenv(CliSetup::CurrentOs() == CliSetup::Os::kWindows ? "USERPROFILE" : "HOME");
            const std::string dir = codexHome != nullptr && codexHome[0] != '\0' ? std::string(codexHome) : std::string(home != nullptr ? home : "") + "/.codex";
            list = chat::ParseCodexModels(ReadAll(dir + "/models_cache.json"));
         }
         if (sFetch[agent] != nullptr)
         {
            std::vector<chat::AgentEvent> drop;
            sFetch[agent]->Poll(drop);
            if (sFetch[agent]->Done())
            {
               list = chat::ParseAgyModels(chat::StripAnsi(sFetch[agent]->Log()));
               sFetch[agent] = nullptr;
            }
         }
         return list;
      }

      // A tool-row pill: [avatar] text, a tint on hover. Drawn right to left from `right`; returns its left edge.
      float Pill(const char* id, const std::string& avatar, const std::string& text, float right, float y, float h, bool& clicked)
      {
         const float av = avatar.empty() ? 0.0f : 14.0f + tok::space_2;
         const float w = av + ImGui::CalcTextSize(text.c_str()).x + 2.0f * tok::space_2;
         ImGui::SetCursorScreenPos(ImVec2(right - w, y));
         clicked = ImGui::InvisibleButton(id, ImVec2(w, h));
         const bool hot = ImGui::IsItemHovered() || ImGui::IsPopupOpen(std::string(id).append("menu").c_str());
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
         if (hot)
            dl->AddRectFilled(mn, mx, TextTint(0.06f), tok::radius_tile);
         if (!avatar.empty())
            DrawAvatar(dl, ImVec2(mn.x + tok::space_2 + 7.0f, mn.y + h * 0.5f), 7.0f, avatar);
         dl->AddText(ImVec2(mn.x + tok::space_2 + av, mn.y + (h - ImGui::GetFontSize()) * 0.5f), hot ? ImGui::GetColorU32(ImGuiCol_Text) : Dim(), text.c_str());
         if (clicked)
            ImGui::OpenPopup(std::string(id).append("menu").c_str());
         return mn.x;
      }

      // Two pills in the tool row: who answers (with where each agent stands) and, beside it, which of that
      // agent's models. Settings keeps the same default model per agent.
      void DrawPicker(float right, float y, float h)
      {
         const std::string cur = DefaultAgent();
         CliSetup::Config& prefs = CliSetup::Prefs();
         const std::vector<chat::Model>& list = cur.empty() ? std::vector<chat::Model>() : ModelsOf(cur);
         const std::string modelId = prefs.models.count(cur) ? prefs.models[cur] : std::string();
         std::string modelFace = T("Default");
         for (const chat::Model& m : list)
            if (m.id == modelId && !m.label.empty())
               modelFace = m.label;
         const float menuW = 248.0f;
         bool clicked = false;
         float left = right;
         if (!cur.empty())
         {
            left = Pill("##chatmodel", "", modelFace, right, y, h, clicked);
            ImGui::SetNextWindowPos(ImVec2(right, y - tok::space_1), ImGuiCond_Always, ImVec2(1.0f, 1.0f));
            ImGui::SetNextWindowSizeConstraints(ImVec2(menuW + 2.0f * tok::space_2, 0.0f), ImVec2(menuW + 2.0f * tok::space_2, 460.0f));
            BeginMenuStyle();
            if (ImGui::BeginPopup("##chatmodelmenu"))
            {
               MenuLabel(T("Model"), menuW);
               if (MenuRow("##m", menuW, T("Default"), "", modelId.empty(), true, std::string()))
               {
                  prefs.models.erase(cur);
                  SaveCliConfig();
                  ImGui::CloseCurrentPopup();
               }
               for (const chat::Model& m : list)
               {
                  if (m.id.empty())
                     continue;
                  ImGui::PushID(m.id.c_str());
                  if (MenuRow("##m", menuW, m.label.c_str(), "", m.id == modelId, true, std::string()))
                  {
                     prefs.models[cur] = m.id;
                     SaveCliConfig();
                     ImGui::CloseCurrentPopup();
                  }
                  ImGui::PopID();
               }
               ImGui::EndPopup();
            }
            EndMenuStyle();
            left -= tok::space_1;
         }
         const float agentRight = left;
         Pill("##chatagent", cur, cur.empty() ? std::string(T("Choose an agent")) : std::string(AgentName(cur)), agentRight, y, h, clicked);
         ImGui::SetNextWindowPos(ImVec2(agentRight, y - tok::space_1), ImGuiCond_Always, ImVec2(1.0f, 1.0f));
         ImGui::SetNextWindowSizeConstraints(ImVec2(menuW + 2.0f * tok::space_2, 0.0f), ImVec2(menuW + 2.0f * tok::space_2, 460.0f));
         BeginMenuStyle();
         if (ImGui::BeginPopup("##chatagentmenu"))
         {
            MenuLabel(T("Agent"), menuW);
            for (const CliSetup::Agent& a : CliSetup::Agents())
            {
               if (sAgents.find(a.id) == sAgents.end())
                  continue;
               ImGui::PushID(a.id);
               const AgentView v = ViewOf(a.id);
               if (v.ready)
               {
                  if (MenuRow("##a", menuW, a.name, "", a.id == cur, true, a.id))
                  {
                     prefs.agent = a.id;
                     SaveCliConfig();
                     ImGui::CloseCurrentPopup();
                  }
               }
               else
               {
                  const bool acts = v.act == Act::SetUp || v.act == Act::TryAgain || v.act == Act::SignIn || v.act == Act::SignInAgain;
                  if (MenuRow("##a", menuW, a.name, acts ? ActLabel(v.act) : v.status.c_str(), false, acts, a.id))
                     Do(a.id, v.act);
               }
               ImGui::PopID();
            }
            ImGui::EndPopup();
         }
         EndMenuStyle();
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
            sPickMoved = false;
            while (sPick < (int)sCands.size() && sCands[(size_t)sPick].info)
               sPick++;
         }
         if (sInput.empty())
            sDismissed.clear();

         const float pad = tok::space_3;
         const float gap = 6.0f;  // air between the text and the tool row
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
         const float wellH = pad + chipsH + textH + gap + barH + tok::space_2;
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
            dl->AddText(ImVec2(fieldAt.x, fieldAt.y + padY), Dim(), sRuns.empty() ? T("Ask, or type @ to name a node") : T("Add to the queue"));
         ImGui::SetCursorScreenPos(fieldAt);
         ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, padY));
         ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
         ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
         if (sRefocus)
         {
            ImGui::SetKeyboardFocusHere();
            sRefocus = false;
         }
         const bool enterPicks = EnterPicks();
         const ImGuiInputTextFlags flags = ImGuiInputTextFlags_WordWrap | ImGuiInputTextFlags_CtrlEnterForNewLine | ImGuiInputTextFlags_EnterReturnsTrue |
                                           ImGuiInputTextFlags_CallbackAlways | ImGuiInputTextFlags_CallbackCompletion;
         const bool enter = ImGui::InputTextMultiline("##chatinput", &sInput, ImVec2(fieldW, textH), flags, InputCallback);
         const bool active = ImGui::IsItemActive();
         ImGui::PopStyleColor();
         ImGui::PopStyleVar(2);
         if (enter)
         {
            if (enterPicks)
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
         const float barY = at.y + pad + chipsH + textH + gap;
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
            {
               sQueueHold = true;
               for (Run& r : sRuns)
                  if (r.run != nullptr)
                     r.run->Stop();
            }
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
         DrawPicker(sc.x - sendW * 0.5f - tok::space_1, barY + (barH - 22.0f) * 0.5f, 22.0f);
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

      // The saved chats, on the left like the sidebar in Claude: search, one row per chat, new chat on top.
      void DrawRail(float w, float h)
      {
         const ImVec2 at = ImGui::GetCursorScreenPos();
         ImDrawList* dl = ImGui::GetWindowDrawList();
         dl->AddRectFilled(at, ImVec2(at.x + w, at.y + h), TextTint(0.035f), tok::radius_field);
         dl->AddLine(ImVec2(at.x + w - 0.5f, at.y), ImVec2(at.x + w - 0.5f, at.y + h), TextTint(0.08f));
         const float pad = tok::space_2;
         const float rowW = w - 2.0f * pad - 1.0f;
         ImGui::SetCursorScreenPos(ImVec2(at.x + pad, at.y + pad));
         if (MenuRow("##railnew", rowW, T("New chat"), "", false, sRuns.empty(), std::string()))
            NewChat();
         ImGui::SetCursorScreenPos(ImVec2(at.x + pad, ImGui::GetCursorScreenPos().y + tok::space_1));
         FormParts::SearchInput("##chatfind", T("Search chats"), sHistoryFilter, sizeof sHistoryFilter, rowW);
         ImGui::SetCursorScreenPos(ImVec2(at.x + pad, ImGui::GetCursorScreenPos().y + tok::space_2));
         std::string needle = sHistoryFilter;
         std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return (char)std::tolower(c); });
         ImGui::BeginChild("##railList", ImVec2(rowW, std::max(10.0f, at.y + h - ImGui::GetCursorScreenPos().y - pad)), false, ImGuiWindowFlags_NoBackground);
         std::string removeId;
         if (sHistory.empty())
            ImGui::TextDisabled("%s", T("No saved chats"));
         for (const chat::ThreadInfo& t : sHistory)
         {
            if (!needle.empty())
            {
               std::string title = t.title;
               std::transform(title.begin(), title.end(), title.begin(), [](unsigned char c) { return (char)std::tolower(c); });
               if (title.find(needle) == std::string::npos)
                  continue;
            }
            ImGui::PushID(t.id.c_str());
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float rw = ImGui::GetContentRegionAvail().x;
            const float rh = 28.0f;
            const bool open = ImGui::InvisibleButton("##open", ImVec2(rw - 24.0f, rh));
            const bool hot = ImGui::IsMouseHoveringRect(p, ImVec2(p.x + rw, p.y + rh));
            ImDrawList* cd = ImGui::GetWindowDrawList();
            const bool current = t.id == sThread.id;
            if (hot || current)
               cd->AddRectFilled(p, ImVec2(p.x + rw, p.y + rh), TextTint(current ? 0.10f : 0.06f), tok::radius_tile);
            const float ty = p.y + (rh - ImGui::GetFontSize()) * 0.5f;
            const std::string age = Age(t.updated);
            const float aw = ImGui::CalcTextSize(age.c_str()).x;
            const float tmax = p.x + rw - (hot ? 24.0f : aw + tok::space_2) - tok::space_1;
            ImGui::RenderTextEllipsis(cd, ImVec2(p.x + tok::space_2, ty), ImVec2(tmax, p.y + rh), tmax, t.title.empty() ? "…" : t.title.c_str(), nullptr, nullptr);
            if (!hot)
               cd->AddText(ImVec2(p.x + rw - aw - tok::space_2, ty), Dim(), age.c_str());
            ImGui::SameLine(0.0f, 0.0f);
            if (ImGui::InvisibleButton("##delete", ImVec2(24.0f, rh)))
               removeId = t.id;
            if (hot)
               glyph::Draw(cd, ImVec2(p.x + rw - 12.0f, p.y + rh * 0.5f), tok::icon_sm, ImGui::IsItemHovered() ? ImGui::GetColorU32(ImGuiCol_Text) : Dim(), IconsInfinite::Close);
            if (ImGui::IsItemHovered())
               ImGui::SetTooltip("%s", T("Delete"));
            if (open && sRuns.empty())
               OpenThread(t.path);
            ImGui::PopID();
         }
         ImGui::EndChild();
         if (!removeId.empty())
         {
            chat::DeleteThread(ChatsDir(), removeId);
            sHistory.erase(std::remove_if(sHistory.begin(), sHistory.end(), [&](const chat::ThreadInfo& t) { return t.id == removeId; }), sHistory.end());
            if (removeId == sThread.id)
               NewChat();
         }
      }

      // The code pane: the chat header's row (title left, Copy and Close right), then the code, monospace,
      // numbered, scrolling both ways so a long line never wraps into the next one.
      void DrawCodePane(float width, float height)
      {
         const ImVec2 at = ImGui::GetCursorScreenPos();
         ImDrawList* dl = ImGui::GetWindowDrawList();
         dl->AddRectFilled(at, ImVec2(at.x + width, at.y + height), tok::U32(tok::terminal_bg, Light()), tok::radius_field);
         const float rowH = 26.0f;
         {
            UiType::Scope s(UiType::Size::Body, UiType::Weight::Medium);
            const float right = at.x + width - tok::space_1 - 2.0f * 22.0f - tok::space_2;
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
            ImGui::RenderTextEllipsis(dl, ImVec2(at.x + tok::space_3, at.y + (rowH - ImGui::GetFontSize()) * 0.5f), ImVec2(right, at.y + rowH), right,
                                      sCodeView.title.c_str(), nullptr, nullptr);
            ImGui::PopStyleColor();
         }
         ImGui::SetCursorScreenPos(ImVec2(at.x + width - tok::space_1 - 2.0f * 22.0f - 2.0f, at.y + (rowH - 18.0f) * 0.5f));
         const double now = ImGui::GetTime();
         if (GlyphToggle::Draw("##codecopy", IconsInfinite::Copy, IconsInfinite::Copy, false))
         {
            ImGui::SetClipboardText(sCodeView.code.c_str());
            sCodeView.copiedAt = now;
         }
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", now - sCodeView.copiedAt < 1.5 ? T("Copied") : T("Copy"));
         ImGui::SameLine(0.0f, 2.0f);
         if (GlyphToggle::Draw("##codeclose", IconsInfinite::Close, IconsInfinite::Close, false))
            sCodeView.open = false;
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", T("Close"));
         dl->AddLine(ImVec2(at.x, at.y + rowH - 0.5f), ImVec2(at.x + width, at.y + rowH - 0.5f), TextTint(0.08f));

         ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + rowH));
         ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(tok::space_2, tok::space_2));
         ImGui::BeginChild("##codebody", ImVec2(width, std::max(1.0f, height - rowH)), ImGuiChildFlags_AlwaysUseWindowPadding,
                           ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_HorizontalScrollbar);
         ImGui::PopStyleVar();
         // The steps rows' face: Geist Mono at caption size.
         ImFont* mono = TerminalMonoFont();
         ImGui::PushFont(mono, UiType::Px(UiType::Size::Caption));
         const int count = (int)sCodeView.lines.size();
         char widest[16];
         std::snprintf(widest, sizeof widest, "%d", std::max(10, count));
         const float gutter = ImGui::CalcTextSize(widest).x + tok::space_3;
         const float lineH = ImGui::GetTextLineHeight() + 3.0f;
         size_t longest = 0;
         for (const std::string& l : sCodeView.lines)
            longest = std::max(longest, l.size());
         const float textW = ImGui::CalcTextSize("M").x * (float)longest;
         const ImVec2 o = ImGui::GetCursorScreenPos();
         ImDrawList* cl = ImGui::GetWindowDrawList();
         ImGuiListClipper clip;
         clip.Begin(count, lineH);
         while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++)
            {
               const float y = o.y + lineH * (float)i;
               char num[16];
               std::snprintf(num, sizeof num, "%d", i + 1);
               const float nw = ImGui::CalcTextSize(num).x;
               cl->AddText(ImVec2(o.x + gutter - tok::space_3 - nw, y), Dim(), num);
               const std::string& l = sCodeView.lines[(size_t)i];
               cl->AddText(ImVec2(o.x + gutter, y), ImGui::GetColorU32(ImGuiCol_Text), l.c_str(), l.c_str() + l.size());
               ImGui::SetCursorScreenPos(ImVec2(o.x, y));
               ImGui::Dummy(ImVec2(gutter + textW, lineH));
            }
         clip.End();
         ImGui::PopFont();
         ImGui::EndChild();
      }

      void DrawHeader(float width)
      {
         const ImVec2 at = ImGui::GetCursorScreenPos();
         const float rowH = 26.0f;
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const char* title = sThread.title.empty() ? T("New chat") : sThread.title.c_str();
         // Past chats on the left before the title; Terminal and New chat on the right, each set the same
         // inset from its edge so the row stays symmetric.
         const float glyph = 22.0f, gap = 2.0f, glyphY = at.y + (rowH - 18.0f) * 0.5f;  // GlyphToggle is 22 x 18: centred on the title's line
         ImGui::SetCursorScreenPos(ImVec2(at.x + tok::space_1, glyphY));
         if (GlyphToggle::Draw("##chathistory", IconsInfinite::History, IconsInfinite::History, sRail))
         {
            sRail = !sRail;
            if (sRail)
               sHistory = chat::ListThreads(ChatsDir()), sHistoryLoaded = true;
         }
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", T("Chats"));
         const float titleX = ImGui::GetItemRectMax().x + tok::space_2;
         const float toolsX = at.x + width - tok::space_1 - 2.0f * glyph - gap;
         ImGui::SetCursorScreenPos(ImVec2(toolsX, glyphY));
         if (GlyphToggle::Draw("##chatterminal", IconsInfinite::Terminal, IconsInfinite::Terminal, gChatSideTerminal))
            gChatSideTerminal = !gChatSideTerminal;  // opens beside the chat, like the side terminal in Claude
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", T("Terminal"));
         ImGui::SameLine(0.0f, gap);
         if (GlyphToggle::Draw("##chatnew", IconsInfinite::Plus, IconsInfinite::Plus, false) && sRuns.empty())
            NewChat();
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", T("New chat"));
         const float right = toolsX - tok::space_2;
         {
            UiType::Scope s(UiType::Size::Body, UiType::Weight::Medium);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
            ImGui::RenderTextEllipsis(dl, ImVec2(titleX, at.y + (rowH - ImGui::GetFontSize()) * 0.5f), ImVec2(right, at.y + rowH), right,
                                      title, nullptr, nullptr);
            ImGui::PopStyleColor();
         }

         ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + rowH));
         dl->AddLine(ImVec2(at.x, at.y + rowH - 0.5f), ImVec2(at.x + width, at.y + rowH - 0.5f), TextTint(0.08f));
      }
   }

   void DrawChatBody();
   void DrawChatWithCode();

   static bool sShotOpenCode = false;
   void ChatShotOpenCode() { sShotOpenCode = true; }
   void ChatReferenceNode(int index)
   {
      sPendingRef = index;
      gTerminalOpen = true;
      gTerminalFace = 0;
      gChatSideTerminal = false;
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
      if (sShotOpenCode)
      {
         sShotOpenCode = false;
         for (const chat::Entry& e : sThread.entries)
            for (const chat::ChangeLine& l : e.changes)
               if (!l.code.empty() && !sCodeView.open)
                  OpenCode(CodeTitle(l), l.code);
      }
      if (sRail && ImGui::GetContentRegionAvail().x > 380.0f)
      {
         const ImVec2 all = ImGui::GetContentRegionAvail();
         const float railW = 220.0f;
         const ImVec2 origin = ImGui::GetCursorScreenPos();
         ImGui::GetWindowDrawList()->AddRectFilled(origin, ImVec2(origin.x + all.x, origin.y + all.y), tok::U32(tok::terminal_bg, Light()), tok::radius_field);
         ImGui::BeginChild("##chatrail", ImVec2(railW, all.y), false, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
         DrawRail(railW, all.y);
         ImGui::EndChild();
         ImGui::SameLine(0.0f, 0.0f);
         ImGui::BeginChild("##chatmain", ImVec2(0.0f, all.y), false, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
         DrawChatWithCode();
         ImGui::EndChild();
         return;
      }
      DrawChatWithCode();
   }

   // The code pane sits right of the chat, as the side terminal does; on a narrow panel it covers the chat.
   void DrawChatWithCode()
   {
      if (!sCodeView.open)
      {
         DrawChatBody();
         return;
      }
      const ImVec2 avail = ImGui::GetContentRegionAvail();
      if (avail.x < 640.0f)
      {
         DrawCodePane(avail.x, avail.y);
         return;
      }
      const float gap = tok::space_1;
      const float codeW = std::floor(std::clamp(avail.x * 0.45f, 300.0f, 640.0f));
      const ImGuiWindowFlags flags = ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
      ImGui::BeginChild("##chatleft", ImVec2(avail.x - codeW - gap, avail.y), false, flags);
      DrawChatBody();
      ImGui::EndChild();
      ImGui::SameLine(0.0f, gap);
      ImGui::BeginChild("##coderight", ImVec2(codeW, avail.y), false, flags);
      DrawCodePane(codeW, avail.y);
      ImGui::EndChild();
   }

   void DrawChatBody()
   {
      const ImVec2 mn = ImGui::GetCursorScreenPos();
      const ImVec2 avail = ImGui::GetContentRegionAvail();
      ImGui::GetWindowDrawList()->AddRectFilled(mn, ImVec2(mn.x + avail.x, mn.y + avail.y), tok::U32(tok::terminal_bg, Light()), tok::radius_field);
      const float width = avail.x;
      DrawHeader(width);
      ImGui::Dummy(ImVec2(0.0f, tok::space_2));  // air under the header, as in Claude
      const float composerW = std::min(width - 2.0f * tok::space_3, 760.0f);
      const float fs = ImGui::GetFontSize();
      // The composer's height is known before it draws: the log takes the rest.
      const float wrapW = std::max(1.0f, composerW - 2.0f * tok::space_3 - ImGui::GetStyle().ScrollbarSize);
      const float textH = fs * (float)std::clamp(VisualLines(wrapW), 1, 6) + 4.0f;
      const float composerH = tok::space_3 + (sAttach.empty() ? 0.0f : 26.0f) + textH + 6.0f + 26.0f + tok::space_2;
      const float queueH = (float)sQueue.size() * kQueueRowH;
      DrawMessages(std::max(40.0f, ImGui::GetContentRegionAvail().y - composerH - queueH - tok::space_4));
      const ImVec2 c = ImGui::GetCursorScreenPos();
      ImGui::SetCursorScreenPos(ImVec2(mn.x + (width - composerW) * 0.5f, c.y + tok::space_1));
      if (!sQueue.empty())
      {
         DrawQueue(composerW);
         ImGui::SetCursorScreenPos(ImVec2(mn.x + (width - composerW) * 0.5f, ImGui::GetCursorScreenPos().y));
      }
      DrawComposer(composerW);
   }

   bool ChatBusy() { return !sRuns.empty(); }

   int ChatSavedCount()
   {
      if (!sHistoryLoaded)
         sHistory = chat::ListThreads(ChatsDir()), sHistoryLoaded = true;
      return (int)sHistory.size();
   }

   // Settings shows and drives the same agents the chat does, so the two never disagree.
   void ChatAgentsTick()
   {
      const int frame = ImGui::GetFrameCount();
      CheckAgents(frame - sLastFrame > 1);
      sLastFrame = frame;
      PollSetups();
   }

   bool ChatAgentCodeEntry(const std::string& id, float width)
{
   const auto it = sAgents.find(id);
   if (it == sAgents.end() || it->second.login == nullptr || !it->second.needsCode)
      return false;
   DrawCodeEntry(it->second, width);
   return true;
}

ChatAgentInfo ChatAgentStatus(const std::string& id)
   {
      const AgentView v = ViewOf(id);
      ChatAgentInfo info;
      info.status = v.status;
      info.action = ActLabel(v.act);
      info.ready = v.ready;
      info.busy = v.busy;
      return info;
   }

   std::vector<std::pair<std::string, std::string>> ChatAgentModels(const std::string& id)
   {
      std::vector<std::pair<std::string, std::string>> out;
      for (const chat::Model& m : ModelsOf(id))
         if (!m.id.empty())
            out.push_back({ m.id, m.label });
      return out;
   }

   void ChatAgentAct(const std::string& id, bool repair)
   {
      const CliSetup::Agent* a = CliSetup::FindAgent(id);
      if (a == nullptr)
         return;
      if (repair)
         SetUpInBackground(*a);
      else
         Do(id, ViewOf(id).act);
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
