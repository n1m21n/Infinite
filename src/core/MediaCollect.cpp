#include "core/MediaCollect.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <set>

#include "platform/AppPaths.h"

namespace MediaCollect
{
   namespace
   {
      // Param values are stored escaped (Patch.cpp's EscapeLine); a path only ever
      // carries the backslash escape.
      std::string Unescape(const std::string& raw)
      {
         std::string out;
         for (size_t i = 0; i < raw.size(); i++)
         {
            if (raw[i] == '\\' && i + 1 < raw.size() && raw[i + 1] == '\\')
               i++;
            out += raw[i];
         }
         return out;
      }

      std::string Escape(const std::string& value)
      {
         std::string out;
         for (char c : value)
         {
            if (c == '\\')
               out += '\\';
            out += c;
         }
         return out;
      }

      std::string Lower(std::string s)
      {
         std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
         return s;
      }

      bool EndsWith(const std::string& s, const std::string& tail)
      {
         return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
      }

      // 0 = nothing there, 1 = file, 2 = folder.
      int Kind(const std::string& path)
      {
         std::error_code ec;
         const std::filesystem::path p = AppPaths::FsPath(path);
         if (std::filesystem::is_directory(p, ec))
            return 2;
         if (std::filesystem::is_regular_file(p, ec))
            return 1;
         return 0;
      }

      // Lexically normal form used to tell "the same file twice" from "two files with one name".
      std::string Identity(const std::string& path)
      {
         std::error_code ec;
         std::filesystem::path p = std::filesystem::weakly_canonical(AppPaths::FsPath(path), ec);
         if (ec)
            p = AppPaths::FsPath(path).lexically_normal();
         std::string s = p.generic_u8string();
#ifdef _WIN32
         s = Lower(s);
#endif
         return s;
      }

      std::string BaseName(const std::string& path)
      {
         std::string p = path;
         while (!p.empty() && (p.back() == '/' || p.back() == '\\'))
            p.pop_back();
         const size_t slash = p.find_last_of("/\\");
         return slash == std::string::npos ? p : p.substr(slash + 1);
      }

      // "kick.wav" -> "kick-2.wav"; a folder or a name with no dot gets "-2" at the end.
      std::string Numbered(const std::string& name, int n, bool isFolder)
      {
         const size_t dot = isFolder ? std::string::npos : name.find_last_of('.');
         const std::string suffix = "-" + std::to_string(n);
         if (dot == std::string::npos || dot == 0)
            return name + suffix;
         return name.substr(0, dot) + suffix + name.substr(dot);
      }

      std::string JoinDir(const std::string& dir, const std::string& rel)
      {
         if (dir.empty())
            return rel;
         const char last = dir.back();
         return (last == '/' || last == '\\') ? dir + rel : dir + "/" + rel;
      }
   }

   bool IsMediaKey(const std::string& paramKey)
   {
      if (paramKey.size() < 3 || paramKey.compare(0, 2, "s ") != 0)
         return false;
      const std::string name = Lower(paramKey.substr(2));
      // Where the app writes, not what it reads.
      if (name.find("record") != std::string::npos || name.find("export") != std::string::npos)
         return false;
      return EndsWith(name, "path") || EndsWith(name, "file") || name == "folder" || name == "pathb";
   }

   bool IsRelative(const std::string& path)
   {
      if (path.empty())
         return false;
      const char c = path[0];
      if (c == '/' || c == '\\' || c == '~' || c == '@')
         return false;
      if (path.size() >= 2 && std::isalpha((unsigned char)c) && path[1] == ':')
         return false;
      return path.find("://") == std::string::npos;
   }

