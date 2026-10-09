// Patch build, settings files, autosave, new patch (moved verbatim from main.cpp).
#include "app/AppShared.h"
#include "core/Notices.h"

namespace app
{
   // ---- saving ----
   // Everything SavePatchTo used to build in place, minus the file write -
   // shared with undo/redo, which snapshots this same in-memory shape rather
   // than round-tripping through disk.
   Patch::Data BuildPatchData()
   {
      ScopedPerfTimer perfTimer("BuildPatchData");
      Patch::Data data;

      // Reverse-lookup tables built once (a single O(N) pass, one dynamic_cast
      // per node per interface) so every per-cable scan below is a map lookup
      // instead of an O(N) linear search - this function used to be O(N^2)
      // with dynamic_cast in the inner loop. See
      // docs/plans/undo-delete-perf-prompt.md.
      std::unordered_map<const void*, int> addrToIndex;
      addrToIndex.reserve(gNodes.size() * 2);
      for (GraphNode& gn : gNodes)
      {
         addrToIndex[(const void*)gn.node.get()] = gn.index;
         // IGeometrySource*/IPaletteSource* are different addresses into the
         // same object under multiple inheritance, so each cast that this
         // function compares against needs its own entry.
         if (auto* asGeo = dynamic_cast<IGeometrySource*>(gn.node.get()))
            addrToIndex[(const void*)asGeo] = gn.index;
         if (auto* asPal = dynamic_cast<IPaletteSource*>(gn.node.get()))
            addrToIndex[(const void*)asPal] = gn.index;
      }
      // Modulator addresses are keyed to the specific output index they came
      // from, unlike the plain addresses above - mirrors ModulatorForOutput's
      // two cases. Built in gNodes/output order with first-write-wins
      // (unordered_map::emplace no-ops on an existing key), matching the
      // original nested scan's "first match wins" semantics exactly.
      struct ModSource { int index; int output; };
      std::unordered_map<const void*, ModSource> modAddrToSource;
      for (GraphNode& src : gNodes)
      {
         int outputs = std::max(1, src.node->OutputCount());
         for (int o = 0; o < outputs; o++)
            if (IModulator* mod = ModulatorForOutput(src.node.get(), o))
               modAddrToSource.emplace((const void*)mod, ModSource{ src.index, o });
      }

      for (GraphNode& gn : gNodes)
      {
         Patch::NodeRecord rec;
         rec.index = gn.index;
         rec.uid = gn.uid;
         rec.category = gn.category;
         rec.typeName = gn.typeName;
         // The cached live position, not the spawn position: the node has almost
         // certainly been dragged since it was created. Read from the cache
         // rather than the editor, since saving runs outside the editor context.
         rec.x = gn.liveX;
         rec.y = gn.liveY;
         rec.showParams = gn.showParams;
         rec.bypassed = gn.node->bypassed;
         rec.showMiniViewport = gn.showMiniViewport;
         rec.showAdvancedParams = gn.showAdvancedParams;
         Patch::SaveParams(gn.node.get(), rec.params);
         data.nodes.push_back(std::move(rec));

         for (int slot = 0; slot < InputCountFor(gn); slot++)
         {
            if (ImageCable* cable = CableFor(gn, slot))
            {
               if (!cable->IsConnected())
                  continue;
               auto it = addrToIndex.find((const void*)cable->GetSource());
               if (it != addrToIndex.end())
                  data.cables.push_back({ gn.index, slot, it->second, cable->GetSourceOutput() });
            }
         }
         // Audio/note cables are typed like image cables (a plain
         // IsConnected()/GetSource()), not raw-pointer like geometry, so they
         // mirror the image-cable loop above rather than the pointer-
         // comparison `record` lambda below.
         for (int slot = 0; slot < kMaxAudioSlots; slot++)
         {
            AudioCable* cable = gn.node->AudioInputSlot(slot);
            if (cable == nullptr || !cable->IsConnected())
               continue;
            auto it = addrToIndex.find((const void*)cable->GetSource());
            if (it != addrToIndex.end())
               data.audio.push_back({ gn.index, slot, it->second, cable->GetOutputSlot() });
         }
         for (int slot = 0; slot < kMaxNoteSlots; slot++)
         {
            NoteCable* cable = gn.node->NoteInputSlot(slot);
            if (cable == nullptr || !cable->IsConnected())
               continue;
            auto it = addrToIndex.find((const void*)cable->GetSource());
            if (it != addrToIndex.end())
               data.notes.push_back({ gn.index, slot, it->second, cable->GetOutputSlot() });
         }
      }

      // Geometry, camera, light, audio and modulator-input pins, found by
      // looking up each candidate source's address rather than scanning for it.
      for (GraphNode& gn : gNodes)
      {
         auto record = [&](const void* wanted, int slot)
         {
            if (wanted == nullptr)
               return;
            auto it = addrToIndex.find(wanted);
            if (it != addrToIndex.end())
               data.geometry.push_back({ gn.index, slot, it->second });
         };

         // Render3DNode's geometry slots are found generically below via
         // GeometryInputSlot() too - only its camera/light pins, which aren't
         // geometry, need special-casing here.
         if (auto* render = dynamic_cast<Render3DNode*>(gn.node.get()))
         {
            record(render->camera, Render3DNode::kSlots);
            for (int i = 0; i < Render3DNode::kLightSlots; i++)
               record(render->lights[i], Render3DNode::kSlots + 1 + i);
         }
         for (int slot = 0; slot < kMaxGeometrySlots; slot++)
            if (IGeometrySource** field = gn.node->GeometryInputSlot(slot))
               record(*field, slot);
         if (auto* setColor = dynamic_cast<SetColorNode*>(gn.node.get()))
         {
            // paletteInput is an IPaletteSource*, not an IGeometrySource*, so
            // it needs its own comparison the same way camera/light do above.
            if (setColor->paletteInput != nullptr)
               record(setColor->paletteInput, 2);
         }
         if (int count = gn.node->ModulatorInputCount())
         {
            for (int slot = 0; slot < count; slot++)
            {
               IModulator* wanted = *gn.node->ModulatorInputSlot(slot);
               if (wanted == nullptr)
                  continue;
               auto it = modAddrToSource.find((const void*)wanted);
               if (it != modAddrToSource.end())
                  // The output index matters here in a way it doesn't for
                  // geometry/camera/light pins: a modulator source can
                  // expose several outputs, and dropping it re-attached every
                  // restored cable to output 0.
                  data.geometry.push_back({ gn.index, slot, it->second.index, it->second.output });
            }
         }
      }

      for (const auto& link : Modulation::Instance().Links())
         data.modulation.push_back({ link.first.first, link.first.second,
                                     link.second.nodeIndex, link.second.outputIndex,
                                     link.second.polarity, link.second.depth, link.second.centre,
                                     link.second.lo, link.second.hi, link.second.hasRange,
                                     link.second.enabled, link.second.curve });
      // Shift-drag/armed recordings looping right now - previously session-
      // only (see GestureRecorder.h), now part of the saved patch itself,
      // same as modulation/palette bindings just above.
      for (const auto& [key, playback] : GestureRecorder::Instance().Playbacks())
      {
         Patch::GestureRecord g;
         g.dstIndex = key.first;
         g.dstParam = key.second;
         g.speed = playback.speed;
         g.hasRangeOverride = playback.hasRangeOverride;
         g.rangeLo = playback.rangeLo;
         g.rangeHi = playback.rangeHi;
         g.curve = playback.curve;
         g.samples.reserve(playback.samples.size());
         for (const GestureRecorder::Sample& s : playback.samples)
            g.samples.push_back({ s.value, s.timeSec, s.startsNewGrab });
         data.gestures.push_back(std::move(g));
      }
      for (const auto& link : PaletteBinding::Instance().Links())
         data.palette.push_back({ link.first.first, link.first.second,
                                  link.second.nodeIndex, link.second.swatchIndex });
      for (const auto& expr : Modulation::Instance().Expressions())
      {
         Patch::ExprRecord rec;
         rec.dstIndex = expr.first.first;
         rec.dstParam = expr.first.second;
         rec.text = expr.second;
         rec.curve = Modulation::Instance().ExpressionCurveFor(rec.dstIndex, rec.dstParam);
         data.expressions.push_back(std::move(rec));
      }
      // Written in list order: a global may reference the ones above it, so
      // the order is part of the meaning, not just presentation.
      for (const ExprGlobals::Global& g : ExprGlobals::All())
         data.globals.push_back({ g.name, g.expr });
      data.performance = gPerfElements;
      data.perfLayout = gPerfLayout;
      // gArrange is the only arrangement state there is (WP5b deleted the
      // seconds mirror), loop included.
      ArrangeModelToPatchData(gArrange, data);
      data.transport.bpm = Transport::Instance().Tempo();
      data.transport.timeSigNum = Transport::Instance().TimeSigNumerator();
      data.transport.timeSigDen = Transport::Instance().TimeSigDenominator();
      data.transport.key = Transport::Instance().Key();
      data.transport.scale = Transport::Instance().Scale();
      data.viewport.open = gViewportPanelOpen;
      data.viewport.dock = gViewportPanelDock;
      data.viewport.width = gViewportPanelWidth;
      data.viewport.height = gViewportPanelHeight;
      data.viewport.nodes = gViewportPanelNodes;
      return data;
   }


