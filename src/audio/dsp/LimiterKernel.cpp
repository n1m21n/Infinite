#include "LimiterKernel.h"

#include "nodes/AudioEffectNode.h"

// Main thread only. Reads the node's raw params and pushes them through the
// mailbox for smoothing - all four of this effect's knobs are continuous.
// The sample rate comes from PrepareToPlay on the audio side, never from
// here: writing it from the main thread raced the audio thread's read.
void LimiterKernel::PushParams(const AudioEffectNode& node, double /*sampleRate*/)
{
   mMailbox.Push(kThreshold, node.Param("threshold"));
   mMailbox.Push(kReleaseMs, node.Param("release"));
   mMailbox.Push(kInGainDb, node.Param("inGain"));
   mMailbox.Push(kOutGainDb, node.Param("outGain"));
}