   std::vector<Item> Plan(Patch::Data& data, const std::string& mediaDir)
   {
      std::vector<Item> items;
      std::map<std::string, size_t> byIdentity;   // source identity -> first item
      std::set<std::string> takenNames;           // lower-case dest names already used
      for (size_t n = 0; n < data.nodes.size(); n++)
      {
         for (auto& param : data.nodes[n].params)
         {
            if (!IsMediaKey(param.first))
               continue;
            const std::string source = Unescape(param.second);
            if (source.empty() || IsRelative(source) || source[0] == '@' || source.find("://") != std::string::npos)
               continue;
            const int kind = Kind(source);
            if (kind == 0)
            {
               // Only something shaped like a file path counts as missing (Gesture's
               // points never contain a separator).
               if (source.find_first_of("/\\") == std::string::npos)
                  continue;
               Item item;
               item.source = source;
               item.state = State::Missing;
               item.node = (int)n;
               items.push_back(item);
               continue;
            }

            Item item;
            item.source = source;
            item.isFolder = kind == 2;
            item.node = (int)n;
            const std::string id = Identity(source);
            auto seen = byIdentity.find(id);
            if (seen != byIdentity.end())
            {
               item.dest = items[seen->second].dest;
               item.state = State::Duplicate;
            }
            else
            {
               const std::string base = BaseName(source);
               std::string name = base;
               for (int k = 2; takenNames.count(Lower(name)) != 0; k++)
                  name = Numbered(base, k, item.isFolder);
               takenNames.insert(Lower(name));
               item.dest = mediaDir + "/" + name;
               byIdentity[id] = items.size();
            }
            param.second = Escape(item.dest);
            items.push_back(item);
         }
      }
      return items;
   }

   bool CopyAll(const std::vector<Item>& items, const std::string& patchDir, int& copied, std::string& error)
   {
      copied = 0;
      for (const Item& item : items)
      {
         if (item.state != State::Copy)
            continue;
         const std::filesystem::path to = AppPaths::FsPath(JoinDir(patchDir, item.dest));
         std::error_code ec;
         std::filesystem::create_directories(to.parent_path(), ec);
         const std::filesystem::path from = AppPaths::FsPath(item.source);
         if (std::filesystem::equivalent(from, to, ec))   // second save into the same media folder
            continue;
         ec.clear();
         if (item.isFolder)
            std::filesystem::copy(from, to,
                                  std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
         else
            std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, ec);
         if (ec)
         {
            error = BaseName(item.source) + ": " + ec.message();
            return false;
         }
         copied++;
      }
      return true;
   }

   void ResolveRelative(Patch::Data& data, const std::string& patchDir)
   {
      if (patchDir.empty())
         return;
      for (Patch::NodeRecord& node : data.nodes)
         for (auto& param : node.params)
         {
            if (!IsMediaKey(param.first))
               continue;
            const std::string value = Unescape(param.second);
            if (!IsRelative(value))
               continue;
            const std::string full = JoinDir(patchDir, value);
            if (Kind(full) == 0)
               continue;
            param.second = Escape(AppPaths::FsPath(full).lexically_normal().u8string());
         }
   }

   void MakeRelative(Patch::Data& data, const std::string& patchDir, const std::string& mediaDir)
   {
      if (patchDir.empty() || mediaDir.empty())
         return;
      const std::filesystem::path base = AppPaths::FsPath(patchDir).lexically_normal();
      const std::string root = Identity(JoinDir(patchDir, mediaDir)) + "/";
      for (Patch::NodeRecord& node : data.nodes)
         for (auto& param : node.params)
         {
            if (!IsMediaKey(param.first))
               continue;
            const std::string value = Unescape(param.second);
            if (value.empty() || IsRelative(value) || Kind(value) == 0)
               continue;
            if (Identity(value).compare(0, root.size(), root) != 0)
               continue;
            const std::filesystem::path rel = AppPaths::FsPath(value).lexically_normal().lexically_relative(base);
            if (!rel.empty())
               param.second = Escape(rel.generic_u8string());
         }
   }

   std::string MediaDirFor(const std::string& patchPath)
   {
      std::string name = BaseName(patchPath);
      const size_t dot = name.find_last_of('.');
      if (dot != std::string::npos && dot > 0)
         name = name.substr(0, dot);
      return (name.empty() ? std::string("Untitled") : name) + " media";
   }
}