   // Self-tests exercising this mechanism (AUTOSAVETEST, AUTOSAVEMARKERTEST)
   // redirect to their own file so they never read, write, or delete the
   // real user's crash-recovery state - the same reasoning as
   // INFINITE_DRAGTEST's separate graphPath a few hundred lines up.
   bool UsingAutosaveTestPaths()
   {
      return getenv("INFINITE_AUTOSAVETEST") != nullptr || getenv("INFINITE_AUTOSAVEMARKERTEST") != nullptr;
   }


   std::string AutosavePath()
   {
      std::string dir = AppPaths::AppSupportDir();
      if (dir.empty())
         return {};
      return dir + (UsingAutosaveTestPaths() ? "/Infinite.autosave-test.inf" : "/Infinite.autosave.inf");
   }


   std::string AutosaveMarkerPath()
   {
      std::string dir = AppPaths::AppSupportDir();
      if (dir.empty())
         return {};
      return dir + (UsingAutosaveTestPaths() ? "/Infinite.session-active-test" : "/Infinite.session-active");
   }


   // Mirrors CategoryColors::ThemePath()/LoadPreference(): one flat
   // preference file next to the app's other Application Support state.
   // Covers everything on the General & Performance settings tab (autosave,
   // target FPS, vsync) - kept as one file since the file predates the FPS/
   // vsync fields and renaming it would strand existing users' autosave prefs.
   std::string GeneralSettingsPath()
   {
      std::string dir = AppPaths::AppSupportDir();
      return dir.empty() ? std::string() : dir + "/Infinite.autosave-settings";
   }


