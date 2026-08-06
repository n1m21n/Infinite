#pragma once

#include <functional>
#include <string>

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
   };

   // Lazily (re)allocates the FBO's color texture to the requested size.
   bool EnsureFbo(Fbo& fbo, int w, int h);
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
   // bound (used to blit the final node's output to the window).
   void DrawTextureToScreen(unsigned int tex, int windowW, int windowH);
}
