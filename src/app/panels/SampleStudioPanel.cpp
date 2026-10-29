// Sample studio: the website's studio as a panel docked bottom or top, like the Timeline (SAMPLE-STUDIO-DESIGN.md).
// A toolbar (generate, instrument, snap to scale, bars; play, loop, ping-pong, reverse; export, convert) over two
// areas: on the left the blob, moving with the take; on the right the take's waveform with its start and end handles
// and the transport's playhead. The settings inspector (pitch, fine, speed, fades, style, wild, key and scale) opens
// on the right like the Timeline's clip inspector. The take plays on the global transport through StudioLoopPlayer;
// its play button switches audio to Studio mode, which bypasses the canvas the way Timeline audio does.
// The engine is a private Generate Samples node (never in the graph, never cooked): a take made here and one made
// on the node are the same sound for the same recipe, role critic included. Snap to scale re-renders the take's own
// seed pinned, so the instrument never swaps under the hand. Session state only: nothing here is saved in a patch.
#include "app/AppShared.h"
#include "app/graph/GenerateSamplesNode.h"
#include "app/graph/NodeClipboard.h"
#include "app/graph/SampleStyles.h"
#include "app/ui/design/GlyphDraw.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/components/ActionButton.h"
#include "app/ui/design/components/ChipButton.h"
#include "app/ui/design/components/DropdownField.h"
#include "app/ui/design/components/FieldWell.h"
#include "app/ui/design/components/FormParts.h"
#include "app/ui/design/components/MenuParts.h"
#include "app/ui/design/components/PanelFrame.h"
#include "app/ui/design/components/SectionCard.h"
#include "audio/AudioEngine.h"
#include "audio/AudioFileWriter.h"
#include "audio/MusicTime.h"
#include "core/GLUtil.h"
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
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>

namespace app
{
namespace
{
   constexpr float kMinHeight = 180.0f;
   constexpr float kSettingsW = 244.0f;
   constexpr double kRate = GenerateSamplesNode::kLoopRate;

   using P = SampleStyles::Part;
   const P kParts[] = { P::Synths, P::Keys, P::Leads, P::Pads, P::Plucks, P::Textures, P::Bass, P::Drums,
                        P::Kicks, P::Snares, P::Hats, P::Toms, P::Percussion, P::Fx, P::Foley };
   const int kBars[] = { 1, 2, 4, 8 };

   struct Take
   {
      SampleStyles::Recipe recipe; // as made: the seed it landed on
      int card = 0;                // style index (SampleStyles::At)
      std::vector<float> l, r;
   };

   struct Studio
   {
      std::unique_ptr<GenerateSamplesNode> gen;
      std::optional<Take> take;
      // Recipe.
      int style = -1; // -1: drawn from the seed, as the node does
      int part = 0;   // index into kParts
      int barsIdx = 1;
      float variation = 0.5f;
      bool followScale = false;
      // Playback shaping, applied to what plays and what is exported. Loop, ping-pong and reverse behave as on the
      // Sampler: all off plays the take once, ping-pong bounces only while looping.
      int trimFrom = 0, trimTo = 0; // 16ths of the take
      float pitch = 0.0f, fine = 0.0f, speed = 1.0f, fadeInMs = 0.0f, fadeOutMs = 0.0f;
      bool loop = true, reverse = false, pingPong = false;
      bool settingsOpen = false;
      // Engine.
      bool busy = false, busyPinned = false;
      int busyCard = 0;
      // The take as heard (main-thread copy, for the blob's bands and for export) and what was handed to the player.
      std::vector<float> outL, outR;
      bool dirty = false;
      int dragHandle = -1; // 0 start, 1 end
      // Waveform peaks cache.
      std::vector<float> peakLo, peakHi;
      const void* peakFor = nullptr;
      int peakCols = 0;
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

   int Sixteenths(const Take& t) { return std::max(1, t.recipe.bars * 16); }

   bool BouncesNow() { return S().loop && S().pingPong; }

   // ---- the take as heard ----

