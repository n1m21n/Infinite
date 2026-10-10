// Settings window (moved verbatim from main.cpp).
#include "app/ui/design/components/MenuParts.h"
#include "app/ui/design/components/FieldWell.h"
#include "app/ui/design/GlyphDraw.h"
#include "app/ui/design/TokenColors.h"
#include "app/AppShared.h"
#include "app/ui/design/components/FormParts.h"
#include "app/ui/design/components/LibraryParts.h"

namespace app
{
   void DrawSettingsWindow(bool* open)
   {
      // No SetNextWindowPos meant this took ImGui's default cascade position
      // for a first-time window, which lands high enough to crowd the menu
      // bar it was just opened from. Centering it on first appearance - same
      // convention as the Unsaved Changes / Recover Autosave modals below -
      // gives it consistent breathing room on every screen size instead of a
      // fixed offset that would be wrong on a small display.
      ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
      ImGui::SetNextWindowSize(ImVec2(740, 560), ImGuiCond_FirstUseEver);
      PushElevatedPanelStyle(/*isChild=*/false);
      // Cards reach SectionCard::kPad into the window padding, so 16 leaves an 8 pt margin to the window edge on every side.
      FormParts::PushWindowPad();
      const bool settingsVisible = ImGui::Begin(L("Settings"), open, ImGuiWindowFlags_NoCollapse);
      FormParts::PopWindowPad();
      if (!settingsVisible)
      {
         ImGui::End();
         PopElevatedPanelStyle();
         return;
      }

      SectionCard::BeginWindow();
      FormParts::PushTabStyle();
      if (ImGui::BeginTabBar("SettingsTabs", ImGuiTabBarFlags_None))
      {
         // 1. Appearance Tab
         if (ImGui::BeginTabItem(L("Appearance"), nullptr, 0))
         {
            const bool isLight = CategoryColors::IsThemeLight();
            ImGui::Spacing();
            FormParts::Section(T("Theme Preset & Palette"));

            const std::vector<std::string>& presets = CategoryColors::PresetNames();
            const int currentPreset = CategoryColors::CurrentPreset();

            // Small fingerprint swatches (panel/text/accent) let a user tell
            // "Nord" from "Nord Light" from "Dracula" apart without needing
            // to already know what each preset looks like. The border color
            // is chosen from the *live* theme's polarity (not the swatch's
            // own color) so a near-white swatch stays legible against a
            // near-white active-theme popup, and a near-black swatch stays
            // legible against a near-black one.
            auto drawThemeSwatches = [&](ImVec2 pos, float sz, float gap, const CategoryColors::UiTheme& t) {
               ImDrawList* dl = ImGui::GetWindowDrawList();
               const CategoryColors::Color swatchCols[3] = { t.panelBg, t.text, t.accent };
               for (int s = 0; s < 3; s++)
               {
                  ImVec2 p0(pos.x + s * (sz + gap), pos.y);
                  ImVec2 p1(p0.x + sz, p0.y + sz);
                  const CategoryColors::Color& c = swatchCols[s];
                  const ImVec2 mid((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);   // filled circle, like the Clip Settings Color Tint
                  dl->AddCircleFilled(mid, sz * 0.5f, ImGui::GetColorU32(ImVec4(c.r, c.g, c.b, 1.0f)), 48);
                  // Outline in the text colour: a preset's background colour can match the popup it sits on, and the swatch would vanish.
                  { const ImVec4 tx = ImGui::GetStyleColorVec4(ImGuiCol_Text); dl->AddCircle(mid, sz * 0.5f - 0.5f, ImGui::GetColorU32(ImVec4(tx.x, tx.y, tx.z, 0.45f)), 48, 1.0f); }
               }
            };

            const float kSwatchSz = 15.0f;
            const float kSwatchGap = 4.0f;
            const float kSwatchStripW = 3 * kSwatchSz + 2 * kSwatchGap;

            static float sHeaderSwatchX = 0.0f;   // the list's swatches line up under the header swatch beside the combo (set last frame)
            if (FormParts::BeginCombo(L("Preset"), presets[currentPreset].c_str(), 240.0f))
            {
               for (int i = 0; i < (int)presets.size(); i++)
               {
                  const bool selected = (currentPreset == i);
                  if (MenuParts::Choice(presets[i].c_str(), selected))
                  {
                     CategoryColors::SetPreset(i);
                     ApplyTheme();
                  }
                  const ImVec2 rMin = ImGui::GetItemRectMin();
                  const ImVec2 rMax = ImGui::GetItemRectMax();
                  const float y = rMin.y + (rMax.y - rMin.y - kSwatchSz) * 0.5f;
                  // One column for every row: from the popup's content edge, not from each row's own rect (the selected row differs).
                  const float x = sHeaderSwatchX > 0.0f ? sHeaderSwatchX : ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - 8.0f - kSwatchStripW;
                  drawThemeSwatches(ImVec2(x, y), kSwatchSz, kSwatchGap, CategoryColors::UiThemeForPreset(i));
               }
               FormParts::EndCombo();
            }
            ImGui::SameLine();
            // Fingerprint swatch for the currently-selected preset, so the
            // tab reads at a glance even before opening the dropdown.
            {
               const ImVec2 cursor = ImGui::GetCursorScreenPos();
               sHeaderSwatchX = cursor.x;
               const float rowH = ImGui::GetFrameHeight();
               const float y = cursor.y + (rowH - kSwatchSz) * 0.5f;
               drawThemeSwatches(ImVec2(cursor.x, y), kSwatchSz, kSwatchGap, CategoryColors::UiThemeForPreset(currentPreset));
               ImGui::Dummy(ImVec2(kSwatchStripW, rowH));
            }
            ImGui::SetCursorPosX(FormParts::kLabelW);
            if (FormParts::Button(L("Reset to Defaults")))
            {
               CategoryColors::ResetAllAppearanceBoth();
               ApplyTheme();
            }
            ImGui::Spacing();

            // Node Module Category Colors
            FormParts::Section(T("Node Module Category Colors"));
            const std::vector<std::string>& catNames = CategoryColors::CategoryNames();
            if (ImGui::BeginTable("CatColorsTbl", 2, ImGuiTableFlags_None))
            {
               ImGui::TableSetupColumn("Col1", ImGuiTableColumnFlags_WidthStretch);
               ImGui::TableSetupColumn("Col2", ImGuiTableColumnFlags_WidthStretch);
               for (size_t i = 0; i < catNames.size(); i++)
               {
                  ImGui::TableNextColumn();
                  const std::string& cat = catNames[i];
                  const CategoryColors::Color& col = CategoryColors::ColorFor(cat);
                  float c[3] = { col.r, col.g, col.b };
                  char pickerId[64];
                  snprintf(pickerId, sizeof(pickerId), "##catcol_%s", cat.c_str());
                  if (FormParts::ColorDot(pickerId, c))
                  {
                     CategoryColors::SetCategoryColor(cat, { c[0], c[1], c[2] }, isLight, false);
                  }
                  if (ImGui::IsItemDeactivatedAfterEdit())
                  {
                     CategoryColors::SaveAppearanceOverrides();
                  }
                  ImGui::SameLine(0.0f, 6.0f);
                  ImGui::TextUnformatted(cat.c_str());
               }
               ImGui::EndTable();
            }

            ImGui::Spacing();
            // Cable Category Colors
            FormParts::Section(T("Cable Category Colors"));
            const std::vector<std::string>& cableNames = CategoryColors::CableTypeNames();
            if (ImGui::BeginTable("CableColorsTbl", 2, ImGuiTableFlags_None))
            {
               ImGui::TableSetupColumn("Col1", ImGuiTableColumnFlags_WidthStretch);
               ImGui::TableSetupColumn("Col2", ImGuiTableColumnFlags_WidthStretch);
               for (int i = 0; i < (int)CategoryColors::CableType::Count; i++)
               {
                  ImGui::TableNextColumn();
                  auto type = (CategoryColors::CableType)i;
                  const CategoryColors::Color& col = CategoryColors::CableColorFor(type);
                  float c[3] = { col.r, col.g, col.b };
                  char pickerId[64];
                  snprintf(pickerId, sizeof(pickerId), "##cablecol_%d", i);
                  if (FormParts::ColorDot(pickerId, c))
                  {
                     CategoryColors::SetCableColor(type, { c[0], c[1], c[2] }, isLight, false);
                  }
                  if (ImGui::IsItemDeactivatedAfterEdit())
                  {
                     CategoryColors::SaveAppearanceOverrides();
                  }
                  ImGui::SameLine(0.0f, 6.0f);
                  ImGui::TextUnformatted(cableNames[i].c_str());
               }
               ImGui::EndTable();
            }

            ImGui::Spacing();
            // Node Card Styling
            FormParts::Section(T("Node Card Styling"));
            float opacity = CategoryColors::GetNodeOpacity();
            if (FormParts::SliderFloat(L("Node Opacity"), &opacity, 0.10f, 1.00f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
            {
               CategoryColors::SetNodeOpacity(opacity, isLight, false);
            }
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
               CategoryColors::SaveAppearanceOverrides();
            }

            float rounding = CategoryColors::GetNodeRounding();
            if (FormParts::SliderFloat(L("Node Corner Radius"), &rounding, 0.0f, 24.0f, "%.0f px", ImGuiSliderFlags_AlwaysClamp))
            {
               CategoryColors::SetNodeRounding(rounding, false);
               ApplyTheme();
            }
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
               CategoryColors::SaveAppearanceOverrides();
            }

            float tintWeight = CategoryColors::GetTintWeight();
            if (FormParts::SliderFloat(L("Tint"), &tintWeight, 0.0f, 0.50f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
            {
               CategoryColors::SetTintWeight(tintWeight, isLight, false);
            }
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
               CategoryColors::SaveAppearanceOverrides();
            }

            ImGui::Spacing();
            FormParts::Section(T("Display"));
            float uiScale = CategoryColors::GetUiScale();
            if (FormParts::SliderFloat(L("UI Scale"), &uiScale, 0.5f, 2.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp))
            {
               CategoryColors::SetUiScale(uiScale, false);
            }
            // Applied on release, not per drag step: every rescale rebakes the font atlas, and
            // the slider itself would move under the cursor while being dragged.
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
               CategoryColors::SaveAppearanceOverrides();
               UiScale::RequestRescale();
            }
            {
               bool tips = CategoryColors::GetTooltips();
               if (FormParts::Switch(L("Help tooltips"), &tips))
                  CategoryColors::SetTooltips(tips);
            }
            {
               bool rm = CategoryColors::GetReduceMotion();
               if (FormParts::Switch(L("Reduce motion"), &rm))
                  CategoryColors::SetReduceMotion(rm);
               if (ImGui::IsItemHovered())
                  HelpTip("%s", L("Hover and toggle eases become instant. Nothing in the UI animates a value."));
            }

            {
               const std::string currentFont = CategoryColors::GetUiFont();
               const char* currentLabel = kInterfaceFonts[0].label;
               for (const InterfaceFont& f : kInterfaceFonts)
                  if (currentFont == f.id)
                     currentLabel = f.label;
               if (FormParts::BeginCombo(L("Interface font"), currentLabel))
               {
                  for (const InterfaceFont& f : kInterfaceFonts)
                  {
                     if (MenuParts::Choice(f.label, currentFont == f.id) && currentFont != f.id)
                     {
                        CategoryColors::SetUiFont(f.id); // saves
                        UiScale::RequestRescale();       // rebakes the atlas with the new face
                     }
                  }
                  FormParts::EndCombo();
               }
            }

            {
               // Native names in their own script, so a user who cannot read the current language
               // can still find theirs. Machine-drafted languages carry "(beta)" until reviewed.
               const std::string current = I18n::CurrentLanguage();
               auto labelOf = [](const I18n::Language& l) {
                  std::string s = l.nativeName;
                  if (l.beta)
                     s += " (beta)";
                  return s;
               };
               std::string currentLabel = "English";
               for (const I18n::Language& l : I18n::Languages())
                  if (current == l.code)
                     currentLabel = labelOf(l);
               if (FormParts::BeginCombo(L("Language"), currentLabel.c_str()))
               {
                  for (const I18n::Language& l : I18n::Languages())
                  {
                     const std::string label = labelOf(l) + "##lang_" + l.code;
                     if (MenuParts::Choice(label.c_str(), current == l.code) && current != l.code)
                     {
                        CategoryColors::SetLanguage(l.code); // saves
                        I18n::RequestLanguage(l.code);
                        UiScale::RequestRescale(); // applies the table and rebakes glyphs
                     }
                  }
                  FormParts::EndCombo();
               }
            }

            ImGui::Spacing();
            // Transparency Backdrop
            FormParts::Section(T("Transparency Backdrop"));
            const char* kBackdropStyles[] = { T("Checkerboard"), T("Solid Color") };
            int backdropStyle = gCheckerboardBackdrop ? 0 : 1;
            if (FormParts::BeginCombo(L("Preview Background"), kBackdropStyles[backdropStyle]))
            {
               for (int bi = 0; bi < 2; ++bi)
                  if (MenuParts::Choice(kBackdropStyles[bi], backdropStyle == bi))
                  {
                     gCheckerboardBackdrop = (bi == 0);
                     SaveGeneralSettings();
                  }
               FormParts::EndCombo();
            }

            SectionCard::End();
            ImGui::EndTabItem();
         }

         // 2. Canvas & Workspace Tab
         if (ImGui::BeginTabItem(L("Canvas & Workspace"), nullptr, 0))
         {
            ImGui::Spacing();
            FormParts::Section(T("Grid & Canvas"));
            if (FormParts::Switch(L("Snap to grid"), &gSnapToGrid))
               SaveWorkspaceSettings();
            if (FormParts::Switch(L("Show canvas grid"), &gShowCanvasGrid))
            {
               ApplyTheme();
               SaveWorkspaceSettings();
            }
            FormParts::SliderFloat(L("Grid size"), &gGridSnap, 5.0f, 100.0f, "%.0f px");
            if (ImGui::IsItemDeactivatedAfterEdit())
               SaveWorkspaceSettings();
            FormParts::SliderFloat(L("Zoom sensitivity"), &gZoomSensitivity, 0.05f, 1.5f, "%.2f");
            if (ImGui::IsItemDeactivatedAfterEdit())
               SaveWorkspaceSettings();

            ImGui::Spacing();
            FormParts::Section(T("Minimap"));
            if (FormParts::Switch(L("Show minimap"), &gMinimapEnabled))
               SaveWorkspaceSettings();
            if (gMinimapEnabled)
            {
               const char* kCorners[] = {
                  T("Top left"), T("Top right"), T("Bottom left"), T("Bottom right")
               };
               if (FormParts::BeginCombo(L("Position"), kCorners[gMinimapCorner]))
               {
                  for (int i = 0; i < 4; i++)
                     if (MenuParts::Choice(kCorners[i], i == gMinimapCorner))
                     {
                        gMinimapCorner = i;
                        SaveWorkspaceSettings();
                     }
                  FormParts::EndCombo();
               }
               FormParts::SliderFloat(L("Size"), &gMinimapSize, 120.0f, 360.0f, "%.0f px");
               if (ImGui::IsItemDeactivatedAfterEdit())
                  SaveWorkspaceSettings();
               FormParts::SliderFloat(L("Opacity"), &gMinimapOpacity, 0.2f, 1.0f, "%.2f");
               if (ImGui::IsItemDeactivatedAfterEdit())
                  SaveWorkspaceSettings();
            }

            ImGui::Spacing();
            FormParts::Section(T("Cable Visibility"));
            bool showMod = (gCableVisibilityMask & 0x4) != 0;
            bool showAudNote = (gCableVisibilityMask & 0x2) != 0;
            bool showImg = (gCableVisibilityMask & 0x1) != 0;

            if (FormParts::Switch(L("Modulation cables"), &showMod))
            {
               gCableVisibilityMask = (gCableVisibilityMask & ~0x4) | (showMod ? 0x4 : 0);
               SaveWorkspaceSettings();
            }
            if (FormParts::Switch(L("Audio & note cables"), &showAudNote))
            {
               gCableVisibilityMask = (gCableVisibilityMask & ~0x2) | (showAudNote ? 0x2 : 0);
               SaveWorkspaceSettings();
            }
            if (FormParts::Switch(L("Image & geometry cables"), &showImg))
            {
               gCableVisibilityMask = (gCableVisibilityMask & ~0x1) | (showImg ? 0x1 : 0);
               SaveWorkspaceSettings();
            }

            ImGui::TextDisabled("%s", T("Note: Image & geometry toggle also covers palette cables."));
            ImGui::Spacing();
            if (FormParts::Button(L("Show all cables")))
            {
               gCableVisibilityMask = 0x7;
               SaveWorkspaceSettings();
            }
            ImGui::SameLine();
            if (FormParts::Button(L("Hide all cables")))
            {
               gCableVisibilityMask = 0x0;
               SaveWorkspaceSettings();
            }

            SectionCard::End();
            ImGui::EndTabItem();
         }

         // 3. Audio Tab
         if (ImGui::BeginTabItem(L("Audio"), nullptr, 0))
         {
            ImGui::Spacing();
            FormParts::Section(T("Audio Devices & Format"));
            const std::vector<Platform::AudioDeviceInfo> devices = Platform::AudioListDevices();
            const bool audioRunning = AudioEngine::Instance().SampleRate() > 0.0;

            std::string outputLabel = (gAudioOutputDeviceId == 0) ? T("System default") : T("Unknown device");
            for (const Platform::AudioDeviceInfo& d : devices)
               if (d.isOutput && d.deviceId == gAudioOutputDeviceId)
                  outputLabel = d.name;
            if (FormParts::BeginCombo(L("Output device"), outputLabel.c_str()))
            {
               if (MenuParts::Choice(L("System default"), gAudioOutputDeviceId == 0))
                  gAudioOutputDeviceId = 0;
               for (const Platform::AudioDeviceInfo& d : devices)
               {
                  if (!d.isOutput)
                     continue;
                  if (MenuParts::Choice(d.name.c_str(), d.deviceId == gAudioOutputDeviceId))
                     gAudioOutputDeviceId = d.deviceId;
               }
               FormParts::EndCombo();
            }

            std::string inputLabel = (gAudioInputDeviceId == 0) ? T("System default") : T("Unknown device");
            for (const Platform::AudioDeviceInfo& d : devices)
               if (d.isInput && d.deviceId == gAudioInputDeviceId)
                  inputLabel = d.name;
            if (FormParts::BeginCombo(L("Input device"), inputLabel.c_str()))
            {
               if (MenuParts::Choice(L("System default"), gAudioInputDeviceId == 0))
               {
                  gAudioInputDeviceId = 0;
                  Platform::AudioInputCaptureSetDevice(0);
               }
               for (const Platform::AudioDeviceInfo& d : devices)
               {
                  if (!d.isInput)
                     continue;
                  if (MenuParts::Choice(d.name.c_str(), d.deviceId == gAudioInputDeviceId))
                  {
                     gAudioInputDeviceId = d.deviceId;
                     Platform::AudioInputCaptureSetDevice(d.deviceId);
                  }
               }
               FormParts::EndCombo();
            }

            static const double kSampleRates[] = { 44100.0, 48000.0, 88200.0, 96000.0 };
            std::string rateLabel = (gAudioSampleRate == 0.0) ? T("Device default") : "";
            if (gAudioSampleRate != 0.0)
            {
               char buf[32];
               snprintf(buf, sizeof(buf), "%.0f Hz", gAudioSampleRate);
               rateLabel = buf;
            }
#if defined(_WIN32)
            ImGui::BeginDisabled();
#endif
            if (FormParts::BeginCombo(L("Sample rate"), rateLabel.c_str()))
            {
               if (MenuParts::Choice(L("Device default"), gAudioSampleRate == 0.0))
                  gAudioSampleRate = 0.0;
               for (double rate : kSampleRates)
               {
                  char label[32];
                  snprintf(label, sizeof(label), "%.0f Hz", rate);
                  if (MenuParts::Choice(label, gAudioSampleRate == rate))
                     gAudioSampleRate = rate;
               }
               FormParts::EndCombo();
            }
#if defined(_WIN32)
            ImGui::EndDisabled();
            if (ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()))
               HelpTip("%s", T("Windows shared-mode audio always runs at the format set in Sound settings. Change the sample rate there."));
#endif

            static const int kBufferSizes[] = { 64, 128, 256, 512, 1024, 2048 };
            char bufferLabel[16];
            snprintf(bufferLabel, sizeof(bufferLabel), "%d", gAudioBufferFrames);
#if defined(_WIN32)
            ImGui::BeginDisabled();
#endif
            if (FormParts::BeginCombo(L("Buffer size"), bufferLabel))
            {
               for (int frames : kBufferSizes)
               {
                  char label[16];
                  snprintf(label, sizeof(label), "%d", frames);
                  if (MenuParts::Choice(label, gAudioBufferFrames == frames))
                     gAudioBufferFrames = frames;
               }
               FormParts::EndCombo();
            }
#if defined(_WIN32)
            ImGui::EndDisabled();
            if (ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()))
               HelpTip("%s", T("Windows shared-mode audio always runs at the device's own period. Change it in Sound settings."));
#endif

#if defined(_WIN32)
            {
               // Opt-in low-latency output. Every mode falls back toward
               // Standard if the device or driver refuses it, so choosing
               // one can never leave the app silent; takes effect on Apply.
               const char* kOutputModeLabels[] = { T("Standard (shared)"), T("Low latency (shared)"),
                                                   T("Exclusive") };
               gAudioOutputMode = std::clamp(gAudioOutputMode, 0, 2);
               if (FormParts::BeginCombo(L("Output mode"), kOutputModeLabels[gAudioOutputMode]))
               {
                  for (int i = 0; i < 3; i++)
                     if (MenuParts::Choice(kOutputModeLabels[i], gAudioOutputMode == i))
                     {
                        gAudioOutputMode = i;
                        SaveAudioSettings();
                     }
                  FormParts::EndCombo();
               }
               if (ImGui::IsItemHovered())
                  HelpTip("%s", T("Standard: Windows shared mode (default).\nLow latency: shared mode at the driver's smallest period (Windows 10+).\nExclusive: takes the device from other apps for the lowest latency.\nIf a mode is unavailable Infinite steps back toward Standard. Click Apply audio settings to use it."));
            }
#endif

            static const float kOversampleValues[] = { 1.0f, 2.0f, 4.0f };
            static const char* kOversampleLabels[] = { "1x", "2x", "4x" };
            int oversampleIdx = 0;
            for (int i = 0; i < 3; i++)
               if (kOversampleValues[i] == gAudioOversample)
                  oversampleIdx = i;
            if (FormParts::BeginCombo(L("Oversampling"), kOversampleLabels[oversampleIdx]))
            {
               for (int i = 0; i < 3; i++)
                  if (MenuParts::Choice(kOversampleLabels[i], oversampleIdx == i))
                  {
                     gAudioOversample = kOversampleValues[i];
                     SaveAudioSettings();
                  }
               FormParts::EndCombo();
            }

            if (audioRunning)
            {
               const uint32_t actualBufferFrames = Platform::AudioDeviceBufferFrames(gAudioOutputDeviceId);
               ImGui::TextDisabled(T("Active: %.0f Hz, %u frames"), AudioEngine::Instance().SampleRate(),
                                   actualBufferFrames);
#if defined(_WIN32)
               {
                  const char* kActiveModeLabels[] = { T("standard"), T("low-latency shared"), T("exclusive") };
                  ImGui::TextDisabled(T("Output mode in use: %s"),
                                      kActiveModeLabels[std::clamp(Platform::AudioOutputModeActive(), 0, 2)]);
               }
#endif
               // Round-trip (input to output) latency estimate: what a
               // looper/overdub must compensate for. 0 = the platform cannot say.
               {
                  const double rate = AudioEngine::Instance().SampleRate();
                  const uint32_t rtFrames = Platform::AudioRoundTripLatencyFrames(gAudioOutputDeviceId);
                  if (rtFrames > 0 && rate > 0.0)
                     ImGui::TextDisabled(T("Round-trip latency: ~%.1f ms (%u frames)"),
                                         (double)rtFrames * 1000.0 / rate, rtFrames);
                  else
                     ImGui::TextDisabled("%s", T("Round-trip latency: unknown"));
                  if (ImGui::IsItemHovered())
                     HelpTip("%s", T("Estimate: output device + stream + buffer, plus the input device's own latency. Real hardware chains can add more (interfaces, Bluetooth, drivers)."));
               }
            }

            ImGui::Spacing();
            if (FormParts::Button(L("Apply audio settings"), ImVec2(180, 0)))
            {
               const bool wasRunning = AudioEngine::Instance().SampleRate() > 0.0;
               if (wasRunning)
                  AudioEngine::Instance().Stop();

               AudioEngine::Instance().SetRequestedDevice(gAudioOutputDeviceId);
               AudioEngine::Instance().SetRequestedSampleRate(gAudioSampleRate);
               AudioEngine::Instance().SetRequestedBufferFrames(gAudioBufferFrames);
               Platform::AudioSetOutputMode(gAudioOutputMode);
               SaveAudioSettings();

               if (wasRunning)
               {
                  gAudioStartError.clear();
                  if (!StartAudioEngine(gAudioStartError))
                     fprintf(stderr, "audio device: %s\n", gAudioStartError.c_str());
               }
            }

            SectionCard::End();
            ImGui::EndTabItem();
         }

         // 4. General & Performance Tab
         // Review hook: INFINITE_OPENPANELS=...settingsgeneral opens this tab first (design review shots).
         static const bool sGeneralFirst = [] { const char* o = getenv("INFINITE_OPENPANELS"); return o && strstr(o, "settingsgeneral"); }();
         static bool sGeneralPicked = false;
         const ImGuiTabItemFlags generalFlags = (sGeneralFirst && !sGeneralPicked) ? ImGuiTabItemFlags_SetSelected : 0;
         sGeneralPicked = true;
         if (ImGui::BeginTabItem(L("General & Performance"), nullptr, generalFlags))
         {
            ImGui::Spacing();
            FormParts::Section(T("Autosave"));
            if (gAutosaveFailed)
            {
               ImGui::PushStyleColor(ImGuiCol_Text, tok::V4(tok::palf::v_1000_420_350_1000));
               ImGui::TextUnformatted(T("Autosave is FAILING - save manually"));
               ImGui::PopStyleColor();
               if (ImGui::IsItemHovered())
               {
                  const std::string path = AutosavePath();
                  ImGui::SetTooltip(T("Could not write %s.\nCheck permissions and free disk space."),
                                    path.empty() ? T("the settings directory") : path.c_str());
               }
            }
            if (FormParts::Switch(L("Autosave enabled"), &gAutosaveEnabled))
               SaveGeneralSettings();
            int seconds = gAutosaveSeconds;
            if (FormParts::SliderInt(L("Autosave interval"), &seconds, 15, 300, "%d sec"))
               gAutosaveSeconds = seconds;
            if (ImGui::IsItemDeactivatedAfterEdit())
               SaveGeneralSettings();

            ImGui::Spacing();
            FormParts::Section(T("Performance & Rendering"));
            const char* kFpsLabels[] = { T("Unlimited"), "30", "60", "120" };
            static const int kFpsValues[] = { 0, 30, 60, 120 };
            int current = 0;
            for (int i = 0; i < 4; i++)
               if (kFpsValues[i] == gTargetFps)
                  current = i;

            // Vsync's own present-time wait already paces the frame; layering
            // the sleep-based cap on top of it fights the driver's vsync
            // quantization (frame times land on multiples of the display's
            // real refresh interval, not the requested budget), producing
            // uneven pacing instead of a clean cap. So the two are mutually
            // exclusive rather than combined - matches how most games do it.
            ImGui::BeginDisabled(gVsync);
            if (FormParts::BeginCombo(L("Target FPS"), kFpsLabels[current]))
            {
               for (int i = 0; i < 4; i++)
               {
                  if (MenuParts::Choice(kFpsLabels[i], current == i))
                  {
                     gTargetFps = kFpsValues[i];
                     SaveGeneralSettings();
                  }
               }
               FormParts::EndCombo();
            }
            ImGui::EndDisabled();
            if (gVsync && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
               HelpTip("%s", T("Disabled while Vsync is on - Vsync alone paces the frame.\nTurn Vsync off to use a manual FPS cap."));

            if (FormParts::Switch(L("Vsync"), &gVsync))
            {
               SetCanvasSwapInterval(gVsync ? 1 : 0);
               SaveGeneralSettings();
            }

            ImGui::Spacing();
            ImGui::Spacing();
            FormParts::Section(T("Movement Log"));
            bool moveLogEnabled = MovementLog::IsEnabled();
            if (FormParts::Switch(L("Record movement log"), &moveLogEnabled))
            {
               MovementLog::SetEnabled(moveLogEnabled);
            }
            if (FormParts::Button(L("Open log folder")))
            {
               std::string dir = MovementLog::GetLogDirectory();
               if (!dir.empty())
                  Platform::RevealInFileManager(dir);
            }
            ImGui::SameLine();
            const uint64_t folderBytes = MovementLog::GetLogFolderSizeBytes();
            const float folderMB = (float)folderBytes / (1024.0f * 1024.0f);
            ImGui::Text(T("(%.1f MB used)"), folderMB);

            static const char* kCapLabels[] = { "256 MB", "1 GB", "4 GB" };
            static const uint64_t kCapValues[] = { 256ULL * 1024 * 1024, 1024ULL * 1024 * 1024, 4096ULL * 1024 * 1024 };
            uint64_t curCap = MovementLog::GetRetentionCapBytes();
            int capIdx = 1;
            for (int i = 0; i < 3; i++)
            {
               if (curCap == kCapValues[i])
                  capIdx = i;
            }
            if (FormParts::BeginCombo(L("Retention cap"), kCapLabels[capIdx]))
            {
               for (int i = 0; i < 3; i++)
               {
                  if (MenuParts::Choice(kCapLabels[i], capIdx == i))
                  {
                     MovementLog::SetRetentionCapBytes(kCapValues[i]);
                  }
               }
               FormParts::EndCombo();
            }

            ImGui::Spacing();
            FormParts::Section(T("Diagnostics"));
            {
               bool ct = CategoryColors::GetCookTimes();
               if (FormParts::Switch(L("Cook times"), &ct))
                  CategoryColors::SetCookTimes(ct);
               ImGui::TextDisabled("%s", T("Shows how long each node takes to cook, on the node."));
            }
            if (FormParts::Button(L("Copy system info")))
               CopySystemInfo();
            if (FormParts::Button(L("Reveal logs")))
               Platform::RevealLogs();

            ImGui::Spacing();
            FormParts::Section(T("Application & Updates"));
            ImGui::Text(T("Version: %s"), INFINITE_VERSION_STRING);
            {
               bool autoCheck = UpdateCheck::AutoCheckEnabled();
               if (FormParts::Switch(L("Check on launch"), &autoCheck))
                  UpdateCheck::SetAutoCheckEnabled(autoCheck);
               ImGui::TextDisabled("%s", T("Asks GitHub Releases once when Infinite starts. Nothing else is sent."));
            }
            if (FormParts::Button(L("Check for updates...")))
            {
               UpdateCheck::Start();
               gShowUpdateCheckModal = true;
            }

            ImGui::Spacing();
            FormParts::Section(T("AI assistants"));
            static std::string sPatchSkillStatus;
            if (FormParts::Button(L("Install AI Skill##patchAuthoring")))
            {
               const std::string folder = Platform::OpenFolderDialog("Choose folder for the patch authoring AI skill file");
               if (!folder.empty())
                  sPatchSkillStatus = SaveAISkillFile(folder, "infinite-patch-authoring.md", AISkillContent::kPatchAuthoringMarkdown);
            }
            if (ImGui::IsItemHovered())
               HelpTip("%s", T("Save a reference file you can hand to any AI assistant (or drop into ~/.claude/skills/)\nso it can write, check and render Infinite patches from the command line."));
            if (!sPatchSkillStatus.empty())
            {
               ImGui::SameLine();
               ImGui::TextDisabled("%s", sPatchSkillStatus.c_str());
            }

            SectionCard::End();
            ImGui::EndTabItem();
         }

         // 5. Field Language Reference Tab
         if (ImGui::BeginTabItem(L("Field Language"), nullptr, 0))
         {
            static char sFilterBuf[64] = "";
            static int sSelectedSection = 0;
            const char* const kSections[] = {
               "All Topics",
               "1. Core Principles",
               "2. Domains & Reserved Names",
               "3. Declarations (param, state, attrib)",
               "4. Types & Vectors",
               "5. Operators & Math Functions",
               "6. Domain Transfer Operators",
               "7. Branching & Control Flow",
               "8. Canonical Recipes"
            };

            ImGui::Spacing();
            ImGui::SetNextItemWidth(220.0f);
            FormParts::BareCombo("##fieldtopic", &sSelectedSection, kSections, IM_ARRAYSIZE(kSections), 220.0f);
            ImGui::SameLine(0.0f, 8.0f);
            FormParts::SearchInput("##fieldsearch", "Search functions / syntax...", sFilterBuf, sizeof(sFilterBuf), 220.0f);
            if (sFilterBuf[0] != '\0')
            {
               ImGui::SameLine(0.0f, 6.0f);
               const float btnW = ImGui::GetFrameHeight();
               if (ActionButton::Draw("##clearfieldsearch", ImVec2(btnW, 0)))
                  sFilterBuf[0] = '\0';
               ImDrawList* dl = ImGui::GetWindowDrawList();
               const ImVec2 bmin = ImGui::GetItemRectMin();
               const ImVec2 bmax = ImGui::GetItemRectMax();
               const ImVec2 center((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
               const float iconSize = (bmax.y - bmin.y) * 0.65f;
               const ImU32 col = ImGui::IsItemHovered() ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled);
               glyph::DrawX(dl, center, iconSize, col);
            }

            static std::string sFieldSkillStatus;
            if (FormParts::Button(L("Install AI Skill##fieldLanguage")))
            {
               const std::string folder = Platform::OpenFolderDialog("Choose folder for the Field Language AI skill file");
               if (!folder.empty())
                  sFieldSkillStatus = SaveAISkillFile(folder, "infinite-field-language.md", AISkillContent::kFieldLanguageMarkdown);
            }
            if (ImGui::IsItemHovered())
               HelpTip("%s", T("Save a Field-language reference file you can hand to any AI assistant\n(or drop into ~/.claude/skills/) so it can write Field kernels for you."));
            if (!sFieldSkillStatus.empty())
            {
               ImGui::SameLine();
               ImGui::TextDisabled("%s", sFieldSkillStatus.c_str());
            }

            ImGui::Spacing();
            // i18n-skip-begin: Field language reference stays English (code vocabulary)
            // See DrawModMatrixDocked's inner-content child for why
            // AlwaysUseWindowPadding is needed alongside Border now.
            LibraryParts::BeginWell("##fieldrefcontent");
            FormParts::PushReferenceStyle();

            auto MatchesFilter = [](const char* text) -> bool {
               if (sFilterBuf[0] == '\0') return true;
               std::string str(text);
               std::string q(sFilterBuf);
               auto toLower = [](std::string& s) {
                  for (char& c : s) c = (char)std::tolower((unsigned char)c);
               };
               toLower(str);
               toLower(q);
               return str.find(q) != std::string::npos;
            };

            auto DrawCodeBox = [](const char* code, const char* copyId) {
               // A recessed code well in the theme's own text tint, so it reads in either theme.
               const ImVec4 tx = ImGui::GetStyleColorVec4(ImGuiCol_Text);
               ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(tx.x, tx.y, tx.z, 0.06f));
               ImGui::PushStyleColor(ImGuiCol_Border, FormParts::EdgeCol());
               ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, tok::radius_tile);
               ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
               const float h = ImGui::CalcTextSize(code).y + 2.0f * tok::space_2;
               ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(tok::space_2, tok::space_2));
               ImGui::BeginChild(copyId, ImVec2(0, h), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                                 ImGuiWindowFlags_NoScrollbar);
               ImGui::PopStyleVar();
               ImGui::TextUnformatted(code);
               ImGui::SameLine(ImGui::GetWindowWidth() - 72.0f);
               char btnLabel[32];
               snprintf(btnLabel, sizeof(btnLabel), "Copy##%s", copyId);
               if (FormParts::Button(btnLabel, ImVec2(56.0f, 0)))
                  ImGui::SetClipboardText(code);
               ImGui::EndChild();
               ImGui::PopStyleVar(2);
               ImGui::PopStyleColor(2);
               ImGui::Spacing();
            };

            // Section 1: Core Principles
            if ((sSelectedSection == 0 || sSelectedSection == 1) && MatchesFilter("principles primitive kernel inferred rates bare names"))
            {
               FormParts::Heading("1. Core Principles");
               ImGui::BulletText("One Primitive: A kernel is a body of code run once per element of its domain.");
               ImGui::BulletText("Inferred Rates: There is no @rate keyword. The compiler infers domain rates automatically from dependencies.");
               ImGui::BulletText("Bare Names: Never use '@' sigils. Plain names are used everywhere (e.g. P.y += bass * 2, never @P.y).");
               ImGui::BulletText("Real-Time Safe: Bounded loops, no dynamic heap allocation or recursion on execution threads.");
               ImGui::Spacing();
            }

            // Section 2: Domains & Reserved Names
            if ((sSelectedSection == 0 || sSelectedSection == 2) && MatchesFilter("domains reserved frame element pixel sample graph"))
            {
               FormParts::Heading("2. Domains & Reserved Names");
               if (ImGui::BeginTable("##domainstbl", 4, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg))
               {
                  ImGui::TableSetupColumn("Domain", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                  ImGui::TableSetupColumn("Rate / Backend", ImGuiTableColumnFlags_WidthFixed, 140.0f);
                  ImGui::TableSetupColumn("Reserved Names", ImGuiTableColumnFlags_WidthFixed, 220.0f);
                  ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
                  ImGui::TableHeadersRow();

                  ImGui::TableNextColumn(); ImGui::Text("frame");
                  ImGui::TableNextColumn(); ImGui::TextDisabled("60 fps / Bytecode VM");
                  ImGui::TableNextColumn(); ImGui::TextColored(app::AccentEmphasisSelected(), "t, dt, frame");
                  ImGui::TableNextColumn(); ImGui::Text("Time in seconds, delta time, integer frame counter.");

                  ImGui::TableNextColumn(); ImGui::Text("element");
                  ImGui::TableNextColumn(); ImGui::TextDisabled("60 x N / Bytecode VM");
                  ImGui::TableNextColumn(); ImGui::TextColored(app::AccentEmphasisSelected(), "P, N, uv, Cd, i, count");
                  ImGui::TableNextColumn(); ImGui::Text("Position (vec3), Normal (vec3), UV (vec2), Color (vec3), vertex index, total count.");

                  ImGui::TableNextColumn(); ImGui::Text("pixel");
                  ImGui::TableNextColumn(); ImGui::TextDisabled("60 x W x H / GLSL");
                  ImGui::TableNextColumn(); ImGui::TextColored(app::AccentEmphasisSelected(), "uv, xy, col, res, aspect, alpha");
                  ImGui::TableNextColumn(); ImGui::Text("Normalized UV [0..1], Pixel XY, Output color (vec3), Screen resolution, aspect ratio, output alpha.");

                  ImGui::TableNextColumn(); ImGui::Text("sample");
                  ImGui::TableNextColumn(); ImGui::TextDisabled("48 kHz / Register VM");
                  ImGui::TableNextColumn(); ImGui::TextColored(app::AccentEmphasisSelected(), "in, out, sr, n, freq, gate");
                  ImGui::TableNextColumn(); ImGui::Text("Audio in/out, sample rate (sr), sample index (n), voice note Hz (freq), gate (1.0/0.0).");

                  ImGui::TableNextColumn(); ImGui::Text("graph");
                  ImGui::TableNextColumn(); ImGui::TextDisabled("Edit time / Interpreter");
                  ImGui::TableNextColumn(); ImGui::TextColored(app::AccentEmphasisSelected(), "(none)");
                  ImGui::TableNextColumn(); ImGui::Text("Node graph metaprogramming: emit(), connect(), set(), place().");

                  ImGui::EndTable();
               }
               ImGui::Spacing();
            }

            // Section 3: Declarations
            if ((sSelectedSection == 0 || sSelectedSection == 3) && MatchesFilter("declarations param state attrib output input image img offset sampling"))
            {
               FormParts::Heading("3. Declarations");
               ImGui::TextColored(app::AccentEmphasisSelected(), "param <type> <name> = <default> [<min>, <max>]");
               ImGui::TextWrapped("Declares an exposed control that auto-creates an interactive, modulatable knob/slider in the node UI.");
               DrawCodeBox("param float cutoff = 0.5 [0, 1]\nparam float resonance = 1.2 [0.1, 4.0]", "cb_param");

               ImGui::TextColored(app::AccentEmphasisSelected(), "state <type> <name> = <init>");
               ImGui::TextWrapped("Persistent delay cell (one execution unit of memory). Essential for feedback loops, filters, and integrators.");
               DrawCodeBox("state float z = 0\nz += (in - z) * cutoff\nout = z", "cb_state");

               ImGui::TextColored(app::AccentEmphasisSelected(), "input pixel image img; img(coord)");
               ImGui::TextWrapped("Field Pixel: img is the current-pixel RGBA input. img(vec2) samples it at normalized coordinates, clamped to the source's edge texel centres. Filtering follows the source sampler (normally linear). Only a declared image input is callable; col is the RGB output. No connected image reads transparent black.");
               DrawCodeBox("input pixel image img;\nc = img(uv + vec2(1.0 / res.x, 0));\ncol = c.rgb; alpha = c.a;", "cb_imageoffset");

               ImGui::TextColored(app::AccentEmphasisSelected(), "attrib <type> <name> = <init>");
               ImGui::TextWrapped("Declares custom per-element attributes living on geometry points or vertices.");
               DrawCodeBox("attrib float heat = 0\nheat += bass * 0.1\nCd = vec3(heat, 0.2, 1.0 - heat)", "cb_attrib");
            }

            // Section 4: Types & Vectors
            if ((sSelectedSection == 0 || sSelectedSection == 4) && MatchesFilter("types vectors vec2 vec3 vec4 swizzle rank polymorphism"))
            {
               FormParts::Heading("4. Types & Rank Polymorphism");
               ImGui::BulletText("Types: float, int, bool, vec2, vec3, vec4.");
               ImGui::BulletText("Swizzling: Components .x .y .z .w and .r .g .b .a. Multi-component swizzles like P.xz or col.bgr are supported.");
               ImGui::BulletText("Rank Polymorphism: Scalars implicitly broadcast across all vector lanes when combined.");
               DrawCodeBox("P *= 2.0;               # Scalar 2.0 broadcasts across vec3 P\nCd = vec3(1, 0, 0);     # RGB Red assignment\nvec2 st = uv.yx;        # Swizzle coordinate inversion", "cb_types");
            }

            // Section 5: Operators & Math Functions
            if ((sSelectedSection == 0 || sSelectedSection == 5) && MatchesFilter("operators math functions sin cos lerp rand noise"))
            {
               FormParts::Heading("5. Operators & Built-in Math Functions");
               if (ImGui::BeginTable("##opstbl", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg))
               {
                  ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 140.0f);
                  ImGui::TableSetupColumn("Functions / Operators", ImGuiTableColumnFlags_WidthFixed, 240.0f);
                  ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthStretch);
                  ImGui::TableHeadersRow();

                  ImGui::TableNextColumn(); ImGui::Text("Operators");
                  ImGui::TableNextColumn(); ImGui::TextColored(app::AccentEmphasisSelected(), "%s", "+ - * / % ^, += -= *= /=");
                  ImGui::TableNextColumn(); ImGui::Text("^ is right-associative power (2^3 == 8).");

                  ImGui::TableNextColumn(); ImGui::Text("Logic & Compare");
                  ImGui::TableNextColumn(); ImGui::TextColored(app::AccentEmphasisSelected(), "== != < <= > >=, && || !");
                  ImGui::TableNextColumn(); ImGui::Text("Standard boolean comparisons and logic gates.");

                  ImGui::TableNextColumn(); ImGui::Text("Trigonometry");
                  ImGui::TableNextColumn(); ImGui::TextColored(app::AccentEmphasisSelected(), "sin, cos, tan, asin, acos, atan, atan2");
                  ImGui::TableNextColumn(); ImGui::Text("Standard trig functions (radians).");

                  ImGui::TableNextColumn(); ImGui::Text("Math Utilities");
                  ImGui::TableNextColumn(); ImGui::TextColored(app::AccentEmphasisSelected(), "abs, floor, ceil, fract, clamp, lerp");
                  ImGui::TableNextColumn(); ImGui::Text("lerp(a, b, t) is linear interpolation.");

                  ImGui::TableNextColumn(); ImGui::Text("Advanced Math");
                  ImGui::TableNextColumn(); ImGui::TextColored(app::AccentEmphasisSelected(), "smoothstep, pow, sqrt, exp, log, min, max");
                  ImGui::TableNextColumn(); ImGui::Text("Hermite interpolation, exponents, roots, min/max bounds.");

                  ImGui::TableNextColumn(); ImGui::Text("Vector Math");
                  ImGui::TableNextColumn(); ImGui::TextColored(app::AccentEmphasisSelected(), "length, normalize, dot, cross, reflect");
                  ImGui::TableNextColumn(); ImGui::Text("Vector norms, dot/cross products, reflection.");

                  ImGui::TableNextColumn(); ImGui::Text("Pure Randomness");
                  ImGui::TableNextColumn(); ImGui::TextColored(app::AccentEmphasisSelected(), "rand(t, seed), noise(t, seed)");
                  ImGui::TableNextColumn(); ImGui::Text("Deterministic, pure functions of time and seed.");

                  ImGui::EndTable();
               }
               ImGui::Spacing();
            }

