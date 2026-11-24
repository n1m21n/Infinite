// Split out of main(): see docs/plans/main-split/README.md (Block C)
#include "app/ui/design/components/ReorderDrag.h"
#include "app/graph/NodeParams.h"
#include "app/ui/design/components/Thumb.h"
#include "app/ui/design/UiInteract.h"
#include "app/ui/design/components/Tip.h"
#include "app/panels/CurveEditorPanel.h"
#include "app/panels/FieldPatcherPanel.h"
#include "app/ui/design/components/StatusDot.h"
#include "app/ui/design/GlyphDraw.h"
#include "app/ui/design/components/IconTile.h"
#include "app/ui/design/components/Readout.h"
#include "app/ui/design/components/TopBarParts.h"
#include "app/ui/design/components/MenuParts.h"
#include "app/ui/design/components/ChipButton.h"
#include "app/ui/design/components/Divider.h"
#include "app/ui/design/components/PanelRail.h"
#include "app/ui/design/components/TransportDisplay.h"
#include "app/ui/design/components/FieldWell.h"
#include "app/ui/design/TokenColors.h"
#include "app/frame/FrameCtx.h"
#include "plugin/engine/HostAudio.h"
#include "plugin/engine/PluginView.h"
#include "app/ui/design/BrandLogo.gen.h"
#include "platform/AppPaths.h"
#include "app/ui/design/UiFmt.h"