   // Pitch without changing the length: Hann grains at 4x overlap, each read at the pitch ratio around its own centre.
   // The take is a loop, so reads past either end wrap.
   void PitchShift(std::vector<float>& x, float semis)
   {
      const int n = (int)x.size();
      if (std::fabs(semis) < 0.005f || n < 64)
         return;
      const double ratio = std::pow(2.0, (double)semis / 12.0);
      const int N = 2048, H = N / 4;
      std::vector<float> y((size_t)n, 0.0f), wsum((size_t)n, 0.0f);
      for (int t0 = -N; t0 < n; t0 += H)
         for (int j = 0; j < N; j++)
         {
            const int o = t0 + j;
            if (o < 0 || o >= n)
               continue;
            const float w = 0.5f - 0.5f * std::cos(6.2831853f * (float)j / (float)N);
            double sp = std::fmod((double)t0 + N * 0.5 + ((double)j - N * 0.5) * ratio, (double)n);
            if (sp < 0.0)
               sp += n;
            const int i0 = (int)sp;
            const float f = (float)(sp - i0);
            const float v = x[(size_t)i0] + (x[(size_t)((i0 + 1) % n)] - x[(size_t)i0]) * f;
            y[(size_t)o] += w * v;
            wsum[(size_t)o] += w;
         }
      for (int i = 0; i < n; i++)
         x[(size_t)i] = wsum[(size_t)i] > 1e-3f ? y[(size_t)i] / wsum[(size_t)i] : 0.0f;
   }

   void Varispeed(std::vector<float>& x, float speed)
   {
      if (std::fabs(speed - 1.0f) < 1e-4f || x.size() < 2)
         return;
      const int n = (int)x.size(), m = std::max(2, (int)std::lround((double)n / speed));
      std::vector<float> y((size_t)m);
      for (int i = 0; i < m; i++)
      {
         const double sp = (double)i * (double)(n - 1) / (double)(m - 1);
         const int i0 = (int)sp;
         const int i1 = std::min(i0 + 1, n - 1);
         y[(size_t)i] = x[(size_t)i0] + (x[(size_t)i1] - x[(size_t)i0]) * (float)(sp - i0);
      }
      x.swap(y);
   }

   // Trim, pitch and fine, speed, reverse, ping-pong, fades: in that order. Returns the beats it spans.
   double Processed(const Take& t, std::vector<float>& l, std::vector<float>& r)
   {
      const Studio& s = S();
      const int frames = (int)t.l.size();
      const int n16 = Sixteenths(t);
      const int a = (int)((int64_t)frames * s.trimFrom / n16), b = (int)((int64_t)frames * s.trimTo / n16);
      l.assign(t.l.begin() + a, t.l.begin() + std::max(a, b));
      r.assign(t.r.begin() + a, t.r.begin() + std::max(a, b));
      const float semis = s.pitch + s.fine / 100.0f;
      PitchShift(l, semis);
      PitchShift(r, semis);
      Varispeed(l, s.speed);
      Varispeed(r, s.speed);
      if (s.reverse)
      {
         std::reverse(l.begin(), l.end());
         std::reverse(r.begin(), r.end());
      }
      double beats = (double)(s.trimTo - s.trimFrom) / 4.0 / std::max(0.05f, s.speed);
      if (BouncesNow() && !l.empty())
      {
         l.insert(l.end(), l.rbegin(), l.rend());
         r.insert(r.end(), r.rbegin(), r.rend());
         beats *= 2.0;
      }
      const auto ramp = [&](float ms, bool in)
      {
         const int f = std::min((int)l.size() / 2, (int)(ms * 0.001f * (float)kRate));
         for (int i = 0; i < f; i++)
         {
            const float g = (float)i / (float)f;
            const size_t k = in ? (size_t)i : l.size() - 1 - (size_t)i;
            l[k] *= g;
            r[k] *= g;
         }
      };
      ramp(s.fadeInMs, true);
      ramp(s.fadeOutMs, false);
      return beats;
   }

   // Hands the take as heard to the transport-synced player.
   void Rebuild()
   {
      Studio& s = S();
      s.dirty = false;
      StudioLoopPlayer& player = AudioEngine::Instance().StudioLoop();
      auto* buf = new Platform::SampleBuffer();
      buf->channels = 2;
      buf->sampleRate = kRate;
      double beats = 4.0;
      if (s.take)
      {
         beats = Processed(*s.take, s.outL, s.outR);
         buf->numFrames = (int)s.outL.size();
         buf->channelData.resize(s.outL.size() * 2);
         std::copy(s.outL.begin(), s.outL.end(), buf->channelData.begin());
         std::copy(s.outR.begin(), s.outR.end(), buf->channelData.begin() + (std::ptrdiff_t)s.outL.size());
      }
      player.Set(buf, beats, s.loop);
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
      g.variation = s.variation;
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
         return;
      Take t;
      t.recipe = g.MadeRecipe();
      t.card = s.busyCard;
      t.l = g.LoopL();
      t.r = g.LoopR();
      const bool keepTrim = s.busyPinned && s.take && Sixteenths(*s.take) == Sixteenths(t);
      s.take = std::move(t);
      if (!keepTrim)
      {
         s.trimFrom = 0;
         s.trimTo = Sixteenths(*s.take);
      }
      s.peakFor = nullptr;
      Rebuild();
      AudioEngine::Instance().StudioLoop().Restart(); // a one-shot plays the new take from its start
   }

