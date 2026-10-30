// The app's own surface over RPC: panels, the selection commands of the Edit menu, the show set
// and every setting (docs/plans/terminal/README.md D19). Same rules as Rpc.cpp: main thread,
// between frames. tools/terminal/parity.py holds the list of what has a verb.
#include <algorithm>
#include <functional>
#include "app/AppShared.h"
#include "app/graph/LearningLog.h"

namespace app
{
   namespace
   {
      using json = nlohmann::json;

      struct Panel
      {
         const char* name;
         bool* open;
         int* dock;       // nullptr = this panel has no dock position
         bool fourSides;  // false = bottom and top only
      };

      const std::vector<Panel>& Panels()
      {
         static const std::vector<Panel> panels = {
            { "viewport", &gViewportPanelOpen, &gViewportPanelDock, true },
            { "modmatrix", &gModMatrixOpen, &gModMatrixDock, true },
            { "perf", &gPerfPanelOpen, &gPerfPanelDock, true },
            { "timeline", &gArrangePanelOpen, nullptr, false },
            { "session", &gSessionPanelOpen, nullptr, false },
            { "samplestudio", &gSampleStudioOpen, &gSampleStudioDock, false },
            { "pianoroll", &gPianoRollOpen, &gPianoRollDock, false },
            { "terminal", &gTerminalOpen, &gTerminalDock, true },
            { "showset", &gShowSetOpen, nullptr, false },
            { "history", &gHistoryOpen, nullptr, false },
            { "settings", &gSettingsOpen, nullptr, false },
            { "templates", &gTemplatesOpen, nullptr, false },
            { "help", &gHelpOpen, nullptr, false },
            { "shortcuts", &gShortcutsOpen, nullptr, false },
         };
         return panels;
      }

      // The dock codes every panel shares: 0 bottom, 1 right, 2 left, 3 top.
      int DockCode(const std::string& side)
      {
         static const char* kSides[] = { "bottom", "right", "left", "top" };
         for (int i = 0; i < 4; i++)
            if (side == kSides[i])
               return i;
         return -1;
      }

      struct Setting
      {
         const char* key;
         std::function<json()> get;
         std::function<bool(const json&)> set; // false = the value is out of range
      };

      template <class T>
      Setting Number(const char* key, T* value, T lo, T hi, void (*save)())
      {
         return { key, [value] { return json(*value); },
                  [value, lo, hi, save](const json& v)
                  {
                     if (!v.is_number() || v.get<T>() < lo || v.get<T>() > hi)
                        return false;
                     *value = v.get<T>();
                     save();
                     return true;
                  } };
      }

      Setting Toggle(const char* key, bool* value, void (*save)())
      {
         return { key, [value] { return json(*value); },
                  [value, save](const json& v)
                  {
                     if (!v.is_boolean())
                        return false;
                     *value = v.get<bool>();
                     save();
                     return true;
                  } };
      }

      Setting Flag(const char* key, std::function<bool()> get, std::function<void(bool)> set)
      {
         return { key, [get] { return json(get()); },
                  [set](const json& v)
                  {
                     if (!v.is_boolean())
                        return false;
                     set(v.get<bool>());
                     return true;
                  } };
      }

      Setting Cable(const char* key, int bit)
      {
         return Flag(key, [bit] { return (gCableVisibilityMask & bit) != 0; },
                     [bit](bool on)
                     {
                        gCableVisibilityMask = (gCableVisibilityMask & ~bit) | (on ? bit : 0);
                        SaveWorkspaceSettings();
                     });
      }

      Setting Appearance(const char* key, float lo, float hi, float (*get)(), std::function<void(float)> set)
      {
         return { key, [get] { return json(get()); },
                  [lo, hi, set](const json& v)
                  {
                     if (!v.is_number() || v.get<float>() < lo || v.get<float>() > hi)
                        return false;
                     set(v.get<float>());
                     CategoryColors::SaveAppearanceOverrides();
                     return true;
                  } };
      }

