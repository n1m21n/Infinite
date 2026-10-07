// Dropdowns, drop handling, browser filter strip + favourites, discrete param slots (moved verbatim from main.cpp).
#include "app/AppShared.h"

namespace app
{
   // Lowercases and strips accents so the dropdown search finds "cafe" from "café". Covers the
   // Latin-1 supplement and Latin Extended-A (U+00C0..U+017F), which is every accented letter
   // that shows up in node, device and preset names; anything else passes through unchanged.
   std::string FoldForSearch(const std::string& in)
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

   // A user picked row `i` in the dropdown popup. One click is one undo entry
   // holding the pre-click state, and this is it. The onSelect lambdas are
   // shared with the modulation-driven path (which suppresses checkpoints),
   // so most of them open with their own PushUndoCheckpoint() - suppressed
   // here too, or every pick left a second, identical entry and the next
   // Undo did nothing. Split out of the popup so INFINITE_MODDROPDOWNUNDOTEST
   // can commit a pick through exactly the code a click runs.
   void CommitDropdownPick(int i)
   {
      if (!gDropdown.onSelect || i == gDropdown.current)
         return;
      PushUndoCheckpoint();
      const bool wasSuppressed = gSuppressUndoCheckpoints;
      gSuppressUndoCheckpoints = true;
      gDropdown.onSelect(i);
      gSuppressUndoCheckpoints = wasSuppressed;
   }
   bool DropdownTestWantsOpen(bool registered, int nodeIndex, int paramIndex)
   {
      if (!registered || gDropdownTestOpenKey.first < 0 ||
          gDropdownTestOpenKey != std::pair<int, int>(nodeIndex, paramIndex))
         return false;
      gDropdownTestOpenKey = std::pair<int, int>(-1, -1);
      return true;
   }

   const FieldDeviceLibraryCache& GetFieldDeviceLibrary(const std::string& domain)
   {
      FieldDeviceLibraryCache& cache = gFieldDeviceLibrary[domain];
      if (!cache.scanned)
      {
         cache.scanned = true;
         const std::string dir = AppPaths::AppSupportDir() + "/Devices/" + domain + "/";
         std::error_code ec;
         if (std::filesystem::exists(dir, ec) && !ec)
         {
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
            {
               if (ec) break;
               if (!entry.is_regular_file())
                  continue;
                if (entry.path().extension() == ".field" || entry.path().extension() == ".infdev")
                {
                   cache.names.push_back(entry.path().stem().string());
                   cache.paths.push_back(entry.path().string());
                }
            }
         }
      }
      return cache;
   }

   void InvalidateFieldDeviceLibrary(const std::string& domain)
   {
      gFieldDeviceLibrary.erase(domain);
   }

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

   void OnFilesDropped(GLFWwindow* window, int count, const char** paths)
   {
      double xpos = 0.0, ypos = 0.0;
      if (window != nullptr)
      {
         glfwGetCursorPos(window, &xpos, &ypos);
         // Window units -> ImGui points (Windows/X11 lay ImGui out in points, core/UiScale.h).
         const float pointScale = ImGui_ImplGlfw_GetPointScale();
         gDropPos = ImVec2((float)xpos / pointScale, (float)ypos / pointScale);
         ImGuiIO& io = ImGui::GetIO();
         io.MousePos = gDropPos;
      }
      else
      {
         gDropPos = ImGui::GetMousePos();
      }
      for (int i = 0; i < count; i++)
         gDroppedFiles.push_back(paths[i]);
   }

   void DropdownButton(const char* label, const std::vector<std::string>& options,
                       int current, std::function<void(int)> onSelect, float width,
                       bool showCaption);

