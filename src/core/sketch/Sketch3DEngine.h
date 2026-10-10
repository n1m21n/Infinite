// Sketch 3D: the JavaScript engine of the Sketch node, but its draw(t) builds a
// triangle mesh (p5 WEBGL-flavoured: box, sphere, translate/rotateY, beginShape...)
// instead of pixels. Pure CPU, no GL; the node (src/nodes/Sketch3DNode) hands the
// mesh to Render 3D like any other geometry source. Same sandbox as Sketch
// (quickjs-ng without quickjs-libc, 64 MB, 50 ms abort, seeded random()).
#pragma once
#include "SketchEngine.h"   // SketchParamDecl, SketchError
#include "../Mesh.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

class Sketch3DEngine
{
public:
   Sketch3DEngine();
   ~Sketch3DEngine();
   Sketch3DEngine(const Sketch3DEngine&) = delete;
   Sketch3DEngine& operator=(const Sketch3DEngine&) = delete;

   // Evaluates `code` in a fresh context (collects param() declarations, finds
   // draw). On failure the previous working program is kept and false returned.
   bool Compile(const std::string& code, SketchError& err);
   bool HasProgram() const;
   const std::vector<SketchParamDecl>& Params() const;

   struct Frame
   {
      double t = 0, beat = 0;
      int frame = 0;
      uint32_t seed = 1;
      const std::map<std::string, float>* params = nullptr;
   };
   // Runs draw(t) and replaces `out` with the mesh it built. Vertex colour is filled
   // (linear RGB) only if the script called fill() this frame. On a runtime error
   // `out` is left untouched and false returned.
   bool Run(const Frame& f, Mesh& out, SketchError& err);

   // Hard limits (a runaway sketch must not eat memory): vertices / indices per frame.
   static constexpr size_t kMaxVertices = 400000;
   static constexpr size_t kMaxIndices = 2400000;

private:
   struct Impl;
   Impl* mImpl;
};
