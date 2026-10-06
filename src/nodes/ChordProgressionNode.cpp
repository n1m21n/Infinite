#include "ChordProgressionNode.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cctype>

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/MusicTime.h"
#include "audio/NoteEvent.h"
#include "audio/NoteEventQueue.h"
#include "audio/QuantizedRestart.h"
#include "core/Transport.h"

namespace
{
   struct Quality
   {
      const char* name;   // shown in the builder dropdown
      // Appended to the root in chord names. Turbo 0.50: the UI font is
      // all caps, so "Am" and "AM" read alike: qualities are spelled out
      // ("C MAJ", "A MIN7"); a bare number stays a dominant / plain chord
      // ("C7", "C6", "C5"). Names are display only, never parsed back.
      const char* suffix;
      int intervals[6];
      int count;
   };

   // Order is the saved builderQuality index: append only. Turbo 0.50:
   // minor qualities read "min..." in the builder too (an uppercase "m7"
   // looked like "M7", the usual shorthand for maj7).
   const Quality kQualities[] = {
      { "maj", " MAJ", { 0, 4, 7 }, 3 },
      { "min", " MIN", { 0, 3, 7 }, 3 },
      { "7", "7", { 0, 4, 7, 10 }, 4 },
      { "maj7", " MAJ7", { 0, 4, 7, 11 }, 4 },
      { "min7", " MIN7", { 0, 3, 7, 10 }, 4 },
      { "dim", " DIM", { 0, 3, 6 }, 3 },
      { "aug", " AUG", { 0, 4, 8 }, 3 },
      { "sus2", " SUS2", { 0, 2, 7 }, 3 },
      { "sus4", " SUS4", { 0, 5, 7 }, 3 },
      { "6", "6", { 0, 4, 7, 9 }, 4 },
      { "min6", " MIN6", { 0, 3, 7, 9 }, 4 },
      { "9", "9", { 0, 4, 7, 10, 14 }, 5 },
      { "maj9", " MAJ9", { 0, 4, 7, 11, 14 }, 5 },
      { "min9", " MIN9", { 0, 3, 7, 10, 14 }, 5 },
      { "min7b5", " MIN7b5", { 0, 3, 6, 10 }, 4 },
      { "dim7", " DIM7", { 0, 3, 6, 9 }, 4 },
      { "add9", " ADD9", { 0, 4, 7, 14 }, 4 },
      { "7sus4", "7 SUS4", { 0, 5, 7, 10 }, 4 },
      { "min(maj7)", " MIN(MAJ7)", { 0, 3, 7, 11 }, 4 },
      { "5", "5", { 0, 7 }, 2 },
      // Turbo 0.47: extended, altered and voicing qualities. 13ths drop the
      // 11th, 7alt drops the 5th (six notes max). Intervals above 12 are
      // voiced in the upper octave of the two-octave keyboard.
      { "11 (9sus4)", "11", { 0, 7, 10, 14, 17 }, 5 },
      { "min11", " MIN11", { 0, 3, 7, 10, 14, 17 }, 6 },
      { "maj7#11", " MAJ7#11", { 0, 4, 7, 11, 18 }, 5 },
      { "maj9#11", " MAJ9#11", { 0, 4, 7, 11, 14, 18 }, 6 },
      { "13", "13", { 0, 4, 7, 10, 14, 21 }, 6 },
      { "min13", " MIN13", { 0, 3, 7, 10, 14, 21 }, 6 },
      { "maj13", " MAJ13", { 0, 4, 7, 11, 14, 21 }, 6 },
      { "13sus4", "13 SUS4", { 0, 5, 7, 10, 14, 21 }, 6 },
      { "9#11", "9#11", { 0, 4, 7, 10, 14, 18 }, 6 },
      { "min(maj9)", " MIN(MAJ9)", { 0, 3, 7, 11, 14 }, 5 },
      { "7b9", "7b9", { 0, 4, 7, 10, 13 }, 5 },
      { "7#9", "7#9", { 0, 4, 7, 10, 15 }, 5 },
      { "7#11", "7#11", { 0, 4, 7, 10, 18 }, 5 },
      { "7b13", "7b13", { 0, 4, 7, 10, 20 }, 5 },
      { "7alt", "7 ALT", { 0, 4, 10, 13, 15, 20 }, 6 },
      { "min7b9", " MIN7b9", { 0, 3, 7, 10, 13 }, 5 },
      { "7#5", "7#5", { 0, 4, 8, 10 }, 4 },
      { "7b5", "7b5", { 0, 4, 6, 10 }, 4 },
      { "maj7#5", " MAJ7#5", { 0, 4, 8, 11 }, 4 },
      { "dim(maj7)", " DIM(MAJ7)", { 0, 3, 6, 11 }, 4 },
      { "6/9", "6/9", { 0, 4, 7, 9, 14 }, 5 },
      { "min6/9", " MIN6/9", { 0, 3, 7, 9, 14 }, 5 },
      { "min add9", " MIN ADD9", { 0, 3, 7, 14 }, 4 },
      { "add11", " ADD11", { 0, 4, 7, 17 }, 4 },
      { "quartal (4ths)", " QUARTAL", { 0, 5, 10, 15 }, 4 },
      { "so what", " SO WHAT", { 0, 5, 10, 15, 19 }, 5 },
      // Turbo 0.51: appended.
      { "maj11", " MAJ11", { 0, 4, 7, 11, 14, 17 }, 6 },
      { "min(maj11)", " MIN(MAJ11)", { 0, 3, 7, 11, 14, 17 }, 6 },
      { "7(9,11)", "7(9,11)", { 0, 4, 7, 10, 14, 17 }, 6 },
   };
   constexpr int kNumQualities = (int)(sizeof(kQualities) / sizeof(kQualities[0]));

   const char* const kPcNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