   void LoadGeneralSettings()
   {
      const std::string path = GeneralSettingsPath();
      if (path.empty())
         return;
      std::ifstream file(path);
      std::string line;
      if (std::getline(file, line) && !line.empty())
         gAutosaveEnabled = (line != "0");
      if (std::getline(file, line) && !line.empty())
      {
         const int seconds = atoi(line.c_str());
         if (seconds > 0)
            gAutosaveSeconds = seconds;
      }
      if (std::getline(file, line) && !line.empty())
         gTargetFps = atoi(line.c_str());
      if (std::getline(file, line) && !line.empty())
         gVsync = (line != "0");
      if (std::getline(file, line) && !line.empty())
         gCheckerboardBackdrop = (line != "0");
      if (std::getline(file, line) && !line.empty())
         gMetronomeVolume = std::clamp((float)atof(line.c_str()), 0.0f, 1.0f);
      if (std::getline(file, line) && !line.empty())
         gMetronomeAccent = (line != "0");
   }


   void SaveGeneralSettings()
   {
      const std::string path = GeneralSettingsPath();
      if (path.empty())
         return;
      std::ofstream file(path);
      file << (gAutosaveEnabled ? "1" : "0") << "\n" << gAutosaveSeconds << "\n"
           << gTargetFps << "\n" << (gVsync ? "1" : "0") << "\n"
           << (gCheckerboardBackdrop ? "1" : "0") << "\n"
           << gMetronomeVolume << "\n" << (gMetronomeAccent ? "1" : "0") << "\n";
   }


   // One flat preference file for the Canvas & Workspace settings tab.
   std::string WorkspaceSettingsPath()
   {
      std::string dir = AppPaths::AppSupportDir();
      return dir.empty() ? std::string() : dir + "/Infinite.workspace-settings";
   }


   void LoadWorkspaceSettings()
   {
      const std::string path = WorkspaceSettingsPath();
      if (path.empty())
         return;
      std::ifstream file(path);
      std::string line;
      while (std::getline(file, line))
      {
         const size_t eq = line.find('=');
         if (eq == std::string::npos)
            continue;
         const std::string key = line.substr(0, eq);
         const std::string val = line.substr(eq + 1);
         if (key == "snapToGrid")
            gSnapToGrid = (val != "0");
         else if (key == "showCanvasGrid")
            gShowCanvasGrid = (val != "0");
         else if (key == "gridSnap")
            gGridSnap = std::strtof(val.c_str(), nullptr);
         else if (key == "zoomSensitivity")
            gZoomSensitivity = std::strtof(val.c_str(), nullptr);
         else if (key == "minimapEnabled")
            gMinimapEnabled = (val != "0");
         else if (key == "minimapCorner")
            gMinimapCorner = atoi(val.c_str());
         else if (key == "minimapSize")
            gMinimapSize = std::strtof(val.c_str(), nullptr);
         else if (key == "minimapOpacity")
            gMinimapOpacity = std::strtof(val.c_str(), nullptr);
         else if (key == "cableVisibilityMask")
            gCableVisibilityMask = atoi(val.c_str());
      }
   }


