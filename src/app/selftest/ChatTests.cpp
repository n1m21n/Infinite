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
#include "core/chat/NodeFinder.h"
#include "core/AgentDocs.h"

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
            const chat::Parse g = ParseLine("@gemini @chatgpt hi", cat);
            bool gem = false, gpt = false;
            for (const chat::Span& sp : g.spans)
               gem = gem || (sp.kind == chat::SpanKind::Agent && sp.key == "antigravity"), gpt = gpt || (sp.kind == chat::SpanKind::Agent && sp.key == "codex");
            check(gem && gpt, "mentions: @gemini and @chatgpt name Antigravity and Codex");
         }

         // Plain words find the node from every card, not just the few the examples name.
         {
            auto top = [](const std::string& ask, const std::string& want, size_t within)
            {
               const std::vector<chat::NodeHit> hits = chat::FindNodes(ask, AgentDocs::kNodeGuide, within);
               return std::any_of(hits.begin(), hits.end(), [&](const chat::NodeHit& h) { return h.name == want; });
            };
            check(top("add some reverb", "Reverb", 3), "find: reverb");
            check(top("blur the image", "Gaussian / Box Blur", 4), "find: blur the image");
            check(top("drum pattern", "Drum Sequencer", 3), "find: drum pattern");
            check(top("send notes out to my synth over midi", "MIDI Out", 5), "find: midi out");
            check(chat::FindNodes("the and of", AgentDocs::kNodeGuide).empty(), "find: stop words alone match nothing");
            const std::vector<chat::NodeHit> one = chat::FindNodes("reverb", AgentDocs::kNodeGuide, 1);
            check(one.size() == 1 && !one[0].kind.empty() && !one[0].purpose.empty(), "find: a hit carries its kind and purpose");
            check(top("add a chorus to the guitar", "Chorus", 3), "find: a node with no guide card is found by its own name");
            const std::vector<chat::NodeHit> wt = chat::FindNodes("wavetable synth", AgentDocs::kNodeGuide, 1);
            check(wt.size() == 1 && wt[0].type == "Wavetable", "find: a card named another way carries the type to add");
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
            // A removal that numbers the rest again (undo, reload): uids keep each node itself, no rename chain.
            {
               chat::Snapshot b2, a2;
               const char* names[] = { "chorder", "arpeggiator", "analog", "reverb" };
               for (int i = 0; i < 4; i++)
               {
                  chat::CatalogNode n;
                  n.index = i + 1;
                  n.uid = 100 + i;
                  n.type = n.title = names[i];
                  b2.catalog.nodes.push_back(n);
                  if (i > 0)
                  {
                     n.index = i;  // one lower once chorder is gone
                     a2.catalog.nodes.push_back(n);
                  }
               }
               b2.links.push_back({ 2, 0, 3, 0, "" });
               a2.links.push_back({ 1, 0, 2, 0, "" });
               const std::vector<chat::ChangeLine> rd = chat::Diff(b2, a2);
               bool renamed = false;
               for (const chat::ChangeLine& l : rd)
                  renamed = renamed || l.text.rfind("renamed", 0) == 0 || l.text.rfind("connected", 0) == 0 || l.text.rfind("disconnected", 0) == 0;
               check(rd.size() == 1 && rd[0].text == "removed chorder" && !renamed, "diff: renumbered nodes are not renames");
            }

            // A value the canvas no longer shows is called out; one that still holds is not.
            const chat::ChangeLine* size = nullptr;
            for (const chat::ChangeLine& l : d)
               if (l.key == "size")
                  size = &l;
            check(size != nullptr && size->node == 1 && size->after == "2.5", "diff: a value change keeps its param and value");
            if (size != nullptr)
            {
               chat::Catalog now = after.catalog;
               check(chat::Drift(*size, now).empty(), "drift: an unchanged value says nothing");
               now.nodes[0].params[1].value = "2.50001";
               check(chat::Drift(*size, now).empty(), "drift: rounding is not a change");
               now.nodes[0].params[1].value = "45.183";
               check(!chat::Drift(*size, now).empty(), "drift: a value moved after the turn is shown");
               now.nodes[0].params[1].modulated = true;
               check(chat::Drift(*size, now).empty(), "drift: a modulated param moves by design");
            }
         }

         // Code comes back unescaped, internal keys stay out, dotted keys link whole.
         {
            chat::Snapshot before, after;
            before.catalog = cat;
            chat::CatalogNode sk;
            sk.index = 20;
            sk.type = "Sketch3D";
            sk.title = "sketch 3d";
            sk.params.push_back({ "code", "Code", "a = 1;", "" });
            sk.params.push_back({ "field_paramMap", "", "{}", "" });
            sk.params.push_back({ "p.count", "", "4", "int" });
            before.catalog.nodes.push_back(sk);
            after.catalog = before.catalog;
            after.catalog.nodes.back().params[0].value = "a = 1;\\nb = \\\"x\\\";\\tc";
            after.catalog.nodes.back().params[1].value = "{\"x\":1}";
            const std::vector<chat::ChangeLine> d = chat::Diff(before, after);
            bool code = false, internal = false;
            for (const chat::ChangeLine& l : d)
            {
               code = code || l.code == "a = 1;\nb = \"x\";\tc";
               internal = internal || l.text.find("field_") != std::string::npos;
            }
            check(code, "diff: saved escapes in code come back as real newlines, quotes and tabs");
            check(!internal, "diff: field_ bookkeeping keys never show as changes");
            const auto dotted = chat::ParseReply("Set @sketch 3d#20.p.count to 8.", before.catalog);
            bool whole = false;
            if (!dotted.empty())
               for (const chat::Seg& g : dotted[0].segs)
                  whole = whole || (g.kind == chat::Seg::Kind::Param && g.key == "p.count" && g.node == 20);
            check(whole, "reply links: a dotted key links as one param chip");
         }

         // Steps: a retry of the same command (with a cd in front, a pipe after) shows once, failures count.
         {
            check(chat::StepKey("cd /tmp && infinite set 1 scale 3") == chat::StepKey("infinite set 1 scale 3"), "steps: a cd in front is the same command");
            check(chat::StepKey("infinite context | head -5") == chat::StepKey("infinite context 2>&1"), "steps: pipes and redirects are the same command");
            const std::vector<std::string> lines = { "Setting the noise", "cd /tmp && infinite set 1 scale 3", "infinite set 1 scale 3", "infinite set 1 warp 0.8" };
            const std::vector<int> marks = { chat::kStepNote, chat::kStepFailed, chat::kStepRan, chat::kStepRan };
            const chat::StepView v = chat::ViewSteps(lines, marks);
            check(v.commands == 2 && v.retried == 1 && v.failed == 0 && v.rows.size() == 3, "steps: a failed try that was retried shows once");
            const chat::StepView f = chat::ViewSteps({ "infinite set 1 scale 3" }, { chat::kStepFailed });
            check(f.failed == 1 && f.rows.size() == 1 && f.rows[0].failed, "steps: a failure that stood is shown");
            const chat::StepView old = chat::ViewSteps({ "infinite a", "infinite b" }, {});
            check(old.commands == 2 && old.failed == 0, "steps: a chat saved without marks reads as commands");
            bool ed = false;
            check(chat::StepPhrase("infinite add Noise --at 0 0", &ed) == "Added Noise" && ed, "steps: add reads as Added <type>");
            check(chat::StepPhrase("infinite connect 3:out 4:in") == "Connected 3 \xE2\x86\x92 4", "steps: connect reads as Connected a to b");
            check(chat::StepPhrase("infinite set 2 scale 3") == "Set 2 scale to 3", "steps: set reads in words");
            check(chat::StepPhrase("cd /x && infinite context | head", &ed) == "Looked at the patch" && !ed, "steps: reading is not an edit");
            check(chat::StepPhrase("echo hi").empty(), "steps: an unknown command keeps its own text");
            const chat::StepView rd = chat::ViewSteps({ "infinite context", "infinite graph", "infinite add Blur" }, {});
            check(rd.rows.size() == 2 && rd.rows[0].times == 2 && rd.edits == 1, "steps: reads in a row fold to one row");
         }

         // ChatGPT's models come from Codex's own cache: listed ones only, in its order.
         {
            const std::vector<chat::Model> m = chat::ParseCodexModels(
               R"({"models":[{"slug":"b","display_name":"B","visibility":"list","priority":2},)"
               R"({"slug":"h","display_name":"H","visibility":"hide","priority":0},)"
               R"({"slug":"a","display_name":"A","visibility":"list","priority":1}]})");
            check(m.size() == 2 && m[0].id == "a" && m[1].label == "B", "models: codex cache, listed only, by priority");
            check(chat::ParseCodexModels("not json").empty(), "models: a broken cache gives none");
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
         // Windows: what cmd.exe and then the C runtime make of WindowsCmdArgs must be the argv we meant.
         auto afterCmd = [](const std::string& raw, bool* bare)
         {
            // `/d /s /c "X"` -> X, then cmd's caret pass; any metacharacter left bare would be acted on.
            const std::string head = "/d /s /c \"";
            std::string x = raw.rfind(head, 0) == 0 && raw.size() > head.size() && raw.back() == '"' ? raw.substr(head.size(), raw.size() - head.size() - 1) : std::string();
            std::string out;
            *bare = false;
            for (size_t i = 0; i < x.size(); i++)
            {
               if (x[i] == '^' && i + 1 < x.size())
               {
                  out += x[++i];
                  continue;
               }
               if (std::string("&|<>%\"").find(x[i]) != std::string::npos)
                  *bare = true;
               out += x[i];
            }
            return out;
         };
         auto crtSplit = [](const std::string& line)
         {
            std::vector<std::string> args;
            size_t i = 0;
            while (i < line.size())
            {
               while (i < line.size() && line[i] == ' ')
                  i++;
               if (i >= line.size())
                  break;
               std::string a;
               bool quoted = false;
               for (; i < line.size() && (quoted || line[i] != ' '); i++)
               {
                  size_t slashes = 0;
                  while (i < line.size() && line[i] == '\\')
                     slashes++, i++;
                  if (i < line.size() && line[i] == '"')
                  {
                     a.append(slashes / 2, '\\');
                     if (slashes % 2)
                        a += '"';
                     else
                        quoted = !quoted;
                     continue;
                  }
                  a.append(slashes, '\\');
                  if (i >= line.size())
                     break;
                  if (!quoted && line[i] == ' ')
                     break;
                  a += line[i];
               }
               args.push_back(a);
            }
            return args;
         };
         const std::string json = "{\"permissions\":{\"allow\":[\"command(infinite)\"]}}";
         const std::vector<std::string> argv = { "claude", "-p", "50% & more | x > y", "C:\\dir\\", json, "say \"hi\" & calc", "two\nlines" };
         bool bare = true;
         const std::vector<std::string> got = crtSplit(afterCmd(chat::WindowsCmdArgs(argv), &bare));
         check(!bare, "windows: no cmd metacharacter is left unescaped");
         check(got.size() == argv.size(), "windows: every argument arrives as one argument");
         if (got.size() == argv.size())
         {
            check(got[2] == argv[2], "windows: & | > and % arrive as typed");
            check(got[3] == argv[3], "windows: a trailing backslash survives");
            check(got[4] == json, "windows: JSON settings keep their quotes");
            check(got[5] == "say \xE2\x80\x9Dhi\xE2\x80\x9D & calc", "windows: quotes next to & become curly, so a .cmd shim cannot break out");
            check(got[6] == "two lines", "windows: a line break becomes a space");
         }
      }
      {
         // C8: sign-in, plain-words errors, models, sound blocks, per-turn options.
         check(chat::SignInArgs(chat::AgentKind::Claude) == std::vector<std::string>({ "claude", "auth", "login" }), "sign-in: claude uses its browser login");
         check(chat::SignInArgs(chat::AgentKind::Codex).size() == 2 && chat::SignInArgs(chat::AgentKind::Codex)[1] == "login", "sign-in: codex uses its browser login");
         check(chat::StripAnsi("\x1b[31mred\x1b[0m\r\n") == "red\n", "ansi codes and carriage returns are stripped");
         check(chat::FindUrl("Open https://claude.ai/oauth?x=1 now") == "https://claude.ai/oauth?x=1", "the sign-in address is found in the output");
         check(chat::AsksForCode("Waiting for authentication (timeout 60s)...\nOr, paste the authorization code here and press Enter:"), "a sign-in that asks for a pasted code is noticed");
         check(!chat::AsksForCode("Open https://claude.ai/oauth?x=1 now"), "a plain browser sign-in does not ask for a code");
         check(chat::Explain("Claude", "Error: not logged in").kind == chat::Friendly::Kind::SignIn, "error: not logged in means sign in");
         check(chat::WaitsForSignIn("Please sign in: https://accounts.google.com/o/oauth2/auth?x=1\nPaste the code here:"), "turn: agy asking for a sign-in mid-turn is noticed");
         check(chat::WaitsForSignIn("\x1b[1mOpen this page to log in\x1b[0m\r\nhttps://example.com/device\n"), "turn: an address on the line after the ask still counts");
         check(!chat::WaitsForSignIn("{\"type\":\"text\",\"text\":\"sign in at https://x.com and paste the code\"}\n"), "turn: a reply that talks about signing in is not a sign-in");
         check(!chat::WaitsForSignIn("ign in at https://x.com/login\",\"more\":1}\n"), "turn: a cut-off piece of a json line is not a sign-in");
         check(!chat::WaitsForSignIn("Working on it\nsee https://infinitedevices.org\n"), "turn: an address alone is not a sign-in");
         {
            using chat::Auth;
            using chat::AgentKind;
            check(chat::SignedInFrom(AgentKind::Claude, true, "{\n  \"loggedIn\": true,\n  \"authMethod\": \"claude.ai\"\n}") == Auth::In, "status: claude json says signed in");
            check(chat::SignedInFrom(AgentKind::Claude, false, "{\"loggedIn\":false}") == Auth::Out, "status: claude json says signed out");
            check(chat::SignedInFrom(AgentKind::Claude, true, "") == Auth::Unknown, "status: claude with no answer stays unknown");
            check(chat::SignedInFrom(AgentKind::Codex, false, "Not logged in\n") == Auth::Out, "status: codex not logged in");
            check(chat::SignedInFrom(AgentKind::Codex, true, "Logged in using ChatGPT\n") == Auth::In, "status: codex logged in");
            check(chat::SignedInFrom(AgentKind::Antigravity, true, "Fetching available models...\nflash\tGemini Flash\n") == Auth::In, "status: agy lists models when signed in");
            check(chat::SignedInFrom(AgentKind::Antigravity, false, "not logged in\n") == Auth::Out, "status: agy without a stored login is signed out");
            check(chat::SignedInFrom(AgentKind::Antigravity, false, "Error: not logged into Antigravity") == Auth::Out, "status: agy not logged into Antigravity");
            check(chat::SignedInFrom(AgentKind::Antigravity, true, "Please sign in: https://accounts.google.com/o/oauth2/auth?x=1\n") == Auth::Out, "status: agy asking for a sign-in is signed out");
            check(chat::SignedInFrom(AgentKind::Antigravity, false, "getaddrinfo ENOTFOUND\n") == Auth::Unknown, "status: agy offline stays unknown");
            check(chat::StatusArgs(AgentKind::Claude, true) == std::vector<std::string>({ "claude", "auth", "status" }), "status: claude asks claude auth status");
            check(chat::StatusArgs(AgentKind::Codex, false) == std::vector<std::string>({ "codex", "login", "status" }), "status: codex asks codex login status");
            const std::vector<std::string> am = chat::StatusArgs(AgentKind::Antigravity, true);
            check(am.size() == 3 && am[0] == "/bin/sh" && am[2].find("find-generic-password -s gemini -a antigravity") != std::string::npos && am[2].find(" -w") == std::string::npos && am[2].find(" -g") == std::string::npos, "status: agy on mac checks the keychain item without reading the secret");
            check(chat::StatusArgs(AgentKind::Antigravity, false) == std::vector<std::string>({ "agy", "models" }), "status: agy elsewhere lists models");
            check(chat::SignOutArgs(AgentKind::Claude, true) == std::vector<std::string>({ "claude", "auth", "logout" }), "sign-out: claude auth logout");
            check(chat::SignOutArgs(AgentKind::Codex, false) == std::vector<std::string>({ "codex", "logout" }), "sign-out: codex logout");
            const std::vector<std::string> ao = chat::SignOutArgs(AgentKind::Antigravity, true);
            check(ao.size() == 3 && ao[2].find("delete-generic-password -s gemini -a antigravity") != std::string::npos && ao[2].find("jetski-standalone-oauth-token") != std::string::npos, "sign-out: agy on mac clears the keychain item and the token file");
            check(chat::SignOutArgs(AgentKind::Antigravity, false).empty(), "sign-out: agy on windows and linux is not offered");
            check(chat::kSignInSeconds >= 60.0 && chat::kSignInSeconds <= 600.0, "sign-in: waiting for the browser times out");
         }
         check(chat::Explain("Codex", "You've hit your usage limit").kind == chat::Friendly::Kind::Limit, "error: usage limit");
         check(chat::Explain("Codex", "getaddrinfo ENOTFOUND api").kind == chat::Friendly::Kind::Offline, "error: offline");
         check(chat::Explain("Codex", "zsh: command not found: codex").kind == chat::Friendly::Kind::Missing, "error: missing command");
         check(chat::Explain("Codex", "boom happened").text == "boom happened", "error: anything else shows the last line");
         const std::vector<chat::Model> agy = chat::ParseAgyModels("ID\tLabel\nflash\tGemini Flash\nnot a row\n");
         check(agy.size() == 1 && agy[0].id == "flash" && agy[0].label == "Gemini Flash", "agy models are parsed from id<TAB>label rows");
         const std::vector<chat::Model> agy2 = chat::ParseAgyModels("Fetching available models...\ngemini-4-argon-medium\tGemini 4 Argon (Medium)\ngemini-3.8-flash-high\tGemini 3.8 Flash (High)\n");
         check(agy2.size() == 2 && agy2[0].id == "gemini-4-argon-medium", "agy models: no header line, every row kept");
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
