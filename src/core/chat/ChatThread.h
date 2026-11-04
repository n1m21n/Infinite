#pragma once
// A saved chat (docs/plans/agent-collab/README.md C7): every message and, per agent, the session it
// resumes, written as one JSON file per chat so reopening Infinite brings the chat back and the next
// message carries on the same agent conversations. Plain data, no app types.
#include "ChatDiff.h"

#include <map>
#include <string>
#include <vector>

namespace chat
{
   struct Entry
   {
      enum class Kind { User, Agent, Change, Steps, Note, Error };
      Kind kind = Kind::Note;
      std::string agent;               // Agent, Steps, Error: which agent ("claude", "codex", "antigravity")
      std::string text;                // User, Agent, Note, Error
      std::vector<std::string> lines;  // Steps: what the agent ran; Change: the `infinite` lines of a direct command
      std::vector<ChangeLine> changes; // Change: what the patch now differs in
      std::vector<std::string> files;  // User: attached files
      bool ok = true;
      bool undone = false;             // Change from a direct command
      std::string detail;              // Error: the raw output behind the plain words (not saved)
   };

   struct Session
   {
      std::string id;
      bool live = false;  // the agent knows the id; the next turn resumes it
   };

   struct Thread
   {
      std::string id;
      std::string title;   // the first message, cut short
      long long updated = 0;
      std::map<std::string, Session> sessions;  // by agent id
      std::vector<Entry> entries;
   };

   struct ThreadInfo
   {
      std::string id, title, path;
      long long updated = 0;
      std::vector<std::string> agents;  // who answered, in order of first reply
   };

   std::string ToJson(const Thread& t);
   bool FromJson(const std::string& text, Thread& t);
   std::string TitleFor(const std::string& firstMessage);
   // A thread's row in the history list.
   ThreadInfo InfoOf(const Thread& t, const std::string& path);

   // One file per chat in `dir`: <id>.json. Newest first.
   bool SaveThread(const std::string& dir, const Thread& t);
   bool LoadThread(const std::string& path, Thread& t);
   std::vector<ThreadInfo> ListThreads(const std::string& dir);
   void DeleteThread(const std::string& dir, const std::string& id);
}
