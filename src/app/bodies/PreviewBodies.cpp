// Comment, group, draw and preview node bodies (moved verbatim from main.cpp).
#include "app/ui/design/TokenColors.h"
#include "app/AppShared.h"

namespace app
{
   // Paintable preview: the Draw node turns its 1:1 preview into the canvas, so
   // you draw directly on the node rather than in a separate window.
   void DrawPaintablePreview(DrawNode* node)
   {
      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImGui::InvisibleButton("##canvas", ImVec2(kPreviewSize, kPreviewSize));

      const int w = node->GetOutputWidth();
      const int h = node->GetOutputHeight();
      float dw = kPreviewSize, dh = kPreviewSize;
      if (w > 0 && h > 0)
      {
         const float scale = kPreviewSize / (float)std::max(w, h);
         dw = w * scale;
         dh = h * scale;
      }
      const ImVec2 tl(origin.x + (kPreviewSize - dw) * 0.5f, origin.y + (kPreviewSize - dh) * 0.5f);

      if (ImGui::IsItemActive())
      {
         const ImVec2 m = ImGui::GetIO().MousePos;
         const float u = (m.x - tl.x) / std::max(1.0f, dw);
         const float v = 1.0f - (m.y - tl.y) / std::max(1.0f, dh); // texture space is bottom-up
         if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            node->BeginStroke(u, v);
         else
            node->ContinueStroke(u, v);
      }
      else if (ImGui::IsItemDeactivated())
      {
         node->EndStroke();
      }

      ImDrawList* dl = ImGui::GetWindowDrawList();
      DrawCheckerboardBackdrop(dl, origin, kPreviewSize);
      if (node->GetOutputTexture() != 0)
         dl->AddImage((ImTextureID)(intptr_t)node->GetOutputTexture(), tl,
                      ImVec2(tl.x + dw, tl.y + dh), ImVec2(0, 1), ImVec2(1, 0));
      dl->AddRect(origin, ImVec2(origin.x + kPreviewSize, origin.y + kPreviewSize),
                  tok::U32(tok::pal::c_5A82BEFF), 4.0f, 0, 2.0f);
   }


   // A comment shows its note on the face of the node, not behind the params
   // (eye) toggle - the whole point of a note is to be readable without an
   inline float CommentFontSize(int sizeIdx, float baseFontSize)
   {
      switch (sizeIdx)
      {
         case 0: return baseFontSize * 0.82f;  // Small (~13px)
         case 1: default: return baseFontSize; // Normal (~15px)
         case 2: return baseFontSize * 1.35f;  // Large (~20px)
         case 3: return baseFontSize * 1.85f;  // Extra Large (~28px)
      }
   }

   float CommentFontScale(int sizeIdx)
   {
      switch (sizeIdx)
      {
         case 0: return 0.82f;
         case 1: default: return 1.0f;
         case 2: return 1.35f;
         case 3: return 1.85f;
      }
   }


   // extra click, the same reasoning DrawNode's canvas is drawn directly
   // rather than collapsed.
   //
   // The text is painted into the node's draw list rather than sitting in an
   // InputTextMultiline, which cannot go here at all: a multiline field is an
   // ImGui child window, and a child window is the one thing the canvas
   // transform cannot carry, so the note came out at the canvas origin instead
   // of on the node. Double-click opens the real editor as a popup, out in
   // screen space, exactly like the colour picker and the dropdowns.
   void DrawCommentPreview(CommentNode* n)
   {
      const float w = std::max(120.0f, n->width);
      const float h = std::max(60.0f, n->height);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);

      // Reserved first: this is what the node measures itself against and what
      // the double-click/hover/type hit-tests.
      ImGui::Dummy(ImVec2(w, h));
      const bool hovered = ImGui::IsItemHovered();

      const bool isLight = IsThemeLight();
      const ImU32 bgCol = isLight
         ? IM_COL32((int)(224 + n->color[0] * 28),
                    (int)(227 + n->color[1] * 25),
                    (int)(232 + n->color[2] * 20), 245)
         : IM_COL32((int)(16 + n->color[0] * 36),
                    (int)(18 + n->color[1] * 36),
                    (int)(24 + n->color[2] * 36), 235);
      const ImU32 borderCol = isLight
         ? IM_COL32((int)(n->color[0] * 180 + 40),
                    (int)(n->color[1] * 180 + 40),
                    (int)(n->color[2] * 180 + 40), 160)
         : IM_COL32((int)(n->color[0] * 200 + 55),
                    (int)(n->color[1] * 200 + 55),
                    (int)(n->color[2] * 200 + 55), 180);

