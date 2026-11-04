#include "AgentAuth.h"

#include <algorithm>
#include <cctype>

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

   int SignInWaitSeconds(AgentKind kind) { return kind == AgentKind::Antigravity ? 60 : 0; }

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
      if (Has(l, { "authentication required", "authentication failed", "not logged in", "not signed in", "please run /login", "please log in",
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

   std::vector<Model> FixedModels(AgentKind kind)
   {
      if (kind == AgentKind::Claude)
         return { { "sonnet", "Sonnet" }, { "opus", "Opus" }, { "haiku", "Haiku" } };
      return {};
   }

   std::vector<Model> ParseAgyModels(const std::string& output)
   {
      std::vector<Model> out;
      size_t start = 0;
      while (start < output.size())
      {
         size_t nl = output.find('\n', start);
         if (nl == std::string::npos)
            nl = output.size();
         const std::string line = StripAnsi(output.substr(start, nl - start));
         start = nl + 1;
         const size_t tab = line.find('\t');
         if (tab == std::string::npos || tab == 0)
            continue;
         Model m;
         m.id = line.substr(0, tab);
         m.label = line.substr(tab + 1);
         if (m.id.find(' ') == std::string::npos && !m.label.empty())
            out.push_back(m);
      }
      return out;
   }
}
