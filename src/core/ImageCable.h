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
   // outputIndex selects which of the source's image outputs this cable
   // reads (build step 11, §5.3) - 0 for every pre-existing call site,
   // matching the pre-step-11 behaviour exactly.
   void Connect(INode* source, int outputIndex = 0) { mSource = source; mSourceOutput = outputIndex; }
   void Disconnect() { mSource = nullptr; mSourceOutput = 0; }
   INode* GetSource() const { return mSource; }
   int GetSourceOutput() const { return mSourceOutput; }
   bool IsConnected() const { return mSource != nullptr; }

   // Walks past bypassed nodes to the first one that is actually enabled. The
   // hop limit stops a bypassed feedback cycle from spinning forever.
   INode* Resolved() const
   {
      INode* node = mSource;
      for (int hops = 0; node != nullptr && node->bypassed && hops < 64; hops++)
         node = node->BypassSource();
      return (node != nullptr && node->bypassed) ? nullptr : node;
   }

   // Pulls the upstream node's output for this frame, cooking it if needed.
   unsigned int Pull(int frameId)
   {
      INode* node = Resolved();
      if (node == nullptr)
         return 0;
      node->CookIfNeeded(frameId);
      return node->GetOutputTexture(mSourceOutput);
   }

   int Width() const
   {
      INode* node = Resolved();
      return node ? node->GetOutputWidth() : 0;
   }
   int Height() const
   {
      INode* node = Resolved();
      return node ? node->GetOutputHeight() : 0;
   }

   // Current revision of whatever this cable resolves to. Call after Pull()
   // has cooked it this frame, so a node that just re-rendered has already
   // bumped its stamp before this is read for a downstream cache signature.
   unsigned long long Revision() const
   {
      INode* node = Resolved();
      return node ? node->TextureRevision() : 0;
   }

private:
   INode* mSource = nullptr;
   int mSourceOutput = 0;
};
