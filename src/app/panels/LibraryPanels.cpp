// Library / plugin / field search panels (moved verbatim from main.cpp).
#include "app/AppShared.h"

namespace app
{
   // Sort/filter option lists and predicates for the Samples and Media
   // modes' browser filter strip, and the comparator behind their sort
   // control - all pulled out as free functions (rather than inlined in
   // DrawLibrarySearchPanel's cache-rebuild block) so RunBrowserSortTest can
   // exercise the exact same logic the panel draws with, not a re-typed copy
   // of it (see INFINITE_BROWSERSORTTEST below).

   const std::vector<std::string>& SampleTypeFilterNames()
   {
      // AIFF covers both "aif" and "aiff" - same format, two adjacent
      // entries for it would be noise (see HasAudioExtension). No
      // "Favourites" entry here - the sort dropdown beside this one already
      // has a Favourites option, and showing it in both was confusing.
      static const std::vector<std::string> names = { "All", "WAV", "AIFF", "CAF", "M4A", "MP3", "FLAC" };
      return names;
   }


   bool SampleEntryMatchesTypeFilter(const SampleScanner::Entry& e, int typeFilter)
   {
      switch (typeFilter)
      {
         case 1: return e.extension == "wav";
         case 2: return e.extension == "aif" || e.extension == "aiff";
         case 3: return e.extension == "caf";
         case 4: return e.extension == "m4a";
         case 5: return e.extension == "mp3";
         case 6: return e.extension == "flac";
         default: return true; // 0 = All, and any out-of-range index
      }
   }


   // "All", "Video", "Image", then every individual extension from
   // MediaExtensions.h in the same order that header lists them - the
   // authority the OS drop handler already uses, not a second list. No
   // "Favourites" entry - see SampleTypeFilterNames's comment.
   const std::vector<std::string>& MediaTypeFilterNames()
   {
      static const std::vector<std::string> names = [] {
         std::vector<std::string> v = { "All", "Video", "Image" };
         for (const std::string& ext : MediaExtensions::Video())
         {
            std::string upper = ext;
            std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
            v.push_back(upper);
         }
         for (const std::string& ext : MediaExtensions::Image())
         {
            std::string upper = ext;
            std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
            v.push_back(upper);
         }
         return v;
      }();
      return names;
   }


   bool MediaEntryMatchesTypeFilter(const SampleScanner::Entry& e, int typeFilter)
   {
      const auto& video = MediaExtensions::Video();
      const auto& image = MediaExtensions::Image();
      if (typeFilter == 0)
         return true;
      if (typeFilter == 1)
         return std::find(video.begin(), video.end(), e.extension) != video.end();
      if (typeFilter == 2)
         return std::find(image.begin(), image.end(), e.extension) != image.end();
      const int videoIdx = typeFilter - 3;
      if (videoIdx >= 0 && videoIdx < (int)video.size())
         return e.extension == video[videoIdx];
      const int imageIdx = videoIdx - (int)video.size();
      if (imageIdx >= 0 && imageIdx < (int)image.size())
         return e.extension == image[imageIdx];
      return true; // out-of-range index
   }


   // Shared by Samples and Media - both draw from SampleScanner::Entry and
   // both modes' sort option list is the same four names (see
   // DrawLibrarySearchPanel). sortMode: 0 Name, 1 File type, 2 Folder, 3 Favourites.
   // Every branch falls through to the lowercased-name compare (with a raw
   // fileName tiebreak) so ties within a type/folder/favourites still read
   // alphabetically, and File-type/Folder ties don't need their own
   // tiebreak beyond that. Compares the already-lowercased fileNameLower
   // rather than folding per call - std::sort/stable_sort invoke the
   // predicate O(n log n) times, and per-call allocation there is what
   // stalls a sort visibly past a few thousand entries.
   bool CompareSampleEntries(const SampleScanner::Entry* a, const SampleScanner::Entry* b, int sortMode, bool mediaKind = false)
   {
      if (sortMode == 3)
      {
         const bool favA = mediaKind ? gBrowserFavorites.IsFavoriteMedia(a->path) : gBrowserFavorites.IsFavoriteSample(a->path);
         const bool favB = mediaKind ? gBrowserFavorites.IsFavoriteMedia(b->path) : gBrowserFavorites.IsFavoriteSample(b->path);
         if (favA != favB)
            return favA > favB;
      }
      if (sortMode == 1 && a->extension != b->extension)
         return a->extension < b->extension;
      if (sortMode == 2 && a->folderRoot != b->folderRoot)
         return a->folderRoot < b->folderRoot;
      if (a->fileNameLower != b->fileNameLower)
         return a->fileNameLower < b->fileNameLower;
      return a->fileName < b->fileName; // stable tiebreak - see the comment above
   }


   // The actual filter+sort a scanner index goes through, factored out of
   // DrawLibrarySearchPanel's cache-rebuild block so INFINITE_BROWSERSORTTEST
   // (below RunPluginScanTest) exercises this exact code path against
   // synthetic data rather than a re-typed copy of it. `lowerQuery` must
   // already be lowercased (callers already have it that way, to avoid
   // lowercasing it once per row).
   std::vector<const SampleScanner::Entry*> FilterAndSortSampleEntries(
      const std::vector<SampleScanner::Entry>& index, const std::string& lowerQuery,
      const BrowserFilterState& state, bool mediaKind)
   {
      std::vector<const SampleScanner::Entry*> filtered;
      filtered.reserve(index.size());
      for (const SampleScanner::Entry& entry : index)
      {
         if (!lowerQuery.empty() && entry.fileNameLower.find(lowerQuery) == std::string::npos)
            continue;
         const bool typeMatch = mediaKind ? MediaEntryMatchesTypeFilter(entry, state.typeFilter)
                                           : SampleEntryMatchesTypeFilter(entry, state.typeFilter);
         if (!typeMatch)
            continue;
         filtered.push_back(&entry);
      }

      const int sortMode = state.sortMode;
      std::stable_sort(filtered.begin(), filtered.end(),
                        [sortMode, mediaKind](const SampleScanner::Entry* a, const SampleScanner::Entry* b) {
                           return CompareSampleEntries(a, b, sortMode, mediaKind);
                        });
      if (state.descending)
         std::reverse(filtered.begin(), filtered.end());
      return filtered;
   }


