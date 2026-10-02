#include "ClipMatrixNode.h"

#include "platform/OpenGLHeaders.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <random>

#include "audio/AudioBuffer.h"
#include "audio/AudioEngine.h"
#include "audio/AudioNode.h"
#include "audio/DspMath.h"
#include "audio/MediaExtensions.h"
#include "audio/NoteEventQueue.h"
#include "audio/SampleSlot.h"
#include "audio/dsp/ClipTimeStretch.h"
#include "core/AudioDecodeCache.h"
#include "core/AudioTopologyRequest.h"
#include "core/BlendModes.h"
#include "core/Transport.h"

namespace
{
   constexpr int R = ClipMatrixNode::kMaxRows;
   constexpr int C = ClipMatrixNode::kMaxCols;
   constexpr double kFadeInSec = 0.002;
   constexpr double kFadeOutSec = 0.008;

   bool HasExt(const std::string& path, const std::vector<std::string>& exts)
   {
      const size_t dot = path.find_last_of('.');
      if (dot == std::string::npos)
         return false;
      std::string e = path.substr(dot + 1);
      for (char& ch : e)
         ch = (char)std::tolower((unsigned char)ch);
      return std::find(exts.begin(), exts.end(), e) != exts.end();
   }
}

ClipMatrixNode::BpmEstimator ClipMatrixNode::sEstimateBpm = nullptr;

const char* ClipMatrixNode::QuantName(int q)
{
   static const char* k[kQuantCount] = { "None", "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars" };
   return k[std::clamp(q, 0, kQuantCount - 1)];
}

double ClipMatrixNode::QuantBeats(int q, double bpb)
{
   switch (std::clamp(q, 0, kQuantCount - 1))
   {
      case 0: return 0.0;
      case 1: return 0.25;
      case 2: return 0.5;
      case 3: return 1.0;
      case 4: return 2.0;
      case 5: return bpb;
      case 6: return 2.0 * bpb;
      default: return 4.0 * bpb;
   }
}

const char* ClipMatrixNode::FollowName(int f)
{
   static const char* k[kFollowCount] = { "none", "stop", "again", "next", "previous", "first", "last", "any", "other" };
   return k[std::clamp(f, 0, kFollowCount - 1)];
}

// ===========================================================================
// Audio thread half
// ===========================================================================
class AudioClipMatrixNode : public AudioNode
{
public:
   enum CmdType { kCmdLaunch = 0, kCmdRelease, kCmdStopRow, kCmdScene, kCmdStopAll };
   struct Cmd
   {
      int type = 0, row = 0, col = 0;
   };
   struct NoteEv
   {
      int note = 0;
      bool on = false;
   };

   struct CellParams
   {
      std::atomic<bool> present { false };
      std::atomic<int> kind { 0 };
      std::atomic<int> mode { 0 };
      std::atomic<int> quant { -1 };
      std::atomic<float> gain { 1.0f };
      std::atomic<float> pitch { 0.0f };
      std::atomic<bool> sync { false };
      std::atomic<float> bpm { 120.0f };
      std::atomic<double> duration { 0.0 };
      std::atomic<bool> followOn { false };
      std::atomic<int> followA { 3 };
      std::atomic<int> followB { 2 };
      std::atomic<float> chanceB { 0.0f };
      std::atomic<float> followBars { 0.0f };
   };

   AudioClipMatrixNode()
   {
      mRowBuf.assign((size_t)R * 2 * kAudioMaxBlockFrames, 0.0f);
      for (int r = 0; r < R; r++)
      {
         mPubPlaying[r].store(-1);
         mPubQueued[r].store(-1);
         mRowGain[r].store(1.0f);
      }
   }

   void PrepareToPlay(double sampleRate, int maxBlockSize) override
   {
      mRate = sampleRate > 0.0 ? sampleRate : 48000.0;
      mStretchCap = std::max(mStretchCap, std::max(64, maxBlockSize));
      for (int r = 0; r < R; r++)
         for (int v = 0; v < 2; v++)
            mRows[r].voice[v].stretch.Prepare(mRate, mStretchCap);
   }

   void SetNoteInbox(NoteEventQueue* inbox, int cursor) override
   {
      mInbox = inbox;
      mCursor = cursor;
   }

   // ---- main thread ---------------------------------------------------------
   void PushBuffer(int r, int c, Platform::SampleBuffer* b) { mSlots[r][c].Push(b); }
   void DrainRetired()
   {
      for (auto& row : mSlots)
         for (auto& s : row)
            s.DrainRetired();
   }
   CellParams& Params(int r, int c) { return mParams[r][c]; }
   void SetScene(int c, float tempo, int num, int den)
   {
      mSceneTempo[c].store(tempo, std::memory_order_relaxed);
      mSceneNum[c].store(num, std::memory_order_relaxed);
      mSceneDen[c].store(den, std::memory_order_relaxed);
   }
   void SetGlobals(int rows, int cols, int quant, float volume)
   {
      mRowsN.store(std::clamp(rows, 1, R), std::memory_order_relaxed);
      mColsN.store(std::clamp(cols, 1, C), std::memory_order_relaxed);
      mQuant.store(quant, std::memory_order_relaxed);
      mVolume.store(volume, std::memory_order_relaxed);
   }
   void SetRowGain(int r, float g) { mRowGain[r].store(g, std::memory_order_relaxed); }

   void Push(const Cmd& c)
   {
      const uint32_t t = mCmdTail.load(std::memory_order_relaxed);
      const uint32_t n = (t + 1) % kCmdCap;
      if (n == mCmdHead.load(std::memory_order_acquire))
         return;
      mCmds[t] = c;
      mCmdTail.store(n, std::memory_order_release);
   }
   bool PopEvent(ClipMatrixNode::Event& e)
   {
      const uint32_t h = mEvHead.load(std::memory_order_relaxed);
      if (h == mEvTail.load(std::memory_order_acquire))
         return false;
      e = mEvents[h];
      mEvHead.store((h + 1) % kEvCap, std::memory_order_release);
      return true;
   }
   bool PopNote(NoteEv& e)
   {
      const uint32_t h = mNoteHead.load(std::memory_order_relaxed);
      if (h == mNoteTail.load(std::memory_order_acquire))
         return false;
      e = mNotes[h];
      mNoteHead.store((h + 1) % kNoteCap, std::memory_order_release);
      return true;
   }

   int PubPlaying(int r) const { return mPubPlaying[r].load(std::memory_order_relaxed); }
   int PubQueued(int r) const { return mPubQueued[r].load(std::memory_order_relaxed); }
   double PubPos(int r) const { return mPubPos[r].load(std::memory_order_relaxed); }
   double PubLaunch(int r) const { return mPubLaunch[r].load(std::memory_order_relaxed); }
   float PubPeak(int r) const { return mPubPeak[r].load(std::memory_order_relaxed); }

   // ---- Clip Matrix Out taps (audio thread, after this node's block) ------
   uint64_t BlockCounter() const { return mBlockCounter.load(std::memory_order_acquire); }
   int LastFrames() const { return mLastFrames; }
   const float* RowChannel(int r, int ch) const
   {
      return mRowBuf.data() + ((size_t)std::clamp(r, 0, R - 1) * 2 + (size_t)(ch & 1)) * kAudioMaxBlockFrames;
   }