   // ---- out of the studio ----

   std::string FileSafe(std::string name)
   {
      for (char& ch : name)
         ch = std::isalnum((unsigned char)ch) ? ch : ch == '#' ? 's' : '-';
      name.erase(std::unique(name.begin(), name.end(), [](char a, char b) { return a == '-' && b == '-'; }), name.end());
      return name;
   }

   std::string TakeName()
   {
      const Studio& s = S();
      if (!s.take)
         return "generated";
      return FileSafe(std::string("generated ") + SampleStyles::At(s.take->card).name + " " + SampleStyles::PartName(s.take->recipe.part));
   }

   // Writes the take as heard; an empty path picks one in the Recordings folder. Returns the path, or "" on failure.
   std::string WriteTake(std::string path, const char* ext, AudioFileWriter::Format fmt, int bits)
   {
      Studio& s = S();
      if (!s.take)
         return std::string();
      if (path.empty())
         path = AudioRecordings::GenerateFilePath(TakeName(), ext);
      std::vector<float> l, r;
      Processed(*s.take, l, r);
      std::vector<float> inter(l.size() * 2);
      for (size_t i = 0; i < l.size(); i++)
      {
         inter[i * 2] = l[i];
         inter[i * 2 + 1] = r[i];
      }
      AudioFileWriter w;
      if (!w.Open(path, kRate, 2, fmt, bits))
         return std::string();
      w.Append(inter.data(), (int)l.size());
      w.Close();
      return path;
   }

   void Export(const char* ext, AudioFileWriter::Format fmt, int bits)
   {
      const std::string path = WriteTake(std::string(), ext, fmt, bits);
      if (!path.empty())
         Platform::RevealInFileManager(path);
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

   // A node that plays the take: Generate Samples keeps the recipe (it re-renders the same take), the others load
   // the take as heard from a .wav in the Recordings folder.
   void ConvertToNode(const char* type)
   {
      Studio& s = S();
      if (!s.take)
         return;
      const bool generator = std::strcmp(type, "Generate Samples") == 0;
      const std::string wav = generator ? std::string() : WriteTake(std::string(), "wav", AudioFileWriter::Format::Wav, 24);
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
            const Take& t = *s.take;
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
            const float n16 = (float)Sixteenths(t);
            n->start = (float)s.trimFrom / n16;
            n->end = (float)s.trimTo / n16;
         }
      }
      else if (auto* n = dynamic_cast<SamplerNode*>(node))
         n->LoadFile(wav);
      else if (auto* n = dynamic_cast<SlicerNode*>(node))
         n->LoadFile(wav);
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

   // Play: the take on the transport, with audio handed to the studio the way Timeline audio takes it from the canvas.
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

   // ---- the blob ----

