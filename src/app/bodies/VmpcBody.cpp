// VMPC body: the MPC card's layout (4x4 square pads, the selected pad's editor
// below), with a clip's filmstrip where the MPC draws a waveform.
#include "app/ui/design/components/EmptyState.h"
#include "app/graph/NodeParams.h"
#include "app/ui/design/components/Thumb.h"
#include "app/ui/design/components/MenuParts.h"
#include "app/ui/design/UiInteract.h"
#include "app/ui/design/TokenColors.h"
#include "app/AppShared.h"
#include "app/ui/design/components/AudioViz.h"
#include "app/ui/design/components/FormParts.h"
#include "app/ui/design/UiFmt.h"

namespace app
{
   // ---- VMPC modulation addressing -----------------------------------------
   // As the MPC: every pad's every param has an address that is a function of
   // (pad, param) only. Floats at VmpcNode::ParamId(pad, k), the three
   // dropdowns at label-hashed slots whose label carries the pad. The selected
   // pad only decides which pad's widgets are drawn; the other fifteen register
   // under gParamRegisterOnly, so a cable never moves when the selection does.
   namespace
   {
      enum VmpcDiscreteKind { kVmpcMode = 0, kVmpcSync, kVmpcDiv, kVmpcNumDiscrete };

      const std::vector<std::string>& VmpcModeList()
      {
         static const std::vector<std::string> kModes = { "one shot", "gate", "loop" };
         return kModes;
      }

      const std::vector<std::string>& VmpcSyncList()
      {
         static const std::vector<std::string> kSync = { "Synced", "Free" };
         return kSync;
      }

      const std::vector<std::string>& VmpcTransitionList()
      {
         static const std::vector<std::string> kList = { "cut", "crossfade", "dip to black" };
         return kList;
      }

      std::string VmpcParamName(int pad, int k)
      {
         char buf[48];
         snprintf(buf, sizeof(buf), "pad %d %s", pad + 1, VmpcNode::Info(k).name);
         return buf;
      }

      std::string VmpcDiscreteLabel(int pad, int which)
      {
         char buf[48];
         snprintf(buf, sizeof(buf),
                  which == kVmpcMode ? "pad %d mode##vmpcmode%d"
                                     : (which == kVmpcSync ? "pad %d sync##vmpcsync%d" : "pad %d rate##vmpcdiv%d"),
                  pad + 1, pad);
         return buf;
      }

      int* VmpcDiscreteValue(VmpcNode* n, int pad, int which)
      {
         pad = VmpcNode::Clamp(pad);
         return which == kVmpcMode ? &n->padMode[pad] : (which == kVmpcSync ? &n->padSync[pad] : &n->padDiv[pad]);
      }

      const std::vector<std::string>& VmpcDiscreteList(int which)
      {
         return which == kVmpcMode ? VmpcModeList() : (which == kVmpcSync ? VmpcSyncList() : MusicTime::RateDivisionList());
      }

      int VmpcDiscreteMax(int which)
      {
         return which == kVmpcMode ? 2 : (which == kVmpcSync ? 1 : (int)MusicTime::kNumRateDivisions - 1);
      }

      void RegisterVmpcPadFloats(VmpcNode* n, int pad)
      {
         const bool saved = gParamRegisterOnly;
         gParamRegisterOnly = true;
         for (int k = 0; k < VmpcNode::kNumPadParams; k++)
         {
            const VmpcNode::ParamInfo& info = VmpcNode::Info(k);
            const std::string name = VmpcParamName(pad, k);
            ModSlider(info.name, n->PadParamPtr(pad, k), info.lo, info.hi, info.fmt, params::vmpc::register_pad_floats_lo, /*audioStyle=*/true, params::vmpc::register_pad_floats_hi,
                      nullptr, nullptr, VmpcNode::ParamId(pad, k), name.c_str());
         }
         gParamRegisterOnly = saved;
      }

      // Fixed order whatever is selected (see RegisterMpcParams): 16 modes, 16
      // syncs, 16 divisions, so a hash-collision probe never depends on the selection.
      void RegisterVmpcParams(VmpcNode* n, int drawnPad)
      {
         for (int p = 0; p < VmpcNode::kPads; p++)
            if (p != drawnPad)
               RegisterVmpcPadFloats(n, p);
         for (int which = 0; which < kVmpcNumDiscrete; which++)
            for (int p = 0; p < VmpcNode::kPads; p++)
            {
               const std::string label = VmpcDiscreteLabel(p, which);
               int* value = VmpcDiscreteValue(n, p, which);
               const int hi = VmpcDiscreteMax(which);
               const DiscreteParamHandle h = RegisterDiscreteParam(label.c_str(), (float)*value, (float)hi,
                                                                   /*isBool=*/false, &VmpcDiscreteList(which));
               if (h.driven)
                  *value = std::clamp((int)lroundf(h.value), 0, hi);
            }
      }

