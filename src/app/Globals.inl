// Part of src/main.cpp, included there in order (one translation unit).
// constants, globals and shared state

   // Every node renders its output at this size, square, above its params.
   const float kPreviewSize = 190.0f;
   // Render 3D's preview is a viewport you actually work in - orbiting and
   // framing a scene through a thumbnail is not workable.
   const float kViewportSize = 340.0f;
   // The dockable viewport panel (right-click a node -> "Open in viewport
   // panel"), separate from both the inline preview above and the inline
   // per-node mini 3D viewport toggle. Not const: the panel's canvas-facing
   // edge is a drag handle, so these are starting sizes, not fixed ones.
   // Clamped against the window every frame - see the layout section.
   float gViewportPanelWidth = 320.0f;
   float gViewportPanelHeight = 260.0f;
   const float kViewportPanelMinWidth = 160.0f;
   float gNodePanelWidth = 300.0f;
   const float kNodePanelMinWidth = 240.0f;
   // Taller than the width floor's equivalent margin: a top/bottom card gives
   // up two full rows (the dock combo/close row, then its own title row)
   // before the image even starts, so a floor sized like the width one left
   // next to nothing for the image itself - see the "so small" screenshot.
   const float kViewportPanelMinHeight = 190.0f;
   const float kParamWidth = 168.0f;
   // Audio node compact grid (docs/plans/audio/audio-node-ui-system.md §1):
   // half of kParamWidth minus a 4px gutter, so two of these plus
   // ImGui::SameLine's default spacing sit inside the same node body width
   // a full-width ModSlider already uses.
   const float kCompactParamWidth = 82.0f;
   // Horizontal audio-node layout (docs/plans/audio/audio-node-ui-system.md
   // v3): audio nodes lay their params out in a wide rack-style strip rather
   // than a narrow vertical stack. v2 declared kAudioNodeWidth but nothing
   // enforced it - the visualizer drew at 440, NodeSeparator drew at
   // kPreviewSize (190), and the knob row grew to whatever it added up to
   // (~556 for Oscillator), so one node body contained three different
   // widths and the node took the widest. v3 makes the width a *scope*
   // (BeginAudioBody/EndAudioBody) that every audio helper reads from, and
   // lays rows out to fit it rather than letting them set it.
   //
   // Two sanctioned widths, nothing else: a full node, and a narrow one for
   // a node with <=2 params (Gain, Splitter, Audio Out) so §7's "a one-param
   // node is visually smaller" is true instead of aspirational.
   const float kAudioNodeWidth = 440.0f;
   const float kAudioNarrowWidth = 200.0f;
   // Two-column layout width for large 3D nodes (Render 3D, Material)
   const float kWideNodeWidth = 476.0f;
   // A third width, for a node that is genuinely two parallel instruments
   // side by side (Wavetable's engine pair). Stacking them vertically at 440
   // put the node near 1600px tall - unreadable, and it forced an A/B tab whose
   // hidden half silently shared its parent's parameter indices, so a
   // modulator patched to engine A's attack drove engine B's the moment the
   // tab flipped. Two columns fixes the height and the aliasing at once,
   // because every param of both engines is now drawn every frame.
   const float kAudioWideWidth = 960.0f;
   const float kAudioHalfWidth = 214.0f;
   // Mixer needs a wider body than the standard 440: with 8 strips, cellW =
   // gAudioContentW / MixerNode::kSlots must leave room for both a 56px knob
   // and its modulation pin's left-margin gutter (ModKnob needs
   // (cell - knob) / 2 >= 20px). Solving (cell - 56) / 2 >= 20 => cell >= 96
   // => body >= 768. 800 is the smallest round number above that floor -
   // don't shrink this back toward 440, the pin-clamp bug it fixes re-engages
   // below 768.
   const float kAudioMixerWidth = 800.0f;
   // Knobs come in two sizes so a row has a visual hierarchy: large for the
   // one or two params that define what the node is doing, small for the
   // rest. A row of identical dials reads as a spreadsheet, which is what v2
   // shipped.
   // One knob size for every audio-node knob. The v3 grammar used two
   // (kKnobLarge for "what the node is", kKnobSmall for the rest) on the
   // theory that size carries hierarchy; in practice it just made every row
   // look unfinished, because the mixed sizes read as an inconsistency rather
   // than as a ranking. Hierarchy comes from grouping and ordering instead.
   const float kKnobStd = 56.0f;
   const float kKnobLarge = kKnobStd;
   const float kKnobSmall = kKnobStd;
   const float kKnobDiameter = kKnobSmall;
   // Macro family body cells (ported from upstream).
   const float kMacroCell = 112.0f;
   const float kMacroWideCell = 176.0f;
   const float kMacroRowH = 26.0f;
   const float kMacroFaderH = 96.0f;
   // Shared height for a time-domain effect graph (Dynamics' transfer
   // curve, Delay's tap graph, Reverb's decay envelope) - one number so the
   // three read as the same instrument family instead of each picking its
   // own. AudioFilter's frequency response keeps its own taller 190px (a
   // curve read for precision at a glance needs more vertical resolution
   // than these three, which are read for overall shape).
   const float kAudioTimeVizH = 150.0f;
   // Inset padding inside a section panel (AudioSection).
   const float kAudioSectionPad = 8.0f;
   const float kPinRadius = 7.0f;
   const float kPinHit = 20.0f; // generous click target - small dots were unhittable

   inline bool IsThemeLight()
   {
      const CategoryColors::UiTheme& t = CategoryColors::CurrentUiTheme();
      return (0.2126f * t.windowBg.r + 0.7152f * t.windowBg.g + 0.0722f * t.windowBg.b > 0.5f);
   }

   inline ImU32 ScopeBgCol()
   {
      return IsThemeLight() ? IM_COL32(236, 240, 248, 255) : IM_COL32(11, 12, 16, 255);
   }

   inline ImU32 ScopeBorderCol()
   {
      return IsThemeLight() ? IM_COL32(185, 192, 208, 255) : IM_COL32(64, 68, 84, 255);
   }

   inline ImU32 ScopeGridCol()
   {
      return IsThemeLight() ? IM_COL32(208, 215, 228, 255) : IM_COL32(255, 255, 255, 12);
   }

   inline ImU32 ScopeMidLineCol()
   {
      return IsThemeLight() ? IM_COL32(178, 186, 204, 255) : IM_COL32(255, 255, 255, 26);
   }

   inline ImU32 ScopeTextCol()
   {
      return IsThemeLight() ? IM_COL32(70, 78, 96, 255) : IM_COL32(120, 128, 150, 255);
   }

   // Backdrop for node previews and viewports: a solid dark (or light in light
   // theme) frame, letterboxing non-square aspect ratios cleanly like TouchDesigner.
   void DrawCheckerboardBackdrop(ImDrawList* dl, ImVec2 origin, ImVec2 br, float rounding = 4.0f)
   {
      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, isLight ? IM_COL32(238, 240, 246, 255) : IM_COL32(18, 18, 24, 255), rounding);
   }

   void DrawCheckerboardBackdrop(ImDrawList* dl, ImVec2 origin, float size, float rounding = 4.0f)
   {
      DrawCheckerboardBackdrop(dl, origin, ImVec2(origin.x + size, origin.y + size), rounding);
   }

   // Blender-style numpad view hotkeys for any hovered orbit-camera viewport
   // item (mini-viewport, viewport-panel card, Render3D preview): 1/3/7 snap
   // to front/right/top, Ctrl+1/3/7 to back/left/bottom, 0 to a default
   // three-quarter view. Both the number row and the numeric keypad fire the
   // same view, matching Blender's own leniency. Must be called right after
   // the InvisibleButton/item whose hover state should gate it - like the
   // drag/wheel handling beside every call site, it reads
   // ImGui::IsItemHovered() for the *last* item, so no hover state needs to
   // be threaded through. Also refuses to fire while a text field has focus,
   // so typing "1" into a param field doesn't reframe a hovered-but-unrelated
   // viewport behind it. Returns true if it changed azimuth/elevation. An
   // optional onWillChange is invoked right before the write happens - the
   // two Render3D call sites use it to PushUndoCheckpoint() while the old
   // values are still live, matching that camera's "orbit"/"elevation"
   // sliders; the two gNodeCameras (mini-viewport/panel) sites pass nothing,
   // matching drag-orbit there, which also pushes no undo checkpoint.
   bool ApplyViewHotkeys(float& azimuth, float& elevation, const std::function<void()>& onWillChange = nullptr)
   {
      if (!ImGui::IsItemHovered() || ImGui::GetIO().WantTextInput)
         return false;

      const bool ctrl = ImGui::GetIO().KeyCtrl;
      // Pole clamp matches the drag-orbit elevation clamp used everywhere
      // else in this file (DrawPreview, the Render3D "elevation" slider):
      // exactly 90 makes the up vector parallel to the view direction and
      // the image rolls.
      const float kPole = 85.9437f;

      auto pressed = [](ImGuiKey rowKey, ImGuiKey padKey) {
         return ImGui::IsKeyPressed(rowKey, false) || ImGui::IsKeyPressed(padKey, false);
      };

      float newAzimuth, newElevation;
      if (pressed(ImGuiKey_1, ImGuiKey_Keypad1))
      {
         newAzimuth = ctrl ? 180.0f : 0.0f;      // front / back
         newElevation = 0.0f;
      }
      else if (pressed(ImGuiKey_3, ImGuiKey_Keypad3))
      {
         newAzimuth = ctrl ? -90.0f : 90.0f;     // right / left
         newElevation = 0.0f;
      }
      else if (pressed(ImGuiKey_7, ImGuiKey_Keypad7))
      {
         newAzimuth = 0.0f;
         newElevation = ctrl ? -kPole : kPole;   // top / bottom
      }
      else if (pressed(ImGuiKey_0, ImGuiKey_Keypad0))
      {
         newAzimuth = 34.3775f;                  // default three-quarter view
         newElevation = 22.9183f;
      }
      else
      {
         return false;
      }

      // Snapshot before the write, not after - an undo checkpoint taken
      // once the new values are already live would have nothing to restore.
      if (onWillChange)
         onWillChange();
      azimuth = newAzimuth;
      elevation = newElevation;
      return true;
   }

   // Registered node names are the patch-file keys and must not change, so
   // casing is a display concern only. Registered names stay untouched so old
   // patches keep loading; the editor presents module/category names in the
   // uppercase visual language used by the original interface.
   std::string DisplayName(const std::string& name)
   {
      std::string out;
      if (name == "Spout In")
         out = "SPOUT IN";
      if (name == "Spout Out")
         out = "SPOUT OUT";
      // "Dynamics" reads as "compressor" everywhere a user sees it - search,
      // spawn menu, node header, help popup - but the registered type key
      // (gn.typeName, NodeFactory's lookup, every saved patch's node-type
      // string) stays "Dynamics" so existing patches keep loading.
      if (name == "Dynamics")
         out = "COMPRESSOR";
      // Registered type key stays "Sampler" (patches/undo/copy-paste key off
      // it), but it grew a record input, an interactive draggable loop
      // range, and reverse/ping-pong - "sample player" reads as what it now
      // is rather than as a bare one-shot playback node.
      if (name == "Sampler")
         out = "SAMPLE PLAYER";
      if (name == "PaulStretch")
         out = "PAUL STRETCH";
      if (name == "Granular")
         out = "GRANULAR";
      if (name == "Fit")
         out = "FIT / RESIZE";
      // Turbo category captions (the category keys stay one token).
      if (name == "AudioIO")
         out = "AUDIO IN / OUT";
      if (name == "AudioVisual")
         out = "AUDIO TO VISUAL";
      if (name == "AudioEffects")
         out = "AUDIO EFFECTS & PLUGINS";
      if (name == "AudioUtility")
         out = "AUDIO MIX & ROUTING";
      if (name == "Synths")
         out = "SYNTHS & SAMPLERS";
      if (name == "Notes")
         out = "NOTE TOOLS"; // Turbo 0.55: the sources moved to NoteSource
      if (name == "NoteSource")
         out = "NOTE SOURCES & SEQUENCERS";
      if (name == "Shapes")
         out = "SHAPES 2D";
      if (name == "3DSource")
         out = "3D SOURCES";
      if (name == "3D")
         out = "3D GEOMETRY";
      if (name == "3DScene")
         out = "3D SCENE & RENDER";
      if (name == "Keyboard")
         out = "VIRTUAL KEYBOARD"; // type key stays "Keyboard"
      if (name == "CVTools")
         out = "CV TOOLS";
      if (name == "Control")
         out = "MIDI, OSC & TRANSPORT";
      if (out.empty())
         out = name;
      std::transform(out.begin(), out.end(), out.begin(),
                     [](unsigned char c) { return (char)std::toupper(c); });
      return out;
   }

   // ImGui IDs must not be derived from the visible caption alone. Output and
   // Feedback are both category names and node names, which previously gave
   // their CollapsingHeader and Selectable the same ID. The click toggled the
   // category instead of spawning the node in both catalogues.
   std::string NodeBrowserLabel(const std::string& visibleName,
                                const std::string& role,
                                const std::string& category,
                                const std::string& registeredName = std::string())
   {
      return DisplayName(visibleName) + "##" + role + ":" + category + ":" + registeredName;
   }

   // Turbo 0.59: the same face again WITHOUT the uppercase remap below, for
   // text people type and the app reads back exactly (param values and
   // expressions, which are case-sensitive: `t` works, `T` does not - and in
   // the all-caps face the two look the same). nullptr when only ImGui's
   // default font loaded; MixedCaseFontScope then does nothing.
   ImFont* gUiFontMixedCase = nullptr;
   struct MixedCaseFontScope
   {
      bool pushed = false;
      MixedCaseFontScope()
      {
         if (gUiFontMixedCase != nullptr)
         {
            ImGui::PushFont(gUiFontMixedCase);
            pushed = true;
         }
      }
      ~MixedCaseFontScope()
      {
         if (pushed)
            ImGui::PopFont();
      }
      MixedCaseFontScope(const MixedCaseFontScope&) = delete;
      MixedCaseFontScope& operator=(const MixedCaseFontScope&) = delete;
   };

   // Present the entire ImGui layer in uppercase without changing stored
   // paths, patch text, plugin names or user input. Lowercase codepoints keep
   // their identity in the string, but render with the uppercase IBM Plex
   // glyph and its metrics.
   void ForceAsciiUppercaseGlyphs(ImFont* font)
   {
      if (font == nullptr)
         return;
      for (ImWchar lowerCode = (ImWchar)'a'; lowerCode <= (ImWchar)'z'; ++lowerCode)
      {
         const ImWchar upperCode = (ImWchar)(lowerCode - 'a' + 'A');
         ImFontGlyph* lower = const_cast<ImFontGlyph*>(font->FindGlyphNoFallback(lowerCode));
         const ImFontGlyph* upper = font->FindGlyphNoFallback(upperCode);
         if (lower == nullptr || upper == nullptr)
            continue;
         const unsigned int originalCodepoint = lower->Codepoint;
         *lower = *upper;
         lower->Codepoint = originalCodepoint;
         if (lowerCode < font->IndexAdvanceX.Size)
            font->IndexAdvanceX[lowerCode] = lower->AdvanceX;
      }
   }

   // Turbo 0.46 (upstream R527): interface scale. 0 = follow Windows' display
   // scale (per monitor); otherwise a fixed factor (Settings > Interface scale).
   float gUiScaleSetting = 0.0f;
   // Turbo 0.51 (upstream 42e4902): interface font choice. `file` is under
   // assets/fonts; the first entry (IBM Plex) is the default and the fallback.
   struct InterfaceFont
   {
      const char* id;
      const char* label;
      const char* file;
   };
   const InterfaceFont kInterfaceFonts[] = {
      { "", "IBM Plex Sans (default)", "IBMPlexSans-Regular.ttf" },
      { "atkinson", "Atkinson Hyperlegible", "AtkinsonHyperlegible-Regular.ttf" },
      { "inter", "Inter (Infinite Dark)", "Inter-Regular.ttf" }, // Turbo 0.57
   };
   std::string gUiFontId; // saved with the machine settings (SceneSettings::uiFont)
   bool gUiRescaleRequested = false;
   float gUiPointScale = 1.0f;

   // Turbo 0.46 (upstream's update check, for this fork): one GitHub request
   // at startup on a worker thread; a newer release shows an UPDATE badge in
   // the top bar. Off in Settings, or with INFINITE_NO_UPDATE_CHECK set.
   bool gUpdateCheckEnabled = true;
   bool gStartWithExample = true; // Turbo 0.46: open superSynthMCP at startup (kept for old settings files)
   // Turbo 0.59: Setup > Startup. 0 empty patch, 1 the last patch, 2 the
   // example, 3 gStartupPatch.
   int gStartupMode = 0;
   std::string gStartupPatch;
   namespace TurboUpdate
   {
      constexpr const char* kReleasesApi = "https://api.github.com/repos/ricardopalmieri/Infinite/releases/latest";
      constexpr const char* kReleasesPage = "https://github.com/ricardopalmieri/Infinite/releases";
      std::mutex gMutex;
      std::atomic<int> gStatus { 0 }; // 0 idle, 1 checking, 2 up to date, 3 newer, 4 failed
      std::string gLatest, gUrl, gError;
      std::thread gWorker;

      std::vector<int> VersionNumbers(const std::string& text)
      {
         std::vector<int> out;
         int cur = -1;
         for (char c : text)
         {
            if (c >= '0' && c <= '9')
               cur = (cur < 0 ? 0 : cur * 10) + (c - '0');
            else if (cur >= 0)
            {
               out.push_back(cur);
               cur = -1;
               if (c != '.')
                  break; // "0.46.0-turbo": stop at the suffix
            }
         }
         if (cur >= 0)
            out.push_back(cur);
         return out;
      }

      bool IsNewer(const std::string& latest, const std::string& current)
      {
         const std::vector<int> a = VersionNumbers(latest), b = VersionNumbers(current);
         for (size_t i = 0; i < std::max(a.size(), b.size()); i++)
         {
            const int x = i < a.size() ? a[i] : 0, y = i < b.size() ? b[i] : 0;
            if (x != y)
               return x > y;
         }
         return false;
      }

      void Start()
      {
         if (gStatus.load() == 1 || std::getenv("INFINITE_NO_UPDATE_CHECK") != nullptr)
            return;
         if (gWorker.joinable())
            gWorker.join();
         gStatus = 1;
         gWorker = std::thread([]()
         {
            std::string body, error;
            int result = 4;
            std::string latest, url;
            if (Platform::HttpGet(kReleasesApi, body, error, 8000))
            {
               try
               {
                  const nlohmann::json j = nlohmann::json::parse(body);
                  latest = j.value("tag_name", std::string());
                  url = j.value("html_url", std::string(kReleasesPage));
                  result = latest.empty() ? 4 : (IsNewer(latest, INFINITE_TURBO_VERSION) ? 3 : 2);
                  if (latest.empty())
                     error = "no release tag";
               }
               catch (const std::exception& e)
               {
                  error = e.what();
               }
            }
            else if (error == "HTTP 404")
               error = "no releases published yet";
            std::lock_guard<std::mutex> lock(gMutex);
            gLatest = latest;
            gUrl = url.empty() ? std::string(kReleasesPage) : url;
            gError = error;
            gStatus = result;
         });
      }

      void Shutdown()
      {
         // Never hold up quitting for a slow network: the process is ending.
         if (gWorker.joinable())
            gWorker.detach();
      }
   }

   void ApplyUiScale(GLFWwindow* window)
   {
      float xscale = 1.0f, yscale = 1.0f;
      glfwGetWindowContentScale(window, &xscale, &yscale);
      if (!(xscale >= 0.25f))
         xscale = 1.0f;
      const float scale = gUiScaleSetting >= 0.5f ? gUiScaleSetting : xscale;
      gUiPointScale = scale;
      ImGui_ImplGlfw_SetPointScale(scale);

      // ImGui 1.90 truncates font sizes when it builds the atlas: bake at the
      // next whole pixel and let FontGlobalScale absorb the difference.
      const float baseSize = 15.0f;
      const float bakedPx = std::ceil(baseSize * scale - 1.0e-3f);
      std::vector<std::string> candidates;
      const std::filesystem::path exeDir =
         std::filesystem::u8path(Platform::ExecutablePath()).parent_path();
      // Turbo 0.57: the Infinite Dark theme brings upstream's Inter unless the
      // user picked a font of their own.
      const std::string fontId = (gUiFontId.empty() && CategoryColors::IsDesignTheme()) ? std::string("inter") : gUiFontId;
      for (const InterfaceFont& f : kInterfaceFonts)
         if (!fontId.empty() && fontId == f.id)
         {
            candidates.push_back((exeDir / "assets" / "fonts" / f.file).u8string());
            candidates.push_back(std::string("assets/fonts/") + f.file);
         }
      candidates.push_back((exeDir / "assets" / "fonts" / "IBMPlexSans-Regular.ttf").u8string());
      candidates.push_back("assets/fonts/IBMPlexSans-Regular.ttf");
      candidates.push_back("C:/Windows/Fonts/SegUIVar.ttf");
      candidates.push_back("C:/Windows/Fonts/segoeui.ttf");
      candidates.push_back("C:/Windows/Fonts/arial.ttf");
      ImGuiIO& io = ImGui::GetIO();
      io.Fonts->Clear();
      // Turbo 0.51 (upstream bfb1833): ImGui bakes only Basic Latin + Latin-1 by
      // default, so names with l-stroke, double acute, Cyrillic, Greek or
      // Vietnamese letters drew as '?'. Glyphs a face lacks are simply skipped.
      static const ImWchar kUiGlyphRanges[] = {
         0x0020, 0x00FF, 0x0100, 0x024F, 0x0370, 0x03FF, 0x0400, 0x04FF,
         0x1E00, 0x1EFF, 0x2000, 0x206F, 0x20A0, 0x20CF, 0
      };
      ImFont* uiFont = nullptr;
      gUiFontMixedCase = nullptr; // io.Fonts->Clear() above freed the old one
      for (const std::string& path : candidates)
      {
         uiFont = io.Fonts->AddFontFromFileTTF(path.c_str(), bakedPx, nullptr, kUiGlyphRanges);
         if (uiFont != nullptr)
         {
            // Turbo 0.59: a second copy that keeps lowercase (see gUiFontMixedCase).
            gUiFontMixedCase = io.Fonts->AddFontFromFileTTF(path.c_str(), bakedPx, nullptr, kUiGlyphRanges);
            io.FontGlobalScale = baseSize / bakedPx;
            RuntimeLog::Write("UI font loaded: %s (scale %.2f)", path.c_str(), scale);
            break;
         }
      }
      if (uiFont == nullptr)
      {
         io.Fonts->AddFontDefault();
         io.FontGlobalScale = 1.0f;
      }
      else
      {
         // Build now so lowercase glyph entries can be mapped to the matching
         // uppercase IBM Plex shapes before the backend uploads the atlas.
         io.Fonts->Build();
         // Turbo 0.57: the Infinite Dark theme keeps upstream's mixed case.
         if (!CategoryColors::IsDesignTheme())
            ForceAsciiUppercaseGlyphs(uiFont);
      }
   }

   // Between frames: rebuilds the font atlas for a new scale (window moved to
   // another monitor, Windows scale or the setting changed).
   void PumpUiRescale(GLFWwindow* window)
   {
      if (!gUiRescaleRequested)
         return;
      if (glfwGetWindowAttrib(window, GLFW_ICONIFIED))
         return;
      gUiRescaleRequested = false;
      ApplyUiScale(window);
      ImGui_ImplOpenGL3_DestroyFontsTexture();
      ImGui_ImplOpenGL3_CreateFontsTexture();
   }

   // True for a NodeFactory-registered name a user should be able to spawn
   // going forward. Currently only excludes GeometryOpNode's three deprecated
   // `*Selected` ops (see the Op enum comment in GeometryOpNodes.h) - they
   // stay registered so old patches and internal test fixtures can still name
   // them, but a fresh graph should reach for the general op + selectionOnly
   // instead. Every spawn-menu / search-list enumeration site should filter
   // through this rather than NodeFactory's raw listing.
   bool IsUserSpawnable(const std::string& name)
   {
      return name != "Delete Selected" && name != "Transform Selected" &&
             name != "Extrude Selected";
   }

   // GeometryOpNode shares one node type across every mesh operator, and its
   // dropdown lets the operator change after spawn - so unlike most other
   // nodes, its title can't be read from the type name frozen at spawn time.
   // GeometryNode and ShapeNode have the identical problem: each primitive is
   // registered as its own searchable spawn entry (typeName == "Torus Knot",
   // "Tube", ...) so the type name frozen at spawn matches the shape you
   // picked, but the node keeps a live "shape" dropdown that can change it
   // afterwards without ever updating typeName - so the title has to track
   // the live shape field the same way, or picking a new shape leaves the
   // header reading whatever primitive was spawned first.
   std::string NodeTitle(const GraphNode& gn)
   {
      if (auto* opNode = dynamic_cast<GeometryOpNode*>(gn.node.get()))
      {
         const auto& names = GeometryOpNode::OpNames();
         if (opNode->op >= 0 && opNode->op < (int)names.size())
            return DisplayName(names[opNode->op]);
      }
      if (auto* geoNode = dynamic_cast<GeometryNode*>(gn.node.get()))
      {
         const auto& names = GeometryNode::ShapeNames();
         if (geoNode->shape >= 0 && geoNode->shape < (int)names.size())
            return DisplayName(names[geoNode->shape]);
      }
      if (auto* shapeNode = dynamic_cast<ShapeNode*>(gn.node.get()))
      {
         const auto& names = ShapeNode::ShapeNames();
         if (shapeNode->shapeType >= 0 && shapeNode->shapeType < (int)names.size())
            return DisplayName(names[shapeNode->shapeType]);
      }
      // Turbo: a Send/Receive reads by its channel, the way [s name] does in Pd.
      if (auto* wireless = dynamic_cast<WirelessNode*>(gn.node.get()))
         return DisplayName(gn.typeName) + " " + wireless->channel;
      return DisplayName(gn.typeName);
   }

   // Undo/redo. Snapshots are whole-graph Patch::Data - the same format a
   // patch file uses - captured just before a mutation rather than diffed
   // after one, so restoring one is exactly LoadPatchFrom's job. Defined near
   // SavePatchTo/LoadPatchFrom, far below; forward-declared here because
   // ModSlider and friends (the widgets that call it) are among the first
   // functions in the file.
   void PushUndoCheckpoint();

   std::vector<GraphNode> gNodes;

   // Disambiguates nodes that share a title (three LFOs, two Super Mixers):
   // rank by GraphNode::index (spawn order, never reused) so the number a
   // user memorised does not shuffle when an unrelated node is deleted.
   // Display only - patches are unaffected.
   int GetNodeInstanceIndex(const GraphNode& target, int* outTotal = nullptr)
   {
      // Rebuilt once per frame (NodeTitle does a few dynamic_casts, and the
      // canvas asks for every node), keyed by GraphNode::index.
      static int sFrame = -1;
      static size_t sCount = 0;
      static std::unordered_map<int, std::pair<int, int>> sRank; // index -> (rank, total)
      const int frame = ImGui::GetFrameCount();
      if (frame != sFrame || sCount != gNodes.size() || sRank.find(target.index) == sRank.end())
      {
         sFrame = frame;
         sCount = gNodes.size();
         sRank.clear();
         std::unordered_map<std::string, std::vector<int>> byTitle;
         for (const GraphNode& g : gNodes)
            byTitle[NodeTitle(g)].push_back(g.index);
         for (auto& kv : byTitle)
         {
            std::sort(kv.second.begin(), kv.second.end());
            const int total = (int)kv.second.size();
            for (int i = 0; i < total; i++)
               sRank[kv.second[i]] = { i + 1, total };
         }
      }
      auto it = sRank.find(target.index);
      if (it == sRank.end())
      {
         if (outTotal != nullptr)
            *outTotal = 1;
         return 1;
      }
      if (outTotal != nullptr)
         *outTotal = it->second.second;
      return it->second.first;
   }

   // NodeTitle plus " #N" when another node shares the title; unchanged for a unique node.
   std::string NodeTitleWithInstance(const GraphNode& gn)
   {
      int total = 0;
      const int inst = GetNodeInstanceIndex(gn, &total);
      return total <= 1 ? NodeTitle(gn) : NodeTitle(gn) + " #" + std::to_string(inst);
   }

   // Which node indices each Group considers its own, once-in-always-in. Kept
   // outside GroupNode because it is keyed by GraphNode::index, not anything
   // the node itself knows about. Membership only grows (see DrawGroupNode) -
   // a stale index (its node deleted, or undo/redo having rewound past it) is
   // simply skipped wherever this is read, never treated as an error.
   std::map<GroupNode*, std::set<int>> gGroupMembers;
   // Starts at 1, not 0: node index 0 would give NodeId 0, and the node editor
   // reserves 0 as its Invalid id. A node with that id still draws, but every
   // `if (ed::NodeId n = ed::GetHoveredNode())` silently reads false for it, so
   // the first node spawned in a patch could not be hovered, selected or
   // dragged the way every other node could.
   int gNextIndex = 1;
   // Turbo 0.43 (from upstream): stable node identity. `index` is reused by
   // RemoveNodeByIndex; arrangement clips reference `uid`, so a clip survives
   // undo and reload. Never reused; saved in the patch.
   uint64_t gNextNodeUid = 1;

   // ---- Arrangement Timeline (Turbo 0.43, port of upstream's) -----------
   // The one arrangement model; saved with the patch, snapshotted by undo.
   // Everything else (panel, audio terminals, compositor) reads it.
   Arrange::Model gArrange;
   bool gArrangePanelOpen = false;
   float gArrangePanelHeight = 300.0f;
   // Timeline mode: the timeline owns the audio output (canvas Audio Outs
   // are muted, live ones excepted). Session state, never saved - the app
   // always opens on the canvas (upstream rule).
   bool gArrangeTimelineMode = false;
   // True while the timeline panel has keyboard focus: the canvas shortcuts
   // (Delete, Ctrl+D, Ctrl+C/V, ...) stand down and the timeline takes them.
   bool gArrangeKeysOwned = false;
   // Turbo 0.51: the Chord Progression slot strip owns Ctrl+D / Ctrl+C /
   // Ctrl+V / Delete / Alt+arrows while a slot is hovered or was the last
   // thing clicked. The body sets it each frame; the shortcut code reads it
   // (as `typing`) and clears it.
   bool gChordSlotsKeysOwned = false;
   // Turbo 0.45 (upstream R470): during a timeline render, gesture loops play
   // on the render's own video time (seconds since the take started, one
   // step per rendered frame) instead of the wall-clock UI clock, so a take
   // plays recorded knob moves at the right speed. < 0 = not rendering.
   // Turbo 0.46: Performance Mode state (see core/PerfPanel.inl).
   bool  gPerfPanelOpen = false;
   int   gPerfPanelDock = 0;              // 0 = bottom, 1 = right, 2 = left, 3 = top
   float gPerfPanelWidth = 460.0f;
   float gPerfPanelHeight = 280.0f;
   const float kPerfPanelMinWidth = 240.0f;
   const float kPerfPanelMinHeight = 160.0f;
   bool  gPerfEditMode = true;            // Edit (arrange, assign) or Perform
   int   gPerfActivePage = 0;
   int   gPerfRenamingPage = -1;
   char  gPerfRenamePageBuffer[64] = "";
   int   gPerfAssigningElemIdx = -1;      // a control waiting for a click on a param
   int   gPerfAssigningAxis = 0;
   int   gPerfMidiLearnIdx = -1;
   int   gPerfMidiLearnAxis = 0;
   struct PerfMidiRuntimeState
   {
      float lastVal = -1.0f;
      float lastValY = -1.0f;
      unsigned int lastHitSeq = 0;
   };
   std::map<size_t, PerfMidiRuntimeState> gPerfMidiRuntimeStates;
   std::map<size_t, float> gPerfBangFlash;
   int   gPerfRenamingElementIdx = -1;
   char  gPerfRenameElementBuffer[64] = "";
   Patch::PerfLayoutRecord gPerfLayout;
   std::vector<Patch::PerfRecord> gPerfElements;
   std::set<size_t> gPerfSelection;
   std::vector<Patch::PerfRecord> gPerfClipboard;
   bool  gPerfMatrixFocused = false;
   bool  gPerfMatrixClaimedKeys = false;
   ImVec2 gPerfPanelRectMin(0.0f, 0.0f);
   ImVec2 gPerfPanelRectMax(0.0f, 0.0f);
   std::map<std::pair<int, int>, float> gPerfPendingWrites;
   int   gPerfDragIdx = -1;
   int   gPerfDragOriginCellX = 0;
   int   gPerfDragOriginCellY = 0;
   ImVec2 gPerfDragMouseStart(0.0f, 0.0f);
   double gGestureRenderClock = -1.0;
   // Turbo 0.45: nodes waiting for the auto layout (no position in the file,
   // or an RPC asked) - laid out once they have drawn and have a size.
   std::vector<int> gAutoLayoutPending;
   int gAutoLayoutWait = 0;
   bool gAutoLayoutFit = false;
   // Turbo 0.45: live validation hints rerun 300 ms after the last edit.
   unsigned gLiveIssueSerial = 1;
   std::string gLiveIssueTip; // hovered node's hints, drawn outside the editor
   // Turbo 0.45: the open patch file is watched (an editor, git or an AI
   // writing it) - see PollPatchFileWatch.
   std::string gWatchedPath;
   long long gWatchedStamp = 0;
   long long gWatchPendingStamp = 0;
   double gWatchNextPoll = 0.0;
   int gWatchReadFailures = 0;
   bool gReloadBanner = false;
   std::string gHandoverPath; // Turbo 0.46: a .inf handed over while this patch has unsaved edits
   long long PatchFileStamp(const std::string& path)
   {
      std::error_code ec;
      const std::filesystem::path p = std::filesystem::u8path(path);
      const auto t = std::filesystem::last_write_time(p, ec);
      if (ec)
         return 0;
      const auto size = std::filesystem::file_size(p, ec);
      if (ec)
         return 0;
      return (long long)t.time_since_epoch().count() ^ ((long long)size << 40);
   }
   double gLiveIssueEditTime = 0.0;
   void NoteGraphEditedForLiveIssues()
   {
      gLiveIssueSerial++;
      gLiveIssueEditTime = glfwGetTime();
   }
   double GesturePlaybackClock()
   {
      return gGestureRenderClock >= 0.0 ? gGestureRenderClock : GestureRecorder::Instance().ClockNow();
   }
   // Re-bases every loop when playback switches clocks, so startTime and the
   // clock read are on the same axis. Called once per frame before playback.
   void GestureSyncClockAxis()
   {
      static bool sWasRendering = false;
      const bool rendering = gGestureRenderClock >= 0.0;
      if (rendering == sWasRendering)
         return;
      sWasRendering = rendering;
      GestureRecorder::Instance().RestartLoops(rendering ? 0.0 : GestureRecorder::Instance().ClockNow());
   }
   ImVec2 gArrangePanelMin(0, 0), gArrangePanelMax(0, 0);
   GraphNode* FindNodeByUid(uint64_t uid);
   void ArrangeModelToPatchData(const Arrange::Model& m, Patch::Data& data);
   void ArrangeLoadFromPatchData(const Patch::Data& data, bool fileLoad);
   void ArrangeSeedAudio(std::set<INode*>& visited, std::vector<AudioTopologyEntry>& order,
                         std::unordered_map<AudioNode*, int>& bufferIndexOf);
   void ArrangeBuildAudioTerminals(const std::unordered_map<AudioNode*, int>& bufferIndexOf, AudioTopology& topology);
   void ArrangeOnNodeRemoved(uint64_t uid);
   void ArrangeResetSession();
   bool ArrangeHandleFileDrop(const std::vector<std::string>& paths, ImVec2 pos);

   ed::EditorContext* gEditor = nullptr;
   GraphNode* gSelfTestFeeder = nullptr;
   bool gPaletteTestOk = false;    // dev test only
   bool gPaletteTestPending = false;
   ImVec2 gSpawnPos(0.0f, 0.0f);

   // Manual drag-and-drop for the Samples search panel: the panel is a
   // plain ImGui child window, but the node canvas it drops onto is managed
   // internally by imgui_node_editor (ed::), which has no public "accept a
   // dropped item anywhere on empty canvas" API the way a normal ImGui
   // window does. Tracking the drag by hand and resolving the drop with
   // ed::GetHoveredNode()/ScreenToCanvas - the same primitives the existing
   // "double-click empty canvas" search popup already uses - avoids fighting
   // ed::'s own item/hover system with an invisible full-canvas button.
   bool gSampleDragActive = false;
   // Which panel mode started the drag, and therefore what the canvas-release
   // handler should resolve it against. Was a plain bool (Sampler vs Media)
   // until the Plugins mode arrived; a third value in the same variable keeps
   // the release handler a single ladder rather than a bool plus a parallel
   // "is it actually a plugin" flag that could disagree with it.
   enum class LibraryDragKind
   {
      Sample,
      Media,
      Plugin,
      SampleFolder // Turbo 0.49: a library folder row, fills lanes / pads
   };
   LibraryDragKind gSampleDragKind = LibraryDragKind::Sample;
   std::string gSampleDragPath; // Sample/Media only: the file being dragged
   std::string gSampleDragName;
   // Turbo 0.49: SampleFolder drags only, the folder's audio files by name.
   std::vector<std::string> gSampleDragFolderFiles;

   // Path of the sample currently auditioning from the Samples panel, or
   // empty. Only one preview plays at a time - clicking a row's play button
   // stops whatever else is auditioning and starts this one - see
   // local-prompts/05-sample-preview-in-search-panel.md. The preview player
   // itself lives on AudioEngine, outside the node topology entirely; this
   // is just the UI's record of which row's button should read "latched".
   std::string gPreviewingSamplePath;
   // Set when the last preview attempt's decode failed, so that row's
   // tooltip can report the error instead of just the file path. Cleared on
   // the next successful play attempt (of any row).
   std::string gPreviewErrorPath;
   std::string gPreviewErrorMessage;
   Platform::PluginDesc gPluginDragDesc; // Plugin drags only - identity, not a path
   SampleScanner gSampleScanner;
   SampleScanner gMediaScanner(SampleScanner::Kind::Media);
   PluginScanner gPluginScanner;
   int gSearchPanelMode = 0; // 0 = Modules, 1 = Samples, 2 = Media, 3 = Plugins

   FILE* gStartupTraceFile = nullptr;

   void StartupTrace(const char* stage)
   {
      if (gStartupTraceFile == nullptr)
      {
         // Keep the very first trace independent of std::filesystem and every
         // platform service. diagnose-windows.bat starts the process in the
         // executable directory and reads this file from there, so even a
         // crash in the first platform/JUCE call leaves useful evidence.
         gStartupTraceFile = std::fopen("Infinite-startup.log", "wb");
      }
      if (gStartupTraceFile != nullptr)
      {
         std::fprintf(gStartupTraceFile, "%s\n", stage != nullptr ? stage : "(null)");
         std::fflush(gStartupTraceFile);
      }
   }
   // INFINITE_SAMPLERDRAGTEST only: the Samples panel's result row screen
   // rect, captured live each frame it's drawn so the synthetic drag driver
   // can aim at the real widget rather than a guessed position.
   ImVec4 gSamplerDragTestRowRect(-1.0f, -1.0f, -1.0f, -1.0f);
   // INFINITE_SAMPLERDRAGTEST only: the target Sampler's screen-space
   // centre, recomputed once per frame from ed::GetNodePosition/GetNodeSize
   // *after* ed::End() (those calls need a live editor context - calling
   // them from the pre-ed::Begin() input driver, like the row rect above
   // is read from, segfaults on a null current-editor pointer).
   ImVec2 gSamplerDragTestTargetScreen(-1.0f, -1.0f);
   bool gSamplerDragTestTargetValid = false;
   // INFINITE_MEDIADRAGTEST only: the Media panel's result row screen rect,
   // same role as gSamplerDragTestRowRect above but for the Media mode.
   ImVec4 gMediaDragTestRowRect(-1.0f, -1.0f, -1.0f, -1.0f);
   ImVec2 gMediaDragTestTargetScreen(-1.0f, -1.0f);
   bool gMediaDragTestTargetValid = false;
   // INFINITE_PLUGINDRAGTEST only: same two roles again, for the Plugins mode.
   ImVec4 gPluginDragTestRowRect(-1.0f, -1.0f, -1.0f, -1.0f);
   // Which plugin that captured row actually is. The Plugins panel lists every
   // installed effect, so the fixture can't assume a particular row - it aims
   // at whichever one it captured and asserts against that one's identifier.
   std::string gPluginDragTestRowId;
   ImVec2 gPluginDragTestTargetScreen(-1.0f, -1.0f);
   bool gPluginDragTestTargetValid = false;
   int gPluginDragTestPhase = 0;
   // Mirrors of the sPhase locals in the two drag-test input drivers below,
   // promoted to globals purely so the INFINITE_EXITAFTER handler can print
   // a diagnostic if a run times out stuck at phase 0 (never found a usable
   // row) instead of exiting with no verdict line at all.
   int gSamplerDragTestPhase = 0;
   int gMediaDragTestPhase = 0;

   // Set when a cable drag is released on empty canvas (link-drag search):
   // the output pin the drag started from, and the node types the search
   // popup should suggest first because they're compatible with it.
   int gLinkDragSourcePin = -1;
   std::vector<std::pair<std::string, std::string>> gLinkDragSuggestions;

   struct LinkInfo
   {
      int id = 0;
      int srcPin = 0;
      int dstPin = 0;
   };
   const int kLinkIdBase = 4000000; // far above any node/pin id
   std::vector<LinkInfo> gLinks;

   std::unordered_map<int, size_t> gLinkPosHint; // see gNodePosHint
   const LinkInfo* FindLink(int id)
   {
      auto hint = gLinkPosHint.find(id);
      if (hint != gLinkPosHint.end() && hint->second < gLinks.size() &&
          gLinks[hint->second].id == id)
         return &gLinks[hint->second];
      for (size_t i = 0; i < gLinks.size(); ++i)
      {
         if (gLinks[i].id == id)
         {
            gLinkPosHint[id] = i;
            return &gLinks[i];
         }
      }
      if (hint != gLinkPosHint.end())
         gLinkPosHint.erase(hint);
      return nullptr;
   }
   bool gSnapToGrid = true;
   float gGridSnap = 20.0f;
   // Audio device/rate/buffer selection, applied to AudioEngine on next
   // Start(). 0 / 0.0 mean "system default" - see
   // Platform::AudioDeviceOpen's doc comment in Platform.h. Session-only,
   // same as every other setting in the "Menu" menu (no settings
   // persistence exists anywhere in this codebase yet).
   uint32_t gAudioOutputDeviceId = 0;
   uint32_t gAudioInputDeviceId = 0;
   double gAudioSampleRate = 0.0;
   int gAudioBufferFrames = 512;
   // Placeholder only - no DSP node reads this yet. P3c's Drive/AudioFilter
   // kernels are the intended future consumer (see README.md's Effects
   // table); this phase just makes the setting exist and be visible.
   float gAudioOversample = 1.0f;
   bool gAudioMirror = false;            // Turbo 0.51: Windows mirror output (OBS)
   std::string gAudioMirrorDevice;       // empty = Windows default
   int gAudioDriver = 0; // Turbo: see Platform::AudioSetRequestedDriver
   std::string gPatchPath;      // "" until the patch has been saved somewhere
   bool gPatchDirty = false;
   std::string gPatchStatus;
   std::vector<std::string> gSkippedNodeTypes; // Turbo 0.53
   bool gSuppressSettingsDirtyOnce = false;
   // Set whenever a Start() call from the toolbar toggle or "Apply audio
   // settings" fails, so the toggle button can surface why on hover instead
   // of just silently staying off. Cleared on a successful Start().
   std::string gAudioStartError;

   // ---- device-change / sleep-wake recovery state (PollAudioRecovery) ----
   // docs/plans/optimization/prompts/02-device-change-and-wake-recovery.md.
   // Bounds recovery to one attempt per kAudioRecoveryMinIntervalMs and no
   // more than kAudioRecoveryMaxAttemptsPerWindow inside any
   // kAudioRecoveryWindowMs rolling window, so a flapping device or a burst
   // of notifications on wake can't spawn overlapping restarts or loop
   // forever - see PollAudioRecovery's comment for how these are used.
   constexpr double kAudioRecoveryMinIntervalMs = 500.0;
   constexpr double kAudioRecoveryWindowMs = 10000.0;
   constexpr int kAudioRecoveryMaxAttemptsPerWindow = 5;
   double gLastAudioRecoveryAttemptMs = -1.0;
   double gAudioRecoveryWindowStartMs = -1.0;
   int gAudioRecoveryAttemptsInWindow = 0;
   // Test-only reset so INFINITE_AUDIORECOVERYTEST can run more than one
   // scenario in the same process without an earlier scenario's backoff
   // state leaking into the next.
   void ResetAudioRecoveryState()
   {
      gLastAudioRecoveryAttemptMs = -1.0;
      gAudioRecoveryWindowStartMs = -1.0;
      gAudioRecoveryAttemptsInWindow = 0;
   }
   double gLastFrameMs = 0.0; // wall clock across the previous whole frame
   double gFrameStart = 0.0;  // when the current frame began, for the limiter
   // 60 by default: a light patch spinning the GPU at 400fps costs power for
   // nothing, and a predictable budget makes the cost of a setting readable.
   int gTargetFps = 60;       // 0 = uncapped; otherwise the frame limiter's budget
   bool gVsync = true;
   bool gDiagnosticLogEnabled = true;
   bool gAutosaveEnabled = true;
   bool gAudioAutoStart = false;        // Turbo: Settings > Audio > start audio when Infinite opens
   bool gAudioAutoStartPending = false; // set at launch, consumed on the first frames
   int gAutosaveSeconds = 30;
   double gLastAutosaveTime = 0.0;
   bool gRecoveryAvailable = false;
   bool gRecoveryPromptShown = false;
   bool gRequestFitView = false;
   // Set by the Edit menu, consumed next to the matching keyboard shortcuts.
   // The menu bar draws outside ed::Begin/End, and grouping needs the live
   // selection, so both routes meet in one place inside the editor frame
   // rather than the menu reaching into the editor from outside it.
   bool gRequestGroup = false;
   bool gRequestUngroup = false;
   int gContextMenuNodeIndex = -1; // node the right-click context menu is open for
   int gHelpPopupNodeIndex = -1; // node the per-node "Help" popup is open for
   bool gOpenNodeHelpPopup = false; // set for one frame to open it (can't OpenPopup from inside another popup's Begin/End and have it show the same frame)
   // Which param's modulation-binding menu (Absolute/Bipolar/depth/Unbind) is
   // open. Same reason as gContextMenuNodeIndex/gOpenNodeHelpPopup above: the
   // param slider/knob is drawn deep inside ed::Begin()/ed::End() for that
   // node, which runs under the node editor's own canvas transform - an
   // ImGui::OpenPopup() called there lands at whatever screen position the
   // canvas transform happens to leave the cursor at (and, worse, doesn't
   // reliably receive its own clicks), same as any other popup in this file.
   // So the widget only *records* nodeIndex/paramIndex and a "please open"
   // flag; the popup itself is opened and drawn once, later, inside the
   // ed::Suspend() block alongside "##nodecontext"/"##nodehelp".
   int gModBindingMenuNode = -1;
   int gModBindingMenuParam = -1;
   bool gOpenModBindingMenu = false;
   // The node browser lives in a docked panel rather than only the canvas popup,
   // so modules can be found without knowing the double-click gesture exists.
   bool gNodePanelOpen = false;
   // Turbo: TouchDesigner-style canvas preview. One node's output is drawn
   // behind the node canvas (under the patch), and the whole UI can go
   // borderless fullscreen (F11) - together they make live-coding possible.
   int gCanvasBgNodeIndex = -1;       // -1 = off
   int gCanvasBgFit = 0;              // GLUtil::OutputFitMode
   float gCanvasBgDim = 0.30f;        // black veil so cables and text stay readable
   float gCanvasBgNodeAlpha = 0.80f;  // node card opacity while the preview is on
   bool gCanvasBgGrid = false;
   bool gCanvasBgActive = false;      // this frame
   bool gMainFullscreen = false;
   bool gMainWasMaximized = false;
   int gMainWindowedX = 100, gMainWindowedY = 100, gMainWindowedW = 1600, gMainWindowedH = 1000;
   // The dockable viewport panel: every node index in this list gets its own
   // card, stacked left-to-right when bottom-docked or top-to-bottom when
   // right/left-docked (see DrawViewportPanelContainer). The panel is showing
   // at all iff this is non-empty. Session UI state only, like gNodePanelOpen
   // above - not serialized to patch data or tracked by undo.
   std::vector<int> gViewportPanelNodes;
   int gViewportPanelDock = 1;        // 0 = bottom, 1 = right, 2 = left
   ImVec2 gViewCenterCanvas(0.0f, 0.0f); // captured inside the editor for spawning
   ImVec2 gGraphScreenTL(0.0f, 0.0f);    // graph canvas's screen-space rect,
   ImVec2 gGraphScreenSize(0.0f, 0.0f);  // captured the same way, for the minimap overlay
   bool gMinimapEnabled = false;

   // Projector output: auxiliary GLFW windows sharing textures and programs
   // with the editor. Each one can be placed on an explicit display and
   // toggled borderless fullscreen without moving the main editor window.
   struct ProjectorWindow
   {
      GLFWwindow* window = nullptr;
      int nodeIndex = -1; // GraphNode::index this window shows
      bool fullscreen = false;
      int monitorIndex = -1;
      int windowedX = 100;
      int windowedY = 100;
      int windowedW = 1280;
      int windowedH = 720;
      int swapInterval = 0; // see ApplyMainSwapInterval
      // Turbo: how the node's texture maps onto this window (GLUtil::OutputFitMode)
      // and the size last requested in exact pixels (0 = not set).
      int fitMode = 0;
      int pixelW = 0;
      int pixelH = 0;
   };
   // Turbo 0.51: a node whose own background is transparent (alpha mode). Only
   // then does a windowed Output pass alpha through to the desktop.
   bool OutputNodeHasTransparentBg(INode* node)
   {
      if (const auto* layout = dynamic_cast<const LayoutNode*>(node))
         return layout->bgAlpha < 0.999f;
      return false;
   }
   int gProjectorDefaultFitMode = 0; // new output windows inherit the last choice
   std::vector<ProjectorWindow> gProjectorWindows;
   GLFWwindow* gMainWindow = nullptr;

   // Turbo: exactly one window waits for VSync per frame. Without output
   // windows that is the editor; with output windows it is the last projector
   // presented, so the projection is tear-free and paced by its own display
   // while the editor presents immediately. -1 forces a re-apply.
   int gMainSwapIntervalApplied = 1;
   int gGLContextMajor = 3; // what the main window actually got
   int gGLContextMinor = 2;
   void ApplyMainSwapInterval(int interval)
   {
      if (interval != gMainSwapIntervalApplied)
      {
         glfwSwapInterval(interval);
         gMainSwapIntervalApplied = interval;
      }
   }
   constexpr const char* kShortcutNew = "Ctrl+N";
   constexpr const char* kShortcutOpen = "Ctrl+O";
   constexpr const char* kShortcutSave = "Ctrl+S";
   constexpr const char* kShortcutSaveAs = "Ctrl+Shift+S";
   constexpr const char* kShortcutUndo = "Ctrl+Z";
   constexpr const char* kShortcutRedo = "Ctrl+Shift+Z";
   constexpr const char* kShortcutGroup = "Ctrl+G";
   constexpr const char* kShortcutUngroup = "Ctrl+Shift+G";
   // Bottom-left by default: the module browser docks on the right, and the
   // minimap draws (and takes its clicks) on the foreground draw list, so a
   // right-hand corner would sit on top of the panel and swallow clicks meant
   // for the module list.
   int gMinimapCorner = 2; // 0=TL, 1=TR, 2=BL, 3=BR
   float gMinimapSize = 190.0f;
   float gMinimapOpacity = 0.85f;
   float gZoomSensitivity = 0.5f;
   bool gHoveringItem = false;   // last frame: cursor over a node/pin/link
   bool gPanWithLeft = false;    // current left-drag is a canvas pan, not a select
   // A Render 3D viewport was hovered this frame (set while drawing nodes) /
   // owns the current right or middle drag, so the canvas must not pan with it.
   bool gViewport3DHoveredNow = false;
   bool gViewport3DOwnsDrag = false;
   ImVec2 gDragTestNodeScreen(0.0f, 0.0f);
   ImVec2 gDragTestNodePos(0.0f, 0.0f);
   ImVec2 gTestMouse(0.0f, 0.0f);
   // Screen rects of the Wavetable node's draggable visualizers, republished
   // every frame the node draws: (frames, amp, pitch, filter) per engine, in
   // column order. Only INFINITE_WTDRAGTEST reads them - it is the one thing
   // that cannot be checked from a screenshot, because "the picture is in the
   // right place" and "dragging the picture moves this engine's parameter"
   // are different claims, and the second is the one that broke.
   std::vector<ImVec4> gWtTestRects;
   bool gWtDragOk = true;
   // Same rects converted to screen space. The conversion cannot happen at
   // capture time: ed::CanvasToScreen hangs when called from inside
   // ed::Begin/End (and from before ed::Begin), and the one place it is known
   // to be safe is the post-editor block, where DRAGTEST already calls it. So
   // the capture stays canvas-space and the block below republishes it each
   // frame; the synthetic mouse then aims with the previous frame's rects,
   // which is fine for a static node.
   std::vector<ImVec4> gWtTestScreen;
   void PublishWtTestRect();
   ImVec2 gDragTestViewAnchor(0.0f, 0.0f);

   // EQ's own drag-verification rect, same reasoning as gWtTestRects/
   // gWtTestScreen above: captured in canvas space inside DrawEqVisualizer
   // (cheap, so published unconditionally rather than only under the test
   // flag), converted to screen space once per frame in the post-editor
   // block (the only place ed::CanvasToScreen is safe to call), and read by
   // INFINITE_EQDRAGTEST's synthetic mouse a frame later.
   ImVec4 gEqTestRect(0.0f, 0.0f, 0.0f, 0.0f);
   ImVec4 gEqTestScreen(0.0f, 0.0f, 0.0f, 0.0f);
