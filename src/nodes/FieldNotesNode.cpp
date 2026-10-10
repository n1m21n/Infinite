#include "FieldNotesNode.h"

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/MeterRing.h"
#include "audio/MusicTime.h"
#include "audio/NoteEventQueue.h"
#include "audio/ParamMailbox.h"
#include "audio/SampleSlot.h"
#include "core/Transport.h"
#include "field/BackendRegister.h"
#include "field/SampleProgram.h"
#include "field/SampleRuntime.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

// ------------------------------------------------------------- audio thread
// Monophonic as a *program* (one state bank, one kernel run per sample),
// polyphonic as an *output* (note() may be called many times per sample).
// Everything the audio thread touches is a fixed array sized here: no
// allocation, locks or strings in ProcessBlock.
class AudioFieldNotesNode : public AudioNode
{
public:
   static constexpr int kMaxPending = 128;      // len > 0 notes waiting for their note-off
   static constexpr int kMaxFollow = 128;       // len == 0 notes tied to an input note
   static constexpr int kMaxFollowOuts = 16;    // output notes per followed input note
   static constexpr int kMaxBlockEmits = 64;    // note() budget per block
   static constexpr int kMaxInEvents = 128;

   AudioFieldNotesNode()
   {
      std::memset(mStateCur, 0, sizeof(mStateCur));
      std::memset(mStateNext, 0, sizeof(mStateNext));
      std::memset(mDelayBuffer, 0, sizeof(mDelayBuffer));
      std::memset(mDelaySwap, 0, sizeof(mDelaySwap));
      std::memset(mDelayCursors, 0, sizeof(mDelayCursors));
      std::memset(mDelaySwapCursors, 0, sizeof(mDelaySwapCursors));
      std::memset(mTableBuffer, 0, sizeof(mTableBuffer));
      std::memset(mTableSwap, 0, sizeof(mTableSwap));
   }

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mMailbox.PrepareToPlay(sampleRate);
      mSamplePos = 0;
      mNextOff = UINT64_MAX;
      for (auto& p : mPending) p.active = false;
      for (auto& f : mFollow) f.active = false;
      mWasPlaying = false;
      mHaveStopSeen = false;
      mPrevBeatValid = false;
   }

   NoteEventQueue* NoteOutbox() override { return &mOutbox; }

   void SetNoteInbox(NoteEventQueue* inbox, int cursor) override
   {
      // Input cable removed: every len == 0 note tied to it is orphaned.
      if (inbox == nullptr && mInbox != nullptr)
         mFlushFollowers.store(true, std::memory_order_relaxed);
      mInbox = inbox;
      mNoteCursor = cursor;
   }

   void PushProgram(Field::SampleProgram* prog) { mProgramSlot.Push(prog); }
   void DrainRetired() { mProgramSlot.DrainRetired(); }
   void PushParam(int mailboxId, float value) { mMailbox.Push(mailboxId, value); }
   void SetScale(int root, int scale)
   {
      mRoot.store(root, std::memory_order_relaxed);
      mScale.store(scale, std::memory_order_relaxed);
   }
   MeterRing& RollRing() { return mRoll; }
   uint64_t EmittedTotal() const { return mEmittedTotal.load(std::memory_order_relaxed); }
   uint64_t DroppedTotal() const { return mDroppedTotal.load(std::memory_order_relaxed); }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      for (int ch = 0; ch < output.numChannels; ch++)
         std::fill(output.channels[ch], output.channels[ch] + output.numFrames, 0.0f);

      if (mProgramSlot.SwapIn())
         AdoptProgram(mProgramSlot.Active());

      const int numFrames = output.numFrames;
      Transport& tr = Transport::Instance();
      const bool playing = tr.IsPlaying();
      const double bpm = std::max(1.0, (double)tr.Tempo());
      mBeatBase = tr.BlockStartBeats();
      mBeatsPerSample = playing ? bpm / (60.0 * mSampleRate) : 0.0;
      mSamplesPerBeat = 60.0 * mSampleRate / bpm;
      mBlockEmits = 0;
      mRootNow = mRoot.load(std::memory_order_relaxed);
      mScaleNow = mScale.load(std::memory_order_relaxed);

      // Rule 1: transport stop releases every note we scheduled.
      if (mWasPlaying && !playing)
         FlushPending(0);
      mWasPlaying = playing;
      if (!playing)
         mPrevBeatValid = false;

      // Rule 4: an unplugged input orphans the notes that were following it.
      if (mFlushFollowers.exchange(false, std::memory_order_relaxed))
         FlushFollowers(0);

      NoteEvent evts[kMaxInEvents];
      int numEvts = (mInbox != nullptr) ? mInbox->Pop(mNoteCursor, evts, kMaxInEvents) : 0;
      int evtIdx = 0;

      if (mActive == nullptr || !mActive->valid)
      {
         // No program (compile never succeeded): still honour note-offs due.
         for (int i = 0; i < numFrames; i++)
            EmitDueOffs(i);
         mSamplePos += (uint64_t)numFrames;
         return;
      }

      float paramVals[Field::kSampleMaxParams] = {};
      const int numParams = (int)mActive->params.size();

      for (int i = 0; i < numFrames; i++)
      {
         EmitDueOffs(i);

         for (int p = 0; p < numParams; p++)
            paramVals[p] = mMailbox.SmoothedValue(mActive->params[p].mailboxId);

         // Input events landing on this sample, in arrival order. A note-off
         // closes its followers immediately; each note-on is one kernel run
         // (rule D5), so a 3-note chord runs the kernel 3 times on one sample.
         int runs = 0;
         while (evtIdx < numEvts && evts[evtIdx].frameOffset <= i)
         {
            const NoteEvent& e = evts[evtIdx++];
            if (e.bendUpdate)
               continue;
            if (e.isNoteOn)
            {
               mLastNoteNum = (float)e.note;
               mLastNoteVel = e.velocity;
               RunKernel(i, true, runs == 0, &e, paramVals);
               runs++;
            }
            else
            {
               CloseFollowers(e.voiceId, i);
            }
         }
         if (runs == 0)
            RunKernel(i, false, true, nullptr, paramVals);
      }

      mSamplePos += (uint64_t)numFrames;
   }