namespace app
{
// The rail of panel toggles down the right edge. Library leads on the top bar's centre line and the panels follow,
// one tile apart; at the bottom, Settings in the corner, then Edit history, then the Terminal.
std::string gRailOrder; // saved tile order

static void DrawPanelRail()
{
   PanelRail::Begin(ImGui::GetMainViewport());
   const float step = tok::control_h + tok::space_3;
   const float top = (tok::bar_h - tok::control_h) * 0.5f + step; // Library starts level with the Viewport tile
   struct Entry
   {
      const char* key;
      const char* id;
      const char* glyphOff;
      const char* glyphOn;
      bool* open;
      const char* tip;
   };
   static const Entry kEntries[] = {
      { "library", "##railLibrary", IconsInfinite::Library, IconsInfinite::LibraryFill, &gNodePanelOpen,
        T("Library - search modules, samples, media and plugins") },
      { "viewport", "##railViewport", IconsInfinite::Viewport, IconsInfinite::ViewportFill, &gViewportPanelOpen, T("Viewport panel") },
      { "modmatrix", "##railModMatrix", IconsInfinite::GridDots, nullptr, &gModMatrixOpen, T("Modulation matrix") },
      { "perf", "##railPerf", IconsInfinite::Perform, nullptr, &gPerfPanelOpen, T("Performance mode") },
      { "mixer", "##railMixer", IconsInfinite::Mixer, nullptr, &gMixerPanelOpen, T("Mixer - every Mixer node as a console (Shift+L)") },
      { "arrange", "##railArrange", IconsInfinite::Cube, nullptr, &gArrangePanelOpen, T("Arrangement timeline") },
      { "session", "##railSession", IconsInfinite::Session, nullptr, &gSessionPanelOpen, T("Session - launch clips by scene") },
      { "studio", "##railStudio", IconsInfinite::Waveform, nullptr, &gSampleStudioOpen,
        T("Sample studio - generate and shape a loop (Shift+W)") },
      { "piano", "##railPianoRoll", IconsInfinite::Piano, nullptr, &gPianoRollOpen, T("Piano roll - edit and generate notes (Shift+R)") },
      { "drums", "##railDrums", IconsInfinite::Drum, nullptr, &gDrumPanelOpen, T("Drums - play and edit Drum Sequencers (Shift+J)") },
      { "view3d", "##railView3D", IconsInfinite::View3d, nullptr, &gView3DOpen,
        T("3D View - arrange node outputs and render a camera (Shift+3)") },
      { "view2d", "##railView2D", IconsInfinite::Layers, nullptr, &gView2DOpen,
        T("2D View - lay out node outputs as layers and render a picture (Shift+2)") },
      { "curve", "##railCurveEditor", IconsInfinite::Curve, nullptr, &gCurveEditorOpen,
        T("Curve editor - draw and shape MSEG curves (Shift+E)") },
      { "field", "##railFieldPatcher", IconsInfinite::Code, nullptr, &gFieldPatcherOpen, T("Field patcher - Field as boxes (Shift+F)") },
   };
   constexpr int kN = (int)(sizeof(kEntries) / sizeof(kEntries[0]));
   // The saved order (gRailOrder, comma-separated keys); unknown keys are skipped, new ones join in default place.
   int order[kN];
   {
      int n = 0;
      bool used[kN] = {};
      size_t at = 0;
      while (at <= gRailOrder.size() && n < kN)
      {
         size_t c = gRailOrder.find(',', at);
         if (c == std::string::npos)
            c = gRailOrder.size();
         const std::string k = gRailOrder.substr(at, c - at);
         at = c + 1;
         for (int i = 0; i < kN; i++)
            if (!used[i] && k == kEntries[i].key)
            {
               used[i] = true;
               order[n++] = i;
               break;
            }
      }
      for (int i = 0; i < kN; i++)
         if (!used[i])
            order[n++] = i;
   }
   // Drag a tile to reorder: the others slide aside live; the order is saved with the workspace.
   int moveFrom = -1, moveTo = -1;
   {
      int drawn = 0;
      for (int slot : ReorderDrag::Order("RAIL_TILE", kN))
      {
         const Entry& e = kEntries[order[slot]];
         const float y = ReorderDrag::Glide(slot, ImVec2(0.0f, top + drawn * step)).y;
         drawn++;
         if (PanelRail::Item(e.id, y, e.glyphOff, e.glyphOn, *e.open, e.tip))
            *e.open = !*e.open;
         ReorderDrag::Item(slot, nullptr);
      }
      if (const ReorderDrag::Move m = ReorderDrag::End())
      {
         moveFrom = m.from;
         moveTo = m.to;
      }
   }
   if (moveFrom >= 0 && moveFrom != moveTo)
   {
      const int moved = order[moveFrom];
      if (moveFrom < moveTo)
         for (int i = moveFrom; i < moveTo; i++)
            order[i] = order[i + 1];
      else
         for (int i = moveFrom; i > moveTo; i--)
            order[i] = order[i - 1];
      order[moveTo] = moved;
      gRailOrder.clear();
      for (int i = 0; i < kN; i++)
         gRailOrder += std::string(i ? "," : "") + kEntries[order[i]].key;
   }
   const float bottom = ImGui::GetWindowHeight() - tok::control_h - tok::space_2;
   if (PanelRail::Item("##railSettings", bottom, IconsInfinite::Settings, nullptr, gSettingsOpen,
                       T("Settings")))
      gSettingsOpen = !gSettingsOpen;
   if (PanelRail::Item("##railHistory", bottom - step, IconsInfinite::History, nullptr, gHistoryOpen,
                       T("Edit history - jump back to any earlier edit")))
      gHistoryOpen = !gHistoryOpen;
   // The agents' shell sits with the tools at the bottom, above Edit history.
   if (!Platform::PluginModeActive() && // no shell or agents inside a DAW
       PanelRail::Item("##railTerminal", bottom - tok::stage_menu_bar_panel_rail_ratio * step, IconsInfinite::Terminal, nullptr, gTerminalOpen,
                       T("Terminal - a real shell, for you and your coding agents (Ctrl+`)")))
   {
      gTerminalOpen = !gTerminalOpen;
      gTerminalFocus = gTerminalOpen;
   }
   PanelRail::End();
}

ImTextureID BrandLogoTexture()
{
   static GLuint tex = 0;
   static bool tried = false;
   if (!tried)
   {
      tried = true;
      int w = 0, h = 0, n = 0;
      unsigned char* px = stbi_load_from_memory(brandlogo::kPng, (int)sizeof(brandlogo::kPng), &w, &h, &n, 4);
      if (px != nullptr)
      {
         glGenTextures(1, &tex);
         glBindTexture(GL_TEXTURE_2D, tex);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
         glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
         stbi_image_free(px);
      }
   }
   return (ImTextureID)(intptr_t)tex;
}

void DrawMenuBar(FrameCtx& fc)
{
   auto& window = fc.window;
   auto& vp = fc.vp;


      // ---------------- node editor ----------------
      vp = ImGui::GetMainViewport();
      ImGui::SetNextWindowPos(vp->WorkPos);
      // The panel rail is a separate window down the right edge; the shell (menu bar, canvas, docked panels)
      // takes the rest, so every dock lays out against the narrower width without knowing about the rail.
      ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x - PanelRail::kWidth, vp->WorkSize.y));
      // NoScrollbar/NoScrollWithMouse: this window is a fixed full-screen
      // shell (also NoResize/NoMove) whose every region is meant to be
      // divided exactly among the menu bar, canvas and docked panels, never
      // scrolled as a whole. Without this flag, being even a few pixels over
      // budget in that division - e.g. the item spacing ImGui inserts between
      // a top/bottom-docked viewport panel and the canvas row below/above it,
      // easy to undercount by hand - grows a scrollbar on THIS window, which
      // reads as "a slider that moves the entire app" rather than as a
      // rounding error in one panel's layout.
      // Zero padding: this shell has no content of its own, only the canvas
      // and the docked panels, and each of those paints its own background
      // and carries its own inner padding. The default 8px WindowPadding just
      // put an 8px band of shell background around and between them - the same
      // "bar between the viewports" the ItemSpacing gaps were producing, at
      // the window edges and under the menu bar.
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
      // The bar is tok::bar_h tall: ImGui sizes it from FramePadding.y at Begin.
      const float barPadY = (tok::bar_h - ImGui::GetFontSize()) * 0.5f;
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, barPadY));
      // A 2D or 3D View on Fill takes the whole window, top bar included (Blender's Fullscreen Area, Photoshop's
      // full screen mode); its own Fill icon brings the bar back.
      const bool viewFills = (gView2DOpen && gView2DDock == 4) || (gView3DOpen && gView3DDock == 4);
      ImGui::Begin("Infinite", nullptr,
                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   (viewFills ? 0 : ImGuiWindowFlags_MenuBar) | ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoScrollWithMouse);
      ImGui::PopStyleVar(2);

      // BeginMenuBar aligns text to FramePadding.y, so the bar's padding is what
      // centres the File/Edit/Menu labels; popped at once so no menu popup inherits it.
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, barPadY));
      const bool menuBarOpen = ImGui::BeginMenuBar();
      ImGui::PopStyleVar();
      if (menuBarOpen)
      {
         auto& MenuTile = TopBarParts::MenuTile;
         float* sMenuMidYPtr = nullptr;
         // Infinite mark (the app icon): opens the About dialog.
         {
            const float s = tok::control_h;
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            // Same centre line as the File/Edit/Menu well (measured last frame), same height.
            static float sMenuMidY = 0.0f;
            const float cy = sMenuMidY > 0.0f ? sMenuMidY : ImGui::GetWindowPos().y + tok::bar_h * 0.5f;
            sMenuMidYPtr = &sMenuMidY;
            ImGui::SetCursorScreenPos(ImVec2(p0.x + tok::space_1, cy - s * 0.5f));
            if (UiInteract::Hit("##brandlogo", ImVec2(s, s)))
               gShowAboutModal = true;
            const bool hov = ImGui::IsItemHovered();
            if (const ImTextureID tex = BrandLogoTexture())
               Thumb::AddRounded(ImGui::GetWindowDrawList(), tex, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                  ImVec2(0, 0), ImVec2(1, 1), ImGui::GetColorU32(tok::V4(tok::stage_menu_bar_col_2, hov ? 1.0f : 0.92f)), 5.0f);
            if (hov)
               HelpTip("%s", T("About Infinite"));
            ImGui::SetCursorScreenPos(ImVec2(ImGui::GetItemRectMax().x + tok::space_2, p0.y));
         }
         // File, Edit and Menu are three closed tiles that read as one section: a shared well sits behind them.
         ImDrawList* menuDl = ImGui::GetWindowDrawList();
         const ImVec2 menuGroupP = ImGui::GetCursorScreenPos();
         float menuGroupMaxX = menuGroupP.x, menuGroupY0 = 0.0f, menuGroupY1 = 0.0f;
         auto GroupTile = [&](const char* label) -> bool
         {
            // Menu-bar items do not share a y after the first; pin each tile to the group's line.
            // A menu-bar title's box reaches 8 px left of the cursor (MenuTile's 16 spacing: +8 in, -16 out), so add 8 back
            // for a true 2 px gap between tiles.
            if (menuGroupY1 > 0.0f)
               ImGui::SetCursorScreenPos(ImVec2(menuGroupMaxX + tok::space_0_5 + tok::space_2, menuGroupP.y));
            const bool open = MenuTile(label);
            menuGroupMaxX = std::max(menuGroupMaxX, ImGui::GetItemRectMax().x);
            if (menuGroupY1 <= 0.0f)
            {
               menuGroupY0 = ImGui::GetItemRectMin().y;
               menuGroupY1 = ImGui::GetItemRectMax().y;
            }
            return open;
         };
         menuDl->ChannelsSplit(2);
         menuDl->ChannelsSetCurrent(1);
         {
            if (GroupTile(L("File")))
         {
            MenuParts::BeginContent();
            if (MenuParts::Item(L("New"), MODKEY "+N"))
               GuardUnsavedChanges([]() { NewPatch(); });
            if (MenuParts::Item(L("New Synth"), MODKEY "+Shift+N"))
               SpawnPlayableSynth();
            if (MenuParts::Item(L("New Effect Chain")))
               SpawnEffectChain();
            if (MenuParts::Item(L("New 3D Scene")))
               SpawnScene3D();
            if (MenuParts::Item(L("Template library...")))
               gTemplatesOpen = true;
            if (MenuParts::Item(L("Open..."), MODKEY "+O"))
            {
               const std::string path = Platform::OpenPatchDialog();
               if (!path.empty())
                  GuardUnsavedChanges([path]() { LoadPatchFrom(path); });
            }

            if (MenuParts::SubMenu(L("Open Recent"), !Patch::Recents().empty()))
            {
               // Copied before iterating: opening one calls NoteRecent, which
               // reorders the very list being walked.
               const std::vector<std::string> recents = Patch::Recents();
               for (const std::string& entry : recents)
               {
                  const std::string name = AppPaths::FsPath(entry).filename().u8string();
                  if (MenuParts::Item(name.c_str()))
                     GuardUnsavedChanges([entry]() { LoadPatchFrom(entry); });
               }
               ImGui::EndMenu();
            }

            MenuParts::Separator();
            if (MenuParts::Item(L("Save"), MODKEY "+S"))
               SavePatchInteractive(false);
            if (MenuParts::Item(L("Save As..."), MODKEY "+Shift+S"))
               SavePatchInteractive(true);
            if (MenuParts::Item(L("Save with media...")))
               gShowSaveMediaModal = true;
            if (MenuParts::Item(L("Share Patch..."), nullptr, false, !SharePatchBusy()))
               SharePatchInteractive();

            if (!gPatchPath.empty() || !gPatchStatus.empty())
            {
               MenuParts::Separator();
               // Wrapped at a fixed width: a long status line must never widen the menu.
               ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + tok::stage_menu_bar_gap);
               if (!gPatchPath.empty())
               {
                  const std::string base = AppPaths::FsPath(gPatchPath).filename().u8string();
                  ImGui::TextDisabled("%s", base.c_str());
               }
               if (!gPatchStatus.empty())
                  ImGui::TextDisabled("%s", gPatchStatus.c_str());
               ImGui::PopTextWrapPos();
            }
            MenuParts::EndContent();
            ImGui::EndMenu();
         }

            if (GroupTile(L("Edit")))
         {
            MenuParts::BeginContent();
            {
               // "Undo Move node": the item names the edit it would undo.
               const std::string undoText = gUndoStack.empty() ? std::string(L("Undo")) : std::string(L("Undo")) + " " + UndoLabelAt(0);
               const std::string redoText = gRedoStack.empty() ? std::string(L("Redo")) : std::string(L("Redo")) + " " + RedoLabelAt(0);
               if (MenuParts::Item(undoText.c_str(), MODKEY "+Z", false, !gUndoStack.empty()))
                  Undo();
               if (MenuParts::Item(redoText.c_str(), MODKEY "+Shift+Z", false, !gRedoStack.empty()))
                  Redo();
               MenuParts::Check(L("History"), &gHistoryOpen);
            }
            MenuParts::Separator();
            if (MenuParts::Item(L("Cut"), MODKEY "+X"))
               gRequestCut = true;
            if (MenuParts::Item(L("Copy"), MODKEY "+C"))
               gRequestCopy = true;
            if (MenuParts::Item(L("Paste"), MODKEY "+V"))
               gRequestPaste = true;
            if (MenuParts::Item(L("Duplicate"), MODKEY "+D"))
               gRequestDuplicate = true;
            if (MenuParts::Item(L("Delete"), "Backspace"))
               gRequestDelete = true;
            MenuParts::Separator();
            if (MenuParts::Item(L("Select All"), "Shift+A"))
               gRequestSelectAll = true;
            if (MenuParts::Item(L("Bypass selection"), "B"))
               gRequestBypass = true;
            MenuParts::Separator();
            if (MenuParts::Item(L("Group selection"), MODKEY "+G"))
               gRequestGroup = true;
            if (MenuParts::Item(L("Ungroup"), MODKEY "+U"))
               gRequestUngroup = true;
            MenuParts::Separator();
            if (MenuParts::Item(L("Add Node..."), "Shift+N"))
               gRequestAddNode = true;
            if (MenuParts::Item(L("Add Note"), "/"))
               gRequestAddComment = true;
            MenuParts::EndContent();
            ImGui::EndMenu();
         }

            if (GroupTile(L("Menu")))
            {
            MenuParts::BeginContent();
         {
            if (MenuParts::Item(L("Settings..."), MODKEY "+0"))
               gSettingsOpen = true;

            MenuParts::Separator();

            if (MenuParts::SubMenu(L("Panels")))
            {
               if (MenuParts::SubMenu(L("Viewport panel")))
               {
                  MenuParts::Check(L("Show viewport panel"), &gViewportPanelOpen);
                  if (gViewportPanelOpen)
                  {
                     ImGui::SetNextItemWidth(tok::stage_menu_bar_set_next_item_width_w);
                     FieldWell::PushStyle(); ViewportPanelDockCombo(); FieldWell::PopStyle();
                     ImGui::SetNextItemWidth(tok::stage_menu_bar_set_next_item_width_w);
                     if (gViewportPanelDock == 1 || gViewportPanelDock == 2)
                     {
                        FieldWell::Slider(L("Width"), &gViewportPanelWidth,
                                           kViewportPanelMinWidth, params::stage_menu_bar::viewport_panel_width_lo, "%.0f px");
                     }
                     else
                     {
                        FieldWell::Slider(L("Height"), &gViewportPanelHeight,
                                           kViewportPanelMinHeight, params::stage_menu_bar::viewport_panel_height_lo, "%.0f px");
                     }
                     MenuParts::Separator();
                     if (!gViewportPanelNodes.empty() && MenuParts::Item(L("Clear Cards")))
                        gViewportPanelNodes.clear();
                     if (MenuParts::Item(L("Close Viewport")))
                        gViewportPanelOpen = false;
                  }
                  ImGui::EndMenu();
               }

               if (MenuParts::SubMenu(L("Modulation matrix")))
               {
                  // Plain themed widgets, same as "Viewport panel" above - this
                  // is menu chrome, not a node body, so it takes the app's own
                  // checkbox/slider colours (ApplyTheme) rather than the P10
                  // dark-contrast-budget style meant for controls inside a node.
                  // The two styles side by side in one menu (one purple/clean,
                  // one flat blue) is what read as inconsistent.
                  MenuParts::Check(L("Show modulation matrix"), &gModMatrixOpen);
                  if (gModMatrixOpen)
                  {
                     ImGui::SetNextItemWidth(tok::stage_menu_bar_set_next_item_width_w);
                     FieldWell::PushStyle(); ModMatrixDockCombo(); FieldWell::PopStyle();
                     ImGui::SetNextItemWidth(tok::stage_menu_bar_set_next_item_width_w);
                     if (gModMatrixDock == 1 || gModMatrixDock == 2)
                     {
                        FieldWell::Slider(L("Width"), &gModMatrixWidth,
                                           kModMatrixMinWidth, params::stage_menu_bar::mod_matrix_width_lo, "%.0f px");
                     }
                     else
                     {
                        FieldWell::Slider(L("Height"), &gModMatrixHeight,
                                           kModMatrixMinHeight, params::stage_menu_bar::mod_matrix_height_lo, "%.0f px");
                     }
                  }
                  ImGui::EndMenu();
               }

               if (MenuParts::SubMenu(L("Mixer")))
               {
                  MenuParts::Check(L("Show Mixer"), &gMixerPanelOpen);
                  if (gMixerPanelOpen)
                  {
                     ImGui::SetNextItemWidth(tok::stage_menu_bar_set_next_item_width_w);
                     FieldWell::PushStyle(); MixerPanelDockCombo(); FieldWell::PopStyle();
                     ImGui::SetNextItemWidth(tok::stage_menu_bar_set_next_item_width_w);
                     if (gMixerPanelDock == 1 || gMixerPanelDock == 2)
                        FieldWell::Slider(L("Width"), &gMixerPanelWidth, kMixerPanelMinWidth, params::stage_menu_bar::mixer_panel_width_lo, "%.0f px");
                     else if (gMixerPanelDock != kDockFloating)
                        FieldWell::Slider(L("Height"), &gMixerPanelHeight, kMixerPanelMinHeight, params::stage_menu_bar::mixer_panel_height_lo, "%.0f px");
                  }
                  ImGui::EndMenu();
               }

               if (MenuParts::SubMenu(L("Performance Matrix")))
               {
                  // Same reasoning as "Modulation matrix" above: plain themed
                  // widgets, not the node-body P10 style.
                  MenuParts::Check(L("Show Performance Matrix"), &gPerfPanelOpen);
                  if (gPerfPanelOpen)
                  {
                     ImGui::SetNextItemWidth(tok::stage_menu_bar_set_next_item_width_w);
                     FieldWell::PushStyle(); PerfPanelDockCombo(); FieldWell::PopStyle();
                     ImGui::SetNextItemWidth(tok::stage_menu_bar_set_next_item_width_w);
                     if (gPerfPanelDock == 1 || gPerfPanelDock == 2)
                     {
                        FieldWell::Slider(L("Width"), &gPerfPanelWidth,
                                           kPerfPanelMinWidth, params::stage_menu_bar::perf_panel_width_lo, "%.0f px");
                     }
                     else
                     {
                        FieldWell::Slider(L("Height"), &gPerfPanelHeight,
                                           kPerfPanelMinHeight, params::stage_menu_bar::perf_panel_height_lo, "%.0f px");
                     }
                     MenuParts::Check(L("Edit Mode"), &gPerfEditMode);
                  }
                  ImGui::EndMenu();
               }

               if (MenuParts::SubMenu(L("Arrangement Timeline")))
               {
                  MenuParts::Check(L("Show Arrangement Timeline"), &gArrangePanelOpen);
                  if (gArrangePanelOpen)
                  {
                     // Bottom or top only - a timeline reads left-to-right, so a
                     // side dock would fight the ruler's own horizontal axis.
                     // Saved with the document (Settings.dockSide); not undoable.
                     int dockSide = gArrange.settings.dockSide == 1 ? 1 : 0;
                     ImGui::SetNextItemWidth(tok::stage_menu_bar_set_next_item_width_w);
                     FieldWell::PushStyle();
                     static const char* kDockNames[] = { I18N_KEY("Bottom"), I18N_KEY("Top") };
                     bool dockChanged = false;
                     if (MenuParts::BeginCombo("##arrdock", T(kDockNames[dockSide])))
                     {
                        for (int i = 0; i < 2; ++i)
                           if (MenuParts::Choice(L(kDockNames[i]), i == dockSide))
                           {
                              dockSide = i;
                              dockChanged = true;
                           }
                        MenuParts::EndCombo();
                     }
                     FieldWell::PopStyle();
                     if (dockChanged && dockSide != gArrange.settings.dockSide)
                     {
                        gArrange.settings.dockSide = dockSide;
                        gArrange.revision++; // a model field like any other (WP5b)
                        gPatchDirty = true;
                     }
                     ImGui::SetNextItemWidth(tok::stage_menu_bar_set_next_item_width_w);
                     FieldWell::Slider(L("Height"), &gArrangePanelHeight,
                                        kArrangePanelMinHeight, params::stage_menu_bar::arrange_panel_height_lo, "%.0f px");
                  }
                  ImGui::EndMenu();
               }

               if (MenuParts::SubMenu(L("Session")))
               {
                  MenuParts::Check(L("Show Session"), &gSessionPanelOpen);
                  if (gSessionPanelOpen)
                  {
                     ImGui::SetNextItemWidth(tok::stage_menu_bar_set_next_item_width_w);
                     FieldWell::Slider(L("Height"), &gSessionPanelHeight, kSessionPanelMinHeight, params::stage_menu_bar::session_panel_height_lo, "%.0f px");
                  }
                  ImGui::EndMenu();
               }

               // Sample Studio and Piano Roll dock at the bottom or the top, like the Timeline.
               auto dockedPanelMenu = [](const char* name, const char* show, bool& open, int& dock, float& height, const char* dockId) {
                  if (!MenuParts::SubMenu(name))
                     return;
                  MenuParts::Check(show, &open);
                  if (open)
                  {
                     ImGui::SetNextItemWidth(tok::stage_menu_bar_set_next_item_width_w);
                     FieldWell::PushStyle();
                     static const char* kDockNames[] = { I18N_KEY("Bottom"), I18N_KEY("Top") };
                     int side = dock == 3 ? 1 : 0;
                     if (MenuParts::BeginCombo(dockId, T(kDockNames[side])))
                     {
                        for (int i = 0; i < 2; ++i)
                           if (MenuParts::Choice(L(kDockNames[i]), i == side))
                              dock = i == 1 ? 3 : 0;
                        MenuParts::EndCombo();
                     }
                     FieldWell::PopStyle();
                     ImGui::SetNextItemWidth(tok::stage_menu_bar_set_next_item_width_w);
                     FieldWell::Slider(L("Height"), &height, params::stage_menu_bar::height_lo, params::stage_menu_bar::height_hi, "%.0f px");
                  }
                  ImGui::EndMenu();
               };
               dockedPanelMenu(L("Sample Studio"), L("Show Sample Studio"), gSampleStudioOpen, gSampleStudioDock, gSampleStudioHeight, "##studiodock");
               // The 2D and 3D Views dock on every side, or fill the window in the canvas's place.
               auto viewPanelMenu = [](const char* name, const char* show, bool& open, int which, float& height, float& width, const char* dockId) {
                  if (!MenuParts::SubMenu(name))
                     return;
                  MenuParts::Check(show, &open);
                  if (open)
                  {
                     static const char* kDockNames[] = { I18N_KEY("Bottom"), I18N_KEY("Right"), I18N_KEY("Left"), I18N_KEY("Top"), I18N_KEY("Fill") };
                     const int dock = which == 2 ? gView2DDock : gView3DDock;
                     ImGui::SetNextItemWidth(tok::stage_menu_bar_set_next_item_width_w);
                     FieldWell::PushStyle();
                     if (MenuParts::BeginCombo(dockId, T(kDockNames[dock])))
                     {
                        for (int i = 0; i < 5; ++i)
                           if (MenuParts::Choice(L(kDockNames[i]), i == dock))
                              SetViewDock(which, i);
                        MenuParts::EndCombo();
                     }
                     FieldWell::PopStyle();
                     ImGui::SetNextItemWidth(tok::stage_menu_bar_set_next_item_width_w);
                     if (dock == 1 || dock == 2)
                        FieldWell::Slider(L("Width"), &width, params::stage_menu_bar::width_lo, params::stage_menu_bar::width_hi, "%.0f px");
                     else if (dock != 4)
                        FieldWell::Slider(L("Height"), &height, params::stage_menu_bar::height_2_lo, params::stage_menu_bar::height_2_hi, "%.0f px");
                  }
                  ImGui::EndMenu();
               };
               viewPanelMenu(L("3D View"), L("Show 3D View"), gView3DOpen, 3, gView3DHeight, gView3DWidth, "##view3ddock");
               viewPanelMenu(L("2D View"), L("Show 2D View"), gView2DOpen, 2, gView2DHeight, gView2DWidth, "##view2ddock");
               dockedPanelMenu(L("Piano Roll"), L("Show Piano Roll"), gPianoRollOpen, gPianoRollDock, gPianoRollHeight, "##rolldock");
               dockedPanelMenu(L("Drums"), L("Show Drums"), gDrumPanelOpen, gDrumPanelDock, gDrumPanelHeight, "##drumsdock");
               if (!Platform::PluginModeActive() && MenuParts::SubMenu(L("Terminal"))) // no shell inside a DAW
               {
                  MenuParts::Check(L("Show Terminal"), &gTerminalOpen);
                  if (gTerminalOpen)
                  {
                     static const char* kTerminalDocks[] = { I18N_KEY("Bottom"), I18N_KEY("Right"), I18N_KEY("Left"), I18N_KEY("Top") };
                     ImGui::SetNextItemWidth(tok::stage_menu_bar_set_next_item_width_w);
                     FieldWell::PushStyle();
                     if (MenuParts::BeginCombo("##terminaldock", T(kTerminalDocks[gTerminalDock])))
                     {
                        for (int i = 0; i < 4; ++i)
                           if (MenuParts::Choice(L(kTerminalDocks[i]), i == gTerminalDock))
                              gTerminalDock = i;
                        MenuParts::EndCombo();
                     }
                     FieldWell::PopStyle();
                     ImGui::SetNextItemWidth(tok::stage_menu_bar_set_next_item_width_w);
                     if (gTerminalDock == 1 || gTerminalDock == 2)
                        FieldWell::Slider(L("Width"), &gTerminalWidth, params::stage_menu_bar::terminal_width_lo, params::stage_menu_bar::terminal_width_hi, "%.0f px");
                     else
                        FieldWell::Slider(L("Height"), &gTerminalHeight, params::stage_menu_bar::terminal_height_lo, params::stage_menu_bar::terminal_height_hi, "%.0f px");
                  }
                  ImGui::EndMenu();
               }
               if (MenuParts::SubMenu(L("Curve Editor")))
               {
                  MenuParts::Check(L("Show Curve Editor"), &gCurveEditorOpen);
                  if (gCurveEditorOpen)
                  {
                     static const char* kCurveDocks[] = { I18N_KEY("Bottom"), I18N_KEY("Top") };
                     static const int kCurveDockCodes[] = { 0, 3 };
                     ImGui::SetNextItemWidth(tok::stage_menu_bar_set_next_item_width_w);
                     FieldWell::PushStyle();
                     if (MenuParts::BeginCombo("##curvedock", T(kCurveDocks[gCurveEditorDock == 3 ? 1 : 0])))
                     {
                        for (int i = 0; i < 2; ++i)
                           if (MenuParts::Choice(L(kCurveDocks[i]), kCurveDockCodes[i] == gCurveEditorDock))
                              gCurveEditorDock = kCurveDockCodes[i];
                        MenuParts::EndCombo();
                     }
                     FieldWell::PopStyle();
                     ImGui::SetNextItemWidth(tok::stage_menu_bar_set_next_item_width_w);
                     FieldWell::Slider(L("Height"), &gCurveEditorHeight, params::stage_menu_bar::curve_editor_height_lo, params::stage_menu_bar::curve_editor_height_hi, "%.0f px");
                  }
                  ImGui::EndMenu();
               }
               if (MenuParts::SubMenu(L("Field Patcher")))
               {
                  MenuParts::Check(L("Show Field Patcher"), &gFieldPatcherOpen);
                  if (gFieldPatcherOpen)
                  {
                     static const char* kPatcherDocks[] = { I18N_KEY("Bottom"), I18N_KEY("Top") };
                     static const int kPatcherDockCodes[] = { 0, 3 };
                     ImGui::SetNextItemWidth(tok::stage_menu_bar_set_next_item_width_w);
                     FieldWell::PushStyle();
                     if (MenuParts::BeginCombo("##patcherdock", T(kPatcherDocks[gFieldPatcherDock == 3 ? 1 : 0])))
                     {
                        for (int i = 0; i < 2; ++i)
                           if (MenuParts::Choice(L(kPatcherDocks[i]), kPatcherDockCodes[i] == gFieldPatcherDock))
                              gFieldPatcherDock = kPatcherDockCodes[i];
                        MenuParts::EndCombo();
                     }
                     FieldWell::PopStyle();
                     ImGui::SetNextItemWidth(tok::stage_menu_bar_set_next_item_width_w);
                     FieldWell::Slider(L("Height"), &gFieldPatcherHeight, params::stage_menu_bar::field_patcher_height_lo, params::stage_menu_bar::field_patcher_height_hi, "%.0f px");
                  }
                  ImGui::EndMenu();
               }

               if (MenuParts::SubMenu(L("Show Set")))
               {
                  MenuParts::Check(L("Show Set window"), &gShowSetOpen);
                  if (MenuParts::Item(L("Next patch"), "PgDn", false, ShowSetSize() > 0))
                     ShowSetStep(+1);
                  if (MenuParts::Item(L("Previous patch"), "PgUp", false, ShowSetSize() > 0))
                     ShowSetStep(-1);
                  MenuParts::Separator();
                  if (MenuParts::Item(L("Show lock"), MODKEY "+Shift+L", gShowLock))
                     ShowLockToggle();
                  if (ShowPanicLatched())
                  {
                     if (MenuParts::Item(L("Resume after panic"), MODKEY "+.", true))
                        ShowPanicToggle();
                  }
                  else if (MenuParts::Item(L("Panic"), MODKEY "+."))
                     ShowPanicToggle();
                  if (MenuParts::Item(L("Pre-show check")))
                     {
                     gShowSetOpen = true;
                     gShowCheckRequest = true;
                  }
                  ImGui::EndMenu();
               }
               ImGui::EndMenu();
            }

            if (MenuParts::SubMenu(L("Nodes")))
            {
               if (MenuParts::Item(L("Show all params")))
               {
                  for (GraphNode& gn : gNodes)
                     gn.showParams = true;
               }
               if (MenuParts::Item(L("Hide all params")))
               {
                  for (GraphNode& gn : gNodes)
                     gn.showParams = false;
               }
               ImGui::EndMenu();
            }

            MenuParts::Separator();
            if (MenuParts::Item(L("All shortcuts...")))
               gShortcutsOpen = true;
            if (MenuParts::Item(L("Help / module reference")))
               gHelpOpen = true;
#ifndef NDEBUG
            // ImGui's built-in inspectors, not a custom tool: the Debugger's
            // Tools > Item Picker names the exact ImGuiCol_*/style var and
            // rect behind whatever you click, and the Style Editor lists and
            // live-previews every one of those values - the fastest way to
            // hand back "this exact knob, this exact number" instead of a
            // screenshot and a guess. Dev-only: excluded from Release builds
            // (NDEBUG) so shipped/public builds never expose these.
            if (MenuParts::Item(L("UI Debugger / Item Picker"))) // i18n-ok (debug UI)
               gUiDebuggerOpen = true;
            if (MenuParts::Item(L("UI Style Editor"))) // i18n-ok (debug UI)
               gUiStyleEditorOpen = true;
#endif
            if (MenuParts::Item(L("Collect diagnostics...")))
               gShowDiagnosticsModal = true;
            if (MenuParts::Item(L("About Infinite")))
               gShowAboutModal = true;
            // Inside a DAW: the app updates the plugin, and the DAW closes it.
            if (!Platform::PluginModeActive())
            {
               if (MenuParts::Item(L("Check for updates")))
               {
                  UpdateCheck::Start();
                  gShowUpdateCheckModal = true;
               }

               MenuParts::Separator();
               if (MenuParts::Item(L("Quit")))
                  RequestClose(window);
            }
         }
            MenuParts::EndContent();
            ImGui::EndMenu();
         }
         }
         // The shared well behind the three tiles.
         menuDl->ChannelsSetCurrent(0);
         {
            const ImVec4 tx = ImGui::GetStyleColorVec4(ImGuiCol_Text);
            const float mid = (menuGroupY0 + menuGroupY1) * 0.5f;
            if (sMenuMidYPtr != nullptr)
               *sMenuMidYPtr = mid;
            menuDl->AddRectFilled(ImVec2(menuGroupP.x - tok::space_0_5, mid - tok::control_h * 0.5f), ImVec2(menuGroupMaxX + tok::space_0_5, mid + tok::control_h * 0.5f),
               ImGui::GetColorU32(ImVec4(tx.x, tx.y, tx.z, tok::stage_menu_bar_alpha_4)), tok::radius_control);
         }
         menuDl->ChannelsMerge();

         Transport& transport = Transport::Instance();
         const bool isTransportPlaying = transport.IsPlaying();

         // Top bar controls styling: clean, symmetrical, unboxed with pixel-perfect alignment.
         ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
         ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, tok::RoleV4(tok::col::ink_white, tok::stage_menu_bar_ink_white_alpha_3));
         ImGui::PushStyleColor(ImGuiCol_FrameBgActive, tok::RoleV4(tok::col::ink_white, tok::stage_menu_bar_ink_white_alpha_4));
         ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
         ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tok::RoleV4(tok::col::ink_white, tok::stage_menu_bar_ink_white_alpha_3));
         ImGui::PushStyleColor(ImGuiCol_ButtonActive, tok::RoleV4(tok::col::ink_white, tok::stage_menu_bar_ink_white_alpha_4));
         ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
         ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tok::radius_control);
         // 24 pt controls (tok::control_h) on one centre line; the cursor Y below pins them in the bar.
         ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(tok::space_2, (tok::control_h - ImGui::GetFontSize()) * 0.5f));
         ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(tok::space_1, tok::space_1));

         // Right-edge items are placed at an absolute x on the bar's line.
         auto TopBarPlaceAt = [](float x) { ImGui::SameLine(x); };
         auto TopBarSameLine = [](float spacing = tok::space_1) {
            ImGui::SameLine(0.0f, spacing);
         };
         // A live number: tabular digits in a slot as wide as its widest value, so it never
         // shifts what sits beside it. `widest` is the longest text it can show.
         auto TabLabel = [](const char* key, const char* text, const char* widest, bool dim)
         {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            float w;
            {
               UiType::Scope ts(UiType::Size::Body, UiType::Weight::Regular);
               w = std::max(Readout::Measure(widest), Readout::Measure(text));
            }
            ImGui::Dummy(ImVec2(w, tok::control_h));
            Readout::Draw(key, UiLayout::Rect { p.x, p.y, w, tok::control_h }, text, text, UiType::Size::Body,
                          UiType::Weight::Regular, Readout::Align::Left, dim ? tok::stage_menu_bar_draw : 1.0f);
         };

         auto& SectionBreak = TopBarParts::SectionBreak;
         // Everything after the menus sits on one centre line, tok::control_h tall in a tok::bar_h bar.

         auto drawTransport = [&]()
         {
         // 1. Transport (Play, Rewind, Audio On/Off)
         if (ActionButton::Draw("##transportplay", ImVec2(tok::control_h, 0),
                                isTransportPlaying ? ActionButton::Kind::Go : ActionButton::Kind::Plain))
            LaunchRequestTogglePlay();
         {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 bmin = ImGui::GetItemRectMin();
            const ImVec2 bmax = ImGui::GetItemRectMax();
            const ImVec2 center((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
            const float iconSize = tok::icon_control;
            const ImU32 col = isTransportPlaying
                                  ? tok::Role(tok::col::ink_white)
                                  : ImGui::GetColorU32(ImGuiCol_Text);
            if (isTransportPlaying)
               glyph::DrawPlayerPause(dl, center, iconSize, col);
            else
               glyph::Draw(dl, center, iconSize, col, IconsInfinite::PlayFill);
         }
         if (ImGui::IsItemHovered())
            HelpTip(T("%s (Space)"), isTransportPlaying ? T("Pause") : T("Play"));

         ImGui::SetCursorScreenPos(ImVec2(ImGui::GetItemRectMax().x + tok::space_1, ImGui::GetItemRectMin().y));
         if (ActionButton::Draw("##transportrewind", ImVec2(tok::control_h, 0)))
            LaunchRequestRewind();
         {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 bmin = ImGui::GetItemRectMin();
            const ImVec2 bmax = ImGui::GetItemRectMax();
            const ImVec2 center((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
            const float iconSize = tok::icon_control;
            const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
            glyph::DrawPlayerRewind(dl, center, iconSize, col);
         }
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("Rewind (Return)"));

         };

         auto drawAudio = [&]()
         {
         // Audio engine power, nothing else: Start starts the device, Stop
         // stops it, and neither touches gAudioMode (which driver the engine
         // plays - the canvas or the Arrangement Timeline - is the panel's
         // "Enable Timeline Audio" toggle). While the timeline drives, a
         // "Timeline" badge sits next to the button so an engine that is on
         // but ignoring the canvas never looks broken.
         {
            const bool engineOn = AudioEngine::Instance().SampleRate() > 0.0;
            const bool audioOn = engineOn;
            if (ChipButton::Draw(audioOn ? L("Stop Audio") : L("Start Audio"), audioOn, tok::control_h, 0.0f, true))
            {
               if (audioOn)
               {
                  AudioEngine::Instance().Stop();
               }
               else
               {
                  gAudioStartError.clear();
                  if (!StartAudioEngine(gAudioStartError))
                     fprintf(stderr, "audio device: %s\n", gAudioStartError.c_str());
               }
            }
            if (audioOn || !gAudioStartError.empty())
               StatusDot::OnLastItem(!audioOn ? StatusDot::State::Error : StatusDot::State::Ok);
            if (!audioOn && !gAudioStartError.empty() && ImGui::IsItemHovered())
               Tip::Show("%s", gAudioStartError.c_str());

         }
         };

         // The Timeline badge sits outside the centred group, so enabling Timeline Audio never shifts the bar.
         auto drawTimelineBadge = [&]()
         {
            const bool engineOn = AudioEngine::Instance().SampleRate() > 0.0;
            if (gAudioMode == AudioMode::Timeline)
            {
               TopBarSameLine(4.0f);
               if (ChipButton::Draw(T("Timeline"), true, tok::control_h, 0.0f, true))
                  gAudioMode = AudioMode::Canvas;
               if (ImGui::IsItemHovered())
                  HelpTip(engineOn
                     ? T("The Arrangement Timeline is driving audio. Click to hand it back to the canvas.")
                     : T("The Arrangement Timeline will drive audio once the engine is started."));
            }
            else if (gAudioMode == AudioMode::Session)
            {
               TopBarSameLine(4.0f);
               if (ChipButton::Draw(L("Session##sessionbadge"), true, tok::control_h, 0.0f, true))
                  gAudioMode = AudioMode::Canvas;
               if (ImGui::IsItemHovered())
                  HelpTip(engineOn
                     ? T("The Session's launched clips are driving audio. Click to hand it back to the canvas.")
                     : T("The Session's launched clips will drive audio once the engine is started."));
            }
            else if (gAudioMode == AudioMode::Studio)
            {
               TopBarSameLine(4.0f);
               if (ChipButton::Draw(L("Studio##studiobadge"), true, tok::control_h, 0.0f, true))
                  gAudioMode = AudioMode::Canvas;
               if (ImGui::IsItemHovered())
                  HelpTip(engineOn
                     ? T("The Sample studio's take is driving audio. Click to hand it back to the canvas.")
                     : T("The Sample studio's take will drive audio once the engine is started."));
            }
            else if (gAudioMode == AudioMode::Mixer)
            {
               TopBarSameLine(4.0f);
               if (ChipButton::Draw(L("Mixer##mixerbadge"), true, tok::control_h, 0.0f, true))
                  SetMixerAudio(false);
               if (ImGui::IsItemHovered())
                  HelpTip(engineOn
                     ? T("The Mixer panel's console is driving audio. Click to hand it back to the canvas.")
                     : T("The Mixer panel's console will drive audio once the engine is started."));
            }
         };

         // Left cluster ends here: menus, transport, audio. Tempo, meter, click and key sit centred in the
         // bar; the readouts sit at the right. The centred group is placed from last frame's measured width.
         const float leftClusterEndX = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
         static float sCentreW = 0.0f;
         const float centreX = std::max(leftClusterEndX + tok::space_5,
                                        std::round((ImGui::GetWindowWidth() - sCentreW) * 0.5f));
         ImGui::SameLine(centreX);
         ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX(), (tok::bar_h - tok::control_h) * 0.5f));
         const float centreStartScreenX = ImGui::GetCursorScreenPos().x;
         // Transport sits just left of the tempo, audio power just right of it.
         if (!Platform::PluginModeActive()) // the DAW's transport plays and stops the plugin
         {
            drawTransport();
            SectionBreak();
         }

         static const int kDens[] = { 1, 2, 4, 8, 16 };
         auto SnapToValidDenominator = [](int val) -> int {
            int bestDen = kDens[0];
            int bestDist = std::abs(val - kDens[0]);
            for (int d : kDens)
            {
               int dist = std::abs(val - d);
               if (dist < bestDist)
               {
                  bestDist = dist;
                  bestDen = d;
               }
            }
            return bestDen;
         };
         auto DenToIdx = [](int d) -> int {
            for (int i = 0; i < 5; i++)
               if (kDens[i] == d) return i;
            return 2;
         };

         enum class TopBarField { None, Bpm, TsNum, TsDen };
         static TopBarField sActiveField = TopBarField::None;
         static char sFieldText[32] = "";
         static bool sFieldJustOpened = false;
         static float sDragAccumY = 0.0f;

         // 3. Global Key & Scale
         static const char* const kKeyNames[] = {
            "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
         };
         auto FormatScaleDisplayName = [](const std::string& name) -> std::string {
            std::string out = name;
            bool capNext = true;
            for (size_t i = 0; i < out.size(); i++)
            {
               if (std::isalpha((unsigned char)out[i]))
               {
                  if (capNext)
                  {
                     out[i] = (char)std::toupper((unsigned char)out[i]);
                     capNext = false;
                  }
               }
               else
               {
                  capNext = true;
               }
            }
            return out;
         };

         int curKey = transport.Key();
         int curScale = transport.Scale();
         const auto& scaleList = MusicTime::ScaleTypeList();
         const char* curScaleName = (curScale >= 0 && curScale < (int)scaleList.size()) ? scaleList[curScale].c_str() : "major";
         const std::string capScaleName = FormatScaleDisplayName(curScaleName);

         // ---- The display: Tempo | Signature | Key | click, one well, values only ----
         namespace TD = TransportDisplay;
         const float fieldPad = tok::space_2;
         const float cellPad = tok::space_2;
         float tempoCellW, numW, slashW, denW, sigCellW, keyW, scaleW, keyCellW;
         {
            tempoCellW = TD::ValueWidth("300.0") + tok::stage_menu_bar_tempo_cell_w_ratio * cellPad;
            numW = TD::ValueWidth("99") + fieldPad;
            denW = TD::ValueWidth("16") + fieldPad;
            slashW = TD::ValueWidth("/") + fieldPad;
            sigCellW = numW + slashW + denW + tok::stage_menu_bar_sig_cell_w_ratio * fieldPad;
            keyW = TD::ValueWidth("C#") + fieldPad;
            // Fixed for every scale: sized to the longest name, so choosing "Minor Pentatonic" never resizes the bar.
            scaleW = 0.0f;
            for (const std::string& n : scaleList)
               scaleW = std::max(scaleW, TD::ValueWidth(FormatScaleDisplayName(n).c_str()));
            scaleW += fieldPad;
            keyCellW = keyW + scaleW + tok::stage_menu_bar_key_cell_w_ratio * fieldPad;
            // The three cells share one width, the widest content, so the display reads as even thirds.
            const float cellW = std::max({ tempoCellW, sigCellW, keyCellW });
            tempoCellW = sigCellW = keyCellW = cellW;
         }
         const float centreY = ImGui::GetCursorScreenPos().y + tok::control_h * 0.5f;
         const ImVec2 wellMin(std::round(ImGui::GetCursorScreenPos().x), std::round(centreY - TD::kHeight * 0.5f));
         // The metronome is the display's last cell: a square the height of the well, set off by a hairline.
         // Not inside a DAW: the host plays its own click.
         const bool showClick = !Platform::PluginModeActive();
         const float clickCellW = showClick ? TD::kHeight : 0.0f;
         const ImVec2 wellMax(wellMin.x + tempoCellW + sigCellW + keyCellW + clickCellW, wellMin.y + TD::kHeight);
         TD::Well(wellMin, wellMax);
         TD::Divider(wellMin.x + tempoCellW, wellMin.y);
         TD::Divider(wellMin.x + tempoCellW + sigCellW, wellMin.y);
         if (showClick)
            TD::Divider(wellMin.x + tempoCellW + sigCellW + keyCellW, wellMin.y);

         // One editable number: drag up/down, double-click or type to edit. `onDrag` gets the vertical delta in
         // points; `onCommit` gets the typed text.
         auto Field = [&](TopBarField which, const char* id, float x, float w, const char* shown, const char* editInit,
                          bool decimal, auto&& onDrag, auto&& onCommit, const char* tip)
         {
            const ImVec2 mn(x, wellMin.y), mx(x + w, wellMin.y + TD::kHeight);
            ImGui::SetCursorScreenPos(mn);
            if (Platform::PluginModeActive() && !Transport::Instance().OwnTempo())
            {
               // Inside a DAW the host sets tempo and meter every block (unless Settings > DAW sync gives tempo to
                  // Infinite): show them, never edit them.
               UiInteract::Hit(id, ImVec2(w, TD::kHeight));
               TD::Value(id, x, w, wellMin.y, shown);
               if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                  HelpTip("%s", T("Set by the DAW"));
               return;
            }
            if (sActiveField == which)
            {
               // The field sits inside the cell with an even margin and its text centred, like the value it replaces.
               const float fieldH = TD::kHeight - tok::stage_menu_bar_field_h_ratio * tok::space_2;
               const float fieldW = w - tok::stage_menu_bar_field_w_ratio * tok::space_2;
               ImGui::SetCursorScreenPos(ImVec2(x + tok::space_2, wellMin.y + tok::space_2));
               ImGui::SetNextItemWidth(fieldW);
               if (sFieldJustOpened)
               {
                  UiInteract::FocusHere();
                  sFieldJustOpened = false;
               }
               const float textW = ImGui::CalcTextSize(sFieldText).x;
               const float padX = std::max(tok::stage_menu_bar_bound, std::floor((fieldW - textW) * 0.5f));
               ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padX, std::max(0.0f, std::floor((fieldH - ImGui::GetFontSize()) * 0.5f))));
               FieldWell::PushTypedEditStyle();
               const bool entered = FieldWell::InputText(id, sFieldText, sizeof(sFieldText),
                                                         ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
               FieldWell::PopTypedEditStyle();
               ImGui::PopStyleVar();
               if (entered || ImGui::IsItemDeactivated())
               {
                  onCommit(sFieldText);
                  sActiveField = TopBarField::None;
                  ImGui::GetCurrentContext()->NavCursorVisible = false; // Enter must not leave a focus ring behind
               }
               return;
            }
            UiInteract::Hit(id, ImVec2(w, TD::kHeight));
            const bool hov = ImGui::IsItemHovered();
            const float hv = UiAnim::Hover(ImGui::GetItemID(), hov, tok::motion_hover_in, tok::motion_hover_out);
            TD::Wash(mn, mx, hv, ImGui::IsItemActive());
            TD::Value(id, x, w, wellMin.y, shown);
            if (!ImGui::IsItemActive() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && tip != nullptr)
               HelpTip("%s", tip);
            if (hov)
            {
               ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
               if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
               {
                  sActiveField = which;
                  snprintf(sFieldText, sizeof(sFieldText), "%s", editInit);
                  sFieldJustOpened = true;
               }
               else
               {
                  ImGuiIO& io = ImGui::GetIO();
                  for (int i = 0; i < io.InputQueueCharacters.Size; ++i)
                  {
                     const ImWchar ch = io.InputQueueCharacters[i];
                     if ((ch >= '0' && ch <= '9') || (decimal && (ch == '.' || ch == '-')))
                     {
                        sActiveField = which;
                        sFieldText[0] = (char)ch;
                        sFieldText[1] = '\0';
                        sFieldJustOpened = true;
                        break;
                     }
                  }
               }
            }
            if (ImGui::IsItemActive())
               onDrag(-ImGui::GetIO().MouseDelta.y);
         };

         // Tempo
         {
            const float bpm = transport.Tempo();
            char shown[32], edit[32];
            snprintf(shown, sizeof(shown), "%s", UiFmt::F1(bpm).c);
            snprintf(edit, sizeof(edit), "%s", UiFmt::F1(bpm).c);
            Field(TopBarField::Bpm, "##bpmField", wellMin.x, tempoCellW, shown, edit, true,
                  [&](float dy) {
                     const float speed = ImGui::GetIO().KeyShift ? 0.05f : 0.25f;
                     transport.SetTempo(std::clamp(bpm + dy * speed, 20.0f, 300.0f));
                  },
                  [&](const char* text) {
                     char* end = nullptr;
                     const float parsed = strtof(text, &end);
                     if (end != text && parsed > 0.0f)
                        transport.SetTempo(std::clamp(parsed, 20.0f, 300.0f));
                  },
                  T("Tempo - drag, double-click or type to change.\nArrangement Timeline clips keep their bar/beat positions:\na tempo change moves their times in seconds, not their bars."));
         }

         // Signature
         {
            int tsNum = transport.TimeSigNumerator();
            const int tsDen = transport.TimeSigDenominator();
            const float sx = std::round(wellMin.x + tempoCellW + (sigCellW - (numW + slashW + denW)) * 0.5f);
            char numShown[16], denShown[16];
            snprintf(numShown, sizeof(numShown), "%d", tsNum);
            snprintf(denShown, sizeof(denShown), "%d", tsDen);
            Field(TopBarField::TsNum, "##tsNumField", sx, numW, numShown, numShown, false,
                  [&](float dy) {
                     sDragAccumY += dy;
                     const float kStep = 6.0f;
                     if (std::abs(sDragAccumY) >= kStep)
                     {
                        const int steps = (int)(sDragAccumY / kStep);
                        sDragAccumY -= steps * kStep;
                        tsNum = std::clamp(tsNum + steps, 1, 99);
                        transport.SetTimeSignature(tsNum, tsDen);
                     }
                  },
                  [&](const char* text) {
                     const int parsed = atoi(text);
                     if (parsed > 0)
                        transport.SetTimeSignature(std::clamp(parsed, 1, 99), tsDen);
                  },
                  nullptr);
            TD::Value("##tsSlash", sx + numW, slashW, wellMin.y, "/", 0.5f);   // ui-audit-ok: centre alignment (half)
            Field(TopBarField::TsDen, "##tsDenField", sx + numW + slashW, denW, denShown, denShown, false,
                  [&](float dy) {
                     sDragAccumY += dy;
                     const float kStep = 10.0f;
                     if (std::abs(sDragAccumY) >= kStep)
                     {
                        const int steps = (int)(sDragAccumY / kStep);
                        sDragAccumY -= steps * kStep;
                        const int curIdx = DenToIdx(tsDen);
                        const int nextIdx = std::clamp(curIdx + steps, 0, 4);
                        if (nextIdx != curIdx)
                           transport.SetTimeSignature(tsNum, kDens[nextIdx]);
                     }
                  },
                  [&](const char* text) {
                     transport.SetTimeSignature(tsNum, SnapToValidDenominator(atoi(text)));
                  },
                  nullptr);
         }

         // Key and scale: two pick fields that open lists.
         {
            auto Pick = [&](const char* id, float x, float w, const char* shown, const char* popup)
            {
               ImGui::SetCursorScreenPos(ImVec2(x, wellMin.y));
               if (UiInteract::Hit(id, ImVec2(w, TD::kHeight)))
                  ImGui::OpenPopup(popup);
               const float hv = UiAnim::Hover(ImGui::GetItemID(), ImGui::IsItemHovered(), tok::motion_hover_in, tok::motion_hover_out);
               TD::Wash(ImVec2(x, wellMin.y), ImVec2(x + w, wellMin.y + TD::kHeight), hv, ImGui::IsItemActive());
               TD::Value(id, x, w, wellMin.y, shown);
            };
            // The cell keeps one width (sized to "C#" and the longest scale); key and scale are sized to what they show,
            // so "C Major" sits as one centred pair, not a key pinned left of a wide scale slot.
            const char* keyShown = kKeyNames[std::clamp(curKey, 0, 11)];
            const float curKeyW = TD::ValueWidth(keyShown) + fieldPad;
            const float curScaleW = TD::ValueWidth(capScaleName.c_str()) + fieldPad;
            const float cellX = wellMin.x + tempoCellW + sigCellW;
            const float kx = std::round(cellX + (keyCellW - (curKeyW + curScaleW)) * 0.5f);
            Pick("##keyField", kx, curKeyW, keyShown, "##globalKeyPopup");
            Pick("##scaleField", kx + curKeyW, curScaleW, capScaleName.c_str(), "##globalScalePopup");
         }

         if (MenuParts::BeginPopup("##globalKeyPopup"))
         {
            for (int i = 0; i < 12; i++)
            {
               if (MenuParts::Choice(kKeyNames[i], i == curKey))
                  transport.SetKey(i);
            }
            MenuParts::EndPopup();
         }
         if (MenuParts::BeginPopup("##globalScalePopup"))
         {
            for (int i = 0; i < (int)scaleList.size(); i++)
            {
               const std::string capOpt = FormatScaleDisplayName(scaleList[i]);
               if (MenuParts::Choice(capOpt.c_str(), i == curScale))
                  transport.SetScale(i);
            }
            MenuParts::EndPopup();
         }

         // The click: the same tile as the panel rail, inside the display's last cell.
         if (!showClick)
            AudioEngine::Instance().SetMetronome(false, gMetronomeVolume, gMetronomeAccent);
         else
         {
         ImGui::SetCursorScreenPos(ImVec2(wellMax.x - clickCellW + (clickCellW - tok::control_h) * 0.5f, centreY - tok::control_h * 0.5f));
         {
            // Same tile as the panel toggles: outline glyph off, accent tile + filled glyph on.
            if (IconTile::Draw("##metronomeBtn", IconsInfinite::Metronome, IconsInfinite::MetronomeFill, gMetronomeOn,
                               tok::control_h, ImGui::GetFrameHeight(), true,
                               // Pendulum swings to the other side on every beat while the click is on and playing.
                               (gMetronomeOn && isTransportPlaying)
                                  ? (((long long)std::floor(transport.Beats()) & 1) ? 1.0f : -1.0f)
                                  : 0.0f))
               gMetronomeOn = !gMetronomeOn;
            if (MenuParts::ContextClicked())
               ImGui::OpenPopup("##metronomePopup");

            if (MenuParts::BeginPopup("##metronomePopup"))
            {
               // The top bar flattens every frame colour to transparent; a
               // slider needs its real theme frame back to be findable.
               const ImGuiStyle& base = ImGui::GetStyle();
               ImGui::PushStyleColor(ImGuiCol_FrameBg, base.Colors[ImGuiCol_FrameBg]);
               ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, base.Colors[ImGuiCol_FrameBgHovered]);
               ImGui::PushStyleColor(ImGuiCol_FrameBgActive, base.Colors[ImGuiCol_FrameBgActive]);
               ImGui::SetNextItemWidth(tok::stage_menu_bar_set_next_item_width_w_2);
               const bool volChanged = FieldWell::Slider(L("volume##metronomeVol"), &gMetronomeVolume, 0.0f, 1.0f, "%.2f");
               ImGui::PopStyleColor(3);
               if (volChanged)
                  gMetronomeDirty = true;
               if (MenuParts::Check(L("accent first beat"), &gMetronomeAccent))
                  gMetronomeDirty = true;
               MenuParts::EndPopup();
            }
            if (gMetronomeDirty && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
               SaveGeneralSettings();
               gMetronomeDirty = false;
            }
            AudioEngine::Instance().SetMetronome(gMetronomeOn, gMetronomeVolume, gMetronomeAccent);
         }
         }

         // Audio power, just right of the click.
         if (!Platform::PluginModeActive()) // the DAW owns the audio device
         {
            SectionBreak();
            ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, centreY - tok::control_h * 0.5f));
            drawAudio();
         }

         // Width of the centred group, measured for next frame's placement.
         sCentreW = ImGui::GetItemRectMax().x - centreStartScreenX;
         float centreEndX = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
         if (gAudioMode != AudioMode::Canvas)
         {
            drawTimelineBadge();
            centreEndX = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
         }

         // 4. Telemetry (frame cost, CPU load) at the right edge, with Update beside it when a newer
         // version exists. The panel toggles live on the rail down the window's right edge.
         static double sSmoothedMs = 0.0;
         sSmoothedMs = (sSmoothedMs <= 0.0)
                          ? gLastFrameMs
                          : sSmoothedMs * 0.9 + gLastFrameMs * 0.1;   // ui-audit-ok: frame time smoothing
         const double fps = sSmoothedMs > 0.0001 ? 1000.0 / sSmoothedMs : 0.0;

         char readout[80];
         snprintf(readout, sizeof(readout), "%s fps   %s", UiFmt::F1(fps).c, UiFmt::Ms(sSmoothedMs).c);

         const bool audioEngineOn = AudioEngine::Instance().SampleRate() > 0.0;
         const double audioLoad = AudioEngine::Instance().LastBlockLoad();
         const AudioEngine::XrunCounts xrunParts = AudioEngine::Instance().Xruns();
         const uint64_t xruns = xrunParts.Total();
         const bool audioDead = audioEngineOn && !AudioEngine::Instance().IsAlive();
         char cpuReadout[32];
         if (audioDead)
            snprintf(cpuReadout, sizeof(cpuReadout), "%s", T("cpu lost"));
         else if (audioEngineOn)
            snprintf(cpuReadout, sizeof(cpuReadout), "cpu %s%s", UiFmt::PctOf(audioLoad).c, xruns > 0 ? " !" : "");
         else
            snprintf(cpuReadout, sizeof(cpuReadout), "cpu --");

         auto TabWidth = [](const char* widest, const char* text)
         {
            UiType::Scope ts(UiType::Size::Body, UiType::Weight::Regular);
            return std::max(Readout::Measure(widest), Readout::Measure(text));
         };
         const char* cpuWidest = audioDead ? cpuReadout : "cpu 99%";
         const float fpsW = TabWidth("99.9 fps   99.9 ms", readout);
         const float cpuW = TabWidth(cpuWidest, cpuReadout);
         const float telemetryGap = tok::space_2;
         const float telemetryW = fpsW + telemetryGap + cpuW;

         const float windowRight = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - tok::space_3;
         const float minGap = tok::space_4;
         float cursorX = windowRight;

         // Update: a quiet icon tile (down arrow in an accent wash, with a dot) that only exists while a newer
         // version is available. Click opens the update dialog (Update now, in place); right-click dismisses until the
         // next version.
         bool showUpdate = UpdateCheck::UpdateAvailable() && !Platform::PluginModeActive(); // the app updates, not the plugin
