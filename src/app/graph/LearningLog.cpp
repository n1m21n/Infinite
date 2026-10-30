#include "app/graph/LearningLog.h"

#include "app/AppCommon.h"
#include "app/graph/RoleCritic.h"
#include "app/graph/UserStyle.h"
#include "core/MovementLog.h"
#include "core/Transport.h"
#include "platform/AppPaths.h"
#include "platform/Platform.h"

#include "json.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define LEARN_ENVIRON _environ
#else
extern char** environ;
#define LEARN_ENVIRON environ
#endif

namespace fs = std::filesystem;
using nlohmann::json;

namespace LearningLog
{
   namespace
   {
      constexpr double kEditWindow = 30.0;   // a knob moved this soon after a take: "edited"
      constexpr double kIgnoreAfter = 45.0;  // nothing done with a take for this long: settled as "ignored" (or "edited")
      constexpr double kMidiFlushEvery = 10.0;
      constexpr double kProfileEvery = 20.0 * 3600.0;
      constexpr double kProfileCheckEvery = 600.0;
      constexpr uint64_t kRetentionCap = 500ull * 1024 * 1024; // raw logs; profile/ is never dropped
      constexpr double kTrustN0 = 20.0;                         // trust n / (n + n0), as UserStyle's

      bool gEnabled = true;
      bool gStarted = false;
      bool gTestMode = false; // INFINITE_LEARNING_DIR: never touch the player's own folders
      bool gForgotten = false;
      // The derived profiles a starter seeds and Forget me deletes (AppSupportDir()/prediction).
      const char* const kProfileFiles[] = { "notes_style.bin",    "rhythm_style.bin",  "velocity_profile.bin",
                                            "quantize_profile.bin", "tempo_profile.txt", "color_stats.bin",
                                            "generate_taste.txt", "generate_taste_vec.txt", "generate_seen.txt" };
      std::string gDir; // empty: off for this run
      std::string gSession;
      double gTestClock = -1.0;
      std::chrono::steady_clock::time_point gT0 = std::chrono::steady_clock::now();

      struct Pending
      {
         std::string id;
         json line; // the take's fields, waiting for its outcome
         double made = 0.0;
         bool edited = false;
      };
      std::map<const void*, Pending> gPending;
      std::map<const void*, std::string> gLastTake; // a take settled already can still be kept later
      int gTakeCounter = 0;

      unsigned long long gMidiCursor = 0;
      bool gMidiCursorSet = false;
      std::vector<std::string> gMidiLines;
      double gLastMidiFlush = 0.0;
      double gLastProfileCheck = -1e9;
      std::thread gProfileThread;
      std::atomic<bool> gProfileBusy{ false };

      double Now()
      {
         if (gTestClock >= 0.0)
            return gTestClock;
         return std::chrono::duration<double>(std::chrono::steady_clock::now() - gT0).count();
      }

      std::string Stamp(const char* fmt)
      {
         std::time_t t = std::time(nullptr);
         std::tm tm{};
#if defined(_WIN32)
         localtime_s(&tm, &t);
#else
         localtime_r(&t, &tm);
#endif
         char buf[32];
         std::strftime(buf, sizeof buf, fmt, &tm);
         return buf;
      }

      std::string IsoNow() { return Stamp("%Y-%m-%dT%H:%M:%S"); }

      bool Writing() { return gEnabled && !gDir.empty(); }

      void AppendLine(const std::string& sub, const std::string& file, const std::string& line)
      {
         std::error_code ec;
         const std::string dir = sub.empty() ? gDir : gDir + "/" + sub;
         fs::create_directories(AppPaths::FsPath(dir), ec);
         std::ofstream f(AppPaths::FsPath(dir + "/" + file), std::ios::app);
         f << line << "\n";
      }

      void AppendDaily(const char* sub, const json& j) { AppendLine(sub, Stamp("%Y%m%d") + ".jsonl", j.dump()); }