private:
   struct Pending
   {
      bool active = false;
      int note = 0;
      int voiceId = 0;
      uint64_t offSample = 0;
   };

   struct Follow
   {
      bool active = false;
      int inVoiceId = 0;
      int count = 0;
      int note[kMaxFollowOuts] = {};
      int voiceId[kMaxFollowOuts] = {};
   };

   void AdoptProgram(Field::SampleProgram* fresh)
   {
      float oldCur[Field::kSampleMaxStateCells];
      std::memcpy(oldCur, mStateCur, sizeof(oldCur));
      const int n = (int)fresh->state.size();
      for (int i = 0; i < n; i++)
      {
         const int from = fresh->state[i].transplantFromIndex;
         const float val = (from >= 0 && from < Field::kSampleMaxStateCells)
            ? oldCur[from] : fresh->state[i].initialValue;
         mStateCur[i] = val;
         mStateNext[i] = val;
      }
      for (int i = n; i < Field::kSampleMaxStateCells; i++)
      {
         mStateCur[i] = 0.0f;
         mStateNext[i] = 0.0f;
      }

      std::memcpy(mDelaySwap, mDelayBuffer, sizeof(mDelaySwap));
      std::memcpy(mDelaySwapCursors, mDelayCursors, sizeof(mDelaySwapCursors));
      std::fill(mDelayBuffer, mDelayBuffer + Field::kSampleMaxDelayCells, 0.0f);
      std::fill(mDelayCursors, mDelayCursors + Field::kSampleMaxDelayLines, 0);
      for (const auto& dl : fresh->delays)
      {
         if (dl.transplantFromOffset >= 0 && dl.transplantFromLength == dl.length &&
             dl.bufferOffset + dl.length <= Field::kSampleMaxDelayCells &&
             dl.transplantFromOffset + dl.transplantFromLength <= Field::kSampleMaxDelayCells)
         {
            std::memcpy(mDelayBuffer + dl.bufferOffset, mDelaySwap + dl.transplantFromOffset,
                        dl.length * sizeof(float));
            if (dl.transplantFromCursor >= 0 && dl.transplantFromCursor < Field::kSampleMaxDelayLines &&
                dl.cursorIndex >= 0 && dl.cursorIndex < Field::kSampleMaxDelayLines)
               mDelayCursors[dl.cursorIndex] = mDelaySwapCursors[dl.transplantFromCursor];
         }
      }

      std::memcpy(mTableSwap, mTableBuffer, sizeof(mTableSwap));
      std::fill(mTableBuffer, mTableBuffer + Field::kSampleMaxTableCells, 0.0f);
      for (const auto& tbl : fresh->tables)
      {
         if (tbl.bufferOffset + tbl.length > Field::kSampleMaxTableCells)
            continue;
         if (tbl.transplantFromOffset >= 0 && tbl.transplantFromLength == tbl.length &&
             tbl.transplantFromOffset + tbl.transplantFromLength <= Field::kSampleMaxTableCells)
            std::memcpy(mTableBuffer + tbl.bufferOffset, mTableSwap + tbl.transplantFromOffset,
                        tbl.length * sizeof(float));
         else
            std::fill(mTableBuffer + tbl.bufferOffset, mTableBuffer + tbl.bufferOffset + tbl.length,
                      tbl.initialValue);
      }

      mActive = fresh; // pending note-offs and followers are deliberately kept (rule 3)
   }

   void RunKernel(int frame, bool noteOn, bool firstRun, const NoteEvent* inEv, const float* paramVals)
   {
      Field::SampleRuntimeInput rin;
      rin.sr = (float)mSampleRate;
      rin.n = (float)(mSamplePos + (uint64_t)frame);
      rin.noteOn = noteOn ? 1.0f : 0.0f;
      rin.noteNum = mLastNoteNum;
      rin.notePitch = 440.0f * std::pow(2.0f, (mLastNoteNum - 69.0f) / 12.0f);
      rin.noteVel = mLastNoteVel;
      rin.beat = mBeatBase + mBeatsPerSample * (double)frame;
      // tick() compares against the beat the previous sample actually saw
      // (not beat - bps recomputed, which differs by an ulp and double-fires
      // or skips a crossing). Extra same-sample note-on runs see an equal
      // pair, which never fires.
      if (firstRun)
      {
         if (!mPrevBeatValid || rin.beat < mPrevBeat - 2.0 * mBeatsPerSample)
            mPrevBeat = rin.beat - mBeatsPerSample; // first sample after play / a seek back
         rin.beatPrev = mPrevBeat;
         mPrevBeat = rin.beat;
         mPrevBeatValid = mBeatsPerSample > 0.0;
      }
      else
      {
         rin.beatPrev = rin.beat;
      }
      rin.root = mRootNow;
      rin.scale = mScaleNow;
      rin.rng = &mRng;
      rin.emit = &mSink;
      rin.paramVals = paramVals;
      rin.stateCur = mStateCur;
      rin.stateNext = mStateNext;
      rin.delayBuf = mDelayBuffer;
      rin.delayCursors = mDelayCursors;
      rin.tableBuf = mTableBuffer;

      mSink.count = 0;
      float regs[Field::kSampleMaxRegs];
      RunSampleProgram(*mActive, rin, regs);
      std::memcpy(mStateCur, mStateNext, sizeof(mStateCur));

      for (int k = 0; k < mSink.count; k++)
         HandleEmit(mSink.items[k], frame, inEv);
      if (mSink.dropped > 0)
      {
         mDroppedTotal.fetch_add((uint64_t)mSink.dropped, std::memory_order_relaxed);
         mSink.dropped = 0;
      }
      mSink.count = 0;
   }

   void Drop() { mDroppedTotal.fetch_add(1, std::memory_order_relaxed); }

   void HandleEmit(const Field::NoteEmit& e, int frame, const NoteEvent* inEv)
   {
      if (!(e.vel > 0.0f) || !std::isfinite(e.pitch) || !std::isfinite(e.vel))
         return; // vel <= 0 is "don't play", not a drop
      // A NaN/inf/huge length must not reach llround or the uint64 cast (undefined); treat as "no length".
      const float evLen = (std::isfinite(e.len) && e.len < 1.0e6f) ? e.len : 0.0f;
      if (mBlockEmits >= kMaxBlockEmits)
      {
         Drop();
         return;
      }

      Pending* pend = nullptr;
      Follow* fol = nullptr;
      if (evLen > 0.0f)
      {
         pend = FreePending();
         if (pend == nullptr) { Drop(); return; }
      }
      else
      {
         // len == 0: the note lives exactly as long as the input note that
         // triggered this run. With no input note there is nothing to follow.
         if (inEv == nullptr) { Drop(); return; }
         fol = FindOrCreateFollow(inEv->voiceId);
         if (fol == nullptr || fol->count >= kMaxFollowOuts) { Drop(); return; }
      }

      const int note = std::clamp((int)std::lround(e.pitch), 0, 127);
      NoteEvent on;
      on.note = note;
      on.velocity = std::clamp(e.vel, 0.0f, 1.0f);
      on.isNoteOn = true;
      on.frameOffset = frame;
      on.source = this;
      on.voiceId = NextVoiceId();
      mOutbox.Push(on);
      mBlockEmits++;
      mEmittedTotal.fetch_add(1, std::memory_order_relaxed);

      if (pend != nullptr)
      {
         const uint64_t len = (uint64_t)std::max(1.0, std::llround((double)evLen * mSamplesPerBeat) + 0.0);
         pend->active = true;
         pend->note = note;
         pend->voiceId = on.voiceId;
         pend->offSample = mSamplePos + (uint64_t)frame + len;
         mNextOff = std::min(mNextOff, pend->offSample);
      }
      else
      {
         fol->note[fol->count] = note;
         fol->voiceId[fol->count] = on.voiceId;
         fol->count++;
      }

      const float roll[3] = { (float)(mBeatBase + mBeatsPerSample * (double)frame), (float)note, on.velocity };
      mRoll.Write(roll, 3);
   }

   Pending* FreePending()
   {
      for (auto& p : mPending)
         if (!p.active) return &p;
      return nullptr;
   }

   Follow* FindOrCreateFollow(int inVoiceId)
   {
      Follow* freeSlot = nullptr;
      for (auto& f : mFollow)
      {
         if (f.active && f.inVoiceId == inVoiceId) return &f;
         if (!f.active && freeSlot == nullptr) freeSlot = &f;
      }
      if (freeSlot != nullptr)
      {
         freeSlot->active = true;
         freeSlot->inVoiceId = inVoiceId;
         freeSlot->count = 0;
      }
      return freeSlot;
   }

   void PushOff(int note, int voiceId, int frame)
   {
      NoteEvent off;
      off.note = note;
      off.velocity = 0.0f;
      off.isNoteOn = false;
      off.frameOffset = frame;
      off.source = this;
      off.voiceId = voiceId; // note-offs are never dropped by the queue
      mOutbox.Push(off);
   }

   void CloseFollowers(int inVoiceId, int frame)
   {
      for (auto& f : mFollow)
      {
         if (!f.active || f.inVoiceId != inVoiceId) continue;
         for (int k = 0; k < f.count; k++)
            PushOff(f.note[k], f.voiceId[k], frame);
         f.active = false;
         f.count = 0;
      }
   }

   void FlushFollowers(int frame)
   {
      for (auto& f : mFollow)
      {
         if (!f.active) continue;
         for (int k = 0; k < f.count; k++)
            PushOff(f.note[k], f.voiceId[k], frame);
         f.active = false;
         f.count = 0;
      }
   }

   void FlushPending(int frame)
   {
      for (auto& p : mPending)
      {
         if (!p.active) continue;
         PushOff(p.note, p.voiceId, frame);
         p.active = false;
      }
      mNextOff = UINT64_MAX;
   }

   void EmitDueOffs(int frame)
   {
      const uint64_t now = mSamplePos + (uint64_t)frame;
      if (now < mNextOff)
         return;
      uint64_t next = UINT64_MAX;
      for (auto& p : mPending)
      {
         if (!p.active) continue;
         if (p.offSample <= now)
         {
            PushOff(p.note, p.voiceId, frame);
            p.active = false;
         }
         else
         {
            next = std::min(next, p.offSample);
         }
      }
      mNextOff = next;
   }

   NoteEventQueue mOutbox;
   NoteEventQueue* mInbox = nullptr;
   int mNoteCursor = -1;

   ParamMailbox mMailbox;
   SampleSlotT<Field::SampleProgram> mProgramSlot;
   Field::SampleProgram* mActive = nullptr;

   float mStateCur[Field::kSampleMaxStateCells];
   float mStateNext[Field::kSampleMaxStateCells];
   float mDelayBuffer[Field::kSampleMaxDelayCells];
   float mDelaySwap[Field::kSampleMaxDelayCells];
   int mDelayCursors[Field::kSampleMaxDelayLines];
   int mDelaySwapCursors[Field::kSampleMaxDelayLines];
   float mTableBuffer[Field::kSampleMaxTableCells];
   float mTableSwap[Field::kSampleMaxTableCells];

   Field::NoteEmitSink mSink;
   Pending mPending[kMaxPending];
   Follow mFollow[kMaxFollow];
   uint64_t mNextOff = UINT64_MAX;

   double mSampleRate = 48000.0;
   uint64_t mSamplePos = 0;
   double mBeatBase = 0.0;
   double mBeatsPerSample = 0.0;
   double mSamplesPerBeat = 24000.0;
   int mBlockEmits = 0;
   int mRootNow = 0;
   int mScaleNow = 0;
   float mLastNoteNum = 0.0f;
   float mLastNoteVel = 0.0f;
   uint32_t mRng = 0x2545F491u;
   bool mWasPlaying = false;
   bool mHaveStopSeen = false;
   double mPrevBeat = 0.0;
   bool mPrevBeatValid = false;

   std::atomic<bool> mFlushFollowers { false };
   std::atomic<int> mRoot { 0 };
   std::atomic<int> mScale { 0 };
   std::atomic<uint64_t> mEmittedTotal { 0 };
   std::atomic<uint64_t> mDroppedTotal { 0 };
   MeterRing mRoll;
};

