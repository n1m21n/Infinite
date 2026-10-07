// Arrange panel support: undo, selection, clipboard, drag, add-to-timeline (moved verbatim from main.cpp).
#include "app/AppShared.h"

namespace app
{
   // ---- Arrangement Timeline ----
   bool IsNodeVideoCompatible(const GraphNode& gn)
   {
      if (gn.node == nullptr) return false;
      // OutputCount() > 0 used to stand in for "produces an image", but its
      // default is 1 for every INode regardless of what that output actually
      // is - an audio node with no image output at all (Oscillator, Reverb -
      // AudioEffectNode never overrides it) passed this check same as a real
      // image source, the same shape of bug item 3 reported for audio.
      // CanShowInViewportPanel already carries the real "does this node emit
      // pixels" gate (it excludes audio/note sources, modulators, analyzers,
      // camera/light, comment/group), kept in sync deliberately whenever a
      // new audio node lands - reuse it here instead of duplicating it.
      return dynamic_cast<IGeometrySource*>(gn.node.get()) != nullptr ||
             dynamic_cast<IPaletteSource*>(gn.node.get()) != nullptr ||
             CanShowInViewportPanel(gn);
   }


   bool IsNodeAudioCompatible(const GraphNode& gn)
   {
      if (gn.node == nullptr) return false;
      // Strictly "this node has an audio buffer a clip terminal can read" -
      // IAudioSource, and nothing looser. INoteSource (Note Sequencer, MIDI
      // Notes...) produces note events, not audio, and IsAudioBodyNode is a
      // UI-drawing category (which nodes get the audio-node body panel) that
      // includes those same note-only nodes - either one previously let a
      // clip get "assigned" to a node with no audio to actually play.
      return dynamic_cast<IAudioSource*>(gn.node.get()) != nullptr;
   }


   // Called once per main-loop frame, after that frame's composites. Safe to
   // free immediately: a geometry viewport's texture is only ever sampled by
   // the composite pass, never queued into an ImGui draw list.
   // An entry survives kArrangeGeomEvictFrames consecutive frames without a
   // composite and is dropped on the next one.
   void ReapArrangeGeomViewports()
   {
      for (auto it = gArrangeGeomViewports.begin(); it != gArrangeGeomViewports.end();)
      {
         if (gArrangeGeomFrame - it->second.lastUsedFrame > kArrangeGeomEvictFrames)
            it = gArrangeGeomViewports.erase(it);
         else
            ++it;
      }
      gArrangeGeomFrame++;
   }


   // A clip's waveform identity: the fields that decide whether existing
   // buckets still describe the clip. Not a security hash - a 64-bit FNV-1a
   // mix, because the only thing on the other side of a collision is one
   // stale display bucket that the next playback pass overwrites anyway.
   uint64_t ArrangeClipShape(uint64_t srcUid, int srcOutput, Arrange::Tick start, Arrange::Tick length)
   {
      uint64_t h = 1469598103934665603ull;
      const uint64_t parts[4] = { srcUid, (uint64_t)(int64_t)srcOutput,
                                  (uint64_t)(int64_t)start, (uint64_t)(int64_t)length };
      for (uint64_t v : parts)
         for (int b = 0; b < 8; b++)
         {
            h ^= (v >> (b * 8)) & 0xffull;
            h *= 1099511628211ull;
         }
      return h;
   }


   int ArrangeWaveBucketCount(Arrange::Tick length)
   {
      const long long n = ((long long)std::max<Arrange::Tick>(0, length) + kArrangeWaveBucketTicks - 1) /
                          kArrangeWaveBucketTicks;
      return (int)std::clamp<long long>(n, 0, kArrangeWaveMaxBuckets);
   }


   // The Audio Sample static waveform, computed from the fully decoded
   // source. Bucket b covers ticks [b, b+1) * kArrangeWaveBucketTicks of the
   // box (the draw code's own mapping), and those ticks are converted to
   // source seconds with exactly the rule the audio thread plays by
   // (Arrange::SampleSourceBpm / ClipWindow): offset + TicksToSeconds(t,
   // effBpm). Buckets past the end of the file stay empty rather than the
   // file being stretched to fill the box, so what is drawn is what plays.
   void ArrangeComputeSampleStaticWave(uint64_t clipId, uint64_t srcUid, int srcOutput,
                                        Arrange::Tick start, Arrange::Tick length,
                                        double effBpm, float sourceOffsetSeconds,
                                        const Platform::SampleBuffer* buf)
   {
      const int buckets = ArrangeWaveBucketCount(length);
      if (buckets <= 0 || buf == nullptr || buf->numFrames <= 0 || buf->channels <= 0 ||
          !(buf->sampleRate > 0.0))
      {
         gArrangeSampleStaticWaves.erase(clipId);
         return;
      }

      const long long frames = buf->numFrames;
      const int channels = buf->channels;
      const double framesPerTick = Arrange::TicksToSeconds(1, effBpm) * buf->sampleRate;
      const double frame0 = (double)sourceOffsetSeconds * buf->sampleRate;

      ArrangeClipWave w;
      w.srcUid = srcUid;
      w.srcOutput = srcOutput;
      w.start = start;
      w.length = length;
      w.shape = ArrangeClipShape(srcUid, srcOutput, start, length);
      w.sampleEffBpm = effBpm;
      w.sampleOffset = sourceOffsetSeconds;
      w.sampleBuf = buf;
      w.minv.assign((size_t)buckets, 0.0f);
      w.maxv.assign((size_t)buckets, 0.0f);
      w.filled.assign((size_t)buckets, 1); // static: filled up-front, all at once

      for (int b = 0; b < buckets; ++b)
      {
         const double t0 = (double)b * (double)kArrangeWaveBucketTicks;
         const double t1 = t0 + (double)kArrangeWaveBucketTicks;
         const long long f0 = std::clamp<long long>((long long)std::floor(frame0 + t0 * framesPerTick), 0, frames);
         const long long f1 = std::clamp<long long>(
            std::max<long long>((long long)std::floor(frame0 + t1 * framesPerTick), f0 + 1), 0, frames);
         float mn = 0.0f, mx = 0.0f;
         for (long long f = f0; f < f1; ++f)
         {
            for (int ch = 0; ch < channels; ++ch)
            {
               const float v = buf->channelData[(size_t)ch * (size_t)frames + (size_t)f];
               mn = std::min(mn, v);
               mx = std::max(mx, v);
            }
         }
         w.minv[(size_t)b] = mn;
         w.maxv[(size_t)b] = mx;
      }

      gArrangeSampleStaticWaves[clipId] = std::move(w);
   }


   double ArrangeSampleEffBpm(const Arrange::Clip& c)
   {
      return Arrange::SampleSourceBpm(c.syncToTempo, c.sampleBpm, (double)Transport::Instance().Tempo());
   }


   // Resizes a Sample's box so it keeps covering the same source audio when
   // its effective BPM changes (sync toggle, or a Sample BPM edit while
   // synced): a trim point the user set survives, and only the playback
   // speed changes. Goes through TrimEdge so growing into a neighbour clamps
   // at it instead of breaking the lane's no-overlap invariant.
   void ArrangeRescaleSampleBox(uint64_t clipId, double oldEffBpm, double newEffBpm)
   {
      const Arrange::Clip* c = Arrange::FindClip(gArrange, clipId);
      if (c == nullptr || !(oldEffBpm > 0.0) || !(newEffBpm > 0.0) || oldEffBpm == newEffBpm)
         return;
      const Arrange::Tick newLength =
         std::max<Arrange::Tick>(1, (Arrange::Tick)std::llround((double)c->length * newEffBpm / oldEffBpm));
      Arrange::TrimEdge(gArrange, clipId, Arrange::kEdgeEnd, c->start + newLength);
   }


   void ArrangeSetSampleSync(uint64_t clipId, bool sync)
   {
      Arrange::Clip* c = Arrange::FindClip(gArrange, clipId);
      if (c == nullptr || c->syncToTempo == sync)
         return;
      const double oldEff = ArrangeSampleEffBpm(*c);
      c->syncToTempo = sync;
      const double newEff = ArrangeSampleEffBpm(*c);
      gArrange.revision++;
      ArrangeRescaleSampleBox(clipId, oldEff, newEff);
   }


   void ArrangeSetSampleBpm(uint64_t clipId, float bpm)
   {
      Arrange::Clip* c = Arrange::FindClip(gArrange, clipId);
      if (c == nullptr)
         return;
      bpm = std::clamp(bpm, 20.0f, 999.0f);
      if (bpm == c->sampleBpm)
         return;
      const double oldEff = ArrangeSampleEffBpm(*c);
      c->sampleBpm = bpm;
      const double newEff = ArrangeSampleEffBpm(*c);
      gArrange.revision++;
      ArrangeRescaleSampleBox(clipId, oldEff, newEff);
   }


   // Status line under the Sample BPM field, shared by the context menu and
   // the docked Clip Settings panel.
   void ArrangeDrawSampleTempoInfo(const Arrange::Clip& c)
   {
      const double tempo = std::max(1.0, (double)Transport::Instance().Tempo());
      if (c.origBpm > 0.0f)
         ImGui::TextDisabled("Detected: %.1f BPM", (double)c.origBpm);
      else
         ImGui::TextDisabled("Detected: none (no clear beat)");
      if (c.syncToTempo)
         ImGui::TextDisabled("Stretched x%.3f to %.1f BPM", tempo / std::max(1.0, (double)c.sampleBpm), tempo);
      else
         ImGui::TextDisabled("Native speed - turn on Sync to Tempo to set Sample BPM");
   }


   // Resolves the decoded source buffer behind an Arrange::Clip's srcUid, for
   // recomputing a Sample's static waveform outside of import/bounce time
   // (e.g. right after a Split - see the two call sites in
   // ArrangeSplitSelectionAt/ArrangeBladeSplitAt). Null if the node is gone
   // or isn't an AudioFileNode (an offline clip, or one manually patched to
   // something else - neither should have sampleDropped set, but this stays
   // defensive rather than assuming).
   const Platform::SampleBuffer* ArrangeSampleBufferForSrcUid(uint64_t srcUid)
   {
      GraphNode* gn = FindNodeByUid(srcUid);
      if (gn == nullptr) return nullptr;
      AudioFileNode* afn = dynamic_cast<AudioFileNode*>(gn->node.get());
      return afn != nullptr ? afn->Buffer() : nullptr;
   }


