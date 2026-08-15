#pragma once

// RebuildAudioTopology() lives on main.cpp's app-scoped node list and can't be
// called directly from nodes/*.cpp. A node whose audio processing needs are
// discovered outside the usual connect/disconnect/spawn/delete actions (e.g.
// SamplerNode::StartRecording/StopRecording changing RequiresAudioProcessing's
// answer) sets this flag instead; main.cpp's per-frame loop checks and clears
// it, rebuilding the topology from scratch the same way it already does after
// any graph edit.
namespace AudioTopologyRequest
{
   inline bool& PendingRebuild()
   {
      static bool pending = false;
      return pending;
   }

   inline void Request() { PendingRebuild() = true; }
}