// ---------------------------------------------------------------- presets
const std::vector<FieldNotesNode::Preset>& FieldNotesNode::Presets()
{
   static const std::vector<Preset> kPresets = {
      { "Transpose +7",
        "if (noteOn > 0.5) {\n"
        "   note(noteNum + 7, noteVel, 0)\n"
        "}\n" },
      { "Harmoniser",
        "if (noteOn > 0.5) {\n"
        "   note(noteNum, noteVel, 0)\n"
        "   note(noteNum + 7, noteVel * 0.7, 0)\n"
        "   note(noteNum + 12, noteVel * 0.5, 0)\n"
        "}\n" },
      { "Ratchet",
        "param float rate = 0.0625 [0.03125, 0.25]\n"
        "state float left = 0\n"
        "state float pitch = 60\n"
        "state float vel = 0\n"
        "if (noteOn > 0.5) { left = 4; pitch = noteNum; vel = noteVel }\n"
        "if (left > 0.5 && tick(rate) > 0.5) {\n"
        "   note(pitch, vel, rate * 0.5)\n"
        "   left = left - 1\n"
        "}\n" },
      { "Euclidean 5/16",
        "param float hits = 5 [1, 16]\n"
        "state float step = 0\n"
        "if (tick(1/16) > 0.5) {\n"
        "   on = floor((step + 1) * hits / 16 + 0.001) - floor(step * hits / 16 + 0.001)\n"
        "   note(deg(0), 0.9 * on, 1/16)\n"
        "   step = (step + 1) % 16\n"
        "}\n" },
      { "Scale Walk",
        "param float density = 0.6 [0, 1]\n"
        "param float spread = 2 [1, 7]\n"
        "state float d = 0\n"
        "if (tick(1/8) > 0.5) {\n"
        "   d = clamp(d + floor((rand() * 2 - 1) * spread + 0.5), -7, 14)\n"
        "   play = if(rand() < density, 1, 0)\n"
        "   note(deg(d), (0.6 + 0.3 * rand()) * play, 1/8)\n"
        "}\n" },
      { "Arp Up",
        "param float steps = 4 [2, 8]\n"
        "state float step = 0\n"
        "if (tick(1/8) > 0.5) {\n"
        "   cnt = floor(steps + 0.5)\n"
        "   note(deg(step * 2), 0.8, 1/8)\n"
        "   step = (step + 1) % cnt\n"
        "}\n" },
      { "Arp Up Down",
        "param float span = 5 [2, 8]\n"
        "state float step = 0\n"
        "if (tick(1/8) > 0.5) {\n"
        "   top = floor(span + 0.5) - 1\n"
        "   idx = top - abs(top - (step % (2 * top)))\n"
        "   note(deg(idx * 2), 0.8, 1/8)\n"
        "   step = (step + 1) % (2 * top)\n"
        "}\n" },
      { "Root Pulse",
        "param float rate = 0.25 [0.0625, 1]\n"
        "if (tick(rate) > 0.5) {\n"
        "   note(deg(0) - 12, 0.9, rate * 0.8)\n"
        "}\n" },
      { "Four On The Floor",
        "if (tick(1) > 0.5) {\n"
        "   note(deg(0) - 24, 1, 1/8)\n"
        "}\n" },
      { "Offbeat Chords",
        "if (tick(1/2) > 0.5 && (floor(beat * 2 + 0.01) % 2) > 0.5) {\n"
        "   note(deg(0), 0.7, 1/4)\n"
        "   note(deg(2), 0.6, 1/4)\n"
        "   note(deg(4), 0.6, 1/4)\n"
        "}\n" },
      { "Random Melody",
        "param float density = 0.7 [0, 1]\n"
        "param float range = 8 [2, 14]\n"
        "if (tick(1/8) > 0.5) {\n"
        "   play = if(rand() < density, 1, 0)\n"
        "   note(deg(floor(rand() * range)), (0.55 + 0.35 * rand()) * play, 1/8)\n"
        "}\n" },
      { "Sparse Drift",
        "param float chance = 0.35 [0, 1]\n"
        "if (tick(1/2) > 0.5) {\n"
        "   play = if(rand() < chance, 1, 0)\n"
        "   note(deg(floor(rand() * 5) * 2), 0.6 * play, 1.5)\n"
        "}\n" },
      { "3 Against 4",
        "if (tick(4/3) > 0.5) {\n"
        "   note(deg(4), 0.8, 1/4)\n"
        "}\n"
        "if (tick(1) > 0.5) {\n"
        "   note(deg(0) - 12, 0.9, 1/4)\n"
        "}\n" },
      { "Octave Bounce Bass",
        "state float step = 0\n"
        "if (tick(1/16) > 0.5) {\n"
        "   up = if((step % 2) > 0.5, 12, 0)\n"
        "   note(deg(0) - 12 + up, 0.7 + 0.25 * if((step % 4) < 0.5, 1, 0), 1/16)\n"
        "   step = (step + 1) % 16\n"
        "}\n" },
      { "Acid Line",
        "param float wander = 1.7 [0.5, 3]\n"
        "state float step = 0\n"
        "if (tick(1/16) > 0.5) {\n"
        "   d = floor(abs(sin(step * wander)) * 6)\n"
        "   acc = if((step % 4) < 0.5, 1, 0)\n"
        "   rest = if(rand() < 0.85, 1, 0)\n"
        "   note(deg(d) - 12, (0.55 + 0.4 * acc) * rest, 1/16)\n"
        "   step = (step + 1) % 16\n"
        "}\n" },
      { "Cascade Down",
        "param float length = 8 [3, 14]\n"
        "state float step = 0\n"
        "if (tick(1/16) > 0.5) {\n"
        "   cnt = floor(length + 0.5)\n"
        "   note(deg(cnt - 1 - step), 0.5 + 0.4 * (1 - step / cnt), 1/16)\n"
        "   step = (step + 1) % cnt\n"
        "}\n" },
      { "Triplet Ostinato",
        "state float step = 0\n"
        "if (tick(1/3) > 0.5) {\n"
        "   note(deg((step % 3) * 2 + 4), 0.75, 1/6)\n"
        "   step = (step + 1) % 3\n"
        "}\n" },
      { "Slow Pad",
        "if (tick(4) > 0.5) {\n"
        "   note(deg(0), 0.6, 4)\n"
        "   note(deg(2), 0.5, 4)\n"
        "   note(deg(4), 0.5, 4)\n"
        "   note(deg(6), 0.45, 4)\n"
        "}\n" },
      { "Chance Chords",
        "param float chance = 0.6 [0, 1]\n"
        "if (tick(2) > 0.5) {\n"
        "   play = if(rand() < chance, 1, 0)\n"
        "   r = floor(rand() * 4)\n"
        "   note(deg(r), 0.7 * play, 2)\n"
        "   note(deg(r + 2), 0.6 * play, 2)\n"
        "   note(deg(r + 4), 0.6 * play, 2)\n"
        "}\n" },
      { "Chord Progression",
        "if (tick(4) > 0.5) {\n"
        "   bar = floor(beat / 4 + 0.01) % 4\n"
        "   r = if(bar < 0.5, 0, if(bar < 1.5, 5, if(bar < 2.5, 3, 4)))\n"
        "   note(deg(r) - 12, 0.8, 4)\n"
        "   note(deg(r), 0.6, 4)\n"
        "   note(deg(r + 2), 0.55, 4)\n"
        "   note(deg(r + 4), 0.55, 4)\n"
        "}\n" },
      { "Octave Doubler",
        "if (noteOn > 0.5) {\n"
        "   note(noteNum, noteVel, 0)\n"
        "   note(noteNum + 12, noteVel * 0.6, 0)\n"
        "}\n" },
      { "Power Chord",
        "if (noteOn > 0.5) {\n"
        "   note(noteNum, noteVel, 0)\n"
        "   note(noteNum + 7, noteVel * 0.85, 0)\n"
        "   note(noteNum + 12, noteVel * 0.7, 0)\n"
        "}\n" },
      { "Major Triad",
        "if (noteOn > 0.5) {\n"
        "   note(noteNum, noteVel, 0)\n"
        "   note(noteNum + 4, noteVel * 0.8, 0)\n"
        "   note(noteNum + 7, noteVel * 0.8, 0)\n"
        "}\n" },
      { "Minor Triad",
        "if (noteOn > 0.5) {\n"
        "   note(noteNum, noteVel, 0)\n"
        "   note(noteNum + 3, noteVel * 0.8, 0)\n"
        "   note(noteNum + 7, noteVel * 0.8, 0)\n"
        "}\n" },
      { "Humanise Velocity",
        "param float amount = 0.25 [0, 0.6]\n"
        "if (noteOn > 0.5) {\n"
        "   note(noteNum, clamp(noteVel + (rand() - 0.5) * amount, 0.05, 1), 0)\n"
        "}\n" },
      { "Probability Gate",
        "param float keep = 0.7 [0, 1]\n"
        "if (noteOn > 0.5 && rand() < keep) {\n"
        "   note(noteNum, noteVel, 0)\n"
        "}\n" },
      { "Octave Scatter",
        "if (noteOn > 0.5) {\n"
        "   shift = (floor(rand() * 3) - 1) * 12\n"
        "   note(noteNum + shift, noteVel, 0)\n"
        "}\n" },
      { "Random Fifth",
        "param float chance = 0.3 [0, 1]\n"
        "if (noteOn > 0.5) {\n"
        "   up = if(rand() < chance, 7, 0)\n"
        "   note(noteNum + up, noteVel, 0)\n"
        "}\n" },
      { "Soft Compressor",
        "if (noteOn > 0.5) {\n"
        "   note(noteNum, 0.5 + noteVel * 0.5, 0)\n"
        "}\n" },
      { "Echo Tail",
        "param float feedback = 0.6 [0.2, 0.9]\n"
        "state float left = 0\n"
        "state float pitch = 60\n"
        "state float vel = 0\n"
        "if (noteOn > 0.5) {\n"
        "   note(noteNum, noteVel, 0)\n"
        "   left = 4\n"
        "   pitch = noteNum\n"
        "   vel = noteVel\n"
        "}\n"
        "if (left > 0.5 && tick(1/4) > 0.5) {\n"
        "   vel = vel * feedback\n"
        "   note(pitch, vel, 1/8)\n"
        "   left = left - 1\n"
        "}\n" },
      { "Shadow Octave",
        "state float armed = 0\n"
        "state float pitch = 60\n"
        "state float vel = 0\n"
        "if (noteOn > 0.5) {\n"
        "   note(noteNum, noteVel, 0)\n"
        "   armed = 1\n"
        "   pitch = noteNum\n"
        "   vel = noteVel\n"
        "}\n"
        "if (armed > 0.5 && tick(1/8) > 0.5) {\n"
        "   note(pitch + 12, vel * 0.6, 1/8)\n"
        "   armed = 0\n"
        "}\n" },
      { "Strum Triad",
        "param float speed = 0.0625 [0.03125, 0.25]\n"
        "state float k = 3\n"
        "state float pitch = 60\n"
        "state float vel = 0\n"
        "if (noteOn > 0.5) {\n"
        "   pitch = noteNum\n"
        "   vel = noteVel\n"
        "   k = 0\n"
        "}\n"
        "if (k < 2.5 && tick(speed) > 0.5) {\n"
        "   off = if(k < 0.5, 0, if(k < 1.5, 4, 7))\n"
        "   note(pitch + off, vel, 1/4)\n"
        "   k = k + 1\n"
        "}\n" },
      { "Latch Arp",
        "state float left = 0\n"
        "state float step = 0\n"
        "state float pitch = 60\n"
        "state float vel = 0\n"
        "if (noteOn > 0.5) {\n"
        "   pitch = noteNum\n"
        "   vel = noteVel\n"
        "   left = 8\n"
        "   step = 0\n"
        "}\n"
        "if (left > 0.5 && tick(1/8) > 0.5) {\n"
        "   off = if((step % 4) < 0.5, 0, if((step % 4) < 1.5, 4, if((step % 4) < 2.5, 7, 12)))\n"
        "   note(pitch + off, vel, 1/8)\n"
        "   step = step + 1\n"
        "   left = left - 1\n"
        "}\n" },
   };
   return kPresets;
}

