#include "app/graph/UserStyle.h"
#include "app/graph/GenerateSamplesCritic.inc"

#include "audio/NoteModel.h"
#include "core/MovementLog.h"
#include "core/MovementStats.h"
#include "core/ParamRoles.h"
#include "platform/AppPaths.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace UserStyle
{
   namespace
   {
      std::string Slurp(const std::string& path)
      {
         std::ifstream f(AppPaths::FsPath(path), std::ios::binary | std::ios::ate);
         if (!f.is_open())
            return {};
         const size_t sz = (size_t)f.tellg();
         f.seekg(0, std::ios::beg);
         std::string text(sz, '\0');
         f.read(text.data(), (std::streamsize)sz);
         return text;
      }

      // velocity_profile.bin and quantize_profile.bin: a uint32 count, then that many int32.
      std::vector<int> ReadInts(const std::string& path)
      {
         const std::string raw = Slurp(path);
         std::vector<int> out;
         if (raw.size() < sizeof(uint32_t))
            return out;
         uint32_t count = 0;
         std::memcpy(&count, raw.data(), sizeof(count));
         if (raw.size() != sizeof(uint32_t) + (size_t)count * sizeof(int32_t))
            return out;
         for (uint32_t i = 0; i < count; i++)
         {
            int32_t v = 0;
            std::memcpy(&v, raw.data() + sizeof(uint32_t) + i * sizeof(int32_t), sizeof(v));
            out.push_back(v);
         }
         return out;
      }

      std::vector<NoteModel::Event> ReadEvents(const std::string& path)
      {
         std::vector<NoteModel::Event> out;
         const std::string raw = Slurp(path);
         if (!raw.empty() && !NoteModel::DecodeEvents(raw, out))
            out.clear();
         return out;
      }

      // Melodic moves, as scale degrees: 12 semitones is 7 degrees.
      void AddSteps(const std::vector<NoteModel::Event>& ev, double* hist, double& total)
      {
         for (size_t i = 1; i < ev.size(); i++)
         {
            if (ev[i].ioiTicks == 0)
               continue; // the same onset: a chord, not a move
            const int d = (int)ev[i].note - (int)ev[i - 1].note;
            if (d == 0 || std::abs(d) > 12)
               continue;
            const int deg = std::clamp((int)std::lround(d * 7.0 / 12.0), -2, 3);
            hist[deg + 2] += 1.0;
            total += 1.0;
         }
      }
   }

   Profile Read(const char* directory)
   {
      const std::string dir = directory;
      Profile p;
      const std::vector<NoteModel::Event> notes = ReadEvents(dir + "/notes_style.bin");
      const std::vector<NoteModel::Event> rhythm = ReadEvents(dir + "/rhythm_style.bin");
      const std::vector<int> ioi = ReadInts(dir + "/quantize_profile.bin");
      const std::vector<int> vel = ReadInts(dir + "/velocity_profile.bin");

      if (notes.size() >= 16)
      {
         double pc[12] = {};
         for (const NoteModel::Event& e : notes)
            pc[e.note % 12] += 1.0;
         for (int i = 0; i < 12; i++)
            p.pitchClass[i] = (float)(pc[i] / (double)notes.size());
         p.hasPitch = true;
      }
      {
         double steps[6] = {};
         double total = 0.0;
         AddSteps(notes, steps, total);
         AddSteps(rhythm, steps, total);
         if (total >= 16.0)
         {
            for (int i = 0; i < 6; i++)
               p.step[i] = (float)(steps[i] / total);
            p.hasPitch = true;
         }
      }

      // Timing: where notes start in the bar (both note stores carry it), and
      // how far the player's spacings sit off the 1/16 grid.
      {
         double onset[16] = {};
         size_t n = 0;
         for (const auto* set : { &notes, &rhythm })
            for (const NoteModel::Event& e : *set)
            {
               onset[e.metric & 15] += 1.0;
               n++;
            }
         if (n >= 16)
         {
            const double mx = *std::max_element(onset, onset + 16);
            for (int i = 0; i < 16; i++)
               p.onset[i] = (float)(onset[i] / std::max(1.0, mx));
            p.hasTiming = true;
         }
      }
      if (ioi.size() >= 16)
      {
         size_t off = 0, counted = 0;
         for (int t : ioi)
         {
            if (t <= 0)
               continue;
            counted++;
            if (t % (NoteModel::kTicksPerBeat / 4) != 0)
               off++;
         }
         if (counted > 0)
         {
            p.offGrid = (float)off / (float)counted;
            p.hasTiming = true;
         }
      }

      if (vel.size() >= 16)
      {
         double sum = 0.0;
         for (int v : vel)
         {
            const float f = std::clamp((float)v / 127.0f, 0.05f, 1.0f);
            p.vel.push_back(f);
            sum += f;
         }
         p.velMean = (float)(sum / (double)vel.size());
         p.hasVelocity = true;
      }
      // Note length and chord shapes: the notes store keeps every onset, and a spacing of 0 is a note
      // that starts with the one before it.
      {
         double dur = 0.0;
         size_t nd = 0;
         for (const auto* set : { &notes, &rhythm })
            for (const NoteModel::Event& e : *set)
               if (e.durTicks > 0)
               {
                  dur += (double)e.durTicks / (double)NoteModel::kTicksPerBeat;
                  nd++;
               }
         if (nd >= 16)
            p.durBeats = std::clamp((float)(dur / (double)nd), 0.05f, 4.0f);
         double sizes[4] = {};
         size_t chords = 0;
         for (const auto* set : { &notes, &rhythm })
            for (size_t i = 0; i < set->size();)
            {
               size_t j = i + 1;
               while (j < set->size() && (*set)[j].ioiTicks == 0)
                  j++;
               const size_t n = j - i;
               if (n >= 2)
               {
                  sizes[std::min<size_t>(n, 5) - 2] += 1.0;
                  chords++;
               }
               i = j;
            }
         if (chords >= 8)
         {
            for (int i = 0; i < 4; i++)
               p.chordSize[i] = (float)(sizes[i] / (double)chords);
            p.hasChords = true;
         }
         auto w = [](double n, double n0) { return (float)std::min((double)kMaxWeight, n / (n + n0) * (double)kMaxWeight / 0.95); };
         p.pitchW = p.hasPitch ? w((double)(notes.size() + rhythm.size()), 120.0) : 0.0f;
         p.timingW = p.hasTiming ? w((double)(notes.size() + rhythm.size() + ioi.size()), 120.0) : 0.0f;
         p.velW = p.hasVelocity ? w((double)vel.size(), 60.0) : 0.0f;
         p.durW = p.durBeats > 0.0f ? w((double)(notes.size() + rhythm.size()), 120.0) : 0.0f;
         p.chordW = p.hasChords ? w((double)chords, 12.0) : 0.0f;
      }
      p.valid = p.hasPitch || p.hasTiming || p.hasVelocity;
      return p;
   }

   namespace
   {
      std::string gOverride;
      bool gOverrideChanged = false;
   }

   void ResetTaste();

   void UseDirectory(const std::string& directory)
   {
      gOverride = directory;
      ResetTaste();
      gOverrideChanged = true;
   }

   const Profile& Get()
   {
      static Profile cached;
      static std::filesystem::file_time_type stamp{};
      static bool loaded = false;
      const std::string dir = gOverride.empty() ? AppPaths::AppSupportDir() + "/prediction" : gOverride;

      // The newest of the four files: a changed profile reloads, an untouched one costs four stats.
      std::filesystem::file_time_type newest{};
      std::error_code ec;
      for (const char* name : { "notes_style.bin", "rhythm_style.bin", "quantize_profile.bin", "velocity_profile.bin" })
      {
         const auto t = std::filesystem::last_write_time(AppPaths::FsPath(dir + "/" + name), ec);
         if (!ec && t > newest)
            newest = t;
      }
      if (!loaded || newest != stamp || gOverrideChanged)
      {
         gOverrideChanged = false;
         cached = Read(dir.c_str());
         stamp = newest;
         loaded = true;
      }
      return cached;
   }
   // ---- tempo: how long the transport has run at each bpm, from the movement log -----------------
   namespace
   {
      struct TempoState
      {
         std::mutex m;
         std::vector<double> bins = std::vector<double>(241, 0.0); // seconds at each whole bpm
         bool ready = false;
         bool started = false;
      };
      TempoState& Tempo()
      {
         static TempoState* t = new TempoState(); // leaked: the worker may outlive static destruction
         return *t;
      }

      void TempoWorker(std::string supportDir)
      {
         const std::string logDir = supportDir + "/movement-log";
         const std::string cache = supportDir + "/prediction/tempo_profile.txt";
         std::vector<double> bins(241, 0.0);
         std::set<std::string> done;
         {
            std::ifstream f(AppPaths::FsPath(cache));
            std::string tag;
            while (f >> tag)
            {
               if (tag == "file")
               {
                  std::string name;
                  f >> name;
                  done.insert(name);
               }
               else if (tag == "bin")
               {
                  int b = 0;
                  double w = 0.0;
                  f >> b >> w;
                  if (b >= 0 && b <= 240)
                     bins[(size_t)b] += w;
               }
            }
         }
         std::vector<std::string> files;
         std::error_code ec;
         for (const auto& e : std::filesystem::directory_iterator(AppPaths::FsPath(logDir), ec))
            if (e.path().extension() == ".z" || e.path().extension() == ".mlog")
               files.push_back(e.path().filename().string());
         std::sort(files.begin(), files.end());
         // The newest file may still be open for writing: leave it for next time.
         if (!files.empty())
            files.pop_back();
         bool changed = false;
         for (const std::string& name : files)
         {
            if (done.count(name))
               continue;
            double last = -1.0;
            MovementLog::ReadFile(logDir + "/" + name, [&](const MovementLog::Record& r)
            {
               if (r.type != MovementLog::Record::Type::Transport)
                  return true;
               if (last >= 0.0 && r.transport.isPlaying && r.transport.bpm >= 40.0f && r.transport.bpm <= 240.0f)
                  bins[(size_t)std::lround(r.transport.bpm)] += std::min(60.0, (double)r.transport.dt_ms / 1000.0);
               last = 0.0;
               return true;
            });
            done.insert(name);
            changed = true;
         }
         if (changed)
         {
            std::error_code ec2;
            std::filesystem::create_directories(AppPaths::FsPath(supportDir + "/prediction"), ec2);
            std::ofstream f(AppPaths::FsPath(cache), std::ios::trunc);
            for (const std::string& n : done)
               f << "file " << n << "\n";
            for (int b = 0; b <= 240; b++)
               if (bins[(size_t)b] > 0.0)
                  f << "bin " << b << " " << bins[(size_t)b] << "\n";
         }
         TempoState& t = Tempo();
         std::lock_guard<std::mutex> lk(t.m);
         t.bins = bins;
         t.ready = true;
      }
   }

   bool DrawTempo(double u, float& bpm, float& weight)
   {
      TempoState& t = Tempo();
      {
         std::lock_guard<std::mutex> lk(t.m);
         if (!t.started)
         {
            t.started = true;
            std::thread(TempoWorker, AppPaths::AppSupportDir()).detach();
         }
         if (!t.ready)
            return false;
         // Smooth a little (a hand-set tempo wobbles a bpm or two), then draw.
         double total = 0.0;
         std::vector<double> sm(241, 0.0);
         for (int b = 0; b <= 240; b++)
            for (int d = -2; d <= 2; d++)
               if (b + d >= 0 && b + d <= 240)
                  sm[(size_t)b] += t.bins[(size_t)(b + d)] * (d == 0 ? 1.0 : std::abs(d) == 1 ? 0.5 : 0.2);
         for (double v : sm)
            total += v;
         if (total < 600.0) // under ten minutes of running: nothing to say yet
            return false;
         double r = u * total;
         for (int b = 0; b <= 240; b++)
         {
            r -= sm[(size_t)b];
            if (r <= 0.0)
            {
               bpm = (float)b;
               break;
            }
         }
         weight = (float)std::min((double)kMaxWeight, total / (total + 3600.0));
      }
      return bpm >= 40.0f;
   }

   // ---- knob habits: pooled statistics from stats.bin -------------------------------------------
   namespace
   {
      struct KnobState
      {
         std::mutex m;
         bool started = false, ready = false;
         MovementStats::Engine* engine = nullptr; // leaked with the worker
      };
      KnobState& K()
      {
         static KnobState* k = new KnobState();
         return *k;
      }
      void KnobWorker(std::string supportDir)
      {
         auto* e = new MovementStats::Engine();
         std::string last;
         const bool ok = MovementStats::LoadStatsFile(MovementStats::StatsPath(supportDir + "/movement-log"), *e, last);
         KnobState& k = K();
         std::lock_guard<std::mutex> lk(k.m);
         if (ok)
            k.engine = e;
         else
            delete e;
         k.ready = true;
      }
      ParamRoles::Range RoleRange(const std::string& role)
      {
         if (role == "filter.cutoff")
            return ParamRoles::UnitRange(ParamRoles::Unit::Log2Hz);
         if (role.rfind("env.", 0) == 0 && role != "env.sustain")
            return ParamRoles::UnitRange(ParamRoles::Unit::LogSec);
         return ParamRoles::UnitRange(ParamRoles::Unit::Pos);
      }
   }

   bool DrawFromHistogram(const float* hist, float lo, float hi, double u, float& value)
   {
      constexpr int n = MovementStats::kBins;
      float sm[n];
      MovementStats::SmoothBins(hist, sm, false);
      float peak = 0.0f;
      for (int i = 0; i < n; i++)
         peak = std::max(peak, sm[i]);
      if (!(peak > 0.0f))
         return false;
      // Under 3% of the busiest bin is wobble, not a place the knob lives.
      double total = 0.0;
      for (int i = 0; i < n; i++)
      {
         if (sm[i] < 0.03f * peak)
            sm[i] = 0.0f;
         total += sm[i];
      }
      double r = std::clamp(u, 0.0, 1.0) * total;
      for (int i = 0; i < n; i++)
      {
         if (sm[i] <= 0.0f)
            continue;
         r -= sm[i];
         if (r <= 0.0)
         {
            const double frac = sm[i] > 0.0f ? std::clamp(1.0 + r / sm[i], 0.0, 1.0) : 0.5; // place inside the bin
            value = lo + (hi - lo) * (float)((i + frac) / n);
            return true;
         }
      }
      return false;
   }

   bool DrawKnob(const char* role, double u, float& value, float& weight)
   {
      KnobState& k = K();
      const MovementStats::ParamStats* st = nullptr;
      {
         std::lock_guard<std::mutex> lk(k.m);
         if (!k.started)
         {
            k.started = true;
            std::thread(KnobWorker, AppPaths::AppSupportDir()).detach();
         }
         if (!k.ready || k.engine == nullptr)
            return false;
         st = k.engine->FindRole(role); // the engine is never changed after the load
      }
      if (st == nullptr)
         return false;
      // About a minute of real moving before it counts for anything (nEff counts 10 Hz samples).
      const double n0 = 600.0;
      if (st->nEff < n0 * 0.5)
         return false;
      const ParamRoles::Range r = RoleRange(role);
      if (!DrawFromHistogram(st->hist, r.lo, r.hi, u, value))
         return false;
      weight = (float)std::min((double)kMaxWeight, st->nEff / (st->nEff + n0) * 0.95);
      return true;
   }

   // ---- taste: which style cards land -----------------------------------------------------------
   namespace
   {
      constexpr int kParts = 12, kCards = 64;
      struct Taste
      {
         float landed[kParts][kCards] = {};
         float skipped[kParts][kCards] = {};
         float featW[kFeatureDim] = {};
         float featPrec[kFeatureDim] = {};
         float featN = 0.0f;
         float pendingPhi[kFeatureDim] = {};
         bool hasPendingPhi = false;
         bool loaded = false;
         bool pending = false;
         int part = 0, card = 0;
         std::chrono::steady_clock::time_point at;
         Taste()
         {
            for (int i = 0; i < kFeatureDim; i++)
               featPrec[i] = 2.0f;
         }
      };
      Taste& T()
      {
         static Taste t;
         return t;
      }
      std::string TasteDir() { return gOverride.empty() ? AppPaths::AppSupportDir() + "/prediction" : gOverride; }
      std::string TastePath() { return TasteDir() + "/generate_taste.txt"; }
      std::string TasteVecPath() { return TasteDir() + "/generate_taste_vec.txt"; }

      void LoadTaste(Taste& t)
      {
         if (t.loaded)
            return;
         t.loaded = true;
         std::ifstream f(AppPaths::FsPath(TastePath()));
         int pa, c;
         float l, s;
         while (f >> pa >> c >> l >> s)
            if (pa >= 0 && pa < kParts && c >= 0 && c < kCards)
            {
               t.landed[pa][c] = l;
               t.skipped[pa][c] = s;
            }
         std::ifstream fv(AppPaths::FsPath(TasteVecPath()));
         if (fv >> t.featN)
            for (int i = 0; i < kFeatureDim; i++)
               fv >> t.featW[i] >> t.featPrec[i];
      }

      void SaveTaste(const Taste& t)
      {
         std::error_code ec;
         std::filesystem::create_directories(AppPaths::FsPath(TasteDir()), ec);
         std::ofstream f(AppPaths::FsPath(TastePath()), std::ios::trunc);
         for (int pa = 0; pa < kParts; pa++)
            for (int c = 0; c < kCards; c++)
               if (t.landed[pa][c] > 0.0f || t.skipped[pa][c] > 0.0f)
                  f << pa << " " << c << " " << t.landed[pa][c] << " " << t.skipped[pa][c] << "\n";
         if (t.featN > 0.0f)
         {
            std::ofstream fv(AppPaths::FsPath(TasteVecPath()), std::ios::trunc);
            fv << t.featN << "\n";
            for (int i = 0; i < kFeatureDim; i++)
               fv << t.featW[i] << " " << t.featPrec[i] << "\n";
         }
      }

      void Resolve(Taste& t, bool landed, float amount)
      {
         if (!t.pending)
            return;
         (landed ? t.landed : t.skipped)[t.part][t.card] += amount;
         if (t.card >= 0 && t.card < SampleCriticData::kStyleDirectiveCount)
         {
            // Connected Genre-Subgenre Network: propagate preference to neighboring sub-genres
            const SampleCriticData::StyleDirective& dir = SampleCriticData::kStyleDirectives[t.card];
            for (int k = 0; k < 4; k++)
            {
               const int nb = dir.neighbors[k];
               if (nb >= 0 && nb < kCards && nb != t.card)
                  (landed ? t.landed : t.skipped)[t.part][nb] += 0.20f * amount * dir.neighborW[k];
            }
         }
         if (t.hasPendingPhi)
         {
            const float y = landed ? 1.0f : -1.0f;
            double dot = 0.0;
            for (int i = 0; i < kFeatureDim; i++)
            {
               const double x = (i == kFeatureDim - 1) ? 0.1 : ((double)t.pendingPhi[i] - 0.35) * 2.0;
               dot += (double)t.featW[i] * x;
            }
            dot = std::clamp(dot, -8.0, 8.0);
            const double pred = 1.0 / (1.0 + std::exp(-y * dot));
            const double err = (1.0 - pred) * y * std::min(2.0f, amount);
            for (int i = 0; i < kFeatureDim; i++)
            {
               const float x = (i == kFeatureDim - 1) ? 0.1f : (t.pendingPhi[i] - 0.35f) * 2.0f;
               t.featPrec[i] += 0.15f * x * x;
               t.featW[i] += (float)((0.65 / std::sqrt(std::max(0.5f, t.featPrec[i]))) * err * x);
            }
            t.featN += amount;
            t.hasPendingPhi = false;
         }
         t.pending = false;
         SaveTaste(t);
      }
   }

   void ResetTaste() { T() = Taste(); }

   void NoteGenerate(int part, int card)
   {
      Taste& t = T();
      LoadTaste(t);
      const auto now = std::chrono::steady_clock::now();
      if (t.pending)
      {
         // Another generate straight after this one: it was not what they wanted.
         const double gap = std::chrono::duration<double>(now - t.at).count();
         Resolve(t, gap >= 20.0, 1.0f);
      }
      t.pending = true;
      t.part = std::clamp(part, 0, kParts - 1);
      t.card = std::clamp(card, 0, kCards - 1);
      t.at = now;
   }

   void NoteKept()
   {
      Taste& t = T();
      LoadTaste(t);
      Resolve(t, true, 3.0f);
   }

   // A loop saved in a patch and opened again was kept. Counted once per (part, card, seed): the
   // seen list is a small file beside the taste, newest 4000 only.
   void NoteSaved(int part, int card, int seed)
   {
      if (card < 0 || card >= kCards)
         return;
      Taste& t = T();
      LoadTaste(t);
      static std::set<std::string> seen;
      static bool loaded = false;
      const std::string path = TasteDir() + "/generate_seen.txt";
      if (!loaded)
      {
         loaded = true;
         std::ifstream f(AppPaths::FsPath(path));
         std::string k;
         while (f >> k)
            seen.insert(k);
      }
      const int pa = std::clamp(part, 0, kParts - 1);
      const std::string key = std::to_string(pa) + ":" + std::to_string(card) + ":" + std::to_string(seed);
      if (!seen.insert(key).second)
         return;
      t.landed[pa][card] += 1.0f;
      SaveTaste(t);
      std::error_code ec;
      std::filesystem::create_directories(AppPaths::FsPath(TasteDir()), ec);
      std::ofstream f(AppPaths::FsPath(path), std::ios::trunc);
      size_t skip = seen.size() > 4000 ? seen.size() - 4000 : 0;
      for (const std::string& k : seen)
         if (skip > 0)
            skip--;
         else
            f << k << "\n";
   }

   void SettleTaste()
   {
      Taste& t = T();
      if (t.pending && std::chrono::duration<double>(std::chrono::steady_clock::now() - t.at).count() > 45.0)
         Resolve(t, true, 1.0f);
   }

   int DrawCard(int part, double u, int cardCount)
   {
      Taste& t = T();
      LoadTaste(t);
      const int pa = std::clamp(part, 0, kParts - 1);
      cardCount = std::clamp(cardCount, 1, kCards);
      // A card's weight is its landing rate against a prior of 1 landing in 2; how much that rate
      // counts grows with the evidence for the part. The floor keeps every card drawable.
      double evidence = 0.0;
      for (int c = 0; c < cardCount; c++)
         evidence += t.landed[pa][c] + t.skipped[pa][c];
      const double m = std::min((double)kMaxWeight, evidence / (evidence + 12.0));
      std::vector<double> w((size_t)cardCount);
      double total = 0.0;
      for (int c = 0; c < cardCount; c++)
      {
         const double rate = (t.landed[pa][c] + 1.0) / (t.landed[pa][c] + t.skipped[pa][c] + 2.0);
         w[(size_t)c] = (1.0 - m) + m * (0.15 + 1.7 * rate);
         total += w[(size_t)c];
      }
      double r = u * total;
      for (int c = 0; c < cardCount; c++)
      {
         r -= w[(size_t)c];
         if (r <= 0.0)
            return c;
      }
      return cardCount - 1;
   }

   void NoteFeatures(const float phi[kFeatureDim])
   {
      Taste& t = T();
      LoadTaste(t);
      for (int i = 0; i < kFeatureDim; i++)
         t.pendingPhi[i] = std::clamp(phi[i], -1.0f, 1.0f);
      t.hasPendingPhi = true;
   }

   float ScoreFeatures(const float phi[kFeatureDim], double exploreU, float& weight)
   {
      Taste& t = T();
      LoadTaste(t);
      weight = (float)std::min((double)kMaxWeight, (double)t.featN / ((double)t.featN + 6.0));
      if (weight <= 1e-5f)
         return 0.5f;
      double dot = 0.0;
      const double jitter = (exploreU - 0.5) * 0.35;
      for (int i = 0; i < kFeatureDim; i++)
      {
         const double wi = t.featW[i] + jitter / std::sqrt(std::max(0.5f, t.featPrec[i]));
         const double x = (i == kFeatureDim - 1) ? 0.1 : (std::clamp((double)phi[i], -1.0, 1.0) - 0.35) * 2.0;
         dot += wi * x;
      }
      dot = std::clamp(dot, -8.0, 8.0);
      return (float)(1.0 / (1.0 + std::exp(-dot)));
   }
}
