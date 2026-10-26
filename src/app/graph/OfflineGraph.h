#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "core/Patch.h"
#include "core/Transport.h"

class INode;
class AudioNode;

namespace app
{
// A private patch rendered to a buffer, away from the live engine (Generate
// Samples). Its nodes are never in gNodes, are never drawn or heard, and run
// their sequencers on their own Transport at their own tempo and key, so a
// render never moves or reads the user's playhead.
//
// Build and Prepare run on the main thread: they make the nodes, load their
// files and cook them once, the same steps a patch load takes. Render then
// runs on any ONE thread (a worker), which owns every node until it returns.
// Destroy on the main thread, never while a Render is running.
class OfflineGraph
{
public:
   // A parameter swept by a slow sine between lo and hi while the graph
   // renders: the motion an LFO cable gives a patch, without the modulation
   // system (which binds to drawn controls this graph never has).
   struct Motion
   {
      int node = 0;            // patch node index
      std::string key;         // saved param key
      float lo = 0.0f, hi = 1.0f;
      double periodBeats = 8.0;
      double phase = 0.0;      // 0..1
   };

   OfflineGraph();
   ~OfflineGraph();

   // Makes every node in `data` and wires its audio and note cables. The
   // render taps the audio output of node `tapIndex` (a patch index).
   // Unknown types, and cables to them, are skipped and listed in Error().
   bool Build(const Patch::Data& data, int tapIndex);
   void AddMotion(const Motion& m) { mMotions.push_back(m); }

   // Sets the clock (tempo, key, scale from `data.transport`), prepares and
   // cooks every node. False when the tap is not an audio source.
   bool Prepare(double sampleRate);

   // Renders `frames` stereo frames from bar 1. Returns false when `cancel`
   // turned true first (the output is then partial). `progress` gets 0..1.
   bool Render(int frames, std::vector<float>& left, std::vector<float>& right,
               const std::atomic<bool>* cancel = nullptr, std::atomic<float>* progress = nullptr);

   const std::string& Error() const { return mError; }
   int NodeCount() const;

private:
   struct Owned;
   struct Entry;
   INode* NodeAt(int patchIndex) const;
   void Collect(INode* node, std::vector<INode*>& visiting);
   void Cook();
   void ApplyMotions(double beats);

   std::vector<Owned> mNodes;
   std::vector<Entry> mOrder;
   std::vector<Motion> mMotions;
   std::vector<std::vector<float>> mBuffers; // two per output: L then R
   Patch::TransportRecord mTransportRec;
   Transport mClock;
   INode* mTap = nullptr;
   double mSampleRate = 48000.0;
   int mCookFrame = 0;
   std::string mError;
};

} // namespace app