            // Section 6: Domain Transfer Operators
            if ((sSelectedSection == 0 || sSelectedSection == 6) && MatchesFilter("transfers reduce rms mean resample downsample map broadcast"))
            {
               FormParts::Heading("6. Domain Transfer Operators");
               ImGui::TextWrapped("element, pixel and sample are mutually incomparable domains - they never join implicitly. Every crossing between them goes through frame, either explicitly (reduce/resample/downsample) or via a reduce.");
               ImGui::Spacing();

               ImGui::TextColored(app::AccentEmphasisSelected(), "reduce.<op>(x) - many to one, written explicitly");
               ImGui::BulletText("Ops: reduce.sum, reduce.rms, reduce.mean, reduce.min, reduce.max.");
               ImGui::BulletText("reduce.rms(in, loHz, hiHz) : 3-arg band-limited RMS, Sample domain only (Sample -> Frame).");
               ImGui::BulletText("Sample -> Frame only compiles inside a Field Synth/Effect (sample) kernel - 'in' is a sample-domain name, unreadable from element/pixel.");
               ImGui::BulletText("In the sample domain, reduce.rms(in, loHz, hiHz) is output-only: write it as 'output frame float name = reduce.rms(in, lo, hi)' (max one per kernel). The result cannot be read back into that same kernel's per-sample lines - it is a pin, driven onto another node's param through the modulation matrix.");
               DrawCodeBox("# inside a sample-domain kernel (Field Synth / Field Effect)\noutput frame float bass = reduce.rms(in, 20, 200)   # exposed as a pin, not usable below\nout = in   # per-sample processing is independent of the line above", "cb_reduce");

               ImGui::TextColored(app::AccentEmphasisSelected(), "map(N) { ... } - one kernel invocation per lane, explicit");
               ImGui::TextWrapped("Runs the body N times, once per lane, inside element or pixel kernels (the body domain must be the same or finer than the surrounding one). Cost is N x the body. The lane index inside the body is map_index; N must be a compile-time constant (1-64).");

               ImGui::TextColored(app::AccentEmphasisSelected(), "broadcast - one to many, implicit, NEVER written");
               ImGui::TextWrapped("Coarse \xE2\x86\x92 fine happens automatically via rate inference. Writing broadcast(...) is a compile error.");
               DrawCodeBox("amount = 0.5 + 0.5 * sin(t)   # frame domain\nP.y += amount                  # element domain reads it, no syntax needed", "cb_broadcast");

               ImGui::TextColored(app::AccentEmphasisSelected(), "resample(x, Domain) - read domain A while standing in domain B");
               ImGui::TextWrapped("Domain is a bare identifier: frame, element, pixel or sample. Samples rather than aggregates, so fine \xE2\x86\x92 coarse can alias - prefer reduce.rms for levels/envelopes.");
               DrawCodeBox("level = resample(lfo, sample)   # frame -> sample, held for the block", "cb_resample");

               ImGui::TextColored(app::AccentEmphasisSelected(), "downsample(x, k) - run at a fraction of the ambient rate");
               ImGui::TextWrapped("k must be a compile-time constant integer literal >= 1. A non-constant k is refused at compile time.");
               DrawCodeBox("slow = downsample(lfo, 32)   # evaluates lfo once every 32 samples", "cb_downsample");
            }

