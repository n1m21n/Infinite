// Viewport panel cards and mini viewports (moved verbatim from main.cpp).
#include "app/ui/design/components/MenuParts.h"
#include "app/ui/design/components/PanelFrame.h"
#include "app/ui/design/components/EmptyState.h"
#include "app/ui/design/GlyphDraw.h"
#include "app/ui/design/TokenColors.h"
#include "app/AppShared.h"
#include "app/ui/design/UiType.h"

namespace app
{
   // A geometry-producing node's own solo render, independent of whatever a
   // downstream Render 3D shows - the point is seeing what *this* node
   // produced (e.g. what a Select actually selected) without having to wire
   // it all the way to the end of the graph. Off by default per node
   // (GraphNode::showMiniViewport); only drawn/rendered at all when a node
   // opts in, so patches that never touch the toggle pay nothing for it.
   void DrawMiniViewport(GraphNode& gn, IGeometrySource* geo)
   {
      NodeViewport& viewport = gNodeViewports[gn.index];
      SharedViewportCamera& cam = gNodeCameras[gn.index];

      const bool isMaterial = (dynamic_cast<MaterialNode*>(gn.node.get()) != nullptr);
      const float size = isMaterial ? kViewportSize : kPreviewSize;

      {
         const float offset = isMaterial ? WideNodeCentreOffset(gn.node.get(), size) : CachedCentreOffset(gn.node.get(), size);
         if (offset > 0.0f)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
      }

      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      DrawCheckerboardBackdrop(dl, origin, size);

      const unsigned int tex = viewport.Render(dynamic_cast<IGeometrySource*>(DisplayNode(gn.node.get())),
                                               cam, (int)size, (int)size);
      if (tex != 0)
      {
         dl->AddImage((ImTextureID)(intptr_t)tex, origin, ImVec2(origin.x + size, origin.y + size),
                      ImVec2(0, 1), ImVec2(1, 0));
      }
      else
      {
         EmptyState::DrawCaption(origin, ImVec2(origin.x + size, origin.y + size), EmptyPreviewLabel(gn.node.get(), "no geometry"));
      }
      dl->AddRect(origin, ImVec2(origin.x + size, origin.y + size),
                  tok::U32(tok::pal::c_464A5AFF), 4.0f);

      // Same drag-to-orbit / scroll-to-zoom feel as DrawPreview's embedded
      // Render3DNode viewport. Orbit rotates this node's own entry in
      // gNodeCameras (SharedViewportCamera.h) - shared with this same node's
      // viewport-panel card and with any camera-less Render3DNode this node
      // feeds directly, but not with any other node; Zoom stays local to this
      // NodeViewport instance's own distance.
      ImGui::SetCursorScreenPos(origin);
      ImGui::InvisibleButton("##miniviewportcanvas", ImVec2(size, size));

      if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
      {
         const ImVec2 drag = ImGui::GetIO().MouseDelta;
         // Degrees per pixel, matching DrawPreview's drag feel (0.6875 deg/px).
         viewport.Orbit(cam, drag.x * 0.6875f, drag.y * 0.6875f);
      }
      if (ImGui::IsItemHovered())
      {
         ImGuiIO& vio = ImGui::GetIO();
         if (vio.MouseWheel != 0.0f)
         {
            viewport.Zoom(vio.MouseWheel);
            vio.MouseWheel = 0.0f;
         }
      }
      // Numpad view hotkeys (1/3/7/0, Ctrl+1/3/7). No undo checkpoint here,
      // matching drag-orbit just above on this same gNodeCameras entry,
      // which also pushes none.
      ApplyViewHotkeys(cam.azimuth, cam.elevation);
      if (ImGui::IsItemHovered() || ImGui::IsItemActive())
         dl->AddRect(origin, ImVec2(origin.x + size, origin.y + size),
                     tok::U32(tok::pal::c_78C8FFC8), 4.0f, 0, 2.0f);
   }


