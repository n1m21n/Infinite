// Comment, group, draw and preview node bodies (moved verbatim from main.cpp).
#include "nodes/ImageSourceNode.h"
#include "app/graph/NodeParams.h"
#include "app/ui/design/components/Thumb.h"
#include "app/ui/design/UiInteract.h"
#include "app/ui/design/Keymap.h"
#include "nodes/VideoSourceNode.h"
#include "nodes/VideoInNode.h"
#include "app/ui/design/components/EmptyState.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/components/FieldWell.h"
#include "app/ui/design/components/GlyphToggle.h"
#include "app/ui/design/components/FormParts.h"
#include "app/AppShared.h"
#include "app/ui/design/DrawParam.h"
#include "app/ui/design/components/AudioViz.h"
#include "app/ui/design/UiFmt.h"
#include "app/ui/design/UiScope.h"

namespace app
{
   // Paintable preview: the Draw node turns its 1:1 preview into the canvas, so
   // you draw directly on the node rather than in a separate window.
   // ui-content-begin: node preview: the node's output picture
   void DrawPaintablePreview(DrawNode* node)
   {
      ImVec2 origin = ImGui::GetCursorScreenPos();
      UiInteract::Hit("##canvas", ImVec2(kPreviewSize, kPreviewSize));

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
         Thumb::Add(dl, (ImTextureID)(intptr_t)node->GetOutputTexture(), tl,
                      ImVec2(tl.x + dw, tl.y + dh), ImVec2(0, 1), ImVec2(1, 0));
      if (!node->HasStrokes())
         AudioViz::IdleLabel(AudioViz::Frame{dl, origin, ImVec2(origin.x + kPreviewSize, origin.y + kPreviewSize)}, "draw here");
      dl->AddRect(origin, ImVec2(origin.x + kPreviewSize, origin.y + kPreviewSize),
                  tok::Role(tok::col::accent_muted_hi), tok::preview_paintable_radius, 0, tok::preview_paintable_stroke);
   }
   // ui-content-end


