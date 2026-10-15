// Part of src/main.cpp, included there in order (one translation unit).
// sample / media library and plugin browser panels

   // ---- browser favourites / sort / filter (Turbo port of upstream's idea) ----
   // One flat file next to the app's other preferences, one section per
   // browser mode: modules by registry name, samples/media by path, plugins
   // by identifier.
   struct BrowserFavorites
   {
      std::set<std::string> modules, samples, media, plugins;
      uint64_t version = 1;
      bool loaded = false;

      static std::string Path()
      {
         const std::string dir = InfiniteSettingsDirectory();
         return dir.empty() ? std::string() : dir + "/Infinite.browserfavorites";
      }
      void EnsureLoaded()
      {
         if (loaded)
            return;
         loaded = true;
         std::ifstream file(Path());
         std::string line, section;
         while (file && std::getline(file, line))
         {
            if (!line.empty() && line.back() == '\r')
               line.pop_back();
            if (line.empty() || line[0] == '#')
               continue;
            if (line.front() == '[' && line.back() == ']')
            {
               section = line.substr(1, line.size() - 2);
               continue;
            }
            if (std::set<std::string>* set = Set(section))
               set->insert(line);
         }
         version++;
      }
      std::set<std::string>* Set(const std::string& section)
      {
         if (section == "modules") return &modules;
         if (section == "samples") return &samples;
         if (section == "media") return &media;
         if (section == "plugins") return &plugins;
         return nullptr;
      }
      bool Is(const char* section, const std::string& key)
      {
         EnsureLoaded();
         const std::set<std::string>* set = Set(section);
         return set != nullptr && set->count(key) > 0;
      }
      void Toggle(const char* section, const std::string& key)
      {
         EnsureLoaded();
         std::set<std::string>* set = Set(section);
         if (set == nullptr)
            return;
         if (!set->erase(key))
            set->insert(key);
         version++;
         std::ofstream file(Path());
         if (!file)
            return;
         const char* names[4] = { "modules", "samples", "media", "plugins" };
         for (const char* n : names)
         {
            file << "[" << n << "]\n";
            for (const std::string& k : *Set(n))
               file << k << "\n";
         }
      }
   };
   BrowserFavorites gBrowserFavorites;

   // Small star toggle drawn as vector shapes (the UI font has no star glyph).
   bool DrawFavoriteStar(const char* id, bool isFav, float size)
   {
      const bool pressed = ImGui::InvisibleButton(id, ImVec2(size, size));
      const bool hovered = ImGui::IsItemHovered();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
      const ImVec2 c((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
      const float ro = size * 0.46f, ri = ro * 0.45f;
      ImVec2 pts[10];
      for (int i = 0; i < 10; i++)
      {
         const float a = -1.5707963f + (float)i * 0.62831853f;
         const float r = (i % 2 == 0) ? ro : ri;
         pts[i] = ImVec2(c.x + cosf(a) * r, c.y + sinf(a) * r);
      }
      ImU32 col;
      if (isFav)
         col = hovered ? IM_COL32(255, 225, 75, 255) : IM_COL32(255, 200, 40, 255);
      else if (hovered)
         col = IM_COL32(255, 215, 80, 220);
      else
      {
         ImVec4 v = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
         v.w *= 0.6f;
         col = ImGui::GetColorU32(v);
      }
      if (isFav)
         for (int i = 0; i < 10; i++)
            dl->AddTriangleFilled(c, pts[i], pts[(i + 1) % 10], col);
      else
         dl->AddPolyline(pts, 10, col, ImDrawFlags_Closed, 1.2f);
      if (hovered)
         ImGui::SetTooltip("%s", isFav ? "Remove from favourites" : "Add to favourites");
      return pressed;
   }

   // Filter / sort row shared by the Samples, Media and Plugins modes.
   struct BrowserListOptions
   {
      int filter = 0;   // index into the mode's filter names (0 = All, 1 = Favourites)
      int sort = 0;     // 0 name, 1 type, 2 folder, 3 favourites first
      bool descending = false;
   };

   bool DrawBrowserListOptions(BrowserListOptions& o, const std::vector<std::string>& filterNames,
                               const std::vector<std::string>& sortNames)
   {
      bool changed = false;
      const float avail = ImGui::GetContentRegionAvail().x;
      const float arrowW = ImGui::GetFrameHeight();
      const float half = std::max(60.0f, (avail - arrowW - ImGui::GetStyle().ItemSpacing.x * 2.0f) * 0.5f);
      ImGui::SetNextItemWidth(half);
      if (ImGui::BeginCombo("##browserfilter", filterNames[std::clamp(o.filter, 0, (int)filterNames.size() - 1)].c_str()))
      {
         for (int i = 0; i < (int)filterNames.size(); i++)
            if (ImGui::Selectable(filterNames[i].c_str(), o.filter == i))
            {
               o.filter = i;
               changed = true;
            }
         ImGui::EndCombo();
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("show");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(half);
      if (ImGui::BeginCombo("##browsersort", sortNames[std::clamp(o.sort, 0, (int)sortNames.size() - 1)].c_str()))
      {
         for (int i = 0; i < (int)sortNames.size(); i++)
            if (ImGui::Selectable(sortNames[i].c_str(), o.sort == i))
            {
               o.sort = i;
               changed = true;
            }
         ImGui::EndCombo();
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("sort by");
      ImGui::SameLine();
      if (ImGui::Button(o.descending ? "v##sortdir" : "^##sortdir", ImVec2(arrowW, 0)))
      {
         o.descending = !o.descending;
         changed = true;
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("%s", o.descending ? "descending" : "ascending");
      return changed;
   }

   const std::vector<std::string>& SampleFilterNames()
   {
      static const std::vector<std::string> v = { "All", "Favourites", "WAV", "AIFF", "FLAC", "MP3", "M4A", "OGG" };
      return v;
   }
   const std::vector<std::string>& MediaFilterNames()
   {
      static const std::vector<std::string> v = { "All", "Favourites", "Video", "Image" };
      return v;
   }
   const std::vector<std::string>& LibrarySortNames()
   {
      static const std::vector<std::string> v = { "Name", "Type", "Folder", "Favourites first" };
      return v;
   }

   std::string LibraryEntryExt(const SampleScanner::Entry& e)
   {
      const size_t dot = e.fileNameLower.find_last_of('.');
      return dot == std::string::npos ? std::string() : e.fileNameLower.substr(dot + 1);
   }

   bool LibraryEntryPasses(const SampleScanner::Entry& e, int filter, bool mediaKind)
   {
      if (filter == 0)
         return true;
      if (filter == 1)
         return gBrowserFavorites.Is(mediaKind ? "media" : "samples", e.path);
      if (mediaKind)
      {
         const bool video = HasExtension(e.path, kVideoExt);
         return filter == 2 ? video : !video;
      }
      const std::string ext = LibraryEntryExt(e);
      switch (filter)
      {
         case 2: return ext == "wav";
         case 3: return ext == "aif" || ext == "aiff";
         case 4: return ext == "flac";
         case 5: return ext == "mp3";
         case 6: return ext == "m4a";
         case 7: return ext == "ogg" || ext == "opus";
         default: return true;
      }
   }

   // Turbo 0.49: the library as a folder tree, rebuilt from the scanner's
   // index whenever it changes, so a kit's files show together under their
   // folder instead of one flat list. Keys are PathKey(root) + "|" +
   // PathKey(relative folder); the root itself has an empty relative part.
   struct LibFolderNode
   {
      std::string key, parentKey;
      std::string root;      // library root this folder lives under
      std::string rel;       // '/'-separated, "" for the root itself
      std::string name;      // display name (a root: its label or folder name)
      std::string nameLower;
      std::string absPath;
      std::vector<std::string> children;               // keys, sorted by name
      std::vector<const SampleScanner::Entry*> files;  // files directly in it
      int total = 0;                                   // files here and below
   };

   struct LibFolderTree
   {
      uint64_t version = 0;
      std::unordered_map<std::string, LibFolderNode> folders;
      std::vector<std::string> roots; // keys, in the user's order

      const LibFolderNode* Find(const std::string& key) const
      {
         auto it = folders.find(key);
         return it == folders.end() ? nullptr : &it->second;
      }
   };

   std::string LibFolderKey(const std::string& rootKey, const std::string& rel)
   {
      return rootKey + "|" + SampleScanner::PathKey(rel);
   }

   std::string LibLower(std::string s)
   {
      std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
      return s;
   }

   int LibFolderTotal(LibFolderTree& t, const std::string& key)
   {
      auto it = t.folders.find(key);
      if (it == t.folders.end())
         return 0;
      int sum = (int)it->second.files.size();
      const std::vector<std::string> children = it->second.children;
      for (const std::string& c : children)
         sum += LibFolderTotal(t, c);
      t.folders[key].total = sum;
      return sum;
   }

   void RebuildLibFolderTree(LibFolderTree& t, const SampleScanner& scanner)
   {
      t.folders.clear();
      t.roots.clear();
#ifdef _WIN32
      const char sep = '\\';
#else
      const char sep = '/';
#endif
      std::unordered_map<std::string, std::string> rootKeyOf; // root spelling -> PathKey
      for (const std::string& r : scanner.Folders())
      {
         const std::string rk = SampleScanner::PathKey(r);
         const std::string key = LibFolderKey(rk, "");
         if (t.folders.count(key) > 0)
            continue;
         LibFolderNode& n = t.folders[key];
         n.key = key;
         n.root = r;
         n.name = scanner.FolderDisplayName(r);
         n.nameLower = LibLower(n.name);
         n.absPath = r;
         t.roots.push_back(key);
         rootKeyOf[r] = rk;
      }

      // Scan order groups a folder's files together, so the last leaf is
      // reused until the folder changes (map element pointers stay valid
      // across inserts).
      LibFolderNode* last = nullptr;
      const SampleScanner::Entry* lastEntry = nullptr;
      for (const SampleScanner::Entry& e : scanner.Index())
      {
         if (last == nullptr || lastEntry == nullptr || lastEntry->folderRoot != e.folderRoot || lastEntry->relDir != e.relDir)
         {
            last = nullptr;
            auto rit = rootKeyOf.find(e.folderRoot);
            if (rit == rootKeyOf.end())
               continue;
            LibFolderNode* node = &t.folders[LibFolderKey(rit->second, "")];
            size_t pos = 0;
            while (pos < e.relDir.size())
            {
               size_t slash = e.relDir.find('/', pos);
               if (slash == std::string::npos)
                  slash = e.relDir.size();
               const std::string rel = e.relDir.substr(0, slash);
               const std::string key = LibFolderKey(rit->second, rel);
               auto [cit, inserted] = t.folders.try_emplace(key);
               LibFolderNode& child = cit->second;
               if (inserted)
               {
                  child.key = key;
                  child.parentKey = node->key;
                  child.root = e.folderRoot;
                  child.rel = rel;
                  child.name = e.relDir.substr(pos, slash - pos);
                  child.nameLower = LibLower(child.name);
                  std::string relNative = rel;
                  std::replace(relNative.begin(), relNative.end(), '/', sep);
                  child.absPath = e.folderRoot;
                  if (!child.absPath.empty() && child.absPath.back() != '/' && child.absPath.back() != '\\')
                     child.absPath += sep;
                  child.absPath += relNative;
                  node->children.push_back(key);
               }
               node = &child;
               pos = slash + 1;
            }
            last = node;
            lastEntry = &e;
         }
         last->files.push_back(&e);
      }

      for (auto& [key, node] : t.folders)
         std::sort(node.children.begin(), node.children.end(), [&t](const std::string& a, const std::string& b) {
            return t.folders.at(a).nameLower < t.folders.at(b).nameLower;
         });
      for (const std::string& r : t.roots)
         LibFolderTotal(t, r);
      t.version = scanner.IndexVersion();
   }

   // The audio files a folder drag loads, in name order: the folder's own
   // files, or (a folder holding only subfolders) every file below it.
   std::vector<std::string> LibFolderDragFiles(const LibFolderTree& t, const LibFolderNode& folder)
   {
      std::vector<const SampleScanner::Entry*> picked = folder.files;
      if (picked.empty())
      {
         std::vector<const LibFolderNode*> stack { &folder };
         while (!stack.empty())
         {
            const LibFolderNode* n = stack.back();
            stack.pop_back();
            picked.insert(picked.end(), n->files.begin(), n->files.end());
            for (const std::string& c : n->children)
               if (const LibFolderNode* cn = t.Find(c))
                  stack.push_back(cn);
         }
      }
      std::stable_sort(picked.begin(), picked.end(), [](const SampleScanner::Entry* a, const SampleScanner::Entry* b) {
         const std::string ka = LibLower(a->relDir) + "/" + a->fileNameLower;
         const std::string kb = LibLower(b->relDir) + "/" + b->fileNameLower;
         return ka < kb;
      });
      std::vector<std::string> out;
      out.reserve(picked.size());
      for (const SampleScanner::Entry* e : picked)
         out.push_back(e->path);
      return out;
   }

   // A small folder glyph drawn as shapes (the UI font has Basic Latin only).
   void DrawLibFolderIcon(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 col)
   {
      const float w = mx.x - mn.x, h = mx.y - mn.y;
      const float s = std::min(w, h * 1.25f);
      const ImVec2 o(mn.x + (w - s) * 0.5f, mn.y + (h - s * 0.8f) * 0.5f);
      const float bh = s * 0.8f;
      dl->AddRectFilled(ImVec2(o.x, o.y), ImVec2(o.x + s * 0.45f, o.y + bh * 0.3f), col, s * 0.08f);
      dl->AddRectFilled(ImVec2(o.x, o.y + bh * 0.18f), ImVec2(o.x + s, o.y + bh), col, s * 0.1f);
   }

   void DrawLibrarySearchPanel(SampleScanner& scanner, const char* idPrefix, const char* searchHint, bool mediaKind)
   {
      scanner.PollResults();

      ImGui::PushID(idPrefix);

      // Per-mode browsing state: the folder on show ("" = the list of
      // library roots) and the root being renamed.
      struct LibraryBrowseState
      {
         std::string folderKey;
         std::string renameRoot;
         char renameBuf[128] = "";
         bool openRoots = false; // open the folders list (rename from a row menu)
         LibFolderTree tree;
      };
      static LibraryBrowseState sSampleBrowse, sMediaBrowse;
      LibraryBrowseState& browse = mediaKind ? sMediaBrowse : sSampleBrowse;
      if (browse.tree.version != scanner.IndexVersion())
         RebuildLibFolderTree(browse.tree, scanner);
      const LibFolderTree& tree = browse.tree;

      const bool scanning = scanner.IsScanning();
      {
         const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
         if (ImGui::Button("Add folder...", ImVec2(half, 0)))
         {
            SampleScanner* target = &scanner;
            StartFileDialog([] { return Platform::OpenFolderDialog("Add a library folder"); },
                            [target](const std::string& path) { target->AddFolder(path); });
         }
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Add a folder to the library (its subfolders are included)");
         ImGui::SameLine();
         if (scanning)
            ImGui::BeginDisabled();
         if (ImGui::Button("Rescan", ImVec2(-1.0f, 0)))
            scanner.StartScan();
         if (scanning)
            ImGui::EndDisabled();
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Scan every library folder again (runs in the background)");
         if (scanning)
            ImGui::TextDisabled("scanning... (%d found)", scanner.FilesFoundSoFar());
      }

      // Library roots: label, rename, remove. Collapsed by default, the
      // browser below is what the panel is for.
      // Removal waits for the end of the panel: it erases index entries
      // the rows below point at.
      std::string rootToRemove, rootToBrowse;
      char rootsHeader[64];
      snprintf(rootsHeader, sizeof(rootsHeader), "Library folders (%d)###libroots", (int)scanner.Folders().size());
      if (browse.openRoots)
      {
         ImGui::SetNextItemOpen(true, ImGuiCond_Always);
         browse.openRoots = false;
      }
      if (ImGui::TreeNodeEx(rootsHeader, ImGuiTreeNodeFlags_SpanAvailWidth))
      {
         const float frameH = ImGui::GetFrameHeight();
         for (const std::string& root : scanner.Folders())
         {
            ImGui::PushID(root.c_str());
            const float avail = ImGui::GetContentRegionAvail().x;
            const float btnW = ImGui::CalcTextSize("Rename").x + ImGui::GetStyle().FramePadding.x * 2.0f;
            const float nameW = std::max(20.0f, avail - btnW - frameH - ImGui::GetStyle().ItemSpacing.x * 2.0f);
            if (browse.renameRoot == root)
            {
               ImGui::SetNextItemWidth(nameW);
               if (ImGui::IsWindowAppearing() || !ImGui::IsAnyItemActive())
                  ImGui::SetKeyboardFocusHere();
               const bool enter = ImGui::InputTextWithHint("##rename", "label (empty = folder name)", browse.renameBuf,
                                                           sizeof(browse.renameBuf), ImGuiInputTextFlags_EnterReturnsTrue);
               if (enter || ImGui::IsItemDeactivated())
               {
                  if (!ImGui::IsKeyPressed(ImGuiKey_Escape))
                     scanner.SetFolderLabel(root, browse.renameBuf);
                  browse.renameRoot.clear();
               }
            }
            else
            {
               const std::string name = scanner.FolderDisplayName(root);
               if (ImGui::Selectable(name.c_str(), false, 0, ImVec2(nameW, 0)))
                  rootToBrowse = root;
               if (ImGui::IsItemHovered())
                  ImGui::SetTooltip("%s\nclick to browse", root.c_str());
            }
            ImGui::SameLine();
            if (ImGui::Button("Rename", ImVec2(btnW, 0)))
            {
               browse.renameRoot = root;
               snprintf(browse.renameBuf, sizeof(browse.renameBuf), "%s", scanner.FolderLabel(root).c_str());
            }
            ImGui::SameLine();
            if (ImGui::Button("x", ImVec2(frameH, 0)))
               rootToRemove = root;
            if (ImGui::IsItemHovered())
               ImGui::SetTooltip("Remove from the library (the files stay on disk)");
            ImGui::PopID();
         }
         if (scanner.Folders().empty())
            ImGui::TextDisabled("no folders yet - Add folder...");
         ImGui::TreePop();
      }

      ImGui::Separator();

      struct LibraryRow
      {
         const LibFolderNode* folder = nullptr;
         const SampleScanner::Entry* entry = nullptr;
      };
      struct LibraryFilterCache
      {
         std::string lastQuery, lastFolder;
         uint64_t lastIndexVersion = 0;
         uint64_t lastFavVersion = 0;
         int lastFilter = -1, lastSort = -1;
         bool lastDescending = false;
         std::vector<LibraryRow> rows;
         int fileCount = 0;
      };
      static LibraryFilterCache sSampleCache;
      static LibraryFilterCache sMediaCache;
      LibraryFilterCache& cache = mediaKind ? sMediaCache : sSampleCache;
      static BrowserListOptions sSampleOpts, sMediaOpts;
      BrowserListOptions& opts = mediaKind ? sMediaOpts : sSampleOpts;

      // Two independent buffers (not one shared static) so the Samples and
      // Media modes each keep their own in-progress query when the user
      // switches tabs and back.
      static char sampleSearch[128] = "";
      static char mediaSearch[128] = "";
      char* searchBuf = mediaKind ? mediaSearch : sampleSearch;
      ImGui::SetNextItemWidth(-1.0f);
      ImGui::InputTextWithHint("##librarysearch", searchHint, searchBuf, 128);
      if (ImGui::IsItemHovered() && searchBuf[0] == '\0')
         ImGui::SetTooltip("Type to search every library folder; empty shows the current folder");
      DrawBrowserListOptions(opts, mediaKind ? MediaFilterNames() : SampleFilterNames(), LibrarySortNames());

      std::string q = searchBuf;
      std::transform(q.begin(), q.end(), q.begin(), ::tolower);
      // Searching, or showing favourites, covers every root; otherwise the
      // list is the current folder.
      const bool everywhere = !q.empty() || opts.filter == 1;

      // Where we are. A single root is opened directly (no one-item list
      // of roots); a folder that vanished in a rescan falls back to home.
      if (!browse.folderKey.empty() && tree.Find(browse.folderKey) == nullptr)
         browse.folderKey.clear();
      if (browse.folderKey.empty() && tree.roots.size() == 1)
         browse.folderKey = tree.roots[0];
      const LibFolderNode* current = browse.folderKey.empty() ? nullptr : tree.Find(browse.folderKey);

      // Breadcrumb: back button, then "root / sub / sub" with each part
      // clickable. Long paths keep their tail and elide the head.
      if (!everywhere)
      {
         const float frameH = ImGui::GetFrameHeight();
         const bool canGoUp = current != nullptr && (!current->parentKey.empty() || tree.roots.size() > 1);
         if (!canGoUp)
            ImGui::BeginDisabled();
         if (ImGui::Button("<##libup", ImVec2(frameH, 0)) && current != nullptr)
            browse.folderKey = current->parentKey;
         if (!canGoUp)
            ImGui::EndDisabled();
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Up one folder");

         std::vector<const LibFolderNode*> chain;
         for (const LibFolderNode* n = current; n != nullptr; n = n->parentKey.empty() ? nullptr : tree.Find(n->parentKey))
            chain.insert(chain.begin(), n);
         struct Crumb
         {
            std::string label, key;
         };
         std::vector<Crumb> crumbs;
         if (tree.roots.size() > 1 || chain.empty())
            crumbs.push_back({ "Library", std::string() });
         for (const LibFolderNode* n : chain)
            crumbs.push_back({ n->name, n->key });

         const ImGuiStyle& st = ImGui::GetStyle();
         const float sepW = ImGui::CalcTextSize("/").x + st.ItemSpacing.x * 2.0f;
         float avail = ImGui::GetContentRegionAvail().x - frameH - st.ItemSpacing.x;
         size_t first = crumbs.size();
         float used = 0.0f;
         const float ellW = ImGui::CalcTextSize("...").x + st.ItemSpacing.x;
         while (first > 0)
         {
            const float w = ImGui::CalcTextSize(crumbs[first - 1].label.c_str()).x + st.FramePadding.x * 2.0f +
                            (first < crumbs.size() ? sepW : 0.0f);
            if (used + w > avail - (first > 1 ? ellW : 0.0f) && first < crumbs.size())
               break;
            used += w;
            first--;
         }
         ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
         if (first > 0)
         {
            ImGui::SameLine();
            ImGui::TextDisabled("...");
         }
         for (size_t i = first; i < crumbs.size(); i++)
         {
            ImGui::SameLine();
            if (i > first)
            {
               ImGui::TextDisabled("/");
               ImGui::SameLine();
            }
            ImGui::PushID((int)i);
            const bool last = (i + 1 == crumbs.size());
            if (last)
               ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
            else
               ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if (ImGui::Button(crumbs[i].label.c_str()) && !last)
               browse.folderKey = crumbs[i].key;
            ImGui::PopStyleColor();
            ImGui::PopID();
         }
         ImGui::PopStyleColor();
      }

      // The button un-latches by itself once the file finishes playing -
      // once per frame, not per row, since it's a property of the preview
      // player, not of any particular row.
      if (!mediaKind && !gPreviewingSamplePath.empty() && !AudioEngine::Instance().Preview().IsPlaying())
         gPreviewingSamplePath.clear();

      // Rows rebuilt only when something that affects the list changes
      // (0 ms when idle/scrolling). Folders first (by name), then files
      // filtered and sorted by the options row.
      gBrowserFavorites.EnsureLoaded();
      const std::string folderState = everywhere ? std::string("\x01") : browse.folderKey;
      if (cache.lastQuery != q || cache.lastIndexVersion != tree.version ||
          cache.lastFavVersion != gBrowserFavorites.version || cache.lastFilter != opts.filter ||
          cache.lastSort != opts.sort || cache.lastDescending != opts.descending || cache.lastFolder != folderState)
      {
         cache.rows.clear();
         std::vector<const SampleScanner::Entry*> files;
         if (everywhere)
         {
            // Folders whose name matches come first (a kit is often what
            // is being looked for), capped so they never bury the files.
            if (!q.empty())
            {
               std::vector<const LibFolderNode*> matches;
               for (const auto& [key, node] : tree.folders)
                  if (node.nameLower.find(q) != std::string::npos)
                     matches.push_back(&node);
               std::sort(matches.begin(), matches.end(), [](const LibFolderNode* a, const LibFolderNode* b) {
                  return a->nameLower != b->nameLower ? a->nameLower < b->nameLower : a->key < b->key;
               });
               if (matches.size() > 40)
                  matches.resize(40);
               for (const LibFolderNode* m : matches)
                  cache.rows.push_back({ m, nullptr });
            }
            const auto& index = scanner.Index();
            files.reserve(index.size());
            for (const SampleScanner::Entry& entry : index)
            {
               if (!q.empty() && entry.fileNameLower.find(q) == std::string::npos)
                  continue;
               if (!LibraryEntryPasses(entry, opts.filter, mediaKind))
                  continue;
               files.push_back(&entry);
            }
         }
         else if (current == nullptr)
         {
            for (const std::string& r : tree.roots)
               if (const LibFolderNode* rn = tree.Find(r))
                  cache.rows.push_back({ rn, nullptr });
         }
         else
         {
            for (const std::string& c : current->children)
               if (const LibFolderNode* cn = tree.Find(c))
                  cache.rows.push_back({ cn, nullptr });
            for (const SampleScanner::Entry* entry : current->files)
               if (LibraryEntryPasses(*entry, opts.filter, mediaKind))
                  files.push_back(entry);
         }

         const int sortMode = opts.sort;
         const char* favSection = mediaKind ? "media" : "samples";
         std::stable_sort(files.begin(), files.end(),
                          [sortMode, favSection](const SampleScanner::Entry* a, const SampleScanner::Entry* b) {
                             if (sortMode == 3)
                             {
                                const bool fa = gBrowserFavorites.Is(favSection, a->path);
                                const bool fb = gBrowserFavorites.Is(favSection, b->path);
                                if (fa != fb)
                                   return fa;
                             }
                             if (sortMode == 1)
                             {
                                const std::string ea = LibraryEntryExt(*a), eb = LibraryEntryExt(*b);
                                if (ea != eb)
                                   return ea < eb;
                             }
                             if (sortMode == 2)
                             {
                                const std::string da = a->path.substr(0, a->path.find_last_of("/\\"));
                                const std::string db = b->path.substr(0, b->path.find_last_of("/\\"));
                                if (da != db)
                                   return da < db;
                             }
                             if (a->fileNameLower != b->fileNameLower)
                                return a->fileNameLower < b->fileNameLower;
                             return a->path < b->path;
                          });
         if (opts.descending)
            std::reverse(files.begin(), files.end());
         cache.fileCount = (int)files.size();
         for (const SampleScanner::Entry* f : files)
            cache.rows.push_back({ nullptr, f });

         cache.lastQuery = q;
         cache.lastFolder = folderState;
         cache.lastIndexVersion = tree.version;
         cache.lastFavVersion = gBrowserFavorites.version;
         cache.lastFilter = opts.filter;
         cache.lastSort = opts.sort;
         cache.lastDescending = opts.descending;
      }

      if (everywhere)
         ImGui::TextDisabled("%d file%s in all library folders", cache.fileCount, cache.fileCount == 1 ? "" : "s");
      else if (cache.rows.empty())
         ImGui::TextDisabled(scanner.Folders().empty() ? "add a folder to start a library"
                             : scanning                ? "scanning..."
                                                       : "nothing here (try Rescan)");

      const auto& rows = cache.rows;
      std::string navigateTo;
      bool clearSearch = false;

      // "root / sub" for a search hit, so same-named files are told apart.
      auto hitFolderText = [&tree](const std::string& root, const std::string& rel) {
         const LibFolderNode* rn = tree.Find(LibFolderKey(SampleScanner::PathKey(root), ""));
         std::string s = rn != nullptr ? rn->name : root;
         if (!rel.empty())
            s += "/" + rel;
         return s;
      };

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
      // must be uniform for it to skip the rest correctly, so folder rows
      // below copy the file rows' layout (star slot, play-button slot,
      // Selectable) item for item. No explicit row-height guess - letting
      // the clipper measure the first row itself avoids even a one-pixel
      // mismatch against the real rendered height.
      ImGuiListClipper clipper;
      clipper.Begin((int)rows.size());
      while (clipper.Step())
      {
         for (int rowIdx = clipper.DisplayStart; rowIdx < clipper.DisplayEnd; rowIdx++)
         {
            if (const LibFolderNode* folder = rows[rowIdx].folder)
            {
               ImGui::PushID(("F" + folder->key).c_str());
               const float frameH = ImGui::GetFrameHeight();
               const float rowH = mediaKind ? 54.0f : frameH;
               const float starSz = frameH * 0.75f;
               const float rowY = ImGui::GetCursorPosY();
               ImGui::SetCursorPosY(rowY + (rowH - starSz) * 0.5f);
               ImGui::Dummy(ImVec2(starSz, starSz));
               ImGui::SameLine();
               ImGui::SetCursorPosY(rowY);
               const ImU32 iconCol = IM_COL32(214, 178, 92, 255);
               if (!mediaKind)
               {
                  const float btnH = frameH * 0.7f;
                  ImGui::SetCursorPosY(rowY + (rowH - btnH) * 0.5f);
                  ImGui::Dummy(ImVec2(btnH, btnH));
                  DrawLibFolderIcon(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), iconCol);
                  ImGui::SameLine();
                  ImGui::SetCursorPosY(rowY);
               }
               const bool pressed = mediaKind ? ImGui::Selectable("##folderrow", false, 0, ImVec2(0.0f, rowH))
                                              : ImGui::Selectable(("##folderrow" + folder->name).c_str());
               const ImVec2 mn = ImGui::GetItemRectMin();
               const ImVec2 mx = ImGui::GetItemRectMax();
               ImDrawList* dl = ImGui::GetWindowDrawList();
               char countText[32];
               snprintf(countText, sizeof(countText), "%d", folder->total);
               const float countW = ImGui::CalcTextSize(countText).x;
               const float textX = mediaKind ? mn.x + 73.0f : mn.x;
               if (mediaKind)
               {
                  const ImVec2 boxMin(mn.x + 3.0f, mn.y + 3.0f), boxMax(mn.x + 67.0f, mx.y - 3.0f);
                  dl->AddRectFilled(boxMin, boxMax, IM_COL32(22, 25, 34, 255), 3.0f);
                  DrawLibFolderIcon(dl, ImVec2(boxMin.x + 14.0f, boxMin.y + 10.0f), ImVec2(boxMax.x - 14.0f, boxMax.y - 10.0f), iconCol);
               }
               const float lineH = ImGui::GetTextLineHeight();
               const bool twoLines = everywhere && mediaKind;
               const float nameY = twoLines ? mn.y + rowH * 0.5f - lineH - 1.0f : mn.y + ((mx.y - mn.y) - lineH) * 0.5f;
               dl->PushClipRect(ImVec2(textX, mn.y), ImVec2(mx.x - countW - 8.0f, mx.y), true);
               dl->AddText(ImVec2(textX, nameY), ImGui::GetColorU32(ImGuiCol_Text), folder->name.c_str());
               if (everywhere && !folder->rel.empty())
               {
                  // Where the matching folder lives.
                  const size_t slash = folder->rel.find_last_of('/');
                  const std::string where =
                     hitFolderText(folder->root, slash == std::string::npos ? std::string() : folder->rel.substr(0, slash));
                  const float nameW = ImGui::CalcTextSize(folder->name.c_str()).x;
                  const ImVec2 at = twoLines ? ImVec2(textX, mn.y + rowH * 0.5f + 1.0f) : ImVec2(textX + nameW + 10.0f, nameY);
                  dl->AddText(at, ImGui::GetColorU32(ImGuiCol_TextDisabled), where.c_str());
               }
               dl->PopClipRect();
               dl->AddText(ImVec2(mx.x - countW - 4.0f, mn.y + ((mx.y - mn.y) - lineH) * 0.5f),
                           ImGui::GetColorU32(ImGuiCol_TextDisabled), countText);

               // Click opens the folder; a drag that started here is a
               // folder drag (Samples only) and must not navigate.
               if (pressed && ImGui::GetIO().MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] < 16.0f)
               {
                  navigateTo = folder->key;
                  clearSearch = everywhere;
               }
               if (!mediaKind && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f) &&
                   !gSampleDragActive)
               {
                  gSampleDragFolderFiles = LibFolderDragFiles(tree, *folder);
                  if (!gSampleDragFolderFiles.empty())
                  {
                     gSampleDragActive = true;
                     gSampleDragKind = LibraryDragKind::SampleFolder;
                     gSampleDragPath = folder->absPath;
                     gSampleDragName = folder->name + " (" + std::to_string(gSampleDragFolderFiles.size()) + " files)";
                  }
               }
               if (ImGui::IsItemHovered() && !gSampleDragActive)
               {
                  if (mediaKind)
                     ImGui::SetTooltip("%s\nclick to open", folder->absPath.c_str());
                  else
                     ImGui::SetTooltip("%s\nclick to open\ndrag onto a Drum Sequencer or MPC to load its audio files\n"
                                       "into the lanes / pads in order (by name); drop on empty canvas\n"
                                       "for a new Drum Sequencer with them",
                                       folder->absPath.c_str());
               }
               if (ImGui::BeginPopupContextItem("##libraryfoldermenu"))
               {
                  if (ImGui::MenuItem("Open"))
                     navigateTo = folder->key;
                  if (ImGui::MenuItem("Show in Explorer"))
                     Platform::RevealInFileManager(folder->absPath);
                  if (ImGui::MenuItem("Copy path"))
                     ImGui::SetClipboardText(folder->absPath.c_str());
                  if (folder->rel.empty())
                  {
                     ImGui::Separator();
                     if (ImGui::MenuItem("Rename label..."))
                     {
                        browse.renameRoot = folder->root;
                        snprintf(browse.renameBuf, sizeof(browse.renameBuf), "%s", scanner.FolderLabel(folder->root).c_str());
                        browse.openRoots = true;
                     }
                     if (ImGui::MenuItem("Remove from library"))
                        rootToRemove = folder->root;
                  }
                  ImGui::EndPopup();
               }
               ImGui::PopID();
               continue;
            }

            const SampleScanner::Entry& entry = *rows[rowIdx].entry;

            ImGui::PushID(entry.path.c_str());

         // favourite star at the head of every row
         {
            const float rowH = mediaKind ? 54.0f : ImGui::GetFrameHeight();
            const float starSz = ImGui::GetFrameHeight() * 0.75f;
            const float rowY = ImGui::GetCursorPosY();
            ImGui::SetCursorPosY(rowY + (rowH - starSz) * 0.5f);
            const char* section = mediaKind ? "media" : "samples";
            if (DrawFavoriteStar("##fav", gBrowserFavorites.Is(section, entry.path), starSz))
               gBrowserFavorites.Toggle(section, entry.path);
            ImGui::SameLine();
            ImGui::SetCursorPosY(rowY);
         }

         // Media mode has no audition - images/video get no play button, and
         // the Selectable alone keeps the same full-width layout it always
         // had there.
         if (!mediaKind)
         {
            const bool isPlaying = (gPreviewingSamplePath == entry.path);
            // Smaller than a full frame-height button - at full size the
            // icon dominated the row next to the filename text.
            const float rowH = ImGui::GetFrameHeight();
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
               ImGui::GetWindowDrawList()->AddRectFilled(rowScreenMin, rowMax, IM_COL32(80, 140, 220, 60));
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
               const ImU32 iconCol = ImGui::IsItemHovered() ? IM_COL32(255, 255, 255, 255) : IM_COL32(215, 218, 228, 255);
               if (isPlaying)
               {
                  const float barW = btnH * 0.13f;
                  const float barH = btnH * 0.38f;
                  const float gapX = btnH * 0.09f;
                  dl->AddRectFilled(ImVec2(center.x - gapX - barW, center.y - barH * 0.5f),
                                     ImVec2(center.x - gapX, center.y + barH * 0.5f), iconCol, 1.0f);
                  dl->AddRectFilled(ImVec2(center.x + gapX, center.y - barH * 0.5f),
                                     ImVec2(center.x + gapX + barW, center.y + barH * 0.5f), iconCol, 1.0f);
               }
               else
               {
                  const float h = btnH * 0.36f;
                  dl->AddTriangleFilled(ImVec2(center.x - h * 0.5f, center.y - h),
                                          ImVec2(center.x - h * 0.5f, center.y + h),
                                          ImVec2(center.x + h * 0.85f, center.y), iconCol);
               }
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
         if (mediaKind)
         {
            const float rowH = 54.0f;
            ImGui::Selectable("##mediarow", false, ImGuiSelectableFlags_None, ImVec2(0.0f, rowH));
            const ImVec2 mn = ImGui::GetItemRectMin();
            const ImVec2 mx = ImGui::GetItemRectMax();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 boxMin(mn.x + 3.0f, mn.y + 3.0f);
            const ImVec2 boxMax(mn.x + 67.0f, mx.y - 3.0f);
            dl->AddRectFilled(boxMin, boxMax, IM_COL32(22, 25, 34, 255), 3.0f);

            MediaThumbnail* thumb = ImGui::IsItemVisible() ? GetMediaThumbnail(entry.path) : nullptr;
            if (thumb != nullptr && thumb->texture != 0)
            {
               const float fit = std::min((boxMax.x - boxMin.x) / thumb->width,
                                          (boxMax.y - boxMin.y) / thumb->height);
               const ImVec2 size(thumb->width * fit, thumb->height * fit);
               const ImVec2 imageMin(boxMin.x + ((boxMax.x - boxMin.x) - size.x) * 0.5f,
                                     boxMin.y + ((boxMax.y - boxMin.y) - size.y) * 0.5f);
               dl->AddImage((ImTextureID)(intptr_t)thumb->texture, imageMin,
                            ImVec2(imageMin.x + size.x, imageMin.y + size.y),
                            ImVec2(0, 1), ImVec2(1, 0));
            }
            else
            {
               const char* kind = HasExtension(entry.path, kVideoExt) ? "VID" : "IMG";
               const ImVec2 ts = ImGui::CalcTextSize(kind);
               dl->AddText(ImVec2((boxMin.x + boxMax.x - ts.x) * 0.5f,
                                  (boxMin.y + boxMax.y - ts.y) * 0.5f),
                           IM_COL32(120, 130, 155, 255), kind);
            }

            const ImVec2 textSize = ImGui::CalcTextSize(entry.fileName.c_str());
            dl->PushClipRect(ImVec2(mn.x + 73.0f, mn.y), mx, true);
            if (everywhere)
            {
               // Turbo 0.49: search hits name their folder on a second line.
               dl->AddText(ImVec2(mn.x + 73.0f, mn.y + rowH * 0.5f - textSize.y - 1.0f), ImGui::GetColorU32(ImGuiCol_Text),
                           entry.fileName.c_str());
               dl->AddText(ImVec2(mn.x + 73.0f, mn.y + rowH * 0.5f + 1.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                           hitFolderText(entry.folderRoot, entry.relDir).c_str());
            }
            else
            {
               const float textY = mn.y + (rowH - textSize.y) * 0.5f;
               dl->AddText(ImVec2(mn.x + 73.0f, textY), ImGui::GetColorU32(ImGuiCol_Text), entry.fileName.c_str());
            }
            dl->PopClipRect();
         }
         else
         {
            ImGui::Selectable(entry.fileName.c_str());
            if (everywhere)
            {
               // Turbo 0.49: search hits name their folder after the file.
               const ImVec2 mn = ImGui::GetItemRectMin();
               const ImVec2 mx = ImGui::GetItemRectMax();
               const float nameW = ImGui::CalcTextSize(entry.fileName.c_str()).x;
               ImDrawList* dl = ImGui::GetWindowDrawList();
               dl->PushClipRect(ImVec2(mn.x + nameW + 8.0f, mn.y), mx, true);
               dl->AddText(ImVec2(mn.x + nameW + 10.0f, mn.y), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                           hitFolderText(entry.folderRoot, entry.relDir).c_str());
               dl->PopClipRect();
            }
         }
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
         if (ImGui::IsItemHovered())
         {
            if (!mediaKind && gPreviewingSamplePath.empty() && gPreviewErrorPath == entry.path)
               ImGui::SetTooltip("%s", gPreviewErrorMessage.c_str());
            else
               ImGui::SetTooltip("%s", entry.path.c_str());
         }
         if (ImGui::BeginPopupContextItem("##libraryrowmenu"))
         {
            const char* section = mediaKind ? "media" : "samples";
            const bool fav = gBrowserFavorites.Is(section, entry.path);
            if (ImGui::MenuItem(fav ? "Remove from favourites" : "Add to favourites"))
               gBrowserFavorites.Toggle(section, entry.path);
            if (ImGui::MenuItem("Show in Explorer"))
               Platform::RevealInFileManager(entry.path);
            if (ImGui::MenuItem("Copy path"))
               ImGui::SetClipboardText(entry.path.c_str());
            ImGui::EndPopup();
         }

         ImGui::PopID();
         }
      }
      ImGui::EndChild();
      ImGui::PopStyleVar();

      if (!rootToBrowse.empty())
      {
         navigateTo = LibFolderKey(SampleScanner::PathKey(rootToBrowse), "");
         clearSearch = true;
      }
      if (!navigateTo.empty())
      {
         browse.folderKey = navigateTo;
         if (clearSearch)
         {
            searchBuf[0] = '\0';
            if (opts.filter == 1)
               opts.filter = 0;
         }
      }
      if (!rootToRemove.empty())
         scanner.RemoveFolder(rootToRemove);

      ImGui::PopID();
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
         for (const std::string& folder : gPluginScanner.Folders())
         {
            ImGui::TextUnformatted(folder.c_str());
            ImGui::SameLine();
            ImGui::PushID(folder.c_str());
            if (ImGui::SmallButton("Remove"))
               gPluginScanner.RemoveFolder(folder);
            ImGui::PopID();
         }
         if (ImGui::Button("Add VST3 folder...", ImVec2(-1.0f, 0)))
         {
            StartFileDialog([] { return Platform::OpenFolderDialog("Add VST3 folder"); },
                            [](const std::string& folder) { gPluginScanner.AddFolder(folder); });
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
#else
      ImGui::TextDisabled("VST3 support is not compiled into this build.");
#endif

      ImGui::Separator();

      // Its own buffer, like the Samples and Media modes each have, so
      // switching tabs and back keeps this mode's in-progress query.
      static char pluginSearch[128] = "";
      static bool sPluginSearchSeeded = false;
      if (!sPluginSearchSeeded)
      {
         sPluginSearchSeeded = true;
         if (const char* seed = getenv("INFINITE_PLUGINDRAGTEST_SEARCH"))
         {
            strncpy(pluginSearch, seed, sizeof(pluginSearch) - 1);
            pluginSearch[sizeof(pluginSearch) - 1] = '\0';
         }
      }
      ImGui::SetNextItemWidth(-1.0f);
      ImGui::InputTextWithHint("##pluginsearch", "SEARCH PLUGINS...", pluginSearch, sizeof(pluginSearch));

      static BrowserListOptions sPluginOpts;
      static const std::vector<std::string> kPluginFilters = { "All", "Favourites", "Instruments", "Effects" };
      static const std::vector<std::string> kPluginSorts = { "Name", "Maker", "Favourites first" };
      DrawBrowserListOptions(sPluginOpts, kPluginFilters, kPluginSorts);

      std::string q = pluginSearch;
      std::transform(q.begin(), q.end(), q.begin(), ::tolower);

      // filtered + sorted view (the scanner index itself stays in scan order)
      std::vector<const PluginScanner::Entry*> pluginRows;
      for (const PluginScanner::Entry& entry : gPluginScanner.Index())
      {
         if (!q.empty())
         {
            std::string hay = entry.name + " " + entry.manufacturer;
            std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
            if (hay.find(q) == std::string::npos)
               continue;
         }
         if (sPluginOpts.filter == 1 && !gBrowserFavorites.Is("plugins", entry.identifier))
            continue;
         if (sPluginOpts.filter == 2 && !entry.acceptsNotes)
            continue;
         if (sPluginOpts.filter == 3 && entry.acceptsNotes)
            continue;
         pluginRows.push_back(&entry);
      }
      {
         const int sortMode = sPluginOpts.sort;
         auto lower = [](std::string t) { std::transform(t.begin(), t.end(), t.begin(), ::tolower); return t; };
         std::stable_sort(pluginRows.begin(), pluginRows.end(),
                          [sortMode, &lower](const PluginScanner::Entry* a, const PluginScanner::Entry* b) {
                             if (sortMode == 2)
                             {
                                const bool fa = gBrowserFavorites.Is("plugins", a->identifier);
                                const bool fb = gBrowserFavorites.Is("plugins", b->identifier);
                                if (fa != fb)
                                   return fa;
                             }
                             if (sortMode == 1)
                             {
                                const std::string ma = lower(a->manufacturer), mb = lower(b->manufacturer);
                                if (ma != mb)
                                   return ma < mb;
                             }
                             return lower(a->name) < lower(b->name);
                          });
         if (sPluginOpts.descending)
            std::reverse(pluginRows.begin(), pluginRows.end());
      }

      ImGui::BeginChild("##pluginpanellist", ImVec2(0, 0), false);
      // INFINITE_PLUGINDRAGTEST captures the FIRST matching row, not the last:
      // this list is every installed effect, and the rows past the visible
      // height are drawn but clipped, so a synthetic press aimed at the last
      // one's rect would land outside the child on nothing at all. The first
      // row is always on screen. (The Samples/Media modes capture the last row
      // because their fixtures stage a folder with exactly one file in it.)
      bool testRowCaptured = false;
      for (const PluginScanner::Entry* entryPtr : pluginRows)
      {
         const PluginScanner::Entry& entry = *entryPtr;
         ImGui::PushID(entry.identifier.c_str());
         {
            const float starSz = ImGui::GetFrameHeight() * 0.75f;
            const float rowY = ImGui::GetCursorPosY();
            ImGui::SetCursorPosY(rowY + (ImGui::GetTextLineHeight() - starSz) * 0.5f);
            if (DrawFavoriteStar("##fav", gBrowserFavorites.Is("plugins", entry.identifier), starSz))
               gBrowserFavorites.Toggle("plugins", entry.identifier);
            ImGui::SameLine();
            ImGui::SetCursorPosY(rowY);
         }

         std::string label = "[" + (entry.format == "vst3" ? std::string("VST3") : std::string("AU")) +
                              "] " + entry.name;
         if (!entry.manufacturer.empty())
            label += "  -  " + entry.manufacturer;
         ImGui::Selectable(label.c_str());
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
         if (ImGui::BeginPopupContextItem("##pluginrowmenu"))
         {
            const bool fav = gBrowserFavorites.Is("plugins", entry.identifier);
            if (ImGui::MenuItem(fav ? "Remove from favourites" : "Add to favourites"))
               gBrowserFavorites.Toggle("plugins", entry.identifier);
            if (!entry.path.empty() && ImGui::MenuItem("Show in Explorer"))
               Platform::RevealInFileManager(entry.path);
            ImGui::EndPopup();
         }
         ImGui::PopID();
      }
      ImGui::EndChild();

      ImGui::PopID();
   }

   // Mirror image of Audio Out's body (a terminal with no controls at all) -
   // Audio In has exactly one control, a trim fader, drawn the same way
   // Gain's is. The stat line reports whether the mic tap is actually live
   // (Platform::AudioInputCaptureIsRunning), since silence could otherwise
   // mean either "nothing arriving" or "not listening yet".
