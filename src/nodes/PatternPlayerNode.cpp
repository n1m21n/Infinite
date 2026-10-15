#include "PatternPlayerNode.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/MusicTime.h"
#include "audio/NoteEvent.h"
#include "audio/NoteEventQueue.h"
#include "core/Transport.h"

namespace
{
   constexpr int kMaxNotes = 1024;
   constexpr int kMaxTones = 8;
   constexpr int kMaxOffs = 512;

   struct PatternSlot
   {
      int steps = 0;
      float stepBeats = 0.25f;
      int count = 0;
      NotePatterns::PNote notes[kMaxNotes];
   };

   // Single producer (main) / single consumer (audio) triple buffer.
   class PatternTripleBuffer
   {
   public:
      PatternSlot& Back() { return mSlots[mBack]; }
      void Publish() { mBack = mMiddle.exchange(mBack | kDirty, std::memory_order_acq_rel) & 3; }
      bool Acquire()
      {
         if ((mMiddle.load(std::memory_order_acquire) & kDirty) == 0)
            return false;
         mFront = mMiddle.exchange(mFront, std::memory_order_acq_rel) & 3;
         return true;
      }
      const PatternSlot& Front() const { return mSlots[mFront]; }

   private:
      static constexpr int kDirty = 4;
      PatternSlot mSlots[3];
      std::atomic<int> mMiddle { 1 };
      int mBack = 2;
      int mFront = 0;
   };
}

class AudioPatternPlayerNode : public AudioNode
{
public:
   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
      ReleaseAll(0);
      mRunning = false;
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      mBuffer.Acquire();
      const PatternSlot& pat = mBuffer.Front();
      const int frames = std::max(1, output.numFrames);
      const Transport& tr = Transport::Instance();
      const bool transportOn = tr.IsPlaying();
      const double bpm = std::max(1.0f, tr.Tempo());
      const double bpf = bpm / (60.0 * mSampleRate); // beats per frame
      const double b0 = tr.Beats();
      const double b1 = b0 + (double)frames * bpf;
      auto frameAt = [&](double beat) {
         return std::clamp((int)std::floor((beat - b0) / bpf), 0, frames - 1);
      };

      // ---- input: held keys, latch, thru ----------------------------------
      NoteEvent evts[64];
      int n;
      const bool thru = mThru.load(std::memory_order_relaxed);
      while ((n = mInbox != nullptr ? mInbox->Pop(mNoteCursor, evts, 64) : 0) > 0)
      {
         for (int i = 0; i < n; i++)
         {
            const NoteEvent& e = evts[i];
            if (thru)
            {
               NoteEvent t = e;
               t.source = this;
               mOutbox.Push(t);
            }
            if (e.bendUpdate || e.note < 0 || e.note > 127)
               continue;
            if (e.isNoteOn)
            {
               if (mHeldCount == 0)
                  mLatchedCount = 0; // a fresh chord replaces the latched one
               if (!mHeld[e.note])
               {
                  mHeld[e.note] = true;
                  mHeldCount++;
               }
               if (mLatchedCount < kMaxTones &&
                   std::find(mLatched, mLatched + mLatchedCount, e.note) == mLatched + mLatchedCount)
                  mLatched[mLatchedCount++] = e.note;
               std::sort(mLatched, mLatched + mLatchedCount);
            }
            else if (mHeld[e.note])
            {
               mHeld[e.note] = false;
               mHeldCount--;
               if (!mLatch.load(std::memory_order_relaxed))
               {
                  int* end = std::remove(mLatched, mLatched + mLatchedCount, e.note);
                  mLatchedCount = (int)(end - mLatched);
               }
            }
         }
         if (n < 64)
            break;
      }

      // ---- chord in use ------------------------------------------------------
      int tones[kMaxTones];
      int toneCount = 0;
      const int source = mSource.load(std::memory_order_relaxed);
      const bool haveInput = mLatchedCount > 0;
      if (source == PatternPlayerNode::kInput || (source == PatternPlayerNode::kAuto && haveInput))
      {
         toneCount = mLatchedCount;
         std::copy(mLatched, mLatched + mLatchedCount, tones);
      }
      else if (source == PatternPlayerNode::kGlobal || source == PatternPlayerNode::kAuto)
      {
         toneCount = std::clamp(mGlobalCount.load(std::memory_order_relaxed), 0, kMaxTones);
         for (int i = 0; i < toneCount; i++)
            tones[i] = mGlobal[i].load(std::memory_order_relaxed);
      }
      const int transpose = mTranspose.load(std::memory_order_relaxed);
      for (int i = 0; i < toneCount; i++)
         tones[i] += transpose;
      mToneReadoutCount.store(toneCount, std::memory_order_relaxed);
      for (int i = 0; i < toneCount; i++)
         mToneReadout[i].store(tones[i], std::memory_order_relaxed);

