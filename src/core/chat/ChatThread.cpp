#include "ChatThread.h"

#include "json.hpp"
#include "platform/AppPaths.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace chat
{
   namespace
   {
      using json = nlohmann::json;

      const char* const kKinds[] = { "user", "agent", "change", "steps", "note", "error" };

      std::vector<std::string> Strings(const json& j, const char* key)
      {
         std::vector<std::string> out;
         if (j.contains(key) && j[key].is_array())
            for (const json& v : j[key])
               if (v.is_string())
                  out.push_back(v.get<std::string>());
         return out;
      }
   }

   std::string ToJson(const Thread& t)
   {
      json j = { { "version", 1 }, { "id", t.id }, { "title", t.title }, { "updated", t.updated } };
      json sessions = json::object();
      for (const auto& [agent, s] : t.sessions)
         sessions[agent] = { { "id", s.id }, { "live", s.live } };
      j["sessions"] = sessions;
      json entries = json::array();
      for (const Entry& e : t.entries)
      {
         json o = { { "kind", kKinds[(int)e.kind] } };
         if (!e.agent.empty())
            o["agent"] = e.agent;
         if (!e.text.empty())
            o["text"] = e.text;
         if (!e.lines.empty())
            o["lines"] = e.lines;
         if (!e.files.empty())
            o["files"] = e.files;
         if (!e.changes.empty())
         {
            json c = json::array();
            for (const ChangeLine& l : e.changes)
               c.push_back(l.code.empty() ? json{ { "text", l.text } } : json{ { "text", l.text }, { "code", l.code } });
            o["changes"] = c;
         }
         if (!e.ok)
            o["ok"] = false;
         if (e.undone)
            o["undone"] = true;
         entries.push_back(o);
      }
      j["entries"] = entries;
      return j.dump(1);
   }

   bool FromJson(const std::string& text, Thread& t)
   {
      const json j = json::parse(text, nullptr, false);
      if (!j.is_object() || j.value("version", 0) != 1)
         return false;
      t = Thread();
      t.id = j.value("id", std::string());
      t.title = j.value("title", std::string());
      t.updated = j.value("updated", 0LL);
      if (j.contains("sessions") && j["sessions"].is_object())
         for (auto it = j["sessions"].begin(); it != j["sessions"].end(); ++it)
            if (it.value().is_object())
               t.sessions[it.key()] = { it.value().value("id", std::string()), it.value().value("live", false) };
      if (j.contains("entries") && j["entries"].is_array())
         for (const json& o : j["entries"])
         {
            if (!o.is_object())
               continue;
            Entry e;
            const std::string kind = o.value("kind", std::string());
            int k = 0;
            while (k < 6 && kind != kKinds[k])
               k++;
            if (k == 6)
               continue;
            e.kind = (Entry::Kind)k;
            e.agent = o.value("agent", std::string());
            e.text = o.value("text", std::string());
            e.lines = Strings(o, "lines");
            e.files = Strings(o, "files");
            if (o.contains("changes") && o["changes"].is_array())
               for (const json& c : o["changes"])
                  if (c.is_object())
                     e.changes.push_back({ c.value("text", std::string()), c.value("code", std::string()) });
            e.ok = o.value("ok", true);
            e.undone = o.value("undone", false);
            t.entries.push_back(e);
         }
      return !t.id.empty();
   }

   std::string TitleFor(const std::string& firstMessage)
   {
      std::string t;
      for (char c : firstMessage)
         t += c == '\n' ? ' ' : c;
      if (t.size() > 60)
      {
         size_t cut = t.rfind(' ', 57);
         t = t.substr(0, cut == std::string::npos || cut < 30 ? 57 : cut) + "…";
      }
      return t;
   }

   bool SaveThread(const std::string& dir, const Thread& t)
   {
      std::error_code ec;
      std::filesystem::create_directories(AppPaths::FsPath(dir), ec);
      const std::string path = dir + "/" + t.id + ".json";
      {
         std::ofstream f(AppPaths::FsPath(path + ".tmp"), std::ios::binary);
         f << ToJson(t);
         if (!f.good())
            return false;
      }
      std::filesystem::rename(AppPaths::FsPath(path + ".tmp"), AppPaths::FsPath(path), ec);
      return !ec;
   }

   bool LoadThread(const std::string& path, Thread& t)
   {
      std::ifstream f(AppPaths::FsPath(path), std::ios::binary);
      if (!f.good())
         return false;
      std::stringstream ss;
      ss << f.rdbuf();
      return FromJson(ss.str(), t);
   }

   std::vector<ThreadInfo> ListThreads(const std::string& dir)
   {
      std::vector<ThreadInfo> out;
      std::error_code ec;
      for (const auto& e : std::filesystem::directory_iterator(AppPaths::FsPath(dir), ec))
      {
         if (e.path().extension() != ".json")
            continue;
         Thread t;
         if (!LoadThread(e.path().u8string(), t) || t.entries.empty())
            continue;
         out.push_back({ t.id, t.title, e.path().u8string(), t.updated });
      }
      std::sort(out.begin(), out.end(), [](const ThreadInfo& a, const ThreadInfo& b) { return a.updated > b.updated; });
      return out;
   }

   void DeleteThread(const std::string& dir, const std::string& id)
   {
      std::error_code ec;
      std::filesystem::remove(AppPaths::FsPath(dir + "/" + id + ".json"), ec);
   }
}
