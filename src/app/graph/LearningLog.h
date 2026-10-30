#pragma once

#include <cstdint>
#include <string>

#include "app/graph/SampleStyles.h"

// Local learning (docs/plans/data-universe/CLOSE-2026-10-30.md §5): what the player keeps, not only how they turn
// knobs. Everything stays on this machine, under AppSupportDir()/learning:
//
//   sessions.jsonl        one line per app run: session id, start, end, app version
//   takes/YYYYMMDD.jsonl  every Generate Samples take with its outcome: kept, rerolled, edited, deleted or ignored
//   audio/YYYYMMDD.jsonl  features of kept audio (RoleCritic::Measure's frame features), never the audio
//   midi/YYYYMMDD.jsonl   notes played in: on/off, velocity, time in beats, tempo, key, scale
//   patches/              patch text at save, one file per content hash, plus index.jsonl
//   profile/              derived only: user_profile.json, rebuilt at most once every 20 h
//
// One "Learn from my playing" switch stops every writer here (and the movement log); "Forget me" deletes the folder.
// Off in headless jobs and self-tests (any INFINITE_* variable), except that INFINITE_LEARNING_DIR points the log at
// a test folder and turns it on. Main thread only, except the profile rebuild, which runs on its own thread.
namespace LearningLog
{
   void Start(); // after MovementLog::Start
   void Stop();  // ends the session line, flushes, joins the profile thread

   bool Enabled(); // the setting (saved with the workspace settings)
   void SetEnabled(bool on);
   bool Active(); // enabled and a folder to write to
   std::string Dir();
   uint64_t FolderBytes();

   // Generate Samples. `owner` is the node the take landed in; a new take from the same owner settles the last one.
   void TakeMade(const void* owner, const SampleStyles::Recipe& r, int attempt, float bpm, int root, int scale);
   void TakeEdited(const void* owner);  // the node's settings moved off the take (counts within 30 s of it)
   void TakeDeleted(const void* owner); // the node went away
   // A take worth taking away (exported, sliced, made a node or patch): the outcome plus its audio features.
   void TakeKept(const void* owner, const char* how, const float* l, const float* r, int frames, double rate);

   void PatchSaved(const std::string& path);

   // Once per frame: reads played notes, settles takes nobody acted on, flushes, starts the profile rebuild when due.
   void Frame(double now);

   // Deletes the learning folder (the session goes on, writing into a fresh one).
   void ForgetMe();

   // Builds profile/user_profile.json from the logs now, on this thread. False with nothing to build from.
   bool RebuildProfile();

   // Tests: the settle clock and the time-based outcomes take this instead of the wall clock when >= 0.
   void SetClockForTest(double seconds);
}
