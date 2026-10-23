// Session view (clip launcher) engine: launch / stop / follow actions and the
// playback model the audio scheduler and the video compositor read.
//
// The idea comes from Ricardo Palmieri's Infinite-Turbo fork; this is a fresh
// implementation.
//
// How it plays: nothing new reaches the audio thread. While any lane has a
// launched slot, ArrangePlaybackModel() returns a copy of gArrange in which
// each launched slot is written out as repeated clips over the arrangement
// (PlaceOverwrite, so the timeline clips under it are cut away), up to a
// horizon a few dozen bars ahead. RebuildAudioTopology and the video
// compositor read that copy instead of gArrange, so a slot gets every clip
// feature for free: retrigger, sample position lock, fades, gain, pan, blend
// modes. A queued launch is already in the copy at its grid line, so its
// onset is sample accurate. The copy is rebuilt when the schedule or the
// arrangement changes, and when the playhead nears the horizon.
//
// Launch state is runtime only: never saved, cleared when the transport stops.
#include "app/AppShared.h"

namespace app
{
   bool gArrangeSessionView = false;

   namespace
   {
      using Arrange::Tick;

      // One change in a lane's schedule: from `at`, play `slot` (-1 = stop,
      // the lane goes back to the arrangement).
      struct SessSeg
      {
         Tick at = 0;
         int slot = -1;
      };

      // What a lane actually plays over [from, to), after follow actions are
      // expanded. `queued` = an explicit launch that has not started yet.
      struct SessRun
      {
         uint64_t laneId = 0;
         int slot = -1;
         Tick from = 0;
         Tick to = 0;
         bool explicitStart = false;
      };

      struct SessRecEntry
      {
         uint64_t laneId = 0;
         int slot = -1;
         Tick from = 0;
         Tick to = 0;
         Arrange::Clip clip;
      };

      constexpr Tick kHorizonTicks = Arrange::kTicksPerBar * 64;
      constexpr Tick kRebuildLeadTicks = Arrange::kTicksPerBar * 32;
      constexpr int kMaxExpandSteps = 4096;

      std::unordered_map<uint64_t, std::vector<SessSeg>> sSchedule;
      std::vector<SessRun> sRuns;
      Arrange::Model sPlay;
      bool sActive = false;
      bool sDirty = false;
      uint64_t sGeneration = 1;
      uint64_t sBuiltArrangeRevision = ~0ull;
      Tick sHorizon = 0;
      double sPrevBeats = 0.0;
      bool sPrevPlaying = false;
      // A scene's tempo, applied when its launch tick arrives.
      Tick sPendingTempoAt = -1;
      float sPendingTempo = 0.0f;
      // Record to Timeline.
      bool sRecording = false;
      Tick sRecStart = 0;
      std::vector<SessRecEntry> sRecLog;

      Tick NowTick()
      {
         return std::max<Tick>(0, Arrange::BeatsToTicks(Transport::Instance().Beats()));
      }

      uint64_t Mix64(uint64_t h)
      {
         h ^= h >> 31;
         h *= 0xD6E8FEB86659FD93ull;
         h ^= h >> 32;
         h *= 0x9E3779B97F4A7C15ull;
         h ^= h >> 29;
         return h;
      }

      // Stable across rebuilds, so a rebuild mid-clip keeps the same window.
      // Bit 62 set: real ids count up from 1 and the copy's own split ids
      // start at bit 61, so none of the three ranges can meet.
      uint64_t SyntheticClipId(uint64_t laneId, int slot, Tick start)
      {
         const uint64_t h = Mix64(laneId * 0x9E3779B97F4A7C15ull ^ (uint64_t)(slot + 1) * 0xC2B2AE3D27D4EB4Full ^
                                  (uint64_t)start * 0x165667B19E3779F9ull);
         return (h & ((1ull << 60) - 1)) | (1ull << 62);
      }

      const Arrange::SessionSlot* SlotAt(const Arrange::Lane& lane, int slot)
      {
         if (slot < 0 || slot >= (int)lane.slots.size() || !lane.slots[(size_t)slot].Has())
            return nullptr;
         return &lane.slots[(size_t)slot];
      }

