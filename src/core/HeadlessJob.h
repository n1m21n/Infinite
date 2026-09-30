#pragma once

#include <string>
#include <vector>

// =========================================================================
// HeadlessJob - the command-line "run one job and exit" modes:
//
//   Infinite --render <patch.inf> <out.(mp4|mov|wav)> [--start S] [--duration S]
//            [--fps N] [--output <index|name>] [--sample-rate HZ] [--no-audio]
//            [--json <file>] [--timeout S] [--lenient]
//   Infinite --frame  <patch.inf> <T | T1,T2,...> <out.png | out_dir/>
//            [--fps N] [--output <index|name>] [--sample-rate HZ] [--json <file>] [--lenient]
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
      Describe,
      Validate,
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
      std::string describeType; // --describe <type>, empty = every type
      bool forRender = false;    // --validate --for-render: a missing Output is an error
      bool json = false; // --version --json
      // --lenient: warnings stay warnings. Without it every CLI mode is strict
      // and a warning the schema pass raised is promoted to an error (exit 3).
      bool lenient = false;
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
      std::string hint; // what to change, may be empty
      bool promoted = false; // a W_ warning that strict mode turned into an error
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

   // Warnings that describe a patch that still means what it says (a node that
   // reaches no output, no Output in a non-render check, a rounded duration).
   // Everything else is a sign the file does not do what its author wrote, so
   // strict mode refuses it.
   bool IsAdvisory(const std::string& code);
   // Strict mode: moves every non-advisory warning into `errors`, keeping its
   // W_ code and marking it promoted. Advisory ones stay in `warnings`.
   void PromoteWarnings(std::vector<Issue>& warnings, std::vector<Issue>& errors);

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

   // True when an .mp4/.mov has a top-level 'moov' box - the index an encoder
   // writes last. A file without one has a plausible size but no player can
   // open it, so a render only reports ok once this holds.
   bool MovieHasIndex(const std::string& path);
}
