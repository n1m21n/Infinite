#include "ChordProgressionNode.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/MusicTime.h"
#include "audio/NoteEvent.h"
#include "audio/NoteEventQueue.h"
#include "core/Transport.h"

namespace
{
   struct Quality
   {
      const char* name;   // shown in the builder dropdown
      const char* suffix; // appended to the root in chord names
      int intervals[6];
      int count;
   };

   // Order is the saved builderQuality index: append only.
   const Quality kQualities[] = {
      { "maj", "", { 0, 4, 7 }, 3 },
      { "min", "m", { 0, 3, 7 }, 3 },
      { "7", "7", { 0, 4, 7, 10 }, 4 },
      { "maj7", "maj7", { 0, 4, 7, 11 }, 4 },
      { "m7", "m7", { 0, 3, 7, 10 }, 4 },
      { "dim", "dim", { 0, 3, 6 }, 3 },
      { "aug", "aug", { 0, 4, 8 }, 3 },
      { "sus2", "sus2", { 0, 2, 7 }, 3 },
      { "sus4", "sus4", { 0, 5, 7 }, 3 },
      { "6", "6", { 0, 4, 7, 9 }, 4 },
      { "m6", "m6", { 0, 3, 7, 9 }, 4 },
      { "9", "9", { 0, 4, 7, 10, 14 }, 5 },
      { "maj9", "maj9", { 0, 4, 7, 11, 14 }, 5 },
      { "m9", "m9", { 0, 3, 7, 10, 14 }, 5 },
      { "m7b5", "m7b5", { 0, 3, 6, 10 }, 4 },
      { "dim7", "dim7", { 0, 3, 6, 9 }, 4 },
      { "add9", "add9", { 0, 4, 7, 14 }, 4 },
      { "7sus4", "7sus4", { 0, 5, 7, 10 }, 4 },
      { "m(maj7)", "m(maj7)", { 0, 3, 7, 11 }, 4 },
      { "5", "5", { 0, 7 }, 2 },
   };
   constexpr int kNumQualities = (int)(sizeof(kQualities) / sizeof(kQualities[0]));

   const char* const kPcNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

   int PcSetOfMask(int mask)
   {
      int set = 0;
      for (int k = 0; k < ChordProgressionNode::kKeys; k++)
         if (mask & (1 << k))
            set |= 1 << (k % 12);
      return set;
   }

   int PcSetOfQuality(const Quality& q, int root)
   {
      int set = 0;
      for (int i = 0; i < q.count; i++)
         set |= 1 << ((root + q.intervals[i]) % 12);
      return set;
   }
}

// ------------------------------------------------------------- audio half
// Timing: chord changes are detected at block start (the chord index comes
// from Transport::Bars()). Everything inside a chord - strum delays, arp and
// pulse steps, their gate releases - is scheduled in samples and emitted at
// its exact frameOffset, sorted, because synths walk a block's events in
// order of frameOffset.
class AudioChordProgressionNode : public AudioNode
{
public:
   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mNumSounding = 0;
      mNumPending = 0;
      mLastKey = -1;
      mSamplePos = 0;
      mPlaying.store(-1, std::memory_order_relaxed);
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      const int numFrames = std::max(1, output.numFrames);
      mNumOut = 0;
      Transport& transport = Transport::Instance();
      if (!transport.IsPlaying())
      {
         ReleaseAll(0);
         mLastKey = -1;
         mPlaying.store(-1, std::memory_order_relaxed);
         Flush();
         mSamplePos += (uint64_t)numFrames;
         return;
      }

      const int count = std::clamp(mCount.load(std::memory_order_relaxed), 1, ChordProgressionNode::kMaxChords);
      double total = 0.0;
      for (int i = 0; i < count; i++)
         total += (double)std::clamp(mBars[i].load(std::memory_order_relaxed), 0.5f, 16.0f);

      const double beatsPerBar = std::max(0.25, transport.BeatsPerBar());
      const double bpm = std::max(1.0, (double)transport.Tempo());
      const double samplesPerBeat = mSampleRate * 60.0 / bpm;
      const double beats0 = std::max(0.0, transport.Beats());
      const double beats1 = beats0 + (double)numFrames / samplesPerBeat;
      const int mode = std::clamp(mMode.load(std::memory_order_relaxed), 0, ChordProgressionNode::kNumPlayModes - 1);
      const float gate = std::clamp(mGate.load(std::memory_order_relaxed), 0.05f, 1.0f);
      const bool stepMode = ChordProgressionNode::IsStepMode(mode);
      const double stepBeats = std::max(1.0 / 64.0, MusicTime::BeatsFor((MusicTime::RateDivision)std::clamp(
                                  mRateDiv.load(std::memory_order_relaxed), 0, MusicTime::kNumRateDivisions - 1)));