      void ApplyAudioSettings()
      {
         const bool wasRunning = AudioEngine::Instance().SampleRate() > 0.0;
         if (wasRunning)
            AudioEngine::Instance().Stop();
         AudioEngine::Instance().SetRequestedDevice(gAudioOutputDeviceId);
         AudioEngine::Instance().SetRequestedSampleRate(gAudioSampleRate);
         AudioEngine::Instance().SetRequestedBufferFrames(gAudioBufferFrames);
         Platform::AudioSetAsioDriver(gAudioAsioDriver);
         Platform::AudioSetOutputMode(gAudioOutputMode);
         SaveAudioSettings();
         if (wasRunning)
         {
            gAudioStartError.clear();
            if (!StartAudioEngine(gAudioStartError))
               fprintf(stderr, "audio device: %s\n", gAudioStartError.c_str());
         }
      }

      // One row per control in the Settings window. Device, rate and buffer changes wait for
      // `settings apply`, as they wait for the window's Apply button.
      const std::vector<Setting>& Settings()
      {
         static const std::vector<Setting> settings = {
            Toggle("snapToGrid", &gSnapToGrid, SaveWorkspaceSettings),
            Toggle("canvasGrid", &gShowCanvasGrid, SaveWorkspaceSettings),
            Number("gridSize", &gGridSnap, 5.0f, 100.0f, SaveWorkspaceSettings),
            Number("zoomSensitivity", &gZoomSensitivity, 0.05f, 1.5f, SaveWorkspaceSettings),
            Toggle("minimap", &gMinimapEnabled, SaveWorkspaceSettings),
            Number("minimapCorner", &gMinimapCorner, 0, 3, SaveWorkspaceSettings),
            Number("minimapSize", &gMinimapSize, 120.0f, 360.0f, SaveWorkspaceSettings),
            Number("minimapOpacity", &gMinimapOpacity, 0.2f, 1.0f, SaveWorkspaceSettings),
            Cable("cablesModulation", 0x4),
            Cable("cablesAudioNote", 0x2),
            Cable("cablesImageGeometry", 0x1),
            Toggle("checkerboard", &gCheckerboardBackdrop, SaveGeneralSettings),
            Toggle("autosave", &gAutosaveEnabled, SaveGeneralSettings),
            Number("autosaveSeconds", &gAutosaveSeconds, 15, 300, SaveGeneralSettings),
            Number("targetFps", &gTargetFps, 0, 120, SaveGeneralSettings),
            Number("outputMode", &gAudioOutputMode, 0, 3, SaveAudioSettings),
            Flag("vsync", [] { return gVsync; },
                 [](bool on)
                 {
                    gVsync = on;
                    SetCanvasSwapInterval(on ? 1 : 0);
                    SaveGeneralSettings();
                 }),
            Appearance("nodeOpacity", 0.1f, 1.0f, CategoryColors::GetNodeOpacity,
                       [](float v) { CategoryColors::SetNodeOpacity(v, CategoryColors::IsThemeLight(), false); }),
            Appearance("nodeRadius", 0.0f, 24.0f, CategoryColors::GetNodeRounding,
                       [](float v)
                       {
                          CategoryColors::SetNodeRounding(v, false);
                          ApplyTheme();
                       }),
            Appearance("tint", 0.0f, 0.5f, CategoryColors::GetTintWeight,
                       [](float v) { CategoryColors::SetTintWeight(v, CategoryColors::IsThemeLight(), false); }),
            Appearance("uiScale", 0.5f, 2.0f, CategoryColors::GetUiScale,
                       [](float v)
                       {
                          CategoryColors::SetUiScale(v, false);
                          UiScale::RequestRescale();
                       }),
            Flag("tooltips", CategoryColors::GetTooltips, [](bool on) { CategoryColors::SetTooltips(on); }),
            Flag("reduceMotion", CategoryColors::GetReduceMotion, [](bool on) { CategoryColors::SetReduceMotion(on); }),
            Flag("cookTimes", CategoryColors::GetCookTimes, [](bool on) { CategoryColors::SetCookTimes(on); }),
            Flag("checkOnLaunch", UpdateCheck::AutoCheckEnabled, UpdateCheck::SetAutoCheckEnabled),
            Flag("credit", Provenance::Enabled,
                 [](bool on)
                 {
                    Provenance::SetEnabled(on);
                    SaveAudioSettings();
                 }),
            Flag("movementLog", MovementLog::IsEnabled, MovementLog::SetEnabled),
            Flag("learn", LearningLog::Enabled,
                 [](bool on)
                 {
                    LearningLog::SetEnabled(on);
                    MovementLog::SetEnabled(on);
                    SaveWorkspaceSettings();
                 }),
            Number("oversample", &gAudioOversample, 1.0f, 4.0f, SaveAudioSettings),
            { "audioThreads", [] { return json(gAudioThreads); },
              [](const json& v)
              {
                 if (!v.is_number_integer() || v.get<int>() < 0 || v.get<int>() > 1)
                    return false;
                 gAudioThreads = v.get<int>();
                 AudioEngine::Instance().SetAudioThreads(gAudioThreads);
                 SaveAudioSettings();
                 return true;
              } },
            { "preset", [] { return json(CategoryColors::PresetNames()[(size_t)CategoryColors::CurrentPreset()]); },
              [](const json& v)
              {
                 if (!v.is_string())
                    return false;
                 const std::vector<std::string>& names = CategoryColors::PresetNames();
                 const auto it = std::find(names.begin(), names.end(), v.get<std::string>());
                 if (it == names.end())
                    return false;
                 CategoryColors::SetPreset((int)(it - names.begin()));
                 ApplyTheme();
                 return true;
              } },
            { "font", [] { return json(CategoryColors::GetUiFont()); },
              [](const json& v)
              {
                 if (!v.is_string())
                    return false;
                 CategoryColors::SetUiFont(v.get<std::string>());
                 UiScale::RequestRescale();
                 return true;
              } },
            { "language", [] { return json(I18n::CurrentLanguage()); },
              [](const json& v)
              {
                 if (!v.is_string())
                    return false;
                 const std::vector<I18n::Language>& all = I18n::Languages();
                 const std::string code = v.get<std::string>();
                 if (std::none_of(all.begin(), all.end(), [&](const I18n::Language& l) { return code == l.code; }))
                    return false;
                 CategoryColors::SetLanguage(code);
                 I18n::RequestLanguage(code);
                 UiScale::RequestRescale();
                 return true;
              } },
            { "outputDevice", [] { return json(gAudioOutputDeviceId); },
              [](const json& v)
              {
                 if (!v.is_number_integer())
                    return false;
                 gAudioOutputDeviceId = v.get<decltype(gAudioOutputDeviceId)>();
                 return true;
              } },
            { "inputDevice", [] { return json(gAudioInputDeviceId); },
              [](const json& v)
              {
                 if (!v.is_number_integer())
                    return false;
                 gAudioInputDeviceId = v.get<decltype(gAudioInputDeviceId)>();
                 Platform::AudioInputCaptureSetDevice(gAudioInputDeviceId);
                 return true;
              } },
            { "sampleRate", [] { return json(gAudioSampleRate); },
              [](const json& v)
              {
                 if (!v.is_number() || v.get<double>() < 0.0)
                    return false;
                 gAudioSampleRate = v.get<double>();
                 return true;
              } },
            { "bufferFrames", [] { return json(gAudioBufferFrames); },
              [](const json& v)
              {
                 if (!v.is_number_integer() || v.get<int>() < 32 || v.get<int>() > 2048)
                    return false;
                 gAudioBufferFrames = v.get<int>();
                 return true;
              } },
         };
         return settings;
      }

