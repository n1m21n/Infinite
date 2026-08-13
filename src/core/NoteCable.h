#pragma once

#include "INode.h"

// Typed patch cable recording which node feeds a note input pin. Mirrors
// AudioCable exactly (see its comment) - no P2 node has a note pin yet, this
// exists so P3a's note nodes have the scaffolding already in place.
class NoteCable
{
public:
   void Connect(INode* source) { mSource = source; }
   void Disconnect() { mSource = nullptr; }
   INode* GetSource() const { return mSource; }
   bool IsConnected() const { return mSource != nullptr; }

private:
   INode* mSource = nullptr;
};