   // One node's card inside the viewport panel: a title row and the render.
   // Same drag-to-orbit / scroll-to-zoom as the inline mini-viewport - it
   // rotates this node's own gNodeCameras entry (SharedViewportCamera.h), the
   // same one its inline mini-viewport uses, so the two stay in agreement;
   // a camera-less Render3DNode fed directly by this node mirrors it too.
   // Other nodes' viewports are unaffected. Zoom stays local to this card's
   // own NodeViewport distance, same as elsewhere. imageSize is the exact box
   // the render fills, so the card never has leftover space for a scrollbar
   // to appear in.
   // Whether a node has anything the viewport panel could actually show.
   // Mirrors the branches DrawPreview's dispatch takes in the node body: a
   // modulator is a value with a scope trace, not an image; Camera/Light show
   // a stat box and implement no geometry interface at all; a Comment is
   // text. All of those would render as a permanent "no input" box, so they
   // don't get offered the menu item in the first place.
   // ParticleSystemNode's viewport auto-frames once on the first handful of
   // spawned particles and never re-frames (NodeViewport::Render's mFramed
   // latch, set once and never cleared) - it locks onto a camera distance
   // sized for a near-empty emitter and stays there as the cloud grows past
   // it. Shared by the inline mini-viewport and the viewport panel/projector
   // path below, since both go through the same NodeViewport framing.
   bool HasUsefulMiniViewport(INode* n)
   {
      return dynamic_cast<ParticleSystemNode*>(n) == nullptr;
   }


   bool CanShowInViewportPanel(const GraphNode& gn)
   {
      INode* n = gn.node.get();
      if (dynamic_cast<IGeometrySource*>(n) != nullptr)
         return HasUsefulMiniViewport(n); // meshes render as their own solo viewport
      if (dynamic_cast<IModulator*>(n) != nullptr)
         return false;
      // The multi-output modulators, which draw their meters in the params
      // panel rather than producing an image.
      if (dynamic_cast<ImageAnalyzeNode*>(n) != nullptr ||
          dynamic_cast<AudioFileNode*>(n) != nullptr ||
          dynamic_cast<AudioAnalyzeNode*>(n) != nullptr)
         return false;
      if (dynamic_cast<CameraNode*>(n) != nullptr || dynamic_cast<LightNode*>(n) != nullptr)
         return false;
      if (dynamic_cast<CommentNode*>(n) != nullptr || dynamic_cast<GroupNode*>(n) != nullptr)
         return false;
      // Audio nodes emit samples, not pixels - GetOutputTexture() is 0 for all
      // of them, so a viewport panel card or projector window would show a
      // blank box. Gated generically (any node that sources or sinks audio)
      // rather than by type, so a new audio node added later can't forget to
      // opt out. P3d's Scope node is the deliberate exception: its spectrum
      // mode DOES emit an image texture (the audio->visual bridge), so it
      // will need an explicit carve-out here when it lands. Note-only nodes
      // (Note Sequencer, Envelope - P3a) get the same treatment: no pixels
      // either, and IsAudioBodyNode's gate above must agree with this one
      // or a note node would slip through to the viewport panel.
      // VideoSourceNode is the deliberate exception mentioned above: it's an
      // IAudioSource (its own audio track) but, unlike every other audio
      // node, GetOutputTexture() is a real video frame, not 0 - so it still
      // belongs in the viewport panel/projector.
      if ((dynamic_cast<IAudioSource*>(n) != nullptr && dynamic_cast<VideoSourceNode*>(n) == nullptr) ||
          n->AudioInputSlot(0) != nullptr ||
          dynamic_cast<INoteSource*>(n) != nullptr || n->NoteInputSlot(0) != nullptr)
         return false;
      return true;
   }


   // The dock picker, shared by the panel's own header and the Menu dropdown
   // so the two can't drift apart.
   void ViewportPanelDockCombo()
   {
      static const char* kDockLabels[] = { I18N_KEY("Bottom"), I18N_KEY("Right"), I18N_KEY("Left"), I18N_KEY("Top") };
      if (MenuParts::BeginCombo("##viewportdock", T(kDockLabels[gViewportPanelDock])))
      {
         for (int i = 0; i < 4; i++)
            if (MenuParts::Choice(L(kDockLabels[i]), i == gViewportPanelDock))
               gViewportPanelDock = i;
         MenuParts::EndCombo();
      }
   }


   // The title row's exact height: SmallButton is a text-height control (it
   // zeroes FramePadding.y), so this is what the row above the render costs.
   // Card and container both size against it, or the render overflows its
   // card by a few pixels and gets clipped.
   // Small breathing room between the title row and the render/image below
   // it - without this the render started at the exact next-line cursor with
   // zero gap, so it visually touched the title row.
   // A card is a group well: padding on every side, a 28 px title row (title left, close tile right), then
   // the render in a rounded tile.
   constexpr float kViewportCardPad = tok::space_2;
   constexpr float kViewportCardTitleRow = 28.0f;

