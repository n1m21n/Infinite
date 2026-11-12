#pragma once

#include "FieldBoxRegistry.h"
#include "FieldError.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Field
{
   struct BoxPin
   {
      std::string name;
      ValueKind kind = ValueKind::Number;
      double defaultValue = 0.0;
   };

   struct BoxNode
   {
      int id = 0;
      std::string key;    // Stable key for @boxes header (e.g. "b1", "k_rate")
      std::string specId; // Registry id, or "code", "ident", "output_pin", "input_pin", "assign_var"
      std::string label;
      float x = 0.0f;
      float y = 0.0f;
      bool hasExplicitPos = false;

      std::vector<BoxPin> inlets;
      std::vector<BoxPin> outlets;

      // Statement / parameter / variable metadata
      std::string wireName;     // Named variable if this box's output is bound to a variable
      std::string varName;      // Target/declared variable name (knob, previous, attrib, output, input, neighbour)
      std::string varType;      // e.g. "float", "vec3", "image", "audio"
      std::string domainName;   // e.g. "frame", "element", "pixel", "sample"
      double numberValue = 0.0; // Literal or param default
      std::string literalText;  // Literal text exactly as written in the source
      double minVal = 0.0;
      double maxVal = 1.0;
      std::string assignOp = "="; // "=", "+=", "-=", "*=", "/="
      std::string swizzleField;   // e.g. "x", "y", "rgb" or reduce op ("max", "mean")
      std::string boundary;       // e.g. "wrap", "border" for state
      std::string rawCode;        // For "code" boxes: the source text, comments included
      int rawPrec = 0;            // Binding strength of rawCode (0 = unknown, always parenthesised)
      std::string callName;       // Set when the source wrote a call (e.g. "pow"); empty for operator form
      std::string comment;        // Whole-line comments printed above this box's statement
      int stmtOrder = -1;         // Top-level statement order (-1 for pure sub-expressions)
      int whenGroupId = -1;       // Group id if inside an `if (cond) { ... }` block
   };

   struct BoxWire
   {
      int id = 0;
      int srcBoxId = 0;
      int srcPinIdx = 0;
      int dstBoxId = 0;
      int dstPinIdx = 0;
      ValueKind kind = ValueKind::Number;
   };

   struct BoxGroup
   {
      int id = 0;
      std::string name;
      std::vector<int> boxIds;
      bool folded = false;
      bool isWhenGroup = false;
      int condBoxId = 0;
      int condPinIdx = 0;
      int stmtOrder = -1;
      std::string comment;
   };

   struct BoxViewState
   {
      float panX = 0.0f;
      float panY = 0.0f;
      float zoom = 1.0f;
      bool valid = false;
   };

   struct BoxGraph
   {
      uint32_t hostMask = HostAll;
      BoxViewState view;
      std::vector<BoxNode> boxes;
      std::vector<BoxWire> wires;
      std::vector<BoxGroup> groups;
      std::string trailingComment; // Comments after the last statement

      const BoxNode* FindBox(int id) const;
      BoxNode* FindBox(int id);
      const BoxWire* FindWireTo(int dstBoxId, int dstPinIdx) const;
      int CountCodeBoxes() const;
   };

   struct BoxHeaderEntry
   {
      std::string key;
      float x = 0.0f;
      float y = 0.0f;
   };

   struct BoxHeaderGroup
   {
      std::string name;
      std::vector<std::string> keys;
      bool folded = false;
   };

   struct BoxHeaderData
   {
      BoxViewState view;
      std::vector<BoxHeaderEntry> positions;
      std::vector<BoxHeaderGroup> groups;
   };

   // Parses `# @boxes v1 view x,y,z | key x,y | group "name" k1 k2` (the older `//` form is accepted).
   // Never fails; malformed or missing headers yield empty/partial BoxHeaderData.
   void ParseBoxesHeader(const std::string& code, BoxHeaderData& outHeader);

   // Formats the `# @boxes v1 ...` header line for the graph.
   std::string FormatBoxesHeader(const BoxGraph& graph);

   // Parses Field source into a BoxGraph. Returns false on lex/parse error.
   bool ParseCodeToBoxGraph(const std::string& code, uint32_t hostMask, BoxGraph& outGraph, FieldError& outError);

   // Prints a BoxGraph back to Field source code, from the wires: statements in
   // dependency order (ties keep source order), an output read more than once is
   // named, everything else is inlined. Comments are kept: a code box keeps its
   // raw text; comments around other statements print as whole lines above them.
   std::string PrintBoxGraphToCode(const BoxGraph& graph, bool includeHeader = true);

   // Assigns clean left-to-right column positions to boxes without explicit positions.
   void AutoLayoutBoxGraph(BoxGraph& graph);
}
