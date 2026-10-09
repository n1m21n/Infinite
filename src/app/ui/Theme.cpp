// Theme: colours, scope palette, panel/button/dropdown styles, ApplyTheme (moved verbatim from main.cpp).
#include "app/AppShared.h"
#include "app/ui/design/components/ActionButton.h"
#include "app/ui/design/TokenColors.h"

namespace app
{
   // Audio node compact grid (docs/plans/audio/audio-node-ui-system.md §1):
   // half of kParamWidth minus a 4px gutter, so two of these plus
   // ImGui::SameLine's default spacing sit inside the same node body width
   // a full-width ModSlider already uses.
   const float kCompactParamWidth = 82.0f;

   const float kAudioHalfWidth = 214.0f;

   // Mixer needs a wider body than the standard 440: with 8 strips, cellW =
   // gAudioContentW / MixerNode::kSlots must leave room for both a 56px knob
   // and its modulation pin's left-margin gutter (ModKnob needs
   // (cell - knob) / 2 >= 20px). Solving (cell - 56) / 2 >= 20 => cell >= 96
   // => body >= 768. 800 is the smallest round number above that floor -
   // don't shrink this back toward 440, the pin-clamp bug it fixes re-engages
   // below 768.
   const float kAudioMixerWidth = 800.0f;
 // generous click target - small dots were unhittable

   bool IsThemeLight()
   {
      const CategoryColors::UiTheme& t = CategoryColors::CurrentUiTheme();
      return (0.2126f * t.windowBg.r + 0.7152f * t.windowBg.g + 0.0722f * t.windowBg.b > 0.5f);
   }


   ImU32 ScopeBgCol()
   {
      return tok::U32(tok::scope_bg, IsThemeLight());
   }


   ImU32 ScopeBorderCol()
   {
      return tok::U32(tok::scope_border, IsThemeLight());
   }


   ImU32 ScopeGridCol()
   {
      return tok::U32(tok::scope_grid, IsThemeLight());
   }


   ImU32 ScopeMidLineCol()
   {
      return tok::U32(tok::scope_midline, IsThemeLight());
   }


   ImU32 ScopeTextCol()
   {
      return tok::U32(tok::scope_text, IsThemeLight());
   }
 // settings file written once the drag ends