   // ---- audio thread --------------------------------------------------------
   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      const int n = std::min(output.numFrames, kAudioMaxBlockFrames);
      for (int ch = 0; ch < output.numChannels; ch++)
         std::fill(output.channels[ch], output.channels[ch] + output.numFrames, 0.0f);
      std::fill(mRowBuf.begin(), mRowBuf.end(), 0.0f);

      ForwardNotes();
      for (int r = 0; r < R; r++)
         for (int c = 0; c < C; c++)
            if (mSlots[r][c].SwapIn())
               for (Voice& v : mRows[r].voice)
                  if (v.active && v.col == c)
                     v.active = false; // the buffer under it was replaced

      Transport& tr = Transport::Instance();
      const bool playing = tr.IsPlaying();
      const double bpm = std::max(1.0, (double)tr.Tempo());
      const double bps = bpm / 60.0 / mRate;
      const double blockStart = tr.Beats();
      const double blockEnd = blockStart + bps * (double)n;
      const double bpb = std::max(0.25, tr.BeatsPerBar());
      const int rows = mRowsN.load(std::memory_order_relaxed);

      // Commands: each becomes a pending action on a grid line.
      for (;;)
      {
         const uint32_t h = mCmdHead.load(std::memory_order_relaxed);
         if (h == mCmdTail.load(std::memory_order_acquire))
            break;
         const Cmd cmd = mCmds[h];
         mCmdHead.store((h + 1) % kCmdCap, std::memory_order_release);
         Apply(cmd, blockStart, bpb, playing, rows);
      }

      // Scene tempo / time signature, on the scene's grid line.
      if (mSceneApplyCol >= 0 && mSceneApplyBeat < blockEnd)
      {
         const float t = mSceneTempo[mSceneApplyCol].load(std::memory_order_relaxed);
         const int num = mSceneNum[mSceneApplyCol].load(std::memory_order_relaxed);
         const int den = mSceneDen[mSceneApplyCol].load(std::memory_order_relaxed);
         if (t > 0.0f)
            tr.SetTempo(t);
         if (num > 0)
            tr.SetTimeSignature(num, den > 0 ? den : 4);
         mSceneApplyCol = -1;
      }

      const float volume = mVolume.load(std::memory_order_relaxed);
      for (int r = 0; r < R; r++)
      {
         RowState& rs = mRows[r];
         float* L = mRowBuf.data() + ((size_t)r * 2) * kAudioMaxBlockFrames;
         float* Rt = mRowBuf.data() + ((size_t)r * 2 + 1) * kAudioMaxBlockFrames;

         if (playing)
         {
            // Follow action of the playing clip.
            Voice* cur = CurrentVoice(rs);
            if (cur != nullptr && !rs.pending && cur->followOn && !cur->followDone)
            {
               if (cur->followLeft <= (blockEnd - blockStart))
               {
                  cur->followDone = true;
                  FollowAction(r, *cur, blockStart + std::max(0.0, cur->followLeft), rows);
               }
               else
                  cur->followLeft -= (blockEnd - blockStart);
            }
            // A pending action whose grid line falls in this block.
            if (rs.pending && rs.pendingBeat < blockEnd)
            {
               const int at = std::clamp((int)((rs.pendingBeat - blockStart) / bps), 0, n - 1);
               const double beat = std::max(blockStart, rs.pendingBeat);
               rs.pending = false;
               if (Voice* old = CurrentVoice(rs))
                  StopVoice(r, *old, at, beat);
               if (rs.pendingCol >= 0)
                  StartVoice(r, rs.pendingCol, at, beat);
               rs.pendingCol = -1;
            }
         }
         else if (rs.pending && rs.pendingCol < 0)
         {
            // Stop while paused: now.
            rs.pending = false;
            if (Voice* old = CurrentVoice(rs))
               StopVoice(r, *old, 0, blockStart);
         }

         float peak = 0.0f;
         if (playing)
            for (Voice& v : rs.voice)
               if (v.active)
                  RenderVoice(r, v, L, Rt, n, blockStart, bps);
         const float g = mRowGain[r].load(std::memory_order_relaxed);
         for (int i = 0; i < n; i++)
         {
            L[i] *= g;
            Rt[i] *= g;
            peak = std::max(peak, std::max(std::fabs(L[i]), std::fabs(Rt[i])));
            if (output.numChannels > 0)
               output.channels[0][i] += L[i] * volume;
            if (output.numChannels > 1)
               output.channels[1][i] += Rt[i] * volume;
         }

         // Publish.
         Voice* cur = CurrentVoice(rs);
         mPubPlaying[r].store(cur != nullptr ? cur->col : -1, std::memory_order_relaxed);
         mPubQueued[r].store(rs.pending ? (rs.pendingCol >= 0 ? rs.pendingCol : -2) : -1, std::memory_order_relaxed);
         mPubPos[r].store(cur != nullptr ? cur->srcSec : 0.0, std::memory_order_relaxed);
         mPubLaunch[r].store(cur != nullptr ? cur->launchBeat : 0.0, std::memory_order_relaxed);
         mPubPeak[r].store(peak, std::memory_order_relaxed);
      }
      for (int ch = 2; ch < output.numChannels; ch++)
         for (int i = 0; i < n; i++)
            output.channels[ch][i] = output.channels[0][i];
      mLastFrames = n;
      mBlockCounter.fetch_add(1, std::memory_order_release);
   }