   // Recomputes the static-waveform cache entry for a Sample clip, looking
   // up its own decoded buffer first. No-op (and clears any stale entry) for
   // a clip that isn't an Audio Sample or whose source no longer resolves.
   void ArrangeRefreshSampleStaticWave(uint64_t clipId)
   {
      Arrange::Clip* c = Arrange::FindClip(gArrange, clipId);
      if (c == nullptr || !c->sampleDropped)
      {
         gArrangeSampleStaticWaves.erase(clipId);
         return;
      }
      const Platform::SampleBuffer* buf = ArrangeSampleBufferForSrcUid(c->srcUid);
      if (buf == nullptr)
      {
         gArrangeSampleStaticWaves.erase(clipId);
         return;
      }
      ArrangeComputeSampleStaticWave(c->id, c->srcUid, c->srcOutput, c->start, c->length,
                                      ArrangeSampleEffBpm(*c), c->sourceOffsetSeconds, buf);
   }

   constexpr double kArrangeThumbRefreshSeconds = 1.0;


   // Aspect-fit copy, letterboxed to black - the same fit the composite uses,
   // so a thumbnail frames its clip the way the monitor does.
   struct ArrangeThumbProgram
   {
      unsigned int program = 0;
      int uTex = -1, uFit = -1;
   };

   const ArrangeThumbProgram& ArrangeThumbShader()
   {
      static ArrangeThumbProgram sProg;
      static bool sTried = false;
      if (sTried)
         return sProg;
      sTried = true;
      const char* src =
         "#version 150\n"
         "in vec2 vUv;\n"
         "out vec4 fragColor;\n"
         "uniform sampler2D uTex;\n"
         "uniform vec4 uFit;\n"
         "void main() {\n"
         "   vec2 uv = (vUv - uFit.zw) / uFit.xy;\n"
         "   if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {\n"
         "      fragColor = vec4(0.0, 0.0, 0.0, 1.0);\n"
         "      return;\n"
         "   }\n"
         "   fragColor = vec4(texture(uTex, uv).rgb, 1.0);\n"
         "}\n";
      sProg.program = GLUtil::CompileProgram(src);
      if (sProg.program != 0)
      {
         sProg.uTex = glGetUniformLocation(sProg.program, "uTex");
         sProg.uFit = glGetUniformLocation(sProg.program, "uFit");
      }
      return sProg;
   }


   // Called from inside the composite's resolve loop, which has not yet saved
   // the caller's framebuffer binding - so this saves and restores its own.
   void ArrangeCaptureClipThumb(uint64_t clipId, unsigned int tex, int srcW, int srcH)
   {
      if (clipId == 0 || tex == 0 || srcW <= 0 || srcH <= 0)
         return;
      auto it = gArrangeClipThumbs.find(clipId);
      if (it == gArrangeClipThumbs.end())
         return; // only clips the sync pass knows about get a thumbnail
      ArrangeClipThumb& th = it->second;
      const double now = glfwGetTime();
      if (th.lastCapture >= 0.0 && now - th.lastCapture < kArrangeThumbRefreshSeconds)
         return;
      const ArrangeThumbProgram& prog = ArrangeThumbShader();
      if (prog.program == 0 || !GLUtil::EnsureFbo(th.fbo, kArrangeThumbW, kArrangeThumbH))
         return;

      GLint prevFbo = 0;
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
      GLint prevVp[4];
      glGetIntegerv(GL_VIEWPORT, prevVp);

      const float dstAspect = (float)kArrangeThumbW / (float)kArrangeThumbH;
      const float srcAspect = (float)srcW / (float)srcH;
      float scaleX = 1.0f, scaleY = 1.0f, offX = 0.0f, offY = 0.0f;
      if (srcAspect > dstAspect)
      {
         scaleY = dstAspect / srcAspect;
         offY = (1.0f - scaleY) * 0.5f;
      }
      else
      {
         scaleX = srcAspect / dstAspect;
         offX = (1.0f - scaleX) * 0.5f;
      }
      GLUtil::RunShaderPass(th.fbo, prog.program, [&]()
      {
         glActiveTexture(GL_TEXTURE0);
         glBindTexture(GL_TEXTURE_2D, tex);
         glUniform1i(prog.uTex, 0);
         glUniform4f(prog.uFit, scaleX, scaleY, offX, offY);
      });
      glBindTexture(GL_TEXTURE_2D, 0);
      glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
      glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
      th.lastCapture = now;
   }


   // Once per main-loop frame, whether or not the panel is open: the ring has
   // to be drained even when nothing draws it, or it fills and starts
   // dropping. Reshapes the cache only when gArrange.revision moved - the one
   // change signal (invariant 6) - so an idle frame costs one ring drain.
   void ArrangeSyncClipVisuals()
   {
      static uint64_t sShapedRevision = ~0ull;
      static float sShapedTempo = -1.0f;
      const float liveTempo = Transport::Instance().Tempo();
      Arrange::gSampleLiveTempoBpm = std::max(1.0, (double)liveTempo);
      if (sShapedRevision != gArrange.revision || sShapedTempo != liveTempo)
      {
         sShapedRevision = gArrange.revision;
         sShapedTempo = liveTempo;
         std::unordered_set<uint64_t> live;
         std::unordered_set<uint64_t> liveVideo;
         for (const Arrange::Lane& lane : gArrange.lanes)
         {
            if (lane.type == Arrange::kLaneVideo)
            {
               // Thumbnails: one slot per video clip, kept across an edit
               // that does not change what the clip shows. A reassign resets
               // lastCapture so the next composite refreshes it at once
               // rather than up to a second later.
               for (const Arrange::Clip& c : lane.clips)
               {
                  liveVideo.insert(c.id);
                  ArrangeClipThumb& th = gArrangeClipThumbs[c.id];
                  if (th.srcUid != c.srcUid || th.srcOutput != c.srcOutput)
                  {
                     th.srcUid = c.srcUid;
                     th.srcOutput = c.srcOutput;
                     th.lastCapture = -1.0;
                  }
               }
               continue;
            }
            if (lane.type != Arrange::kLaneAudio)
               continue;
            for (const Arrange::Clip& c : lane.clips)
            {
               // Audio Sample: no live-fill entry at all - its waveform is
               // the static peak array computed once at import/bounce
               // (gArrangeSampleStaticWaves, see ArrangePollMediaImports),
               // never rebuilt here on a start/length change (a BPM edit
               // must not blow away the one-time measurement).
               if (c.sampleDropped)
               {
                  live.insert(c.id);
                  if (c.importPending)
                     continue;
                  const Platform::SampleBuffer* buf = ArrangeSampleBufferForSrcUid(c.srcUid);
                  const double effBpm = ArrangeSampleEffBpm(c);
                  auto sw = gArrangeSampleStaticWaves.find(c.id);
                  if (sw == gArrangeSampleStaticWaves.end() || sw->second.sampleBuf != buf ||
                      sw->second.srcUid != c.srcUid || sw->second.length != c.length ||
                      sw->second.start != c.start || sw->second.sampleEffBpm != effBpm ||
                      sw->second.sampleOffset != c.sourceOffsetSeconds)
                     ArrangeComputeSampleStaticWave(c.id, c.srcUid, c.srcOutput, c.start, c.length,
                                                    effBpm, c.sourceOffsetSeconds, buf);
                  continue;
               }
               const int buckets = ArrangeWaveBucketCount(c.length);
               if (buckets <= 0)
                  continue;
               live.insert(c.id);
               ArrangeClipWave& w = gArrangeClipWaves[c.id];
               if (w.srcUid != c.srcUid || w.srcOutput != c.srcOutput || w.start != c.start ||
                   w.length != c.length)
               {
                  w.srcUid = c.srcUid;
                  w.srcOutput = c.srcOutput;
                  w.start = c.start;
                  w.length = c.length;
                  w.shape = ArrangeClipShape(c.srcUid, c.srcOutput, c.start, c.length);
                  w.minv.assign((size_t)buckets, 0.0f);
                  w.maxv.assign((size_t)buckets, 0.0f);
                  w.filled.assign((size_t)buckets, 0);
               }
            }
         }
         // A deleted clip frees its cache here rather than on a timer: the
         // id is gone from the model, so nothing will ever fill it again.
         for (auto it = gArrangeClipWaves.begin(); it != gArrangeClipWaves.end();)
            it = live.count(it->first) == 0 ? gArrangeClipWaves.erase(it) : std::next(it);
         // Same prune for the Sample static-wave cache - a clip only ever
         // has an entry in one of the two maps (sampleDropped picks which),
         // but both are pruned off the same `live` set of ids still in the
         // model.
         for (auto it = gArrangeSampleStaticWaves.begin(); it != gArrangeSampleStaticWaves.end();)
            it = live.count(it->first) == 0 ? gArrangeSampleStaticWaves.erase(it) : std::next(it);
         // Same for a deleted video clip, plus its FBO. Safe to free here:
         // the thumbnail is only ever sampled by ImGui's draw list for the
         // frame that queued it, and a clip that is gone from the model
         // queued nothing this frame.
         for (auto it = gArrangeClipThumbs.begin(); it != gArrangeClipThumbs.end();)
         {
            if (liveVideo.count(it->first) != 0)
            {
               ++it;
               continue;
            }
            GLUtil::DestroyFbo(it->second.fbo);
            it = gArrangeClipThumbs.erase(it);
         }
      }

      static ClipPeak sPeaks[256];
      int n = 0;
      while ((n = AudioEngine::Instance().ClipPeaks().Read(sPeaks, 256)) > 0)
      {
         for (int i = 0; i < n; i++)
         {
            auto it = gArrangeClipWaves.find(sPeaks[i].clipId);
            if (it == gArrangeClipWaves.end())
               continue; // clip deleted or resized while the bucket was in flight
            ArrangeClipWave& w = it->second;
            if (sPeaks[i].shape != w.shape)
               continue; // measured before an edit reshaped this clip
            const int b = sPeaks[i].bucket;
            if (b < 0 || b >= (int)w.minv.size())
               continue;
            w.minv[(size_t)b] = sPeaks[i].minValue;
            w.maxv[(size_t)b] = sPeaks[i].maxValue;
            w.filled[(size_t)b] = 1;
         }
         if (n < 256)
            break;
      }
   }