      // A block can cross a chord boundary; each segment is handled at its
      // own offset so chord changes land on the beat, not a block late.
      double segBeat = beats0;
      int idx = 0;
      double inChord = 0.0;
      for (int seg = 0; seg < 4; seg++)
      {
         const double bars = segBeat / beatsPerBar;
         const long long cycle = (long long)std::floor(bars / total);
         const double pos = bars - (double)cycle * total;
         double start = 0.0;
         double len = 1.0;
         for (idx = 0; idx < count; idx++)
         {
            len = (double)std::clamp(mBars[idx].load(std::memory_order_relaxed), 0.5f, 16.0f);
            if (pos < start + len || idx == count - 1)
               break;
            start += len;
         }
         inChord = std::clamp((pos - start) / len, 0.0, 1.0);
         const double chordStartBeat = ((double)cycle * total + start) * beatsPerBar;
         const double chordEndBeat = chordStartBeat + len * beatsPerBar;
         const int segOffset = std::clamp((int)((segBeat - beats0) * samplesPerBeat), 0, numFrames - 1);

         const long long key = cycle * ChordProgressionNode::kMaxChords + idx;
         if (key != mLastKey)
         {
            DrainPending(mSamplePos + (uint64_t)segOffset);
            ReleaseAll(segOffset);
            mLastKey = key;
            mNextStep = 0;
            BuildNotes(idx);
            if (!stepMode)
               StrikeChord(mode, segOffset);
         }
         else if (!stepMode && (mNumSounding > 0 || mNumPending > 0) && gate < 0.999f && inChord >= (double)gate)
         {
            ReleaseAll(segOffset);
         }

         const double segEnd = std::min(beats1, chordEndBeat);
         if (stepMode && mNumNotes > 0)
         {
            // Every step starting before this segment ends. A step already
            // late (e.g. the transport jumped) plays at the segment start.
            for (int guard = 0; guard < 64; guard++)
            {
               const double stepBeat = chordStartBeat + (double)mNextStep * stepBeats;
               if (stepBeat >= segEnd - 1.0e-9)
                  break;
               const int offset = std::clamp((int)((stepBeat - beats0) * samplesPerBeat), segOffset, numFrames - 1);
               DrainPending(mSamplePos + (uint64_t)offset); // earlier gate-offs keep their own time
               ReleaseAll(offset);
               PlayStep(mode, mNextStep, offset);
               if (gate < 0.999f)
               {
                  const uint64_t offAt = mSamplePos + (uint64_t)offset +
                                         (uint64_t)std::max(1.0, stepBeats * samplesPerBeat * (double)gate);
                  for (int i = 0; i < mNumSounding; i++)
                     Schedule(offAt, mSounding[i].note, false, mSounding[i].voiceId);
               }
               mNextStep++;
            }
         }

         if (chordEndBeat >= beats1)
            break;
         segBeat = chordEndBeat + 1.0e-9;
      }

      // Pending events (strum note-ons, step gate note-offs) due this block.
      const uint64_t blockEnd = mSamplePos + (uint64_t)numFrames;
      DrainPending(blockEnd);

      Flush();
      mSamplePos = blockEnd;
      mPlaying.store(idx, std::memory_order_relaxed);
      mProgress.store((float)inChord, std::memory_order_relaxed);
   }

   NoteEventQueue* NoteOutbox() override { return &mOutbox; }

   // Main thread only.
   void PushParams(const ChordProgressionNode& n)
   {
      mCount.store(n.chordCount, std::memory_order_relaxed);
      mBaseOctave.store(n.baseOctave, std::memory_order_relaxed);
      mTranspose.store(n.transpose, std::memory_order_relaxed);
      mVelocity.store(n.velocity, std::memory_order_relaxed);
      mGate.store(n.gate, std::memory_order_relaxed);
      mBass.store(n.bass, std::memory_order_relaxed);
      mMode.store(n.playMode, std::memory_order_relaxed);
      mRateDiv.store(n.rateDiv, std::memory_order_relaxed);
      mStrumMs.store(n.strumMs, std::memory_order_relaxed);
      mOctaves.store(n.arpOctaves, std::memory_order_relaxed);
      for (int i = 0; i < ChordProgressionNode::kMaxChords; i++)
      {
         mMask[i].store(n.chordMask[i], std::memory_order_relaxed);
         mBars[i].store(n.chordBars[i], std::memory_order_relaxed);
      }
   }

   int Playing() const { return mPlaying.load(std::memory_order_relaxed); }
   float Progress() const { return mProgress.load(std::memory_order_relaxed); }