   void SaveWorkspaceSettings()
   {
      const std::string path = WorkspaceSettingsPath();
      if (path.empty())
         return;
      std::ofstream file(path);
      file << "snapToGrid=" << (gSnapToGrid ? 1 : 0) << "\n";
      file << "showCanvasGrid=" << (gShowCanvasGrid ? 1 : 0) << "\n";
      file << "gridSnap=" << gGridSnap << "\n";
      file << "zoomSensitivity=" << gZoomSensitivity << "\n";
      file << "minimapEnabled=" << (gMinimapEnabled ? 1 : 0) << "\n";
      file << "minimapCorner=" << gMinimapCorner << "\n";
      file << "minimapSize=" << gMinimapSize << "\n";
      file << "minimapOpacity=" << gMinimapOpacity << "\n";
      file << "cableVisibilityMask=" << gCableVisibilityMask << "\n";
   }


   // One flat preference file for the Audio settings tab.
   std::string AudioSettingsPath()
   {
      std::string dir = AppPaths::AppSupportDir();
      return dir.empty() ? std::string() : dir + "/Infinite.audio-settings";
   }


   void LoadAudioSettings()
   {
      const std::string path = AudioSettingsPath();
      if (path.empty())
         return;
      std::ifstream file(path);
      std::string line;
      while (std::getline(file, line))
      {
         const size_t eq = line.find('=');
         if (eq == std::string::npos)
            continue;
         const std::string key = line.substr(0, eq);
         const std::string val = line.substr(eq + 1);
         if (key == "outputDeviceId")
            gAudioOutputDeviceId = (uint32_t)strtoul(val.c_str(), nullptr, 10);
         else if (key == "inputDeviceId")
         {
            gAudioInputDeviceId = (uint32_t)strtoul(val.c_str(), nullptr, 10);
            Platform::AudioInputCaptureSetDevice(gAudioInputDeviceId);
         }
         else if (key == "sampleRate")
            gAudioSampleRate = strtod(val.c_str(), nullptr);
         else if (key == "bufferFrames")
            gAudioBufferFrames = atoi(val.c_str());
         else if (key == "oversample")
            gAudioOversample = std::strtof(val.c_str(), nullptr);
         else if (key == "outputMode")
            gAudioOutputMode = std::clamp(atoi(val.c_str()), 0, 2);
      }
   }


   void SaveAudioSettings()
   {
      const std::string path = AudioSettingsPath();
      if (path.empty())
         return;
      std::ofstream file(path);
      file << "outputDeviceId=" << gAudioOutputDeviceId << "\n";
      file << "inputDeviceId=" << gAudioInputDeviceId << "\n";
      file << "sampleRate=" << gAudioSampleRate << "\n";
      file << "bufferFrames=" << gAudioBufferFrames << "\n";
      file << "oversample=" << gAudioOversample << "\n";
      file << "outputMode=" << gAudioOutputMode << "\n";
   }


   // One flat preference file holding the user's default expression globals -
   // the set every brand-new patch starts with, independent of any saved
   // .patch file (which carries its own globals as `glob` lines, see
   // Patch::Data::globals). Same "glob <name> <escaped expr>" shape as the
   // patch format so the two stay easy to reason about together, but this is
   // a separate file: it is an app-wide preference, not part of a document.
   std::string ExprGlobalsSettingsPath()
   {
      std::string dir = AppPaths::AppSupportDir();
      return dir.empty() ? std::string() : dir + "/Infinite.expression-globals";
   }


   std::string EscapeExprGlobalsLine(const std::string& value)
   {
      std::string clean;
      for (char c : value)
      {
         if (c == '\\')
            clean += "\\\\";
         else if (c == '\n')
            clean += "\\n";
         else if (c != '\r')
            clean += c;
      }
      return clean;
   }


   std::string UnescapeExprGlobalsLine(const std::string& raw)
   {
      std::string out;
      for (size_t i = 0; i < raw.size(); i++)
      {
         if (raw[i] == '\\' && i + 1 < raw.size() && raw[i + 1] == 'n')
         {
            out += '\n';
            i++;
         }
         else if (raw[i] == '\\' && i + 1 < raw.size() && raw[i + 1] == '\\')
         {
            out += '\\';
            i++;
         }
         else
         {
            out += raw[i];
         }
      }
      return out;
   }