const std::vector<std::string>& FieldNotesNode::PresetNames()
{
   static std::vector<std::string> kNames;
   if (kNames.empty())
      for (const auto& p : Presets())
         kNames.push_back(p.name);
   return kNames;
}

void FieldNotesNode::LoadPreset(int index)
{
   const auto& presets = Presets();
   if (index >= 0 && index < (int)presets.size())
   {
      presetIndex = index;
      code = presets[index].code;
      Apply();
   }
}

// -------------------------------------------------------------- main thread
FieldNotesNode::FieldNotesNode()
   : mAudioNode(std::make_unique<AudioFieldNotesNode>())
{
   code = Presets()[presetIndex].code;
   Apply();
}

FieldNotesNode::~FieldNotesNode() = default;

AudioNode* FieldNotesNode::GetAudioNode() { return mAudioNode.get(); }

bool FieldNotesNode::Apply()
{
   auto newProgram = std::make_unique<Field::SampleProgram>();
   Field::FieldError err;
   const Field::SampleProgram* prev = mLastCompiled.valid ? &mLastCompiled : nullptr;
   if (!Field::CompileSampleProgram(code, prev, *newProgram, err, /*notesHost*/ true))
   {
      // Keep the last working program running (field-compiler error rule).
      mLastError = err.message + " at line " + std::to_string(err.span.line) + ", col " + std::to_string(err.span.col);
      if (!err.hint.empty())
         mLastError += " - " + err.hint;
      return false;
   }

   std::vector<Field::DeclaredParam> declared;
   declared.reserve(newProgram->params.size());
   for (const auto& p : newProgram->params)
   {
      Field::DeclaredParam dp;
      dp.name = p.name;
      dp.typeName = "float";
      dp.defaultValue = p.defaultValue;
      dp.minValue = p.minValue;
      dp.maxValue = p.maxValue;
      declared.push_back(dp);
   }
   mParamTable.Reconcile(declared, mNodeIndex, mNotice);
   mCompiledParams = newProgram->params;
   mLastCompiled = *newProgram;

   mAudioNode->PushProgram(newProgram.release());
   mLastError.clear();
   return true;
}

void FieldNotesNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;

   mAudioNode->DrainRetired();
   mAudioNode->SetScale(root, scale);

   for (const auto& p : mCompiledParams)
   {
      if (const Field::ParamEntry* entry = mParamTable.Find(p.name))
         mAudioNode->PushParam(p.mailboxId, entry->value);
   }

   // Drain the note-roll triplets (beat, note, vel) the audio thread queued.
   float buf[999];
   const int got = (mAudioNode->RollRing().Read(buf, 999) / 3) * 3;
   for (int k = 0; k + 2 < got; k += 3)
   {
      RollNote& r = mRoll[mRollHead];
      r.beat = buf[k];
      r.note = buf[k + 1];
      r.vel = buf[k + 2];
      mRollHead = (mRollHead + 1) % kRollCapacity;
      if (mRollCount < kRollCapacity)
         mRollCount++;
      mLastBeat = r.beat;
   }
   mEmittedTotal = mAudioNode->EmittedTotal();
   mDroppedTotal = mAudioNode->DroppedTotal();
}

void FieldNotesNode::VisitParams(ParamVisitor& v)
{
   v.Text("code", code);
   v.Int("root", root);
   v.Int("scale", scale);
   mParamTable.VisitParams(v);
}

Field::DeviceFile FieldNotesNode::ToDeviceFile() const
{
   Field::DeviceFile device;
   device.domain = "notes";
   device.code = code;
   for (const auto& p : mParamTable.Params())
   {
      if (p.isDeclared)
         device.params[p.name] = p.value;
   }
   device.nodeSettings["root"] = (double)root;
   device.nodeSettings["scale"] = (double)scale;
   return device;
}

void FieldNotesNode::LoadDeviceFile(const Field::DeviceFile& device)
{
   code = device.code;
   auto itR = device.nodeSettings.find("root");
   if (itR != device.nodeSettings.end())
      root = (int)itR->second;
   auto itS = device.nodeSettings.find("scale");
   if (itS != device.nodeSettings.end())
      scale = (int)itS->second;
   Apply();
   for (const auto& kv : device.params)
   {
      Field::ParamEntry* p = mParamTable.Find(kv.first);
      if (p != nullptr)
         p->value = kv.second;
   }
}