private:
   static constexpr int kMaxNotes = ChordProgressionNode::kKeys + 1;
   static constexpr int kMaxSounding = kMaxNotes * 3;
   static constexpr int kMaxPending = 128;
   static constexpr int kMaxOut = 256;

   struct Sounding { int note; int voiceId; };
   struct Pending { uint64_t sample; int note; int voiceId; bool on; };

   // ---- event output, sorted per block
   void EmitOn(int note, int voiceId, int offset)
   {
      if (note < 0 || note > 127 || mNumOut >= kMaxOut)
         return;
      NoteEvent& e = mOut[mNumOut++];
      e = NoteEvent();
      e.note = note;
      e.velocity = std::clamp(mVelocity.load(std::memory_order_relaxed), 0.0f, 1.0f);
      e.isNoteOn = true;
      e.frameOffset = offset;
      e.source = this;
      e.voiceId = voiceId;
      if (mNumSounding < kMaxSounding)
         mSounding[mNumSounding++] = { note, voiceId };
   }

   void EmitOff(int note, int voiceId, int offset)
   {
      for (int i = 0; i < mNumSounding; i++)
         if (mSounding[i].voiceId == voiceId)
         {
            mSounding[i] = mSounding[--mNumSounding];
            if (mNumOut < kMaxOut)
            {
               NoteEvent& e = mOut[mNumOut++];
               e = NoteEvent();
               e.note = note;
               e.velocity = 0.0f;
               e.isNoteOn = false;
               e.frameOffset = offset;
               e.source = this;
               e.voiceId = voiceId;
            }
            return;
         }
   }

   void Flush()
   {
      // Insertion sort by offset; at equal offsets note-offs go first so a
      // re-struck pitch releases before it retriggers.
      for (int i = 1; i < mNumOut; i++)
      {
         NoteEvent e = mOut[i];
         int j = i - 1;
         while (j >= 0 && (mOut[j].frameOffset > e.frameOffset ||
                           (mOut[j].frameOffset == e.frameOffset && mOut[j].isNoteOn && !e.isNoteOn)))
         {
            mOut[j + 1] = mOut[j];
            j--;
         }
         mOut[j + 1] = e;
      }
      for (int i = 0; i < mNumOut; i++)
         mOutbox.Push(mOut[i]);
      mNumOut = 0;
   }

   // Emits every pending event due before `until` (absolute sample) at its
   // own offset in the current block.
   void DrainPending(uint64_t until)
   {
      for (int i = 0; i < mNumPending;)
      {
         const Pending p = mPending[i];
         if (p.sample < until)
         {
            const int offset = p.sample > mSamplePos ? (int)(p.sample - mSamplePos) : 0;
            mPending[i] = mPending[--mNumPending];
            if (p.on)
               EmitOn(p.note, p.voiceId, offset);
            else
               EmitOff(p.note, p.voiceId, offset);
         }
         else
         {
            i++;
         }
      }
   }

   void Schedule(uint64_t sample, int note, bool on, int voiceId)
   {
      if (mNumPending < kMaxPending)
         mPending[mNumPending++] = { sample, note, voiceId, on };
   }

   // Releases everything sounding at `offset` and drops every pending event
   // (a delayed strum note that has not started yet simply never starts).
   void ReleaseAll(int offset)
   {
      mNumPending = 0;
      while (mNumSounding > 0)
      {
         const Sounding s = mSounding[mNumSounding - 1];
         EmitOff(s.note, s.voiceId, offset);
      }
   }

   // ---- the chord's notes, ascending, bass included
   void BuildNotes(int idx)
   {
      mNumNotes = 0;
      const int mask = mMask[idx].load(std::memory_order_relaxed);
      if (mask == 0)
         return; // an empty chord is a rest
      const int base = (std::clamp(mBaseOctave.load(std::memory_order_relaxed), -1, 7) + 1) * 12 +
                       std::clamp(mTranspose.load(std::memory_order_relaxed), -24, 24);
      int lowest = -1;
      for (int k = 0; k < ChordProgressionNode::kKeys; k++)
         if (mask & (1 << k))
         {
            if (lowest < 0)
               lowest = k;
         }
      if (mBass.load(std::memory_order_relaxed) && lowest >= 0)
         AddNote(base + lowest - 12);
      for (int k = 0; k < ChordProgressionNode::kKeys; k++)
         if (mask & (1 << k))
            AddNote(base + k);
   }

   void AddNote(int note)
   {
      if (note < 0 || note > 127 || mNumNotes >= kMaxNotes)
         return;
      for (int i = 0; i < mNumNotes; i++)
         if (mNotes[i] == note)
            return; // a bass note that doubles a chord tone plays once
      mNotes[mNumNotes++] = note;
   }

   void StrikeChord(int mode, int offset)
   {
      const double strumSamples =
         (mode == ChordProgressionNode::kBlock) ? 0.0
                                                : std::clamp(mStrumMs.load(std::memory_order_relaxed), 0.0f, 250.0f) *
                                                     mSampleRate / 1000.0;
      for (int i = 0; i < mNumNotes; i++)
      {
         const int n = (mode == ChordProgressionNode::kStrumDown) ? mNotes[mNumNotes - 1 - i] : mNotes[i];
         const int id = NextVoiceId();
         const uint64_t delay = (uint64_t)(strumSamples * (double)i);
         if (delay == 0)
            EmitOn(n, id, offset);
         else
            Schedule(mSamplePos + (uint64_t)offset + delay, n, true, id);
      }
   }

   void PlayStep(int mode, int step, int offset)
   {
      const int n = mNumNotes;
      const int octaves = std::clamp(mOctaves.load(std::memory_order_relaxed), 1, 3);
      const int span = n * octaves; // arp notes across octaves
      auto arpNote = [&](int i) { return mNotes[i % n] + 12 * (i / n); };

      switch (mode)
      {
         case ChordProgressionNode::kArpUp:
            EmitOn(arpNote(step % span), NextVoiceId(), offset);
            break;
         case ChordProgressionNode::kArpDown:
            EmitOn(arpNote(span - 1 - step % span), NextVoiceId(), offset);
            break;
         case ChordProgressionNode::kArpUpDown:
         {
            const int period = span > 1 ? 2 * span - 2 : 1;
            const int p = step % period;
            EmitOn(arpNote(p < span ? p : period - p), NextVoiceId(), offset);
            break;
         }
         case ChordProgressionNode::kArpRandom:
         {
            mRng = mRng * 1664525u + 1013904223u;
            EmitOn(arpNote((int)((mRng >> 8) % (uint32_t)span)), NextVoiceId(), offset);
            break;
         }
         case ChordProgressionNode::kPulse:
            for (int i = 0; i < n; i++)
               EmitOn(mNotes[i], NextVoiceId(), offset);
            break;
         case ChordProgressionNode::kAlberti:
         {
            // low - high - middle - high
            const int pat = step % 4;
            const int i = (pat == 0) ? 0 : (pat == 2 ? n / 2 : n - 1);
            EmitOn(mNotes[i], NextVoiceId(), offset);
            break;
         }
         case ChordProgressionNode::kBassChord:
         default:
            if (step % 2 == 0 || n == 1)
               EmitOn(mNotes[0], NextVoiceId(), offset);
            else
               for (int i = 1; i < n; i++)
                  EmitOn(mNotes[i], NextVoiceId(), offset);
            break;
      }
   }

   NoteEventQueue mOutbox;
   double mSampleRate = 48000.0;
   uint64_t mSamplePos = 0;

   Sounding mSounding[kMaxSounding] = {};
   int mNumSounding = 0;
   Pending mPending[kMaxPending] = {};
   int mNumPending = 0;
   NoteEvent mOut[kMaxOut];
   int mNumOut = 0;

   int mNotes[kMaxNotes] = {};
   int mNumNotes = 0;
   int mNextStep = 0;
   long long mLastKey = -1;
   uint32_t mRng = 0x2545F491u;

   std::atomic<int> mCount { 4 };
   std::atomic<int> mBaseOctave { 3 };
   std::atomic<int> mTranspose { 0 };
   std::atomic<float> mVelocity { 0.8f };
   std::atomic<float> mGate { 1.0f };
   std::atomic<bool> mBass { true };
   std::atomic<int> mMode { 0 };
   std::atomic<int> mRateDiv { 9 };
   std::atomic<float> mStrumMs { 40.0f };
   std::atomic<int> mOctaves { 1 };
   std::atomic<int> mMask[ChordProgressionNode::kMaxChords] = {};
   std::atomic<float> mBars[ChordProgressionNode::kMaxChords] = {};
   std::atomic<int> mPlaying { -1 };
   std::atomic<float> mProgress { 0.0f };
};

