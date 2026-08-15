#include "AudioVoice.h"

VoiceAllocator::VoiceAllocator(int maxVoices)
{
   mVoices.resize(maxVoices > 0 ? maxVoices : 1);
}

void VoiceAllocator::SetSampleRate(double sr)
{
   for (auto& voice : mVoices)
      voice.envelope.SetSampleRate(sr);
}

void VoiceAllocator::SetADSR(float attackMs, float decayMs, float sustainLevel, float releaseMs)
{
   for (auto& voice : mVoices)
      voice.envelope.SetADSR(attackMs, decayMs, sustainLevel, releaseMs);
}

int VoiceAllocator::NoteOn(int midiNote, float velocity, int voiceId)
{
   // Round-robin first: cycle through voices starting at the cursor,
   // preferring one that's already inactive.
   const int numVoices = (int)mVoices.size();
   int chosen = -1;
   for (int i = 0; i < numVoices; ++i)
   {
      const int idx = (mRoundRobinCursor + i) % numVoices;
      if (!mVoices[idx].envelope.IsActive())
      {
         chosen = idx;
         break;
      }
   }

   // Every voice busy: steal the oldest active one.
   if (chosen < 0)
   {
      uint64_t oldestAge = UINT64_MAX;
      for (int i = 0; i < numVoices; ++i)
      {
         if (mVoices[i].age < oldestAge)
         {
            oldestAge = mVoices[i].age;
            chosen = i;
         }
      }
   }

   mRoundRobinCursor = (chosen + 1) % numVoices;

   Voice& voice = mVoices[chosen];
   voice.note = midiNote;
   voice.voiceId = voiceId;
   voice.velocity = velocity;
   voice.age = mNextAge++;
   voice.envelope.NoteOn();
   return chosen;
}

void VoiceAllocator::NoteOff(int voiceId)
{
   for (auto& voice : mVoices)
   {
      if (voice.voiceId == voiceId && voice.envelope.IsActive())
      {
         voice.envelope.NoteOff();
         return;
      }
   }
}
