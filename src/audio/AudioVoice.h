#pragma once

#include <cstdint>
#include <vector>

// Fixed-voice-count polyphony: an ADSR envelope per voice, plus a
// round-robin/oldest-steal allocator on top. Both classes are written as if
// NoteOn/NoteOff/Process run on the real-time audio thread (P3 will call
// them that way once MIDI is wired to an audio-backed INode) - no
// allocation, no locks, no unbounded loops in any of the per-call paths.
// The allocator's own storage is sized once at construction (main thread),
// never resized afterward.
class Envelope
{
public:
   void SetSampleRate(double sr) { mSampleRate = sr > 0.0 ? sr : 44100.0; }

   void SetADSR(float attackMs, float decayMs, float sustainLevel, float releaseMs)
   {
      mSustainLevel = sustainLevel < 0.0f ? 0.0f : (sustainLevel > 1.0f ? 1.0f : sustainLevel);
      mAttackInc = SegmentInc(attackMs);
      mDecayInc = SegmentInc(decayMs, 1.0f - mSustainLevel);
      mReleaseMs = releaseMs;
   }

   void NoteOn()
   {
      mStage = Stage::Attack;
   }

   void NoteOff()
   {
      if (mStage != Stage::Idle)
      {
         mReleaseInc = SegmentInc(mReleaseMs, mLevel);
         mStage = Stage::Release;
      }
   }

   float Process()
   {
      switch (mStage)
      {
         case Stage::Idle:
            mLevel = 0.0f;
            break;

         case Stage::Attack:
            mLevel += mAttackInc;
            if (mLevel >= 1.0f)
            {
               mLevel = 1.0f;
               mStage = Stage::Decay;
            }
            break;

         case Stage::Decay:
            mLevel -= mDecayInc;
            if (mLevel <= mSustainLevel)
            {
               mLevel = mSustainLevel;
               mStage = Stage::Sustain;
            }
            break;

         case Stage::Sustain:
            mLevel = mSustainLevel;
            break;

         case Stage::Release:
            mLevel -= mReleaseInc;
            if (mLevel <= 0.0f)
            {
               mLevel = 0.0f;
               mStage = Stage::Idle;
            }
            break;
      }
      return mLevel;
   }

   bool IsActive() const { return mStage != Stage::Idle; }
   float Level() const { return mLevel; }

private:
   enum class Stage
   {
      Idle,
      Attack,
      Decay,
      Sustain,
      Release
   };

   // Linear per-sample increment to cross a distance of `span` (default:
   // the full 0..1 range) over durationMs at the current sample rate.
   float SegmentInc(float durationMs, float span = 1.0f) const
   {
      const float samples = durationMs * 0.001f * (float)mSampleRate;
      return samples > 0.0f ? span / samples : span;
   }

   double mSampleRate = 44100.0;
   Stage mStage = Stage::Idle;
   float mLevel = 0.0f;
   float mAttackInc = 1.0f;
   float mDecayInc = 1.0f;
   float mReleaseInc = 1.0f;
   float mSustainLevel = 1.0f;
   float mReleaseMs = 0.0f;
};

class VoiceAllocator
{
public:
   explicit VoiceAllocator(int maxVoices);

   void SetSampleRate(double sr);
   void SetADSR(float attackMs, float decayMs, float sustainLevel, float releaseMs);

   // Returns the voice index used (steals the oldest active voice if every
   // voice is already busy).
   int NoteOn(int midiNote, float velocity, int voiceId);

   // Releases the envelope of whichever voice currently holds voiceId (a
   // no-op if no voice holds it). Does not free the slot immediately - the
   // envelope's release stage does that once IsActive() goes false.
   void NoteOff(int voiceId);

   int NumVoices() const { return (int)mVoices.size(); }
   int NoteAt(int voiceIndex) const { return mVoices[voiceIndex].note; }
   float VelocityAt(int voiceIndex) const { return mVoices[voiceIndex].velocity; }
   Envelope& EnvelopeAt(int voiceIndex) { return mVoices[voiceIndex].envelope; }
   bool IsVoiceActive(int voiceIndex) const { return mVoices[voiceIndex].envelope.IsActive(); }

private:
   struct Voice
   {
      int note = -1;
      int voiceId = 0;
      float velocity = 0.0f;
      uint64_t age = 0; // higher = more recently triggered
      Envelope envelope;
   };

   std::vector<Voice> mVoices;
   int mRoundRobinCursor = 0;
   uint64_t mNextAge = 1;
};