      void WriteOutcome(const Pending& p, const char* outcome)
      {
         json j = p.line;
         j["outcome"] = outcome;
         j["settled"] = IsoNow();
         AppendDaily("takes", j);
      }

      void FlushMidi()
      {
         if (gMidiLines.empty())
            return;
         if (Writing())
         {
            std::error_code ec;
            fs::create_directories(AppPaths::FsPath(gDir + "/midi"), ec);
            std::ofstream f(AppPaths::FsPath(gDir + "/midi/" + Stamp("%Y%m%d") + ".jsonl"), std::ios::app);
            for (const std::string& l : gMidiLines)
               f << l << "\n";
         }
         gMidiLines.clear();
      }

      // The oldest raw logs go first once the folder passes the cap; profile/ and sessions.jsonl stay.
      void Prune()
      {
         std::error_code ec;
         struct F
         {
            fs::path p;
            fs::file_time_type t;
            uint64_t size;
         };
         std::vector<F> files;
         uint64_t total = 0;
         for (const char* sub : { "midi", "takes", "audio", "patches" })
         {
            const fs::path d = AppPaths::FsPath(gDir + "/" + sub);
            if (!fs::is_directory(d, ec))
               continue;
            for (const auto& e : fs::directory_iterator(d, ec))
            {
               if (!e.is_regular_file(ec) || e.path().filename() == "index.jsonl")
                  continue;
               const uint64_t s = (uint64_t)e.file_size(ec);
               files.push_back({ e.path(), e.last_write_time(ec), s });
               total += s;
            }
         }
         if (total <= kRetentionCap)
            return;
         std::sort(files.begin(), files.end(), [](const F& a, const F& b) { return a.t < b.t; });
         for (const F& f : files)
         {
            if (total <= kRetentionCap)
               break;
            fs::remove(f.p, ec);
            total -= f.size;
         }
      }

      void WriteSessionLine(const char* what)
      {
         json j = { { "session", gSession }, { "event", what }, { "time", IsoNow() }, { "version", INFINITE_VERSION_STRING },
                    { "learning", gEnabled } };
         AppendLine("", "sessions.jsonl", j.dump());
      }

      std::string FileText(const std::string& path)
      {
         std::ifstream f(AppPaths::FsPath(path), std::ios::binary);
         std::ostringstream ss;
         ss << f.rdbuf();
         return ss.str();
      }

      std::vector<json> ReadJsonl(const std::string& sub)
      {
         std::vector<json> out;
         std::error_code ec;
         const fs::path d = AppPaths::FsPath(gDir + "/" + sub);
         if (!fs::is_directory(d, ec))
            return out;
         std::vector<fs::path> paths;
         for (const auto& e : fs::directory_iterator(d, ec))
            if (e.path().extension() == ".jsonl" && e.path().filename() != "index.jsonl")
               paths.push_back(e.path());
         std::sort(paths.begin(), paths.end());
         for (const fs::path& p : paths)
         {
            std::ifstream f(p);
            std::string line;
            while (std::getline(f, line))
            {
               json j = json::parse(line, nullptr, false);
               if (!j.is_discarded() && j.is_object())
                  out.push_back(std::move(j));
            }
         }
         return out;
      }

      double Median(std::vector<double> v)
      {
         if (v.empty())
            return 0.0;
         std::sort(v.begin(), v.end());
         const size_t n = v.size();
         return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
      }

      double Quantile(std::vector<double> v, double q)
      {
         if (v.empty())
            return 0.0;
         std::sort(v.begin(), v.end());
         return v[(size_t)std::clamp(std::lround(q * (double)(v.size() - 1)), 0l, (long)v.size() - 1)];
      }

      double Trust(double n) { return n / (n + kTrustN0); }

      json Shares(const std::vector<double>& counts)
      {
         double sum = 0.0;
         for (double c : counts)
            sum += c;
         json a = json::array();
         for (double c : counts)
            a.push_back(sum > 0.0 ? std::round(1e4 * c / sum) / 1e4 : 0.0);
         return a;
      }