   int PcSetOfMask(uint64_t mask)
   {
      int set = 0;
      for (int k = 0; k < ChordProgressionNode::kKeysTotal; k++)
         if (mask & (1ull << k))
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
// Timing: the chord index comes from the transport position minus the
// restart anchor (Turbo 0.50), measured from the block start. Everything inside a chord - strum delays, arp and
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
      mNumPrev = 0;
      mPrevSeen = mPrevSeq.load(std::memory_order_relaxed);
      mLastKey = kNoKey;
      mSamplePos = 0;
      mPlaying.store(-1, std::memory_order_relaxed);
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      const int numFrames = std::max(1, output.numFrames);
      mNumOut = 0;
      Transport& transport = Transport::Instance();
      ProcessPreview(numFrames);

      if (!transport.IsPlaying())
      {
         ReleaseAll(0);
         mLastKey = kNoKey;
         // A rewind to 0 drops the restart anchor so a fresh start is
         // bar 1 = chord 1, as before.
         mRestart.Update(false, 0.0, 4.0);
         if (transport.Beats() <= 1.0e-9)
            mAnchor = 0.0;
         mPlaying.store(-1, std::memory_order_relaxed);
         Flush();
         mSamplePos += (uint64_t)numFrames;
         return;
      }

      const int count = std::clamp(mCount.load(std::memory_order_relaxed), 1, ChordProgressionNode::kMaxChords);
      double total = 0.0;
      for (int i = 0; i < count; i++)
         total += (double)std::clamp(mBars[i].load(std::memory_order_relaxed), ChordProgressionNode::kMinBars, 16.0f);

      const double beatsPerBar = std::max(0.25, transport.BeatsPerBar());
      const double bpm = std::max(1.0, (double)transport.Tempo());
      const double samplesPerBeat = mSampleRate * 60.0 / bpm;
      // Turbo 0.50: Beats() inside ProcessBlock is the block END (the clock
      // advances before the nodes run), so the block starts one block back.
      const double beats1 = std::max(0.0, transport.Beats());
      const double beats0 = std::max(0.0, beats1 - (double)numFrames / samplesPerBeat);
      const int mode = std::clamp(mMode.load(std::memory_order_relaxed), 0, ChordProgressionNode::kNumPlayModes - 1);
      const float gate = std::clamp(mGate.load(std::memory_order_relaxed), 0.05f, 1.0f);
      const bool stepMode = ChordProgressionNode::IsStepMode(mode);
      const double stepBeats = std::max(1.0 / 64.0, MusicTime::BeatsFor((MusicTime::RateDivision)std::clamp(
                                  mRateDiv.load(std::memory_order_relaxed), 0, MusicTime::kNumRateDivisions - 1)));

      mRestart.Update(true, beats0, beatsPerBar); // Turbo 0.50

      // A block can cross a chord boundary (or the restart line); each
      // segment is handled at its own offset so changes land on the beat.
      double segBeat = beats0;
      int idx = 0;
      double inChord = 0.0;
      for (int seg = 0; seg < 8; seg++)
      {
         if (mRestart.IsArmed() && segBeat >= mRestart.ArmBeat() - 1.0e-9)
         {
            // Restart: chord 1 begins exactly here.
            segBeat = std::max(segBeat, mRestart.ArmBeat());
            mAnchor = mRestart.ArmBeat();
            mRestart.Fire();
            mLastKey = kNoKey; // re-strike even if chord 1 is already playing
         }
         const double bars = (segBeat - mAnchor) / beatsPerBar;
         const long long cycle = (long long)std::floor(bars / total);
         const double pos = bars - (double)cycle * total;
         double start = 0.0;
         double len = 1.0;
         for (idx = 0; idx < count; idx++)
         {
            len = (double)std::clamp(mBars[idx].load(std::memory_order_relaxed), ChordProgressionNode::kMinBars, 16.0f);
            if (pos < start + len || idx == count - 1)
               break;
            start += len;
         }
         inChord = std::clamp((pos - start) / len, 0.0, 1.0);
         const double chordStartBeat = mAnchor + ((double)cycle * total + start) * beatsPerBar;
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

         // The segment also ends at an armed restart line inside this chord.
         const bool restartFirst = mRestart.IsArmed() && mRestart.ArmBeat() < chordEndBeat;
         const double boundary = restartFirst ? mRestart.ArmBeat() : chordEndBeat;
         const double segEnd = std::min(beats1, boundary);
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

         if (boundary >= beats1)
            break;
         segBeat = restartFirst ? boundary : chordEndBeat + 1.0e-9;
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
      mRestart.SetQuant(n.restartQuant);
      for (int i = 0; i < ChordProgressionNode::kMaxChords; i++)
      {
         mMask[i].store(n.chordMask[i], std::memory_order_relaxed);
         mMaskHi[i].store(n.maskHi[i], std::memory_order_relaxed);
         mSlash[i].store(n.slashBass[i], std::memory_order_relaxed);
         mBars[i].store(n.chordBars[i], std::memory_order_relaxed);
      }
   }

   // Main thread: keyboard audition (Turbo 0.51). Mask first, then the
   // sequence number the audio thread watches.
   void Preview(uint64_t fullMask, bool on)
   {
      mPrevLo.store((int)(fullMask & 0xFFFFFFu), std::memory_order_relaxed);
      mPrevHi.store((int)((fullMask >> 24) & 0xFFFFFFu), std::memory_order_relaxed);
      mPrevOn.store(on, std::memory_order_relaxed);
      mPrevSeq.fetch_add(1, std::memory_order_release);
   }

   int Playing() const { return mPlaying.load(std::memory_order_relaxed); }
   // Main thread: arm a restart (Turbo 0.50).
   void RequestRestart() { mRestart.Request(); }
   bool Armed() const { return mRestart.Armed(); }
   float Progress() const { return mProgress.load(std::memory_order_relaxed); }

private:
   static constexpr int kMaxNotes = ChordProgressionNode::kKeysTotal + 2;
   static constexpr int kMaxSounding = kMaxNotes * 3;
   static constexpr int kMaxPending = 128;
   static constexpr int kMaxOut = 256;
   // Never a real chord key: cycles go negative once a restart anchor sits
   // ahead of a rewound transport, so -1 is no longer free.
   static constexpr long long kNoKey = LLONG_MIN;

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

   int BaseNote() const
   {
      return (std::clamp(mBaseOctave.load(std::memory_order_relaxed), -1, 7) + 1) * 12 +
             std::clamp(mTranspose.load(std::memory_order_relaxed), -24, 24);
   }

   // ---- keyboard audition: its own note list, so chord changes and
   // ReleaseAll never touch it and it never touches the playing chord.
   void ProcessPreview(int numFrames)
   {
      const int seq = mPrevSeq.load(std::memory_order_acquire);
      if (seq != mPrevSeen)
      {
         mPrevSeen = seq;
         ReleasePreview();
         if (mPrevOn.load(std::memory_order_relaxed))
         {
            const uint64_t mask = (uint64_t)((unsigned)mPrevLo.load(std::memory_order_relaxed) & 0xFFFFFFu) |
                                  ((uint64_t)((unsigned)mPrevHi.load(std::memory_order_relaxed) & 0xFFFFFFu) << 24);
            const int base = BaseNote();
            for (int k = 0; k < ChordProgressionNode::kKeysTotal && mNumPrev < kMaxNotes; k++)
               if ((mask & (1ull << k)) && base + k >= 0 && base + k <= 127 && mNumOut < kMaxOut)
               {
                  const int id = NextVoiceId();
                  NoteEvent& e = mOut[mNumOut++];
                  e = NoteEvent();
                  e.note = base + k;
                  e.velocity = std::clamp(mVelocity.load(std::memory_order_relaxed), 0.0f, 1.0f);
                  e.isNoteOn = true;
                  e.frameOffset = 0;
                  e.source = this;
                  e.voiceId = id;
                  mPrev[mNumPrev++] = { base + k, id };
               }
            mPrevAge = 0;
         }
      }
      else if (mNumPrev > 0)
      {
         mPrevAge += numFrames;
         if ((double)mPrevAge > 1.5 * mSampleRate)
            ReleasePreview();
      }
   }

   void ReleasePreview()
   {
      for (int i = 0; i < mNumPrev; i++)
         if (mNumOut < kMaxOut)
         {
            NoteEvent& e = mOut[mNumOut++];
            e = NoteEvent();
            e.note = mPrev[i].note;
            e.velocity = 0.0f;
            e.isNoteOn = false;
            e.frameOffset = 0;
            e.source = this;
            e.voiceId = mPrev[i].voiceId;
         }
      mNumPrev = 0;
   }

   // ---- the chord's notes, ascending, bass included
   void BuildNotes(int idx)
   {
      mNumNotes = 0;
      const uint64_t mask = (uint64_t)((unsigned)mMask[idx].load(std::memory_order_relaxed) & 0xFFFFFFu) |
                            ((uint64_t)((unsigned)mMaskHi[idx].load(std::memory_order_relaxed) & 0xFFFFFFu) << 24);
      if (mask == 0)
         return; // an empty chord is a rest
      const int base = BaseNote();
      int lowest = -1;
      for (int k = 0; k < ChordProgressionNode::kKeysTotal && lowest < 0; k++)
         if (mask & (1ull << k))
            lowest = k;
      const int slash = mSlash[idx].load(std::memory_order_relaxed);
      if (slash >= 0 && slash < 12 && lowest >= 0)
      {
         // Slash bass: the nearest such pitch class below the lowest key.
         const int below = lowest - 1;
         AddNote(base + below - (((below - slash) % 12) + 12) % 12);
      }
      else if (mBass.load(std::memory_order_relaxed) && lowest >= 0)
         AddNote(base + lowest - 12);
      for (int k = 0; k < ChordProgressionNode::kKeysTotal; k++)
         if (mask & (1ull << k))
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
   long long mLastKey = kNoKey;

   // Restart (Turbo 0.50).
   QuantizedRestart mRestart;
   double mAnchor = 0.0; // transport beat where chord 1 of cycle 0 starts
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
   std::atomic<int> mMaskHi[ChordProgressionNode::kMaxChords] = {};
   std::atomic<int> mSlash[ChordProgressionNode::kMaxChords] = {};

   // Keyboard audition.
   std::atomic<int> mPrevLo { 0 };
   std::atomic<int> mPrevHi { 0 };
   std::atomic<bool> mPrevOn { false };
   std::atomic<int> mPrevSeq { 0 };
   int mPrevSeen = 0;
   Sounding mPrev[kMaxNotes] = {};
   int mNumPrev = 0;
   int mPrevAge = 0; // samples since the audition started
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
      maskHi[i] = 0;
      slashBass[i] = -1;
   }
   // I - vi - IV - V in C, root position around C3, so the node plays
   // something the moment it is patched into a synth.
   SetFullMask(0, BuildMask(0, 0));   // C
   SetFullMask(1, BuildMask(9, 1));   // Am
   SetFullMask(2, BuildMask(5, 0));   // F
   SetFullMask(3, BuildMask(7, 0));   // G
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
      AnalyseMask(FullMask(playing), root, quality, slashBass[playing]);
      if (FullMask(playing) != 0)
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
   v.Int("restartQuant", restartQuant); // Turbo 0.50, appended
   // Turbo 0.51, appended: keys 24..47 and the slash bass per chord.
   for (int i = 0; i < kMaxChords; i++)
   {
      char key[16];
      snprintf(key, sizeof(key), "maskHi%d", i);
      v.Int(key, maskHi[i]);
      snprintf(key, sizeof(key), "slashBass%d", i);
      v.Int(key, slashBass[i]);
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

void ChordProgressionNode::RequestRestart()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioChordProgressionNode>();
   mAudioNode->RequestRestart();
}

bool ChordProgressionNode::RestartArmed() const
{
   return mAudioNode ? mAudioNode->Armed() : false;
}

const std::vector<std::string>& ChordProgressionNode::RestartQuantNames()
{
   return QuantizedRestart::Names();
}

float ChordProgressionNode::PlayingProgress() const
{
   return mAudioNode ? mAudioNode->Progress() : 0.0f;
}

float ChordProgressionNode::TotalBars() const
{
   float total = 0.0f;
   for (int i = 0; i < std::clamp(chordCount, 1, kMaxChords); i++)
      total += std::clamp(chordBars[i], kMinBars, 16.0f);
   return total;
}

std::string ChordProgressionNode::ChordName(int index) const
{
   if (index < 0 || index >= kMaxChords)
      return "";
   return NameForMask(FullMask(index), slashBass[index]);
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

const std::vector<std::string>& ChordProgressionNode::QualityCategories()
{
   // Parallel to kQualities: the builder dropdown groups by these, in this
   // order (QualityDisplayOrder).
   static const std::vector<std::string> cats = [] {
      static const char* const kCat[] = {
         "triads", "triads", "7ths", "7ths", "7ths", "triads", "triads", "triads", "triads", "6ths & adds",
         "6ths & adds", "9ths, 11ths, 13ths", "9ths, 11ths, 13ths", "9ths, 11ths, 13ths", "7ths", "7ths",
         "6ths & adds", "7ths", "7ths", "triads",
         "9ths, 11ths, 13ths", "9ths, 11ths, 13ths", "9ths, 11ths, 13ths", "9ths, 11ths, 13ths",
         "9ths, 11ths, 13ths", "9ths, 11ths, 13ths", "9ths, 11ths, 13ths", "9ths, 11ths, 13ths",
         "9ths, 11ths, 13ths", "9ths, 11ths, 13ths",
         "altered", "altered", "altered", "altered", "altered", "altered",
         "7ths", "7ths", "7ths", "7ths",
         "6ths & adds", "6ths & adds", "6ths & adds", "6ths & adds",
         "voicings", "voicings",
         "9ths, 11ths, 13ths", "9ths, 11ths, 13ths", "9ths, 11ths, 13ths",
      };
      static_assert(sizeof(kCat) / sizeof(kCat[0]) == (size_t)kNumQualities, "one category per quality");
      return std::vector<std::string>(kCat, kCat + kNumQualities);
   }();
   return cats;
}

const std::vector<int>& ChordProgressionNode::QualityDisplayOrder()
{
   static const std::vector<int> order = [] {
      static const char* const kRank[] = { "triads", "6ths & adds", "7ths", "9ths, 11ths, 13ths", "altered", "voicings" };
      std::vector<int> v;
      for (const char* rank : kRank)
         for (int q = 0; q < kNumQualities; q++)
            if (QualityCategories()[q] == rank)
               v.push_back(q);
      return v;
   }();
   return order;
}

uint64_t ChordProgressionNode::BuildMask(int rootPc, int quality)
{
   const Quality& q = kQualities[std::clamp(quality, 0, kNumQualities - 1)];
   const int root = ((rootPc % 12) + 12) % 12;
   uint64_t mask = 0;
   for (int i = 0; i < q.count; i++)
   {
      int k = root + q.intervals[i];
      while (k >= kKeys)
         k -= 12; // a 9th above a high root folds down inside the first two octaves
      mask |= 1ull << k;
   }
   return mask;
}

namespace
{
   int LowestKey(uint64_t mask)
   {
      for (int k = 0; k < ChordProgressionNode::kKeysTotal; k++)
         if (mask & (1ull << k))
            return k;
      return -1;
   }

   int PopCount(uint64_t v)
   {
      int n = 0;
      for (; v != 0; v &= v - 1)
         n++;
      return n;
   }

   // Interval analysis for sets the table does not know (Turbo 0.51): per
   // candidate root, name the third (or sus), the fifth, the seventh and the
   // extensions; the best-scoring root wins (bass as root, a third present,
   // fewest alterations).
   struct Analysis
   {
      int root = 0;
      std::string suffix;
      int family = -1;
      int score = -1000;
   };

   bool AnalyseIntervals(int set, int bassPc, Analysis& best)
   {
      if (PopCount((uint64_t)set) < 3)
         return false;
      bool found = false;
      for (int r = 0; r < 12; r++)
      {
         if (!(set & (1 << r)))
            continue;
         int iv = 0;
         for (int i = 0; i < 12; i++)
            if (set & (1 << ((r + i) % 12)))
               iv |= 1 << i;
         auto has = [&](int i) { return ((iv >> i) & 1) != 0; };
         const bool m3 = has(3), M3 = has(4), t3 = m3 || M3;
         const bool p5 = has(7), b5 = has(6), s5 = has(8);
         const bool minor = m3 && !M3;
         const bool dim = minor && b5 && !p5;
         const bool aug = M3 && s5 && !p5;
         int sus = 0;
         if (!t3)
            sus = has(5) ? 4 : (has(2) ? 2 : 0);
         int sev = 0; // 1 b7, 2 maj7, 3 six, 4 dim7
         if (has(10)) sev = 1;
         else if (has(11)) sev = 2;
         else if (has(9) && dim) sev = 4;
         else if (has(9)) sev = 3;

         std::string suf;
         int fam = -1;
         std::vector<std::string> alt;
         if (t3)
         {
            if (dim)
            {
               suf = sev == 4 ? " DIM7" : sev == 1 ? " MIN7b5" : sev == 2 ? " DIM(MAJ7)" : " DIM";
               fam = sev == 4 ? 15 : sev == 1 ? 14 : 5;
            }
            else if (minor)
            {
               suf = sev == 1 ? " MIN7" : sev == 2 ? " MIN(MAJ7)" : sev == 3 ? " MIN6" : " MIN";
               fam = sev == 1 ? 4 : sev == 2 ? 18 : sev == 3 ? 10 : 1;
               if (s5 && !p5)
                  alt.push_back("#5");
            }
            else
            {
               if (aug && sev == 1) { suf = "7#5"; fam = 2; }
               else if (aug && sev == 2) { suf = " MAJ7#5"; fam = 3; }
               else if (aug && sev == 0) { suf = " AUG"; fam = 6; }
               else
               {
                  suf = sev == 1 ? "7" : sev == 2 ? " MAJ7" : sev == 3 ? "6" : " MAJ";
                  fam = sev == 1 ? 2 : sev == 2 ? 3 : sev == 3 ? 9 : 0;
                  if (aug)
                     alt.push_back("#5");
               }
               if (b5 && !p5)
                  alt.push_back("b5");
            }
            if (!p5 && !b5 && !s5)
               alt.push_back("no5");
         }
         else if (sus != 0)
         {
            const char* head = sev == 1 ? "7" : sev == 2 ? " MAJ7" : sev == 3 ? "6" : "";
            suf = std::string(head) + (sus == 4 ? " SUS4" : " SUS2");
            fam = sus == 2 ? 7 : (sev == 1 ? 17 : 8);
            if (!p5 && b5)
               alt.push_back("b5");
            else if (!p5 && s5)
               alt.push_back("#5");
         }
         else
         {
            suf = p5 ? "5" : "";
            fam = p5 ? 19 : -1;
         }
         // Extensions, ascending.
         if (has(1)) alt.push_back("b9");
         if (has(2) && sus != 2) alt.push_back("9");
         if (has(3) && M3) alt.push_back("#9");
         if (has(5) && t3) alt.push_back("11");
         if (has(6) && p5) alt.push_back("#11");
         if (has(8) && p5) alt.push_back("b13");
         if (has(9) && (sev == 1 || sev == 2)) alt.push_back("13");
         if (!t3 && sus == 0 && !p5)
            alt.push_back("no3");

         int score = 20 - 3 * (int)alt.size();
         if (!t3 && sus == 0)
            score -= 4;
         if (r == bassPc)
            score += 3;
         if (t3)
            score += 1;
         if (!found || score > best.score)
         {
            found = true;
            best.root = r;
            best.score = score;
            best.family = fam;
            best.suffix = suf;
            if (!alt.empty())
            {
               best.suffix += "(";
               for (size_t i = 0; i < alt.size(); i++)
                  best.suffix += (i ? "," : "") + alt[i];
               best.suffix += ")";
            }
         }
      }
      return found;
   }

   // Roots to try for a table match: the bass first (a root-position chord
   // never reads as a rarer inversion), then every other chord tone.
   int RootCandidates(int set, int bassPc, int* roots)
   {
      int n = 0;
      if (set & (1 << bassPc))
         roots[n++] = bassPc;
      for (int pc = 0; pc < 12; pc++)
         if (pc != bassPc && (set & (1 << pc)))
            roots[n++] = pc;
      return n;
   }
}

uint64_t ChordProgressionNode::InvertMask(uint64_t mask)
{
   if (mask == 0)
      return mask;
   const int lowest = LowestKey(mask);
   int root = 0, quality = -1;
   AnalyseMask(mask, root, quality);
   uint64_t out = mask;
   if (lowest + 12 < kKeysTotal && !(mask & (1ull << (lowest + 12))))
   {
      out = (mask & ~(1ull << lowest)) | (1ull << (lowest + 12));
   }
   else
   {
      // No room for another inversion: wrap to root position.
      out = 0;
      for (int pc = 0; pc < 12; pc++)
         if (PcSetOfMask(mask) & (1 << pc))
            out |= 1ull << (root + (pc - root + 12) % 12);
      return out;
   }
   // Back on the root after the last inversion: sink to the low register.
   if (LowestKey(out) % 12 == root && PopCount(mask) > 1)
      while (LowestKey(out) >= 12)
         out >>= 12;
   return out;
}

bool ChordProgressionNode::AnalyseMask(uint64_t mask, int& rootPc, int& quality, int slash)
{
   rootPc = 0;
   quality = -1;
   if (mask == 0)
      return false;
   const int bassPc = (slash >= 0 && slash < 12) ? slash : LowestKey(mask) % 12;
   rootPc = LowestKey(mask) % 12;
   const int set = PcSetOfMask(mask);
   int roots[12];
   const int numRoots = RootCandidates(set, bassPc, roots);
   for (int r = 0; r < numRoots; r++)
      for (int q = 0; q < kNumQualities; q++)
         if (PcSetOfQuality(kQualities[q], roots[r]) == set)
         {
            rootPc = roots[r];
            quality = q;
            return true;
         }
   Analysis a;
   if (AnalyseIntervals(set, bassPc, a))
   {
      rootPc = a.root;
      quality = a.family;
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
      MusicTime::kMinorPentatonic, // 5
      MusicTime::kMixolydian,      // 11 (9sus4)
      MusicTime::kDorian,          // m11
      MusicTime::kLydian,          // maj7#11
      MusicTime::kLydian,          // maj9#11
      MusicTime::kMixolydian,      // 13
      MusicTime::kDorian,          // m13
      MusicTime::kMajor,           // maj13
      MusicTime::kMixolydian,      // 13sus4
      -1,                          // 9#11 (lydian dominant: not in the scale list)
      MusicTime::kMelodicMinor,    // m(maj9)
      -1,                          // 7b9
      -1,                          // 7#9
      -1,                          // 7#11
      -1,                          // 7b13
      -1,                          // 7alt
      MusicTime::kPhrygian,        // m7b9
      MusicTime::kWholeTone,       // 7#5
      MusicTime::kWholeTone,       // 7b5
      -1,                          // maj7#5
      -1,                          // dim(maj7)
      MusicTime::kMajorPentatonic, // 6/9
      MusicTime::kDorian,          // m6/9
      MusicTime::kNaturalMinor,    // madd9
      MusicTime::kMajor,           // add11
      MusicTime::kMinorPentatonic, // quartal
      MusicTime::kDorian,          // so what
      MusicTime::kMajor,           // maj11
      MusicTime::kMelodicMinor,    // m(maj11)
      MusicTime::kMixolydian       // 7(9,11)
   };
   static_assert(sizeof(kScale) / sizeof(kScale[0]) == (size_t)kNumQualities, "one scale per quality");
   return kScale[std::clamp(quality, 0, kNumQualities - 1)];
}

std::string ChordProgressionNode::NameForMask(uint64_t mask, int slash)
{
   if (mask == 0)
      return "rest";
   const int bassPc = (slash >= 0 && slash < 12) ? slash : LowestKey(mask) % 12;
   const int set = PcSetOfMask(mask);
   int roots[12];
   const int numRoots = RootCandidates(set, bassPc, roots);

   // Exact table matches first, so every 0.50 name is unchanged.
   for (int r = 0; r < numRoots; r++)
      for (const Quality& q : kQualities)
         if (PcSetOfQuality(q, roots[r]) == set)
         {
            std::string name = std::string(kPcNames[roots[r]]) + q.suffix;
            if (roots[r] != bassPc)
               name += std::string("/") + kPcNames[bassPc];
            return name;
         }

   Analysis a;
   if (AnalyseIntervals(set, bassPc, a))
   {
      std::string name = std::string(kPcNames[a.root]) + a.suffix;
      if (a.root != bassPc)
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

// ---------------------------------------------------------- extension chips
namespace
{
   const char* const kExtNames[ChordProgressionNode::kNumExtensions] = {
      "7", "maj7", "b9", "9", "#9", "11", "#11", "b13", "13", "add9", "sus2", "sus4", "no3", "no5"
   };

   bool RelHas(uint64_t mask, int root, int i)
   {
      return (PcSetOfMask(mask) >> ((root + i) % 12)) & 1;
   }

   uint64_t ClearPc(uint64_t mask, int pc)
   {
      for (int k = 0; k < ChordProgressionNode::kKeysTotal; k++)
         if (k % 12 == pc)
            mask &= ~(1ull << k);
      return mask;
   }

   // Adds the interval above the lowest key of the root pitch class.
   uint64_t AddInterval(uint64_t mask, int root, int semis)
   {
      int rootKey = root;
      for (int k = 0; k < ChordProgressionNode::kKeysTotal; k++)
         if ((mask & (1ull << k)) && k % 12 == root)
         {
            rootKey = k;
            break;
         }
      int k = rootKey + semis;
      while (k >= ChordProgressionNode::kKeysTotal)
         k -= 12;
      return mask | (1ull << k);
   }
}

const char* ChordProgressionNode::ExtensionName(int ext)
{
   return kExtNames[std::clamp(ext, 0, kNumExtensions - 1)];
}

bool ChordProgressionNode::HasExtension(uint64_t mask, int root, int ext)
{
   if (mask == 0)
      return false;
   const bool third = RelHas(mask, root, 3) || RelHas(mask, root, 4);
   const bool seventh = RelHas(mask, root, 10) || RelHas(mask, root, 11);
   switch (ext)
   {
      case 0: return RelHas(mask, root, 10);
      case 1: return RelHas(mask, root, 11);
      case 2: return RelHas(mask, root, 1);
      case 3: return RelHas(mask, root, 2) && seventh;
      case 4: return RelHas(mask, root, 3) && RelHas(mask, root, 4);
      case 5: return RelHas(mask, root, 5) && third;
      case 6: return RelHas(mask, root, 6) && RelHas(mask, root, 7);
      case 7: return RelHas(mask, root, 8) && RelHas(mask, root, 7);
      case 8: return RelHas(mask, root, 9) && seventh;
      case 9: return RelHas(mask, root, 2) && !seventh && third;
      case 10: return RelHas(mask, root, 2) && !third;
      case 11: return RelHas(mask, root, 5) && !third;
      case 12: return !third;
      default: return !RelHas(mask, root, 7);
   }
}

uint64_t ChordProgressionNode::ToggleExtension(uint64_t mask, int root, int ext)
{
   root = ((root % 12) + 12) % 12;
   const bool on = HasExtension(mask, root, ext);
   if (mask == 0)
      mask = AddInterval(0, root, 0);
   const bool seventh = RelHas(mask, root, 10) || RelHas(mask, root, 11);
   switch (ext)
   {
      case 0: // 7 and maj7 replace each other
         mask = on ? ClearPc(mask, (root + 10) % 12) : AddInterval(ClearPc(mask, (root + 11) % 12), root, 10);
         break;
      case 1:
         mask = on ? ClearPc(mask, (root + 11) % 12) : AddInterval(ClearPc(mask, (root + 10) % 12), root, 11);
         break;
      case 2: mask = on ? ClearPc(mask, (root + 1) % 12) : AddInterval(mask, root, 13); break;
      case 3: // 9 and 13 imply a seventh (dominant unless one is there)
         mask = on ? ClearPc(mask, (root + 2) % 12) : AddInterval(seventh ? mask : AddInterval(mask, root, 10), root, 14);
         break;
      case 4: mask = on ? ClearPc(mask, (root + 3) % 12) : AddInterval(mask, root, 15); break;
      case 5: mask = on ? ClearPc(mask, (root + 5) % 12) : AddInterval(mask, root, 17); break;
      case 6: mask = on ? ClearPc(mask, (root + 6) % 12) : AddInterval(mask, root, 18); break;
      case 7: mask = on ? ClearPc(mask, (root + 8) % 12) : AddInterval(mask, root, 20); break;
      case 8:
         mask = on ? ClearPc(mask, (root + 9) % 12) : AddInterval(seventh ? mask : AddInterval(mask, root, 10), root, 21);
         break;
      case 9: mask = on ? ClearPc(mask, (root + 2) % 12) : AddInterval(mask, root, 14); break;
      case 10: // sus2 / sus4 / no3 swap the third; switching off restores a major third
         mask = on ? AddInterval(ClearPc(mask, (root + 2) % 12), root, 4)
                   : AddInterval(ClearPc(ClearPc(mask, (root + 3) % 12), (root + 4) % 12), root, 2);
         break;
      case 11:
         mask = on ? AddInterval(ClearPc(mask, (root + 5) % 12), root, 4)
                   : AddInterval(ClearPc(ClearPc(mask, (root + 3) % 12), (root + 4) % 12), root, 5);
         break;
      case 12:
         mask = on ? AddInterval(mask, root, 4) : ClearPc(ClearPc(mask, (root + 3) % 12), (root + 4) % 12);
         break;
      default:
         mask = on ? AddInterval(mask, root, 7) : ClearPc(mask, (root + 7) % 12);
         break;
   }
   return mask;
}

// ----------------------------------------------------------------- voicings
uint64_t ChordProgressionNode::Voice(uint64_t mask, int op)
{
   if (mask == 0)
      return mask;
   int keys[kKeysTotal];
   int n = 0;
   for (int k = 0; k < kKeysTotal; k++)
      if (mask & (1ull << k))
         keys[n++] = k;
   switch (op)
   {
      case 0: // close: every other note within an octave above the lowest
      {
         uint64_t out = 1ull << keys[0];
         for (int i = 1; i < n; i++)
            out |= 1ull << (keys[0] + (keys[i] - keys[0]) % 12);
         return out;
      }
      case 1: // drop 2: the second note from the top goes down an octave
      {
         if (n < 3)
            return mask;
         uint64_t m = mask;
         int k = keys[n - 2];
         if (k - 12 < 0)
         {
            if (keys[n - 1] + 12 >= kKeysTotal)
               return mask;
            m <<= 12;
            k += 12;
         }
         if (m & (1ull << (k - 12)))
            return mask;
         return (m & ~(1ull << k)) | (1ull << (k - 12));
      }
      case 2: // spread: every second note up an octave
      {
         if (n < 3)
            return mask;
         uint64_t out = mask;
         for (int i = 1; i < n; i += 2)
            if (keys[i] + 12 < kKeysTotal && !(out & (1ull << (keys[i] + 12))))
               out = (out & ~(1ull << keys[i])) | (1ull << (keys[i] + 12));
         return out;
      }
      case 3:
         return keys[n - 1] + 12 < kKeysTotal ? mask << 12 : mask;
      default:
         return keys[0] >= 12 ? mask >> 12 : mask;
   }
}

uint64_t ChordProgressionNode::VoiceLead(uint64_t prev, uint64_t mask)
{
   if (mask == 0)
      return mask;
   int pcs[12];
   int n = 0;
   const int set = PcSetOfMask(mask);
   for (int pc = 0; pc < 12; pc++)
      if (set & (1 << pc))
         pcs[n++] = pc;
   int pk[kKeysTotal];
   int pn = 0;
   for (int k = 0; k < kKeysTotal; k++)
      if (prev & (1ull << k))
         pk[pn++] = k;
   float pmean = 14.0f;
   if (pn > 0)
   {
      pmean = 0.0f;
      for (int i = 0; i < pn; i++)
         pmean += (float)pk[i];
      pmean /= (float)pn;
   }
   float bestCost = 1e30f;
   uint64_t best = mask;
   for (int inv = 0; inv < n; inv++)
      for (int oct = 0; oct < 4; oct++)
      {
         int cand[12];
         cand[0] = pcs[inv] + 12 * oct;
         for (int i = 1; i < n; i++)
         {
            const int pc = pcs[(inv + i) % n];
            int k = cand[i - 1] + 1;
            while (k % 12 != pc)
               k++;
            cand[i] = k;
         }
         if (cand[n - 1] >= kKeysTotal)
            continue;
         float mean = 0.0f;
         for (int i = 0; i < n; i++)
            mean += (float)cand[i];
         mean /= (float)n;
         float cost = 0.3f * std::fabs(mean - pmean);
         const int m = std::min(n, pn);
         for (int i = 0; i < m; i++) // top voices matched, as NoteTheory::VoiceChord does
            cost += (float)std::abs(cand[n - 1 - i] - pk[pn - 1 - i]);
         if (cost < bestCost)
         {
            bestCost = cost;
            best = 0;
            for (int i = 0; i < n; i++)
               best |= 1ull << cand[i];
         }
      }
   return best;
}

// ------------------------------------------------------------ text parsing
namespace
{
   std::string Lower(std::string s)
   {
      for (char& c : s)
         c = (char)std::tolower((unsigned char)c);
      return s;
   }

   std::string Trim(const std::string& s)
   {
      size_t a = 0, b = s.size();
      while (a < b && std::isspace((unsigned char)s[a]))
         a++;
      while (b > a && std::isspace((unsigned char)s[b - 1]))
         b--;
      return s.substr(a, b - a);
   }

   struct Alias { std::string text; int quality; bool caseSensitive; };

   const std::vector<Alias>& Aliases()
   {
      static const std::vector<Alias> list = [] {
         std::vector<Alias> v;
         for (int q = 0; q < kNumQualities; q++)
            v.push_back({ Lower(kQualities[q].name), q, false });
         const struct { const char* t; int q; bool cs; } extra[] = {
            { "", 0, false }, { "major", 0, false }, { "m", 1, true }, { "minor", 1, false }, { "-", 1, false },
            { "mi", 1, false }, { "m7", 4, true }, { "-7", 4, false }, { "mi7", 4, false }, { "m6", 10, true },
            { "m9", 13, true }, { "m7b5", 14, true }, { "-7b5", 14, false }, { "ø", 14, false }, { "m(maj7)", 18, true },
            { "mmaj7", 18, true }, { "m11", 21, true }, { "m13", 25, true }, { "m7b9", 35, true }, { "m6/9", 41, true },
            { "madd9", 42, true }, { "m(maj9)", 29, true }, { "m(maj11)", 47, true }, { "M7", 3, true },
            { "M9", 12, true }, { "M", 0, true }, { "\xCE\x94", 3, false }, { "+", 6, false }, { "o", 5, false },
            { "o7", 15, false }, { "sus", 8, false }, { "7sus", 17, false }, { "dom7", 2, false }, { "11", 20, false },
            { "dim7", 15, false }, { "m11", 21, true }, { "maj7#11", 22, false }, { "m(add9)", 42, false },
         };
         for (const auto& e : extra)
            v.push_back({ e.cs ? std::string(e.t) : Lower(e.t), e.q, e.cs });
         std::stable_sort(v.begin(), v.end(), [](const Alias& a, const Alias& b) { return a.text.size() > b.text.size(); });
         return v;
      }();
      return list;
   }

   // "(9,11)", "b9 #11", "no5": false on an unknown token.
   bool ApplyExtensionList(std::string text, std::vector<int>& iv)
   {
      text = Trim(text);
      if (text.empty())
         return true;
      if (text.front() == '(')
      {
         if (text.back() != ')')
            return false;
         text = text.substr(1, text.size() - 2);
      }
      for (char& c : text)
         if (c == ',' || c == ';')
            c = ' ';
      size_t pos = 0;
      bool any = false;
      while (pos < text.size())
      {
         while (pos < text.size() && std::isspace((unsigned char)text[pos]))
            pos++;
         size_t end = pos;
         while (end < text.size() && !std::isspace((unsigned char)text[end]))
            end++;
         if (end == pos)
            break;
         std::string tok = Lower(text.substr(pos, end - pos));
         pos = end;
         if (tok.rfind("add", 0) == 0)
            tok = tok.substr(3);
         auto removeAll = [&](int a, int b) {
            iv.erase(std::remove_if(iv.begin(), iv.end(), [&](int x) { return x == a || x == b; }), iv.end());
         };
         int add = -1;
         if (tok == "no5" || tok == "omit5") removeAll(7, 7);
         else if (tok == "no3" || tok == "omit3") removeAll(3, 4);
         else if (tok == "b5") { removeAll(7, 7); add = 6; }
         else if (tok == "#5") { removeAll(7, 7); add = 8; }
         else if (tok == "b9") add = 13;
         else if (tok == "9" || tok == "2") add = 14;
         else if (tok == "#9") add = 15;
         else if (tok == "11" || tok == "4") add = 17;
         else if (tok == "#11") add = 18;
         else if (tok == "b13") add = 20;
         else if (tok == "13") add = 21;
         else if (tok == "7" || tok == "b7") add = 10;
         else if (tok == "maj7") add = 11;
         else if (tok == "6") add = 9;
         else
            return false;
         if (add >= 0 && std::find(iv.begin(), iv.end(), add) == iv.end())
            iv.push_back(add);
         any = true;
      }
      return any;
   }

   bool ParseNoteName(const std::string& s, size_t& pos, int& pc)
   {
      if (pos >= s.size())
         return false;
      const char c = (char)std::toupper((unsigned char)s[pos]);
      static const int kBase[7] = { 9, 11, 0, 2, 4, 5, 7 }; // A..G
      if (c < 'A' || c > 'G')
         return false;
      pc = kBase[c - 'A'];
      pos++;
      if (pos < s.size() && s[pos] == '#')
      {
         pc++;
         pos++;
      }
      else if (pos < s.size() && s[pos] == 'b')
      {
         pc--;
         pos++;
      }
      pc = (pc + 12) % 12;
      return true;
   }
}

bool ChordProgressionNode::ParseChordSymbol(const std::string& textIn, uint64_t& mask, int& slash)
{
   std::string text = Trim(textIn);
   slash = -1;
   mask = 0;
   if (text.empty())
      return false;
   size_t pos = 0;
   int root = 0;
   if (!ParseNoteName(text, pos, root))
      return false;
   std::string rest = Trim(text.substr(pos));
   // A trailing "/E" is the bass; "6/9" is part of the quality.
   const size_t sl = rest.rfind('/');
   if (sl != std::string::npos)
   {
      const std::string tail = Trim(rest.substr(sl + 1));
      size_t p = 0;
      int bassPc = 0;
      if (ParseNoteName(tail, p, bassPc) && p == tail.size())
      {
         slash = bassPc == root ? -1 : bassPc;
         rest = Trim(rest.substr(0, sl));
      }
   }
   const std::string restLower = Lower(rest);
   for (const Alias& a : Aliases())
   {
      const std::string& hay = a.caseSensitive ? rest : restLower;
      if (hay.compare(0, a.text.size(), a.text) != 0)
         continue;
      std::vector<int> iv;
      const Quality& q = kQualities[a.quality];
      for (int i = 0; i < q.count; i++)
         iv.push_back(q.intervals[i]);
      if (!ApplyExtensionList(rest.substr(a.text.size()), iv))
         continue;
      for (int i : iv)
      {
         int k = root + i;
         while (k >= kKeys)
            k -= 12;
         mask |= 1ull << k;
      }
      return mask != 0;
   }
   return false;
}

bool ChordProgressionNode::ParseBarsText(const std::string& textIn, double beatsPerBar, float& outBars)
{
   const std::string text = Lower(Trim(textIn));
   if (text.empty())
      return false;
   const double bpb = std::max(1.0, beatsPerBar);
   double bars = -1.0;
   char* end = nullptr;
   const size_t colon = text.find(':');
   if (colon != std::string::npos)
   {
      const double a = std::strtod(text.substr(0, colon).c_str(), &end);
      const double b = std::strtod(text.substr(colon + 1).c_str(), &end);
      bars = a + b / bpb;
   }
   else
   {
      size_t unitAt = text.find_first_not_of("0123456789.");
      const std::string num = text.substr(0, unitAt);
      const std::string unit = unitAt == std::string::npos ? "" : Trim(text.substr(unitAt));
      if (num.empty() || num == ".")
         return false;
      const double v = std::strtod(num.c_str(), &end);
      if (unit.empty())
      {
         const size_t dot = num.find('.');
         if (dot != std::string::npos && num.size() == dot + 2 && num[dot + 1] >= '1' &&
             (double)(num[dot + 1] - '0') <= bpb - 1.0)
            bars = std::strtod(num.substr(0, dot).c_str(), &end) + (double)(num[dot + 1] - '0') / bpb; // bar.beat
         else
            bars = v;
      }
      else if (unit == "b" || unit == "beat" || unit == "beats" || unit == "bt")
         bars = v / bpb;
      else if (unit == "bar" || unit == "bars")
         bars = v;
      else
         return false;
   }
   if (!(bars > 0.0))
      return false;
   outBars = (float)std::clamp(bars, (double)kMinBars, 16.0);
   return true;
}

std::string ChordProgressionNode::BarsLabel(float bars, double beatsPerBar)
{
   char buf[32];
   const double bpb = std::max(1.0, beatsPerBar);
   if (bars >= 1.0f - 1.0e-4f)
      snprintf(buf, sizeof(buf), "%g bar", std::round((double)bars * 100.0) / 100.0);
   else
      snprintf(buf, sizeof(buf), "%g beat", std::round((double)bars * bpb * 100.0) / 100.0);
   return buf;
}

// ------------------------------------------------------------- slot editing
ChordProgressionNode::Slot ChordProgressionNode::GetSlot(int i) const
{
   i = std::clamp(i, 0, kMaxChords - 1);
   return { chordMask[i], maskHi[i], slashBass[i], chordBars[i] };
}

void ChordProgressionNode::SetSlot(int i, const Slot& s)
{
   if (i < 0 || i >= kMaxChords)
      return;
   chordMask[i] = s.lo;
   maskHi[i] = s.hi;
   slashBass[i] = s.slash;
   chordBars[i] = std::clamp(s.bars, kMinBars, 16.0f);
}

bool ChordProgressionNode::InsertSlot(int at, const Slot& s)
{
   chordCount = std::clamp(chordCount, 1, kMaxChords);
   if (chordCount >= kMaxChords)
      return false;
   at = std::clamp(at, 0, chordCount);
   for (int i = chordCount; i > at; i--)
      SetSlot(i, GetSlot(i - 1));
   SetSlot(at, s);
   chordCount++;
   if (selected >= at)
      selected++;
   selected = std::clamp(selected, 0, chordCount - 1);
   return true;
}

bool ChordProgressionNode::DuplicateSlot(int i)
{
   i = std::clamp(i, 0, chordCount - 1);
   if (!InsertSlot(i + 1, GetSlot(i)))
      return false;
   selected = i + 1; // the copy
   return true;
}

bool ChordProgressionNode::DeleteSlot(int i)
{
   if (chordCount <= 1 || i < 0 || i >= chordCount)
      return false;
   for (int j = i; j < chordCount - 1; j++)
      SetSlot(j, GetSlot(j + 1));
   SetSlot(chordCount - 1, Slot());
   chordCount--;
   selected = std::clamp(selected > i ? selected - 1 : selected, 0, chordCount - 1);
   return true;
}

bool ChordProgressionNode::MoveSlot(int from, int to)
{
   if (from < 0 || from >= chordCount || to < 0 || to >= chordCount || from == to)
      return false;
   const Slot moved = GetSlot(from);
   const int dir = to > from ? 1 : -1;
   for (int j = from; j != to; j += dir)
      SetSlot(j, GetSlot(j + dir));
   SetSlot(to, moved);
   // `selected` follows its chord.
   if (selected == from)
      selected = to;
   else if (dir > 0 && selected > from && selected <= to)
      selected--;
   else if (dir < 0 && selected >= to && selected < from)
      selected++;
   return true;
}

void ChordProgressionNode::PreviewNotes(uint64_t fullMask, bool on)
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioChordProgressionNode>();
   mAudioNode->Preview(fullMask, on);
}