   // A comment shows its note on the face of the node, not behind the params
   // (eye) toggle - the whole point of a note is to be readable without an
   inline float CommentFontSize(int sizeIdx, float baseFontSize)
   {
      switch (sizeIdx)
      {
         case 0: return baseFontSize * tok::preview_comment_font_size_ratio;  // Small (~13px)
         case 1: default: return baseFontSize; // Normal (~15px)
         case 2: return baseFontSize * tok::preview_comment_font_size_scale_sm;  // Large (~20px)
         case 3: return baseFontSize * tok::preview_comment_font_size_scale_lg;  // Extra Large (~28px)
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
      const float w = std::max(tok::preview_comment_min_lg, n->width);
      const float h = std::max(tok::preview_comment_min_sm, n->height);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);

      // Reserved first: this is what the node measures itself against and what
      // the double-click/hover/type hit-tests.
      Ui::Claim(ImVec2(w, h));
      const bool hovered = ImGui::IsItemHovered();

      const bool isLight = IsThemeLight();
      const ImU32 bgCol = isLight
         ? IM_COL32((int)(tok::preview_comment_n + n->color[0] * tok::preview_comment_bg_col_shade_lg),
                    (int)(tok::preview_comment_bg_col_shade_xxl + n->color[1] * tok::preview_comment_bg_col_shade_md),
                    (int)(tok::preview_comment_bg_col_shade_xxxl + n->color[2] * tok::preview_comment_bg_col_shade_xs), tok::preview_bg_col_alpha_lg)
         : IM_COL32((int)(tok::preview_comment_bg_col_shade_xxxs + n->color[0] * tok::preview_comment_bg_col_shade_xl),
                    (int)(tok::preview_comment_bg_col_shade_xxs + n->color[1] * tok::preview_comment_bg_col_shade_xl),
                    (int)(tok::preview_comment_bg_col_shade_sm + n->color[2] * tok::preview_comment_bg_col_shade_xl), tok::preview_bg_col_alpha_sm);
      const ImU32 borderCol = isLight
         ? IM_COL32((int)(n->color[0] * tok::preview_border_col_shade_md + tok::preview_border_col_shade_xs),
                    (int)(n->color[1] * tok::preview_border_col_shade_md + tok::preview_border_col_shade_xs),
                    (int)(n->color[2] * tok::preview_border_col_shade_md + tok::preview_border_col_shade_xs), tok::preview_grip_col_alpha)
         : IM_COL32((int)(n->color[0] * tok::preview_border_col_shade_lg + tok::preview_border_col_shade_sm),
                    (int)(n->color[1] * tok::preview_border_col_shade_lg + tok::preview_border_col_shade_sm),
                    (int)(n->color[2] * tok::preview_border_col_shade_lg + tok::preview_border_col_shade_sm), tok::preview_border_col_alpha);

      dl->AddRectFilled(origin, br, bgCol, tok::preview_comment_radius);
      // Header accent bar (Apple Notes / sticky card feel)
      dl->AddRectFilled(origin, ImVec2(br.x, origin.y + tok::space_1),
                        IM_COL32((int)(n->color[0] * tok::preview_comment_ratio), (int)(n->color[1] * tok::preview_comment_ratio),
                                 (int)(n->color[2] * tok::preview_comment_ratio), tok::preview_fill_alpha_lg), tok::preview_comment_radius, ImDrawFlags_RoundCornersTop);
      dl->AddRect(origin, br, borderCol, tok::preview_comment_radius, 0, tok::preview_comment_stroke);

      const ImU32 textCol = isLight
         ? tok::Base(tok::col::surface_lo)
         : IM_COL32((int)((n->color[0] * tok::preview_comment_shade + tok::preview_comment_shade) * 255),
                    (int)((n->color[1] * tok::preview_comment_shade + tok::preview_comment_shade) * 255),
                    (int)((n->color[2] * tok::preview_comment_shade + tok::preview_comment_shade) * 255), tok::preview_comment_alpha);
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
                     ImVec2(origin.x + tok::space_2, origin.y + tok::space_2),
                     isLight ? tok::Base(tok::col::edge_mid) : tok::Base(tok::col::text_secondary),
                     "double-click or type to write", nullptr, w - tok::space_4);
      }
      else
      {
         dl->AddText(ImGui::GetFont(), drawFontSize,
                     ImVec2(origin.x + tok::space_2, origin.y + tok::space_2), textCol,
                     n->text.c_str(), nullptr, w - tok::space_4);
      }

      // Draw bottom-right resize grip indicator (3 diagonal grip lines)
      const ImU32 gripCol = isLight
         ? IM_COL32((int)(n->color[0] * tok::preview_grip_col_shade_md + tok::preview_border_col_shade_xs), (int)(n->color[1] * tok::preview_grip_col_shade_md + tok::preview_border_col_shade_xs), (int)(n->color[2] * tok::preview_grip_col_shade_md + tok::preview_border_col_shade_xs), tok::preview_grip_col_alpha)
         : IM_COL32((int)(n->color[0] * tok::preview_grip_col_shade + tok::preview_grip_col_shade_sm), (int)(n->color[1] * tok::preview_grip_col_shade + tok::preview_grip_col_shade_sm), (int)(n->color[2] * tok::preview_grip_col_shade + tok::preview_grip_col_shade_sm), tok::preview_grip_col_alpha);
      for (int i = 0; i < 3; i++)
      {
         const float offset = tok::space_1 + (float)i * 4.0f;
         dl->AddLine(ImVec2(br.x - offset, br.y - tok::space_0_75), ImVec2(br.x - tok::space_0_75, br.y - offset), gripCol, tok::preview_comment_stroke);
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
      const float gripSize = tok::preview_grip_size;
      ImGui::SetCursorScreenPos(ImVec2(br.x - gripSize, br.y - gripSize));
      UiInteract::Hit("##commentresizegrip", ImVec2(gripSize, gripSize));
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
         const float zoom = std::max(tok::preview_zoom_min, ed::GetCurrentZoom());
         ImGuiIO& io = ImGui::GetIO();
         n->width = std::clamp(n->width + io.MouseDelta.x / zoom, tok::preview_comment_min_lg, tok::preview_comment_hi_lg);
         n->height = std::clamp(n->height + io.MouseDelta.y / zoom, tok::preview_comment_min_sm, tok::preview_comment_hi_sm);
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
            if (Keymap::Confirm(false))
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
         return ImVec2(0.0f, tok::preview_cluster_offset_y);
      const float kMargin = tok::preview_margin;
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
         // Not yet placed: the editor reports (0,0) with no size, which can sit inside a group near the origin and
         // stretch it across the canvas for a frame.
         if (other.needsPosition)
            continue;
         if (other.noGroupAdoptUntilMoved)
         {
            const ImVec2 at = ed::GetNodePosition(other.NodeId());
            if (std::abs(at.x - other.spawnX) < 1.0f && std::abs(at.y - other.spawnY) < 1.0f)
               continue;
            other.noGroupAdoptUntilMoved = false;
         }
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
      // A group that is off reads as dimmed: its members hold black and silent.
      const float offDim = n->on ? 1.0f : 0.4f;
      const float bgAlpha = (isLight ? tok::preview_group_node_bg_alpha_sm : tok::preview_group_node_bg_alpha_lg) * offDim;
      const float borderAlpha = (isLight ? tok::preview_group_node_border_alpha_sm : tok::preview_group_node_border_alpha_lg) * offDim;
      ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(0, 0, 0, 0));
      ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(0, 0, 0, 0));
      ed::PushStyleColor(ed::StyleColor_GroupBg,
                         ImColor(n->color[0], n->color[1], n->color[2], bgAlpha));
      ed::PushStyleColor(ed::StyleColor_GroupBorder,
                         ImColor(n->color[0], n->color[1], n->color[2], borderAlpha));

      ed::BeginNode(gn.NodeId());
      ImGui::PushID(gn.index);
      ImGui::BeginGroup();
      // The same power glyph as a node's: lit while the group plays. A live
      // control, like node power, so it is not an undo step.
      if (GlyphToggle::Draw("##grouppower", IconsInfinite::Power, IconsInfinite::PowerFill, n->on, tok::preview_grouppower_cap_y, true))
         n->on = !n->on;
      if (ImGui::IsItemHovered())
         HelpTip("%s", n->on ? T("Turn the group off: its nodes stop cooking and go silent") : T("Turn the group on"));
      FormParts::Inline();
      if (ImGui::ColorButton("##groupcolor", ImVec4(n->color[0], n->color[1], n->color[2], 1.0f),
                             ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
                             ImVec2(tok::preview_group_node_color_button_w, tok::preview_group_node_color_button_h)))
      {
         PushUndoCheckpoint();
         gColor.target = n->color;
         gColor.owner = n;
         gColor.label = "colour";
         gColor.justOpened = true;
      }
      Ui::SameRow();
      if (isLight)
         ImGui::PushStyleColor(ImGuiCol_Text,
                               ImVec4(n->color[0] * tok::preview_group_node_y_lg, n->color[1] * tok::preview_group_node_y_lg,
                                      n->color[2] * tok::preview_group_node_y_lg, 1.0f));
      else
         ImGui::PushStyleColor(ImGuiCol_Text,
                               ImVec4(n->color[0] * tok::preview_group_node_y_sm + tok::preview_group_node_y_md, n->color[1] * tok::preview_group_node_y_sm + tok::preview_group_node_y_md,
                                      n->color[2] * tok::preview_group_node_y_sm + tok::preview_group_node_y_md, 1.0f));
      if (n->renaming)
      {
         if (n->renameJustStarted)
         {
            UiInteract::FocusHere();
            n->renameJustStarted = false;
         }
         ImGui::PushStyleColor(ImGuiCol_FrameBg, tok::V4(tok::preview_frame_bg_shade));
         ImGui::SetNextItemWidth(std::max(tok::preview_grouprename_min, n->width - tok::preview_group_node_span));
         if (FieldWell::InputText("##grouprename", &n->label, ImGuiInputTextFlags_EnterReturnsTrue) ||
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
      n->width = std::max(tok::preview_group_node_min, total.x);
      n->headerH = headerSize.y + ImGui::GetStyle().ItemSpacing.y;
      n->height = std::max(tok::preview_grouprename_min, total.y - n->headerH);
   }


   namespace
   {
      using DrawParamsRow = params::Row<DrawNode>;
      const DrawParamsRow kDrawParamsRows1[] = {
         DrawParamsRow::Choice<&DrawNode::brush>("brush", &DrawNode::BrushNames),
         DrawParamsRow::Slider<&DrawNode::brushSize>("size", params::Draw::brush_size_lo, params::Draw::brush_size_hi),
         DrawParamsRow::Slider<&DrawNode::opacity>("opacity", params::Draw::opacity_lo, params::Draw::opacity_hi),
         DrawParamsRow::Slider<&DrawNode::hardness>("hardness", params::Draw::hardness_lo, params::Draw::hardness_hi),
         DrawParamsRow::Slider<&DrawNode::spacing>("spacing", params::Draw::spacing_lo, params::Draw::spacing_hi),
         DrawParamsRow::Slider<&DrawNode::jitter>("jitter", params::Draw::jitter_lo, params::Draw::jitter_hi),
         DrawParamsRow::Colour<&DrawNode::color>("colour"),
         DrawParamsRow::Toggle<&DrawNode::eraser>("eraser"),
      };
      const DrawParamsRow kDrawParamsRows2[] = {
         DrawParamsRow::Toggle<&DrawNode::loopPlayback>("loop replay"),
         DrawParamsRow::Slider<&DrawNode::playSpeed>("replay speed", params::Draw::play_speed_lo, params::Draw::play_speed_hi),
      };
      const DrawParamsRow kDrawParamsRows3[] = {
         DrawParamsRow::Slider<&DrawNode::canvasWidth>("canvas w", params::Draw::canvas_width_lo, params::Draw::canvas_width_hi).Fmt("%.0f"),
         DrawParamsRow::Slider<&DrawNode::canvasHeight>("canvas h", params::Draw::canvas_height_lo, params::Draw::canvas_height_hi).Fmt("%.0f"),
      };
   }

   void DrawDrawParams(DrawNode* n)
   {
      gParamWidthLive = std::max(gParamWidthLive, kPreviewSize);   // sliders as wide as the canvas and the buttons
      const float halfW = (kPreviewSize - tok::space_1) * 0.5f;
      Ui::DrawParams(n, kDrawParamsRows1);
      if (ActionButton::Draw("Clear canvas", ImVec2(kPreviewSize, 0)))
         n->ClearCanvas();

      NodeSeparator("animation");
      if (n->IsRecordingStrokes())
      {
         if (ActionButton::Draw("Stop rec", ImVec2(halfW, 0), ActionButton::Kind::Record))
            n->StopRecording();
      }
      else if (ActionButton::Draw("Rec strokes", ImVec2(halfW, 0)))
      {
         n->StartRecording();
      }
      Ui::SameRow(Ui::Space::S1);
      if (n->IsPlayingBack())
      {
         if (ActionButton::Draw("Stop", ImVec2(halfW, 0)))
            n->StopPlayback();
      }
      else if (ActionButton::Draw("Replay", ImVec2(halfW, 0)))
      {
         n->PlayRecording();
      }

      if (n->RecordedStamps() > 0)
      {
         ImGui::TextDisabled("%zu marks over %s", n->RecordedStamps(), UiFmt::Beats(n->RecordedLength()).c);
         if (n->IsPlayingBack())
            ImGui::TextColored(tok::RoleV4(tok::col::state_ok), "playhead %s", UiFmt::F1(n->PlayheadBeats()).c);
         if (n->RecordingCapped())
         {
            PushTextWrapWidth(kPreviewSize);
            ImGui::TextDisabled("recording capped - further strokes won't be recorded");
            PopTextWrapWidth();
         }
      }
      Ui::DrawParams(n, kDrawParamsRows2);
      if (ActionButton::Draw("clear recording", ImVec2(kPreviewSize, 0)))
         n->ClearRecording();
      Ui::DrawParams(n, kDrawParamsRows3);
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
      if (node->bypassed)
         return T("Bypassed");
      return fallback;
   }


   // Square, letterboxed preview so non-square sources still read 1:1.
   // ui-content-begin: node preview: the node's output picture
   void DrawPreview(INode* node)
   {
      INode* shown = DisplayNode(node);
      unsigned int tex = shown != nullptr ? shown->GetOutputTexture() : 0;
      auto* render = dynamic_cast<Render3DNode*>(node);
      const bool wantsBigCanvas =
         render != nullptr || dynamic_cast<ViewportNode*>(node) != nullptr;
      const float size = wantsBigCanvas ? kViewportSize : kPreviewSize;

      {
         const float offset = render != nullptr ? WideNodeCentreOffset(node, size) : CachedCentreOffset(node, size);
         Ui::CentreNext(offset);
      }

      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();

      DrawCheckerboardBackdrop(dl, origin, size);

      // An off group sends nothing on, so its last frame would be a stale picture.
      if (node->blockOff)
      {
         EmptyState::DrawCaption(origin, ImVec2(origin.x + size, origin.y + size), T("Block off"));
      }
      else if (tex != 0 && shown->GetOutputWidth() > 0)
      {
         float w = (float)shown->GetOutputWidth();
         float h = (float)shown->GetOutputHeight();
         float scale = size / std::max(w, h);
         float dw = w * scale;
         float dh = h * scale;
         ImVec2 tl(origin.x + (size - dw) * 0.5f, origin.y + (size - dh) * 0.5f);
         Thumb::Add(dl, (ImTextureID)(intptr_t)tex, tl, ImVec2(tl.x + dw, tl.y + dh),
                      ImVec2(0, 1), ImVec2(1, 0));
      }
      else
      {
         EmptyState::DrawCaption(origin, ImVec2(origin.x + size, origin.y + size), EmptyPreviewLabel(node, T("No input")));
      }

      if (!node->bypassed && !node->blockOff)
      {
         const char* idle = nullptr;
         if (auto* is = dynamic_cast<ImageSourceNode*>(node); is != nullptr && is->LoadedPath().empty())
            idle = "drop an image";
         else if (auto* vs = dynamic_cast<VideoSourceNode*>(node); vs != nullptr && vs->LoadedPath().empty())
            idle = "drop a video";
         else if (auto* vi = dynamic_cast<VideoInNode*>(node); vi != nullptr && !vi->IsRunning())
            idle = "no camera yet";
         if (idle != nullptr)
            EmptyState::DrawCaption(origin, ImVec2(origin.x + size, origin.y + size), idle);
      }

      if (render != nullptr && render->FlattenedGeometry().empty())
         AudioViz::IdleLabel(AudioViz::Frame{dl, origin, ImVec2(origin.x + size, origin.y + size)}, "patch geometry in");

      AudioViz::Border(dl, origin, ImVec2(origin.x + size, origin.y + size));

      // A Render 3D preview is a viewport, not a picture: drag to orbit, scroll
      // to zoom. An InvisibleButton is what makes this safe inside the node
      // editor - while it is active the editor leaves the drag alone, which is
      // the same mechanism the in-node sliders already rely on.
      if (render == nullptr)
      {
         Ui::Claim(ImVec2(size, size));
         return;
      }

      ImGui::SetCursorScreenPos(origin);
      UiInteract::Hit("##viewport", ImVec2(size, size));

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
         *azimuth -= drag.x * tok::preview_azimuth_ratio;
         // Clamped just short of the poles: straight overhead makes the view
         // matrix's up vector parallel to the view direction and the image rolls.
         *elevation = std::max(-85.9437f, std::min(*elevation + drag.y * tok::preview_ratio, 85.9437f));
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
                     tok::Role(tok::col::accent_pale_muted_warm, tok::preview_outline_alpha), tok::preview_radius, 0, tok::preview_stroke);
   }
   // ui-content-end


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
   // ui-content-begin: node visualizer: the signal picture (trace, spectrum, meter, grid)
   void DrawFieldGraphWaveform(FieldGraphNode* fgn, INode* audioTerminal)
   {
      const double now = ImGui::GetTime();
      if (fgn->waveformCacheTime < 0.0 || now - fgn->waveformCacheTime > 1.0 / tok::motion_hz_scope)
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
         else if (auto* n = dynamic_cast<FMNode*>(audioTerminal))
            rawCount = n->ReadScope(raw, FMNode::kScopeCacheCapacity);
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
      const float h = tok::preview_h;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      AudioViz::Fill(dl, origin, br);
      dl->PushClipRect(origin, br, true);

      const float midY = origin.y + h * 0.5f;
      AudioViz::MidLine(dl, origin.x, br.x, midY);

      if (fgn->waveformCacheCount > 0)
      {
         const bool isLight = IsThemeLight();
         const int count = fgn->waveformCacheCount;
         for (int i = 0; i < count; i++)
         {
            const float x = origin.x + w * (float)i / (float)count;
            const float barW = std::max(1.0f, w / (float)count);
            const float top = midY - fgn->waveformMax[i] * h * tok::preview_top_ratio;
            const float bottom = midY - fgn->waveformMin[i] * h * tok::preview_bottom_ratio;
            dl->AddRectFilled(ImVec2(x, top), ImVec2(x + barW, bottom),
                              isLight ? tok::Base(tok::col::accent, tok::preview_fill_alpha_md) : tok::Base(tok::col::accent_pale_muted_lo, tok::preview_fill_alpha_sm));
         }
      }
      else
      {
         dl->AddText(ImVec2(origin.x + tok::space_2, origin.y + tok::space_1), ScopeTextCol(), "idle");
      }

      dl->PopClipRect();
      AudioViz::Border(dl, origin, br);
      Ui::Claim(ImVec2(w, h));
   }
   // ui-content-end
}