      std::vector<int> VmpcPadBoundPins(int pad, int nodeIndex)
      {
         std::vector<int> pins;
         for (int k = 0; k < VmpcNode::kNumPadParams; k++)
            if (Modulation::Instance().IsModulated(nodeIndex, VmpcNode::ParamId(pad, k)))
               pins.push_back(VmpcNode::ParamId(pad, k));
         for (int which = 0; which < kVmpcNumDiscrete; which++)
         {
            const int slot = DiscreteParamSlot(nodeIndex, VmpcDiscreteLabel(pad, which));
            if (Modulation::Instance().IsModulated(nodeIndex, slot))
               pins.push_back(slot);
         }
         return pins;
      }

      // Thumbnail `idx` of a pad's filmstrip into [mn, mx], cover-cropped. The
      // atlas rows are in the decoder's order, so v runs 1 -> 0 (as every
      // video texture in the app is drawn).
      void DrawThumb(ImDrawList* dl, unsigned int tex, int idx, ImVec2 mn, ImVec2 mx, ImU32 tint)
      {
         const float bw = std::max(1.0f, mx.x - mn.x), bh = std::max(1.0f, mx.y - mn.y);
         const float ta = (float)VmpcNode::kThumbW / (float)VmpcNode::kThumbH;
         const float ba = bw / bh;
         float uHalf = 0.5f, vHalf = 0.5f; // of one thumbnail
         if (ba < ta)
            uHalf = 0.5f * ba / ta;
         else
            vHalf = 0.5f * ta / ba;
         const float cell = 1.0f / (float)VmpcNode::kThumbs;
         const float uc = ((float)std::clamp(idx, 0, VmpcNode::kThumbs - 1) + 0.5f) * cell;
         Thumb::Add(dl, (ImTextureID)(intptr_t)tex, mn, mx, ImVec2(uc - uHalf * cell, tok::px_align + vHalf),
                      ImVec2(uc + uHalf * cell, tok::px_align - vHalf), tint);
      }

      int ThumbIndexAt(float frac)
      {
         return std::clamp((int)(frac * (float)VmpcNode::kThumbs), 0, VmpcNode::kThumbs - 1);
      }
   }

   // Routes dropped video files to pads: the pad under the drop, else the
   // filmstrip (the selected pad), else the first empty pad, else the selected
   // one. Further files in one drop fill the next empty pads.
   void VmpcDropFiles(VmpcNode* n, float cx, float cy, const std::vector<std::string>& paths)
   {
      int pad = n->PadAtCanvas(cx, cy);
      if (pad < 0 && cx >= n->stripRect[0] && cx <= n->stripRect[2] && cy >= n->stripRect[1] && cy <= n->stripRect[3])
         pad = VmpcNode::Clamp(n->selectedPad);
      if (pad < 0)
      {
         pad = n->NextEmptyPad(0);
         if (pad < 0)
            pad = VmpcNode::Clamp(n->selectedPad);
      }
      for (const std::string& path : paths)
      {
         if (pad < 0)
            break;
         if (n->LoadPad(pad, path))
            n->selectedPad = pad;
         const int next = n->NextEmptyPad(pad + 1);
         pad = (next >= 0 && next != pad) ? next : -1;
      }
   }