      // ---- note-offs due this block --------------------------------------------
      EmitOffsBefore(b1, frameAt);

      // ---- run state -------------------------------------------------------------
      const bool want = mPlay.load(std::memory_order_relaxed) && transportOn;
      if (!want)
      {
         if (mRunning)
         {
            ReleaseAll(0);
            mRunning = false;
         }
         mStepReadout.store(-1, std::memory_order_relaxed);
         return;
      }
      const double bpb = std::max(1.0, tr.BeatsPerBar());
      if (!mRunning)
      {
         mAnchor = mSyncBar.load(std::memory_order_relaxed) ? std::ceil(b0 / bpb - 1e-9) * bpb : b0;
         mRunning = true;
      }
      if (pat.steps <= 0 || pat.count <= 0 || toneCount <= 0)
      {
         mStepReadout.store(-1, std::memory_order_relaxed);
         return;
      }

      static const float kSpeed[3] = { 0.5f, 1.0f, 2.0f };
      const double sb = (double)pat.stepBeats / (double)kSpeed[std::clamp(mSpeed.load(std::memory_order_relaxed), 0, 2)];
      // Trim: the loop is steps [ts, te).
      const int ts = std::clamp(mTrimStart.load(std::memory_order_relaxed), 0, pat.steps - 1);
      int te = mTrimEnd.load(std::memory_order_relaxed);
      te = te <= 0 ? pat.steps : std::clamp(te, ts + 1, pat.steps);
      const double loopLen = sb * (double)(te - ts);
      const double swing = (double)mSwing.load(std::memory_order_relaxed) * sb * 0.5;
      const double gate = std::clamp((double)mGate.load(std::memory_order_relaxed), 0.05, 1.0);
      const float velScale = mVelocity.load(std::memory_order_relaxed);
      if (b1 > mAnchor)
      {
         const double rel0 = std::max(0.0, b0 - mAnchor);
         const long long firstLoop = (long long)std::floor(rel0 / loopLen);
         const long long lastLoop = (long long)std::floor((b1 - mAnchor) / loopLen);
         for (long long li = firstLoop; li <= lastLoop; li++)
         {
            const double loopStart = mAnchor + (double)li * loopLen;
            for (int k = 0; k < pat.count; k++)
            {
               const NotePatterns::PNote& pn = pat.notes[k];
               if (pn.step < (float)ts || pn.step >= (float)te)
                  continue;
               double t = loopStart + ((double)pn.step - (double)ts) * sb;
               const double whole = std::floor(pn.step);
               if (pn.step == (float)whole && ((long long)whole & 1))
                  t += swing;
               if (t < b0 || t >= b1 || t < mAnchor)
                  continue;
               const float vel = std::clamp(pn.vel * velScale, 0.01f, 1.0f);
               const double offBeat = t + std::max(0.02, std::min((double)pn.len, (double)te - (double)pn.step) * sb * gate);
               PlayNote(pn, tones, toneCount, vel, frameAt(t), offBeat);
            }
         }
         const double rel = std::fmod(std::max(0.0, b0 - mAnchor), loopLen);
         mStepReadout.store(b0 >= mAnchor ? ts + (int)std::floor(rel / sb) : -1, std::memory_order_relaxed);
      }
      else
         mStepReadout.store(-1, std::memory_order_relaxed);
      // Offs that fall inside this same block (very short notes).
      EmitOffsBefore(b1, frameAt);
   }

   NoteEventQueue* NoteOutbox() override { return &mOutbox; }
   void SetNoteInbox(NoteEventQueue* inbox, int cursor) override
   {
      mInbox = inbox;
      mNoteCursor = cursor;
   }

   // Main thread.
   void PushParams(const PatternPlayerNode& n, const int* globalTones, int globalCount, int keyPc, int scaleMask)
   {
      mTrimStart.store(n.trimStart, std::memory_order_relaxed);
      mTrimEnd.store(n.trimEnd, std::memory_order_relaxed);
      mFit.store(n.fitScale, std::memory_order_relaxed);
      mKeyPc.store(keyPc, std::memory_order_relaxed);
      mScaleMask.store(scaleMask, std::memory_order_relaxed);
      mPlay.store(n.playing, std::memory_order_relaxed);
      mLatch.store(n.latch, std::memory_order_relaxed);
      mSyncBar.store(n.syncBar, std::memory_order_relaxed);
      mThru.store(n.thru, std::memory_order_relaxed);
      mSource.store(n.source, std::memory_order_relaxed);
      mTranspose.store(n.transpose, std::memory_order_relaxed);
      mSpeed.store(n.speed, std::memory_order_relaxed);
      mGate.store(n.gate, std::memory_order_relaxed);
      mSwing.store(n.swing, std::memory_order_relaxed);
      mVelocity.store(n.velocity, std::memory_order_relaxed);
      for (int i = 0; i < kMaxTones; i++)
         mGlobal[i].store(i < globalCount ? globalTones[i] : 0, std::memory_order_relaxed);
      mGlobalCount.store(globalCount, std::memory_order_relaxed);
   }
   void PublishPattern(const NotePatterns::Pattern* p)
   {
      PatternSlot& s = mBuffer.Back();
      if (p == nullptr)
      {
         s.steps = 0;
         s.count = 0;
      }
      else
      {
         s.steps = std::max(1, p->steps);
         s.stepBeats = p->stepBeats > 0.0f ? p->stepBeats : 0.25f;
         s.count = std::min((int)p->notes.size(), kMaxNotes);
         std::copy(p->notes.begin(), p->notes.begin() + s.count, s.notes);
      }
      mBuffer.Publish();
   }
   int StepReadout() const { return mStepReadout.load(std::memory_order_relaxed); }
   int ToneReadout(int out[8]) const
   {
      const int c = std::clamp(mToneReadoutCount.load(std::memory_order_relaxed), 0, kMaxTones);
      for (int i = 0; i < c; i++)
         out[i] = mToneReadout[i].load(std::memory_order_relaxed);
      return c;
   }
   bool RunningReadout() const { return mRunningReadout.load(std::memory_order_relaxed); }

