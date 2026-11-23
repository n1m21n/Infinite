// Split out of main(): see docs/plans/main-split/README.md (Block C)
#include "app/frame/CanvasLink.h"
#include <set>
#include "app/ui/design/components/MenuParts.h"
#include "app/ui/design/components/ChipButton.h"
#include "app/ui/design/components/FieldWell.h"
#include "app/ui/design/components/StateRing.h"
#include "imgui_internal.h"
#include "app/ui/design/components/LibraryParts.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/UiType.h"
#include "app/frame/FrameCtx.h"
#include "app/panels/CurveEditorPanel.h"

namespace app
{
// Spawns the node an audio drop was aimed at, loads the file(s) into it and, when it is one node, makes
// it audible (HearNode: Audio Out + engine on). Several files into several nodes stay unwired: one Audio
// Out takes one cable. Returns every node it added, so the caller can take them back.
std::vector<int> SpawnAudioDropNodes(const std::string& name, const std::string& category,
                                     const std::vector<std::string>& paths, const ImVec2& canvasPos)
{
   std::vector<int> added;
   if (paths.empty())
      return added;
   if (name == "MPC" || name == "Drum Sequencer")
   {
      GraphNode* spawned = SpawnNode(name, category, canvasPos.x, canvasPos.y);
      if (spawned == nullptr)
         return added;
      if (auto* mpc = dynamic_cast<MpcNode*>(spawned->node.get()))
         for (int i = 0; i < (int)paths.size() && i < MpcNode::kPads; ++i)
            mpc->LoadPad(i, paths[i]);
      else if (auto* drum = dynamic_cast<DrumSequencerNode*>(spawned->node.get()))
         for (int i = 0; i < (int)paths.size() && i < DrumSequencerNode::kNumLanes; ++i)
            drum->LoadFileToLane(i, paths[i]);
      spawned->showParams = true;
      added.push_back(spawned->index);
   }
   else
   {
      float offset = 0.0f;
      for (const std::string& path : paths)
      {
         GraphNode* spawned = SpawnNode(name, category, canvasPos.x + offset, canvasPos.y);
         offset += 240.0f;
         if (spawned == nullptr)
            continue;
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
         added.push_back(spawned->index);
      }
   }
   gPatchDirty = true;
   RebuildAudioTopology();
   if (added.size() == 1)
   {
      const int out = HearNode(added[0]);
      if (out >= 0)
         added.push_back(out);
   }
   return added;
}

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
            ImGui::PushFont(UiType::Font(UiType::Weight::Regular), UiType::Px(UiType::Size::Body) * commentZoom * fontScale);

            // Filling the whole popup cleanly at 1:1 scale with the canvas card
            FieldWell::InputTextMultiline("##commenttext", &c->text, ImVec2(gCommentEditRect.z, gCommentEditRect.w));

            ImGui::PopFont();
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
         LibraryParts::SearchField("nodepicker", T("Search nodes"), searchBuf, sizeof(searchBuf));
         MenuParts::Separator();

         std::string q(searchBuf);
         std::transform(q.begin(), q.end(), q.begin(), ::tolower);

         const bool pickFirst = ImGui::IsKeyPressed(ImGuiKey_Enter, false);
         std::string spawnName, spawnCategory;
         int shown = 0;

         // #53: dim incompatible node types on cable drop (except modulator cables),
         // keeping every item clickable. Fold gLinkDragSuggestions into a set once per frame.
         bool isModulatorCable = false;
         if (gLinkDragSourcePin >= 0)
         {
            if (GraphNode::IsParamPin(gLinkDragSourcePin))
               isModulatorCable = true;
            else if (GraphNode::IsOutputPin(gLinkDragSourcePin))
            {
               if (GraphNode* dragSrc = FindNodeByIndex(GraphNode::NodeIndexFromPin(gLinkDragSourcePin)))
               {
                  const int outSlot = GraphNode::OutputIndexFromPin(gLinkDragSourcePin);
                  isModulatorCable = (dynamic_cast<IModulator*>(dragSrc->node.get()) != nullptr ||
                                      ModulatorForOutput(dragSrc->node.get(), outSlot) != nullptr);
               }
            }
         }
         const std::set<std::pair<std::string, std::string>> suggestedSet(
            gLinkDragSuggestions.begin(), gLinkDragSuggestions.end());
         const bool dimByPort = !isModulatorCable && !suggestedSet.empty();
         auto drawMenuItem = [&](const char* label, const char* shortcut, bool dimmed) -> bool
         {
            if (dimmed)
               ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            const bool picked = MenuParts::Item(label, shortcut);
            if (dimmed)
               ImGui::PopStyleColor();
            return picked;
         };