   void DrawVmpcBody(GraphNode& gn, VmpcNode* n)
   {
      int loaded = 0;
      for (int p = 0; p < VmpcNode::kPads; p++)
         loaded += n->PadLoaded(p) ? 1 : 0;
      char stat[112];
      if (loaded == 0)
         snprintf(stat, sizeof(stat), "empty - drop clips here, then click a pad or send notes 36-51");
      else
         snprintf(stat, sizeof(stat), "%d/16 loaded - click a pad or send notes 36-51", loaded);
      BeginAudioBody(gn.index, gn.category, MpcNodeWidth(), stat);
      const bool isLight = IsThemeLight();
      const double now = ImGui::GetTime();
      int openLoadPad = -1;
      const int sel = VmpcNode::Clamp(n->selectedPad);
      n->selectedPad = sel;

      RegisterVmpcParams(n, sel);
      std::vector<int> boundPins[VmpcNode::kPads];
      for (int p = 0; p < VmpcNode::kPads; p++)
         boundPins[p] = VmpcPadBoundPins(p, gn.index);

      // 4x4 pad grid, pad 1 bottom-left, exactly the MPC's: square pads with a
      // gate pin gutter to the left. The picture sits where the MPC's waveform does.
      {
         const float gap = ImGui::GetStyle().ItemSpacing.x;
         const float side = kMpcPadSide;
         const float cellW = kMpcPinGutter + side;
         const float x0 = gAudioContentX;
         const float y0 = ImGui::GetCursorScreenPos().y;
         static const char* kModeTag[3] = { "shot", "gate", "loop" };
         for (int row = 0; row < 4; row++)
         {
            for (int col = 0; col < 4; col++)
            {
               const int pad = (tok::vmpc_body_gap - row) * tok::vmpc_pad_ratio + col;
               char id[16];
               snprintf(id, sizeof(id), "vpad%d", pad + 1);
               ImGui::SetCursorScreenPos(ImVec2(x0 + (float)col * (cellW + gap), y0 + (float)row * (side + gap)));
               const bool isLoaded = n->PadLoaded(pad);
               const bool isSel = pad == sel;
               const bool playing = n->PadPlaying(pad);
               const bool anyMod = !boundPins[pad].empty();
               const unsigned int thumbs = isLoaded ? n->ThumbTexture(pad) : 0;
               ImVec2 modDot(0.0f, 0.0f);
               bool activated = false;
               const bool level = DrawGateButton(id, cellW, side,
                  [&](ImDrawList* dl, ImVec2 mn, ImVec2 mx, bool hovered, bool lvl)
                  {
                     n->padRect[pad][0] = mn.x; n->padRect[pad][1] = mn.y;
                     n->padRect[pad][2] = mx.x; n->padRect[pad][3] = mx.y;
                     if (hovered && MenuParts::RightPressed())
                        n->selectedPad = pad;
                     const bool hit = lvl || (now - n->padFlash[pad]) < tok::motion_s_pad_flash;
                     const bool lit = playing || hit;
                     const ImU32 green = isLight ? tok::Base(tok::col::state_ok_4) : tok::Base(tok::col::state_ok_5);
                     ImU32 fill = isLoaded ? (isLight ? tok::Base(tok::col::surface_hi) : tok::Base(tok::col::edge_lo))
                                           : (isLight ? tok::Base(tok::col::edge_hi) : tok::Base(tok::col::edge_deep));
                     if (hovered && !lit)
                        fill = isLight ? tok::Base(tok::col::surface_hi) : tok::Base(tok::col::edge_lo);
                     if (lit)
                        fill = green;
                     dl->AddRectFilled(mn, mx, fill, tok::vmpc_body_radius);
                     const ImU32 selCol = isLight ? tok::Base(tok::col::accent) : tok::Base(tok::col::accent_4);
                     const ImU32 edge = isLight ? tok::Base(tok::col::edge_mid_2) : tok::Base(tok::col::surface_lo_2, 200);
                     dl->AddRect(mn, mx, isSel ? selCol : edge, tok::vmpc_body_radius, 0, isSel ? tok::vmpc_body_stroke : tok::vmpc_body_stroke_2);

                     const ImU32 ink = lit ? tok::Base(tok::col::ink_white)
                                           : (isLoaded ? (isLight ? tok::Base(tok::col::edge_lo) : tok::Base(tok::col::surface_hi))
                                                       : tok::Base(tok::col::edge_mid));
                     const ImU32 dim = lit ? tok::Base(tok::col::ink_white, 190) : (isLight ? tok::Base(tok::col::edge_lo_3) : tok::Base(tok::col::text_secondary));
                     dl->PushClipRect(ImVec2(mn.x + 1.0f, mn.y + 1.0f), ImVec2(mx.x - 1.0f, mx.y - 1.0f), true);
                     const float lh = AudioLabelFontSize();
                     char num[16];
                     snprintf(num, sizeof(num), "%d", pad + 1);
                     AudioLabelText(dl, ImVec2(mn.x + tok::space_1_25, mn.y + tok::space_0_75), ink, num);
                     if (isLoaded)
                     {
                        char tag[32];
                        snprintf(tag, sizeof(tag), "%s", kModeTag[std::clamp(n->padMode[pad], 0, 2)]);
                        if (n->padSync[pad] == VmpcNode::kSynced)
                           snprintf(tag, sizeof(tag), "%s %s", kModeTag[std::clamp(n->padMode[pad], 0, 2)],
                                    MusicTime::RateDivisionName(n->padDiv[pad]));
                        const ImVec2 ts = AudioLabelSize(tag);
                        AudioLabelText(dl, ImVec2(mx.x - ts.x - tok::space_1_25, mn.y + tok::space_0_75), dim, tag);

                        // The picture: the filmstrip frame nearest the pad's
                        // position (its window start when idle), in the box
                        // the MPC gives its waveform.
                        const ImVec2 pmn(mn.x + tok::space_1_25, mn.y + lh + tok::vmpc_body_gap_2);
                        const ImVec2 pmx(mx.x - tok::space_1_25, mx.y - lh - tok::vmpc_body_gap_2);
                        const float cur = n->PadCursor(pad);
                        const float at = cur >= 0.0f ? cur : n->padStart[pad] / 100.0f;
                        if (thumbs != 0)
                           DrawThumb(dl, thumbs, ThumbIndexAt(at), pmn, pmx, tok::Role(tok::col::ink_white));
                        else
                        {
                           AudioViz::Fill(dl, pmn, pmx);
                           const char* cap = n->PadIsHap(pad) ? "hap" : "no preview";
                           const ImVec2 cs = AudioLabelSize(cap);
                           AudioLabelText(dl, ImVec2((pmn.x + pmx.x - cs.x) * 0.5f, (pmn.y + pmx.y - cs.y) * 0.5f), dim, cap);
                        }
                        if (playing && cur >= 0.0f)
                        {
                           // Progress through the clip along the picture's bottom edge.
                           const float px = pmn.x + (pmx.x - pmn.x) * std::clamp(cur, 0.0f, 1.0f);
                           dl->AddRectFilled(ImVec2(pmn.x, pmx.y - tok::space_0_5), ImVec2(pmx.x, pmx.y), tok::Role(tok::col::ink_black, tok::vmpc_body_ink_black_alpha_2));
                           dl->AddRectFilled(ImVec2(pmn.x, pmx.y - tok::space_0_5), ImVec2(px, pmx.y), tok::Role(tok::col::ink_white, tok::vmpc_body_ink_white_alpha_3));
                        }
                        std::string nm = n->PadName(pad);
                        const size_t dot = nm.find_last_of('.');
                        if (dot != std::string::npos && dot > 0)
                           nm.resize(dot);
                        dl->PushClipRect(ImVec2(mn.x + 1.0f, mn.y + 1.0f), ImVec2(mx.x - tok::space_4, mx.y - 1.0f), true);
                        AudioLabelText(dl, ImVec2(mn.x + tok::space_1_25, mx.y - lh - tok::space_1), dim, nm.c_str());
                        dl->PopClipRect();
                     }
                     else
                     {
                        const char* hint = "+ load";
                        const ImVec2 hs = AudioLabelSize(hint);
                        AudioLabelText(dl, ImVec2((mn.x + mx.x - hs.x) * 0.5f, (mn.y + mx.y - hs.y) * 0.5f), dim, hint);
                     }
                     modDot = ImVec2(mx.x - tok::space_2, mx.y - tok::space_2);
                     if (anyMod)
                     {
                        dl->AddCircleFilled(modDot, tok::vmpc_body_r, isLight ? tok::Base(tok::col::state_warn) : tok::Base(tok::col::role_modulation));
                        dl->AddCircle(modDot, tok::vmpc_body_r, isLight ? tok::Base(tok::col::edge_mid) : tok::Base(tok::col::surface_lo), tok::vmpc_body_segments, 1.0f);
                     }
                     dl->PopClipRect();
                  },
                  &activated);
               if (activated)
               {
                  n->selectedPad = pad;
                  if (!isLoaded)
                     openLoadPad = pad; // an empty pad has nothing to play: a click means "put a clip here"
               }
               if (level)
                  n->padFlash[pad] = now;
               n->SetPadHeld(pad, level, 1.0f);
               if (!isSel && anyMod && modDot.x != 0.0f)
                  for (int slot : boundPins[pad])
                     NoteHiddenParamAnchor(gn.index, slot, modDot);
            }
         }
         ImGui::SetCursorScreenPos(ImVec2(x0, y0));
         ImGui::Dummy(ImVec2(gAudioContentW, tok::vmpc_body_ratio * side + tok::vmpc_body_ratio_2 * gap));
      }
      ImGui::Dummy(ImVec2(0.0f, tok::vmpc_body_dummy_h));

      // Selected pad: header, filmstrip (also the drop target), Load / Folder /
      // Clear, then the pad's rows.
      {
         const int cur = VmpcNode::Clamp(n->selectedPad);
         char header[96];
         if (n->PadLoaded(cur))
            snprintf(header, sizeof(header), "pad %d - %s", cur + 1, n->PadName(cur).c_str());
         else if (n->PadHasPath(cur))
            snprintf(header, sizeof(header), "pad %d - missing file", cur + 1);
         else
            snprintf(header, sizeof(header), "pad %d - empty", cur + 1);
         BeginAudioSection(header);

         {
            const float w = AudioFullWidth();
            const float h = tok::vmpc_h;
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const ImVec2 br(origin.x + w, origin.y + h);
            n->stripRect[0] = origin.x; n->stripRect[1] = origin.y; n->stripRect[2] = br.x; n->stripRect[3] = br.y;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const bool has = n->PadLoaded(cur);
            UiInteract::Hit("##vmpcstrip", ImVec2(w, h));
            const bool hov = ImGui::IsItemHovered();
            if (!has && ImGui::IsItemClicked(ImGuiMouseButton_Left))
               openLoadPad = cur;
            AudioViz::Fill(dl, origin, br);
            dl->PushClipRect(origin, br, true);
            if (has)
            {
               const unsigned int tex = n->ThumbTexture(cur);
               const float cw = w / (float)VmpcNode::kThumbs;
               if (tex != 0)
                  for (int i = 0; i < VmpcNode::kThumbs; i++)
                     DrawThumb(dl, tex, i, ImVec2(origin.x + cw * (float)i, origin.y),
                               ImVec2(origin.x + cw * (float)(i + 1), br.y), tok::Role(tok::col::ink_white));
               else
                  EmptyState::DrawCaption(origin, br, n->PadLoading(cur) ? T("Loading preview...")
                                                      : (n->PadIsHap(cur) ? T("HAP clip - no filmstrip") : T("No preview")));
               // The play window: outside start..end is dimmed, its edges marked.
               float a = std::clamp(n->padStart[cur] / 100.0f, 0.0f, 1.0f);
               float b = std::clamp(n->padEnd[cur] / 100.0f, 0.0f, 1.0f);
               if (b < a)
                  std::swap(a, b);
               const float xa = origin.x + w * a, xb = origin.x + w * b;
               dl->AddRectFilled(origin, ImVec2(xa, br.y), tok::Role(tok::col::ink_black, tok::vmpc_body_ink_black_alpha_2));
               dl->AddRectFilled(ImVec2(xb, origin.y), br, tok::Base(tok::col::ink_black, tok::vmpc_body_ink_black_alpha_2));
               const ImU32 edgeCol = isLight ? tok::Base(tok::col::accent) : tok::Base(tok::col::accent_4);
               dl->AddLine(ImVec2(xa, origin.y), ImVec2(xa, br.y), edgeCol, tok::vmpc_body_stroke);
               dl->AddLine(ImVec2(xb, origin.y), ImVec2(xb, br.y), edgeCol, tok::vmpc_body_stroke);
               const float at = n->PadCursor(cur);
               if (n->PadPlaying(cur) && at >= 0.0f)
               {
                  const float px = origin.x + w * std::clamp(at, 0.0f, 1.0f);
                  dl->AddLine(ImVec2(px, origin.y), ImVec2(px, br.y), tok::Role(tok::col::ink_white, tok::vmpc_body_ink_white_alpha_4), tok::vmpc_body_stroke_3);
               }
               if (hov)
               {
                  char val[32];
                  const float frac = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / w, 0.0f, 1.0f);
                  snprintf(val, sizeof(val), "%s", UiFmt::Sec(frac * (float)n->PadDuration(cur)).c);
                  SetAudioReadout("time", val);
               }
            }
            else
               EmptyState::DrawCaption(origin, br, n->PadHasPath(cur) ? T("File missing - drop a clip or Clear")
                                                                      : T("No clip loaded"));
            dl->PopClipRect();
            AudioViz::Border(dl, origin, br, (!has && hov) ? (isLight ? tok::Base(tok::col::accent) : tok::Base(tok::col::accent_4)) : 0);
            if (!has && hov)
               ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
         }
         ImGui::Dummy(ImVec2(0.0f, tok::vmpc_body_dummy_h));

         const float thirdW = (AudioFullWidth() - ImGui::GetStyle().ItemSpacing.x * tok::vmpc_body_ratio_3) / tok::vmpc_third_w_ratio;
         if (ActionButton::Draw("Load...##vmpc", ImVec2(thirdW, 0)))
            openLoadPad = cur;
         FormParts::Inline();
         if (ActionButton::Draw("Folder...##vmpc", ImVec2(thirdW, 0)))
         {
            const std::string folder = Platform::OpenFolderDialog("Load a folder of clips into the 16 pads");
            if (!folder.empty())
            {
               PushUndoCheckpoint();
               n->LoadFolder(folder);
            }
         }
         FormParts::Inline();
         ImGui::BeginDisabled(!n->PadHasPath(cur));
         if (ActionButton::Draw("Clear##vmpc", ImVec2(thirdW, 0)))
         {
            PushUndoCheckpoint();
            n->ClearPad(cur);
         }
         ImGui::EndDisabled();
         ImGui::Dummy(ImVec2(0.0f, tok::vmpc_body_dummy_h));

         auto slider = [&](int k, float width)
         {
            const VmpcNode::ParamInfo& info = VmpcNode::Info(k);
            const std::string name = VmpcParamName(cur, k);
            AudioSlider(info.name, n->PadParamPtr(cur, k), info.lo, info.hi, info.fmt, width, nullptr, nullptr,
                        VmpcNode::ParamId(cur, k), name.c_str());
         };
         auto dropdown = [&](int which, float width)
         {
            const std::string label = VmpcDiscreteLabel(cur, which);
            int* value = VmpcDiscreteValue(n, cur, which);
            DropdownButton(label.c_str(), VmpcDiscreteList(which), *value,
                           [value](int i) { PushUndoCheckpoint(); *value = i; }, width, /*showCaption=*/false);
         };
         // The MPC's two-column grid: timing, window, playback, mode, fades.
         dropdown(kVmpcSync, AudioHalfWidth());
         FormParts::Inline();
         ImGui::BeginDisabled(n->padSync[cur] != VmpcNode::kSynced);
         dropdown(kVmpcDiv, AudioHalfWidth());
         ImGui::EndDisabled();
         slider(VmpcNode::kStart, AudioHalfWidth());
         FormParts::Inline();
         slider(VmpcNode::kEnd, AudioHalfWidth());
         slider(VmpcNode::kSpeed, AudioHalfWidth());
         FormParts::Inline();
         slider(VmpcNode::kOpacity, AudioHalfWidth());
         dropdown(kVmpcMode, AudioFullWidth());
         slider(VmpcNode::kFadeIn, AudioHalfWidth());
         FormParts::Inline();
         slider(VmpcNode::kFadeOut, AudioHalfWidth());
         EndAudioSection();
      }