   const char* const kBlobFs =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform vec2 uRes; uniform float uT, uE, uBusy; uniform float uB[16]; uniform vec3 uA, uS;\n"
      "uniform float uL, uTone, uHarsh, uSpace, uMood; uniform vec3 uP[4], uC[4];\n"
      "float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }\n"
      "float noise(vec2 p){ vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);\n"
      "  return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y); }\n"
      "float fbm(vec2 p){ float v = 0.0, a = 0.5; for (int i = 0; i < 4; i++) { v += a * noise(p); p = p * 2.03 + 7.1; a *= 0.5; } return v; }\n"
      "float band(float m){ float s = 0.0, w = 0.0; for (int i = 0; i < 16; i++) { float d = m * 15.0 - float(i); float k = exp(-d * d * 0.35); s += uB[i] * k; w += k; } return s / w; }\n"
      "void main(){\n"
      "  vec2 p = (gl_FragCoord.xy - 0.5 * uRes) / min(uRes.x, uRes.y) * 2.0;\n"
      "  float t = uT, a = atan(p.y, p.x), r = length(p);\n"
      "  vec2 q = vec2(fbm(p * 1.3 + t * 0.12), fbm(p * 1.3 - t * 0.1 + 4.0));\n"
      "  float wob = (fbm(vec2(cos(a), sin(a)) * 1.4 + t * 0.18) - 0.5) * (0.26 + 0.16 * uBusy);\n"
      "  wob += uHarsh * ((noise(vec2(a * 5.0 + t * 1.6, t * 0.9)) - 0.5) * 0.24 + 0.05 * sin(a * 21.0 + t * 5.0));\n"
      "  vec2 n = p / max(r, 1e-4); float lean = 0.0; vec3 tint = vec3(0.0); float tw = 0.0;\n"
      "  for (int i = 0; i < 4; i++) { float k = pow(max(dot(n, normalize(uP[i].xy + 1e-4)), 0.0), 5.0); lean += uP[i].z * k; float pw = max(uP[i].z, 0.0) * k; tint += uC[i] * pw; tw += pw; }\n"
      "  float R = 0.62 + 0.17 * lean + wob + 0.24 * band(abs(a) / 3.14159) + 0.1 * uE + 0.04 * uBusy * sin(t * 5.0);\n"
      "  float d = r - R;\n"
      "  float soft = mix(1.0, 0.3, uHarsh) * (1.0 + 0.6 * max(uSpace, 0.0));\n"
      "  float body = smoothstep(0.2 * soft, -0.22 * soft, d);\n"
      "  float glow = exp(-max(d, 0.0) * mix(4.2, 1.8, uSpace * 0.5 + 0.5)) * (0.5 + 0.2 * uSpace) * smoothstep(1.0, 0.55, r) * smoothstep(1.0, 0.6, abs(p.y));\n"
      "  float f = fbm(p * 1.1 + q * 1.6 + t * 0.07);\n"
      "  float g = clamp(f * 1.5 + p.y * 0.35 - p.x * 0.2 + 0.25 * uE + 0.2 * uTone, 0.0, 1.0);\n"
      "  g = mix(g, smoothstep(0.35, 0.65, fract(g * 2.5 + t * 0.15)) * 0.5 + g * 0.5, uHarsh * 0.6);\n"
      "  vec3 A = mix(uA, vec3(1.0, 0.66, 0.3), max(uMood, 0.0) * 0.55); A = mix(A, vec3(0.45, 0.38, 0.95), max(-uMood, 0.0) * 0.6);\n"
      "  vec3 Sg = mix(uS, vec3(0.32, 0.5, 1.0), max(-uMood, 0.0) * 0.4);\n"
      "  vec3 hot = mix(A, mix(vec3(1.0, 0.82, 0.62), vec3(1.0, 0.3, 0.16), uHarsh), 0.55);\n"
      "  float w = mix(0.45, 0.1, uHarsh);\n"
      "  vec3 c = mix(Sg, A, smoothstep(0.55 - w, 0.55, g)); c = mix(c, hot, smoothstep(1.0 - w, 1.0, g));\n"
      "  c *= mix(0.66, 1.08, uTone * 0.5 + 0.5);\n"
      "  vec3 cl = mix(uS * 0.8, vec3(0.5, 0.32, 0.88), smoothstep(0.1, 0.55, g)); cl = mix(cl, vec3(0.9, 0.5, 0.82), smoothstep(0.55, 1.0, g));\n"
      "  c = mix(c, cl, uL);\n"
      "  c = mix(c, vec3(1.0), mix(0.28, 0.1, uL) * (0.6 + 0.6 * max(uTone, 0.0)) * smoothstep(0.6, 0.0, length(p - vec2(-0.18, 0.2))) * body);\n"
      "  c = mix(c, tint / max(tw, 1e-4), clamp(tw, 0.0, 1.0) * 0.6 * smoothstep(-0.45, 0.05, d));\n"
      "  float edge = smoothstep(-0.5, 0.2, d) * body; c = mix(c, uS, edge * 0.45);\n"
      "  float al = clamp(body * 0.94 + glow * (1.0 - body), 0.0, 1.0);\n"
      "  vec3 col = mix(mix(mix(uS, uA, 0.5), tint / max(tw, 1e-4), clamp(tw, 0.0, 1.0) * 0.7), c, body);\n"
      "  col += (hash(gl_FragCoord.xy + t) - 0.5) * (1.0 / 120.0 + 0.07 * uHarsh * body);\n"
      "  fragColor = vec4(col, al);\n"
      "}\n";

   struct Blob
   {
      GLUtil::Fbo fbo;
      unsigned int prog = 0;
      bool tried = false;
      int uRes = -1, uT = -1, uE = -1, uBusy = -1, uB = -1, uA = -1, uS = -1, uL = -1, uTone = -1, uHarsh = -1, uSpace = -1,
          uMood = -1, uP = -1, uC = -1;
      float t = 0.0f, busyK = 0.0f, energy = 0.0f;
      float bands[16] = {};
   };

