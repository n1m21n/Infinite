#pragma once

#include <string>

// Mix-in interface for image-graph nodes, ported from BespokeSynth's IVisualNode.
// A node renders its output into a GL texture; downstream nodes pull that texture
// via GetOutputTexture(). Cooking is pull-based and memoized per frame so a node
// feeding several consumers only renders once.
class IModulator;

// Visits a node's saveable parameters.
//
// One declaration serves both directions: saving walks the fields and writes
// them, loading walks the same fields and fills them in. Writing separate save
// and load functions per node would mean two lists that have to agree, and the
// moment someone adds a parameter to one and not the other, patches quietly
// load with the wrong value.
//
// Names are stable keys in the patch file, so renaming one silently drops that
// parameter from existing patches - change the name only when the meaning
// genuinely changes.
class ParamVisitor
{
public:
   virtual ~ParamVisitor() {}
   virtual void Float(const char* name, float& value) = 0;
   virtual void Int(const char* name, int& value) = 0;
   virtual void Bool(const char* name, bool& value) = 0;
   virtual void Text(const char* name, std::string& value) = 0;
   virtual void Color(const char* name, float rgb[3]) = 0;
};

// Zero is reserved as "nothing produced yet", so the first real stamp is 1.
// Analogous to NextMeshRevision() (Mesh.h) on the geometry side; kept as an
// inline function-local static rather than a Mesh.cpp-style free function
// since INode has no matching .cpp to define one in.
inline unsigned long long NextTextureRevision()
{
   static unsigned long long sCounter = 0;
   return ++sCounter;
}

// Bumped once by each node that does real re-render work this frame (a
// cache hit does not bump it) - FilterNode's RunShaderPass, Render3DNode's
// draw passes, and so on. main.cpp compares this frame-over-frame to detect
// a fully idle patch (no node did new work) and log it, the same way a
// static Blender viewport eventually settles into "not re-rendering".
inline unsigned long long& NodeWorkCounter()
{
   static unsigned long long counter = 0;
   return counter;
}

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

   // Revision stamp for this node's current output texture: changes only when
   // the pixels actually change, mirroring IGeometrySource::MeshRevision() on
   // the mesh side. A node that tracks this for real lets downstream FilterNode
   // caches skip re-rendering when nothing upstream moved. The default hands
   // back a fresh value on every call - i.e. "always changed" - so any node
   // that doesn't override this stays exactly as conservative (and correct)
   // as the no-caching behavior that predates this mechanism.
   virtual unsigned long long TextureRevision() const { return NextTextureRevision(); }

   // Most nodes emit a single output. Modulators that carry more than one value
   // (an XY pad, say) report a higher count and hand back a different modulator
   // per index; see ModulatorForOutput in Modulation.h.
   virtual int OutputCount() const { return 1; }
   virtual const char* OutputLabel(int /*index*/) const { return "out"; }

   // Nodes that emit several control values return a distinct modulator per
   // output index. Returning null falls back to the node itself for output 0,
   // which is what every single-output modulator wants.
   virtual IModulator* ModulatorOutput(int /*index*/) { return nullptr; }

   // --- bypass -------------------------------------------------------------
   // A bypassed node is skipped entirely: anything pulling from it is handed
   // whatever its own pass-through input produces instead. Nodes with an image
   // input return that input's source here; sources return null, so bypassing
   // one simply removes it from the chain.
   bool bypassed = false;
   virtual INode* BypassSource() { return nullptr; }

   // Label shown next to an input pin. Defaults to A, B, C... in the editor.
   virtual const char* InputLabel(int /*slot*/) const { return nullptr; }

   // Declare every parameter that should survive a save/load round trip.
   // A node that does not override this saves its connections and position but
   // none of its settings.
   virtual void VisitParams(ParamVisitor& /*visitor*/) {}
};