   // The Samples/Media modes of the docked node-browser panel (docs/plans/
   // audio/README.md P3e): folder list management, a background-thread scan
   // (SampleScanner), filter-as-you-type over the persisted index, and a
   // drag source per result row that gSampleDragActive (above) resolves on
   // release into either a new node or an existing matching one's file.
   // Shared by both modes - idPrefix keeps the two modes' ImGui widget IDs
   // (and therefore their input focus/state) from colliding, searchHint is
   // the mode-specific placeholder text, and mediaKind tags the drag so the
   // release handler knows whether to resolve it against Sampler or against
   // Image Source/Video.
   void DrawLibrarySearchPanel(SampleScanner& scanner, const char* idPrefix, const char* searchHint, bool mediaKind)
   {
      scanner.PollResults();

      ImGui::PushID(idPrefix);

      {
         const char* addLabel = "Add folder...";
         const float iconSize = ImGui::GetFrameHeight() * 0.65f;
         const float iconGap = 6.0f;
         const ImVec2 btnPos = ImGui::GetCursorScreenPos();
         const bool clicked = ImGui::Button("##addfolder", ImVec2(-1.0f, 0));
         const ImVec2 bmin = ImGui::GetItemRectMin();
         const ImVec2 bmax = ImGui::GetItemRectMax();
         const float btnW = bmax.x - bmin.x;
         const float btnH = bmax.y - bmin.y;
         const float textW = ImGui::CalcTextSize(addLabel).x;
         const float totalContentW = iconSize + iconGap + textW;
         const float startX = bmin.x + (btnW - totalContentW) * 0.5f;
         const float centerY = bmin.y + btnH * 0.5f;

         ImDrawList* dl = ImGui::GetWindowDrawList();
         const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
         Tabler::DrawPlus(dl, ImVec2(startX + iconSize * 0.5f, centerY), iconSize, col);
         dl->AddText(ImVec2(startX + iconSize + iconGap, centerY - ImGui::GetTextLineHeight() * 0.5f), col, addLabel);

         if (clicked)
         {
            const std::string path = Platform::OpenFolderDialog();
            if (!path.empty())
               scanner.AddFolder(path);
         }
      }

      // Folders list, each with its own refresh and remove button. Kept
      // short (no scroll region of its own) since a handful of library
      // folders is the expected case - the result list below is where
      // scrolling matters.
      std::string folderToRemove;
      std::string folderToScan;
      bool scanAll = false;
      const bool scanning = scanner.IsScanning();
      const float panelW = ImGui::GetContentRegionAvail().x;
      for (const std::string& folder : scanner.Folders())
      {
         ImGui::PushID(folder.c_str());
         const float btnW = ImGui::GetFrameHeight();
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + panelW - 2.0f * btnW - 14.0f);
         ImGui::TextDisabled("%s", folder.c_str());
         ImGui::PopTextWrapPos();
         ImGui::SameLine(panelW - 2.0f * btnW - 4.0f);
         if (scanning)
            ImGui::BeginDisabled();
         // Drawn as a vector arc-with-arrowhead, not the U+21BB clockwise
         // arrow character it used to be - this font has no glyph range
         // beyond Basic Latin (see DrawBrowserFilterStrip's sort-direction
         // comment), so that rendered as a literal '?'.
         const bool refreshClicked = ImGui::Button("##refreshfolder", ImVec2(btnW, 0));
         {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 bmin = ImGui::GetItemRectMin();
            const ImVec2 bmax = ImGui::GetItemRectMax();
            const ImVec2 center((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
            const float iconSize = (bmax.y - bmin.y) * 0.72f;
            const ImU32 col = ImGui::IsItemHovered() || scanning ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled);
            Tabler::DrawRefresh(dl, center, iconSize, col);
         }
         if (refreshClicked)
            folderToScan = folder;
         if (scanning)
            ImGui::EndDisabled();
         ImGui::SameLine(panelW - btnW);
         if (ImGui::Button("##removefolder", ImVec2(btnW, 0)))
            folderToRemove = folder;
         {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 bmin = ImGui::GetItemRectMin();
            const ImVec2 bmax = ImGui::GetItemRectMax();
            const ImVec2 center((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
            const float iconSize = (bmax.y - bmin.y) * 0.65f;
            const ImU32 col = ImGui::IsItemHovered() ? IM_COL32(230, 60, 60, 255) : ImGui::GetColorU32(ImGuiCol_TextDisabled);
            Tabler::DrawX(dl, center, iconSize, col);
         }
         ImGui::PopID();
      }
      if (!folderToRemove.empty())
         scanner.RemoveFolder(folderToRemove);

      ImGui::Dummy(ImVec2(0.0f, 4.0f));
      {
         if (scanning)
            ImGui::BeginDisabled();
         const char* refreshLabel = "Refresh all";
         const float iconSize = ImGui::GetFrameHeight() * 0.65f;
         const float iconGap = 6.0f;
         const bool clicked = ImGui::Button("##refreshall", ImVec2(-1.0f, 0));
         const ImVec2 bmin = ImGui::GetItemRectMin();
         const ImVec2 bmax = ImGui::GetItemRectMax();
         const float btnW = bmax.x - bmin.x;
         const float btnH = bmax.y - bmin.y;
         const float textW = ImGui::CalcTextSize(refreshLabel).x;
         const float totalContentW = iconSize + iconGap + textW;
         const float startX = bmin.x + (btnW - totalContentW) * 0.5f;
         const float centerY = bmin.y + btnH * 0.5f;

         ImDrawList* dl = ImGui::GetWindowDrawList();
         const ImU32 col = ImGui::GetColorU32(scanning ? ImGuiCol_TextDisabled : ImGuiCol_Text);
         Tabler::DrawRefresh(dl, ImVec2(startX + iconSize * 0.5f, centerY), iconSize, col);
         dl->AddText(ImVec2(startX + iconSize + iconGap, centerY - ImGui::GetTextLineHeight() * 0.5f), col, refreshLabel);

         if (clicked)
            scanAll = true;
         if (scanning)
            ImGui::EndDisabled();
         if (scanning)
            ImGui::TextDisabled("scanning... (%d found)", scanner.FilesFoundSoFar());
      }
      if (scanAll)
         scanner.StartScan();
      else if (!folderToScan.empty())
         scanner.StartScan(folderToScan);

      ImGui::Separator();

      struct LibraryFilterCache
      {
         std::string lastQuery;
         uint64_t lastIndexVersion = 0;
         uint64_t lastFavoritesVersion = 0;
         int lastSortMode = -1;
         int lastTypeFilter = -1;
         bool lastDescending = false;
         std::vector<const SampleScanner::Entry*> filtered;
      };
      static LibraryFilterCache sSampleCache;
      static LibraryFilterCache sMediaCache;
      LibraryFilterCache& cache = mediaKind ? sMediaCache : sSampleCache;

      // Two independent BrowserFilterStates (not one shared instance) so the
      // Samples and Media modes each keep their own in-progress query, sort
      // and filter when the user switches tabs and back - this is what the
      // old per-mode `static char` search buffers here used to guarantee for
      // the query alone.
      BrowserFilterState& filterState = mediaKind ? gMediaFilter : gSampleFilter;
      static const std::vector<std::string> kLibrarySortNames = { "Name", "File type", "Folder", "Favourites" };
      const bool filterChanged = DrawBrowserFilterStrip(
         filterState, searchHint, kLibrarySortNames, mediaKind ? MediaTypeFilterNames() : SampleTypeFilterNames());
      if (filterChanged)
         SaveBrowserFilterPrefs();

      std::string q = filterState.query;
      std::transform(q.begin(), q.end(), q.begin(), ::tolower);

      // The button un-latches by itself once the file finishes playing -
      // once per frame, not per row, since it's a property of the preview
      // player, not of any particular row.
      if (!mediaKind && !gPreviewingSamplePath.empty() && !AudioEngine::Instance().Preview().IsPlaying())
         gPreviewingSamplePath.clear();

      // Rebuilt only when the query, the index, or any sort/filter control
      // changes (0 ms when idle/scrolling/browsing). Sorting is strictly
      // more expensive than filtering, so it happens in this same
      // cache-rebuild block rather than the draw loop below - miss any one
      // of these five keys and that control silently stops updating the
      // list the moment the user touches it.
      if (cache.lastQuery != q || cache.lastIndexVersion != scanner.IndexVersion() ||
          cache.lastFavoritesVersion != gBrowserFavorites.Version() ||
          cache.lastSortMode != filterState.sortMode || cache.lastTypeFilter != filterState.typeFilter ||
          cache.lastDescending != filterState.descending)
      {
         cache.filtered = FilterAndSortSampleEntries(scanner.Index(), q, filterState, mediaKind);

         cache.lastQuery = q;
         cache.lastIndexVersion = scanner.IndexVersion();
         cache.lastFavoritesVersion = gBrowserFavorites.Version();
         cache.lastSortMode = filterState.sortMode;
         cache.lastTypeFilter = filterState.typeFilter;
         cache.lastDescending = filterState.descending;
      }

      const auto& filtered = cache.filtered;

      // Tighter vertical rhythm than ImGui's default ItemSpacing - a few
      // hundred one-shot rows is the point of this panel, and the default
      // spacing wastes a row's worth of height every 4-5 entries.
      const ImVec2 savedItemSpacing = ImGui::GetStyle().ItemSpacing;
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(savedItemSpacing.x, 2.0f));
      ImGui::BeginChild("##librarypanellist", ImVec2(0, 0), false);
      // A scanned library folder can hold tens of thousands of files -
      // submitting a Selectable (now a button too) for every one of them
      // regardless of scroll position is what tanked this panel's frame
      // time. The clipper only visits rows actually on screen; row height
      // must be uniform for it to skip the rest correctly, which holds here
      // since every row is exactly one frame-height button/Selectable tall.
      // No explicit row-height guess - letting the clipper measure the
      // first row itself avoids even a one-pixel mismatch against the real
      // rendered height, which compounds over thousands of rows into a
      // visible gap of blank space once scrolled near the end of the list.
      ImGuiListClipper clipper;
      clipper.Begin((int)filtered.size());
      while (clipper.Step())
      {
         for (int rowIdx = clipper.DisplayStart; rowIdx < clipper.DisplayEnd; rowIdx++)
         {
            const SampleScanner::Entry& entry = *filtered[rowIdx];

            ImGui::PushID(entry.path.c_str());

            const float rowH = ImGui::GetFrameHeight();
            const bool isFav = mediaKind ? gBrowserFavorites.IsFavoriteMedia(entry.path) : gBrowserFavorites.IsFavoriteSample(entry.path);

         // Media mode has no audition - images/video get no play button, and
         // the Selectable alone keeps the same full-width layout it always
         // had there.
         if (!mediaKind)
         {
            const bool isPlaying = (gPreviewingSamplePath == entry.path);
            // Smaller than a full frame-height button - at full size the
            // icon dominated the row next to the filename text.
            const float btnH = rowH * 0.7f;
            const float btnW = btnH; // square, tighter than a wide text label
            const float rowStartY = ImGui::GetCursorPosY();
            const ImVec2 rowScreenMin = ImGui::GetCursorScreenPos();

            if (isPlaying)
            {
               // A subtle tint behind the whole row so the playing one stays
               // findable after the list has scrolled - drawn before the
               // row's widgets so it sits behind them, not on top.
               const ImVec2 rowMax(rowScreenMin.x + ImGui::GetContentRegionAvail().x, rowScreenMin.y + rowH);
               ImGui::GetWindowDrawList()->AddRectFilled(rowScreenMin, rowMax,
                                                        ImGui::ColorConvertFloat4ToU32(AccentEmphasisHover()));
            }

            // Nudge the (now-shorter) button down so it sits centered
            // against the full-height Selectable beside it, rather than
            // pinned to the row's top edge.
            ImGui::SetCursorPosY(rowStartY + (rowH - btnH) * 0.5f);

            // Drawn as vector shapes on the button, not a font glyph - the
            // UI font is loaded with no glyph range beyond Basic Latin, so a
            // real play/pause character here would render as a literal '?'
            // (docs/plans/audio/plugin-hosting.md §3 already hit this for
            // the plugin editor's open/close button). A triangle/two bars
            // reads as a transport icon at a glance; "|>" as literal text
            // does not.
            const bool clicked = ImGui::Button("##preview", ImVec2(btnW, btnH));
            {
               ImDrawList* dl = ImGui::GetWindowDrawList();
               const ImVec2 bmin = ImGui::GetItemRectMin();
               const ImVec2 bmax = ImGui::GetItemRectMax();
               const ImVec2 center((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
               const ImU32 iconCol = (ImGui::IsItemHovered() || isPlaying) ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled);
               const float iconSize = btnH * 0.85f;
               if (isPlaying)
                  Tabler::DrawPlayerPause(dl, center, iconSize, iconCol);
               else
                  Tabler::DrawPlayerPlay(dl, center, iconSize, iconCol, true);
            }
            if (clicked)
            {
               SamplePreviewPlayer& preview = AudioEngine::Instance().Preview();
               if (isPlaying)
               {
                  preview.Stop();
                  gPreviewingSamplePath.clear();
               }
               else
               {
                  preview.Stop();
                  auto* decoded = new Platform::SampleBuffer();
                  std::string error;
                  if (Platform::DecodeAudioFileToBuffer(entry.path, *decoded, error))
                  {
                     preview.Play(decoded);
                     gPreviewingSamplePath = entry.path;
                     gPreviewErrorPath.clear();
                  }
                  else
                  {
                     delete decoded;
                     gPreviewingSamplePath.clear();
                     gPreviewErrorPath = entry.path;
                     gPreviewErrorMessage = error.empty() ? "failed to decode" : error;
                  }
               }
            }
            ImGui::SameLine();
            // Back to the row's actual top, not the button's centered
            // offset, so the Selectable spans the full row height and the
            // clipper's fixed row-height assumption keeps holding.
            ImGui::SetCursorPosY(rowStartY);
         }

         // The drag target - IsItemActive()/IsMouseDragging() and the
         // INFINITE_SAMPLERDRAGTEST/MEDIADRAGTEST row-rect capture below all
         // key off *this* item, not the play button, so a drag started on
         // the button (which is a separate widget one item back) never
         // begins a sample drag.
         const float availW = ImGui::GetContentRegionAvail().x;
         // Reserved unconditionally (not just when isFav) so a row's text
         // doesn't reflow when its favourite state toggles.
         const float badgeReserve = 20.0f;
         const std::string rowLabel = TruncateWithEllipsis(entry.fileName, std::max(20.0f, availW - badgeReserve));
         ImGui::Selectable(rowLabel.c_str(), false, 0, ImVec2(availW, 0));
         const ImVec2 selMin = ImGui::GetItemRectMin();
         const ImVec2 selMax = ImGui::GetItemRectMax();
         if (!mediaKind && getenv("INFINITE_SAMPLERDRAGTEST") != nullptr)
         {
            const ImVec2 mn = ImGui::GetItemRectMin();
            const ImVec2 mx = ImGui::GetItemRectMax();
            gSamplerDragTestRowRect = ImVec4(mn.x, mn.y, mx.x, mx.y);
         }
         if (mediaKind && getenv("INFINITE_MEDIADRAGTEST") != nullptr)
         {
            const ImVec2 mn = ImGui::GetItemRectMin();
            const ImVec2 mx = ImGui::GetItemRectMax();
            gMediaDragTestRowRect = ImVec4(mn.x, mn.y, mx.x, mx.y);
         }
         // A drag starts once the mouse has moved a few pixels past the
         // click - matching ImGui's own drag threshold - rather than on the
         // first frame the button goes active, so a plain click still just
         // clicks (Selectable has no useful action of its own here today,
         // but a click-drag distinction matters the moment one is added).
         if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f))
         {
            gSampleDragActive = true;
            gSampleDragKind = mediaKind ? LibraryDragKind::Media : LibraryDragKind::Sample;
            gSampleDragPath = entry.path;
            gSampleDragName = entry.fileName;
         }
         // ImGuiHoveredFlags_ForTooltip (stationary + a short shared delay,
         // per style.HoverFlagsForTooltipMouse) rather than a bare
         // IsItemHovered(), which fired on literally the first hovered frame
         // of every row. Only the decode-error tooltip is shown here - the
         // absolute-path tooltip was removed as noise on every hover.
         if (!gSampleDragActive && !mediaKind && gPreviewingSamplePath.empty() &&
             gPreviewErrorPath == entry.path &&
             ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
         {
            ImGui::SetTooltip("%s", gPreviewErrorMessage.c_str());
         }

         DrawFavoriteBadge(selMin, selMax, isFav);

         if (ImGui::BeginPopupContextItem("##entry_ctx"))
         {
            if (ImGui::MenuItem(isFav ? "Remove from favourites" : "Add to favourites"))
            {
               if (mediaKind)
                  gBrowserFavorites.ToggleMedia(entry.path);
               else
                  gBrowserFavorites.ToggleSample(entry.path);
            }
            if (ImGui::MenuItem("Add to canvas"))
            {
               const ImVec2 spawnPos = FindFreeSpawnPosition(gViewCenterCanvas);
               PushUndoCheckpoint();
               if (!mediaKind)
               {
                  if (GraphNode* gn = SpawnNode("Sampler", "Synths", spawnPos.x, spawnPos.y))
                  {
                     if (auto* sampler = dynamic_cast<SamplerNode*>(gn->node.get()))
                        sampler->LoadFile(entry.path);
                     gPatchDirty = true;
                  }
               }
               else
               {
                  const bool isVid = HasExtension(entry.path, kVideoExt);
                  if (GraphNode* gn = SpawnNode(isVid ? "Video" : "Image Source", "Source", spawnPos.x, spawnPos.y))
                  {
                     if (isVid)
                     {
                        if (auto* vid = dynamic_cast<VideoSourceNode*>(gn->node.get()))
                           vid->Open(entry.path);
                     }
                     else
                     {
                        if (auto* img = dynamic_cast<ImageSourceNode*>(gn->node.get()))
                           img->Load(entry.path);
                     }
                     gPatchDirty = true;
                  }
               }
            }
            ImGui::EndPopup();
         }

         ImGui::PopID();
         }
      }
      ImGui::EndChild();
      ImGui::PopStyleVar();

      ImGui::PopID();
   }


   // Case-insensitive ASCII fold-and-compare - same fold SampleScanner's
   // ToLower uses, byte-wise rather than std::locale collation (see
   // CompareSampleEntries's comment; that decision applies here too).
   // Plugin counts are dozens to low hundreds, not the thousands a scanned
   // sample library can hold, so folding per comparison call (rather than a
   // precomputed lowercase field on Platform::PluginDesc) is cheap enough
   // here.
   bool ILess(const std::string& a, const std::string& b)
   {
      std::string la = a, lb = b;
      std::transform(la.begin(), la.end(), la.begin(), [](unsigned char c) { return (char)std::tolower(c); });
      std::transform(lb.begin(), lb.end(), lb.begin(), [](unsigned char c) { return (char)std::tolower(c); });
      return la != lb ? la < lb : a < b; // stable tiebreak on the raw string
   }


   // No "Favourites" entry - the sort dropdown beside this one already has
   // a Favourites option, and showing it in both was confusing.
   const std::vector<std::string>& PluginTypeFilterNames()
   {
      static const std::vector<std::string> names = { "All", "AU", "VST3" };
      return names;
   }


   bool PluginEntryMatchesTypeFilter(const PluginScanner::Entry& e, int typeFilter)
   {
      switch (typeFilter)
      {
         case 1: return e.format == "au";
         case 2: return e.format == "vst3";
         default: return true; // 0 = All, and any out-of-range index
      }
   }


   // sortMode: 0 Name, 1 Format, 2 Manufacturer, 3 Favourites. Every branch falls through
   // to the name compare so ties within a format/manufacturer/favourites still read
   // alphabetically.
   bool ComparePluginEntries(const PluginScanner::Entry* a, const PluginScanner::Entry* b, int sortMode)
   {
      if (sortMode == 3)
      {
         const bool favA = gBrowserFavorites.IsFavoritePlugin(a->identifier);
         const bool favB = gBrowserFavorites.IsFavoritePlugin(b->identifier);
         if (favA != favB)
            return favA > favB;
      }
      if (sortMode == 1 && a->format != b->format)
         return a->format < b->format;
      if (sortMode == 2 && a->manufacturer != b->manufacturer)
         return ILess(a->manufacturer, b->manufacturer);
      return ILess(a->name, b->name);
   }


   // Same reasoning as FilterAndSortSampleEntries above - factored out so
   // INFINITE_BROWSERSORTTEST exercises the real filter+sort path.
   // `lowerQuery` must already be lowercased.
   std::vector<const PluginScanner::Entry*> FilterAndSortPluginEntries(
      const std::vector<PluginScanner::Entry>& index, const std::string& lowerQuery, const BrowserFilterState& state)
   {
      std::vector<const PluginScanner::Entry*> filtered;
      filtered.reserve(index.size());
      for (const PluginScanner::Entry& entry : index)
      {
         if (!lowerQuery.empty())
         {
            std::string hay = entry.name + " " + entry.manufacturer;
            std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
            if (hay.find(lowerQuery) == std::string::npos)
               continue;
         }
         if (!PluginEntryMatchesTypeFilter(entry, state.typeFilter))
            continue;
         filtered.push_back(&entry);
      }

      const int sortMode = state.sortMode;
      std::stable_sort(filtered.begin(), filtered.end(),
                        [sortMode](const PluginScanner::Entry* a, const PluginScanner::Entry* b) {
                           return ComparePluginEntries(a, b, sortMode);
                        });
      if (state.descending)
         std::reverse(filtered.begin(), filtered.end());
      return filtered;
   }


   // The Plugins mode of the docked node-browser panel. Same shape as
   // DrawLibrarySearchPanel above, minus the folder machinery: Audio Unit
   // discovery is a registry query, not a directory walk, so there is nothing
   // to add a folder to and the whole mode reduces to one Rescan button. Like
   // the other modes, the list shown at launch comes off disk - a scan only
   // ever happens when the user asks for one.
   void DrawPluginSearchPanel()
   {
      gPluginScanner.PollResults();

      ImGui::PushID("##plugins");

      const bool scanning = gPluginScanner.IsScanning();
      if (scanning)
         ImGui::BeginDisabled();
      if (ImGui::Button("Rescan plugins", ImVec2(-1.0f, 0)))
         gPluginScanner.StartScan();
      if (scanning)
         ImGui::EndDisabled();

      if (scanning)
         ImGui::TextDisabled("scanning... (%d found)", gPluginScanner.PluginsFoundSoFar());
      else if (gPluginScanner.Index().empty())
         ImGui::TextDisabled("no plugins indexed yet - hit Rescan plugins");

#if INFINITE_ENABLE_VST3
      // VST3 folder management: AU is discovered entirely through the OS
      // component registry and needs none of this, but VST3 has no registry -
      // only the two OS-standard directories plus whatever the user adds here.
      if (ImGui::TreeNodeEx("VST3 search folders", ImGuiTreeNodeFlags_None))
      {
         // Snapshot rather than iterate gPluginScanner.Folders() directly:
         // RemoveFolder() below erases from that same live vector, which
         // would invalidate this loop's iterator mid-iteration.
         const std::vector<std::string> folders = gPluginScanner.Folders();
         for (const std::string& folder : folders)
         {
            // Button first, path wrapped after it: a long folder path used to
            // push Remove past the right edge of the panel, out of reach.
            ImGui::PushID(folder.c_str());
            if (ImGui::SmallButton("Remove"))
               gPluginScanner.RemoveFolder(folder);
            ImGui::PopID();
            ImGui::SameLine();
            ImGui::TextWrapped("%s", folder.c_str());
         }
         if (ImGui::Button("Add VST3 folder...", ImVec2(-1.0f, 0)))
         {
            const std::string folder = Platform::OpenFolderDialog("Add VST3 folder");
            if (!folder.empty())
               gPluginScanner.AddFolder(folder);
         }
         ImGui::TreePop();
      }

      const std::vector<std::string> blocklist = Platform::VST3Blocklist();
      if (!blocklist.empty())
      {
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.55f, 0.25f, 1.0f));
         if (ImGui::TreeNodeEx("Blocklisted VST3 bundles (crashed or hung while scanning)",
                                ImGuiTreeNodeFlags_None))
         {
            ImGui::PopStyleColor();
            for (const std::string& path : blocklist)
               ImGui::TextWrapped("%s", path.c_str());
            if (ImGui::Button("Clear blocklist and retry", ImVec2(-1.0f, 0)))
               Platform::ClearVST3Blocklist();
            ImGui::TreePop();
         }
         else
         {
            ImGui::PopStyleColor();
         }
      }