   Blob& B()
   {
      static Blob b;
      if (!b.tried)
      {
         b.tried = true;
         b.prog = GLUtil::CompileProgram(kBlobFs);
         if (b.prog != 0)
         {
            const auto loc = [&](const char* n) { return glGetUniformLocation(b.prog, n); };
            b.uRes = loc("uRes"); b.uT = loc("uT"); b.uE = loc("uE"); b.uBusy = loc("uBusy"); b.uB = loc("uB");
            b.uA = loc("uA"); b.uS = loc("uS"); b.uL = loc("uL"); b.uTone = loc("uTone"); b.uHarsh = loc("uHarsh");
            b.uSpace = loc("uSpace"); b.uMood = loc("uMood"); b.uP = loc("uP"); b.uC = loc("uC");
         }
      }
      return b;
   }

   // Sixteen bands of the take as heard, at the playhead: a short windowed DFT at log-spaced frequencies.
   void TakeBands(float out[16])
   {
      const Studio& s = S();
      for (int i = 0; i < 16; i++)
         out[i] = 0.0f;
      const float phase = AudioEngine::Instance().StudioLoop().Phase();
      const int n = (int)s.outL.size();
      constexpr int W = 512;
      if (phase < 0.0f || n < W)
         return;
      const int at = std::clamp((int)(phase * (float)n), 0, n - W);
      for (int b = 0; b < 16; b++)
      {
         const double hz = 60.0 * std::pow(200.0, (double)b / 15.0); // 60 Hz .. 12 kHz
         const double w = 6.283185307 * hz / kRate;
         double re = 0.0, im = 0.0;
         for (int i = 0; i < W; i++)
         {
            const double win = 0.5 - 0.5 * std::cos(6.283185307 * i / W);
            const double v = 0.5 * (s.outL[(size_t)(at + i)] + s.outR[(size_t)(at + i)]) * win;
            re += v * std::cos(w * i);
            im -= v * std::sin(w * i);
         }
         out[b] = std::clamp((float)(std::sqrt(re * re + im * im) / (W * 0.25)) * 4.0f, 0.0f, 1.0f);
      }
   }

   ImVec4 Rgb(const tok::Rgba8& c) { return ImGui::ColorConvertU32ToFloat4(tok::U32(c)); }

   // The blob, filling a square: it breathes with the take's bands at the playhead and quickens while a take is made.
   void DrawBlob(ImVec2 mn, float side)
   {
      Studio& s = S();
      Blob& b = B();
      ImDrawList* dl = ImGui::GetWindowDrawList();

      // Smoothing, as the website does it.
      float bandsNow[16];
      TakeBands(bandsNow);
      float sum = 0.0f;
      for (int i = 0; i < 16; i++)
      {
         b.bands[i] += (bandsNow[i] - b.bands[i]) * 0.3f;
         sum += b.bands[i];
      }
      b.energy += (sum / 16.0f - b.energy) * 0.2f;
      b.busyK += ((s.busy ? 1.0f : 0.0f) - b.busyK) * 0.08f;
      b.t += 0.016f + 0.03f * b.busyK;

      const ImVec2 fbs = ImGui::GetIO().DisplayFramebufferScale;
      const int px = std::max(8, (int)(side * fbs.x)), py = std::max(8, (int)(side * fbs.y));
      if (b.prog == 0 || !GLUtil::EnsureFbo(b.fbo, px, py))
         return;
      const ImVec4 a = Rgb(tok::pal::c_F97316FF), sg = Rgb(tok::pal::c_7890E6FF);
      const float zero[12] = {};
      const GLboolean blendWas = glIsEnabled(GL_BLEND);
      GLUtil::RunShaderPass(b.fbo, b.prog, [&]()
      {
         glDisable(GL_BLEND);
         glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
         glClear(GL_COLOR_BUFFER_BIT);
         glUniform2f(b.uRes, (float)px, (float)py);
         glUniform1f(b.uT, b.t);
         glUniform1f(b.uE, b.energy);
         glUniform1f(b.uBusy, b.busyK);
         glUniform1fv(b.uB, 16, b.bands);
         glUniform3f(b.uA, a.x, a.y, a.z);
         glUniform3f(b.uS, sg.x, sg.y, sg.z);
         glUniform1f(b.uL, CategoryColors::IsThemeLight() ? 1.0f : 0.0f);
         glUniform1f(b.uTone, 0.0f);
         glUniform1f(b.uHarsh, 0.0f);
         glUniform1f(b.uSpace, 0.0f);
         glUniform1f(b.uMood, 0.0f);
         glUniform3fv(b.uP, 4, zero);
         glUniform3fv(b.uC, 4, zero);
      });
      if (blendWas)
         glEnable(GL_BLEND);
      dl->AddImage((ImTextureID)(intptr_t)b.fbo.tex, mn, ImVec2(mn.x + side, mn.y + side), ImVec2(0, 1), ImVec2(1, 0));
   }

