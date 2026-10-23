// VMPC body: the MPC card's layout (4x4 square pads, the selected pad's editor
// below), with a clip's filmstrip where the MPC draws a waveform.
#include "app/ui/design/components/EmptyState.h"
#include "app/ui/design/TokenColors.h"
#include "app/AppShared.h"
#include "app/ui/design/components/AudioViz.h"
#include "app/ui/design/components/FormParts.h"

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
            ModSlider(info.name, n->PadParamPtr(pad, k), info.lo, info.hi, info.fmt, 100.0f, /*audioStyle=*/true, 0.0f,
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
         dl->AddImage((ImTextureID)(intptr_t)tex, mn, mx, ImVec2(uc - uHalf * cell, 0.5f + vHalf),
                      ImVec2(uc + uHalf * cell, 0.5f - vHalf), tint);
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
               const int pad = (3 - row) * 4 + col;
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
                     if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                        n->selectedPad = pad;
                     const bool hit = lvl || (now - n->padFlash[pad]) < 0.14;
                     const bool lit = playing || hit;
                     const ImU32 green = isLight ? tok::U32(tok::pal::c_239650FF) : tok::U32(tok::pal::c_3CAF64FF);
                     ImU32 fill = isLoaded ? (isLight ? tok::U32(tok::pal::c_D6DDECFF) : tok::U32(tok::pal::c_282E40FF))
                                           : (isLight ? tok::U32(tok::pal::c_E8EBF2FF) : tok::U32(tok::pal::c_1A1C25FF));
                     if (hovered && !lit)
                        fill = isLight ? tok::U32(tok::pal::c_CCD4E6FF) : tok::U32(tok::pal::c_343B50FF);
                     if (lit)
                        fill = green;
                     dl->AddRectFilled(mn, mx, fill, 6.0f);
                     const ImU32 selCol = isLight ? tok::U32(tok::pal::c_3264E6FF) : tok::U32(tok::pal::c_6EA0FFFF);
                     const ImU32 edge = isLight ? tok::U32(tok::pal::c_AAB2C3FF) : tok::U32(tok::pal::c_555C73C8);
                     dl->AddRect(mn, mx, isSel ? selCol : edge, 6.0f, 0, isSel ? 2.0f : 1.0f);

                     const ImU32 ink = lit ? tok::U32(tok::pal::c_FFFFFFFF)
                                           : (isLoaded ? (isLight ? tok::U32(tok::pal::c_282D3CFF) : tok::U32(tok::pal::c_D7DCEBFF))
                                                       : tok::U32(tok::pal::c_788096FF));
                     const ImU32 dim = lit ? tok::U32(tok::pal::c_FFFFFFBE) : (isLight ? tok::U32(tok::pal::c_5F6982FF) : tok::U32(tok::pal::c_969EB4FF));
                     dl->PushClipRect(ImVec2(mn.x + 1.0f, mn.y + 1.0f), ImVec2(mx.x - 1.0f, mx.y - 1.0f), true);
                     const float lh = AudioLabelFontSize();
                     char num[8];
                     snprintf(num, sizeof(num), "%d", pad + 1);
                     AudioLabelText(dl, ImVec2(mn.x + 5.0f, mn.y + 3.0f), ink, num);
                     if (isLoaded)
                     {
                        char tag[32];
                        snprintf(tag, sizeof(tag), "%s", kModeTag[std::clamp(n->padMode[pad], 0, 2)]);
                        if (n->padSync[pad] == VmpcNode::kSynced)
                           snprintf(tag, sizeof(tag), "%s %s", kModeTag[std::clamp(n->padMode[pad], 0, 2)],
                                    MusicTime::RateDivisionName(n->padDiv[pad]));
                        const ImVec2 ts = AudioLabelSize(tag);
                        AudioLabelText(dl, ImVec2(mx.x - ts.x - 5.0f, mn.y + 3.0f), dim, tag);

                        // The picture: the filmstrip frame nearest the pad's
                        // position (its window start when idle), in the box
                        // the MPC gives its waveform.
                        const ImVec2 pmn(mn.x + 5.0f, mn.y + lh + 7.0f);
                        const ImVec2 pmx(mx.x - 5.0f, mx.y - lh - 7.0f);
                        const float cur = n->PadCursor(pad);
                        const float at = cur >= 0.0f ? cur : n->padStart[pad] / 100.0f;
                        if (thumbs != 0)
                           DrawThumb(dl, thumbs, ThumbIndexAt(at), pmn, pmx, tok::U32(tok::pal::c_FFFFFFFF));
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
                           dl->AddRectFilled(ImVec2(pmn.x, pmx.y - 2.0f), ImVec2(pmx.x, pmx.y), tok::U32(tok::pal::c_0000008C));
                           dl->AddRectFilled(ImVec2(pmn.x, pmx.y - 2.0f), ImVec2(px, pmx.y), tok::U32(tok::pal::c_FFFFFFE6));
                        }
                        std::string nm = n->PadName(pad);
                        const size_t dot = nm.find_last_of('.');
                        if (dot != std::string::npos && dot > 0)
                           nm.resize(dot);
                        dl->PushClipRect(ImVec2(mn.x + 1.0f, mn.y + 1.0f), ImVec2(mx.x - 16.0f, mx.y - 1.0f), true);
                        AudioLabelText(dl, ImVec2(mn.x + 5.0f, mx.y - lh - 4.0f), dim, nm.c_str());
                        dl->PopClipRect();
                     }
                     else
                     {
                        const char* hint = "+ load";
                        const ImVec2 hs = AudioLabelSize(hint);
                        AudioLabelText(dl, ImVec2((mn.x + mx.x - hs.x) * 0.5f, (mn.y + mx.y - hs.y) * 0.5f), dim, hint);
                     }
                     modDot = ImVec2(mx.x - 8.0f, mx.y - 8.0f);
                     if (anyMod)
                     {
                        dl->AddCircleFilled(modDot, 4.0f, isLight ? tok::U32(tok::pal::c_D77D14FF) : tok::U32(tok::pal::c_FFBE5AFF));
                        dl->AddCircle(modDot, 4.0f, isLight ? tok::U32(tok::pal::c_6E7382FF) : tok::U32(tok::pal::c_1E2028FF), 12, 1.0f);
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
         ImGui::Dummy(ImVec2(gAudioContentW, 4.0f * side + 3.0f * gap));
      }
      ImGui::Dummy(ImVec2(0.0f, 6.0f));

      // Selected pad: header, filmstrip (also the drop target), Load / Folder /
      // Clear, then the pad's rows.
      {
         const int cur = VmpcNode::Clamp(n->selectedPad);
         char header[96];
         if (n->PadLoaded(cur))
            snprintf(header, sizeof(header), "pad %d - %s", cur + 1, n->PadName(cur).c_str());
         else
            snprintf(header, sizeof(header), "pad %d - empty", cur + 1);
         BeginAudioSection(header);

         {
            const float w = AudioFullWidth();
            const float h = 90.0f;
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const ImVec2 br(origin.x + w, origin.y + h);
            n->stripRect[0] = origin.x; n->stripRect[1] = origin.y; n->stripRect[2] = br.x; n->stripRect[3] = br.y;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const bool has = n->PadLoaded(cur);
            ImGui::InvisibleButton("##vmpcstrip", ImVec2(w, h));
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
                               ImVec2(origin.x + cw * (float)(i + 1), br.y), tok::U32(tok::pal::c_FFFFFFFF));
               else
                  EmptyState::DrawCaption(origin, br, n->PadIsHap(cur) ? "HAP clip - no filmstrip" : "no preview");
               // The play window: outside start..end is dimmed, its edges marked.
               float a = std::clamp(n->padStart[cur] / 100.0f, 0.0f, 1.0f);
               float b = std::clamp(n->padEnd[cur] / 100.0f, 0.0f, 1.0f);
               if (b < a)
                  std::swap(a, b);
               const float xa = origin.x + w * a, xb = origin.x + w * b;
               dl->AddRectFilled(origin, ImVec2(xa, br.y), tok::U32(tok::pal::c_0000008C));
               dl->AddRectFilled(ImVec2(xb, origin.y), br, tok::U32(tok::pal::c_0000008C));
               const ImU32 edgeCol = isLight ? tok::U32(tok::pal::c_3264E6FF) : tok::U32(tok::pal::c_6EA0FFFF);
               dl->AddLine(ImVec2(xa, origin.y), ImVec2(xa, br.y), edgeCol, 2.0f);
               dl->AddLine(ImVec2(xb, origin.y), ImVec2(xb, br.y), edgeCol, 2.0f);
               const float at = n->PadCursor(cur);
               if (n->PadPlaying(cur) && at >= 0.0f)
               {
                  const float px = origin.x + w * std::clamp(at, 0.0f, 1.0f);
                  dl->AddLine(ImVec2(px, origin.y), ImVec2(px, br.y), tok::U32(tok::pal::c_FFFFFFEB), 1.5f);
               }
               if (hov)
               {
                  char val[32];
                  const float frac = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / w, 0.0f, 1.0f);
                  snprintf(val, sizeof(val), "%.2f s", frac * (float)n->PadDuration(cur));
                  SetAudioReadout("time", val);
               }
            }
            else
               EmptyState::DrawCaption(origin, br, "no clip loaded");
            dl->PopClipRect();
            AudioViz::Border(dl, origin, br, (!has && hov) ? (isLight ? tok::U32(tok::pal::c_3264E6FF) : tok::U32(tok::pal::c_6EA0FFFF)) : 0);
            if (!has && hov)
               ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
         }
         ImGui::Dummy(ImVec2(0.0f, 6.0f));

         const float thirdW = (AudioFullWidth() - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
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
         ImGui::BeginDisabled(!n->PadLoaded(cur));
         if (ActionButton::Draw("Clear##vmpc", ImVec2(thirdW, 0)))
         {
            PushUndoCheckpoint();
            n->ClearPad(cur);
         }
         ImGui::EndDisabled();
         ImGui::Dummy(ImVec2(0.0f, 6.0f));

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
         AudioKnobRow row(4);
         row.Dropdown("transition##vmpctrans", VmpcTransitionList(), n->transition,
                      [n](int i) { PushUndoCheckpoint(); n->transition = i; });
         ImGui::BeginDisabled(n->transition == VmpcNode::kCut);
         row.Knob("time", &n->transitionMs, 0.0f, 2000.0f, "%.0f ms", kKnobSmall, false, false,
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
