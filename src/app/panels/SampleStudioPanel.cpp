// Sample studio: the website's studio as a docked panel (docs/plans/data-universe/SAMPLE-STUDIO-DESIGN.md).
// Pick a style, part and length, Generate, shape the take with the feel field and the pills, trim it, then export
// the .wav, save the patch that made it, or put it on the canvas as a Generate Samples node.
// The engine is a private Generate Samples node (never in the graph, never cooked): a take made here and one made
// on the node are the same sound for the same recipe, role critic included. A dot move re-renders the take's own
// seed pinned, so the instrument never swaps under the hand. Session state only: nothing here is saved in a patch.
#include "app/AppShared.h"
#include "app/graph/GenerateSamplesNode.h"
#include "app/graph/SampleStyles.h"
#include "app/ui/design/components/ActionButton.h"
#include "app/ui/design/components/BusyLine.h"
#include "app/ui/design/components/ChipButton.h"
#include "app/ui/design/components/DropdownField.h"
#include "app/ui/design/components/FieldWell.h"
#include "app/ui/design/components/FormParts.h"
#include "app/ui/design/components/MenuParts.h"
#include "app/ui/design/components/PanelFrame.h"
#include "audio/AudioEngine.h"
#include "audio/AudioFileWriter.h"
#include "platform/AppPaths.h"
#include "platform/Platform.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <memory>

namespace app
{
namespace
{
   constexpr float kMinWidth = 300.0f;
   constexpr int kMaxTakes = 6;
   constexpr double kRate = GenerateSamplesNode::kLoopRate;
   constexpr float kFadeSec = 0.01f; // the "fade" toggle: 10 ms in and out, as on the website

   using P = SampleStyles::Part;
   const P kParts[] = { P::Synths, P::Keys, P::Leads, P::Pads, P::Plucks, P::Textures, P::Bass, P::Drums,
                        P::Kicks, P::Snares, P::Hats, P::Toms, P::Percussion, P::Fx, P::Foley };
   const int kBars[] = { 1, 2, 4, 8 };

   struct Take
   {
      SampleStyles::Recipe recipe; // as made: the seed it landed on
      int card = 0;                // style index (SampleStyles::At)
      std::string label;
      std::vector<float> l, r;
   };

   struct Studio
   {
      std::unique_ptr<GenerateSamplesNode> gen;
      int style = -1; // -1: drawn from the seed, as the node does
      int part = 0;   // index into kParts
      int barsIdx = 1;
      float tone = 0.0f, energy = 0.0f, space = 0.0f, mood = 0.0f, variation = 0.5f;
      std::deque<Take> takes; // newest first
      int current = -1;
      bool busy = false, busyPinned = false;
      int busyCard = 0;
      std::string status;
      // Trim in 16ths of the take; reverse and fade apply to what plays and what is exported.
      int trimFrom = 0, trimTo = 0;
      bool reverse = false, fade = false, loop = true;
      bool playing = false; // the audition is ours
      int dragHandle = -1;  // 0 start, 1 end
   };

   Studio& S()
   {
      static Studio s;
      if (!s.gen)
         s.gen = std::make_unique<GenerateSamplesNode>();
      return s;
   }

   Take* Current()
   {
      Studio& s = S();
      return (s.current >= 0 && s.current < (int)s.takes.size()) ? &s.takes[(size_t)s.current] : nullptr;
   }

   int Sixteenths(const Take& t) { return std::max(1, t.recipe.bars * 16); }

   // The take as heard: trimmed, reversed, faded. Interleaving is the caller's.
   void Processed(const Take& t, std::vector<float>& l, std::vector<float>& r)
   {
      const Studio& s = S();
      const int frames = (int)t.l.size();
      const int n16 = Sixteenths(t);
      const int a = (int)((int64_t)frames * s.trimFrom / n16), b = (int)((int64_t)frames * s.trimTo / n16);
      l.assign(t.l.begin() + a, t.l.begin() + std::max(a, b));
      r.assign(t.r.begin() + a, t.r.begin() + std::max(a, b));
      if (s.reverse)
      {
         std::reverse(l.begin(), l.end());
         std::reverse(r.begin(), r.end());
      }
      if (s.fade && !l.empty())
      {
         const int f = std::min((int)l.size() / 2, (int)(kFadeSec * kRate));
         for (int i = 0; i < f; i++)
         {
            const float g = (float)i / (float)f;
            l[(size_t)i] *= g;
            r[(size_t)i] *= g;
            l[l.size() - 1 - (size_t)i] *= g;
            r[r.size() - 1 - (size_t)i] *= g;
         }
      }
   }