      json ShowSetState()
      {
         json entries = json::array();
         for (int i = 0; i < ShowSetSize(); i++)
            entries.push_back({ { "name", ShowSetName(i) }, { "path", ShowSetEntryPath(i) }, { "ready", ShowSetReady(i) },
                                { "error", ShowSetError(i) } });
         return { { "entries", entries }, { "current", ShowSetCurrent() }, { "requested", ShowSetRequested() },
                  { "lock", gShowLock }, { "panic", ShowPanicLatched() }, { "fadeMs", ShowSetFadeMs() },
                  { "quantize", ShowSetQuantize() } };
      }
   }

   bool HandleAppRpc(const std::string& method, const nlohmann::json& params, nlohmann::json& outResult, std::string& outError)
   {
      if (method == "panel")
      {
         const std::string name = params.value("name", std::string());
         json all = json::object();
         for (const Panel& panel : Panels())
         {
            if (panel.name == name)
            {
               if (params.contains("open"))
                  *panel.open = params.value("open", true);
               if (params.contains("dock") && panel.open == &gArrangePanelOpen)
               {
                  const int code = DockCode(params.value("dock", std::string()));
                  if (code != 0 && code != 3)
                  {
                     outError = "timeline docks bottom or top";
                     return true;
                  }
                  gArrange.settings.dockSide = code == 3 ? 1 : 0;
                  gArrange.revision++;
                  gPatchDirty = true;
               }
               else if (params.contains("dock"))
               {
                  const int code = DockCode(params.value("dock", std::string()));
                  if (panel.dock == nullptr || code < 0 || (!panel.fourSides && code != 0 && code != 3))
                  {
                     outError = name + (panel.fourSides ? " docks bottom, right, left or top" : panel.dock ? " docks bottom or top" : " has no dock position");
                     return true;
                  }
                  *panel.dock = code;
               }
            }
            all[panel.name] = { { "open", *panel.open } };
            if (panel.open == &gArrangePanelOpen)
               all[panel.name]["dock"] = ArrangePanelDock();
            if (panel.dock != nullptr)
               all[panel.name]["dock"] = *panel.dock;
         }
         if (!name.empty() && !all.contains(name))
         {
            outError = "unknown panel '" + name + "'";
            return true;
         }
         outResult = name.empty() ? all : all[name];
         return true;
      }
      if (method == "edit")
      {
         // Each of these acts on the canvas selection at the top of the next frame, as the menu does.
         const std::string action = params.value("action", std::string());
         if (action == "copy")
            gRequestCopy = true;
         else if (action == "paste")
            gRequestPaste = true;
         else if (action == "duplicate")
            gRequestDuplicate = true;
         else if (action == "delete")
            gRequestDelete = true;
         else if (action == "bypass")
            gRequestBypass = true;
         else if (action == "group")
            gRequestGroup = true;
         else if (action == "ungroup")
            gRequestUngroup = true;
         else
         {
            outError = "unknown edit action '" + action + "'";
            return true;
         }
         outResult = json::object();
         return true;
      }
      if (method == "select")
      {
         ed::EditorContext* previous = ed::GetCurrentEditor();
         ed::SetCurrentEditor(gEditor);
         ed::ClearSelection();
         ed::SetCurrentEditor(previous);
         gPendingSelect.clear();
         if (params.value("all", false))
            gRequestSelectAll = true;
         for (const json& index : params.value("nodes", json::array()))
         {
            GraphNode* gn = index.is_number_integer() ? FindNodeByIndex(index.get<int>()) : nullptr;
            if (gn == nullptr)
            {
               outError = "unknown node index";
               return true;
            }
            gPendingSelect.push_back(gn->NodeId());
         }
         outResult = json::object();
         return true;
      }
      if (method == "spawn")
      {
         const std::string what = params.value("what", std::string());
         int index = -1;
         if (what == "synth")
            index = SpawnPlayableSynth();
         else if (what == "effect")
            index = SpawnEffectChain();
         else if (what == "3d")
            index = SpawnScene3D();
         else
         {
            outError = "unknown starter '" + what + "'; synth, effect or 3d";
            return true;
         }
         outResult = { { "index", index } };
         return true;
      }
      if (method == "recent")
      {
         outResult = { { "patches", Patch::Recents() } };
         return true;
      }
      if (method == "view")
      {
         const std::string action = params.value("action", std::string());
         if (action == "params")
         {
            const bool show = params.value("show", true);
            for (GraphNode& gn : gNodes)
               gn.showParams = show;
         }
         else if (action == "metronome")
         {
            if (params.contains("on"))
               gMetronomeOn = params.value("on", false);
            if (params.contains("accent"))
               gMetronomeAccent = params.value("accent", true);
            if (params.contains("volume"))
               gMetronomeVolume = params.value("volume", gMetronomeVolume);
            gMetronomeDirty = true;
            outResult = { { "on", gMetronomeOn }, { "accent", gMetronomeAccent }, { "volume", gMetronomeVolume } };
            return true;
         }
         else if (action == "clearcards")
            gViewportPanelNodes.clear();
         else
         {
            outError = "unknown view action '" + action + "'";
            return true;
         }
         outResult = json::object();
         return true;
      }
      if (method == "quit")
      {
         // The same door as File > Quit, so unsaved work still asks.
         RequestClose(glfwGetCurrentContext());
         outResult = json::object();
         return true;
      }
      if (method == "show")
      {
         const std::string action = params.value("action", std::string());
         const int index = params.value("index", -1);
         const std::string path = params.value("path", std::string());
         if (action == "next" || action == "prev")
         {
            if (ShowSetSize() == 0)
            {
               outError = "the show set is empty";
               return true;
            }
            ShowSetStep(action == "next" ? 1 : -1);
         }
         else if (action == "go" || action == "remove")
         {
            if (index < 0 || index >= ShowSetSize())
            {
               outError = "no show set entry " + std::to_string(index);
               return true;
            }
            if (action == "go")
               ShowSetGo(index);
            else
               ShowSetRemove(index);
         }
         else if (action == "lock")
         {
            if (gShowLock != params.value("on", !gShowLock))
               ShowLockToggle();
         }
         else if (action == "panic")
         {
            if (ShowPanicLatched() != params.value("on", !ShowPanicLatched()))
               ShowPanicToggle();
         }
         else if (action == "add")
         {
            if (path.empty())
            {
               if (!ShowSetAddCurrent())
               {
                  outError = "save the patch first; a show set holds files";
                  return true;
               }
            }
            else
               ShowSetAddFile(path);
         }
         else if (action == "clear")
            ShowSetClear();
         else if (action == "open" || action == "save")
         {
            if (!(action == "open" ? ShowSetOpenFile(path) : ShowSetSaveFile(path)))
            {
               outError = "could not " + action + " " + path;
               return true;
            }
         }
         else if (action == "check")
         {
            json items = json::array();
            for (const ShowCheckItem& item : ShowPreCheck())
               items.push_back({ { "error", item.error }, { "text", item.text } });
            outResult = { { "items", items } };
            return true;
         }
         else if (!action.empty() && action != "list")
         {
            outError = "unknown show action '" + action + "'";
            return true;
         }
         outResult = ShowSetState();
         return true;
      }
      if (method == "settings")
      {
         const std::string action = params.value("action", std::string());
         if (action == "apply")
         {
            ApplyAudioSettings();
            outResult = { { "error", gAudioStartError } };
            return true;
         }
         if (action == "updates")
         {
            UpdateCheck::Start();
            gShowUpdateCheckModal = true;
            outResult = json::object();
            return true;
         }
         if (action == "reset")
         {
            CategoryColors::ResetAllAppearanceBoth();
            ApplyTheme();
            outResult = json::object();
            return true;
         }
         if (action == "logs")
         {
            Platform::RevealLogs();
            outResult = json::object();
            return true;
         }
         if (action == "gpu")
         {
            if (params.contains("value"))
               Platform::SetGpuPreference(std::clamp(params.value("value", 0), 0, 2));
            outResult = { { "gpu", Platform::GpuPreference() } };
            return true;
         }
         if (action == "global")
         {
            std::vector<ExprGlobals::Global>& globals = ExprGlobals::All();
            const std::string name = params.value("name", std::string());
            const auto found = std::find_if(globals.begin(), globals.end(), [&](const ExprGlobals::Global& g) { return g.name == name; });
            if (!name.empty())
            {
               PushUndoCheckpoint();
               if (!params.contains("expr"))
               {
                  if (found != globals.end())
                     globals.erase(found);
               }
               else if (found != globals.end())
                  found->expr = params.value("expr", std::string());
               else
                  globals.push_back({ name, params.value("expr", std::string()), 0.0f, std::string() });
               gPatchDirty = true;
               SaveDefaultExprGlobals();
            }
            json all = json::object();
            for (const ExprGlobals::Global& g : globals)
               all[g.name] = g.expr;
            outResult = { { "globals", all } };
            return true;
         }
         if (action == "extension")
         {
            Extensions::InstallFileAsync(params.value("path", std::string()));
            outResult = json::object();
            return true;
         }
         if (action == "devices")
         {
            json devices = json::array();
            for (const Platform::AudioDeviceInfo& d : Platform::AudioListDevices())
               devices.push_back({ { "id", d.deviceId }, { "name", d.name }, { "input", d.isInput }, { "output", d.isOutput } });
            outResult = { { "devices", devices } };
            return true;
         }
         const std::string key = params.value("key", std::string());
         json all = json::object();
         for (const Setting& setting : Settings())
         {
            if (key == setting.key && params.contains("value") && !setting.set(params["value"]))
            {
               outError = "'" + params["value"].dump() + "' is not a value for " + key;
               return true;
            }
            all[setting.key] = setting.get();
         }
         if (!key.empty() && !all.contains(key))
         {
            outError = "unknown setting '" + key + "'; `infinite settings` lists them";
            return true;
         }
         outResult = key.empty() ? all : json{ { key, all[key] } };
         return true;
      }
      return false;
   }
}