   // Appends the persisted default globals onto whatever is currently in
   // ExprGlobals::All() - callers clear first if they want a clean slate.
   void LoadDefaultExprGlobals()
   {
      const std::string path = ExprGlobalsSettingsPath();
      if (path.empty())
         return;
      std::ifstream file(path);
      if (!file)
         return;
      std::string tag, line;
      while (file >> tag)
      {
         std::getline(file, line);
         if (tag != "glob")
            continue;
         std::istringstream nameStream(line);
         std::string name;
         nameStream >> name;
         std::string raw;
         std::getline(nameStream, raw);
         if (!raw.empty() && raw[0] == ' ')
            raw.erase(0, 1);
         if (name.empty())
            continue;
         ExprGlobals::All().push_back({ name, UnescapeExprGlobalsLine(raw), 0.0f, std::string() });
      }
   }


   // Saves the live ExprGlobals::All() as the new app-wide default. Called
   // after every edit in Settings > Expression Globals so the defaults stay
   // in sync with what's on screen, the same immediate-persist pattern as
   // SaveWorkspaceSettings/SaveAudioSettings above.
   void SaveDefaultExprGlobals()
   {
      const std::string path = ExprGlobalsSettingsPath();
      if (path.empty())
         return;
      std::ofstream file(path);
      for (const ExprGlobals::Global& g : ExprGlobals::All())
         file << "glob " << g.name << " " << EscapeExprGlobalsLine(g.expr) << "\n";
   }


   void DiscardAutosave()
   {
      const std::string path = AutosavePath();
      if (!path.empty())
      {
         std::error_code ec;
         std::filesystem::remove(path, ec);
      }
   }


   // Writes the live graph to the autosave file, atomically: write a .tmp
   // then rename it over the real path, so a crash mid-write leaves the
   // previous autosave intact instead of a truncated file that fails to
   // parse. std::filesystem::rename is atomic within a filesystem on POSIX
   // but fails on Windows if the destination exists, hence the remove+retry
   // fallback - not fully atomic there (a window exists where neither file
   // is present) but strictly better than writing in place.
   bool WriteAutosaveNow()
   {
      const std::string path = AutosavePath();
      if (path.empty())
         return false;
      const Patch::Data data = BuildPatchData();
      const std::string temp = path + ".tmp";
      std::string error;
      if (!Patch::Write(temp, data, error))
         return false;

      std::error_code ec;
      std::filesystem::rename(temp, path, ec);
      if (ec)
      {
         ec.clear();
         std::filesystem::remove(path, ec);
         ec.clear();
         std::filesystem::rename(temp, path, ec);
      }
      return !ec;
   }


   // Called once per frame from the main loop. Deliberately does not check
   // gPatchDirty. Several continuous controls update live while dragged and
   // create their undo checkpoint only at gesture end, so a crash mid-gesture
   // must still recover the values the user was actually seeing and hearing.
   void PollColorStatsAutosave(double now);

   void PollPredictionProfilesAutosave(double now);


   void PollAutosave()
   {
      // A harness or screenshot run must never overwrite the real user's crash-recovery file.
      if (!gAutosaveEnabled || gNodes.empty() || (IsHeadlessProcess() && !UsingAutosaveTestPaths()))
         return;
      const double now = glfwGetTime();
      if (gLastAutosaveTime > 0.0 && now - gLastAutosaveTime < (double)gAutosaveSeconds)
         return;
      gLastAutosaveTime = now;
      const bool wrote = WriteAutosaveNow();
      gAutosaveFailed = !wrote;
      if (!wrote && !gAutosaveFailureLogged)
      {
         gAutosaveFailureLogged = true;
         const std::string path = AutosavePath();
         Platform::AppendLogLine(
            "autosave write failed" +
            (path.empty() ? std::string(" (no writable settings directory)")
                          : std::string(" (") + path + ")"));
      }
      if (wrote)
         gAutosaveFailureLogged = false;

      PollColorStatsAutosave(now);
      PollPredictionProfilesAutosave(now);
   }


   // Predictive Coloring's global learned profile used to be saved only on
   // Stop-Learning, so a crash mid-session (or simply never pressing Stop)
   // lost that session's colour learning entirely. Piggyback on the same
   // gAutosaveSeconds cadence (independent of gAutosaveEnabled - this is
   // learned-data persistence, not patch-file autosave) and on the shutdown
   // path below.
   void PollColorStatsAutosave(double now)
   {
      static double sLastSave = 0.0;
      if (sLastSave > 0.0 && now - sLastSave < (double)gAutosaveSeconds)
         return;
      sLastSave = now;
      if (ColorStats::Engine::Instance().HasLearnedData())
         ColorStats::Engine::Instance().Save(AppPaths::AppSupportDir() + "/prediction");
   }


