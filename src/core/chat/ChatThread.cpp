#include "ChatThread.h"

#include "json.hpp"
#include "platform/AppPaths.h"

#include <algorithm>
#include <cstring>
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
         for (int m : e.marks)
            if (m != kStepRan)
            {
               o["marks"] = e.marks;
               break;
            }
         if (!e.files.empty())
            o["files"] = e.files;
         if (!e.changes.empty())
         {
            json c = json::array();
            for (const ChangeLine& l : e.changes)
            {
               json one = { { "text", l.text } };
               if (!l.code.empty())
                  one["code"] = l.code;
               if (l.node >= 0 && !l.key.empty())
               {
                  one["node"] = l.node;
                  one["key"] = l.key;
                  one["after"] = l.after;
               }
               c.push_back(one);
            }
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
            if (o.contains("marks") && o["marks"].is_array())
               for (const json& m : o["marks"])
                  e.marks.push_back(m.is_number_integer() ? m.get<int>() : 0);
            e.files = Strings(o, "files");
            if (o.contains("changes") && o["changes"].is_array())
               for (const json& c : o["changes"])
                  if (c.is_object())
                     e.changes.push_back({ c.value("text", std::string()), c.value("code", std::string()), c.value("node", -1),
                                           c.value("key", std::string()), c.value("after", std::string()) });
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

   ThreadInfo InfoOf(const Thread& t, const std::string& path)
   {
      ThreadInfo info{ t.id, t.title, path, t.updated, {} };
      for (const Entry& e : t.entries)
         if (e.kind == Entry::Kind::Agent && !e.agent.empty() && std::find(info.agents.begin(), info.agents.end(), e.agent) == info.agents.end())
            info.agents.push_back(e.agent);
      return info;
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
         out.push_back(InfoOf(t, e.path().u8string()));
      }
      std::sort(out.begin(), out.end(), [](const ThreadInfo& a, const ThreadInfo& b) { return a.updated > b.updated; });
      return out;
   }

   void DeleteThread(const std::string& dir, const std::string& id)
   {
      std::error_code ec;
      std::filesystem::remove(AppPaths::FsPath(dir + "/" + id + ".json"), ec);
   }

   std::string StepKey(const std::string& command)
   {
      std::string c = command;
      auto trim = [](std::string& t)
      {
         const size_t a = t.find_first_not_of(" \t\n");
         const size_t b = t.find_last_not_of(" \t\n");
         t = a == std::string::npos ? std::string() : t.substr(a, b - a + 1);
      };
      trim(c);
      // `cd somewhere && …` in front, as many as there are.
      while (c.rfind("cd ", 0) == 0)
      {
         const size_t amp = c.find("&&");
         if (amp == std::string::npos)
            break;
         c = c.substr(amp + 2);
         trim(c);
      }
      // A filter or a redirect behind: `| grep …`, `| head`, `2>&1`.
      const size_t pipe = c.find(" | ");
      if (pipe != std::string::npos)
         c = c.substr(0, pipe);
      for (const char* tail : { " 2>&1", " 2>/dev/null" })
      {
         const size_t n = std::strlen(tail);
         if (c.size() >= n && c.compare(c.size() - n, n, tail) == 0)
            c = c.substr(0, c.size() - n);
      }
      trim(c);
      std::string out;
      for (char ch : c)
         if (!(ch == ' ' && !out.empty() && out.back() == ' '))
            out += ch;
      return out;
   }

   std::string StepPhrase(const std::string& command, bool* edits)
   {
      if (edits != nullptr)
         *edits = false;
      // Words, quotes kept together.
      std::vector<std::string> w;
      {
         const std::string c = StepKey(command);
         std::string cur;
         char q = 0;
         bool have = false;
         for (char ch : c)
         {
            if (q != 0)
            {
               if (ch == q)
                  q = 0;
               else
                  cur += ch;
               continue;
            }
            if (ch == '"' || ch == '\'')
            {
               q = ch;
               have = true;
               continue;
            }
            if (ch == ' ' || ch == '\t' || ch == '\n')
            {
               if (have || !cur.empty())
                  w.push_back(cur);
               cur.clear();
               have = false;
               continue;
            }
            if ((ch == ';' || ch == '&' || ch == '|') && q == 0)
               break;  // the first command of a chain says what it is
            cur += ch;
         }
         if (have || !cur.empty())
            w.push_back(cur);
      }
      if (w.empty())
         return std::string();
      auto base = [](const std::string& t) { const size_t s = t.find_last_of('/'); return s == std::string::npos ? t : t.substr(s + 1); };
      auto arg = [&](size_t i) { return i < w.size() ? w[i] : std::string(); };
      auto mark = [&](bool e) { if (edits != nullptr) *edits = e; };
      const std::string tool = base(w[0]);
      if (tool != "infinite" && tool != "infinite.exe")
      {
         static const char* const kRead[] = { "cat", "head", "tail", "sed", "grep", "rg", "ls", "find", "wc", "less", "file", "stat", "tree", "jq" };
         for (const char* r : kRead)
            if (tool == r)
               return "Read files";
         if (tool == "python" || tool == "python3" || tool == "node" || tool == "bash" || tool == "sh" || tool == "zsh")
            return "Ran a script";
         if (tool == "cp" || tool == "mv" || tool == "mkdir" || tool == "ln")
         {
            mark(true);
            return "Moved files";
         }
         if (tool == "curl" || tool == "wget")
            return "Fetched from the web";
         if (tool == "ffmpeg" || tool == "ffprobe" || tool == "sox")
            return "Worked on a media file";
         return std::string();
      }
      const std::string v = arg(1);
      static const char* const kLook[] = { "context", "graph", "explain", "params", "nodes", "describe", "status", "selection", "patch", "attention", "kinds" };
      for (const char* l : kLook)
         if (v == l)
            return v == "params" || v == "describe" ? (arg(2).empty() ? "Looked at the patch" : "Looked at " + arg(2)) : "Looked at the patch";
      if (v == "docs" || v == "guide" || v == "examples" || v == "recipes" || v == "help" || v == "--help")
         return "Read the docs";
      if (v == "render" || v == "shot" || v == "frame")
         return "Looked at the picture";
      if (v == "audio")
         return "Listened to the sound";
      if (v == "validate" || v == "check" || v == "errors")
         return "Checked the patch";
      mark(true);
      if (v == "add")
         return "Added " + (arg(2).empty() ? std::string("a node") : arg(2));
      if (v == "remove")
         return "Removed " + (arg(2).empty() ? std::string("a node") : arg(2));
      if (v == "connect" || v == "disconnect")
      {
         std::string a = arg(2), b = arg(3);
         a = a.substr(0, a.find(':'));
         b = b.substr(0, b.find(':'));
         return std::string(v == "connect" ? "Connected " : "Disconnected ") + a + (b.empty() ? "" : " \xE2\x86\x92 " + b);
      }
      if (v == "set" && w.size() >= 5)
         return "Set " + arg(2) + " " + arg(3) + " to " + arg(4);
      if (v == "set")
         return "Set " + arg(2);
      if (v == "mod")
         return "Modulated " + arg(2) + (arg(3).empty() ? "" : " " + arg(3));
      if (v == "expr")
         return "Gave " + arg(2) + (arg(3).empty() ? "" : " " + arg(3)) + " a formula";
      if (v == "bypass")
         return "Bypassed " + arg(2);
      if (v == "batch" || v == "apply")
         return "Made several changes";
      if (v == "undo")
         return "Undid a change";
      if (v == "redo")
         return "Redid a change";
      if (v == "code")
         return "Edited code";
      if (v == "panel")
         return "Opened " + (arg(2).empty() ? std::string("a panel") : arg(2));
      if (v == "settings")
         return "Changed a setting";
      if (v == "save")
         return "Saved the patch";
      if (v == "open")
         return "Opened a patch";
      if (v == "new")
         return "Started a new patch";
      if (v == "play")
         return "Started playback";
      if (v == "stop")
         return "Stopped playback";
      if (v == "tempo")
         return "Set the tempo" + (arg(2).empty() ? "" : " to " + arg(2));
      static const char* const kDid[][2] = { { "copy", "Copied" }, { "paste", "Pasted" }, { "duplicate", "Duplicated" }, { "group", "Grouped" }, { "ungroup", "Ungrouped" } };
      for (const auto& d : kDid)
         if (v == d[0])
            return std::string(d[1]) + (arg(2).empty() ? "" : " " + arg(2));
      mark(false);
      return std::string();
   }

   StepView ViewSteps(const std::vector<std::string>& lines, const std::vector<int>& marks)
   {
      StepView v;
      std::vector<std::string> keys(lines.size());
      for (size_t i = 0; i < lines.size(); i++)
         keys[i] = StepKey(lines[i]);
      for (size_t i = 0; i < lines.size(); i++)
      {
         const int m = i < marks.size() ? marks[i] : kStepRan;
         if (m == kStepNote)
         {
            StepRow note;
            note.text = lines[i];
            note.note = true;
            v.rows.push_back(note);
            continue;
         }
         bool again = false;
         for (size_t j = i + 1; j < lines.size() && !again; j++)
            again = (j >= marks.size() || marks[j] != kStepNote) && keys[j] == keys[i];
         if (again)
         {
            v.retried++;
            continue;
         }
         v.commands++;
         if (m == kStepFailed)
            v.failed++;
         bool edits = false;
         StepRow row;
         row.text = lines[i];
         row.phrase = StepPhrase(lines[i], &edits);
         row.failed = m == kStepFailed;
         if (edits && !row.failed)
            v.edits++;
         // Three reads in a row are one row: "Looked at the patch ×3".
         if (!row.phrase.empty() && !row.failed && !v.rows.empty() && !v.rows.back().note && !v.rows.back().failed &&
             v.rows.back().phrase == row.phrase)
         {
            v.rows.back().times++;
            v.rows.back().text += "\n" + row.text;
            continue;
         }
         v.rows.push_back(row);
      }
      return v;
   }
}
