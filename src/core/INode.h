#pragma once

// Mix-in interface for image-graph nodes, ported from BespokeSynth's IVisualNode.
// A node renders its output into a GL texture; downstream nodes pull that texture
// via GetOutputTexture(). Cooking is pull-based and memoized per frame so a node
// feeding several consumers only renders once.
class INode
{
public:
   virtual ~INode() {}

   virtual unsigned int GetOutputTexture() = 0;
   virtual int GetOutputWidth() const = 0;
   virtual int GetOutputHeight() const = 0;

   // Ensure this node has produced its output for the given frame (memoized).
   // Nodes with inputs should pull their inputs' CookIfNeeded() before rendering themselves.
   virtual void CookIfNeeded(int frameId) = 0;
};
