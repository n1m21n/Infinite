// The app's own surface over RPC: panels, the selection commands of the Edit menu, the show set
// and every setting (docs/plans/terminal/README.md D19). Same rules as Rpc.cpp: main thread,
// between frames. tools/terminal/parity.py holds the list of what has a verb.
#include <algorithm>
#include <cctype>
#include <functional>
#include "app/AppShared.h"
#include "app/panels/CurveEditorPanel.h"
#include "imgui.h"
#include "app/graph/LearningLog.h"

namespace app
{
   bool OpenSettingsAt(const std::string& place);  // SettingsWindow.cpp
   std::vector<std::string> SettingsPlaces();

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
            { "view3d", &gView3DOpen, &gView3DDock, false },
            { "terminal", &gTerminalOpen, &gTerminalDock, true },
            { "curveeditor", &gCurveEditorOpen, &gCurveEditorDock, false },
            { "showset", &gShowSetOpen, nullptr, false },
            { "history", &gHistoryOpen, nullptr, false },
            { "settings", &gSettingsOpen, nullptr, false },
            { "templates", &gTemplatesOpen, nullptr, false },
            { "help", &gHelpOpen, nullptr, false },
            { "shortcuts", &gShortcutsOpen, nullptr, false },
            { "library", &gNodePanelOpen, nullptr, false },
         };
         return panels;
      }

      // The names people and agents write for a panel ("Piano Roll", "nodes", "sample-studio") to its own name;
      // a name already in Panels() comes back unchanged.
      std::string PanelName(const std::string& raw)
      {
         std::string k;
         for (char c : raw)
            if (std::isalnum((unsigned char)c))
               k += (char)std::tolower((unsigned char)c);
         static const std::pair<const char*, const char*> kAlias[] = {
            { "nodes", "library" }, { "nodelibrary", "library" }, { "browser", "library" }, { "addnode", "library" },
            { "piano", "pianoroll" }, { "notes", "pianoroll" }, { "studio", "samplestudio" }, { "sample", "samplestudio" },
            { "samples", "samplestudio" }, { "matrix", "modmatrix" },
            { "modulation", "modmatrix" }, { "modulationmatrix", "modmatrix" }, { "performance", "perf" },
            { "arrange", "timeline" }, { "arrangement", "timeline" }, { "tracks", "timeline" }, { "3d", "view3d" },
            { "3dview", "view3d" }, { "chat", "terminal" }, { "agent", "terminal" }, { "shell", "terminal" }, { "curve", "curveeditor" }, { "curves", "curveeditor" }, { "mseg", "curveeditor" },
            { "preview", "viewport" }, { "output", "viewport" }, { "show", "showset" }, { "undo", "history" },
            { "keys", "shortcuts" }, { "keyboardshortcuts", "shortcuts" },
         };
         for (const auto& [from, to] : kAlias)
            if (k == from)
               return to;
         for (const Panel& p : Panels())
            if (k == p.name)
               return p.name;
         return raw;
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

   namespace
   {
      // A control by node and modulatable index: its saved key, label and current value.
      json ParamInfo(int nodeIndex, int paramIndex)
      {
         GraphNode* gn = FindNodeByIndex(nodeIndex);
         if (gn == nullptr || paramIndex < 0)
            return nullptr;
         json out = { { "node", gn->index }, { "type", gn->typeName }, { "title", NodeTitle(*gn) }, { "param", paramIndex } };
         const auto join = gParamJoin.find(gn->typeName);
         if (join != gParamJoin.end())
         {
            const auto key = join->second.keyOfParam.find(paramIndex);
            if (key != join->second.keyOfParam.end())
            {
               out["key"] = key->second;
               std::vector<std::pair<std::string, std::string>> raw;
               Patch::SaveParams(gn->node.get(), raw);
               for (const auto& kv : raw)
                  if (kv.first == key->second)
                     out["value"] = kv.second;
            }
         }
         if (const ParamRef* ref = Modulation::Instance().KnownParam(gn->index, paramIndex))
         {
            out["label"] = ref->name;
            out["min"] = ref->minValue;
            out["max"] = ref->maxValue;
            out["modulated"] = Modulation::Instance().IsModulated(gn->index, paramIndex);
         }
         return out;
      }
   }

   bool HandleAppRpc(const std::string& method, const nlohmann::json& params, nlohmann::json& outResult, std::string& outError)
   {
      // "attention": everything the user is pointing at, so an agent never has to ask. Selected nodes,
      // the control under the mouse, the one last pressed, the Tab-focused one and the open panels.
      if (method == "shortcuts")
      {
         outResult = { { "text", ShortcutsText() } };
         return true;
      }
      if (method == "attention")
      {
         json nodes = json::array();
         std::vector<ed::NodeId> ids;
         int got = 0;
         if (gEditor != nullptr)
         {
            ed::EditorContext* previous = ed::GetCurrentEditor();
            ed::SetCurrentEditor(gEditor);
            const int count = ed::GetSelectedObjectCount();
            ids.resize((size_t)std::max(1, count));
            got = ed::GetSelectedNodes(ids.data(), count);
            ed::SetCurrentEditor(previous);
         }
         for (int i = 0; i < got; i++)
            if (GraphNode* gn = FindNodeByIndex((int)ids[(size_t)i].Get() / GraphNode::kStride))
               nodes.push_back({ { "index", gn->index }, { "type", gn->typeName }, { "title", NodeTitle(*gn) } });
         json panels = json::array();
         for (const Panel& panel : Panels())
            if (*panel.open)
               panels.push_back(panel.name);
         outResult = { { "selected", nodes }, { "hovered", ParamInfo(gHoverParamNode, gHoverParamIndex) },
                       { "touched", ParamInfo(gTouchParamNode, gTouchParamIndex) }, { "keyboard", ParamInfo(gKbFocusNode, gKbFocusParam) },
                       { "panels", panels } };
         return true;
      }
      // "ui": synthetic ImGui input for scripted documentation shots (tools/manual/ui_shots_offscreen.py), so menus,
      // right-click menus and dropdowns can be opened in an off-screen run. x, y are window points.
      // actions: move {x,y}; click {x,y,button 0|1|2, double}; down / up {x,y,button} (a drag is down, move, up); key {key: escape|enter|tab|up|down|left|right|a..z|0..9,
      // shift, ctrl, alt, super}. ImGui trickles the queued events, so a down and up land on separate frames.
      if (method == "ui")
      {
         ImGuiIO& io = ImGui::GetIO();
         const std::string action = params.value("action", std::string());
         if (action == "move" || action == "click" || action == "down" || action == "up")
            io.AddMousePosEvent(params.value("x", 0.0f), params.value("y", 0.0f));
         if (action == "down" || action == "up")
            io.AddMouseButtonEvent(std::clamp(params.value("button", 0), 0, 2), action == "down");
         else if (action == "click")
         {
            const int button = std::clamp(params.value("button", 0), 0, 2);
            for (int n = params.value("double", false) ? 2 : 1; n > 0; --n)
            {
               io.AddMouseButtonEvent(button, true);
               io.AddMouseButtonEvent(button, false);
            }
         }
         else if (action == "key")
         {
            const std::string k = params.value("key", std::string());
            ImGuiKey key = ImGuiKey_None;
            static const std::pair<const char*, ImGuiKey> kNamed[] = {
               { "escape", ImGuiKey_Escape }, { "enter", ImGuiKey_Enter }, { "tab", ImGuiKey_Tab },
               { "up", ImGuiKey_UpArrow },     { "down", ImGuiKey_DownArrow }, { "left", ImGuiKey_LeftArrow },
               { "right", ImGuiKey_RightArrow }, { "space", ImGuiKey_Space }, { "slash", ImGuiKey_Slash },
            };
            for (const auto& [name, v] : kNamed)
               if (k == name)
                  key = v;
            if (k.size() == 1 && k[0] >= 'a' && k[0] <= 'z')
               key = (ImGuiKey)(ImGuiKey_A + (k[0] - 'a'));
            if (k.size() == 1 && k[0] >= '0' && k[0] <= '9')
               key = (ImGuiKey)(ImGuiKey_0 + (k[0] - '0'));
            if (key == ImGuiKey_None)
            {
               outError = "unknown key '" + k + "'";
               return true;
            }
            const std::pair<const char*, ImGuiKey> mods[] = {
               { "shift", ImGuiMod_Shift }, { "ctrl", ImGuiMod_Ctrl }, { "alt", ImGuiMod_Alt }, { "super", ImGuiMod_Super } };
            for (const auto& [name, m] : mods)
               if (params.value(name, false))
                  io.AddKeyEvent(m, true);
            io.AddKeyEvent(key, true);
            io.AddKeyEvent(key, false);
            for (const auto& [name, m] : mods)
               if (params.value(name, false))
                  io.AddKeyEvent(m, false);
         }
         else if (action != "move" && action != "down" && action != "up")
         {
            outError = "unknown ui action '" + action + "'";
            return true;
         }
         outResult = json::object();
         return true;
      }
      if (method == "panel")
      {
         std::string name = params.value("name", std::string());
         // "settings/midi": Settings, on the page and section that place names.
         if (const size_t slash = name.find('/'); slash != std::string::npos && name.compare(0, slash, "settings") == 0)
         {
            const std::string place = name.substr(slash + 1);
            if (!OpenSettingsAt(place))
            {
               std::string all;
               for (const std::string& p : SettingsPlaces())
                  all += (all.empty() ? "" : ", ") + p;
               outError = "unknown settings place '" + place + "'; places: " + all;
               return true;
            }
            outResult = { { "open", true }, { "place", place } };
            return true;
         }
         name = PanelName(name);
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
         else if (action == "view3d-add")
         {
            // The menu's Add to 3D View, by node index; a node that cannot be added is an error, not a silent skip.
            for (const json& index : params.value("nodes", json::array()))
            {
               GraphNode* gn = index.is_number_integer() ? FindNodeByIndex(index.get<int>()) : nullptr;
               if (gn == nullptr)
               {
                  outError = "unknown node index";
                  return true;
               }
               if (!View3DCanAdd(*gn))
               {
                  outError = "node " + std::to_string(index.get<int>()) + " cannot be added to the 3D View";
                  return true;
               }
               View3DAddNode(*gn);
            }
         }
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
         if (action == "midi")
         {
            // Connected MIDI controllers, and whether Infinite listens to each.
            json inputs = json::array();
            const bool running = Platform::MidiIsRunning();
            if (running)
               for (const auto& src : Platform::MidiListSources())
                  inputs.push_back({ { "name", src.name },
                                     { "on", std::find(gMidiOffSources.begin(), gMidiOffSources.end(), src.name) == gMidiOffSources.end() } });
            outResult = { { "running", running }, { "inputs", inputs } };
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