   // One lane's contribution at a given instant.
   struct ArrangeVideoLayer
   {
      GraphNode* gn = nullptr;
      int srcOutput = 0;
      int blendMode = 0;
      float opacity = 1.0f;
      uint64_t clipId = 0; // whose thumbnail this layer's texture feeds (WP8)
      // Basic color grade (Clip::colorBrightness/colorContrast/colorSaturation).
      // 0/0/1 is a no-op, so every clip that predates this field grades identically.
      float gradeBrightness = 0.0f;
      float gradeContrast = 0.0f;
      float gradeSaturation = 1.0f;
   };


   // Every video lane's active clip at `beat`, in COMPOSITE order: bottom lane
   // first, top lane last (frontmost). Disabled clips and unassigned/offline
   // clips (no live source node) contribute nothing - the same rule the audio
   // scheduler applies (RebuildAudioTopology's `scheduled` loop).
   //
   // Reads gArrange directly (WP5b). Clip ticks convert to beats with no
   // tempo, so the playhead is Transport::Beats() - the axis the audio
   // envelope uses - and a tempo change moves nothing.
   void CollectArrangeVideoLayers(double beat, std::vector<ArrangeVideoLayer>& out)
   {
      out.clear();
      for (size_t li = gArrange.lanes.size(); li-- > 0;)
      {
         const Arrange::Lane& lane = gArrange.lanes[li];
         if (lane.type != Arrange::kLaneVideo)
            continue;
         if (!gArrangeRenderActiveLaneScope.empty() && !gArrangeRenderActiveLaneScope.count(lane.id))
            continue;
         for (const Arrange::Clip& c : lane.clips)
         {
            const double startBeat = Arrange::TicksToBeats(c.start);
            if (startBeat > beat)
               break; // clips are sorted by start; nothing later can cover `beat`
            if (!(beat < Arrange::TicksToBeats(c.End())))
               continue;
            // Lanes never overlap, so this is the lane's only candidate
            // whether or not it turns out to be usable.
            if (c.enabled && c.srcUid != 0 && Arrange::LaneEffectivelyEnabled(gArrange, lane))
            {
               GraphNode* gn = FindNodeByUid(c.srcUid);
               int srcOutput = c.srcOutput;
               // A clip plays what its node gives the canvas: bypassed
               // effect -> the picture feeding it, bypassed source -> nothing.
               // Same rule the audio terminals follow via ResolvedAudioSource.
               // Geometry nodes forward their own bypass, so they are drawn as is.
               if (gn != nullptr && gn->node != nullptr && gn->node->bypassed &&
                   dynamic_cast<IGeometrySource*>(gn->node.get()) == nullptr)
               {
                  INode* resolved = gn->node.get();
                  for (int hops = 0; resolved != nullptr && resolved->bypassed && hops < 64; hops++)
                     resolved = resolved->BypassSource();
                  if (resolved != nullptr && resolved->bypassed)
                     resolved = nullptr;
                  gn = nullptr;
                  srcOutput = 0;
                  for (GraphNode& cand : gNodes)
                     if (resolved != nullptr && cand.node.get() == resolved)
                        gn = &cand;
               }
               if (gn != nullptr && gn->node != nullptr)
                  out.push_back({ gn, srcOutput, c.blendMode, std::clamp(lane.opacity * c.opacity, 0.0f, 1.0f),
                                  c.id, c.colorBrightness, c.colorContrast, c.colorSaturation });
            }
            break;
         }
      }
   }


   int CountActiveArrangeVideoClips(double beat, std::string* outFrontTitle)
   {
      static std::vector<ArrangeVideoLayer> sLayers;
      CollectArrangeVideoLayers(beat, sLayers);
      if (outFrontTitle != nullptr && !sLayers.empty())
         *outFrontTitle = NodeTitle(*sLayers.back().gn);
      return (int)sLayers.size();
   }


   // Arrangement Timeline exact seek for Video Samples - the video-side
   // counterpart of AudioEngine::RunTopology's per-block audio seek
   // lookahead (see ClipWindow::sampleDropped's comment there), but run on
   // the main thread instead: video has no audio-thread block cadence, and
   // VideoSourceNode::CookIfNeeded runs once per frame from a flat, always-
   // on `gNodes` loop with no reachability filter and no idea which
   // arrangement clip (if any) it belongs to (see the cartographer
   // investigation this was built from - main.cpp's `gn.node->CookIfNeeded`
   // loop runs unconditionally before CompositeArrangeMonitorIfRequested /
   // CompositeArrangeTimelineVideo each frame, in both the realtime loop and
   // the offline render pump).
   //
   // Must be called BEFORE that per-frame CookIfNeeded loop, every frame,
   // for every Video Sample clip currently under the playhead: unlike the
   // audio path this pushes the exact position unconditionally rather than
   // only on a detected discontinuity, because on the main thread (not
   // real-time-block-constrained) recomputing "elapsed timeline seconds
   // since this window's onset" every frame is cheap and mathematically
   // identical to CookIfNeeded's own wall-clock-delta accumulation during
   // ordinary continuous playback - so it never fights normal playback, and
   // it also transparently fixes VideoSourceNode::CookIfNeeded's own
   // backward-delta-clamped-to-zero bug for these clips, since a backward
   // playhead move is just another exact position here, not a delta.
   // A live Video Clip (sampleDropped == false) is left on CookIfNeeded's
   // existing wall-clock free-run, unchanged, per the "only clips are
   // supposed to be live" distinction.
   // Runs every frame regardless of play state - see
   // VideoSourceNode::SyncToArrangement's own comment for why that's safe
   // now (it only actually seeks on a genuine discontinuity), and why that
   // matters: scrubbing the playhead while paused has to move the picture
   // too, not just while transport is running.
   void ArrangeSeekVideoSampleSources(double beat)
   {
      const double bpm = std::max(1.0, (double)Transport::Instance().Tempo());
      for (const Arrange::Lane& lane : gArrange.lanes)
      {
         if (lane.type != Arrange::kLaneVideo)
            continue;
         for (const Arrange::Clip& c : lane.clips)
         {
            const double startBeat = Arrange::TicksToBeats(c.start);
            if (startBeat > beat)
               break; // clips are sorted by start; nothing later can cover `beat`
            if (!(beat < Arrange::TicksToBeats(c.End())))
               continue;
            if (c.sampleDropped && c.enabled && c.srcUid != 0)
            {
               GraphNode* gn = FindNodeByUid(c.srcUid);
               if (auto* vid = gn != nullptr ? dynamic_cast<VideoSourceNode*>(gn->node.get()) : nullptr)
               {
                  // Elapsed timeline seconds since the clip's onset, scaled by
                  // the node's own `speed` (a real playback-rate control, the
                  // same as VCR varispeed) - unlike Sync to Tempo, `speed`
                  // really does change how many source-seconds pass per
                  // timeline-second, so leaving it out would seek to the
                  // wrong frame for any Sample with speed != 1.
                  const double elapsedSeconds = std::max(0.0, (beat - startBeat) * 60.0 / bpm);
                  const double offsetSeconds = elapsedSeconds * (double)vid->speed;
                  vid->SyncToArrangement(vid->trimStart + offsetSeconds);
               }
            }
            break;
         }
      }
   }


   // The lane blend program, compiled once with its uniform locations.
   struct ArrangeComposeProgram
   {
      unsigned int program = 0;
      int uTexBase = -1, uTexTop = -1, uMode = -1, uOpacity = -1, uTopFit = -1, uGrade = -1;
   };

   const ArrangeComposeProgram& ArrangeComposeShader()
   {
      static ArrangeComposeProgram sProg;
      static bool sTried = false;
      if (sTried)
         return sProg;
      sTried = true;
      const std::string src =
         std::string(
            "#version 150\n"
            "in vec2 vUv;\n"
            "out vec4 fragColor;\n"
            "uniform sampler2D uTexBase;\n"
            "uniform sampler2D uTexTop;\n"
            "uniform int uMode;\n"
            "uniform float uOpacity;\n"
            "uniform vec4 uTopFit;\n"
            "uniform vec3 uGrade;\n") // x = brightness -1..1, y = contrast -1..1, z = saturation 0..2
         + BlendModes::kBlendGLSL
         + "void main() {\n"
           "   vec2 topUv = (vUv - uTopFit.zw) / uTopFit.xy;\n"
           "   vec4 top = vec4(0.0);\n"
           "   if (topUv.x >= 0.0 && topUv.x <= 1.0 && topUv.y >= 0.0 && topUv.y <= 1.0) {\n"
           "      top = texture(uTexTop, topUv);\n"
           "      vec3 graded = (top.rgb - 0.5) * (1.0 + uGrade.y) + 0.5 + uGrade.x;\n"
           "      float luma = dot(graded, vec3(0.299, 0.587, 0.114));\n"
           "      top.rgb = clamp(mix(vec3(luma), graded, uGrade.z), 0.0, 1.0);\n"
           "   }\n"
           "   vec4 base = texture(uTexBase, vUv);\n"
           "   float as = top.a * uOpacity;\n"
           "   if (as <= 1e-6) {\n"
           "      fragColor = base;\n"
           "      return;\n"
           "   }\n"
           "   if (uMode == 30) { fragColor = vec4(base.rgb, base.a * (1.0 - as)); return; }\n"
           "   if (uMode == 31) { fragColor = vec4(base.rgb, base.a * (1.0 - (1.0 - top.a) * uOpacity)); return; }\n"
           "   vec3 blended = blendMode(uMode, base.rgb, top.rgb);\n"
           "   vec3 cs = mix(top.rgb, blended, base.a);\n"
           "   float ar = as + base.a * (1.0 - as);\n"
           "   vec3 cr = (ar > 1e-5) ? (cs * as + base.rgb * base.a * (1.0 - as)) / ar : vec3(0.0);\n"
           "   fragColor = vec4(cr, ar);\n"
           "}\n";
      sProg.program = GLUtil::CompileProgram(src.c_str());
      if (sProg.program != 0)
      {
         sProg.uTexBase = glGetUniformLocation(sProg.program, "uTexBase");
         sProg.uTexTop = glGetUniformLocation(sProg.program, "uTexTop");
         sProg.uMode = glGetUniformLocation(sProg.program, "uMode");
         sProg.uOpacity = glGetUniformLocation(sProg.program, "uOpacity");
         sProg.uTopFit = glGetUniformLocation(sProg.program, "uTopFit");
         sProg.uGrade = glGetUniformLocation(sProg.program, "uGrade");
      }
      return sProg;
   }


