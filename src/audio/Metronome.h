#pragma once

#include <algorithm>
#include <cmath>

// Turbo: the metronome click. Header-only, allocation-free, audio thread
// only. One short decaying sine "blip" per beat, higher and louder on the
// first beat of each bar. Used twice: by AudioEngine, mixed straight into the
// device output after the graph (no cables, like the sample preview), and by
// Transport Control's audio output pin for patching into the graph.
//
// Timing follows the same convention as every beat-synced node: the block
// starts at Transport::Beats() as read inside the audio callback, so the
// click lands with the Drum Sequencer, Note Sequencer and Chord Progression.
class MetronomeClick
{
public:
   // Adds the click into `channels` (all of them get the same mono signal).
   // blockStartBeats: transport position at sample 0; samplesPerBeat at the
   // current tempo; beatsPerBar from the time signature (in quarter beats).
   void RenderAdd(float* const* channels, int numChannels, int numFrames, double blockStartBeats,
                  double samplesPerBeat, double beatsPerBar, double sampleRate, float volume, bool accent)
   {
      if (numFrames <= 0 || samplesPerBeat <= 0.0 || sampleRate <= 0.0)
         return;
      const double blockEndBeats = blockStartBeats + (double)numFrames / samplesPerBeat;
      // Next beat at or after the block start (a transport jump re-arms).
      if (mNextBeat < 0.0 || mNextBeat < blockStartBeats - 1.0 || mNextBeat > blockStartBeats + 4.0)
         mNextBeat = std::ceil(blockStartBeats - 1.0e-9);

      int frame = 0;
      while (frame < numFrames)
      {
         int segmentEnd = numFrames;
         bool trigger = false;
         if (mNextBeat < blockEndBeats)
         {
            const int at = std::clamp((int)std::floor((mNextBeat - blockStartBeats) * samplesPerBeat), 0, numFrames - 1);
            if (at <= frame)
               trigger = true;
            else
               segmentEnd = at;
         }
         if (trigger)
         {
            const double bpb = std::max(0.25, beatsPerBar);
            const double barPos = std::fmod(mNextBeat, bpb);
            const bool downbeat = accent && (barPos < 1.0e-6 || bpb - barPos < 1.0e-6);
            mFreq = downbeat ? 1760.0 : 1320.0;
            mAmp = downbeat ? 1.0f : 0.6f;
            mPhase = 0.0;
            mDecay = (float)std::exp(-1.0 / (0.012 * sampleRate)); // ~12 ms time constant
            mRemaining = (int)(0.06 * sampleRate);
            mNextBeat += 1.0;
            continue;
         }
         const double inc = 2.0 * 3.14159265358979323846 * mFreq / sampleRate;
         for (int i = frame; i < segmentEnd; i++)
         {
            if (mRemaining <= 0)
               break;
            const float s = (float)std::sin(mPhase) * mAmp * volume;
            for (int ch = 0; ch < numChannels; ch++)
               if (channels[ch] != nullptr)
                  channels[ch][i] += s;
            mPhase += inc;
            mAmp *= mDecay;
            mRemaining--;
         }
         frame = segmentEnd;
      }
   }

   void Reset()
   {
      mNextBeat = -1.0;
      mRemaining = 0;
   }

private:
   double mNextBeat = -1.0;
   double mPhase = 0.0;
   double mFreq = 1320.0;
   float mAmp = 0.0f;
   float mDecay = 0.0f;
   int mRemaining = 0;
};