private:
   struct Off
   {
      double beat;
      int note;
      int voiceId;
   };

   template <typename FrameAt>
   void EmitOffsBefore(double beatLimit, FrameAt frameAt)
   {
      for (int i = 0; i < mOffCount;)
      {
         if (mOffs[i].beat < beatLimit)
         {
            NoteEvent off;
            off.note = mOffs[i].note;
            off.velocity = 0.0f;
            off.isNoteOn = false;
            off.frameOffset = frameAt(mOffs[i].beat);
            off.source = this;
            off.voiceId = mOffs[i].voiceId;
            mOutbox.Push(off);
            mOffs[i] = mOffs[--mOffCount];
         }
         else
            i++;
      }
      mRunningReadout.store(mRunning, std::memory_order_relaxed);
   }

   void ReleaseAll(int frameOffset)
   {
      for (int i = 0; i < mOffCount; i++)
      {
         NoteEvent off;
         off.note = mOffs[i].note;
         off.velocity = 0.0f;
         off.isNoteOn = false;
         off.frameOffset = frameOffset;
         off.source = this;
         off.voiceId = mOffs[i].voiceId;
         mOutbox.Push(off);
      }
      mOffCount = 0;
      mRunningReadout.store(false, std::memory_order_relaxed);
   }

   void Emit(int key, float vel, int frame, double offBeat)
   {
      if (key < 0 || key > 127)
         return;
      if (mOffCount >= kMaxOffs)
         return; // never more than this sounding: drop rather than leak a stuck note
      NoteEvent on;
      on.note = key;
      on.velocity = vel;
      on.isNoteOn = true;
      on.frameOffset = frame;
      on.source = this;
      on.voiceId = NextVoiceId();
      mOutbox.Push(on);
      mOffs[mOffCount++] = { offBeat, key, on.voiceId };
   }

   // Nearest note of the global scale (ties go down); off = unchanged.
   int FitToScale(int key) const
   {
      const int mask = mScaleMask.load(std::memory_order_relaxed);
      if (!mFit.load(std::memory_order_relaxed) || (mask & 0xFFF) == 0)
         return key;
      const int keyPc = mKeyPc.load(std::memory_order_relaxed);
      auto in = [&](int k) { return (mask >> ((((k - keyPc) % 12) + 12) % 12)) & 1; };
      for (int d = 0; d < 7; d++)
      {
         if (in(key - d))
            return key - d;
         if (in(key + d))
            return key + d;
      }
      return key;
   }

   void PlayNote(const NotePatterns::PNote& pn, const int* tones, int count, float vel, int frame, double offBeat)
   {
      auto toneKey = [&](int t) {
         const int oct = t / count;
         return tones[t % count] + 12 * oct;
      };
      switch (pn.kind)
      {
      case NotePatterns::kTone:
         Emit(toneKey(std::max(0, pn.tone)) + 12 * pn.oct, vel, frame, offBeat);
         break;
      case NotePatterns::kChord:
         for (int i = 0; i < count; i++)
            Emit(tones[i] + 12 * pn.oct, vel, frame, offBeat);
         break;
      case NotePatterns::kBass:
         Emit(tones[0] - 12 + 12 * pn.oct, vel, frame, offBeat);
         break;
      case NotePatterns::kRandom:
         mRng = mRng * 1664525u + 1013904223u;
         Emit(tones[(mRng >> 8) % (unsigned)count] + 12 * pn.oct, vel, frame, offBeat);
         break;
      case NotePatterns::kSemitone:
         Emit(FitToScale(tones[0] + pn.tone + 12 * pn.oct), vel, frame, offBeat);
         break;
      }
   }

   PatternTripleBuffer mBuffer;
   NoteEventQueue mOutbox;
   NoteEventQueue* mInbox = nullptr;
   int mNoteCursor = -1;
   double mSampleRate = 44100.0;

   bool mHeld[128] = {};
   int mHeldCount = 0;
   int mLatched[kMaxTones] = {};
   int mLatchedCount = 0;
   Off mOffs[kMaxOffs];
   int mOffCount = 0;
   bool mRunning = false;
   double mAnchor = 0.0;
   unsigned mRng = 0x1234567u;

   std::atomic<bool> mPlay { false }, mLatch { true }, mSyncBar { true }, mThru { false };
   std::atomic<int> mSource { 0 }, mTranspose { 0 }, mSpeed { 1 };
   std::atomic<int> mTrimStart { 0 }, mTrimEnd { 0 }, mKeyPc { 0 }, mScaleMask { 0 };
   std::atomic<bool> mFit { false };
   std::atomic<float> mGate { 0.85f }, mSwing { 0.0f }, mVelocity { 1.0f };
   std::atomic<int> mGlobal[kMaxTones] {};
   std::atomic<int> mGlobalCount { 0 };
   std::atomic<int> mStepReadout { -1 };
   std::atomic<int> mToneReadout[kMaxTones] {};
   std::atomic<int> mToneReadoutCount { 0 };
   std::atomic<bool> mRunningReadout { false };
};