private:
   struct Voice
   {
      bool active = false;
      int col = -1;
      double launchBeat = 0.0;
      double srcSec = 0.0;      // position in the clip
      int startAt = 0;          // first frame this block (launch inside the block)
      int fadeOutAt = -1;       // frame where the fade-out starts (-1 none)
      float fade = 0.0f;
      bool stopping = false;
      bool stretchLive = false; // stretcher anchored
      bool followOn = false;
      bool followDone = false;
      double followLeft = 0.0;  // beats until the follow action
      ClipTimeStretch stretch;
   };
   struct RowState
   {
      Voice voice[2];
      int cur = -1; // index of the current (not stopping) voice
      bool pending = false;
      int pendingCol = -1; // -1 = stop
      double pendingBeat = 0.0;
   };

   Voice* CurrentVoice(RowState& rs)
   {
      if (rs.cur < 0)
         return nullptr;
      Voice& v = rs.voice[rs.cur];
      return (v.active && !v.stopping) ? &v : nullptr;
   }

   double GridTarget(double now, int quant, double bpb, bool playing)
   {
      const double q = ClipMatrixNode::QuantBeats(quant, bpb);
      if (!playing || q <= 0.0)
         return now;
      const double k = std::ceil(now / q - 1e-7);
      return std::max(now, k * q);
   }

   int CellQuant(int r, int c)
   {
      const int q = mParams[r][c].quant.load(std::memory_order_relaxed);
      return q >= 0 ? q : mQuant.load(std::memory_order_relaxed);
   }

   void Queue(int r, int col, double beat)
   {
      RowState& rs = mRows[r];
      rs.pending = true;
      rs.pendingCol = col;
      rs.pendingBeat = beat;
   }

   void Apply(const Cmd& cmd, double now, double bpb, bool playing, int rows)
   {
      const int r = std::clamp(cmd.row, 0, R - 1);
      const int c = std::clamp(cmd.col, 0, C - 1);
      switch (cmd.type)
      {
         case kCmdLaunch:
            if (!mParams[r][c].present.load(std::memory_order_relaxed))
               Queue(r, -1, GridTarget(now, mQuant.load(std::memory_order_relaxed), bpb, playing));
            else
               Queue(r, c, GridTarget(now, CellQuant(r, c), bpb, playing));
            break;
         case kCmdRelease:
         {
            RowState& rs = mRows[r];
            if (mParams[r][c].mode.load(std::memory_order_relaxed) != ClipMatrixNode::kGate)
               break;
            if (rs.pending && rs.pendingCol == c)
               rs.pending = false; // released before it started
            else if (Voice* v = CurrentVoice(rs); v != nullptr && v->col == c)
               StopVoice(r, *v, 0, now); // gates stop at once
            break;
         }
         case kCmdStopRow:
            Queue(r, -1, GridTarget(now, mQuant.load(std::memory_order_relaxed), bpb, playing));
            break;
         case kCmdScene:
         {
            const double t = GridTarget(now, mQuant.load(std::memory_order_relaxed), bpb, playing);
            for (int row = 0; row < rows; row++)
               Queue(row, mParams[row][c].present.load(std::memory_order_relaxed) ? c : -1, t);
            mSceneApplyCol = c;
            mSceneApplyBeat = t;
            break;
         }
         case kCmdStopAll:
         {
            const double t = GridTarget(now, mQuant.load(std::memory_order_relaxed), bpb, playing);
            for (int row = 0; row < R; row++)
               Queue(row, -1, t);
            break;
         }
         default:
            break;
      }
   }

   void PushEvent(int type, int r, int c, double beat, double src)
   {
      const uint32_t t = mEvTail.load(std::memory_order_relaxed);
      const uint32_t nx = (t + 1) % kEvCap;
      if (nx == mEvHead.load(std::memory_order_acquire))
         return;
      ClipMatrixNode::Event e;
      e.type = type;
      e.row = r;
      e.col = c;
      e.beat = beat;
      e.sourceSeconds = src;
      mEvents[t] = e;
      mEvTail.store(nx, std::memory_order_release);
   }

   void StartVoice(int r, int c, int at, double beat)
   {
      RowState& rs = mRows[r];
      // Take the voice that is not playing (or the quietest one).
      int slot = 0;
      if (rs.voice[0].active && !rs.voice[1].active)
         slot = 1;
      else if (rs.voice[0].active && rs.voice[1].active)
         slot = (rs.cur == 0) ? 1 : 0;
      Voice& v = rs.voice[slot];
      v.active = true;
      v.col = c;
      v.launchBeat = beat;
      v.srcSec = 0.0;
      v.startAt = at;
      v.fadeOutAt = -1;
      v.fade = 0.0f;
      v.stopping = false;
      v.stretchLive = false;
      const CellParams& p = mParams[r][c];
      v.followOn = p.followOn.load(std::memory_order_relaxed);
      v.followDone = false;
      const double bpb = std::max(0.25, Transport::Instance().BeatsPerBar());
      double fb = (double)p.followBars.load(std::memory_order_relaxed) * bpb;
      if (fb <= 0.0)
      {
         // The clip's own length, in beats at the tempo it plays at.
         const double dur = p.duration.load(std::memory_order_relaxed);
         const double eff = p.sync.load(std::memory_order_relaxed) ? (double)p.bpm.load(std::memory_order_relaxed)
                                                                   : std::max(1.0, (double)Transport::Instance().Tempo());
         fb = dur > 0.0 ? dur * eff / 60.0 : bpb;
      }
      v.followLeft = std::max(0.01, fb);
      rs.cur = slot;
      PushEvent(ClipMatrixNode::Event::kStart, r, c, beat, 0.0);
   }

   void StopVoice(int r, Voice& v, int at, double beat)
   {
      if (!v.active || v.stopping)
         return;
      v.stopping = true;
      v.fadeOutAt = at;
      PushEvent(ClipMatrixNode::Event::kStop, r, v.col, beat, v.srcSec);
   }

   int FindPresent(int r, int from, int step, int cols)
   {
      for (int k = 1; k <= cols; k++)
      {
         const int c = ((from + step * k) % cols + cols) % cols;
         if (mParams[r][c].present.load(std::memory_order_relaxed))
            return c;
      }
      return -1;
   }

   void FollowAction(int r, Voice& v, double beat, int /*rows*/)
   {
      const CellParams& p = mParams[r][v.col];
      const float chanceB = p.chanceB.load(std::memory_order_relaxed);
      std::uniform_real_distribution<float> u(0.0f, 1.0f);
      const int action = (u(mRng) < chanceB) ? p.followB.load(std::memory_order_relaxed)
                                             : p.followA.load(std::memory_order_relaxed);
      const int cols = mColsN.load(std::memory_order_relaxed);
      int target = -2; // -2 = nothing to do
      switch (action)
      {
         case ClipMatrixNode::kFollowStop: target = -1; break;
         case ClipMatrixNode::kFollowAgain: target = v.col; break;
         case ClipMatrixNode::kFollowNext: target = FindPresent(r, v.col, 1, cols); break;
         case ClipMatrixNode::kFollowPrev: target = FindPresent(r, v.col, -1, cols); break;
         case ClipMatrixNode::kFollowFirst: target = FindPresent(r, -1, 1, cols); break;
         case ClipMatrixNode::kFollowLast: target = FindPresent(r, cols, -1, cols); break;
         case ClipMatrixNode::kFollowAny:
         case ClipMatrixNode::kFollowOther:
         {
            int cand[C];
            int nC = 0;
            for (int c = 0; c < cols; c++)
               if (mParams[r][c].present.load(std::memory_order_relaxed) &&
                   (action == ClipMatrixNode::kFollowAny || c != v.col))
                  cand[nC++] = c;
            if (nC > 0)
               target = cand[std::uniform_int_distribution<int>(0, nC - 1)(mRng)];
            else if (action == ClipMatrixNode::kFollowOther)
               target = v.col;
            break;
         }
         default: break;
      }
      if (target == -2)
         return;
      Queue(r, target, beat);
   }

   void RenderVoice(int r, Voice& v, float* L, float* Rt, int n, double blockStart, double bps)
   {
      const CellParams& p = mParams[r][v.col];
      const Platform::SampleBuffer* buf = mSlots[r][v.col].Active();
      const int mode = p.mode.load(std::memory_order_relaxed);
      const bool loop = mode != ClipMatrixNode::kOnce;
      const float gain = p.gain.load(std::memory_order_relaxed);
      const double tempo = std::max(1.0, (double)Transport::Instance().Tempo());
      const bool sync = p.sync.load(std::memory_order_relaxed);
      const double eff = sync ? std::max(1.0, (double)p.bpm.load(std::memory_order_relaxed)) : tempo;
      const double rate = std::clamp(tempo / eff, 0.05, 20.0); // source seconds per real second
      const float pitch = p.pitch.load(std::memory_order_relaxed);
      double duration = p.duration.load(std::memory_order_relaxed);
      const bool hasBuf = buf != nullptr && buf->numFrames > 0 && buf->sampleRate > 0.0;
      if (hasBuf)
         duration = (double)buf->numFrames / buf->sampleRate;
      const double secPerFrame = rate / mRate;
      const float fadeInStep = (float)(1.0 / std::max(1.0, kFadeInSec * mRate));
      const float fadeOutStep = (float)(1.0 / std::max(1.0, kFadeOutSec * mRate));

      // Stretcher (time / pitch independent) when the rate or pitch is not
      // plain and the block fits its preparation.
      const bool plain = std::fabs(rate - 1.0) < 1e-4 && pitch == 0.0f;
      const bool useStretch = hasBuf && !plain && v.stretch.Prepared() && n <= mStretchCap;
      const double fileFramesPerInput = hasBuf ? buf->sampleRate / mRate : 1.0;

      int i = std::clamp(v.startAt, 0, n);
      v.startAt = 0;
      while (i < n && v.active)
      {
         // Frames until the clip end (wrap / stop) at the current rate.
         int segEnd = n;
         if (duration > 0.0)
         {
            const double left = (duration - v.srcSec) / std::max(1e-12, secPerFrame);
            if (left < (double)(n - i))
               segEnd = i + std::max(1, (int)std::ceil(left));
         }
         const int len = segEnd - i;
         if (useStretch)
         {
            v.stretch.SetPitch(pitch);
            const double inputFrame = v.srcSec * buf->sampleRate / fileFramesPerInput;
            if (!v.stretchLive)
            {
               v.stretch.Start(*buf, fileFramesPerInput, inputFrame, rate);
               v.stretchLive = true;
            }
            v.stretch.Render(*buf, fileFramesPerInput, inputFrame, rate, len);
         }
         for (int k = 0; k < len; k++)
         {
            const int f = i + k;
            if (v.fadeOutAt >= 0 && f >= v.fadeOutAt)
               v.fade = std::max(0.0f, v.fade - fadeOutStep);
            else
               v.fade = std::min(1.0f, v.fade + fadeInStep);
            float l = 0.0f, rr = 0.0f;
            if (hasBuf)
            {
               if (useStretch)
               {
                  l = v.stretch.Out(0, k);
                  rr = v.stretch.Out(1, k);
               }
               else
               {
                  const double pos = v.srcSec * buf->sampleRate;
                  l = ReadBufferInterp(*buf, 0, pos);
                  rr = ReadBufferInterp(*buf, 1, pos);
               }
            }
            const float g = gain * v.fade;
            L[f] += l * g;
            Rt[f] += rr * g;
            v.srcSec += secPerFrame;
            if (v.stopping && v.fade <= 0.0f)
            {
               v.active = false;
               break;
            }
         }
         i = segEnd;
         if (!v.active)
            break;
         if (duration > 0.0 && v.srcSec >= duration - 1e-9)
         {
            const double beat = blockStart + bps * (double)i;
            if (loop)
            {
               v.srcSec = std::fmod(v.srcSec, duration);
               v.stretchLive = false; // re-anchor at the loop start
               if (!v.stopping)
                  PushEvent(ClipMatrixNode::Event::kWrap, r, v.col, beat, v.srcSec);
            }
            else
            {
               // Play once: done.
               if (!v.stopping)
                  PushEvent(ClipMatrixNode::Event::kStop, r, v.col, beat, duration);
               v.active = false;
               v.stopping = true;
            }
         }
      }
      if (v.fadeOutAt >= 0)
         v.fadeOutAt = 0; // a fade-out in progress continues from frame 0 next block
   }

   void ForwardNotes()
   {
      if (mInbox == nullptr)
         return;
      NoteEvent notes[64];
      int got = 0;
      while ((got = mInbox->Pop(mCursor, notes, 64)) > 0)
         for (int i = 0; i < got; i++)
         {
            if (notes[i].bendUpdate)
               continue;
            const uint32_t t = mNoteTail.load(std::memory_order_relaxed);
            const uint32_t nx = (t + 1) % kNoteCap;
            if (nx == mNoteHead.load(std::memory_order_acquire))
               break;
            mNotes[t] = { notes[i].note, notes[i].isNoteOn };
            mNoteTail.store(nx, std::memory_order_release);
         }
   }

   static constexpr uint32_t kCmdCap = 256;
   static constexpr uint32_t kEvCap = 1024;
   static constexpr uint32_t kNoteCap = 256;

   double mRate = 48000.0;
   int mStretchCap = 512;
   SampleSlot mSlots[R][C];
   CellParams mParams[R][C];
   RowState mRows[R];
   std::vector<float> mRowBuf;
   std::mt19937 mRng { 0x51ce };
   int mSceneApplyCol = -1;
   double mSceneApplyBeat = 0.0;

   std::atomic<float> mSceneTempo[C] = {};
   std::atomic<int> mSceneNum[C] = {};
   std::atomic<int> mSceneDen[C] = {};
   std::atomic<int> mRowsN { 4 };
   std::atomic<int> mColsN { 8 };
   std::atomic<int> mQuant { 5 };
   std::atomic<float> mVolume { 1.0f };
   std::atomic<float> mRowGain[R];

   Cmd mCmds[kCmdCap];
   std::atomic<uint32_t> mCmdHead { 0 }, mCmdTail { 0 };
   ClipMatrixNode::Event mEvents[kEvCap];
   std::atomic<uint32_t> mEvHead { 0 }, mEvTail { 0 };
   NoteEv mNotes[kNoteCap];
   std::atomic<uint32_t> mNoteHead { 0 }, mNoteTail { 0 };
   NoteEventQueue* mInbox = nullptr;
   int mCursor = -1;

   std::atomic<int> mPubPlaying[R];
   std::atomic<int> mPubQueued[R];
   std::atomic<double> mPubPos[R] = {};
   std::atomic<double> mPubLaunch[R] = {};
   std::atomic<float> mPubPeak[R] = {};
   std::atomic<uint64_t> mBlockCounter { 0 };
   int mLastFrames = 0;
};