         if (q.empty())
         {
            // Link-drag search: show a compact top-N Suggested section so the popup
            // keeps the same height and category-first layout as the normal node picker.
            if (!gLinkDragSuggestions.empty())
            {
               constexpr size_t kMaxInlineSuggestions = 8;
               const size_t nShow = std::min(gLinkDragSuggestions.size(), kMaxInlineSuggestions);
               ImGui::TextDisabled("%s", T("Suggested"));
               for (size_t i = 0; i < nShow; ++i)
               {
                  const auto& t = gLinkDragSuggestions[i];
                  ++shown;
                  const std::string title = DisplayName(t.first);
                  const std::string category = DisplayName(t.second);
                  ImGui::PushID(t.first.c_str());
                  ImGui::PushID(t.second.c_str());
                  const bool picked = MenuParts::Item(title.c_str(), category.c_str());
                  ImGui::PopID();
                  ImGui::PopID();
                  if (picked)
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
               std::vector<std::string> catNodes;
               for (const std::string& name : NodeFactory::Instance().GetNodesInCategory(category))
                  if (IsUserSpawnable(name))
                     catNodes.push_back(name);
               bool anyCompatible = !dimByPort;
               if (dimByPort)
               {
                  std::stable_partition(catNodes.begin(), catNodes.end(), [&](const std::string& name) {
                     const bool ok = suggestedSet.find({ name, category }) != suggestedSet.end();
                     if (ok)
                        anyCompatible = true;
                     return ok;
                  });
               }
               ImGui::SetNextWindowSizeConstraints(ImVec2(180, 0), ImVec2(320, 440));
               if (!anyCompatible)
                  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
               const bool openCat = MenuParts::SubMenu(DisplayName(category).c_str());
               if (!anyCompatible)
                  ImGui::PopStyleColor();
               if (openCat)
               {
                  for (const std::string& name : catNodes)
                  {
                     ++shown;
                     const bool dimmed = dimByPort && suggestedSet.find({ name, category }) == suggestedSet.end();
                     if (drawMenuItem(DisplayName(name).c_str(), nullptr, dimmed))
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
            std::vector<std::pair<std::string, std::string>> matches;
            for (const auto& t : allTypes)
            {
               std::string hay = DisplayName(t.first) + " " + DisplayName(t.second);
               if (I18n::CurrentLanguage() != "en")
                  hay += " " + NodeSearchHaystack(t.first, t.second);
               if (NodeSearchMatches(hay, q))
                  matches.push_back(t);
            }
            if (dimByPort)
               std::stable_partition(matches.begin(), matches.end(), [&](const auto& t) {
                  return suggestedSet.find(t) != suggestedSet.end();
               });

            for (const auto& t : matches)
            {
               ++shown;
               const std::string title = DisplayName(t.first);
               const std::string category = DisplayName(t.second);
               const bool dimmed = dimByPort && suggestedSet.find(t) == suggestedSet.end();
               // Two node types can share a display name ("Invert" is both a Modulator and a Compositing effect); the
               // row id comes from the label, so the type name keeps each row's id unique.
               ImGui::PushID(t.first.c_str());
               ImGui::PushID(t.second.c_str());
               bool activate = drawMenuItem(title.c_str(), category.c_str(), dimmed);
               ImGui::PopID();
               ImGui::PopID();
               if (shown == 1 && pickFirst)
                  activate = true;
               if (activate)
               {
                  spawnName = t.first;
                  spawnCategory = t.second;
               }
            }
            if (shown == 0)
               ImGui::TextDisabled("%s", T("No matches"));
         }

         if (!spawnName.empty())
         {
            GraphNode* spawned = SpawnNode(spawnName, spawnCategory, gSpawnPos.x, gSpawnPos.y);
            if (spawned != nullptr && gLinkDragSourcePin >= 0)
               spawned->noGroupAdoptUntilMoved = true;
            if (gLinkDragSourcePin >= 0 && spawned != nullptr && !GraphNode::IsOutputPin(gLinkDragSourcePin))
               WireSpawnedNodeToPin(*spawned, gLinkDragSourcePin);
            else if (gLinkDragSourcePin >= 0 && spawned != nullptr)
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
                  const bool srcIsShade = IsShadeSource(dragSrcNode->node.get());

                  const int slotCount = InputCountFor(*spawned);
                  for (int slot = 0; slot < slotCount; ++slot)
                  {
                     if (IsInputSlotCompatible(spawned, slot, srcIsModulator, srcPalette, srcGeometry,
                                                srcCamera, srcLight, srcIsEnvironment,
                                                srcIsAudioNode, srcIsNoteSource, srcIsPredictor, srcIsShade))
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
                                              160.0f, 280.0f);
      ImGui::SetNextWindowSizeConstraints(ImVec2(dropdownMinWidth, 0), ImVec2(280.0f, ImMin(480.0f, ImGui::GetMainViewport()->Size.y - 40.0f)));
      const bool ddOpen = MenuParts::BeginPopup("##dropdown");
      if (ddOpen)
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
            // The focused search field takes the first Esc for itself; the list should close on it.
            if (ImGui::IsKeyPressed(ImGuiKey_Escape))
               ImGui::CloseCurrentPopup();
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

      DrawSharePatchPopup();

      // Field build step 17: .infdev device Save name-prompt - same
      // deferred-popup discipline as "##dropdown" just above (opened from
      // inside a node body, rendered out here where the canvas transform
      // doesn't apply).
      if (gFieldDeviceSave.justOpened)
      {
         ImGui::OpenPopup("##fielddevicesave");
         gFieldDeviceSave.justOpened = false;
      }
      if (MenuParts::BeginPopup("##fielddevicesave"))
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
         MenuParts::EndPopup();
      }

      if (gAudioDropPicker.justOpened)
      {
         ImGui::OpenPopup("##audiodroppicker");
         gAudioDropPicker.justOpened = false;
         const ImVec2 popupPos = (gAudioDropPicker.screenPos.x != 0.0f || gAudioDropPicker.screenPos.y != 0.0f)
            ? gAudioDropPicker.screenPos
            : ed::CanvasToScreen(gAudioDropPicker.canvasPos);
         // With the file already on the canvas at the drop point, the list opens to its left, not over it.
         if (!gAudioDropPicker.autoNodes.empty())
            ImGui::SetNextWindowPos(ImVec2(popupPos.x - 12.0f, popupPos.y), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
         else
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
               ImGui::TextDisabled(gAudioDropPicker.autoNodes.empty() ? T("Load %s into:") : T("Playing %s. Load into instead:"), filename.c_str());
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
                  // The dropped file is already playing through an Audio File; a pick replaces that.
                  for (int idx : gAudioDropPicker.autoNodes)
                     RemoveNodeByIndex(idx);
                  gAudioDropPicker.autoNodes.clear();
                  SpawnAudioDropNodes(opt.name, opt.category, gAudioDropPicker.paths, gAudioDropPicker.canvasPos);
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
         {
            gAudioDropPicker.paths.clear();
            gAudioDropPicker.autoNodes.clear(); // dismissed: the Audio File stays
         }
      }

      // ---- trash button on a hovered cable ----
      // Sits at the middle of the cable while it is hovered and stays while the cursor is on the cable or the
      // button; a click disconnects that one cable (same path as selecting it and pressing Delete).
      {
         static int sTrashLink = 0;
         static double sTrashSeen = -1.0;
         static int sTrashGone = 0; // a cable just deleted: ignore it until the cursor leaves it
         const double now = ImGui::GetTime();
         const ed::LinkId hovered = ed::GetHoveredLink();
         if (!hovered || (int)hovered.Get() != sTrashGone)
            sTrashGone = 0;
         if (hovered && !gShowLock && (int)hovered.Get() != sTrashGone && FindLink((int)hovered.Get()) != nullptr)
         {
            sTrashLink = (int)hovered.Get();
            sTrashSeen = now;
         }
         ImVec2 canvasMid;
         if (sTrashLink != 0 && FindLink(sTrashLink) != nullptr && now - sTrashSeen < tok::motion_s_trash_grace && ed::GetLinkMidpoint(ed::LinkId(sTrashLink), &canvasMid))
         {
            ImVec2 c = ed::CanvasToScreen(canvasMid);
            // Drawn on the foreground list, so it must be hidden by hand wherever something else is in front of the
            // cable's midpoint: a floating window / popup over it, or a node that covers it.
            // Docked panels (viewport, timeline) sit outside the graph rect: a midpoint under one is hidden too.
            bool covered = c.x < gGraphScreenTL.x || c.y < gGraphScreenTL.y || c.x > gGraphScreenTL.x + gGraphScreenSize.x ||
                           c.y > gGraphScreenTL.y + gGraphScreenSize.y;
            {
               ImGuiContext& g = *ImGui::GetCurrentContext();
               const ImGuiWindow* canvasRoot = ImGui::GetCurrentWindow()->RootWindow;
               bool above = false;
               for (ImGuiWindow* w : g.Windows)
               {
                  if (w->RootWindow == canvasRoot)
                  {
                     above = true;
                     continue;
                  }
                  if (above && w->WasActive && !w->Hidden && (w->Flags & (ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_Tooltip)) == 0 &&
                      w->Rect().Contains(c))
                  {
                     covered = true;
                     break;
                  }
               }
               for (const GraphNode& gn : gNodes)
               {
                  if (covered)
                     break;
                  const ImVec2 np = ed::CanvasToScreen(ed::GetNodePosition(gn.NodeId()));
                  const ImVec2 ns = ed::CanvasToScreen(ed::GetNodePosition(gn.NodeId()) + ed::GetNodeSize(gn.NodeId()));
                  if (c.x >= np.x && c.x <= ns.x && c.y >= np.y && c.y <= ns.y)
                     covered = true;
               }
            }
            if (covered)
            {
               sTrashLink = 0;
               goto trashDone;
            }
            // Scales with the canvas zoom so it stays proportionate to the cable when zoomed out.
            const float zoom = ed::CanvasToScreen(ImVec2(1.0f, 0.0f)).x - ed::CanvasToScreen(ImVec2(0.0f, 0.0f)).x;
            const float k = std::min(1.0f, std::max(0.4f, zoom));
            const float r = 11.0f * k;
            const ImVec2 m = ImGui::GetMousePos();
            const bool over = (m.x - c.x) * (m.x - c.x) + (m.y - c.y) * (m.y - c.y) <= (r + 2.0f) * (r + 2.0f);
            // Stays while the cursor is on the button (or just beside it); gone ~0.1 s after the cursor leaves both.
            if ((m.x - c.x) * (m.x - c.x) + (m.y - c.y) * (m.y - c.y) <= 20.0f * 20.0f)
               sTrashSeen = now;
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            const ImVec4 tx = ImGui::GetStyleColorVec4(ImGuiCol_Text);
            const ImU32 fg = ImGui::GetColorU32(ImVec4(tx.x, tx.y, tx.z, 0.95f));
            dl->AddCircleFilled(c, r, ImGui::GetColorU32(over ? ImGuiCol_ButtonHovered : ImGuiCol_PopupBg), 24);
            dl->AddCircle(c, r, ImGui::GetColorU32(ImGuiCol_Border), 24, 1.0f);
            dl->AddLine(ImVec2(c.x - 5.0f * k, c.y - 3.5f * k), ImVec2(c.x + 5.0f * k, c.y - 3.5f * k), fg, std::max(1.0f, 1.5f * k)); // lid
            dl->AddLine(ImVec2(c.x - 1.6f * k, c.y - 5.5f * k), ImVec2(c.x + 1.6f * k, c.y - 5.5f * k), fg, std::max(1.0f, 1.5f * k)); // handle
            dl->AddLine(ImVec2(c.x - 3.6f * k, c.y - 3.5f * k), ImVec2(c.x - 3.0f * k, c.y + 5.0f * k), fg, std::max(1.0f, 1.5f * k)); // body
            dl->AddLine(ImVec2(c.x + 3.6f * k, c.y - 3.5f * k), ImVec2(c.x + 3.0f * k, c.y + 5.0f * k), fg, std::max(1.0f, 1.5f * k));
            dl->AddLine(ImVec2(c.x - 3.0f * k, c.y + 5.0f * k), ImVec2(c.x + 3.0f * k, c.y + 5.0f * k), fg, std::max(1.0f, 1.5f * k));
            if (over && ImGui::IsMouseClicked(0))
            {
               PushUndoCheckpoint();
               DisconnectLinkById(sTrashLink);
               sTrashGone = sTrashLink;
               sTrashLink = 0;
               sTrashSeen = -1.0;
            }
            if (over)
               ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
         }
         else
            sTrashLink = 0;
      trashDone:;
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
            ed::NavigateToSelection(gRequestFitViewNodeZoom, 0.0f);
            ed::ClearSelection();
         }
         gRequestFitViewNodeIndex = -1;
         gRequestFitViewNodeZoom = false;
      }
      // A node asked to be the selection (New Synth selects its Keyboard). The node has to have been
      // drawn once before the editor knows it, so the request waits a frame if it is not there yet.
      if (gRequestSelectNodeIndex >= 0)
      {
         GraphNode* sel = FindNodeByIndex(gRequestSelectNodeIndex);
         if (sel == nullptr)
            gRequestSelectNodeIndex = -1;
         else if (ed::GetNodeSize(sel->NodeId()).x > 0.0f)
         {
            ed::SelectNode(sel->NodeId(), false);
            gRequestSelectNodeIndex = -1;
         }
      }
      // A page spawned nodes (New Drum Page, Add Piano Roll Page...): once drawn, select them and glide the canvas there,
      // so the new node is never lost off-screen. Gives up after a second if a node never lays out.
      if (!gRequestRevealUids.empty())
      {
         static int sRevealWait = 0;
         bool ready = true;
         for (uint64_t uid : gRequestRevealUids)
            if (GraphNode* n = FindNodeByUid(uid))
               ready = ready && ed::GetNodeSize(n->NodeId()).x > 0.0f;
         if (ready || ++sRevealWait > 60)
         {
            ed::ClearSelection();
            for (uint64_t uid : gRequestRevealUids)
               if (GraphNode* n = FindNodeByUid(uid))
                  ed::SelectNode(n->NodeId(), true);
            if (ed::GetSelectedObjectCount() > 0)
               ed::NavigateToSelection(false, 0.25f);
            gRequestRevealUids.clear();
            sRevealWait = 0;
         }
      }
      if (getenv("INFINITE_PALETTETEST") != nullptr && frameId == 3)
         gRequestFitView = true; // dev screenshot: frame the whole fixture
      if ((getenv("INFINITE_AUDIOUITEST") != nullptr || getenv("INFINITE_FXGALLERY") != nullptr || getenv("INFINITE_NODEGALLERY") != nullptr) && frameId == 3)
         gRequestFitView = true; // same, for the audio node UI fixture
      if ((getenv("INFINITE_NODEGALLERY") != nullptr || getenv("INFINITE_LOADPATCH") != nullptr) && frameId == 12)
      {
         // Review aid: INFINITE_GALLERYZOOM=<z> zooms the canvas, INFINITE_GALLERYOPENDD=<param index> opens that
         // dropdown on the first node, so a popup can be judged at any zoom (screenshot at a later frame).
         if (const char* z = getenv("INFINITE_GALLERYZOOM"))
         {
            gKbSavedZoom = (float)atof(z);
            gKbSavedScroll = ImVec2(-60.0f, -40.0f);
            gKbViewRestore = true; // applied inside the editor scope next frame
         }
      }
      if (getenv("INFINITE_NODEGALLERY") != nullptr && frameId == 24)
      {
         if (const char* d = getenv("INFINITE_GALLERYOPENDD"))
         {
            gDropdownTestOpenKey = std::pair<int, int>(0, atoi(d));
            if (atoi(d) < 0) // synthetic list, opened as a node control would at the canvas zoom
            {
               gDropdown.options.clear();
               for (int i = -12; i <= 12; ++i)
                  gDropdown.options.push_back("semi " + std::string(i > 0 ? "+" : "") + std::to_string(i));
               gDropdown.categories.clear();
               gDropdown.onSelect = [](int) {};
               gDropdown.current = 12;
               gDropdown.justOpened = true;
               gDropdown.focusSearch = false;
               gDropdown.filterBuf[0] = '\0';
            }
         }
      }
      if (const char* live = getenv("INFINITE_LIVESTATE"); live != nullptr && frameId == 6)
      {
         // Review aid: INFINITE_LIVESTATE=<dir with sample.wav and live.mid> loads that sample into every Sampler,
         // Slicer and Granular and the MIDI into every MIDI File, then starts the transport so a shot shows
         // waveforms, playheads and visualisers with real data.
         const std::string dir = live;
         for (GraphNode& gn : gNodes)
         {
            if (auto* n = dynamic_cast<SamplerNode*>(gn.node.get()))
               n->LoadFile(dir + "/sample.wav");
            else if (auto* n2 = dynamic_cast<SlicerNode*>(gn.node.get()))
               n2->LoadFile(dir + "/sample.wav");
            else if (auto* n3 = dynamic_cast<GranularNode*>(gn.node.get()))
               n3->LoadFile(dir + "/sample.wav");
            else if (auto* n4 = dynamic_cast<MidiFileNode*>(gn.node.get()))
               n4->LoadFile(dir + "/live.mid");
         }
         Transport::Instance().SetPlaying(true);
      }
      if (getenv("INFINITE_NODELIST") != nullptr && frameId == 1)
      {
         // Review aid: every registered type by category, one line each ("NODELIST <category>|<type>").
         for (const std::string& cat : NodeFactory::Instance().GetCategories())
            for (const std::string& name : NodeFactory::Instance().GetNodesInCategory(cat))
               printf("NODELIST %s|%s\n", cat.c_str(), name.c_str());
         fflush(stdout);
      }
      if (getenv("INFINITE_GALLERYSIZES") != nullptr && frameId == 30)
      {
         // Review aid for the gallery: canvas size of every node, to compare params closed vs open.
         for (const GraphNode& gn : gNodes)
         {
            const ImVec2 s = ed::GetNodeSize(gn.NodeId());
            printf("NODESIZE %s|%.0f|%.0f\n", gn.typeName.c_str(), s.x, s.y);
         }
         fflush(stdout);
      }
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

      // Knob picks from panels (Perf element, Curve assign / record, Drift follow) all go through Canvas Link: one
      // hit-test, one ring, one banner naming the knob under the pointer, Esc / right-click cancels.
      {
         const bool perfOn = gPerfAssigningElemIdx >= 0 && gPerfAssigningElemIdx < (int)gPerfElements.size();
         CanvasLink::Sync("perf.assign", perfOn ? "perf." + std::to_string(gPerfAssigningElemIdx) + "." + std::to_string(gPerfAssigningAxis) : "",
            [] {
               CanvasLink::Spec sp;
               sp.target = CanvasLink::Target::Param;
               sp.kind = T("knob");
               const int elemIdx = gPerfAssigningElemIdx, axis = gPerfAssigningAxis;
               const std::string key = "perf." + std::to_string(elemIdx) + "." + std::to_string(axis);
               sp.onPickedParam = [elemIdx, axis](int n, int pi) {
                  if (elemIdx < 0 || elemIdx >= (int)gPerfElements.size())
                     return;
                  const ParamPinScreenInfo* info = nullptr;
                  for (const ParamPinScreenInfo& q : gParamPinScreenList)
                     if (q.nodeIndex == n && q.paramIndex == pi)
                        info = &q;
                  PushUndoCheckpoint();
                  auto& el = gPerfElements[elemIdx];
                  if (axis == 0)
                  {
                     el.dstIndex = n;
                     el.dstParam = pi;
                     el.targets.clear();
                     el.targets.push_back({ n, pi, "" });
                     static const char* kDefaults[] = { "Knob", "Fader", "Slider", "Toggle", "XY Pad", "Trigger", "NumBox", "Selector", "Pan/Detent", "Step Gate" };
                     bool generic = el.label.empty();
                     for (const char* d : kDefaults)
                        generic = generic || el.label == d;
                     if (generic && info != nullptr)
                        el.label = info->paramName;
                  }
                  else if (axis == 1)
                  {
                     el.dstParam2 = pi;
                     el.targetsY.clear();
                     el.targetsY.push_back({ n, pi, "" });
                  }
                  gPerfAssigningElemIdx = -1;
               };
               sp.onCancel = [key] {
                  if ("perf." + std::to_string(gPerfAssigningElemIdx) + "." + std::to_string(gPerfAssigningAxis) == key)
                     gPerfAssigningElemIdx = -1;
               };
               return sp;
            });
      }

      // Curve Editor Assign (docs/plans/curve-editor/README.md §8.6): binds the armed MSEG to the knob clicked. Shift
      // keeps it armed for more. A curve page dragged onto a knob does the same on release (a drag, not a pick).
      {
         auto curveAccepts = [](uint64_t uid) {
            return [uid](int n, int) {
               GraphNode* src = FindNodeByUid(uid);
               return src != nullptr && n != src->index; // a curve never drives its own controls
            };
         };
         auto bindCurve = [](uint64_t uid, int n, int pi) {
            GraphNode* src = FindNodeByUid(uid);
            if (src == nullptr)
               return;
            PushUndoCheckpoint("Assign Curve");
            Modulation::Instance().Bind(n, pi, src->index, 0);
            gPatchDirty = true;
         };
         if (gCurveAssignUid != 0)
         {
            GraphNode* srcGn = FindNodeByUid(gCurveAssignUid);
            if (srcGn == nullptr || dynamic_cast<MSEGNode*>(srcGn->node.get()) == nullptr)
               gCurveAssignUid = 0;
         }
         CanvasLink::Sync("curve.assign", gCurveAssignUid != 0 ? "curve." + std::to_string(gCurveAssignUid) : "", [&] {
            CanvasLink::Spec sp;
            sp.target = CanvasLink::Target::Param;
            sp.kind = T("knob");
            const uint64_t uid = gCurveAssignUid;
            sp.acceptsParam = curveAccepts(uid);
            sp.onPickedParam = [uid, bindCurve](int n, int pi) {
               bindCurve(uid, n, pi);
               if (!ImGui::GetIO().KeyShift && gCurveAssignUid == uid)
                  gCurveAssignUid = 0;
            };
            sp.onCancel = [uid] {
               if (gCurveAssignUid == uid)
                  gCurveAssignUid = 0;
            };
            return sp;
         });
         const ImGuiPayload* curvePayload = ImGui::GetDragDropPayload();
         if (curvePayload != nullptr && curvePayload->IsDataType("INFINITE_CURVE_PAGE") && curvePayload->DataSize == sizeof(uint64_t))
         {
            const uint64_t uid = *static_cast<const uint64_t*>(curvePayload->Data);
            const int hit = CanvasLink::HoveredParam(curveAccepts(uid));
            if (hit >= 0)
            {
               CanvasLink::RingParam(hit);
               if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                  bindCurve(uid, gParamPinScreenList[hit].nodeIndex, gParamPinScreenList[hit].paramIndex);
            }
         }
      }

      // Curve Editor Record, Any knob (§8.9): the knob clicked becomes the take's source.
      CanvasLink::Sync("curve.record", gCurveRecordPicking ? "curve.record" : "", [] {
         CanvasLink::Spec sp;
         sp.target = CanvasLink::Target::Param;
         sp.kind = T("knob");
         sp.onPickedParam = [](int n, int pi) { CurveEditorRecordPicked(n, pi); };
         sp.onCancel = [] { gCurveRecordPicking = false; };
         return sp;
      });

      // Predictive Drift Follow leader: stores the leader's *uid*, not its index - an index is reused by
      // RemoveNodeByIndex and would repoint Follow at another node after an unrelated delete/undo (ParamRef::uid).
      {
         if (gDriftFollowPickingUid != 0)
         {
            GraphNode* pickingGn = FindNodeByUid(gDriftFollowPickingUid);
            if (pickingGn == nullptr || dynamic_cast<DriftNode*>(pickingGn->node.get()) == nullptr)
               gDriftFollowPickingUid = 0; // the node was deleted while picking
         }
         CanvasLink::Sync("drift.follow", gDriftFollowPickingUid != 0 ? "drift." + std::to_string(gDriftFollowPickingUid) : "", [] {
            CanvasLink::Spec sp;
            sp.target = CanvasLink::Target::Param;
            sp.kind = T("knob");
            const uint64_t self = gDriftFollowPickingUid;
            // Can't follow yourself: a self-target would drive the slot it reads, a loop with no way out.
            sp.acceptsParam = [self](int n, int) {
               GraphNode* g = FindNodeByIndex(n);
               return g != nullptr && g->uid != self;
            };
            sp.onPickedParam = [self](int n, int pi) {
               GraphNode* me = FindNodeByUid(self);
               DriftNode* drift = me != nullptr ? dynamic_cast<DriftNode*>(me->node.get()) : nullptr;
               GraphNode* leader = FindNodeByIndex(n);
               if (drift != nullptr && leader != nullptr)
               {
                  PushUndoCheckpoint();
                  drift->leaderNodeIndex = (int)leader->uid;
                  drift->leaderParamIndex = pi;
               }
               if (gDriftFollowPickingUid == self)
                  gDriftFollowPickingUid = 0;
            };
            sp.onCancel = [self] {
               if (gDriftFollowPickingUid == self)
                  gDriftFollowPickingUid = 0;
            };
            return sp;
         });
      }
      }
}