   void DrawCheckerboardBackdrop(ImDrawList* dl, ImVec2 origin, ImVec2 br, float rounding)
   {
      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, tok::U32(tok::backdrop_base, isLight), rounding);
      if (!gCheckerboardBackdrop)
         return;
      const float cell = 12.0f;
      const int cols = (int)std::ceil((br.x - origin.x) / cell);
      const int rows = (int)std::ceil((br.y - origin.y) / cell);
      for (int y = 0; y < rows; y++)
      {
         for (int x = 0; x < cols; x++)
         {
            if ((x + y) % 2)
               continue;
            const ImVec2 tl(origin.x + x * cell, origin.y + y * cell);
            const ImVec2 cbr(std::min(br.x, tl.x + cell), std::min(br.y, tl.y + cell));
            dl->AddRectFilled(tl, cbr, tok::U32(tok::backdrop_check, isLight));
         }
      }
   }


   void DrawCheckerboardBackdrop(ImDrawList* dl, ImVec2 origin, float size, float rounding)
   {
      DrawCheckerboardBackdrop(dl, origin, ImVec2(origin.x + size, origin.y + size), rounding);
   }


   bool TextFocusClaimed();


   // ---- The one selection/emphasis ladder ----
   //
   // Pre-composites the theme accent over the theme panel background and
   // returns an OPAQUE color. Every "selected / hovered / active" surface in
   // the app used to be the accent at some *alpha* over whatever happened to
   // sit behind it - nine different alphas across ApplyTheme alone, each
   // compositing against a different backdrop - which is why a selected
   // Settings tab, the browser panel's selected mode tab, a selected list row
   // and a segmented-control's active segment all read as unrelated colors
   // despite tracing back to one accent. Compositing here instead means a
   // given emphasis level is the exact same RGB everywhere it appears.
   //
   // Three levels, one ladder, used identically by ApplyTheme (Header*/Tab*)
   // and by every hand-drawn segmented/toggle control in a node body:
   //   hover    - transient, the lightest touch
   //   selected - persistent "you are here"
   //   pressed  - momentary, the strongest
   // Full-strength accent stays reserved for genuinely different things: the
   // filled primary action (PushPrimaryButtonStyle below), check marks,
   // slider grabs, drag-drop and nav highlights.
   //
   // Never hand-roll a "selected" blue/purple at a call site - it will not
   // track the theme preset, and the app has ten of them.
   inline ImVec4 AccentTint(float amount)
   {
      const CategoryColors::UiTheme& t = CategoryColors::CurrentUiTheme();
      return ImVec4(t.panelBg.r + (t.accent.r - t.panelBg.r) * amount,
                    t.panelBg.g + (t.accent.g - t.panelBg.g) * amount,
                    t.panelBg.b + (t.accent.b - t.panelBg.b) * amount, 1.0f);
   }


   // One set of numbers for both polarities. The earlier light/dark split
   // (0.34 vs 0.42 for "selected") was an attempt to equalise *perceived*
   // strength, but it meant a selected row was a different colour depending
   // on the theme - the exact class of drift this ladder exists to end - and
   // both values landed too weak to read as a selection at a glance. Selected
   // is 0.60 accent, everywhere, in every theme; hover and pressed bracket it.
   ImVec4 AccentEmphasisHover() { return AccentTint(tok::accent_hover); }

   ImVec4 AccentEmphasisSelected() { return AccentTint(tok::accent_selected); }

   ImVec4 AccentEmphasisPressed() { return AccentTint(tok::accent_pressed); }

   // A button that is already on keeps its accent when hovered: same hue, ~4% brighter (a hint of life, no colour
   // change). Press darkens; release returns to the selected look. Pair with PopSelectedButtonColors.
   void PushSelectedButtonColors() { ActionButton::Scoped() = ActionButton::Kind::Selected; }
   void PopSelectedButtonColors() { ActionButton::Scoped() = ActionButton::Kind::Plain; }


   // Shared "this is the recommended action" emphasis for a modal dialog's
   // button row - the app's accent color on exactly one button, matching
   // the platform convention of a single filled default action among plain
   // ones. Never hand-roll a one-off accent color at a call site (same
   // reasoning as PushDropdownStyle/PushCheckboxStyle below); this is the
   // one place it's defined. Equally valid in light and dark: it always
   // reads against the theme's own accent, never a fixed literal.
   void PushPrimaryButtonStyle()
   {
      const CategoryColors::UiTheme& t = CategoryColors::CurrentUiTheme();
      const ImVec4 accent(t.accent.r, t.accent.g, t.accent.b, 1.0f);
      ImGui::PushStyleColor(ImGuiCol_Button, accent);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                             ImVec4(std::min(accent.x * 1.1f, 1.0f), std::min(accent.y * 1.1f, 1.0f),
                                    std::min(accent.z * 1.1f, 1.0f), 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                             ImVec4(accent.x * 0.82f, accent.y * 0.82f, accent.z * 0.82f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_Text, tok::V4(tok::palf::v_1000_1000_1000_1000));
      ActionButton::Scoped() = ActionButton::Kind::Primary;
   }


   void PopPrimaryButtonStyle()
   {
      ActionButton::Scoped() = ActionButton::Kind::Plain;
      ImGui::PopStyleColor(4);
   }


   void PushDropdownStyle()
   {
      const bool isLight = IsThemeLight();
      // The fill sits within ~0.06 luminance of the node body (a
      // category-tinted mix of panelBg, see DrawNodes' NodeBg push) - a
      // recess, not a chip. No frame stroke in either theme now - the
      // caption text is what carries the control's identity, at near-full
      // contrast, not the frame; light mode used to keep a hairline border
      // here (P10 in .claude/skills/node-ui-pillars/SKILL.md reasoned the
      // brighter panel needed a real edge), but that was the one dropdown
      // border left standing after every other border in the app was
      // deleted rather than recolored - same fix applies here.
      ImGui::PushStyleColor(ImGuiCol_Button, tok::V4(tok::dropdown_fill, isLight));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tok::V4(tok::dropdown_hover, isLight));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, tok::V4(tok::dropdown_active, isLight));
      ImGui::PushStyleColor(ImGuiCol_Text, tok::V4(tok::dropdown_text, isLight));
      ImGui::PushStyleColor(ImGuiCol_Border, tok::V4(tok::palf::v_0_0_0_0));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
   }


   void PopDropdownStyle()
   {
      ImGui::PopStyleVar(2);
      ImGui::PopStyleColor(5);
   }


   // Subtle "glass" elevation for a panel that floats above the main node
   // canvas (Settings, Formula/Field editors, the docked Modulation and
   // Performance matrices) - see .claude/skills/apple-design-skill's
   // materials.md: a material establishes hierarchy by letting the panel
   // read as a distinct raised layer, not by faking a blur ImGui can't do.
   // The tint is the theme's own panelBg (already one step up from
   // windowBg/canvas in every preset) at near-full alpha, plus a border
   // that carries a faint top highlight rather than a flat, even ring - the
   // highlight is what reads as "catching light from above" without any
   // gradient trickery. Kept subtle in both themes per P10: this is a
   // professional tool, not a mobile card stack.
   // `isChild` selects ImGuiCol_ChildBg (for a BeginChild-based docked
   // panel) vs. ImGuiCol_WindowBg (for a plain ImGui::Begin floating
   // window); both panel families get the identical tint/border recipe so
   // they read as one consistent elevation language app-wide.
   void PushElevatedPanelStyle(bool isChild)
   {
      const CategoryColors::UiTheme& t = CategoryColors::CurrentUiTheme();
      const bool isLight = IsThemeLight();
      const ImVec4 bg = isLight ? ImVec4(t.panelBg.r, t.panelBg.g, t.panelBg.b, 0.99f)
                                 : ImVec4(t.panelBg.r, t.panelBg.g, t.panelBg.b, 0.97f);
      ImGui::PushStyleColor(isChild ? ImGuiCol_ChildBg : ImGuiCol_WindowBg, bg);
      // No border at all, in either theme: the previous white-tinted "catching
      // light" border was the actual source of the corner-bleed artifact users
      // saw around Settings/search-popup/color-picker dialogs (a 1px near-white
      // line at 0.55/0.05 alpha still rasterizes as a visible highlight on
      // rounded corners). The BorderShadow alone gives these dialogs enough
      // depth to read as "above" the canvas without needing an edge line.
      // Border size is zeroed here too (not just the colour) so no outline is
      // rasterized at all - a transparent 1px border still antialiases against
      // the rounded corner and was itself the corner-bleed users saw.
      ImGui::PushStyleColor(ImGuiCol_Border, tok::V4(tok::palf::v_0_0_0_0));
      ImGui::PushStyleColor(ImGuiCol_BorderShadow, ImVec4(0.0f, 0.0f, 0.0f, isLight ? 0.06f : 0.22f));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
      // Top-level floating windows (Settings, Formula/Field-*-editor) use the
      // default WindowRounding (4.0f), which reads as flat/square next to
      // this same HIG pass's PopupRounding (12.0f) - a popup and an elevated
      // window are both "material above the canvas" and should read as the
      // same shape language. Only the isChild==false path gets the bump;
      // docked child panels (Modulation/Performance matrices, node browser)
      // are governed by ImGuiStyleVar_ChildRounding, not WindowRounding, so
      // pushing this unconditionally is harmless for them - kept
      // unconditional (rather than gated on isChild) so the push/pop count
      // here stays a fixed constant regardless of which path runs.
      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, isChild ? ImGui::GetStyle().WindowRounding : 12.0f);
   }


   void PopElevatedPanelStyle()
   {
      ImGui::PopStyleVar(3);
      ImGui::PopStyleColor(3);
   }


   // PushElevatedPanelStyle's white-tinted "catching light" border reads as
   // a subtle accent on a small, fully-enclosed floating dialog (Settings, a
   // color picker) - but a docked panel (node browser, viewport panel,
   // Modulation/Performance matrices) has one long edge butting straight up
   // against the canvas or another docked panel, where that same white tint
   // becomes a persistent bright seam instead of a highlight, and fights the
   // docked panels the identical opaque panelBg fill (so they never fall
   // through to the transparent-ChildBg/backbuffer bug) and NO edge of any
   // kind - no border, no shadow. Docked panels butt straight up against the
   // canvas or against each other, and every edge treatment tried on that
   // seam (white tint, then the theme's t.border) was reported as a visible
   // bar between viewports. A panel is distinguished by its panelBg fill
   // against the canvas' windowBg; the edge was never carrying information
   // the fill wasn't. Tune this and PushElevatedPanelStyle independently;
   // do not merge them back into one function (see node-ui-pillars P10 and
   // the "codebase-navigation" note on this split).
   void PushDockedPanelStyle(bool isChild)
   {
      const CategoryColors::UiTheme& t = CategoryColors::CurrentUiTheme();
      const bool isLight = IsThemeLight();
      const ImVec4 bg = isLight ? ImVec4(t.panelBg.r, t.panelBg.g, t.panelBg.b, 0.99f)
                                 : ImVec4(t.panelBg.r, t.panelBg.g, t.panelBg.b, 0.97f);
      ImGui::PushStyleColor(isChild ? ImGuiCol_ChildBg : ImGuiCol_WindowBg, bg);
      ImGui::PushStyleColor(ImGuiCol_Border, tok::V4(tok::palf::v_0_0_0_0));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
   }


   void PopDockedPanelStyle()
   {
      ImGui::PopStyleVar(2);
      ImGui::PopStyleColor(2);
   }


   // ---- The one panel seam ----
   //
   // What separates two adjacent panes, after several rounds of getting this
   // wrong, is the same thing AppKit uses between the panes of a split view:
   // a single 1pt hairline in one separator colour, drawn at every boundary
   // and nowhere else. Not a band, not a gap, not a shadow, and above all not
   // a different treatment per boundary - the complaint that the separators
   // "aren't uniform" was true because each dock edge was arriving at its
   // divider by a different route (a hand-picked AddLine here, an unpainted
   // child there, ImGui's item spacing somewhere else).
   //
   // The colour is derived from windowBg per polarity rather than picked, so
   // it is a low-contrast edge in both themes by construction: light themes
   // step windowBg down toward black, dark themes step it up toward white.
   // It cannot come out black-on-light or white-on-dark, which is the exact
   // failure every hand-chosen divider colour in this file has produced.
   ImU32 PanelSeamColor()
   {
      const CategoryColors::UiTheme& t = CategoryColors::CurrentUiTheme();
      const bool isLight = IsThemeLight();
      auto step = [isLight](float c) {
         return isLight ? c * 0.86f : c + (1.0f - c) * 0.10f;
      };
      return ImGui::GetColorU32(ImVec4(step(t.windowBg.r), step(t.windowBg.g), step(t.windowBg.b), 1.0f));
   }


   // Draws that hairline along the just-submitted item's canvas-facing edge.
   // Call it straight after a docked panel's resize-grip InvisibleButton: the
   // grip always sits on the edge that faces the canvas, so the grip's own
   // rect is already the boundary and no panel has to know its own geometry.
   // `vertical` - the grip is a column (left/right dock) rather than a row.
   // `facesStart` - the canvas is above / to the left, so the seam is the
   // grip's top / left edge; otherwise its bottom / right.
   //
   // The 0.5 offset puts the 1px stroke inside the panel instead of straddling
   // the boundary, so it is never half-covered by whatever is drawn next to it.
   void DrawPanelSeam(bool vertical, bool facesStart)
   {
      const ImVec2 a = ImGui::GetItemRectMin();
      const ImVec2 b = ImGui::GetItemRectMax();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImU32 col = PanelSeamColor();
      if (vertical)
      {
         const float x = facesStart ? a.x + 0.5f : b.x - 0.5f;
         dl->AddLine(ImVec2(x, a.y), ImVec2(x, b.y), col, 1.0f);
      }
      else
      {
         const float y = facesStart ? a.y + 0.5f : b.y - 0.5f;
         dl->AddLine(ImVec2(a.x, y), ImVec2(b.x, y), col, 1.0f);
      }
   }


   // Pins and mod dots are drawn small but hit-tested at >= 20 pt (R572). The
   // rect grows up/down and away from the control (left), never rightwards:
   // the widget the pin belongs to starts a few px past the box, and a rect
   // overlapping it would turn the widget's first pixels into a link drag.
   // Call between ed::BeginPin and ed::EndPin.
   void ExpandPinHit(const ImVec2& c, float boxRight)
   {
      const float kMin = 20.0f;
      ed::PinRect(ImVec2(std::min(c.x - kMin * 0.5f, boxRight - kMin), c.y - kMin * 0.5f),
                  ImVec2(boxRight, c.y + kMin * 0.5f));
   }


   // Restyles ImGui's whole palette plus the node-editor canvas from the
   // active CategoryColors preset, so switching presets re-themes the app
   // rather than just the graph. Mutates ImGuiStyle/ed::Style directly
   // (like StyleColorsDark() itself), not a push/pop - it's meant to stick
   // until the user picks a different preset. Node header/border tinting
   // is separate, applied per-node from the same preset's category table.
   void ApplyTheme()
   {
      const CategoryColors::UiTheme& t = CategoryColors::CurrentUiTheme();
      auto vec = [](const CategoryColors::Color& c, float a = 1.0f) {
         return ImVec4(c.r, c.g, c.b, a);
      };
      const float lum = 0.2126f * t.windowBg.r + 0.7152f * t.windowBg.g + 0.0722f * t.windowBg.b;
      const bool isLight = (lum > 0.5f);

      auto shade = [isLight](const CategoryColors::Color& c, float amt) {
         if (isLight)
            return ImVec4(c.r * (1.0f - amt * 0.45f), c.g * (1.0f - amt * 0.45f),
                          c.b * (1.0f - amt * 0.45f), 1.0f);
         else
            return ImVec4(c.r + (1.0f - c.r) * amt, c.g + (1.0f - c.g) * amt,
                          c.b + (1.0f - c.b) * amt, 1.0f);
      };

      // ---- The one selection/emphasis ladder ----
      //
      // Every "this is selected / hovered / active" surface in the app used to
      // be the accent at some *alpha* over whatever happened to sit behind it:
      // Header at 0.20, HeaderHovered 0.35, HeaderActive 0.50, TabHovered
      // 0.35, TabActive 0.50, and a fully-opaque accent on primary buttons.
      // Six different alphas, each compositing against a different backdrop
      // (panelBg here, windowBg there, a table row somewhere else), which is
      // why the Settings tab, the browser panel's Modules tab, the search
      // panel's selected row and the Field editor's Apply button all read as
      // four unrelated purples even though one accent feeds them all.
      //
      // Defined once, next to PushPrimaryButtonStyle - see the long note
      // there. Used from here for list rows and tabs, and from node bodies
      // for hand-drawn segmented controls, so all three stay identical.
      const ImVec4 kEmphasisHover = AccentEmphasisHover();
      const ImVec4 kEmphasisSelected = AccentEmphasisSelected();
      const ImVec4 kEmphasisPressed = AccentEmphasisPressed();

      ImGuiStyle& style = ImGui::GetStyle();
      style.Colors[ImGuiCol_Text] = vec(t.text);
      style.Colors[ImGuiCol_TextDisabled] = vec(t.textDim);
      style.Colors[ImGuiCol_WindowBg] = vec(t.windowBg);
      style.Colors[ImGuiCol_ChildBg] = vec(t.panelBg, 0.0f);
      style.Colors[ImGuiCol_PopupBg] = vec(t.panelBg, 0.98f);
      style.Colors[ImGuiCol_Border] = vec(t.border);
      style.Colors[ImGuiCol_FrameBg] = shade(t.panelBg, 0.08f);
      style.Colors[ImGuiCol_FrameBgHovered] = shade(t.panelBg, 0.14f);
      style.Colors[ImGuiCol_FrameBgActive] = shade(t.panelBg, 0.24f);
      style.Colors[ImGuiCol_TitleBg] = vec(t.windowBg);
      style.Colors[ImGuiCol_TitleBgActive] = vec(t.windowBg);
      style.Colors[ImGuiCol_TitleBgCollapsed] = vec(t.windowBg, 0.75f);
      // Was t.panelBg, which reads as a hard horizontal seam against the
      // node-editor canvas immediately below it: the canvas' own background
      // (ed::StyleColor_Bg below) resolves to windowBg once its alpha is
      // composited over this window's own WindowBg fill, so any color here
      // other than windowBg is a visible color-boundary line under the menu
      // bar in every theme (panelBg and windowBg are never equal - see the
      // preset table in CategoryColors.cpp). Match windowBg so the menu bar
      // and canvas read as one continuous surface, the same fix already
      // applied to the seam above bottom-docked panels.
      style.Colors[ImGuiCol_MenuBarBg] = vec(t.windowBg);
      style.Colors[ImGuiCol_ScrollbarBg] = vec(t.windowBg);
      // Quiet text-tinted pill that deepens on hover/drag; the track stays flat.
      style.Colors[ImGuiCol_ScrollbarGrab] = vec(t.text, 0.22f);
      style.Colors[ImGuiCol_ScrollbarGrabHovered] = vec(t.text, 0.40f);
      style.Colors[ImGuiCol_ScrollbarGrabActive] = vec(t.text, 0.55f);
      style.Colors[ImGuiCol_CheckMark] = vec(t.accent);
      style.Colors[ImGuiCol_SliderGrab] = vec(t.accent, 0.85f);
      style.Colors[ImGuiCol_SliderGrabActive] = vec(t.accent);
      // Idle Button was pinned to the exact same color as the panel it sits
      // on (panelBg, no offset) - a button was distinguishable from plain
      // text only once hovered, so buttons like "Show all cables" or
      // "Add global" looked like static labels until the mouse found them.
      // Give it the same resting recess as FrameBg (shade 0.08) so it has a
      // visible fill at rest, a step below Hovered's 0.18.
      style.Colors[ImGuiCol_Button] = shade(t.panelBg, 0.08f);
      style.Colors[ImGuiCol_ButtonHovered] = shade(t.panelBg, 0.18f);
      style.Colors[ImGuiCol_ButtonActive] = isLight ? shade(t.panelBg, 0.32f) : vec(t.accent, 0.65f);
      style.Colors[ImGuiCol_Header] = kEmphasisSelected;
      style.Colors[ImGuiCol_HeaderHovered] = kEmphasisHover;
      style.Colors[ImGuiCol_HeaderActive] = kEmphasisPressed;
      style.Colors[ImGuiCol_Separator] = vec(t.border);
      style.Colors[ImGuiCol_SeparatorHovered] = vec(t.accent, 0.6f);
      style.Colors[ImGuiCol_SeparatorActive] = vec(t.accent);
      style.Colors[ImGuiCol_ResizeGrip] = vec(t.border, 0.4f);
      style.Colors[ImGuiCol_ResizeGripHovered] = vec(t.accent, 0.6f);
      style.Colors[ImGuiCol_ResizeGripActive] = vec(t.accent);
      // Tabs and browser-panel mode rows are the same affordance wearing two
      // widget types, so they share the ladder exactly: unselected panelBg,
      // then hovered, then selected. Now literally the same constants as
      // Header/HeaderHovered/
      // HeaderActive above - the previous attempt at this used the *hovered*
      // and *active* alpha tiers for tabs (0.35/0.50) but the lower *idle*
      // tier for a selected browser row (0.20), so a selected Settings tab
      // always rendered at roughly double the strength of a selected browser
      // tab despite the comment claiming they matched.
      style.Colors[ImGuiCol_Tab] = vec(t.panelBg);
      style.Colors[ImGuiCol_TabHovered] = kEmphasisHover;
      style.Colors[ImGuiCol_TabActive] = kEmphasisSelected;
      style.Colors[ImGuiCol_TabUnfocused] = vec(t.windowBg);
      style.Colors[ImGuiCol_TabUnfocusedActive] = vec(t.panelBg);
      // ImGui's own tab bar draws a 1px full-width bar under EVERY tab strip
      // every frame, tinted with TabActive/TabUnfocusedActive regardless of
      // which tab is selected (imgui_widgets.cpp TabBarLayout) - it reads as
      // a leftover underline that "never clears" when a Settings tab already
      // opens with its own SeparatorText just below. Kill it app-wide; the
      // tab fill colour above already carries the active/hover distinction.
      style.TabBarBorderSize = 0.0f;
      // No border line on any floating window or popup, in either theme.
      // Every "black/white edge around the dialog box" report so far has been
      // this 1px outline: whatever colour it is given, it is wrong against one
      // of the two backgrounds it straddles (the dialog's own fill on one side,
      // whatever the dialog happens to be floating over on the other), so it
      // has to be re-tuned every time either theme moves. A dialog is already
      // distinguished by its opaque panelBg fill sitting on a different
      // windowBg, plus its rounding and shadow - the outline was carrying no
      // information the fill wasn't already carrying. Deleting the element is
      // the only fix that cannot regress; docked panels push their own
      // border size explicitly (PushDockedPanelStyle) and are unaffected.
      style.WindowBorderSize = 0.0f;
      style.PopupBorderSize = 0.0f;
      // Why a *third* border size is needed to finish the job: a top-level
      // menu popup is a Popup and takes PopupBorderSize, but a SUB-menu
      // opened from inside one is flagged ImGuiWindowFlags_ChildWindow
      // (imgui_widgets.cpp BeginMenuEx) and so takes ChildBorderSize instead
      // (imgui.cpp Begin, "LOCK BORDER SIZE"). That is why Menu > "Modulation
      // matrix" and Menu > "Performance Matrix" still drew an outline after
      // the dialog/popup borders were removed - they were never going through
      // PopupBorderSize at all. BeginChild zeroes ChildBorderSize itself
      // unless ImGuiChildFlags_Borders is passed, so this only reaches
      // submenus and the handful of deliberately-bordered children, which
      // push their own size back locally (the Field reference code boxes).
      style.ChildBorderSize = 0.0f;
      style.Colors[ImGuiCol_TextSelectedBg] = vec(t.accent, 0.35f);
      style.Colors[ImGuiCol_DragDropTarget] = vec(t.accent);
      style.Colors[ImGuiCol_NavHighlight] = vec(t.accent);
      // Table colors default to ImGui's built-in dark style and were never
      // themed - harmless in the default dark "Infinite" preset but a
      // near-black header on light presets (e.g. GitHub Light), found while
      // verifying the Modulation Matrix table in light mode. Tie them to the
      // theme like every other chrome color above.
      style.Colors[ImGuiCol_TableHeaderBg] = shade(t.panelBg, 0.16f);
      style.Colors[ImGuiCol_TableBorderStrong] = vec(t.border);
      style.Colors[ImGuiCol_TableBorderLight] = vec(t.border, 0.5f);
      style.Colors[ImGuiCol_TableRowBg] = vec(t.windowBg, 0.0f);
      style.Colors[ImGuiCol_TableRowBgAlt] = vec(t.text, isLight ? 0.03f : 0.04f);

      // The node-graph canvas itself is drawn by imgui-node-editor from its
      // own style table, not ImGui's - Bg/Grid are the two slots visible
      // behind every node regardless of category. ed::GetStyle() reads
      // through the *current* editor context, which is null here when this
      // runs from the menu bar (that frame's ed::SetCurrentEditor(gEditor)
      // happens later, after the menu bar) - set/restore it explicitly
      // rather than relying on caller state, since the startup call and the
      // menu-bar call have different current-editor state at the time.
      ed::EditorContext* prevEditor = ed::GetCurrentEditor();
      ed::SetCurrentEditor(gEditor);
      ed::Style& edStyle = ed::GetStyle();
      edStyle.Colors[ed::StyleColor_Bg] = vec(t.windowBg, isLight ? 1.0f : 0.784f);
      edStyle.Colors[ed::StyleColor_Grid] = vec(t.border, !gShowCanvasGrid ? 0.0f : (isLight ? 0.16f : 0.12f));
      edStyle.Colors[ed::StyleColor_NodeBg] = vec(t.panelBg, isLight ? 0.95f : 0.80f);
      edStyle.Colors[ed::StyleColor_NodeBorder] = vec(t.border, isLight ? 0.70f : 0.40f);
      edStyle.NodeRounding = CategoryColors::GetNodeRounding();
      edStyle.GroupRounding = CategoryColors::GetNodeRounding() * 0.5f;
      edStyle.GridSpacing = gGridSnap;
      ed::SetCurrentEditor(prevEditor);
   }
}
