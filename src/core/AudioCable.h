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
   // outputSlot selects which of the source's outputs this cable reads from -
   // every audio source has exactly one (outputSlot 0) except a node like
   // VideoSourceNode, which is also an image source and puts its audio on a
   // later output index (see IAudioSource::IsAudioOutputIndex). Recording
   // this is what lets link-drawing anchor the cable on the correct pin
   // (see NoteCable::GetOutputSlot() for the same pattern).
   void Connect(INode* source, int outputSlot = 0) { mSource = source; mOutputSlot = outputSlot; }
   void Disconnect() { mSource = nullptr; mOutputSlot = 0; }
   INode* GetSource() const { return mSource; }
   int GetOutputSlot() const { return mOutputSlot; }
   bool IsConnected() const { return mSource != nullptr; }

private:
   INode* mSource = nullptr;
   int mOutputSlot = 0;
};