// One row of a Clip Matrix as its own audio output (Clip Matrix Out).
class AudioClipMatrixTap : public AudioNode
{
public:
   void SetSource(AudioClipMatrixNode* src, int row)
   {
      mRow.store(row, std::memory_order_relaxed);
      mSource.store(src, std::memory_order_release);
   }
   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      AudioClipMatrixNode* src = mSource.load(std::memory_order_acquire);
      const uint64_t block = src != nullptr ? src->BlockCounter() : 0;
      if (src == nullptr || block == mLastBlock || src->LastFrames() < output.numFrames)
      {
         for (int ch = 0; ch < output.numChannels; ch++)
            std::fill(output.channels[ch], output.channels[ch] + output.numFrames, 0.0f);
         mLastBlock = block;
         return;
      }
      const int row = mRow.load(std::memory_order_relaxed);
      for (int ch = 0; ch < output.numChannels; ch++)
      {
         const float* s = src->RowChannel(row, ch);
         std::copy(s, s + output.numFrames, output.channels[ch]);
      }
      mLastBlock = block;
   }

private:
   std::atomic<AudioClipMatrixNode*> mSource { nullptr };
   std::atomic<int> mRow { 0 };
   uint64_t mLastBlock = ~0ull;
};

// ===========================================================================
// Main thread
// ===========================================================================
ClipMatrixNode::ClipMatrixNode() : mAudio(std::make_unique<AudioClipMatrixNode>())
{
   for (int r = 0; r < kMaxRows; r++)
   {
      mPlaying[r] = -1;
      mQueued[r] = -1;
   }
}

