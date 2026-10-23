// Show set (v0.6 2.35): a list of patches played one after another in a live
// set. Every entry's file is read in the background as soon as it is added, so
// a switch only applies data already in memory. A switch lands on the launch
// grid (bar by default): the outgoing patch keeps sounding as the audio
// engine's tail and crossfades into the new one at the grid line
// (AudioEngine::ArmTail), and each projector window holds its last frame until
// the new patch's Output has drawn, so the screen never goes black.
//
// Program Change n picks entry n+1 (index n); PageDown / PageUp, the Menu tile
// and OSC (/infinite/show/next, /prev, /go) step through it.
#include "app/AppShared.h"

#include "audio/AudioEngine.h"
#include "audio/MusicTime.h"
#include "core/Transport.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <map>
#include <memory>
#include "platform/AppPaths.h"

namespace app
{
   bool gShowSetOpen = false;

   namespace
   {
      struct ReadResult
      {
         std::shared_ptr<Patch::Data> data;
         std::string error;
      };

      struct Entry
      {
         std::string path;
         std::string name;
         std::shared_ptr<Patch::Data> data;
         std::future<ReadResult> reading;
         std::filesystem::file_time_type mtime {};
         std::string error;
      };

      std::vector<Entry> gEntries;
      std::string gSetPath;
      int gCurrent = -1;
      int gQuantize = MusicTime::kLaunchBar;
      float gFadeMs = 1000.0f;

      // A requested switch, waiting for its entry's data, for the tail of the
      // last switch, or for the launch grid to come within a beat.
      int gRequest = -1;
      double gRequestBeat = 0.0;  // transport beat when it was asked for
      double gTargetBeat = -1.0;  // where it lands (-1 = at once)
      bool gTargetSet = false;

      // The switch in flight, after its load.
      bool gHoldActive = false;
      bool gPanicHold = false; // panic: every projector holds its last frame until resume
      bool gTailArmed = false;
      int gHoldFrames = 0;
      int gLandedFrames = 0;
      bool gTransportPending = false;
      Patch::TransportRecord gPendingTransport;

      // Per projector window: the texture it showed last frame (noted by the
      // projector loop) and, during a switch, a copy of it to hold on screen.
      struct Held
      {
         unsigned int liveTex = 0;
         int liveW = 0, liveH = 0;
         GLUtil::Fbo fbo;
         bool captured = false;
      };
      std::map<GLFWwindow*, Held> gHeld;

      uint32_t gProgramSeen = 0;
      double gLastStatPoll = 0.0;

      // SHOWSETTEST probes (test-only).
      bool gTestActive = false;
      int gBlackFrames = 0;
      bool gSeenFirstFrame = false;
      float gLastCentre[3] = { 0.0f, 0.0f, 0.0f };

      std::string NameOf(const std::string& path)
      {
         return std::filesystem::path(path).stem().string();
      }

      std::filesystem::file_time_type MTimeOf(const std::string& path)
      {
         std::error_code ec;
         const auto t = std::filesystem::last_write_time(path, ec);
         return ec ? std::filesystem::file_time_type {} : t;
      }

      void StartRead(Entry& e)
      {
         e.mtime = MTimeOf(e.path);
         e.error.clear();
         const std::string path = e.path;
         e.reading = std::async(std::launch::async, [path]() {
            ReadResult r;
            auto data = std::make_shared<Patch::Data>();
            if (Patch::Read(path, *data, r.error))
               r.data = std::move(data);
            return r;
         });
      }