      // Taste: the last outcome of each take wins, except that a take kept at any point counts as kept.
      json BuildTaste(int& nTakes)
      {
         std::map<std::string, json> takes;
         std::map<std::string, bool> kept;
         for (const json& j : ReadJsonl("takes"))
         {
            const std::string id = j.value("id", "");
            if (id.empty())
               continue;
            if (j.contains("part"))
               takes[id] = j;
            else if (takes.count(id))
               takes[id]["outcome"] = j.value("outcome", "");
            if (j.value("outcome", "") == "kept")
               kept[id] = true;
         }
         struct Count
         {
            double n = 0, kept = 0, rerolled = 0;
         };
         std::map<std::string, Count> byPart, byStyle, byPair;
         std::map<std::string, double> outcomes;
         for (auto& [id, j] : takes)
         {
            const bool k = kept.count(id) > 0;
            const std::string out = k ? "kept" : j.value("outcome", "ignored");
            outcomes[out] += 1;
            const std::string part = j.value("part", ""), style = j.value("style", "");
            for (Count* c : { &byPart[part], &byStyle[style], &byPair[part + "|" + style] })
            {
               c->n += 1;
               c->kept += k ? 1 : 0;
               c->rerolled += out == "rerolled" ? 1 : 0;
            }
         }
         nTakes = (int)takes.size();
         auto table = [](const std::map<std::string, Count>& m) {
            json t = json::object();
            for (const auto& [k, c] : m)
               t[k] = { { "n", (int)c.n }, { "kept", (int)c.kept }, { "rerolled", (int)c.rerolled },
                        { "keep_rate", c.n > 0 ? std::round(1e4 * c.kept / c.n) / 1e4 : 0.0 },
                        { "trust", std::round(1e4 * Trust(c.n)) / 1e4 } };
            return t;
         };
         return { { "takes", nTakes }, { "outcomes", outcomes }, { "part", table(byPart) }, { "style", table(byStyle) },
                  { "part_style", table(byPair) }, { "trust", std::round(1e4 * Trust(nTakes)) / 1e4 } };
      }

      // Sound: the median of each kept-audio feature, per part.
      json BuildSound(int& nAudio)
      {
         std::map<std::string, std::vector<std::vector<double>>> byPart;
         nAudio = 0;
         for (const json& j : ReadJsonl("audio"))
         {
            if (!j.contains("features") || !j["features"].is_array())
               continue;
            std::vector<double> f;
            for (const json& v : j["features"])
               f.push_back(v.is_number() ? v.get<double>() : 0.0);
            byPart[j.value("part", "")].push_back(std::move(f));
            nAudio++;
         }
         json out = json::object();
         for (auto& [part, rows] : byPart)
         {
            const size_t dims = rows.front().size();
            json med = json::array();
            for (size_t d = 0; d < dims; d++)
            {
               std::vector<double> col;
               for (const auto& r : rows)
                  if (d < r.size())
                     col.push_back(r[d]);
               med.push_back(std::round(1e5 * Median(col)) / 1e5);
            }
            out[part] = { { "n", (int)rows.size() }, { "trust", std::round(1e4 * Trust((double)rows.size())) / 1e4 },
                          { "features", "role_critic_v1" }, { "median", med } };
         }
         return out;
      }

