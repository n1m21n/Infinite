// Part of src/main.cpp, included there in order (one translation unit).
// deferred dropdown, modulatable params, readouts, sliders, MIDI learn pins, knobs

   // ---- deferred dropdown -------------------------------------------------
   // ImGui combos opened inside a node get clipped and mis-scaled by the node
   // editor's canvas transform. Instead a node draws a plain button, records
   // what it wants to show, and the popup is rendered once per frame outside
   // the canvas (inside ed::Suspend) as a normal, scrollable ImGui popup.
   struct DropdownRequest
   {
      // Stored BY VALUE, not as a pointer to the caller's vector: several call
      // sites pass an inline temporary (e.g. `{ "Free", "Synced" }`), which is
      // destroyed at the end of that statement. The popup itself doesn't
      // render until later - ImGui::OpenPopup here just flags it, and
      // BeginPopup happens further down this same frame - so a stored pointer
      // would already be dangling by then. This was the Stutter "Synced"
      // crash: its sync dropdown is exactly such a temporary.
      std::vector<std::string> options;
      std::vector<std::string> categories;
      std::function<void(int)> onSelect;
      int current = 0;
      bool justOpened = false;
   };
   // Turbo 0.51 (upstream f08e6bf): lowercases and strips accents so the dropdown
   // search finds "cafe" from "café". Latin-1 supplement and Latin Extended-A
   // (U+00C0..U+017F); anything else passes through unchanged.
   inline std::string FoldForSearch(const std::string& in)
   {
      static const char* const kFold[0x180 - 0xC0] = {
         "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
         "d", "n", "o", "o", "o", "o", "o", "", "o", "u", "u", "u", "u", "y", "th", "ss",
         "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
         "d", "n", "o", "o", "o", "o", "o", "", "o", "u", "u", "u", "u", "y", "th", "y",
         "a", "a", "a", "a", "a", "a", "c", "c", "c", "c", "c", "c", "c", "c", "d", "d",
         "d", "d", "e", "e", "e", "e", "e", "e", "e", "e", "e", "e", "g", "g", "g", "g",
         "g", "g", "g", "g", "h", "h", "h", "h", "i", "i", "i", "i", "i", "i", "i", "i",
         "i", "i", "ij", "ij", "j", "j", "k", "k", "k", "l", "l", "l", "l", "l", "l", "l",
         "l", "l", "l", "n", "n", "n", "n", "n", "n", "n", "n", "n", "o", "o", "o", "o",
         "o", "o", "oe", "oe", "r", "r", "r", "r", "r", "r", "s", "s", "s", "s", "s", "s",
         "s", "s", "t", "t", "t", "t", "t", "t", "u", "u", "u", "u", "u", "u", "u", "u",
         "u", "u", "u", "u", "w", "w", "y", "y", "y", "z", "z", "z", "z", "z", "z", "s"
      };
      std::string out;
      out.reserve(in.size());
      for (size_t i = 0; i < in.size(); i++)
      {
         const unsigned char c = (unsigned char)in[i];
         if (c < 0x80)
            out += (char)std::tolower(c);
         else if ((c == 0xC3 || c == 0xC4 || c == 0xC5) && i + 1 < in.size())
         {
            const unsigned cp = ((c & 0x1Fu) << 6) | ((unsigned char)in[i + 1] & 0x3Fu);
            out += kFold[cp - 0xC0];
            i++;
         }
         else
            out += (char)c;
      }
      return out;
   }

   DropdownRequest gDropdown;

   void PrepareDropdown(const std::vector<std::string>& options,
                        const std::vector<std::string>& categories, int current,
                        std::function<void(int)> onSelect, bool keepOrder = false)
   {
      std::vector<int> order(options.size());
      for (int i = 0; i < (int)order.size(); ++i) order[i] = i;
      // Turbo: keepOrder lists the options as given (categories in their own
      // order, e.g. the drum pattern library), and current = -1 means no
      // item is marked, so every pick fires onSelect, even the same one twice.
      auto lower = [](std::string s) {
         std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
         return s;
      };
      if (!keepOrder)
         std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
         const std::string ca = a < (int)categories.size() ? lower(categories[a]) : std::string();
         const std::string cb = b < (int)categories.size() ? lower(categories[b]) : std::string();
         return ca == cb ? lower(options[a]) < lower(options[b]) : ca < cb;
      });
      gDropdown.options.clear();
      gDropdown.categories.clear();
      int sortedCurrent = (keepOrder && current < 0) ? -1 : 0;
      for (int i = 0; i < (int)order.size(); ++i)
      {
         const int original = order[i];
         gDropdown.options.push_back(options[original]);
         gDropdown.categories.push_back(original < (int)categories.size() ? categories[original] : std::string());
         if (original == current) sortedCurrent = i;
      }
      gDropdown.onSelect = [order, onSelect = std::move(onSelect)](int sorted) {
         if (onSelect && sorted >= 0 && sorted < (int)order.size()) onSelect(order[sorted]);
      };
      gDropdown.current = sortedCurrent;
      gDropdown.justOpened = true;
   }

   // Same story for ImGui's colour picker: opened inside a node it inherits the
   // canvas transform and the hue bar / sliders stop tracking the cursor. Route
   // it through a popup rendered outside the canvas instead.
   struct ColorRequest
   {
      float* target = nullptr;
      const INode* owner = nullptr; // revalidated each frame; nodes can be deleted
      std::string label;
      bool justOpened = false;
   };
   ColorRequest gColor;
   ImVec4 gColorPickerRect(0, 0, 0, 0); // x, y, w, h of the picker widget on screen

   // And again for a comment's text. A multiline text field is an ImGui child
   // window, which the canvas cannot transform at all: drawn inside the node it
   // rendered the note at the canvas origin, nowhere near the comment it
   // belonged to. So the note is painted into the node by hand and typing into
   // it happens in a popup out here (see DrawCommentPreview).
   struct CommentEditRequest
   {
      CommentNode* target = nullptr; // revalidated each frame; nodes can be deleted
      bool justOpened = false;
      // The double-click that opens the editor is also a click on the canvas,
      // and the canvas takes the focus back when the button comes up - one
      // frame after the popup appeared. So the field asks for the keyboard for
      // the first few frames rather than only on the frame it appears, which
      // otherwise left it open but dead until it was clicked a third time.
      int framesOpen = 0;
      bool cursorToEnd = false; // opened by typing: the typed text is already in, caret after it
   };
   CommentEditRequest gCommentEdit;

   inline float CommentFontScale(int sizeIdx)
   {
      switch (sizeIdx)
      {
         case 0: return 0.82f;
         case 2: return 1.35f;
         case 3: return 1.85f;
         default: return 1.0f;
      }
   }
   // Screen-space rect of the last comment body drawn, so the dev test can aim
   // a synthetic double-click at it. Inside a node ImGui draws in canvas space,
   // hence the explicit conversion where this is filled in.
   ImVec4 gCommentBodyRect(0, 0, 0, 0); // x, y, w, h
   // Screen-space rect of whichever comment is currently open for editing,
   // refreshed every frame so the edit popup can be pinned exactly on top of
   // it - the point being that typing looks like it happens straight into the
   // node's own box rather than in a separate window somewhere else on screen.
   ImVec4 gCommentEditRect(0, 0, 0, 0); // x, y, w, h
   FormulaNode* gFormulaEditor = nullptr;
   bool gFormulaEditorOpen = false;
   bool gGlobalsOpen = false;
   bool gHelpOpen = false;

   // Files dropped on the window, consumed on the next frame so the spawn can
   // happen inside the editor where canvas coordinates are meaningful.
   std::vector<std::string> gDroppedFiles;
   ImVec2 gDropPos(0.0f, 0.0f);

   bool HasExtension(const std::string& path, const std::vector<std::string>& exts)
   {
      std::string cleanPath = path;
      while (cleanPath.size() > 1 && (cleanPath.back() == '/' || cleanPath.back() == '\\'))
         cleanPath.pop_back();
      size_t dot = cleanPath.find_last_of('.');
      if (dot == std::string::npos)
         return false;
      std::string ext = cleanPath.substr(dot + 1);
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
      for (const std::string& e : exts)
      {
         if (ext == e)
            return true;
      }
      return false;
   }

   // Shared by the OS file-drop handler and the Media search panel's
   // canvas drag-and-drop resolution, so both classify a given path
   // identically (see MediaExtensions.h).
   const std::vector<std::string>& kVideoExt = MediaExtensions::Video();
   const std::vector<std::string>& kImageExt = MediaExtensions::Image();

   // Finds the first node under a canvas-space point whose node.get()
   // dynamic_casts to T - the Samples/Media drag-drop release handler's hit
   // test, factored out so it isn't copy-pasted once per draggable node
   // type (SamplerNode, VideoSourceNode, ImageSourceNode). Deliberately a
   // plain rect test against ed::GetNodePosition/GetNodeSize rather than
   // ed::GetHoveredNode() - see the release handler's own comment for why.
   template <typename T>
   T* FindNodeUnderCanvasPoint(const ImVec2& canvasPoint)
   {
      for (GraphNode& gn : gNodes)
      {
         auto* typed = gn.node ? dynamic_cast<T*>(gn.node.get()) : nullptr;
         if (typed == nullptr)
            continue;
         const ImVec2 p = ed::GetNodePosition(gn.NodeId());
         const ImVec2 s = ed::GetNodeSize(gn.NodeId());
         if (canvasPoint.x >= p.x && canvasPoint.x <= p.x + s.x && canvasPoint.y >= p.y &&
             canvasPoint.y <= p.y + s.y)
            return typed;
      }
      return nullptr;
   }

   // Resolves a canvas-space y (ed::ScreenToCanvas'd from a drag release or
   // a file-drop's real mouse position) to a lane index against whatever
   // DrumSequencerNode last cached its grid rect during
   // DrawDrumSequencerBody - see the fields' comment on DrumSequencerNode
   // for why this is canvas space, not real screen space.
   int DrumSequencerLaneForCanvasY(DrumSequencerNode* n, float canvasY)
   {
      if (n->gridCanvasRowH <= 0.0f)
         return 0;
      const int lane = (int)((canvasY - n->gridCanvasTopY) / n->gridCanvasRowH);
      return std::clamp(lane, 0, DrumSequencerNode::kNumLanes - 1);
   }

   // Same idea, but checks each lane card's cached rect first - that's what
   // a drop landing on a card (rather than down on the step grid) is
   // actually aiming at. The grid-only version above always read as
   // "before row 0" for a card drop, since every card sits above the
   // grid's own rect, which is why every card drop used to resolve to lane
   // 0 regardless of which card the cursor was over.
   int DrumSequencerLaneForCanvasPos(DrumSequencerNode* n, float canvasX, float canvasY)
   {
      for (int lane = 0; lane < DrumSequencerNode::kNumLanes; lane++)
      {
         if (canvasX >= n->laneCardCanvasX0[lane] && canvasX <= n->laneCardCanvasX1[lane] &&
             canvasY >= n->laneCardCanvasY0[lane] && canvasY <= n->laneCardCanvasY1[lane])
            return lane;
      }
      return DrumSequencerLaneForCanvasY(n, canvasY);
   }

   void OnFilesDropped(GLFWwindow*, int count, const char** paths)
   {
      gDropPos = ImGui::GetMousePos();
      for (int i = 0; i < count; i++)
         gDroppedFiles.push_back(paths[i]);
   }

   // Turbo 0.57: Infinite Dark (2026) param pin. A dark well with a ring when
   // idle; when driven, the ring takes the source colour, the centre fills and
   // a soft halo pulses so live modulation reads at a glance.
   // state: 0 idle, 1 modulated, 2 expression.
   void DrawDesignParamPin(ImDrawList* dl, ImVec2 c, int state)
   {
      const float r = tok::pin_r_param;
      dl->AddCircleFilled(c, r, tok::U32(tok::pin_well, false), 16);
      if (state == 0)
      {
         dl->AddCircle(c, r, tok::U32(tok::pin_idle, false), 16, 1.5f);
         return;
      }
      const ImU32 col = tok::U32(state == 1 ? tok::pin_mod : tok::pin_expr, false);
      const float pulse = 0.5f + 0.5f * sinf((float)ImGui::GetTime() * 5.0f);
      dl->AddCircleFilled(c, r + 2.0f + pulse * 1.5f, (col & 0x00FFFFFFu) | ((ImU32)(40 + 50 * pulse) << 24), 16);
      dl->AddCircle(c, r, col, 16, 1.5f);
      dl->AddCircleFilled(c, tok::pin_r_param_on * 0.6f, col, 12);
   }

   void DropdownButton(const char* label, const std::vector<std::string>& options,
                       int current, std::function<void(int)> onSelect,
                       float width = kParamWidth, bool modulatable = true);

   // ---- modulatable parameters --------------------------------------------
   // Every slider that can be driven by a modulator goes through ModSlider. It
   // draws an input pin beside the control, registers the parameter for this
   // frame so the modulator can write into it, and shows the live value (and
   // locks the slider) while something is patched in.
   int gCurrentNodeIndex = -1;
   int gParamCounter = 0;
   // Buttons, checkboxes and dropdowns live in a separate, label-derived
   // parameter range. Numeric parameter ordinals are part of the patch format;
   // mixing new discrete controls into gParamCounter would silently retarget
   // every existing modulation cable that follows one of them.
   constexpr int kDiscreteParamBase = 256;
   constexpr int kDiscreteParamCount = 544; // 256..799, below GraphNode::kColorBase
   std::map<std::pair<int, int>, float> gDiscreteParamStore;
   std::map<std::pair<int, int>, bool> gDiscretePreviousHigh;
   bool gApplyingDiscreteCv = false;
   std::map<std::string, int> gDiscreteLabelOccurrences;
   std::map<int, std::string> gDiscreteLabelsThisNode;
   // Colour pins are counted separately from parameter pins (see
   // GraphNode::kColorBase): sharing the counter would renumber every slider
   // that follows a swatch and repoint existing patches' modulation.
   int gColorCounter = 0;
   std::set<int> gDrawnColorPins;
   // Defined further down, once the gNodes lookup exists.
   IPaletteSource* PaletteSourceByIndex(int nodeIndex);
   std::set<std::pair<int, int>> gTypedParam;                 // params showing a text field
   // Params that entered typing mode via hover+type rather than double-click;
   // these get the cursor placed at the end instead of a full selection, so
   // the seeded digit is appended to rather than replaced by the next keystroke.
   std::set<std::pair<int, int>> gTypedParamNoAutoSelect;
   // Params whose InputFloat selection/cursor state still needs to be forced
   // once the field is confirmed active (see the comment at its use site).
   std::set<std::pair<int, int>> gTypedParamPendingInit;
   // Live text for the field currently being typed into. A plain number
   // commits as a value and clears any expression; text starting with '='
   // commits as an expression (see ModSlider) - the same field doubles as
   // both, like a spreadsheet cell.
   std::map<std::pair<int, int>, std::string> gTypedParamText;

   std::string TrimCopy(const std::string& s)
   {
      size_t start = s.find_first_not_of(" \t");
      if (start == std::string::npos)
         return std::string();
      size_t end = s.find_last_not_of(" \t");
      return s.substr(start, end - start + 1);
   }
   // Turbo 0.59: whether committed field text is an expression, and which.
   // Any '=' makes it one, not only a leading one: a double-click seeds the
   // field with the old value, and when that seed is not replaced the typed
   // formula lands after it ("0.500=t % 1"), which used to commit as the
   // number 0.5 and silently drop the formula. A real number never holds '='.
   bool TypedTextExpression(const std::string& trimmed, std::string& exprOut)
   {
      const size_t eq = trimmed.find('=');
      if (eq == std::string::npos)
         return false;
      size_t from = eq;
      while (from < trimmed.size() && trimmed[from] == '=')
         from++; // "==t" reads as "t"
      exprOut = TrimCopy(trimmed.substr(from));
      return true;
   }
   // Param pins declared this frame. A node with its params collapsed declares
   // none, and emitting a link to an undeclared pin makes the editor treat the
   // link as dead and delete it - which silently dropped the modulation.
   std::set<int> gDrawnParamPins;
   // Nodes to select next frame; they do not exist in the editor until they
   // have been drawn once, so selection has to wait a frame.
   std::vector<int> gPendingSelect;
   bool gPendingNavigateSelection = false;
   // Sidebar picks are consumed inside the editor on the next frame. Creating
   // them after ed::End left node-editor without a live item for selection and
   // was especially visible with Feedback, whose empty preview has no texture
   // size to help the user find a stacked spawn.
   struct SidebarSpawnRequest { std::string name, category; };
   std::deque<SidebarSpawnRequest> gPendingSidebarSpawns;
   int gPendingSidebarRevealNodeId = -1;
   int gSidebarSpawnCascade = 0;
   std::map<int, std::vector<float>> gUiHistoryX;
   std::map<int, std::vector<float>> gUiHistoryY;
   std::pair<int, int> gTypedParamJustOpened(-1, -1);
   std::vector<int> gParamPinsThisFrame;
   // Set when a right-click on a param already opened its text field this
   // frame, so the node-level right-click context menu (checked later, after
   // ed::Suspend()) knows to stay closed instead of covering the field.
   bool gParamRightClickConsumedThisFrame = false;

   void BeginNodeParams(int nodeIndex)
   {
      gCurrentNodeIndex = nodeIndex;
      gParamCounter = 0;
      gColorCounter = 0;
      gDiscreteLabelOccurrences.clear();
      gDiscreteLabelsThisNode.clear();
   }

   // Turbo 0.51: a console-taper dB control reads "-inf dB" at its floor (the
   // DSP already treats <= -59.9 dB as silence).
   void FormatParamValue(char* buf, size_t n, const char* fmt, float v, bool dbTaper)
   {
      if (dbTaper && v <= -59.9f)
         snprintf(buf, n, "-inf dB");
      else
         snprintf(buf, n, fmt, v);
   }

   // Opens the text field for a param, seeded either from its current numeric
   // value or (if it's already driven by an expression) from that expression
   // text with its '=' prefix - used by both double-click and right-click.
   void BeginTypedEditFromCurrent(const std::pair<int, int>& editKey, int nodeIndex, int paramIndex,
                                   float* value, const char* fmt, bool hasExpr, bool dbTaper = false)
   {
      if (hasExpr)
      {
         const std::string* expr = Modulation::Instance().ExpressionFor(nodeIndex, paramIndex);
         gTypedParamText[editKey] = std::string("=") + (expr != nullptr ? *expr : std::string());
      }
      else
      {
         char seed[64];
         FormatParamValue(seed, sizeof(seed), fmt, *value, dbTaper);
         gTypedParamText[editKey] = seed;
      }
      gTypedParam.insert(editKey);
      gTypedParamJustOpened = editKey;
      gTypedParamPendingInit.insert(editKey);
   }

   // Hovering a param (whether it's a plain slider or already showing an
   // expression) and pressing a digit/'-'/'.'/'=' jumps straight into typing
   // mode with that keystroke seeded, without needing to double-click first.
   // '=' always starts a blank formula - even over an existing one, so typing
   // '=' is a reliable way back into formula mode - and a digit always starts
   // a blank numeric entry, overriding whatever mode the param was already in.
   void HandleParamTypeHotkeys(const std::pair<int, int>& editKey, float* value)
   {
      ImGuiIO& io = ImGui::GetIO();
      for (int i = 0; i < io.InputQueueCharacters.Size; ++i)
      {
         ImWchar ch = io.InputQueueCharacters[i];
         if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '.' || ch == '=')
         {
            PushUndoCheckpoint();
            if (ch == '=')
               gTypedParamText[editKey] = "=";
            else if (ch >= '0' && ch <= '9')
            {
               *value = (float)(ch - '0');
               gTypedParamText[editKey] = std::string(1, (char)ch);
            }
            else if (ch == '-')
            {
               *value = -0.0f;
               gTypedParamText[editKey] = "-";
            }
            else
            {
               *value = 0.0f;
               gTypedParamText[editKey] = ".";
            }
            gTypedParam.insert(editKey);
            gTypedParamJustOpened = editKey;
            gTypedParamNoAutoSelect.insert(editKey);
            gTypedParamPendingInit.insert(editKey);
            break;
         }
      }
   }

   // ---- audio node value readout -----------------------------------------
   // v2 showed a hovered knob's value via ImGui::SetTooltip from inside
   // ed::Begin()/ed::End() with no ed::Suspend() around it, so the node
   // editor's canvas transform was applied to the tooltip and it landed
   // offset from the cursor by an amount proportional to zoom and pan (v3
   // §1a - the "hover value floats somewhere else" report).
   //
   // The fix is not a correctly-positioned tooltip: it's not having one.
   // Every plugin this UI is benchmarked against puts the hovered/dragged
   // param's name and value in ONE fixed readout location per panel, which
   // never occludes the control being adjusted and never moves. A control
   // writes here; BeginAudioBody draws last frame's string in the node's
   // readout strip. The one-frame lag is imperceptible, and it buys a
   // strictly top-down layout with no second pass.
   std::map<int, std::string> gAudioReadout;

   void SetAudioReadout(const char* label, const char* valueText)
   {
      if (gCurrentNodeIndex < 0)
         return;
      const char* name = label;
      if (name != nullptr && name[0] == '#')
         name = "";
      char buf[96];
      snprintf(buf, sizeof(buf), "%s   %s", name != nullptr ? name : "", valueText);
      gAudioReadout[gCurrentNodeIndex] = buf;
   }

   // ---- horizontal audio slider -------------------------------------------
   // Label left, value right, both *inside* the track; a low-alpha fill from
   // the left edge to the current value instead of a grab handle. This is
   // the layout every plugin uses for a horizontal parameter, and it
   // replaces v2's ImGui-default look (label stranded in a ragged column to
   // the right of the widget, value floating mid-track, a lone purple grab
   // block) for audio nodes only - visual nodes keep plain ModSlider.
   //
   // Interaction is still ImGui::SliderFloat: the grab is styled fully
   // transparent and the format string emptied, so the widget behaves
   // exactly as it always has (drag, clamp, Ctrl+click, the
   // IsItemActivated/IsItemHovered surface ModSlider's surrounding logic
   // depends on) and only the pixels are ours.
   bool AudioSliderFloat(const char* label, float* value, float minV, float maxV, const char* fmt,
                         float width, ImU32 fillColor, bool readOnly)
   {
      const bool isLight = IsThemeLight();
      if (isLight)
      {
         ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.88f, 0.89f, 0.92f, 1.0f));
         ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.83f, 0.85f, 0.89f, 1.0f));
         ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.78f, 0.81f, 0.86f, 1.0f));
      }
      else
      {
         ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.055f, 0.060f, 0.080f, 1.0f));
         ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.085f, 0.092f, 0.118f, 1.0f));
         ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.100f, 0.108f, 0.138f, 1.0f));
      }
      ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
      ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);

      std::string id = std::string("##as_") + label;
      ImGui::SetNextItemWidth(width);
      const bool changed = ImGui::SliderFloat(id.c_str(), value, minV, maxV, "",
                                              readOnly ? ImGuiSliderFlags_NoInput : ImGuiSliderFlags_None);
      ImGui::PopStyleVar();
      ImGui::PopStyleColor(5);

      const ImVec2 r0 = ImGui::GetItemRectMin();
      const ImVec2 r1 = ImGui::GetItemRectMax();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const float t = (maxV > minV) ? std::clamp((*value - minV) / (maxV - minV), 0.0f, 1.0f) : 0.0f;
      const float fillX = r0.x + (r1.x - r0.x) * t;
      if (t > 0.0f)
      {
         // Deliberately low alpha: the fill is a tint the label and value
         // stay readable over, not a solid block that hides them.
         const ImU32 alpha = isLight ? 110 : 86;
         const ImU32 soft = (fillColor & 0x00FFFFFF) | (alpha << 24);
         dl->AddRectFilled(r0, ImVec2(fillX, r1.y), soft, 3.0f);
      }
      dl->AddRect(r0, r1, isLight ? IM_COL32(180, 185, 200, 255) : IM_COL32(72, 76, 92, 255), 3.0f);

      std::string name(label);
      const size_t hash = name.find("##");
      if (hash != std::string::npos)
         name = name.substr(0, hash);
      char valBuf[48];
      snprintf(valBuf, sizeof(valBuf), fmt, *value);
      const float textY = r0.y + (r1.y - r0.y - ImGui::GetTextLineHeight()) * 0.5f;
      const ImVec2 valSize = ImGui::CalcTextSize(valBuf);
      const float valX = r1.x - 7.0f - valSize.x;
      dl->PushClipRect(r0, r1, true);
      const ImU32 valCol = isLight
         ? (readOnly ? IM_COL32(110, 115, 130, 255) : IM_COL32(25, 30, 45, 255))
         : (readOnly ? IM_COL32(190, 192, 206, 255) : IM_COL32(232, 236, 246, 255));
      dl->AddText(ImVec2(valX, textY), valCol, valBuf);

      dl->PushClipRect(r0, ImVec2(std::max(r0.x, valX - 6.0f), r1.y), true);
      const ImU32 nameCol = isLight
         ? (readOnly ? IM_COL32(125, 130, 145, 255) : IM_COL32(186, 192, 208, 255))
         : (readOnly ? IM_COL32(150, 152, 166, 255) : IM_COL32(186, 192, 208, 255));
      dl->AddText(ImVec2(r0.x + 7.0f, textY), nameCol, name.c_str());
      dl->PopClipRect();
      dl->PopClipRect();

      if (ImGui::IsItemHovered() || ImGui::IsItemActive())
         SetAudioReadout(name.c_str(), valBuf);
      return changed;
   }

   // Right-click on a modulated (read-only) param requests this: a polarity
   // toggle plus, in Bipolar mode, a depth slider - and Unbind, so there's
   // still a way to detach a cable without dragging it off or selecting the
   // link itself. Deliberately a context menu, not a panel - see 00-
   // modulation-polarity.md's "keep it minimal".
   //
   // Only records the request here - see gModBindingMenuNode's comment for
   // why the popup itself has to be opened and drawn later, outside
   // ed::Begin()/ed::End(), rather than right where the param is hovered.
   void DrawModulationBindingMenu(int nodeIndex, int paramIndex, bool hovered)
   {
      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
      {
         gModBindingMenuNode = nodeIndex;
         gModBindingMenuParam = paramIndex;
         gOpenModBindingMenu = true;
         gParamRightClickConsumedThisFrame = true;
      }
   }

   // ---- Turbo MIDI learn: pin decoration + where each param's pin is -----
   // Every CV pin drawn this frame records its centre (in canvas space) and
   // label, so a click in MIDI learn mode can pick the parameter under the
   // mouse, and a mapped / armed parameter can be marked on its pin.
   struct ParamPinSpot
   {
      ImVec2 c;
      std::string name;
   };
   std::map<std::pair<int, int>, ParamPinSpot> gParamPinSpots;
   int gMidiMenuNode = -1;
   int gMidiMenuParam = -1;
   bool gOpenMidiMenu = false;
   bool gMidiWindowOpen = false;
   int gMidiWindowSelected = -1;
   bool gModMatrixOpen = false; // Turbo: VIEW > Modulation matrix (Shift+M)

   std::string CleanParamLabel(const char* label)
   {
      std::string s = label != nullptr ? label : "";
      const size_t hidden = s.find("##");
      if (hidden != std::string::npos)
         s = s.substr(0, hidden);
      return s.empty() ? std::string("control") : s;
   }

   void DecorateParamPin(ImDrawList* dl, const ImVec2& c, int nodeIndex, int paramIndex, const char* label)
   {
      gParamPinSpots[{ nodeIndex, paramIndex }] = { c, CleanParamLabel(label) };
      const double now = ImGui::GetTime();
      if (MidiMap::IsArmed(nodeIndex, paramIndex))
      {
         const float pulse = 0.5f + 0.5f * std::sin((float)now * 10.0f);
         dl->AddCircle(c, 7.5f, IM_COL32(255, 150, 40, (int)(110 + 145 * pulse)), 16, 2.5f);
      }
      else if (MidiMap::Find(nodeIndex, paramIndex) != nullptr)
      {
         const bool hot = MidiMap::RecentlyActive(nodeIndex, paramIndex, now);
         dl->AddCircle(c, 7.0f, hot ? IM_COL32(150, 255, 170, 255) : IM_COL32(70, 200, 110, 230), 16, 2.0f);
      }
      else if (MidiMap::LearnMode())
         dl->AddCircle(c, 7.0f, IM_COL32(255, 150, 40, 120), 16, 1.5f);

      // Turbo: gesture state - red while a Shift-drag records, green while a
      // recorded loop plays on this param.
      const GestureRecorder& gr = GestureRecorder::Instance();
      if (gr.Playbacks().count(GestureRecorder::Key(nodeIndex, paramIndex)) > 0)
         dl->AddCircleFilled(ImVec2(c.x + 5.5f, c.y - 5.5f), 2.5f, IM_COL32(90, 220, 120, 255));
      else if (ImGui::GetIO().KeyShift && gr.IsRecording(nodeIndex, paramIndex))
         dl->AddCircleFilled(ImVec2(c.x + 5.5f, c.y - 5.5f), 2.5f, IM_COL32(240, 70, 70, 255));
   }

   // Turbo (from upstream): Shift-drag records a param's movement; releasing
   // Shift turns the trace into a loop that replays while the transport plays.
   // A plain grab (no Shift) of a looping param takes it back.
   void GestureWidgetHook(int nodeIndex, int paramIndex, float value, bool active, bool activated)
   {
      if (nodeIndex < 0)
         return;
      GestureRecorder& gr = GestureRecorder::Instance();
      const bool shift = ImGui::GetIO().KeyShift;
      if (activated && !shift)
         gr.StopPlayback(nodeIndex, paramIndex);
      if (active && shift)
         gr.NotifyMovement(nodeIndex, paramIndex, value, gr.RecordClockNow(), activated);
   }

   struct DiscreteParamRef
   {
      int nodeIndex = -1;
      int paramIndex = -1;
      float* value = nullptr;
      bool modulated = false;
      bool valid = false;
   };

   int StableDiscreteParamIndex(const char* label)
   {
      std::string stable = label != nullptr ? label : "control";
      const size_t hiddenId = stable.find("##");
      if (hiddenId != std::string::npos)
         stable = stable.substr(hiddenId + 2);
      const int occurrence = gDiscreteLabelOccurrences[stable]++;
      if (occurrence > 0)
         stable += "#" + std::to_string(occurrence);

      unsigned int hash = 2166136261u;
      for (unsigned char c : stable)
      {
         hash ^= c;
         hash *= 16777619u;
      }

      int index = kDiscreteParamBase + (int)(hash % kDiscreteParamCount);
      for (int probe = 0; probe < kDiscreteParamCount; ++probe)
      {
         auto used = gDiscreteLabelsThisNode.find(index);
         if (used == gDiscreteLabelsThisNode.end() || used->second == stable)
         {
            gDiscreteLabelsThisNode[index] = stable;
            return index;
         }
         index = kDiscreteParamBase + ((index - kDiscreteParamBase + 1) % kDiscreteParamCount);
      }
      return kDiscreteParamBase;
   }

   // Turbo 0.46: a Performance Mode write to a control backed by a per-frame
   // slot (discrete, int) - consumed where that slot is refreshed, so the
   // write is not overwritten by the node's own value before it lands.
   std::map<std::pair<int, int>, float> gPerfSlotWrites;
   bool TakePerfSlotWrite(const std::pair<int, int>& key, float& out)
   {
      auto it = gPerfSlotWrites.find(key);
      if (it == gPerfSlotWrites.end())
         return false;
      out = it->second;
      gPerfSlotWrites.erase(it);
      return true;
   }

   DiscreteParamRef BeginDiscreteParam(const char* label, float currentValue,
                                       float minValue, float maxValue, int kind = 0,
                                       const std::vector<std::string>* options = nullptr)
   {
      DiscreteParamRef result;
      if (gCurrentNodeIndex < 0)
         return result;

      result.nodeIndex = gCurrentNodeIndex;
      result.paramIndex = StableDiscreteParamIndex(label);
      const std::pair<int, int> key(result.nodeIndex, result.paramIndex);
      float& slot = gDiscreteParamStore[key];
      // A MIDI mapping drives a discrete control like a CV does (the store
      // keeps the value MIDI wrote instead of being reset from the UI).
      result.modulated = Modulation::Instance().IsModulated(result.nodeIndex, result.paramIndex) ||
                         MidiMap::Find(result.nodeIndex, result.paramIndex) != nullptr;
      float perfValue = 0.0f;
      if (TakePerfSlotWrite(key, perfValue))
      {
         slot = perfValue; // Turbo 0.46: driven by the Performance Mode this frame
         result.modulated = true;
      }
      else if (!result.modulated)
         slot = currentValue;

      ParamRef ref;
      ref.nodeIndex = result.nodeIndex;
      ref.paramIndex = result.paramIndex;
      ref.value = &slot;
      ref.minValue = minValue;
      ref.maxValue = maxValue;
      ref.name = label != nullptr ? label : "control";
      ref.step = 1.0f; // kind: 0 plain, 1 checkbox, 2 trigger, 3 dropdown
      ref.isBool = kind == 1;
      ref.momentary = kind == 2;
      if (kind == 3 && options != nullptr)
      {
         ref.isEnum = true;
         ref.enumOptions = *options;
      }
      Modulation::Instance().RegisterParam(ref);

      const int pinId = result.nodeIndex * GraphNode::kStride + GraphNode::kParamBase + result.paramIndex;
      gDrawnParamPins.insert(pinId);
      ImGui::PushID(result.paramIndex + 15000);
      ed::BeginPin(pinId, ed::PinKind::Input);
      ed::PinPivotAlignment(ImVec2(0.5f, 0.5f));
      const ImVec2 p = ImGui::GetCursorScreenPos();
      constexpr float box = 14.0f;
      ImGui::Dummy(ImVec2(box, box));
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 c(p.x + box * 0.5f, p.y + box * 0.5f);
      const bool isLight = IsThemeLight();
      const ImU32 pinColor = result.modulated
         ? (isLight ? IM_COL32(215, 125, 20, 255) : IM_COL32(255, 190, 90, 255))
         : (isLight ? IM_COL32(165, 175, 195, 255) : IM_COL32(95, 100, 120, 255));
      if (CategoryColors::IsDesignTheme())
         DrawDesignParamPin(dl, c, result.modulated ? 1 : 0);
      else
      {
         dl->AddCircleFilled(c, 4.0f, pinColor);
         dl->AddCircle(c, 4.0f,
                       isLight ? IM_COL32(120, 130, 150, 255) : IM_COL32(30, 32, 42, 255),
                       0, 1.0f);
      }
      DecorateParamPin(dl, c, result.nodeIndex, result.paramIndex, label);
      ed::EndPin();
      ImGui::SameLine(0.0f, 4.0f);

      result.value = &slot;
      result.valid = true;
      return result;
   }

   void EndDiscreteParam(const DiscreteParamRef& ref, bool hovered)
   {
      if (!ref.valid)
         return;
      DrawModulationBindingMenu(ref.nodeIndex, ref.paramIndex, hovered);
      ImGui::PopID();
   }

   bool ModCheckbox(const char* label, bool* value)
   {
      if (gCurrentNodeIndex < 0)
         return ImGui::Checkbox(label, value);

      DiscreteParamRef ref = BeginDiscreteParam(label, *value ? 1.0f : 0.0f, 0.0f, 1.0f, 1);
      bool changed = false;
      if (ref.modulated)
      {
         const bool driven = *ref.value >= 0.5f;
         if (*value != driven)
         {
            *value = driven;
            changed = true;
         }
         ImGui::BeginDisabled();
      }
      if (ImGui::Checkbox(label, value))
      {
         PushUndoCheckpoint();
         changed = true;
      }
      const bool hovered = ImGui::IsItemHovered(ref.modulated ? ImGuiHoveredFlags_AllowWhenDisabled
                                                              : ImGuiHoveredFlags_None);
      if (ref.modulated)
         ImGui::EndDisabled();
      EndDiscreteParam(ref, hovered);
      return changed;
   }

   // Turbo: upstream signature. The 2-arg version already pushes the undo
   // checkpoint on a real click, so outUserChanged stays false here and the
   // caller never pushes a second one.
   bool ModCheckbox(const char* label, bool* value, bool* outUserChanged)
   {
      if (outUserChanged)
         *outUserChanged = false;
      return ModCheckbox(label, value);
   }


   // Turbo 0.57: Infinite Dark (2026) gives dropdowns a field-well face with
   // left-aligned text. Scoped so the classic themes are untouched.
   struct DesignDropdownFace
   {
      bool on = false;
      DesignDropdownFace()
      {
         on = CategoryColors::IsDesignTheme();
         if (!on)
            return;
         ImGui::PushStyleColor(ImGuiCol_Button, tok::V4(tok::dropdown_fill, false));
         ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tok::V4(tok::dropdown_hover, false));
         ImGui::PushStyleColor(ImGuiCol_ButtonActive, tok::V4(tok::dropdown_active, false));
         ImGui::PushStyleColor(ImGuiCol_Text, tok::V4(tok::dropdown_text, false));
         ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
         ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tok::radius_field);
      }
      ~DesignDropdownFace()
      {
         if (!on)
            return;
         ImGui::PopStyleVar(2);
         ImGui::PopStyleColor(4);
      }
   };

   void DropdownButton(const char* label, const std::vector<std::string>& options,
                       int current, std::function<void(int)> onSelect,
                       float width, bool modulatable)
   {
      if (options.empty())
         return;

      DiscreteParamRef ref;
      if (modulatable && gCurrentNodeIndex >= 0)
         ref = BeginDiscreteParam(label, (float)current, 0.0f, (float)options.size() - 1.0f, 3, &options);

      int safeCurrent = std::clamp(current, 0, (int)options.size() - 1);
      if (ref.valid && ref.modulated)
      {
         const int driven = std::clamp((int)lroundf(*ref.value), 0, (int)options.size() - 1);
         if (driven != safeCurrent)
         {
            safeCurrent = driven;
            gApplyingDiscreteCv = true;
            onSelect(driven);
            gApplyingDiscreteCv = false;
         }
         ImGui::BeginDisabled();
      }

      std::string caption = options[safeCurrent] + "##" + label;
      const float pinSpace = ref.valid ? 18.0f : 0.0f;
      bool clicked = false;
      {
         DesignDropdownFace face;
         clicked = ImGui::Button(caption.c_str(), ImVec2(std::max(24.0f, width - pinSpace), 0));
      }
      if (clicked)
         PrepareDropdown(options, {}, safeCurrent, onSelect);
      const bool hovered = ImGui::IsItemHovered(ref.valid && ref.modulated
                                                   ? ImGuiHoveredFlags_AllowWhenDisabled
                                                   : ImGuiHoveredFlags_None);
      if (ref.valid && ref.modulated)
         ImGui::EndDisabled();

      if (ref.valid)
         EndDiscreteParam(ref, hovered);
      ImGui::SameLine();
      std::string shown(label);
      const size_t hidden = shown.find("##");
      if (hidden != std::string::npos)
         shown = shown.substr(0, hidden);
      ImGui::TextDisabled("%s", shown.c_str());
   }

   // Turbo 0.57: a plain dropdown for node bodies (no label, no CV pin).
   // Node bodies are drawn under the node editor's canvas transform, so an
   // ImGui::BeginCombo there opens its list at the wrong place (often the
   // bottom-right corner of the screen) and cannot be clicked. Never use
   // BeginCombo / Combo inside a node body: use this, DropdownButton or
   // AudioBareDropdown, which open the list through the deferred dropdown.
   void NodeDropdown(const char* id, const std::vector<std::string>& options, int current,
                     std::function<void(int)> onSelect, float width)
   {
      if (options.empty())
         return;
      const int safe = std::clamp(current, 0, (int)options.size() - 1);
      const std::string caption = options[(size_t)safe] + "##" + id;
      bool clicked = false;
      {
         DesignDropdownFace face;
         clicked = ImGui::Button(caption.c_str(), ImVec2(width, 0.0f));
      }
      if (clicked)
         PrepareDropdown(options, {}, safe, std::move(onSelect), true);
   }

   // Turbo 0.50: shared MIDI channel picker for every MIDI input node. The
   // stored int keeps its 0-based meaning (-1 = omni, 0-15 = channel 1-16) so
   // saved patches are unchanged; only the UI speaks 1-16.
   const std::vector<std::string>& MidiChannelNames(bool withOmni)
   {
      static std::vector<std::string> omni, plain;
      if (plain.empty())
      {
         omni.push_back("omni");
         for (int c = 1; c <= 16; c++)
         {
            const std::string name = "ch " + std::to_string(c);
            omni.push_back(name);
            plain.push_back(name);
         }
      }
      return withOmni ? omni : plain;
   }

   void MidiChannelDropdown(const char* label, int* channel, bool withOmni, float width = kParamWidth)
   {
      const int current = withOmni ? std::clamp(*channel, -1, 15) + 1 : std::clamp(*channel, 0, 15);
      DropdownButton(label, MidiChannelNames(withOmni), current,
                     [channel, withOmni](int i) {
                        const int v = withOmni ? i - 1 : i;
                        if (v == *channel)
                           return;
                        if (!gApplyingDiscreteCv)
                           PushUndoCheckpoint();
                        *channel = v;
                     },
                     width);
   }

   // Turbo 0.49: `caption` (optional) is the button's text when it differs
   // from the param's name, e.g. a "<" arrow whose param reads "prev groove"
   // in MIDI learn, the Performance panel and the MCP param list.
   // Turbo 0.50: `outParamIndex` (optional) receives the hashed param index,
   // so a node can tell the Performance panel which trigger to light.
   bool ModTriggerButton(const char* label, const ImVec2& size, const char* caption = nullptr,
                         int* outParamIndex = nullptr)
   {
      if (caption == nullptr)
         caption = label;
      if (gCurrentNodeIndex < 0)
         return ImGui::Button(caption, size);

      DiscreteParamRef ref = BeginDiscreteParam(label, 0.0f, 0.0f, 1.0f, 2);
      if (outParamIndex != nullptr)
         *outParamIndex = ref.paramIndex;
      const std::pair<int, int> key(ref.nodeIndex, ref.paramIndex);
      bool firedByCv = false;
      if (ref.modulated)
      {
         const bool high = *ref.value >= 0.5f;
         firedByCv = high && !gDiscretePreviousHigh[key];
         gDiscretePreviousHigh[key] = high;
         ImGui::BeginDisabled();
      }
      else
      {
         gDiscretePreviousHigh[key] = false;
      }

      const float width = size.x > 0.0f ? std::max(8.0f, size.x - 18.0f) : size.x;
      const bool clicked = ImGui::Button(caption, ImVec2(width, size.y));
      const bool hovered = ImGui::IsItemHovered(ref.modulated ? ImGuiHoveredFlags_AllowWhenDisabled
                                                              : ImGuiHoveredFlags_None);
      if (ref.modulated)
         ImGui::EndDisabled();
      EndDiscreteParam(ref, hovered);
      return clicked || firedByCv;
   }

   // toggleOnRise (Turbo): for footswitch-style buttons (Looper transport) a
   // driving CV presses the button on each rising edge - one Macro Trigger or
   // MIDI pad hit = one press - instead of holding the state while high.
   bool ModStateButton(const char* label, bool currentState, bool& requestedState,
                       const ImVec2& size, bool toggleOnRise = false)
   {
      if (gCurrentNodeIndex < 0)
      {
         if (!ImGui::Button(label, size))
            return false;
         requestedState = !currentState;
         return true;
      }

      // Turbo 0.48: a footswitch registers as momentary (kind 2), not as a
      // checkbox, so a Performance Mode bang or a Macro Trigger drives it with
      // the plain 1-while-held / 0-on-release level and every trigger rises
      // (a flip-per-trigger bool only rose on every second one, as upstream
      // c4a9c58 found for its Looper).
      DiscreteParamRef ref = BeginDiscreteParam(label, currentState ? 1.0f : 0.0f, 0.0f, 1.0f, toggleOnRise ? 2 : 1);
      bool changedByCv = false;
      if (ref.modulated)
      {
         const bool high = *ref.value >= 0.5f;
         const std::pair<int, int> key(ref.nodeIndex, ref.paramIndex);
         if (toggleOnRise)
         {
            changedByCv = high && !gDiscretePreviousHigh[key];
            if (changedByCv)
               requestedState = !currentState;
         }
         else
         {
            requestedState = high;
            changedByCv = requestedState != gDiscretePreviousHigh[key];
         }
         gDiscretePreviousHigh[key] = high;
         ImGui::BeginDisabled();
      }
      else
      {
         // A rising-edge button idles low between drives: a one-frame
         // Performance Mode write must still read as an edge while the
         // button's state is on (e.g. stopping a take).
         gDiscretePreviousHigh[{ref.nodeIndex, ref.paramIndex}] = toggleOnRise ? false : currentState;
      }

      const float width = size.x > 0.0f ? std::max(8.0f, size.x - 18.0f) : size.x;
      const bool clicked = ImGui::Button(label, ImVec2(width, size.y));
      const bool hovered = ImGui::IsItemHovered(ref.modulated ? ImGuiHoveredFlags_AllowWhenDisabled
                                                              : ImGuiHoveredFlags_None);
      if (ref.modulated)
         ImGui::EndDisabled();
      EndDiscreteParam(ref, hovered);

      if (clicked)
      {
         requestedState = !currentState;
         return true;
      }
      return changedByCv;
   }

   // Turbo: a momentary pad button. Returns true while held - by the mouse or
   // by its CV pin being high (>= 0.5) - so a pad can be played by hand and
   // by a MIDI controller at the same time. `clicked` reports a mouse press
   // this frame (used to select the pad). Unlike ModStateButton it stays live
   // while modulated. `subLabel` is drawn under the main label.
   bool ModGateButton(const char* label, const ImVec2& size, bool lit, bool selected, bool loaded,
                      const char* subLabel, bool& clicked)
   {
      clicked = false;
      DiscreteParamRef ref;
      // Turbo 0.48: momentary (kind 2), so a Performance Mode bang or Macro
      // Trigger holds the pad for as long as it is high instead of flipping
      // it on one trigger and off the next (every trigger plays the pad).
      if (gCurrentNodeIndex >= 0)
         ref = BeginDiscreteParam(label, 0.0f, 0.0f, 1.0f, 2);
      const bool cvHigh = ref.valid && ref.modulated && *ref.value >= 0.5f;
      const float width = ref.valid ? std::max(8.0f, size.x - 18.0f) : size.x;
      const bool isLight = IsThemeLight();
      const bool showLit = lit || cvHigh;
      const ImU32 fill = showLit ? IM_COL32(235, 150, 60, 255)
                       : loaded  ? (isLight ? IM_COL32(205, 212, 228, 255) : IM_COL32(64, 72, 96, 255))
                                 : (isLight ? IM_COL32(228, 230, 236, 255) : IM_COL32(40, 43, 55, 255));
      const ImU32 hover = showLit ? IM_COL32(245, 170, 80, 255)
                                  : (isLight ? IM_COL32(190, 200, 222, 255) : IM_COL32(80, 90, 118, 255));
      ImGui::PushStyleColor(ImGuiCol_Button, fill);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(250, 185, 90, 255));
      ImGui::PushStyleColor(ImGuiCol_Border, selected ? IM_COL32(120, 200, 255, 255) : IM_COL32(90, 96, 120, 200));
      ImGui::PushStyleColor(ImGuiCol_Text, showLit ? IM_COL32(20, 20, 24, 255)
                                                   : (isLight ? IM_COL32(40, 45, 60, 255) : IM_COL32(220, 225, 238, 255)));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, selected ? 2.0f : 1.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.08f, 0.12f));
      const ImVec2 topLeft = ImGui::GetCursorScreenPos();
      ImGui::Button(label, ImVec2(width, size.y));
      clicked = ImGui::IsItemActivated();
      const bool held = ImGui::IsItemActive();
      const bool hovered = ImGui::IsItemHovered();
      ImGui::PopStyleVar(2);
      ImGui::PopStyleColor(5);
      if (subLabel != nullptr && subLabel[0] != '\0')
      {
         ImDrawList* dl = ImGui::GetWindowDrawList();
         dl->AddText(ImVec2(topLeft.x + 6.0f, topLeft.y + size.y * 0.42f),
                     showLit ? IM_COL32(30, 30, 36, 255) : (isLight ? IM_COL32(70, 76, 92, 255) : IM_COL32(150, 158, 180, 255)),
                     subLabel);
      }
      if (ref.valid)
         EndDiscreteParam(ref, hovered);
      return held || cvHigh;
   }


   // `audioStyle` swaps the widget in the middle of this function for
   // AudioSliderFloat above. Everything around it - the pin, the typed-edit
   // field, the expression/modulation branches, undo, hotkeys - is shared,
   // so an audio slider is modulatable and expression-drivable on exactly
   // the same terms as every other param in the app.
   bool ModSlider(const char* label, float* value, float minV, float maxV, const char* fmt = "%.3f",
                  float width = kParamWidth, bool audioStyle = false)
   {
      const int nodeIndex = gCurrentNodeIndex;
      const int paramIndex = gParamCounter++;

      ParamRef ref;
      ref.nodeIndex = nodeIndex;
      ref.paramIndex = paramIndex;
      ref.value = value;
      ref.minValue = minV;
      ref.maxValue = maxV;
      ref.name = label;
      Modulation::Instance().RegisterParam(ref);

      const int pinId = nodeIndex * GraphNode::kStride + GraphNode::kParamBase + paramIndex;
      const bool modulated = Modulation::Instance().IsModulated(nodeIndex, paramIndex);
      // An expression only actually drives the field while nothing is wired
      // into its pin - see Modulation::SetExpression. It stays stored either
      // way, so unpatching the cable brings it straight back.
      const bool hasExpr = !modulated && Modulation::Instance().HasExpression(nodeIndex, paramIndex);
      // A stored expression that currently fails to parse/evaluate (bad
      // syntax, unknown function, division by zero) still counts as
      // "has an expression" - it stays queued for the moment it's fixed -
      // but nothing is overwriting *value while it's broken (see the apply
      // pass), so it's safe to just fall back to the plain, fully-interactive
      // slider look rather than a special locked/coloured state. The fx/x
      // controls stay so the stored formula can still be reopened or cleared.
      const std::string* hasExprErr = hasExpr ? Modulation::Instance().ExpressionErrorFor(nodeIndex, paramIndex)
                                               : nullptr;
      const bool exprErrored = hasExprErr != nullptr && !hasExprErr->empty();
      gDrawnParamPins.insert(pinId);

      ImGui::PushID(paramIndex + 5000);

      ed::BeginPin(pinId, ed::PinKind::Input);
      ed::PinPivotAlignment(ImVec2(0.5f, 0.5f));
      ImVec2 p = ImGui::GetCursorScreenPos();
      const float box = 14.0f;
      ImGui::Dummy(ImVec2(box, box));
      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 c(p.x + box * 0.5f, p.y + box * 0.5f);
      const bool isLight = IsThemeLight();
      const ImU32 pinColor = modulated
         ? (isLight ? IM_COL32(215, 125, 20, 255) : IM_COL32(255, 190, 90, 255))
         : hasExpr && !exprErrored
            ? (isLight ? IM_COL32(130, 80, 230, 255) : IM_COL32(170, 130, 255, 255))
            : (isLight ? IM_COL32(165, 175, 195, 255) : IM_COL32(95, 100, 120, 255));
      if (CategoryColors::IsDesignTheme())
         DrawDesignParamPin(dl, c, modulated ? 1 : (hasExpr && !exprErrored ? 2 : 0));
      else
      {
         dl->AddCircleFilled(c, 4.0f, pinColor);
         dl->AddCircle(c, 4.0f, isLight ? IM_COL32(120, 130, 150, 255) : IM_COL32(30, 32, 42, 255), 0, 1.0f);
      }
      DecorateParamPin(dl, c, nodeIndex, paramIndex, label);
      ed::EndPin();
      ImGui::SameLine(0.0f, 4.0f);

      // Double-clicking swaps the slider for a text field so an exact value
      // (or, prefixed with '=', an expression) can be typed. ImGui's built-in
      // Ctrl+click does this too, but double-click is what people reach for.
      const std::pair<int, int> editKey(nodeIndex, paramIndex);
      bool typing = gTypedParam.count(editKey) > 0;

      bool changed = false;
      if (typing && !modulated)
      {
         ImGui::SetNextItemWidth(width - box - 4.0f);
         if (gTypedParamJustOpened == editKey)
         {
            ImGui::SetKeyboardFocusHere();
            gTypedParamJustOpened = std::pair<int, int>(-1, -1);
         }
         char buf[256];
         snprintf(buf, sizeof(buf), "%s", gTypedParamText[editKey].c_str());
         // A plain InputText rather than InputFloat: the field has to accept
         // letters too, so a typed expression like "sin(t)" is not filtered
         // out character-by-character the way InputFloat's numeric-only input
         // would filter it.
         //
         // ImGuiInputTextFlags_AutoSelectAll only selects on a *mouse* click;
         // this field is focused programmatically via SetKeyboardFocusHere,
         // which instead runs through ImGui's nav-activation path and forces
         // its own select-all the moment the field actually goes active
         // (which can land a frame after SetKeyboardFocusHere is called) -
         // regardless of any flag we pass. So don't rely on flags for
         // selection at all: drive it explicitly below once the field is
         // confirmed active, which also lets the hover+type path leave the
         // cursor at the end instead of selecting the seeded digit.
         // '##'-prefixed, like ModKnob's field. ImGui::InputText(label, ...)
         // draws the caption OUTSIDE the box to the right and adds its width
         // to the item, so a bare label both shrinks the field and spills the
         // caption across whatever is beside it - inside a column that means
         // over the neighbouring engine. This is the same defect the knob path
         // fixed; the slider path never got it.
         const std::string typedId = std::string("##typed") + label;
         bool entered = false;
         {
            MixedCaseFontScope trueCase; // Turbo 0.59: `t` and `T` must look different here
            entered = ImGui::InputText(typedId.c_str(), buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue);
         }
         gTypedParamText[editKey] = buf;
         if (gTypedParamPendingInit.count(editKey) && ImGui::IsItemActive())
         {
            if (ImGuiInputTextState* state = ImGui::GetInputTextState(ImGui::GetItemID()))
            {
               if (gTypedParamNoAutoSelect.count(editKey))
               {
                  state->Stb.cursor = state->CurLenW;
                  state->ClearSelection();
               }
               else
               {
                  state->SelectAll();
               }
            }
            gTypedParamPendingInit.erase(editKey);
         }
         // IsItemActivated() has to be queried right after its widget, and it
         // fires on the frame focus begins - before any edit is applied that
         // same frame - so the checkpoint still captures the pre-edit value
         // even though the check reads textually "after" InputText here.
         if (ImGui::IsItemActivated())
            PushUndoCheckpoint();
         if (entered || ImGui::IsItemDeactivated())
         {
            const std::string trimmed = TrimCopy(gTypedParamText[editKey]);
            std::string exprText;
            if (TypedTextExpression(trimmed, exprText))
            {
               if (exprText.empty())
                  Modulation::Instance().ClearExpression(nodeIndex, paramIndex);
               else
                  Modulation::Instance().SetExpression(nodeIndex, paramIndex, exprText);
            }
            else
            {
               Modulation::Instance().ClearExpression(nodeIndex, paramIndex);
               if (!trimmed.empty())
               {
                  char* end = nullptr;
                  float parsed = strtof(trimmed.c_str(), &end);
                  if (end != trimmed.c_str())
                  {
                     *value = std::clamp(parsed, std::min(minV, maxV), std::max(minV, maxV));
                     changed = true;
                  }
               }
            }
            gTypedParam.erase(editKey);
            gTypedParamText.erase(editKey);
            gTypedParamNoAutoSelect.erase(editKey);
            gTypedParamPendingInit.erase(editKey);
         }
      }
      else if (modulated)
      {
         // value is driven externally; show it read-only so it is obvious why
         // dragging does nothing
         float shown = *value;
         if (audioStyle)
         {
            AudioSliderFloat(label, &shown, minV, maxV, fmt, width - box - 4.0f,
                             IM_COL32(255, 190, 90, 255), /*readOnly=*/true);
         }
         else
         {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.32f, 0.24f, 0.08f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.95f, 0.72f, 0.32f, 1.0f));
            ImGui::SetNextItemWidth(width - box - 4.0f);
            ImGui::SliderFloat(label, &shown, minV, maxV, fmt, ImGuiSliderFlags_NoInput);
            ImGui::PopStyleColor(2);
         }
         DrawModulationBindingMenu(nodeIndex, paramIndex, ImGui::IsItemHovered());
      }
      else if (hasExpr && !exprErrored)
      {
         // Driven by a typed expression, re-evaluated every frame by the
         // apply pass in the main loop. Read-only like the modulated state,
         // but a distinct colour and an fx badge. Double-click reopens the
         // expression text for editing, same as any other field.
         float shown = *value;
         // The fx badge and the clear button are SameLine'd after the slider,
         // so the slider has to give up their width or they hang off the right
         // edge of the cell - outside the section panel and over the next
         // column. Audio bodies only: a visual node's params are one wide
         // column with room to spare, and the standing rule is not to restyle
         // that library.
         // The "fx" caption is dropped in audio bodies and only the clear
         // button kept. An expression is already signalled twice over - the
         // pin turns purple and the track fills purple - and in a half-width
         // envelope field the caption's ~19px is the difference between the
         // label reading "release" and reading "re".
         const float exprSuffixW =
            audioStyle ? ImGui::CalcTextSize("x").x + ImGui::GetStyle().FramePadding.x * 2.0f + 4.0f
                       : 0.0f;
         if (audioStyle)
         {
            AudioSliderFloat(label, &shown, minV, maxV, fmt, width - box - 4.0f - exprSuffixW,
                             IM_COL32(170, 130, 255, 255), /*readOnly=*/true);
         }
         else
         {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.20f, 0.15f, 0.32f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.66f, 0.51f, 0.98f, 1.0f));
            ImGui::SetNextItemWidth(width - box - 4.0f);
            ImGui::SliderFloat(label, &shown, minV, maxV, fmt, ImGuiSliderFlags_NoInput);
            ImGui::PopStyleColor(2);
         }
         const bool sliderHovered = ImGui::IsItemHovered();
         bool hovered = sliderHovered;
         if (!audioStyle)
         {
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextColored(ImVec4(0.66f, 0.51f, 0.98f, 1.0f), "fx");
            hovered = hovered || ImGui::IsItemHovered();
         }
         if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            BeginTypedEditFromCurrent(editKey, nodeIndex, paramIndex, value, fmt, /*hasExpr=*/true);
         if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
         {
            BeginTypedEditFromCurrent(editKey, nodeIndex, paramIndex, value, fmt, /*hasExpr=*/true);
            gParamRightClickConsumedThisFrame = true;
         }
         if (hovered)
            HandleParamTypeHotkeys(editKey, value);
         ImGui::SameLine(0.0f, 4.0f);
         ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
         ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.15f, 0.15f, 1.0f));
         ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
         if (ImGui::SmallButton("x"))
            Modulation::Instance().ClearExpression(nodeIndex, paramIndex);
         ImGui::PopStyleColor(3);
      }
      else if (hasExpr) // exprErrored
      {
         // The stored expression is currently failing to evaluate. Nothing
         // is writing to *value while that's true (see the apply pass), so
         // there's nothing unsafe about just falling back to a plain,
         // fully-interactive slider on the real value - no special colour,
         // no lock, and no fx/x badge either: it looks exactly like a param
         // with no expression at all. The stored (broken) formula is still
         // reachable to fix or clear via double-click, right-click, or
         // hovering and typing '=' - see HandleParamTypeHotkeys - same as it
         // would be if this were a fresh, non-expression param.
         if (audioStyle)
         {
            changed = AudioSliderFloat(label, value, minV, maxV, fmt, width - box - 4.0f,
                                       IM_COL32(120, 200, 255, 235), /*readOnly=*/false);
         }
         else
         {
            ImGui::SetNextItemWidth(width - box - 4.0f);
            changed = ImGui::SliderFloat(label, value, minV, maxV, fmt);
         }
         if (ImGui::IsItemActivated())
            PushUndoCheckpoint();
         const bool hovered = ImGui::IsItemHovered();
         if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            BeginTypedEditFromCurrent(editKey, nodeIndex, paramIndex, value, fmt, /*hasExpr=*/true);
         if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
         {
            BeginTypedEditFromCurrent(editKey, nodeIndex, paramIndex, value, fmt, /*hasExpr=*/true);
            gParamRightClickConsumedThisFrame = true;
         }
         if (hovered && !ImGui::IsItemActive())
            HandleParamTypeHotkeys(editKey, value);
         // Turbo 0.59: say so - a broken formula used to look exactly like no
         // formula at all, so a typo read as "expressions don't work".
         ImGui::SameLine(0.0f, 4.0f);
         ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "fx!");
         if (hovered || ImGui::IsItemHovered())
         {
            const std::string* exprText = Modulation::Instance().ExpressionFor(nodeIndex, paramIndex);
            gLiveIssueTip = "expression error: " + *hasExprErr + "\n=" + (exprText != nullptr ? *exprText : std::string()) +
                            "\n(double-click to fix it, or type a number to drop it)";
         }
      }
      else
      {
         if (audioStyle)
         {
            changed = AudioSliderFloat(label, value, minV, maxV, fmt, width - box - 4.0f,
                                       IM_COL32(120, 200, 255, 235), /*readOnly=*/false);
         }
         else
         {
            ImGui::SetNextItemWidth(width - box - 4.0f);
            changed = ImGui::SliderFloat(label, value, minV, maxV, fmt);
         }
         // Activation is the first frame of the drag, before that frame's own
         // delta is applied, so this is still the pre-drag value.
         if (ImGui::IsItemActivated())
            PushUndoCheckpoint();
         GestureWidgetHook(nodeIndex, paramIndex, *value, ImGui::IsItemActive(), ImGui::IsItemActivated());
         if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            BeginTypedEditFromCurrent(editKey, nodeIndex, paramIndex, value, fmt, /*hasExpr=*/false);
         // Right-click also jumps straight into the text field, same as
         // double-click - and marks the click consumed so the node-level
         // right-click context menu doesn't also try to open over it.
         if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
         {
            BeginTypedEditFromCurrent(editKey, nodeIndex, paramIndex, value, fmt, /*hasExpr=*/false);
            gParamRightClickConsumedThisFrame = true;
         }
         // Hovering (not dragging) and pressing a digit/'-'/'.'/'=' starts a
         // fresh typed value (or expression) immediately, without needing to
         // double-click first. IsItemActive() guards against a mouse-drag
         // coinciding with a keypress from stealing the drag into typing mode.
         if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
            HandleParamTypeHotkeys(editKey, value);
      }

      ImGui::PopID();
      return changed;
   }

   // Integer parameter that is still modulatable. The backing float lives in a
   // std::map so its address stays valid for the whole frame (node references are
   // stable), which matters because the modulation pass writes through that pointer.
   std::map<std::pair<int, int>, float> gIntParamStore;

   // The last integer this widget itself wrote back, per param. See
   // ModKnobInt below for why a slider doesn't need it but a knob does.
   std::map<std::pair<int, int>, int> gIntParamLastWritten;

   bool ModSliderInt(const char* label, int* value, int minV, int maxV, float width = kParamWidth,
                     bool audioStyle = false)
   {
      const std::pair<int, int> key(gCurrentNodeIndex, gParamCounter);
      float& slot = gIntParamStore[key];
      float perfValue = 0.0f;
      if (TakePerfSlotWrite(key, perfValue))
         slot = perfValue; // Turbo 0.46: Performance Mode
      else if (!Modulation::Instance().IsModulated(key.first, key.second))
         slot = (float)*value;

      bool changed = ModSlider(label, &slot, (float)minV, (float)maxV, "%.0f", width, audioStyle);
      // lroundf, not (int)(x + 0.5f): the latter truncates toward zero, so
      // -3.7 lands on -3 instead of -4 and every negative-range param (octave,
      // semi, transpose) is biased upward by half a step.
      *value = (int)lroundf(slot);
      return changed;
   }

   // ---- rotary knob (audio nodes' Tier 1 grid) ----------------------------
   // Raw draw+interaction primitive for a knob, built on InvisibleButton
   // rather than any ImGui slider. It still leaves the normal
   // IsItemHovered/IsItemActive/IsItemActivated query surface behind it,
   // which is what lets ModKnob below reuse ModSlider's pin/typing/
   // expression/hotkey logic completely unchanged - only the widget in the
   // middle of that logic is different.
   //
   // `cellW` is the width of the layout cell this knob sits in (see
   // AudioKnobRow): the knob and its caption are centred in that cell, so a
   // row of N knobs is exactly bodyWidth wide by construction rather than
   // however wide N knobs happen to add up to (v3 §3b). cellW <= 0 means
   // "size to the knob", the pre-v3 behaviour.
   // Which physical control a Tier-1 param is drawn as. The knob is the
   // default and covers most params; the fader exists because a mixer's
   // channel gains are the one place where *comparing* several values at a
   // glance matters more than setting any one of them precisely - which is
   // exactly why every mixing desk ever built uses faders there and knobs
   // everywhere else. A row of 8 knobs cannot be read as a balance; a row of
   // 8 faders is read as one by shape alone.
   enum class AudioWidgetStyle
   {
      Knob,
      VFader,
      // Same widget as VFader, routed through ConsoleFaderTaper - unity at
      // 75% of throw instead of a linear-in-dB mapping. Only meaningful for
      // a -60..+12 dB range; see the taper's own comment.
      VFaderDb,
      // Same widget as Knob, routed through ConsoleFaderTaper.
      KnobDb,
      // Logarithmic taper matching human ear frequency/pitch perception and visualizer response curves.
      KnobFreq
   };

   struct AudioWidgetInteraction
   {
      bool hovered = false;
      bool active = false;
      bool activated = false;
   };
   AudioWidgetInteraction gLastAudioWidgetInteraction;

   // Visual width the fader occupies inside its cell (track plus the cap's
   // overhang). The interactive rect matches, for the same reason KnobFloat's
   // does: the cell's side margins belong to ModKnob's modulation pin.
   const float kFaderWidth = 22.0f;

   // Optional non-linear position<->value mapping for VFaderFloat / KnobFloat. nullptr
   // (the default at every call site but the console strip/channel faders)
   // means plain linear, i.e. the original behaviour. A future non-dB taper
   // could plug in here the same way - this is a general fader capability,
   // not dB-specific plumbing hardcoded into the widget.
   using FaderPosToValueFn = float (*)(float pos01, float minV, float maxV);
   using FaderValueToPosFn = float (*)(float value, float minV, float maxV);

   // Logarithmic frequency taper: maps 0..1 throw to an octave-linear scale between minV and maxV.
   // Perceptually centres ~630 Hz on 20..20,000 Hz, matching human pitch perception and filter graphs.
   namespace FrequencyTaper
   {
      inline float PosToValue(float pos01, float minV, float maxV)
      {
         const float lo = std::max(1.0f, minV);
         const float hi = std::max(lo + 1.0f, maxV);
         return lo * powf(hi / lo, std::clamp(pos01, 0.0f, 1.0f));
      }

      inline float ValueToPos(float value, float minV, float maxV)
      {
         const float lo = std::max(1.0f, minV);
         const float hi = std::max(lo + 1.0f, maxV);
         const float clampedVal = std::clamp(value, lo, hi);
         return std::clamp((logf(clampedVal) - logf(lo)) / (logf(hi) - logf(lo)), 0.0f, 1.0f);
      }
   }

   // Piecewise-linear -60..+12 dB console taper: unity sits at 75% of throw
   // and the range below -30 dB (near-inaudible) is compressed, instead of
   // giving 0 -> -12 dB (a large, obvious level change) and -60 -> -30 dB
   // (barely audible) equal pixel budget the way a linear-in-dB fader does.
   // Only valid for a -60..+12 dB range - callers with a different minV/maxV
   // should not opt in.
   namespace ConsoleFaderTaper
   {
      struct Point { float pos, db; };
      const Point kPoints[] = {
         { 0.00f, -60.0f }, { 0.15f, -45.0f }, { 0.30f, -30.0f }, { 0.45f, -20.0f },
         { 0.60f, -10.0f }, { 0.75f,   0.0f }, { 1.00f,  12.0f },
      };
      constexpr int kCount = sizeof(kPoints) / sizeof(kPoints[0]);
      // Detents drawn on a tapered fader, in dB - meaningless as even quarters
      // of the throw once the taper is nonlinear.
      const float kDetentsDb[] = { 12.0f, 6.0f, 0.0f, -6.0f, -12.0f, -20.0f, -30.0f, -40.0f, -60.0f }; // Turbo 0.51
      constexpr int kNumDetents = sizeof(kDetentsDb) / sizeof(kDetentsDb[0]);

      float PosToDb(float pos)
      {
         pos = std::clamp(pos, 0.0f, 1.0f);
         for (int i = 0; i + 1 < kCount; i++)
         {
            if (pos <= kPoints[i + 1].pos)
            {
               const float t = (pos - kPoints[i].pos) / (kPoints[i + 1].pos - kPoints[i].pos);
               return kPoints[i].db + (kPoints[i + 1].db - kPoints[i].db) * t;
            }
         }
         return kPoints[kCount - 1].db;
      }

      float DbToPos(float db)
      {
         if (db <= kPoints[0].db)
            return kPoints[0].pos;
         if (db >= kPoints[kCount - 1].db)
            return kPoints[kCount - 1].pos;
         for (int i = 0; i + 1 < kCount; i++)
         {
            if (db <= kPoints[i + 1].db)
            {
               const float t = (db - kPoints[i].db) / (kPoints[i + 1].db - kPoints[i].db);
               return kPoints[i].pos + (kPoints[i + 1].pos - kPoints[i].pos) * t;
            }
         }
         return kPoints[kCount - 1].pos;
      }

      float PosToValue(float pos01, float /*minV*/, float /*maxV*/) { return PosToDb(pos01); }
      float ValueToPos(float value, float /*minV*/, float /*maxV*/) { return DbToPos(value); }
   }

   // Turbo 0.51: horizontal dB slider through ConsoleFaderTaper (the clip matrix
   // cell/row gains). Stored value stays dB. Ctrl/Alt+click resets to 0 dB.
   bool TaperedDbSlider(const char* label, float* db, float lo, float hi, bool infAtFloor = false)
   {
      float pos = std::clamp(ConsoleFaderTaper::ValueToPos(*db, lo, hi), 0.0f, 1.0f);
      char txt[32];
      if (infAtFloor && *db <= -59.9f)
         snprintf(txt, sizeof(txt), "-inf dB");
      else
         snprintf(txt, sizeof(txt), "%.1f dB", *db);
      bool changed = false;
      if (ImGui::SliderFloat(label, &pos, 0.0f, 1.0f, txt, ImGuiSliderFlags_NoInput))
      {
         *db = std::clamp(ConsoleFaderTaper::PosToValue(pos, lo, hi), lo, hi);
         changed = true;
      }
      if (ImGui::IsItemActivated() && (ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyAlt))
      {
         PushUndoCheckpoint();
         *db = 0.0f;
         changed = true;
      }
      return changed;
   }

   // Vertical fader with KnobFloat's exact contract - draws at the cursor,
   // reserves (cell, rowH), leaves IsItemHovered/IsItemActive queryable, fills
   // the readout strip on hover - so ModKnob can drive either one without
   // knowing which it has.
   bool VFaderFloat(const char* label, float* value, float minV, float maxV, const char* fmt,
                    float height, ImU32 fillColor, bool readOnly, float cellW = 0.0f,
                    FaderPosToValueFn posToValue = nullptr, FaderValueToPosFn valueToPos = nullptr)
   {
      const float cell = cellW > 0.0f ? cellW : kFaderWidth;
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const float textH = ImGui::GetTextLineHeight();
      const float rowH = height + 4.0f + textH;

      auto ValueToPos01 = [&](float v) -> float
      {
         return valueToPos ? std::clamp(valueToPos(v, minV, maxV), 0.0f, 1.0f)
                            : ((maxV > minV) ? std::clamp((v - minV) / (maxV - minV), 0.0f, 1.0f) : 0.0f);
      };
      auto Pos01ToValue = [&](float pos) -> float
      {
         return posToValue ? std::clamp(posToValue(pos, minV, maxV), minV, maxV)
                            : std::clamp(minV + (maxV - minV) * pos, minV, maxV);
      };

      ImGui::SetCursorScreenPos(ImVec2(p.x + (cell - kFaderWidth) * 0.5f, p.y));
      ImGui::InvisibleButton(label, ImVec2(kFaderWidth, height));
      const bool hovered = ImGui::IsItemHovered();
      gLastAudioWidgetInteraction = { hovered, ImGui::IsItemActive(), ImGui::IsItemActivated() };
      const bool active = !readOnly && gLastAudioWidgetInteraction.active;
      bool changed = false;
      if (active && ImGui::GetIO().MouseDelta.y != 0.0f)
      {
         // Travel is the fader's own length, not a fixed 200px as on the
         // knob: a fader that doesn't track the cap under the cursor reads as
         // broken in a way a knob never does, because the cap is visibly a
         // position rather than an angle. Dragging moves position linearly
         // (screen pixels are literally position), then position maps to
         // value through whatever taper is active - linear when there isn't one.
         const float speed = ImGui::GetIO().KeyShift ? 0.2f : 1.0f;
         const float travel = std::max(1.0f, height - 12.0f);
         const float posNow = ValueToPos01(*value);
         const float nextPos = std::clamp(posNow - ImGui::GetIO().MouseDelta.y * speed / travel, 0.0f, 1.0f);
         const float next = Pos01ToValue(nextPos);
         if (next != *value)
         {
            *value = next;
            changed = true;
         }
      }

      const float t = ValueToPos01(*value);
      const float cx = p.x + cell * 0.5f;
      const float top = p.y + 6.0f;
      const float bottom = p.y + height - 6.0f;
      const float capY = bottom - t * (bottom - top);

      const bool isLight = IsThemeLight();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      if (isLight)
      {
         dl->AddRectFilled(ImVec2(cx - 3.0f, top - 4.0f), ImVec2(cx + 3.0f, bottom + 4.0f),
                           IM_COL32(215, 220, 230, 255), 3.0f);
         dl->AddRect(ImVec2(cx - 3.0f, top - 4.0f), ImVec2(cx + 3.0f, bottom + 4.0f),
                     IM_COL32(180, 185, 200, 255), 3.0f);
         if (t > 0.0f)
            dl->AddRectFilled(ImVec2(cx - 2.0f, capY), ImVec2(cx + 2.0f, bottom + 4.0f), fillColor, 2.0f);

         if (valueToPos == nullptr)
         {
            for (int i = 0; i <= 4; i++)
            {
               const float y = bottom - (float)i * 0.25f * (bottom - top);
               dl->AddLine(ImVec2(cx + 6.0f, y), ImVec2(cx + 9.0f, y), IM_COL32(0, 0, 0, 45), 1.0f);
               dl->AddLine(ImVec2(cx - 9.0f, y), ImVec2(cx - 6.0f, y), IM_COL32(0, 0, 0, 45), 1.0f);
            }
         }
         else
         {
            for (int i = 0; i < ConsoleFaderTaper::kNumDetents; i++)
            {
               const float dpos = std::clamp(valueToPos(ConsoleFaderTaper::kDetentsDb[i], minV, maxV), 0.0f, 1.0f);
               const float y = bottom - dpos * (bottom - top);
               dl->AddLine(ImVec2(cx + 6.0f, y), ImVec2(cx + 9.0f, y), IM_COL32(0, 0, 0, 45), 1.0f);
               dl->AddLine(ImVec2(cx - 9.0f, y), ImVec2(cx - 6.0f, y), IM_COL32(0, 0, 0, 45), 1.0f);
            }
         }
         const ImU32 capCol = readOnly ? IM_COL32(180, 185, 198, 255) : IM_COL32(235, 238, 245, 255);
         dl->AddRectFilled(ImVec2(cx - 9.0f, capY - 6.0f), ImVec2(cx + 9.0f, capY + 6.0f), capCol, 3.0f);
         dl->AddRect(ImVec2(cx - 9.0f, capY - 6.0f), ImVec2(cx + 9.0f, capY + 6.0f),
                     IM_COL32(150, 155, 170, 255), 3.0f);
         dl->AddLine(ImVec2(cx - 7.0f, capY), ImVec2(cx + 7.0f, capY),
                     readOnly ? IM_COL32(120, 125, 140, 255) : IM_COL32(40, 45, 60, 255), 1.6f);
         if (hovered && !readOnly)
            dl->AddRect(ImVec2(cx - 10.0f, capY - 7.0f), ImVec2(cx + 10.0f, capY + 7.0f),
                        IM_COL32(0, 0, 0, 30), 4.0f, 0, 2.0f);
      }
      else
      {
         dl->AddRectFilled(ImVec2(cx - 3.0f, top - 4.0f), ImVec2(cx + 3.0f, bottom + 4.0f),
                           IM_COL32(14, 15, 20, 255), 3.0f);
         dl->AddRect(ImVec2(cx - 3.0f, top - 4.0f), ImVec2(cx + 3.0f, bottom + 4.0f),
                     IM_COL32(58, 62, 76, 255), 3.0f);
         if (t > 0.0f)
            dl->AddRectFilled(ImVec2(cx - 2.0f, capY), ImVec2(cx + 2.0f, bottom + 4.0f), fillColor, 2.0f);

         if (valueToPos == nullptr)
         {
            for (int i = 0; i <= 4; i++)
            {
               const float y = bottom - (float)i * 0.25f * (bottom - top);
               dl->AddLine(ImVec2(cx + 6.0f, y), ImVec2(cx + 9.0f, y), IM_COL32(255, 255, 255, 34), 1.0f);
               dl->AddLine(ImVec2(cx - 9.0f, y), ImVec2(cx - 6.0f, y), IM_COL32(255, 255, 255, 34), 1.0f);
            }
         }
         else
         {
            for (int i = 0; i < ConsoleFaderTaper::kNumDetents; i++)
            {
               const float dpos = std::clamp(valueToPos(ConsoleFaderTaper::kDetentsDb[i], minV, maxV), 0.0f, 1.0f);
               const float y = bottom - dpos * (bottom - top);
               dl->AddLine(ImVec2(cx + 6.0f, y), ImVec2(cx + 9.0f, y), IM_COL32(255, 255, 255, 34), 1.0f);
               dl->AddLine(ImVec2(cx - 9.0f, y), ImVec2(cx - 6.0f, y), IM_COL32(255, 255, 255, 34), 1.0f);
            }
         }
         const ImU32 capCol = readOnly ? IM_COL32(96, 99, 114, 255) : IM_COL32(72, 77, 94, 255);
         dl->AddRectFilled(ImVec2(cx - 9.0f, capY - 6.0f), ImVec2(cx + 9.0f, capY + 6.0f), capCol, 3.0f);
         dl->AddRect(ImVec2(cx - 9.0f, capY - 6.0f), ImVec2(cx + 9.0f, capY + 6.0f),
                     IM_COL32(18, 19, 25, 200), 3.0f);
         dl->AddLine(ImVec2(cx - 7.0f, capY), ImVec2(cx + 7.0f, capY),
                     readOnly ? IM_COL32(200, 202, 212, 255) : IM_COL32(238, 240, 248, 255), 1.6f);
         if (hovered && !readOnly)
            dl->AddRect(ImVec2(cx - 10.0f, capY - 7.0f), ImVec2(cx + 10.0f, capY + 7.0f),
                        IM_COL32(255, 255, 255, 60), 4.0f, 0, 2.0f);
      }

      // Turbo 0.51: tapered faders get a brighter 0 dB tick and, when tall enough,
      // small dB labels beside the track.
      if (valueToPos != nullptr)
      {
         const float y0 = bottom - std::clamp(valueToPos(0.0f, minV, maxV), 0.0f, 1.0f) * (bottom - top);
         const ImU32 zeroCol = isLight ? IM_COL32(0, 0, 0, 150) : IM_COL32(255, 255, 255, 140);
         dl->AddLine(ImVec2(cx + 5.0f, y0), ImVec2(cx + 10.0f, y0), zeroCol, 1.5f);
         dl->AddLine(ImVec2(cx - 10.0f, y0), ImVec2(cx - 5.0f, y0), zeroCol, 1.5f);
         if (height >= 40.0f && cell >= 40.0f)
         {
            const float fs = ImGui::GetFontSize() * 0.62f;
            const ImU32 labCol = isLight ? IM_COL32(0, 0, 0, 120) : IM_COL32(255, 255, 255, 105);
            float lastY = -1.0e9f;
            for (int i = 0; i < ConsoleFaderTaper::kNumDetents; i++)
            {
               const float dv = ConsoleFaderTaper::kDetentsDb[i];
               if (dv < minV || dv > maxV)
                  continue;
               const float y = bottom - std::clamp(valueToPos(dv, minV, maxV), 0.0f, 1.0f) * (bottom - top);
               if (fabsf(y - lastY) < fs + 1.0f)
                  continue;
               lastY = y;
               char lab[8];
               snprintf(lab, sizeof(lab), "%d", (int)dv);
               dl->AddText(ImGui::GetFont(), fs, ImVec2(cx + 11.0f, y - fs * 0.5f), labCol, lab);
            }
         }
      }

      // Caption below, same baseline rule as the knob's.
      const char* caption = label[0] == '#' ? "" : label;
      const ImVec2 textSize = ImGui::CalcTextSize(caption);
      const ImU32 capTextCol = isLight
         ? (readOnly ? IM_COL32(125, 130, 145, 255) : IM_COL32(50, 55, 70, 255))
         : (readOnly ? IM_COL32(140, 140, 150, 255) : IM_COL32(176, 182, 198, 255));
      const float capTextY = p.y + height + 4.0f;
      dl->PushClipRect(ImVec2(p.x, capTextY), ImVec2(p.x + cell, capTextY + textH), true);
      dl->AddText(ImVec2(cx - textSize.x * 0.5f, capTextY), capTextCol, caption);
      dl->PopClipRect();

      if (hovered || active)
      {
         char buf[48];
         FormatParamValue(buf, sizeof(buf), fmt, *value, valueToPos == ConsoleFaderTaper::ValueToPos);
         SetAudioReadout(caption, buf);
      }

      ImGui::SetCursorScreenPos(p);
      ImGui::Dummy(ImVec2(cell, rowH));
      return changed;
   }

   bool KnobFloat(const char* label, float* value, float minV, float maxV, const char* fmt,
                  float diameter, ImU32 fillColor, bool readOnly, float cellW = 0.0f,
                  FaderPosToValueFn posToValue = nullptr, FaderValueToPosFn valueToPos = nullptr)
   {
      const float kTwoPi = 6.28318530717958647692f;
      const float aMin = 0.75f * kTwoPi * 0.5f; // 135 deg
      const float aMax = 2.25f * kTwoPi * 0.5f; // 405 deg (270 deg sweep)

      const float cell = cellW > 0.0f ? cellW : diameter;
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const char* caption = label[0] == '#' ? "" : label;
      const float textH = (caption[0] == '\0') ? 0.0f : ImGui::GetTextLineHeight();
      const float rowH = (caption[0] == '\0') ? diameter : (diameter + 4.0f + textH);

      auto ValueToPos01 = [&](float v) -> float
      {
         return valueToPos ? std::clamp(valueToPos(v, minV, maxV), 0.0f, 1.0f)
                            : ((maxV > minV) ? std::clamp((v - minV) / (maxV - minV), 0.0f, 1.0f) : 0.0f);
      };
      auto Pos01ToValue = [&](float pos) -> float
      {
         return posToValue ? std::clamp(posToValue(pos, minV, maxV), minV, maxV)
                            : std::clamp(minV + (maxV - minV) * pos, minV, maxV);
      };

      // The interactive rect is the knob itself, centred in the cell - not
      // the whole cell. The cell's side margins have to stay free of this
      // widget's drag handling because that is where ModKnob puts the
      // modulation pin (v3 §3b), and a drag starting on a pin belongs to the
      // node editor's link machinery, not to the knob.
      ImGui::SetCursorScreenPos(ImVec2(p.x + (cell - diameter) * 0.5f, p.y));
      ImGui::InvisibleButton(label, ImVec2(diameter, rowH));
      const bool hovered = ImGui::IsItemHovered();
      gLastAudioWidgetInteraction = { hovered, ImGui::IsItemActive(), ImGui::IsItemActivated() };
      const bool active = !readOnly && gLastAudioWidgetInteraction.active;
      bool changed = false;
      if (active && ImGui::GetIO().MouseDelta.y != 0.0f)
      {
         const float speed = ImGui::GetIO().KeyShift ? 0.2f : 1.0f;
         const float posNow = ValueToPos01(*value);
         const float nextPos = std::clamp(posNow - ImGui::GetIO().MouseDelta.y * speed / 200.0f, 0.0f, 1.0f);
         const float next = Pos01ToValue(nextPos);
         if (next != *value)
         {
            *value = next;
            changed = true;
         }
      }

      // Centred in the cell, not left-aligned to it - that centring is what
      // makes a row of mixed large/small knobs read as a rack strip instead
      // of a ragged left-packed list.
      const ImVec2 center(p.x + cell * 0.5f, p.y + diameter * 0.5f);
      const float radius = diameter * 0.5f - 2.0f;
      const float t = ValueToPos01(*value);
      const float angle = aMin + t * (aMax - aMin);

      const bool isLight = IsThemeLight();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      if (isLight)
      {
         dl->AddCircleFilled(center, radius, IM_COL32(220, 224, 234, 255), 32);
         dl->AddCircleFilled(ImVec2(center.x, center.y - 0.5f), radius - 3.0f, IM_COL32(242, 245, 250, 255), 32);
         dl->PathArcTo(center, radius + 2.5f, aMin, aMax, 32);
         dl->PathStroke(IM_COL32(195, 200, 212, 255), 0, 3.0f);
         if (t > 0.0f)
         {
            dl->PathArcTo(center, radius + 2.5f, aMin, angle, 32);
            dl->PathStroke(fillColor, 0, 3.0f);
         }
         const ImVec2 tipIn(center.x + cosf(angle) * (radius * 0.35f), center.y + sinf(angle) * (radius * 0.35f));
         const ImVec2 tipOut(center.x + cosf(angle) * (radius - 3.0f), center.y + sinf(angle) * (radius - 3.0f));
         dl->AddLine(tipIn, tipOut, readOnly ? IM_COL32(140, 145, 160, 255) : IM_COL32(40, 45, 60, 255), 2.0f);
         dl->AddCircle(center, radius, IM_COL32(175, 180, 195, 255), 32, 1.0f);
         if (hovered && !readOnly)
            dl->AddCircle(center, radius + 2.5f, IM_COL32(0, 0, 0, 30), 32, 3.0f);
      }
      else
      {
         dl->AddCircleFilled(center, radius, IM_COL32(36, 38, 48, 255), 32);
         dl->AddCircleFilled(ImVec2(center.x, center.y - 0.5f), radius - 3.0f, IM_COL32(22, 23, 30, 255), 32);
         dl->PathArcTo(center, radius + 2.5f, aMin, aMax, 32);
         dl->PathStroke(IM_COL32(58, 62, 76, 255), 0, 3.0f);
         if (t > 0.0f)
         {
            dl->PathArcTo(center, radius + 2.5f, aMin, angle, 32);
            dl->PathStroke(fillColor, 0, 3.0f);
         }
         const ImVec2 tipIn(center.x + cosf(angle) * (radius * 0.35f), center.y + sinf(angle) * (radius * 0.35f));
         const ImVec2 tipOut(center.x + cosf(angle) * (radius - 3.0f), center.y + sinf(angle) * (radius - 3.0f));
         dl->AddLine(tipIn, tipOut, readOnly ? IM_COL32(200, 202, 212, 255) : IM_COL32(238, 240, 248, 255), 2.0f);
         dl->AddCircle(center, radius, IM_COL32(74, 78, 94, 255), 32, 1.0f);
         if (hovered && !readOnly)
            dl->AddCircle(center, radius + 2.5f, IM_COL32(255, 255, 255, 40), 32, 3.0f);
      }

      // The knob's permanent caption is the param *name*, not its value - a
      // hardware knob doesn't print a live number on its cap either. The
      // exact value goes to the node's readout strip on hover/drag (see
      // SetAudioReadout), and is always reachable precisely via
      // double-click/right-click-to-type (see ModKnob).
      if (caption[0] != '\0')
      {
         // Clip rather than let a long caption widen the cell and break the
         // row's fit-to-body-width guarantee.
         ImVec2 textSize = ImGui::CalcTextSize(caption);
         const ImU32 capCol = isLight
            ? (readOnly ? IM_COL32(125, 130, 145, 255) : IM_COL32(50, 55, 70, 255))
            : (readOnly ? IM_COL32(140, 140, 150, 255) : IM_COL32(176, 182, 198, 255));
         const float capY = p.y + diameter + 4.0f;
         if (textSize.x <= cell)
         {
            dl->AddText(ImVec2(center.x - textSize.x * 0.5f, capY), capCol, caption);
         }
         else
         {
            dl->PushClipRect(ImVec2(p.x, capY), ImVec2(p.x + cell, capY + textH), true);
            dl->AddText(ImVec2(p.x, capY), capCol, caption);
            dl->PopClipRect();
         }
      }

      if (hovered || active)
      {
         char buf[48];
         FormatParamValue(buf, sizeof(buf), fmt, *value, valueToPos == ConsoleFaderTaper::ValueToPos);
         SetAudioReadout(caption, buf);
      }

      // Reserve the full cell so a caller that just chains widgets normally
      // still advances by a cell, while a row layout (AudioKnobRow) that
      // positions each cell absolutely gets the correct row height from the
      // last cell it draws.
      ImGui::SetCursorScreenPos(p);
      ImGui::Dummy(ImVec2(cell, rowH));
      return changed;
   }

   // Knob counterpart of ModSlider - same pin/typing/expression/undo/hotkey
   // behaviour (copied verbatim), just a KnobFloat in place of each
   // SliderFloat. Kept as a full copy rather than a shared helper because the
   // two widgets' read-only/interactive branches differ in exactly which
   // extra decorations (fx badge, x button) they draw around the control,
   // and threading a widget-drawing callback through that would obscure more
   // than it'd save.
   // `diameter` is the knob's diameter, or the fader's *height* when
   // style is VFader - in both cases the widget's vertical extent, which is
   // all the pin placement below actually needs from it.
   bool ModKnob(const char* label, float* value, float minV, float maxV, const char* fmt = "%.3f",
                float diameter = kKnobDiameter, float cellW = 0.0f,
                AudioWidgetStyle style = AudioWidgetStyle::Knob)
   {
      // Turbo 0.51 (upstream): the dB styles run through the console taper, the
      // frequency style through the octave-linear one.
      FaderPosToValueFn p2v = nullptr;
      FaderValueToPosFn v2p = nullptr;
      if (style == AudioWidgetStyle::VFaderDb || style == AudioWidgetStyle::KnobDb)
      {
         p2v = ConsoleFaderTaper::PosToValue;
         v2p = ConsoleFaderTaper::ValueToPos;
      }
      else if (style == AudioWidgetStyle::KnobFreq)
      {
         p2v = FrequencyTaper::PosToValue;
         v2p = FrequencyTaper::ValueToPos;
      }
      auto DrawWidget = [&](float* v, ImU32 col, bool readOnly) -> bool
      {
         const float cw = cellW > 0.0f ? cellW : 0.0f;
         if (style == AudioWidgetStyle::VFader || style == AudioWidgetStyle::VFaderDb)
            return VFaderFloat(label, v, minV, maxV, fmt, diameter, col, readOnly, cw, p2v, v2p);
         return KnobFloat(label, v, minV, maxV, fmt, diameter, col, readOnly, cw, p2v, v2p);
      };
      const bool dbTaperStyle = style == AudioWidgetStyle::VFaderDb || style == AudioWidgetStyle::KnobDb;
      const float widgetW = (style == AudioWidgetStyle::VFader || style == AudioWidgetStyle::VFaderDb)
                               ? kFaderWidth : diameter;

      const int nodeIndex = gCurrentNodeIndex;
      const int paramIndex = gParamCounter++;

      ParamRef ref;
      ref.nodeIndex = nodeIndex;
      ref.paramIndex = paramIndex;
      ref.value = value;
      ref.minValue = minV;
      ref.maxValue = maxV;
      ref.taper = (style == AudioWidgetStyle::VFaderDb || style == AudioWidgetStyle::KnobDb) ? 1
                : (style == AudioWidgetStyle::KnobFreq ? 2 : 0);
      ref.name = label;
      Modulation::Instance().RegisterParam(ref);

      const int pinId = nodeIndex * GraphNode::kStride + GraphNode::kParamBase + paramIndex;
      const bool modulated = Modulation::Instance().IsModulated(nodeIndex, paramIndex);
      const bool hasExpr = !modulated && Modulation::Instance().HasExpression(nodeIndex, paramIndex);
      const std::string* hasExprErr = hasExpr ? Modulation::Instance().ExpressionErrorFor(nodeIndex, paramIndex)
                                               : nullptr;
      const bool exprErrored = hasExprErr != nullptr && !hasExprErr->empty();
      gDrawnParamPins.insert(pinId);

      ImGui::PushID(paramIndex + 5000);

      // v2 put this pin *before* the knob on the same line, costing 18px of
      // horizontal row budget per knob (108px on Oscillator's six-control
      // row - a third of why that row overflowed kAudioNodeWidth) and
      // leaving a line of dots floating above the knob caps because a 14px
      // pin and a 70px knob baseline-align to different centres (v3 §1g).
      // v3 draws the knob first and tucks the pin into the cell's own left
      // margin afterwards, so it costs no row width at all.
      const ImVec2 cellOrigin = ImGui::GetCursorScreenPos();
      const float cell = cellW > 0.0f ? cellW : diameter;
      const ImU32 pinColor = modulated              ? IM_COL32(255, 190, 90, 255)
                            : hasExpr && !exprErrored ? IM_COL32(170, 130, 255, 255)
                                                      : IM_COL32(130, 138, 162, 255);

      const std::pair<int, int> editKey(nodeIndex, paramIndex);
      bool typing = gTypedParam.count(editKey) > 0;

      bool changed = false;
      if (typing && !modulated)
      {
         // The field takes the widget's place inside the same cell, centred on
         // where the knob cap was, and the cell's full height is reserved
         // afterwards - so starting to type never reflows the row around it.
         // Before this it was a bare 60px InputText whose label ImGui drew
         // OUTSIDE the box to the right, which both shrank the field and
         // spilled the caption across the neighbouring cell: exactly the
         // "entering a formula breaks the UI" failure.
         const float fieldW = std::min(cell - 6.0f, 96.0f);
         const float fieldH = ImGui::GetFrameHeight();
         ImGui::SetCursorScreenPos(ImVec2(cellOrigin.x + (cell - fieldW) * 0.5f,
                                          cellOrigin.y + (diameter - fieldH) * 0.5f));
         ImGui::SetNextItemWidth(fieldW);
         if (gTypedParamJustOpened == editKey)
         {
            ImGui::SetKeyboardFocusHere();
            gTypedParamJustOpened = std::pair<int, int>(-1, -1);
         }
         char buf[256];
         snprintf(buf, sizeof(buf), "%s", gTypedParamText[editKey].c_str());
         const std::string typedId = std::string("##typed") + label;
         bool entered = false;
         {
            MixedCaseFontScope trueCase; // Turbo 0.59: `t` and `T` must look different here
            entered = ImGui::InputText(typedId.c_str(), buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue);
         }
         gTypedParamText[editKey] = buf;
         if (gTypedParamPendingInit.count(editKey) && ImGui::IsItemActive())
         {
            if (ImGuiInputTextState* state = ImGui::GetInputTextState(ImGui::GetItemID()))
            {
               if (gTypedParamNoAutoSelect.count(editKey))
               {
                  state->Stb.cursor = state->CurLenW;
                  state->ClearSelection();
               }
               else
               {
                  state->SelectAll();
               }
            }
            gTypedParamPendingInit.erase(editKey);
         }
         if (ImGui::IsItemActivated())
            PushUndoCheckpoint();
         if (entered || ImGui::IsItemDeactivated())
         {
            const std::string trimmed = TrimCopy(gTypedParamText[editKey]);
            std::string exprText;
            if (TypedTextExpression(trimmed, exprText))
            {
               if (exprText.empty())
                  Modulation::Instance().ClearExpression(nodeIndex, paramIndex);
               else
                  Modulation::Instance().SetExpression(nodeIndex, paramIndex, exprText);
            }
            else
            {
               Modulation::Instance().ClearExpression(nodeIndex, paramIndex);
               if (!trimmed.empty())
               {
                  char* end = nullptr;
                  float parsed = strtof(trimmed.c_str(), &end);
                  // Turbo 0.51: "-inf" (also "-inf dB") sets the floor explicitly.
                  if (trimmed.size() >= 4 && trimmed[0] == '-' && (trimmed[1] | 32) == 'i' &&
                      (trimmed[2] | 32) == 'n' && (trimmed[3] | 32) == 'f')
                  {
                     parsed = -1.0e9f;
                     end = const_cast<char*>(trimmed.c_str()) + 4;
                  }
                  if (end != trimmed.c_str())
                  {
                     *value = std::clamp(parsed, std::min(minV, maxV), std::max(minV, maxV));
                     changed = true;
                  }
               }
            }
            gTypedParam.erase(editKey);
            gTypedParamText.erase(editKey);
            gTypedParamNoAutoSelect.erase(editKey);
            gTypedParamPendingInit.erase(editKey);
         }
         // Same footprint the widget would have had, so the row is identical
         // whether or not one of its cells is being typed into.
         ImGui::SetCursorScreenPos(cellOrigin);
         ImGui::Dummy(ImVec2(cell, diameter + 4.0f + ImGui::GetTextLineHeight()));
      }
      else if (modulated)
      {
         float shown = *value;
         DrawWidget(&shown, IM_COL32(255, 190, 90, 255), /*readOnly=*/true);
         DrawModulationBindingMenu(nodeIndex, paramIndex, gLastAudioWidgetInteraction.hovered);
      }
      else if (hasExpr && !exprErrored)
      {
         float shown = *value;
         DrawWidget(&shown, IM_COL32(170, 130, 255, 255), /*readOnly=*/true);
         const bool knobHovered = gLastAudioWidgetInteraction.hovered;
         // The fx badge and its clear button are placed absolutely in the
         // cell's top-right corner rather than chained with SameLine: a
         // SameLine here would push the row past the body width the moment
         // any one param got an expression bound, which is exactly the kind
         // of "the layout is whatever it adds up to" failure v3 exists to
         // remove.
         const ImVec2 cursorAfterKnob = ImGui::GetCursorScreenPos();
         ImGui::GetWindowDrawList()->AddText(ImVec2(cellOrigin.x + cell - 16.0f, cellOrigin.y),
                                             IM_COL32(168, 130, 250, 255), "fx");
         ImGui::SetCursorScreenPos(ImVec2(cellOrigin.x + cell - 16.0f, cellOrigin.y + 13.0f));
         ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
         ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.15f, 0.15f, 1.0f));
         ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
         if (ImGui::SmallButton("x"))
            Modulation::Instance().ClearExpression(nodeIndex, paramIndex);
         ImGui::PopStyleColor(3);
         const bool hovered = knobHovered;
         ImGui::SetCursorScreenPos(cursorAfterKnob);
         if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            BeginTypedEditFromCurrent(editKey, nodeIndex, paramIndex, value, fmt, /*hasExpr=*/true, dbTaperStyle);
         if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
         {
            BeginTypedEditFromCurrent(editKey, nodeIndex, paramIndex, value, fmt, /*hasExpr=*/true, dbTaperStyle);
            gParamRightClickConsumedThisFrame = true;
         }
         if (hovered)
            HandleParamTypeHotkeys(editKey, value);
      }
      else // plain interactive, including a currently-errored expression
      {
         changed = DrawWidget(value, IM_COL32(120, 200, 255, 235), /*readOnly=*/false);
         if (gLastAudioWidgetInteraction.activated)
         {
            PushUndoCheckpoint();
            // Turbo 0.51: Ctrl/Alt+click on a dB-taper control resets it to 0 dB.
            if (dbTaperStyle && (ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyAlt) && minV <= 0.0f && maxV >= 0.0f)
            {
               *value = 0.0f;
               changed = true;
            }
         }
         GestureWidgetHook(nodeIndex, paramIndex, *value, gLastAudioWidgetInteraction.active,
                           gLastAudioWidgetInteraction.activated);
         const bool hovered = gLastAudioWidgetInteraction.hovered;
         if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            BeginTypedEditFromCurrent(editKey, nodeIndex, paramIndex, value, fmt, /*hasExpr=*/false, dbTaperStyle);
         if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
         {
            BeginTypedEditFromCurrent(editKey, nodeIndex, paramIndex, value, fmt, /*hasExpr=*/false, dbTaperStyle);
            gParamRightClickConsumedThisFrame = true;
         }
         if (hovered && !gLastAudioWidgetInteraction.active)
            HandleParamTypeHotkeys(editKey, value);
      }

      // The modulation pin, tucked into the cell's left margin at the knob's
      // base - inside the cell the row already paid for, outside the knob's
      // own interactive rect so a drag from the pin starts a link instead of
      // turning the knob. Same pin id, same ed::BeginPin/EndPin pair, same
      // Modulation registration as before: only where it is drawn changed.
      {
         const ImVec2 cursorAfter = ImGui::GetCursorScreenPos();
         const float box = 12.0f;
         // Immediately to the left of the knob, not at the far edge of the
         // cell: in a wide cell a pin parked at the cell boundary reads as
         // belonging to the gap between two knobs rather than to either one.
         // 8px of clear space between the pin's ring and the widget's outer
         // arc. At the old 2px the two visually touched and read as one
         // control with a wart on it rather than as a knob and its pin.
         const float pinX = cellOrigin.x + std::max(0.0f, (cell - widgetW) * 0.5f - box - 8.0f);
         const ImVec2 pinTL(pinX, cellOrigin.y + diameter - box);
         ImGui::SetCursorScreenPos(pinTL);
         ed::BeginPin(pinId, ed::PinKind::Input);
         ed::PinPivotAlignment(ImVec2(0.5f, 0.5f));
         ImGui::Dummy(ImVec2(box, box));
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const ImVec2 c(pinTL.x + box * 0.5f, pinTL.y + box * 0.5f);
         // A ring rather than a bare dot: at 4px a filled dot beside a 56px
         // knob reads as a rendering artifact, not an affordance.
         if (CategoryColors::IsDesignTheme())
            DrawDesignParamPin(dl, c, modulated ? 1 : (hasExpr ? 2 : 0));
         else
         {
            dl->AddCircleFilled(c, 4.0f, IM_COL32(18, 19, 25, 255));
            dl->AddCircle(c, modulated || hasExpr ? 4.0f : 4.5f, pinColor, 12, 2.0f);
            if (modulated || hasExpr)
               dl->AddCircleFilled(c, 2.0f, pinColor);
         }
         DecorateParamPin(dl, c, nodeIndex, paramIndex, label);
         ed::EndPin();
         ImGui::SetCursorScreenPos(cursorAfter);
      }

      ImGui::PopID();
      return changed;
   }

   // Integer knob. Unlike ModSliderInt above, this one has to *accumulate*.
   //
   // A slider is absolute - the value is wherever the grab is, so re-deriving
   // the backing float from the int each frame costs nothing. A knob is
   // relative: KnobFloat adds this frame's mouse delta to the float and the
   // int is rounded off it. Re-syncing the float from the int every frame
   // therefore threw away every fraction of a step, so a drag only registered
   // when a *single frame's* delta crossed half a step - about 14 px at once
   // for a 1..8 unison knob, and completely impossible for a wide range. That
   // is why the integer knobs "didn't work perfectly".
   //
   // So the accumulator persists across frames, and is re-seeded from `*value`
   // only when the param moved for some reason other than this knob's own drag
   // (a patch load, an undo, a typed value, a macro). Comparing against what
   // this widget last wrote is what distinguishes the two cases.
   bool ModKnobInt(const char* label, int* value, int minV, int maxV, float diameter = kKnobDiameter,
                   float cellW = 0.0f)
   {
      const std::pair<int, int> key(gCurrentNodeIndex, gParamCounter);
      float& slot = gIntParamStore[key];
      float perfValue = 0.0f;
      if (TakePerfSlotWrite(key, perfValue))
         slot = perfValue; // Turbo 0.46: Performance Mode
      else if (!Modulation::Instance().IsModulated(key.first, key.second))
      {
         auto last = gIntParamLastWritten.find(key);
         if (last == gIntParamLastWritten.end() || last->second != *value)
            slot = (float)*value;
      }

      bool changed = ModKnob(label, &slot, (float)minV, (float)maxV, "%.0f", diameter, cellW);
      slot = std::clamp(slot, (float)minV, (float)maxV);
      *value = (int)lroundf(slot);
      gIntParamLastWritten[key] = *value;
      return changed;
   }
