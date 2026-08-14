#pragma once

#include "INode.h"

// Typed patch cable recording which node feeds a note input pin. Mirrors
// AudioCable exactly (see its comment) - no P2 node has a note pin yet, this
// exists so P3a's note nodes have the scaffolding already in place.
class NoteCable
{
public:
   // outputSlot selects which of the source's note outputs this cable reads
   // from - every note source has exactly one (outputSlot 0) except Note
   // Router, the system's only note fan-out point (audio-graph-semantics.md
   // §1), which has four.
   void Connect(INode* source, int outputSlot = 0) { mSource = source; mOutputSlot = outputSlot; }
   void Disconnect() { mSource = nullptr; mOutputSlot = 0; }
   INode* GetSource() const { return mSource; }
   int GetOutputSlot() const { return mOutputSlot; }
   bool IsConnected() const { return mSource != nullptr; }

private:
   INode* mSource = nullptr;
   int mOutputSlot = 0;
};