   // ---- the waveform ----

   void DrawWave(ImVec2 mn, ImVec2 size)
   {
      Studio& s = S();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 mx(mn.x + size.x, mn.y + size.y);
      ImGui::SetCursorScreenPos(mn);
      ImGui::InvisibleButton("##studioWave", size);
      FieldWell::Draw(dl, mn, mx, false, false);
      const float mid = (mn.y + mx.y) * 0.5f;
      if (s.busy)
      {
         // Progress along the bottom edge.
         const float f = std::clamp(s.gen->Progress(), 0.0f, 1.0f);
         dl->AddRectFilled(ImVec2(mn.x, mx.y - 2.0f), ImVec2(mn.x + size.x * f, mx.y), ImGui::GetColorU32(AccentEmphasisSelected()));
      }
      if (!s.take || s.take->l.empty())
      {
         dl->AddLine(ImVec2(mn.x + tok::space_2, mid), ImVec2(mx.x - tok::space_2, mid), ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.25f));
         return;
      }
      const Take& t = *s.take;
      const int n16 = Sixteenths(t);
      const float w = size.x, h = size.y;
      const auto xOf = [&](float six) { return mn.x + w * six / (float)n16; };

      // Handles: the nearer one follows the mouse, snapped to 16ths.
      if (ImGui::IsItemActivated())
      {
         const float mxp = ImGui::GetIO().MousePos.x;
         s.dragHandle = std::fabs(mxp - xOf((float)s.trimFrom)) <= std::fabs(mxp - xOf((float)s.trimTo)) ? 0 : 1;
      }
      if (ImGui::IsItemActive() && s.dragHandle >= 0)
      {
         const int six = std::clamp((int)std::lround((ImGui::GetIO().MousePos.x - mn.x) / w * (float)n16), 0, n16);
         const int from = s.trimFrom, to = s.trimTo;
         if (s.dragHandle == 0)
            s.trimFrom = std::min(six, s.trimTo - 1);
         else
            s.trimTo = std::max(six, s.trimFrom + 1);
         if (from != s.trimFrom || to != s.trimTo)
            s.dirty = true;
      }
      if (ImGui::IsItemDeactivated())
         s.dragHandle = -1;
      if (ImGui::IsItemHovered() || ImGui::IsItemActive())
         ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

      // 16th grid, beats stronger.
      for (int i = 1; i < n16; i++)
         dl->AddLine(ImVec2(xOf((float)i), mn.y + 2.0f), ImVec2(xOf((float)i), mx.y - 2.0f),
                     ImGui::GetColorU32(ImGuiCol_TextDisabled, i % 4 == 0 ? 0.22f : 0.07f));
      // Peaks, one column per pixel, cached per take and width.
      const int cols = std::max(1, (int)w);
      const int frames = (int)t.l.size();
      if (s.peakFor != (const void*)t.l.data() || s.peakCols != cols)
      {
         s.peakFor = t.l.data();
         s.peakCols = cols;
         s.peakLo.assign((size_t)cols, 0.0f);
         s.peakHi.assign((size_t)cols, 0.0f);
         for (int x = 0; x < cols; x++)
         {
            const int a = (int)((int64_t)frames * x / cols), b = std::max(a + 1, (int)((int64_t)frames * (x + 1) / cols));
            float lo = 0.0f, hi = 0.0f;
            for (int i = a; i < b && i < frames; i++)
            {
               const float v = 0.5f * (t.l[(size_t)i] + t.r[(size_t)i]);
               lo = std::min(lo, v);
               hi = std::max(hi, v);
            }
            s.peakLo[(size_t)x] = lo;
            s.peakHi[(size_t)x] = hi;
         }
      }
      const float amp = h * 0.46f;
      const ImU32 inCol = ImGui::GetColorU32(ImGuiCol_Text, 0.75f), outCol = ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.35f);
      const float xa = xOf((float)s.trimFrom), xb = xOf((float)s.trimTo);
      for (int x = 0; x < cols; x++)
      {
         const float px = mn.x + (float)x + 0.5f;
         dl->AddLine(ImVec2(px, mid - s.peakHi[(size_t)x] * amp), ImVec2(px, mid - s.peakLo[(size_t)x] * amp + 1.0f),
                     px >= xa && px < xb ? inCol : outCol);
      }
      // Start and end handles: a line with a tab at the top.
      const ImU32 acc = ImGui::GetColorU32(AccentEmphasisSelected());
      for (int k = 0; k < 2; k++)
      {
         const float x = k == 0 ? xa : xb;
         dl->AddLine(ImVec2(x, mn.y), ImVec2(x, mx.y), acc, 2.0f);
         const float tw = 7.0f;
         dl->AddTriangleFilled(ImVec2(x, mn.y), ImVec2(k == 0 ? x + tw : x - tw, mn.y), ImVec2(x, mn.y + tw), acc);
      }
      // Playhead, from the transport's position inside the take as heard, mapped back onto the original.
      const float u = AudioEngine::Instance().StudioLoop().Phase();
      if (u >= 0.0f)
      {
         float k = u;
         if (BouncesNow())
            k = k < 0.5f ? k * 2.0f : 2.0f - k * 2.0f;
         if (s.reverse)
            k = 1.0f - k;
         const float x = xa + (xb - xa) * k;
         dl->AddLine(ImVec2(x, mn.y), ImVec2(x, mx.y), ImGui::GetColorU32(ImGuiCol_Text), 1.0f);
      }
   }

