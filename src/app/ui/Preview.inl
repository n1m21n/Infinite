// Part of src/main.cpp, included there in order (one translation unit).
// previews, comments, groups, mini viewport, viewport panel

   void ExportImage(OutputNode* out, const std::string& path, int jpgQuality = 90)
   {
      int w = out->GetOutputWidth();
      int h = out->GetOutputHeight();
      if (w <= 0 || h <= 0)
         return;

      std::vector<unsigned char> pixels(w * h * 4);
      GLint prevFbo = 0;
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
      GLuint fbo = 0;
      glGenFramebuffers(1, &fbo);
      glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, out->GetOutputTexture(), 0);
      glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
      glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
      glDeleteFramebuffers(1, &fbo);

      stbi_flip_vertically_on_write(1);

      std::string lowerPath = path;
      for (char& c : lowerPath) c = (char)tolower((unsigned char)c);

      if (lowerPath.length() >= 4 && (lowerPath.rfind(".jpg") == lowerPath.length() - 4 || lowerPath.rfind(".jpeg") == lowerPath.length() - 5))
      {
         // Convert RGBA to RGB for JPEG compatibility
         std::vector<unsigned char> rgb(w * h * 3);
         for (int i = 0; i < w * h; i++)
         {
            rgb[i * 3 + 0] = pixels[i * 4 + 0];
            rgb[i * 3 + 1] = pixels[i * 4 + 1];
            rgb[i * 3 + 2] = pixels[i * 4 + 2];
         }
         stbi_write_jpg(path.c_str(), w, h, 3, rgb.data(), jpgQuality);
      }
      else
      {
         stbi_write_png(path.c_str(), w, h, 4, pixels.data(), w * 4);
      }
   }

   void ExportPng(OutputNode* out, const std::string& path)
   {
      ExportImage(out, path);
   }

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
                  IM_COL32(90, 130, 190, 255), 4.0f, 0, 2.0f);
   }

   // A comment shows its note on the face of the node, not behind the params
   // (eye) toggle - the whole point of a note is to be readable without an
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
      // the double-click below hit-tests, so it has to be a real item.
      ImGui::Dummy(ImVec2(w, h));
      const bool hovered = ImGui::IsItemHovered();

      dl->AddRectFilled(origin, br,
                        IM_COL32((int)(n->color[0] * 40), (int)(n->color[1] * 40),
                                 (int)(n->color[2] * 40), 255), 4.0f);
      dl->AddRect(origin, br,
                  IM_COL32((int)(n->color[0] * 255), (int)(n->color[1] * 255),
                           (int)(n->color[2] * 255), 200), 4.0f, 0, 1.5f);

      const ImU32 textCol = IM_COL32((int)((n->color[0] * 0.6f + 0.4f) * 255),
                                     (int)((n->color[1] * 0.6f + 0.4f) * 255),
                                     (int)((n->color[2] * 0.6f + 0.4f) * 255), 255);
      // Clipped to the box so a note longer than its height is cut off at the
      // edge instead of spilling over the params below it.
      const float drawFontSize = ImGui::GetFontSize() * CommentFontScale(n->fontSize);
      dl->PushClipRect(origin, br, true);
      if (n == gCommentEdit.target)
      {
         // the editor popup sits on top; drawing the text too would double it
      }
      else if (n->text.empty())
         dl->AddText(ImGui::GetFont(), drawFontSize, ImVec2(origin.x + 8, origin.y + 6), IM_COL32(150, 150, 160, 255),
                     "double-click or type to write", nullptr, w - 16.0f);
      else
         dl->AddText(ImGui::GetFont(), drawFontSize,
                     ImVec2(origin.x + 8, origin.y + 6), textCol,
                     n->text.c_str(), nullptr, w - 16.0f);

      // bottom-right resize grip
      const ImU32 gripCol = IM_COL32((int)(n->color[0] * 160 + 60), (int)(n->color[1] * 160 + 60),
                                     (int)(n->color[2] * 160 + 60), 160);
      for (int i = 0; i < 3; i++)
      {
         const float off = 4.0f + (float)i * 4.0f;
         dl->AddLine(ImVec2(br.x - off, br.y - 3.0f), ImVec2(br.x - 3.0f, br.y - off), gripCol, 1.2f);
      }
      dl->PopClipRect();

      {
         const ImVec2 tl = ed::CanvasToScreen(origin);
         const ImVec2 rb = ed::CanvasToScreen(br);
         gCommentBodyRect = ImVec4(tl.x, tl.y, rb.x - tl.x, rb.y - tl.y);
         if (n == gCommentEdit.target)
            gCommentEditRect = gCommentBodyRect;
      }

      // Corner drag resizes. An active item inside the node keeps the node
      // editor from dragging the node itself; the delta is divided by the zoom
      // because the canvas is scaled.
      const float gripSize = 20.0f;
      const ImVec2 afterBody = ImGui::GetCursorScreenPos();
      ImGui::SetCursorScreenPos(ImVec2(br.x - gripSize, br.y - gripSize));
      ImGui::InvisibleButton("##commentresizegrip", ImVec2(gripSize, gripSize));
      const bool gripHovered = ImGui::IsItemHovered();
      const bool gripActive = ImGui::IsItemActive();
      if (ImGui::IsItemActivated())
         PushUndoCheckpoint();
      if (gripHovered || gripActive)
         ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
      if (gripActive)
      {
         const float zoom = std::max(0.01f, ed::GetCurrentZoom());
         const ImGuiIO& io = ImGui::GetIO();
         n->width = std::clamp(n->width + io.MouseDelta.x / zoom, 120.0f, 1600.0f);
         n->height = std::clamp(n->height + io.MouseDelta.y / zoom, 60.0f, 1200.0f);
         gPatchDirty = true;
      }
      ImGui::SetCursorScreenPos(afterBody);

      // Double-click, Enter, or just start typing while the pointer is over
      // the note. Shift-typing is left out so Shift+A / Shift+D keep working
      // over a comment.
      ImGuiIO& io = ImGui::GetIO();
      if (hovered && !gripHovered && !gripActive && gCommentEdit.target == nullptr && !io.WantTextInput)
      {
         bool open = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
         std::string typed;
         if (!open && !io.KeyCtrl && !io.KeySuper && !io.KeyAlt && !io.KeyShift)
         {
            if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))
               open = true;
            else
               for (ImWchar ch : io.InputQueueCharacters)
                  if (ch > 32 && ch != 127 && ch != '/')
                  {
                     char utf8[5] = {};
                     ImTextCharToUtf8(utf8, ch);
                     typed += utf8;
                  }
            if (!typed.empty())
               open = true;
         }
         if (open)
         {
            PushUndoCheckpoint(); // before the edit, so one undo restores the old note
            if (!typed.empty())
            {
               n->text += typed;
               gCommentEdit.cursorToEnd = true;
               io.InputQueueCharacters.resize(0);
               gPatchDirty = true;
            }
            gCommentEdit.target = n;
            gCommentEdit.justOpened = true;
         }
      }
   }

   void DrawCommentParams(CommentNode* n)
   {
      ModSlider("width", &n->width, 120.0f, 1600.0f, "%.0f");
      ModSlider("height", &n->height, 60.0f, 1200.0f, "%.0f");
      ColorSwatch("colour", n->color, n);
      static const std::vector<std::string> kSizes = { "Small", "Normal", "Large", "Extra Large" };
      DropdownButton("font size", kSizes, n->fontSize, [n](int i) { n->fontSize = i; gPatchDirty = true; });
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
   // Because the box is fully derived from the member set, a manual resize
   // does not survive as a size; what it does instead is decide membership.
   // Dragging an edge out over a loose node adopts that node on the next
   // frame, and the box then refits around the enlarged set - so resizing
   // still reads as "reach out and grab that one too", which is the only
   // thing resizing a group is ever really for.
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
   // get the stick-together behavior at all. The label sits above the
   // resizable box as the node's only ordinary content, so it becomes the
   // group's Header region - the part the editor lets you drag by.
   //
   // The header is plain text, not a text field: an ImGui widget spanning
   // most of the header would capture clicks itself (the same reason a
   // slider inside an ordinary node's body doesn't drag the node), leaving
   // almost nowhere on the header for the editor's own drag-to-move to ever
   // trigger. Renaming instead happens in-place on double-click.
   //
   // ed::Group(size) only honors `size` the first frame a node becomes a
   // group; after that the library tracks the user's own resize (and our own
   // growth, via SetGroupSize) internally and ignores whatever we pass. So
   // width/height here exist purely to seed that first frame and to survive
   // save/load - every subsequent frame they are overwritten from the node's
   // actual measured size, the same pattern liveX/liveY use for position.
   void DrawGroupNode(GraphNode& gn, GroupNode* n)
   {
      AutoFitGroupToMembers(gn, n);

      ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(0, 0, 0, 0));
      ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(0, 0, 0, 0));
      ed::PushStyleColor(ed::StyleColor_GroupBg,
                         ImColor(n->color[0], n->color[1], n->color[2], 0.10f));
      ed::PushStyleColor(ed::StyleColor_GroupBorder,
                         ImColor(n->color[0], n->color[1], n->color[2], 0.85f));

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
      const CategoryColors::UiTheme& t = CategoryColors::CurrentUiTheme();
      const bool isLight = (0.2126f * t.windowBg.r + 0.7152f * t.windowBg.g + 0.0722f * t.windowBg.b > 0.5f);
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
      if (ModTriggerButton("Clear canvas##drawClear", ImVec2(kPreviewSize, 0)))
         n->ClearCanvas();

      NodeSeparator("animation");
      const bool recordingStrokes = n->IsRecordingStrokes();
      bool requestedRecordingStrokes = recordingStrokes;
      if (recordingStrokes)
         ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.15f, 0.15f, 1.0f));
      if (ModStateButton(recordingStrokes ? "Stop rec##drawRecord" : "Rec strokes##drawRecord",
                         recordingStrokes, requestedRecordingStrokes, ImVec2(kPreviewSize * 0.48f, 0)))
      {
         if (requestedRecordingStrokes)
            n->StartRecording();
         else
            n->StopRecording();
      }
      if (recordingStrokes)
         ImGui::PopStyleColor();
      ImGui::SameLine();
      const bool playingBack = n->IsPlayingBack();
      bool requestedPlayingBack = playingBack;
      if (ModStateButton(playingBack ? "Stop##drawReplay" : "Replay##drawReplay",
                         playingBack, requestedPlayingBack, ImVec2(kPreviewSize * 0.48f, 0)))
      {
         if (requestedPlayingBack)
            n->PlayRecording();
         else
            n->StopPlayback();
      }

      if (n->RecordedStamps() > 0)
      {
         ImGui::TextDisabled("%zu marks over %.1f beats", n->RecordedStamps(), n->RecordedLength());
         if (n->IsPlayingBack())
            ImGui::TextColored(ImVec4(0.5f, 0.95f, 0.6f, 1.0f), "playhead %.1f", n->PlayheadBeats());
      }
      ModCheckbox("loop replay", &n->loopPlayback);
      ModSlider("replay speed", &n->playSpeed, 0.1f, 4.0f);
      if (ModTriggerButton("clear recording##drawClearRecording", ImVec2(kPreviewSize, 0)))
         n->ClearRecording();
      ModSlider("canvas w", &n->canvasWidth, 64.0f, 4096.0f, "%.0f");
      ModSlider("canvas h", &n->canvasHeight, 64.0f, 4096.0f, "%.0f");
   }

   // Square, letterboxed preview so non-square sources still read 1:1.
   // Render 3D viewport mouse, TouchDesigner style: left drag orbits, middle
   // drag (or Shift + left) pans the target in the view plane, right drag
   // dollies, the wheel zooms. Called right after the viewport's
   // InvisibleButton, which must accept all three buttons. A patched Camera
   // node owns the view, so its values move instead of the built-in ones.
   void Render3DViewportControls(Render3DNode* render, float viewPx)
   {
      const bool hasCam = render->camera != nullptr;
      float* azimuth = hasCam ? &render->camera->azimuth : &render->camAzimuth;
      float* elevation = hasCam ? &render->camera->elevation : &render->camElevation;
      float* distance = hasCam ? &render->camera->distance : &render->camDistance;
      float* tx = hasCam ? &render->camera->targetX : &render->targetX;
      float* ty = hasCam ? &render->camera->targetY : &render->targetY;
      float* tz = hasCam ? &render->camera->targetZ : &render->targetZ;
      const bool ortho = (hasCam ? render->camera->projection : render->projection) == 1;
      const float fov = hasCam ? render->camera->fov : render->fov;
      const float orthoH = hasCam ? render->camera->orthoHeight : render->orthoHeight;

      ImGuiIO& io = ImGui::GetIO();
      if (ImGui::IsItemHovered())
         gViewport3DHoveredNow = true;
      if (!ImGui::IsItemActive())
      {
         if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f)
         {
            *distance = std::max(0.2f, std::min(*distance * (1.0f - io.MouseWheel * 0.15f), 60.0f));
            io.MouseWheel = 0.0f; // consumed, or the canvas zooms too
         }
         return;
      }
      const ImVec2 d = io.MouseDelta;
      if (d.x == 0.0f && d.y == 0.0f)
         return;
      const bool pan = ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
                       (ImGui::IsMouseDown(ImGuiMouseButton_Left) && io.KeyShift);
      if (pan)
      {
         const float az = *azimuth * (float)M_PI / 180.0f, el = *elevation * (float)M_PI / 180.0f;
         // Eye direction from the target (see Render3DNode's camera setup).
         const float dir[3] = { std::cos(el) * std::cos(az), std::sin(el), std::cos(el) * std::sin(az) };
         // right = normalize(cross(forward, worldUp)), forward = -dir.
         float right[3] = { dir[2], 0.0f, -dir[0] };
         const float rl = std::sqrt(right[0] * right[0] + right[2] * right[2]);
         if (rl > 1e-6f) { right[0] /= rl; right[2] /= rl; }
         else { right[0] = 1.0f; right[2] = 0.0f; }
         // up = cross(right, forward)
         const float up[3] = { -(right[1] * dir[2] - right[2] * dir[1]), -(right[2] * dir[0] - right[0] * dir[2]),
                               -(right[0] * dir[1] - right[1] * dir[0]) };
         const float worldPerPx = (ortho ? orthoH : 2.0f * *distance * std::tan(fov * 0.5f * (float)M_PI / 180.0f)) /
                                  std::max(1.0f, viewPx);
         // The picture follows the mouse: the target moves the other way.
         *tx += (-right[0] * d.x + up[0] * d.y) * worldPerPx;
         *ty += (-right[1] * d.x + up[1] * d.y) * worldPerPx;
         *tz += (-right[2] * d.x + up[2] * d.y) * worldPerPx;
      }
      else if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
      {
         *distance = std::max(0.2f, std::min(*distance * (1.0f + d.y * 0.01f), 60.0f));
      }
      else if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
      {
         // Degrees per pixel (0.6875 matches the old 0.012 rad/px feel).
         *azimuth -= d.x * 0.6875f;
         // Clamped short of the poles: overhead would make the view roll.
         *elevation = std::max(-85.9437f, std::min(*elevation + d.y * 0.6875f, 85.9437f));
      }
   }

   void DrawPreview(INode* node)
   {
      unsigned int tex = node->GetOutputTexture();
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

      if (tex != 0 && node->GetOutputWidth() > 0)
      {
         float w = (float)node->GetOutputWidth();
         float h = (float)node->GetOutputHeight();
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
                     IM_COL32(120, 120, 135, 255), "no input");
      }

      dl->AddRect(origin, ImVec2(origin.x + size, origin.y + size),
                  IM_COL32(70, 74, 90, 255), 4.0f);

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
      ImGui::InvisibleButton("##viewport", ImVec2(size, size),
                             ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
      Render3DViewportControls(render, size);

      float* azimuth = render->camera ? &render->camera->azimuth : &render->camAzimuth;
      float* elevation = render->camera ? &render->camera->elevation : &render->camElevation;

      // Numpad view hotkeys (1/3/7/0, Ctrl+1/3/7). Unlike drag-orbit above,
      // a hotkey view snap on this render's own camera is treated as a
      // deliberate edit worth undoing - PushUndoCheckpoint() before the
      // write, the same as the "orbit"/"elevation" sliders for this same
      // camera (main.cpp ~23053).
      ApplyViewHotkeys(*azimuth, *elevation, []() { PushUndoCheckpoint(); });

      if (ImGui::IsItemHovered() || ImGui::IsItemActive())
         dl->AddRect(origin, ImVec2(origin.x + size, origin.y + size),
                     IM_COL32(120, 200, 255, 200), 4.0f, 0, 2.0f);
   }

   // Rolling history so a modulator reads like a scope rather than a number.
   std::map<int, std::vector<float>> gModHistory;

   // Per-node mini 3D viewport GL state, keyed by GraphNode::index - same
   // per-index-map pattern as gModHistory above. Lazily created the first time
   // a node's viewport toggle is switched on; nothing is allocated for a node
   // that never opts in.
   std::map<int, NodeViewport> gNodeViewports;

   // Every node's own default-camera rotation (SharedViewportCamera.h),
   // keyed by GraphNode::index. A node's inline mini-viewport (above) and its
   // viewport-panel card (gPanelViewports below) both read/write the same
   // entry here, so the two agree on rotation even though they're separate
   // NodeViewport/FBO instances; two different nodes never share an entry.
   // Render3DNode keeps its own completely separate camAzimuth/camElevation
   // fields (Geometry3DNodes.h) - the final output's framing is a deliberate
   // setup, not something casual node-viewport orbiting should ever move.
   std::map<int, SharedViewportCamera> gNodeCameras;

   // Nodes/viewports retired by RemoveNodeByIndex this frame, held until the
   // start of the next frame - see RemoveNodeByIndex for why teardown can't
   // happen synchronously. NodeViewport has a user-declared destructor (which
   // suppresses its implicit move ctor), so it can't live in a
   // vector<NodeViewport> without a copy that would double-free its GL
   // texture; map::node_type from extract() moves the whole tree node instead
   // of the mapped value, sidestepping that.
   std::vector<std::unique_ptr<INode>> gRetiredNodes;
   // Turbo 0.47: retired nodes may still be in the process list the audio
   // thread is running. They are destroyed only once two audio blocks have
   // finished since the last topology publish (or audio is off). Fixes the
   // crash on File > New with audio playing (NewPatch never republished the
   // topology, so the audio thread kept running freed nodes).
   uint64_t gRetiredSafeAfterBlocks = 0;
   std::vector<std::map<int, NodeViewport>::node_type> gRetiredViewports;

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

      if (isMaterial)
      {
         const float offset = std::max(0.0f, (kWideNodeWidth - size) * 0.5f);
         if (offset > 0.0f)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
      }

      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      DrawCheckerboardBackdrop(dl, origin, size);

      const unsigned int tex = viewport.Render(geo, cam, (int)size, (int)size);
      if (tex != 0)
      {
         dl->AddImage((ImTextureID)(intptr_t)tex, origin, ImVec2(origin.x + size, origin.y + size),
                      ImVec2(0, 1), ImVec2(1, 0));
      }
      else
      {
         dl->AddText(ImVec2(origin.x + 10, origin.y + size * 0.5f - 8),
                     IM_COL32(120, 120, 135, 255), "no geometry");
      }
      dl->AddRect(origin, ImVec2(origin.x + size, origin.y + size),
                  IM_COL32(70, 74, 90, 255), 4.0f);

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
                     IM_COL32(120, 200, 255, 200), 4.0f, 0, 2.0f);
   }

   // Viewport-panel 3D renders, kept in their own map rather than sharing
   // gNodeViewports with the inline per-node toggle above: the two draw at
   // different sizes, and one NodeViewport reallocates its FBO whenever the
   // requested size changes - sharing would make a node that is open in both
   // places thrash its FBO every frame.
   std::map<int, NodeViewport> gPanelViewports;

   // Same reasoning as gPanelViewports above, one more map for projector
   // windows: a geometry node has no meaningful GetOutputTexture() of its own
   // (see GeometryNode::GetOutputTexture's comment - "geometry itself
   // produces no image"), so a projector window showing one needs its own
   // solo render, same mechanism the viewport panel/mini-viewport use, sized
   // to that window rather than thrashing a shared FBO.
   std::map<int, NodeViewport> gProjectorViewports;

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
          dynamic_cast<AudioAnalyzeNode*>(n) != nullptr ||
          dynamic_cast<OscToCvNode*>(n) != nullptr)
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
      // Video is deliberately a mixed-media exception: it implements
      // IAudioSource for its second output but its first output is a texture.
      // VMPC too: a note input, but its output is an image.
      const bool mixedMediaVideo = dynamic_cast<VideoSourceNode*>(n) != nullptr;
      if (dynamic_cast<VmpcNode*>(n) != nullptr || dynamic_cast<ClipMatrixNode*>(n) != nullptr)
         return true;
      if (dynamic_cast<ClipMatrixOutNode*>(n) != nullptr)
         return false;
      if ((!mixedMediaVideo && dynamic_cast<IAudioSource*>(n) != nullptr) || n->AudioInputSlot(0) != nullptr ||
          dynamic_cast<INoteSource*>(n) != nullptr || n->NoteInputSlot(0) != nullptr)
         return false;
      return true;
   }

   bool CanToggleInlinePreview(const GraphNode& gn)
   {
      INode* n = gn.node.get();
      // UI controls remain interactive in the node body at all times; their
      // monitor toggle controls only the history/scope overlay.
      if (dynamic_cast<UIButtonNode*>(n) != nullptr ||
          dynamic_cast<UISliderNode*>(n) != nullptr ||
          dynamic_cast<UIXYNode*>(n) != nullptr)
         return true;
      if (dynamic_cast<IGeometrySource*>(n) != nullptr)
         return HasUsefulMiniViewport(n);
      return CanShowInViewportPanel(gn);
   }

   bool InlinePreviewEnabled(const GraphNode& gn)
   {
      if (dynamic_cast<IGeometrySource*>(gn.node.get()) != nullptr)
         return gn.showMiniViewport;
      return gn.showPreview;
   }

   void SetInlinePreviewEnabled(GraphNode& gn, bool enabled)
   {
      if (dynamic_cast<IGeometrySource*>(gn.node.get()) != nullptr)
         gn.showMiniViewport = enabled;
      else
         gn.showPreview = enabled;
   }

   // The dock picker, shared by the panel's own header and the Menu dropdown
   // so the two can't drift apart.
   void ViewportPanelDockCombo()
   {
      static const char* kDockLabels[] = { "Bottom", "Right", "Left", "Top" };
      if (ImGui::BeginCombo("##viewportdock", kDockLabels[gViewportPanelDock]))
      {
         for (int i = 0; i < 4; i++)
            if (ImGui::Selectable(kDockLabels[i], i == gViewportPanelDock))
               gViewportPanelDock = i;
         ImGui::EndCombo();
      }
   }

   // The title row's exact height: SmallButton is a text-height control (it
   // zeroes FramePadding.y), so this is what the row above the render costs.
   // Card and container both size against it, or the render overflows its
   // card by a few pixels and gets clipped.
   float ViewportPanelTitleHeight()
   {
      return ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.y;
   }

   void DrawViewportPanelCard(GraphNode& gn, const ImVec2& imageSize)
   {
      const float titleH = ViewportPanelTitleHeight();
      char childId[32];
      snprintf(childId, sizeof(childId), "##viewportcard%d", gn.index);
      ImGui::BeginChild(childId, ImVec2(imageSize.x, imageSize.y + titleH), false,
                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

      const bool closeRequested = ImGui::SmallButton("X");
      ImGui::SameLine();
      ImGui::TextUnformatted(NodeTitleWithInstance(gn).c_str());

      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + imageSize.x, origin.y + imageSize.y);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      DrawCheckerboardBackdrop(dl, origin, br);

      if (auto* geo = dynamic_cast<IGeometrySource*>(gn.node.get()))
      {
         // Same solo mesh render as the inline mini viewport, including its
         // orbit/zoom input handling. cam is the same gNodeCameras entry the
         // inline mini-viewport for this node index uses, so the two agree
         // even though they're separate NodeViewport/FBO instances.
         NodeViewport& viewport = gPanelViewports[gn.index];
         SharedViewportCamera& cam = gNodeCameras[gn.index];
         const unsigned int tex = viewport.Render(geo, cam, (int)imageSize.x, (int)imageSize.y);
         if (tex != 0)
            dl->AddImage((ImTextureID)(intptr_t)tex, origin, br, ImVec2(0, 1), ImVec2(1, 0));
         else
            dl->AddText(ImVec2(origin.x + 10, origin.y + imageSize.y * 0.5f - 8),
                        IM_COL32(120, 120, 135, 255), "no geometry");

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
            dl->AddRect(origin, br, IM_COL32(120, 200, 255, 200), 4.0f, 0, 2.0f);
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
         const unsigned int tex = gn.node->GetOutputTexture();
         float dw = imageSize.x, dh = imageSize.y;
         ImVec2 tl = origin;
         if (tex != 0 && gn.node->GetOutputWidth() > 0)
         {
            const float w = (float)gn.node->GetOutputWidth();
            const float h = (float)gn.node->GetOutputHeight();
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
            dl->AddText(ImVec2(origin.x + 10, origin.y + imageSize.y * 0.5f - 8),
                        IM_COL32(120, 120, 135, 255), "no input");
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
            ImGui::InvisibleButton(btnId, imageSize,
                                   ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
            Render3DViewportControls(render, std::min(imageSize.x, imageSize.y));

            float* azimuth = render->camera ? &render->camera->azimuth : &render->camAzimuth;
            float* elevation = render->camera ? &render->camera->elevation : &render->camElevation;
            // Numpad view hotkeys (1/3/7/0, Ctrl+1/3/7) - undoable, same as
            // this render's "orbit"/"elevation" sliders (main.cpp ~23053).
            ApplyViewHotkeys(*azimuth, *elevation, []() { PushUndoCheckpoint(); });
            if (ImGui::IsItemHovered() || ImGui::IsItemActive())
               dl->AddRect(origin, br, IM_COL32(120, 200, 255, 200), 4.0f, 0, 2.0f);
            ImGui::SetCursorScreenPos(origin);
         }
      }

      ImGui::Dummy(imageSize);
      ImGui::EndChild();

      if (closeRequested)
      {
         gViewportPanelNodes.erase(
            std::remove(gViewportPanelNodes.begin(), gViewportPanelNodes.end(), gn.index),
            gViewportPanelNodes.end());
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
      const float box = horizontal ? std::max(48.0f, strip.y - bar)
                                   : std::max(48.0f, strip.x - bar);

      ImGui::BeginChild("##viewportcards", strip, false,
                        horizontal ? ImGuiWindowFlags_HorizontalScrollbar : 0);

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
            ImGui::SameLine();
         first = false;

         DrawViewportPanelCard(*gn, ImVec2(box, box));
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

      if (ImGui::BeginPopup("##viewportpanelctx"))
      {
         static const char* kDockLabels[] = { "Bottom", "Right", "Left", "Top" };
         for (int i = 0; i < 4; i++)
            if (ImGui::MenuItem(kDockLabels[i], nullptr, i == gViewportPanelDock))
               gViewportPanelDock = i;
         ImGui::Separator();
         if (ImGui::MenuItem("Close panel"))
            gViewportPanelNodes.clear();
         ImGui::EndPopup();
      }
   }

   // The panel's outer frame at every dock position: a borderless child plus
   // exactly one hairline along the canvas-facing edge. One line is all the
   // separation this needs - a full border here (and another around every
   // card inside it) just stacked up rules for no extra information.
   //
   // That same canvas-facing edge is the panel's resize handle: a thin strip
   // reserved inside the panel, so it belongs to the panel's own window and
   // reliably takes the hover (a strip drawn just outside would be over the
   // node canvas, whose window would win the hover instead).
   void DrawViewportPanelDocked(const char* id, const ImVec2& size)
   {
      const float kGrip = 6.0f;
      const int dock = gViewportPanelDock;
      const bool vertical = (dock == 1 || dock == 2);  // grip is a column, not a row
      const bool gripFirst = (dock == 0 || dock == 1); // canvas is above / to the left

      ImGui::BeginChild(id, size, false);
      const ImVec2 inner = ImGui::GetContentRegionAvail();

      auto grip = [&]()
      {
         ImGui::InvisibleButton("##viewportgrip",
                                vertical ? ImVec2(kGrip, std::max(1.0f, inner.y))
                                         : ImVec2(std::max(1.0f, inner.x), kGrip));
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
      ImGui::BeginChild("##viewportpanelcontent",
                        vertical ? ImVec2(std::max(1.0f, inner.x - kGrip - gap.x), 0)
                                 : ImVec2(0, std::max(1.0f, inner.y - kGrip - gap.y)),
                        false);
      DrawViewportPanelContainer();
      ImGui::EndChild();

      if (!gripFirst)
      {
         if (vertical)
            ImGui::SameLine();
         grip();
      }
      ImGui::EndChild();

      const ImVec2 tl = ImGui::GetItemRectMin();
      const ImVec2 br = ImGui::GetItemRectMax();
      const ImU32 line = IM_COL32(70, 74, 90, 255);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      switch (dock)
      {
         case 0: dl->AddLine(tl, ImVec2(br.x, tl.y), line); break;          // bottom dock -> top edge
         case 1: dl->AddLine(tl, ImVec2(tl.x, br.y), line); break;          // right dock -> left edge
         case 2: dl->AddLine(ImVec2(br.x, tl.y), br, line); break;          // left dock -> right edge
         default: dl->AddLine(ImVec2(tl.x, br.y), br, line); break;         // top dock -> bottom edge
      }
   }

   // Turbo: range mapper for one binding - which part of the incoming 0..1
   // is used, and onto which part of the parameter's range it lands. Shown
   // in the binding popup (right-click a modulated parameter).
   // Turbo node colour markers (index 0 = none).
   constexpr int kNodeTagCount = 9;
