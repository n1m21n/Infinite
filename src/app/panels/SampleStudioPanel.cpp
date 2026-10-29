// Sample studio: a sample and slice editor docked bottom or top, like the Timeline (SAMPLE-STUDIO-DESIGN.md,
// docs/plans/sample-studio/SLICE-EDITOR.md). The source is a generated take or any audio file (open, or drop it on
// the panel). The waveform zooms and scrolls (overview strip, ruler in bars, seconds or samples, sample dots at deep
// zoom); markers cut it into slices, each with its own colour, name, gain, pan, pitch, stretch, length, fades,
// transient and decay shaping, compressor, filter, reverse, mute and solo (SliceSet). The inspector on the right
// edits the selected slices, or the whole sample when none is selected. The edit renders off the main thread
// (SliceRender) and plays on the global transport through StudioLoopPlayer; play hands audio to the Studio, which
// bypasses the canvas the way Timeline audio does. Editing here never touches the canvas: the result leaves as a
// node (Sampler, Slicer with these slices, one Sampler per slice, ...) or as files. The Studio's work is saved in
// the patch (`studio` lines) and has its own undo while the pointer is over the waveform.
// The engine is a private Generate Samples node (never in the graph, never cooked): a take made here and one made
// on the node are the same sound for the same recipe. Snap to scale re-renders the take's own seed pinned.
#include "app/AppShared.h"
#include "app/graph/GenerateSamplesNode.h"
#include "app/graph/NodeClipboard.h"
#include "app/graph/SampleStyles.h"
#include "app/ui/design/GlyphDraw.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/components/ActionButton.h"
#include "app/ui/design/components/ChipButton.h"
#include "app/ui/design/components/DialogParts.h"
#include "app/ui/design/components/DropdownField.h"
#include "app/ui/design/components/FieldWell.h"
#include "app/ui/design/components/FormParts.h"
#include "app/ui/design/components/MenuParts.h"
#include "app/ui/design/components/PanelFrame.h"
#include "app/ui/design/components/SectionCard.h"
#include "audio/AudioEngine.h"
#include "audio/AudioFileWriter.h"
#include "audio/MusicTime.h"
#include "audio/SliceRender.h"
#include "audio/SliceSet.h"
#include "audio/dsp/SlicerDsp.h"
#include "core/Patch.h"
#include "core/Transport.h"
#include "nodes/GrainMolderNode.h"
#include "nodes/GranularNode.h"
#include "nodes/MolderNode.h"
#include "nodes/PaulStretchNode.h"
#include "nodes/SamplerNode.h"
#include "nodes/SlicerNode.h"
#include "platform/Platform.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <thread>

namespace app
{

namespace
{
   constexpr float kMinHeight = 180.0f;
   constexpr float kSettingsW = 264.0f;
   constexpr double kRate = GenerateSamplesNode::kLoopRate;
   constexpr int kPaletteSize = 12;
   constexpr size_t kUndoDepth = 64;
   constexpr float kOverviewH = 14.0f, kRulerH = 16.0f, kTabH = 14.0f;

   ImU32 AccentU32(float alpha)
   {
      const ImVec4 a = AccentEmphasisSelected();
      return ImGui::GetColorU32(ImVec4(a.x, a.y, a.z, a.w * alpha));
   }

   using P = SampleStyles::Part;
   const P kParts[] = { P::Synths, P::Keys, P::Leads, P::Pads, P::Plucks, P::Textures, P::Bass, P::Drums,
                        P::Kicks, P::Snares, P::Hats, P::Toms, P::Percussion, P::Fx, P::Foley };
   const int kBars[] = { 1, 2, 4, 8 };

   // A generated source: the recipe it was made from, so a saved patch can make it again.
   struct TakeInfo
   {
      SampleStyles::Recipe recipe; // as made: the seed it landed on
      int card = 0;                // style index (SampleStyles::At)
   };

   // What undo restores: the source and the edit on it.
   struct Snapshot
   {
      std::shared_ptr<const SourceAudio> src;
      std::string path;
      std::optional<TakeInfo> take;
      SliceSet doc;
   };

   struct RenderJob
   {
      std::atomic<bool> cancel{ false }, done{ false };
      bool ok = false;
      SliceRenderResult res;
   };

   struct OnsetJob
   {
      std::atomic<bool> abort{ false }, done{ false };
      std::vector<int> onsets;
      std::vector<float> strengths;
      int64_t from = 0, to = 0;
   };

   // Min/max of the mono mix per block of frames, for drawing at any zoom without touching every frame.
   struct Mip
   {
      int block = 1;
      std::vector<float> lo, hi;
   };

   struct Studio
   {
      std::unique_ptr<GenerateSamplesNode> gen;
      // Recipe for the next Generate.
      int style = -1; // -1: drawn from the seed, as the node does
      int part = 0;   // index into kParts
      int barsIdx = 1;
      float variation = 0.5f;
      bool followScale = false;
      // The source and the edit.
      std::shared_ptr<const SourceAudio> src;
      std::string path;              // file source; empty for a take
      std::optional<TakeInfo> take;  // generated source
      SliceSet doc;
      std::optional<SliceSet> pendingDoc; // a loaded patch's edit, applied once its take has been made again
      std::vector<uint16_t> sel;          // selected slice ids
      uint16_t primary = 0;               // the one the inspector shows
      std::vector<Snapshot> undo, redo;
      std::string status;                 // a line under the toolbar when something failed
      // View, in source frames.
      double viewStart = 0.0, viewLen = 1.0;
      float level = 1.0f; // vertical zoom
      int ruler = 0;      // 0 bars, 1 seconds, 2 samples
      bool snapGrid = true;
      bool settingsOpen = true;
      // Engine.
      bool busy = false, busyPinned = false;
      int busyCard = 0;
      bool dirty = false;
      std::shared_ptr<RenderJob> job;
      std::shared_ptr<SliceRenderCache> cache = std::make_shared<SliceRenderCache>();
      std::shared_ptr<const SliceRenderResult> heard; // the edit as heard
      std::shared_ptr<OnsetJob> onsetJob;
      int onsetMax = 16;
      // An auto-slice that would replace hand-placed slices waits here for the confirm dialog.
      std::function<void()> pendingReplace;
      bool replaceConfirmed = false, openReplaceConfirm = false;
      // Interaction.
      int dragKind = -1; // 0 a slice start, 1 the end marker, 2 the overview
      int dragIndex = -1;
      // Peaks.
      const SourceAudio* mipFor = nullptr;
      Mip mip256, mip4096;
   };

   Studio& S()
   {
      static Studio s;
      if (!s.gen)
      {
         s.gen = std::make_unique<GenerateSamplesNode>();
         s.gen->followTransport = true; // the take is made at the transport's tempo, so it sits on the grid as made
      }
      return s;
   }

   int64_t Frames() { return S().src ? S().src->Frames() : 0; }
   double Sr() { return S().src ? S().src->sr : kRate; }
   double Bpm() { return S().doc.sampleBpm > 0.0f ? (double)S().doc.sampleBpm : Transport::Instance().Tempo(); }
   double FramesPerBeat() { return Sr() * 60.0 / std::max(1.0, Bpm()); }

   ImU32 SliceColour(const SliceSet& d, int i, float alpha = 1.0f)
   {
      const int c = d.slices[(size_t)i].colour >= 0 ? d.slices[(size_t)i].colour : i;
      const float h = (float)(c % kPaletteSize) / (float)kPaletteSize;
      float r, g, b;
      ImGui::ColorConvertHSVtoRGB(h, 0.55f, 0.88f, r, g, b);
      return ImGui::GetColorU32(ImVec4(r, g, b, alpha));
   }

   std::string SliceLabel(const SliceSet& d, int i)
   {
      return d.slices[(size_t)i].name.empty() ? std::to_string(i + 1) : d.slices[(size_t)i].name;
   }

   // ---- selection ----

   bool IsSel(uint16_t id) { return std::find(S().sel.begin(), S().sel.end(), id) != S().sel.end(); }

   void SelectOnly(int i)
   {
      Studio& s = S();
      s.sel.clear();
      if (i >= 0 && i < s.doc.Count())
      {
         s.sel.push_back(s.doc.slices[(size_t)i].id);
         s.primary = s.sel.back();
      }
   }

   // Drops ids of slices that no longer exist; keeps a primary when anything is selected.
   void PruneSelection()
   {
      Studio& s = S();
      s.sel.erase(std::remove_if(s.sel.begin(), s.sel.end(), [&](uint16_t id) { return s.doc.IndexOfId(id) < 0; }), s.sel.end());
      if (!s.sel.empty() && !IsSel(s.primary))
         s.primary = s.sel.front();
   }

   std::vector<int> SelectedIndices()
   {
      std::vector<int> out;
      for (uint16_t id : S().sel)
         if (const int i = S().doc.IndexOfId(id); i >= 0)
            out.push_back(i);
      std::sort(out.begin(), out.end());
      return out;
   }

   // ---- undo ----

   Snapshot Capture()
   {
      const Studio& s = S();
      return Snapshot{ s.src, s.path, s.take, s.doc };
   }

   void PushUndo(Snapshot snap)
   {
      Studio& s = S();
      s.undo.push_back(std::move(snap));
      if (s.undo.size() > kUndoDepth)
         s.undo.erase(s.undo.begin());
      s.redo.clear();
   }
   void PushUndo() { PushUndo(Capture()); }

   void Restore(Snapshot& snap)
   {
      Studio& s = S();
      s.src = snap.src;
      s.path = snap.path;
      s.take = snap.take;
      s.doc = snap.doc;
      PruneSelection();
      s.dirty = true;
   }

   void Undo()
   {
      Studio& s = S();
      if (s.undo.empty())
         return;
      s.redo.push_back(Capture());
      Restore(s.undo.back());
      s.undo.pop_back();
   }

   void Redo()
   {
      Studio& s = S();
      if (s.redo.empty())
         return;
      s.undo.push_back(Capture());
      Restore(s.redo.back());
      s.redo.pop_back();
   }

   // ---- the view ----

   void ClampView()
   {
      Studio& s = S();
      const double total = std::max<double>(1.0, (double)Frames());
      s.viewLen = std::clamp(s.viewLen, std::min(32.0, total), total);
      s.viewStart = std::clamp(s.viewStart, 0.0, total - s.viewLen);
   }

   void ViewAll()
   {
      S().viewStart = 0.0;
      S().viewLen = (double)std::max<int64_t>(1, Frames());
   }

   void ZoomAround(double frame, double factor)
   {
      Studio& s = S();
      const double u = (frame - s.viewStart) / s.viewLen;
      s.viewLen *= factor;
      ClampView();
      s.viewStart = frame - u * s.viewLen;
      ClampView();
   }

   void ZoomToSelection()
   {
      Studio& s = S();
      const std::vector<int> idx = SelectedIndices();
      if (idx.empty())
      {
         ViewAll();
         return;
      }
      const double a = (double)s.doc.slices[(size_t)idx.front()].start, b = (double)s.doc.EndOf(idx.back());
      const double pad = (b - a) * 0.05;
      s.viewStart = a - pad;
      s.viewLen = b - a + 2.0 * pad;
      ClampView();
   }

   // ---- peaks ----

   void BuildMip(const SourceAudio& a, int block, Mip& m)
   {
      m.block = block;
      const int64_t n = a.Frames(), cols = (n + block - 1) / block;
      m.lo.assign((size_t)cols, 0.0f);
      m.hi.assign((size_t)cols, 0.0f);
      for (int64_t c = 0; c < cols; c++)
      {
         float lo = 0.0f, hi = 0.0f;
         for (int64_t i = c * block, e = std::min(n, (c + 1) * block); i < e; i++)
         {
            const float v = 0.5f * (a.l[(size_t)i] + a.r[(size_t)i]);
            lo = std::min(lo, v);
            hi = std::max(hi, v);
         }
         m.lo[(size_t)c] = lo;
         m.hi[(size_t)c] = hi;
      }
   }

   void EnsurePeaks()
   {
      Studio& s = S();
      if (!s.src || s.mipFor == s.src.get())
         return;
      s.mipFor = s.src.get();
      BuildMip(*s.src, 256, s.mip256);
      BuildMip(*s.src, 4096, s.mip4096);
   }

   // Min and max of the mono mix over [a, b), from the coarsest level that still resolves it.
   void RangeMinMax(int64_t a, int64_t b, float& lo, float& hi)
   {
      Studio& s = S();
      const SourceAudio& src = *s.src;
      a = std::clamp<int64_t>(a, 0, src.Frames());
      b = std::clamp<int64_t>(b, a, src.Frames());
      lo = hi = 0.0f;
      const Mip* m = b - a >= 4096 * 2 ? &s.mip4096 : b - a >= 256 * 2 ? &s.mip256 : nullptr;
      if (m != nullptr)
      {
         for (int64_t c = a / m->block, e = std::min<int64_t>((int64_t)m->lo.size(), (b + m->block - 1) / m->block); c < e; c++)
         {
            lo = std::min(lo, m->lo[(size_t)c]);
            hi = std::max(hi, m->hi[(size_t)c]);
         }
         return;
      }
      for (int64_t i = a; i < b; i++)
      {
         const float v = 0.5f * (src.l[(size_t)i] + src.r[(size_t)i]);
         lo = std::min(lo, v);
         hi = std::max(hi, v);
      }
   }

