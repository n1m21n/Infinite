// Field Patcher panel (internal/plans/field-patcher/README.md §8). Docks bottom or top like the Curve Editor; one
// page per Field Notes / Field Primitive / Field Modifier node, following the canvas selection, plus scratch pages
// with no node yet. The node's `code` is the model: every gesture prints the box graph back to Field (with the
// `# @boxes v1` header that keeps positions), sets the node's code, compiles it and is one patch undo step. A
// failed compile leaves the node on its last good program; the footer names the error.
#include "app/panels/FieldPatcherPanel.h"

#include "app/AppShared.h"
#include "app/ui/design/UiDensity.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/components/ActionButton.h"
#include "app/ui/design/components/ChipButton.h"
#include "app/ui/design/components/EmptyState.h"
#include "app/ui/design/components/FieldWell.h"
#include "app/ui/design/components/FormParts.h"
#include "app/ui/design/components/IconTile.h"
#include "app/ui/design/components/MenuParts.h"
#include "app/ui/design/components/PagePicker.h"
#include "app/ui/design/components/PanelFrame.h"
#include "core/Modulation.h"
#include "core/Patch.h"
#include "core/field/FieldBoxGraph.h"
#include "core/field/FieldLex.h"
#include "core/field/FieldParse.h"
#include "core/field/FieldPatcherStarters.h"
#include "nodes/FieldElementNode.h"
#include "nodes/FieldNotesNode.h"
#include "nodes/FieldPixelNode.h"
#include "nodes/FieldPrimitiveNode.h"
#include "nodes/FieldSampleNode.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace app
{
bool gFieldPatcherOpen = false;
ImVec2 gFieldPatcherRectMin, gFieldPatcherRectMax;
int gFieldPatcherDock = 0;
float gFieldPatcherHeight = 380.0f;
bool gFieldPatcherClaimedKeys = false;
bool gFieldPatcherMaximised = false;
float gFieldPatcherShownHeight = 380.0f;

namespace
{
   constexpr float kMinHeight = 220.0f;
   constexpr float kInspectorW = UiDensity::kInspectorW + 2.0f * tok::space_2;   // one compact strip
   constexpr int kPinStride = 128; // pin ids: box * stride + 1 + inlet, outlets from +64

   // ---- hosts: the three Field nodes a page can show

   struct Host
   {
      GraphNode* gn = nullptr;
      std::string* code = nullptr;
      uint32_t mask = 0;
      const char* nodeName = "";
      std::string error;
      Field::ParamTable* params = nullptr;
      IModulator* publish = nullptr;
      bool Apply()
      {
         if (gn == nullptr)
            return false;
         if (auto* n = dynamic_cast<FieldNotesNode*>(gn->node.get()))
            return n->Apply();
         if (auto* n = dynamic_cast<FieldPrimitiveNode*>(gn->node.get()))
            return n->Apply();
         if (auto* n = dynamic_cast<FieldElementNode*>(gn->node.get()))
            return n->Apply();
         if (auto* n = dynamic_cast<FieldPixelNode*>(gn->node.get()))
            return n->Apply();
         if (auto* n = dynamic_cast<FieldSampleNode*>(gn->node.get()))
            return n->Apply();
         return false;
      }
   };

   bool IsFieldHost(const GraphNode& gn)
   {
      INode* n = gn.node.get();
      return dynamic_cast<FieldNotesNode*>(n) != nullptr || dynamic_cast<FieldPrimitiveNode*>(n) != nullptr ||
             dynamic_cast<FieldElementNode*>(n) != nullptr || dynamic_cast<FieldPixelNode*>(n) != nullptr ||
             dynamic_cast<FieldSampleNode*>(n) != nullptr;
   }

   Host HostOf(GraphNode* gn)
   {
      Host h;
      h.gn = gn;
      if (gn == nullptr)
         return h;
      if (auto* n = dynamic_cast<FieldNotesNode*>(gn->node.get()))
      {
         h.code = &n->code;
         h.mask = Field::HostNotes;
         h.nodeName = "Field Notes";
         h.error = n->LastError();
         h.params = &n->GetParamTable();
         h.publish = n->publishScalarOutput ? n->ModulatorOutput(1) : nullptr;
      }
      else if (auto* m = dynamic_cast<FieldModulatorNode*>(gn->node.get()))
      {
         h.code = &m->code;
         h.mask = Field::HostPrimitive;
         h.nodeName = "Field Modulator";
         h.error = m->LastError();
         h.params = &m->GetParamTable();
         h.publish = m->ModulatorOutput(0);
      }
      else if (auto* n = dynamic_cast<FieldPrimitiveNode*>(gn->node.get()))
      {
         h.code = &n->code;
         h.mask = Field::HostPrimitive;
         h.nodeName = n->valueMode ? "Field Primitive · Value" : "Field Primitive";
         h.error = n->LastError();
         h.params = &n->GetParamTable();
         h.publish = n->publishScalarOutput ? n->ModulatorOutput(1) : nullptr;
      }
      else if (auto* n = dynamic_cast<FieldElementNode*>(gn->node.get()))
      {
         h.code = &n->code;
         h.mask = Field::HostElement;
         h.nodeName = "Field Modifier";
         h.error = n->LastError();
         h.params = &n->GetParamTable();
         h.publish = n->publishScalarOutput ? n->ModulatorOutput(1) : nullptr;
      }
      else if (auto* n = dynamic_cast<FieldPixelNode*>(gn->node.get()))
      {
         h.code = &n->code;
         h.mask = Field::HostPixel;
         h.nodeName = "Field Pixel";
         h.error = n->LastError();
         h.params = &n->GetParamTable();
      }
      else if (auto* n = dynamic_cast<FieldSampleNode*>(gn->node.get()))
      {
         h.code = &n->code;
         h.mask = Field::HostSample;
         h.nodeName = "Field Effect";
         h.error = n->LastError();
         h.params = &n->GetParamTable();
      }
      return h;
   }

   // ---- pages

   struct Page
   {
      uint64_t uid = 0;          // the Field node; 0 = scratch page
      int scratchId = 0;         // scratch pages only
      std::string scratchCode;   // scratch pages only: the source the page holds
      std::string scratchName;
      bool pinned = false;
      unsigned int color = 0;    // page chip colour, 0xRRGGBB
      ed::EditorContext* ctx = nullptr;
      Field::BoxGraph graph;
      std::string parsedFrom;    // the source text the graph was built from
      std::string parseError;
      bool placeAll = true;      // push every box position into the editor next frame
      bool navigate = true;
         // the tab's name when the user renamed it (node pages: a label, the node keeps its own)
      bool hoverItem = false;    // last frame: the pointer was over a box, pin or wire (so a left drag is not a pan)
      bool panLeft = false;      // a left drag that began on empty canvas pans it
      ImVec2 canvasMin {}, canvasMax {}; // last frame's canvas rectangle on screen (key test)
   };

   struct State
   {
      std::vector<std::unique_ptr<Page>> pages;
      int front = -1;
      uint64_t lastSelected = 0;
      std::set<uint64_t> closed;  // node pages hidden from the strip (the nodes stay)
      int nextScratch = 1;
      int inspectorTab = 0;       // 0 Code, 1 Box
      std::string codeBuf;
      std::string codeBufFor;     // the source codeBuf was filled from
      bool codeEditing = false;
      int pendingFromPin = 0;     // a wire dropped in empty space: the pin it came from
      ImVec2 pendingAt {};
      char search[64] = {};
      int railFamily = -1;
      bool kindHint = false;
      std::map<uint64_t, std::string> nodeNames;
      std::vector<Field::BoxNode> clipBoxes; // copied boxes, with the wires that ran between them
      std::vector<Field::BoxWire> clipWires;
      int ctxBox = 0;             // the box a right-click menu is open on
      int ctxLink = 0;
      int renaming = -1;          // the page whose tab is being renamed
      char renameBuf[64] = {};
      bool renameFocus = false;
   };

   State& S()
   {
      static State s;
      return s;
   }

   Page* Front()
   {
      State& s = S();
      return (s.front >= 0 && s.front < (int)s.pages.size()) ? s.pages[s.front].get() : nullptr;
   }

   void DropContext(Page& p)
   {
      if (p.ctx == nullptr)
         return;
      ed::EditorContext* prev = ed::GetCurrentEditor();
      ed::EditorContext* dying = p.ctx;
      p.ctx = nullptr;
      ed::DestroyEditor(dying);
      ed::SetCurrentEditor(prev == dying ? nullptr : prev);
   }

   int FindPage(uint64_t uid)
   {
      const State& s = S();
      for (int i = 0; i < (int)s.pages.size(); i++)
         if (s.pages[i]->uid == uid && uid != 0)
            return i;
      return -1;
   }

   void ShowNodePage(uint64_t uid)
   {
      State& s = S();
      s.closed.erase(uid);
      int i = FindPage(uid);
      if (i < 0)
      {
         auto p = std::make_unique<Page>();
         p->uid = uid;
         s.pages.push_back(std::move(p));
         i = (int)s.pages.size() - 1;
      }
      s.front = i;
   }

   void ClosePage(int i)
   {
      State& s = S();
      if (i < 0 || i >= (int)s.pages.size())
         return;
      Page& p = *s.pages[i];
      if (p.uid != 0)
         s.closed.insert(p.uid);
      DropContext(p);
      s.pages.erase(s.pages.begin() + i);
      if (i < s.front)
         s.front--; // the page in front keeps its place
      if (s.front >= (int)s.pages.size())
         s.front = (int)s.pages.size() - 1;
   }

   Page& AddScratch(const std::string& name, const std::string& code)
   {
      State& s = S();
      auto p = std::make_unique<Page>();
      p->scratchId = s.nextScratch++;
      p->scratchName = name.empty() ? std::string(T("Scratch")) + " " + std::to_string(p->scratchId) : name;
      p->scratchCode = code;
      s.pages.push_back(std::move(p));
      s.front = (int)s.pages.size() - 1;
      return *s.pages.back();
   }

   // Drop pages whose node is gone; add nothing (pages appear by selection, ⧉ or +).
   void PrunePages()
   {
      State& s = S();
      for (int i = (int)s.pages.size() - 1; i >= 0; i--)
      {
         Page& p = *s.pages[i];
         if (p.uid == 0)
            continue;
         GraphNode* gn = FindNodeByUid(p.uid);
         if (gn == nullptr || !IsFieldHost(*gn))
         {
            const uint64_t uid = p.uid;
            ClosePage(i);
            s.closed.erase(uid);
         }
      }
   }

   void FollowCanvasSelection()
   {
      State& s = S();
      Page* f = Front();
      if (f != nullptr && f->pinned)
         return;
      uint64_t uid = 0;
      ed::EditorContext* prev = ed::GetCurrentEditor();
      ed::SetCurrentEditor(gEditor);
      for (GraphNode& gn : gNodes)
         if (IsFieldHost(gn) && ed::IsNodeSelected(gn.NodeId()))
         {
            uid = gn.uid;
            break;
         }
      ed::SetCurrentEditor(prev);
      if (uid == s.lastSelected)
         return;
      s.lastSelected = uid;
      if (uid != 0)
         ShowNodePage(uid);
   }

   // ---- the page's source text

   int StarterIndex(const char* name)
   {
      for (int k = 0; k < Field::kPatcherStarterCount; k++)
         if (std::strcmp(Field::kPatcherStarters[k].name, name) == 0)
            return k;
      return 0;
   }

   // True when some line of `src` assigns `name` (`col = ...`, `out=...`).
   bool AssignsLine(const std::string& src, const char* name)
   {
      const size_t n = std::strlen(name);
      size_t at = 0;
      while (at < src.size())
      {
         size_t b = at;
         while (b < src.size() && (src[b] == ' ' || src[b] == '\t'))
            b++;
         if (src.compare(b, n, name) == 0)
         {
            size_t e = b + n;
            while (e < src.size() && src[e] == ' ')
               e++;
            if (e < src.size() && src[e] == '=' && (e + 1 >= src.size() || src[e + 1] != '='))
               return true;
         }
         const size_t nl = src.find('\n', at);
         if (nl == std::string::npos)
            break;
         at = nl + 1;
      }
      return false;
   }

   // A scratch page runs where its program can: a note() call makes it a Field Notes page, `col =` a Field Pixel
   // page, `out =` a Field Effect page, anything else a Field Primitive one (value and geometry outlets both live there).
   uint32_t ScratchMask(const std::string& src)
   {
      if (src.find("note(") != std::string::npos)
         return Field::HostNotes;
      if (AssignsLine(src, "col"))
         return Field::HostPixel;
      if (AssignsLine(src, "out"))
         return Field::HostSample;
      return Field::HostPrimitive;
   }

   std::string SourceOf(Page& p, Host& h)
   {
      if (p.uid == 0)
         return p.scratchCode;
      return h.code != nullptr ? *h.code : std::string();
   }

   uint32_t MaskOf(Page& p, Host& h) { return p.uid == 0 ? ScratchMask(p.scratchCode) : h.mask; }

   void Reparse(Page& p, const std::string& src, uint32_t mask)
   {
      Field::FieldError err;
      Field::BoxGraph g;
      if (Field::ParseCodeToBoxGraph(src, mask, g, err))
      {
         p.graph = std::move(g);
         p.parseError.clear();
      }
      else
      {
         p.graph = Field::BoxGraph{};
         p.parseError = err.message;
      }
      p.parsedFrom = src;
      p.placeAll = true;
   }

   // Positions survive a commit: the reparsed graph's boxes take the old positions of the boxes they came from,
   // matched by kind in order (the printer keeps that order), so the header written next keeps the layout.
   void CarryPositions(const Field::BoxGraph& from, Field::BoxGraph& to)
   {
      std::map<std::string, std::vector<const Field::BoxNode*>> byKind;
      for (const Field::BoxNode& b : from.boxes)
         byKind[b.specId].push_back(&b);
      std::map<std::string, size_t> used;
      for (Field::BoxNode& b : to.boxes)
      {
         auto it = byKind.find(b.specId);
         size_t& k = used[b.specId];
         if (it == byKind.end() || k >= it->second.size())
            continue;
         const Field::BoxNode* o = it->second[k++];
         b.x = o->x;
         b.y = o->y;
         b.hasExplicitPos = true;
      }
      to.view = from.view;
   }

   std::string CompileCheck(const std::string& src)
   {
      std::vector<Field::Token> toks;
      Field::FieldError err;
      Field::AstNodePtr ast;
      if (!Field::Lex(src, toks, err) || !Field::ParseProgram(toks, ast, err))
         return err.message;
      return std::string();
   }

   // One gesture: print the graph, keep positions, write the source, compile. One undo step.
   void Commit(Page& p, Host& h, const char* what)
   {
      const std::string body = Field::PrintBoxGraphToCode(p.graph, false);
      Field::BoxGraph re;
      Field::FieldError err;
      std::string src = body;
      if (Field::ParseCodeToBoxGraph(body, MaskOf(p, h), re, err))
      {
         CarryPositions(p.graph, re);
         src = Field::FormatBoxesHeader(re) + "\n" + body;
      }
      if (p.uid == 0)
         p.scratchCode = src;
      else if (h.code != nullptr)
      {
         PushUndoCheckpoint(what);
         *h.code = src;
         h.Apply();
      }
      Reparse(p, src, MaskOf(p, h));
   }

   void SetSource(Page& p, Host& h, const std::string& src)
   {
      if (p.uid == 0)
         p.scratchCode = src;
      else if (h.code != nullptr)
      {
         PushUndoCheckpoint("Edit Field");
         *h.code = src;
         h.Apply();
      }
      Reparse(p, src, MaskOf(p, h));
   }

   // ---- boxes

   int NextBoxId(const Field::BoxGraph& g)
   {
      int id = 0;
      for (const Field::BoxNode& b : g.boxes)
         id = std::max(id, b.id);
      return id + 1;
   }

   std::string FreshName(const Field::BoxGraph& g, const char* stem)
   {
      for (int i = 1;; i++)
      {
         std::string n = std::string(stem) + std::to_string(i);
         bool taken = false;
         for (const Field::BoxNode& b : g.boxes)
            taken = taken || b.varName == n || b.wireName == n;
         if (!taken)
            return n;
      }
   }

   Field::BoxNode& AddBox(Page& p, const Field::BoxSpec& spec, ImVec2 at)
   {
      Field::BoxNode b;
      b.id = NextBoxId(p.graph);
      b.specId = spec.id;
      b.label = spec.label;
      for (const Field::PinSpec& pin : spec.inlets)
         b.inlets.push_back({ pin.name, pin.kind, pin.defaultValue });
      for (const Field::PinSpec& pin : spec.outlets)
         b.outlets.push_back({ pin.name, pin.kind, pin.defaultValue });
      b.x = at.x;
      b.y = at.y;
      b.hasExplicitPos = true;
      if (spec.id == "number")
      {
         b.numberValue = 1.0;
         b.literalText = "1";
      }
      else if (spec.id == "knob")
      {
         b.varName = FreshName(p.graph, "k");
         b.varType = "float";
         b.numberValue = 0.5;
         b.minVal = 0.0;
         b.maxVal = 1.0;
      }
      else if (spec.id == "previous")
      {
         b.varName = FreshName(p.graph, "s");
         b.varType = "float";
      }
      else if (spec.id == "swizzle")
         b.swizzleField = "x";
      else if (spec.id == "reduce")
         b.swizzleField = "max";
      else if (spec.id == "neighbour")
         b.varName = "P";
      else if (spec.id == "code")
         b.rawCode = "# Field";
      p.graph.boxes.push_back(b);
      return p.graph.boxes.back();
   }

   void RemoveBox(Page& p, int id)
   {
      auto& w = p.graph.wires;
      w.erase(std::remove_if(w.begin(), w.end(), [id](const Field::BoxWire& x) { return x.srcBoxId == id || x.dstBoxId == id; }), w.end());
      auto& b = p.graph.boxes;
      b.erase(std::remove_if(b.begin(), b.end(), [id](const Field::BoxNode& x) { return x.id == id; }), b.end());
      for (Field::BoxGroup& g : p.graph.groups)
         g.boxIds.erase(std::remove(g.boxIds.begin(), g.boxIds.end(), id), g.boxIds.end());
   }

   void Connect(Page& p, int src, int srcPin, int dst, int dstPin)
   {
      auto& w = p.graph.wires;
      w.erase(std::remove_if(w.begin(), w.end(), [&](const Field::BoxWire& x) { return x.dstBoxId == dst && x.dstPinIdx == dstPin; }), w.end());
      Field::BoxWire nw;
      int id = 0;
      for (const Field::BoxWire& x : w)
         id = std::max(id, x.id);
      nw.id = id + 1;
      nw.srcBoxId = src;
      nw.srcPinIdx = srcPin;
      nw.dstBoxId = dst;
      nw.dstPinIdx = dstPin;
      if (const Field::BoxNode* b = p.graph.FindBox(src))
         if (srcPin >= 0 && srcPin < (int)b->outlets.size())
            nw.kind = b->outlets[srcPin].kind;
      w.push_back(nw);
   }

   // ---- copy, paste, duplicate, delete (the box ids the editor has selected)

   std::vector<int> SelectedIds(Page& p)
   {
      std::vector<int> out;
      if (p.ctx == nullptr)
         return out;
      ed::EditorContext* prev = ed::GetCurrentEditor();
      ed::SetCurrentEditor(p.ctx);
      std::vector<ed::NodeId> ids((size_t)std::max(1, ed::GetSelectedObjectCount()));
      const int n = ed::GetSelectedNodes(ids.data(), (int)ids.size());
      for (int i = 0; i < n; i++)
         out.push_back((int)ids[i].Get());
      ed::SetCurrentEditor(prev);
      return out;
   }

   void CopyBoxes(Page& p, const std::vector<int>& ids)
   {
      State& s = S();
      s.clipBoxes.clear();
      s.clipWires.clear();
      auto has = [&](int id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); };
      for (const Field::BoxNode& b : p.graph.boxes)
         if (has(b.id))
            s.clipBoxes.push_back(b);
      for (const Field::BoxWire& w : p.graph.wires)
         if (has(w.srcBoxId) && has(w.dstBoxId))
            s.clipWires.push_back(w);
   }

   // Pastes the clipboard so its top-left box lands on `at`. Returns the number of boxes added.
   int PasteBoxes(Page& p, ImVec2 at)
   {
      State& s = S();
      if (s.clipBoxes.empty())
         return 0;
      float minX = 1e9f, minY = 1e9f;
      for (const Field::BoxNode& b : s.clipBoxes)
      {
         minX = std::min(minX, b.x);
         minY = std::min(minY, b.y);
      }
      int maxStmt = -1;
      for (const Field::BoxNode& b : p.graph.boxes)
         maxStmt = std::max(maxStmt, b.stmtOrder);
      std::map<int, int> remap;
      int next = NextBoxId(p.graph);
      for (const Field::BoxNode& src : s.clipBoxes)
      {
         Field::BoxNode b = src;
         b.id = next++;
         remap[src.id] = b.id;
         b.key.clear();
         b.wireName.clear();
         b.whenGroupId = -1;
         b.x = at.x + (src.x - minX);
         b.y = at.y + (src.y - minY);
         b.hasExplicitPos = true;
         if (src.stmtOrder >= 0)
            b.stmtOrder = ++maxStmt;
         if (src.specId == "knob")
            b.varName = FreshName(p.graph, "k");
         else if (src.specId == "previous")
            b.varName = FreshName(p.graph, "s");
         else if (src.specId == "assign_var")
            b.varName = FreshName(p.graph, "v");
         p.graph.boxes.push_back(b);
      }
      int wid = 0;
      for (const Field::BoxWire& w : p.graph.wires)
         wid = std::max(wid, w.id);
      for (const Field::BoxWire& w : s.clipWires)
      {
         Field::BoxWire nw = w;
         nw.id = ++wid;
         nw.srcBoxId = remap[w.srcBoxId];
         nw.dstBoxId = remap[w.dstBoxId];
         p.graph.wires.push_back(nw);
      }
      return (int)s.clipBoxes.size();
   }

   void SelectAllBoxes(Page& p)
   {
      if (p.ctx == nullptr)
         return;
      ed::EditorContext* prev = ed::GetCurrentEditor();
      ed::SetCurrentEditor(p.ctx);
      bool first = true;
      for (const Field::BoxNode& b : p.graph.boxes)
      {
         ed::SelectNode(b.id, !first);
         first = false;
      }
      ed::SetCurrentEditor(prev);
   }

   int InPinId(int box, int pin) { return box * kPinStride + 1 + pin; }
   int OutPinId(int box, int pin) { return box * kPinStride + 64 + pin; }
   void DecodePin(int id, int& box, int& pin, bool& out)
   {
      box = id / kPinStride;
      const int r = id % kPinStride;
      out = r >= 64;
      pin = out ? r - 64 : r - 1;
   }

   ImU32 KindCol(Field::ValueKind k, float a = 1.0f)
   {
      switch (k)
      {
      case Field::ValueKind::Bool: return ImGui::GetColorU32(ImGuiCol_CheckMark, a);
      case Field::ValueKind::Pulse: return ImGui::GetColorU32(ImGuiCol_PlotHistogram, a);
      case Field::ValueKind::Note: return ImGui::GetColorU32(ImGuiCol_PlotLines, a);
      case Field::ValueKind::Vec3:
      case Field::ValueKind::Vec2: return ImGui::GetColorU32(ImGuiCol_ButtonActive, a);
      case Field::ValueKind::PerPoint: return ImGui::GetColorU32(ImGuiCol_PlotLinesHovered, a);
      default: return ImGui::GetColorU32(ImGuiCol_Text, 0.55f * a);
      }
   }

   const char* BoxTitle(const Field::BoxNode& b)
   {
      if (b.specId == "code")
         return T("code");
      if (b.specId == "assign_var")
         return b.varName.c_str();
      if (!b.label.empty())
         return T(b.label.c_str());
      return b.specId.c_str();
   }

   std::string BoxSubtitle(const Field::BoxNode& b, const Field::BoxSpec* spec)
   {
      if (!b.wireName.empty())
         return b.wireName;
      if ((b.specId == "knob" || b.specId == "previous" || b.specId == "attrib") && !b.varName.empty())
         return b.varName;
      if (b.specId == "swizzle" || b.specId == "reduce")
         return "." + b.swizzleField;
      if (spec != nullptr && spec->fieldWord != spec->label)
         return spec->fieldWord;
      return std::string();
   }

   // ---- the pick menu (§8.6): a search field and the boxes this page's host can run

   // The same Dock Position submenu as the Curve Editor.
   void DockItem()
   {
      if (MenuParts::SubMenu(T("Dock Position")))
      {
         static const char* kDocks[] = { "Bottom", "Top" };
         static const int kCodes[] = { 0, 3 };
         for (int i = 0; i < 2; i++)
            if (MenuParts::Item(T(kDocks[i]), nullptr, gFieldPatcherDock == kCodes[i]))
               gFieldPatcherDock = kCodes[i];
         ImGui::EndMenu();
      }
      MenuParts::Separator();
      if (MenuParts::Item(T("Close Field Patcher")))
         gFieldPatcherOpen = false;
   }

   // Boxes only: paste, select all and the dock live on keys and the strip.
   bool PickMenu(Page& p, Host& h, ImVec2 at, int fromPin, int family)
   {
      State& s = S();
      bool added = false;
      ImGui::SetNextItemWidth(150.0f);
      if (ImGui::IsWindowAppearing())
      {
         s.search[0] = 0; // a stale query from last time would open on an empty list
         ImGui::SetKeyboardFocusHere();
      }
      FieldWell::InputTextWithHint("##fpSearch", T("Box name or Field word"), s.search, sizeof(s.search));
      const bool enter = ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter, false);
      const uint32_t mask = MaskOf(p, h);
      std::vector<const Field::BoxSpec*> list = Field::FindBoxSpecsByQuery(s.search, 0);

      auto place = [&](const Field::BoxSpec& spec) {
         Field::BoxNode& b = AddBox(p, spec, at);
         const int newId = b.id;
         if (fromPin != 0)
         {
            int box, pin;
            bool out;
            DecodePin(fromPin, box, pin, out);
            if (out && !spec.inlets.empty())
               Connect(p, box, pin, newId, 0);
            else if (!out && !spec.outlets.empty())
               Connect(p, newId, 0, box, pin);
         }
         Commit(p, h, "Add Box");
         added = true;
         s.search[0] = 0;
         ImGui::CloseCurrentPopup();
      };

      // The list scrolls in a window of about ten rows, so the menu never runs off the screen.
      const float rowH = ImGui::GetTextLineHeightWithSpacing() + 4.0f;
      ImGui::BeginChild("##fpPickList", ImVec2(260.0f, rowH * 10.0f), false);
      bool first = true;
      for (const Field::BoxSpec* spec : list)
      {
         if (family >= 0 && (int)spec->family != family)
            continue;
         if (spec->id == "assign_var" || spec->id == "ident")
            continue;
         const bool runs = (spec->hostMask & mask) != 0;
         if (enter && first && runs && !added)
         {
            place(*spec);
            break;
         }
         first = false;
         std::string row = std::string(T(spec->label.c_str()));
         if (spec->fieldWord != spec->label)
            row += "   " + spec->fieldWord;
         row += "##" + spec->id;
         if (MenuParts::Item(row.c_str(), nullptr, false, runs))
         {
            place(*spec);
            break;
         }
      }
      ImGui::EndChild();

      return added;
   }

   // ---- one box

   void DrawBox(Page& p, Host& h, Field::BoxNode& b, bool& changed)
   {
      const Field::BoxSpec* spec = Field::FindBoxSpec(b.specId);
      ed::BeginNode(b.id);
      ImGui::PushID(b.id);
      ImGui::TextUnformatted(BoxTitle(b));
      const std::string sub = BoxSubtitle(b, spec);
      if (!sub.empty())
      {
         FormParts::Inline();
         ImGui::TextDisabled("%s", sub.c_str());
      }

      // Bodies: a number scrubs, a knob shows its live value, a code box shows its text.
      if (b.specId == "number")
      {
         float v = (float)b.numberValue;
         ImGui::SetNextItemWidth(84.0f);
         if (ImGui::DragFloat("##num", &v, 0.01f, 0.0f, 0.0f, "%.3g"))
         {
            b.numberValue = v;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%g", v);
            b.literalText = buf;
         }
         if (ImGui::IsItemDeactivatedAfterEdit())
            changed = true;
      }
      else if (b.specId == "knob")
      {
         Field::ParamEntry* e = h.params != nullptr ? h.params->Find(b.varName) : nullptr;
         float v = e != nullptr ? e->value : (float)b.numberValue;
         ImGui::SetNextItemWidth(110.0f);
         if (ImGui::SliderFloat("##knob", &v, (float)b.minVal, (float)b.maxVal, "%.3g"))
         {
            if (e != nullptr)
               e->value = v; // live: the param moves without a recompile
            else
               b.numberValue = v;
         }
         if (ImGui::IsItemActivated() && p.uid != 0)
            PushUndoCheckpoint("Edit Knob");
         if (ImGui::IsItemDeactivatedAfterEdit() && e == nullptr)
            changed = true;
      }
      else if (b.specId == "code")
      {
         std::string text = b.rawCode;
         if (text.size() > 160)
            text = text.substr(0, 160) + " …";
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 240.0f);
         ImGui::TextDisabled("%s", text.c_str());
         ImGui::PopTextWrapPos();
      }
      else if ((b.specId == "value_out" || b.specId == "pulse_out") && h.publish != nullptr)
      {
         ImGui::TextDisabled("%.3f", h.publish->Value01());
      }

      // Pins: inlets left, outlets right, coloured by kind.
      const int rows = std::max((int)b.inlets.size(), (int)b.outlets.size());
      for (int r = 0; r < rows; r++)
      {
         if (r < (int)b.inlets.size())
         {
            ed::BeginPin(InPinId(b.id, r), ed::PinKind::Input);
            ed::PinPivotAlignment(ImVec2(0.0f, 0.5f));
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(KindCol(b.inlets[r].kind)), "● %s", b.inlets[r].name.c_str());
            ed::EndPin();
         }
         else
            ImGui::Dummy(ImVec2(60.0f, ImGui::GetTextLineHeight()));
         if (r < (int)b.outlets.size())
         {
            FormParts::Inline(0.0f, 24.0f);
            ed::BeginPin(OutPinId(b.id, r), ed::PinKind::Output);
            ed::PinPivotAlignment(ImVec2(1.0f, 0.5f));
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(KindCol(b.outlets[r].kind)), "%s ●", b.outlets[r].name.c_str());
            ed::EndPin();
         }
      }
      ImGui::PopID();
      ed::EndNode();
   }

   // ---- the box canvas

   void DrawCanvas(Page& p, Host& h, ImVec2 size)
   {
      State& s = S();
      if (p.ctx == nullptr)
      {
         ed::Config cfg;
         cfg.SettingsFile = nullptr; // positions live in the source header, not a json file
         cfg.EnableSmoothZoom = true;
         p.ctx = ed::CreateEditor(&cfg);
         p.placeAll = true;
      }
      ed::EditorContext* prev = ed::GetCurrentEditor();
      ed::Style mainStyle;
      ed::SetCurrentEditor(gEditor);
      mainStyle = ed::GetStyle();
      ed::SetCurrentEditor(p.ctx);
      ed::GetStyle() = mainStyle; // the same look as the main canvas

      {
         // A left drag that begins on empty canvas pans it (the main canvas does the same); a box, pin or wire keeps
         // its own drag, and Shift drags a selection box.
         ed::Config& live = const_cast<ed::Config&>(ed::GetConfig(p.ctx));
         if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered() && !p.hoverItem && !ImGui::GetIO().KeyShift &&
             !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeySuper)
            p.panLeft = true;
         if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            p.panLeft = false;
         live.NavigateButtonIndex = p.panLeft ? 0 : 1;
      }
      p.canvasMin = ImGui::GetCursorScreenPos();
      p.canvasMax = ImVec2(p.canvasMin.x + size.x, p.canvasMin.y + size.y);
      ed::Begin("##fpCanvas", size);
      bool changed = false;
      if (p.placeAll)
      {
         for (const Field::BoxNode& b : p.graph.boxes)
            ed::SetNodePosition(b.id, ImVec2(b.x, b.y));
      }
      for (Field::BoxNode& b : p.graph.boxes)
         DrawBox(p, h, b, changed);
      for (const Field::BoxWire& w : p.graph.wires)
      {
         const ImU32 col = KindCol(w.kind, 0.9f);
         ed::Link(w.id, OutPinId(w.srcBoxId, w.srcPinIdx), InPinId(w.dstBoxId, w.dstPinIdx), ImGui::ColorConvertU32ToFloat4(col), 2.0f);
         if (w.kind == Field::ValueKind::Pulse)
            ed::Flow(w.id);
      }
      if (p.placeAll)
      {
         p.placeAll = false;
         if (p.navigate)
         {
            ed::NavigateToContent(0.0f);
            p.navigate = false;
         }
      }

      // New wires: output to input. A wire dropped on nothing opens the pick menu with that pin.
      if (ed::BeginCreate())
      {
         ed::PinId a, c;
         if (ed::QueryNewLink(&a, &c))
         {
            int ab, ap, cb, cp;
            bool ao, co;
            DecodePin((int)a.Get(), ab, ap, ao);
            DecodePin((int)c.Get(), cb, cp, co);
            if (ao == co || ab == cb)
               ed::RejectNewItem();
            else if (ed::AcceptNewItem())
            {
               if (ao)
                  Connect(p, ab, ap, cb, cp);
               else
                  Connect(p, cb, cp, ab, ap);
               changed = true;
            }
         }
         ed::PinId from;
         if (ed::QueryNewNode(&from) && ed::AcceptNewItem())
         {
            s.pendingFromPin = (int)from.Get();
            s.pendingAt = ed::ScreenToCanvas(ImGui::GetMousePos());
            ed::Suspend();
            ImGui::OpenPopup("##fpPick");
            ed::Resume();
         }
      }
      ed::EndCreate();

      if (ed::BeginDelete())
      {
         ed::LinkId l;
         while (ed::QueryDeletedLink(&l))
            if (ed::AcceptDeletedItem())
            {
               auto& w = p.graph.wires;
               w.erase(std::remove_if(w.begin(), w.end(), [&](const Field::BoxWire& x) { return x.id == (int)l.Get(); }), w.end());
               changed = true;
            }
         ed::NodeId n;
         while (ed::QueryDeletedNode(&n))
            if (ed::AcceptDeletedItem())
            {
               RemoveBox(p, (int)n.Get());
               changed = true;
            }
      }
      ed::EndDelete();

      // Moves: a drag that ends with a box somewhere new is one header-only change.
      if (!changed && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
      {
         for (Field::BoxNode& b : p.graph.boxes)
         {
            const ImVec2 at = ed::GetNodePosition(b.id);
            if (std::fabs(at.x - b.x) > 0.5f || std::fabs(at.y - b.y) > 0.5f)
            {
               b.x = at.x;
               b.y = at.y;
               b.hasExplicitPos = true;
               changed = true;
            }
         }
      }

      p.hoverItem = ed::GetHoveredNode().Get() != 0 || ed::GetHoveredPin().Get() != 0 || ed::GetHoveredLink().Get() != 0;
      // A double-click on empty canvas opens the box search there, as on the main canvas.
      const bool dblPick = !p.hoverItem && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

      // Keys while the pointer is over the panel and no text field has the keyboard: copy, cut, paste, duplicate,
      // select all, delete.
      const ImGuiIO& io = ImGui::GetIO();
      if (!changed && gFieldPatcherClaimedKeys && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !io.WantTextInput)
      {
         const bool cmd = io.KeyCtrl || io.KeySuper || gFieldPatcherTestCmd;
         const std::vector<int> sel = SelectedIds(p);
         const ImVec2 mouse = ed::ScreenToCanvas(ImGui::GetMousePos());
         if (cmd && ImGui::IsKeyPressed(ImGuiKey_C, false) && !sel.empty())
            CopyBoxes(p, sel);
         else if (cmd && ImGui::IsKeyPressed(ImGuiKey_X, false) && !sel.empty())
         {
            CopyBoxes(p, sel);
            for (int id : sel)
               RemoveBox(p, id);
            changed = true;
         }
         else if (cmd && ImGui::IsKeyPressed(ImGuiKey_V, false) && !s.clipBoxes.empty())
            changed = PasteBoxes(p, mouse) > 0;
         else if (cmd && ImGui::IsKeyPressed(ImGuiKey_D, false) && !sel.empty())
         {
            CopyBoxes(p, sel);
            ImVec2 tl(1e9f, 1e9f);
            for (const Field::BoxNode& b : s.clipBoxes)
               tl = ImVec2(std::min(tl.x, b.x), std::min(tl.y, b.y));
            changed = PasteBoxes(p, ImVec2(tl.x + 40.0f, tl.y + 40.0f)) > 0;
         }
         else if (cmd && ImGui::IsKeyPressed(ImGuiKey_A, false))
            SelectAllBoxes(p);
         else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
         {
            // Selected wires go too, as on the main canvas.
            std::vector<ed::LinkId> links((size_t)std::max(1, ed::GetSelectedObjectCount()));
            const int nl = ed::GetSelectedLinks(links.data(), (int)links.size());
            auto& w = p.graph.wires;
            for (int i = 0; i < nl; i++)
            {
               const int lid = (int)links[i].Get();
               w.erase(std::remove_if(w.begin(), w.end(), [lid](const Field::BoxWire& x) { return x.id == lid; }), w.end());
            }
            for (int id : sel)
               RemoveBox(p, id);
            changed = nl > 0 || !sel.empty();
            if (changed)
               ed::ClearSelection();
         }
         else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            ed::ClearSelection();
         else if (io.KeyShift && !cmd && ImGui::IsKeyPressed(ImGuiKey_Y, false))
            ed::NavigateToContent(0.2f); // Shift+Y fits the view, as on the main canvas
      }

      ed::Suspend();
      ed::NodeId ctxNode;
      ed::LinkId ctxLink;
      if (ed::ShowNodeContextMenu(&ctxNode))
      {
         s.ctxBox = (int)ctxNode.Get();
         if (!ed::IsNodeSelected(ctxNode))
            ed::SelectNode(ctxNode, false);
         ImGui::OpenPopup("##fpNodeMenu");
      }
      else if (ed::ShowLinkContextMenu(&ctxLink))
      {
         s.ctxLink = (int)ctxLink.Get();
         ImGui::OpenPopup("##fpLinkMenu");
      }
      else if (ed::ShowBackgroundContextMenu() || dblPick)
      {
         s.pendingFromPin = 0;
         s.pendingAt = ed::ScreenToCanvas(ImGui::GetMousePos());
         ImGui::OpenPopup("##fpPick");
      }
      if (MenuParts::BeginPopup("##fpPick"))
      {
         if (PickMenu(p, h, s.pendingAt, s.pendingFromPin, -1))
            changed = false; // PickMenu committed
         MenuParts::EndPopup();
      }
      if (MenuParts::BeginPopup("##fpNodeMenu"))
      {
         const std::vector<int> sel = SelectedIds(p);
         if (MenuParts::Item(T("Duplicate"), nullptr, false, !sel.empty()))
         {
            CopyBoxes(p, sel);
            ImVec2 tl(1e9f, 1e9f);
            for (const Field::BoxNode& b : s.clipBoxes)
               tl = ImVec2(std::min(tl.x, b.x), std::min(tl.y, b.y));
            if (PasteBoxes(p, ImVec2(tl.x + 40.0f, tl.y + 40.0f)) > 0)
               Commit(p, h, "Duplicate Boxes"), changed = false;
         }
         if (MenuParts::Item(T("Copy"), nullptr, false, !sel.empty()))
            CopyBoxes(p, sel);
         if (MenuParts::Item(T("Cut"), nullptr, false, !sel.empty()))
         {
            CopyBoxes(p, sel);
            for (int id : sel)
               RemoveBox(p, id);
            changed = true;
         }
         MenuParts::Separator();
         if (MenuParts::Item(T("Disconnect")))
         {
            for (int id : sel)
            {
               auto& w = p.graph.wires;
               w.erase(std::remove_if(w.begin(), w.end(), [id](const Field::BoxWire& x) { return x.srcBoxId == id || x.dstBoxId == id; }), w.end());
            }
            changed = true;
         }
         if (MenuParts::Item(T("Delete")))
         {
            for (int id : sel)
               RemoveBox(p, id);
            changed = true;
         }
         MenuParts::EndPopup();
      }
      if (MenuParts::BeginPopup("##fpLinkMenu"))
      {
         if (MenuParts::Item(T("Delete Wire")))
         {
            auto& w = p.graph.wires;
            const int lid = s.ctxLink;
            w.erase(std::remove_if(w.begin(), w.end(), [lid](const Field::BoxWire& x) { return x.id == lid; }), w.end());
            changed = true;
         }
         MenuParts::EndPopup();
      }
      ed::Resume();

      // The box under selection, for the inspector.
      ed::End();
      if (changed)
      {
         for (Field::BoxNode& b : p.graph.boxes)
         {
            const ImVec2 at = ed::GetNodePosition(b.id);
            if (at.x < 1e8f)
            {
               b.x = at.x;
               b.y = at.y;
               b.hasExplicitPos = true;
            }
         }
         Commit(p, h, "Edit Boxes");
      }
      ed::SetCurrentEditor(prev);
   }

   int SelectedBox(Page& p)
   {
      if (p.ctx == nullptr)
         return 0;
      ed::EditorContext* prev = ed::GetCurrentEditor();
      ed::SetCurrentEditor(p.ctx);
      ed::NodeId ids[2];
      const int n = ed::GetSelectedNodes(ids, 2);
      ed::SetCurrentEditor(prev);
      return n == 1 ? (int)ids[0].Get() : 0;
   }

   // ---- the family rail (§8.4)

   const char* FamilyGlyph(int f)
   {
      static const char* kGlyphs[] = { IconsInfinite::FamilySource, IconsInfinite::Plus, IconsInfinite::Range, IconsInfinite::Waveform,
                                       IconsInfinite::Bolt, IconsInfinite::Metronome, IconsInfinite::Spark, IconsInfinite::Cube,
                                       IconsInfinite::Piano, IconsInfinite::GridDots };
      return kGlyphs[std::clamp(f, 0, 9)];
   }

   void DrawRail(Page& p, Host& h, float height)
   {
      State& s = S();
      ImGui::BeginChild("##fpRail", ImVec2(tok::control_h, height), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
      for (int f = 0; f < (int)Field::BoxFamily::Count; f++)
      {
         ImGui::PushID(f);
         if (IconTile::Draw("##fam", FamilyGlyph(f), FamilyGlyph(f), s.railFamily == f, tok::control_h, tok::control_h))
         {
            s.railFamily = f;
            s.search[0] = 0;
            ImGui::OpenPopup("##fpFamily");
         }
         if (ImGui::IsItemHovered())
            HelpTip("%s", T(Field::BoxFamilyName((Field::BoxFamily)f)));
         if (MenuParts::BeginPopup("##fpFamily"))
         {
            ImVec2 at(0, 0);
            if (p.ctx != nullptr)
            {
               ed::EditorContext* prev = ed::GetCurrentEditor();
               ed::SetCurrentEditor(p.ctx);
               // The middle of this page's canvas, not of the window: a docked panel's canvas is far from it.
               at = ed::ScreenToCanvas(ImVec2((p.canvasMin.x + p.canvasMax.x) * 0.5f, (p.canvasMin.y + p.canvasMax.y) * 0.5f));
               ed::SetCurrentEditor(prev);
            }
            PickMenu(p, h, at, 0, f);
            MenuParts::EndPopup();
         }
         else if (s.railFamily == f)
            s.railFamily = -1;
         ImGui::PopID();
      }
      ImGui::EndChild();
   }

   // ---- inspector: Code and Box

   void DrawInspector(Page& p, Host& h, ImVec2 size)
   {
      State& s = S();
      ImGui::BeginChild("##fpInspector", size, false, ImGuiWindowFlags_NoScrollbar);
      UiDensity::Compact compact;
      if (ChipButton::Draw(T("Code"), s.inspectorTab == 0))
         s.inspectorTab = 0;
      FormParts::Inline(0.0f, tok::space_1);
      if (ChipButton::Draw(T("Box"), s.inspectorTab == 1))
         s.inspectorTab = 1;
      const std::string src = SourceOf(p, h);
      if (s.inspectorTab == 0)
      {
         if (!s.codeEditing && s.codeBufFor != src)
         {
            s.codeBuf = src;
            s.codeBufFor = src;
         }
         const ImVec2 avail = ImGui::GetContentRegionAvail();
         FieldWell::InputTextMultiline("##fpCode", &s.codeBuf, ImVec2(avail.x, std::max(60.0f, avail.y)), ImGuiInputTextFlags_WordWrap);
         s.codeEditing = ImGui::IsItemActive();
         if (ImGui::IsItemDeactivatedAfterEdit() && s.codeBuf != src)
         {
            SetSource(p, h, s.codeBuf);
            s.codeBufFor = s.codeBuf;
         }
      }
      else
      {
         const int id = SelectedBox(p);
         Field::BoxNode* b = id != 0 ? p.graph.FindBox(id) : nullptr;
         if (b == nullptr)
            EmptyState::DrawInWindow(T("No box selected"), T("Click a box"));
         else
         {
            const Field::BoxSpec* spec = Field::FindBoxSpec(b->specId);
            FormParts::Section(BoxTitle(*b));
            if (spec != nullptr)
            {
               ImGui::TextDisabled("%s", spec->fieldWord.c_str());
               if (!spec->aliases.empty())
               {
                  std::string a;
                  for (const std::string& x : spec->aliases)
                     a += (a.empty() ? "" : ", ") + x;
                  ImGui::TextDisabled("%s", a.c_str());
               }
            }
            if (b->specId == "code")
            {
               const ImVec2 avail = ImGui::GetContentRegionAvail();
               FieldWell::InputTextMultiline("##fpRaw", &b->rawCode, ImVec2(avail.x, std::max(60.0f, avail.y)), ImGuiInputTextFlags_WordWrap);
               if (ImGui::IsItemDeactivatedAfterEdit())
                  Commit(p, h, "Edit Code Box");
            }
            else if (b->specId == "knob")
            {
               bool edited = false;
               FieldWell::InputText(T("name"), &b->varName);
               edited |= ImGui::IsItemDeactivatedAfterEdit();
               float lo = (float)b->minVal, hi = (float)b->maxVal, d = (float)b->numberValue;
               ImGui::DragFloat(T("default"), &d, 0.01f);
               edited |= ImGui::IsItemDeactivatedAfterEdit();
               ImGui::DragFloat(T("min"), &lo, 0.01f);
               edited |= ImGui::IsItemDeactivatedAfterEdit();
               ImGui::DragFloat(T("max"), &hi, 0.01f);
               edited |= ImGui::IsItemDeactivatedAfterEdit();
               b->numberValue = d;
               b->minVal = lo;
               b->maxVal = std::max(lo, hi);
               if (edited)
                  Commit(p, h, "Edit Knob");
            }
            else if (b->specId == "swizzle" || b->specId == "reduce")
            {
               FieldWell::InputText(T("field"), &b->swizzleField);
               if (ImGui::IsItemDeactivatedAfterEdit())
                  Commit(p, h, "Edit Box");
            }
            else if (b->specId == "number")
            {
               float v = (float)b->numberValue;
               ImGui::DragFloat(T("value"), &v, 0.01f);
               if (ImGui::IsItemEdited())
               {
                  b->numberValue = v;
                  char buf[32];
                  std::snprintf(buf, sizeof(buf), "%g", v);
                  b->literalText = buf;
               }
               if (ImGui::IsItemDeactivatedAfterEdit())
                  Commit(p, h, "Edit Number");
            }
         }
      }
      compact.End(); // before the child closes: the pushes were made inside it
      ImGui::EndChild();
   }

   // ---- scratch export (§8.10)

   struct ExportTarget
   {
      const char* typeName = nullptr;
      const char* category = nullptr;
      const char* label = nullptr;
      bool valueMode = false;
   };

   ExportTarget TargetOf(const Field::BoxGraph& g, uint32_t mask = Field::HostPrimitive)
   {
      if (mask & Field::HostPixel)
         return { "FieldPixel", "Source", "Field Pixel", false };
      if (mask & Field::HostSample)
         return { "Field Effect", "AudioEffects", "Field Effect", false };
      bool notes = false, geo = false, value = false;
      for (const Field::BoxNode& b : g.boxes)
      {
         notes = notes || b.specId == "play_note";
         geo = geo || b.specId == "position_out" || b.specId == "normal_out" || b.specId == "color_out";
         value = value || b.specId == "value_out" || b.specId == "pulse_out";
      }
      if (notes)
         return { "Field Notes", "Notes", "Field Notes", false };
      if (geo)
         return { "Field Primitive", "3D", "Field Primitive", false };
      if (value)
         return { "Field Modulator", "Modulators", "Field Modulator", true };
      return {};
   }

   void Export(Page& p)
   {
      const ExportTarget t = TargetOf(p.graph, ScratchMask(p.scratchCode));
      if (t.typeName == nullptr)
         return;
      PushUndoCheckpoint("Export Field Page");
      const ImVec2 at = FindFreeSpawnPosition(gViewCenterCanvas);
      GraphNode* gn = SpawnNode(t.typeName, t.category, at.x, at.y);
      if (gn == nullptr)
         return;
      if (auto* n = dynamic_cast<FieldPrimitiveNode*>(gn->node.get()))
      {
         n->code = p.scratchCode;
         n->valueMode = t.valueMode;
         if (t.valueMode)
            n->publishScalarOutput = true;
         n->Apply();
      }
      else if (auto* n = dynamic_cast<FieldPixelNode*>(gn->node.get()))
      {
         n->code = p.scratchCode;
         n->Apply();
      }
      else if (auto* n = dynamic_cast<FieldSampleNode*>(gn->node.get()))
      {
         n->code = p.scratchCode;
         n->Apply();
      }
      else if (auto* n = dynamic_cast<FieldNotesNode*>(gn->node.get()))
      {
         n->code = p.scratchCode;
         for (const Field::BoxNode& b : p.graph.boxes)
            n->publishScalarOutput = n->publishScalarOutput || b.specId == "value_out" || b.specId == "pulse_out";
         n->Apply();
      }
      p.uid = gn->uid;
      if (!p.scratchName.empty())
         S().nodeNames[gn->uid] = p.scratchName; // the tab keeps "Slow LFO", not "Field Primitive 3"
      p.scratchCode.clear();
      p.parsedFrom.clear();
   }

   // ---- header strip and footer

   std::string PageTitle(Page& p)
   {
      if (p.uid != 0)
      {
         auto it = S().nodeNames.find(p.uid);
         if (it != S().nodeNames.end())
            return it->second;
      }
      if (p.uid == 0)
         return p.scratchName;
      GraphNode* gn = FindNodeByUid(p.uid);
      std::string name = gn != nullptr ? gn->typeName : std::string("?");
      return std::string(T(name.c_str())) + " " + std::to_string(gn != nullptr ? gn->index : 0);
   }

   // One strip-high text line: the text sits on the chips' centre line, not on their top edge.
   void RowText(const char* fmt, ...) IM_FMTARGS(1);
   void RowText(const char* fmt, ...)
   {
      char buf[512];
      va_list args;
      va_start(args, fmt);
      std::vsnprintf(buf, sizeof(buf), fmt, args);
      va_end(args);
      const float dy = std::max(0.0f, (ChipButton::Height() - ImGui::GetTextLineHeight()) * 0.5f);
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + dy);
      ImGui::TextDisabled("%s", buf);
      ImGui::SameLine(0.0f, tok::space_2);
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() - dy);
   }

   void DrawStrip()
   {
      State& s = S();
      // The shared page strip: same menu, same order as every paged panel. Middle-click closes
      // a page; Pin is the pin tile at the strip's end.
      std::vector<PagePicker::Entry> entries;
      entries.reserve(s.pages.size());
      for (auto& pp : s.pages)
      {
         PagePicker::Entry e;
         e.name = PageTitle(*pp) + (pp->pinned ? " \xE2\x80\xA2" : "");
         e.color = pp->color;
         entries.push_back(std::move(e));
      }
      PagePicker::Options opt;
      opt.maxW = std::max(1.0f, ImGui::GetContentRegionAvail().x - 3.0f * (tok::control_h + tok::space_1) - 8.0f);
      opt.canClose = true;
      int closeAt = -1;
      {
         // The rename field starts from the bare title, without the pin mark.
         std::vector<PagePicker::Entry> bare = entries;
         for (size_t i = 0; i < bare.size(); i++)
            bare[i].name = PageTitle(*s.pages[i]);
         PagePicker::RenameKey("##fpPages", bare, s.front);
      }
      const PagePicker::Result r = PagePicker::Draw("##fpPages", entries, s.front, T("New scratch page"), opt);
      Page* rp = r.index >= 0 && r.index < (int)s.pages.size() ? s.pages[r.index].get() : nullptr;
      switch (r.act)
      {
      case PagePicker::Act::Select: s.front = r.index; break;
      case PagePicker::Act::Add: ImGui::OpenPopup("##fpNew"); break;
      case PagePicker::Act::Rename:
         if (rp->uid == 0)
            rp->scratchName = r.name;
         else
            s.nodeNames[rp->uid] = r.name;
         break;
      case PagePicker::Act::Recolor: rp->color = r.color; break;
      case PagePicker::Act::Duplicate:
      {
         const std::string code = rp->uid == 0 ? rp->scratchCode : rp->parsedFrom;
         const unsigned int col = rp->color;
         Page& d = AddScratch(PageTitle(*rp) + " " + T("copy"), code);
         d.color = col;
         break;
      }
      case PagePicker::Act::Move:
      {
         // The page in front stays in front, wherever it lands.
         const Page* front = s.front >= 0 && s.front < (int)s.pages.size() ? s.pages[s.front].get() : nullptr;
         auto moved = std::move(s.pages[r.index]);
         s.pages.erase(s.pages.begin() + r.index);
         s.pages.insert(s.pages.begin() + r.to, std::move(moved));
         for (int i = 0; i < (int)s.pages.size(); i++)
            if (s.pages[i].get() == front)
               s.front = i;
         break;
      }
      case PagePicker::Act::Delete:   // one behaviour for every page: it leaves the strip, the canvas node stays
         ClosePage(r.index);
         break;
      case PagePicker::Act::Close: closeAt = r.index; break;
      default: break;
      }
      if (closeAt >= 0)
         ClosePage(closeAt);
      if (MenuParts::BeginPopup("##fpNew"))
      {
         if (MenuParts::Item(T("Empty page")))
            AddScratch("", "");
         MenuParts::Separator();
         for (int k = 0; k < Field::kPatcherStarterCount; k++)
         {
            if (k > 0 && k % (Field::kPatcherStarterCount / 5) == 0)
               MenuParts::Separator();
            if (MenuParts::Item(T(Field::kPatcherStarters[k].name)))
               FieldPatcherOpenStarter(k);
         }
         MenuParts::EndPopup();
      }
      FormParts::Inline(0.0f, tok::space_1);
      if (Page* fp = Front())
      {
         // Pin: the front page stays put when the canvas selection changes.
         if (IconTile::Draw("##fpPin", IconsInfinite::Lock, IconsInfinite::Lock, fp->pinned, ChipButton::Height()))
            fp->pinned = !fp->pinned;
         if (ImGui::IsItemHovered())
            HelpTip("%s", fp->pinned ? T("Unpin page") : T("Pin page"));
         FormParts::Inline(0.0f, tok::space_1);
      }
      if (IconTile::Draw("##fpMax", IconsInfinite::PopOut, IconsInfinite::PopOut, gFieldPatcherMaximised, ChipButton::Height()))
         gFieldPatcherMaximised = !gFieldPatcherMaximised;
      if (ImGui::IsItemHovered())
         HelpTip("%s", T("Maximise (Esc returns)"));
      // The free strip to the right: right-click for the dock switch.
      FormParts::Inline(0.0f, tok::space_1);
      const float freeW = ImGui::GetContentRegionAvail().x;
      if (freeW > 8.0f)
      {
         ImGui::InvisibleButton("##fpStripFree", ImVec2(freeW, ChipButton::Height()));
         if (MenuParts::BeginContextItem("##fpStripMenu"))
         {
            DockItem();
            MenuParts::EndPopup();
         }
      }
   }

   void DrawFooter(Page& p, Host& h)
   {
      const int boxes = (int)p.graph.boxes.size();
      const int codeBoxes = p.graph.CountCodeBoxes();
      const ImVec4 warn = ImGui::ColorConvertU32ToFloat4(ImGui::GetColorU32(ImGuiCol_PlotHistogramHovered));
      const float dy = std::max(0.0f, (ChipButton::Height() - ImGui::GetTextLineHeight()) * 0.5f);
      const float y0 = ImGui::GetCursorPosY();
      // Every item of the row shares one centre line: chips are kHeight tall, text is nudged to match.
      auto text = [&](const char* t, const ImVec4* col = nullptr) {
         ImGui::SetCursorPosY(y0 + dy);
         if (col != nullptr)
            ImGui::TextColored(*col, "%s", t);
         else
            ImGui::TextDisabled("%s", t);
         ImGui::SameLine(0.0f, tok::space_3);
      };
      ImGui::SetCursorPosY(y0);
      if (p.uid == 0)
      {
         const ExportTarget t = TargetOf(p.graph, ScratchMask(p.scratchCode));
         const std::string err = CompileCheck(p.scratchCode);
         if (!err.empty())
            text(err.c_str(), &warn);
         else if (t.typeName != nullptr)
         {
            const std::string chip = std::string(T(t.label)) + "##fpExport";
            ImGui::SetCursorPosY(y0);
            if (ChipButton::Draw(chip.c_str(), true))
               Export(p);
            if (ImGui::IsItemHovered())
               HelpTip("%s", T("Place this page as a node"));
            ImGui::SameLine(0.0f, tok::space_3);
         }
         else
            text(T("Add an outlet to make a node"));
      }
      else
      {
         text(T(h.nodeName));
         if (h.error.empty())
            text("✓");
         else
         {
            const std::string e = std::string(T("last good ·")) + " " + h.error;
            text(e.c_str(), &warn);
         }
      }
      char count[96];
      if (codeBoxes > 0)
         std::snprintf(count, sizeof(count), "%d %s · %d %s", boxes, T("boxes"), codeBoxes, T("code"));
      else
         std::snprintf(count, sizeof(count), "%d %s", boxes, T("boxes"));
      text(count);
      if (!p.parseError.empty())
         text(p.parseError.c_str(), &warn);
      // The free footer: right-click for the dock switch.
      ImGui::SetCursorPosY(y0);
      const float freeW = ImGui::GetContentRegionAvail().x;
      if (freeW > 8.0f)
      {
         ImGui::InvisibleButton("##fpFooterFree", ImVec2(freeW, ChipButton::Height()));
         if (MenuParts::BeginContextItem("##fpFooterMenu"))
         {
            DockItem();
            MenuParts::EndPopup();
         }
      }
      else
         ImGui::NewLine();
   }

   // No page: a bare canvas, as Max and Pd open on an empty patcher. Double-click starts an empty page; right-click
   // lists the starters by kind.
   void DrawEmpty()
   {
      const ImVec2 avail = ImGui::GetContentRegionAvail();
      const ImVec2 mn = ImGui::GetCursorScreenPos();
      const ImVec2 mx(mn.x + std::max(1.0f, avail.x), mn.y + std::max(1.0f, avail.y));
      ImU32 bg = ImGui::GetColorU32(ImGuiCol_WindowBg);
      if (gEditor != nullptr)
      {
         ed::EditorContext* prev = ed::GetCurrentEditor();
         ed::SetCurrentEditor(gEditor);
         bg = ImGui::GetColorU32(ed::GetStyle().Colors[ed::StyleColor_Bg]);
         ed::SetCurrentEditor(prev);
      }
      ImGui::GetWindowDrawList()->AddRectFilled(mn, mx, bg, tok::radius_container);
      ImGui::InvisibleButton("##fpEmptyCanvas", ImVec2(mx.x - mn.x, mx.y - mn.y));
      if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
         AddScratch("", "");
      if (MenuParts::BeginContextItem("##fpEmptyMenu"))
      {
         if (MenuParts::Item(T("Empty page")))
            AddScratch("", "");
         MenuParts::Separator();
         static const char* kKinds[] = { "Primitive", "Modulator", "Notes", "Pixel", "Effect" };
         constexpr int kKindCount = 5;
         const int perKind = Field::kPatcherStarterCount / kKindCount;
         for (int c = 0; c < kKindCount; c++)
         {
            if (!MenuParts::SubMenu(T(kKinds[c])))
               continue;
            for (int k = c * perKind; k < Field::kPatcherStarterCount && (k < (c + 1) * perKind || c == kKindCount - 1); k++)
               if (MenuParts::Item(T(Field::kPatcherStarters[k].name)))
                  FieldPatcherOpenStarter(k);
            ImGui::EndMenu();
         }
         MenuParts::Separator();
         DockItem();
         MenuParts::EndPopup();
      }
   }

   void DrawPanel()
   {
      gFieldPatcherClaimedKeys = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !ImGui::GetIO().WantTextInput;
      if (gFieldPatcherMaximised && gFieldPatcherClaimedKeys && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
         gFieldPatcherMaximised = false;
      PrunePages();
      FollowCanvasSelection();
      DrawStrip();
      Page* p = Front();
      if (p == nullptr)
      {
         DrawEmpty();
         return;
      }
      Host h = HostOf(p->uid != 0 ? FindNodeByUid(p->uid) : nullptr);
      const std::string src = SourceOf(*p, h);
      if (src != p->parsedFrom)
         Reparse(*p, src, MaskOf(*p, h));

      const float footerH = ImGui::GetFrameHeightWithSpacing();
      const ImVec2 avail = ImGui::GetContentRegionAvail();
      const float bodyH = std::max(1.0f, avail.y - footerH);
      const float gap = tok::space_2;
      DrawRail(*p, h, bodyH);
      FormParts::Inline(0.0f, gap);
      const float inspW = std::min(kInspectorW, avail.x * 0.35f);
      const float canvasW = std::max(80.0f, avail.x - tok::control_h - inspW - 2.0f * gap);
      ImGui::BeginChild("##fpCanvasWrap", ImVec2(canvasW, bodyH), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
      DrawCanvas(*p, h, ImVec2(canvasW, bodyH));
      ImGui::EndChild();
      FormParts::Inline(0.0f, gap);
      DrawInspector(*p, h, ImVec2(inspW, bodyH));
      DrawFooter(*p, h);
   }
}

void FieldPatcherOpenNode(uint64_t uid)
{
   gFieldPatcherOpen = true;
   ShowNodePage(uid);
   if (Page* p = Front())
      p->pinned = false;
}

void FieldPatcherOpenStarter(int index)
{
   if (index < 0 || index >= Field::kPatcherStarterCount)
      return;
   gFieldPatcherOpen = true;
   const Field::PatcherStarter& st = Field::kPatcherStarters[index];
   AddScratch(T(st.name), st.code);
}

// Saved with the patch: one line per scratch page (name, then the source with \n and \\ escaped), pins, and page colours.
std::string FieldPatcherSaveState()
{
   auto esc = [](const std::string& x) {
      std::string o;
      for (char c : x)
      {
         if (c == '\\')
            o += "\\\\";
         else if (c == '\n')
            o += "\\n";
         else if (c == '\t')
            o += "\\t";
         else
            o += c;
      }
      return o;
   };
   std::string out;
   for (const auto& p : S().pages)
   {
      if (p->uid == 0)
         out += "s\t" + esc(p->scratchName) + "\t" + esc(p->scratchCode) + "\n";
      else if (p->pinned)
         out += "p\t" + std::to_string(p->uid) + "\n";
      else
         continue;
      if (p->color != 0)   // the chip colour of the page on the line above
         out += "c\t" + std::to_string(p->color) + "\n";
   }
   for (const auto& kv : S().nodeNames)
      out += "r\t" + std::to_string(kv.first) + "\t" + esc(kv.second) + "\n";
   return out;
}

void FieldPatcherLoadState(const std::string& text)
{
   State& s = S();
   for (auto& p : s.pages)
      DropContext(*p);
   s.pages.clear();
   s.front = -1;
   s.closed.clear();
   s.nodeNames.clear();
   auto unesc = [](const std::string& x) {
      std::string o;
      for (size_t i = 0; i < x.size(); i++)
      {
         if (x[i] == '\\' && i + 1 < x.size())
         {
            const char c = x[++i];
            o += c == 'n' ? '\n' : (c == 't' ? '\t' : c);
         }
         else
            o += x[i];
      }
      return o;
   };
   Page* last = nullptr;   // the page a "c" line colours
   size_t at = 0;
   while (at < text.size())
   {
      size_t nl = text.find('\n', at);
      if (nl == std::string::npos)
         nl = text.size();
      const std::string line = text.substr(at, nl - at);
      at = nl + 1;
      if (line.size() > 2 && line[0] == 's' && line[1] == '\t')
      {
         const size_t tab = line.find('\t', 2);
         if (tab == std::string::npos)
            continue;
         last = &AddScratch(unesc(line.substr(2, tab - 2)), unesc(line.substr(tab + 1)));
      }
      else if (line.size() > 2 && line[0] == 'c' && line[1] == '\t')
      {
         if (last != nullptr)
            last->color = (unsigned int)std::strtoul(line.c_str() + 2, nullptr, 10);
      }
      else if (line.size() > 2 && line[0] == 'r' && line[1] == '\t')
      {
         const size_t tab = line.find('\t', 2);
         if (tab != std::string::npos)
            s.nodeNames[std::strtoull(line.c_str() + 2, nullptr, 10)] = unesc(line.substr(tab + 1));
      }
      else if (line.size() > 2 && line[0] == 'p' && line[1] == '\t')
      {
         const uint64_t uid = std::strtoull(line.c_str() + 2, nullptr, 10);
         ShowNodePage(uid);
         last = Front();
         if (last != nullptr)
            last->pinned = true;
      }
   }
}

void DrawFieldPatcherDocked(const char* id, const ImVec2& size)
{
   const float kGrip = PanelFrame::kGap;
   const int dock = gFieldPatcherDock;
   const bool gripFirst = dock == 0;
   ImGui::BeginChild(id, size, false);
   gFieldPatcherRectMin = ImGui::GetWindowPos();
   gFieldPatcherRectMax = ImVec2(gFieldPatcherRectMin.x + ImGui::GetWindowWidth(), gFieldPatcherRectMin.y + ImGui::GetWindowHeight());
   gFieldPatcherClaimedKeys = false;
   const ImVec2 inner = ImGui::GetContentRegionAvail();
   auto grip = [&]() {
      ImGui::InvisibleButton("##fpgrip", ImVec2(std::max(1.0f, inner.x), kGrip));
      DrawPanelSeam(false, gripFirst);
      if (ImGui::IsItemHovered() || ImGui::IsItemActive())
         ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
      if (ImGui::IsItemActive())
      {
         const float d = ImGui::GetIO().MouseDelta.y;
         gFieldPatcherHeight += dock == 0 ? -d : d;
         gFieldPatcherHeight = std::max(kMinHeight, gFieldPatcherHeight);
      }
   };
   if (gripFirst && !gFieldPatcherMaximised)
      grip();
   const ImVec2 gap = ImGui::GetStyle().ItemSpacing;
   const float gripH = gFieldPatcherMaximised ? 0.0f : kGrip + gap.y;
   const ImVec2 cardSize(std::max(1.0f, inner.x), std::max(1.0f, inner.y - gripH));
   PanelFrame::Insets in;
   if (dock == 0)
      in.t = 0.0f;
   else
      in.b = 0.0f;
   PushDockedPanelStyle(/*isChild=*/true);
   PanelFrame::BeginCard("##fpcontent", cardSize, in, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
   PopDockedPanelStyle();
   DrawPanel();
   PanelFrame::EndCard();
   if (!gripFirst && !gFieldPatcherMaximised)
      grip();
   const ImVec2 savedSpacing = ImGui::GetStyle().ItemSpacing;
   ImGui::GetStyle().ItemSpacing = ImVec2(0.0f, 0.0f);
   ImGui::EndChild();
   ImGui::GetStyle().ItemSpacing = savedSpacing;
}
}

// Self-test (INFINITE_FIELDPATCHERTEST, FrameTests3.cpp): the gestures the panel makes, run on real nodes.
namespace app
{
bool gFieldPatcherTestCmd = false;

bool FieldPatcherTestCanvasCentre(ImVec2& out)
{
   Page* p = Front();
   if (p == nullptr || p->canvasMax.x <= p->canvasMin.x)
      return false;
   out = ImVec2((p->canvasMin.x + p->canvasMax.x) * 0.5f, (p->canvasMin.y + p->canvasMax.y) * 0.5f);
   return true;
}

void FieldPatcherTestCounts(int& boxes, int& wires)
{
   Page* p = Front();
   boxes = p != nullptr ? (int)p->graph.boxes.size() : -1;
   wires = p != nullptr ? (int)p->graph.wires.size() : -1;
}

void FieldPatcherTestSelect(int kind, int index)
{
   Page* p = Front();
   if (p == nullptr || p->ctx == nullptr)
      return;
   ed::EditorContext* prev = ed::GetCurrentEditor();
   ed::SetCurrentEditor(p->ctx);
   ed::ClearSelection();
   if (kind == 0 && index < (int)p->graph.boxes.size())
      ed::SelectNode(p->graph.boxes[index].id, false);
   if (kind == 1 && index < (int)p->graph.wires.size())
      ed::SelectLink(p->graph.wires[index].id, false);
   ed::SetCurrentEditor(prev);
}

int FieldPatcherSelfCheck()
{
   int fails = 0;
   auto expect = [&](const char* what, bool pass) {
      printf("FIELDPATCHER %s  %s\n", what, pass ? "OK" : "FAIL");
      fails += pass ? 0 : 1;
   };
   State& s = S();

   // 1. A node page: edit a number box, move a box; both reach the node's code, compile, and keep positions.
   GraphNode* prim = SpawnNode("Field Primitive", "3D", 0.0f, 0.0f);
   expect("spawned a Field Primitive", prim != nullptr);
   if (prim != nullptr)
   {
      auto* n = dynamic_cast<FieldPrimitiveNode*>(prim->node.get());
      n->code = Field::kPatcherStarters[StarterIndex("Spiral")].code;
      n->Apply();
      FieldPatcherOpenNode(prim->uid);
      Page* p = Front();
      Host h = HostOf(prim);
      Reparse(*p, *h.code, h.mask);
      expect("the page opens box-only", !p->graph.boxes.empty() && p->graph.CountCodeBoxes() == 0);
      Field::BoxNode* num = nullptr;
      for (Field::BoxNode& b : p->graph.boxes)
         if (b.specId == "number" && b.literalText == "2.39996")
            num = &b;
      expect("the golden-angle number is a box", num != nullptr);
      if (num != nullptr)
      {
         num->numberValue = 1.5;
         num->literalText = "1.5";
         num->x = 432.0f;
         num->y = 96.0f;
         num->hasExplicitPos = true;
         Commit(*p, h, "Edit Number");
         expect("the edit reached the code", n->code.find("1.5") != std::string::npos);
         expect("the code keeps the boxes header", n->code.rfind("# @boxes v1", 0) == 0);
         expect("it compiles", n->LastError().empty());
         bool kept = false;
         for (const Field::BoxNode& b : p->graph.boxes)
            kept = kept || (b.specId == "number" && b.literalText == "1.5" && std::fabs(b.x - 432.0f) < 0.5f && std::fabs(b.y - 96.0f) < 0.5f);
         expect("the moved box keeps its place after the round trip", kept);
      }
      // A broken gesture: the error shows and the code text keeps what was typed.
      const std::string good = n->code;
      SetSource(*p, h, good + "\nP = vec3(\n");
      h = HostOf(prim);
      expect("a broken program reports an error", !h.error.empty());
      SetSource(*p, h, good);
      expect("restoring it compiles again", n->LastError().empty());
   }

   // 2. Scratch pages export to the node their outlets need.
   const int before = (int)gNodes.size();
   FieldPatcherOpenStarter(StarterIndex("Melody from counting"));
   Page* mel = Front();
   Reparse(*mel, mel->scratchCode, ScratchMask(mel->scratchCode));
   expect("the melody starter targets Field Notes", TargetOf(mel->graph).typeName != nullptr &&
                                                     std::string(TargetOf(mel->graph).typeName) == "Field Notes");
   Export(*mel);
   GraphNode* notes = mel->uid != 0 ? FindNodeByUid(mel->uid) : nullptr;
   expect("export placed a Field Notes node", notes != nullptr && (int)gNodes.size() == before + 1 &&
                                               dynamic_cast<FieldNotesNode*>(notes->node.get()) != nullptr);
   if (notes != nullptr)
      expect("the exported melody compiles", dynamic_cast<FieldNotesNode*>(notes->node.get())->LastError().empty());

   FieldPatcherOpenStarter(StarterIndex("Slow LFO"));
   Page* light = Front();
   Reparse(*light, light->scratchCode, ScratchMask(light->scratchCode));
   Export(*light);
   GraphNode* val = light->uid != 0 ? FindNodeByUid(light->uid) : nullptr;
   auto* vp = val != nullptr ? dynamic_cast<FieldModulatorNode*>(val->node.get()) : nullptr;
   expect("a value-only page exports a Field Modulator", vp != nullptr && vp->valueMode && vp->publishScalarOutput);
   if (vp != nullptr)
   {
      expect("the modulator has no mesh", vp->GetMesh().vertices.empty());
      expect("the modulator's first outlet is its value", vp->ModulatorOutput(0) != nullptr && vp->OutputLabel(0) == std::string("value"));
      expect("the modulator is not a geometry outlet", !vp->IsGeometryOutputIndex(0));
      expect("the LFO compiles", vp->LastError().empty());
      vp->CookIfNeeded(987654);
      const float v = vp->Value01();
      expect("the LFO value is in 0..1", v >= 0.0f && v <= 1.0f);
   }

   // 3. Scratch pages and pins survive save and load, through the .inf text itself.
   AddScratch("Keep me", "x = 1\n\ty = x\n").color = 0x40A0E0u;
   const int pagesBefore = (int)s.pages.size();
   Patch::Data written;
   written.fieldPages = FieldPatcherSaveState();
   Patch::NodeRecord shape; // a patch needs one node to read back
   shape.index = 1;
   shape.category = "Source";
   shape.typeName = "Shape";
   written.nodes.push_back(shape);
   std::string inf;
   Patch::Data reread;
   std::string readErr;
   const bool textOk = Patch::WriteText(written, inf) && Patch::ReadText(inf, reread, readErr);
   expect("the patch text carries fieldpage lines", inf.find("fieldpage s\tKeep me") != std::string::npos);
   expect("fieldpage lines read back unchanged", textOk && reread.fieldPages == written.fieldPages);
   FieldPatcherLoadState(reread.fieldPages);
   bool found = false;
   for (const auto& p : s.pages)
      found = found || (p->uid == 0 && p->scratchName == "Keep me" && p->scratchCode == "x = 1\n\ty = x\n" && p->color == 0x40A0E0u);
   expect("a scratch page and its colour survive save and load", found);
   expect("pages come back in number", (int)s.pages.size() <= pagesBefore && !s.pages.empty());

   // 4. Every starter compiles box-only through the page path.
   for (int k = 0; k < Field::kPatcherStarterCount; k++)
   {
      Field::BoxGraph g;
      Field::FieldError err;
      const std::string dom = Field::kPatcherStarters[k].domain;
      const uint32_t mask = dom == "notes" ? Field::HostNotes : dom == "pixel" ? Field::HostPixel : dom == "sample" ? Field::HostSample : Field::HostPrimitive;
      expect((std::string("kind guessed: ") + Field::kPatcherStarters[k].name).c_str(), ScratchMask(Field::kPatcherStarters[k].code) == mask);
      const bool ok = Field::ParseCodeToBoxGraph(Field::kPatcherStarters[k].code, mask, g, err) && g.CountCodeBoxes() == 0;
      expect(Field::kPatcherStarters[k].name, ok);
   }

   // 5. Every starter, placed as a node: the right kind, it compiles, and the tab keeps the starter's name.
   for (int k = 0; k < Field::kPatcherStarterCount; k++)
   {
      const Field::PatcherStarter& st = Field::kPatcherStarters[k];
      FieldPatcherOpenStarter(k);
      Page* pg = Front();
      Reparse(*pg, pg->scratchCode, ScratchMask(pg->scratchCode));
      Export(*pg);
      GraphNode* gn = pg->uid != 0 ? FindNodeByUid(pg->uid) : nullptr;
      std::string err = "not placed";
      bool kind = false;
      if (gn != nullptr)
      {
         if (auto* fn = dynamic_cast<FieldNotesNode*>(gn->node.get()))
            err = fn->LastError(), kind = std::string(st.domain) == "notes";
         else if (auto* fp = dynamic_cast<FieldPrimitiveNode*>(gn->node.get()))
            err = fp->LastError(), kind = std::string(st.domain) == "element" && fp->valueMode == st.valueMode &&
                                          (dynamic_cast<FieldModulatorNode*>(fp) != nullptr) == st.valueMode;
         else if (auto* px = dynamic_cast<FieldPixelNode*>(gn->node.get()))
            err = px->LastError(), kind = std::string(st.domain) == "pixel";
         else if (auto* fs = dynamic_cast<FieldSampleNode*>(gn->node.get()))
            err = fs->LastError(), kind = std::string(st.domain) == "sample";
      }
      const std::string what = std::string("placed: ") + st.name + (err.empty() ? "" : " (" + err + ")");
      expect(what.c_str(), gn != nullptr && kind && err.empty() && PageTitle(*pg) == T(st.name));
   }
   return fails;
}
}
