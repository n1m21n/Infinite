// Mixer panel render column (internal/plans/mixer-panel Block C): records a console page's Audio
// Out to a file. Live takes run through the Audio Out's own recorder with a sample-exact window
// (plugin latency skipped, length + tail kept). Fast takes pump the engine offline through the
// same recorder, a few milliseconds per frame so the panel stays alive and Cancel works.
#include "app/ui/design/components/FormParts.h"
#include "app/ui/design/components/FieldWell.h"
#include "app/ui/design/components/ChipButton.h"
#include "app/ui/design/components/ActionButton.h"
#include "app/ui/design/components/BusyLine.h"
#include "app/ui/design/UiType.h"
#include "app/AppShared.h"
#include "audio/AudioEngine.h"
#include "core/Notices.h"
#include "core/Transport.h"
#include "nodes/AudioNodes.h"
#include "nodes/OutputNode.h"
#include "arrange/ArrangeModel.h"
#include "platform/AppPaths.h"
#include "platform/Platform.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace app
{
   MixerRenderSettings gMixerRender;

   namespace
   {
      enum class Phase { Idle, Armed, Running, Tail };

      struct Take
      {
         uint64_t outUid = 0;
         std::string path;
      };

      struct Job
      {
         Phase phase = Phase::Idle;
         bool fast = false, untilStop = false;
         bool sawPlaying = false, transportStopped = false;
         bool restartDevice = false, wasPlaying = false;
         double rate = 0.0, savedHeadlessRate = 0.0, startSec = 0.0;
         int64_t lengthFrames = 0, tailFrames = 0, skip = 0;
         int64_t done = 0, total = 0, step = 0;
         int bits = 16;
         const char* ext = "wav";
         std::string name;
         std::vector<Take> takes;
         std::vector<uint64_t> sumUids; // G8 "All pages": every page's Audio Out, summed into takes[0]
      };

      Job sJob;
      std::vector<MixerRenderResult> sResults;
      std::string sRefusal;

      AudioOutputNode* OutOf(uint64_t uid)
      {
         GraphNode* gn = FindNodeByUid(uid);
         return gn != nullptr ? dynamic_cast<AudioOutputNode*>(gn->node.get()) : nullptr;
      }

      bool Exists(const std::string& path)
      {
         struct stat st;
         return stat(path.c_str(), &st) == 0;
      }

      int64_t SizeOf(const std::string& path)
      {
         struct stat st;
         return stat(path.c_str(), &st) == 0 ? (int64_t)st.st_size : 0;
      }

      void Replace(std::string& s, const char* what, const std::string& with)
      {
         const size_t len = std::strlen(what);
         for (size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + with.size()))
            s.replace(at, len, with);
      }

      std::string DirOf(const AudioOutputNode* out)
      {
         return out->recordDirectory.empty() ? AppPaths::DesktopDir() : out->recordDirectory;
      }

      bool Refuse(const char* why)
      {
         sRefusal = why;
         Notices::Post(Notices::Level::Warning, "mixer.render", T("Render"), why);
         return false;
      }

      // Opens every take's file. A name is picked only now, so an armed take never reserves one.
      void EndSum()
      {
         if (sJob.sumUids.empty())
            return;
         AudioEngine::Instance().SetCaptureSum(nullptr);
         for (uint64_t uid : sJob.sumUids)
            if (AudioOutputNode* o = OutOf(uid))
               o->CaptureRing().joinSum.store(false, std::memory_order_relaxed);
      }

      bool Begin()
      {
         if (!sJob.sumUids.empty() && !sJob.takes.empty())
         {
            // Join first, then point the engine at the target: until the target ring is enabled by
            // StartRecording, the sum is formed but written nowhere, so the file starts on one block.
            for (uint64_t uid : sJob.sumUids)
               if (AudioOutputNode* o = OutOf(uid))
                  o->CaptureRing().joinSum.store(true, std::memory_order_relaxed);
            if (AudioOutputNode* target = OutOf(sJob.takes[0].outUid))
               AudioEngine::Instance().SetCaptureSum(&target->CaptureRing());
         }
         for (Take& t : sJob.takes)
         {
            AudioOutputNode* out = OutOf(t.outUid);
            if (out == nullptr || out->IsRecording())
               return false;
            const std::string dir = DirOf(out);
            t.path = dir + "/" + MixerRenderFileName(sJob.name, dir, sJob.ext);
            if (!out->StartRecording(t.path, sJob.rate, sJob.bits))
               return false;
            out->SetTakeWindow(sJob.skip, sJob.untilStop ? 0 : sJob.lengthFrames + sJob.tailFrames);
         }
         sJob.phase = Phase::Running;
         return true;
      }

      void LeaveOffline()
      {
         if (!sJob.fast)
            return;
         Transport::Instance().SetOfflineMode(false);
         Transport::Instance().SetPlaying(sJob.wasPlaying);
         gHeadlessAudioRate = sJob.savedHeadlessRate;
         if (sJob.restartDevice)
         {
            std::string err;
            if (!StartAudioEngine(err))
               fprintf(stderr, "audio device: %s\n", err.c_str());
         }
         ForceAudioRepare();
         RebuildAudioTopology();
      }

      // keep = false: a cancelled fast take leaves no file behind.
      void Finish(bool keep)
      {
         std::vector<MixerRenderResult> results;
         for (Take& t : sJob.takes)
         {
            AudioOutputNode* out = OutOf(t.outUid);
            if (out == nullptr || t.path.empty())
               continue;
            out->StopRecording();
            if (!keep)
            {
               std::remove(t.path.c_str());
               continue;
            }
            MixerRenderResult r;
            r.path = t.path;
            r.peak = out->TakePeak();
            r.lufs = out->TakeLufs();
            r.truePeakDb = out->TakeTruePeakDb();
            r.frames = out->TakeFrames();
            r.seconds = sJob.rate > 0.0 ? (double)r.frames / sJob.rate : 0.0;
            r.bytes = SizeOf(t.path);
            results.push_back(r);
         }
         EndSum();
         LeaveOffline();
         if (keep)
            sResults = results;
         sJob = Job();
      }

      bool AllComplete()
      {
         for (const Take& t : sJob.takes)
         {
            AudioOutputNode* out = OutOf(t.outUid);
            if (out == nullptr)
               return true; // the Audio Out was deleted: nothing more will arrive
            if (!out->TakeComplete())
               return false;
         }
         return true;
      }

      int64_t FramesSoFar()
      {
         AudioOutputNode* out = sJob.takes.empty() ? nullptr : OutOf(sJob.takes[0].outUid);
         return out != nullptr ? out->TakeFrames() : 0;
      }

      void Drain()
      {
         for (const Take& t : sJob.takes)
            if (AudioOutputNode* out = OutOf(t.outUid))
               out->CookIfNeeded(0);
      }

      // One video frame (1/60 s) of the offline take: cook the graph, then its audio blocks.
      void PumpFastStep()
      {
         static int sFrame = 1 << 27;
         static float sL[kAudioMaxBlockFrames];
         static float sR[kAudioMaxBlockFrames];
         static float* sCh[2] = { sL, sR };
         ++sFrame;
         Transport::Instance().SetOfflineVideoTime(sJob.startSec + (double)sJob.step / 60.0);
         ApplyModulationAndPalette(sFrame);
         for (GraphNode& gn : gNodes)
            if (!gn.node->bypassed)
               gn.node->CookIfNeeded(sFrame);
         ++sJob.step;
         const int blockCap = OfflineAudioBlockFrames();
         const int64_t target = std::min(sJob.total, (int64_t)std::llround((double)sJob.step * sJob.rate / 60.0));
         while (sJob.done < target)
         {
            if (!sJob.transportStopped && sJob.done >= sJob.lengthFrames)
            {
               Transport::Instance().SetPlaying(false); // the tail rings out with the transport stopped
               sJob.transportStopped = true;
            }
            int64_t stop = target;
            if (!sJob.transportStopped)
               stop = std::min(stop, sJob.lengthFrames);
            const int n = (int)std::min<int64_t>(blockCap, stop - sJob.done);
            AudioBuffer buf;
            buf.channels = sCh;
            buf.numChannels = 2;
            buf.numFrames = n;
            AudioEngine::Instance().ProcessOffline(buf);
            sJob.done += n;
            Drain();
         }
      }
   }

   // {patch} {take} {date} {time}. Never an existing file: {take} counts up, and a template
   // without {take} gets a numbered suffix.
   std::string MixerRenderFileName(const std::string& tmpl, const std::string& dir, const char* ext)
   {
      std::string patch = gPatchPath.empty() ? "Untitled" : gPatchPath.substr(gPatchPath.find_last_of("/\\") + 1);
      const size_t dot = patch.find_last_of('.');
      if (dot != std::string::npos && dot > 0)
         patch = patch.substr(0, dot);
      char date[16] = "", clock[16] = "";
      const std::time_t now = std::time(nullptr);
      if (const std::tm* tm = std::localtime(&now))
      {
         std::strftime(date, sizeof(date), "%Y-%m-%d", tm);
         std::strftime(clock, sizeof(clock), "%H%M%S", tm);
      }
      std::string base = tmpl.empty() ? std::string("{patch}-{take}") : tmpl;
      Replace(base, "{patch}", patch);
      Replace(base, "{date}", date);
      Replace(base, "{time}", clock);
      for (char& c : base)
         if (c == '/' || c == '\\' || c == ':')
            c = '-';
      const bool counted = base.find("{take}") != std::string::npos;
      for (int i = 1; i < 10000; i++)
      {
         std::string name = base;
         char num[16];
         if (counted)
         {
            snprintf(num, sizeof(num), "%03d", i);
            Replace(name, "{take}", num);
         }
         else if (i > 1)
         {
            snprintf(num, sizeof(num), "-%d", i);
            name += num;
         }
         name += std::string(".") + ext;
         if (dir.empty() || !Exists(dir + "/" + name))
            return name;
      }
      return base + "." + ext;
   }

   bool MixerRenderActive() { return sJob.phase != Phase::Idle; }
   bool MixerRenderArmed() { return sJob.phase == Phase::Armed; }
   const std::vector<MixerRenderResult>& MixerRenderResults() { return sResults; }
   const std::string& MixerRenderRefusal() { return sRefusal; }

   float MixerRenderProgress()
   {
      if (sJob.phase == Phase::Idle || sJob.phase == Phase::Armed)
         return 0.0f;
      if (sJob.fast)
         return sJob.total > 0 ? (float)((double)sJob.done / (double)sJob.total) : 0.0f;
      if (sJob.untilStop)
         return -1.0f;
      const int64_t all = sJob.lengthFrames + sJob.tailFrames;
      return all > 0 ? (float)std::min(1.0, (double)FramesSoFar() / (double)all) : 0.0f;
   }

   bool MixerRenderArrangeSpan(double& startSec, double& lengthSec)
   {
      Arrange::Tick a = 0, b = 0;
      if (gArrange.settings.loop.enabled && gArrange.settings.loop.end > gArrange.settings.loop.start)
      {
         a = gArrange.settings.loop.start;
         b = gArrange.settings.loop.end;
      }
      else
         b = ArrangeRenderableEndTick();
      const double bpm = std::max(1.0, (double)Transport::Instance().Tempo());
      startSec = Arrange::TicksToSeconds(a, bpm);
      lengthSec = b > a ? Arrange::TicksToSeconds(b - a, bpm) : 0.0;
      return lengthSec > 0.0;
   }

   double MixerRenderLengthSeconds()
   {
      const MixerRenderSettings& s = gMixerRender;
      if (s.lengthUnit == 3)
      {
         double startSec = 0.0, lengthSec = 0.0;
         return MixerRenderArrangeSpan(startSec, lengthSec) ? lengthSec : 0.0;
      }
      if (s.lengthUnit == 1)
      {
         const double bpm = std::max(1.0, (double)Transport::Instance().Tempo());
         return (double)s.length * Transport::Instance().BeatsPerBar() * 60.0 / bpm;
      }
      return (double)s.length;
   }

   bool MixerRenderStart(const std::vector<int>& outIndices)
   {
      sRefusal.clear();
      if (MixerRenderActive())
         return false;
      const MixerRenderSettings s = gMixerRender;
      Job job;
      for (int index : outIndices)
      {
         GraphNode* gn = FindNodeByIndex(index);
         auto* out = gn != nullptr ? dynamic_cast<AudioOutputNode*>(gn->node.get()) : nullptr;
         if (out == nullptr)
            continue;
         if (out->IsRecording())
            return Refuse(T("This Audio Out is already recording."));
         Take t;
         t.outUid = gn->uid;
         job.takes.push_back(t);
      }
      if (job.takes.empty())
         return false;
      if (s.source == 1 && job.takes.size() > 1)
      {
         for (const Take& t : job.takes)
            job.sumUids.push_back(t.outUid);
         job.takes.resize(1); // one writer: the first page's Audio Out records the sum
      }
      job.fast = s.mode == 1;
      job.untilStop = s.lengthUnit == 2;
      const double lengthSec = MixerRenderLengthSeconds();
      if (!job.untilStop && lengthSec <= 0.0)
         return Refuse(s.lengthUnit == 3 ? T("The arrangement is empty.") : T("Set a length first."));
      // Arrange range: the take always starts at the range start, whatever Start says.
      double rangeStart = 0.0, rangeLen = 0.0;
      const bool arrange = s.lengthUnit == 3 && MixerRenderArrangeSpan(rangeStart, rangeLen);
      const int startMode = arrange ? 2 : s.start;
      const double startAt = arrange ? rangeStart : 0.0;
      static const char* kExt[4] = { "wav", "wav", "flac", "mp3" };
      const int format = std::clamp(s.format, 0, 3);
      job.ext = kExt[format];
      job.bits = format == 1 ? 24 : 16;
      job.name = s.name;

      if (job.fast)
      {
         if (job.untilStop)
            return Refuse(T("Fast needs a length."));
         if (FindHardwareDrivenNode() != nullptr)
            return Refuse(T("Fast can't render a live source (camera, MIDI, audio in). Use Live."));
         if (Platform::PluginModeActive() || gOfflineRender.active)
            return Refuse(T("Fast isn't available right now. Use Live."));
      }
      else if (AudioEngine::Instance().SampleRate() <= 0.0)
         return Refuse(T("Audio is off. Start audio, or use Fast."));

      Transport& tr = Transport::Instance();
      if (!job.fast)
      {
         job.rate = AudioEngine::Instance().SampleRate();
         job.lengthFrames = (int64_t)std::llround(lengthSec * job.rate);
         job.tailFrames = (int64_t)std::llround(std::max(0.0f, s.tail) * job.rate);
         job.skip = std::max(0, AudioEngine::Instance().OutputLatencySamples());
         sJob = job;
         if (startMode == 1 && !tr.IsPlaying())
         {
            sJob.phase = Phase::Armed;
            return true;
         }
         if (startMode == 2)
         {
            tr.Seek(startAt);
            tr.SetPlaying(true);
         }
         if (!Begin())
         {
            Finish(false);
            return Refuse(T("Couldn't create the file."));
         }
         return true;
      }

      // Fast: the device stops, the engine runs offline at the same rate, and the transport is driven by hand.
      const double deviceRate = AudioEngine::Instance().SampleRate();
      job.restartDevice = deviceRate > 0.0;
      if (job.restartDevice)
         AudioEngine::Instance().Stop();
      job.savedHeadlessRate = gHeadlessAudioRate;
      if (deviceRate > 0.0)
         gHeadlessAudioRate = deviceRate;
      else if (gHeadlessAudioRate <= 0.0)
         gHeadlessAudioRate = 48000.0;
      job.rate = gHeadlessAudioRate;
      job.lengthFrames = (int64_t)std::llround(lengthSec * job.rate);
      job.tailFrames = (int64_t)std::llround(std::max(0.0f, s.tail) * job.rate);
      job.wasPlaying = tr.IsPlaying();
      if (startMode == 2)
         tr.Seek(startAt);
      job.startSec = startMode == 2 ? startAt : tr.Seconds();
      tr.SetOfflineMode(true, job.rate);
      tr.SetPlaying(true);
      tr.SetOfflineVideoTime(job.startSec);
      static int sFrame = 1 << 26;
      ++sFrame;
      ApplyModulationAndPalette(sFrame);
      for (GraphNode& gn : gNodes)
         if (!gn.node->bypassed)
            gn.node->CookIfNeeded(sFrame);
      ForceAudioRepare();
      RebuildAudioTopology();
      job.skip = std::max(0, AudioEngine::Instance().OutputLatencySamples());
      job.total = job.skip + job.lengthFrames + job.tailFrames;
      sJob = job;
      if (!Begin())
      {
         Finish(false);
         return Refuse(T("Couldn't create the file."));
      }
      return true;
   }

   // Live: stops the take and keeps the file. Fast or armed: drops it.
   void MixerRenderCancel()
   {
      if (sJob.phase == Phase::Idle)
         return;
      const bool keep = !sJob.fast && sJob.phase != Phase::Armed;
      Finish(keep);
   }

   // Every frame, from the frame pump.
   void MixerRenderTick()
   {
      if (sJob.phase == Phase::Idle)
         return;
      Transport& tr = Transport::Instance();
      if (sJob.phase == Phase::Armed)
      {
         if (tr.IsPlaying() && !Begin())
            Finish(false);
         return;
      }
      for (const Take& t : sJob.takes)
         if (OutOf(t.outUid) == nullptr)
         {
            Finish(!sJob.fast);
            return;
         }
      if (sJob.fast)
      {
         const auto t0 = std::chrono::steady_clock::now();
         while (sJob.done < sJob.total)
         {
            PumpFastStep();
            if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > 0.010)
               break;
         }
         if (sJob.done >= sJob.total)
            Finish(true);
         return;
      }
      if (sJob.phase == Phase::Running && sJob.untilStop)
      {
         sJob.sawPlaying = sJob.sawPlaying || tr.IsPlaying();
         if (sJob.sawPlaying && !tr.IsPlaying())
         {
            if (sJob.tailFrames <= 0)
            {
               Finish(true);
               return;
            }
            for (const Take& t : sJob.takes)
               if (AudioOutputNode* out = OutOf(t.outUid))
                  out->SetTakeWindow(0, out->TakeFrames() + sJob.tailFrames);
            sJob.phase = Phase::Tail;
         }
         return;
      }
      if (sJob.phase == Phase::Running && !sJob.transportStopped && FramesSoFar() >= sJob.lengthFrames)
      {
         if (tr.IsPlaying())
            tr.SetPlaying(false); // the tail rings out with the transport stopped
         sJob.transportStopped = true;
      }
      if (AllComplete())
         Finish(true);
   }

   namespace
   {
      std::string Items(std::initializer_list<const char*> keys)
      {
         std::string s;
         for (const char* k : keys)
         {
            s += T(k);
            s += '\0';
         }
         return s;
      }

      void ResultRow(const MixerRenderResult& r)
      {
         const std::string file = r.path.substr(r.path.find_last_of("/\\") + 1);
         ImGui::TextUnformatted(file.c_str());
         if (ImGui::IsItemHovered())
            HelpTip("%s", r.path.c_str());
         const float peakDb = r.peak > 1e-6f ? 20.0f * std::log10(r.peak) : -120.0f;
         char line[160];
         if (r.lufs <= -99.0f)
            snprintf(line, sizeof(line), "%.1f dB%s  %.1f s  %.1f MB", peakDb, OutputNode::ClipSuffix(r.peak).c_str(),
                     r.seconds, (double)r.bytes / (1024.0 * 1024.0));
         else
            snprintf(line, sizeof(line), "%.1f dB%s  %.1f LUFS  %.1f s  %.1f MB", peakDb, OutputNode::ClipSuffix(r.peak).c_str(),
                     r.lufs, r.seconds, (double)r.bytes / (1024.0 * 1024.0));
         ImGui::TextDisabled("%s", line);
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("Peak, integrated loudness, length and size of the file."));
         ImGui::PushID(r.path.c_str());
         if (ActionButton::Draw(T("Reveal")))
            Platform::RevealInFileManager(r.path);
         ImGui::PopID();
      }
   }

   // The column itself. pageOuts: every page's Audio Out (node indices); currentOut: this page's.
   void DrawMixerRenderColumn(const std::vector<int>& pageOuts, int currentOut)
   {
      MixerRenderSettings& s = gMixerRender;
      FormParts::Narrow narrow(58.0f);
      {
         UiType::Scope hs(UiType::Size::Body, UiType::Weight::Semibold);
         ImGui::TextUnformatted(T("Render"));
      }
      GraphNode* gn = FindNodeByIndex(currentOut);
      auto* out = gn != nullptr ? dynamic_cast<AudioOutputNode*>(gn->node.get()) : nullptr;
      if (out == nullptr)
         return;
      const bool active = MixerRenderActive();

      ImGui::BeginDisabled(active);
      FormParts::Combo(T("Source"), &s.source, Items({ I18N_KEY("This page"), I18N_KEY("All pages") }).c_str());
      if (ImGui::IsItemHovered())
         HelpTip("%s", T("All pages writes one file: every page summed."));
      FormParts::Combo(T("Mode"), &s.mode, Items({ I18N_KEY("Live"), I18N_KEY("Fast") }).c_str());
      if (ImGui::IsItemHovered())
         HelpTip("%s", T("Live records what plays. Fast renders offline, quicker than real time; live inputs can't be rendered that way."));
      FormParts::Combo(T("Length"), &s.lengthUnit,
                       Items({ I18N_KEY("Seconds"), I18N_KEY("Bars"), I18N_KEY("Until stop"), I18N_KEY("Arrange range") }).c_str());
      if (s.lengthUnit == 3)
      {
         double startSec = 0.0, lengthSec = 0.0;
         const bool any = MixerRenderArrangeSpan(startSec, lengthSec);
         FormParts::Label("##lenrange");
         if (any)
            ImGui::TextDisabled(T("%.1f s %s"), lengthSec, gArrange.settings.loop.enabled ? T("(loop)") : T("(whole)"));
         else
            ImGui::TextDisabled("%s", T("Empty"));
      }
      else if (s.lengthUnit != 2)
      {
         FormParts::Label("##lenval");
         ImGui::SetNextItemWidth(FormParts::ControlW());
         FieldWell::Slider("##lenval", &s.length, 1.0f, s.lengthUnit == 1 ? 256.0f : 600.0f, s.lengthUnit == 1 ? "%.0f" : "%.0f s");
         s.length = std::clamp(s.lengthUnit == 1 ? std::round(s.length) : s.length, 1.0f, 3600.0f);
      }
      FormParts::SliderFloat(T("Tail"), &s.tail, 0.0f, 30.0f, "%.1f s");
      if (ImGui::IsItemHovered())
         HelpTip("%s", T("Extra time after the length, with the transport stopped, so reverbs and delays ring out."));
      ImGui::BeginDisabled(s.lengthUnit == 3);
      FormParts::Combo(T("Start"), &s.start, Items({ I18N_KEY("Now"), I18N_KEY("With play"), I18N_KEY("From start") }).c_str());
      ImGui::EndDisabled();
      FormParts::Combo(T("Format"), &s.format, Items({ "WAV 16", "WAV 24", "FLAC", "MP3" }).c_str());
      FormParts::Label(T("Name"));
      ImGui::SetNextItemWidth(FormParts::ControlW());
      FieldWell::InputText("##rendername", &s.name);
      if (ImGui::IsItemHovered())
         HelpTip("%s", T("{patch} {take} {date} {time}. An existing file is never overwritten."));

      const std::string dir = DirOf(out);
      FormParts::Label(T("Folder"));
      if (ActionButton::Draw(T("Choose")))
      {
         const std::string chosen = Platform::OpenFolderDialog(T("Render folder"), dir);
         if (!chosen.empty())
         {
            out->recordDirectory = chosen;
            gPatchDirty = true;
         }
      }
      if (ImGui::IsItemHovered())
         HelpTip("%s", dir.c_str());
      ImGui::SameLine(0.0f, tok::space_1);
      if (ActionButton::Draw(T("Reveal##folder")))
         Platform::RevealInFileManager(dir);
      ImGui::EndDisabled();

      ImGui::Dummy(ImVec2(0.0f, tok::space_1));
      const float w = std::max(1.0f, ImGui::GetContentRegionAvail().x);
      if (active)
      {
         const float p = MixerRenderProgress();
         const char* what = MixerRenderArmed() ? T("Waiting for play") : (sJob.fast ? T("Rendering") : T("Recording"));
         BusyLine::Draw(what, MixerRenderArmed() ? -1.0f : p);
         if (ActionButton::Draw(sJob.fast || MixerRenderArmed() ? T("Cancel") : T("Stop"), ImVec2(w, 0.0f)))
            MixerRenderCancel();
      }
      else if (out->IsRecording())
      {
         // The Audio Out's own Record is running: show that take, and leave Record off.
         ImGui::TextDisabled(T("REC %.1f s  %.1f MB"), out->ElapsedSeconds(), (double)out->FileSizeBytes() / (1024.0 * 1024.0));
         ImGui::BeginDisabled(true);
         ActionButton::Draw(T("Record"), ImVec2(w, 0.0f), ActionButton::Kind::Primary);
         ImGui::EndDisabled();
      }
      else
      {
         const bool blocked = s.mode == 1 && s.lengthUnit == 2;
         ImGui::BeginDisabled(blocked);
         if (ActionButton::Draw(T("Record"), ImVec2(w, 0.0f), ActionButton::Kind::Primary))
            MixerRenderStart(s.source == 1 ? pageOuts : std::vector<int>{ currentOut });
         ImGui::EndDisabled();
         if (blocked && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            HelpTip("%s", T("Fast needs a length."));
      }
      for (const MixerRenderResult& r : sResults)
         ResultRow(r);
   }
}