      // Harmony and melody from the notes played in.
      void BuildNotes(json& harmony, json& melody, int& nNotes)
      {
         std::vector<json> ons;
         for (json& j : ReadJsonl("midi"))
            if (j.value("on", false))
               ons.push_back(std::move(j));
         nNotes = (int)ons.size();
         std::vector<double> pc(12, 0.0), keys(12, 0.0), scales(64, 0.0), grid(16, 0.0), chord(8, 0.0);
         std::vector<double> pitch, vel, steps;
         double step = 0, leap = 0, rep = 0;
         std::map<std::string, std::pair<int, double>> lastBy; // device/channel -> last note, its time
         double groupT = -1e9;
         int groupN = 0;
         std::string groupSession;
         for (const json& j : ons)
         {
            const int note = j.value("note", 60);
            const double t = j.value("t", 0.0), beat = j.value("beat", 0.0);
            const std::string session = j.value("session", "");
            pc[((note % 12) + 12) % 12] += 1;
            pitch.push_back(note);
            vel.push_back(j.value("vel", 0.0));
            keys[((j.value("key", 0) % 12) + 12) % 12] += 1;
            scales[(size_t)std::clamp(j.value("scale", 0), 0, 63)] += 1;
            const double frac = beat - std::floor(beat);
            grid[(size_t)std::clamp((int)std::floor(frac * 16.0 + 0.5) % 16, 0, 15)] += 1;
            // A chord: notes starting within 30 ms of the first.
            if (session == groupSession && t - groupT < 0.03)
               groupN++;
            else
            {
               if (groupN > 0)
                  chord[(size_t)std::min(groupN, 7)] += 1;
               groupN = 1;
               groupT = t;
               groupSession = session;
            }
            const std::string who = session + "/" + std::to_string(j.value("dev", 0)) + "/" + std::to_string(j.value("ch", 0));
            auto it = lastBy.find(who);
            if (it != lastBy.end() && t - it->second.second > 0.03 && t - it->second.second < 2.0)
            {
               const int d = std::abs(note - it->second.first);
               (d == 0 ? rep : d <= 2 ? step : leap) += 1;
               steps.push_back(d);
            }
            lastBy[who] = { note, t };
         }
         if (groupN > 0)
            chord[(size_t)std::min(groupN, 7)] += 1;
         const double moves = step + leap + rep;
         harmony = { { "notes", nNotes }, { "trust", std::round(1e4 * Trust(nNotes / 50.0)) / 1e4 },
                     { "pitch_class", Shares(pc) }, { "key", Shares(keys) },
                     { "scale", Shares(std::vector<double>(scales.begin(), scales.begin() + 16)) },
                     { "chord_size", Shares(std::vector<double>(chord.begin() + 1, chord.end())) } };
         melody = { { "notes", nNotes }, { "trust", std::round(1e4 * Trust(nNotes / 50.0)) / 1e4 },
                    { "step", moves > 0 ? std::round(1e4 * step / moves) / 1e4 : 0.0 },
                    { "leap", moves > 0 ? std::round(1e4 * leap / moves) / 1e4 : 0.0 },
                    { "repeat", moves > 0 ? std::round(1e4 * rep / moves) / 1e4 : 0.0 },
                    { "interval_median", Median(steps) }, { "low", Quantile(pitch, 0.05) }, { "high", Quantile(pitch, 0.95) },
                    { "velocity_median", std::round(1e4 * Median(vel)) / 1e4 }, { "onset_grid16", Shares(grid) } };
      }

      void StartProfileIfDue(double now)
      {
         if (now - gLastProfileCheck < kProfileCheckEvery || gProfileBusy.load())
            return;
         gLastProfileCheck = now;
         std::error_code ec;
         const fs::path p = AppPaths::FsPath(gDir + "/profile/user_profile.json");
         if (fs::exists(p, ec))
         {
            const auto age = fs::file_time_type::clock::now() - fs::last_write_time(p, ec);
            if (std::chrono::duration<double>(age).count() < kProfileEvery)
               return;
         }
         if (gProfileThread.joinable())
            gProfileThread.join();
         gProfileBusy.store(true);
         gProfileThread = std::thread([] {
            RebuildProfile();
            gProfileBusy.store(false);
         });
      }
   } // namespace

   bool Enabled() { return gEnabled; }

   void SetEnabled(bool on)
   {
      if (on == gEnabled)
         return;
      if (!on)
      {
         // Off stops every writer: what was waiting is dropped, not written.
         gPending.clear();
         gMidiLines.clear();
      }
      gEnabled = on;
      if (gStarted && !gDir.empty())
      {
         const bool was = gEnabled;
         gEnabled = true; // the switch itself is recorded
         WriteSessionLine(on ? "learning_on" : "learning_off");
         gEnabled = was;
      }
   }