   // ---- the edit as heard ----

   void Rebuild()
   {
      Studio& s = S();
      StudioLoopPlayer& player = AudioEngine::Instance().StudioLoop();
      auto* buf = new Platform::SampleBuffer();
      buf->channels = 2;
      buf->sampleRate = Sr();
      double beats = 4.0;
      if (s.heard && !s.heard->l.empty())
      {
         const SliceRenderResult& h = *s.heard;
         buf->numFrames = (int)h.l.size();
         buf->channelData.resize(h.l.size() * 2);
         std::copy(h.l.begin(), h.l.end(), buf->channelData.begin());
         std::copy(h.r.begin(), h.r.end(), buf->channelData.begin() + (std::ptrdiff_t)h.l.size());
         beats = (double)h.l.size() / Sr() * Bpm() / 60.0;
      }
      player.Set(buf, beats, s.doc.wide.loop);
   }

   // Starts a render when the edit changed and none is running; a running one is cancelled and restarted after.
   void PumpRender()
   {
      Studio& s = S();
      if (s.job && s.job->done.load(std::memory_order_acquire))
      {
         if (s.job->ok && !s.job->cancel.load())
         {
            s.heard = std::make_shared<const SliceRenderResult>(std::move(s.job->res));
            Rebuild();
         }
         s.job.reset();
      }
      if (!s.dirty)
         return;
      if (!s.src || s.doc.Empty())
      {
         s.dirty = false;
         s.heard.reset();
         Rebuild();
         return;
      }
      if (s.job)
      {
         s.job->cancel.store(true);
         return;
      }
      if (s.dragKind >= 0)
         return; // a marker in motion: render once it lands
      s.dirty = false;
      s.cache->Retain(s.doc);
      auto job = std::make_shared<RenderJob>();
      s.job = job;
      std::thread([job, src = s.src, doc = s.doc, cache = s.cache]()
                  {
                     job->ok = RenderSliceSet(*src, doc, cache.get(), &job->cancel, job->res);
                     job->done.store(true, std::memory_order_release);
                  })
         .detach();
   }

   // A new source: slices reset to one over the whole sample (the sample-wide settings stay).
   void SetSource(std::shared_ptr<const SourceAudio> src, const std::string& path, std::optional<TakeInfo> take, bool keepSlices)
   {
      Studio& s = S();
      const int64_t oldFrames = Frames();
      s.src = std::move(src);
      s.path = path;
      s.take = std::move(take);
      const int64_t n = Frames();
      if (keepSlices && oldFrames > 0 && !s.doc.Empty())
      {
         // The same take made again at another length: the markers keep their place in it.
         if (oldFrames != n)
         {
            const double k = (double)n / (double)oldFrames;
            for (Slice& sl : s.doc.slices)
               sl.start = std::min<int64_t>(n - 1, (int64_t)std::llround((double)sl.start * k));
            s.doc.endFrame = std::min<int64_t>(n, (int64_t)std::llround((double)s.doc.endFrame * k));
         }
      }
      else
      {
         s.doc.sampleBpm = (float)Transport::Instance().Tempo();
         s.doc.Reset(0, n);
         s.sel.clear();
         ViewAll();
      }
      ClampView();
      s.dirty = true;
      AudioEngine::Instance().StudioLoop().Restart();
   }

   bool LoadFile(const std::string& path)
   {
      Platform::SampleBuffer b;
      std::string err;
      if (!Platform::DecodeAudioFileToBuffer(path, b, err) || b.numFrames < 2 || b.channels < 1)
      {
         S().status = std::string(T("Could not open")) + " " + path + (err.empty() ? "" : ": " + err);
         return false;
      }
      auto a = std::make_shared<SourceAudio>();
      a->sr = b.sampleRate > 0.0 ? b.sampleRate : kRate;
      a->l.assign(b.channelData.begin(), b.channelData.begin() + b.numFrames);
      if (b.channels > 1)
         a->r.assign(b.channelData.begin() + b.numFrames, b.channelData.begin() + 2 * (std::ptrdiff_t)b.numFrames);
      else
         a->r = a->l;
      if (S().src)
         PushUndo();
      S().status.clear();
      SetSource(std::move(a), path, std::nullopt, false);
      return true;
   }

   bool IsAudioPath(const std::string& p)
   {
      static const std::vector<std::string> kExt = { "wav", "aif", "aiff", "mp3", "m4a", "aac", "caf", "flac", "ogg" };
      return HasExtension(p, kExt);
   }

   // ---- engine ----

   // A first take of the settings, or (pinned) the current take again with the dots as they are now.
   void Render(bool pinned)
   {
      Studio& s = S();
      GenerateSamplesNode& g = *s.gen;
      if (pinned && !s.take)
         return;
      g.part = (int)(pinned ? s.take->recipe.part : kParts[s.part]);
      g.bars = pinned ? s.take->recipe.bars : kBars[s.barsIdx];
      g.variation = pinned ? s.take->recipe.variation : s.variation;
      g.followScale = s.followScale;
      g.tone = g.energy = g.space = g.mood = 0.0f;
      if (pinned)
      {
         g.seed = (int)s.take->recipe.seed;
         g.card = s.take->card;
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
      g.Generate(pinned);
   }

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
         s.pendingDoc.reset();
         return;
      }
      auto a = std::make_shared<SourceAudio>();
      a->sr = kRate;
      a->l = g.LoopL();
      a->r = g.LoopR();
      const bool keep = s.busyPinned && s.take.has_value();
      if (s.src && !s.pendingDoc)
         PushUndo();
      SetSource(std::move(a), std::string(), TakeInfo{ g.MadeRecipe(), s.busyCard }, keep);
      if (s.pendingDoc)
      {
         const int64_t n = Frames();
         SliceSet d = std::move(*s.pendingDoc);
         s.pendingDoc.reset();
         if (d.endFrame > 0 && d.endFrame != n)
         {
            const double k = (double)n / (double)d.endFrame;
            for (Slice& sl : d.slices)
               sl.start = std::min<int64_t>(n - 1, (int64_t)std::llround((double)sl.start * k));
            d.endFrame = n;
         }
         s.doc = std::move(d);
         ViewAll();
      }
   }

   // ---- slicing ----

   void Split(int64_t frame)
   {
      Studio& s = S();
      if (!s.src)
         return;
      frame = SnapToZeroCrossing(s.src->l.data(), s.src->r.data(), Frames(), frame, 64);
      Snapshot before = Capture();
      const int i = s.doc.SplitAt(frame);
      if (i < 0)
         return;
      PushUndo(std::move(before));
      s.doc.MarkHandEdited(true);
      SelectOnly(i);
      s.dirty = true;
   }

   void JoinSelected()
   {
      Studio& s = S();
      std::vector<int> idx = SelectedIndices();
      if (idx.size() == 1 && idx[0] == 0 && s.doc.Count() > 1)
         idx[0] = 1; // the first slice joins the one after it
      if (idx.empty())
         return;
      PushUndo();
      for (auto it = idx.rbegin(); it != idx.rend(); ++it)
         if (*it >= 1)
            s.doc.Join(*it);
      s.doc.MarkHandEdited(true);
      PruneSelection();
      if (s.sel.empty())
         SelectOnly(std::max(0, idx.front() - 1));
      s.dirty = true;
   }

   // Replacing hand-placed slices asks first; the auto-slicers only replace what is between the start and end.
   bool ConfirmReplace(std::function<void()> again)
   {
      Studio& s = S();
      if (s.replaceConfirmed || !s.doc.HandEdited() || s.doc.Count() <= 1)
      {
         s.replaceConfirmed = false;
         return true;
      }
      s.pendingReplace = std::move(again);
      s.openReplaceConfirm = true;
      return false;
   }

   void DrawReplaceConfirm()
   {
      Studio& s = S();
      if (s.openReplaceConfirm)
      {
         ImGui::OpenPopup(L("Replace the slices?##studioReplace"));
         s.openReplaceConfirm = false;
      }
      if (!DialogParts::Begin(L("Replace the slices?##studioReplace")))
         return;
      DialogParts::Title(T("Replace the slices?"));
      DialogParts::Message(T("Slices you placed by hand will be replaced."));
      const int pick = DialogParts::Buttons({ L("Cancel"), L("Replace") });
      if (pick == 1 && s.pendingReplace)
      {
         s.replaceConfirmed = true;
         std::function<void()> f = std::move(s.pendingReplace);
         f();
      }
      if (pick >= 0)
      {
         s.pendingReplace = nullptr;
         ImGui::CloseCurrentPopup();
      }
      DialogParts::End();
   }

   void SliceByGrid(double perStep)
   {
      Studio& s = S();
      if (!s.src || !ConfirmReplace([perStep] { SliceByGrid(perStep); }))
         return;
      PushUndo();
      s.doc.FromGrid(perStep, s.doc.StartFrame(), s.doc.endFrame);
      s.sel.clear();
      s.dirty = true;
   }

   void SliceEqual(int n)
   {
      Studio& s = S();
      if (!s.src || !ConfirmReplace([n] { SliceEqual(n); }))
         return;
      PushUndo();
      s.doc.FromEqual(n, s.doc.StartFrame(), s.doc.endFrame);
      s.sel.clear();
      s.dirty = true;
   }

   void SliceByTransients(float sensitivity)
   {
      Studio& s = S();
      if (!s.src || s.onsetJob || !ConfirmReplace([sensitivity] { SliceByTransients(sensitivity); }))
         return;
      auto job = std::make_shared<OnsetJob>();
      job->from = s.doc.StartFrame();
      job->to = s.doc.endFrame;
      s.onsetJob = job;
      std::thread([job, src = s.src, sensitivity]()
                  {
                     std::vector<float> mono((size_t)(job->to - job->from));
                     for (size_t i = 0; i < mono.size(); i++)
                        mono[i] = 0.5f * (src->l[(size_t)job->from + i] + src->r[(size_t)job->from + i]);
                     SlicerDsp::Params p;
                     p.sensitivity = sensitivity;
                     SlicerDsp::Detect(mono.data(), (int)mono.size(), src->sr, p, job->onsets, job->strengths, &job->abort);
                     job->done.store(true, std::memory_order_release);
                  })
         .detach();
   }

   void PollOnsets()
   {
      Studio& s = S();
      if (!s.onsetJob || !s.onsetJob->done.load(std::memory_order_acquire))
         return;
      std::shared_ptr<OnsetJob> job = std::move(s.onsetJob);
      if (!s.src || job->to > Frames())
         return;
      PushUndo();
      s.doc.FromOnsets(job->onsets, job->strengths, job->from, job->to, s.onsetMax);
      s.sel.clear();
      s.dirty = true;
   }

   void ClearSlices()
   {
      Studio& s = S();
      if (!s.src || s.doc.Count() <= 1)
         return;
      PushUndo();
      const int64_t a = s.doc.StartFrame(), b = s.doc.endFrame;
      s.doc.Reset(a, b);
      s.sel.clear();
      s.dirty = true;
   }

   // Sample gain so the edit peaks at -0.3 dBFS.
   void Normalise()
   {
      Studio& s = S();
      if (!s.heard)
         return;
      const float peak = PeakOf(s.heard->l, s.heard->r);
      if (peak < 1e-6f)
         return;
      PushUndo();
      s.doc.wide.gainDb += -0.3f - 20.0f * std::log10(peak);
      s.dirty = true;
   }

   // ---- out of the studio ----

   std::string FileSafe(std::string name)
   {
      for (char& ch : name)
         ch = std::isalnum((unsigned char)ch) ? ch : ch == '#' ? 's' : '-';
      name.erase(std::unique(name.begin(), name.end(), [](char a, char b) { return a == '-' && b == '-'; }), name.end());
      return name;
   }

   std::string SourceName()
   {
      const Studio& s = S();
      if (s.take)
         return FileSafe(std::string("generated ") + SampleStyles::At(s.take->card).name + " " + SampleStyles::PartName(s.take->recipe.part));
      if (!s.path.empty())
      {
         std::string base = s.path.substr(s.path.find_last_of("/\\") + 1);
         base = base.substr(0, base.find_last_of('.'));
         return FileSafe(base + " edit");
      }
      return "sample";
   }