   // Composites every active video lane at `beat` into `dest` (targetW x
   // targetH), or into target.result when `dest` is null. Each lane uses its
   // own blend mode and opacity from the model (no UI for them yet - owner
   // decision) and is aspect-fit with letterboxing. No active clip clears
   // the destination to opaque black. Returns the destination texture.
   //
   // Sources are read as they stand: the caller cooks the graph first (the
   // main loop's cook, or the offline pump's), so every clip's texture
   // belongs to the same frame as the clip state that selected it.
   unsigned int CompositeArrangeTimelineVideo(ArrangeCompositeTarget& target, GLUtil::Fbo* dest,
                                              double beat, int targetW, int targetH)
   {
      if (targetW <= 1 || targetH <= 1)
         return 0;
      if (dest == nullptr)
      {
         // The resize survives one more composite (see retiredResult).
         GLUtil::DestroyFbo(target.retiredResult);
         if (target.result.fbo != 0 && (target.result.w != targetW || target.result.h != targetH))
         {
            target.retiredResult = target.result;
            target.result = GLUtil::Fbo();
         }
         dest = &target.result;
      }
      if (!GLUtil::EnsureFbo(*dest, targetW, targetH))
         return 0;

      static std::vector<ArrangeVideoLayer> sLayers;
      CollectArrangeVideoLayers(beat, sLayers);

      struct ResolvedLayer
      {
         unsigned int tex;
         int srcW, srcH;
         int blendMode;
         float opacity;
         float gradeBrightness, gradeContrast, gradeSaturation;
      };
      static std::vector<ResolvedLayer> sResolved;
      sResolved.clear();
      for (const ArrangeVideoLayer& layer : sLayers)
      {
         GraphNode* gn = layer.gn;
         unsigned int tex = 0;
         int w = 0, h = 0;
         if (auto* geo = dynamic_cast<IGeometrySource*>(gn->node.get()))
         {
            ArrangeGeomViewport& slot = gArrangeGeomViewports[{ gn->uid, target.slot }];
            slot.lastUsedFrame = gArrangeGeomFrame;
            // Offline export must not trust NodeViewport's live-preview
            // change-gate: it skips redraw whenever the node's own
            // revisions haven't ticked between calls, which is fine for a
            // UI glance but silently recaptures the prior frame's pixels
            // under a correct-but-stale timestamp during a take.
            tex = slot.viewport.Render(geo, gNodeCameras[gn->index], targetW, targetH,
                                       Transport::Instance().IsOfflineMode());
            w = targetW;
            h = targetH;
         }
         else
         {
            // The clip's chosen output, as a cable would pull it (index 0 is
            // the ordinary image; FieldPixel's aux texture is index 1).
            // Every multi-output node sizes its outputs alike.
            tex = gn->node->GetOutputTexture(layer.srcOutput);
            w = gn->node->GetOutputWidth();
            h = gn->node->GetOutputHeight();
         }
         if (tex != 0 && w > 0 && h > 0)
         {
            sResolved.push_back({ tex, w, h, layer.blendMode, layer.opacity,
                                   layer.gradeBrightness, layer.gradeContrast, layer.gradeSaturation });
            // The thumbnail rides on the composite's own resolve: the
            // texture is already in hand, so a thumbnail never costs a
            // second decode or a second geometry render. Skipped during an
            // offline take - the timeline is locked and nothing would draw
            // it, and a take must not spend its frame budget here.
            if (!Transport::Instance().IsOfflineMode())
               ArrangeCaptureClipThumb(layer.clipId, tex, w, h);
         }
      }

      GLint prevFbo = 0;
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
      GLint prevVp[4];
      glGetIntegerv(GL_VIEWPORT, prevVp);
      auto clearToBlack = [&](const GLUtil::Fbo& f)
      {
         glBindFramebuffer(GL_FRAMEBUFFER, f.fbo);
         glViewport(0, 0, targetW, targetH);
         glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
         glClear(GL_COLOR_BUFFER_BIT);
      };
      auto restore = [&]()
      {
         glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
         glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
      };

      const ArrangeComposeProgram& prog = ArrangeComposeShader();
      if (sResolved.empty() || prog.program == 0)
      {
         clearToBlack(*dest);
         restore();
         return dest->tex;
      }

      // Pass k reads `base` and writes the other scratch buffer, except the
      // last pass, which writes `dest` directly - so the result never needs a
      // copy back, and `dest` is never read and written by the same pass.
      // The second scratch buffer is only needed from two layers up.
      const size_t n = sResolved.size();
      if (!GLUtil::EnsureFbo(target.scratch[0], targetW, targetH) ||
          (n >= 2 && !GLUtil::EnsureFbo(target.scratch[1], targetW, targetH)))
      {
         clearToBlack(*dest);
         restore();
         return dest->tex;
      }
      clearToBlack(target.scratch[0]);
      int baseIdx = 0;
      const float targetAspect = (float)targetW / (float)targetH;
      for (size_t k = 0; k < n; k++)
      {
         const ResolvedLayer& layer = sResolved[k];
         const float srcAspect = (float)layer.srcW / (float)layer.srcH;
         float scaleX = 1.0f, scaleY = 1.0f, offX = 0.0f, offY = 0.0f;
         if (srcAspect > targetAspect)
         {
            scaleY = targetAspect / srcAspect;
            offY = (1.0f - scaleY) * 0.5f;
         }
         else
         {
            scaleX = srcAspect / targetAspect;
            offX = (1.0f - scaleX) * 0.5f;
         }

         const bool last = (k + 1 == n);
         const GLUtil::Fbo& out = last ? *dest : target.scratch[1 - baseIdx];
         const unsigned int baseTex = target.scratch[baseIdx].tex;
         GLUtil::RunShaderPass(out, prog.program, [&]()
         {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, baseTex);
            glUniform1i(prog.uTexBase, 0);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, layer.tex);
            glUniform1i(prog.uTexTop, 1);
            glUniform1i(prog.uMode, layer.blendMode);
            glUniform1f(prog.uOpacity, layer.opacity);
            glUniform4f(prog.uTopFit, scaleX, scaleY, offX, offY);
            glUniform3f(prog.uGrade, layer.gradeBrightness, layer.gradeContrast, layer.gradeSaturation);
         });
         baseIdx = 1 - baseIdx;
      }
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, 0);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, 0);

      restore();
      return dest->tex;
   }


   // The monitor's composite, run once per main-loop frame right after the
   // cook loop so it sees this frame's textures (it used to run inside the
   // panel draw, before the cook, and showed last frame's). The panel only
   // records a request and draws target.result; ImGui renders after this.
   void CompositeArrangeMonitorIfRequested()
   {
      ArrangeCompositeTarget& t = gArrangeMonitorTarget;
      if (t.requestW > 1 && t.requestH > 1)
         CompositeArrangeTimelineVideo(t, nullptr, Transport::Instance().Beats(), t.requestW, t.requestH);
      t.requestW = 0;
      t.requestH = 0;
   }


   // ---- arrangement editing on gArrange (overhaul WP5a) -------------------
   //
   // Everything below edits gArrange through Arrange:: ops only, so the two
   // model invariants (lanes sorted and non-overlapping, ids unique) hold by
   // construction, and every discrete edit is exactly one undo entry
   // (ArrangeEdit). They are free functions rather than panel code so
   // INFINITE_ARRANGEEDITTEST can drive the same paths a key press does.

   // Which outputs of `gn` carry the given lane type. Audio: the outputs its
   // IAudioSource says are audio (VideoSourceNode: only 1). Video: every
   // other output that is not a modulator (FieldPixelNode: 0 = out, 1 = the
   // aux "state" texture when exposed, plus declared image outputs).
   std::vector<int> ArrangeOutputsOfType(const GraphNode& gn, int laneType)
   {
      std::vector<int> out;
      if (gn.node == nullptr)
         return out;
      IAudioSource* audio = dynamic_cast<IAudioSource*>(gn.node.get());
      const int count = std::max(1, gn.node->OutputCount());
      for (int i = 0; i < count; i++)
      {
         const bool isAudio = audio != nullptr && audio->IsAudioOutputIndex(i);
         if (laneType == Arrange::kLaneAudio)
         {
            if (isAudio)
               out.push_back(i);
         }
         else if (!isAudio && gn.node->ModulatorOutput(i) == nullptr)
         {
            out.push_back(i);
         }
      }
      return out;
   }


   int ArrangeDefaultOutput(const GraphNode& gn, int laneType)
   {
      const std::vector<int> outs = ArrangeOutputsOfType(gn, laneType);
      return outs.empty() ? 0 : outs.front();
   }


   // A lane type's natural home for a node: anything that produces an image
   // goes on a video lane (including VideoSourceNode, whose audio half is
   // offered separately); audio-only nodes go on an audio lane.
   int ArrangeLaneTypeForNode(const GraphNode& gn)
   {
      return IsNodeVideoCompatible(gn) ? Arrange::kLaneVideo : Arrange::kLaneAudio;
   }


   // Drops everything that must not outlive its document or its clips: on a
   // new-document boundary the selection, rename/assign/context targets and
   // clipboard go; otherwise only the ids that no longer resolve. A stale id
   // therefore clears - it can never land on a different clip, because ids
   // are never reused.
   void ArrangePruneSelection()
   {
      if (gArrangeSelGeneration != gArrangePatchGeneration)
      {
         gArrangeSel.clear();
         gArrangeSelAnchor = 0;
         gArrangeRenamingClipId = 0;
         gArrangeCtxClipId = 0;
         gArrangeAssigningClipId = 0;
         gArrangeRenamingMarkerId = 0;
         gArrangeCtxMarkerId = 0;
         gArrangeMarkerDragId = 0;
         gArrangeRowSel.clear();
         gArrangeRowSelAnchor = 0;
         gArrangeSelGeneration = gArrangePatchGeneration;
      }
      // Row selection (tracks + group headers): same stale-id rule as the
      // clip selection above, since gArrangeRowSel is now file-scope and
      // outlives any single track/group's lifetime.
      for (auto it = gArrangeRowSel.begin(); it != gArrangeRowSel.end();)
      {
         const bool live = Arrange::FindLane(gArrange, *it) != nullptr || Arrange::FindTrackGroup(gArrange, *it) != nullptr;
         it = live ? std::next(it) : gArrangeRowSel.erase(it);
      }
      if (gArrangeRowSelAnchor != 0 && Arrange::FindLane(gArrange, gArrangeRowSelAnchor) == nullptr &&
          Arrange::FindTrackGroup(gArrange, gArrangeRowSelAnchor) == nullptr)
         gArrangeRowSelAnchor = 0;
      // Marker ids the panel holds, same rule as the clip ids below.
      auto markerLive = [](uint64_t id)
      {
         for (const Arrange::Marker& mk : gArrange.markers)
            if (mk.id == id) return true;
         return false;
      };
      if (gArrangeRenamingMarkerId != 0 && !markerLive(gArrangeRenamingMarkerId))
         gArrangeRenamingMarkerId = 0;
      if (gArrangeCtxMarkerId != 0 && !markerLive(gArrangeCtxMarkerId))
         gArrangeCtxMarkerId = 0;
      if (gArrangeMarkerDragId != 0 && !markerLive(gArrangeMarkerDragId))
         gArrangeMarkerDragId = 0;
      if (gArrangeClipboard.generation != gArrangePatchGeneration)
         gArrangeClipboard.items.clear();
      for (auto it = gArrangeSel.begin(); it != gArrangeSel.end();)
         it = Arrange::Find(gArrange, *it).Valid() ? std::next(it) : gArrangeSel.erase(it);
      if (gArrangeSelAnchor != 0 && !Arrange::Find(gArrange, gArrangeSelAnchor).Valid())
         gArrangeSelAnchor = 0;
      if (gArrangeRenamingClipId != 0 && !Arrange::Find(gArrange, gArrangeRenamingClipId).Valid())
         gArrangeRenamingClipId = 0;
      if (gArrangeCtxClipId != 0 && !Arrange::Find(gArrange, gArrangeCtxClipId).Valid())
         gArrangeCtxClipId = 0;
      if (gArrangeAssigningClipId != 0 && !Arrange::Find(gArrange, gArrangeAssigningClipId).Valid())
         gArrangeAssigningClipId = 0;
   }


   std::vector<uint64_t> ArrangeSelectionIds()
   {
      ArrangePruneSelection();
      return std::vector<uint64_t>(gArrangeSel.begin(), gArrangeSel.end());
   }


   // Click semantics. A grouped clip selects its whole group unless
   // `singleMember` (Alt-click). `toggle` (Cmd/Shift-click) adds or removes
   // without disturbing the rest of the selection.
   void ArrangeClickSelect(uint64_t clipId, bool toggle, bool singleMember)
   {
      ArrangePruneSelection();
      gArrangeRowSel.clear();
      gArrangeRowSelAnchor = 0;
      std::vector<uint64_t> ids{ clipId };
      if (!singleMember)
         ids = Arrange::ExpandSelectionToGroups(gArrange, ids);
      if (toggle)
      {
         const bool wasSelected = gArrangeSel.count(clipId) != 0;
         for (uint64_t id : ids)
         {
            if (wasSelected)
               gArrangeSel.erase(id);
            else
               gArrangeSel.insert(id);
         }
      }
      else
      {
         // Clicking a clip that is already part of a multi-selection keeps
         // the selection, so the drag that follows moves all of it.
         if (gArrangeSel.count(clipId) == 0 || singleMember)
         {
            gArrangeSel.clear();
            gArrangeSelAnchor = 0;
            gArrangeSel.insert(ids.begin(), ids.end());
         }
      }
      gArrangeSelAnchor = gArrangeSel.count(clipId) ? clipId : 0;
   }


   bool ArrangeCopySelection()
   {
      const std::vector<uint64_t> ids = ArrangeSelectionIds();
      if (ids.empty())
         return false;
      int minLane = INT_MAX;
      Arrange::Tick minStart = Arrange::kMaxTick;
      for (uint64_t id : ids)
      {
         const Arrange::Loc loc = Arrange::Find(gArrange, id);
         minLane = std::min(minLane, loc.lane);
         minStart = std::min(minStart, gArrange.lanes[loc.lane].clips[loc.index].start);
      }
      gArrangeClipboard.items.clear();
      gArrangeClipboard.generation = gArrangePatchGeneration;
      for (uint64_t id : ids)
      {
         const Arrange::Loc loc = Arrange::Find(gArrange, id);
         ArrangeClipboardItem item;
         item.clip = gArrange.lanes[loc.lane].clips[loc.index];
         item.laneOffset = loc.lane - minLane;
         item.laneType = gArrange.lanes[loc.lane].type;
         item.tickOffset = item.clip.start - minStart;
         gArrangeClipboard.items.push_back(item);
      }
      return true;
   }


   // Paste at `atTick` (the playhead) with the copied block's top lane on the
   // anchor clip's lane. A clipboard item whose relative lane is missing or of
   // the wrong type falls to the nearest lane of its type below that, then
   // any lane of its type, then a new one. Groups come back as new groups.
   bool ArrangePasteAt(Arrange::Tick atTick)
   {
      ArrangePruneSelection();
      if (gArrangeClipboard.items.empty())
         return false;
      std::vector<uint64_t> made;
      const bool changed = ArrangeEdit([&]()
      {
         int baseLane = -1;
         if (gArrangeSelAnchor != 0)
            baseLane = Arrange::Find(gArrange, gArrangeSelAnchor).lane;
         if (baseLane < 0)
         {
            for (int i = 0; i < (int)gArrange.lanes.size(); i++)
               if (gArrange.lanes[i].type == gArrangeClipboard.items.front().laneType) { baseLane = i; break; }
         }
         if (baseLane < 0)
            baseLane = 0;

         std::map<uint64_t, std::vector<uint64_t>> newGroups;
         for (const ArrangeClipboardItem& item : gArrangeClipboard.items)
         {
            int lane = baseLane + item.laneOffset;
            auto fits = [&](int i) { return i >= 0 && i < (int)gArrange.lanes.size() && gArrange.lanes[i].type == item.laneType; };
            if (!fits(lane))
            {
               int found = -1;
               for (int i = std::max(0, lane); i < (int)gArrange.lanes.size() && found < 0; i++)
                  if (fits(i)) found = i;
               for (int i = 0; i < (int)gArrange.lanes.size() && found < 0; i++)
                  if (fits(i)) found = i;
               if (found < 0)
               {
                  const uint64_t laneId = Arrange::AddLane(gArrange, item.laneType);
                  found = Arrange::LaneIndex(gArrange, laneId);
               }
               lane = found;
            }
            Arrange::Clip c = item.clip;
            c.id = 0;          // a paste is a new clip
            c.groupId = 0;     // regrouped below, all members at once
            c.start = std::max<Arrange::Tick>(0, atTick + item.tickOffset);
            uint64_t newId = 0;
            if (Arrange::PlaceOverwrite(gArrange, gArrange.lanes[lane].id, c, &newId))
            {
               made.push_back(newId);
               if (item.clip.groupId != 0)
                  newGroups[item.clip.groupId].push_back(newId);
            }
         }
         // PlaceOverwrite dissolves singleton groups, so a group is only
         // re-created once every member is back.
         for (auto& kv : newGroups)
            Arrange::Group(gArrange, kv.second);
      });
      if (!changed)
         return false;
      // A pasted Sample clip still points at the ORIGINAL clip's srcUid at
      // this point (PlaceOverwrite copies the Clip struct verbatim aside
      // from id/groupId/start) - give it its own private hidden node and a
      // fresh decode before anything else touches it, so it never contends
      // with the original (or any other paste of the same clip) over one
      // node's single playback cursor/stretcher.
      for (uint64_t id : made)
         ArrangeRespawnCloneNode(id);
      // A pasted clip is a fresh id (c.id = 0 above, reassigned by
      // PlaceOverwrite) - the static-waveform cache is keyed by clip id, so
      // without this a pasted Sample clip shows no waveform at all until
      // some other edit happens to touch it.
      for (uint64_t id : made)
         ArrangeRefreshSampleStaticWave(id);
      gArrangeSel.clear();
      for (uint64_t id : made)
         if (Arrange::Find(gArrange, id).Valid())
            gArrangeSel.insert(id);
      gArrangeSelAnchor = made.empty() ? 0 : made.front();
      return true;
   }


   bool ArrangeDuplicateSelection()
   {
      const std::vector<uint64_t> ids = ArrangeSelectionIds();
      std::vector<uint64_t> made;
      if (!ArrangeEdit([&]() { Arrange::DuplicateBlock(gArrange, ids, &made); }))
         return false;
      // Same node-sharing hazard as ArrangePasteAt above.
      for (uint64_t id : made)
         ArrangeRespawnCloneNode(id);
      // Same fresh-id/stale-cache gap as ArrangePasteAt above.
      for (uint64_t id : made)
         ArrangeRefreshSampleStaticWave(id);
      gArrangeSel.clear();
      gArrangeSel.insert(made.begin(), made.end());
      gArrangeSelAnchor = made.empty() ? 0 : made.front();
      return true;
   }


   bool ArrangeDeleteSelection()
   {
      const std::vector<uint64_t> ids = ArrangeSelectionIds();
      if (!ArrangeEdit([&]() { Arrange::Delete(gArrange, ids); }))
         return false;
      gArrangeSel.clear();
      gArrangeSelAnchor = 0;
      return true;
   }


   // Cmd+E: cuts every selected clip the tick passes through; both halves
   // stay selected.
   bool ArrangeSplitSelectionAt(Arrange::Tick tick)
   {
      const std::vector<uint64_t> ids = ArrangeSelectionIds();
      std::vector<uint64_t> rights;
      if (!ArrangeEdit([&]()
          {
             for (uint64_t id : ids)
             {
                uint64_t right = 0;
                if (Arrange::Split(gArrange, id, tick, &right))
                {
                   rights.push_back(right);
                   // The left half's own static waveform (if it's a Sample)
                   // is now stale - its buckets covered the pre-split length
                   // - and the right half has no entry at all yet. See
                   // ArrangeComputeSampleStaticWave's own comment.
                   ArrangeRefreshSampleStaticWave(id);
                   ArrangeRefreshSampleStaticWave(right);
                }
             }
          }))
         return false;
      gArrangeSel.insert(rights.begin(), rights.end());
      return true;
   }


   // Blade click: cuts the clicked clip at `tick` - and, groups being whole,
   // every other member of its group the tick passes through. One undo
   // entry. A right half joins the selection only if its left half was in it.
   bool ArrangeBladeSplitAt(uint64_t clipId, Arrange::Tick tick)
   {
      ArrangePruneSelection();
      const std::vector<uint64_t> ids = Arrange::ExpandSelectionToGroups(gArrange, { clipId });
      std::vector<uint64_t> rights;
      if (!ArrangeEdit([&]()
          {
             for (uint64_t id : ids)
             {
                uint64_t right = 0;
                if (Arrange::Split(gArrange, id, tick, &right))
                {
                   // Same static-waveform refresh as ArrangeSplitSelectionAt
                   // above, regardless of whether the right half ends up
                   // selected.
                   ArrangeRefreshSampleStaticWave(id);
                   ArrangeRefreshSampleStaticWave(right);
                   if (gArrangeSel.count(id) != 0)
                      rights.push_back(right);
                }
             }
          }))
         return false;
      gArrangeSel.insert(rights.begin(), rights.end());
      return true;
   }


   // `0`: a mixed selection is disabled first (any enabled clip wins), so one
   // press always leaves the whole selection in one state.
   bool ArrangeToggleEnabledSelection()
   {
      const std::vector<uint64_t> ids = ArrangeSelectionIds();
      bool anyEnabled = false;
      for (uint64_t id : ids)
         if (const Arrange::Clip* c = Arrange::FindClip(gArrange, id))
            anyEnabled = anyEnabled || c->enabled;
      return ArrangeEdit([&]() { Arrange::SetEnabled(gArrange, ids, anyEnabled ? Arrange::kDisable : Arrange::kEnable); });
   }


   // Whether Group would do anything: the selection, grown to whole groups,
   // is at least two clips and not already exactly one group.
   bool ArrangeCanGroupSelection()
   {
      const std::vector<uint64_t> ids = Arrange::ExpandSelectionToGroups(gArrange, ArrangeSelectionIds());
      if (ids.size() < 2)
         return false;
      uint64_t only = 0;
      for (uint64_t id : ids)
      {
         const Arrange::Clip* c = Arrange::FindClip(gArrange, id);
         if (c == nullptr || c->groupId == 0 || (only != 0 && c->groupId != only))
            return true;
         only = c->groupId;
      }
      return false;
   }


   bool ArrangeCanUngroupSelection()
   {
      for (uint64_t id : ArrangeSelectionIds())
         if (const Arrange::Clip* c = Arrange::FindClip(gArrange, id))
            if (c->groupId != 0)
               return true;
      return false;
   }


   // Groups are whole (Arrange::Group): grouping merges every group the
   // selection touches plus its loose clips into one, and the selection
   // grows to match so it shows what was grouped.
   bool ArrangeGroupSelection()
   {
      const std::vector<uint64_t> ids = ArrangeSelectionIds();
      if (!ArrangeEdit([&]() { Arrange::Group(gArrange, ids); }))
         return false;
      const std::vector<uint64_t> all = Arrange::ExpandSelectionToGroups(gArrange, ids);
      gArrangeSel.insert(all.begin(), all.end());
      return true;
   }


   bool ArrangeUngroupSelection()
   {
      std::vector<uint64_t> groups;
      for (uint64_t id : ArrangeSelectionIds())
         if (const Arrange::Clip* c = Arrange::FindClip(gArrange, id))
            if (c->groupId != 0)
               groups.push_back(c->groupId);
      return ArrangeEdit([&]() { Arrange::Ungroup(gArrange, groups); });
   }


   // ---- header-row (track/group) selection ops --------------------------
   // Shift+D, Delete and Cmd+G/Cmd+Shift+G act on gArrangeRowSel when it is
   // non-empty (see DrawArrangePanelContent's keyboard block) instead of the
   // clip selection above - "selection is the context" for every one of
   // these shortcuts, and a track/group row selection always wins over a
   // stale or coincidental clip selection.

   // Cmd+G with tracks/groups selected: wraps every selected row as children
   // of one brand-new group, nested at the lowest common ancestor of their
   // current parents (0 = top level) - the same placement rule the "Group
   // Selected" lane context-menu item uses, extended to cover a selected
   // group (reparented whole, subtree and all) as well as a selected lane.
   bool ArrangeGroupRowSelection()
   {
      std::vector<uint64_t> selLanes, selGroups;
      for (uint64_t id : gArrangeRowSel)
      {
         if (Arrange::FindLane(gArrange, id) != nullptr) selLanes.push_back(id);
         else if (Arrange::FindTrackGroup(gArrange, id) != nullptr) selGroups.push_back(id);
      }
      // A selected group that is a descendant of another selected group
      // moves along with its ancestor - drop it so it isn't reparented twice.
      selGroups.erase(std::remove_if(selGroups.begin(), selGroups.end(), [&](uint64_t gid)
      {
         for (uint64_t anc : Arrange::GroupAncestors(gArrange, gid))
            if (std::find(selGroups.begin(), selGroups.end(), anc) != selGroups.end())
               return true;
         return false;
      }), selGroups.end());
      if (selLanes.empty() && selGroups.empty())
         return false;

      auto chainFor = [&](uint64_t parentGroupId) -> std::vector<uint64_t>
      {
         std::vector<uint64_t> anc = Arrange::GroupAncestors(gArrange, parentGroupId);
         std::reverse(anc.begin(), anc.end());
         if (parentGroupId != 0) anc.push_back(parentGroupId);
         return anc;
      };
      std::vector<uint64_t> lca;
      bool first = true;
      auto foldChain = [&](const std::vector<uint64_t>& chain)
      {
         if (first) { lca = chain; first = false; return; }
         const size_t n = std::min(lca.size(), chain.size());
         size_t common = 0;
         while (common < n && lca[common] == chain[common]) common++;
         lca.resize(common);
      };
      for (uint64_t lid : selLanes)
      {
         const Arrange::Lane* ln = Arrange::FindLane(gArrange, lid);
         foldChain(chainFor(ln ? ln->groupId : 0));
      }
      for (uint64_t gid : selGroups)
      {
         const Arrange::TrackGroup* g = Arrange::FindTrackGroup(gArrange, gid);
         foldChain(chainFor(g ? g->parentGroupId : 0));
      }
      const uint64_t shallowestParent = lca.empty() ? 0 : lca.back();

      uint64_t newGroupId = 0;
      if (!ArrangeEdit([&]()
          {
             newGroupId = Arrange::GroupSelectedLanes(gArrange, selLanes, shallowestParent);
             if (newGroupId == 0)
                newGroupId = Arrange::AddTrackGroup(gArrange, {}, std::string(), shallowestParent);
             for (uint64_t gid : selGroups)
                Arrange::SetTrackGroupParent(gArrange, gid, newGroupId);
          }))
         return false;

      gArrangeRowSel.clear();
      gArrangeRowSel.insert(newGroupId);
      gArrangeRowSelAnchor = newGroupId;
      return true;
   }


   // Cmd+Shift+G with tracks/groups selected: ungroups every selected group
   // one level (members promoted to its own parent, same as the group
   // context menu's own Ungroup) - selected lanes are not group containers
   // and are simply ignored.
   bool ArrangeUngroupRowSelection()
   {
      std::vector<uint64_t> selGroups;
      for (uint64_t id : gArrangeRowSel)
         if (Arrange::FindTrackGroup(gArrange, id) != nullptr)
            selGroups.push_back(id);
      if (selGroups.empty())
         return false;
      if (!ArrangeEdit([&]()
          {
             for (uint64_t gid : selGroups)
                Arrange::RemoveTrackGroup(gArrange, gid, /*deleteLanes=*/false);
          }))
         return false;
      gArrangeRowSel.clear();
      gArrangeRowSelAnchor = 0;
      return true;
   }


   // Shift+D with tracks/groups selected: duplicates every selected row. A
   // selected group takes its whole subtree with it (DuplicateTrackGroup); a
   // selected lane duplicates on its own (DuplicateLane) unless it already
   // sits inside a selected group's subtree, which just duplicated it too -
   // skipped there to avoid a double copy.
   bool ArrangeDuplicateRowSelection()
   {
      std::vector<uint64_t> selLanes, selGroups;
      for (uint64_t id : gArrangeRowSel)
      {
         if (Arrange::FindLane(gArrange, id) != nullptr) selLanes.push_back(id);
         else if (Arrange::FindTrackGroup(gArrange, id) != nullptr) selGroups.push_back(id);
      }
      if (selLanes.empty() && selGroups.empty())
         return false;

      auto laneInsideGroup = [&](uint64_t laneId, uint64_t groupId)
      {
         const Arrange::Lane* ln = Arrange::FindLane(gArrange, laneId);
         if (!ln || ln->groupId == 0) return false;
         if (ln->groupId == groupId) return true;
         for (uint64_t anc : Arrange::GroupAncestors(gArrange, ln->groupId))
            if (anc == groupId) return true;
         return false;
      };

      std::vector<uint64_t> newIds;
      if (!ArrangeEdit([&]()
          {
             for (uint64_t gid : selGroups)
             {
                uint64_t newGid = 0;
                if (Arrange::DuplicateTrackGroup(gArrange, gid, &newGid))
                   newIds.push_back(newGid);
             }
             for (uint64_t lid : selLanes)
             {
                bool covered = false;
                for (uint64_t gid : selGroups)
                   if (laneInsideGroup(lid, gid)) { covered = true; break; }
                if (covered) continue;
                uint64_t newLid = 0;
                if (Arrange::DuplicateLane(gArrange, lid, &newLid))
                   newIds.push_back(newLid);
             }
          }))
         return false;

      gArrangeRowSel.clear();
      gArrangeRowSel.insert(newIds.begin(), newIds.end());
      gArrangeRowSelAnchor = newIds.empty() ? 0 : newIds.front();
      return true;
   }


   // Delete with tracks/groups selected: deletes every selected row (a
   // selected group takes its whole subtree with it).
   bool ArrangeDeleteRowSelection()
   {
      std::vector<uint64_t> selLanes, selGroups;
      for (uint64_t id : gArrangeRowSel)
      {
         if (Arrange::FindLane(gArrange, id) != nullptr) selLanes.push_back(id);
         else if (Arrange::FindTrackGroup(gArrange, id) != nullptr) selGroups.push_back(id);
      }
      if (selLanes.empty() && selGroups.empty())
         return false;
      if (!ArrangeEdit([&]()
          {
             for (uint64_t gid : selGroups)
                Arrange::RemoveTrackGroup(gArrange, gid, /*deleteLanes=*/true);
             for (uint64_t lid : selLanes)
                Arrange::RemoveLane(gArrange, lid);
          }))
         return false;
      gArrangeRowSel.clear();
      gArrangeRowSelAnchor = 0;
      return true;
   }


   // Shift+J (Video) / Shift+K (Audio) on the timeline: adds a new track after the
   // current row-selection anchor (or at the end if nothing is selected).
   bool ArrangeAddTrackShortcut(bool isVideo)
   {
      int insertAfter = -1;
      if (gArrangeRowSelAnchor != 0)
      {
         const int idx = Arrange::LaneIndex(gArrange, gArrangeRowSelAnchor);
         if (idx >= 0) insertAfter = idx;
      }
      uint64_t newId = 0;
      if (!ArrangeEdit([&]()
          {
             const int laneType = isVideo ? Arrange::kLaneVideo : Arrange::kLaneAudio;
             int n = 1;
             for (const Arrange::Lane& l : gArrange.lanes)
                if (l.type == laneType) n++;
             const int at = (insertAfter < 0 || insertAfter >= (int)gArrange.lanes.size()) ? -1 : insertAfter + 1;
             newId = Arrange::AddLane(gArrange, laneType, at);
             if (Arrange::Lane* l = Arrange::FindLane(gArrange, newId))
                l->name = (isVideo ? "Video " : "Audio ") + std::to_string(n);
          }))
         return false;
      gArrangeRowSel.clear();
      gArrangeRowSel.insert(newId);
      gArrangeRowSelAnchor = newId;
      return true;
   }


   // Left / Right (WP6): with clips selected, nudge the selection one grid
   // step through MoveClips (one undo entry; the block stops at 0 as a
   // whole). With nothing selected, step the playhead to the previous / next
   // grid point. Returns whether anything moved.
   bool ArrangeNudge(int dir)
   {
      const Arrange::Tick step = ArrangeNudgeStepTicks();
      const std::vector<uint64_t> ids = ArrangeSelectionIds();
      if (!ids.empty())
         return ArrangeEdit([&]() { Arrange::MoveClips(gArrange, ids, dir < 0 ? -step : step, 0); });
      const Arrange::Tick play = ArrangePlayTick();
      const Arrange::Tick target = dir < 0 ? Arrange::GridCeil(play, step) - step
                                           : Arrange::GridFloor(play, step) + step;
      const Arrange::Tick clamped = std::clamp<Arrange::Tick>(target, 0, Arrange::kMaxTick);
      if (clamped == play)
         return false;
      ArrangeSeekTick(clamped);
      return true;
   }


   // Cmd+R / Ctrl+R: renames the active selection (track, group, clip, or
   // multiple selected clips). If header rows are selected, acts on the anchor
   // row; otherwise acts on the selected clip(s).
   bool ArrangeRenameSelection()
   {
      // Track or group row selection takes precedence if active
      if (!gArrangeRowSel.empty())
      {
         uint64_t targetId = 0;
         if (gArrangeRowSelAnchor != 0 && gArrangeRowSel.count(gArrangeRowSelAnchor))
            targetId = gArrangeRowSelAnchor;
         else
            targetId = *gArrangeRowSel.begin();

         if (targetId != 0)
         {
            gArrangeRenamingLaneId = targetId;
            gArrangeRenameJustStarted = true;
            return true;
         }
      }

      // Clip selection (single or multi)
      const std::vector<uint64_t> selIds = ArrangeSelectionIds();
      if (!selIds.empty())
      {
         uint64_t cid = 0;
         if (gArrangeSelAnchor != 0 && std::find(selIds.begin(), selIds.end(), gArrangeSelAnchor) != selIds.end())
            cid = gArrangeSelAnchor;
         else
            cid = selIds.front();

         const Arrange::Clip* cp = Arrange::FindClip(gArrange, cid);
         if (cp != nullptr)
         {
            const GraphNode* ctxNode = FindNodeByUid(cp->srcUid);
            const std::string label = !cp->name.empty() ? cp->name
               : (ctxNode != nullptr ? NodeTitle(*ctxNode) : std::string("Unassigned"));
            gArrangeRenamingClipId = cid;
            gArrangeRenameTargetIds.clear();
            for (uint64_t id : selIds)
               if (id != cid)
                  gArrangeRenameTargetIds.push_back(id);
            snprintf(gArrangeRenameClipBuffer, sizeof(gArrangeRenameClipBuffer), "%s", label.c_str());
            return true;
         }
      }
      return false;
   }


   // ---- live clip drag ------------------------------------------------------

   // Starts a drag gesture on `clipId`. `mode` is an ArrangeDragMode; for a
   // move, the current selection is what moves.
   void ArrangeDragBegin(int mode, uint64_t clipId, int edge, Arrange::Tick grabTick)
   {
      ArrangeGestureBegin();
      ArrangeDragState d;
      d.mode = mode;
      d.clipId = clipId;
      d.edge = edge;
      d.grabTick = grabTick;
      const Arrange::Loc loc = Arrange::Find(gArrange, clipId);
      if (!loc.Valid())
      {
         gArrangeDrag = ArrangeDragState();
         return;
      }
      const Arrange::Clip& c = gArrange.lanes[loc.lane].clips[loc.index];
      d.grabLane = loc.lane;
      d.groupId = c.groupId;
      d.origStart = c.start;
      d.origEnd = c.End();
      if (mode == kArrangeDragGroupEdge || mode == kArrangeDragGroupScale)
      {
         bool first = true;
         for (const Arrange::Lane& l : gArrange.lanes)
            for (const Arrange::Clip& m : l.clips)
               if (m.groupId == c.groupId)
               {
                  d.origStart = first ? m.start : std::min(d.origStart, m.start);
                  d.origEnd = first ? m.End() : std::max(d.origEnd, m.End());
                  first = false;
               }
      }
      if (mode == kArrangeDragMove)
      {
         d.ids = ArrangeSelectionIds();
         if (std::find(d.ids.begin(), d.ids.end(), clipId) == d.ids.end())
            d.ids = { clipId };
         d.appliedDelta = 0;
      }
      else
      {
         d.appliedTick = (edge == Arrange::kEdgeStart) ? d.origStart : d.origEnd;
      }
      d.appliedLaneDelta = 0;
      gArrangeDrag = d;
   }


   // Rebuilds gArrange as (gesture snapshot + this drag). `value` is the tick
   // delta for a move and the absolute edge tick for every edge mode.
   // Returns whether the model changed this call. The same op the release
   // would run, so the live view is exactly the drop.
   bool ArrangeDragUpdate(Arrange::Tick value, int laneDelta)
   {
      ArrangeDragState& d = gArrangeDrag;
      if (d.mode == kArrangeDragNone || !gArrangeGestureOpen)
         return false;
      if (d.mode == kArrangeDragMove)
      {
         if (value == d.appliedDelta && laneDelta == d.appliedLaneDelta)
            return false;
      }
      else if (value == d.appliedTick)
      {
         return false;
      }

      const uint64_t liveRevision = gArrange.revision;
      const uint64_t liveNextId = gArrange.nextId;
      const Arrange::LoopRange liveLoop = gArrange.settings.loop;
      const ArrangeViewSettings liveView = ArrangeKeepViewSettings(gArrange);
      gArrange = gArrangeGestureBefore;
      gArrange.nextId = std::max(gArrange.nextId, liveNextId);
      gArrange.settings.loop = liveLoop;
      ArrangeRestoreViewSettings(gArrange, liveView);
      // Restoring the snapshot is itself a change of what is on screen, and
      // revision must only climb (the audio rebuild keys on it).
      gArrange.revision = liveRevision + 1;

      switch (d.mode)
      {
         case kArrangeDragMove:
            // The lane delta applies only if every clip lands on a lane of its
            // own type; otherwise the block still moves in time on its lanes.
            if (!Arrange::MoveClips(gArrange, d.ids, value, laneDelta) && laneDelta != 0)
               Arrange::MoveClips(gArrange, d.ids, value, 0);
            // Keyed on what was asked for, so the next frame with the same
            // mouse position is a no-op.
            d.appliedDelta = value;
            d.appliedLaneDelta = laneDelta;
            break;
         case kArrangeDragTrimStart:
         case kArrangeDragTrimEnd:
            if (Arrange::TrimEdge(gArrange, d.clipId, d.edge, value) && d.edge == Arrange::kEdgeStart)
               // A start trim moved this clip's sourceOffsetSeconds (see
               // TrimEdge's own comment) - the same staleness Split leaves
               // behind, so refresh the same way its call sites do.
               ArrangeRefreshSampleStaticWave(d.clipId);
            d.appliedTick = value;
            break;
         case kArrangeDragGroupEdge:
            if (Arrange::TrimGroupEdge(gArrange, d.groupId, d.edge, value) && d.edge == Arrange::kEdgeStart)
               for (const Arrange::Lane& l : gArrange.lanes)
                  for (const Arrange::Clip& c : l.clips)
                     if (c.groupId == d.groupId)
                        ArrangeRefreshSampleStaticWave(c.id);
            d.appliedTick = value;
            break;
         case kArrangeDragGroupScale:
            Arrange::ScaleGroup(gArrange, d.groupId, d.edge, value);
            d.appliedTick = value;
            break;
         default:
            break;
      }
      return true;
   }


   // Mouse up. One undo entry for the whole drag, and none at all if it ended
   // where it started (ArrangeGestureEnd compares content, not revision).
   // Returns whether an entry was pushed.
   bool ArrangeDragEnd()
   {
      if (gArrangeDrag.mode == kArrangeDragNone)
         return false;
      gArrangeDrag = ArrangeDragState();
      return ArrangeGestureEnd();
   }


   // Adds `nodeIndex` to the timeline as a one-bar clip. `laneType` -1 picks
   // the node's natural lane (ArrangeLaneTypeForNode); `srcOutput` -1 picks
   // that lane type's first matching output (VideoSourceNode audio: 1). Lands
   // on the first lane of that type (a new one if there is none), at the
   // later of the playhead and that lane's last clip end. Returns the new
   // clip's id, 0 if nothing was added.
   uint64_t AddNodeToArrangeTimeline(int nodeIndex, int laneType, int srcOutput)
   {
      GraphNode* gn = FindNodeByIndex(nodeIndex);
      if (gn == nullptr || gn->node == nullptr)
         return 0;
      if (laneType < 0)
         laneType = ArrangeLaneTypeForNode(*gn);
      if (laneType == Arrange::kLaneAudio ? !IsNodeAudioCompatible(*gn) : !IsNodeVideoCompatible(*gn))
         return 0;
      if (srcOutput < 0)
         srcOutput = ArrangeDefaultOutput(*gn, laneType);

      uint64_t made = 0;
      ArrangeEdit([&]()
      {
         int lane = -1;
         for (int i = 0; i < (int)gArrange.lanes.size(); i++)
            if (gArrange.lanes[i].type == laneType) { lane = i; break; }
         if (lane < 0)
         {
            const uint64_t laneId = Arrange::AddLane(gArrange, laneType);
            lane = Arrange::LaneIndex(gArrange, laneId);
         }
         // The first one-bar gap at or after the playhead - not after the
         // lane's last clip, which on a long arrangement put the new clip
         // far off-screen. Clips on a lane are sorted and never overlap.
         Arrange::Tick start = ArrangePlayTick();
         for (const Arrange::Clip& lc : gArrange.lanes[lane].clips)
         {
            if (lc.End() <= start)
               continue;
            if (lc.start >= start + Arrange::kTicksPerBar)
               break;
            start = lc.End();
         }
         Arrange::Clip c;
         c.start = start;
         c.length = Arrange::kTicksPerBar;
         c.srcUid = gn->uid;
         c.srcOutput = srcOutput;
         Arrange::PlaceOverwrite(gArrange, gArrange.lanes[lane].id, c, &made);
      });
      if (made != 0)
      {
         gArrangePanelOpen = true;
         gArrangeSel = { made };
         gArrangeSelAnchor = made;
         gArrangeRevealClipId = made;
         gArrangeRevealFrames = 3;
         gArrangeFlashClipId = made;
         gArrangeFlashStart = ImGui::GetTime();
      }
      return made;
   }


   // Adds an audio or video/image file drop onto the timeline at a given screen position.
   uint64_t AddFileToTimelineAt(const std::string& path, const ImVec2& screenPos)
   {
      int laneType = -1;
      std::string nodeType, nodeCategory;
      bool isImage = false;

      if (HasExtension(path, MediaExtensions::Audio()))
      {
         laneType = Arrange::kLaneAudio;
         nodeType = "Audio File";
         nodeCategory = "Synths";
      }
      else if (HasExtension(path, MediaExtensions::Video()))
      {
         laneType = Arrange::kLaneVideo;
         nodeType = "Video";
         nodeCategory = "Source";
      }
      else if (HasExtension(path, MediaExtensions::Image()))
      {
         laneType = Arrange::kLaneVideo;
         nodeType = "Image Source";
         nodeCategory = "Source";
         isImage = true;
      }
      else
      {
         return 0;
      }

      // Spawn backing node on the graph canvas
      GraphNode* gn = SpawnNode(nodeType, nodeCategory, 0.0f, 0.0f);
      if (gn == nullptr || gn->node == nullptr)
         return 0;

      if (laneType == Arrange::kLaneAudio)
      {
         if (auto* af = dynamic_cast<AudioFileNode*>(gn->node.get()))
            af->Open(path);
      }
      else if (isImage)
      {
         if (auto* img = dynamic_cast<ImageSourceNode*>(gn->node.get()))
            img->Load(path);
      }
      else
      {
         if (auto* vid = dynamic_cast<VideoSourceNode*>(gn->node.get()))
            vid->Open(path);
      }

      // Resolve drop tick position from screenPos.x
      const double startBeat = std::max(0.0, gArrangeScrollBeats);
      const double ppb = std::max(1.0, (double)gArrangePixelsPerBeat);
      const float rulerStart = (sArrangeLastRulerStartX > 0.0f) ? sArrangeLastRulerStartX : (gArrangePanelRectMin.x + 300.0f);
      const double dropBeat = std::max(0.0, startBeat + (double)(screenPos.x - rulerStart) / ppb);
      Arrange::Tick dropTick = Arrange::BeatsToTicks(dropBeat);
      const Arrange::Tick gridTicks = ArrangeSnapGridTicks();
      if (gridTicks > 0)
         dropTick = Arrange::SnapToGrid(dropTick, gridTicks);

      // Clip length: image defaults to 5.0 seconds, others 4 bars
      const Arrange::Tick clipLength = isImage
         ? std::max<Arrange::Tick>(Arrange::kPPQ, Arrange::SecondsToTicks(5.0, std::max(1.0, (double)Transport::Instance().Tempo())))
         : (4 * Arrange::kTicksPerBar);

      uint64_t made = 0;
      ArrangeEdit([&]()
      {
         int targetLane = -1;
         for (int i = 0; i < (int)gArrange.lanes.size(); i++)
         {
            if (gArrange.lanes[i].type == laneType)
            {
               targetLane = i;
               break;
            }
         }
         if (targetLane < 0)
         {
            const uint64_t laneId = Arrange::AddLane(gArrange, laneType);
            targetLane = Arrange::LaneIndex(gArrange, laneId);
         }

         if (targetLane >= 0 && targetLane < (int)gArrange.lanes.size())
         {
            Arrange::Clip c;
            c.start = dropTick;
            c.length = clipLength;
            c.srcUid = gn->uid;
            c.srcOutput = 0;
            std::filesystem::path p(path);
            c.name = p.stem().string();
            Arrange::PlaceOverwrite(gArrange, gArrange.lanes[targetLane].id, c, &made);
         }
      });

      if (made != 0)
      {
         gArrangePanelOpen = true;
         gArrangeSel = { made };
         gArrangeSelAnchor = made;
         gArrangeRevealClipId = made;
         gArrangeRevealFrames = 3;
         gArrangeFlashClipId = made;
         gArrangeFlashStart = ImGui::GetTime();
         gArrangeClipSettingsPanelOpen = true;
         gArrangeSettingsPanelTarget = made;
      }
      return made;
   }


   // Points clip `clipId` at node `uid` (the canvas "Assign Node..." picker
   // and the fixtures). The node must fit the clip's lane type; the output
   // is that lane type's first matching one. One undo entry, none if the
   // clip already had exactly this source.
   bool ArrangeAssignClipSource(uint64_t clipId, uint64_t uid)
   {
      const Arrange::Loc loc = Arrange::Find(gArrange, clipId);
      GraphNode* gn = FindNodeByUid(uid);
      if (!loc.Valid() || gn == nullptr || gn->node == nullptr)
         return false;
      const int laneType = gArrange.lanes[loc.lane].type;
      if (laneType == Arrange::kLaneAudio ? !IsNodeAudioCompatible(*gn) : !IsNodeVideoCompatible(*gn))
         return false;
      const int out = ArrangeDefaultOutput(*gn, laneType);
      return ArrangeEdit([&]()
      {
         Arrange::Clip* c = Arrange::FindClip(gArrange, clipId);
         // An Audio/Video Sample owns the private node its own drag-drop
         // import created - repointing it at another node would defeat the
         // whole point of a sample (see sampleDropped's doc comment in
         // ArrangeModel.h). Only Audio/Video Clip can be reassigned.
         if (c == nullptr || c->sampleDropped || (c->srcUid == uid && c->srcOutput == out))
            return;
         c->srcUid = uid;
         c->srcOutput = out;
         gArrange.revision++;
      });
   }
}