   bool Active() { return Writing(); }
   std::string Dir() { return gDir; }

   uint64_t FolderBytes()
   {
      if (gDir.empty())
         return 0;
      std::error_code ec;
      uint64_t total = 0;
      const fs::path d = AppPaths::FsPath(gDir);
      if (!fs::is_directory(d, ec))
         return 0;
      for (auto it = fs::recursive_directory_iterator(d, ec); it != fs::recursive_directory_iterator(); it.increment(ec))
         if (it->is_regular_file(ec))
            total += (uint64_t)it->file_size(ec);
      return total;
   }

   void Start(const std::string& starterDir)
   {
      if (gStarted)
         return;
      gStarted = true;
      gT0 = std::chrono::steady_clock::now();
      if (const char* dir = std::getenv("INFINITE_LEARNING_DIR"); dir && *dir)
      {
         gDir = dir;
         gEnabled = true;
         gTestMode = true;
      }
      else
      {
         for (char** env = LEARN_ENVIRON; env && *env; ++env)
            if (std::strncmp(*env, "INFINITE_", 9) == 0)
               return; // a self-test or headless job: never the player's folder
         gDir = AppPaths::AppSupportDir() + "/learning";
         if (!starterDir.empty())
            SeedStarter(starterDir, AppPaths::AppSupportDir() + "/prediction", AppPaths::AppSupportDir() + "/Infinite.starter-profile");
      }
      std::random_device rd;
      char tail[8];
      std::snprintf(tail, sizeof tail, "%04x", (unsigned)(rd() & 0xffff));
      gSession = Stamp("%Y%m%d-%H%M%S-") + tail;
      if (Writing())
      {
         Prune();
         WriteSessionLine("start");
      }
   }

   void Stop()
   {
      if (!gStarted)
         return;
      for (auto& [owner, p] : gPending)
         if (Writing())
            WriteOutcome(p, p.edited ? "edited" : "ignored");
      gPending.clear();
      FlushMidi();
      if (Writing())
         WriteSessionLine("end");
      if (gProfileThread.joinable())
         gProfileThread.join();
      gStarted = false;
   }

   void TakeMade(const void* owner, const SampleStyles::Recipe& r, int attempt, float bpm, int root, int scale)
   {
      if (!Writing() || !owner)
         return;
      auto it = gPending.find(owner);
      if (it != gPending.end())
      {
         WriteOutcome(it->second, it->second.edited ? "edited" : "rerolled");
         gPending.erase(it);
      }
      Pending p;
      p.id = gSession + "-" + std::to_string(++gTakeCounter);
      p.made = Now();
      p.line = { { "id", p.id }, { "session", gSession }, { "made", IsoNow() }, { "part", SampleStyles::PartName(r.part) },
                 { "style", r.style }, { "seed", r.seed + (uint32_t)attempt * 7919u }, { "attempt", attempt },
                 { "variation", r.variation }, { "bars", r.bars }, { "bpm", std::round(bpm * 100.0f) / 100.0 },
                 { "root", root }, { "scale", scale }, { "follow_bpm", r.bpm > 0.0f }, { "follow_key", r.key >= 0 },
                 { "tone", r.tone }, { "energy", r.energy }, { "space", r.space }, { "mood", r.mood },
                 { "version", INFINITE_VERSION_STRING } };
      gLastTake[owner] = p.id;
      gPending[owner] = std::move(p);
   }

   void TakeEdited(const void* owner)
   {
      auto it = gPending.find(owner);
      if (it != gPending.end() && !it->second.edited && Now() - it->second.made <= kEditWindow)
         it->second.edited = true;
   }

   void TakeDeleted(const void* owner)
   {
      auto it = gPending.find(owner);
      if (it != gPending.end())
      {
         if (Writing())
            WriteOutcome(it->second, "deleted");
         gPending.erase(it);
      }
      gLastTake.erase(owner);
   }

