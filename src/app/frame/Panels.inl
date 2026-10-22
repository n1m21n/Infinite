// Part of src/main.cpp, included there in order (one translation unit).
// node browser panel and windows outside the canvas

      // ---- node browser panel ----
      // The same catalogue as the canvas popup, but persistent: it can be left
      // open while building a patch, which the popup cannot.
      //
      // Drawn at an explicit width AND height (rather than the (0,0) "fill
      // remaining" it used before the viewport panel existed). Width, because
      // with the right-docked viewport panel also open, "remaining" would
      // include its space too - see the combined rightReserved calc above
      // ed::Begin(). Height, because "remaining" runs to the bottom of the
      // window, which swallows the row a bottom-docked viewport panel is
      // about to be drawn into and leaves that panel clipped out of sight.
      if (gNodePanelOpen)
      {
         ImGui::SameLine();
         ImGui::BeginChild("##nodepanel", ImVec2(gNodePanelWidth, graphHeight), true);

         const float nodeGripHeight = ImGui::GetContentRegionAvail().y;
         ImGui::InvisibleButton("##nodepanel_resize", ImVec2(6.0f, nodeGripHeight));
         if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
         if (ImGui::IsItemActive())
            gNodePanelWidth = std::max(kNodePanelMinWidth,
                                       std::min(720.0f, gNodePanelWidth - ImGui::GetIO().MouseDelta.x));
         ImGui::SameLine(0.0f, 3.0f);
         ImGui::BeginChild("##nodepanel_content", ImVec2(0, 0), false);

         // Mode switcher: Modules is the original, always-present catalogue;
         // Samples and Media extend it per docs/plans/audio/README.md P3e.
         // Kept as plain selectable-style buttons rather than an ImGui tab
         // bar so the active mode reads clearly against the panel's own
         // dark background.
         // Sized from the actual content region rather than a fraction of
         // kNodePanelWidth: four tabs no longer fit at 0.30 each, and deriving
         // the width means the row stays exact if a fifth mode ever lands or
         // the panel width changes.
         const float tabGap = ImGui::GetStyle().ItemSpacing.x;
         const float tabW = std::max(40.0f, (ImGui::GetContentRegionAvail().x - tabGap * 3.0f) * 0.25f);
         if (ImGui::Selectable("MODULES", gSearchPanelMode == 0, 0, ImVec2(tabW, 0)))
            gSearchPanelMode = 0;
         ImGui::SameLine();
         if (ImGui::Selectable("SAMPLES", gSearchPanelMode == 1, 0, ImVec2(tabW, 0)))
            gSearchPanelMode = 1;
         ImGui::SameLine();
         if (ImGui::Selectable("MEDIA", gSearchPanelMode == 2, 0, ImVec2(tabW, 0)))
            gSearchPanelMode = 2;
         ImGui::SameLine();
         if (ImGui::Selectable("PLUGINS", gSearchPanelMode == 3, 0, ImVec2(tabW, 0)))
            gSearchPanelMode = 3;
         ImGui::Separator();

         if (gSearchPanelMode == 0)
         {
            static char panelSearch[128] = "";
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##panelsearch", "SEARCH MODULES...", panelSearch, sizeof(panelSearch));

            std::string q = panelSearch;
            std::transform(q.begin(), q.end(), q.begin(), ::tolower);

            std::string spawnName, spawnCategory;
            ImGui::Separator();
            ImGui::BeginChild("##nodepanellist", ImVec2(0, 0), false);
            // A module row: favourite star, then the spawn Selectable, with a
            // right-click menu for the star as well.
            auto moduleRow = [&](const std::string& name, const std::string& category, const std::string& label) {
               ImGui::PushID(name.c_str());
               const float starSz = ImGui::GetFrameHeight() * 0.7f;
               const float rowY = ImGui::GetCursorPosY();
               ImGui::SetCursorPosY(rowY + (ImGui::GetTextLineHeight() - starSz) * 0.5f);
               if (DrawFavoriteStar("##fav", gBrowserFavorites.Is("modules", name), starSz))
                  gBrowserFavorites.Toggle("modules", name);
               ImGui::SameLine();
               ImGui::SetCursorPosY(rowY);
               if (ImGui::Selectable(label.c_str()))
               {
                  spawnName = name;
                  spawnCategory = category;
               }
               if (ImGui::BeginPopupContextItem("##modulerowmenu"))
               {
                  const bool fav = gBrowserFavorites.Is("modules", name);
                  if (ImGui::MenuItem(fav ? "Remove from favourites" : "Add to favourites"))
                     gBrowserFavorites.Toggle("modules", name);
                  ImGui::EndPopup();
               }
               ImGui::PopID();
            };
            gBrowserFavorites.EnsureLoaded();
            if (q.empty() && !gBrowserFavorites.modules.empty())
            {
               std::vector<std::string> favs;
               for (const std::string& name : gBrowserFavorites.modules)
                  if (!NodeFactory::Instance().CategoryOf(name).empty() && IsUserSpawnable(name))
                     favs.push_back(name);
               if (!favs.empty())
               {
                  ImGui::PushID("##favouritesheader");
                  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.80f, 0.25f, 1.0f));
                  const bool open = ImGui::CollapsingHeader("FAVOURITES", ImGuiTreeNodeFlags_DefaultOpen);
                  ImGui::PopStyleColor();
                  if (open)
                  {
                     ImGui::Indent();
                     for (const std::string& name : favs)
                     {
                        const std::string category = NodeFactory::Instance().CategoryOf(name);
                        moduleRow(name, category, NodeBrowserLabel(name, "SIDEBAR_NODE", category, name));
                     }
                     ImGui::Unindent();
                  }
                  ImGui::PopID();
               }
            }
            for (const std::string& category : NodeFactory::Instance().GetCategories())
            {
               // With a query the categories are only drawn when something in them
               // matches, so an empty heading never sits there on its own.
               std::vector<std::string> matches;
               for (const std::string& name : NodeFactory::Instance().GetNodesInCategory(category))
               {
                  if (!IsUserSpawnable(name))
                     continue;
                  if (q.empty())
                  {
                     matches.push_back(name);
                     continue;
                  }
                  std::string hay = name + " " + category;
                  std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
                  if (hay.find(q) != std::string::npos)
                     matches.push_back(name);
               }
               if (matches.empty())
                  continue;

               ImGui::PushID(category.c_str());
               const auto& cc = CategoryColors::ColorFor(category);
               // Turbo 0.61: a heading where the colour family changes.
               {
                  static int sLastFamily = -1;
                  if (category == NodeFactory::Instance().GetCategories().front())
                     sLastFamily = -1;
                  const int family = CategoryColors::FamilyRank(category);
                  if (q.empty() && family != sLastFamily)
                  {
                     ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(cc.r, cc.g, cc.b, 0.75f));
                     ImGui::SeparatorText(CategoryColors::FamilyName(family));
                     ImGui::PopStyleColor();
                  }
                  sLastFamily = family;
               }
               ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(cc.r, cc.g, cc.b, 1.0f));
               const std::string categoryLabel =
                  NodeBrowserLabel(category, "SIDEBAR_CATEGORY", category);
               const bool showCategory = !q.empty() || ImGui::CollapsingHeader(categoryLabel.c_str());
               ImGui::PopStyleColor();
               if (showCategory)
               {
                  if (q.empty())
                     ImGui::Indent();
                  else
                     ImGui::SeparatorText(DisplayName(category).c_str());
                  for (const std::string& name : matches)
                     moduleRow(name, category, NodeBrowserLabel(name, "SIDEBAR_NODE", category, name));
                  if (q.empty())
                     ImGui::Unindent();
               }
               ImGui::PopID();
            }
            ImGui::EndChild();

            if (!spawnName.empty())
            {
               gPendingSidebarSpawns.push_back({spawnName, spawnCategory});
            }
         }
         else if (gSearchPanelMode == 1)
         {
            DrawLibrarySearchPanel(gSampleScanner, "##samples", "SEARCH SAMPLES...", false);
         }
         else if (gSearchPanelMode == 2)
         {
            DrawLibrarySearchPanel(gMediaScanner, "##media", "SEARCH MEDIA...", true);
         }
         else
         {
            DrawPluginSearchPanel();
         }

         ImGui::EndChild();
         ImGui::EndChild();
      }

      // Right-docked viewport panel, chained via SameLine right after the
      // node browser panel above (whether or not that one is open) so the
      // two sit side by side instead of overlapping.
      if (viewportRight)
      {
         ImGui::SameLine();
         DrawViewportPanelDocked("##viewportpanel_right", ImVec2(gViewportPanelWidth, graphHeight));
      }

      // Bottom-docked viewport panel: a fresh, full-width row below the
      // canvas (and below the row above, if that one drew anything) rather
      // than same-line - see the graphHeight calc above ed::Begin(), which
      // already reserved this space.
      if (perfRight)
      {
         ImGui::SameLine();
         DrawPerfPanelDocked("##perfpanel_right", ImVec2(gPerfPanelWidth, graphHeight));
      }
      if (viewportBottom)
         DrawViewportPanelDocked("##viewportpanel_bottom", ImVec2(0, gViewportPanelHeight));
      if (perfBottom)
         DrawPerfPanelDocked("##perfpanel_bottom", ImVec2(0, gPerfPanelHeight));

      // Turbo 0.43: Arrangement Timeline, docked at the bottom.
      if (gArrangePanelOpen)
         DrawArrangePanelDocked(ImVec2(0, gArrangePanelHeight));
      else
         gArrangeKeysOwned = false;

      ImGui::End();
      DrawArrangeClipSettingsWindow();
      DrawArrangeRenderWindow();

      // ---- windows that must live outside the node canvas ----
      if (gFormulaEditorOpen && gFormulaEditor != nullptr)
      {
         // guard against the node being deleted while its editor is open
         bool alive = false;
         for (const GraphNode& gn : gNodes)
         {
            if (gn.node.get() == gFormulaEditor)
               alive = true;
         }
         if (!alive)
         {
            gFormulaEditor = nullptr;
            gFormulaEditorOpen = false;
         }
      }

      if (gFormulaEditorOpen && gFormulaEditor != nullptr)
      {
         ImGui::SetNextWindowSize(ImVec2(620, 460), ImGuiCond_FirstUseEver);
         if (ImGui::Begin("Formula editor", &gFormulaEditorOpen))
         {
            ImGui::TextDisabled("native: the body of  vec4 shape(vec2 uv, vec2 p, float t);  p is centred, t is seconds, knobs uA-uD");
            ImGui::TextDisabled("Shadertoy: paste the whole Image tab (with mainImage); Common tab code goes above it");
            ImGui::TextDisabled("extra knobs:  uniform float uSpeed; // 0..4 = 1");
            ImGui::Separator();

            // Turbo 0.56: growable buffer (a fixed 8 KB one silently refused
            // longer pastes) and a one-click paste for Shadertoy code.
            static std::string editBuf;
            static FormulaNode* lastEdited = nullptr;
            if (lastEdited != gFormulaEditor)
            {
               editBuf = gFormulaEditor->formula;
               lastEdited = gFormulaEditor;
            }

            ImGui::InputTextMultiline("##glsl", &editBuf, ImVec2(-1, ImGui::GetContentRegionAvail().y - 70),
                                      ImGuiInputTextFlags_AllowTabInput);

            if (ImGui::Button("Apply", ImVec2(120, 0)))
            {
               PushUndoCheckpoint();
               gFormulaEditor->formula = editBuf;
               gFormulaEditor->Apply();
               gPatchDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Revert", ImVec2(120, 0)))
               editBuf = gFormulaEditor->formula;
            ImGui::SameLine();
            if (ImGui::Button("Paste + Apply", ImVec2(140, 0)))
            {
               if (const char* clip = ImGui::GetClipboardText())
               {
                  editBuf = clip;
                  PushUndoCheckpoint();
                  gFormulaEditor->formula = editBuf;
                  gFormulaEditor->Apply();
                  gPatchDirty = true;
               }
            }
            ImGui::SameLine();
            ImGui::TextDisabled(gFormulaEditor->ShadertoyMode() ? "Shadertoy mode" : "native mode");

            if (!gFormulaEditor->LastError().empty())
            {
               ImGui::TextWrapped("%s", gFormulaEditor->LastError().c_str());
            }
         }
         ImGui::End();
      }