   float ViewportPanelTitleHeight()
   {
      return kViewportCardTitleRow;
   }


   void DrawViewportPanelCard(GraphNode& gn, const ImVec2& cardSize)
   {
      const float titleH = ViewportPanelTitleHeight();
      const float pad = kViewportCardPad;
      // The render's own box: the card minus its padding and title row.
      const ImVec2 imageSize(std::max(16.0f, cardSize.x - pad * 2.0f), std::max(16.0f, cardSize.y - titleH - pad));
      char childId[32];
      snprintf(childId, sizeof(childId), "##viewportcard%d", gn.index);
      PushDockedPanelStyle(/*isChild=*/true);
      ImGui::BeginChild(childId, ImVec2(cardSize.x, cardSize.y), false,
                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 cardMin = ImGui::GetWindowPos();
      const ImVec2 cardMax(cardMin.x + cardSize.x, cardMin.y + cardSize.y);
      {
         ImVec4 w = ImGui::GetStyleColorVec4(ImGuiCol_Text);
         w.w *= 0.05f;
         dl->AddRectFilled(cardMin, cardMax, ImGui::GetColorU32(w), tok::radius_group);
      }

      // Title row: name on the left, a quiet close tile on the right (neutral wash on hover, no red).
      const float tile = 20.0f;
      ImGui::SetCursorScreenPos(ImVec2(cardMax.x - pad - tile, cardMin.y + (titleH - tile) * 0.5f + 2.0f));
      char closeId[40];
      snprintf(closeId, sizeof(closeId), "##closeviewportcard%d", gn.index);
      const bool closeRequested = ImGui::InvisibleButton(closeId, ImVec2(tile, tile));
      {
         const ImVec2 bmin = ImGui::GetItemRectMin();
         const ImVec2 bmax = ImGui::GetItemRectMax();
         const bool hot = ImGui::IsItemHovered();
         ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
         if (hot)
            dl->AddRectFilled(bmin, bmax, ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, ImGui::IsItemActive() ? 0.12f : 0.07f)),
                              tok::radius_tile);
         t.w *= hot ? 1.0f : 0.55f;
         glyph::DrawX(dl, ImVec2((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f), tile * 0.5f, ImGui::GetColorU32(t));
      }
      {
         UiType::Scope ts(UiType::Size::Body, UiType::Weight::Medium);
         const std::string title = NodeTitleWithInstance(gn);
         const float maxW = cardSize.x - pad * 2.0f - tile - tok::space_1;
         dl->PushClipRect(ImVec2(cardMin.x + pad, cardMin.y), ImVec2(cardMin.x + pad + std::max(1.0f, maxW), cardMax.y), true);
         dl->AddText(ImVec2(cardMin.x + pad, cardMin.y + (titleH - ImGui::GetTextLineHeight()) * 0.5f + 2.0f),
                     ImGui::GetColorU32(ImGuiCol_Text), title.c_str());
         dl->PopClipRect();
      }
      ImGui::SetCursorScreenPos(ImVec2(cardMin.x + pad, cardMin.y + titleH));

      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + imageSize.x, origin.y + imageSize.y);
      DrawCheckerboardBackdrop(dl, origin, br, tok::radius_tile);