      if (!gPluginScanner.FailedBundles().empty())
      {
         if (ImGui::TreeNodeEx("Bundles that failed to describe", ImGuiTreeNodeFlags_None))
         {
            for (const std::string& path : gPluginScanner.FailedBundles())
               ImGui::TextWrapped("%s", path.c_str());
            ImGui::TreePop();
         }
      }

      if (!gPluginScanner.UnsupportedPlugins().empty())
      {
         const std::string label = std::to_string(gPluginScanner.UnsupportedPlugins().size())
            + " VST2 plugin" + (gPluginScanner.UnsupportedPlugins().size() == 1 ? "" : "s")
            + " found and skipped - Infinite hosts VST3 and AU only";
         if (ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_None))
         {
            for (const std::string& path : gPluginScanner.UnsupportedPlugins())
               ImGui::TextWrapped("%s", path.c_str());
            ImGui::TreePop();
         }
      }
#else
      ImGui::TextDisabled("VST3 support is not compiled into this build.");
#endif

      ImGui::Separator();

      // Its own BrowserFilterState, like the Samples and Media modes each
      // have, so switching tabs and back keeps this mode's in-progress
      // query, sort and filter.
      static bool sPluginSearchSeeded = false;
      if (!sPluginSearchSeeded)
      {
         if (const char* seed = getenv("INFINITE_PLUGINDRAGTEST_SEARCH"))
         {
            strncpy(gPluginFilter.query, seed, sizeof(gPluginFilter.query) - 1);
            gPluginFilter.query[sizeof(gPluginFilter.query) - 1] = '\0';
            sPluginSearchSeeded = true;
         }
         else if (getenv("INFINITE_PLUGINDRAGTEST") != nullptr)
         {
            // Narrow the list to a single row before the drag gesture runs.
            // With every installed plugin listed, PluginScanner::PollResults
            // can swap the whole index in between the row the fixture
            // latched onto and the row actually under the cursor when the
            // drag starts, landing the drop on a different plugin (same
            // manufacturer, different name) than the one that was expected.
            // Sorting is a second way that can happen now - the query still
            // narrows the *set* to one row regardless of sort order, so this
            // logic is unaffected by which sort mode is active.
            // A one-row list removes the swap target entirely. Wait for the
            // scan to settle with at least one result before picking it.
            if (!gPluginScanner.IsScanning() && !gPluginScanner.Index().empty())
            {
               auto countMatches = [&](const std::string& query) {
                  std::string ql = query;
                  std::transform(ql.begin(), ql.end(), ql.begin(), ::tolower);
                  int n = 0;
                  for (const PluginScanner::Entry& e : gPluginScanner.Index())
                  {
                     std::string hay = e.name + " " + e.manufacturer;
                     std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
                     if (hay.find(ql) != std::string::npos)
                        n++;
                  }
                  return n;
               };
               const PluginScanner::Entry& first = gPluginScanner.Index()[0];
               std::string query = first.name;
               if (countMatches(query) != 1)
                  query = first.name + " " + first.manufacturer;
               strncpy(gPluginFilter.query, query.c_str(), sizeof(gPluginFilter.query) - 1);
               gPluginFilter.query[sizeof(gPluginFilter.query) - 1] = '\0';
               sPluginSearchSeeded = true;
            }
         }
         else
         {
            sPluginSearchSeeded = true;
         }
      }

      static const std::vector<std::string> kPluginSortNames = { "Name", "Format", "Manufacturer", "Favourites" };
      // Empty typeNames under !INFINITE_ENABLE_VST3 hides the type control
      // rather than showing a dropdown with one real option (AU) - the
      // index only ever holds AU entries in that build anyway.
