// INFINITE_CHATTEST: the chat's patch grammar and reply links against a hand-made catalog
// (docs/plans/agent-collab/README.md C3, C4). No window, no agent. Early exit before glfwInit().
#include "app/AppShared.h"
#include "core/chat/AgentStream.h"
#include "core/chat/PatchGrammar.h"
#include "core/chat/ReplyLinks.h"

#include <algorithm>
#include <cstdio>

namespace app
{
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
      }
      std::printf("CHATTEST %s\n", ok ? "PASS" : "FAIL");
      return ok;
   }
}
