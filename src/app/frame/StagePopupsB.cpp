// Split out of main(): see docs/plans/main-split/README.md (Block C)
#include "app/ui/design/components/MenuParts.h"
#include "app/ui/design/components/ChipButton.h"
#include "app/ui/design/components/FieldWell.h"
#include "app/ui/design/components/StateRing.h"
#include "imgui_internal.h"
#include "app/ui/design/components/LibraryParts.h"
#include "app/ui/design/TokenColors.h"
#include "app/frame/FrameCtx.h"

namespace app
{
void DrawPopupsB(FrameCtx& fc)
{
   auto& window = fc.window;
   auto& allTypes = fc.allTypes;
   auto& searchBuf = fc.searchBuf;
   auto& searchJustOpened = fc.searchJustOpened;
   auto& searchPopupCentered = fc.searchPopupCentered;
   auto& searchPopupOpen = fc.searchPopupOpen;
   auto& searchRequestClose = fc.searchRequestClose;
   auto& frameId = fc.frameId;


      // dev-only: force the picker open (optionally with a query) to verify layout
      if (const char* pk = getenv("INFINITE_PICKERTEST"))
      {
         if (frameId == 6)
         {
            gSpawnPos = ImVec2(200.0f, 200.0f);
            snprintf(searchBuf, sizeof(searchBuf), "%s", std::string(pk) == "empty" ? "" : pk);
            searchJustOpened = (std::string(pk) == "empty");
            ImGui::OpenPopup("search");
         }
      }

      if (gDropdown.justOpened)
      {
         ImGui::OpenPopup("##dropdown");
         gDropdown.justOpened = false;
         gDropdown.filterBuf[0] = '\0'; // never reopen on the previous list's search text
      }

      FrameTest_COLORTEST_3(frameId, window);

      if (gCommentEdit.justOpened)
      {
         ImGui::OpenPopup("##commentedit");
         gCommentEdit.justOpened = false;
      }

      // the comment may have been deleted while its editor was open
      if (gCommentEdit.target != nullptr)
      {
         bool alive = false;
         for (const GraphNode& gn : gNodes)
         {
            if (gn.node.get() == gCommentEdit.target)
               alive = true;
         }
         if (!alive)
            gCommentEdit.target = nullptr;
      }

      // Pinned to the node's own on-screen box (refreshed every frame in
      // DrawCommentPreview) so the editor lands exactly over the note instead
      // of opening as a separate window elsewhere on screen - typing is meant
      // to read as happening straight into the box you double-clicked.
      if (gCommentEdit.target != nullptr && gCommentEditRect.z > 0.0f)
      {
         ImGui::SetNextWindowPos(ImVec2(gCommentEditRect.x, gCommentEditRect.y));
         ImGui::SetNextWindowSize(ImVec2(gCommentEditRect.z, gCommentEditRect.w));
      }
      const float commentZoom = std::max(0.1f, gCommentEditZoom > 0.0f ? gCommentEditZoom : ed::GetCurrentZoom());
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
      ImGui::PushStyleColor(ImGuiCol_PopupBg, tok::V4(tok::palf::v_0_0_0_0));
      ImGui::PushStyleColor(ImGuiCol_Border, tok::V4(tok::palf::v_0_0_0_0));

      if (ImGui::BeginPopup("##commentedit", ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar))
      {
         if (gCommentEdit.target != nullptr)
         {
            CommentNode* c = gCommentEdit.target;
            gCommentEdit.framesOpen++;
            if (gCommentEdit.framesOpen <= 2) // see CommentEditRequest::framesOpen
            {
               ImGui::SetWindowFocus();
               ImGui::SetKeyboardFocusHere();
            }

            const bool isLight = CategoryColors::IsThemeLight();
            const float* col = c->color;
            const ImVec4 textCol = isLight
               ? ImVec4(30.0f / 255.0f, 36.0f / 255.0f, 48.0f / 255.0f, 1.0f)
               : ImVec4(col[0] * 0.5f + 0.5f, col[1] * 0.5f + 0.5f, col[2] * 0.5f + 0.5f, 1.0f);

            const float fontScale = CommentFontScale(c->fontSize);
            ImGui::PushStyleColor(ImGuiCol_Text, textCol);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, tok::V4(tok::palf::v_0_0_0_0));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, tok::V4(tok::palf::v_0_0_0_0));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, isLight ? tok::V4(tok::palf::v_0_0_0_150) : tok::V4(tok::palf::v_1000_1000_1000_180));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, isLight ? tok::V4(tok::palf::v_0_0_0_300) : tok::V4(tok::palf::v_1000_1000_1000_350));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, isLight ? tok::V4(tok::palf::v_0_0_0_450) : tok::V4(tok::palf::v_1000_1000_1000_500));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f * commentZoom, 8.0f * commentZoom));
            ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 6.0f * commentZoom);
            ImGui::SetWindowFontScale(commentZoom * fontScale);

            // Filling the whole popup cleanly at 1:1 scale with the canvas card
            FieldWell::InputTextMultiline("##commenttext", &c->text, ImVec2(gCommentEditRect.z, gCommentEditRect.w));

            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(6);

            // The checkpoint was pushed when the editor opened, so every
            // keystroke here is part of that one undo step; all that is left is
            // to keep the patch marked unsaved.
            if (ImGui::IsItemEdited())
               gPatchDirty = true;
            // Esc finishes the same way clicking outside does - no separate
            // "done" step needed for a note this small.
            if (ImGui::IsKeyPressed(ImGuiKey_Escape))
               ImGui::CloseCurrentPopup();
         }
         else
         {
            ImGui::CloseCurrentPopup();
         }
         ImGui::EndPopup();
      }
      ImGui::PopStyleColor(2);
      ImGui::PopStyleVar(3);
      if (!ImGui::IsPopupOpen("##commentedit"))
      {
         gCommentEdit.target = nullptr;
         gCommentEdit.framesOpen = 0;
      }

      if (gColor.justOpened)
      {
         ImGui::OpenPopup("##colorpick");
         gColor.justOpened = false;
      }

      // the owning node may have been deleted while the picker was open
      if (gColor.owner != nullptr)
      {
         bool alive = false;
         for (const GraphNode& gn : gNodes)
         {
            if (gn.node.get() == gColor.owner)
               alive = true;
         }
         if (!alive)
         {
            gColor.owner = nullptr;
            gColor.target = nullptr;
         }
      }

      if (MenuParts::BeginPopup("##colorpick"))
      {
         if (gColor.target != nullptr)
         {
            ImGui::TextDisabled("%s", gColor.label.c_str());
            gColorPickerRect = ImVec4(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y,
                                      0.0f, 0.0f);
            ImGui::ColorPicker3("##pick", gColor.target,
                                ImGuiColorEditFlags_PickerHueBar |
                                ImGuiColorEditFlags_DisplayRGB |
                                ImGuiColorEditFlags_DisplayHSV |
                                ImGuiColorEditFlags_DisplayHex);
            gColorPickerRect.z = ImGui::GetItemRectSize().x;
            gColorPickerRect.w = ImGui::GetItemRectSize().y;
         }
         else
         {
            ImGui::CloseCurrentPopup();
         }
         MenuParts::EndPopup();
      }

      ImGui::SetNextWindowSizeConstraints(ImVec2(260, 0), ImVec2(320, 440));
      if (searchPopupCentered)
      {
         const ImVec2 center = ImVec2(gGraphScreenTL.x + gGraphScreenSize.x * 0.5f,
                                      gGraphScreenTL.y + gGraphScreenSize.y * 0.5f);
         ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
      }
      if (MenuParts::BeginPopup("search"))
      {
         searchPopupCentered = false;
         searchPopupOpen = true;
         // Shift+N pressed again while the picker was up - see searchRequestClose.
         if (searchRequestClose)
         {
            searchRequestClose = false;
            searchPopupOpen = false;
            searchJustOpened = false;
            ImGui::CloseCurrentPopup();
         }
         // The accent ring ImGui draws on the nav-focused row (the first item of each opened category)
         // is dropped here; the row keeps its quiet header wash, which only shows while the keyboard drives.
         ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
         // Same search field as the Library panel (rounded well, our own magnifier glyph, clear button).
         // SetKeyboardFocusHere() targets the next item, which is its InputText.
         if (searchJustOpened)
         {
            ImGui::SetKeyboardFocusHere();
            ImGui::GetCurrentContext()->NavCursorVisible = false;  // no focus ring until the keyboard navigates
            searchJustOpened = false;
         }
         LibraryParts::SearchField("nodepicker", T("search nodes..."), searchBuf, sizeof(searchBuf));
         MenuParts::Separator();

         std::string q(searchBuf);
         std::transform(q.begin(), q.end(), q.begin(), ::tolower);

         const bool pickFirst = ImGui::IsKeyPressed(ImGuiKey_Enter, false);
         std::string spawnName, spawnCategory;
         int shown = 0;

         if (q.empty())
         {
            // Link-drag search: a cable was just dropped on empty canvas, so
            // lead with the node types compatible with the pin it came from
            // rather than making the user scroll every category to find one.
            if (!gLinkDragSuggestions.empty())
            {
               ImGui::TextDisabled("%s", T("Suggested"));
               for (const auto& t : gLinkDragSuggestions)
               {
                  ++shown;
                  const std::string title = DisplayName(t.first);
                  const std::string category = DisplayName(t.second);
                  if (MenuParts::Item(title.c_str(), category.c_str()))
                  {
                     spawnName = t.first;
                     spawnCategory = t.second;
                  }
               }
               MenuParts::Separator();
            }

            // no query yet: browse by category submenu
            std::vector<std::string> cats = NodeFactory::Instance().GetCategories();
            std::stable_sort(cats.begin(), cats.end(), [](const std::string& a, const std::string& b) {
               return CategoryColors::SemanticRank(a) < CategoryColors::SemanticRank(b);
            });

            for (const std::string& category : cats)
            {
               ImGui::SetNextWindowSizeConstraints(ImVec2(180, 0), ImVec2(320, 440));
               if (MenuParts::SubMenu(DisplayName(category).c_str()))
               {
                  for (const std::string& name : NodeFactory::Instance().GetNodesInCategory(category))
                  {
                     if (!IsUserSpawnable(name))
                        continue;
                     ++shown;
                     if (MenuParts::Item(DisplayName(name).c_str()))
                     {
                        spawnName = name;
                        spawnCategory = category;
                     }
                  }
                  ImGui::EndMenu();
               }
            }
         }
         else
         {
            for (const auto& t : allTypes)
            {
               std::string hay = DisplayName(t.first) + " " + DisplayName(t.second);
               if (I18n::CurrentLanguage() != "en")
                  hay += " " + NodeSearchHaystack(t.first, t.second);
               if (!NodeSearchMatches(hay, q))
                  continue;
               ++shown;
               const std::string title = DisplayName(t.first);
               const std::string category = DisplayName(t.second);
               bool activate = MenuParts::Item(title.c_str(), category.c_str());
               if (shown == 1 && pickFirst)
                  activate = true;
               if (activate)
               {
                  spawnName = t.first;
                  spawnCategory = t.second;
               }
            }
            if (shown == 0)
               ImGui::TextDisabled("%s", T("no matches"));
         }

         if (!spawnName.empty())
         {
            GraphNode* spawned = SpawnNode(spawnName, spawnCategory, gSpawnPos.x, gSpawnPos.y);
            if (gLinkDragSourcePin >= 0 && spawned != nullptr)
            {
               GraphNode* dragSrcNode = FindNodeByIndex(GraphNode::NodeIndexFromPin(gLinkDragSourcePin));
               if (dragSrcNode != nullptr)
               {
                  const int dragOutputSlot = GraphNode::OutputIndexFromPin(gLinkDragSourcePin);
                  const bool srcIsModulator = (dynamic_cast<IModulator*>(dragSrcNode->node.get()) != nullptr ||
                                               ModulatorForOutput(dragSrcNode->node.get(), dragOutputSlot) != nullptr);
                  auto* srcPalette = dynamic_cast<IPaletteSource*>(dragSrcNode->node.get());
                  auto* srcGeometry = dynamic_cast<IGeometrySource*>(dragSrcNode->node.get());
                  if (srcGeometry != nullptr && !srcGeometry->IsGeometryOutputIndex(dragOutputSlot))
                     srcGeometry = nullptr;
                  auto* srcCamera = dynamic_cast<CameraNode*>(dragSrcNode->node.get());
                  auto* srcLight = dynamic_cast<LightNode*>(dragSrcNode->node.get());
                  const bool srcIsEnvironment =
                     dynamic_cast<EnvironmentNode*>(dragSrcNode->node.get()) != nullptr;
                  auto* srcAudioSource = dynamic_cast<IAudioSource*>(dragSrcNode->node.get());
                  const bool srcIsAudioNode =
                     srcAudioSource != nullptr && srcAudioSource->IsAudioOutputIndex(dragOutputSlot);
                  const bool srcIsNoteSource =
                     dynamic_cast<INoteSource*>(dragSrcNode->node.get()) != nullptr;
                  const bool srcIsPredictor =
                     dynamic_cast<IPredictor*>(dragSrcNode->node.get()) != nullptr;

                  const int slotCount = InputCountFor(*spawned);
                  for (int slot = 0; slot < slotCount; ++slot)
                  {
                     if (IsInputSlotCompatible(spawned, slot, srcIsModulator, srcPalette, srcGeometry,
                                                srcCamera, srcLight, srcIsEnvironment,
                                                srcIsAudioNode, srcIsNoteSource, srcIsPredictor))
                     {
                        // No PushUndoCheckpoint() here: SpawnNode() above already
                        // pushed one capturing the state before the node existed,
                        // so a single Undo removes the spawn and the wire
                        // together, as one user action - not two separate steps.
                        WireInputSlot(*dragSrcNode, *spawned, slot,
                                      GraphNode::OutputIndexFromPin(gLinkDragSourcePin));
                        if (srcIsAudioNode || srcIsNoteSource ||
                            spawned->node->AudioInputSlot(slot) != nullptr ||
                            spawned->node->NoteInputSlot(slot) != nullptr)
                           RebuildAudioTopology();
                        break;
                     }
                  }
               }
            }
            gLinkDragSourcePin = -1;
            gLinkDragSuggestions.clear();
            ImGui::CloseCurrentPopup();
         }
         ImGui::PopStyleColor();
         MenuParts::EndPopup();
      }
      else
      {
         // Popup closed without a pick (Escape, click-away, or the manual
         // spawn-menu paths below) - clear so a later unrelated spawn doesn't
         // inherit a stale suggestion list from an earlier drag.
         gLinkDragSourcePin = -1;
         gLinkDragSuggestions.clear();
         searchPopupOpen = false;
         searchRequestClose = false;
      }

      // Size the popup to its actual content rather than a fixed 240px
      // floor - a short list like the filter-mode options ("off", "lp 12",
      // ...) doesn't need anywhere near that width, and a fixed floor just
      // stretches the pills into a wide, empty-feeling bar.
      const float dropdownTextPadX = 8.0f;
      float dropdownMaxTextW = 0.0f;
      for (const std::string& opt : gDropdown.options)
         dropdownMaxTextW = ImMax(dropdownMaxTextW, ImGui::CalcTextSize(opt.c_str()).x);
      for (const std::string& cat : gDropdown.categories)
         dropdownMaxTextW = ImMax(dropdownMaxTextW, ImGui::CalcTextSize(cat.c_str()).x);
      const float dropdownMinWidth = ImClamp(dropdownMaxTextW + (MenuParts::kInset + MenuParts::kTextInset) * 2.0f
                                                 + dropdownTextPadX * 2.0f
                                                 + ImGui::GetStyle().ScrollbarSize,
                                              160.0f, 400.0f);
      ImGui::SetNextWindowSizeConstraints(ImVec2(dropdownMinWidth, 0), ImVec2(520, 480));
      if (MenuParts::BeginPopup("##dropdown"))
      {
         const bool showSearch = gDropdown.focusSearch || gDropdown.options.size() >= kDropdownAutoSearchMin;
         if (showSearch)
         {
            if (ImGui::IsWindowAppearing())
            {
               ImGui::SetKeyboardFocusHere();
               ImGui::GetCurrentContext()->NavCursorVisible = false; // no focus ring until the keyboard navigates
            }
            LibraryParts::SearchField("ddsearch", T("search..."), gDropdown.filterBuf, sizeof(gDropdown.filterBuf));
            MenuParts::Separator();
         }

         const std::string q = showSearch ? FoldForSearch(gDropdown.filterBuf) : std::string();

         std::string lastCategory;
         for (int i = 0; i < (int)gDropdown.options.size(); i++)
         {
            if (!q.empty())
            {
               std::string hay = gDropdown.options[i];
               if (i < (int)gDropdown.categories.size() && !gDropdown.categories[i].empty())
                  hay += " " + gDropdown.categories[i];
               hay = FoldForSearch(hay);
               if (hay.find(q) == std::string::npos)
                  continue;
            }

            if (i < (int)gDropdown.categories.size() && !gDropdown.categories[i].empty())
            {
               if (gDropdown.categories[i] != lastCategory)
               {
                  if (i > 0)
                     MenuParts::Separator();
                  ImGui::TextDisabled("%s", gDropdown.categories[i].c_str());
                  lastCategory = gDropdown.categories[i];
               }
            }
            const bool selected = (i == gDropdown.current);
            ImGui::PushID(i);
            const bool clicked = MenuParts::Choice(gDropdown.options[i].c_str(), selected);
            ImGui::PopID();
            if (clicked)
            {
               CommitDropdownPick(i); // one click, one undo entry
               ImGui::CloseCurrentPopup();
            }
            if (selected && ImGui::IsWindowAppearing() && !gDropdown.focusSearch)
               ImGui::SetScrollHereY(0.5f);
         }

         MenuParts::EndPopup();
      }

      // Field build step 17: .infdev device Save name-prompt - same
      // deferred-popup discipline as "##dropdown" just above (opened from
      // inside a node body, rendered out here where the canvas transform
      // doesn't apply).
      if (gFieldDeviceSave.justOpened)
      {
         ImGui::OpenPopup("##fielddevicesave");
         gFieldDeviceSave.justOpened = false;
      }
      if (ImGui::BeginPopup("##fielddevicesave"))
      {
         ImGui::TextDisabled("%s", T("Save device as:"));
         ImGui::SetNextItemWidth(220.0f);
         bool enterPressed = FieldWell::InputText("##fielddevicesavename", gFieldDeviceSave.nameBuf,
                                                  sizeof(gFieldDeviceSave.nameBuf),
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
         ImGui::SameLine(0.0f, tok::space_2);
         bool doSave = enterPressed || ChipButton::Draw(L("Save##fielddevicesaveconfirm"), true, ImGui::GetFrameHeight());
         if (doSave && gFieldDeviceSave.nameBuf[0] != '\0')
         {
            Field::DeviceFile device;
            if (gFieldDeviceSave.getDeviceFile && gFieldDeviceSave.getDeviceFile(device))
            {
               const std::string dir = AppPaths::AppSupportDir() + "/Devices/" + gFieldDeviceSave.domain + "/";
               if (AppPaths::EnsureDir(dir))
               {
                  Field::SaveToFieldFile(dir + gFieldDeviceSave.nameBuf + ".field", device);
                  InvalidateFieldDeviceLibrary(gFieldDeviceSave.domain);
               }
            }
            ImGui::CloseCurrentPopup();
         }
         ImGui::EndPopup();
      }

      if (gAudioDropPicker.justOpened)
      {
         ImGui::OpenPopup("##audiodroppicker");
         gAudioDropPicker.justOpened = false;
         const ImVec2 popupPos = (gAudioDropPicker.screenPos.x != 0.0f || gAudioDropPicker.screenPos.y != 0.0f)
            ? gAudioDropPicker.screenPos
            : ed::CanvasToScreen(gAudioDropPicker.canvasPos);
         ImGui::SetNextWindowPos(popupPos, ImGuiCond_Always);
      }
      else
      {
         const ImVec2 popupPos = (gAudioDropPicker.screenPos.x != 0.0f || gAudioDropPicker.screenPos.y != 0.0f)
            ? gAudioDropPicker.screenPos
            : ed::CanvasToScreen(gAudioDropPicker.canvasPos);
         ImGui::SetNextWindowPos(popupPos, ImGuiCond_Appearing);
      }
      ImGui::SetNextWindowSizeConstraints(ImVec2(240, 0), ImVec2(360, 480));
      if (MenuParts::BeginPopup("##audiodroppicker"))
      {
         if (gAudioDropPicker.paths.empty())
         {
            ImGui::CloseCurrentPopup();
         }
         else
         {
            if (gAudioDropPicker.paths.size() == 1)
            {
               std::string filename = gAudioDropPicker.paths[0];
               const size_t lastSlash = filename.find_last_of("/\\");
               if (lastSlash != std::string::npos)
                  filename = filename.substr(lastSlash + 1);
               ImGui::TextDisabled(T("Load %s into:"), filename.c_str());
            }
            else
            {
               ImGui::TextDisabled(T("Load %d samples into:"), (int)gAudioDropPicker.paths.size());
            }
            MenuParts::Separator();

            struct PickerOption
            {
               const char* name;
               const char* category;
               const char* desc;
            };
            static const PickerOption kOptions[] = {
               { "Sampler",        "Synths",     "Sample playback with pitch & envelope" },
               { "Audio File",     "Modulators", "Streaming playback & follower" },
               { "Slicer",         "Synths",     "Beat/transient slicer" },
               { "Drum Sequencer", "Synths",     "Step sequencer, grooves & drum kit" },
               { "MPC",            "Synths",     "16-pad sample player" },
               { "PaulStretch",    "Synths",     "Extreme time-stretch & wash" },
               { "Granular",       "Synths",     "Granular cloud synthesis" },
               { "Grain Molder",   "Synths",     "Granular morph & shape" },
               { "Molder",         "Synths",     "Spectral cross-synthesis" },
            };

            for (const auto& opt : kOptions)
            {
               if (MenuParts::Item(opt.name, opt.category))
               {
                  if (std::string(opt.name) == "MPC")
                  {
                     GraphNode* spawned = SpawnNode(opt.name, opt.category, gAudioDropPicker.canvasPos.x,
                                                    gAudioDropPicker.canvasPos.y);
                     if (spawned != nullptr)
                     {
                        if (auto* mpc = dynamic_cast<MpcNode*>(spawned->node.get()))
                           for (int i = 0; i < (int)gAudioDropPicker.paths.size() && i < MpcNode::kPads; ++i)
                              mpc->LoadPad(i, gAudioDropPicker.paths[i]);
                        spawned->showParams = true;
                        gPatchDirty = true;
                        RebuildAudioTopology();
                     }
                  }
                  else if (std::string(opt.name) == "Drum Sequencer")
                  {
                     GraphNode* spawned = SpawnNode(opt.name, opt.category,
                                                    gAudioDropPicker.canvasPos.x,
                                                    gAudioDropPicker.canvasPos.y);
                     if (spawned != nullptr)
                     {
                        auto* drum = dynamic_cast<DrumSequencerNode*>(spawned->node.get());
                        if (drum != nullptr)
                        {
                           for (int i = 0; i < (int)gAudioDropPicker.paths.size() && i < DrumSequencerNode::kNumLanes; ++i)
                              drum->LoadFileToLane(i, gAudioDropPicker.paths[i]);
                        }
                        spawned->showParams = true;
                        gPatchDirty = true;
                        RebuildAudioTopology();
                     }
                  }
                  else
                  {
                     float offset = 0.0f;
                     for (const std::string& path : gAudioDropPicker.paths)
                     {
                        GraphNode* spawned = SpawnNode(opt.name, opt.category,
                                                       gAudioDropPicker.canvasPos.x + offset,
                                                       gAudioDropPicker.canvasPos.y);
                        if (spawned != nullptr)
                        {
                           if (auto* s = dynamic_cast<SamplerNode*>(spawned->node.get()))
                              s->LoadFile(path);
                           else if (auto* af = dynamic_cast<AudioFileNode*>(spawned->node.get()))
                              af->Open(path);
                           else if (auto* sl = dynamic_cast<SlicerNode*>(spawned->node.get()))
                              sl->LoadFile(path);
                           else if (auto* ps = dynamic_cast<PaulStretchNode*>(spawned->node.get()))
                              ps->LoadFile(path);
                           else if (auto* gran = dynamic_cast<GranularNode*>(spawned->node.get()))
                              gran->LoadFile(path);
                           else if (auto* gm = dynamic_cast<GrainMolderNode*>(spawned->node.get()))
                              gm->LoadFile(path);
                           else if (auto* mol = dynamic_cast<MolderNode*>(spawned->node.get()))
                              mol->LoadFile(path);

                           spawned->showParams = true;
                           gPatchDirty = true;
                           RebuildAudioTopology();
                        }
                        offset += 240.0f;
                     }
                  }
                  gAudioDropPicker.paths.clear();
                  ImGui::CloseCurrentPopup();
                  break;
               }
               if (ImGui::IsItemHovered() && opt.desc != nullptr && opt.desc[0] != '\0')
               {
                  ImGui::SetTooltip("%s", opt.desc);
               }
            }
         }
         MenuParts::EndPopup();
      }
      else
      {
         if (!gAudioDropPicker.paths.empty() && !gAudioDropPicker.justOpened)
            gAudioDropPicker.paths.clear();
      }

      gHoveringItem = ed::GetHoveredNode() || ed::GetHoveredPin() || ed::GetHoveredLink();

      ed::Resume();

      // Fit-to-content has to happen down here, after every node has been
      // submitted this frame. ed::Begin() marks all nodes not-live and only
      // drawing them marks them live again, and NavigateToContent() measures
      // live nodes only - so called up next to ed::Begin() it always fit an
      // empty rectangle and silently did nothing at all. The new view is
      // picked up by the next ed::Begin(), one frame later.
      if (gRequestFitView)
      {
         ed::NavigateToContent(0.0f);
         gRequestFitView = false;
      }
      if (gRequestFitViewNodeIndex >= 0)
      {
         if (GraphNode* fitNode = FindNodeByIndex(gRequestFitViewNodeIndex))
         {
            // NavigateToSelection reads the selection's bounds immediately
            // (see ax::NodeEditor::NavigateToSelection), so it's safe to clear
            // the selection right back out afterward - the node won't render
            // with a selected-highlight border on the frame that gets shot.
            ed::ClearSelection();
            ed::SelectNode(fitNode->NodeId());
            ed::NavigateToSelection(false, 0.0f);
            ed::ClearSelection();
         }
         gRequestFitViewNodeIndex = -1;
      }
      if (getenv("INFINITE_PALETTETEST") != nullptr && frameId == 3)
         gRequestFitView = true; // dev screenshot: frame the whole fixture
      if ((getenv("INFINITE_AUDIOUITEST") != nullptr || getenv("INFINITE_FXGALLERY") != nullptr) && frameId == 3)
         gRequestFitView = true; // same, for the audio node UI fixture
      if (getenv("INFINITE_LOADPATCH") != nullptr && (frameId == 2 || frameId == 4))
         gRequestFitView = true;
      if (const char* fitNode = getenv("INFINITE_FITNODE"); fitNode != nullptr && frameId == 8)
      {
         // dev screenshot: close-up on one node (zoomed in, unlike gRequestFitViewNodeIndex)
         if (GraphNode* n = FindNodeByIndex(atoi(fitNode)))
         {
            ed::ClearSelection();
            ed::SelectNode(n->NodeId());
            ed::NavigateToSelection(true, 0.0f);
            ed::ClearSelection();
         }
      }
      FrameTest_AUDIOUITEST_2(frameId, window);
      if (getenv("INFINITE_HIDETEST") != nullptr && frameId == 3)
         gRequestFitView = true; // dev screenshot: frame the whole fixture
      if ((getenv("INFINITE_WTDRAGTEST") != nullptr || getenv("INFINITE_EQDRAGTEST") != nullptr ||
          getenv("INFINITE_PREDBINDTEST") != nullptr) && frameId == 3)
         gRequestFitView = true;

      if (gPerfAssigningElemIdx >= 0 && gPerfAssigningElemIdx < (int)gPerfElements.size())
      {
         const auto& assignElem = gPerfElements[gPerfAssigningElemIdx];
         // imgui-node-editor remaps io.MousePos into its own local/zoomed
         // canvas space for the duration of ed::Begin()/ed::End() (see
         // ImGuiEx::Canvas::EnterLocalSpace), so both GetMousePos() and
         // GetIO().MousePos are canvas-space here, not real screen pixels.
         // gGraphScreenTL/gGraphScreenSize were captured before ed::Begin(),
         // in real screen space, so they have to be converted with
         // ed::ScreenToCanvas before comparing against the mouse - comparing
         // them directly (as this used to) left mouseOverGraph false at any
         // pan/zoom other than the coincidental default, which silently
         // broke every canvas-click parameter assignment.
         const ImVec2 mp = ImGui::GetMousePos();
         int hoveredPinIdx = -1;

         const ImVec2 graphTL = ed::ScreenToCanvas(gGraphScreenTL);
         const ImVec2 graphBR = ed::ScreenToCanvas(
            ImVec2(gGraphScreenTL.x + gGraphScreenSize.x, gGraphScreenTL.y + gGraphScreenSize.y));
         const bool mouseOverGraph = (mp.x >= graphTL.x && mp.x <= graphBR.x &&
                                      mp.y >= graphTL.y && mp.y <= graphBR.y);

         if (mouseOverGraph)
         {
            for (size_t pi = 0; pi < gParamPinScreenList.size(); pi++)
            {
               const auto& pInfo = gParamPinScreenList[pi];
               bool inBounds = (mp.x >= pInfo.rowMin.x && mp.x <= pInfo.rowMax.x &&
                                mp.y >= pInfo.rowMin.y && mp.y <= pInfo.rowMax.y);
               float dx = mp.x - pInfo.screenPos.x;
               float dy = mp.y - pInfo.screenPos.y;
               if (inBounds || (dx * dx + dy * dy < 20.0f * 20.0f))
               {
                  hoveredPinIdx = (int)pi;
               }
            }
         }

         if (hoveredPinIdx >= 0)
         {
            const auto& pInfo = gParamPinScreenList[hoveredPinIdx];
            // Glowing box + ring around the hovered control, drawn while still
            // inside ed::Begin/End so it rides the same local-space vertex
            // transform as the row rect it's outlining - drawing it after
            // ed::Suspend() below would place it in real screen space and it
            // would land in the wrong spot at any pan/zoom.
            {
               ImDrawList* hoverDl = ImGui::GetWindowDrawList();
               if (pInfo.isCircle)
                  StateRing::DrawCircle(hoverDl, pInfo.shapeCenter, pInfo.shapeRadius, StateRing::Kind::Target, IsThemeLight());
               else
                  StateRing::Draw(hoverDl, pInfo.rowMin, pInfo.rowMax, StateRing::Kind::Target, IsThemeLight(), tok::radius_field);
            }
            ed::Suspend();
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip(T("Assign to '%s' (%s) -> %s: %s"),
                              assignElem.label.c_str(),
                              gPerfAssigningAxis == 1 ? "Y Axis" : (assignElem.kind == 4 ? "X Axis" : "Param"),
                              pInfo.nodeTitle.c_str(), pInfo.paramName.c_str());
            ed::Resume();

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
               PushUndoCheckpoint();
               auto& el = gPerfElements[gPerfAssigningElemIdx];
               if (gPerfAssigningAxis == 0)
               {
                  el.dstIndex = pInfo.nodeIndex;
                  el.dstParam = pInfo.paramIndex;
                  el.targets.clear();
                  el.targets.push_back({ pInfo.nodeIndex, pInfo.paramIndex, "" });
                  if (el.label.empty() || el.label == "Knob" || el.label == "Fader" || el.label == "Slider" || el.label == "Toggle" || el.label == "XY Pad" || el.label == "Trigger" || el.label == "NumBox" || el.label == "Selector" || el.label == "Pan/Detent" || el.label == "Step Gate")
                     el.label = pInfo.paramName;
               }
               else if (gPerfAssigningAxis == 1)
               {
                  el.dstParam2 = pInfo.paramIndex;
                  el.targetsY.clear();
                  el.targetsY.push_back({ pInfo.nodeIndex, pInfo.paramIndex, "" });
               }
               gPerfAssigningElemIdx = -1;
            }
         }

         if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
         {
            gPerfAssigningElemIdx = -1;
         }
      }

      // Predictive Drift Follow leader picker - identical click-to-assign UX to the
      // performance-matrix picker just above, but resolves to a DriftNode's leaderNodeIndex/
      // leaderParamIndex instead of a perf element target. Deliberately stores the target's
      // *uid* (gNodes[nodeIndex].uid), not its index - an index is reused by
      // RemoveNodeByIndex and would silently repoint Follow at a different node after an
      // unrelated delete/undo, exactly the stability trap Modulation.h's own uid comment
      // (ParamRef::uid) warns about.
      if (gDriftFollowPickingUid != 0)
      {
         GraphNode* pickingGn = FindNodeByUid(gDriftFollowPickingUid);
         DriftNode* pickingDrift = pickingGn != nullptr ? dynamic_cast<DriftNode*>(pickingGn->node.get()) : nullptr;
         if (pickingDrift == nullptr)
         {
            gDriftFollowPickingUid = 0; // the node was deleted while picking
         }
         else
         {
            const ImVec2 mp = ImGui::GetMousePos();
            int hoveredPinIdx = -1;
            const ImVec2 graphTL = ed::ScreenToCanvas(gGraphScreenTL);
            const ImVec2 graphBR = ed::ScreenToCanvas(
               ImVec2(gGraphScreenTL.x + gGraphScreenSize.x, gGraphScreenTL.y + gGraphScreenSize.y));
            const bool mouseOverGraph = (mp.x >= graphTL.x && mp.x <= graphBR.x &&
                                         mp.y >= graphTL.y && mp.y <= graphBR.y);
            if (mouseOverGraph)
            {
               for (size_t pi = 0; pi < gParamPinScreenList.size(); pi++)
               {
                  const auto& pInfo = gParamPinScreenList[pi];
                  // Can't follow yourself - a self-target would drive the same slot
                  // it's reading, an infinite feedback loop with no way to break it.
                  if (pInfo.nodeIndex >= 0 && pInfo.nodeIndex < (int)gNodes.size() &&
                      gNodes[pInfo.nodeIndex].uid == gDriftFollowPickingUid)
                     continue;
                  const bool inBounds = (mp.x >= pInfo.rowMin.x && mp.x <= pInfo.rowMax.x &&
                                         mp.y >= pInfo.rowMin.y && mp.y <= pInfo.rowMax.y);
                  const float dx = mp.x - pInfo.screenPos.x;
                  const float dy = mp.y - pInfo.screenPos.y;
                  if (inBounds || (dx * dx + dy * dy < 20.0f * 20.0f))
                     hoveredPinIdx = (int)pi;
               }
            }
            if (hoveredPinIdx >= 0)
            {
               const auto& pInfo = gParamPinScreenList[hoveredPinIdx];
               {
                  ImDrawList* hoverDl = ImGui::GetWindowDrawList();
                  const ImU32 glowCol = tok::U32(tok::pal::c_78C8FF1C);
                  const ImU32 ringCol = tok::U32(tok::pal::c_78C8FFAA);
                  if (pInfo.isCircle)
                  {
                     hoverDl->AddCircleFilled(pInfo.shapeCenter, pInfo.shapeRadius, glowCol, 24);
                     hoverDl->AddCircle(pInfo.shapeCenter, pInfo.shapeRadius, ringCol, 24, 1.0f);
                  }
                  else
                  {
                     hoverDl->AddRectFilled(pInfo.rowMin, pInfo.rowMax, glowCol, 4.0f);
                     hoverDl->AddRect(pInfo.rowMin, pInfo.rowMax, ringCol, 4.0f, 0, 1.0f);
                  }
               }
               ed::Suspend();
               ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
               ImGui::SetTooltip(T("Follow leader -> %s: %s"), pInfo.nodeTitle.c_str(), pInfo.paramName.c_str());
               ed::Resume();
               if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && pInfo.nodeIndex >= 0 &&
                   pInfo.nodeIndex < (int)gNodes.size())
               {
                  PushUndoCheckpoint();
                  pickingDrift->leaderNodeIndex = (int)gNodes[pInfo.nodeIndex].uid;
                  pickingDrift->leaderParamIndex = pInfo.paramIndex;
                  gDriftFollowPickingUid = 0;
               }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
               gDriftFollowPickingUid = 0;
         }
      }}
}