#if INFINITE_ENABLE_VST3
      const std::vector<std::string>& pluginTypeNames = PluginTypeFilterNames();
#else
      static const std::vector<std::string> pluginTypeNames;
#endif
      const bool filterChanged =
         DrawBrowserFilterStrip(gPluginFilter, "search plugins...", kPluginSortNames, pluginTypeNames);
      if (filterChanged)
         SaveBrowserFilterPrefs();

      std::string q = gPluginFilter.query;
      std::transform(q.begin(), q.end(), q.begin(), ::tolower);

      struct PluginFilterCache
      {
         std::string lastQuery;
         uint64_t lastIndexVersion = 0;
         uint64_t lastFavoritesVersion = 0;
         int lastSortMode = -1;
         int lastTypeFilter = -1;
         bool lastDescending = false;
         std::vector<const PluginScanner::Entry*> filtered;
      };
      static PluginFilterCache sCache;

      // Plugins mode had no filter cache before this change - it filtered
      // inline every frame, which was fine with no sort. Sorting needs one,
      // same LibraryFilterCache shape as Samples/Media (see that struct) -
      // a third instance, not a new abstraction.
      if (sCache.lastQuery != q || sCache.lastIndexVersion != gPluginScanner.IndexVersion() ||
          sCache.lastFavoritesVersion != gBrowserFavorites.Version() ||
          sCache.lastSortMode != gPluginFilter.sortMode || sCache.lastTypeFilter != gPluginFilter.typeFilter ||
          sCache.lastDescending != gPluginFilter.descending)
      {
         sCache.filtered = FilterAndSortPluginEntries(gPluginScanner.Index(), q, gPluginFilter);

         sCache.lastQuery = q;
         sCache.lastIndexVersion = gPluginScanner.IndexVersion();
         sCache.lastFavoritesVersion = gBrowserFavorites.Version();
         sCache.lastSortMode = gPluginFilter.sortMode;
         sCache.lastTypeFilter = gPluginFilter.typeFilter;
         sCache.lastDescending = gPluginFilter.descending;
      }

      ImGui::BeginChild("##pluginpanellist", ImVec2(0, 0), false);
      // INFINITE_PLUGINDRAGTEST captures the FIRST matching row, not the last:
      // this list is every installed effect, and the rows past the visible
      // height are drawn but clipped, so a synthetic press aimed at the last
      // one's rect would land outside the child on nothing at all. The first
      // row is always on screen. (The Samples/Media modes capture the last row
      // because their fixtures stage a folder with exactly one file in it.)
      bool testRowCaptured = false;
      for (const PluginScanner::Entry* entryPtr : sCache.filtered)
      {
         const PluginScanner::Entry& entry = *entryPtr;
         ImGui::PushID(entry.identifier.c_str());

         const bool isFav = gBrowserFavorites.IsFavoritePlugin(entry.identifier);

         std::string label = "[" + (entry.format == "vst3" ? std::string("VST3") : std::string("AU")) +
                              "] " + entry.name;
         if (!entry.manufacturer.empty())
            label += "  -  " + entry.manufacturer;

         const float availW = ImGui::GetContentRegionAvail().x;
         const float badgeReserve = 20.0f;
         const std::string rowLabel = TruncateWithEllipsis(label, std::max(20.0f, availW - badgeReserve));
         ImGui::Selectable(rowLabel.c_str(), false, 0, ImVec2(availW, 0));
         const ImVec2 selMin = ImGui::GetItemRectMin();
         const ImVec2 selMax = ImGui::GetItemRectMax();
         if (!testRowCaptured && getenv("INFINITE_PLUGINDRAGTEST") != nullptr)
         {
            const ImVec2 mn = ImGui::GetItemRectMin();
            const ImVec2 mx = ImGui::GetItemRectMax();
            gPluginDragTestRowRect = ImVec4(mn.x, mn.y, mx.x, mx.y);
            gPluginDragTestRowId = entry.identifier;
            testRowCaptured = true;
         }
         // Same manual drag mechanism as the Samples/Media rows - see
         // gSampleDragActive's comment for why this isn't an ImGui drag source.
         if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f))
         {
            gSampleDragActive = true;
            gSampleDragKind = LibraryDragKind::Plugin;
            gSampleDragPath.clear();
            gSampleDragName = entry.name;
            gPluginDragDesc = entry;
         }
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\n%s", entry.format.c_str(), entry.identifier.c_str());

         DrawFavoriteBadge(selMin, selMax, isFav);

         if (ImGui::BeginPopupContextItem("##plugin_ctx"))
         {
            if (ImGui::MenuItem(isFav ? "Remove from favourites" : "Add to favourites"))
               gBrowserFavorites.TogglePlugin(entry.identifier);
            if (ImGui::MenuItem("Add to canvas"))
            {
               const ImVec2 spawnPos = FindFreeSpawnPosition(gViewCenterCanvas);
               PushUndoCheckpoint();
               if (GraphNode* gn = SpawnNode("Plugin", "AudioEffects", spawnPos.x, spawnPos.y))
               {
                  if (auto* plugin = dynamic_cast<AudioPluginNode*>(gn->node.get()))
                     plugin->LoadPlugin(entry);
                  gPatchDirty = true;
               }
            }
            ImGui::EndPopup();
         }

         ImGui::PopID();
      }
      ImGui::EndChild();

      ImGui::PopID();
   }


   inline const std::vector<FieldSearchEntry>& GetAllFieldLibraryEntries()
   {
      static std::vector<FieldSearchEntry> sEntries;
      if (sEntries.empty())
      {
         const auto& synths = FieldSynthNode::Presets();
         for (size_t i = 0; i < synths.size(); i++)
            sEntries.push_back({ synths[i].name, "Synth", "Field Synth", "Synths", (int)i });

         const auto& effects = FieldSampleNode::Presets();
         for (size_t i = 0; i < effects.size(); i++)
            sEntries.push_back({ effects[i].name, "Effects", "Field Effect", "AudioEffects", (int)i });

         const auto& modifiers = FieldElementNode::Presets();
         for (size_t i = 0; i < modifiers.size(); i++)
            sEntries.push_back({ modifiers[i].name, "Modifiers", "Field Modifier", "3D", (int)i });

         const auto& prims = FieldPrimitiveNode::Presets();
         for (size_t i = 0; i < prims.size(); i++)
            sEntries.push_back({ prims[i].name, "3D Shapes", "Field Primitive", "3D", (int)i });

         const auto& pixels = FieldPixelNode::Presets();
         for (size_t i = 0; i < pixels.size(); i++)
            sEntries.push_back({ pixels[i].name, "2D Visuals", "FieldPixel", "Source", (int)i });
      }
      return sEntries;
   }


   void SpawnFieldPresetNode(const FieldSearchEntry& entry, float x, float y)
   {
      PushUndoCheckpoint();
      if (GraphNode* gn = SpawnNode(entry.nodeType, entry.nodeCategory, x, y))
      {
         if (auto* sn = dynamic_cast<FieldSynthNode*>(gn->node.get()))
         {
            sn->presetIndex = entry.presetIndex;
            sn->LoadPreset(entry.presetIndex);
         }
         else if (auto* fn = dynamic_cast<FieldSampleNode*>(gn->node.get()))
         {
            fn->presetIndex = entry.presetIndex;
            fn->LoadPreset(entry.presetIndex);
         }
         else if (auto* en = dynamic_cast<FieldElementNode*>(gn->node.get()))
         {
            en->presetIndex = entry.presetIndex;
            en->LoadPreset(entry.presetIndex);
         }
         else if (auto* pn = dynamic_cast<FieldPrimitiveNode*>(gn->node.get()))
         {
            pn->presetIndex = entry.presetIndex;
            pn->LoadPreset(entry.presetIndex);
         }
         else if (auto* px = dynamic_cast<FieldPixelNode*>(gn->node.get()))
         {
            px->presetIndex = entry.presetIndex;
            px->LoadPreset(entry.presetIndex);
         }
         gPatchDirty = true;
      }
   }


   void DrawFieldSearchPanel()
   {
      ImGui::PushID("##field_panel");

      static const std::vector<std::string> kFieldSortNames = { "Category", "Name", "Favourites" };
      static const std::vector<std::string> kFieldCategories = { "All", "Synth", "Effects", "Modifiers", "3D Shapes", "2D Visuals" };

      if (DrawBrowserFilterStrip(gFieldFilter, "search field presets...", kFieldSortNames, kFieldCategories))
         SaveBrowserFilterPrefs();

      std::string q = gFieldFilter.query;
      std::transform(q.begin(), q.end(), q.begin(), ::tolower);

      const std::string catFilter = (gFieldFilter.typeFilter > 0 && gFieldFilter.typeFilter < (int)kFieldCategories.size())
         ? kFieldCategories[gFieldFilter.typeFilter] : std::string();

      const auto& allEntries = GetAllFieldLibraryEntries();
      std::vector<const FieldSearchEntry*> matches;
      for (const auto& entry : allEntries)
      {
         if (!catFilter.empty() && entry.category != catFilter)
            continue;
         if (!q.empty())
         {
            std::string hay = entry.name + " " + entry.category;
            std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
            if (hay.find(q) == std::string::npos)
               continue;
         }
         matches.push_back(&entry);
      }

      if (gFieldFilter.sortMode == 1) // Name
      {
         std::stable_sort(matches.begin(), matches.end(), [](const FieldSearchEntry* a, const FieldSearchEntry* b) {
            return ILess(a->name, b->name);
         });
      }
      else if (gFieldFilter.sortMode == 2) // Favourites
      {
         std::stable_sort(matches.begin(), matches.end(), [](const FieldSearchEntry* a, const FieldSearchEntry* b) {
            const bool favA = gBrowserFavorites.IsFavoriteFieldPreset(a->name);
            const bool favB = gBrowserFavorites.IsFavoriteFieldPreset(b->name);
            if (favA != favB)
               return favA > favB;
            return ILess(a->name, b->name);
         });
      }
      else // Category
      {
         std::stable_sort(matches.begin(), matches.end(), [](const FieldSearchEntry* a, const FieldSearchEntry* b) {
            if (a->category != b->category)
               return a->category < b->category;
            return ILess(a->name, b->name);
         });
      }

      if (gFieldFilter.descending)
         std::reverse(matches.begin(), matches.end());

      ImGui::Separator();
      ImGui::BeginChild("##fieldpanellist", ImVec2(0, 0), false);

      ImGuiListClipper clipper;
      clipper.Begin((int)matches.size());
      while (clipper.Step())
      {
         for (int rowIdx = clipper.DisplayStart; rowIdx < clipper.DisplayEnd; rowIdx++)
         {
            const FieldSearchEntry& entry = *matches[rowIdx];
            ImGui::PushID(entry.name.c_str());

            const bool isFav = gBrowserFavorites.IsFavoriteFieldPreset(entry.name);
            const float availW = ImGui::GetContentRegionAvail().x;
            const float badgeReserve = 20.0f;
            const float catTagReserve = 70.0f;

            const std::string rowLabel = TruncateWithEllipsis(entry.name, std::max(20.0f, availW - badgeReserve - catTagReserve));
            if (ImGui::Selectable(rowLabel.c_str(), false, 0, ImVec2(availW, 0)))
            {
               const ImVec2 spawnPos = FindFreeSpawnPosition(gViewCenterCanvas);
               SpawnFieldPresetNode(entry, spawnPos.x, spawnPos.y);
            }

            const ImVec2 selMin = ImGui::GetItemRectMin();
            const ImVec2 selMax = ImGui::GetItemRectMax();

            // Category badge on the right
            const float catTextW = ImGui::CalcTextSize(entry.category.c_str()).x;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddText(ImVec2(selMax.x - badgeReserve - catTextW - 6.0f, selMin.y + (selMax.y - selMin.y - ImGui::GetTextLineHeight()) * 0.5f),
                        ImGui::GetColorU32(ImGuiCol_TextDisabled), entry.category.c_str());

            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f))
            {
               gSampleDragActive = true;
               gSampleDragKind = LibraryDragKind::FieldPreset;
               gSampleDragPath.clear();
               gSampleDragName = entry.name;
               gFieldDragPresetName = entry.name;
               gFieldDragNodeType = entry.nodeType;
               gFieldDragNodeCategory = entry.nodeCategory;
               gFieldDragIndex = entry.presetIndex;
            }

            DrawFavoriteBadge(selMin, selMax, isFav);

            if (ImGui::BeginPopupContextItem("##field_ctx"))
            {
               if (ImGui::MenuItem(isFav ? "Remove from favourites" : "Add to favourites"))
                  gBrowserFavorites.ToggleFieldPreset(entry.name);
               if (ImGui::MenuItem("Add to canvas"))
               {
                  const ImVec2 spawnPos = FindFreeSpawnPosition(gViewCenterCanvas);
                  SpawnFieldPresetNode(entry, spawnPos.x, spawnPos.y);
               }
               ImGui::EndPopup();
            }

            ImGui::PopID();
         }
      }

      ImGui::EndChild();
      ImGui::PopID();
   }
}
