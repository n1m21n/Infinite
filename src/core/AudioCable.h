#pragma once

#include "INode.h"

// Typed patch cable recording which node feeds an audio input pin.
//
// Unlike ImageCable, this is not the real-time signal path - it exists only
// for the editor/save-load side of things (who feeds this pin, for
// save/load and link drawing). The actual real-time audio graph is the
// flattened AudioNode* list AudioEngine::SetTopology is given, built by
// walking these cables on the main thread (see docs/plans/audio/README.md,
// P2's "AudioEngine::SetTopology integration" step). Deliberately does not
// walk past bypassed nodes the way ImageCable::Resolved() does - none of the
// P2 audio node types override BypassSource() yet, so that semantic doesn't
// exist for audio cables yet either.
class AudioCable
{
public:
   void Connect(INode* source) { mSource = source; }
   void Disconnect() { mSource = nullptr; }
   INode* GetSource() const { return mSource; }
   bool IsConnected() const { return mSource != nullptr; }

private:
   INode* mSource = nullptr;
};