   // ---- the settings inspector ----

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

      SectionCard::Title(T("Sample"));
      FormParts::Inline(availW - 16.0f + ImGui::GetStyle().WindowPadding.x);
      {
         const float sz = 16.0f;
         if (ImGui::InvisibleButton("##closestudiosettings", ImVec2(sz, sz)))
            s.settingsOpen = false;
         const ImVec2 bmin = ImGui::GetItemRectMin(), bmax = ImGui::GetItemRectMax();
         glyph::DrawX(ImGui::GetWindowDrawList(), ImVec2((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f), sz * 0.65f,
                      ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGuiCol_Text : ImGuiCol_TextDisabled));
      }

      const auto edited = [&]() { if (ImGui::IsItemEdited()) s.dirty = true; };

      SectionCard::Begin(T("Pitch"));
      SectionCard::RowLabel(T("Pitch"));
      ImGui::SetNextItemWidth(fieldW);
      FieldWell::Slider("##studiopitch", &s.pitch, -24.0f, 24.0f, "%+.0f st");
      if (ImGui::IsItemEdited())
      {
         s.pitch = std::round(s.pitch);
         s.dirty = true;
      }
      SectionCard::RowLabel(T("Fine"));
      ImGui::SetNextItemWidth(fieldW);
      FieldWell::Slider("##studiofine", &s.fine, -100.0f, 100.0f, "%+.0f ct");
      edited();

      SectionCard::Begin(T("Speed"));
      SectionCard::RowLabel(T("Speed"));
      ImGui::SetNextItemWidth(fieldW);
      FieldWell::Slider("##studiospeed", &s.speed, 0.25f, 4.0f, "%.2fx", ImGuiSliderFlags_Logarithmic);
      edited();

