   // Turbo 0.61: Collect Media and portable paths.
   //
   // A node keeps a file it uses in a text param (`s` in the patch). Rather
   // than a list of path keys per node type, any text value that names an
   // existing file counts as media - so a node added later is covered too.
   //
   // Collect: every such file is copied into <patch name>_media/<type>/ next
   // to the .inf (same content once, by size + hash; a name clash with other
   // content gets _2, _3), a .gltf's own buffers and images come along, and
   // the patch is written with paths relative to its own folder. Files that
   // no longer exist are listed in the report.
   //
   // Any save: a path inside the patch's folder is written relative. Any
   // load: a relative path that exists next to the patch is made absolute
   // before the nodes read it.

   struct CollectReport
   {
      int copied = 0;
      int reused = 0;  // same content already copied
      int already = 0; // already inside the patch folder
      uint64_t bytes = 0;
      std::vector<std::string> missing;  // "Node type: path"
      std::vector<std::string> failures; // copy errors
      std::string mediaDir;
   };
   CollectReport gCollectReport;
   bool gCollectReportOpen = false;

   namespace CollectMedia
   {
      namespace fs = std::filesystem;

      std::string Lower(std::string s)
      {
         std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
         return s;
      }

      std::string TypeFolder(const fs::path& p)
      {
         const std::string e = Lower(p.extension().u8string());
         auto any = [&](std::initializer_list<const char*> list) {
            for (const char* x : list)
               if (e == x)
                  return true;
            return false;
         };
         if (any({ ".wav", ".aif", ".aiff", ".mp3", ".flac", ".ogg", ".m4a", ".opus" })) return "audio";
         if (any({ ".mp4", ".mov", ".avi", ".mkv", ".webm", ".m4v", ".mxf", ".hap" })) return "video";
         if (any({ ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif", ".webp", ".tif", ".tiff", ".exr", ".hdr", ".dds" }))
            return "images";
         if (any({ ".obj", ".gltf", ".glb", ".fbx", ".ply", ".stl", ".dae", ".usd", ".usdz", ".bin" })) return "models";
         if (any({ ".mid", ".midi" })) return "midi";
         if (any({ ".cube", ".3dl", ".lut" })) return "luts";
         if (any({ ".ttf", ".otf", ".ttc" })) return "fonts";
         if (any({ ".vstpreset", ".fxp", ".fxb", ".aupreset" })) return "presets";
         return "other";
      }

      // A text value that names an existing regular file (absolute, or relative
      // to the patch folder). Free text (labels, expressions) never matches.
      bool AsFile(const std::string& value, const fs::path& patchDir, fs::path& out)
      {
         if (value.empty() || value.size() > 1024 || value.find('\n') != std::string::npos)
            return false;
         std::error_code ec;
         fs::path p = fs::u8path(value);
         if (p.is_relative() && !patchDir.empty())
            p = patchDir / p;
         if (!fs::is_regular_file(p, ec))
            return false;
         out = fs::weakly_canonical(p, ec);
         if (ec)
            out = p;
         return true;
      }

      // Looks like a path someone meant (has a folder separator or a drive and
      // an extension) - used only to report a file that has gone missing.
      bool LooksLikePath(const std::string& v)
      {
         if (v.size() < 4 || v.size() > 1024 || v.find('\n') != std::string::npos || v[0] == '=')
            return false;
         const bool sep = v.find('/') != std::string::npos || v.find('\\') != std::string::npos;
         const size_t dot = v.find_last_of('.');
         return sep && dot != std::string::npos && dot + 1 < v.size() && v.size() - dot <= 6;
      }

      std::string Relative(const fs::path& file, const fs::path& base)
      {
         std::error_code ec;
         fs::path rel = fs::relative(file, base, ec);
         if (ec || rel.empty())
            return file.u8string();
         std::string s = rel.generic_u8string(); // forward slashes: same text on every machine
         return s;
      }

      bool Inside(const fs::path& file, const fs::path& dir)
      {
         const std::string f = Lower(file.lexically_normal().generic_u8string());
         std::string d = Lower(dir.lexically_normal().generic_u8string());
         if (!d.empty() && d.back() != '/')
            d += '/';
         return f.compare(0, d.size(), d) == 0;
      }

      uint64_t ContentHash(const fs::path& p, uint64_t& size)
      {
         std::ifstream in(p, std::ios::binary);
         uint64_t h = 1469598103934665603ull;
         size = 0;
         char buf[1 << 16];
         while (in)
         {
            in.read(buf, sizeof(buf));
            const std::streamsize n = in.gcount();
            for (std::streamsize i = 0; i < n; i++)
               h = (h ^ (unsigned char)buf[i]) * 1099511628211ull;
            size += (uint64_t)n;
         }
         return h;
      }

      // The files a .gltf names by relative "uri" (buffers, images).
      std::vector<std::string> GltfSidecars(const fs::path& gltf)
      {
         std::vector<std::string> out;
         std::ifstream in(gltf);
         const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
         size_t pos = 0;
         while ((pos = text.find("\"uri\"", pos)) != std::string::npos)
         {
            const size_t q1 = text.find('"', text.find(':', pos) + 1);
            const size_t q2 = q1 == std::string::npos ? q1 : text.find('"', q1 + 1);
            if (q1 == std::string::npos || q2 == std::string::npos)
               break;
            const std::string uri = text.substr(q1 + 1, q2 - q1 - 1);
            if (uri.compare(0, 5, "data:") != 0 && uri.find("://") == std::string::npos)
               out.push_back(uri);
            pos = q2 + 1;
         }
         return out;
      }

      // Copies `src` into mediaDir/<type>/, reusing a copy with the same content.
      // Returns the copy's path, or empty on failure.
      fs::path CopyIn(const fs::path& src, const fs::path& mediaDir, std::map<std::string, fs::path>& bySource,
                      std::map<std::pair<uint64_t, uint64_t>, fs::path>& byContent, CollectReport& rep)
      {
         const std::string key = Lower(src.generic_u8string());
         auto known = bySource.find(key);
         if (known != bySource.end())
            return known->second;
         uint64_t size = 0;
         const uint64_t hash = ContentHash(src, size);
         auto same = byContent.find({ hash, size });
         if (same != byContent.end())
         {
            rep.reused++;
            bySource[key] = same->second;
            return same->second;
         }
         std::error_code ec;
         const fs::path folder = mediaDir / TypeFolder(src);
         fs::create_directories(folder, ec);
         fs::path dst = folder / src.filename();
         for (int n = 2; fs::exists(dst, ec); n++)
            dst = folder / fs::u8path(src.stem().u8string() + "_" + std::to_string(n) + src.extension().u8string());
         fs::copy_file(src, dst, fs::copy_options::none, ec);
         if (ec)
         {
            rep.failures.push_back(src.u8string() + ": " + ec.message());
            return fs::path();
         }
         rep.copied++;
         rep.bytes += size;
         bySource[key] = dst;
         byContent[{ hash, size }] = dst;
         // A .gltf references its buffers / textures by relative uri: they go
         // next to the copy, under the same relative names.
         if (Lower(src.extension().u8string()) == ".gltf")
            for (const std::string& uri : GltfSidecars(src))
            {
               const fs::path from = src.parent_path() / fs::u8path(uri);
               const fs::path to = dst.parent_path() / fs::u8path(uri);
               fs::create_directories(to.parent_path(), ec);
               if (fs::is_regular_file(from, ec) && !fs::exists(to, ec))
                  fs::copy_file(from, to, fs::copy_options::none, ec);
            }
         return dst;
      }
   }

   // Every save: paths inside the patch's folder are written relative.
   void MakeMediaRelative(Patch::Data& data, const std::string& patchPath)
   {
      namespace fs = std::filesystem;
      const fs::path dir = fs::u8path(patchPath).parent_path();
      if (dir.empty())
         return;
      for (Patch::NodeRecord& rec : data.nodes)
         for (auto& kv : rec.params)
         {
            if (kv.first.size() < 2 || kv.first[0] != 's')
               continue;
            fs::path file;
            const fs::path raw = fs::u8path(kv.second);
            if (raw.is_absolute() && CollectMedia::AsFile(kv.second, dir, file) && CollectMedia::Inside(file, dir))
               kv.second = CollectMedia::Relative(file, dir);
         }
   }

   // Every load: a relative path that exists next to the patch becomes absolute.
   void ResolveRelativeMedia(Patch::Data& data, const std::string& patchPath)
   {
      namespace fs = std::filesystem;
      const fs::path dir = fs::u8path(patchPath).parent_path();
      if (dir.empty())
         return;
      for (Patch::NodeRecord& rec : data.nodes)
         for (auto& kv : rec.params)
         {
            if (kv.first.size() < 2 || kv.first[0] != 's' || kv.second.empty())
               continue;
            const fs::path raw = fs::u8path(kv.second);
            if (!raw.is_relative())
               continue;
            fs::path file;
            if (CollectMedia::AsFile(kv.second, dir, file))
               kv.second = file.u8string();
         }
   }

   // Copies the patch's media next to `patchPath` and rewrites `data` to the copies.
   CollectReport CollectPatchMedia(Patch::Data& data, const std::string& patchPath)
   {
      namespace fs = std::filesystem;
      CollectReport rep;
      const fs::path inf = fs::u8path(patchPath);
      const fs::path dir = inf.parent_path();
      const fs::path mediaDir = dir / fs::u8path(inf.stem().u8string() + "_media");
      rep.mediaDir = mediaDir.u8string();
      std::map<std::string, fs::path> bySource;
      std::map<std::pair<uint64_t, uint64_t>, fs::path> byContent;
      // The patch's current folder too: re-collecting a patch moved elsewhere
      // still finds its relative files.
      const fs::path currentDir = gPatchPath.empty() ? dir : fs::u8path(gPatchPath).parent_path();
      for (Patch::NodeRecord& rec : data.nodes)
         for (auto& kv : rec.params)
         {
            if (kv.first.size() < 2 || kv.first[0] != 's' || kv.second.empty())
               continue;
            fs::path file;
            if (!CollectMedia::AsFile(kv.second, currentDir, file) && !CollectMedia::AsFile(kv.second, dir, file))
            {
               if (CollectMedia::LooksLikePath(kv.second))
                  rep.missing.push_back(rec.typeName + ": " + kv.second);
               continue;
            }
            if (CollectMedia::Inside(file, dir))
            {
               rep.already++;
               kv.second = CollectMedia::Relative(file, dir);
               continue;
            }
            const fs::path copy = CollectMedia::CopyIn(file, mediaDir, bySource, byContent, rep);
            if (!copy.empty())
               kv.second = CollectMedia::Relative(copy, dir);
         }
      return rep;
   }

   // The report window after a collect (FILE > Save with media...).
   void DrawCollectMediaReport()
   {
      if (!gCollectReportOpen)
         return;
      ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f), ImGuiCond_Appearing);
      if (ImGui::Begin("Collect media", &gCollectReportOpen, ImGuiWindowFlags_NoCollapse))
      {
         const CollectReport& r = gCollectReport;
         ImGui::Text("%d file(s) copied (%.1f MB), %d duplicate(s) reused, %d already next to the patch.", r.copied,
                     (double)r.bytes / (1024.0 * 1024.0), r.reused, r.already);
         ImGui::TextDisabled("%s", r.mediaDir.c_str());
         if (ImGui::Button("Open media folder"))
            Platform::RevealInFileManager(r.mediaDir);
         if (!r.missing.empty())
         {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.2f, 1.0f), "%d file(s) not found - relink them on their nodes:",
                               (int)r.missing.size());
            for (const std::string& m : r.missing)
               ImGui::BulletText("%s", m.c_str());
         }
         if (!r.failures.empty())
         {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.95f, 0.3f, 0.25f, 1.0f), "%d copy error(s):", (int)r.failures.size());
            for (const std::string& f : r.failures)
               ImGui::BulletText("%s", f.c_str());
         }
      }
      ImGui::End();
   }