   void TakeKept(const void* owner, const char* how, const float* l, const float* r, int frames, double rate)
   {
      if (!Writing() || !owner)
         return;
      std::string id, part;
      auto it = gPending.find(owner);
      if (it != gPending.end())
      {
         json j = it->second.line;
         j["how"] = how;
         id = it->second.id;
         part = j.value("part", "");
         Pending p = it->second;
         p.line = j;
         WriteOutcome(p, "kept");
         gPending.erase(it);
      }
      else if (auto lt = gLastTake.find(owner); lt != gLastTake.end())
      {
         // Settled already (rerolled, ignored): kept later still counts, as its own line.
         id = lt->second;
         AppendDaily("takes", json{ { "id", id }, { "outcome", "kept" }, { "how", how }, { "settled", IsoNow() } });
      }
      else
         return;
      if (l && r && frames > 0)
      {
         const std::vector<double> f = RoleCritic::Measure(l, r, frames, rate);
         json a = json::array();
         for (double v : f)
            a.push_back(std::isfinite(v) ? std::round(v * 1e5) / 1e5 : 0.0);
         AppendDaily("audio", json{ { "id", id }, { "session", gSession }, { "part", part }, { "how", how },
                                    { "seconds", std::round(100.0 * frames / rate) / 100.0 }, { "features", a } });
      }
   }

   void PatchSaved(const std::string& path)
   {
      if (!Writing() || path.empty())
         return;
      const std::string text = FileText(path);
      if (text.empty() || text.size() > 8u * 1024 * 1024)
         return;
      uint64_t h = 1469598103934665603ull; // FNV-1a
      for (unsigned char c : text)
         h = (h ^ c) * 1099511628211ull;
      char hex[20];
      std::snprintf(hex, sizeof hex, "%016llx", (unsigned long long)h);
      std::error_code ec;
      fs::create_directories(AppPaths::FsPath(gDir + "/patches"), ec);
      const std::string out = gDir + "/patches/" + hex + ".inf";
      if (!fs::exists(AppPaths::FsPath(out), ec))
      {
         std::ofstream f(AppPaths::FsPath(out), std::ios::binary);
         f << text;
      }
      const size_t slash = path.find_last_of("/\\");
      AppendLine("patches", "index.jsonl",
                 json{ { "hash", hex }, { "session", gSession }, { "time", IsoNow() },
                       { "name", slash == std::string::npos ? path : path.substr(slash + 1) } }
                    .dump());
   }

   void Frame(double)
   {
      if (!gStarted || !Writing())
         return;
      const double now = Now();
      for (auto it = gPending.begin(); it != gPending.end();)
      {
         if (now - it->second.made > kIgnoreAfter)
         {
            WriteOutcome(it->second, it->second.edited ? "edited" : "ignored");
            it = gPending.erase(it);
         }
         else
            ++it;
      }

      if (!gMidiCursorSet)
      {
         gMidiCursor = Platform::MidiNoteStreamPosition(); // from now on, not what came before
         gMidiCursorSet = true;
      }
      Platform::MidiNoteMessage msgs[64];
      int n;
      while ((n = Platform::MidiReadNotesSince(gMidiCursor, msgs, 64)) > 0)
      {
         const Transport& tr = Transport::Instance();
         for (int i = 0; i < n; i++)
         {
            const Platform::MidiNoteMessage& m = msgs[i];
            json j = { { "session", gSession }, { "t", std::round(now * 1000.0) / 1000.0 },
                       { "beat", std::round(tr.Beats() * 1000.0) / 1000.0 }, { "bpm", std::round(tr.Tempo() * 100.0f) / 100.0 },
                       { "playing", tr.IsPlaying() }, { "key", tr.Key() }, { "scale", tr.Scale() },
                       { "dev", (unsigned long long)m.device }, { "ch", m.channel }, { "note", m.note },
                       { "vel", std::round(m.velocity01 * 1000.0f) / 1000.0 }, { "on", m.isNoteOn && m.velocity01 > 0.0f } };
            gMidiLines.push_back(j.dump());
         }
         if (n < 64)
            break;
      }
      if (gMidiLines.size() >= 256 || (!gMidiLines.empty() && now - gLastMidiFlush > kMidiFlushEvery))
      {
         FlushMidi();
         gLastMidiFlush = now;
      }
      if (gTestClock < 0.0)
         StartProfileIfDue(now);
   }

