#pragma once
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>

// What a node reports when it cannot do its job: the one record behind the error/warning badge on the node
// title. `reason` is a calm sentence (what happened, what is safe); `fix` names what a click on the badge does.
struct NodeIssue
{
   enum class Level { None, Warning, Error };
   enum class Fix { None, Relink, AudioSettings, Plugins, ShowNode };

   Level level = Level::None;
   Fix fix = Fix::None;
   std::string reason;

   explicit operator bool() const { return level != Level::None; }

   static NodeIssue Warn(std::string reason, Fix fix = Fix::None) { return { Level::Warning, fix, std::move(reason) }; }
   static NodeIssue Err(std::string reason, Fix fix = Fix::None) { return { Level::Error, fix, std::move(reason) }; }
};

namespace NodeIssues
{
   inline std::string FirstLine(const std::string& s)
   {
      const size_t nl = s.find('\n');
      return nl == std::string::npos ? s : s.substr(0, nl);
   }

   inline std::string FileName(const std::string& path)
   {
      const size_t slash = path.find_last_of("/\\");
      return slash == std::string::npos ? path : path.substr(slash + 1);
   }

   // A file-backed node whose load failed. Missing is a warning (the path is kept, the file may come back);
   // unreadable is an error. The disk is asked once per (path, error) pair, never per frame. Main thread only.
   inline NodeIssue MediaFile(const std::string& path, const std::string& error)
   {
      if (error.empty() || path.empty())
         return {};
      static std::unordered_map<std::string, bool> missingByKey;
      const std::string key = path + '\n' + error;
      auto it = missingByKey.find(key);
      if (it == missingByKey.end())
      {
         if (missingByKey.size() > 256)
            missingByKey.clear();
         std::error_code ec;
         it = missingByKey.emplace(key, !std::filesystem::exists(std::filesystem::u8path(path), ec)).first;
      }
      const std::string name = FileName(path);
      if (it->second)
         return NodeIssue::Warn("Can't find " + name + ". The patch keeps its path. Click to choose the file again.",
                                NodeIssue::Fix::Relink);
      return NodeIssue::Err("Can't read " + name + ": " + FirstLine(error) + ". Nothing in the patch was changed. Click to choose another file.",
                            NodeIssue::Fix::Relink);
   }

   // A Field node whose program does not compile.
   inline NodeIssue FieldCompile(const std::string& error)
   {
      if (error.empty())
         return {};
      return NodeIssue::Err("This Field can't run yet: " + FirstLine(error) + ". Click to find the node and fix the code.",
                            NodeIssue::Fix::ShowNode);
   }
}
