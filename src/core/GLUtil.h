#pragma once

#include <OpenGL/gl3.h>
#include <functional>
#include <string>
#include <vector>

// Minimal GL FBO / shader-pass helpers, ported from BespokeSynth's VizGL.
// Targets OpenGL 3.2 core profile (matches Bespoke's macOS context) using plain
// GLSL #version 150 shaders and a shared fullscreen-quad draw.
namespace GLUtil
{
   struct Fbo
   {
      unsigned int fbo = 0;
      unsigned int tex = 0;
      int w = 0;
      int h = 0;
      unsigned int internalFormat = GL_RGBA8;
   };

   // Lazily (re)allocates the FBO's color texture to the requested size and
   // internal format (default GL_RGBA8, matching prior behavior). Pass
   // GL_RGBA16F for nodes that need HDR range or unclamped simulation state.
   bool EnsureFbo(Fbo& fbo, int w, int h, unsigned int internalFormat = GL_RGBA8);
   void DestroyFbo(Fbo& fbo);

   inline unsigned int FboTexture(const Fbo& fbo) { return fbo.tex; }

   // Compiles fragSrc against a shared fullscreen-quad vertex shader (attributes
   // aPos/aUv at locations 0/1). Returns 0 on failure; if outError is non-null the
   // compile/link log is written there instead of stderr (used by FormulaNode to
   // show the user their own typo inline).
   unsigned int CompileProgram(const char* fragSrc, std::string* outError = nullptr);

   // Binds out's FBO, sets viewport, runs `setup` to bind uniforms, then draws
   // the shared fullscreen quad. Restores previously-bound FBO/viewport after.
   void RunShaderPass(const Fbo& out, unsigned int program, const std::function<void()>& setup);

   // Draws `tex` as a fullscreen quad into whatever framebuffer is currently
   // bound (used to blit a node's output to a window). Clears the full
   // windowW x windowH area first, then draws into a letterboxed viewport
   // sized to preserve texW/texH's aspect ratio when given (texW/texH <= 0
   // skips letterboxing and just fills the whole window, matching the
   // original stretch-to-fill behavior). checkerBg composites the texture's
   // own alpha over the same checkerboard pattern the node editor draws
   // behind a transparent preview, instead of showing raw (unpremultiplied)
   // color where alpha is 0.
   void DrawTextureToScreen(unsigned int tex, int windowW, int windowH,
                             int texW = 0, int texH = 0, bool checkerBg = false);

   // Reads an existing GPU texture's pixels back to the CPU as RGBA floats,
   // for nodes that need to sample a texture per-vertex rather than per-pixel
   // (e.g. displacing a mesh). `scratchFbo` is a caller-owned, lazily-created
   // framebuffer name reused across calls; `srcTex` is attached to it
   // temporarily rather than copied, so this does not take ownership of it.
   // Call once per texture change, not per vertex - a glReadPixels per vertex
   // would serialise the whole pipeline instead of one readback per rebuild.
   bool ReadTexturePixels(unsigned int& scratchFbo, unsigned int srcTex, int w, int h,
                          std::vector<float>& outRGBA);
}