      // What every pad shares: how one picture gives way to the next, and the sound.
      BeginAudioSection("output");
      {
         AudioKnobRow row(tok::vmpc_body_audio_knob_row_layout);
         row.Dropdown("transition##vmpctrans", VmpcTransitionList(), n->transition,
                      [n](int i) { PushUndoCheckpoint(); n->transition = i; });
         ImGui::BeginDisabled(n->transition == VmpcNode::kCut);
         row.Knob("time", &n->transitionMs, params::Vmpc::transition_ms_lo, params::Vmpc::transition_ms_hi, "%.0f ms", kKnobSmall, false, false,
                  AudioWidgetStyle::Knob, nullptr, nullptr, VmpcNode::kTimeParamId, "transition time");
         ImGui::EndDisabled();
         bool changed = false;
         row.Checkbox("hold frame##vmpchold", &n->holdLast, &changed);
         if (changed)
            PushUndoCheckpoint();
         row.Checkbox("soundtrack##vmpcsnd", &n->soundtrack, &changed);
         if (changed)
            PushUndoCheckpoint();
         row.End();
         int skipped = 0;
         for (int p = 0; p < VmpcNode::kPads; p++)
            skipped += n->PadSoundSkipped(p) ? 1 : 0;
         if (n->soundtrack && skipped > 0)
         {
            char note[96];
            snprintf(note, sizeof(note), "%d clip%s too long for the 256 MB soundtrack budget: silent", skipped,
                     skipped == 1 ? "" : "s");
            ImGui::TextDisabled("%s", note);
         }
      }
      EndAudioSection();
      EndAudioBody();

      if (openLoadPad >= 0)
      {
         const std::string path = Platform::OpenVideoDialog();
         if (!path.empty())
         {
            PushUndoCheckpoint();
            if (n->LoadPad(openLoadPad, path))
               n->selectedPad = openLoadPad;
         }
      }
   }
}