      SectionCard::Begin(T("Fade"));
      SectionCard::RowLabel(T("In"));
      ImGui::SetNextItemWidth(fieldW);
      FieldWell::Slider("##studiofadein", &s.fadeInMs, 0.0f, 1000.0f, "%.0f ms");
      edited();
      SectionCard::RowLabel(T("Out"));
      ImGui::SetNextItemWidth(fieldW);
      FieldWell::Slider("##studiofadeout", &s.fadeOutMs, 0.0f, 1000.0f, "%.0f ms");
      edited();

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
         SectionCard::RowLabel(T("Wild"));
         ImGui::SetNextItemWidth(fieldW);
         FieldWell::Slider("##studiowild", &s.variation, 0.0f, 1.0f, "%.2f");
      }

      // The project's key and scale, which "snap to scale" follows (the same values as the top bar's key).
      SectionCard::Begin(T("Scale"));
      {
         Transport& tr = Transport::Instance();
         static const char* const keys[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
         const std::vector<std::string>& scales = MusicTime::ScaleTypeList();
         ImGui::BeginDisabled(!s.followScale);
         SectionCard::RowLabel(T("Key"));
         ImGui::SetNextItemWidth(fieldW);
         if (MenuParts::BeginCombo("##studiokey", keys[std::clamp(tr.Key(), 0, 11)]))
         {
            for (int i = 0; i < 12; i++)
               if (MenuParts::Choice(keys[i], i == tr.Key()))
                  tr.SetKey(i);
            MenuParts::EndCombo();
         }
         SectionCard::RowLabel(T("Scale"));
         ImGui::SetNextItemWidth(fieldW);
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

   void DrawToolbar(float w)
   {
      Studio& s = S();
      const bool has = s.take.has_value();
      const float x0 = ImGui::GetCursorScreenPos().x;
      const float in = tok::space_1, between = tok::space_3;

      // Make: generate (again), instrument, snap to scale, bars.
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
         if (has && !s.busy)
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
                   playing ? T("Stop") : T("Play the take (pauses the canvas audio)"), true))
         TogglePlay();
      ImGui::EndDisabled();
      const auto toggle = [&](const char* label, bool& v, const char* tip)
      {
         FormParts::Inline(0.0f, in);
         if (AudioToggleButton(label, &v, TextW(label), tok::tile))
         {
            s.dirty = true;
            AudioEngine::Instance().StudioLoop().SetLoop(s.loop);
            AudioEngine::Instance().StudioLoop().Restart();
         }
         if (ImGui::IsItemHovered())
            HelpTip("%s", tip);
      };
      toggle(L("Loop##studioLoop"), s.loop, T("Loop the take; off plays it once"));
      toggle(L("Ping-pong##studioPP"), s.pingPong, T("While looping, play forward then back"));
      toggle(L("Reverse##studioRev"), s.reverse, T("Play the take backwards"));

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
      if (TextChip(patchL))
         ConvertToPatch();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         HelpTip("%s", T("The graph that made the take, as a group on the canvas"));
      FormParts::Inline(0.0f, in);
      if (TextChip(nodeL))
         ImGui::OpenPopup("##studioNodeMenu");
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         HelpTip("%s", T("A sample node holding the take"));
      ImGui::EndDisabled();
      FormParts::Inline(0.0f, in);
      if (IconChip("##studioSettings", IconsInfinite::Settings, s.settingsOpen, T("Sample settings")))
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
            const std::string path = Platform::SaveWavDialog(TakeName() + ".wav");
            if (!path.empty())
               WriteTake(path, "wav", AudioFileWriter::Format::Wav, 24);
         }
         MenuParts::EndPopup();
      }
      if (MenuParts::BeginPopup("##studioNodeMenu"))
      {
         static const char* const types[] = { "Generate Samples", "Sampler", "Slicer", "Molder", "Grain Molder", "PaulStretch", "Granular" };
         for (const char* t : types)
         {
            if (MenuParts::Item(T(t)))
               ConvertToNode(t);
            if (t == types[0])
               MenuParts::Separator();
         }
         MenuParts::EndPopup();
      }
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
      // INFINITE_OPENPANELS=...,studiopatch: convert the first take to a patch, to review the layout.
      static bool sDemoPatch = [] { const char* op = getenv("INFINITE_OPENPANELS"); return op != nullptr && std::strstr(op, "studiopatch") != nullptr; }();
      if (sDemoPatch && s.take)
      {
         sDemoPatch = false;
         ConvertToPatch();
      }
      if (s.dirty && s.dragHandle < 0)
         Rebuild();
      StudioLoopPlayer& player = AudioEngine::Instance().StudioLoop();
      player.SetGain(gAudioMode == AudioMode::Studio ? 0.8f : 0.0f);
      // Loop off: the take played once, so the transport stops (as a one-shot does in the Sampler).
      if (!s.loop && player.Finished() && gAudioMode == AudioMode::Studio && Transport::Instance().IsPlaying())
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

      ImGui::Dummy(ImVec2(0.0f, tok::space_1));
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 avail = ImGui::GetContentRegionAvail();
      const float h = std::max(40.0f, avail.y);
      const float gap = ImGui::GetStyle().ItemSpacing.x;
      const float settingsW = s.settingsOpen ? kSettingsW + gap : 0.0f;
      DrawBlob(origin, h);
      DrawWave(ImVec2(origin.x + h + gap, origin.y), ImVec2(std::max(40.0f, avail.x - settingsW - h - gap), h));
      if (s.settingsOpen)
      {
         ImGui::SetCursorScreenPos(ImVec2(origin.x + avail.x - kSettingsW, origin.y));
         DrawSettings(kSettingsW);
      }
   }
}

void DrawSampleStudioDocked(const char* id, const ImVec2& size)
{
   const float kGrip = PanelFrame::kGap;
   const bool gripFirst = gSampleStudioDock != 3; // bottom: the grip sits on top
   ImGui::BeginChild(id, size, false);
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