PatternPlayerNode::PatternPlayerNode() = default;
PatternPlayerNode::~PatternPlayerNode() = default;

AudioNode* PatternPlayerNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioPatternPlayerNode>();
   return mAudioNode.get();
}

const char* PatternPlayerNode::SpeedName(int speed)
{
   static const char* kNames[3] = { "half", "x1", "double" };
   return kNames[std::clamp(speed, 0, 2)];
}

int PatternPlayerNode::PatternIndex() const
{
   const NotePatterns::Library& lib = NotePatterns::Current();
   for (int i = 0; i < (int)lib.patterns.size(); i++)
      if (lib.patterns[i].category == category && lib.patterns[i].name == patternName)
         return i;
   return -1;
}

const NotePatterns::Pattern* PatternPlayerNode::CurrentPattern() const
{
   const int i = PatternIndex();
   return i >= 0 ? &NotePatterns::Current().patterns[i] : nullptr;
}

void PatternPlayerNode::SelectPattern(int libraryIndex)
{
   const NotePatterns::Library& lib = NotePatterns::Current();
   if (libraryIndex < 0 || libraryIndex >= (int)lib.patterns.size())
      return;
   if (category != lib.patterns[libraryIndex].category || patternName != lib.patterns[libraryIndex].name)
   {
      trimStart = 0; // a new pattern plays whole
      trimEnd = 0;
   }
   category = lib.patterns[libraryIndex].category;
   patternName = lib.patterns[libraryIndex].name;
}

bool PatternPlayerNode::ImportMidiFile(const std::string& path, std::string& error)
{
   NotePatterns::Pattern probe;
   if (!NotePatterns::LoadMidiPattern(path, probe, error))
      return false;
   const std::string folder = NotePatterns::UserFolder();
   if (folder.empty())
   {
      error = "no user data folder";
      return false;
   }
   const std::filesystem::path src = std::filesystem::u8path(path);
   const std::filesystem::path dir = std::filesystem::u8path(folder) / "Imported";
   std::error_code ec;
   std::filesystem::create_directories(dir, ec);
   const std::filesystem::path dst = dir / src.filename();
   if (!std::filesystem::equivalent(src, dst, ec))
      std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
   if (ec)
   {
      error = ec.message();
      return false;
   }
   NotePatterns::Rescan();
   const NotePatterns::Library& lib = NotePatterns::Current();
   const std::string stem = dst.stem().u8string();
   for (int i = 0; i < (int)lib.patterns.size(); i++)
      if (lib.patterns[i].category == "Imported" && lib.patterns[i].name == stem)
      {
         SelectPattern(i);
         return true;
      }
   error = "copied, but the bank did not list it";
   return false;
}

