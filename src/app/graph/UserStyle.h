#pragma once

#include <string>
#include <vector>

// What this machine's player has taught the Prediction nodes, read from the
// profiles they keep in <AppSupportDir>/prediction (notes_style.bin,
// rhythm_style.bin, quantize_profile.bin, velocity_profile.bin). Nothing leaves
// the machine: Generate Samples reads these files and leans its choices toward
// them, so the same node sounds different for each player. Empty profiles
// (a new user, or learning switched off) give valid == false and the generator
// behaves as the style cards alone.
namespace UserStyle
{
   struct Profile
   {
      bool valid = false;
      bool hasPitch = false;       // pitchClass and step are meaningful
      bool hasVelocity = false;    // vel is non-empty
      bool hasTiming = false;      // onset and offGrid are meaningful
      float pitchClass[12] = {};   // share of played notes per absolute pitch class, sums to 1
      float step[6] = {};          // share of melodic moves of -2..3 scale degrees, sums to 1
      float onset[16] = {};        // how often a note starts on each sixteenth, 0..1 (1 = the busiest)
      float offGrid = 0.0f;        // share of spacings that miss the 1/16 grid (swing, triplets), 0..1
      float velMean = 0.7f;        // 0..1
      std::vector<float> vel;      // every learned velocity, 0..1
      float durBeats = 0.0f;       // typical note length in beats, 0 when unknown
      bool hasChords = false;
      float chordSize[4] = {};     // share of chords of 2, 3, 4 and 5 notes, sums to 1
      // How far each part of the profile is to be trusted, 0..kMaxWeight: n / (n + n0) of the
      // evidence behind it. A new player gets a little, a player with thousands of notes gets most
      // of the choice; the style cards are the baseline underneath.
      float pitchW = 0.0f, timingW = 0.0f, velW = 0.0f, durW = 0.0f, chordW = 0.0f;
   };
   constexpr float kMaxWeight = 0.9f;

   // Loaded on first use and again when a profile file changes.
   const Profile& Get();

   // Points Get() at another directory (empty: back to the app's own). Tests only.
   void UseDirectory(const std::string& directory);

   // The player's usual tempo, drawn from how long they have run the transport at each bpm in the
   // movement log. False until the background read has finished (a first run reads the whole log;
   // later runs only the sessions since). `u` is a uniform 0..1; `weight` is how far to trust it.
   bool DrawTempo(double u, float& bpm, float& weight);

   // Where this player leaves a knob, from the pooled movement statistics (stats.bin): `role` is a
   // ParamRoles role ("filter.cutoff", "env.attack", "env.decay", "env.sustain", "env.release",
   // "filter.resonance"); `value` comes back in the role's own unit (log2 Hz, log2 seconds, 0..1).
   // The histogram is smoothed and its thin tails dropped before drawing, so a stray wobble is not
   // a habit. False until the background read has finished, or when too little was played.
   bool DrawKnob(const char* role, double u, float& value, float& weight);

   // The smoothing and draw on a bare 64-bin histogram over [lo, hi]. Public for tests.
   bool DrawFromHistogram(const float* hist, float lo, float hi, double u, float& value);

   // What the player does with generated samples. Each generate is judged by what happens next: a
   // new generate within a few seconds is a rejection, a kept file or a pause is a landing. The
   // style cards that land more often get drawn more often; every card keeps a floor so nothing
   // is ever ruled out.
   void NoteGenerate(int part, int card); // main thread, as a generate starts
   void NoteKept();                       // export wav
   void NoteSaved(int part, int card, int seed); // a saved patch reopened: that loop was kept, counted once
   void SettleTaste();                    // main thread, each frame: a loop left alone lands
   int DrawCard(int part, double u, int cardCount); // by what has landed for this part
   void ResetTaste();                     // Forget me: the in-memory taste starts over (the files are deleted by the caller)

   // 16-dimensional contextual preference model over musical features phi(x) in [-1, 1].
   // Every landed/skipped sample updates the user's feature weights across all styles.
   constexpr int kFeatureDim = 16;
   void NoteFeatures(const float phi[kFeatureDim]);
   float ScoreFeatures(const float phi[kFeatureDim], double exploreU, float& weight);

   // Reads a directory of profiles. Public so a test can point it at a fixture.
   Profile Read(const char* directory);
}