   // Predictive Quantize/Velocity have no Learn/Stop anymore - they are always capturing and
   // always adapting - so there is no "Stop" moment to hang a save off of at all. Same cadence and
   // shutdown-path pairing as PollColorStatsAutosave above.
   void PollPredictionProfilesAutosave(double now)
   {
      static double sLastSave = 0.0;
      if (sLastSave > 0.0 && now - sLastSave < (double)gAutosaveSeconds)
         return;
      sLastSave = now;
      if (PredictiveQuantizeProfile::HasLearnedData())
         PredictiveQuantizeProfile::Save(AppPaths::AppSupportDir() + "/prediction");
      if (PredictiveVelocityProfile::HasLearnedData())
         PredictiveVelocityProfile::Save(AppPaths::AppSupportDir() + "/prediction");
      if (PredictiveNotesStyle::HasLearnedData())
         PredictiveNotesStyle::Save(AppPaths::AppSupportDir() + "/prediction");
      if (PredictiveRhythmStyle::HasLearnedData())
         PredictiveRhythmStyle::Save(AppPaths::AppSupportDir() + "/prediction");
   }


   // Called once at startup, after the graph and GL are initialised but
   // before the first frame is presented. Reads the *previous* run's
   // marker/autosave (if any) so the first frame's UI can offer recovery,
   // then writes a fresh marker for *this* run regardless of what it found -
   // a run that exits cleanly deletes it again at the single post-loop
   // cleanup site.
   void CheckAutosaveRecovery()
   {
      const std::string marker = AutosaveMarkerPath();
      const std::string autosave = AutosavePath();
      if (!marker.empty() && std::filesystem::exists(marker))
      {
         if (!autosave.empty() && std::filesystem::exists(autosave))
         {
            Patch::Data data;
            std::string error;
            if (Patch::Read(autosave, data, error))
            {
               gPendingRecoveryData = std::move(data);
               gShowAutosaveRecoveryModal = true;

               std::error_code ec;
               const auto ftime = std::filesystem::last_write_time(autosave, ec);
               if (!ec)
               {
                  // Pre-C++20: file_time_type isn't system_clock, so rebase
                  // through "now" on both clocks rather than assuming a
                  // shared epoch.
                  const auto sctp = std::chrono::system_clock::now() +
                     std::chrono::duration_cast<std::chrono::system_clock::duration>(
                        ftime - std::filesystem::file_time_type::clock::now());
                  const std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
                  if (std::tm* tmVal = std::localtime(&tt))
                  {
                     char buf[64] = "";
                     std::strftime(buf, sizeof(buf), "%b %d, %I:%M %p", tmVal);
                     gAutosaveRecoveryTimestamp = buf;
                  }
               }
            }
            else
            {
               // Corrupt autosave: say so and leave the file on disk rather
               // than deleting it - a corrupt file the user can still find
               // and inspect beats one silently erased.
               gAutosaveRecoveryError = error;
               gPatchStatus = std::string(T("Autosave found but could not be read: ")) + error;
               Notices::Post(Notices::Level::Warning, "autosave.unreadable", "Last session's autosave couldn't be read",
                             "It is left on disk, not deleted. Your saved patches are unaffected.");
            }
         }
         // Marker present, no autosave: nothing to offer, not an error.
      }

      if (!marker.empty())
      {
         std::ofstream m(marker);
      }
   }


   void NotePatchFileStamp(const std::string& path);
 // R39, defined with the watcher below

   bool SavePatchTo(const std::string& path)
   {
      Patch::Data data = BuildPatchData();
      std::string error;
      if (!Patch::Write(path, data, error))
      {
         gPatchStatus = std::string(T("Save failed: ")) + error;
         Notices::Post(Notices::Level::Error, "patch.save", "Couldn't save the patch",
                       error + ". Your patch is still open and unchanged. Check the disk has space and the folder can be written to, or use Save As to pick another place.");
         return false;
      }

      gPatchPath = path;
      gPatchDirty = false;
      gPatchStatus = "Saved";
      NotePatchFileStamp(path); // our own write is not an outside change
      Patch::NoteRecent(path);
      // Whatever the autosave was covering is now safely on disk under the
      // user's own file - see §3: leaving it around would offer to recover
      // already-saved work on the next launch.
      DiscardAutosave();
      gLastAutosaveTime = 0.0;
      return true;
   }


