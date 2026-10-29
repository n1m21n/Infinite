#pragma once

#include <memory>
#include <string>
#include <vector>

#include "app/graph/SampleStyles.h"
#include "nodes/SamplerNode.h"

// Generate Samples: a sample player whose sample is made, not loaded. Pick a
// part (synths, pads, plucks, percussion, drums), press generate, and a
// private patch built from a style card drawn by the seed (SampleStyles::Compile) renders a loop of 1-8 bars on a
// worker thread. The result plays back through the sample player it extends:
// waveform, start/end, pitch, volume, loop, and notes in to play it.
//
// Lives in app/graph, not nodes/: the render needs the patch loader and the
// node factory, which no plain node reaches.
//
// Saved as its recipe, not its audio: part, seed, variation, length and
// tempo source, plus the generator version that made it. Reopening a patch
// renders it again (deterministic in the recipe; a newer generator may make
// a slightly different loop, which is fine - patches evolve with the app).
// "keep as file" writes the loop to a wav, after which it is saved and
// reloaded as a file like any sample player's.
class GenerateSamplesNode : public SamplerNode
{
public:
   static INode* Create() { return new GenerateSamplesNode(); }
   GenerateSamplesNode();
   ~GenerateSamplesNode() override;

   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;
   // No record-in pin: the sample comes from the generator.
   AudioCable* AudioInputSlot(int) override { return nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }

   enum class State
   {
      Empty,      // nothing generated yet
      Generating, // a render is running
      Ready,      // the sample matches the settings
      Stale,      // settings changed since the sample was made
      Failed      // every redraw came out silent or broken
   };
   State GetState() const;
   float Progress() const;

   // Starts a render of the current settings (cancelling one in flight). Pinned: the seed is the one a take already
   // landed on (a dot move in the Sample studio), so there is no redraw to another seed and only silent or broken audio
   // is refused, as the website studio's shaped renders do.
   void Generate(bool pinned = false);
   // Takes a finished render (CookIfNeeded calls it; the Sample studio's private node, which is never cooked, calls it itself).
   void Poll();
   void Cancel();
   // Writes the loop to a wav in the recordings folder and switches this node
   // to it. False with nothing generated or when the write fails.
   // Writes the made loop to a wav at `path`, as heard (the -14 LUFS master). The node is not changed.
   bool ExportWav(const std::string& path) const;
   bool HasLoopToExport() const { return !mLoopL.empty(); }
   std::string ExportName() const; // a file name for the made loop
   // A new random seed (the generate button does this before every render).
   void RollSeed();

   // Recipe. There is no style to pick: the seed draws the style card (its
   // voices, chords and rhythm), so each generate is a new take on the part.
   // The seed is saved so a reopened patch renders the same loop; the node's
   // generate button rolls a new one, the file-load path does not.
   int seed = 1;
   int card = -1; // the style card the seed drew (by what has landed for this part); -1: from the seed
   float variation = 0.5f;
   int bars = 2;                 // 1, 2, 4 or 8
   bool followTransport = false; // "follow tempo": the transport's tempo, else the card's own (name kept for saved patches)
   bool followScale = false;     // "follow scale": the transport's key and scale, else the card's own
   int part = (int)SampleStyles::Part::Synths; // SampleStyles::Part: synths, pads, plucks, percussion or drums
   // The Sample studio's dots (SampleStyles::Recipe), each -1..1; 0 is the sample as it always was. No control on the node.
   float tone = 0.0f, energy = 0.0f, space = 0.0f, mood = 0.0f;
   // A take brought over from the Sample studio: its seed is the one it landed on, so every render of it is pinned (no
   // redraw to another seed). The node's own generate button clears it.
   bool keepSeed = false;

   static constexpr int kGeneratorVersion = 1;
   // Last generate's resolved tempo and key, for the status line.
   float madeBpm = 0.0f;
   int madeRoot = 0;
   int madeScale = 0;
   // "synths 1204, a natural minor": what the sample is, for its
   // name and a kept file's.
   std::string MadeLabel() const;
   // The take as made: its recipe with the seed it landed on (after any redraws), and that loop at -14 LUFS.
   SampleStyles::Recipe MadeRecipe() const;
   const std::vector<float>& LoopL() const { return mLoopL; }
   const std::vector<float>& LoopR() const { return mLoopR; }
   static constexpr double kLoopRate = 48000.0;
   static std::string Label(const SampleStyles::Recipe& r, int root, int scale);

   // The step every made loop goes through: false when it is silent,
   // non-finite, flat or off its part's spectral gate (redraw it), else
   // `out` is it at -14 LUFS under a -1 dBTP ceiling. Shared with the
   // website pool batch. `part` is a SampleStyles::Part index.
   // lenient (pinned renders): only silence or broken audio is refused, never a timbre the gate would redraw.
   static bool Master(int part, const float* l, const float* r, int frames, std::vector<float>& outL, std::vector<float>& outR,
                      bool lenient = false);
   // Frames of the pre-roll a loop of `bars` renders first and drops.
   static int PrerollBars(int bars) { return bars == 1 ? 1 : 2; }

private:
   struct Job;
   SampleStyles::Recipe CurrentRecipe() const;
   bool StartAttempt(int attempt);
   void Finish();

   std::unique_ptr<Job> mJob;
   SampleStyles::Recipe mMade; // what the current buffer was made from
   int mMadeAttempt = 0;       // the redraw it landed on (its seed is mMade.seed + attempt * 7919)
   bool mHasSample = false;
   bool mFailed = false;
   bool mGenerated = false;   // saved: a sample existed, so reopening renders it again
   int mGenVersion = kGeneratorVersion;
   bool mRestoreRange = false; // reopening: keep the saved start/end/position
   float mSavedStart = 0.0f, mSavedEnd = 1.0f, mSavedPosition = 0.0f;
   std::vector<float> mLoopL, mLoopR; // the finished loop, for keep as file
};
