#include "HeadlessJob.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Headless
{
   namespace
   {
      bool ParseNumber(const char* s, double& out)
      {
         if (s == nullptr || *s == '\0')
            return false;
         char* end = nullptr;
         const double v = std::strtod(s, &end);
         if (end == s || *end != '\0')
            return false;
         out = v;
         return true;
      }

      std::string Num(double v)
      {
         char buf[64];
         std::snprintf(buf, sizeof(buf), "%.6g", v);
         return buf;
      }

      void IssuesJson(std::string& s, const std::vector<Issue>& list)
      {
         s += "[";
         for (size_t i = 0; i < list.size(); i++)
         {
            const Issue& is = list[i];
            if (i > 0)
               s += ",";
            s += "{\"code\":\"" + JsonEscape(is.code) + "\"";
            if (is.node >= 0)
               s += ",\"node\":" + std::to_string(is.node);
            if (is.line > 0)
               s += ",\"line\":" + std::to_string(is.line);
            s += ",\"message\":\"" + JsonEscape(is.message) + "\"}";
         }
         s += "]";
      }
   }

   bool ParseTimes(const std::string& list, std::vector<double>& out)
   {
      out.clear();
      size_t pos = 0;
      while (pos <= list.size())
      {
         size_t comma = list.find(',', pos);
         if (comma == std::string::npos)
            comma = list.size();
         double v = 0.0;
         const std::string tok = list.substr(pos, comma - pos);
         if (!ParseNumber(tok.c_str(), v) || v < 0.0)
            return false;
         out.push_back(v);
         pos = comma + 1;
      }
      std::sort(out.begin(), out.end());
      return !out.empty();
   }

   bool ParseArgs(int argc, char** argv, Job& job, std::string& usageError)
   {
      job = Job();
      usageError.clear();
      if (argc < 2 || argv[1] == nullptr)
         return false;

      const std::string first = argv[1];
      int positional = 0;
      if (first == "--render")
      {
         job.mode = Mode::Render;
         positional = 2;
      }
      else if (first == "--frame")
      {
         job.mode = Mode::Frame;
         positional = 3;
      }
      else if (first == "--version")
      {
         job.mode = Mode::Version;
         for (int i = 2; i < argc; i++)
            if (std::strcmp(argv[i], "--json") == 0)
               job.json = true;
         return true;
      }
      else
         return false;

      std::vector<std::string> pos;
      for (int i = 2; i < argc; i++)
      {
         const std::string a = argv[i];
         auto next = [&](const char* flag) -> const char*
         {
            if (i + 1 >= argc)
            {
               usageError = std::string(flag) + " needs a value";
               return nullptr;
            }
            return argv[++i];
         };
         auto numFlag = [&](const char* flag, double& dst) -> bool
         {
            const char* v = next(flag);
            if (v == nullptr)
               return false;
            if (!ParseNumber(v, dst))
            {
               usageError = std::string(flag) + " needs a number, got '" + v + "'";
               return false;
            }
            return true;
         };

         if (a == "--start")
         {
            if (!numFlag("--start", job.start))
               return true;
            if (job.start < 0.0)
            {
               usageError = "--start must be >= 0";
               return true;
            }
         }
         else if (a == "--duration")
         {
            if (!numFlag("--duration", job.duration))
               return true;
            if (job.duration <= 0.0)
            {
               usageError = "--duration must be > 0";
               return true;
            }
         }
         else if (a == "--fps")
         {
            double v = 0.0;
            if (!numFlag("--fps", v))
               return true;
            if (v < 1.0 || v > 240.0 || v != (double)(int)v)
            {
               usageError = "--fps must be a whole number 1..240";
               return true;
            }
            job.fps = (int)v;
         }
         else if (a == "--sample-rate")
         {
            if (!numFlag("--sample-rate", job.sampleRate))
               return true;
            if (job.sampleRate < 8000.0 || job.sampleRate > 384000.0)
            {
               usageError = "--sample-rate must be 8000..384000";
               return true;
            }
         }
         else if (a == "--timeout")
         {
            if (!numFlag("--timeout", job.timeoutSec))
               return true;
         }
         else if (a == "--output")
         {
            const char* v = next("--output");
            if (v == nullptr)
               return true;
            job.output = v;
         }
         else if (a == "--json")
         {
            const char* v = next("--json");
            if (v == nullptr)
               return true;
            job.jsonPath = v;
         }
         else if (a == "--no-audio")
            job.noAudio = true;
         else if (a.rfind("--", 0) == 0)
         {
            usageError = "unknown option " + a;
            return true;
         }
         else
            pos.push_back(a);
      }

      if ((int)pos.size() != positional)
      {
         usageError = job.mode == Mode::Render
                         ? "usage: Infinite --render <patch.inf> <out.mp4|mov|wav> [options]"
                         : "usage: Infinite --frame <patch.inf> <T[,T...]> <out.png|out_dir/> [options]";
         return true;
      }

      job.patch = pos[0];
      if (job.mode == Mode::Render)
         job.out = pos[1];
      else
      {
         if (!ParseTimes(pos[1], job.times))
         {
            usageError = "--frame times must be comma-separated non-negative seconds, got '" + pos[1] + "'";
            return true;
         }
         job.out = pos[2];
      }
      return true;
   }

   int ExitCodeFor(const Status& status)
   {
      if (status.ok && status.errors.empty())
         return 0;
      if (status.errors.empty())
         return 5;
      const std::string& c = status.errors.front().code;
      if (c == "E_USAGE" || c == "E_UNSUPPORTED_CONTAINER")
         return 2;
      if (c == "E_LOAD" || c == "E_NO_OUTPUT" || c == "E_AMBIGUOUS_OUTPUT" || c == "E_UNKNOWN_TYPE" ||
          c == "E_BAD_SLOT" || c == "E_KIND_MISMATCH" || c == "E_DANGLING" || c == "E_CYCLE")
         return 3;
      if (c == "E_HARDWARE_SOURCE")
         return 4;
      if (c == "E_TIMEOUT")
         return 6;
      return 5;
   }

   std::string JsonEscape(const std::string& s)
   {
      std::string o;
      o.reserve(s.size() + 8);
      for (unsigned char c : s)
      {
         switch (c)
         {
         case '"': o += "\\\""; break;
         case '\\': o += "\\\\"; break;
         case '\n': o += "\\n"; break;
         case '\r': o += "\\r"; break;
         case '\t': o += "\\t"; break;
         default:
            if (c < 0x20)
            {
               char buf[8];
               std::snprintf(buf, sizeof(buf), "\\u%04x", c);
               o += buf;
            }
            else
               o += (char)c;
         }
      }
      return o;
   }

   std::string StatusToJson(const Status& st)
   {
      std::string s = "{";
      s += std::string("\"ok\":") + (st.ok && st.errors.empty() ? "true" : "false");
      s += ",\"mode\":\"" + JsonEscape(st.mode) + "\"";
      s += ",\"patch\":\"" + JsonEscape(st.patch) + "\"";
      s += ",\"out\":\"" + JsonEscape(st.out) + "\"";
      s += ",\"frames\":" + std::to_string(st.frames);
      s += ",\"fps\":" + std::to_string(st.fps);
      s += ",\"size\":[" + std::to_string(st.width) + "," + std::to_string(st.height) + "]";
      s += ",\"audio\":{\"sample_rate\":" + Num(st.audioSampleRate) + ",\"frames\":" +
           std::to_string(st.audioFrames) + "}";
      s += ",\"elapsed_ms\":" + std::to_string(st.elapsedMs);
      if (!st.statusText.empty())
         s += ",\"status\":\"" + JsonEscape(st.statusText) + "\"";
      s += ",\"files\":[";
      for (size_t i = 0; i < st.files.size(); i++)
         s += (i ? ",\"" : "\"") + JsonEscape(st.files[i]) + "\"";
      s += "]";
      for (const std::string& frag : st.extraJson)
         s += "," + frag;
      s += ",\"warnings\":";
      IssuesJson(s, st.warnings);
      s += ",\"errors\":";
      IssuesJson(s, st.errors);
      s += "}";
      return s;
   }

   int Emit(const Job& job, const Status& status)
   {
      const std::string json = StatusToJson(status);
      if (!job.jsonPath.empty())
      {
         if (FILE* f = std::fopen(job.jsonPath.c_str(), "wb"))
         {
            std::fwrite(json.data(), 1, json.size(), f);
            std::fputc('\n', f);
            std::fclose(f);
         }
         else
            std::fprintf(stderr, "could not write %s\n", job.jsonPath.c_str());
      }
      std::fprintf(stdout, "%s\n", json.c_str());
      std::fflush(stdout);
      return ExitCodeFor(status);
   }
}