   void Play()
   {
      Studio& s = S();
      const Take* t = Current();
      if (t == nullptr)
         return;
      std::vector<float> l, r;
      Processed(*t, l, r);
      if (l.empty())
         return;
      auto* buf = new Platform::SampleBuffer();
      buf->channels = 2;
      buf->numFrames = (int)l.size();
      buf->sampleRate = kRate;
      buf->channelData.resize(l.size() * 2);
      std::copy(l.begin(), l.end(), buf->channelData.begin());
      std::copy(r.begin(), r.end(), buf->channelData.begin() + (std::ptrdiff_t)l.size());
      AudioEngine::Instance().Preview().Play(buf);
      s.playing = true;
   }

   void StopPlaying()
   {
      Studio& s = S();
      if (s.playing)
         AudioEngine::Instance().Preview().Stop();
      s.playing = false;
   }

   // A first take of the settings, or (pinned) the current take again with the dots as they are now.
   void Render(bool pinned)
   {
      Studio& s = S();
      GenerateSamplesNode& g = *s.gen;
      const Take* t = Current();
      if (pinned && t == nullptr)
         return;
      g.part = (int)(pinned ? t->recipe.part : kParts[s.part]);
      g.bars = pinned ? t->recipe.bars : kBars[s.barsIdx];
      g.variation = s.variation;
      g.tone = s.tone;
      g.energy = s.energy;
      g.space = s.space;
      g.mood = s.mood;
      if (pinned)
      {
         g.seed = (int)t->recipe.seed;
         g.card = t->card;
      }
      else
      {
         g.RollSeed();
         if (s.style >= 0)
            g.card = s.style;
      }
      s.busyCard = g.card >= 0 ? g.card % SampleStyles::Count() : g.seed % SampleStyles::Count();
      s.busy = true;
      s.busyPinned = pinned;
      s.status.clear();
      g.Generate(pinned);
   }

   // The engine is polled here, every frame the panel is open.
   void PollEngine()
   {
      Studio& s = S();
      GenerateSamplesNode& g = *s.gen;
      g.Poll();
      if (!s.busy || g.GetState() == GenerateSamplesNode::State::Generating)
         return;
      s.busy = false;
      if (g.GetState() == GenerateSamplesNode::State::Failed || g.LoopL().empty())
      {
         s.status = T("Every try came out silent. Generate again.");
         return;
      }
      Take t;
      t.recipe = g.MadeRecipe();
      t.card = s.busyCard;
      t.label = g.MadeLabel() + " \xC2\xB7 " + SampleStyles::At(t.card).name;
      t.l = g.LoopL();
      t.r = g.LoopR();
      if (s.busyPinned && Current() != nullptr)
         *Current() = std::move(t); // a dot move reshapes the take in place
      else
      {
         s.takes.push_front(std::move(t));
         while ((int)s.takes.size() > kMaxTakes)
            s.takes.pop_back();
         s.current = 0;
         s.trimFrom = 0;
         s.trimTo = Sixteenths(s.takes.front());
      }
      Play();
   }

   std::string FileSafe(std::string name)
   {
      for (char& ch : name)
         ch = std::isalnum((unsigned char)ch) ? ch : ch == '#' ? 's' : '-';
      name.erase(std::unique(name.begin(), name.end(), [](char a, char b) { return a == '-' && b == '-'; }), name.end());
      return name;
   }

