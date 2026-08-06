#pragma once

// Global clock. Everything time-based in the graph (modulators, video playback)
// reads from here rather than wall time, so play/pause genuinely freezes the
// whole patch and tempo changes retime every modulator at once.
class Transport
{
public:
   static Transport& Instance();

   // Advances the clock by one frame's worth of real time, but only while playing.
   void Tick(float deltaSeconds);

   void SetPlaying(bool playing) { mPlaying = playing; }
   bool IsPlaying() const { return mPlaying; }
   void TogglePlay() { mPlaying = !mPlaying; }

   void Rewind()
   {
      mBeats = 0.0;
      mSeconds = 0.0;
   }

   void SetTempo(float bpm) { mBpm = bpm < 1.0f ? 1.0f : bpm; }
   float Tempo() const { return mBpm; }

   // Musical position; modulator rates are expressed in beats.
   double Beats() const { return mBeats; }
   // Playing time in seconds, also frozen while paused.
   double Seconds() const { return mSeconds; }

private:
   bool mPlaying = true;
   float mBpm = 120.0f;
   double mBeats = 0.0;
   double mSeconds = 0.0;
};
