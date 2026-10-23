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
   // `label` names the target for upstream's GPU memory bench; in Turbo it
   // labels the texture and framebuffer for GPU captures (F3).
   bool EnsureFbo(Fbo& fbo, int w, int h, unsigned int internalFormat = GL_RGBA8,
                  const char* label = "Fbo");
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

   // Turbo (TOP parity): how a shader reads past the edge of its input
   // (TouchDesigner's Extend) and how it filters it (Input Smoothness).
   // Sampler objects override the texture's own wrap/filter state for one
   // texture unit only, so a consumer can read an upstream texture with
   // Repeat or Nearest without changing what any other consumer of that same
   // texture sees. Always UnbindSampler after the pass: an unbound sampler
   // is what every other node in the codebase assumes.
   enum Extend { kExtendHold = 0, kExtendZero, kExtendRepeat, kExtendMirror, kExtendCount };
   enum Filter { kFilterLinear = 0, kFilterNearest, kFilterMipmap, kFilterCount };
   unsigned int SamplerFor(int extend, int filter);
   void BindSampler(int unit, int extend, int filter);
   void UnbindSampler(int unit);
   // Rebuilds tex's mip chain; kFilterMipmap reads it. Call when the
   // texture's pixels changed (its producer's revision moved).
   void GenerateMipmaps(unsigned int tex);
   const char* FormatName(unsigned int internalFormat); // "8-bit" / "16f" / "32f"

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
                             int fitMode = kFitLetterbox, const float* bgRGB = nullptr,
                             bool transparentWindow = false);
   // Turbo 0.51 (upstream 8af69a1): transparentWindow (a window made with a
   // transparent framebuffer) writes the texture's alpha premultiplied, clears
   // to alpha 0 and skips the background mix, so the desktop / OBS window
   // capture sees through a node with a transparent background.

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

   // Turbo 0.62: a texture read back to the CPU without making the CPU wait
   // for the GPU. Each Read() queues a copy into a pixel-pack buffer (fenced)
   // and hands back the newest copy the GPU has finished - normally last
   // frame's - so analysers, colour pickers and image-driven geometry trail
   // their input by about a frame instead of stalling the whole pipeline
   // every cook. The very first Read() (and any size change) reads
   // synchronously, so a node still has data from its first cook.
   class AsyncReadback
   {
   public:
      AsyncReadback() = default;
      AsyncReadback(const AsyncReadback&) = delete;
      AsyncReadback& operator=(const AsyncReadback&) = delete;
      ~AsyncReadback();
      // `tex` is w x h; `out` receives w * h pixels of `format` / `type`
      // (GL_RGBA / GL_RED..., GL_UNSIGNED_BYTE or GL_FLOAT), tightly packed.
      // False (out untouched) when no new copy has finished yet.
      bool Read(unsigned int tex, int w, int h, unsigned int format, unsigned int type, void* out);
      // Same, for the framebuffer that is bound for reading when it is called.
      bool ReadBound(int w, int h, unsigned int format, unsigned int type, void* out);

   private:
      bool Run(unsigned int tex, int w, int h, unsigned int format, unsigned int type, void* out);
      struct Slot
      {
         unsigned int pbo = 0;
         void* fence = nullptr;
         int w = 0, h = 0;
         unsigned int format = 0, type = 0;
         size_t bytes = 0, capacity = 0;
         unsigned long long order = 0;
      };
      Slot mSlots[3];
      unsigned int mFbo = 0;
      unsigned long long mSeq = 0;
      int mLastW = 0, mLastH = 0;
      unsigned int mLastFormat = 0, mLastType = 0;
   };

   // Turbo 0.70 (F3): names for GPU captures (RenderDoc, Nsight Graphics).
   // Debug groups bracket a piece of GPU work, labels name GL objects; both
   // need GL_KHR_debug (core in 4.3) and are no-ops without it, so a push
   // and its pop always agree. Main GL context only.
   void PushDebugGroup(const char* name);
   void PopDebugGroup();
   // `identifier` is GL_TEXTURE, GL_FRAMEBUFFER, GL_BUFFER, GL_PROGRAM...
   void LabelObject(unsigned int identifier, unsigned int object, const char* label);
   class DebugScope
   {
   public:
      explicit DebugScope(const char* name) { PushDebugGroup(name); }
      explicit DebugScope(const std::string& name) { PushDebugGroup(name.c_str()); }
      ~DebugScope() { PopDebugGroup(); }
      DebugScope(const DebugScope&) = delete;
      DebugScope& operator=(const DebugScope&) = delete;
   };
   // Debug builds, or INFINITE_GLDEBUG=1: GL_DEBUG_OUTPUT with a callback that
   // prints errors, undefined behaviour and performance warnings to stderr
   // (the first 200). INFINITE_GLDEBUG=sync also names the debug group each
   // message came from and, for errors and performance warnings, the calling
   // code (synchronous output, slower; symbols from the exe's PDB). Call
   // once, with the main context current.
   void InstallDebugOutput();
}