ClipMatrixNode::~ClipMatrixNode()
{
   for (auto& row : mCells)
      for (Cell& c : row)
         ReleaseCellMedia(c);
   for (unsigned int& t : mRowTex)
      if (t != 0)
         glDeleteTextures(1, &t);
   if (mClearTex != 0)
      glDeleteTextures(1, &mClearTex);
   GLUtil::DestroyFbo(mScratch[0]);
   GLUtil::DestroyFbo(mScratch[1]);
   GLUtil::DestroyFbo(mOut);
   if (mProgram != 0)
      glDeleteProgram(mProgram);
}

AudioNode* ClipMatrixNode::GetAudioNode()
{
   return mAudio.get();
}

void ClipMatrixNode::ReleaseCellMedia(Cell& c)
{
   if (c.video != nullptr)
   {
      Platform::VideoClose(c.video);
      c.video = nullptr;
   }
   if (c.imageTex != 0)
   {
      glDeleteTextures(1, &c.imageTex);
      c.imageTex = 0;
   }
}

bool ClipMatrixNode::LoadCell(int row, int col, const std::string& path)
{
   row = Clamp(row, kMaxRows);
   col = Clamp(col, kMaxCols);
   Cell& c = mCells[row][col];
   static const std::vector<std::string> kAudioExt = { "wav", "aif", "aiff", "mp3", "m4a", "aac", "caf", "flac", "ogg" };
   int kind = kEmpty;
   if (HasExt(path, kAudioExt))
      kind = kAudio;
   else if (HasExt(path, MediaExtensions::Video()))
      kind = kVideo;
   else if (HasExt(path, MediaExtensions::Image()))
      kind = kImage;
   if (kind == kEmpty)
   {
      c.status = "not an audio, video or image file";
      return false;
   }

   const Cell keep = c; // settings survive a reload of the same slot
   ReleaseCellMedia(c);
   c.kind = kind;
   c.path = path;
   c.hasAudio = false;
   c.duration = 0.0;
   c.w = c.h = 0;
   c.status.clear();
   if (keep.path != path || keep.name.empty())
   {
      const std::filesystem::path p = std::filesystem::u8path(path);
      c.name = p.stem().u8string();
   }

   Platform::SampleBuffer* audio = nullptr;
   std::string err;
   if (kind == kAudio || kind == kVideo)
   {
      auto* decoded = new Platform::SampleBuffer();
      if (AudioDecodeCache::DecodeCached(path, *decoded, err) && decoded->numFrames > 0 && decoded->channels > 0)
      {
         audio = decoded;
         c.hasAudio = true;
         c.duration = decoded->sampleRate > 0.0 ? decoded->numFrames / decoded->sampleRate : 0.0;
      }
      else
         delete decoded;
   }
   if (kind == kAudio && audio == nullptr)
   {
      c = Cell();
      c.status = err.empty() ? "could not decode" : err;
      return false;
   }
   if (kind == kVideo)
   {
      std::string verr;
      c.video = Platform::VideoOpen(path, verr);
      if (c.video == nullptr)
      {
         delete audio;
         c = Cell();
         c.status = verr.empty() ? "could not open the video" : verr;
         return false;
      }
      c.duration = std::max(c.duration, Platform::VideoDuration(c.video));
      c.w = Platform::VideoWidth(c.video);
      c.h = Platform::VideoHeight(c.video);
   }
   if (kind == kImage)
   {
      std::vector<unsigned char> px;
      int w = 0, h = 0;
      if (!Platform::LoadImageRGBA(path, px, w, h, err) || w <= 0 || h <= 0)
      {
         c = Cell();
         c.status = err.empty() ? "could not load the image" : err;
         return false;
      }
      glGenTextures(1, &c.imageTex);
      glBindTexture(GL_TEXTURE_2D, c.imageTex);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glBindTexture(GL_TEXTURE_2D, 0);
      c.w = w;
      c.h = h;
   }

   // Tempo of a new audio loop: detected, and synced when it was found.
   if (keep.path != path)
   {
      c.mode = keep.kind == kEmpty ? kLoop : keep.mode;
      if (audio != nullptr && sEstimateBpm != nullptr && kind == kAudio)
      {
         bool detected = false;
         const float bpm = sEstimateBpm(audio, path, std::max(1.0f, Transport::Instance().Tempo()), &detected);
         c.sampleBpm = bpm;
         c.origBpm = detected ? bpm : 0.0f;
         c.sync = detected;
      }
      else
      {
         c.sampleBpm = std::max(1.0f, Transport::Instance().Tempo());
         c.origBpm = 0.0f;
         c.sync = false;
      }
   }
   mAudio->PushBuffer(row, col, audio != nullptr ? audio : new Platform::SampleBuffer());
   PushCellParams(row, col);
   AudioTopologyRequest::Request();
   return true;
}

void ClipMatrixNode::ReloadFromPaths()
{
   for (int r = 0; r < kMaxRows; r++)
      for (int c = 0; c < kMaxCols; c++)
      {
         Cell& cell = mCells[r][c];
         if (cell.path.empty())
         {
            if (cell.kind != kEmpty)
               ClearCell(r, c);
            continue;
         }
         const std::string path = cell.path;
         Cell settings = cell;
         settings.video = nullptr;
         settings.imageTex = 0;
         if (!LoadCell(r, c, path))
         {
            // Keep the reference (and its settings) so a moved file can be
            // found again; it just stays silent.
            const std::string status = cell.status;
            cell = settings;
            cell.kind = kEmpty;
            cell.status = status.empty() ? "file missing" : status;
            PushCellParams(r, c);
         }
      }
}

void ClipMatrixNode::ClearCell(int row, int col)
{
   row = Clamp(row, kMaxRows);
   col = Clamp(col, kMaxCols);
   ReleaseCellMedia(mCells[row][col]);
   mCells[row][col] = Cell();
   mAudio->PushBuffer(row, col, new Platform::SampleBuffer());
   PushCellParams(row, col);
}

// Settings move with the clip; media is reloaded at the new place.
void ClipMatrixNode::CopyCell(int r0, int c0, int r1, int c1)
{
   if (r0 == r1 && c0 == c1)
      return;
   Cell src = mCells[Clamp(r0, kMaxRows)][Clamp(c0, kMaxCols)];
   src.video = nullptr;
   src.imageTex = 0;
   if (src.kind == kEmpty)
   {
      ClearCell(r1, c1);
      return;
   }
   ClearCell(r1, c1);
   Cell& dst = mCells[Clamp(r1, kMaxRows)][Clamp(c1, kMaxCols)];
   dst = src; // settings, so LoadCell keeps them (same path)
   dst.video = nullptr;
   dst.imageTex = 0;
   dst.kind = kEmpty; // forces the media load below
   const std::string path = src.path;
   const Cell settings = src;
   if (LoadCell(r1, c1, path))
   {
      Cell& d = mCells[Clamp(r1, kMaxRows)][Clamp(c1, kMaxCols)];
      d.mode = settings.mode;
      d.quant = settings.quant;
      d.gainDb = settings.gainDb;
      d.pitch = settings.pitch;
      d.sync = settings.sync;
      d.sampleBpm = settings.sampleBpm;
      d.origBpm = settings.origBpm;
      d.color = settings.color;
      d.followOn = settings.followOn;
      d.followA = settings.followA;
      d.followB = settings.followB;
      d.followChanceB = settings.followChanceB;
      d.followBars = settings.followBars;
      d.name = settings.name;
      PushCellParams(r1, c1);
   }
}