      void PollReads()
      {
         for (Entry& e : gEntries)
         {
            if (!e.reading.valid() ||
                e.reading.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
               continue;
            ReadResult r = e.reading.get();
            if (r.data != nullptr)
            {
               e.data = std::move(r.data);
               e.error.clear();
            }
            else
               e.error = r.error;
         }
         // A file saved again from another window or editor is read again.
         const double now = glfwGetTime();
         if (now - gLastStatPoll < 1.0)
            return;
         gLastStatPoll = now;
         for (Entry& e : gEntries)
            if (!e.reading.valid() && MTimeOf(e.path) != e.mtime)
               StartRead(e);
      }

      double Beats() { return Transport::Instance().Beats(); }

      bool AudioRunning() { return AudioEngine::Instance().SampleRate() > 0.0; }

      // Main context: copy what each projector showed last frame into a
      // show-set-owned texture (textures are shared with the projector
      // contexts; framebuffers are not, so the copy is made here).
      void CaptureProjectors(GLFWwindow* mainWindow)
      {
         glfwMakeContextCurrent(mainWindow);
         GLint prevRead = 0, prevDraw = 0;
         glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
         glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDraw);
         GLuint readFbo = 0;
         glGenFramebuffers(1, &readFbo);
         for (const ProjectorWindow& pw : gProjectorWindows)
         {
            Held& h = gHeld[pw.window];
            h.captured = false;
            if (h.liveTex == 0 || h.liveW <= 0 || h.liveH <= 0)
               continue;
            if (!GLUtil::EnsureFbo(h.fbo, h.liveW, h.liveH, GL_RGBA16F, "ShowSetHold"))
               continue;
            glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, h.liveTex, 0);
            if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
               continue;
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, h.fbo.fbo);
            glBlitFramebuffer(0, 0, h.liveW, h.liveH, 0, 0, h.liveW, h.liveH, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            h.captured = true;
         }
         glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
         glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prevDraw);
         glDeleteFramebuffers(1, &readFbo);
         glFlush(); // the projector contexts sample these textures next
      }

      void ReleaseHold()
      {
         gHoldActive = false;
         if (gPanicHold)
            return; // the frozen frames stay until the panic is lifted
         for (auto it = gHeld.begin(); it != gHeld.end();)
         {
            GLUtil::DestroyFbo(it->second.fbo);
            it->second.captured = false;
            const bool open = std::any_of(gProjectorWindows.begin(), gProjectorWindows.end(),
                                          [&](const ProjectorWindow& pw) { return pw.window == it->first; });
            it = open ? std::next(it) : gHeld.erase(it);
         }
      }

      // The k-th projector window goes to the new patch's k-th Output, extra
      // windows to the last one; with no Output a window closes once the hold
      // ends.
      void RemapProjectors()
      {
         std::vector<int> outputs;
         for (const GraphNode& gn : gNodes)
            if (dynamic_cast<OutputNode*>(gn.node.get()) != nullptr)
               outputs.push_back(gn.index);
         for (size_t k = 0; k < gProjectorWindows.size(); k++)
            gProjectorWindows[k].nodeIndex = outputs.empty() ? -1 : outputs[std::min(k, outputs.size() - 1)];
      }

      bool NewOutputDrawn()
      {
         for (const ProjectorWindow& pw : gProjectorWindows)
         {
            GraphNode* gn = FindNodeByIndex(pw.nodeIndex);
            if (gn != nullptr && gn->node->GetOutputTexture() == 0)
               return false;
         }
         return true;
      }

      void ApplyTransport(const Patch::TransportRecord& t)
      {
         Transport& tr = Transport::Instance();
         tr.SetTempo(t.bpm);
         tr.SetTimeSignature(t.timeSigNum, t.timeSigDen);
         tr.SetKey(t.key);
         tr.SetScale(t.scale);
      }

      bool LoadEntry(int index, GLFWwindow* window)
      {
         Entry& e = gEntries[index];
         const bool hadEdits = gPatchDirty;
         CaptureProjectors(window);
         AudioEngine& engine = AudioEngine::Instance();
         const bool playing = Transport::Instance().IsPlaying() && AudioRunning();
         const double sr = engine.SampleRate();
         engine.ArmTail(playing && gTargetBeat >= 0.0 ? gTargetBeat : -1.0,
                        (int)std::lround(std::clamp(gFadeMs, 0.0f, 8000.0f) * 0.001 * (sr > 0.0 ? sr : 48000.0)));

         // The tempo, meter and key change when the switch sounds, not now.
         Patch::Data copy = *e.data;
         Transport& tr = Transport::Instance();
         copy.transport.bpm = tr.Tempo();
         copy.transport.timeSigNum = tr.TimeSigNumerator();
         copy.transport.timeSigDen = tr.TimeSigDenominator();
         copy.transport.key = tr.Key();
         copy.transport.scale = tr.Scale();
         const bool ok = LoadPatchDataImpl(copy, e.path, false);
         // Only the topology the load published may take the tail; a load
         // that published none must not leave it armed for a later edit.
         engine.DisarmTail();
         gTailArmed = engine.TailActive();
         if (!ok)
         {
            gHoldActive = false;
            for (auto& kv : gHeld)
               kv.second.captured = false;
            return false;
         }
         RemapProjectors();
         gCurrent = index;
         gHoldActive = true;
         gHoldFrames = 0;
         gLandedFrames = 0;
         gPendingTransport = e.data->transport;
         gTransportPending = true;
         gPatchStatus = std::string(T("Show set")) + ": " + e.name +
                        (hadEdits ? " (" + std::string(T("unsaved edits to the last patch were discarded")) + ")" : std::string());
         return true;
      }

      void RunShowSetTest(int frameId, GLFWwindow* window);
   }

   int ShowSetSize() { return (int)gEntries.size(); }
   const std::string& ShowSetName(int i) { return gEntries[i].name; }
   const std::string& ShowSetEntryPath(int i) { return gEntries[i].path; }
   const std::string& ShowSetError(int i) { return gEntries[i].error; }
   bool ShowSetReady(int i) { return gEntries[i].data != nullptr; }
   int ShowSetCurrent() { return gCurrent; }
   int ShowSetRequested() { return gRequest; }
   int& ShowSetQuantize() { return gQuantize; }

   namespace
   {
      void SayLaunch(Transport::LaunchAction a)
      {
         const Transport& tr = Transport::Instance();
         const std::vector<std::string>& qs = MusicTime::LaunchQuantizeList();
         const std::string where = qs[std::clamp(tr.LaunchQuantize(), 0, (int)qs.size() - 1)];
         if (a == Transport::kLaunchPause)
            gPatchStatus = std::string(T("Pauses on the next grid line")) + " (" + T(where.c_str()) + ")";
         else if (a == Transport::kLaunchRestart)
            gPatchStatus = std::string(T("Restarts on the next grid line")) + " (" + T(where.c_str()) + ")";
      }
   }

   void LaunchRequestTogglePlay() { SayLaunch(Transport::Instance().RequestTogglePlay()); }
   void LaunchRequestRewind() { SayLaunch(Transport::Instance().RequestRewind()); }
   float& ShowSetFadeMs() { return gFadeMs; }
   const std::string& ShowSetPath() { return gSetPath; }

   void ShowSetAddFile(const std::string& path)
   {
      if (path.empty())
         return;
      Entry e;
      e.path = std::filesystem::absolute(path).string();
      e.name = NameOf(e.path);
      StartRead(e);
      gEntries.push_back(std::move(e));
   }

   bool ShowSetAddCurrent()
   {
      if (gPatchPath.empty())
      {
         gPatchStatus = T("Save the patch before adding it to the show set");
         return false;
      }
      ShowSetAddFile(gPatchPath);
      gCurrent = (int)gEntries.size() - 1;
      return true;
   }

   void ShowSetRemove(int i)
   {
      if (i < 0 || i >= (int)gEntries.size())
         return;
      gEntries.erase(gEntries.begin() + i);
      if (gCurrent == i)
         gCurrent = -1;
      else if (gCurrent > i)
         gCurrent--;
      if (gRequest == i)
         gRequest = -1;
      else if (gRequest > i)
         gRequest--;
   }

   void ShowSetMove(int i, int dir)
   {
      const int j = i + dir;
      if (i < 0 || j < 0 || i >= (int)gEntries.size() || j >= (int)gEntries.size())
         return;
      std::swap(gEntries[i], gEntries[j]);
      if (gCurrent == i)
         gCurrent = j;
      else if (gCurrent == j)
         gCurrent = i;
      if (gRequest == i)
         gRequest = j;
      else if (gRequest == j)
         gRequest = i;
   }

   void ShowSetClear()
   {
      gEntries.clear();
      gCurrent = -1;
      gRequest = -1;
      gSetPath.clear();
   }

   bool ShowSetOpenFile(const std::string& path)
   {
      std::ifstream in(AppPaths::FsPath(path));
      if (!in.is_open())
      {
         gPatchStatus = std::string(T("Couldn't open the show set")) + ": " + path;
         return false;
      }
      ShowSetClear();
      std::string line;
      while (std::getline(in, line))
      {
         while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
         if (line.empty() || line[0] == '#')
            continue;
         ShowSetAddFile(line);
      }
      gSetPath = path;
      for (int i = 0; i < (int)gEntries.size(); i++)
         if (gEntries[i].path == gPatchPath)
            gCurrent = i;
      return true;
   }

   bool ShowSetSaveFile(const std::string& path)
   {
      std::ofstream out(AppPaths::FsPath(path));
      if (!out.is_open())
      {
         gPatchStatus = std::string(T("Couldn't save the show set")) + ": " + path;
         return false;
      }
      for (const Entry& e : gEntries)
         out << e.path << "\n";
      gSetPath = path;
      return true;
   }

   // Panic (v0.6 2.36): freeze every projector on its last frame, drop any
   // pending switch; resume lets the Outputs draw again.
   void ShowSetPanicHold(bool on, GLFWwindow* mainWindow)
   {
      if (on)
      {
         gRequest = -1;
         gTargetSet = false;
         if (!gHoldActive)
            CaptureProjectors(mainWindow);
         gPanicHold = true;
      }
      else
      {
         gPanicHold = false;
         if (!gHoldActive)
            ReleaseHold();
      }
   }

   void ShowSetGo(int index)
   {
      if (ShowPanicLatched())
         return;
      if (index < 0 || index >= (int)gEntries.size())
         return;
      gRequest = index;
      gTargetSet = false;
   }

   void ShowSetStep(int dir)
   {
      if (gEntries.empty())
         return;
      const int from = gRequest >= 0 ? gRequest : gCurrent;
      const int n = (int)gEntries.size();
      ShowSetGo(from < 0 ? (dir > 0 ? 0 : n - 1) : std::clamp(from + dir, 0, n - 1));
   }

   // Main thread, once per frame from the frame pump (main GL context).
   void ShowSetTick(int frameId, GLFWwindow* window)
   {
      static const bool sTest = getenv("INFINITE_SHOWSETTEST") != nullptr;
      if (sTest)
         RunShowSetTest(frameId, window);

      PollReads();

      int channel = 0, program = 0;
      while (Platform::MidiPollProgramChange(gProgramSeen, channel, program))
         ShowSetGo(program);

      AudioEngine& engine = AudioEngine::Instance();

      // The switch in flight: its transport when it sounds, its picture once
      // the new Output has drawn.
      const bool landed = !gTailArmed || engine.SwitchLanded();
      if (gTransportPending && landed)
      {
         ApplyTransport(gPendingTransport);
         gTransportPending = false;
      }
      if (gHoldActive)
      {
         gHoldFrames++;
         if (landed)
            gLandedFrames++;
         if (landed && gHoldFrames >= 3 && (NewOutputDrawn() || gLandedFrames > 120))
            ReleaseHold();
      }

      if (gRequest < 0)
         return;
      if (gRequest >= (int)gEntries.size())
      {
         gRequest = -1;
         return;
      }
      // One switch at a time: the next waits for the last one's fade.
      if (engine.TailActive() || gHoldActive)
         return;
      Entry& e = gEntries[gRequest];
      if (e.data == nullptr)
      {
         if (!e.reading.valid() && !e.error.empty())
         {
            gPatchStatus = std::string(T("Show set")) + ": " + T("can't read") + " " + e.name + ": " + e.error;
            gRequest = -1;
         }
         return;
      }

      const bool playing = Transport::Instance().IsPlaying() && AudioRunning();
      if (!gTargetSet)
      {
         gRequestBeat = Beats();
         gTargetBeat = (playing && gQuantize != MusicTime::kLaunchNone)
                          ? MusicTime::NextLaunchBeat(gRequestBeat, gQuantize, 0.25)
                          : -1.0;
         gTargetSet = true;
      }
      // Load about a beat ahead, so the new patch has built and warmed up by
      // the time it sounds; with no grid, a beat grid or a stopped transport,
      // at once.
      if (playing && gTargetBeat >= 0.0 && gQuantize != MusicTime::kLaunchBeat && gTargetBeat - Beats() > 1.0)
         return;
      if (!playing)
         gTargetBeat = -1.0;
      const int index = gRequest;
      gRequest = -1;
      gTargetSet = false;
      LoadEntry(index, window);
   }

   void ShowSetProbeProjector(size_t i);

   // Projector loop, first thing for window i: while a switch is held, draw
   // the captured frame instead of the (new, not yet drawn) node.
   bool ShowSetDrawHeldProjector(size_t i)
   {
      if ((!gHoldActive && !gPanicHold) || i >= gProjectorWindows.size())
         return false;
      auto it = gHeld.find(gProjectorWindows[i].window);
      if (it == gHeld.end() || !it->second.captured)
         return false;
      const Held& h = it->second;
      glfwMakeContextCurrent(gProjectorWindows[i].window);
      int pw = 0, ph = 0;
      glfwGetFramebufferSize(gProjectorWindows[i].window, &pw, &ph);
      GLUtil::DrawTextureToScreen(h.fbo.tex, pw, ph, h.fbo.w, h.fbo.h, /*checkerBg=*/false);
      ShowSetProbeProjector(i);
      glFlush();
      return true;
   }

   // Projector loop, once the texture window i will show is known.
   void ShowSetNoteProjectorTexture(size_t i, unsigned int tex, int w, int h)
   {
      if (i >= gProjectorWindows.size())
         return;
      Held& held = gHeld[gProjectorWindows[i].window];
      held.liveTex = tex;
      held.liveW = w;
      held.liveH = h;
   }

   // Test-only: the centre pixel a projector just drew (normal path).
   void ShowSetProbeProjector(size_t i)
   {
      if (!gTestActive || i >= gProjectorWindows.size())
         return;
      int pw = 0, ph = 0;
      glfwGetFramebufferSize(gProjectorWindows[i].window, &pw, &ph);
      float px[4] = { 0, 0, 0, 0 };
      glReadPixels(pw / 2, ph / 2, 1, 1, GL_RGBA, GL_FLOAT, px);
      const bool black = std::max({ px[0], px[1], px[2] }) < 0.15f;
      if (gSeenFirstFrame && black)
         gBlackFrames++;
      gSeenFirstFrame = gSeenFirstFrame || !black;
      std::copy(px, px + 3, gLastCentre);
   }

   namespace
   {
      // ================================================== INFINITE_SHOWSETTEST
      // Two patches (220 Hz + red picture, 330 Hz + blue), a projector on the
      // Output, a bar-quantized switch requested mid-bar, then an unquantized
      // switch back. Gates: no dropout in the crossfade (quietest block >= half
      // the steady level), no xruns, the switch on a bar line to 2 samples,
      // under a bar of latency, no black projector frame, and the window ends
      // up on the new patch's Output.
      bool BuildTestPatch(float hz, const char* rgb, const std::string& path)
      {
         NewPatch();
         // SpawnNode's pointer is into gNodes, which the next spawn can move: keep indices.
         auto spawn = [](const char* type, const char* cat, float x, float y) {
            GraphNode* gn = SpawnNode(type, cat, x, y);
            return gn != nullptr ? gn->index : -1;
         };
         const int osc = spawn("Oscillator", "Synths", 40.0f, 300.0f);
         const int aout = spawn("Audio Out", "Utility", 400.0f, 300.0f);
         const int formula = spawn("Formula", "Source", 40.0f, 40.0f);
         const int out = spawn("Output", "Utility", 400.0f, 40.0f);
         if (osc < 0 || aout < 0 || formula < 0 || out < 0)
            return false;
         static_cast<OscillatorNode*>(FindNodeByIndex(osc)->node.get())->frequency = hz;
         auto* f = static_cast<FormulaNode*>(FindNodeByIndex(formula)->node.get());
         f->formula = std::string("return vec4(") + rgb + ", 1.0);";
         f->Apply();
         std::string err;
         if (!ConnectNodes(osc, 0, aout, 0, err) || !ConnectNodes(formula, 0, out, 0, err))
         {
            printf("SHOWSETTEST: connect failed: %s\n", err.c_str());
            return false;
         }
         return SavePatchTo(path);
      }

      void RunShowSetTest(int frameId, GLFWwindow* window)
      {
         static int sPhase = 0;
         static double sT = 0.0;
         static float sPreFloor = 0.0f, sSwitchFloor = 0.0f;
         static uint64_t sXruns = 0;
         static double sReqBeat = 0.0, sExpected = 0.0, sSwitchBeat = -1.0;
         static float sRedCentre[3], sBlueCentre[3];
         static const std::string kA = "/tmp/infinite_showset_a.inf", kB = "/tmp/infinite_showset_b.inf";
         static int sExitAfter = getenv("INFINITE_EXITAFTER") ? atoi(getenv("INFINITE_EXITAFTER")) : 0;
         AudioEngine& engine = AudioEngine::Instance();
         Transport& tr = Transport::Instance();
         const double now = glfwGetTime();
         auto fail = [&](const std::string& why) {
            printf("SHOWSETTEST FAIL (%s)\n", why.c_str());
            fflush(stdout);
            sPhase = 99;
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         };
         if (sPhase < 99 && sExitAfter > 0 && frameId >= sExitAfter - 1)
         {
            fail("timed out in phase " + std::to_string(sPhase));
            return;
         }

         switch (sPhase)
         {
            case 0:
               if (frameId < 5)
                  return;
               gTestActive = true;
               if (!AudioRunning() && !StartAudioEngine(gAudioStartError))
                  return fail("audio did not start: " + gAudioStartError);
               if (!BuildTestPatch(220.0f, "1.0, 0.0, 0.0", kA) || !BuildTestPatch(330.0f, "0.0, 0.0, 1.0", kB))
                  return fail("could not build the test patches");
               if (!LoadPatchFrom(kA))
                  return fail("could not open patch A");
               ShowSetClear();
               ShowSetAddFile(kA);
               ShowSetAddFile(kB);
               gCurrent = 0;
               for (GraphNode& gn : gNodes)
                  if (dynamic_cast<OutputNode*>(gn.node.get()) != nullptr)
                     OpenProjectorWindow(window, gn);
               if (gProjectorWindows.empty())
                  return fail("no projector window");
               tr.SetTempo(120.0f);
               if (!tr.IsPlaying())
                  tr.SetPlaying(true);
               sT = now;
               sPhase = 1;
               return;
            case 1: // settle, then measure the steady level
               if (now - sT < 1.0)
                  return;
               engine.ResetBlockPeakFloor();
               sT = now;
               sPhase = 2;
               return;
            case 2:
               if (now - sT < 0.5 || !ShowSetReady(0) || !ShowSetReady(1))
                  return;
               sPreFloor = engine.BlockPeakFloor();
               std::copy(gLastCentre, gLastCentre + 3, sRedCentre);
               sXruns = engine.XrunCount();
               sPhase = 3;
               return;
            case 3: // ask mid-bar (beat 1.5 to 2.5 of 4)
            {
               const double inBar = std::fmod(Beats(), tr.BeatsPerBar());
               if (inBar < 1.5 || inBar > 2.5)
                  return;
               gQuantize = MusicTime::kLaunchBar;
               gFadeMs = 1000.0f;
               engine.ResetBlockPeakFloor();
               sReqBeat = Beats();
               sExpected = MusicTime::NextLaunchBeat(sReqBeat, MusicTime::kLaunchBar, 0.25);
               ShowSetStep(+1);
               sT = now;
               sPhase = 4;
               return;
            }
            case 4: // the bar switch and its fade
               if (gRequest >= 0 || gHoldActive || engine.TailActive())
                  return;
               sSwitchFloor = engine.BlockPeakFloor();
               sSwitchBeat = engine.LastSwitchBeat();
               sT = now;
               sPhase = 5;
               return;
            case 5: // let B's picture settle, then switch back at once
               if (now - sT < 0.3)
                  return;
               std::copy(gLastCentre, gLastCentre + 3, sBlueCentre);
               {
                  GraphNode* gn = gProjectorWindows.empty() ? nullptr : FindNodeByIndex(gProjectorWindows[0].nodeIndex);
                  if (gn == nullptr || dynamic_cast<OutputNode*>(gn->node.get()) == nullptr || gPatchPath != kB || gCurrent != 1)
                     return fail("after the switch the window is not on patch B's Output");
               }
               gQuantize = MusicTime::kLaunchNone;
               ShowSetStep(-1);
               sPhase = 6;
               return;
            case 6:
               if (gRequest >= 0 || gHoldActive || engine.TailActive())
                  return;
               sT = now;
               sPhase = 7;
               return;
            case 7:
            {
               if (now - sT < 0.3)
                  return;
               const double sr = engine.SampleRate();
               const double twoSamples = 2.0 * tr.Tempo() / (60.0 * sr);
               const double barPos = std::fmod(sSwitchBeat, tr.BeatsPerBar());
               const double offBar = std::min(barPos, tr.BeatsPerBar() - barPos);
               const double latency = sSwitchBeat - sReqBeat;
               const uint64_t xruns = engine.XrunCount() - sXruns;
               const bool backOnA = gPatchPath == kA && gCurrent == 0 && gLastCentre[0] > 0.5f && gLastCentre[2] < 0.2f;
               printf("SHOWSETTEST: floor pre %.3f switch %.3f; request beat %.3f, expected %.3f, switched %.4f "
                      "(%.2f samples off the bar); latency %.3f beats; xruns %llu; black frames %d; "
                      "red (%.2f %.2f %.2f) blue (%.2f %.2f %.2f)\n",
                      sPreFloor, sSwitchFloor, sReqBeat, sExpected, sSwitchBeat, offBar / (twoSamples / 2.0), latency,
                      (unsigned long long)xruns, gBlackFrames, sRedCentre[0], sRedCentre[1], sRedCentre[2],
                      sBlueCentre[0], sBlueCentre[1], sBlueCentre[2]);
               std::string why;
               if (!(sPreFloor > 0.05f))
                  why += "no steady sound before the switch; ";
               if (sSwitchFloor < 0.5f * sPreFloor)
                  why += "dropout in the crossfade; ";
               if (xruns != 0)
                  why += "xruns; ";
               if (std::fabs(sSwitchBeat - sExpected) > twoSamples || offBar > twoSamples)
                  why += "switch not on the bar line; ";
               if (latency > tr.BeatsPerBar() + 1e-6)
                  why += "latency over a bar; ";
               if (gBlackFrames != 0)
                  why += "black projector frames; ";
               if (!(sRedCentre[0] > 0.5f && sRedCentre[2] < 0.2f) || !(sBlueCentre[2] > 0.5f && sBlueCentre[0] < 0.2f))
                  why += "projector did not show A then B; ";
               if (!backOnA)
                  why += "switch back to A did not land; ";
               if (!why.empty())
                  return fail(why);
               printf("SHOWSETTEST OK\n");
               fflush(stdout);
               sPhase = 99;
               glfwSetWindowShouldClose(window, GLFW_TRUE);
               return;
            }
            default:
               return;
         }
      }
   }
}
