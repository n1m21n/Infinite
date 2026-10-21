#include "DynamicsKernel.h"

#include "nodes/AudioEffectNode.h"

#include <cmath>

// Main thread only. Reads the node's raw params and pushes them - the
// continuous ones through the mailbox for smoothing, the switches and the
// style as plain atomics.
// The sample rate comes from PrepareToPlay, never from here: writing it
// from the main thread raced the audio thread's read.
void DynamicsKernel::PushParams(const AudioEffectNode& node, double /*sampleRate*/)
{

   mDetectorRms.store(node.Param("detectorRms") != 0.0f ? 1 : 0, std::memory_order_relaxed);
   mSidechainExternal.store(node.Param("sidechainExternal") != 0.0f ? 1 : 0, std::memory_order_relaxed);
   mAnalog.store(node.Param("analog") != 0.0f ? 1 : 0, std::memory_order_relaxed);
   mScHpfOn.store(node.Param("scHpf") != 0.0f ? 1 : 0, std::memory_order_relaxed);
   mStyle.store((int)std::lround(node.Param("style")), std::memory_order_relaxed);

   mMailbox.Push(kThreshold, node.Param("threshold"));
   mMailbox.Push(kRatio, node.Param("ratio"));
   mMailbox.Push(kAttackMs, node.Param("attack"));
   mMailbox.Push(kReleaseMs, node.Param("release"));
   mMailbox.Push(kMakeupDb, node.Param("makeup"));
}