void PatternPlayerNode::Step(int delta)
{
   const NotePatterns::Library& lib = NotePatterns::Current();
   const std::vector<int> in = lib.InCategory(category);
   if (in.empty())
      return;
   const int cur = PatternIndex();
   int pos = 0;
   for (int i = 0; i < (int)in.size(); i++)
      if (in[i] == cur)
         pos = i;
   pos = ((pos + delta) % (int)in.size() + (int)in.size()) % (int)in.size();
   SelectPattern(in[pos]);
}

void PatternPlayerNode::RandomInCategory()
{
   const std::vector<int> in = NotePatterns::Current().InCategory(category);
   if (in.size() < 2)
      return;
   static unsigned seed = 0x9E3779B9u;
   const int cur = PatternIndex();
   for (int tries = 0; tries < 8; tries++)
   {
      seed = seed * 1664525u + 1013904223u;
      const int pick = in[(seed >> 8) % in.size()];
      if (pick != cur)
      {
         SelectPattern(pick);
         return;
      }
   }
}

int PatternPlayerNode::PlayingStep() const { return mAudioNode ? mAudioNode->StepReadout() : -1; }
int PatternPlayerNode::CurrentTones(int out[8]) const { return mAudioNode ? mAudioNode->ToneReadout(out) : 0; }
bool PatternPlayerNode::Running() const { return mAudioNode && mAudioNode->RunningReadout(); }

void PatternPlayerNode::PushPattern()
{
   const std::string key = category + "\x1f" + patternName;
   const NotePatterns::Library& lib = NotePatterns::Current();
   // The library is rebuilt by Rescan; its size is a cheap generation stamp.
   const unsigned gen = (unsigned)lib.patterns.size();
   if (key == mPushedKey && gen == mPushedGen)
      return;
   mPushedKey = key;
   mPushedGen = gen;
   mAudioNode->PublishPattern(CurrentPattern());
}

void PatternPlayerNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   GetAudioNode();
   degree = std::clamp(degree, 1, 7);
   chordSize = std::clamp(chordSize, 3, 5);
   octave = std::clamp(octave, 0, 7);
   transpose = std::clamp(transpose, -24, 24);
   speed = std::clamp(speed, 0, 2);
   source = std::clamp(source, 0, 2);

   // The global chord: stacked thirds of the transport's scale on `degree`.
   const Transport& tr = Transport::Instance();
   const MusicTime::ScaleDef& sc = MusicTime::ScaleTable(tr.Scale());
   const int count = std::max(1, sc.count);
   const int rootKey = ((tr.Key() % 12) + 12) % 12 + 12 * (octave + 1);
   int tones[8] = {};
   for (int i = 0; i < chordSize; i++)
   {
      const int idx = (degree - 1) + 2 * i;
      tones[i] = rootKey + sc.intervals[idx % count] + 12 * (idx / count);
   }
   int mask = 0;
   for (int i = 0; i < count; i++)
      mask |= 1 << (((sc.intervals[i] % 12) + 12) % 12);
   if (const NotePatterns::Pattern* p = CurrentPattern())
   {
      trimStart = std::clamp(trimStart, 0, std::max(0, p->steps - 1));
      trimEnd = trimEnd <= 0 || trimEnd >= p->steps ? 0 : std::max(trimStart + 1, trimEnd);
   }
   mAudioNode->PushParams(*this, tones, chordSize, ((tr.Key() % 12) + 12) % 12, mask);
   PushPattern();
}

void PatternPlayerNode::VisitParams(ParamVisitor& v)
{
   v.Bool("playing", playing);
   v.Bool("latch", latch);
   v.Bool("syncBar", syncBar);
   v.Bool("thru", thru);
   v.Int("source", source);
   v.Int("degree", degree);
   v.Int("chordSize", chordSize);
   v.Int("octave", octave);
   v.Int("transpose", transpose);
   v.Int("speed", speed);
   v.Float("gate", gate);
   v.Float("swing", swing);
   v.Float("velocity", velocity);
   v.Int("trimStart", trimStart);
   v.Int("trimEnd", trimEnd);
   v.Bool("fitScale", fitScale);
   v.Text("category", category);
   v.Text("patternName", patternName);
}