void ClipMatrixNode::SwapCells(int r0, int c0, int r1, int c1)
{
   if (r0 == r1 && c0 == c1)
      return;
   const Cell a = mCells[Clamp(r0, kMaxRows)][Clamp(c0, kMaxCols)];
   const Cell b = mCells[Clamp(r1, kMaxRows)][Clamp(c1, kMaxCols)];
   auto restore = [this](int r, int c, const Cell& s)
   {
      if (s.kind == kEmpty)
      {
         ClearCell(r, c);
         return;
      }
      CellAt(r, c) = Cell();
      mAudio->PushBuffer(r, c, new Platform::SampleBuffer());
      if (LoadCell(r, c, s.path))
      {
         Cell& d = CellAt(r, c);
         const std::string nm = s.name;
         Cell copy = s;
         copy.video = d.video;
         copy.imageTex = d.imageTex;
         copy.duration = d.duration;
         copy.hasAudio = d.hasAudio;
         copy.w = d.w;
         copy.h = d.h;
         copy.kind = d.kind;
         d = copy;
         PushCellParams(r, c);
      }
   };
   // Release both cells' media first, then reload crosswise.
   ReleaseCellMedia(mCells[Clamp(r0, kMaxRows)][Clamp(c0, kMaxCols)]);
   ReleaseCellMedia(mCells[Clamp(r1, kMaxRows)][Clamp(c1, kMaxCols)]);
   Cell a2 = a, b2 = b;
   a2.video = b2.video = nullptr;
   a2.imageTex = b2.imageTex = 0;
   restore(r0, c0, b2);
   restore(r1, c1, a2);
}

void ClipMatrixNode::PushCellParams(int row, int col)
{
   const Cell& c = mCells[row][col];
   AudioClipMatrixNode::CellParams& p = mAudio->Params(row, col);
   p.present.store(c.kind != kEmpty, std::memory_order_relaxed);
   p.kind.store(c.kind, std::memory_order_relaxed);
   p.mode.store(c.mode, std::memory_order_relaxed);
   p.quant.store(c.quant, std::memory_order_relaxed);
   p.gain.store(DspMath::DbToLinear(std::clamp(c.gainDb, -60.0f, 12.0f)), std::memory_order_relaxed);
   p.pitch.store(c.kind == kAudio ? std::clamp(c.pitch, -24.0f, 24.0f) : 0.0f, std::memory_order_relaxed);
   p.sync.store(c.sync, std::memory_order_relaxed);
   p.bpm.store(std::clamp(c.sampleBpm, 20.0f, 999.0f), std::memory_order_relaxed);
   p.duration.store(c.kind == kImage ? 0.0 : c.duration, std::memory_order_relaxed);
   p.followOn.store(c.followOn, std::memory_order_relaxed);
   p.followA.store(c.followA, std::memory_order_relaxed);
   p.followB.store(c.followB, std::memory_order_relaxed);
   p.chanceB.store(std::clamp(c.followChanceB, 0.0f, 1.0f), std::memory_order_relaxed);
   p.followBars.store(std::max(0.0f, c.followBars), std::memory_order_relaxed);
}

void ClipMatrixNode::Launch(int row, int col)
{
   if (!Transport::Instance().IsPlaying())
      Transport::Instance().SetPlaying(true); // like Live: launching starts the transport
   mAudio->Push({ AudioClipMatrixNode::kCmdLaunch, Clamp(row, kMaxRows), Clamp(col, kMaxCols) });
}
void ClipMatrixNode::Release(int row, int col)
{
   mAudio->Push({ AudioClipMatrixNode::kCmdRelease, Clamp(row, kMaxRows), Clamp(col, kMaxCols) });
}
void ClipMatrixNode::StopRow(int row)
{
   mAudio->Push({ AudioClipMatrixNode::kCmdStopRow, Clamp(row, kMaxRows), 0 });
}
void ClipMatrixNode::LaunchScene(int col)
{
   if (!Transport::Instance().IsPlaying())
      Transport::Instance().SetPlaying(true);
   mAudio->Push({ AudioClipMatrixNode::kCmdScene, 0, Clamp(col, kMaxCols) });
}
void ClipMatrixNode::StopAll()
{
   mAudio->Push({ AudioClipMatrixNode::kCmdStopAll, 0, 0 });
}

bool ClipMatrixNode::PopEvent(Event& out)
{
   return mAudio->PopEvent(out);
}

void ClipMatrixNode::UpdateRowVideo(int row, double /*beat*/)
{
   mRowShowTex[row] = 0;
   const int col = mPlaying[row];
   if (col < 0)
      return;
   Cell& c = mCells[row][col];
   if (c.kind == kImage && c.imageTex != 0)
   {
      mRowShowTex[row] = c.imageTex;
      mRowShowW[row] = c.w;
      mRowShowH[row] = c.h;
      return;
   }
   if (c.kind != kVideo || c.video == nullptr)
      return;
   double pos = mAudio->PubPos(row);
   if (c.duration > 0.0)
      pos = std::clamp(pos, 0.0, std::max(0.0, c.duration - 1e-3));
   if (mRowTex[row] == 0)
   {
      glGenTextures(1, &mRowTex[row]);
      glBindTexture(GL_TEXTURE_2D, mRowTex[row]);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glBindTexture(GL_TEXTURE_2D, 0);
   }
   if (Platform::VideoFrameAt(c.video, pos, mFrame) && !mFrame.empty())
   {
      const int w = Platform::VideoWidth(c.video);
      const int h = Platform::VideoHeight(c.video);
      if (w > 0 && h > 0 && mFrame.size() >= (size_t)w * (size_t)h * 4)
      {
         glBindTexture(GL_TEXTURE_2D, mRowTex[row]);
         glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
         if (w != mRowTexW[row] || h != mRowTexH[row])
         {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, mFrame.data());
            mRowTexW[row] = w;
            mRowTexH[row] = h;
         }
         else
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, mFrame.data());
         glBindTexture(GL_TEXTURE_2D, 0);
      }
   }
   if (mRowTexW[row] > 0)
   {
      mRowShowTex[row] = mRowTex[row];
      mRowShowW[row] = mRowTexW[row];
      mRowShowH[row] = mRowTexH[row];
   }
}