      if (auto* geo = dynamic_cast<IGeometrySource*>(gn.node.get()))
      {
         // Same solo mesh render as the inline mini viewport, including its
         // orbit/zoom input handling. cam is the same gNodeCameras entry the
         // inline mini-viewport for this node index uses, so the two agree
         // even though they're separate NodeViewport/FBO instances.
         NodeViewport& viewport = gPanelViewports[gn.index];
         SharedViewportCamera& cam = gNodeCameras[gn.index];
         const unsigned int tex = viewport.Render(dynamic_cast<IGeometrySource*>(DisplayNode(gn.node.get())),
                                                  cam, (int)imageSize.x, (int)imageSize.y);
         if (tex != 0)
            dl->AddImageRounded((ImTextureID)(intptr_t)tex, origin, br, ImVec2(0, 1), ImVec2(1, 0), IM_COL32_WHITE, tok::radius_tile);
         else
            EmptyState::DrawCaption(origin, br, EmptyPreviewLabel(gn.node.get(), "no geometry"));

         ImGui::SetCursorScreenPos(origin);
         char btnId[32];
         snprintf(btnId, sizeof(btnId), "##viewportcanvas%d", gn.index);
         ImGui::InvisibleButton(btnId, imageSize);

         if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
         {
            const ImVec2 drag = ImGui::GetIO().MouseDelta;
            viewport.Orbit(cam, drag.x * 0.6875f, drag.y * 0.6875f);
         }
         if (ImGui::IsItemHovered())
         {
            ImGuiIO& vio = ImGui::GetIO();
            if (vio.MouseWheel != 0.0f)
            {
               viewport.Zoom(vio.MouseWheel);
               vio.MouseWheel = 0.0f;
            }
         }
         // Numpad view hotkeys (1/3/7/0, Ctrl+1/3/7). No undo checkpoint,
         // matching drag-orbit just above on this same gNodeCameras entry.
         ApplyViewHotkeys(cam.azimuth, cam.elevation);
         if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            dl->AddRect(origin, br, ImGui::GetColorU32(ImGuiCol_CheckMark, 0.6f), tok::radius_tile, 0, 1.0f);
         // Layout cursor already sits at origin+imageSize from the
         // InvisibleButton above; the trailing Dummy(imageSize) below is
         // shared with the non-geometry branch, so rewind rather than
         // double-reserve space in this child.
         ImGui::SetCursorScreenPos(origin);
      }
      else
      {
         // Everything else: blit the node's output texture, letterboxed to
         // its own aspect, mirroring DrawPreview's blit/"no input" fallback.
         INode* shown = DisplayNode(gn.node.get());
         const unsigned int tex = shown != nullptr ? shown->GetOutputTexture() : 0;
         float dw = imageSize.x, dh = imageSize.y;
         ImVec2 tl = origin;
         if (tex != 0 && shown->GetOutputWidth() > 0)
         {
            const float w = (float)shown->GetOutputWidth();
            const float h = (float)shown->GetOutputHeight();
            const float scale = std::min(imageSize.x / w, imageSize.y / h);
            dw = w * scale;
            dh = h * scale;
            tl = ImVec2(origin.x + (imageSize.x - dw) * 0.5f,
                       origin.y + (imageSize.y - dh) * 0.5f);
            dl->AddImage((ImTextureID)(intptr_t)tex, tl, ImVec2(tl.x + dw, tl.y + dh),
                         ImVec2(0, 1), ImVec2(1, 0));
         }
         else
         {
            EmptyState::DrawCaption(origin, ImVec2(origin.x + imageSize.x, origin.y + imageSize.y), EmptyPreviewLabel(gn.node.get(), "no input"));
         }

         // A Draw node's panel card is paintable, same as its inline preview
         // (DrawPaintablePreview) - mouse drags map to canvas u/v against the
         // letterboxed tl/dw/dh computed above, since the card isn't a fixed
         // square like the inline preview.
         if (auto* draw = dynamic_cast<DrawNode*>(gn.node.get()))
         {
            ImGui::SetCursorScreenPos(origin);
            char btnId[32];
            snprintf(btnId, sizeof(btnId), "##drawviewportcanvas%d", gn.index);
            ImGui::InvisibleButton(btnId, imageSize);

            if (ImGui::IsItemActive())
            {
               const ImVec2 m = ImGui::GetIO().MousePos;
               const float u = (m.x - tl.x) / std::max(1.0f, dw);
               const float v = 1.0f - (m.y - tl.y) / std::max(1.0f, dh); // texture space is bottom-up
               if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                  draw->BeginStroke(u, v);
               else
                  draw->ContinueStroke(u, v);
            }
            else if (ImGui::IsItemDeactivated())
            {
               draw->EndStroke();
            }

            ImGui::SetCursorScreenPos(origin);
         }

         // A Projection node's panel card supports draggable warping pins,
         // matching its inline preview
         if (auto* proj = dynamic_cast<ProjectionNode*>(gn.node.get()))
         {
            ImGui::SetCursorScreenPos(origin);
            char btnId[32];
            snprintf(btnId, sizeof(btnId), "##projviewportcanvas%d", gn.index);
            DrawProjectionHandleOverlay(proj, origin, imageSize, btnId);
            ImGui::SetCursorScreenPos(origin);
         }

         // A Render 3D card gets the same drag-to-orbit/scroll-to-zoom as its
         // inline preview (DrawPreview), driving that render's own camera -
         // separate from any node's mini-viewport/panel camera, see
         // SharedViewportCamera.h. Every other node stays a plain, static image.
         if (auto* render = dynamic_cast<Render3DNode*>(gn.node.get()))
         {
            ImGui::SetCursorScreenPos(origin);
            char btnId[32];
            snprintf(btnId, sizeof(btnId), "##render3dviewportcanvas%d", gn.index);
            ImGui::InvisibleButton(btnId, imageSize);

            float* azimuth = render->camera ? &render->camera->azimuth : &render->camAzimuth;
            float* elevation = render->camera ? &render->camera->elevation : &render->camElevation;
            float* distance = render->camera ? &render->camera->distance : &render->camDistance;

            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
               const ImVec2 drag = ImGui::GetIO().MouseDelta;
               *azimuth -= drag.x * 0.6875f;
               *elevation = std::max(-85.9437f, std::min(*elevation + drag.y * 0.6875f, 85.9437f));
            }
            if (ImGui::IsItemHovered())
            {
               ImGuiIO& vio = ImGui::GetIO();
               if (vio.MouseWheel != 0.0f)
               {
                  *distance = std::max(0.2f, std::min(*distance * (1.0f - vio.MouseWheel * 0.15f), 60.0f));
                  vio.MouseWheel = 0.0f;
               }
            }
            // Numpad view hotkeys (1/3/7/0, Ctrl+1/3/7) - undoable, same as
            // this render's "orbit"/"elevation" sliders (main.cpp ~23053).
            ApplyViewHotkeys(*azimuth, *elevation, []() { PushUndoCheckpoint(); });
            if (ImGui::IsItemHovered() || ImGui::IsItemActive())
               dl->AddRect(origin, br, ImGui::GetColorU32(ImGuiCol_CheckMark, 0.6f), tok::radius_tile, 0, 1.0f);
            ImGui::SetCursorScreenPos(origin);
         }
      }

      ImGui::Dummy(imageSize);
      ImGui::EndChild();
      PopDockedPanelStyle();

      if (closeRequested)
      {
         gViewportPanelNodes.erase(
            std::remove(gViewportPanelNodes.begin(), gViewportPanelNodes.end(), gn.index),
            gViewportPanelNodes.end());
         if (gViewportPanelNodes.empty())
            gViewportPanelOpen = false;
         // Note: this node's NodeViewport is NOT destroyed here - see the
         // deferred prune in the frame loop. Its texture has already been
         // submitted to this frame's draw list.
      }
   }


   // The dockable global viewport panel (see gViewportPanelNodes), opened via
   // a node's right-click menu -> "Open in viewport panel". Unlike the inline
   // per-node preview/mini-viewport above, this holds any number of nodes at
   // once - stacked left-to-right when docked top/bottom, or top-to-bottom
   // when docked left/right - in a single shared strip.
   //
   // Every render is sized to exactly fill the strip's short axis, so cards
   // butt up against each other with no dead space between them, and no card
   // ever has room left over for a scrollbar of its own.
   void DrawViewportPanelContainer()
   {
      const bool horizontal = (gViewportPanelDock == 0 || gViewportPanelDock == 3);

      // The render's fixed axis: the strip's height when cards run across it,
      // its width when they run down it. Squared off, since a mesh render has
      // no aspect of its own and an image is letterboxed into it anyway.
      //
      // Measured HERE, outside the scrolling child, and with the scroll
      // axis's scrollbar reserved unconditionally. Both matter: sizing cards
      // from the region *inside* the scrolling child feeds back on itself -
      // enough cards make a scrollbar appear, which shrinks the region, which
      // shrinks the cards, which can drop the total back under the scroll
      // threshold, which removes the scrollbar again. That loop is what makes
      // the panel flicker once a certain number of cards is open.
      const float bar = ImGui::GetStyle().ScrollbarSize;
      const ImVec2 panelOrigin = ImGui::GetCursorScreenPos();
      const ImVec2 strip = ImGui::GetContentRegionAvail();
      // The PanelFrame card around this already keeps space_2 to the edge on every side, so cards fill the strip.
      const float box = horizontal ? std::max(48.0f, strip.y - bar) : std::max(48.0f, strip.x - bar);

      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
      ImGui::BeginChild("##viewportcards", strip, false, horizontal ? ImGuiWindowFlags_HorizontalScrollbar : 0);
      ImGui::PopStyleVar();

      // Snapshot before drawing: a card's own close button mutates
      // gViewportPanelNodes, which would otherwise invalidate this loop.
      const std::vector<int> nodes = gViewportPanelNodes;
      bool first = true;
      for (int idx : nodes)
      {
         GraphNode* gn = FindNodeByIndex(idx);
         if (gn == nullptr)
         {
            // Stale - the node was deleted, or undo/redo rewound past it.
            // Drop silently rather than show stale content.
            gViewportPanelNodes.erase(
               std::remove(gViewportPanelNodes.begin(), gViewportPanelNodes.end(), idx),
               gViewportPanelNodes.end());
            continue;
         }

         if (!first && horizontal)
            ImGui::SameLine(0.0f, tok::space_2);
         first = false;

         DrawViewportPanelCard(*gn, ImVec2(box, box));
      }

      if (nodes.empty())
      {
         EmptyState::DrawInWindow(T("No active viewport cards"), T("Select nodes & press Shift+V or right-click to add"));
      }

      ImGui::EndChild();

      // Right-click anywhere on the panel - empty space or a card - to
      // reposition or close it. Replaces the dock-combo/close-button header
      // row, which cost a whole line of chrome for controls used rarely.
      //
      // A plain rect test against the mouse, not BeginPopupContextWindow:
      // the cards above are nested child windows, and ImGui's window-hover
      // test only sees the topmost window under the cursor, so a
      // context-window helper attached here would only fire in the gaps
      // between cards, not over the renders themselves.
      const ImVec2 mouse = ImGui::GetIO().MousePos;
      const bool overPanel = mouse.x >= panelOrigin.x && mouse.x < panelOrigin.x + strip.x &&
                             mouse.y >= panelOrigin.y && mouse.y < panelOrigin.y + strip.y;
      if (overPanel && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
         ImGui::OpenPopup("##viewportpanelctx");

      if (MenuParts::BeginPopup("##viewportpanelctx"))
      {
         static const char* kDockLabels[] = { I18N_KEY("Bottom"), I18N_KEY("Right"), I18N_KEY("Left"), I18N_KEY("Top") };
         for (int i = 0; i < 4; i++)
            if (MenuParts::Item(L(kDockLabels[i]), nullptr, i == gViewportPanelDock))
               gViewportPanelDock = i;
         MenuParts::Separator();
         if (MenuParts::Item(L("Close panel")))
            gViewportPanelOpen = false;
         if (!gViewportPanelNodes.empty())
         {
            if (MenuParts::Item(L("Clear all cards")))
            {
               gViewportPanelNodes.clear();
               gViewportPanelOpen = false;
            }
         }
         MenuParts::EndPopup();
      }
   }


   // The panel's outer frame at every dock position: a borderless child, and
   // no edge line of any kind. The panel's own opaque panelBg fill against the
   // canvas' windowBg is the whole separation - a hairline here (never mind a
   // full border, plus another around every card inside it) just stacked up
   // rules for no extra information, and was a recurring theme-regression
   // source besides.
   //
   // That same canvas-facing edge is the panel's resize handle: a thin strip
   // reserved inside the panel, so it belongs to the panel's own window and
   // reliably takes the hover (a strip drawn just outside would be over the
   // node canvas, whose window would win the hover instead).
   void DrawViewportPanelDocked(const char* id, const ImVec2& size)
   {
      const float kGrip = PanelFrame::kGap;   // the grip strip is the gap on the canvas-facing side
      const int dock = gViewportPanelDock;
      const bool vertical = (dock == 1 || dock == 2);  // grip is a column, not a row
      const bool gripFirst = (dock == 0 || dock == 1); // canvas is above / to the left

      // The outer child is what actually holds the resize-grip strip and the
      // spacing between it and the content child - and ImGuiCol_ChildBg is
      // alpha 0 by default (see ApplyTheme), so that ~11px band was showing
      // straight THROUGH the UI to whatever happens to be painted behind it.
      // That is the "black in light mode, grey in dark mode" band between
      // viewports: it was never a coloured divider, it was a hole. Give the
      // outer child the same opaque panelBg fill the content child already
      // has so the panel is solid edge to edge.
      // Outer child is transparent: the panel floats as a card on the canvas colour (PanelFrame).
      ImGui::BeginChild(id, size, false);
      const ImVec2 inner = ImGui::GetContentRegionAvail();

      auto grip = [&]()
      {
         ImGui::InvisibleButton("##viewportgrip",
                                vertical ? ImVec2(kGrip, std::max(1.0f, inner.y))
                                         : ImVec2(std::max(1.0f, inner.x), kGrip));
         // The panel's canvas-facing boundary, and the only thing that marks
         // it: one hairline, same colour and same 1px weight at every dock
         // edge (see DrawPanelSeam).
         DrawPanelSeam(vertical, gripFirst);
         if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(vertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
         if (ImGui::IsItemActive())
         {
            // Each dock grows toward the canvas, so the sign flips with the
            // side the panel is on.
            const ImVec2 d = ImGui::GetIO().MouseDelta;
            switch (dock)
            {
               case 0: gViewportPanelHeight -= d.y; break;
               case 1: gViewportPanelWidth -= d.x; break;
               case 2: gViewportPanelWidth += d.x; break;
               default: gViewportPanelHeight += d.y; break;
            }
            gViewportPanelWidth = std::max(kViewportPanelMinWidth, gViewportPanelWidth);
            gViewportPanelHeight = std::max(kViewportPanelMinHeight, gViewportPanelHeight);
         }
      };

      if (gripFirst)
      {
         grip();
         if (vertical)
            ImGui::SameLine();
      }

      // The grip and the content sit on one layout axis with ImGui's item
      // spacing between them, so the content has to give up that spacing as
      // well as the grip itself - otherwise the two together overrun the
      // panel and the panel grows a scrollbar of its own.
      const ImVec2 gap = ImGui::GetStyle().ItemSpacing;
      const ImVec2 cardSize = vertical ? ImVec2(std::max(1.0f, inner.x - kGrip - gap.x), inner.y)
                                       : ImVec2(std::max(1.0f, inner.x), std::max(1.0f, inner.y - kGrip - gap.y));
      PanelFrame::Insets in;
      switch (dock)   // the grip side needs no inset of its own
      {
         case 0: in.t = 0.0f; break;
         case 1: in.l = 0.0f; break;
         case 2: in.r = 0.0f; break;
         default: in.b = 0.0f; break;
      }
      PushDockedPanelStyle(/*isChild=*/true);
      PanelFrame::BeginCard("##viewportpanelcontent", cardSize, in);
      PopDockedPanelStyle();
      DrawViewportPanelContainer();
      PanelFrame::EndCard();

      if (!gripFirst)
      {
         if (vertical)
            ImGui::SameLine();
         grip();
      }
      // Zeroed only around EndChild, which is where ImGui charges the gap
      // that follows this panel - the panel's own contents above keep normal
      // spacing. Without this, the ItemSpacing between the panel and whatever
      // is laid out next shows a strip of the shell window's windowBg, which
      // reads as a bar separating the two viewports.
      // Set directly, not pushed: a push made in the child and popped in the parent unbalances both stacks.
      const ImVec2 savedSpacing = ImGui::GetStyle().ItemSpacing;
      ImGui::GetStyle().ItemSpacing = ImVec2(0.0f, 0.0f);
      ImGui::EndChild();
      ImGui::GetStyle().ItemSpacing = savedSpacing;

      // No divider line along the canvas-facing edge, in either theme. This
      // hairline was a fixed dark constant, then a theme-derived one, and was
      // reported as a wrong-coloured seam both times - like the dialog border
      // above, it straddles two different backgrounds (panel fill on one side,
      // canvas on the other), so no single colour is right against both and
      // every theme change re-breaks it. The panel already reads as separate
      // because its opaque panelBg fill differs from the canvas' windowBg;
      // the line added no information. Removed rather than re-tuned.
   }


   // The dock picker for the modulation matrix, shared by the panel's own
   // right-click menu and the Menu dropdown - see ViewportPanelDockCombo.
   void ModMatrixDockCombo()
   {
      static const char* kDockLabels[] = { I18N_KEY("Bottom"), I18N_KEY("Right"), I18N_KEY("Left"), I18N_KEY("Top") };
      if (MenuParts::BeginCombo("##modmatrixdock", T(kDockLabels[gModMatrixDock])))
      {
         for (int i = 0; i < 4; i++)
            if (MenuParts::Choice(L(kDockLabels[i]), i == gModMatrixDock))
               gModMatrixDock = i;
         MenuParts::EndCombo();
      }
   }
}