// ------------------------------------------------------------- main half
ChordProgressionNode::ChordProgressionNode()
{
   for (int i = 0; i < kMaxChords; i++)
   {
      chordMask[i] = 0;
      chordBars[i] = 1.0f;
   }
   // I - vi - IV - V in C, root position around C3, so the node plays
   // something the moment it is patched into a synth.
   chordMask[0] = BuildMask(0, 0);   // C
   chordMask[1] = BuildMask(9, 1);   // Am
   chordMask[2] = BuildMask(5, 0);   // F
   chordMask[3] = BuildMask(7, 0);   // G
}

ChordProgressionNode::~ChordProgressionNode() = default;

void ChordProgressionNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioChordProgressionNode>();
   chordCount = std::clamp(chordCount, 1, kMaxChords);
   mAudioNode->PushParams(*this);

   // Publish the playing chord's key (main thread, once per chord change).
   const int playing = PlayingIndex();
   if (setsKey && playing >= 0 && playing != mLastPublished)
   {
      int root = 0, quality = -1;
      AnalyseMask(chordMask[playing], root, quality);
      if (chordMask[playing] != 0)
      {
         Transport::Instance().SetKey(((root + transpose) % 12 + 12) % 12);
         if (quality >= 0 && ScaleForQuality(quality) >= 0)
            Transport::Instance().SetScale(ScaleForQuality(quality));
      }
   }
   mLastPublished = playing;
}