      // Where a follow action goes. Next/prev wrap and skip empty slots;
      // random picks another filled slot (the same one only if it is alone),
      // seeded by lane and tick so every rebuild makes the same choice.
      int FollowTarget(const Arrange::Lane& lane, int slot, int action, Tick at)
      {
         const int n = (int)lane.slots.size();
         if (action == Arrange::kFollowStop || n == 0)
            return -1;
         if (action == Arrange::kFollowNext || action == Arrange::kFollowPrev)
         {
            const int dir = action == Arrange::kFollowNext ? 1 : -1;
            for (int k = 1; k <= n; k++)
            {
               const int s = ((slot + dir * k) % n + n) % n;
               if (lane.slots[(size_t)s].Has())
                  return s;
            }
            return -1;
         }
         if (action == Arrange::kFollowRandom)
         {
            std::vector<int> filled;
            for (int s = 0; s < n; s++)
               if (lane.slots[(size_t)s].Has() && s != slot)
                  filled.push_back(s);
            if (filled.empty())
               return SlotAt(lane, slot) ? slot : -1;
            const uint64_t h = Mix64(lane.id ^ ((uint64_t)at * 0x9E3779B97F4A7C15ull));
            return filled[(size_t)(h % filled.size())];
         }
         return slot;
      }

      // The clip a slot repeats: its length cut to the loop length.
      Arrange::Clip LoopClip(const Arrange::SessionSlot& ss)
      {
         Arrange::Clip c = ss.clip;
         c.length = ss.LoopLen();
         c.fadeOut = std::min(c.fadeOut, c.length);
         c.fadeIn = std::min(c.fadeIn, c.length);
         return c;
      }

      void ExpandLane(const Arrange::Lane& lane, const std::vector<SessSeg>& segs, Tick horizon,
                      std::vector<SessRun>& out)
      {
         for (size_t i = 0; i < segs.size(); i++)
         {
            const Tick t1 = i + 1 < segs.size() ? segs[i + 1].at : horizon;
            int slot = segs[i].slot;
            Tick cursor = segs[i].at;
            bool first = true;
            for (int step = 0; slot >= 0 && cursor < t1 && step < kMaxExpandSteps; step++)
            {
               const Arrange::SessionSlot* ss = SlotAt(lane, slot);
               if (ss == nullptr)
                  break;
               Tick runEnd = t1;
               Tick follow = -1;
               if (!ss->loop)
                  runEnd = std::min(runEnd, cursor + ss->LoopLen());   // one-shot
               if (ss->followAction != Arrange::kFollowNone)
               {
                  const Tick f = cursor + (Tick)std::clamp(ss->followBars, Arrange::kFollowBarsMin,
                                                           Arrange::kFollowBarsMax) * Arrange::kTicksPerBar;
                  if (f < runEnd)
                  {
                     runEnd = f;
                     follow = f;
                  }
               }
               out.push_back(SessRun{ lane.id, slot, cursor, runEnd, first });
               first = false;
               if (follow < 0)
                  break;
               slot = FollowTarget(lane, slot, ss->followAction, follow);
               cursor = follow;
            }
         }
      }

      // Writes `clip` repeated from `from` (phase anchor) over [lo, hi) into
      // `m`'s lane. Synthetic ids for the playback copy, fresh ids (0) when
      // recording into gArrange.
      void PlaceRepeats(Arrange::Model& m, uint64_t laneId, int slot, const Arrange::Clip& clip, Tick from, Tick lo,
                        Tick hi, bool synthetic)
      {
         const Tick len = clip.length;
         if (len <= 0 || hi <= lo)
            return;
         Tick k0 = lo > from ? (lo - from) / len : 0;
         for (Tick start = from + k0 * len; start < hi; start += len)
         {
            const Tick end = std::min(start + len, hi);
            if (end <= lo)
               continue;
            Arrange::Clip c = clip;
            c.groupId = 0;
            c.start = start;
            c.length = end - start;
            c.fadeIn = std::min(c.fadeIn, c.length);
            c.fadeOut = std::min(c.fadeOut, c.length);
            c.id = synthetic ? SyntheticClipId(laneId, slot, start) : 0;
            c.importPending = false;
            Arrange::PlaceOverwrite(m, laneId, c, nullptr);
         }
      }

      bool AnyScheduled()
      {
         for (const auto& kv : sSchedule)
            if (!kv.second.empty())
               return true;
         return false;
      }

