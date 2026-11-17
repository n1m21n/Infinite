#pragma once
// Canvas Link: the one way a panel talks to the canvas (internal/plans/ui-system/canvas-link.md).
//   Pick  - arm a canvas click: the next eligible node clicked becomes the panel's source
//   Show  - select the linked node(s) and glide the canvas there
//   Add   - spawn a new node beside the view and show it
//   Unlink
// One armed pick at a time (arming in one panel cancels another). While armed the canvas dims every node the
// pick can't take, rings the hovered one, shows "Click a <kind>" at the top, and Esc / right-click cancels.
#include <cstdint>
#include <functional>
#include <string>
#include "imgui.h"

struct GraphNode;

namespace app
{

namespace CanvasLink
{
   enum class Verb { Pick, Show, Add, Unlink, Hide };

   const char* Label(Verb v);   // "Pick on Canvas", "Show on Canvas", "Add to Canvas", "Unlink", "Hide from Canvas"
   const char* Glyph(Verb v);   // one glyph per verb, the same in every panel

   enum class Target { Node, Param };

   struct Spec
   {
      std::string owner;                                   // who armed it, e.g. "pianoroll.page"
      std::string kind;                                    // what to click, in words: "Piano Roll", "node", "knob"
      Target target = Target::Node;
      std::function<bool(const GraphNode&)> accepts;       // Node: empty = any node
      std::function<void(uint64_t uid)> onPicked;          // Node: runs once, on the click (wrap your own undo step)
      std::function<bool(int nodeIndex, int paramIndex)> acceptsParam; // Param: empty = any control
      std::function<void(int nodeIndex, int paramIndex)> onPickedParam; // Param: runs once, on the click
      std::function<void()> onCancel;                      // Esc, right-click, Cancel, another pick armed
   };

   void Arm(Spec spec);
   void Cancel();
   bool Armed(const char* owner = nullptr);                // any pick, or this owner's
   const std::string& ArmedOwner();

   // For a panel that keeps its own "picking" flag: call every frame with the key it is picking for ("" = not
   // picking). Arms when the key appears or changes, cancels when it goes. Re-keying a site doesn't fire the old
   // key's onCancel, so the flag the panel just set survives.
   void Sync(const char* site, const std::string& key, const std::function<Spec()>& make);

   // The control under the pointer (index into gParamPinScreenList, -1 = none) and its ring; canvas space, inside
   // the editor scope. For the drag-onto-a-knob gesture, which is not an armed pick.
   int HoveredParam(const std::function<bool(int nodeIndex, int paramIndex)>& accepts);
   void RingParam(int pinIndex);

   // Inside the canvas editor scope, once per frame (StageSidePanels): dims, rings, banner, click, cancel.
   void CanvasHook();

   // A full-width card button: "Pick on Canvas" / armed "Click a <kind>..." (second press cancels).
   bool PickButton(const Spec& spec);
   // The same toggle as a small icon chip, for toolbars and field rows.
   bool PickChip(const char* id, const Spec& spec);
   // The same toggle as a menu row.
   bool PickMenuItem(const Spec& spec);

   bool ShowChip(const char* id, uint64_t uid);            // locate the linked node; disabled when uid is gone
   bool ShowMenuItem(uint64_t uid);
   void Show(uint64_t uid);
   void Show(std::initializer_list<uint64_t> uids);

   // The link field: the linked node's name (click = Choose list of eligible nodes, "None" first when allowNone),
   // then the Pick chip and the Show chip. Choosing and picking both call spec.onPicked (0 = None). Draws no label:
   // the caller's form column does. w <= 0 fills the row.
   bool Field(const char* id, uint64_t uid, const Spec& spec, bool allowNone, float w = 0.0f);

   // Spawn beside what is on screen and show it. Returns the new node (nullptr on failure).
   GraphNode* Add(const char* type, const char* category);

   // Self-test (INFINITE_CANVASLINKTEST): a node's screen rect as last drawn while armed.
   bool ScreenRectForTest(uint64_t uid, ImVec2& mn, ImVec2& mx);
   // Self-test: the screen centre of a node's first control row the armed knob pick takes.
   bool ParamScreenPosForTest(int nodeIndex, ImVec2& at, int& paramIndex);
}
}
