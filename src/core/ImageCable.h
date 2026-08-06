#pragma once

#include "INode.h"

// Typed patch cable carrying a texture handle between two nodes.
//
// BespokeSynth's visual nodes connect via a generic PatchCableSource
// (kConnectionType_Special) and recover the real type with dynamic_cast<IVisualNode*>
// at cook time. This project's research doc (image-resynth-research.md §8.1/§8.3)
// flags that as worth upgrading for a new project, so ImageCable is a typed
// connection from the start: it only ever holds an INode*.
class ImageCable
{
public:
   void Connect(INode* source) { mSource = source; }
   void Disconnect() { mSource = nullptr; }
   INode* GetSource() const { return mSource; }
   bool IsConnected() const { return mSource != nullptr; }

   // Pulls the upstream node's output for this frame, cooking it if needed.
   unsigned int Pull(int frameId)
   {
      if (mSource == nullptr)
         return 0;
      mSource->CookIfNeeded(frameId);
      return mSource->GetOutputTexture();
   }

   int Width() const { return mSource ? mSource->GetOutputWidth() : 0; }
   int Height() const { return mSource ? mSource->GetOutputHeight() : 0; }

private:
   INode* mSource = nullptr;
};