   void ExportWav()
   {
      const Take* t = Current();
      if (t == nullptr)
         return;
      const std::string path = Platform::SaveWavDialog(FileSafe("generated " + t->label) + ".wav");
      if (path.empty())
         return;
      std::vector<float> l, r;
      Processed(*t, l, r);
      std::vector<float> inter(l.size() * 2);
      for (size_t i = 0; i < l.size(); i++)
      {
         inter[i * 2] = l[i];
         inter[i * 2 + 1] = r[i];
      }
      S().status = AudioRecordings::WriteWav(path, inter.data(), (int)l.size(), kRate, 2) ? T("Saved the .wav") : T("Could not write the .wav");
   }

   // The graph that made the take, with an Audio Out on its tap, so the saved .inf opens and plays as it is.
   void SavePatch()
   {
      const Take* t = Current();
      if (t == nullptr)
         return;
      const SampleStyles::Compiled c = SampleStyles::Compile(t->recipe);
      if (!c.error.empty())
      {
         S().status = c.error;
         return;
      }
      const std::string path = Platform::SavePatchDialog(FileSafe("generated " + t->label) + ".inf");
      if (path.empty())
         return;
      int last = 0;
      for (size_t at = c.patch.find("\nnode "); at != std::string::npos; at = c.patch.find("\nnode ", at + 1))
         last = std::max(last, atoi(c.patch.c_str() + at + 6));
      std::ofstream f(AppPaths::FsPath(path));
      f << c.patch << "node " << last + 1 << " Utility Audio Out\nend\naud " << last + 1 << " 0 " << c.tap << "\n";
      S().status = f.good() ? T("Saved the patch") : T("Could not write the patch");
   }

   // A Generate Samples node holding the take: same recipe, its seed kept, the trim as start and end.
   void AddToCanvas()
   {
      const Studio& s = S();
      const Take* t = Current();
      if (t == nullptr)
         return;
      const ImVec2 at = FindFreeSpawnPosition(gViewCenterCanvas);
      PushUndoCheckpoint("Add sample from studio");
      GraphNode* gn = SpawnNode("Generate Samples", "Synths", at.x, at.y);
      auto* n = gn != nullptr ? dynamic_cast<GenerateSamplesNode*>(gn->node.get()) : nullptr;
      if (n == nullptr)
         return;
      n->part = (int)t->recipe.part;
      n->bars = t->recipe.bars;
      n->seed = (int)t->recipe.seed;
      n->card = t->card;
      n->variation = t->recipe.variation;
      n->tone = t->recipe.tone;
      n->energy = t->recipe.energy;
      n->space = t->recipe.space;
      n->mood = t->recipe.mood;
      n->keepSeed = true;
      n->Generate();
      const float n16 = (float)Sixteenths(*t);
      n->start = (float)s.trimFrom / n16;
      n->end = (float)s.trimTo / n16;
      gPatchDirty = true;
      S().status = T("Added a Generate Samples node");
   }

   // ---- drawing ----

   // A dropdown face that opens a menu list; returns the picked index or -1.
   int Dropdown(const char* id, const char* label, const char* value, float w, const std::vector<std::string>& items, int current)
   {
      int picked = -1;
      if (DropdownField::Draw((std::string(value) + "##" + id).c_str(), ImVec2(w, 0.0f), label))
         ImGui::OpenPopup(id);
      if (MenuParts::BeginPopup(id))
      {
         for (int i = 0; i < (int)items.size(); i++)
            if (MenuParts::Choice(items[(size_t)i].c_str(), i == current))
               picked = i;
         MenuParts::EndPopup();
      }
      return picked;
   }