void ChordProgressionNode::VisitParams(ParamVisitor& v)
{
   v.Int("chordCount", chordCount);
   v.Int("baseOctave", baseOctave);
   v.Int("transpose", transpose);
   v.Float("velocity", velocity);
   v.Float("gate", gate);
   v.Bool("bass", bass);
   v.Int("selected", selected);
   v.Int("builderRoot", builderRoot);
   v.Int("builderQuality", builderQuality);
   v.Int("playMode", playMode);
   v.Int("rateDiv", rateDiv);
   v.Float("strumMs", strumMs);
   v.Int("arpOctaves", arpOctaves);
   v.Bool("setsKey", setsKey);
   for (int i = 0; i < kMaxChords; i++)
   {
      char key[16];
      snprintf(key, sizeof(key), "mask%d", i);
      v.Int(key, chordMask[i]);
      snprintf(key, sizeof(key), "bars%d", i);
      v.Float(key, chordBars[i]);
   }
}

AudioNode* ChordProgressionNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioChordProgressionNode>();
   return mAudioNode.get();
}

int ChordProgressionNode::PlayingIndex() const
{
   return mAudioNode ? mAudioNode->Playing() : -1;
}

float ChordProgressionNode::PlayingProgress() const
{
   return mAudioNode ? mAudioNode->Progress() : 0.0f;
}

float ChordProgressionNode::TotalBars() const
{
   float total = 0.0f;
   for (int i = 0; i < std::clamp(chordCount, 1, kMaxChords); i++)
      total += std::clamp(chordBars[i], 0.5f, 16.0f);
   return total;
}

std::string ChordProgressionNode::ChordName(int index) const
{
   if (index < 0 || index >= kMaxChords)
      return "";
   return NameForMask(chordMask[index]);
}

const std::vector<std::string>& ChordProgressionNode::PlayModeNames()
{
   static const std::vector<std::string> names = {
      "block", "strum up", "strum down", "arp up", "arp down", "arp up-down", "arp random",
      "pulse", "alberti", "bass + chord"
   };
   return names;
}

const std::vector<std::string>& ChordProgressionNode::QualityNames()
{
   static const std::vector<std::string> names = [] {
      std::vector<std::string> v;
      for (const Quality& q : kQualities)
         v.push_back(q.name);
      return v;
   }();
   return names;
}

