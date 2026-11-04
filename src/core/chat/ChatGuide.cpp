#include "ChatGuide.h"

#include <algorithm>
#include <cctype>

namespace chat
{
   namespace
   {
      const char* const kSite = "https://infinitedevices.org";
      const char* const kRepo = "https://github.com/n1m21n/Infinite";

      std::string Lower(std::string s)
      {
         std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
         return s;
      }
   }

   const std::vector<SlashCommand>& SlashCommands()
   {
      static const std::vector<SlashCommand> cmds = {
         { "help", "What the chat can do", false },
         { "shortcuts", "Every keyboard shortcut", false },
         { "manuals", "The manuals and where to read them", false },
         { "languages", "The code languages built in", false },
         { "about", "What Infinite is", false },
         { "website", "Open the website", false },
         { "github", "Open the GitHub page", false },
         { "agent", "Switch agent: /agent claude", true },
         { "model", "Switch model: /model opus", true },
         { "new", "Start a new chat", false },
         { "undo", "Undo the last change", false },
         { "explain", "Explain this patch", true },
         { "check", "Validate the patch and its code", false },
         { "docs", "Ask the manuals: /docs how does Field work", true },
      };
      return cmds;
   }

   bool ParseSlash(const std::string& line, std::string& name, std::string& arg)
   {
      if (line.size() < 2 || line[0] != '/')
         return false;
      size_t end = 1;
      while (end < line.size() && (std::isalpha((unsigned char)line[end]) || line[end] == '-'))
         end++;
      if (end == 1 || (end < line.size() && !std::isspace((unsigned char)line[end])))
         return false;  // "/Users/me" and "/tmp/x" are paths
      name = Lower(line.substr(1, end - 1));
      const size_t from = line.find_first_not_of(" \t", end);
      arg = from == std::string::npos ? std::string() : line.substr(from);
      while (!arg.empty() && std::isspace((unsigned char)arg.back()))
         arg.pop_back();
      return true;
   }

   std::vector<const SlashCommand*> MatchSlash(const std::string& prefix)
   {
      std::vector<const SlashCommand*> out;
      const std::string p = Lower(prefix);
      for (const SlashCommand& c : SlashCommands())
         if (std::string(c.name).rfind(p, 0) == 0)
            out.push_back(&c);
      return out;
   }

   std::string SlashAnswer(const std::string& name, const std::string& arg)
   {
      (void)arg;
      if (name == "help")
      {
         std::string out = "**Commands**\n";
         for (const SlashCommand& c : SlashCommands())
            out += std::string("- [/") + c.name + (c.takesArg ? " …" : "") + "](say:/" + c.name + (c.takesArg ? " " : "") + ") " + c.summary + "\n";
         out += "\n**Point at things**\n- `@cube` a node, `@cube.detail` one of its controls\n- `@claude`, `@codex`, `@antigravity` choose who answers\n"
                "- add, connect, remove, bypass, undo, modulate, set: Infinite does it directly, no agent needed\n"
                "- attach a file with +, or drop it in\n";
         return out;
      }
      if (name == "shortcuts")
         return "Every keyboard shortcut, for this system.\n[Open the shortcut list](open:shortcuts)";
      if (name == "manuals")
         return "**Three manuals, all searchable by an agent**\n"
                "- Node Field Guide: every node, its controls and ranges\n"
                "- Field manual: the Field language, domains and nodes\n"
                "- Headless guide: render, check and drive Infinite without the window\n"
                "Ask in plain words, or `/docs <question>`; the agent reads them with `infinite docs`.\n"
                "[Node help](open:help) [/docs …](say:/docs ) "
                "[Developers page](web:https://infinitedevices.org/developers.html)";
      if (name == "languages")
         return "**Code languages built in**\n"
                "- Field: one kernel for sound, pixels and modulation (`.field`)\n"
                "- Sketch: 2D drawing in JavaScript (`.sketch.js`)\n"
                "- Sketch 3D: three.js-style scenes (`.sketch3d.js`)\n"
                "- GLSL: fragment shaders (`.glsl`)\n"
                "- Formula and Equation: expressions inside a control\n"
                "Each is checked before it runs: `infinite code check <file>`.\n"
                "[Ask about Field](say:/docs how does Field work)";
      if (name == "about")
         return "Infinite is a free, open-source workstation for visuals, music and code on one canvas. Wire shaders, 3D, synths and modulation together in real time. "
                "It runs on macOS, Windows and Linux, and can be driven from the command line or by agents.\n"
                "Made by Naman Soni (n1m21n).\n"
                "[Website](web:https://infinitedevices.org) [GitHub](web:https://github.com/n1m21n/Infinite)";
      if (name == "website")
         return "[Open infinitedevices.org](web:https://infinitedevices.org)";
      if (name == "github")
         return "[Open the GitHub page](web:https://github.com/n1m21n/Infinite)";
      return std::string();
   }

   std::string SlashPrompt(const std::string& name, const std::string& arg)
   {
      if (name == "explain")
         return "Explain the open patch in plain words: what it makes, how the signal flows, and which controls to try. " +
                (arg.empty() ? std::string("Use `infinite context`.") : "Focus on: " + arg);
      if (name == "check")
         return "Check the open patch: run `infinite patch check` on it and `infinite code errors` for the window, "
                "then report what is wrong in one line each, with a button to fix what you can.";
      if (name == "docs")
         return arg.empty() ? std::string()
                            : "Answer from the manuals, not from memory: find it with `infinite docs` (topics: nodes, field, headless, connections, "
                              "patch-format, geometry, sketch, glsl, expressions, shortcuts). Question: " + arg;
      return std::string();
   }

   std::string Welcome()
   {
      return "**Welcome to Infinite chat**\n"
             "Ask for what you want; it changes your patch while you watch.\n"
             "1. **Say it.** \"Add a Noise and wire it to the output.\" Short commands (add, connect, remove, bypass, undo) run at once, with no agent.\n"
             "2. **Point at it.** Type `@` to name a node, `@cube.detail` for one control. `@claude` or `@codex` picks who answers.\n"
             "3. **Add files.** Press + or drop audio, video, images, models, MIDI, patches or code in.\n"
             "4. **Review.** Every change is listed. Undo it with one button, or press Undo.\n"
             "Type `/` for commands: shortcuts, manuals, languages, and more.\n"
             "[Add a Noise and wire it to the output](say:Add a Noise and wire it to the output) "
             "[Explain this patch](say:/explain) [All commands](say:/help)";
   }

   std::string Briefing()
   {
      return std::string("You are working inside Infinite (") + kSite + ", " + kRepo + "), a free open-source workstation for visuals, music and code. "
             "The user talks to you from its chat. Answer from the source, never from memory:\n"
             "- `infinite docs` lists the manuals: nodes (Node Field Guide), field, headless, connections, patch-format, geometry, sketch, sketch3d, glsl, "
             "expressions, shortcuts, panels, map. `infinite context` shows the open patch.\n"
             "- Check code before you say it works: `infinite code check <file>`, `infinite patch check <p.inf>`, `infinite code errors`.\n"
             "- Change the open window with `infinite` commands only; never write a new .inf unless asked.\n"
             "- Reply in short plain sentences. Name nodes as @title#index, controls as @title#index.key, a change as old → new. "
             "Put a file you made on its own line as an absolute path. Offer a button as [label](add:Type), [label](set:@title#i.key=value), "
             "[label](go:@title#i), [label](undo:now), [label](open:panel) or [label](web:https://...).\n"
             "- When asked about shortcuts, the website, the repository, or code languages, point to the matching command or page above.\n\n";
   }
}
