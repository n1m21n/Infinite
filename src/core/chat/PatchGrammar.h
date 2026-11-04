#pragma once
// The chat's patch grammar (docs/plans/agent-collab/README.md C4). A line that parses fully becomes one
// RPC batch the chat runs directly, with the `infinite` lines it equals; anything else goes to the agent.
// It also says which slot the end of the line is in, so the composer can open the right dropdown.
//
//   @a modulate <key> with @b[:out] [from x to y]     bind_mod
//   @a [set] <key> to <value>                          set_param
//   @a <key> = <formula>                               set_expr
//   [connect] @a[:out] to @b[:slot]                    connect
//   add <Type> [after @a]                              create_node (+ connect)
//   remove @a | bypass @a | unbypass @a | undo | redo
//   clauses joined by `and` run as one batch; `or` or `@agent` always goes to the agent
#include "ChatCatalog.h"

#include "json.hpp"

#include <string>
#include <vector>

namespace chat
{
   enum class SpanKind { Node, Param, Verb, Joiner, Value, Type, Formula, Agent, Bad };

   struct Span
   {
      size_t begin = 0, end = 0;  // bytes in the line
      SpanKind kind = SpanKind::Bad;
      int node = -1;    // Node, Param
      std::string key;  // Param
   };

   // What the word at the end of the line is, or the next word would be.
   enum class Slot { None, Start, Node, AfterNode, Param, Source, Value, Joiner, Type };

   struct Parse
   {
      bool direct = false;        // every clause parsed: run `calls`, no agent
      bool toAgent = false;       // `@agent` or `or` was written: never direct
      nlohmann::json calls = nlohmann::json::array();  // [{method, params}] for one `batch`
      std::vector<std::string> cli;  // the `infinite ...` lines the calls equal
      std::vector<Span> spans;
      // The first word that does not fit, with the nearest valid words.
      std::string error;
      size_t errorBegin = 0, errorEnd = 0;
      std::vector<std::string> near;
      // For the dropdown: the slot at the end of the line, the word typed into it so far, and the
      // node/param it belongs to (Param: the node; Value: the node and key).
      Slot slot = Slot::Start;
      std::string partial;
      size_t partialBegin = 0;
      int slotNode = -1;
      std::string slotKey;
      std::vector<std::string> words;  // grammar words that fit the slot (`with`, `to`, `and`...)
   };

   Parse ParseLine(const std::string& line, const Catalog& catalog);
}
