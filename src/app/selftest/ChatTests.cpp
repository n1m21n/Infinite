// INFINITE_CHATTEST: the chat's patch grammar and reply links against a hand-made catalog
// (docs/plans/agent-collab/README.md C3-C7): grammar, reply links, agent streams, diffs, saved chats, files. No window, no agent. Early exit before glfwInit().
#include "app/AppShared.h"
#include "core/chat/AgentAuth.h"
#include "core/chat/AgentRun.h"
#include "core/chat/AgentStream.h"
#include "core/chat/ChatDiff.h"
#include "core/chat/ChatFiles.h"
#include "core/chat/ChatThread.h"
#include "core/chat/PatchGrammar.h"
#include "core/chat/ReplyLinks.h"
#include "core/chat/ChatGuide.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <thread>

namespace app
{
   bool HandleAppRpc(const std::string& method, const nlohmann::json& params, nlohmann::json& outResult, std::string& outError);

   namespace
   {
      chat::Catalog MakeCatalog()
      {
         chat::Catalog c;
         chat::CatalogNode cube;
         cube.index = 1;
         cube.type = "Cube";
         cube.title = "cube";
         cube.params = { { "detail", "Detail", "3", "int", 1, 8, {}, true },
                         { "size", "Size", "1", "float", 0, 10, {}, true },
                         { "type", "Texture", "0", "enum", 0, 3, { "Brick", "Voronoi", "Magic", "Wave" }, true } };
         chat::CatalogNode lfo;
         lfo.index = 4;
         lfo.type = "LFO";
         lfo.title = "LFO";
         lfo.modulator = true;
         lfo.params = { { "rate", "Rate", "1", "float", 0.01f, 20, {}, true } };
         chat::CatalogNode osc;
         osc.index = 2;
         osc.type = "Oscillator";
         osc.title = "osc";
         osc.params = { { "frequency", "Frequency", "440", "float", 20, 20000, {}, true } };
         chat::CatalogNode rnd;
         rnd.index = 5;
         rnd.type = "Random";
         rnd.title = "Random";
         rnd.modulator = true;
         chat::CatalogNode pixel;
         pixel.index = 7;
         pixel.type = "Field Pixel";
         pixel.title = "Field Pixel";
         pixel.params = { { "speed", "Speed", "1", "float", 0, 4, {}, true } };
         chat::CatalogNode cube2 = cube;
         cube2.index = 9;
         cube2.title = "twin";
         chat::CatalogNode twin = cube;
         twin.index = 10;
         twin.title = "twin";
         c.nodes = { cube, lfo, osc, rnd, pixel, cube2, twin };
         c.types = { { "Blur", "Effects" }, { "Field Pixel", "Field" }, { "LFO", "Modulators" } };
         return c;
      }
   }

