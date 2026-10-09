// Sketch: JavaScript drawing engine (quickjs-ng + plutovg). Pure CPU, no GL, no
// platform code - the node (src/nodes/SketchNode) uploads the RGBA result.
// Design and decisions: docs/plans/sketch/README.md.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct SketchParamDecl
{
   std::string name;
   float def = 0, lo = 0, hi = 1;
};

struct SketchError
{
   std::string message;
   int line = 0, col = 0;   // 1-based; 0 when unknown
   bool Any() const { return !message.empty(); }
};

class SketchEngine
{
public:
   SketchEngine();
   ~SketchEngine();
   SketchEngine(const SketchEngine&) = delete;
   SketchEngine& operator=(const SketchEngine&) = delete;

   // Evaluates `code` in a fresh context (collects param() declarations, finds
   // draw). On failure the previous working program is kept and false returned.
   bool Compile(const std::string& code, SketchError& err);
   bool HasProgram() const;
   const std::vector<SketchParamDecl>& Params() const;

   // Optional font used by text(); a TTF/OTF path. Call before Run.
   void SetFontFile(const std::string& path);

   struct Frame
   {
      int width = 512, height = 512;
      double t = 0, beat = 0;
      int frame = 0;
      uint32_t seed = 1;
      const std::map<std::string, float>* params = nullptr;
   };
   // Runs draw(t) and writes straight-alpha RGBA8 (width*height*4) into `rgba`.
   // On a runtime error `rgba` is left untouched and false returned.
   bool Run(const Frame& f, std::vector<uint8_t>& rgba, SketchError& err);

private:
   struct Impl;
   Impl* mImpl;
};