void ClipMatrixNode::EnsureCompose()
{
   if (mClearTex == 0)
   {
      const unsigned char px[4] = { 0, 0, 0, 0 };
      glGenTextures(1, &mClearTex);
      glBindTexture(GL_TEXTURE_2D, mClearTex);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glBindTexture(GL_TEXTURE_2D, 0);
   }
   if (mProgramTried)
      return;
   mProgramTried = true;
   const std::string src =
      std::string("#version 150\n"
                  "in vec2 vUv;\n"
                  "out vec4 fragColor;\n"
                  "uniform sampler2D uTexBase;\n"
                  "uniform sampler2D uTexTop;\n"
                  "uniform int uMode;\n"
                  "uniform float uOpacity;\n"
                  "uniform vec4 uTopFit;\n")
      + BlendModes::kBlendGLSL
      + "void main() {\n"
        "   vec2 topUv = (vUv - uTopFit.zw) / uTopFit.xy;\n"
        "   vec4 top = vec4(0.0);\n"
        "   if (topUv.x >= 0.0 && topUv.x <= 1.0 && topUv.y >= 0.0 && topUv.y <= 1.0) top = texture(uTexTop, topUv);\n"
        "   vec4 base = texture(uTexBase, vUv);\n"
        "   float as = top.a * uOpacity;\n"
        "   if (as <= 1e-6) { fragColor = base; return; }\n"
        "   if (uMode == 30) { fragColor = vec4(base.rgb, base.a * (1.0 - as)); return; }\n"
        "   if (uMode == 31) { fragColor = vec4(base.rgb, base.a * (1.0 - (1.0 - top.a) * uOpacity)); return; }\n"
        "   vec3 blended = blendMode(uMode, base.rgb, top.rgb);\n"
        "   vec3 cs = mix(top.rgb, blended, base.a);\n"
        "   float ar = as + base.a * (1.0 - as);\n"
        "   vec3 cr = (ar > 1e-5) ? (cs * as + base.rgb * base.a * (1.0 - as)) / ar : vec3(0.0);\n"
        "   fragColor = vec4(cr, ar);\n"
        "}\n";
   mProgram = GLUtil::CompileProgram(src.c_str());
}

// Rows composited bottom (last row) first, so row 1 is in front.
void ClipMatrixNode::Compose()
{
   EnsureCompose();
   width = std::clamp(width, 16, 8192);
   height = std::clamp(height, 16, 8192);
   if (!GLUtil::EnsureFbo(mOut, width, height))
      return;
   GLint prevFbo = 0, prevVp[4];
   glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
   glGetIntegerv(GL_VIEWPORT, prevVp);
   auto clear = [&](const GLUtil::Fbo& f)
   {
      glBindFramebuffer(GL_FRAMEBUFFER, f.fbo);
      glViewport(0, 0, width, height);
      glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
      glClear(GL_COLOR_BUFFER_BIT);
   };
   int layers[kMaxRows];
   int n = 0;
   for (int r = rows - 1; r >= 0; r--)
      if (mRowShowTex[r] != 0 && rowParams[r].opacity > 0.0f && !rowParams[r].mute)
         layers[n++] = r;
   if (n == 0 || mProgram == 0 || !GLUtil::EnsureFbo(mScratch[0], width, height) ||
       (n >= 2 && !GLUtil::EnsureFbo(mScratch[1], width, height)))
   {
      clear(mOut);
      glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
      glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
      return;
   }
   clear(mScratch[0]);
   const float targetAspect = (float)width / (float)height;
   const int uBase = glGetUniformLocation(mProgram, "uTexBase");
   const int uTop = glGetUniformLocation(mProgram, "uTexTop");
   const int uMode = glGetUniformLocation(mProgram, "uMode");
   const int uOpacity = glGetUniformLocation(mProgram, "uOpacity");
   const int uFit = glGetUniformLocation(mProgram, "uTopFit");
   int base = 0;
   for (int k = 0; k < n; k++)
   {
      const int r = layers[k];
      const float srcAspect = (float)std::max(1, mRowShowW[r]) / (float)std::max(1, mRowShowH[r]);
      float sx = 1.0f, sy = 1.0f, ox = 0.0f, oy = 0.0f;
      if (srcAspect > targetAspect)
      {
         sy = targetAspect / srcAspect;
         oy = (1.0f - sy) * 0.5f;
      }
      else
      {
         sx = srcAspect / targetAspect;
         ox = (1.0f - sx) * 0.5f;
      }
      const bool last = k + 1 == n;
      const GLUtil::Fbo& out = last ? mOut : mScratch[1 - base];
      const unsigned int baseTex = mScratch[base].tex;
      const unsigned int topTex = mRowShowTex[r];
      const int mode = std::clamp(rowParams[r].blendMode, 0, 31);
      const float op = std::clamp(rowParams[r].opacity, 0.0f, 1.0f);
      GLUtil::RunShaderPass(out, mProgram, [&]()
      {
         glActiveTexture(GL_TEXTURE0);
         glBindTexture(GL_TEXTURE_2D, baseTex);
         glUniform1i(uBase, 0);
         glActiveTexture(GL_TEXTURE1);
         glBindTexture(GL_TEXTURE_2D, topTex);
         glUniform1i(uTop, 1);
         glUniform1i(uMode, mode);
         glUniform1f(uOpacity, op);
         glUniform4f(uFit, sx, sy, ox, oy);
      });
      base = 1 - base;
   }
   glActiveTexture(GL_TEXTURE1);
   glBindTexture(GL_TEXTURE_2D, 0);
   glActiveTexture(GL_TEXTURE0);
   glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
   glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
}

void ClipMatrixNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   rows = std::clamp(rows, 1, kMaxRows);
   cols = std::clamp(cols, 1, kMaxCols);
   quantize = std::clamp(quantize, 0, kQuantCount - 1);

   mAudio->DrainRetired();
   mAudio->SetGlobals(rows, cols, quantize, std::clamp(volume, 0.0f, 2.0f));
   for (int r = 0; r < kMaxRows; r++)
   {
      const Row& rp = rowParams[r];
      mAudio->SetRowGain(r, (rp.mute || r >= rows) ? 0.0f : DspMath::DbToLinear(std::clamp(rp.gainDb, -60.0f, 12.0f)));
      for (int c = 0; c < kMaxCols; c++)
         PushCellParams(r, c);
   }
   for (int c = 0; c < kMaxCols; c++)
      mAudio->SetScene(c, scenes[c].tempo, scenes[c].sigNum, scenes[c].sigDen);

   // Notes: base note + row * cols + col; note-off releases gate clips.
   AudioClipMatrixNode::NoteEv ev;
   while (mAudio->PopNote(ev))
   {
      const int idx = ev.note - baseNote;
      if (idx < 0 || idx >= rows * cols)
         continue;
      const int r = idx / cols, c = idx % cols;
      if (ev.on)
         Launch(r, c);
      else
         Release(r, c);
   }

   for (int r = 0; r < kMaxRows; r++)
   {
      mPlaying[r] = mAudio->PubPlaying(r);
      mQueued[r] = mAudio->PubQueued(r);
      mRowPeak[r] = mAudio->PubPeak(r);
      mLaunchBeat[r] = mAudio->PubLaunch(r);
      const int col = mPlaying[r];
      const double dur = col >= 0 ? mCells[r][col].duration : 0.0;
      mProgress[r] = dur > 0.0 ? (float)std::clamp(mAudio->PubPos(r) / dur, 0.0, 1.0) : 0.0f;
      if (r < rows)
         UpdateRowVideo(r, Transport::Instance().Beats());
      else
         mRowShowTex[r] = 0;
   }
   Compose();
}