   bool WriteAudio(const std::string& path, const std::vector<float>& l, const std::vector<float>& r, AudioFileWriter::Format fmt, int bits)
   {
      std::vector<float> inter(l.size() * 2);
      for (size_t i = 0; i < l.size(); i++)
      {
         inter[i * 2] = l[i];
         inter[i * 2 + 1] = r[i];
      }
      AudioFileWriter w;
      if (!w.Open(path, Sr(), 2, fmt, bits))
         return false;
      w.Append(inter.data(), (int)l.size());
      w.Close();
      return true;
   }

   // The edit as heard, rendered now (not the cached result, which may be a step behind). "" on failure.
   std::string WriteEdit(std::string path, const char* ext, AudioFileWriter::Format fmt, int bits, SliceRenderResult* outRes = nullptr)
   {
      Studio& s = S();
      if (!s.src)
         return std::string();
      SliceRenderResult res;
      RenderSliceSet(*s.src, s.doc, s.cache.get(), nullptr, res);
      if (res.l.empty())
         return std::string();
      if (path.empty())
         path = AudioRecordings::GenerateFilePath(SourceName(), ext);
      if (!WriteAudio(path, res.l, res.r, fmt, bits))
         return std::string();
      if (outRes != nullptr)
         *outRes = std::move(res);
      return path;
   }

   void Export(const char* ext, AudioFileWriter::Format fmt, int bits)
   {
      const std::string path = WriteEdit(std::string(), ext, fmt, bits);
      if (!path.empty())
         Platform::RevealInFileManager(path);
   }

   // Each slice through its own chain, one file per slice. Returns the paths in slice order.
   std::vector<std::string> WriteSlices(const std::string& folder)
   {
      Studio& s = S();
      std::vector<std::string> out;
      if (!s.src)
         return out;
      const std::string base = SourceName();
      for (int i = 0; i < s.doc.Count(); i++)
      {
         std::vector<float> l, r;
         int64_t body = 0;
         RenderSlice(*s.src, s.doc, i, false, l, r, body);
         if (l.empty())
            continue;
         char num[8];
         snprintf(num, sizeof(num), "%02d", i + 1);
         const std::string name = base + "-" + num + (s.doc.slices[(size_t)i].name.empty() ? "" : "-" + FileSafe(s.doc.slices[(size_t)i].name));
         const std::string path = folder.empty() ? AudioRecordings::GenerateFilePath(name, "wav") : folder + "/" + name + ".wav";
         if (WriteAudio(path, l, r, AudioFileWriter::Format::Wav, 24))
            out.push_back(path);
      }
      return out;
   }

   void ExportSlices()
   {
      const std::string folder = Platform::OpenFolderDialog(T("Export slices to"));
      if (folder.empty())
         return;
      const std::vector<std::string> paths = WriteSlices(folder);
      if (!paths.empty())
         Platform::RevealInFileManager(paths.front());
   }

   // The slice starts in the written file, 0..1, for the Slicer.
   std::vector<float> SliceFractions(const SliceRenderResult& res)
   {
      const Studio& s = S();
      std::vector<float> f;
      if (res.laidFrames <= 0 || res.outStart.size() < 2)
         return f;
      const double laid = (double)res.laidFrames;
      for (size_t i = 0; i + 1 < res.outStart.size(); i++)
         f.push_back((float)((double)res.outStart[i] / laid));
      if (s.doc.wide.reverse)
      {
         for (float& x : f)
            x = 1.0f - x;
         f.erase(f.begin()); // what was the first start is now the end
         f.insert(f.begin(), 0.0f);
         for (size_t i = 1; i < f.size(); i++)
            f[i] = 1.0f - (float)((double)res.outStart[res.outStart.size() - 1 - i] / laid);
         std::sort(f.begin(), f.end());
      }
      if (res.bounced)
         for (float& x : f)
            x *= 0.5f;
      return f;
   }

   // The graph that made the take, with an Audio Out on its tap, pasted onto the canvas inside a Group.
   void ConvertToPatch()
   {
      const Studio& s = S();
      if (!s.take)
         return;
      const SampleStyles::Compiled c = SampleStyles::Compile(s.take->recipe);
      if (!c.error.empty())
         return;
      int last = 0;
      for (size_t at = c.patch.find("\nnode "); at != std::string::npos; at = c.patch.find("\nnode ", at + 1))
         last = std::max(last, atoi(c.patch.c_str() + at + 6));
      const std::string text = "infinite-nodes v" + std::to_string(Patch::FormatVersion()) + "\n" + c.patch + "node " +
                               std::to_string(last + 1) + " Utility Audio Out\nend\naud " + std::to_string(last + 1) +
                               " 0 " + std::to_string(c.tap) + "\n";
      const ImVec2 at = FindFreeSpawnPosition(gViewCenterCanvas);
      const NodePasteResult res = NodeClipboardPaste(text, at);
      if (!res.ok || res.newIndices.empty())
         return;
      // A tiny box at the paste point: the group's auto-fit sizes it around its members from the next frame.
      gSuppressUndoCheckpoints = true;
      if (GraphNode* ggn = SpawnNode("Group", "Compositing", at.x, at.y))
         if (auto* grp = dynamic_cast<GroupNode*>(ggn->node.get()))
         {
            grp->label = std::string(SampleStyles::At(s.take->card).name) + " " + SampleStyles::PartName(s.take->recipe.part);
            grp->width = 8.0f;
            grp->height = 8.0f;
            gGroupMembers[grp] = std::set<int>(res.newIndices.begin(), res.newIndices.end());
         }
      gSuppressUndoCheckpoints = false;
      gPatchDirty = true;
   }

   // A node that plays the edit: Generate Samples keeps the recipe (it re-renders the same take between the start
   // and end markers), the others load the edit as heard from a .wav in the Recordings folder. The Slicer gets
   // these slices.
   void ConvertToNode(const char* type)
   {
      Studio& s = S();
      if (!s.src)
         return;
      const bool generator = std::strcmp(type, "Generate Samples") == 0;
      if (generator && !s.take)
         return;
      SliceRenderResult res;
      const std::string wav = generator ? std::string() : WriteEdit(std::string(), "wav", AudioFileWriter::Format::Wav, 24, &res);
      if (!generator && wav.empty())
         return;
      const ImVec2 at = FindFreeSpawnPosition(gViewCenterCanvas);
      PushUndoCheckpoint("Add sample from studio");
      GraphNode* gn = SpawnNode(type, "Synths", at.x, at.y);
      if (gn == nullptr)
         return;
      INode* node = gn->node.get();
      if (generator)
      {
         if (auto* n = dynamic_cast<GenerateSamplesNode*>(node))
         {
            const TakeInfo& t = *s.take;
            n->part = (int)t.recipe.part;
            n->bars = t.recipe.bars;
            n->seed = (int)t.recipe.seed;
            n->card = t.card;
            n->variation = t.recipe.variation;
            n->tone = t.recipe.tone;
            n->energy = t.recipe.energy;
            n->space = t.recipe.space;
            n->mood = t.recipe.mood;
            n->followTransport = true;
            n->followScale = s.followScale;
            n->keepSeed = true;
            n->Generate();
            const double total = (double)std::max<int64_t>(1, Frames());
            n->start = (float)((double)s.doc.StartFrame() / total);
            n->end = (float)((double)s.doc.endFrame / total);
         }
      }
      else if (auto* n = dynamic_cast<SamplerNode*>(node))
         n->LoadFile(wav);
      else if (auto* n = dynamic_cast<SlicerNode*>(node))
      {
         n->LoadFile(wav);
         if (s.doc.Count() > 1)
            n->SetSlices(SliceFractions(res));
      }
      else if (auto* n = dynamic_cast<MolderNode*>(node))
         n->LoadFile(wav);
      else if (auto* n = dynamic_cast<GrainMolderNode*>(node))
         n->LoadFile(wav);
      else if (auto* n = dynamic_cast<PaulStretchNode*>(node))
         n->LoadFile(wav);
      else if (auto* n = dynamic_cast<GranularNode*>(node))
         n->LoadFile(wav);
      gPatchDirty = true;
   }

   // One Sampler per slice, in a group named after the source.
   void SlicesToSamplers()
   {
      Studio& s = S();
      if (!s.src)
         return;
      const std::vector<std::string> paths = WriteSlices(std::string());
      if (paths.empty())
         return;
      PushUndoCheckpoint("Slices to Samplers");
      gSuppressUndoCheckpoints = true;
      const ImVec2 at = FindFreeSpawnPosition(gViewCenterCanvas);
      std::set<int> members;
      ImVec2 pos = at;
      for (const std::string& p : paths)
      {
         pos = FindFreeSpawnPosition(pos);
         if (GraphNode* gn = SpawnNode("Sampler", "Synths", pos.x, pos.y))
         {
            if (auto* n = dynamic_cast<SamplerNode*>(gn->node.get()))
               n->LoadFile(p);
            members.insert(gn->index);
         }
      }
      if (GraphNode* ggn = SpawnNode("Group", "Compositing", at.x, at.y))
         if (auto* grp = dynamic_cast<GroupNode*>(ggn->node.get()))
         {
            grp->label = SourceName() + " slices";
            grp->width = 8.0f;
            grp->height = 8.0f;
            gGroupMembers[grp] = members;
         }
      gSuppressUndoCheckpoints = false;
      gPatchDirty = true;
   }

   // Play: the edit on the transport, with audio handed to the studio the way Timeline audio takes it from the canvas.
   void SetStudioAudio(bool on)
   {
      if (on == (gAudioMode == AudioMode::Studio))
         return;
      gAudioMode = on ? AudioMode::Studio : AudioMode::Canvas;
      if (on && AudioEngine::Instance().SampleRate() <= 0.0)
      {
         gAudioStartError.clear();
         if (!StartAudioEngine(gAudioStartError))
            fprintf(stderr, "audio device: %s\n", gAudioStartError.c_str());
      }
      RebuildAudioTopology();
   }

   void TogglePlay()
   {
      const bool playing = Transport::Instance().IsPlaying();
      if (!playing)
      {
         SetStudioAudio(true);
         AudioEngine::Instance().StudioLoop().Restart();
      }
      LaunchRequestTogglePlay();
   }

   // One slice through its chain, on the preview voice (no transport needed).
   void Audition(int i)
   {
      Studio& s = S();
      if (!s.src || i < 0 || i >= s.doc.Count())
         return;
      if (AudioEngine::Instance().SampleRate() <= 0.0)
      {
         gAudioStartError.clear();
         StartAudioEngine(gAudioStartError);
      }
      std::vector<float> l, r;
      int64_t body = 0;
      RenderSlice(*s.src, s.doc, i, false, l, r, body);
      if (l.empty())
         return;
      auto* buf = new Platform::SampleBuffer();
      buf->channels = 2;
      buf->sampleRate = Sr();
      buf->numFrames = (int)l.size();
      buf->channelData.resize(l.size() * 2);
      std::copy(l.begin(), l.end(), buf->channelData.begin());
      std::copy(r.begin(), r.end(), buf->channelData.begin() + (std::ptrdiff_t)l.size());
      AudioEngine::Instance().Preview().Play(buf);
   }

   // ---- slice edits from keys and menus ----

   template <typename F>
   void EditSelected(F f)
   {
      Studio& s = S();
      const std::vector<int> idx = SelectedIndices();
      if (idx.empty())
         return;
      PushUndo();
      for (int i : idx)
         f(s.doc.slices[(size_t)i].p);
      s.dirty = true;
   }

   void SelectStep(int dir)
   {
      Studio& s = S();
      if (s.doc.Empty())
         return;
      const int cur = s.sel.empty() ? (dir > 0 ? -1 : s.doc.Count()) : s.doc.IndexOfId(s.primary);
      const int next = std::clamp(cur + dir, 0, s.doc.Count() - 1);
      SelectOnly(next);
      const double a = (double)s.doc.slices[(size_t)next].start, b = (double)s.doc.EndOf(next);
      if (a < s.viewStart || b > s.viewStart + s.viewLen)
      {
         s.viewStart = (a + b) * 0.5 - s.viewLen * 0.5;
         ClampView();
      }
   }

   // ---- the waveform ----

   struct WaveGeom
   {
      ImVec2 mn, mx; // the main wave area
      double FrameAt(float x) const { return S().viewStart + (double)(x - mn.x) / (double)(mx.x - mn.x) * S().viewLen; }
      float XOf(double f) const { return mn.x + (float)((f - S().viewStart) / S().viewLen) * (mx.x - mn.x); }
   };

   // Ruler step: the smallest of 1-2-5 x 10^n units that leaves at least `minPx` between ticks.
   double NiceStep(double unitsPerPx, double minPx)
   {
      const double want = unitsPerPx * minPx;
      double step = std::pow(10.0, std::floor(std::log10(std::max(1e-9, want))));
      for (double m : { 1.0, 2.0, 5.0, 10.0 })
         if (step * m >= want)
            return step * m;
      return step * 10.0;
   }