int ChordProgressionNode::BuildMask(int rootPc, int quality)
{
   const Quality& q = kQualities[std::clamp(quality, 0, kNumQualities - 1)];
   const int root = ((rootPc % 12) + 12) % 12;
   int mask = 0;
   for (int i = 0; i < q.count; i++)
   {
      int k = root + q.intervals[i];
      while (k >= kKeys)
         k -= 12; // a 9th above a high root folds down inside the keyboard
      mask |= 1 << k;
   }
   return mask;
}

int ChordProgressionNode::InvertMask(int mask)
{
   // Next inversion: the lowest note moves up an octave, if it fits.
   for (int k = 0; k < kKeys; k++)
      if (mask & (1 << k))
      {
         if (k + 12 < kKeys && !(mask & (1 << (k + 12))))
            return (mask & ~(1 << k)) | (1 << (k + 12));
         return mask;
      }
   return mask;
}

bool ChordProgressionNode::AnalyseMask(int mask, int& rootPc, int& quality)
{
   rootPc = 0;
   quality = -1;
   if (mask == 0)
      return false;
   int bass = 0;
   while (bass < kKeys && !(mask & (1 << bass)))
      bass++;
   const int bassPc = bass % 12;
   rootPc = bassPc;
   const int set = PcSetOfMask(mask);
   int roots[12];
   int numRoots = 0;
   roots[numRoots++] = bassPc;
   for (int pc = 0; pc < 12; pc++)
      if (pc != bassPc && (set & (1 << pc)))
         roots[numRoots++] = pc;
   for (int r = 0; r < numRoots; r++)
      for (int q = 0; q < kNumQualities; q++)
         if (PcSetOfQuality(kQualities[q], roots[r]) == set)
         {
            rootPc = roots[r];
            quality = q;
            return true;
         }
   return false;
}

int ChordProgressionNode::ScaleForQuality(int quality)
{
   // Index order matches kQualities.
   static const int kScale[] = {
      MusicTime::kMajor,          // maj
      MusicTime::kNaturalMinor,   // min
      MusicTime::kMixolydian,     // 7
      MusicTime::kMajor,          // maj7
      MusicTime::kDorian,         // m7
      MusicTime::kLocrian,        // dim
      MusicTime::kWholeTone,      // aug
      MusicTime::kMajor,          // sus2
      MusicTime::kMixolydian,     // sus4
      MusicTime::kMajor,          // 6
      MusicTime::kDorian,         // m6
      MusicTime::kMixolydian,     // 9
      MusicTime::kMajor,          // maj9
      MusicTime::kDorian,         // m9
      MusicTime::kLocrian,        // m7b5
      -1,                         // dim7: no 7-note scale here fits it, keep the current one
      MusicTime::kMajor,          // add9
      MusicTime::kMixolydian,     // 7sus4
      MusicTime::kMelodicMinor,   // m(maj7)
      MusicTime::kMinorPentatonic // 5
   };
   static_assert(sizeof(kScale) / sizeof(kScale[0]) == (size_t)kNumQualities, "one scale per quality");
   return kScale[std::clamp(quality, 0, kNumQualities - 1)];
}

std::string ChordProgressionNode::NameForMask(int mask)
{
   if (mask == 0)
      return "rest";
   int bass = 0;
   while (bass < kKeys && !(mask & (1 << bass)))
      bass++;
   const int bassPc = bass % 12;
   const int set = PcSetOfMask(mask);

   // Try the bass note as the root first, then every other chord tone, so a
   // root-position chord never reads as some rarer inversion of another.
   int roots[12];
   int numRoots = 0;
   roots[numRoots++] = bassPc;
   for (int pc = 0; pc < 12; pc++)
      if (pc != bassPc && (set & (1 << pc)))
         roots[numRoots++] = pc;

   for (int r = 0; r < numRoots; r++)
      for (const Quality& q : kQualities)
         if (PcSetOfQuality(q, roots[r]) == set)
         {
            std::string name = std::string(kPcNames[roots[r]]) + q.suffix;
            if (roots[r] != bassPc)
               name += std::string("/") + kPcNames[bassPc];
            return name;
         }

   if ((set & (set - 1)) == 0)
      return kPcNames[bassPc]; // a single note
   std::string name;
   for (int pc = 0; pc < 12; pc++)
      if (set & (1 << pc))
      {
         if (!name.empty())
            name += " ";
         name += kPcNames[pc];
      }
   return name;
}