   void DrawRecipeRow(float w)
   {
      Studio& s = S();
      const float gap = ImGui::GetStyle().ItemSpacing.x;
      static std::vector<std::string> styles, parts;
      if (styles.empty())
      {
         styles.push_back(T("any"));
         for (int i = 0; i < SampleStyles::Count(); i++)
            styles.push_back(SampleStyles::At(i).name);
         for (P p : kParts)
            parts.push_back(SampleStyles::PartName(p));
      }
      static const std::vector<std::string> bars = { "1 bar", "2 bars", "4 bars", "8 bars" };
      const float wStyle = std::floor((w - 2.0f * gap) * 0.44f), wRest = std::floor((w - 2.0f * gap - wStyle) * 0.5f);
      if (const int i = Dropdown("##studioStyle", T("style"), styles[(size_t)(s.style + 1)].c_str(), wStyle, styles, s.style + 1); i >= 0)
         s.style = i - 1;
      FormParts::Inline();
      if (const int i = Dropdown("##studioPart", T("part"), parts[(size_t)s.part].c_str(), wRest, parts, s.part); i >= 0)
         s.part = i;
      FormParts::Inline();
      if (const int i = Dropdown("##studioBars", T("length"), bars[(size_t)s.barsIdx].c_str(), w - wStyle - wRest - 2.0f * gap, bars, s.barsIdx); i >= 0)
         s.barsIdx = i;

      if (s.busy)
      {
         if (ActionButton::Draw(T("Cancel##studioGen"), ImVec2(w, 0.0f), ActionButton::Kind::Record))
         {
            s.gen->Cancel();
            s.busy = false;
         }
         BusyLine::Draw(s.busyPinned ? T("Reshaping the take") : T("Making a take"), s.gen->Progress());
      }
      else if (ActionButton::Draw(T("Generate##studioGen"), ImVec2(w, 0.0f), ActionButton::Kind::Primary))
         Render(false);
   }