   // The clock gesture playback reads, read safely. Live, that is
   // GestureRecorder::ClockNow(): the clock samples were timestamped with,
   // which only advances while Transport plays (see AdvanceClock). During an
   // offline render (Render Now, headless --render/--frame, arrange WAV) it is
   // Transport's offline video seconds instead: AdvanceClock runs once per UI
   // frame on wall-clock DeltaTime, while the offline pump renders many video
   // frames per UI frame, so the live clock would stair-step and differ run to
   // run. Offline, every loop starts at 0 (GestureSyncClockAxis), so a loop's
   // phase is (T * speed) mod duration and --frame at T matches --render at T.
   //
   // Undo/Redo are reachable before ImGui::CreateContext() - the headless
   // self-tests that exercise the undo stack (PERFMATRIXTEST) run from main()
   // well before the context exists. Both clocks are plain stored values
   // rather than ImGui::GetTime(), so this is safe to read with no context.
   double GesturePlaybackClock()
   {
      Transport& transport = Transport::Instance();
      return transport.IsOfflineMode() ? transport.Seconds() : GestureRecorder::Instance().ClockNow();
   }


   double GestureClockNow()
   {
      return GesturePlaybackClock();
   }


   // Re-bases every loop's startTime when playback switches clocks, so the
   // startTime and the clock GetPlaybackValue reads are on the same axis.
   // One edge detector here instead of a call at every SetOfflineMode site:
   // Render Now, --frame, arrange WAV and the self-test fixtures all toggle
   // offline mode, and a missed site would leave loops on the wrong axis.
   // Called once per ApplyModulationAndPalette, before playback is read.
   void GestureSyncClockAxis()
   {
      static bool sWasOffline = false;
      const bool offline = Transport::Instance().IsOfflineMode();
      if (offline == sWasOffline)
         return;
      sWasOffline = offline;
      GestureRecorder& recorder = GestureRecorder::Instance();
      recorder.RestartLoops(offline ? 0.0 : recorder.ClockNow());
   }


   // Rewrites a snapshot's gesture keys from the indices that were live when
   // it was captured to the fresh indices ApplyPatchData just handed out.
   // A recording whose node has no remap entry belonged to a node that does
   // not exist at this point in history and is dropped.
   GestureRecorder::PlaybackMap RemapGestures(const GestureRecorder::PlaybackMap& gestures,
                                              const std::map<int, int>& remap)
   {
      GestureRecorder::PlaybackMap out;
      for (const auto& [key, playback] : gestures)
      {
         auto it = remap.find(key.first);
         if (it != remap.end())
            out[GestureRecorder::Key(it->second, key.second)] = playback;
      }
      return out;
   }


   // A brand new document (and a fresh app launch before any patch is
   // recovered/opened) starts with four video and four audio lanes
   // rather than an empty timeline - matches every DAW/NLE default and
   // saves the "+ Track" click most sessions would make anyway. Called
   // only when gArrange has no lanes yet so it never clobbers a
   // loaded or in-progress arrangement (a genuine File > Open, or
   // CheckAutosaveRecovery, replaces gArrange wholesale via
   // ApplyPatchData right after this could run - same seed-then-overwrite
   // shape as LoadDefaultExprGlobals()).
   void SeedDefaultArrangeStreams()
   {
      if (!gArrange.lanes.empty())
         return;
      for (int i = 0; i < 4; i++)
      {
         const uint64_t id = Arrange::AddLane(gArrange, Arrange::kLaneVideo);
         Arrange::FindLane(gArrange, id)->name = "Video " + std::to_string(i + 1);
      }
      for (int i = 0; i < 4; i++)
      {
         const uint64_t id = Arrange::AddLane(gArrange, Arrange::kLaneAudio);
         Arrange::FindLane(gArrange, id)->name = "Audio " + std::to_string(i + 1);
      }
      PublishArrangeLoop();
   }


   void ClearPatchWatch();
 // R39: a genuinely new document stops the file watch