            // Section 7: Branching & Control Flow
            if ((sSelectedSection == 0 || sSelectedSection == 7) && MatchesFilter("branching if control flow predication cost"))
            {
               FormParts::Heading("7. Branching & Control Flow");
               ImGui::TextWrapped("Field allows data-dependent branching (unlike Kronos, which forbids it entirely). Every branch has a real cost that depends on the domain it runs in - always know which domain you're in before you branch.");
               DrawCodeBox("if (P.y > 0.5) { Cd = vec3(1, 0, 0) }", "cb_if_stmt");

               if (ImGui::BeginTable("##branchtbl", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg))
               {
                  ImGui::TableSetupColumn("Domain", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                  ImGui::TableSetupColumn("Lowering", ImGuiTableColumnFlags_WidthFixed, 140.0f);
                  ImGui::TableSetupColumn("Cost", ImGuiTableColumnFlags_WidthStretch);
                  ImGui::TableHeadersRow();

                  ImGui::TableNextColumn(); ImGui::Text("frame");
                  ImGui::TableNextColumn(); ImGui::Text("real branch");
                  ImGui::TableNextColumn(); FormParts::Heading("free");

                  ImGui::TableNextColumn(); ImGui::Text("element");
                  ImGui::TableNextColumn(); ImGui::Text("real branch");
                  ImGui::TableNextColumn(); ImGui::Text("breaks vectorization of the batch");

                  ImGui::TableNextColumn(); ImGui::Text("sample");
                  ImGui::TableNextColumn(); ImGui::Text("real branch");
                  ImGui::TableNextColumn(); ImGui::Text("mispredict risk on the audio thread");

                  ImGui::TableNextColumn(); ImGui::Text("pixel");
                  ImGui::TableNextColumn(); ImGui::Text("predication");
                  ImGui::TableNextColumn(); ImGui::TextColored(tok::V4(tok::palf::v_1000_700_400_1000), "always pays the sum of both sides (GPU evaluates both)");

                  ImGui::EndTable();
               }
               ImGui::Spacing();
               ImGui::TextWrapped("The function form if(cond, a, b) also exists (from the '=' parameter-expression language) and always evaluates both branches with no short-circuit - it is not the same construct as the statement form above, but both are valid Field/expression syntax.");
            }

            // Section 8: Canonical Recipes
            if ((sSelectedSection == 0 || sSelectedSection == 8) && MatchesFilter("recipes synth filter ripple sdf resonator glow example templates"))
            {
               FormParts::Heading("8. Canonical Recipes");
               ImGui::TextWrapped("Ready-to-use starting points - copy into a Field node's editor and adjust the param ranges.");
               ImGui::Spacing();

               FormParts::Heading("Audio Synth: Sine Oscillator (Field Synth Node)");
               DrawCodeBox("state float phase = 0\nphase = phase + freq / sr\nphase = phase - floor(phase)\nout = sin(phase * 6.283185) * gate", "cb_recipe_synth");

               FormParts::Heading("Audio Effect: 1-Pole Lowpass Filter (Field Effect Node)");
               DrawCodeBox("param float cutoff = 0.25 [0.01, 0.99]\nstate float z = 0\nz += (in - z) * cutoff\nout = z", "cb_recipe_filter");

               FormParts::Heading("Audio Effect: Expose a Bass Level (Field Effect Node, reduce.rms)");
               ImGui::TextWrapped("reduce.rms(in, lo, hi) is output-only in v1: it must be its own 'output frame float <name> = reduce.rms(in, lo, hi)' declaration (at most one per kernel), and that value cannot be read back into this same kernel's per-sample lines - it can only be exposed as a pin and driven onto another node's param through the modulation matrix. Signal processing on 'in' happens independently, in ordinary per-sample lines.");
               DrawCodeBox("param float boost = 1.5 [0.5, 4.0]\noutput frame float bass = reduce.rms(in, 20.0, 200.0)\nout = in * boost", "cb_recipe_wah");

               FormParts::Heading("Geometry Deformer: Wave Ripple (Field Modifier Node)");
               DrawCodeBox("param float speed = 2.0 [0, 10]\nparam float height = 0.3 [0, 2]\ndist = length(P.xz)\nP.y += sin(dist * 4.0 - t * speed) * height\nCd = vec3(0.5 + 0.5 * sin(P.y * 5.0), 0.4, 0.8)", "cb_recipe_geom");

               FormParts::Heading("Pixel Shader: Radial Glow SDF (Field Pixel Node)");
               DrawCodeBox("param float radius = 0.3 [0.05, 0.8]\np = uv - vec2(0.5, 0.5)\nd = length(p) - radius\nglow = clamp(0.02 / (abs(d) + 0.02), 0.0, 1.0)\ncol = vec3(glow * 0.9, glow * 0.4, glow * 1.0)", "cb_recipe_pixel");

               ImGui::TextWrapped("Want geometry or pixels to react to audio? A single kernel can't read 'in' from an element or pixel domain - drive a param from an audio-analysis node through the modulation matrix instead, and reference the param (e.g. 'height' above) from within the geometry/pixel kernel.");
            }

            // i18n-skip-end
            FormParts::PopReferenceStyle();
            LibraryParts::EndWell();
            ImGui::EndTabItem();
         }