   bool MaySaveProfiles() { return gEnabled && !gForgotten; }

   bool SeedStarter(const std::string& starterDir, const std::string& predictionDir, const std::string& marker)
   {
      std::error_code ec;
      if (starterDir.empty() || fs::exists(AppPaths::FsPath(marker), ec) || !fs::is_directory(AppPaths::FsPath(starterDir), ec))
         return false;
      for (const char* f : kProfileFiles)
         if (fs::exists(AppPaths::FsPath(predictionDir + "/" + f), ec))
            return false; // this machine has learned already; it keeps its own
      fs::create_directories(AppPaths::FsPath(predictionDir), ec);
      int copied = 0;
      for (const char* f : kProfileFiles)
      {
         const fs::path src = AppPaths::FsPath(starterDir + "/" + f);
         if (fs::exists(src, ec) && fs::copy_file(src, AppPaths::FsPath(predictionDir + "/" + f), ec))
            copied++;
      }
      std::ofstream m(AppPaths::FsPath(marker), std::ios::trunc);
      m << "starter profile seeded " << IsoNow() << " (" << copied << " files)\n";
      return copied > 0;
   }

   void ForgetMe()
   {
      gForgotten = true;
      gPending.clear();
      gLastTake.clear();
      gMidiLines.clear();
      if (gProfileThread.joinable())
         gProfileThread.join();
      if (gDir.empty())
         return;
      std::error_code ec;
      fs::remove_all(AppPaths::FsPath(gDir), ec);
      gLastProfileCheck = Now(); // nothing to build from for a while
      if (gTestMode)
         return;
      const std::string pred = AppPaths::AppSupportDir() + "/prediction";
      for (const char* f : kProfileFiles)
         fs::remove(AppPaths::FsPath(pred + "/" + f), ec);
      const std::string moves = MovementLog::GetLogDirectory();
      if (!moves.empty())
         for (const auto& e : fs::directory_iterator(AppPaths::FsPath(moves), ec))
            fs::remove(e.path(), ec); // the open session file goes too where the platform allows it
      UserStyle::ResetTaste();
   }

   bool RebuildProfile()
   {
      if (gDir.empty())
         return false;
      int nTakes = 0, nAudio = 0, nNotes = 0;
      json taste = BuildTaste(nTakes), sound = BuildSound(nAudio), harmony, melody;
      BuildNotes(harmony, melody, nNotes);
      if (nTakes + nAudio + nNotes == 0)
         return false;
      int sessions = 0;
      {
         std::ifstream f(AppPaths::FsPath(gDir + "/sessions.jsonl"));
         std::string line;
         while (std::getline(f, line))
            sessions += line.find("\"start\"") != std::string::npos;
      }
      const json profile = { { "version", 1 }, { "built", IsoNow() }, { "app", INFINITE_VERSION_STRING },
                             { "sessions", sessions }, { "trust_n0", kTrustN0 }, { "taste", taste }, { "sound", sound },
                             { "harmony", harmony }, { "melody", melody } };
      std::error_code ec;
      fs::create_directories(AppPaths::FsPath(gDir + "/profile"), ec);
      const std::string tmp = gDir + "/profile/user_profile.json.tmp";
      {
         std::ofstream f(AppPaths::FsPath(tmp), std::ios::trunc);
         f << profile.dump(1);
         if (!f)
            return false;
      }
      fs::rename(AppPaths::FsPath(tmp), AppPaths::FsPath(gDir + "/profile/user_profile.json"), ec);
      return !ec;
   }

   void SetClockForTest(double seconds) { gTestClock = seconds; }
}