   void NewPatch()
   {
      MovementLog::NoteMark(MovementLog::Mark::PatchNew);
      // Learn state keys on node uids / element indices that this reset
      // invalidates; a learn must never survive a patch change.
      MidiLearnCancelAll();
      // Retire rather than destroy outright: NewPatch can run mid-frame (it's
      // the first step of ApplyPatchData, which Undo/Redo call), after this
      // frame's ImGui draw list has already queued AddImage() calls
      // referencing these nodes' GL output textures. Destroying them
      // synchronously here - same hazard RemoveNodeByIndex guards against -
      // would free a GL name pending draw commands still reference, flashing
      // garbage/reused-texture content for one frame right before the node
      // disappears. Actual teardown is gated on both the next frame AND the
      // audio thread confirming it's done with these nodes (see gRetiredNodes'
      // declaration and its drain next to glfwPollEvents()). Every node here
      // shares the same "last generation that can still reach it" - whatever
      // is current right now - since none of them can appear in any topology
      // RebuildAudioTopology builds after this point (gNodes is about to be
      // cleared).
      const uint64_t safeAfterGeneration = AudioEngine::Instance().CurrentGeneration();
      for (GraphNode& gn : gNodes)
         gRetiredNodes.push_back({ std::move(gn.node), safeAfterGeneration });
      while (!gNodeViewports.empty())
         gRetiredViewports.push_back(gNodeViewports.extract(gNodeViewports.begin()));
      gNodes.clear();
      InvalidateNodeByUid();
      gLinks.clear();
      gModHistory.clear();
      Modulation::Instance().Clear();
      // Same reason as Modulation::Clear() above: gNextIndex restarts at 1,
      // so a recording left keyed to an old node index would silently
      // re-attach to whichever node lands on that index next. Undo/Redo
      // restore their own snapshot immediately after this (see Undo()), so
      // clearing here is what makes a recording actually disappear when you
      // undo past the point it was made.
      GestureRecorder::Instance().Clear();
      {
         // nextId and revision are counters, not content: they carry across
         // the reset (ApplyPatchData relies on nextId surviving this for its
         // own clamp, and revision must only climb - see ApplyArrangeOnlyEntry).
         const uint64_t keepNextId = gArrange.nextId;
         const uint64_t keepRevision = gArrange.revision;
         gArrange = Arrange::Model();
         gArrange.nextId = keepNextId;
         gArrange.revision = keepRevision + 1;
      }
      gArrangeGestureOpen = false;
      gArrangeDrag = ArrangeDragState();
      gArrangeMarkerDragId = 0; // a flag drag's gesture just closed too
      ForgetAllDiscreteSlots();
      PaletteBinding::Instance().Clear();
      ExprGlobals::Clear();
      gViewportPanelOpen = false;
      gViewportPanelNodes.clear();
      gViewportPanelDock = 1;
      gViewportPanelWidth = 320.0f;
      gViewportPanelHeight = 260.0f;
      gNextIndex = 1;
      // Unlike gNextIndex, this does NOT restart: uids are only useful because
      // they are never reused, and a fresh document that started minting 1, 2,
      // 3 again would collide with the uids an undo entry from the previous
      // document still carries. ApplyPatchData clamps it upward, never down.
      gPatchPath.clear();
      gPatchDirty = false;
      gPatchStatus = T("New patch");
      // Only for a genuine "start a fresh document" - not when NewPatch is
      // called from inside ApplyPatchData as the first step of restoring a
      // snapshot, which must leave the stacks alone.
      if (!gSuppressUndoCheckpoints)
      {
         gUndoStack.clear();
         gRedoStack.clear();
         ClearPatchWatch();
         // A genuine "start a fresh document" also resets the global
         // transport - a loaded patch restores its own bpm/time signature/
         // key/scale right after this call (see ApplyPatchData), so this
         // would otherwise just be clobbered a moment later.
         Transport::Instance().SetTempo(120.0f);
         Transport::Instance().SetTimeSignature(4, 4);
         Transport::Instance().SetKey(0);
         Transport::Instance().SetScale(0);
         // A loaded/undone patch restores its own saved globals right after
         // this call (see ApplyPatchData) - only a genuine fresh document
         // seeds from the app-wide default set.
         LoadDefaultExprGlobals();
         // A new document: the clip clipboard and selection belong to the
         // old one (see gArrangePatchGeneration).
         gArrangePatchGeneration++;
         SeedDefaultArrangeStreams();
         // Audio routing is a monitoring choice, not part of the document
         // (see gAudioMode): a fresh document always starts on the canvas.
         // Inside this branch and not above it, so an undo/redo - which runs
         // NewPatch as its first step - never changes what you are hearing.
         gAudioMode = AudioMode::Canvas;
         // The nodes are gone; publish the empty graph so the engine stops
         // rendering them (otherwise the old sound plays on after New and the
         // retired nodes stay pinned by the old topology). Not for the
         // ApplyPatchData path, which rebuilds once the patch is restored.
         RebuildAudioTopology();
      }
   }


   // Writes `content` to `folder/filename`, for the "Install AI Skill"
   // buttons in Settings > Field Language and Settings > Expression Globals.
   // Returns a short status line for inline feedback next to the button -
   // there's no app-wide toast system to hook into, so the button's own row
   // is where success/failure has to show up.
   std::string SaveAISkillFile(const std::string& folder, const char* filename, const char* content)
   {
      const std::string path = folder + "/" + filename;
      std::ofstream file(path);
      if (!file.is_open())
         return "Couldn't write " + path;
      file << content;
      return "Saved " + path;
   }
}