         // 6. Expression Globals Tab
         if (ImGui::BeginTabItem(L("Expression Globals"), nullptr, 0))
         {
            ImGui::Spacing();
            FormParts::PushReferenceStyle();
            FormParts::Section(T("Expression Globals"));
            ImGui::TextDisabled("%s", T("Named values every '=' parameter expression can read."));
            ImGui::TextDisabled("%s", T("Each row sees t and the rows above it:  beat = mod(t * 2, 1) < 0.5"));
            ImGui::TextDisabled("%s", T("Saved as your app-wide default - every new patch starts with this set."));
            if (ImGui::CollapsingHeader(L("Language Reference")))
            {
               // i18n-skip-begin: expression vocabulary
               ImGui::TextDisabled("operators   + - * / %% ^   < <= > >= == !=   && || !   . (swizzle)");
               ImGui::TextDisabled("functions   sin cos tan abs sign sqrt exp log pow");
               ImGui::TextDisabled("            floor ceil round mod min max clamp lerp mix");
               ImGui::TextDisabled("            step(edge,x) smoothstep(e0,e1,x) if(cond,a,b)");
               ImGui::TextDisabled("            rand/noise/sh: f() f(speed) f(min,max) f(min,max,speed,seed)");
               ImGui::TextDisabled("vectors     vec2(x,y) vec3(x,y,z) vec4(x,y,z,w) or splat vec3(1)");
               ImGui::TextDisabled("            swizzles: .xy .xyz .xyzw or .rg .rgb .rgba (e.g. P.xz, Cd.bgr)");
               ImGui::TextDisabled("bound       t = transport seconds, pi");
               ImGui::TextDisabled("in a param  lo / hi = that param's own range, plus its siblings");
               ImGui::TextDisabled("            a sibling of the same name shadows a global");
               // i18n-skip-end
            }

            static std::string sExprSkillStatus;
            if (FormParts::Button(L("Install AI Skill##exprGlobals")))
            {
               const std::string folder = Platform::OpenFolderDialog("Choose folder for the Expression Globals AI skill file");
               if (!folder.empty())
                  sExprSkillStatus = SaveAISkillFile(folder, "infinite-expression-globals.md", AISkillContent::kExpressionGlobalsMarkdown);
            }
            if (ImGui::IsItemHovered())
               HelpTip("%s", T("Save a reference file you can hand to any AI assistant\n(or drop into ~/.claude/skills/) so it can write '=' expressions and Globals for you."));
            if (!sExprSkillStatus.empty())
            {
               ImGui::SameLine();
               ImGui::TextDisabled("%s", sExprSkillStatus.c_str());
            }
            SectionCard::End();
            FormParts::Section(T("Globals"));

            std::vector<ExprGlobals::Global>& globals = ExprGlobals::All();
            int removeAt = -1;
            for (size_t i = 0; i < globals.size(); i++)
            {
               ExprGlobals::Global& g = globals[i];
               ImGui::PushID((int)i);

               char nameBuf[64];
               snprintf(nameBuf, sizeof(nameBuf), "%s", g.name.c_str());
               ImGui::SetNextItemWidth(130.0f);
               FieldWell::PushStyle();
               const bool nameEdited = FieldWell::InputText("##name", nameBuf, sizeof(nameBuf));
               FieldWell::PopStyle();
               if (nameEdited)
               {
                  PushUndoCheckpoint();
                  g.name = nameBuf;
                  gPatchDirty = true;
                  SaveDefaultExprGlobals();
               }

               ImGui::SameLine();
               ImGui::TextUnformatted("=");
               ImGui::SameLine();

               char exprBuf[512];
               snprintf(exprBuf, sizeof(exprBuf), "%s", g.expr.c_str());
               ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 150.0f);
               FieldWell::PushStyle();
               const bool exprEdited = FieldWell::InputText("##expr", exprBuf, sizeof(exprBuf));
               FieldWell::PopStyle();
               if (exprEdited)
               {
                  PushUndoCheckpoint();
                  g.expr = exprBuf;
                  gPatchDirty = true;
                  SaveDefaultExprGlobals();
               }

               // The value readout is live, so its text width changes every
               // frame ("0.5865" vs "-12.3400"), and a plain SameLine() passed
               // that jitter straight down the row - the delete button visibly
               // slid left and right while the globals evaluated. Both columns
               // are pinned to fixed x instead: the number is right-aligned
               // against a fixed edge (digits grow leftwards, the way any
               // numeric column should) and the button sits at a constant x,
               // so nothing downstream of a variable-width string can move.
               const float btnW = ImGui::GetFrameHeight();
               const float btnX = ImGui::GetWindowContentRegionMax().x - btnW;
               const float valueRightX = btnX - ImGui::GetStyle().ItemSpacing.x;

               char valueText[32];
               snprintf(valueText, sizeof(valueText), "%.4f", g.value);
               ImGui::SameLine();
               ImGui::SetCursorPosX(valueRightX - ImGui::CalcTextSize(valueText).x);
               ImGui::TextDisabled("%s", valueText);
               ImGui::SameLine();
               ImGui::SetCursorPosX(btnX);
               {
                  if (ActionButton::Draw("##removeexprglobal", ImVec2(btnW, 0)))
                     removeAt = (int)i;
                  ImDrawList* dl = ImGui::GetWindowDrawList();
                  const ImVec2 bmin = ImGui::GetItemRectMin();
                  const ImVec2 bmax = ImGui::GetItemRectMax();
                  const ImVec2 center((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
                  const float iconSize = (bmax.y - bmin.y) * 0.65f;
                  const ImU32 col = ImGui::IsItemHovered() ? tok::U32(tok::pal::c_E63C3CFF) : ImGui::GetColorU32(ImGuiCol_TextDisabled);
                  glyph::DrawX(dl, center, iconSize, col);
               }

               if (!g.error.empty())
                  ImGui::TextColored(tok::V4(tok::palf::v_1000_550_500_1000), "   %s", g.error.c_str());

               ImGui::PopID();
            }

            if (removeAt >= 0)
            {
               PushUndoCheckpoint();
               globals.erase(globals.begin() + removeAt);
               gPatchDirty = true;
               SaveDefaultExprGlobals();
            }

            ImGui::Spacing();
            if (FormParts::Button(L("Add global"), ImVec2(110, 0)))
            {
               PushUndoCheckpoint();
               char name[32];
               snprintf(name, sizeof(name), "g%d", (int)globals.size() + 1);
               globals.push_back({ name, "0", 0.0f, std::string() });
               gPatchDirty = true;
               SaveDefaultExprGlobals();
            }
            ImGui::SameLine();
            if (FormParts::Button(L("Presets..."), ImVec2(110, 0)))
            {
               ImGui::OpenPopup("ExprPresetsMenuSettings");
            }

            {
               // Anchored under the Presets button, fixed width, scrolls past 320 pt: never covers the globals list.
               const ImVec2 bmin = ImGui::GetItemRectMin();
               ImGui::SetNextWindowPos(ImVec2(bmin.x, ImGui::GetItemRectMax().y + tok::space_1));
               const ImGuiViewport* vp = ImGui::GetMainViewport();
               const float room = vp->Pos.y + vp->Size.y - ImGui::GetItemRectMax().y - 16.0f;
               ImGui::SetNextWindowSizeConstraints(ImVec2(400.0f, 0.0f), ImVec2(400.0f, std::clamp(room, 160.0f, 320.0f)));
            }
            if (MenuParts::BeginPopup("ExprPresetsMenuSettings"))
            {
               constexpr float kNameW = 110.0f;   // name column; the formula follows, dimmed and clipped
               std::string currentCategory;
               for (const ExprGlobals::Preset& p : ExprGlobals::Presets())
               {
                  if (p.category != currentCategory)
                  {
                     if (!currentCategory.empty())
                        MenuParts::Separator();
                     currentCategory = p.category;
                     ImGui::TextDisabled("%s", currentCategory.c_str());
                  }

                  char itemId[256];
                  snprintf(itemId, sizeof(itemId), "##preset_%s", p.name.c_str());
                  const bool picked = ImGui::Selectable(itemId, false);
                  MenuParts::Wash(ImGui::IsItemHovered(), ImGui::IsItemActive(), false);
                  {
                     ImDrawList* dl = ImGui::GetWindowDrawList();
                     const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
                     const float ty = std::floor((mn.y + mx.y - ImGui::GetTextLineHeight()) * 0.5f);
                     const ImVec4 tc = ImGui::GetStyleColorVec4(ImGuiCol_Text);
                     const float right = mx.x - MenuParts::kTextInset;
                     dl->AddText(ImVec2(mn.x + 4.0f, ty), ImGui::GetColorU32(tc), p.name.c_str());
                     dl->PushClipRect(ImVec2(mn.x + kNameW, mn.y), ImVec2(right, mx.y), true);
                     dl->AddText(ImVec2(mn.x + kNameW, ty), ImGui::GetColorU32(ImVec4(tc.x, tc.y, tc.z, 0.5f)), p.expr.c_str());
                     dl->PopClipRect();
                  }
                  if (picked)
                  {
                     PushUndoCheckpoint();
                     bool found = false;
                     for (ExprGlobals::Global& g : globals)
                     {
                        if (g.name == p.name)
                        {
                           g.expr = p.expr;
                           found = true;
                           break;
                        }
                     }
                     if (!found)
                     {
                        globals.push_back({ p.name, p.expr, 0.0f, std::string() });
                     }
                     gPatchDirty = true;
                     SaveDefaultExprGlobals();
                  }
                  if (ImGui::IsItemHovered())
                  {
                     ImGui::SetTooltip(T("%s\nFormula: %s"), p.description.c_str(), p.expr.c_str());
                  }
               }
               MenuParts::EndPopup();
            }

            FormParts::PopReferenceStyle();
            SectionCard::End();
            ImGui::EndTabItem();
         }

         ImGui::EndTabBar();
         FormParts::PopTabStyle();
      }

      SectionCard::EndWindow();
      FormParts::WindowEdge();
      ImGui::End();
      PopElevatedPanelStyle();
   }


   // Rebuilds the live graph from a snapshot - shared by LoadPatchFrom (from
   // disk) and Undo/Redo (from the in-memory stacks). Callers decide what
   // happens to gPatchPath/gUndoStack/gRedoStack afterwards; a loaded file is
   // a new document boundary, an undo is not.
   void NoteGraphEditedForLiveIssues();
}