      dl->AddRectFilled(origin, br, bgCol, 6.0f);
      // Header accent bar (Apple Notes / sticky card feel)
      dl->AddRectFilled(origin, ImVec2(br.x, origin.y + 4.0f),
                        IM_COL32((int)(n->color[0] * 255), (int)(n->color[1] * 255),
                                 (int)(n->color[2] * 255), 220), 6.0f, ImDrawFlags_RoundCornersTop);
      dl->AddRect(origin, br, borderCol, 6.0f, 0, 1.2f);

      const ImU32 textCol = isLight
         ? tok::U32(tok::pal::c_1E2430FF)
         : IM_COL32((int)((n->color[0] * 0.5f + 0.5f) * 255),
                    (int)((n->color[1] * 0.5f + 0.5f) * 255),
                    (int)((n->color[2] * 0.5f + 0.5f) * 255), 255);
      // Clipped to the box so a note longer than its height is cut off at the
      // edge instead of spilling over the card.
      dl->PushClipRect(origin, br, true);
      const float drawFontSize = CommentFontSize(n->fontSize, ImGui::GetFontSize());
      if (n == gCommentEdit.target)
      {
         // Suppress preview text while actively editing in the overlay popup so they don't double-render
      }
      else if (n->text.empty())
      {
         dl->AddText(ImGui::GetFont(), drawFontSize,
                     ImVec2(origin.x + 8, origin.y + 8),
                     isLight ? tok::U32(tok::pal::c_8C92A0FF) : tok::U32(tok::pal::c_9696A0FF),
                     "double-click or type to write", nullptr, w - 16.0f);
      }
      else
      {
         dl->AddText(ImGui::GetFont(), drawFontSize,
                     ImVec2(origin.x + 8, origin.y + 8), textCol,
                     n->text.c_str(), nullptr, w - 16.0f);
      }

      // Draw bottom-right resize grip indicator (3 diagonal grip lines)
      const ImU32 gripCol = isLight
         ? IM_COL32((int)(n->color[0] * 120 + 40), (int)(n->color[1] * 120 + 40), (int)(n->color[2] * 120 + 40), 160)
         : IM_COL32((int)(n->color[0] * 160 + 60), (int)(n->color[1] * 160 + 60), (int)(n->color[2] * 160 + 60), 160);
      for (int i = 0; i < 3; i++)
      {
         const float offset = 4.0f + (float)i * 4.0f;
         dl->AddLine(ImVec2(br.x - offset, br.y - 3.0f), ImVec2(br.x - 3.0f, br.y - offset), gripCol, 1.2f);
      }
      dl->PopClipRect();

      {
         const ImVec2 tl = ed::CanvasToScreen(origin);
         const ImVec2 rb = ed::CanvasToScreen(br);
         gCommentBodyRect = ImVec4(tl.x, tl.y, rb.x - tl.x, rb.y - tl.y);
         if (n == gCommentEdit.target)
         {
            gCommentEditRect = gCommentBodyRect;
            gCommentEditZoom = ed::GetCurrentZoom();
         }
      }