   // The feel field: energy across, mood up. Releasing the dot re-renders the current take pinned.
   void DrawFeelField(float w)
   {
      Studio& s = S();
      const float h = std::floor(std::min(w * 0.62f, 200.0f));
      const ImVec2 mn = ImGui::GetCursorScreenPos(), mx(mn.x + w, mn.y + h);
      ImGui::InvisibleButton("##studioFeel", ImVec2(w, h));
      const bool hot = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      FieldWell::Draw(dl, mn, mx, hot, active);
      const ImU32 guide = ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.25f);
      const ImVec2 c((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
      dl->AddLine(ImVec2(mn.x + tok::space_2, c.y), ImVec2(mx.x - tok::space_2, c.y), guide);
      dl->AddLine(ImVec2(c.x, mn.y + tok::space_2), ImVec2(c.x, mx.y - tok::space_2), guide);
      const ImU32 cap = ImGui::GetColorU32(ImGuiCol_TextDisabled);
      const float lh = ImGui::GetTextLineHeight();
      // Energy on the side edges, mood on the top and bottom edges.
      dl->AddText(ImVec2(mn.x + tok::space_2, c.y - lh - tok::space_1), cap, T("calm"));
      const char* drv = T("driving");
      dl->AddText(ImVec2(mx.x - tok::space_2 - ImGui::CalcTextSize(drv).x, c.y - lh - tok::space_1), cap, drv);
      const char* br = T("bright");
      dl->AddText(ImVec2(c.x + tok::space_2, mn.y + tok::space_1), cap, br);
      const char* dk = T("dark");
      dl->AddText(ImVec2(c.x + tok::space_2, mx.y - lh - tok::space_1), cap, dk);
      if (active)
      {
         const ImVec2 m = ImGui::GetIO().MousePos;
         s.energy = std::clamp((m.x - mn.x) / w * 2.0f - 1.0f, -1.0f, 1.0f);
         s.mood = std::clamp(1.0f - (m.y - mn.y) / h * 2.0f, -1.0f, 1.0f);
      }
      if (ImGui::IsItemDeactivated() && Current() != nullptr)
         Render(true);
      if (hot || active)
         ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
      const ImVec2 dot(mn.x + (s.energy + 1.0f) * 0.5f * w, mn.y + (1.0f - s.mood) * 0.5f * h);
      const ImVec4 a = AccentEmphasisSelected();
      dl->AddCircleFilled(dot, tok::space_5, ImGui::GetColorU32(ImVec4(a.x, a.y, a.z, 0.22f)), 32);
      dl->AddCircleFilled(dot, tok::pin_r_param_on, ImGui::GetColorU32(a), 24);
      if (hot && !active)
         ImGui::SetTooltip("%s", T("Drag: energy across, mood up. The take reshapes when you let go."));
   }

   // Tone, space and wild: one slider each. A release re-renders the current take pinned.
   void DrawPills(float w)
   {
      Studio& s = S();
      struct Pill { const char* id; const char* label; float* v; float lo, hi; };
      const Pill pills[] = { { "##studioTone", T("tone"), &s.tone, -1.0f, 1.0f },
                             { "##studioSpace", T("space"), &s.space, -1.0f, 1.0f },
                             { "##studioWild", T("wild"), &s.variation, 0.0f, 1.0f } };
      const float gap = ImGui::GetStyle().ItemSpacing.x;
      const float pw = std::floor((w - 2.0f * gap) / 3.0f);
      for (int i = 0; i < 3; i++)
      {
         if (i > 0)
            FormParts::Inline();
         ImGui::SetNextItemWidth(i == 2 ? w - 2.0f * (pw + gap) : pw);
         const std::string fmt = std::string(pills[i].label) + " %.2f";
         FieldWell::Slider(pills[i].id, pills[i].v, pills[i].lo, pills[i].hi, fmt.c_str());
         if (ImGui::IsItemDeactivatedAfterEdit() && Current() != nullptr)
            Render(true);
      }
   }

   // The waveform with the trim handles (16ths) and the playhead.
   void DrawWave(float w)
   {
      Studio& s = S();
      const Take* t = Current();
      const float h = 84.0f;
      const ImVec2 mn = ImGui::GetCursorScreenPos(), mx(mn.x + w, mn.y + h);
      ImGui::InvisibleButton("##studioWave", ImVec2(w, h));
      ImDrawList* dl = ImGui::GetWindowDrawList();
      FieldWell::Draw(dl, mn, mx, false, false);
      if (t == nullptr || t->l.empty())
      {
         const char* msg = T("Generate a take to see it here");
         const ImVec2 ts = ImGui::CalcTextSize(msg);
         dl->AddText(ImVec2(mn.x + (w - ts.x) * 0.5f, mn.y + (h - ts.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), msg);
         return;
      }
      const int n16 = Sixteenths(*t);
      const auto xOf = [&](int six) { return mn.x + w * (float)six / (float)n16; };
      if (ImGui::IsItemActivated())
      {
         const float mxp = ImGui::GetIO().MousePos.x;
         s.dragHandle = std::fabs(mxp - xOf(s.trimFrom)) <= std::fabs(mxp - xOf(s.trimTo)) ? 0 : 1;
      }
      if (ImGui::IsItemActive() && s.dragHandle >= 0)
      {
         const int six = std::clamp((int)std::lround((ImGui::GetIO().MousePos.x - mn.x) / w * (float)n16), 0, n16);
         if (s.dragHandle == 0)
            s.trimFrom = std::min(six, s.trimTo - 1);
         else
            s.trimTo = std::max(six, s.trimFrom + 1);
      }
      if (ImGui::IsItemDeactivated())
      {
         s.dragHandle = -1;
         if (s.playing)
            Play();
      }
      if (ImGui::IsItemHovered() || ImGui::IsItemActive())
         ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

      // 16th grid, beats stronger.
      for (int i = 1; i < n16; i++)
         dl->AddLine(ImVec2(xOf(i), mn.y + 2.0f), ImVec2(xOf(i), mx.y - 2.0f),
                     ImGui::GetColorU32(ImGuiCol_TextDisabled, i % 4 == 0 ? 0.22f : 0.07f));
      // Peaks, one column per pixel, from the left channel; reversed when the take plays reversed.
      const int frames = (int)t->l.size();
      const int cols = std::max(1, (int)w);
      const float mid = (mn.y + mx.y) * 0.5f, amp = h * 0.46f;
      const ImU32 inCol = ImGui::GetColorU32(ImGuiCol_Text, 0.75f), outCol = ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.35f);
      for (int x = 0; x < cols; x++)
      {
         const int c = s.reverse ? cols - 1 - x : x;
         const int a = (int)((int64_t)frames * c / cols), b = std::max(a + 1, (int)((int64_t)frames * (c + 1) / cols));
         float lo = 0.0f, hi = 0.0f;
         for (int i = a; i < b && i < frames; i++)
         {
            lo = std::min(lo, t->l[(size_t)i]);
            hi = std::max(hi, t->l[(size_t)i]);
         }
         const float px = mn.x + (float)x + 0.5f;
         const int six = (int)((float)x / (float)cols * (float)n16);
         const int sixShown = s.reverse ? n16 - 1 - six : six;
         const bool inside = sixShown >= s.trimFrom && sixShown < s.trimTo;
         dl->AddLine(ImVec2(px, mid - hi * amp), ImVec2(px, mid - lo * amp + 1.0f), inside ? inCol : outCol);
      }
      // Trim handles (shown where the trim sits on the drawn wave).
      const ImVec4 a = AccentEmphasisSelected();
      const ImU32 acc = ImGui::GetColorU32(a);
      for (int k = 0; k < 2; k++)
      {
         const int six = k == 0 ? s.trimFrom : s.trimTo;
         const float x = xOf(s.reverse ? n16 - six : six);
         dl->AddLine(ImVec2(x, mn.y), ImVec2(x, mx.y), acc, 2.0f);
      }
      // Playhead.
      if (s.playing && AudioEngine::Instance().Preview().IsPlaying())
      {
         const float sec = AudioEngine::Instance().Preview().PositionSeconds();
         const float spanSec = (float)frames * (float)(s.trimTo - s.trimFrom) / (float)n16 / (float)kRate;
         const float u = spanSec > 0.0f ? std::clamp(sec / spanSec, 0.0f, 1.0f) : 0.0f;
         const float lo16 = (float)(s.reverse ? n16 - s.trimTo : s.trimFrom), hi16 = (float)(s.reverse ? n16 - s.trimFrom : s.trimTo);
         const float x = mn.x + w * (lo16 + u * (hi16 - lo16)) / (float)n16;
         dl->AddLine(ImVec2(x, mn.y), ImVec2(x, mx.y), ImGui::GetColorU32(ImGuiCol_Text), 1.0f);
      }
   }

   void DrawTransportRow()
   {
      Studio& s = S();
      const bool has = Current() != nullptr;
      ImGui::BeginDisabled(!has);
      const bool on = s.playing && AudioEngine::Instance().Preview().IsPlaying();
      if (ChipButton::Draw(on ? T("Stop##studioPlay") : T("Play##studioPlay"), on, FormParts::kRowH))
      {
         if (on)
            StopPlaying();
         else
            Play();
      }
      FormParts::Inline();
      if (ChipButton::Draw(T("Loop##studioLoop"), s.loop, FormParts::kRowH))
         s.loop = !s.loop;
      FormParts::Inline();
      if (ChipButton::Draw(T("Reverse##studioRev"), s.reverse, FormParts::kRowH))
      {
         s.reverse = !s.reverse;
         if (s.playing)
            Play();
      }
      FormParts::Inline();
      if (ChipButton::Draw(T("Fade##studioFade"), s.fade, FormParts::kRowH))
      {
         s.fade = !s.fade;
         if (s.playing)
            Play();
      }
      ImGui::EndDisabled();
   }

   void DrawTakes()
   {
      Studio& s = S();
      if (s.takes.empty())
         return;
      ImGui::TextDisabled("%s", T("Takes"));
      for (int i = 0; i < (int)s.takes.size(); i++)
      {
         if (i > 0)
            FormParts::Inline();
         const std::string label = std::to_string((int)s.takes.size() - i) + "##studioTake" + std::to_string(i);
         if (ChipButton::Draw(label.c_str(), i == s.current, FormParts::kRowH, FormParts::kRowH))
         {
            s.current = i;
            s.trimFrom = 0;
            s.trimTo = Sixteenths(s.takes[(size_t)i]);
            const SampleStyles::Recipe& r = s.takes[(size_t)i].recipe;
            s.tone = r.tone;
            s.energy = r.energy;
            s.space = r.space;
            s.mood = r.mood;
            s.variation = r.variation;
            Play();
         }
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", s.takes[(size_t)i].label.c_str());
      }
   }

   void DrawActions(float w)
   {
      const bool has = Current() != nullptr;
      const float gap = ImGui::GetStyle().ItemSpacing.x;
      const float bw = std::floor((w - 2.0f * gap) / 3.0f);
      ImGui::BeginDisabled(!has || S().busy);
      if (ActionButton::Draw(T("Export WAV##studio"), ImVec2(bw, 0.0f)))
         ExportWav();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("%s", T("Saves the take as heard: trimmed, reversed and faded"));
      FormParts::Inline();
      if (ActionButton::Draw(T("Save patch##studio"), ImVec2(bw, 0.0f)))
         SavePatch();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("%s", T("Saves the .inf that made the take; open it to see and change the graph"));
      FormParts::Inline();
      if (ActionButton::Draw(T("To canvas##studio"), ImVec2(w - 2.0f * (bw + gap), 0.0f)))
         AddToCanvas();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("%s", T("Adds a Generate Samples node holding this take; it saves with the patch"));
      ImGui::EndDisabled();
   }

   void DrawStudio()
   {
      Studio& s = S();
      // Review shots (INFINITE_OPENPANELS=studiodemo): a take is made on open so the panel has something to show.
      static bool sDemo = [] { const char* op = getenv("INFINITE_OPENPANELS"); return op != nullptr && std::strstr(op, "studiodemo") != nullptr; }();
      if (sDemo)
      {
         sDemo = false;
         s.style = 0;
         s.energy = 0.4f;
         s.mood = 0.3f;
         Render(false);
      }
      PollEngine();
      // Loop: the audition is one-shot, so a finished pass starts again while loop is on.
      if (s.playing && !AudioEngine::Instance().Preview().IsPlaying())
      {
         if (s.loop && Current() != nullptr)
            Play();
         else
            s.playing = false;
      }
      const float w = std::max(1.0f, ImGui::GetContentRegionAvail().x);
      ImGui::TextUnformatted(T("Sample studio"));
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextDisabled("%s", T("Generate a loop, then move the dot and the pills: the same take reshapes."));
      ImGui::PopTextWrapPos();
      ImGui::Spacing();
      DrawRecipeRow(w);
      ImGui::Spacing();
      DrawFeelField(w);
      DrawPills(w);
      ImGui::Spacing();
      DrawWave(w);
      DrawTransportRow();
      if (const Take* t = Current())
         ImGui::TextDisabled("%s", t->label.c_str());
      DrawTakes();
      ImGui::Spacing();
      DrawActions(w);
      if (!s.status.empty())
         ImGui::TextDisabled("%s", s.status.c_str());
   }
}

void DrawSampleStudioDocked(const char* id, const ImVec2& size)
{
   const float kGrip = PanelFrame::kGap;
   // Sit beside whatever was drawn last, like the other right-docked panels.
   {
      const ImVec2 prevMax = ImGui::GetItemRectMax();
      ImGui::SetCursorScreenPos(ImVec2(prevMax.x, ImGui::GetItemRectMin().y));
   }
   ImGui::BeginChild(id, size, false);
   const ImVec2 inner = ImGui::GetContentRegionAvail();

   ImGui::InvisibleButton("##studiogrip", ImVec2(kGrip, std::max(1.0f, inner.y)));
   DrawPanelSeam(true, true);
   if (ImGui::IsItemHovered() || ImGui::IsItemActive())
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
   if (ImGui::IsItemActive())
      gSampleStudioWidth = std::max(kMinWidth, gSampleStudioWidth - ImGui::GetIO().MouseDelta.x);
   {
      const ImVec2 gripMax = ImGui::GetItemRectMax();
      ImGui::SetCursorScreenPos(ImVec2(gripMax.x + ImGui::GetStyle().ItemSpacing.x, ImGui::GetItemRectMin().y));
   }

   const ImVec2 gap = ImGui::GetStyle().ItemSpacing;
   const ImVec2 cardSize(std::max(1.0f, inner.x - kGrip - gap.x), inner.y);
   PanelFrame::Insets in;
   in.l = 0.0f;
   PushDockedPanelStyle(/*isChild=*/true);
   PanelFrame::BeginCard("##studiopanelcontent", cardSize, in);
   PopDockedPanelStyle();
   DrawStudio();
   PanelFrame::EndCard();

   const ImVec2 savedSpacing = ImGui::GetStyle().ItemSpacing;
   ImGui::GetStyle().ItemSpacing = ImVec2(0.0f, 0.0f);
   ImGui::EndChild();
   ImGui::GetStyle().ItemSpacing = savedSpacing;
}
}