      // Drops segments wholly in the past and re-anchors the current one at
      // the run that is playing now. Follow targets hash on the tick, and the
      // clip phase is anchored at the run start, so this changes nothing
      // audible; it only stops the expansion from replaying hours of history.
      void Compact(Tick now)
      {
         for (auto it = sSchedule.begin(); it != sSchedule.end();)
         {
            std::vector<SessSeg>& segs = it->second;
            size_t cur = 0;
            bool found = false;
            for (size_t i = 0; i < segs.size(); i++)
               if (segs[i].at <= now)
               {
                  cur = i;
                  found = true;
               }
            if (found && cur > 0)
               segs.erase(segs.begin(), segs.begin() + (long)cur);
            if (found && !segs.empty() && segs[0].slot < 0)
               segs.erase(segs.begin()); // stopped in the past: nothing to remember
            else if (found && !segs.empty())
            {
               for (const SessRun& r : sRuns)
                  if (r.laneId == it->first && r.from <= now && now < r.to && r.from >= segs[0].at)
                  {
                     segs[0].at = r.from;
                     segs[0].slot = r.slot;
                     break;
                  }
            }
            it = segs.empty() ? sSchedule.erase(it) : std::next(it);
         }
      }

      void Materialize(Tick now)
      {
         Compact(now);
         sRuns.clear();
         sActive = AnyScheduled();
         if (!sActive)
         {
            sPlay = Arrange::Model{};
            return;
         }
         sHorizon = (now / Arrange::kTicksPerBar) * Arrange::kTicksPerBar + kHorizonTicks;
         for (const Arrange::Lane& lane : gArrange.lanes)
         {
            auto it = sSchedule.find(lane.id);
            if (it != sSchedule.end())
               ExpandLane(lane, it->second, sHorizon, sRuns);
         }
         sPlay = gArrange;
         sPlay.nextId = 1ull << 61;
         const Tick lo = std::max<Tick>(0, now - Arrange::kTicksPerBar);
         for (const SessRun& r : sRuns)
         {
            const Arrange::Lane* lane = Arrange::FindLane(gArrange, r.laneId);
            const Arrange::SessionSlot* ss = lane ? SlotAt(*lane, r.slot) : nullptr;
            if (ss != nullptr && r.to > lo)
               PlaceRepeats(sPlay, r.laneId, r.slot, LoopClip(*ss), r.from, std::max(lo, r.from), r.to, true);
         }
      }

      void EnsureTimelineAudio()
      {
         if (gAudioMode == AudioMode::Timeline)
            return;
         gAudioMode = AudioMode::Timeline;
         if (AudioEngine::Instance().SampleRate() <= 0.0)
         {
            gAudioStartError.clear();
            if (!StartAudioEngine(gAudioStartError))
               fprintf(stderr, "audio device: %s\n", gAudioStartError.c_str());
         }
      }

      // Where a launch or stop asked for now lands: the next launch-grid line
      // while playing; right here when stopped (the click starts the
      // transport) or with no grid.
      Tick LaunchTick(bool startTransport, int quantize = -1)
      {
         Transport& tr = Transport::Instance();
         if (!tr.IsPlaying())
         {
            if (startTransport)
            {
               EnsureTimelineAudio();
               tr.SetPlaying(true);
            }
            return NowTick();
         }
         return std::max<Tick>(0, Arrange::BeatsToTicks(MusicTime::NextLaunchBeat(tr.Beats(), quantize >= 0 ? quantize : tr.LaunchQuantize())));
      }

      void Schedule(uint64_t laneId, Tick at, int slot)
      {
         std::vector<SessSeg>& segs = sSchedule[laneId];
         while (!segs.empty() && segs.back().at >= at)
            segs.pop_back();
         if (slot < 0 && segs.empty())
         {
            sSchedule.erase(laneId);
            sDirty = true;
            return;
         }
         segs.push_back(SessSeg{ at, slot });
         sDirty = true;
      }

      void UpdateRecordLog(Tick now)
      {
         for (const SessRun& r : sRuns)
         {
            if (r.from > now || r.to <= sRecStart)
               continue;
            const Arrange::Lane* lane = Arrange::FindLane(gArrange, r.laneId);
            const Arrange::SessionSlot* ss = lane ? SlotAt(*lane, r.slot) : nullptr;
            if (ss == nullptr)
               continue;
            const Tick to = std::min(r.to, now);
            bool found = false;
            for (SessRecEntry& e : sRecLog)
               if (e.laneId == r.laneId && e.slot == r.slot && e.from == r.from)
               {
                  e.to = std::max(e.to, to);
                  found = true;
                  break;
               }
            if (!found)
               sRecLog.push_back(SessRecEntry{ r.laneId, r.slot, r.from, to, LoopClip(*ss) });
         }
      }