      // Resize grip interaction (bottom-right corner)
      const float gripSize = 20.0f;
      ImGui::SetCursorScreenPos(ImVec2(br.x - gripSize, br.y - gripSize));
      ImGui::InvisibleButton("##commentresizegrip", ImVec2(gripSize, gripSize));
      const bool gripHovered = ImGui::IsItemHovered();
      const bool gripActive = ImGui::IsItemActive();
      if (ImGui::IsItemActivated())
      {
         PushUndoCheckpoint();
      }
      if (gripHovered || gripActive)
      {
         ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
      }
      if (gripActive)
      {
         const float zoom = std::max(0.01f, ed::GetCurrentZoom());
         ImGuiIO& io = ImGui::GetIO();
         n->width = std::clamp(n->width + io.MouseDelta.x / zoom, 120.0f, 1600.0f);
         n->height = std::clamp(n->height + io.MouseDelta.y / zoom, 60.0f, 1200.0f);
         gPatchDirty = true;
      }
      ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + h));

      // Hover & typing / double-click / Enter to edit
      ImGuiIO& io = ImGui::GetIO();
      if (hovered && !gripHovered && !gripActive && gCommentEdit.target == nullptr && !TextFocusClaimed())
      {
         bool shouldOpen = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
         if (!shouldOpen && !io.KeyCtrl && !io.KeySuper && !io.KeyAlt)
         {
            if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))
               shouldOpen = true;
            else if (!io.InputQueueCharacters.empty())
            {
               for (ImWchar cChar : io.InputQueueCharacters)
               {
                  if (cChar >= 32 && cChar != 127)
                  {
                     shouldOpen = true;
                     break;
                  }
               }
            }
         }
         if (shouldOpen)
         {
            PushUndoCheckpoint();
            gCommentEdit.target = n;
            gCommentEdit.justOpened = true;
         }
      }
   }


   void DrawCommentParams(CommentNode*)
   {
   }


   // Padding kept between a group's members and the edge of its box.
   const float kGroupPadding = 24.0f;


   // Which group, if any, currently owns a node. Membership is exclusive: a
   // node belongs to at most one group, which is what stops two groups from
   // both auto-fitting around the same nodes and ending up drawn inside one
   // another.
   GroupNode* GroupOwning(int nodeIndex)
   {
      for (const auto& entry : gGroupMembers)
      {
         if (entry.second.count(nodeIndex) != 0)
            return entry.first;
      }
      return nullptr;
   }


   // The gNodes index a GroupNode* lives at, or -1 if it is not (or no longer)
   // in the graph. Used to translate group ownership into an index that
   // survives being carried around in a clipboard/duplicate item list.
   int IndexOfGroupNode(GroupNode* g)
   {
      if (g == nullptr)
         return -1;
      for (const GraphNode& gn : gNodes)
      {
         if (gn.node.get() == g)
            return gn.index;
      }
      return -1;
   }


   // How far to shift a duplicated/pasted cluster so the copy reads as a
   // separate thing directly below the original, rather than sitting on
   // top of it. A flat 40px works for a single node, but a whole group can
   // span hundreds of pixels - the same flat offset there leaves the copy's
   // box nearly fully overlapping the original. Shifting straight down by
   // the cluster's own height (plus a margin) clears it in one predictable
   // direction, landing right underneath rather than off at a diagonal.
   ImVec2 ClusterOffset(const std::set<int>& indices)
   {
      bool any = false;
      ImVec2 bmin(0.0f, 0.0f), bmax(0.0f, 0.0f);
      for (int index : indices)
      {
         GraphNode* gn = FindNodeByIndex(index);
         if (gn == nullptr)
            continue;
         const ImVec2 p = ed::GetNodePosition(gn->NodeId());
         const ImVec2 s = ed::GetNodeSize(gn->NodeId());
         if (!any)
         {
            bmin = p; bmax = ImVec2(p.x + s.x, p.y + s.y); any = true;
         }
         else
         {
            bmin.x = std::min(bmin.x, p.x);
            bmin.y = std::min(bmin.y, p.y);
            bmax.x = std::max(bmax.x, p.x + s.x);
            bmax.y = std::max(bmax.y, p.y + s.y);
         }
      }
      if (!any)
         return ImVec2(0.0f, 40.0f);
      const float kMargin = 60.0f;
      return ImVec2(0.0f, (bmax.y - bmin.y) + kMargin);
   }


   // Drops membership sets whose group no longer exists. Deleting a node goes
   // through RemoveNodeByIndex, which cleans up as it goes, but undo/redo
   // rebuilds gNodes wholesale without ever calling it - and a stale
   // GroupNode* key is not merely wasted memory, it is an address the
   // allocator can hand back to a brand new group, which would then inherit a
   // stranger's member list.
   void PruneDeadGroups()
   {
      std::set<GroupNode*> live;
      for (GraphNode& gn : gNodes)
      {
         if (auto* g = dynamic_cast<GroupNode*>(gn.node.get()))
            live.insert(g);
      }
      for (auto it = gGroupMembers.begin(); it != gGroupMembers.end(); )
         it = (live.count(it->first) != 0) ? std::next(it) : gGroupMembers.erase(it);
   }


   // Fits a group's box to exactly its members' bounding box plus padding,
   // every frame, in both directions - so dragging a node towards the edge
   // stretches the box and dragging it back shrinks the box down again.
   //
   // Edge/corner resizing no longer exists (imgui-node-editor's c_Regions
   // list was narrowed to Header+Center - see the comment at its
   // definition), so the box's size is purely a function of its members: the
   // only way to grow it is to drag a loose node in, and the only way to
   // shrink it is to drag a member out. That still reads as "reach out and
   // grab that one too", it just happens by moving the node instead of
   // dragging the box's edge over it.
   //
   // Runs before the group draws itself, using last frame's box (this node's
   // position plus the width/height/headerH already synced onto n) against
   // every other node's current position. Drag deltas for the frame are
   // applied before this per-node loop runs, so every position read here is
   // this frame's live one.
   void AutoFitGroupToMembers(GraphNode& gn, GroupNode* n)
   {
      const ImVec2 nodePos = ed::GetNodePosition(gn.NodeId());
      const ImVec2 boxMin(nodePos.x, nodePos.y + n->headerH);
      const ImVec2 boxMax(boxMin.x + n->width, boxMin.y + n->height);

      std::set<int>& members = gGroupMembers[n];

      // Adopt anything now fully inside the box. A group never adopts another
      // group (nesting is not supported), nor a node another group already
      // owns.
      for (GraphNode& other : gNodes)
      {
         if (other.index == gn.index || dynamic_cast<GroupNode*>(other.node.get()) != nullptr)
            continue;
         GroupNode* owner = GroupOwning(other.index);
         if (owner != nullptr && owner != n)
            continue;
         const ImVec2 p = ed::GetNodePosition(other.NodeId());
         const ImVec2 s = ed::GetNodeSize(other.NodeId());
         if (p.x >= boxMin.x && p.y >= boxMin.y && p.x + s.x <= boxMax.x && p.y + s.y <= boxMax.y)
            members.insert(other.index);
      }

      // Union the members' bounds, dropping any whose node has been deleted.
      ImVec2 fitMin(0.0f, 0.0f), fitMax(0.0f, 0.0f);
      bool any = false;
      for (auto it = members.begin(); it != members.end(); )
      {
         GraphNode* member = FindNodeByIndex(*it);
         if (member == nullptr)
         {
            it = members.erase(it);
            continue;
         }
         const ImVec2 p = ed::GetNodePosition(member->NodeId());
         const ImVec2 s = ed::GetNodeSize(member->NodeId());
         if (!any)
         {
            fitMin = p;
            fitMax = ImVec2(p.x + s.x, p.y + s.y);
            any = true;
         }
         else
         {
            fitMin.x = std::min(fitMin.x, p.x);
            fitMin.y = std::min(fitMin.y, p.y);
            fitMax.x = std::max(fitMax.x, p.x + s.x);
            fitMax.y = std::max(fitMax.y, p.y + s.y);
         }
         ++it;
      }

      // An empty group has nothing to fit to, so it keeps whatever size the
      // user gave it - otherwise it would collapse the moment it was spawned
      // and there would be no box left to drag over anything.
      if (!any)
         return;

      fitMin.x -= kGroupPadding; fitMin.y -= kGroupPadding;
      fitMax.x += kGroupPadding; fitMax.y += kGroupPadding;

      const ImVec2 size(fitMax.x - fitMin.x, fitMax.y - fitMin.y);
      // Both setters no-op when the value already matches, so this does not
      // mark the patch dirty every frame just by sitting there.
      ed::SetNodePosition(gn.NodeId(), ImVec2(fitMin.x, fitMin.y - n->headerH));
      ed::SetGroupSize(gn.NodeId(), size);
      n->width = size.x;
      n->height = size.y;
   }


   // Drawn as an ed::Group() rather than through the normal pin/param flow: a
   // group has no image in or out, and it needs the library's own notion of
   // "group" (a node the editor drags its geometric contents along with) to
   // get the stick-together behavior at all. The label sits above the box as
   // the node's only ordinary content, so it becomes the group's Header
   // region; the box's Center region is also interactive (see the c_Regions
   // comment in imgui_node_editor.cpp), so pressing anywhere in the box's
   // empty interior drags the whole group too, not just the header. Known
   // trade-off: this also means a rubber-band drag can no longer start from
   // inside a group's box - only from outside it - since empty interior
   // space now belongs to the group rather than to the canvas.
   //
   // The header is plain text, not a text field: an ImGui widget spanning
   // most of the header would capture clicks itself (the same reason a
   // slider inside an ordinary node's body doesn't drag the node), leaving
   // almost nowhere on the header for the editor's own drag-to-move to ever
   // trigger. Renaming instead happens in-place on double-click.
   //
   // ed::Group(size) only honors `size` the first frame a node becomes a
   // group; after that the library tracks its own notion of the node's size
   // (and our own growth, via SetGroupSize) internally and ignores whatever
   // we pass. Edge/corner resizing is disabled (see the c_Regions comment in
   // imgui_node_editor.cpp), so SetGroupSize's every-frame call from
   // AutoFitGroupToMembers is the only thing that ever changes it. Width/
   // height here exist purely to seed that first frame and to survive
   // save/load - every subsequent frame they are overwritten from the node's
   // actual measured size, the same pattern liveX/liveY use for position.
   void DrawGroupNode(GraphNode& gn, GroupNode* n)
   {
      AutoFitGroupToMembers(gn, n);

      const bool isLight = IsThemeLight();
      const float bgAlpha = isLight ? 0.08f : 0.13f;
      const float borderAlpha = isLight ? 0.65f : 0.80f;
      ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(0, 0, 0, 0));
      ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(0, 0, 0, 0));
      ed::PushStyleColor(ed::StyleColor_GroupBg,
                         ImColor(n->color[0], n->color[1], n->color[2], bgAlpha));
      ed::PushStyleColor(ed::StyleColor_GroupBorder,
                         ImColor(n->color[0], n->color[1], n->color[2], borderAlpha));

      ed::BeginNode(gn.NodeId());
      ImGui::PushID(gn.index);
      ImGui::BeginGroup();
      if (ImGui::ColorButton("##groupcolor", ImVec4(n->color[0], n->color[1], n->color[2], 1.0f),
                             ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
                             ImVec2(14, 14)))
      {
         PushUndoCheckpoint();
         gColor.target = n->color;
         gColor.owner = n;
         gColor.label = "colour";
         gColor.justOpened = true;
      }
      ImGui::SameLine();
      if (isLight)
         ImGui::PushStyleColor(ImGuiCol_Text,
                               ImVec4(n->color[0] * 0.70f, n->color[1] * 0.70f,
                                      n->color[2] * 0.70f, 1.0f));
      else
         ImGui::PushStyleColor(ImGuiCol_Text,
                               ImVec4(n->color[0] * 0.4f + 0.6f, n->color[1] * 0.4f + 0.6f,
                                      n->color[2] * 0.4f + 0.6f, 1.0f));
      if (n->renaming)
      {
         if (n->renameJustStarted)
         {
            ImGui::SetKeyboardFocusHere();
            n->renameJustStarted = false;
         }
         ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0.35f));
         ImGui::SetNextItemWidth(std::max(60.0f, n->width - 40.0f));
         if (ImGui::InputText("##grouprename", &n->label, ImGuiInputTextFlags_EnterReturnsTrue) ||
             ImGui::IsItemDeactivated())
            n->renaming = false;
         ImGui::PopStyleColor();
      }
      else
      {
         ImGui::TextUnformatted(n->label.c_str());
         if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
         {
            PushUndoCheckpoint();
            n->renaming = true;
            n->renameJustStarted = true;
         }
      }
      ImGui::PopStyleColor();
      ImGui::EndGroup();
      const ImVec2 headerSize = ImGui::GetItemRectSize();
      ed::Group(ImVec2(n->width, n->height));
      ImGui::PopID();
      ed::EndNode();

      ed::PopStyleColor(4);

      const ImVec2 total = ed::GetNodeSize(gn.NodeId());
      n->width = std::max(120.0f, total.x);
      n->headerH = headerSize.y + ImGui::GetStyle().ItemSpacing.y;
      n->height = std::max(60.0f, total.y - n->headerH);
   }


   void DrawDrawParams(DrawNode* n)
   {
      DropdownButton("brush", DrawNode::BrushNames(), n->brush, [n](int i) { n->brush = i; });
      ModSlider("size", &n->brushSize, 0.002f, 0.5f);
      ModSlider("opacity", &n->opacity, 0.02f, 1.0f);
      ModSlider("hardness", &n->hardness, 0.0f, 0.98f);
      ModSlider("spacing", &n->spacing, 0.02f, 1.0f);
      ModSlider("jitter", &n->jitter, 0.0f, 2.0f);
      ColorSwatch("colour", n->color, n);
      ModCheckbox("eraser", &n->eraser);
      if (ImGui::Button("Clear canvas", ImVec2(kPreviewSize, 0)))
         n->ClearCanvas();

      NodeSeparator("animation");
      if (n->IsRecordingStrokes())
      {
         ImGui::PushStyleColor(ImGuiCol_Button, tok::V4(tok::palf::v_650_150_150_1000));
         if (ImGui::Button("Stop rec", ImVec2(kPreviewSize * 0.48f, 0)))
            n->StopRecording();
         ImGui::PopStyleColor();
      }
      else if (ImGui::Button("Rec strokes", ImVec2(kPreviewSize * 0.48f, 0)))
      {
         n->StartRecording();
      }
      ImGui::SameLine();
      if (n->IsPlayingBack())
      {
         if (ImGui::Button("Stop", ImVec2(kPreviewSize * 0.48f, 0)))
            n->StopPlayback();
      }
      else if (ImGui::Button("Replay", ImVec2(kPreviewSize * 0.48f, 0)))
      {
         n->PlayRecording();
      }

      if (n->RecordedStamps() > 0)
      {
         ImGui::TextDisabled("%zu marks over %.1f beats", n->RecordedStamps(), n->RecordedLength());
         if (n->IsPlayingBack())
            ImGui::TextColored(tok::V4(tok::palf::v_500_950_600_1000), "playhead %.1f", n->PlayheadBeats());
         if (n->RecordingCapped())
            ImGui::TextDisabled("recording capped - further strokes won't be recorded");
      }
      ModCheckbox("loop replay", &n->loopPlayback);
      ModSlider("replay speed", &n->playSpeed, 0.1f, 4.0f);
      if (ImGui::SmallButton("clear recording"))
         n->ClearRecording();
      ModSlider("canvas w", &n->canvasWidth, 64.0f, 4096.0f, "%.0f");
      ModSlider("canvas h", &n->canvasHeight, 64.0f, 4096.0f, "%.0f");
   }


   // What a node *shows* - inline preview, mini viewport, viewport panel,
   // projector window. A bypassed node doesn't cook, so its own FBO/mesh is a
   // frozen last frame; showing that would claim the node is still in the
   // chain. Instead show exactly what leaves it: walk BypassSource() the same
   // way ImageCable::Resolved() does. nullptr = a bypassed source (nothing
   // leaves it), which callers draw as an empty "bypassed" box.
   INode* DisplayNode(INode* node)
   {
      for (int hops = 0; node != nullptr && node->bypassed && hops < 64; hops++)
         node = node->BypassSource();
      return (node != nullptr && node->bypassed) ? nullptr : node;
   }


   // Placeholder text for an empty preview: says *why* it's empty.
   const char* EmptyPreviewLabel(INode* node, const char* fallback)
   {
      return node->bypassed ? "bypassed" : fallback;
   }


   // Square, letterboxed preview so non-square sources still read 1:1.
   void DrawPreview(INode* node)
   {
      INode* shown = DisplayNode(node);
      unsigned int tex = shown != nullptr ? shown->GetOutputTexture() : 0;
      auto* render = dynamic_cast<Render3DNode*>(node);
      const bool wantsBigCanvas =
         render != nullptr || dynamic_cast<ViewportNode*>(node) != nullptr;
      const float size = wantsBigCanvas ? kViewportSize : kPreviewSize;

      if (render != nullptr)
      {
         const float offset = std::max(0.0f, (kWideNodeWidth - size) * 0.5f);
         if (offset > 0.0f)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
      }

      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();

      DrawCheckerboardBackdrop(dl, origin, size);

      if (tex != 0 && shown->GetOutputWidth() > 0)
      {
         float w = (float)shown->GetOutputWidth();
         float h = (float)shown->GetOutputHeight();
         float scale = size / std::max(w, h);
         float dw = w * scale;
         float dh = h * scale;
         ImVec2 tl(origin.x + (size - dw) * 0.5f, origin.y + (size - dh) * 0.5f);
         dl->AddImage((ImTextureID)(intptr_t)tex, tl, ImVec2(tl.x + dw, tl.y + dh),
                      ImVec2(0, 1), ImVec2(1, 0));
      }
      else
      {
         dl->AddText(ImVec2(origin.x + 10, origin.y + size * 0.5f - 8),
                     tok::U32(tok::pal::c_787887FF), EmptyPreviewLabel(node, "no input"));
      }

      dl->AddRect(origin, ImVec2(origin.x + size, origin.y + size),
                  ScopeBorderCol(), 4.0f);

      // A Render 3D preview is a viewport, not a picture: drag to orbit, scroll
      // to zoom. An InvisibleButton is what makes this safe inside the node
      // editor - while it is active the editor leaves the drag alone, which is
      // the same mechanism the in-node sliders already rely on.
      if (render == nullptr)
      {
         ImGui::Dummy(ImVec2(size, size));
         return;
      }

      ImGui::SetCursorScreenPos(origin);
      ImGui::InvisibleButton("##viewport", ImVec2(size, size));

      // A patched Camera node owns the view, so drive that instead of the
      // built-in values; otherwise the drag would appear to do nothing. This
      // is this Render3DNode's own camera, entirely separate from any node's
      // mini-viewport/panel camera (SharedViewportCamera.h/gNodeCameras): the
      // final output framing is a deliberate setup, not something casual
      // node-viewport orbiting should ever move.
      float* azimuth = render->camera ? &render->camera->azimuth : &render->camAzimuth;
      float* elevation = render->camera ? &render->camera->elevation : &render->camElevation;
      float* distance = render->camera ? &render->camera->distance : &render->camDistance;

      if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
      {
         const ImVec2 drag = ImGui::GetIO().MouseDelta;
         // azimuth/elevation are stored in degrees; 0.6875 deg/px matches the
         // drag feel of the old 0.012 rad/px (0.012 * 180/pi).
         *azimuth -= drag.x * 0.6875f;
         // Clamped just short of the poles: straight overhead makes the view
         // matrix's up vector parallel to the view direction and the image rolls.
         *elevation = std::max(-85.9437f, std::min(*elevation + drag.y * 0.6875f, 85.9437f));
      }

      if (ImGui::IsItemHovered())
      {
         ImGuiIO& vio = ImGui::GetIO();
         if (vio.MouseWheel != 0.0f)
         {
            *distance = std::max(0.2f, std::min(*distance * (1.0f - vio.MouseWheel * 0.15f), 60.0f));
            // Consumed, or the canvas zooms at the same time as the camera.
            vio.MouseWheel = 0.0f;
         }
      }

      // Numpad view hotkeys (1/3/7/0, Ctrl+1/3/7). Unlike drag-orbit above,
      // a hotkey view snap on this render's own camera is treated as a
      // deliberate edit worth undoing - PushUndoCheckpoint() before the
      // write, the same as the "orbit"/"elevation" sliders for this same
      // camera (main.cpp ~23053).
      ApplyViewHotkeys(*azimuth, *elevation, []() { PushUndoCheckpoint(); });

      if (ImGui::IsItemHovered() || ImGui::IsItemActive())
         dl->AddRect(origin, ImVec2(origin.x + size, origin.y + size),
                     tok::U32(tok::pal::c_78C8FFC8), 4.0f, 0, 2.0f);
   }


   // Build step 15 follow-up (§5.3.1): inline min/max waveform preview for
   // an encapsulated FieldGraphNode's audio-domain terminal - the sibling of
   // DrawPreview above for the "terminal has no texture, but does have
   // RequiresAudioProcessing()" case. No generic "any INode's live audio"
   // hook exists anywhere in this codebase (confirmed by grep: ReadScope()/
   // scopeCache/scopeCacheCount/scopeCacheTime is a repeated-but-not-
   // virtualized method+field set independently implemented on
   // WavetableNode, FieldSampleNode, OscillatorNode, MetallicNode,
   // WaveTerrainNode, ImageSpectralSynthNode and EquationNode - each already
   // backed by that node's own MeterRing, the one sanctioned audio-thread-
   // to-main-thread channel in this codebase, field-integration §4). This
   // dispatches to whichever of those the resolved terminal happens to be,
   // the exact same dynamic_cast-chain shape DrawAudioNodeBody already uses
   // to pick a per-type body-draw function for a directly-visible node of
   // one of these types - not a new mechanism, the established one for
   // "which concrete node type is this" here. A terminal whose type has no
   // scope hook (Gain, Mixer, DrumSequencer, ...) falls back to the same
   // "idle" placeholder those per-type scope draws already show when they
   // have nothing queued, rather than fabricating data or reaching into
   // ProcessBlock directly (doc trap 7).
   //
   // The 256-slot decimated min/max cache itself lives on FieldGraphNode
   // (kWaveformCacheSize/waveformMin/waveformMax/waveformCacheCount), not on
   // the terminal - adapting GranularNode's own shape (GranularNode.h
   // kWaveformCacheSize/waveformMin/waveformMax) rather than a fourth
   // pattern. Rebuilt on a throttled ~30Hz cadence, matching
   // DrawFieldSampleScope's own cadence above - never synchronously inside
   // an audio callback.
   void DrawFieldGraphWaveform(FieldGraphNode* fgn, INode* audioTerminal)
   {
      const double now = ImGui::GetTime();
      if (fgn->waveformCacheTime < 0.0 || now - fgn->waveformCacheTime > 1.0 / 30.0)
      {
         float raw[256];
         int rawCount = 0;
         if (auto* n = dynamic_cast<WavetableNode*>(audioTerminal))
            rawCount = n->ReadScope(raw, WavetableNode::kScopeCacheCapacity);
         else if (auto* n = dynamic_cast<FieldSampleNode*>(audioTerminal))
            rawCount = n->ReadScope(raw, FieldSampleNode::kScopeCacheCapacity);
         else if (auto* n = dynamic_cast<OscillatorNode*>(audioTerminal))
            rawCount = n->ReadScope(raw, OscillatorNode::kScopeCacheCapacity);
         else if (auto* n = dynamic_cast<MetallicNode*>(audioTerminal))
            rawCount = n->ReadScope(raw, MetallicNode::kScopeCacheCapacity);
         else if (auto* n = dynamic_cast<WaveTerrainNode*>(audioTerminal))
            rawCount = n->ReadScope(raw, WaveTerrainNode::kScopeCapacity);
         else if (auto* n = dynamic_cast<ImageSpectralSynthNode*>(audioTerminal))
            rawCount = n->ReadScope(raw, ImageSpectralSynthNode::kScopeCapacity);
         else if (auto* n = dynamic_cast<EquationNode*>(audioTerminal))
            rawCount = n->ReadScope(raw, EquationNode::kScopeCapacity);

         if (rawCount > 1)
         {
            const int bucketCount = std::min(FieldGraphNode::kWaveformCacheSize, rawCount);
            const int bucketSize = std::max(1, rawCount / bucketCount);
            for (int b = 0; b < bucketCount; b++)
            {
               const int startI = b * bucketSize;
               const int endI = std::min(rawCount, (b + 1) * bucketSize);
               float minV = 0.0f;
               float maxV = 0.0f;
               for (int i = startI; i < endI; i++)
               {
                  const float v = raw[i];
                  if (v < minV) minV = v;
                  if (v > maxV) maxV = v;
               }
               fgn->waveformMin[b] = minV;
               fgn->waveformMax[b] = maxV;
            }
            fgn->waveformCacheCount = bucketCount;
         }
         else
         {
            fgn->waveformCacheCount = 0;
         }
         fgn->waveformCacheTime = now;
      }

      const float w = AudioFullWidth();
      const float h = 100.0f;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      const float midY = origin.y + h * 0.5f;
      dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeMidLineCol(), 1.0f);

      if (fgn->waveformCacheCount > 0)
      {
         const bool isLight = IsThemeLight();
         const int count = fgn->waveformCacheCount;
         for (int i = 0; i < count; i++)
         {
            const float x = origin.x + w * (float)i / (float)count;
            const float barW = std::max(1.0f, w / (float)count);
            const float top = midY - fgn->waveformMax[i] * h * 0.45f;
            const float bottom = midY - fgn->waveformMin[i] * h * 0.45f;
            dl->AddRectFilled(ImVec2(x, top), ImVec2(x + barW, bottom),
                              isLight ? tok::U32(tok::pal::c_285AC8C8) : tok::U32(tok::pal::c_8CA0DCAF));
         }
      }
      else
      {
         dl->AddText(ImVec2(origin.x + 8.0f, origin.y + 4.0f), ScopeTextCol(), "idle");
      }

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 4.0f);
      ImGui::Dummy(ImVec2(w, h));
   }
}
