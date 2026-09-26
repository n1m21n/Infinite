#pragma once

#include "platform/OpenGLHeaders.h"
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

   // Shared scratch render target, one per (w, h, internalFormat), for an
   // intermediate written and read inside a single cook and never published
   // (FilterNode's two-pass pre-pass): nine blurs in a chain hold one buffer
   // instead of nine. Valid ONLY until the next AcquireScratchFbo from any
   // caller - write it and consume it straight away. Main GL context only.
   Fbo* AcquireScratchFbo(int w, int h, unsigned int internalFormat);

   // Once per main-loop frame: frees scratch targets unused for 300 frames.
   void EndFrameScratchFbos();

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
   //
   // Turbo: `fitMode` picks how the texture maps to the window when texW/texH
   // are given (see OutputFitMode), and the bars/background are `bgRGB`
   // (black by default - a square video on a 16:9 fullscreen output used to
   // show grey side bars).
   enum OutputFitMode { kFitLetterbox = 0, kFitPixel = 1, kFitFill = 2, kFitStretch = 3 };
   void DrawTextureToScreen(unsigned int tex, int windowW, int windowH,
                             int texW = 0, int texH = 0, bool checkerBg = false,
                             int fitMode = kFitLetterbox, const float* bgRGB = nullptr);

   // Draws the shared unit quad (clip-space -1..1, vUv 0..1) with whatever
   // program/viewport is bound. Used by nodes that composite into sub-rects.
   void DrawFullscreenQuad();

   // VAOs are not shared by GLFW OpenGL contexts even when textures,
   // programs and buffers are. Call while an auxiliary context is current,
   // immediately before destroying its window.
   void ForgetCurrentContextObjects();

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