      // Writes what was played while recording into the arrangement, as one
      // undo step. Played slots overwrite the timeline under them.
      void CommitRecording(Tick end)
      {
         if (sRecLog.empty())
            return;
         const Tick recStart = sRecStart;
         std::vector<SessRecEntry> log;
         log.swap(sRecLog);
         ArrangeEdit([&]()
         {
            for (const SessRecEntry& e : log)
            {
               const Tick lo = std::max(e.from, recStart);
               const Tick hi = std::min(e.to, end);
               if (Arrange::FindLane(gArrange, e.laneId) != nullptr)
                  PlaceRepeats(gArrange, e.laneId, e.slot, e.clip, e.from, lo, hi, false);
            }
         });
         gPatchDirty = true;
      }
   }


   const Arrange::Model& ArrangePlaybackModel()
   {
      // A render takes the arrangement as written, never a live launch.
      return sActive && !ArrangeRenderBusy() ? sPlay : gArrange;
   }


   uint64_t ArrangeSessionGeneration()
   {
      return sGeneration;
   }


   void ArrangeSessionTick()
   {
      Transport& tr = Transport::Instance();
      const bool playing = tr.IsPlaying();
      const double beats = tr.Beats();
      const Tick now = NowTick();

      if (!playing)
      {
         if (sRecording && sPrevPlaying)
         {
            UpdateRecordLog(Arrange::BeatsToTicks(sPrevBeats));
            CommitRecording(Arrange::BeatsToTicks(sPrevBeats));
         }
         if (!sSchedule.empty())
         {
            sSchedule.clear();
            sDirty = true;
         }
         sPendingTempoAt = -1;
      }
      else if (sPrevPlaying)
      {
         // A seek or a loop wrap: carry the schedule along with the playhead
         // so launched slots keep playing (and keep their phase when the jump
         // is a whole number of clip lengths, as a bar-aligned loop is).
         const double d = beats - sPrevBeats;
         if (d < -0.25 || d > 8.0)
         {
            const Tick shift = Arrange::BeatsToTicks(beats) - Arrange::BeatsToTicks(sPrevBeats);
            if (sRecording)
            {
               UpdateRecordLog(Arrange::BeatsToTicks(sPrevBeats));
               CommitRecording(Arrange::BeatsToTicks(sPrevBeats));
               sRecStart = now;
            }
            for (auto& kv : sSchedule)
               for (SessSeg& s : kv.second)
                  s.at = std::max<Tick>(0, s.at + shift);
            for (SessRun& r : sRuns)
            {
               r.from = std::max<Tick>(0, r.from + shift);
               r.to = std::max<Tick>(0, r.to + shift);
            }
            if (sPendingTempoAt >= 0)
               sPendingTempoAt = std::max<Tick>(0, sPendingTempoAt + shift);
            sDirty = true;
         }
      }
      sPrevBeats = beats;
      sPrevPlaying = playing;

      if (sPendingTempoAt >= 0 && now >= sPendingTempoAt)
      {
         tr.SetTempo(sPendingTempo);
         sPendingTempoAt = -1;
      }

      const bool wasActive = sActive;
      const bool wantActive = AnyScheduled();
      if (sDirty || (wantActive && (sBuiltArrangeRevision != gArrange.revision || now > sHorizon - kRebuildLeadTicks)) ||
          wantActive != wasActive)
      {
         Materialize(now);
         sBuiltArrangeRevision = gArrange.revision;
         sDirty = false;
         sGeneration++;
      }

      if (sRecording && playing)
         UpdateRecordLog(now);
   }


   void ArrangeSessionLaunch(uint64_t laneId, int slot)
   {
      const Arrange::Lane* lane = Arrange::FindLane(gArrange, laneId);
      if (lane == nullptr)
         return;
      if (SlotAt(*lane, slot) == nullptr)
      {
         ArrangeSessionStopLane(laneId);
         return;
      }
      const Arrange::SessionSlot* ss = SlotAt(*lane, slot);
      // Toggle: clicking the playing clip stops it.
      if (ss->launchMode == 2 && ArrangeSessionLaneQuery(laneId).playingSlot == slot)
      {
         ArrangeSessionStopLane(laneId);
         return;
      }
      Schedule(laneId, LaunchTick(true, ss->quantize), slot);
   }