   bool RunChatTest()
   {
      setvbuf(stdout, nullptr, _IONBF, 0);
      bool ok = true;
      auto check = [&](bool c, const char* what)
      {
         std::printf("CHATTEST %s  %s\n", what, c ? "OK" : "FAIL");
         ok = ok && c;
      };
      const chat::Catalog cat = MakeCatalog();
      using chat::ParseLine;
      using chat::Slot;

      // The owner's example: two modulations, one batch.
      {
         const chat::Parse p = ParseLine("@cube modulate detail with @lfo from 2 to 6 and @osc modulate frequency with @random", cat);
         check(p.direct && p.calls.size() == 2, "owner example: direct, two calls");
         check(p.calls[0]["method"] == "bind_mod" && p.calls[0]["params"]["src"] == 4 && p.calls[0]["params"]["dst"] == 1 &&
                  p.calls[0]["params"]["key"] == "detail" && p.calls[0]["params"]["lo"] == 2.0 && p.calls[0]["params"]["hi"] == 6.0,
               "first clause binds LFO to cube.detail 2-6");
         check(p.calls[1]["params"]["src"] == 5 && p.calls[1]["params"]["dst"] == 2 && !p.calls[1]["params"].contains("lo"), "second clause: no range");
         check(p.cli.size() == 2 && p.cli[0] == "infinite mod LFO cube detail 2 6" && p.cli[1] == "infinite mod Random osc frequency",
               "the CLI lines it equals");
      }
      {
         const chat::Parse p = ParseLine("@cube set size to 2.5", cat);
         check(p.direct && p.calls[0]["method"] == "set_param" && p.calls[0]["params"]["value"] == "2.5", "set to");
         check(ParseLine("@cube size 2.5", cat).direct, "set without `to`");
         check(ParseLine("@cube Texture to voronoi", cat).calls[0]["params"]["value"] == "1", "label + enum option by name");
         check(ParseLine("@osc frequency to 440 Hz", cat).direct, "unit after the number");
      }
      {
         const chat::Parse p = ParseLine("@cube size = sin(t)*2", cat);
         check(p.direct && p.calls[0]["method"] == "set_expr" && p.calls[0]["params"]["text"] == "=sin(t)*2", "formula after =");
         check(ParseLine("@cube size = 3", cat).calls[0]["method"] == "set_param", "= number is a plain set");
      }
      {
         const chat::Parse p = ParseLine("@lfo to @Field Pixel", cat);
         check(p.direct && p.calls[0]["method"] == "connect" && p.calls[0]["params"]["dstIndex"] == 7, "connect, title with a space");
         check(p.cli[0] == "infinite connect LFO 7", "a title with a space goes to the CLI as its index");
         check(ParseLine("connect @lfo:1 to @cube", cat).calls[0]["params"]["srcOutput"] == 1, "connect with a pin");
      }
      {
         const chat::Parse p = ParseLine("add blur after @cube", cat);
         check(p.direct && p.calls.size() == 2 && p.calls[0]["method"] == "create_node" && p.calls[0]["params"]["category"] == "Effects" &&
                  p.calls[1]["params"]["dstIndex"] == "$0.index",
               "add after: create then wire to the new node");
         check(ParseLine("add field pixel", cat).calls[0]["params"]["typeName"] == "Field Pixel", "type of two words");
      }
      {
         check(ParseLine("remove @cube and bypass @osc", cat).calls.size() == 2, "remove and bypass");
         check(ParseLine("undo", cat).direct, "undo alone");
         check(!ParseLine("undo and remove @cube", cat).direct, "undo cannot share a batch");
      }
      {
         const chat::Parse p = ParseLine("@cube detail to 12", cat);
         check(!p.direct && p.error.find("1–8") != std::string::npos, "out of range: not direct, says the range");
         const chat::Parse q = ParseLine("@cube modulate detial with @lfo", cat);
         check(!q.direct && !q.near.empty() && q.near[0] == "detail", "typo in a key: nearest key offered");
         const chat::Parse r = ParseLine("@twin size to 2", cat);
         check(!r.direct && r.near.size() == 2, "two nodes share a title: both offered");
         check(ParseLine("@twin#10 size to 2", cat).calls[0]["params"]["index"] == 10, "title#index picks one");
         check(!ParseLine("make the cube breathe", cat).direct, "plain words go to the agent");
         const chat::Parse s = ParseLine("@cube modulate size or detail with @lfo", cat);
         check(s.toAgent && !s.direct, "`or` goes to the agent");
         const chat::Parse a = ParseLine("@agent make @cube pulse", cat);
         check(a.toAgent && !a.direct && a.spans.size() == 2 && a.spans[1].kind == chat::SpanKind::Node, "@agent: agent route, node still a chip");
      }
      // Slots for the dropdowns.
      {
         check(ParseLine("", cat).slot == Slot::Start, "empty: start");
         check(ParseLine("@cu", cat).slot == Slot::Node && ParseLine("@cu", cat).partial == "@cu", "typing a node");
         const chat::Parse p = ParseLine("@cube ", cat);
         check(p.slot == Slot::AfterNode && p.slotNode == 1, "after a node: its params and verbs");
         const chat::Parse q = ParseLine("@cube modulate det", cat);
         check(q.slot == Slot::Param && q.slotNode == 1 && q.partial == "det", "typing a param");
         check(ParseLine("@cube modulate detail with ", cat).slot == Slot::Source, "after with: sources");
         check(ParseLine("@cube modulate detail with @l", cat).slot == Slot::Source, "typing a source stays a source");
         const chat::Parse r = ParseLine("@cube modulate detail with @lfo from ", cat);
         check(r.slot == Slot::Value && r.slotNode == 1 && r.slotKey == "detail", "after from: the value of cube.detail");
         const chat::Parse s = ParseLine("@cube modulate detail ", cat);
         check(s.slot == Slot::Joiner && s.words.size() == 1 && s.words[0] == "with", "after a key in modulate: with");
         check(ParseLine("add ", cat).slot == Slot::Type, "after add: types");
      }

      // Reply links.
      {
         using chat::Block;
         using chat::Seg;
         const auto blocks = chat::ParseReply("Slowed @cube#1.size 1 → 0.5 and added @LFO#4 on @Field Pixel#7.speed.\n\n- @gone#99 was removed\n```\ninfinite set 1 size 0.5\n```\n/tmp/look.png\n", cat);
         check(blocks.size() == 4, "paragraph, bullet, code, image");
         if (blocks.size() == 4)
         {
            const auto& s = blocks[0].segs;
            int nodes = 0, params = 0;
            for (const Seg& g : s)
            {
               nodes += g.kind == Seg::Kind::Node;
               params += g.kind == Seg::Kind::Param;
            }
            check(params == 2 && nodes == 1, "two param links and one node link");
            check(s[1].kind == Seg::Kind::Param && s[1].text == "cube.Size" && s[1].key == "size", "param link shows the label");
            check(blocks[1].kind == Block::Kind::Bullet && blocks[1].segs[0].kind == Seg::Kind::Missing, "a node that is gone: struck");
            check(blocks[2].kind == Block::Kind::Code && blocks[2].text == "infinite set 1 size 0.5", "code block");
            check(blocks[3].kind == Block::Kind::Image && blocks[3].text == "/tmp/look.png", "picture path");
         }
         const auto bare = chat::ParseReply("I raised the detail on @cube#1, and the size too.", cat);
         int params = 0;
         for (const Seg& g : bare[0].segs)
            params += g.kind == Seg::Kind::Param;
         check(params == 2, "bare keys link when the node is named in the paragraph");
         {
            const auto acts = chat::ParseReply("[Add Bloom](add:Bloom) [Smaller](set:@cube#1.size=0.5) [Remove](delete:@cube#1) [Show](go:@cube#1) "
                                               "[Timeline](open:timeline) [Undo](undo:) [x](rm -rf:/) [Gone](delete:@gone#99) [see](https://a.b)", cat);
            std::vector<Seg> a;
            if (acts.size() == 1)
               for (const Seg& g : acts[0].segs)
                  if (g.kind == Seg::Kind::Action)
                     a.push_back(g);
            check(a.size() == 6 && a[0].verb == "add" && a[0].arg == "Bloom" && a[1].verb == "set" && a[1].node == 1 && a[1].key == "size" &&
                     a[1].arg == "0.5" && a[2].node == 1 && a[3].verb == "go" && a[4].arg == "timeline" && a[5].verb == "undo" && a[0].text == "Add Bloom",
                  "actions: add, set, delete, go, open and undo buttons; unknown verbs, missing nodes and web links stay text");
         }
         const auto prose = chat::ParseReply("More detail would help.", cat);
         check(prose[0].segs.size() == 1 && prose[0].segs[0].kind == Seg::Kind::Text, "bare words alone stay plain");
         const auto tick = chat::ParseReply("Look at `osc` and `out.png` and **this**", cat);
         check(tick.size() == 2 && tick[0].segs[1].kind == Seg::Kind::Node && tick[1].kind == Block::Kind::Image, "backticked title links, backticked picture shows");
         check(chat::ParseReply("mail me at a@cube.com", cat)[0].segs.size() == 1, "an @ inside a word is not a link");
      }

      // The agent's stream: Claude Code stream-json and Codex exec --json lines.
      {
         chat::AgentStream s(chat::AgentKind::Claude);
         std::vector<chat::AgentEvent> ev;
         s.Feed("{\"type\":\"system\",\"subtype\":\"init\",\"session_id\":\"abc\"}\r\n", ev);
         s.Feed("{\"type\":\"assistant\",\"message\":{\"content\":[{\"type\":\"text\",\"text\":\"Hi @cube#1\"},{\"type\":\"tool_use\",\"id\":\"t1\",\"name\":\"Bash\",\"input\":{\"command\":\"infinite context\"}}]}}\n{\"type\":\"user\",\"message\":{\"content\":[{\"type\":\"tool_result\",\"tool_use_id\":\"t1\",\"content\":\"ok\"}]}}\n", ev);
         s.Feed("{\"type\":\"result\",\"subtype\":\"success\",\"is_error\":false,\"result\":\"done\",\"session_id\":\"abc\",\"permission_denials\":[{\"tool_name\":\"Write\",\"tool_input\":{\"file_path\":\"/x\"}}]}", ev);
         s.Feed("\n", ev);
         check(ev.size() == 5, "claude: init, text, tool, tool result, end");
         if (ev.size() == 5)
         {
            check(ev[0].kind == chat::AgentEvent::Kind::Session && ev[0].text == "abc", "claude: session id");
            check(ev[1].kind == chat::AgentEvent::Kind::Text && ev[1].text == "Hi @cube#1", "claude: text");
            check(ev[2].kind == chat::AgentEvent::Kind::Tool && ev[2].text == "infinite context", "claude: Bash shows its command");
            check(ev[4].kind == chat::AgentEvent::Kind::End && !ev[4].error && ev[4].denied.size() == 1 && ev[4].denied[0] == "Write /x", "claude: end with a denied tool");
         }
         chat::AgentStream c(chat::AgentKind::Codex);
         std::vector<chat::AgentEvent> cv;
         c.Feed("{\"type\":\"thread.started\",\"thread_id\":\"t-9\"}\n{\"type\":\"item.started\",\"item\":{\"id\":\"i1\",\"type\":\"command_execution\",\"command\":\"infinite context\"}}\n"
                "{\"type\":\"item.completed\",\"item\":{\"id\":\"i2\",\"type\":\"agent_message\",\"text\":\"Done\"}}\n{\"type\":\"turn.completed\"}\nnot json\n", cv);
         check(cv.size() == 4 && cv[0].text == "t-9" && cv[1].kind == chat::AgentEvent::Kind::Tool && cv[2].text == "Done" &&
                  cv[3].kind == chat::AgentEvent::Kind::End,
               "codex: thread, command, message, end; other lines skipped");
         const std::vector<std::string> first = chat::AgentArgs(chat::AgentKind::Claude, "hello", "abc", false);
         const std::vector<std::string> next = chat::AgentArgs(chat::AgentKind::Claude, "hello", "abc", true);
         check(first[0] == "claude" && std::find(first.begin(), first.end(), "--session-id") != first.end() &&
                  std::find(next.begin(), next.end(), "--resume") != next.end() && next.back() == "hello",
               "claude: first turn names the session, later turns resume it");
         check(chat::TerminalResumeLine(chat::AgentKind::Claude, "abc") == "claude --resume abc", "terminal face resumes the same session");

         // A turn through the login shell: a stand-in agent prints stream-json, the runner hands it over.
         auto runTurn = [](const std::vector<std::string>& argv, std::vector<chat::AgentEvent>& out)
         {
            chat::AgentRun::Options o;
            o.argv = argv;
            std::shared_ptr<chat::AgentRun> run = chat::AgentRun::Start(o);
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            while (!run->Done() && std::chrono::steady_clock::now() < until)
            {
               run->Poll(out);
               std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            run->Poll(out);
            return run->Done();
         };
         // A recorded real `claude -p` turn, when one is given: INFINITE_CHATTEST_CLAUDE_LOG=<stream-json file>.
         if (const char* log = std::getenv("INFINITE_CHATTEST_CLAUDE_LOG"))
         {
            std::ifstream f(log, std::ios::binary);
            const std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            chat::AgentStream real(chat::AgentKind::Claude);
            std::vector<chat::AgentEvent> lv;
            real.Feed(bytes, lv);
            int texts = 0;
            for (const chat::AgentEvent& e : lv)
               texts += e.kind == chat::AgentEvent::Kind::Text;
            check(!bytes.empty() && lv.size() >= 3 && lv.front().kind == chat::AgentEvent::Kind::Session && texts >= 1 &&
                     lv.back().kind == chat::AgentEvent::Kind::End && !lv.back().error,
                  "claude: a recorded real turn reads as session, text, end");
         }
         // Antigravity: the reply step's text arrives in deltas and goes out whole when the step is done.
         {
            chat::AgentStream g(chat::AgentKind::Antigravity);
            std::vector<chat::AgentEvent> gv;
            g.Feed("{\"event\":\"init\",\"conversation_id\":\"c-1\",\"init\":{}}\n"
                   "{\"event\":\"step_update\",\"step_update\":{\"step_index\":0,\"state\":\"DONE\",\"step_type\":\"user_input\"}}\n"
                   "{\"event\":\"step_update\",\"step_update\":{\"step_index\":1,\"state\":\"ACTIVE\",\"step_type\":\"tool\",\"tool_name\":\"run_command\",\"tool_info\":{\"name\":\"run_command\",\"parameters\":{\"CommandLine\":\"infinite context\"}}}}\n"
                   "{\"event\":\"step_update\",\"step_update\":{\"step_index\":1,\"state\":\"DONE\",\"step_type\":\"tool\",\"tool_name\":\"run_command\",\"tool_info\":{\"name\":\"run_command\",\"parameters\":{\"CommandLine\":\"infinite context\"},\"output\":\"Command exited with code 0\"}}}\n"
                   "{\"event\":\"step_update\",\"step_update\":{\"step_index\":2,\"state\":\"ACTIVE\",\"step_type\":\"agent_response\",\"text_delta\":\"mari\"}}\n"
                   "{\"event\":\"step_update\",\"step_update\":{\"step_index\":2,\"state\":\"DONE\",\"step_type\":\"agent_response\",\"text_delta\":\"gold\"}}\n"
                   "{\"event\":\"result\",\"result\":{\"conversation_id\":\"c-1\",\"status\":\"SUCCESS\"}}\n",
                   gv);
            int tools = 0, texts = 0;
            std::string text;
            for (const chat::AgentEvent& e : gv)
            {
               tools += e.kind == chat::AgentEvent::Kind::Tool;
               if (e.kind == chat::AgentEvent::Kind::Text)
                  texts++, text = e.text;
            }
            check(!gv.empty() && gv.front().kind == chat::AgentEvent::Kind::Session && gv.front().text == "c-1" && tools == 1 && texts == 1 &&
                     text.find("marigold") != std::string::npos && gv.back().kind == chat::AgentEvent::Kind::End && !gv.back().error,
                  "antigravity: session, one command, the reply whole, end");
            const std::vector<std::string> ga = chat::AgentArgs(chat::AgentKind::Antigravity, "hi", "c-1", true, { "/tmp/a" });
            check(ga[0] == "agy" && ga.back() == "hi" && ga[ga.size() - 2] == "-p" && std::find(ga.begin(), ga.end(), "--add-dir") != ga.end() &&
                     std::find(ga.begin(), ga.end(), "--conversation") != ga.end(),
                  "antigravity: prompt last, file folder readable, resumes the conversation");
            const std::vector<std::string> ca = chat::AgentArgs(chat::AgentKind::Claude, "hi", "abc", false, { "/tmp/a", "/tmp/b" });
            check(std::find(ca.begin(), ca.end(), "--add-dir") != ca.end() && std::find(ca.begin(), ca.end(), "/tmp/b") != ca.end() && ca.back() == "hi",
                  "claude: attached files' folders are readable");
            if (const char* log = std::getenv("INFINITE_CHATTEST_AGY_LOG"))
            {
               std::ifstream f(log, std::ios::binary);
               const std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
               chat::AgentStream real(chat::AgentKind::Antigravity);
               std::vector<chat::AgentEvent> lv;
               real.Feed(bytes, lv);
               check(!lv.empty() && lv.front().kind == chat::AgentEvent::Kind::Session && lv.back().kind == chat::AgentEvent::Kind::End && !lv.back().error,
                     "antigravity: a recorded real turn reads as session ... end");
            }
         }

         // Mentions route the message: each named agent, never direct.
         {
            const chat::Parse p = ParseLine("@claude @codex make it glow", cat);
            int agents = 0;
            bool claude = false, codex = false;
            for (const chat::Span& sp : p.spans)
               if (sp.kind == chat::SpanKind::Agent)
                  agents++, claude = claude || sp.key == "claude", codex = codex || sp.key == "codex";
            check(!p.direct && agents == 2 && claude && codex, "mentions: @claude @codex are two agents, never direct");
            const chat::Parse a = ParseLine("@antigravity explain @cube", cat);
            bool node = false, ag = false;
            for (const chat::Span& sp : a.spans)
               node = node || (sp.kind == chat::SpanKind::Node && sp.node == 1), ag = ag || (sp.kind == chat::SpanKind::Agent && sp.key == "antigravity");
            check(!a.direct && node && ag, "mentions: an agent and a node in one line");
         }

         // What a turn changed: params, added and removed nodes, cables, code as a block.
         {
            chat::Snapshot before, after;
            before.catalog = cat;
            after.catalog = cat;
            after.catalog.nodes[0].params[1].value = "2.5";  // cube size
            after.catalog.nodes.erase(after.catalog.nodes.begin() + 3);  // Random gone
            chat::CatalogNode blur;
            blur.index = 12;
            blur.type = "Blur";
            blur.title = "Blur";
            after.catalog.nodes.push_back(blur);
            after.catalog.nodes[3].params[0].value = "a = 1;\nb = 2;\nout = a + b;";  // Field Pixel speed stands in for a code param
            after.catalog.nodes[3].params[0].kind = "";  // text params carry no kind
            after.links.push_back({ 12, 0, 1, 0, "" });
            const std::vector<chat::ChangeLine> d = chat::Diff(before, after);
            auto has = [&](const char* what)
            {
               for (const chat::ChangeLine& l : d)
                  if (l.text.find(what) != std::string::npos)
                     return true;
               return false;
            };
            bool code = false;
            for (const chat::ChangeLine& l : d)
               code = code || l.code.find("out = a + b;") != std::string::npos;
            check(has("@cube#1.size") && has("2.5"), "diff: a changed param names node, key and value");
            check(has("Blur") && has("Random"), "diff: an added and a removed node");
            check(has("@Blur#12") && has("@cube#1") && d.size() >= 5, "diff: a new cable names both ends");
            check(code, "diff: a code param comes back as a block");
            check(chat::Diff(before, before).empty(), "diff: nothing changed, nothing to show");
         }

         // A saved chat comes back with its entries and the agents' sessions.
         {
            chat::Thread t;
            t.id = "t-test";
            t.title = chat::TitleFor("make the colours drift slowly and then make the whole thing glow with a long soft bloom on the highlights");
            t.updated = 1234;
            t.sessions["claude"] = { "s-1", true };
            t.sessions["antigravity"] = { "c-1", false };
            chat::Entry u;
            u.kind = chat::Entry::Kind::User;
            u.text = "hi";
            u.files = { "/tmp/a.png" };
            chat::Entry c;
            c.kind = chat::Entry::Kind::Change;
            c.changes = { { "@cube#1.size 1 → 2", "" }, { "@Field Pixel#7.code", "x = 1;" } };
            c.undone = true;
            chat::Entry st;
            st.kind = chat::Entry::Kind::Steps;
            st.agent = "claude";
            st.lines = { "infinite context" };
            st.ok = false;
            t.entries = { u, c, st };
            chat::Thread back;
            const bool read = chat::FromJson(chat::ToJson(t), back);
            check(read && back.id == "t-test" && back.updated == 1234 && back.sessions.size() == 2 && back.sessions["claude"].live &&
                     !back.sessions["antigravity"].live && back.entries.size() == 3 && back.entries[0].files.size() == 1 &&
                     back.entries[1].changes[1].code == "x = 1;" && back.entries[1].undone && back.entries[2].agent == "claude" && !back.entries[2].ok,
                  "thread: a saved chat reads back whole");
            check(t.title.size() <= 64 && t.title.find("…") != std::string::npos, "thread: a long first message makes a short title");
            check(!chat::FromJson("{\"version\":2}", back) && !chat::FromJson("not json", back), "thread: other files are not chats");
            chat::Entry a1, a2, a3;
            a1.kind = a2.kind = a3.kind = chat::Entry::Kind::Agent;
            a1.agent = "codex", a2.agent = "claude", a3.agent = "codex";
            t.entries.insert(t.entries.end(), { a1, a2, a3 });
            const chat::ThreadInfo info = chat::InfoOf(t, "/x/t-test.json");
            check(info.agents == std::vector<std::string>{ "codex", "claude" } && info.id == "t-test" && info.path == "/x/t-test.json",
                  "thread: history lists who answered, first reply first");
         }

         // Attachments: every kind Infinite opens, its folder readable.
         {
            check(chat::FileKind("/a/b.WAV") == "audio" && chat::FileKind("x.png") == "image" && chat::FileKind("m.glb") == "model" &&
                     chat::FileKind("s.mid") == "midi" && chat::FileKind("p.inf") == "patch" && chat::FileKind("k.field") == "field" &&
                     chat::FileKind("n.md") == "text" && chat::FileKind("x.exe").empty() && chat::FileKind("noext").empty(),
                  "files: kinds by extension");
            {
               const std::string keys = ShortcutsText();
               check(keys.find("## File & Patch") != std::string::npos && keys.find("New Patch: ") != std::string::npos,
                     "docs: the shortcut table reads as text");
               nlohmann::json out;
               std::string err;
               check(HandleAppRpc("attention", nlohmann::json::object(), out, err) && out.contains("selected") && out.contains("touched") && out["panels"].is_array(),
                     "docs: attention lists selection, touched control and panels");
            }
            const std::string w = chat::WithFiles("look", { "/a/b.wav", "/c/d.png" });
            check(w.rfind("look", 0) == 0 && w.find("/a/b.wav (audio)") != std::string::npos && w.find("/c/d.png (image)") != std::string::npos,
                  "files: the prompt lists each path and kind");
            const std::vector<std::string> dirs = chat::FileDirs({ "/a/b.wav", "/a/c.png", "/d/e.txt" });
            check(dirs.size() == 2 && dirs[0] == "/a" && dirs[1] == "/d", "files: each folder once");
            const std::vector<std::string> ex = chat::AttachExtensions();
            bool all = true;
            for (const std::string& e : ex)
               all = all && !chat::FileKind("x." + e).empty();
            check(ex.size() > 20 && all && std::count(ex.begin(), ex.end(), "png") == 1, "files: the dialog offers exactly what the chat takes");
         }

#if !defined(_WIN32)
         std::vector<chat::AgentEvent> rv;
         const bool done = runTurn({ "printf", "%s\\n", "{\"type\":\"system\",\"subtype\":\"init\",\"session_id\":\"s1\"}",
                                     "{\"type\":\"assistant\",\"message\":{\"content\":[{\"type\":\"text\",\"text\":\"Hello\"}]}}",
                                     "{\"type\":\"result\",\"subtype\":\"success\",\"is_error\":false,\"session_id\":\"s1\"}" },
                                   rv);
         check(done && rv.size() == 3 && rv[0].text == "s1" && rv[1].text == "Hello" && rv[2].kind == chat::AgentEvent::Kind::End && !rv[2].error,
               "runner: a turn's events arrive in order and it ends");
         std::vector<chat::AgentEvent> fv;
         const bool failed = runTurn({ "sh", "-c", "echo 'not found: claude' >&2; exit 3" }, fv);
         check(failed && fv.size() == 1 && fv[0].kind == chat::AgentEvent::Kind::End && fv[0].error && fv[0].text.find("not found") != std::string::npos,
               "runner: a child that dies without a result ends with its last words");
         chat::AgentRun::Options so;
         so.argv = { "sleep", "30" };
         std::shared_ptr<chat::AgentRun> slow = chat::AgentRun::Start(so);
         slow->Stop();
         std::vector<chat::AgentEvent> sv;
         const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
         while (!slow->Done() && std::chrono::steady_clock::now() < until)
         {
            slow->Poll(sv);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
         }
         slow->Poll(sv);
         check(slow->Done() && !sv.empty() && sv.back().kind == chat::AgentEvent::Kind::End && !sv.back().error, "runner: Stop ends the turn quietly");
#endif
      }
      {
         // C8: sign-in, plain-words errors, models, sound blocks, per-turn options.
         check(chat::SignInArgs(chat::AgentKind::Claude) == std::vector<std::string>({ "claude", "auth", "login" }), "sign-in: claude uses its browser login");
         check(chat::SignInArgs(chat::AgentKind::Codex).size() == 2 && chat::SignInArgs(chat::AgentKind::Codex)[1] == "login", "sign-in: codex uses its browser login");
         check(chat::StripAnsi("\x1b[31mred\x1b[0m\r\n") == "red\n", "ansi codes and carriage returns are stripped");
         check(chat::FindUrl("Open https://claude.ai/oauth?x=1 now") == "https://claude.ai/oauth?x=1", "the sign-in address is found in the output");
         check(chat::Explain("Claude", "Error: not logged in").kind == chat::Friendly::Kind::SignIn, "error: not logged in means sign in");
         check(chat::Explain("Codex", "You've hit your usage limit").kind == chat::Friendly::Kind::Limit, "error: usage limit");
         check(chat::Explain("Codex", "getaddrinfo ENOTFOUND api").kind == chat::Friendly::Kind::Offline, "error: offline");
         check(chat::Explain("Codex", "zsh: command not found: codex").kind == chat::Friendly::Kind::Missing, "error: missing command");
         check(chat::Explain("Codex", "boom happened").text == "boom happened", "error: anything else shows the last line");
         const std::vector<chat::Model> agy = chat::ParseAgyModels("ID\tLabel\nflash\tGemini Flash\nnot a row\n");
         check(agy.size() == 1 && agy[0].id == "flash" && agy[0].label == "Gemini Flash", "agy models are parsed from id<TAB>label rows");
         chat::TurnOptions to;
         to.model = "opus";
         const std::vector<std::string> ma = chat::AgentArgs(chat::AgentKind::Claude, "hi", "abc", false, {}, to);
         check(std::find(ma.begin(), ma.end(), "--model") != ma.end() && std::find(ma.begin(), ma.end(), "opus") != ma.end(), "claude: the chosen model is passed");
         check(chat::IsAudioPath("/tmp/a.wav") && chat::IsAudioPath("~/x/y.mp3") && !chat::IsAudioPath("notes.txt"), "sound paths are recognised");
         const std::vector<chat::Block> rb = chat::ParseReply("Here it is\n/tmp/kick.wav\n/tmp/pic.png\n```\ncode\n```\n", cat);
         bool aud = false, img = false, code = false;
         for (const chat::Block& b : rb)
         {
            aud = aud || b.kind == chat::Block::Kind::Audio;
            img = img || b.kind == chat::Block::Kind::Image;
            code = code || b.kind == chat::Block::Kind::Code;
         }
         check(aud && img && code, "a reply can hold a sound, a picture and a code block");
         // C9: slash commands, welcome, briefing, files.
         std::string sn, sa;
         check(chat::ParseSlash("/help", sn, sa) && sn == "help" && sa.empty(), "/help parses");
         check(chat::ParseSlash("/Docs fields now", sn, sa) && sn == "docs" && sa == "fields now", "a command keeps its argument");
         check(!chat::ParseSlash("/Users/x/kick.wav", sn, sa), "a path is not a command");
         check(!chat::ParseSlash("add a noise", sn, sa), "plain text is not a command");
         check(chat::MatchSlash("sh").size() == 1 && chat::MatchSlash("").size() == chat::SlashCommands().size(), "commands complete by prefix");
         bool buttons = false, web = false, say = false;
         for (const chat::SlashCommand& c : chat::SlashCommands())
         {
            const std::string ans = chat::SlashAnswer(c.name, "");
            if (ans.empty())
               continue;
            for (const chat::Block& b : chat::ParseReply(ans, cat))
               for (const chat::Seg& g : b.segs)
                  if (g.kind == chat::Seg::Kind::Action)
                  {
                     buttons = true;
                     web = web || g.verb == "web";
                  }
         }
         for (const chat::Block& b : chat::ParseReply(chat::Welcome(), cat))
            for (const chat::Seg& g : b.segs)
               say = say || (g.kind == chat::Seg::Kind::Action && g.verb == "say");
         check(buttons && web, "built-in answers carry buttons, links open the website");
         check(say, "the welcome offers things to say");
         check(chat::Briefing().find("infinite docs") != std::string::npos, "the briefing points agents at the manuals");
         check(chat::IsFilePath("/tmp/out.mp4") && !chat::IsFilePath("/tmp/a file.mp4") && !chat::IsFilePath("out.mp4"), "files are recognised");
         bool file = false;
         for (const chat::Block& b : chat::ParseReply("Saved\n/tmp/out.mp4\n", cat))
            file = file || b.kind == chat::Block::Kind::File;
         check(file, "a reply can hold a file");
      }
      std::printf("CHATTEST %s\n", ok ? "PASS" : "FAIL");
      return ok;
   }
}
