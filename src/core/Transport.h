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

   // External sync (MIDI Clock, etc.): while enabled, only the tempo (mBpm)
   // is driven by the clock source - mBeats keeps free-running from wherever
   // it already was rather than snapping to the source's beat grid. Matching
   // BPM without phase-locking still gets most of the visual benefit and
   // avoids a visible jump in beat position the moment sync engages.
   void SetExternalSyncEnabled(bool on) { mExternalSync = on; }
   bool ExternalSyncEnabled() const { return mExternalSync; }

   // Called once a frame by whoever is reading the clock source, with its
   // latest smoothed BPM estimate. Only takes effect while external sync is
   // enabled; SetTempo() still works for manual control when it's off, and a
   // dropped-out clock source does not silently keep feeding a stale BPM
   // forever - see the timeout in Tick().
   void ReportExternalTempo(float bpm)
   {
      if (!mExternalSync)
         return;
      mBpm = bpm < 1.0f ? 1.0f : bpm;
      mExternalClockFresh = true;
      mExternalClockAge = 0.0f;
   }

   // True once sync is on and a clock pulse has been reported recently.
   bool IsExternalClockPresent() const { return mExternalSync && mExternalClockFresh; }

private:
   static constexpr float kExternalClockTimeoutSeconds = 2.0f;

   bool mPlaying = true;
   float mBpm = 120.0f;
   double mBeats = 0.0;
   double mSeconds = 0.0;

   bool mExternalSync = false;
   bool mExternalClockFresh = false;
   float mExternalClockAge = 0.0f; // seconds since the last ReportExternalTempo call
};