unsigned int ClipMatrixNode::GetOutputTexture()
{
   return mOut.tex != 0 ? mOut.tex : mClearTex;
}

unsigned int ClipMatrixNode::RowTexture(int row)
{
   EnsureCompose();
   const int r = Clamp(row, kMaxRows);
   return mRowShowTex[r] != 0 ? mRowShowTex[r] : mClearTex;
}
int ClipMatrixNode::RowTextureWidth(int row) const
{
   const int r = Clamp(row, kMaxRows);
   return mRowShowTex[r] != 0 ? mRowShowW[r] : 1;
}
int ClipMatrixNode::RowTextureHeight(int row) const
{
   const int r = Clamp(row, kMaxRows);
   return mRowShowTex[r] != 0 ? mRowShowH[r] : 1;
}

void ClipMatrixNode::VisitParams(ParamVisitor& v)
{
   v.Int("rows", rows);
   v.Int("cols", cols);
   v.Int("quantize", quantize);
   v.Int("baseNote", baseNote);
   v.Float("volume", volume);
   v.Int("width", width);
   v.Int("height", height);
   v.Bool("recordToArrangement", recordToArrangement);
   char k[48];
   for (int c = 0; c < kMaxCols; c++)
   {
      Scene& s = scenes[c];
      snprintf(k, sizeof(k), "s%d_name", c); v.Text(k, s.name);
      snprintf(k, sizeof(k), "s%d_tempo", c); v.Float(k, s.tempo);
      snprintf(k, sizeof(k), "s%d_num", c); v.Int(k, s.sigNum);
      snprintf(k, sizeof(k), "s%d_den", c); v.Int(k, s.sigDen);
   }
   for (int r = 0; r < kMaxRows; r++)
   {
      Row& rp = rowParams[r];
      snprintf(k, sizeof(k), "r%d_name", r); v.Text(k, rp.name);
      snprintf(k, sizeof(k), "r%d_gain", r); v.Float(k, rp.gainDb);
      snprintf(k, sizeof(k), "r%d_mute", r); v.Bool(k, rp.mute);
      snprintf(k, sizeof(k), "r%d_blend", r); v.Int(k, rp.blendMode);
      snprintf(k, sizeof(k), "r%d_opacity", r); v.Float(k, rp.opacity);
      for (int c = 0; c < kMaxCols; c++)
      {
         Cell& cl = mCells[r][c];
         // Only filled cells are written; a missing key keeps the default.
         snprintf(k, sizeof(k), "c%d_%d_path", r, c); v.Text(k, cl.path);
         if (cl.path.empty())
            continue;
         snprintf(k, sizeof(k), "c%d_%d_name", r, c); v.Text(k, cl.name);
         snprintf(k, sizeof(k), "c%d_%d_mode", r, c); v.Int(k, cl.mode);
         snprintf(k, sizeof(k), "c%d_%d_quant", r, c); v.Int(k, cl.quant);
         snprintf(k, sizeof(k), "c%d_%d_gain", r, c); v.Float(k, cl.gainDb);
         snprintf(k, sizeof(k), "c%d_%d_pitch", r, c); v.Float(k, cl.pitch);
         snprintf(k, sizeof(k), "c%d_%d_sync", r, c); v.Bool(k, cl.sync);
         snprintf(k, sizeof(k), "c%d_%d_bpm", r, c); v.Float(k, cl.sampleBpm);
         snprintf(k, sizeof(k), "c%d_%d_obpm", r, c); v.Float(k, cl.origBpm);
         snprintf(k, sizeof(k), "c%d_%d_color", r, c); v.Int(k, cl.color);
         snprintf(k, sizeof(k), "c%d_%d_fon", r, c); v.Bool(k, cl.followOn);
         snprintf(k, sizeof(k), "c%d_%d_fa", r, c); v.Int(k, cl.followA);
         snprintf(k, sizeof(k), "c%d_%d_fb", r, c); v.Int(k, cl.followB);
         snprintf(k, sizeof(k), "c%d_%d_fch", r, c); v.Float(k, cl.followChanceB);
         snprintf(k, sizeof(k), "c%d_%d_fbars", r, c); v.Float(k, cl.followBars);
      }
   }
}

// ===========================================================================
// Clip Matrix Out
// ===========================================================================
ClipMatrixOutNode::ClipMatrixOutNode()
{
   for (int r = 0; r < ClipMatrixNode::kMaxRows; r++)
   {
      mTaps[r] = std::make_unique<AudioClipMatrixTap>();
      mLabels[2 * r] = std::to_string(r + 1) + " video";
      mLabels[2 * r + 1] = std::to_string(r + 1) + " audio";
   }
}

ClipMatrixOutNode::~ClipMatrixOutNode() = default;

ClipMatrixNode* ClipMatrixOutNode::Matrix() const
{
   return dynamic_cast<ClipMatrixNode*>(input.GetSource());
}

int ClipMatrixOutNode::Rows() const
{
   const ClipMatrixNode* m = Matrix();
   return m != nullptr ? std::clamp(m->rows, 1, ClipMatrixNode::kMaxRows) : 4;
}

const char* ClipMatrixOutNode::OutputLabel(int index) const
{
   return mLabels[std::clamp(index, 0, 2 * ClipMatrixNode::kMaxRows - 1)].c_str();
}

AudioNode* ClipMatrixOutNode::GetAudioNode()
{
   return mTaps[0].get();
}

AudioNode* ClipMatrixOutNode::ExtraAudioNode(int i)
{
   return (i >= 0 && i + 1 < ClipMatrixNode::kMaxRows) ? mTaps[i + 1].get() : nullptr;
}

AudioNode* ClipMatrixOutNode::AudioNodeForOutput(int output)
{
   if (output < 0 || (output & 1) == 0)
      return nullptr;
   return mTaps[std::clamp(output / 2, 0, ClipMatrixNode::kMaxRows - 1)].get();
}

void ClipMatrixOutNode::ResolveAudioTaps()
{
   ClipMatrixNode* m = Matrix();
   for (int r = 0; r < ClipMatrixNode::kMaxRows; r++)
      mTaps[r]->SetSource(m != nullptr ? m->AudioHalf() : nullptr, r);
}

void ClipMatrixOutNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (ClipMatrixNode* m = Matrix())
      m->CookIfNeeded(frameId);
}

unsigned int ClipMatrixOutNode::GetOutputTextureAt(int output)
{
   ClipMatrixNode* m = Matrix();
   return m != nullptr ? m->RowTexture(std::max(0, output) / 2) : 0;
}
int ClipMatrixOutNode::GetOutputWidthAt(int output) const
{
   const ClipMatrixNode* m = Matrix();
   return m != nullptr ? m->RowTextureWidth(std::max(0, output) / 2) : 0;
}
int ClipMatrixOutNode::GetOutputHeightAt(int output) const
{
   const ClipMatrixNode* m = Matrix();
   return m != nullptr ? m->RowTextureHeight(std::max(0, output) / 2) : 0;
}
int ClipMatrixOutNode::GetOutputWidth() const { return GetOutputWidthAt(0); }
int ClipMatrixOutNode::GetOutputHeight() const { return GetOutputHeightAt(0); }