   // Search box + sort dropdown + type-filter dropdown for one mode of the
   // docked node-browser panel (see BrowserFilterState above). `sortNames`
   // and `typeNames` are that mode's own option lists; an empty `typeNames`
   // hides the type control entirely rather than showing a one-option
   // dropdown (Plugins does this when VST3 support isn't compiled in).
   //
   // Two rows: the search box full width on its own row (already the
   // convention every mode used before this), sort and type sharing a
   // second row at half width each - derived from the content region the
   // same way the mode tab row above derives its quarter-width, not
   // hardcoded. The ascending/descending toggle is a small arrow button
   // appended to the sort dropdown rather than a third dropdown, to keep
   // the strip to two rows.
   //
   // Returns true on any frame the query changed, or (one frame later,
   // since DropdownButton's own choice lands through the global gDropdown
   // popup rendered at the end of the frame) the sort/type/direction
   // changed. Advisory only - each mode's own cache rebuild condition
   // compares the state fields directly and is the real source of truth.
   bool DrawBrowserFilterStrip(BrowserFilterState& state,
                               const char* searchHint,
                               const std::vector<std::string>& sortNames,
                               const std::vector<std::string>& typeNames)
   {
      const BrowserFilterState before = state;

      ImGui::SetNextItemWidth(-1.0f);
      ImGui::InputTextWithHint("##browserquery", searchHint, state.query, sizeof(state.query));

      const float spacing = ImGui::GetStyle().ItemSpacing.x;
      const float avail = ImGui::GetContentRegionAvail().x;
      const float arrowW = ImGui::GetFrameHeight();
      // No trailing "sort"/"type" caption on these two (showCaption=false
      // below), so the remaining width just splits between the two
      // dropdowns and the direction-arrow button.
      const float remaining = avail - arrowW - 2.0f * spacing;
      const float typeW = typeNames.empty() ? 0.0f : std::max(40.0f, remaining * 0.5f);
      const float sortW = typeNames.empty() ? std::max(40.0f, remaining) : std::max(40.0f, remaining - typeW);

      DropdownButton("sort", sortNames, state.sortMode, [&state](int i) { state.sortMode = i; }, sortW, false);

      ImGui::SameLine();
      // Drawn as a vector triangle on the button, not a Unicode arrow
      // character - see the play/pause button's comment in
      // DrawLibrarySearchPanel: the UI font has no glyph range beyond Basic
      // Latin, so U+2191/U+2193 here rendered as a literal '?'.
      const bool dirClicked = ImGui::Button("##sortdir", ImVec2(arrowW, 0));
      {
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const ImVec2 bmin = ImGui::GetItemRectMin();
         const ImVec2 bmax = ImGui::GetItemRectMax();
         const ImVec2 center((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
         const float iconSize = (bmax.y - bmin.y) * 0.75f;
         const ImU32 col = ImGui::IsItemHovered() ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled);
         if (state.descending)
            Tabler::DrawChevronDown(dl, center, iconSize, col);
         else
            Tabler::DrawChevronUp(dl, center, iconSize, col);
      }
      if (dirClicked)
         state.descending = !state.descending;

      if (!typeNames.empty())
      {
         ImGui::SameLine();
         DropdownButton("type", typeNames, state.typeFilter, [&state](int i) { state.typeFilter = i; }, typeW, false);
      }

      return strcmp(before.query, state.query) != 0 || before.sortMode != state.sortMode ||
             before.typeFilter != state.typeFilter || before.descending != state.descending;
   }

   // Persisted sort/filter selections for the four browser panel modes -
   // NOT the free-typed query text, which (like the theme preset) is a
   // per-session choice, not a saved preference. Mirrors
   // CategoryColors::ThemePath()/LoadPreference(): one flat file next to the
   // app's other Application Support state, not a bundled settings format.
   // Four modes worth of three ints is a handful of numbers, so they share
   // one file rather than four.
   std::string BrowserFilterPrefsPath()
   {
      const std::string dir = AppPaths::AppSupportDir();
      return dir.empty() ? std::string() : dir + "/Infinite.browserfilters";
   }

   void LoadBrowserFilterPrefs()
   {
      const std::string path = BrowserFilterPrefsPath();
      if (path.empty())
         return;
      std::ifstream file(path);
      if (!file)
         return;
      BrowserFilterState* states[5] = { &gModulesFilter, &gSampleFilter, &gMediaFilter, &gPluginFilter, &gFieldFilter };
      for (int i = 0; i < 5; i++)
      {
         int sortMode = 0, typeFilter = 0, descending = 0;
         if (!(file >> sortMode >> typeFilter >> descending))
            break;
         states[i]->sortMode = sortMode;
         states[i]->typeFilter = typeFilter;
         states[i]->descending = (descending != 0);
      }
   }

   void SaveBrowserFilterPrefs()
   {
      const std::string path = BrowserFilterPrefsPath();
      if (path.empty())
         return;
      std::ofstream file(path);
      if (!file)
         return;
      const BrowserFilterState* states[5] = { &gModulesFilter, &gSampleFilter, &gMediaFilter, &gPluginFilter, &gFieldFilter };
      for (int i = 0; i < 5; i++)
         file << states[i]->sortMode << " " << states[i]->typeFilter << " " << (states[i]->descending ? 1 : 0) << "\n";
   }

   // Draws a small yellow Tabler star icon on the right side of a row when favorited.
   // Pure draw list rendering; no interactive buttons or cursor offsets so ImGuiListClipper
   // row height is never disturbed.
   void DrawFavoriteBadge(const ImVec2& itemMin, const ImVec2& itemMax, bool isFav)
   {
      if (!isFav)
         return;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const float starSize = 11.0f;
      const ImVec2 center(itemMax.x - 12.0f, (itemMin.y + itemMax.y) * 0.5f);
      const ImU32 starCol = IM_COL32(255, 205, 45, 255);
      Tabler::DrawStar(dl, center, starSize, starCol, /*filled=*/true);
   }

   // Truncates a row label to fit maxWidth, appending "..." when it doesn't -
   // browser panel rows (long sample/plugin/module names) otherwise just get
   // clipped mid-character by the window edge with no visual cue there's more
   // text. Measures with ImGui::CalcTextSize, so must run while the row's font
   // is active. Byte-length binary search, not a UTF-8-aware one - matches
   // how the rest of this file's string helpers (e.g. ILess) already treat
   // these names as plain byte strings.
   std::string TruncateWithEllipsis(const std::string& label, float maxWidth)
   {
      if (ImGui::CalcTextSize(label.c_str()).x <= maxWidth)
         return label;
      const char* kEllipsis = "...";
      const float ellipsisW = ImGui::CalcTextSize(kEllipsis).x;
      if (maxWidth <= ellipsisW)
         return kEllipsis;
      int lo = 0, hi = (int)label.size();
      while (lo < hi)
      {
         const int mid = (lo + hi + 1) / 2;
         if (ImGui::CalcTextSize(label.substr(0, mid).c_str()).x + ellipsisW <= maxWidth)
            lo = mid;
         else
            hi = mid - 1;
      }
      return label.substr(0, lo) + kEllipsis;
   }
   const int kDiscreteParamSlots = 400;

   int DiscreteParamSlot(int nodeIndex, const std::string& rawLabel)
   {
      const std::string label = gDiscreteSlotScope.empty() ? rawLabel
                                                           : rawLabel + gDiscreteSlotScope;
      const std::pair<int, std::string> key(nodeIndex, label);
      const auto cached = gDiscreteSlotByLabel.find(key);
      if (cached != gDiscreteSlotByLabel.end())
         return cached->second;

      // Rename-compatibility shim: map renamed discrete parameter labels to their
      // historical string so FNV-1a hash ordinal doesn't change and existing patches stay valid.
      static const std::unordered_map<std::string, std::string> kRenameAliases = {
         { "Global Scale##midiSnap", "Snap to Key##midiSnap" },
         { "Global Scale##transposeSnap", "Snap to Key##transposeSnap" },
         { "Global Scale##arpSnap", "Snap to Key##arpSnap" },
         { "Global Scale##seqSnap", "Snap to Key##seqSnap" },
         { "Global Scale##stackSnap", "Snap to Key##stackSnap" },
         // The EQ's five band `type` dropdowns used to be one control whose
         // target followed `selectedBand` (see DrawEqBody). Splitting them
         // into five separately-bindable dropdowns gave each its own label,
         // and so its own slot - band 1's is aliased back to the bare "type"
         // the single dropdown used, so a binding saved against it still
         // resolves to a real control.
         { "band 1 type##eqType1", "type" },
      };

      const std::string* hashStr = &label;
      auto aliasIt = kRenameAliases.find(label);
      if (aliasIt != kRenameAliases.end())
         hashStr = &aliasIt->second;

      // FNV-1a, so the same label lands on the same slot in every run and a
      // saved patch's bindings still resolve after a reload. Probing only
      // happens on a real collision within one node.
      uint32_t h = 2166136261u;
      for (const char c : *hashStr)
      {
         h ^= (uint8_t)c;
         h *= 16777619u;
      }
      const int start = (int)(h % (uint32_t)kDiscreteParamSlots);
      for (int probe = 0; probe < kDiscreteParamSlots; probe++)
      {
         const int cand = kDiscreteParamBase + (start + probe) % kDiscreteParamSlots;
         if (gDiscreteLabelBySlot.find({ nodeIndex, cand }) == gDiscreteLabelBySlot.end())
         {
            gDiscreteLabelBySlot[{ nodeIndex, cand }] = label;
            gDiscreteSlotByLabel[key] = cand;
            return cand;
         }
      }
      return kDiscreteParamBase; // 400 discrete params in one node: not a real case
   }

   // A node index is being reused (node deleted, patch cleared): drop its slot
   // assignments so the next occupant hashes fresh instead of inheriting them.
   void ForgetDiscreteSlots(int nodeIndex)
   {
      for (auto it = gDiscreteSlotByLabel.begin(); it != gDiscreteSlotByLabel.end();)
         it = (it->first.first == nodeIndex) ? gDiscreteSlotByLabel.erase(it) : std::next(it);
      for (auto it = gDiscreteLabelBySlot.begin(); it != gDiscreteLabelBySlot.end();)
         it = (it->first.first == nodeIndex) ? gDiscreteLabelBySlot.erase(it) : std::next(it);
   }

   void ForgetAllDiscreteSlots()
   {
      gDiscreteSlotByLabel.clear();
      gDiscreteLabelBySlot.clear();
   }
   // Defined further down, once the gNodes lookup exists.
   IPaletteSource* PaletteSourceByIndex(int nodeIndex);
   std::string TrimCopy(const std::string& s);
   bool TypedTextIsUntouchedSeed(const std::pair<int, int>& key, const std::string& trimmed)
   {
      auto it = gTypedParamSeed.find(key);
      const bool untouched = it != gTypedParamSeed.end() && TrimCopy(it->second) == trimmed;
      gTypedParamSeed.erase(key);
      return untouched;
   }

   std::string TrimCopy(const std::string& s)
   {
      size_t start = s.find_first_not_of(" \t");
      if (start == std::string::npos)
         return std::string();
      size_t end = s.find_last_not_of(" \t");
      return s.substr(start, end - start + 1);
   }
}
