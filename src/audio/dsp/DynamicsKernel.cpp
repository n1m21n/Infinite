#include "DynamicsKernel.h"

#include "nodes/AudioEffectNode.h"

// Main thread only. Reads the node's raw params and pushes them - the
// continuous ones through the mailbox for smoothing, the two switches as
// plain atomics.
void DynamicsKernel::PushParams(const AudioEffectNode& node, double sampleRate)
{
   mSampleRate = sampleRate;

   mDetectorRms.store(node.Param("detectorRms") != 0.0f ? 1 : 0, std::memory_order_relaxed);
   mSidechainExternal.store(node.Param("sidechainExternal") != 0.0f ? 1 : 0, std::memory_order_relaxed);

   mMailbox.Push(kThreshold, node.Param("threshold"));
   mMailbox.Push(kRatio, node.Param("ratio"));
   mMailbox.Push(kAttackMs, node.Param("attack"));
   mMailbox.Push(kReleaseMs, node.Param("release"));
   mMailbox.Push(kMakeupDb, node.Param("makeup"));
}
