#include "AgentAuth.h"

#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace chat
{
   std::vector<std::string> SignInArgs(AgentKind kind)
   {
      switch (kind)
      {
         case AgentKind::Codex:
            return { "codex", "login" };
         case AgentKind::Antigravity:
            // No login command: the first headless turn asks for the sign-in itself, then answers.
            return { "agy", "--output-format", "stream-json", "-p", "Reply with the word ok." };
         default:
            return { "claude", "auth", "login" };
      }
   }

   std::vector<std::string> StatusArgs(AgentKind kind, bool mac)
   {
      switch (kind)
      {
         case AgentKind::Codex:
            return { "codex", "login", "status" };
         case AgentKind::Antigravity:
            // `agy models` lists models only when signed in; its list fills the picker too.
            if (!mac)
               return { "agy", "models" };
            return { "/bin/sh", "-c",
                     "if /usr/bin/security find-generic-password -s gemini -a antigravity >/dev/null 2>&1 || "
                     "[ -f \"$HOME/.gemini/jetski-standalone-oauth-token\" ]; then exec agy models; fi; "
                     "echo 'not logged in'; exit 1" };
         default:
            return { "claude", "auth", "status" };
      }
   }

   std::vector<std::string> SignOutArgs(AgentKind kind, bool mac)
   {
      switch (kind)
      {
         case AgentKind::Codex:
            return { "codex", "logout" };
         case AgentKind::Antigravity:
            if (!mac)
               return {};
            return { "/bin/sh", "-c",
                     "/usr/bin/security delete-generic-password -s gemini -a antigravity >/dev/null 2>&1; "
                     "rm -f \"$HOME/.gemini/jetski-standalone-oauth-token\"; exit 0" };
         default:
            return { "claude", "auth", "logout" };
      }
   }

   std::string StripAnsi(const std::string& text)
   {
      std::string out;
      out.reserve(text.size());
      for (size_t i = 0; i < text.size(); i++)
      {
         const unsigned char c = (unsigned char)text[i];
         if (c == 0x1b)
         {
            if (i + 1 < text.size() && text[i + 1] == '[')
            {
               i += 2;
               while (i < text.size() && !(text[i] >= '@' && text[i] <= '~'))
                  i++;
            }
            else if (i + 1 < text.size() && text[i + 1] == ']')
            {
               i += 2;  // OSC: up to BEL or ESC \
               while (i < text.size() && text[i] != 0x07 && !(text[i] == 0x1b && i + 1 < text.size() && text[i + 1] == '\\'))
                  i++;
               if (i < text.size() && text[i] == 0x1b)
                  i++;
            }
            continue;
         }
         if (c == '\r')
            continue;
         if (c < 0x20 && c != '\n' && c != '\t')
            continue;
         out += (char)c;
      }
      return out;
   }

   std::string FindUrl(const std::string& text)
   {
      const std::string t = StripAnsi(text);
      const size_t at = t.find("https://");
      if (at == std::string::npos)
         return std::string();
      size_t end = at;
      while (end < t.size() && !std::isspace((unsigned char)t[end]))
         end++;
      return t.substr(at, end - at);
   }

   bool AsksForCode(const std::string& log)
   {
      std::string l = StripAnsi(log);
      std::transform(l.begin(), l.end(), l.begin(), [](unsigned char c) { return (char)std::tolower(c); });
      // Gemini: "paste the authorization code"; Claude with no browser: "Paste code here if prompted".
      return l.find("paste the authorization code") != std::string::npos || l.find("paste the code") != std::string::npos ||
             l.find("paste code here") != std::string::npos || l.find("enter the authorization code") != std::string::npos;
   }

   namespace
   {
      std::string Lower(std::string s)
      {
         std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
         return s;
      }

      bool Has(const std::string& hay, std::initializer_list<const char*> words)
      {
         for (const char* w : words)
            if (hay.find(w) != std::string::npos)
               return true;
         return false;
      }

      // The last line that says something: not blank, not a bare brace.
      std::string LastLine(const std::string& text)
      {
         size_t end = text.size();
         while (end > 0)
         {
            size_t start = text.rfind('\n', end - 1);
            start = start == std::string::npos ? 0 : start + 1;
            std::string line = text.substr(start, end - start);
            while (!line.empty() && std::isspace((unsigned char)line.back()))
               line.pop_back();
            while (!line.empty() && std::isspace((unsigned char)line.front()))
               line.erase(0, 1);
            if (line.size() > 2)
               return line.size() > 160 ? line.substr(0, 157) + "..." : line;
            if (start == 0)
               break;
            end = start - 1;
         }
         return std::string();
      }
   }

   Friendly Explain(const std::string& agentName, const std::string& raw)
   {
      Friendly f;
      const std::string clean = StripAnsi(raw);
      const std::string l = Lower(clean);
      // Machine problems first: their messages can also contain words like "login" or "not found".
      if (Has(l, { "no space left on device", "enospc", "not enough space", "disk full", "there is not enough space" }))
      {
         f.kind = Friendly::Kind::Disk;
         f.text = "Not enough free space. Free up 2 GB, then try again.";
      }
      else if (Has(l, { "running scripts is disabled", "executionpolicy", "execution policy", "constrainedlanguage" }))
      {
         f.kind = Friendly::Kind::Policy;
         f.text = "This computer's settings block installers. Ask the person who manages it.";
      }
      else if (Has(l, { "a password is required", "a terminal is required to read the password", "sudo: no tty" }))
      {
         f.kind = Friendly::Kind::Password;
         f.text = "This step needs your computer password.";
      }
      else if (Has(l, { "xdg-open: not found", "no browser", "could not open a browser", "failed to open browser", "unable to open browser", "couldn't open the browser" }))
      {
         f.kind = Friendly::Kind::NoBrowser;
         f.text = "No web browser found. Copy the sign-in address to a browser, then paste the code here.";
      }
      else if (Has(l, { "requires a subscription", "subscription required", "upgrade your plan", "upgrade to pro", "not available on your plan", "does not have access to claude code", "credit balance is too low" }))
      {
         f.kind = Friendly::Kind::Plan;
         f.text = "Your " + agentName + " account needs a paid plan for this. Pick another one or upgrade.";
      }
      else if (Has(l, { "authentication required", "authentication failed", "not logged in", "not signed in", "please run /login", "please log in",
                   "invalid api key", "invalid x-api-key", "unauthorized", "token expired", "expired token", "oauth", "sign in", "log in", "login required" }))
      {
         f.kind = Friendly::Kind::SignIn;
         f.text = "Sign in to " + agentName + " to continue.";
      }
      else if (Has(l, { "terms of service", "accept the terms" }))
      {
         f.kind = Friendly::Kind::Terms;
         f.text = agentName + " needs its terms accepted once. Open the terminal and answer its first screen.";
      }
      else if (Has(l, { "rate limit", "rate_limit", "usage limit", "quota", "limit reached", "overloaded", "too many requests" }))
      {
         f.kind = Friendly::Kind::Limit;
         f.text = agentName + " hit its usage limit. Try again later or pick another agent.";
      }
      else if (Has(l, { "enotfound", "could not resolve", "network is unreachable", "connection refused", "connection reset", "offline", "econnreset", "getaddrinfo" }))
      {
         f.kind = Friendly::Kind::Offline;
         f.text = "Could not reach " + agentName + ". Check the internet connection.";
      }
      else if (Has(l, { "command not found", "not found: ", "no such file or directory", "is not recognized" }))
      {
         f.kind = Friendly::Kind::Missing;
         f.text = agentName + " is not installed. Set it up again.";
      }
      else
      {
         const std::string last = LastLine(clean);
         f.text = last.empty() ? agentName + " stopped with an error." : last;
      }
      return f;
   }

   bool WaitsForSignIn(const std::string& log)
   {
      // Only plain lines count: a stream-json line (or a piece of one) can quote anything the model wrote.
      std::string plain;
      const std::string clean = StripAnsi(log);
      size_t start = 0;
      while (start < clean.size())
      {
         size_t nl = clean.find('\n', start);
         if (nl == std::string::npos)
            nl = clean.size();
         std::string line = clean.substr(start, nl - start);
         start = nl + 1;
         const size_t b = line.find_first_not_of(" \t");
         if (b == std::string::npos || line[b] == '{' || line.find("\":") != std::string::npos)
            continue;
         plain += Lower(line) + '\n';
      }
      if (plain.empty())
         return false;
      if (AsksForCode(plain))
         return true;
      return plain.find("https://") != std::string::npos &&
             Has(plain, { "sign in", "sign-in", "log in", "login", "authoriz", "authoris", "authenticat", "oauth" });
   }

   Auth SignedInFrom(AgentKind kind, bool exitOk, const std::string& output)
   {
      std::string l = Lower(StripAnsi(output));
      if (kind == AgentKind::Claude)
      {
         // JSON: "loggedIn": true
         std::string tight;
         for (char c : l)
            if (!std::isspace((unsigned char)c))
               tight += c;
         if (tight.find("\"loggedin\":true") != std::string::npos)
            return Auth::In;
         if (tight.find("\"loggedin\":false") != std::string::npos)
            return Auth::Out;
         return exitOk ? Auth::Unknown : Auth::Out;
      }
      if (kind == AgentKind::Codex)
      {
         if (l.find("not logged in") != std::string::npos)
            return Auth::Out;
         if (exitOk && l.find("logged in") != std::string::npos)
            return Auth::In;
         return exitOk ? Auth::Unknown : Auth::Out;
      }
      if (WaitsForSignIn(output) || l.find("not logged in") != std::string::npos)
         return Auth::Out;
      if (exitOk && !ParseAgyModels(StripAnsi(output)).empty())
         return Auth::In;
      return Auth::Unknown;
   }

   std::vector<Model> FixedModels(AgentKind kind)
   {
      if (kind == AgentKind::Claude)
         // The ids are Claude Code's aliases, so a newer model is picked up by itself; the names say which one
         // the aliases mean today (update them when a new family ships).
         return { { "sonnet", "Sonnet 5.5" }, { "opus", "Opus 5.5" }, { "fable", "Fable 5.1" }, { "haiku", "Haiku 5.5" } };
      return {};
   }

   std::vector<Model> ParseCodexModels(const std::string& text)
   {
      using json = nlohmann::json;
      const json j = json::parse(text, nullptr, false);
      if (!j.is_object() || !j.contains("models") || !j["models"].is_array())
         return {};
      std::vector<std::pair<int, Model>> ranked;
      for (const json& m : j["models"])
      {
         if (!m.is_object() || m.value("visibility", std::string()) != "list")
            continue;
         Model one;
         one.id = m.value("slug", std::string());
         one.label = m.value("display_name", one.id);
         if (!one.id.empty())
            ranked.push_back({ m.value("priority", 1000), one });
      }
      std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
      std::vector<Model> out;
      for (auto& r : ranked)
         out.push_back(std::move(r.second));
      return out;
   }

   std::string ModelSettingsPath(AgentKind kind, const std::string& home, const std::string& codexHome)
   {
      if (kind == AgentKind::Codex)
         return (codexHome.empty() ? home + "/.codex" : codexHome) + "/config.toml";
      if (kind == AgentKind::Antigravity)
         return home + "/.gemini/antigravity-cli/settings.json";
      return home + "/.claude/settings.json";
   }

   std::string ConfiguredModel(AgentKind kind, const std::string& text)
   {
      if (kind != AgentKind::Codex)
      {
         const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
         return j.is_object() && j.contains("model") && j["model"].is_string() ? j["model"].get<std::string>() : std::string();
      }
      // TOML: the top-level `model = "..."`, before the first [table]; `model_reasoning_effort` is another key.
      size_t start = 0;
      while (start < text.size())
      {
         size_t nl = text.find('\n', start);
         if (nl == std::string::npos)
            nl = text.size();
         std::string line = text.substr(start, nl - start);
         start = nl + 1;
         const size_t b = line.find_first_not_of(" \t");
         if (b == std::string::npos || line[b] == '#')
            continue;
         if (line[b] == '[')
            break;
         const size_t eq = line.find('=');
         if (eq == std::string::npos)
            continue;
         std::string key = line.substr(b, eq - b);
         key.erase(key.find_last_not_of(" \t") + 1);
         if (key != "model")
            continue;
         const size_t q1 = line.find('"', eq), q2 = q1 == std::string::npos ? q1 : line.find('"', q1 + 1);
         return q2 == std::string::npos ? std::string() : line.substr(q1 + 1, q2 - q1 - 1);
      }
      return {};
   }

   std::vector<Model> ParseAgyModels(const std::string& output)
   {
      std::vector<Model> out;
      size_t start = 0;
      bool first = true;
      while (start < output.size())
      {
         size_t nl = output.find('\n', start);
         if (nl == std::string::npos)
            nl = output.size();
         const std::string line = StripAnsi(output.substr(start, nl - start));
         start = nl + 1;
         // Rows are `<id>\t<label>`; progress lines have no tab, and a column header names "ID".
         const size_t tab = line.find('\t');
         if (tab == std::string::npos || tab == 0)
            continue;
         if (first && (line.compare(0, tab, "ID") == 0 || line.compare(0, tab, "id") == 0))
         {
            first = false;
            continue;
         }
         first = false;
         Model m;
         m.id = line.substr(0, tab);
         m.label = line.substr(tab + 1);
         if (m.id.find(' ') == std::string::npos && !m.label.empty())
            out.push_back(m);
      }
      ShortenModelLabels(out);
      return out;
   }

   void ShortenModelLabels(std::vector<Model>& models)
   {
      // "Gemini 3.8 Flash (Medium)" -> "Gemini 3.8 Flash" when no other name in the list is left the same.
      std::vector<std::string> names;
      for (const Model& m : models)
      {
         std::string s = m.full.empty() ? m.label : m.full;
         if (const size_t open = s.find(" ("); open != std::string::npos)
            s.erase(open);
         while (!s.empty() && s.back() == ' ')
            s.pop_back();
         names.push_back(s);
      }
      std::vector<std::string> sorted = names;
      std::sort(sorted.begin(), sorted.end());
      if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end() || std::find(names.begin(), names.end(), std::string()) != names.end())
         return;
      for (size_t i = 0; i < models.size(); ++i)
      {
         if (models[i].full.empty())
            models[i].full = models[i].label;
         models[i].label = names[i];
      }
   }
}