   // The mouse let go of a cell: a Gate clip plays only while held.
   void ArrangeSessionRelease(uint64_t laneId, int slot)
   {
      const Arrange::Lane* lane = Arrange::FindLane(gArrange, laneId);
      const Arrange::SessionSlot* ss = lane ? SlotAt(*lane, slot) : nullptr;
      if (ss != nullptr && ss->launchMode == 1)
         ArrangeSessionStopLane(laneId);
   }


   void ArrangeSessionStopLane(uint64_t laneId)
   {
      if (!Transport::Instance().IsPlaying())
      {
         if (sSchedule.erase(laneId) > 0)
            sDirty = true;
         return;
      }
      if (sSchedule.find(laneId) == sSchedule.end())
         return;
      Schedule(laneId, LaunchTick(false), -1);
   }


   void ArrangeSessionStopAll()
   {
      std::vector<uint64_t> ids;
      for (const auto& kv : sSchedule)
         ids.push_back(kv.first);
      for (uint64_t id : ids)
         ArrangeSessionStopLane(id);
   }


   void ArrangeSessionLaunchScene(int scene)
   {
      if (scene < 0 || scene >= (int)gArrange.scenes.size())
         return;
      bool any = false;
      for (const Arrange::Lane& lane : gArrange.lanes)
         any = any || SlotAt(lane, scene) != nullptr;
      if (!any && sSchedule.empty())
         return;
      const Tick at = LaunchTick(any, gArrange.scenes[(size_t)scene].quantize);
      const bool additive = gArrange.scenes[(size_t)scene].additive;
      for (const Arrange::Lane& lane : gArrange.lanes)
      {
         if (SlotAt(lane, scene) != nullptr)
            Schedule(lane.id, at, scene);
         else if (!additive && sSchedule.find(lane.id) != sSchedule.end())
            Schedule(lane.id, at, -1);
      }
      const float tempo = gArrange.scenes[(size_t)scene].tempo;
      if (tempo > 0.0f)
      {
         if (at <= NowTick())
            Transport::Instance().SetTempo(tempo);
         else
         {
            sPendingTempoAt = at;
            sPendingTempo = tempo;
         }
      }
   }


   ArrangeSessionSlotState ArrangeSessionQuery(uint64_t laneId, int slot)
   {
      ArrangeSessionSlotState st;
      const Tick now = NowTick();
      for (const SessRun& r : sRuns)
      {
         if (r.laneId != laneId || r.slot != slot)
            continue;
         if (r.from <= now && now < r.to)
         {
            st.playing = true;
            const Arrange::Lane* lane = Arrange::FindLane(gArrange, laneId);
            const Arrange::SessionSlot* ss = lane ? SlotAt(*lane, slot) : nullptr;
            if (ss != nullptr && ss->clip.length > 0)
               st.progress = (float)((double)((now - r.from) % ss->clip.length) / (double)ss->clip.length);
         }
         else if (r.from > now && r.explicitStart)
            st.queued = true;
      }
      return st;
   }


   ArrangeSessionLaneState ArrangeSessionLaneQuery(uint64_t laneId)
   {
      ArrangeSessionLaneState st;
      const Tick now = NowTick();
      for (const SessRun& r : sRuns)
         if (r.laneId == laneId && r.from <= now && now < r.to)
            st.playingSlot = r.slot;
      auto it = sSchedule.find(laneId);
      if (it != sSchedule.end())
         for (const SessSeg& s : it->second)
            if (s.at > now && s.slot < 0)
               st.stopQueued = true;
      return st;
   }


   bool ArrangeSessionAnyLaunched()
   {
      return AnyScheduled();
   }


   bool ArrangeSessionRecording()
   {
      return sRecording;
   }


   void ArrangeSessionSetRecording(bool on)
   {
      if (on == sRecording)
         return;
      const Tick now = NowTick();
      if (on)
      {
         sRecording = true;
         sRecStart = now;
         sRecLog.clear();
         return;
      }
      if (Transport::Instance().IsPlaying())
      {
         UpdateRecordLog(now);
         CommitRecording(now);
      }
      sRecording = false;
      sRecLog.clear();
   }
}