   void DrawRuler(ImDrawList* dl, ImVec2 mn, ImVec2 mx, const WaveGeom& g)
   {
      Studio& s = S();
      ImGui::SetCursorScreenPos(mn);
      ImGui::InvisibleButton("##studioRuler", ImVec2(mx.x - mn.x, mx.y - mn.y));
      if (ImGui::IsItemClicked())
         s.ruler = (s.ruler + 1) % 3;
      if (ImGui::IsItemHovered())
         HelpTip("%s", T("Click: bars, seconds or samples"));
      const ImU32 tick = ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.6f), txt = ImGui::GetColorU32(ImGuiCol_TextDisabled);
      const double fpp = s.viewLen / std::max(1.0f, mx.x - mn.x);
      const double v0 = s.viewStart, v1 = s.viewStart + s.viewLen;
      const auto label = [&](double f, const char* text)
      {
         const float x = g.XOf(f);
         dl->AddLine(ImVec2(x, mx.y - 5.0f), ImVec2(x, mx.y), tick);
         dl->AddText(ImVec2(x + 2.0f, mn.y), txt, text);
      };
      char buf[48];
      if (s.ruler == 0)
      {
         // Bars and beats of the sample's tempo; finer steps as the zoom allows.
         const double fpb = FramesPerBeat();
         double stepBeats = 4.0;
         while (stepBeats / 4.0 * fpb / fpp > 70.0 && stepBeats > 0.25)
            stepBeats /= 2.0;
         while (stepBeats * fpb / fpp < 50.0)
            stepBeats *= 2.0;
         for (double b = std::floor(v0 / fpb / stepBeats) * stepBeats; b * fpb <= v1; b += stepBeats)
         {
            const int bar = (int)std::floor(b / 4.0 + 1e-9);
            const double inBar = b - bar * 4.0;
            if (std::fabs(inBar) < 1e-6)
               snprintf(buf, sizeof(buf), "%d", bar + 1);
            else
               snprintf(buf, sizeof(buf), "%d.%g", bar + 1, inBar + 1.0);
            label(b * fpb, buf);
         }
      }
      else if (s.ruler == 1)
      {
         const double step = NiceStep(fpp / Sr(), 60.0);
         for (double t = std::floor(v0 / Sr() / step) * step; t * Sr() <= v1; t += step)
         {
            snprintf(buf, sizeof(buf), step < 0.01 ? "%.4fs" : step < 1.0 ? "%.2fs" : "%.0fs", t);
            label(t * Sr(), buf);
         }
      }
      else
      {
         const double step = std::max(1.0, NiceStep(fpp, 70.0));
         for (double f = std::floor(v0 / step) * step; f <= v1; f += step)
         {
            snprintf(buf, sizeof(buf), "%.0f", f);
            label(f, buf);
         }
      }
   }

   void DrawOverview(ImDrawList* dl, ImVec2 mn, ImVec2 mx)
   {
      Studio& s = S();
      const float w = mx.x - mn.x;
      ImGui::SetCursorScreenPos(mn);
      ImGui::InvisibleButton("##studioOverview", ImVec2(w, mx.y - mn.y));
      dl->AddRectFilled(mn, mx, ImGui::GetColorU32(ImGuiCol_FrameBg, 0.6f), 3.0f);
      const double total = (double)Frames();
      if (total <= 0.0)
         return;
      if (ImGui::IsItemActive())
      {
         s.viewStart = (double)(ImGui::GetIO().MousePos.x - mn.x) / w * total - s.viewLen * 0.5;
         ClampView();
      }
      if (ImGui::IsItemHovered())
         HelpTip("%s", T("Drag to move the view; double-click to see it all"));
      if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
         ViewAll();
      const float mid = (mn.y + mx.y) * 0.5f, amp = (mx.y - mn.y) * 0.45f;
      const ImU32 col = ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.6f);
      for (int x = 0; x < (int)w; x++)
      {
         float lo, hi;
         RangeMinMax((int64_t)(total * x / w), (int64_t)(total * (x + 1) / w), lo, hi);
         dl->AddLine(ImVec2(mn.x + x + 0.5f, mid - hi * amp), ImVec2(mn.x + x + 0.5f, mid - lo * amp + 1.0f), col);
      }
      const float va = mn.x + (float)(s.viewStart / total) * w, vb = mn.x + (float)((s.viewStart + s.viewLen) / total) * w;
      dl->AddRectFilled(ImVec2(va, mn.y), ImVec2(std::max(va + 2.0f, vb), mx.y), AccentU32(0.18f), 2.0f);
      dl->AddRect(ImVec2(va, mn.y), ImVec2(std::max(va + 2.0f, vb), mx.y), AccentU32(0.7f), 2.0f);
   }

   // The marker under x: kind 0 a slice start (its index), kind 1 the end marker. -1 when none is within reach.
   int MarkerAt(const WaveGeom& g, float x, int& index)
   {
      Studio& s = S();
      float best = 6.0f;
      int kind = -1;
      for (int i = 0; i < s.doc.Count(); i++)
         if (const float d = std::fabs(g.XOf((double)s.doc.slices[(size_t)i].start) - x); d < best)
         {
            best = d;
            kind = 0;
            index = i;
         }
      if (const float d = std::fabs(g.XOf((double)s.doc.endFrame) - x); d < best)
      {
         kind = 1;
         index = -1;
      }
      return kind;
   }

   void HandleWaveInput(const WaveGeom& g)
   {
      Studio& s = S();
      ImGuiIO& io = ImGui::GetIO();
      const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
      const bool cmd = io.KeyCtrl || io.KeySuper;
      const double mf = std::clamp(g.FrameAt(io.MousePos.x), 0.0, (double)Frames());

      // Wheel: scroll; Cmd zooms around the pointer; Alt zooms the level.
      ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
      ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelX);
      if (hovered && (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f))
      {
         if (io.KeyAlt)
            s.level = std::clamp(s.level * std::pow(1.15f, io.MouseWheel), 0.25f, 32.0f);
         else if (cmd)
            ZoomAround(mf, std::pow(0.85, (double)io.MouseWheel));
         else
         {
            const float d = io.MouseWheelH != 0.0f ? io.MouseWheelH : io.MouseWheel;
            s.viewStart -= (double)d * s.viewLen * 0.08;
            ClampView();
         }
      }

      int idx = -1;
      const int near = hovered || active ? MarkerAt(g, io.MousePos.x, idx) : -1;
      if ((near >= 0 && hovered) || s.dragKind == 0 || s.dragKind == 1)
         ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

      if (ImGui::IsItemActivated())
      {
         if (near >= 0)
         {
            PushUndo();
            s.dragKind = near;
            s.dragIndex = idx;
            if (near == 0)
               SelectOnly(idx);
         }
         else if (!s.doc.Empty())
         {
            const int i = s.doc.IndexAt((int64_t)mf);
            const uint16_t id = s.doc.slices[(size_t)i].id;
            if (io.KeyShift && !s.sel.empty())
            {
               const int a = std::min(i, s.doc.IndexOfId(s.primary)), b = std::max(i, s.doc.IndexOfId(s.primary));
               s.sel.clear();
               for (int k = a; k <= b; k++)
                  s.sel.push_back(s.doc.slices[(size_t)k].id);
            }
            else if (cmd)
            {
               if (IsSel(id))
                  s.sel.erase(std::find(s.sel.begin(), s.sel.end(), id));
               else
               {
                  s.sel.push_back(id);
                  s.primary = id;
               }
               PruneSelection();
            }
            else if (IsSel(id) && s.sel.size() == 1)
               ; // a second click on the selection keeps it
            else
               SelectOnly(i);
         }
      }
      if (active && (s.dragKind == 0 || s.dragKind == 1))
      {
         int64_t f = (int64_t)std::llround(mf);
         if (s.snapGrid != io.KeyShift && s.doc.sampleBpm > 0.0f) // Shift flips the grid snap while dragging
         {
            const double step = FramesPerBeat() / 4.0;
            f = (int64_t)std::llround(std::round((double)f / step) * step);
         }
         if (s.dragKind == 0)
            s.doc.Move(s.dragIndex, f);
         else
            s.doc.MoveEnd(f);
         s.dirty = true;
      }
      if (ImGui::IsItemDeactivated() && (s.dragKind == 0 || s.dragKind == 1))
      {
         // Land on a zero crossing near where it was let go (the grid stays within 32 frames of it).
         const bool grid = s.snapGrid != io.KeyShift && s.doc.sampleBpm > 0.0f;
         const int64_t at = s.dragKind == 0 ? s.doc.slices[(size_t)s.dragIndex].start : s.doc.endFrame;
         const int64_t z = SnapToZeroCrossing(s.src->l.data(), s.src->r.data(), Frames(), at, grid ? 32 : 64);
         if (s.dragKind == 0)
            s.doc.Move(s.dragIndex, z);
         else
            s.doc.MoveEnd(z);
         s.doc.MarkHandEdited(true);
         s.dragKind = -1;
         s.dirty = true;
      }
      if (hovered && near < 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
         Split((int64_t)mf);

      // Keys, while the pointer is over the waveform (the canvas's are off then: gSampleStudioClaimedKeys).
      if (!hovered || io.WantTextInput)
         return;
      if (cmd && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false))
         Undo();
      else if (cmd && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false))
         Redo();
      else if (cmd && ImGui::IsKeyPressed(ImGuiKey_E, false))
         Split((int64_t)mf);
      else if (cmd && ImGui::IsKeyPressed(ImGuiKey_A, false))
      {
         s.sel.clear();
         for (const Slice& sl : s.doc.slices)
            s.sel.push_back(sl.id);
         if (!s.sel.empty())
            s.primary = s.sel.front();
      }
      else if (cmd)
         return;
      else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
         JoinSelected();
      else if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
         io.KeyShift ? ViewAll() : ZoomToSelection();
      else if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd))
         ZoomAround(mf, 0.7);
      else if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract))
         ZoomAround(mf, 1.0 / 0.7);
      else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
         SelectStep(-1);
      else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
         SelectStep(1);
      else if (ImGui::IsKeyPressed(ImGuiKey_Q, false))
         Audition(s.sel.empty() ? s.doc.IndexAt((int64_t)mf) : s.doc.IndexOfId(s.primary));
      else if (ImGui::IsKeyPressed(ImGuiKey_M, false))
         EditSelected([](SliceParams& p) { p.mute = !p.mute; });
      else if (ImGui::IsKeyPressed(ImGuiKey_S, false))
         EditSelected([](SliceParams& p) { p.solo = !p.solo; });
      else if (ImGui::IsKeyPressed(ImGuiKey_R, false))
         EditSelected([](SliceParams& p) { p.reverse = !p.reverse; });
   }

   void DrawWaveContextMenu(double frame)
   {
      Studio& s = S();
      if (!MenuParts::BeginPopup("##studioWaveMenu"))
         return;
      const bool any = !s.sel.empty();
      if (MenuParts::Item(T("Split here"), "Cmd+E"))
         Split((int64_t)frame);
      if (MenuParts::Item(T("Join"), "Delete", false, any && s.doc.Count() > 1))
         JoinSelected();
      MenuParts::Separator();
      if (MenuParts::Item(T("Play slice"), "Q", false, any))
         Audition(s.doc.IndexOfId(s.primary));
      if (MenuParts::Item(T("Mute"), "M", false, any))
         EditSelected([](SliceParams& p) { p.mute = !p.mute; });
      if (MenuParts::Item(T("Solo"), "S", false, any))
         EditSelected([](SliceParams& p) { p.solo = !p.solo; });
      if (MenuParts::Item(T("Reverse"), "R", false, any))
         EditSelected([](SliceParams& p) { p.reverse = !p.reverse; });
      if (MenuParts::SubMenu(T("Colour"), any))
      {
         for (int c = -1; c < kPaletteSize; c++)
         {
            char lab[32];
            snprintf(lab, sizeof(lab), c < 0 ? "%s" : "%s %d", c < 0 ? T("By position") : T("Colour"), c + 1);
            if (MenuParts::Item(lab))
            {
               PushUndo();
               for (int i : SelectedIndices())
                  s.doc.slices[(size_t)i].colour = c;
            }
         }
         ImGui::EndMenu();
      }
      MenuParts::Separator();
      if (MenuParts::Item(T("Zoom to selection"), "Z", false, any))
         ZoomToSelection();
      if (MenuParts::Item(T("Zoom out"), "Shift+Z"))
         ViewAll();
      MenuParts::EndPopup();
   }

   void DrawWave(ImVec2 mn, ImVec2 size)
   {
      Studio& s = S();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 mx(mn.x + size.x, mn.y + size.y);
      if (!s.src)
      {
         ImGui::SetCursorScreenPos(mn);
         ImGui::InvisibleButton("##studioWave", size);
         gSampleStudioClaimedKeys = false;
         FieldWell::Draw(dl, mn, mx, false, false);
         if (s.busy)
         {
            const float f = std::clamp(s.gen->Progress(), 0.0f, 1.0f);
            dl->AddRectFilled(ImVec2(mn.x, mx.y - 2.0f), ImVec2(mn.x + size.x * f, mx.y), ImGui::GetColorU32(AccentEmphasisSelected()));
         }
         const char* hint = T("Generate a take, open a file, or drop a sample here");
         const ImVec2 ts = ImGui::CalcTextSize(hint);
         dl->AddText(ImVec2((mn.x + mx.x - ts.x) * 0.5f, (mn.y + mx.y - ts.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), hint);
         return;
      }
      EnsurePeaks();
      ClampView();

      DrawOverview(dl, mn, ImVec2(mx.x, mn.y + kOverviewH));
      WaveGeom g;
      g.mn = ImVec2(mn.x, mn.y + kOverviewH + kRulerH + 2.0f);
      g.mx = mx;
      DrawRuler(dl, ImVec2(mn.x, mn.y + kOverviewH + 1.0f), ImVec2(mx.x, mn.y + kOverviewH + kRulerH + 1.0f), g);

      ImGui::SetCursorScreenPos(g.mn);
      ImGui::InvisibleButton("##studioWave", ImVec2(size.x, g.mx.y - g.mn.y));
      gSampleStudioClaimedKeys = ImGui::IsItemHovered() && !ImGui::GetIO().WantTextInput;
      const double menuFrame = g.FrameAt(ImGui::GetIO().MousePos.x);
      if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
      {
         const int i = s.doc.IndexAt((int64_t)menuFrame);
         if (!IsSel(s.doc.slices[(size_t)i].id))
            SelectOnly(i);
         ImGui::OpenPopup("##studioWaveMenu");
      }
      HandleWaveInput(g);
      static double sMenuFrame = 0.0;
      if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
         sMenuFrame = menuFrame;
      DrawWaveContextMenu(sMenuFrame);

      FieldWell::Draw(dl, g.mn, g.mx, false, false);
      dl->PushClipRect(g.mn, g.mx, true);
      const float w = g.mx.x - g.mn.x, top = g.mn.y + kTabH, mid = (top + g.mx.y) * 0.5f, amp = (g.mx.y - top) * 0.46f * s.level;
      const int64_t n = Frames();

      // Slice regions: a wash in the slice's colour, stronger when selected.
      for (int i = 0; i < s.doc.Count(); i++)
      {
         const float xa = g.XOf((double)s.doc.slices[(size_t)i].start), xb = g.XOf((double)s.doc.EndOf(i));
         if (xb < g.mn.x || xa > g.mx.x)
            continue;
         const bool sel = IsSel(s.doc.slices[(size_t)i].id);
         dl->AddRectFilled(ImVec2(xa, top), ImVec2(xb, g.mx.y), SliceColour(s.doc, i, sel ? 0.20f : 0.07f));
      }
      // Outside the start and end markers: dimmed.
      const ImU32 shade = ImGui::GetColorU32(ImGuiCol_WindowBg, 0.55f);
      dl->AddRectFilled(ImVec2(g.mn.x, top), ImVec2(std::max(g.mn.x, g.XOf((double)s.doc.StartFrame())), g.mx.y), shade);
      dl->AddRectFilled(ImVec2(std::min(g.mx.x, g.XOf((double)s.doc.endFrame)), top), ImVec2(g.mx.x, g.mx.y), shade);

      // Beat grid, when the sample has a tempo and the beats are far enough apart.
      if (s.doc.sampleBpm > 0.0f)
      {
         const double fpb = FramesPerBeat(), pxPerBeat = fpb / s.viewLen * w;
         const double step = pxPerBeat > 40.0 ? 0.25 : pxPerBeat > 8.0 ? 1.0 : 4.0;
         if (pxPerBeat * step > 6.0)
            for (double b = std::ceil(s.viewStart / fpb / step) * step; b * fpb < s.viewStart + s.viewLen; b += step)
            {
               const float x = g.XOf(b * fpb);
               const bool bar = std::fabs(std::fmod(b, 4.0)) < 1e-6, beat = std::fabs(b - std::round(b)) < 1e-6;
               dl->AddLine(ImVec2(x, top), ImVec2(x, g.mx.y), ImGui::GetColorU32(ImGuiCol_TextDisabled, bar ? 0.28f : beat ? 0.14f : 0.06f));
            }
      }
      dl->AddLine(ImVec2(g.mn.x, mid), ImVec2(g.mx.x, mid), ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.15f));

      // The waveform: min/max columns, a line through the samples when zoomed in, dots on them when deeper still.
      const double fpp = s.viewLen / w;
      const ImU32 waveCol = ImGui::GetColorU32(ImGuiCol_Text, 0.78f), mutedCol = ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.35f);
      const bool anySolo = s.doc.AnySolo();
      const auto audible = [&](int64_t f)
      {
         const int i = s.doc.IndexAt(f);
         const SliceParams& p = s.doc.slices[(size_t)i].p;
         return f >= s.doc.StartFrame() && f < s.doc.endFrame && !p.mute && (!anySolo || p.solo);
      };
      if (fpp > 1.5)
      {
         for (int x = 0; x < (int)w; x++)
         {
            const int64_t a = (int64_t)(s.viewStart + fpp * x), b = (int64_t)(s.viewStart + fpp * (x + 1));
            if (a >= n)
               break;
            float lo, hi;
            RangeMinMax(a, std::max(a + 1, b), lo, hi);
            const float px = g.mn.x + (float)x + 0.5f;
            dl->AddLine(ImVec2(px, mid - hi * amp), ImVec2(px, mid - lo * amp + 1.0f), audible(a) ? waveCol : mutedCol);
         }
      }
      else
      {
         const int64_t a = std::max<int64_t>(0, (int64_t)s.viewStart - 1), b = std::min<int64_t>(n, (int64_t)(s.viewStart + s.viewLen) + 2);
         ImVec2 prev;
         for (int64_t f = a; f < b; f++)
         {
            const float v = 0.5f * (s.src->l[(size_t)f] + s.src->r[(size_t)f]);
            const ImVec2 p(g.XOf((double)f), mid - v * amp);
            const ImU32 c = audible(f) ? waveCol : mutedCol;
            if (f > a)
               dl->AddLine(prev, p, c, 1.2f);
            if (fpp < 0.25)
               dl->AddCircleFilled(p, 2.5f, c);
            prev = p;
         }
      }

      // Slice markers: a line in the slice's colour and a numbered tab; the sample's start and end in the accent.
      const ImU32 acc = ImGui::GetColorU32(AccentEmphasisSelected());
      for (int i = 0; i < s.doc.Count(); i++)
      {
         const float x = g.XOf((double)s.doc.slices[(size_t)i].start);
         const float xn = g.XOf((double)s.doc.EndOf(i));
         if (xn < g.mn.x || x > g.mx.x)
            continue;
         const bool sel = IsSel(s.doc.slices[(size_t)i].id);
         const ImU32 c = i == 0 ? acc : SliceColour(s.doc, i);
         dl->AddLine(ImVec2(x, g.mn.y), ImVec2(x, g.mx.y), c, sel || i == 0 ? 2.0f : 1.0f);
         const std::string lab = SliceLabel(s.doc, i);
         const ImVec2 ts = ImGui::CalcTextSize(lab.c_str());
         const float tabW = std::min(ts.x + 8.0f, std::max(14.0f, xn - x - 1.0f));
         dl->AddRectFilled(ImVec2(x, g.mn.y), ImVec2(x + tabW, g.mn.y + kTabH), SliceColour(s.doc, i, sel ? 0.95f : 0.6f), 2.0f);
         dl->PushClipRect(ImVec2(x, g.mn.y), ImVec2(x + tabW, g.mn.y + kTabH), true);
         dl->AddText(ImVec2(x + 4.0f, g.mn.y + (kTabH - ts.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_WindowBg), lab.c_str());
         dl->PopClipRect();
         const SliceParams& p = s.doc.slices[(size_t)i].p;
         // Small state letters after the tab: muted, solo, reversed, stretched.
         std::string flags;
         if (p.mute)
            flags += "M ";
         if (p.solo)
            flags += "S ";
         if (p.reverse)
            flags += "R ";
         if (p.stretchMode != 0 || p.lengthRatio != 1.0f)
            flags += "T ";
         if (!flags.empty() && xn - x - tabW > 30.0f)
            dl->AddText(ImVec2(x + tabW + 3.0f, g.mn.y), ImGui::GetColorU32(ImGuiCol_TextDisabled), flags.c_str());
      }
      {
         const float x = g.XOf((double)s.doc.endFrame);
         dl->AddLine(ImVec2(x, g.mn.y), ImVec2(x, g.mx.y), acc, 2.0f);
         dl->AddTriangleFilled(ImVec2(x, g.mn.y), ImVec2(x - 8.0f, g.mn.y), ImVec2(x, g.mn.y + 8.0f), acc);
      }

      // Playhead: the transport's place in the edit as heard, mapped back onto the source.
      const float u = AudioEngine::Instance().StudioLoop().Phase();
      if (u >= 0.0f && s.heard && s.heard->laidFrames > 0)
      {
         double k = u;
         if (s.heard->bounced)
            k = k < 0.5 ? k * 2.0 : 2.0 - k * 2.0;
         if (s.doc.wide.reverse)
            k = 1.0 - k;
         const double src = LaidToSource(s.doc, *s.heard, k * (double)s.heard->laidFrames);
         const float x = g.XOf(src);
         dl->AddLine(ImVec2(x, g.mn.y), ImVec2(x, g.mx.y), ImGui::GetColorU32(ImGuiCol_Text), 1.0f);
      }
      if (s.job || s.onsetJob || s.busy)
         dl->AddRectFilled(ImVec2(g.mn.x, g.mx.y - 2.0f), ImVec2(g.mn.x + w * (s.busy ? std::clamp(s.gen->Progress(), 0.0f, 1.0f) : 1.0f), g.mx.y),
                           AccentU32(s.busy ? 1.0f : 0.4f));
      dl->PopClipRect();
   }

   // ---- the inspector ----

   // Tracks whether any widget in a block was grabbed or changed, so one undo step covers one gesture.
   struct EditTrack
   {
      bool activated = false, changed = false;
      void Item()
      {
         activated |= ImGui::IsItemActivated();
         changed |= ImGui::IsItemEdited();
      }
      void Changed(bool c)
      {
         activated |= c;
         changed |= c;
      }
   };

   void Row(const char* label, float fieldW)
   {
      SectionCard::RowLabel(label);
      ImGui::SetNextItemWidth(fieldW);
   }

   bool ComboRow(const char* label, const char* id, int& cur, const std::vector<const char*>& items, float fieldW)
   {
      Row(label, fieldW);
      bool changed = false;
      if (MenuParts::BeginCombo(id, items[(size_t)std::clamp(cur, 0, (int)items.size() - 1)]))
      {
         for (int i = 0; i < (int)items.size(); i++)
            if (MenuParts::Choice(items[(size_t)i], i == cur) && i != cur)
            {
               cur = i;
               changed = true;
            }
         MenuParts::EndCombo();
      }
      return changed;
   }

   void DrawSliceInspector(float fieldW)
   {
      Studio& s = S();
      const int pi = s.doc.IndexOfId(s.primary);
      if (pi < 0)
         return;
      Slice& primary = s.doc.slices[(size_t)pi];
      const SliceParams before = primary.p;
      SliceParams e = before;
      const Snapshot snap = Capture();
      EditTrack t;

      SectionCard::Begin(T("Slice"));
      if (s.sel.size() == 1)
      {
         Row(T("Name"), fieldW);
         std::string name = primary.name;
         if (FieldWell::InputText("##sliceName", &name))
         {
            primary.name = name;
            t.Changed(true);
         }
      }
      SectionCard::RowLabel(T("Colour"));
      {
         // The palette as small chips; the first is "by position".
         const float d = 14.0f;
         for (int c = -1; c < kPaletteSize; c++)
         {
            if (c > -1)
               ImGui::SameLine(0.0f, 3.0f);
            ImGui::PushID(c);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##col", ImVec2(d, d)))
            {
               PushUndo();
               for (int i : SelectedIndices())
                  s.doc.slices[(size_t)i].colour = c;
            }
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float h = (float)((c < 0 ? pi : c) % kPaletteSize) / (float)kPaletteSize;
            float r, gg, b;
            ImGui::ColorConvertHSVtoRGB(h, c < 0 ? 0.0f : 0.55f, 0.88f, r, gg, b);
            dl->AddCircleFilled(ImVec2(p.x + d * 0.5f, p.y + d * 0.5f), d * 0.42f, ImGui::GetColorU32(ImVec4(r, gg, b, 1.0f)));
            if (primary.colour == c)
               dl->AddCircle(ImVec2(p.x + d * 0.5f, p.y + d * 0.5f), d * 0.5f, ImGui::GetColorU32(ImGuiCol_Text), 0, 1.5f);
            if (ImGui::IsItemHovered())
               HelpTip("%s", c < 0 ? T("By position") : T("Slice colour"));
            ImGui::PopID();
            if (c == 4)
               SectionCard::RowLabel("");
         }
      }
      if (ActionButton::Draw(L("Play slice##sliceAudition"), ImVec2(fieldW * 0.5f, 0.0f)))
         Audition(pi);

      SectionCard::Begin(T("Level"));
      Row(T("Gain"), fieldW);
      FieldWell::Slider("##sliceGain", &e.gainDb, -60.0f, 12.0f, "%+.1f dB");
      t.Item();
      Row(T("Pan"), fieldW);
      FieldWell::Slider("##slicePan", &e.pan, -1.0f, 1.0f, "%+.2f");
      t.Item();
      {
         bool m = e.mute, so = e.solo;
         SectionCard::RowLabel(T("Play"));
         if (AudioToggleButton(L("Mute##sliceMute"), &m, fieldW / 3.0f - 3.0f, 0.0f))
            t.Changed(true);
         ImGui::SameLine(0.0f, 3.0f);
         if (AudioToggleButton(L("Solo##sliceSolo"), &so, fieldW / 3.0f - 3.0f, 0.0f))
            t.Changed(true);
         ImGui::SameLine(0.0f, 3.0f);
         bool rv = e.reverse;
         if (AudioToggleButton(L("Rev##sliceRev"), &rv, fieldW / 3.0f, 0.0f))
            t.Changed(true);
         e.mute = m;
         e.solo = so;
         e.reverse = rv;
      }

      SectionCard::Begin(T("Pitch"));
      Row(T("Pitch"), fieldW);
      if (FieldWell::Slider("##slicePitch", &e.pitchSt, -48.0f, 48.0f, "%+.0f st"))
         e.pitchSt = std::round(e.pitchSt);
      t.Item();
      Row(T("Fine"), fieldW);
      FieldWell::Slider("##sliceFine", &e.fineCt, -100.0f, 100.0f, "%+.0f ct");
      t.Item();

      SectionCard::Begin(T("Time"));
      {
         static const std::vector<const char*> modes = { "off", "repitch", "beats", "tonal" };
         t.Changed(ComboRow(T("Stretch"), "##sliceStretch", e.stretchMode, modes, fieldW));
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("off: pitch changes the length; repitch: length changes the pitch; beats and tonal: length and pitch apart"));
      }
      Row(T("Length"), fieldW);
      FieldWell::Slider("##sliceLen", &e.lengthRatio, 0.25f, 4.0f, "%.2fx", ImGuiSliderFlags_Logarithmic);
      t.Item();
      SectionCard::RowLabel(T("Grid"));
      {
         bool fit = e.fitToGrid;
         if (FormParts::Checkbox(T("Fit to 16ths"), &fit))
            t.Changed(true);
         e.fitToGrid = fit;
      }
      {
         static const std::vector<const char*> loops = { "off", "forward", "ping-pong" };
         ImGui::BeginDisabled(e.stretchMode != 0 || e.lengthRatio <= 1.0f);
         t.Changed(ComboRow(T("Fill"), "##sliceLoop", e.loopMode, loops, fieldW));
         ImGui::EndDisabled();
      }

      SectionCard::Begin(T("Shape"));
      Row(T("Transient"), fieldW);
      FieldWell::Slider("##sliceTrans", &e.transient, -1.0f, 1.0f, "%+.2f");
      t.Item();
      Row(T("Decay"), fieldW);
      FieldWell::Slider("##sliceDecay", &e.decay, -1.0f, 1.0f, "%+.2f");
      t.Item();

      SectionCard::Begin(T("Fade"));
      Row(T("In"), fieldW);
      FieldWell::Slider("##sliceFadeIn", &e.fadeInMs, 0.0f, 500.0f, "%.0f ms");
      t.Item();
      Row(T("Out"), fieldW);
      FieldWell::Slider("##sliceFadeOut", &e.fadeOutMs, 0.0f, 500.0f, "%.0f ms");
      t.Item();
      Row(T("Curve"), fieldW);
      FieldWell::Slider("##sliceFadeCurve", &e.fadeCurve, -1.0f, 1.0f, "%+.2f");
      t.Item();

      SectionCard::Begin(T("Compress"));
      Row(T("Amount"), fieldW);
      FieldWell::Slider("##sliceCompAmt", &e.compAmount, 0.0f, 1.0f, "%.2f");
      t.Item();
      ImGui::BeginDisabled(e.compAmount <= 0.0f);
      Row(T("Threshold"), fieldW);
      FieldWell::Slider("##sliceCompTh", &e.compThreshDb, -60.0f, 0.0f, "%.1f dB");
      t.Item();
      Row(T("Ratio"), fieldW);
      FieldWell::Slider("##sliceCompRatio", &e.compRatio, 1.0f, 20.0f, "%.1f:1", ImGuiSliderFlags_Logarithmic);
      t.Item();
      Row(T("Attack"), fieldW);
      FieldWell::Slider("##sliceCompAtk", &e.compAttackMs, 0.1f, 100.0f, "%.1f ms", ImGuiSliderFlags_Logarithmic);
      t.Item();
      Row(T("Release"), fieldW);
      FieldWell::Slider("##sliceCompRel", &e.compReleaseMs, 5.0f, 1000.0f, "%.0f ms", ImGuiSliderFlags_Logarithmic);
      t.Item();
      Row(T("Makeup"), fieldW);
      FieldWell::Slider("##sliceCompMk", &e.compMakeupDb, -24.0f, 24.0f, "%+.1f dB");
      t.Item();
      ImGui::EndDisabled();

      SectionCard::Begin(T("Filter"));
      t.Changed(ComboRow(T("Mode"), "##sliceFilter", e.filterMode, SliceFilterNames(), fieldW));
      ImGui::BeginDisabled(e.filterMode == 0);
      Row(T("Cutoff"), fieldW);
      FieldWell::Slider("##sliceCutoff", &e.cutoffHz, 20.0f, 20000.0f, "%.0f Hz", ImGuiSliderFlags_Logarithmic);
      t.Item();
      Row(T("Res"), fieldW);
      FieldWell::Slider("##sliceRes", &e.res, 0.0f, 1.0f, "%.2f");
      t.Item();
      ImGui::EndDisabled();
      {
         static const std::vector<const char*> groups = { "none", "1", "2", "3", "4", "5", "6", "7", "8" };
         SectionCard::Begin(T("Choke"));
         t.Changed(ComboRow(T("Group"), "##sliceChoke", e.choke, groups, fieldW));
      }

      if (t.activated)
         PushUndo(snap);
      if (t.changed || e != before)
      {
         for (int i : SelectedIndices())
            CopyChangedFields(before, e, s.doc.slices[(size_t)i].p);
         s.dirty = true;
      }
   }

   void DrawSampleInspector(float fieldW)
   {
      Studio& s = S();
      SampleWide& w = s.doc.wide;
      const SampleWide before = w;
      const Snapshot snap = Capture();
      EditTrack t;

      SectionCard::Begin(T("Pitch"));
      Row(T("Pitch"), fieldW);
      if (FieldWell::Slider("##studiopitch", &w.pitch, -24.0f, 24.0f, "%+.0f st"))
         w.pitch = std::round(w.pitch);
      t.Item();
      Row(T("Fine"), fieldW);
      FieldWell::Slider("##studiofine", &w.fine, -100.0f, 100.0f, "%+.0f ct");
      t.Item();

      SectionCard::Begin(T("Speed"));
      Row(T("Speed"), fieldW);
      FieldWell::Slider("##studiospeed", &w.speed, 0.25f, 4.0f, "%.2fx", ImGuiSliderFlags_Logarithmic);
      t.Item();

      SectionCard::Begin(T("Fade"));
      Row(T("In"), fieldW);
      FieldWell::Slider("##studiofadein", &w.fadeInMs, 0.0f, 1000.0f, "%.0f ms");
      t.Item();
      Row(T("Out"), fieldW);
      FieldWell::Slider("##studiofadeout", &w.fadeOutMs, 0.0f, 1000.0f, "%.0f ms");
      t.Item();

      SectionCard::Begin(T("Level"));
      Row(T("Gain"), fieldW);
      FieldWell::Slider("##studiogain", &w.gainDb, -48.0f, 24.0f, "%+.1f dB");
      t.Item();
      SectionCard::RowLabel("");
      if (ActionButton::Draw(L("Normalise##studioNorm"), ImVec2(fieldW, 0.0f)))
         Normalise();
      if (ImGui::IsItemHovered())
         HelpTip("%s", T("Set the gain so the edit peaks at -0.3 dB"));

      SectionCard::Begin(T("Tempo"));
      Row(T("Sample"), fieldW);
      FieldWell::Slider("##studiobpm", &s.doc.sampleBpm, 0.0f, 300.0f, s.doc.sampleBpm > 0.0f ? "%.1f bpm" : "unknown");
      t.Item();
      if (ImGui::IsItemHovered())
         HelpTip("%s", T("The sample's own tempo: the grid, Fit to 16ths and the beats ruler use it"));

      if (t.activated)
         PushUndo(snap);
      if (t.changed || !(w == before))
         s.dirty = true;

      // What the next Generate makes.
      SectionCard::Begin(T("Generate"));
      {
         static std::vector<std::string> styles;
         if (styles.empty())
         {
            styles.push_back(T("any"));
            for (int i = 0; i < SampleStyles::Count(); i++)
               styles.push_back(SampleStyles::At(i).name);
         }
         SectionCard::RowLabel(T("Style"));
         ImGui::SetNextItemWidth(fieldW);
         if (MenuParts::BeginCombo("##studiostyle", styles[(size_t)(s.style + 1)].c_str()))
         {
            for (int i = 0; i < (int)styles.size(); i++)
               if (MenuParts::Choice(styles[(size_t)i].c_str(), i == s.style + 1))
                  s.style = i - 1;
            MenuParts::EndCombo();
         }
         Row(T("Wild"), fieldW);
         FieldWell::Slider("##studiowild", &s.variation, 0.0f, 1.0f, "%.2f");
      }

      // The project's key and scale, which "snap to scale" follows (the same values as the top bar's key).
      SectionCard::Begin(T("Scale"));
      {
         Transport& tr = Transport::Instance();
         static const char* const keys[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
         const std::vector<std::string>& scales = MusicTime::ScaleTypeList();
         ImGui::BeginDisabled(!s.followScale);
         Row(T("Key"), fieldW);
         if (MenuParts::BeginCombo("##studiokey", keys[std::clamp(tr.Key(), 0, 11)]))
         {
            for (int i = 0; i < 12; i++)
               if (MenuParts::Choice(keys[i], i == tr.Key()))
                  tr.SetKey(i);
            MenuParts::EndCombo();
         }
         Row(T("Scale"), fieldW);
         const int sc = std::clamp(tr.Scale(), 0, (int)scales.size() - 1);
         if (MenuParts::BeginCombo("##studioscale", scales[(size_t)sc].c_str()))
         {
            for (int i = 0; i < (int)scales.size(); i++)
               if (MenuParts::Choice(scales[(size_t)i].c_str(), i == sc))
                  tr.SetScale(i);
            MenuParts::EndCombo();
         }
         ImGui::EndDisabled();
      }
   }

   void DrawSettings(float panelW)
   {
      Studio& s = S();
      PushDockedPanelStyle(/*isChild=*/true);
      const ImVec2 savedPad = ImGui::GetStyle().WindowPadding;
      ImGui::GetStyle().WindowPadding = ImVec2(tok::space_3, tok::space_3);
      ImGui::BeginChild("##studiosettings_child", ImVec2(panelW, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, 0);
      ImGui::GetStyle().WindowPadding = savedPad;
      PopDockedPanelStyle();
      SectionCard::BeginWindow();
      const float availW = ImGui::GetContentRegionAvail().x;
      const float fieldW = std::max(70.0f, availW - SectionCard::kLabelW);

      PruneSelection();
      char title[64];
      if (s.sel.empty())
         snprintf(title, sizeof(title), "%s", T("Sample"));
      else if (s.sel.size() == 1)
         snprintf(title, sizeof(title), "%s %s", T("Slice"), SliceLabel(s.doc, s.doc.IndexOfId(s.primary)).c_str());
      else
         snprintf(title, sizeof(title), "%d %s", (int)s.sel.size(), T("slices"));
      SectionCard::Title(title);
      FormParts::Inline(availW - 16.0f + ImGui::GetStyle().WindowPadding.x);
      {
         const float sz = 16.0f;
         if (ImGui::InvisibleButton("##closestudiosettings", ImVec2(sz, sz)))
            s.settingsOpen = false;
         const ImVec2 bmin = ImGui::GetItemRectMin(), bmax = ImGui::GetItemRectMax();
         glyph::DrawX(ImGui::GetWindowDrawList(), ImVec2((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f), sz * 0.65f,
                      ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGuiCol_Text : ImGuiCol_TextDisabled));
      }
      if (!s.sel.empty())
      {
         if (ActionButton::Draw(L("Whole sample##studioDeselect"), ImVec2(availW, 0.0f)))
            s.sel.clear();
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("Edit the whole sample instead of the selected slices"));
      }
      if (s.sel.empty())
         DrawSampleInspector(fieldW);
      else
         DrawSliceInspector(fieldW);

      SectionCard::EndWindow();
      ImGui::EndChild();
   }

   // ---- the toolbar ----
   // One height (tok::tile) for every item, one gap inside a group (space_1) and one between groups (space_3).

   void PushToolbarChip(bool on, bool green = false)
   {
      ActionButton::Scoped() = !on ? ActionButton::Kind::Plain : green ? ActionButton::Kind::Go : ActionButton::Kind::Selected;
   }
   void PopToolbarChip() { ActionButton::Scoped() = ActionButton::Kind::Plain; }

   constexpr float kIconW = 30.0f;

   float TextW(const char* label) { return ImGui::CalcTextSize(label, ImGui::FindRenderedTextEnd(label)).x + 2.0f * tok::space_3; }

   // An icon chip in the Timeline's toolbar grammar. Returns true when clicked.
   bool IconChip(const char* id, const char* glyphStr, bool on, const char* tip, bool green = false)
   {
      PushToolbarChip(on, green);
      const bool clicked = ActionButton::Draw(id, ImVec2(kIconW, tok::tile));
      PopToolbarChip();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         HelpTip("%s", tip);
      const ImVec2 bmin = ImGui::GetItemRectMin(), bmax = ImGui::GetItemRectMax();
      const ImU32 icol = on ? tok::U32(tok::pal::c_FFFFFFFF) : ImGui::GetColorU32(ImGuiCol_Text);
      glyph::Draw(ImGui::GetWindowDrawList(), ImVec2((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f), tok::icon_md, icol, glyphStr);
      return clicked;
   }

   bool TextChip(const char* label, ActionButton::Kind kind = ActionButton::Kind::Plain)
   {
      return ActionButton::Draw(label, ImVec2(TextW(label), tok::tile), kind);
   }

   // A dropdown chip: the value on the left, a chevron on the right, the list below. The list fades and settles in
   // over one motion step and scrolls past nine rows. Returns true when the choice changed.
   bool PickerChip(const char* id, const std::vector<std::string>& items, int& cur, float w, const char* tip)
   {
      static ImGuiID sOpenId = 0;
      static double sOpenedAt = -1.0;
      const ImGuiID pid = ImGui::GetID(id);
      if (ActionButton::Draw(id, ImVec2(w, tok::tile)))
      {
         ImGui::OpenPopup(id);
         sOpenId = pid;
         sOpenedAt = ImGui::GetTime();
      }
      if (ImGui::IsItemHovered())
         HelpTip("%s", tip);
      const ImVec2 bmin = ImGui::GetItemRectMin(), bmax = ImGui::GetItemRectMax();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImU32 tc = ImGui::GetColorU32(ImGuiCol_Text);
      const float cy = (bmin.y + bmax.y) * 0.5f;
      const std::string& value = items[(size_t)std::clamp(cur, 0, (int)items.size() - 1)];
      dl->AddText(ImVec2(bmin.x + tok::space_3, cy - ImGui::GetFontSize() * 0.5f), tc, value.c_str());
      glyph::Draw(dl, ImVec2(bmax.x - tok::space_3 - tok::icon_sm * 0.5f, cy), tok::icon_sm, ImGui::GetColorU32(ImGuiCol_TextDisabled),
                  IconsInfinite::ChevronDown);

      const float k = sOpenId == pid ? std::clamp((float)((ImGui::GetTime() - sOpenedAt) * 1000.0 / tok::motion_on), 0.0f, 1.0f) : 1.0f;
      const float e = 1.0f - (1.0f - k) * (1.0f - k) * (1.0f - k);
      const float rowH = ImGui::GetFrameHeight();
      ImGui::SetNextWindowPos(ImVec2(bmin.x, bmax.y + tok::space_1 + (1.0f - e) * tok::space_2));
      ImGui::SetNextWindowSizeConstraints(ImVec2(std::max(160.0f, w), 0.0f), ImVec2(280.0f, rowH * 9.5f + tok::space_3 * 2.0f));
      const float savedAlpha = ImGui::GetStyle().Alpha;
      ImGui::GetStyle().Alpha = savedAlpha * e;
      bool changed = false;
      if (MenuParts::BeginPopup(id))
      {
         for (int i = 0; i < (int)items.size(); i++)
            if (MenuParts::Choice(items[(size_t)i].c_str(), i == cur) && i != cur)
            {
               cur = i;
               changed = true;
            }
         MenuParts::EndPopup();
      }
      ImGui::GetStyle().Alpha = savedAlpha;
      return changed;
   }

   void DrawSliceMenu()
   {
      Studio& s = S();
      if (!MenuParts::BeginPopup("##studioSliceMenu"))
         return;
      if (MenuParts::SubMenu(T("By transients"), !s.onsetJob))
      {
         const struct { const char* label; float sens; } kinds[] = { { "Fewer", 40.0f }, { "Normal", 65.0f }, { "More", 85.0f } };
         for (const auto& k : kinds)
            if (MenuParts::Item(T(k.label)))
               SliceByTransients(k.sens);
         MenuParts::Separator();
         for (int m : { 8, 16, 32, 64 })
         {
            char lab[32];
            snprintf(lab, sizeof(lab), "%s %d", T("At most"), m);
            if (MenuParts::Choice(lab, s.onsetMax == m))
               s.onsetMax = m;
         }
         ImGui::EndMenu();
      }
      if (MenuParts::SubMenu(T("By grid")))
      {
         const double fpb = FramesPerBeat();
         if (MenuParts::Item(T("1 bar")))
            SliceByGrid(fpb * 4.0);
         if (MenuParts::Item(T("1/4")))
            SliceByGrid(fpb);
         if (MenuParts::Item(T("1/8")))
            SliceByGrid(fpb / 2.0);
         if (MenuParts::Item(T("1/16")))
            SliceByGrid(fpb / 4.0);
         ImGui::EndMenu();
      }
      if (MenuParts::SubMenu(T("Equal")))
      {
         for (int n : { 2, 4, 8, 16, 32 })
            if (MenuParts::Item(std::to_string(n).c_str()))
               SliceEqual(n);
         ImGui::EndMenu();
      }
      MenuParts::Separator();
      if (MenuParts::Item(T("Clear slices"), nullptr, false, s.doc.Count() > 1))
         ClearSlices();
      MenuParts::EndPopup();
   }

   void DrawToolbar(float w)
   {
      Studio& s = S();
      const bool has = s.src != nullptr;
      const float x0 = ImGui::GetCursorScreenPos().x;
      const float in = tok::space_1, between = tok::space_3;

      // Make: open a file, generate (again), instrument, snap to scale, bars.
      if (IconChip("##studioOpen", IconsInfinite::Folder, false, T("Open an audio file (or drop one on the Studio)")))
      {
         const std::string p = Platform::OpenAudioDialog();
         if (!p.empty())
            LoadFile(p);
      }
      FormParts::Inline(0.0f, in);
      if (TextChip(L("Generate##studioGen"), s.busy ? ActionButton::Kind::Go : ActionButton::Kind::Primary))
      {
         if (s.busy)
            s.gen->Cancel();
         Render(false);
      }
      if (ImGui::IsItemHovered())
         HelpTip("%s", s.busy ? T("Making a take: click to start over") : T("Generate a new take"));
      FormParts::Inline(0.0f, in);
      {
         static std::vector<std::string> parts;
         if (parts.empty())
            for (P p : kParts)
               parts.push_back(SampleStyles::PartName(p));
         if (PickerChip("##studioPart", parts, s.part, 128.0f, T("Instrument")) && s.take && !s.busy)
            Render(false);
      }
      FormParts::Inline(0.0f, in);
      if (IconChip("##studioScale", s.followScale ? IconsInfinite::NoteSnapFill : IconsInfinite::NoteSnap, s.followScale,
                   s.followScale ? T("Snapped to the project's key and scale") : T("Snap to the project's key and scale")))
      {
         s.followScale = !s.followScale;
         if (s.take && !s.busy)
            Render(true);
      }
      FormParts::Inline(0.0f, in);
      {
         static std::vector<std::string> bars;
         if (bars.empty())
            for (const char* b : { "1 bar", "2 bars", "4 bars", "8 bars" })
               bars.push_back(T(b));
         if (PickerChip("##studioBars", bars, s.barsIdx, 92.0f, T("Length")) && s.take && !s.busy)
            Render(false);
      }

      // Hear: play on the transport (hands audio to the studio, pausing the canvas), then the Sampler's three toggles.
      FormParts::Inline(0.0f, between);
      const bool playing = Transport::Instance().IsPlaying() && gAudioMode == AudioMode::Studio;
      ImGui::BeginDisabled(!has);
      if (IconChip("##studioPlay", playing ? IconsInfinite::PauseFill : IconsInfinite::PlayFill, playing,
                   playing ? T("Stop") : T("Play the edit (pauses the canvas audio)"), true))
         TogglePlay();
      ImGui::EndDisabled();
      const auto toggle = [&](const char* label, bool& v, const char* tip)
      {
         FormParts::Inline(0.0f, in);
         const bool before = v;
         if (AudioToggleButton(label, &v, TextW(label), tok::tile))
         {
            const bool after = v;
            v = before;
            PushUndo();
            v = after;
            s.dirty = true;
            AudioEngine::Instance().StudioLoop().SetLoop(s.doc.wide.loop);
            AudioEngine::Instance().StudioLoop().Restart();
         }
         if (ImGui::IsItemHovered())
            HelpTip("%s", tip);
      };
      toggle(L("Loop##studioLoop"), s.doc.wide.loop, T("Loop the edit; off plays it once"));
      toggle(L("Ping-pong##studioPP"), s.doc.wide.pingPong, T("While looping, play forward then back"));
      toggle(L("Reverse##studioRev"), s.doc.wide.reverse, T("Play the whole edit backwards"));

      // Slice: the auto-slicers, split, grid snap, zoom.
      FormParts::Inline(0.0f, between);
      ImGui::BeginDisabled(!has);
      if (TextChip(L("Slice##studioSlice")))
         ImGui::OpenPopup("##studioSliceMenu");
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         HelpTip("%s", T("Cut the sample by transients, grid or into equal parts"));
      FormParts::Inline(0.0f, in);
      if (IconChip("##studioSplit", IconsInfinite::Scissors, false, T("Split the selected slice in half (Cmd+E splits at the pointer)")))
      {
         const int i = s.sel.empty() ? 0 : s.doc.IndexOfId(s.primary);
         if (i >= 0 && i < s.doc.Count())
            Split((s.doc.slices[(size_t)i].start + s.doc.EndOf(i)) / 2);
      }
      FormParts::Inline(0.0f, in);
      if (IconChip("##studioSnap", IconsInfinite::Snap, s.snapGrid, s.snapGrid ? T("Markers snap to 16ths (Shift while dragging: free)") : T("Snap markers to 16ths")))
         s.snapGrid = !s.snapGrid;
      FormParts::Inline(0.0f, in);
      if (IconChip("##studioZoom", IconsInfinite::Zoom, false, T("Zoom to the selection (Z); Cmd+wheel zooms, Alt+wheel the level")))
         ZoomToSelection();
      ImGui::EndDisabled();
      DrawSliceMenu();
      DrawReplaceConfirm();

      // Out, pinned right: export, convert to patch, convert to node, settings.
      const char* exportL = L("Export##studioExport");
      const char* patchL = L("Convert to patch##studioPatch");
      const char* nodeL = L("Convert to node##studioNode");
      const float rightW = TextW(exportL) + TextW(patchL) + TextW(nodeL) + kIconW + 3.0f * in;
      FormParts::Inline(0.0f, 0.0f);
      ImGui::SetCursorScreenPos(ImVec2(std::max(ImGui::GetCursorScreenPos().x + between, x0 + w - rightW), ImGui::GetCursorScreenPos().y));
      ImGui::BeginDisabled(!has || s.busy);
      if (TextChip(exportL))
         ImGui::OpenPopup("##studioExportMenu");
      FormParts::Inline(0.0f, in);
      ImGui::BeginDisabled(!s.take);
      if (TextChip(patchL))
         ConvertToPatch();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         HelpTip("%s", s.take ? T("The graph that made the take, as a group on the canvas") : T("Only for a generated take"));
      ImGui::EndDisabled();
      FormParts::Inline(0.0f, in);
      if (TextChip(nodeL))
         ImGui::OpenPopup("##studioNodeMenu");
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         HelpTip("%s", T("A sample node holding the edit"));
      ImGui::EndDisabled();
      FormParts::Inline(0.0f, in);
      if (IconChip("##studioSettings", IconsInfinite::Settings, s.settingsOpen, T("Sample and slice settings")))
         s.settingsOpen = !s.settingsOpen;

      if (MenuParts::BeginPopup("##studioExportMenu"))
      {
         if (MenuParts::Item(T("WAV, 24-bit")))
            Export("wav", AudioFileWriter::Format::Wav, 24);
         if (MenuParts::Item(T("WAV, 16-bit")))
            Export("wav", AudioFileWriter::Format::Wav, 16);
         if (MenuParts::Item(T("FLAC")))
            Export("flac", AudioFileWriter::Format::Flac, 24);
         if (MenuParts::Item(T("MP3")))
            Export("mp3", AudioFileWriter::Format::Mp3, 16);
         MenuParts::Separator();
         if (MenuParts::Item(T("WAV, choose where...")))
         {
            const std::string path = Platform::SaveWavDialog(SourceName() + ".wav");
            if (!path.empty())
               WriteEdit(path, "wav", AudioFileWriter::Format::Wav, 24);
         }
         if (MenuParts::Item(T("Each slice to a folder..."), nullptr, false, s.doc.Count() > 1))
            ExportSlices();
         MenuParts::EndPopup();
      }
      if (MenuParts::BeginPopup("##studioNodeMenu"))
      {
         static const char* const types[] = { "Generate Samples", "Sampler", "Slicer", "Molder", "Grain Molder", "PaulStretch", "Granular" };
         for (const char* t : types)
         {
            const bool gen = t == types[0];
            if (MenuParts::Item(T(t), nullptr, false, !gen || s.take.has_value()))
               ConvertToNode(t);
            if (gen)
               MenuParts::Separator();
         }
         MenuParts::Separator();
         if (MenuParts::Item(T("One Sampler per slice"), nullptr, false, s.doc.Count() > 1))
            SlicesToSamplers();
         MenuParts::EndPopup();
      }
   }

   // A file dropped anywhere on the Studio opens in it (the canvas leaves drops inside this rect alone).
   void TakeDrops()
   {
      if (gDroppedFiles.empty() || gDropPos.x < gSampleStudioRectMin.x || gDropPos.x >= gSampleStudioRectMax.x ||
          gDropPos.y < gSampleStudioRectMin.y || gDropPos.y >= gSampleStudioRectMax.y)
         return;
      for (const std::string& p : gDroppedFiles)
         if (IsAudioPath(p))
         {
            LoadFile(p);
            break;
         }
      gDroppedFiles.clear();
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
         s.settingsOpen = std::strstr(getenv("INFINITE_OPENPANELS"), "studiosettings") != nullptr;
         Render(false);
      }
      PollEngine();
      // INFINITE_OPENPANELS=...,studioslices: cut the first take into 8 and dress two slices, to review the editor.
      static bool sDemoSlices = [] { const char* op = getenv("INFINITE_OPENPANELS"); return op != nullptr && std::strstr(op, "studioslices") != nullptr; }();
      if (sDemoSlices && s.src && !s.busy)
      {
         sDemoSlices = false;
         s.doc.FromEqual(8, s.doc.StartFrame(), s.doc.endFrame);
         s.doc.slices[2].p.reverse = true;
         s.doc.slices[2].colour = 7;
         s.doc.slices[5].p.mute = true;
         SelectOnly(3);
         s.dirty = true;
      }
      // INFINITE_OPENPANELS=...,studiopatch: convert the first take to a patch, to review the layout.
      static bool sDemoPatch = [] { const char* op = getenv("INFINITE_OPENPANELS"); return op != nullptr && std::strstr(op, "studiopatch") != nullptr; }();
      if (sDemoPatch && s.take)
      {
         sDemoPatch = false;
         ConvertToPatch();
      }
      TakeDrops();
      PollOnsets();
      PumpRender();
      StudioLoopPlayer& player = AudioEngine::Instance().StudioLoop();
      player.SetGain(gAudioMode == AudioMode::Studio ? 0.8f : 0.0f);
      // Loop off: the edit played once, so the transport stops (as a one-shot does in the Sampler).
      if (!s.doc.wide.loop && player.Finished() && gAudioMode == AudioMode::Studio && Transport::Instance().IsPlaying())
      {
         player.Restart();
         LaunchRequestTogglePlay();
      }

      const float w = std::max(1.0f, ImGui::GetContentRegionAvail().x);
      DrawToolbar(w);
      // Right-click the toolbar's empty space: dock top or bottom.
      const ImVec2 tbMin(ImGui::GetWindowPos().x, ImGui::GetItemRectMin().y), tbMax(tbMin.x + ImGui::GetWindowSize().x, ImGui::GetItemRectMax().y);
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && ImGui::IsMouseHoveringRect(tbMin, tbMax) && !ImGui::IsAnyItemHovered() &&
          ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
         ImGui::OpenPopup("##studioDockMenu");
      if (MenuParts::BeginPopup("##studioDockMenu"))
      {
         if (MenuParts::Choice(T("Dock bottom"), gSampleStudioDock != 3))
            gSampleStudioDock = 0;
         if (MenuParts::Choice(T("Dock top"), gSampleStudioDock == 3))
            gSampleStudioDock = 3;
         MenuParts::EndPopup();
      }
      if (!s.status.empty())
      {
         ImGui::TextDisabled("%s", s.status.c_str());
         if (ImGui::IsItemClicked())
            s.status.clear();
      }

      ImGui::Dummy(ImVec2(0.0f, tok::space_1));
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 avail = ImGui::GetContentRegionAvail();
      const float h = std::max(40.0f, avail.y);
      const float gap = ImGui::GetStyle().ItemSpacing.x;
      const float settingsW = s.settingsOpen ? kSettingsW + gap : 0.0f;
      DrawWave(origin, ImVec2(std::max(40.0f, avail.x - settingsW), h));
      if (s.settingsOpen)
      {
         ImGui::SetCursorScreenPos(ImVec2(origin.x + avail.x - kSettingsW, origin.y));
         DrawSettings(kSettingsW);
      }
   }

   // ---- save and load ----

   std::string Esc(const std::string& v)
   {
      std::string o;
      for (char c : v)
      {
         if (c == ' ' || c == '%' || c == '=' || c == '\n' || c == '\r' || c == '\t')
         {
            char b[4];
            snprintf(b, sizeof(b), "%%%02X", (unsigned char)c);
            o += b;
         }
         else
            o += c;
      }
      return o;
   }

   std::string Unesc(const std::string& v)
   {
      std::string o;
      for (size_t i = 0; i < v.size(); i++)
      {
         if (v[i] == '%' && i + 2 < v.size())
         {
            o += (char)strtol(v.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
         }
         else
            o += v[i];
      }
      return o;
   }
}

// `studio 1`, then `src path=<file>` or `gen <recipe>`, then the SliceSet's own lines. Empty when nothing is open.
std::string SampleStudioSerialize()
{
   const Studio& s = S();
   if (!s.src || (s.path.empty() && !s.take))
      return std::string();
   std::string out = "studio 1\n";
   if (s.take)
   {
      const SampleStyles::Recipe& r = s.take->recipe;
      char b[512];
      snprintf(b, sizeof(b),
               "gen style=%s seed=%u var=%.9g bars=%d bpm=%.9g key=%d scale=%d part=%d personal=%d tone=%.9g energy=%.9g space=%.9g mood=%.9g card=%d follow=%d\n",
               Esc(r.style).c_str(), (unsigned)r.seed, (double)r.variation, r.bars, (double)r.bpm, r.key, r.scale, (int)r.part,
               r.personal ? 1 : 0, (double)r.tone, (double)r.energy, (double)r.space, (double)r.mood, s.take->card, s.followScale ? 1 : 0);
      out += b;
   }
   else
      out += "src path=" + Esc(s.path) + "\n";
   out += s.doc.Serialize();
   return out;
}

void SampleStudioApply(const std::string& text)
{
   Studio& s = S();
   if (s.job)
      s.job->cancel.store(true);
   if (s.onsetJob)
      s.onsetJob->abort.store(true);
   s.onsetJob.reset();
   s.undo.clear();
   s.redo.clear();
   s.sel.clear();
   s.pendingDoc.reset();
   s.status.clear();
   if (text.empty())
   {
      s.src.reset();
      s.path.clear();
      s.take.reset();
      s.doc = SliceSet();
      s.heard.reset();
      s.dirty = true;
      return;
   }
   std::istringstream in(text);
   std::string line, rest;
   std::getline(in, line);
   if (line.rfind("studio ", 0) != 0)
      return;
   std::string source;
   std::getline(in, source);
   for (std::string l; std::getline(in, l);)
      rest += l + "\n";
   SliceSet doc;
   if (!SliceSet::Parse(rest, doc))
      return;
   std::map<std::string, std::string> kv;
   std::istringstream src(source);
   std::string tag, tok;
   src >> tag;
   while (src >> tok)
      if (const size_t eq = tok.find('='); eq != std::string::npos)
         kv[tok.substr(0, eq)] = Unesc(tok.substr(eq + 1));
   const auto num = [&](const char* k, double d) { const auto it = kv.find(k); return it == kv.end() ? d : atof(it->second.c_str()); };
   if (tag == "src")
   {
      const std::string path = kv["path"];
      if (!LoadFile(path))
         return;
      s.undo.clear();
      s.doc = doc;
      s.doc.endFrame = std::min(s.doc.endFrame, Frames());
      ViewAll();
      s.dirty = true;
   }
   else if (tag == "gen")
   {
      TakeInfo t;
      t.recipe.style = kv.count("style") ? kv["style"] : t.recipe.style;
      t.recipe.seed = (uint32_t)num("seed", 1);
      t.recipe.variation = (float)num("var", 0.5);
      t.recipe.bars = (int)num("bars", 2);
      t.recipe.bpm = (float)num("bpm", 0);
      t.recipe.key = (int)num("key", -1);
      t.recipe.scale = (int)num("scale", -1);
      t.recipe.part = (SampleStyles::Part)(int)num("part", 0);
      t.recipe.personal = num("personal", 0) != 0.0;
      t.recipe.tone = (float)num("tone", 0);
      t.recipe.energy = (float)num("energy", 0);
      t.recipe.space = (float)num("space", 0);
      t.recipe.mood = (float)num("mood", 0);
      t.card = (int)num("card", 0);
      s.followScale = num("follow", 0) != 0.0;
      // The take is made again from its recipe; the edit goes on once it lands (PollEngine).
      s.src.reset();
      s.heard.reset();
      s.take = t;
      s.pendingDoc = doc;
      if (s.busy)
         s.gen->Cancel();
      Render(true);
   }
}

void DrawSampleStudioDocked(const char* id, const ImVec2& size)
{
   const float kGrip = PanelFrame::kGap;
   const bool gripFirst = gSampleStudioDock != 3; // bottom: the grip sits on top
   ImGui::BeginChild(id, size, false);
   gSampleStudioRectMin = ImGui::GetWindowPos();
   gSampleStudioRectMax = ImVec2(gSampleStudioRectMin.x + ImGui::GetWindowSize().x, gSampleStudioRectMin.y + ImGui::GetWindowSize().y);
   gSampleStudioClaimedKeys = false;
   const ImVec2 inner = ImGui::GetContentRegionAvail();
   auto grip = [&]()
   {
      ImGui::InvisibleButton("##studiogrip", ImVec2(std::max(1.0f, inner.x), kGrip));
      DrawPanelSeam(false, gripFirst);
      if (ImGui::IsItemHovered() || ImGui::IsItemActive())
         ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
      if (ImGui::IsItemActive())
      {
         gSampleStudioHeight += gripFirst ? -ImGui::GetIO().MouseDelta.y : ImGui::GetIO().MouseDelta.y;
         gSampleStudioHeight = std::max(kMinHeight, gSampleStudioHeight);
      }
   };
   if (gripFirst)
      grip();
   const ImVec2 gap = ImGui::GetStyle().ItemSpacing;
   const ImVec2 cardSize(std::max(1.0f, inner.x), std::max(1.0f, inner.y - kGrip - gap.y));
   PanelFrame::Insets in;
   if (gripFirst)
      in.t = 0.0f;
   else
      in.b = 0.0f;
   PanelFrame::BeginCard("##studiopanelcontent", cardSize, in);
   DrawStudio();
   PanelFrame::EndCard();
   if (!gripFirst)
      grip();
   const ImVec2 savedSpacing = ImGui::GetStyle().ItemSpacing;
   ImGui::GetStyle().ItemSpacing = ImVec2(0.0f, 0.0f);
   ImGui::EndChild();
   ImGui::GetStyle().ItemSpacing = savedSpacing;
}
}