#ifndef NDEBUG
         if (getenv("INFINITE_SHOW_UPDATE_ICON") != nullptr)
            showUpdate = true;
#endif
         if (showUpdate)
         {
            const float updateWidth = tok::control_h;
            if (cursorX - updateWidth - telemetryGap - telemetryW >= centreEndX + minGap)
            {
               cursorX -= updateWidth;
               TopBarPlaceAt(cursorX);
               ImGui::PushID("##updateicon");
               const bool clicked = UiInteract::Hit("##update", ImVec2(tok::control_h, tok::control_h));
               ImGui::PopID();
               const bool hov = ImGui::IsItemHovered();
               const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
               const ImVec2 c((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
               ImDrawList* dl = ImGui::GetWindowDrawList();
               const ImVec4 ac = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
               dl->AddRectFilled(mn, mx, ImGui::GetColorU32(ImVec4(ac.x, ac.y, ac.z, hov ? tok::stage_menu_bar_alpha_5 : tok::stage_menu_bar_alpha_6)), tok::radius_control);
               const ImU32 fg = ImGui::GetColorU32(ac);
               glyph::Draw(dl, c, tok::icon_lg, fg, IconsInfinite::Download);
               StatusDot::OnLastItem(StatusDot::State::Error);
               if (clicked)
                  gShowUpdateCheckModal = true;   // the result is already in; the dialog offers Update now
               if (hov)
               {
                  const std::string& lv = UpdateCheck::LatestVersion();
                  Tip::Show(T("version %s is available (you have %s) - click to update, right-click to dismiss"),
                                     lv.empty() ? "?" : lv.c_str(), INFINITE_VERSION_STRING);
               }
               if (MenuParts::ContextClicked())
                  UpdateCheck::Dismiss();
               cursorX -= telemetryGap;
            }
         }

         // Inside a DAW (plugin mode, README 12.4): pop-out, picture/canvas, the host slots, and
         // blocks the host played as silence because the engine was late (only once there are any).
         if (Platform::PluginModeActive())
         {
            auto fits = [&](float w) { return cursorX - w - telemetryGap - telemetryW >= centreEndX + minGap; };
            if (fits(tok::control_h))
            {
               cursorX -= tok::control_h;
               TopBarPlaceAt(cursorX);
               const bool popped = PluginView::Popped();
               if (IconTile::Draw("##popout", IconsInfinite::PopOut, IconsInfinite::PopOut, popped, tok::control_h,
                                  ImGui::GetFrameHeight()))
                  PluginView::RequestPopped(!popped);
               if (ImGui::IsItemHovered())
                  Tip::Show("%s", popped ? T("Back to the plugin") : T("Pop out"));
               cursorX -= telemetryGap;
            }
            if (fits(tok::control_h))
            {
               cursorX -= tok::control_h;
               TopBarPlaceAt(cursorX);
               const bool picture = PluginView::Picture();
               if (IconTile::Draw("##picture", IconsInfinite::Viewport, IconsInfinite::ViewportFill, picture, tok::control_h,
                                  ImGui::GetFrameHeight()))
                  PluginView::SetPicture(!picture);
               if (ImGui::IsItemHovered())
                  Tip::Show("%s", picture ? T("Canvas") : T("Picture"));
               cursorX -= telemetryGap;
            }
            if (fits(tok::control_h))
            {
               cursorX -= tok::control_h;
               TopBarPlaceAt(cursorX);
               if (IconTile::Draw("##slots", IconsInfinite::FamilyMacros, IconsInfinite::FamilyMacros,
                                  ImGui::IsPopupOpen("##slotsmap"), tok::control_h, ImGui::GetFrameHeight()))
                  ImGui::OpenPopup("##slotsmap");
               if (ImGui::IsItemHovered() && !ImGui::IsPopupOpen("##slotsmap"))
                  Tip::Show("%s", T("Host slots"));
               if (MenuParts::BeginPopup("##slotsmap"))
               {
                  int shown = 0;
                  for (int i = 0; i < PluginEngine::kSlotCount; i++)
                  {
                     const std::string name = PluginEngine::SlotName(i);
                     if (name.empty())
                        continue;
                     ImGui::Text("%s  %s", UiFmt::Pad2(i + 1).c, name.c_str());
                     shown++;
                  }
                  if (shown == 0)
                     ImGui::TextDisabled("%s", T("No Macro is on a host slot"));
                  MenuParts::EndPopup();
               }
               cursorX -= telemetryGap;
            }
            const uint64_t hostXruns = PluginEngine::HostXruns();
            if (hostXruns > 0)
            {
               char xr[32];
               snprintf(xr, sizeof(xr), "%llu xruns", (unsigned long long)hostXruns);
               const float w = TabWidth("99999 xruns", xr);
               if (fits(w))
               {
                  cursorX -= w;
                  TopBarPlaceAt(cursorX);
                  TabLabel("topbar.hostxruns", xr, "99999 xruns", true);
                  if (ImGui::IsItemHovered())
                     Tip::Show("%s", T("Late blocks the host played as silence"));
                  cursorX -= telemetryGap;
               }
            }
         }

         // On a narrow window the readouts are dropped rather than drawn over the centred group.
         // INFINITE_HIDE_TELEMETRY drops them for screenshot and manual runs, where a live frame rate is noise.
         static const bool sHideTelemetry = getenv("INFINITE_HIDE_TELEMETRY") != nullptr;
         if (!sHideTelemetry && cursorX - telemetryW >= centreEndX + minGap)
         {
            TopBarPlaceAt(cursorX - telemetryW);
            TabLabel("topbar.fps", readout, "99.9 fps   99.9 ms", true);
            TopBarSameLine(telemetryGap);
            TabLabel("topbar.cpu", cpuReadout, cpuWidest, true);

            if (audioEngineOn && xruns > 0 && ImGui::IsItemHovered())
               Tip::Show(T("xruns=%llu this session\n%llu late block(s) (render over the deadline)\n%llu reported by the audio device"),
                                 (unsigned long long)xruns,
                                 (unsigned long long)xrunParts.deadline,
                                 (unsigned long long)xrunParts.os);
         }

         ImGui::PopStyleColor(6);
         ImGui::PopStyleVar(4);

         // Bottom edge: separates the bar from the canvas.
         {
            const ImRect r = ImGui::GetCurrentWindow()->MenuBarRect();
            ImGui::GetWindowDrawList()->AddLine(ImVec2(r.Min.x, r.Max.y - tok::px_align), ImVec2(r.Max.x, r.Max.y - tok::px_align),
                                                ImGui::GetColorU32(ImGuiCol_Border), tok::stage_menu_bar_stroke);
         }
         ImGui::EndMenuBar();
      }

      DrawPanelRail();
   }
}
