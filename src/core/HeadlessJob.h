#pragma once

#include <string>
#include <vector>

// =========================================================================
// HeadlessJob - the command-line "run one job and exit" modes:
//
//   Infinite --render <patch.inf> <out.(mp4|mov|wav)> [--start S] [--duration S]
//            [--fps N] [--output <index|name>] [--sample-rate HZ] [--no-audio]
//            [--json <file>] [--timeout S]
//   Infinite --frame  <patch.inf> <T | T1,T2,...> <out.png | out_dir/>
//            [--fps N] [--output <index|name>] [--sample-rate HZ] [--json <file>]
//   Infinite --version --json
//
// Argument parsing, the status JSON and the exit-code table live here with no
// GL/ImGui dependency so they can be unit tested; main.cpp owns the job itself
// (docs/fix-briefs/headless-engine.md, block 1).
// =========================================================================

namespace Headless
{
   enum class Mode
   {
      None,
      Render,
      Frame,
      Version,
   };

   struct Job
   {
      Mode mode = Mode::None;
      std::string patch;
      std::string out;           // file for --render, file or directory for --frame
      std::string output;        // --output: node index or name; empty = the only Output
      std::string jsonPath;      // --json
      std::vector<double> times; // --frame timestamps, seconds, ascending
      double start = 0.0;        // --render range start, seconds
      double duration = -1.0;    // <= 0: use the Output's own duration
      int fps = 30;
      double sampleRate = 48000.0;
      bool noAudio = false;
      bool json = false; // --version --json
      double timeoutSec = 600.0;
   };

   // Every message the tool can emit carries one of these codes. The exit code
   // is derived from the FIRST error, see ExitCodeFor.
   struct Issue
   {
      std::string code; // E_* / W_*
      std::string message;
      int line = 0;     // patch line, 0 = none
      int node = -1;    // node index, -1 = none
   };

   struct Status
   {
      bool ok = false;
      std::string mode;
      std::string patch;
      std::string out;
      long long frames = 0;
      int fps = 0;
      int width = 0;
      int height = 0;
      double audioSampleRate = 0.0;
      long long audioFrames = 0;
      long long elapsedMs = 0;
      std::string statusText; // the Output's RecordStatus, when there is one
      std::vector<std::string> files; // every file written
      std::vector<std::string> extraJson; // pre-rendered `"key":value` fragments
      std::vector<Issue> warnings;
      std::vector<Issue> errors;
   };

   // True when argv names one of the job modes. On a malformed command line
   // returns true too, with `usageError` set (exit code 2); returns false for
   // an ordinary interactive launch (argv[1] is a patch path, a fixture flag,
   // or nothing).
   bool ParseArgs(int argc, char** argv, Job& job, std::string& usageError);

   // 0 ok, 2 usage, 3 patch load/validation, 4 refused (live source),
   // 5 render/encode failure, 6 timeout.
   int ExitCodeFor(const Status& status);

   std::string JsonEscape(const std::string& s);
   std::string StatusToJson(const Status& status);
   // Prints StatusToJson as the LAST line on stdout and, when the job has a
   // --json path, writes it there too. Returns the exit code.
   int Emit(const Job& job, const Status& status);

   // "0.5,1,2.25" -> {0.5, 1, 2.25}; false on any non-number or negative value.
   bool ParseTimes(const std::string& list, std::vector<double>& out);
}
