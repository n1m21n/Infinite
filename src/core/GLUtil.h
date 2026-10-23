#pragma once

#include "gl3.h"
#include <functional>
#include <string>
#include <vector>

// Infinite's small OpenGL toolkit for image nodes: render targets (Fbo),
// fragment-only programs compiled against one shared fullscreen-quad vertex
// shader, single-pass draws into a target, screen blits, and CPU readback.
// Targets the OpenGL 3.2 core profile (GLSL #version 150), the newest core
// profile macOS offers and the common floor for Windows and Linux.
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
   // `label` is the Bench::GpuMem tag B9 groups render targets by.
   // A render target side from a size param: lo..kMaxTargetSide, NaN -> lo.
   // A patch, OSC/RPC set or modulator can hold any float, and a side past
   // the knob asked the GPU for gigabytes or failed the render (B-K-038).
   constexpr int kMaxTargetSide = 8192;
   inline int TargetSide(float v, int lo)
   {
      if (!(v >= (float)lo))
         return lo;
      return v > (float)kMaxTargetSide ? kMaxTargetSide : (int)v;
   }

   bool EnsureFbo(Fbo& fbo, int w, int h, unsigned int internalFormat = GL_RGBA8,
                  const char* label = "Fbo");
   void DestroyFbo(Fbo& fbo);

   // Shared scratch render target, one per (w, h, internalFormat), for an
   // intermediate that is written and read inside a single cook and never
   // published (FilterNode's two-pass pre-pass). Nine blur-family filters in
   // a chain then hold one buffer between them instead of nine.
   //
   // The contents are valid ONLY until the next AcquireScratchFbo call, from
   // any caller: write it and consume it straight away, with no Pull, cook or
   // other acquire in between. `slot` gives one cook several distinct buffers
   // of the same size and format (acquire them all first, then re-acquire
   // the first: a later acquire can move the earlier pointers). Main GL context only - every image cook runs
   // there (projector contexts only blit), and FBO names are per context.
   // Returns nullptr if the allocation fails.
   Fbo* AcquireScratchFbo(int w, int h, unsigned int internalFormat, int slot = 0);

   // Call once per main-loop frame: frees scratch targets that have gone
   // unused for 300 frames (a resized or deleted filter's old
   // size ages out here; no per-node refcount to leak on delete or undo).
   void EndFrameScratchFbos();

   // Process-wide count of render-target (re)allocations: every time
   // EnsureFbo actually creates a texture+FBO, plus any other owner of a
   // render target (NodeViewport) that reports its own through
   // NoteFboAllocation(). Diagnostic only - fixtures difference it across a
   // run of frames to prove a steady-state path allocates nothing.
   void NoteFboAllocation();
   unsigned long long FboAllocationCount();

   inline unsigned int FboTexture(const Fbo& fbo) { return fbo.tex; }

   // Storage format of the 2D image chain (filters, Curves, Blend, Layer
   // Stack, Color Ramp). Half float, so a chain of N nodes rounds once at
   // the output instead of N times to 8 bits (docs/plans/image-fx).
   // INFINITE_IMAGE_RGBA8=1 restores the old 8-bit chain for A/B benches.
   unsigned int ImageWorkingFormat();

   // Wraps a fragment shader so its write is clamped to 0..1, the range an
   // RGBA8 target used to clamp to for free. Keeps the look of existing
   // patches while the chain stores float; nodes that want headroom opt out
   // by not using it. Renames the source's `void main()`.
   // `beforeClamp` is spliced in after the original main returns (FilterNode
   // encodes linear light back to sRGB there).
   // With headroom (uniform `int uImgHeadroom` = 1, declared by the wrapper
   // unless the source already declares it) the write keeps light above 1.0
   // and only clamps alpha and negatives: the float chain then carries
   // over-white values to the output node. Patches saved before headroom
   // existed load with it off and clip as they always did.
   std::string WithClampedOutput(const std::string& fragSrc, const char* beforeClamp = "");
   // Sets uImgHeadroom on the bound program (no-op when it has none).
   void SetImageHeadroom(unsigned int program, bool on);

   // GLSL: `vec3 OutputDither(vec3 c)` adds a fixed per-pixel offset in
   // +-0.5/255, so a value already on an 8-bit code rounds back to the same
   // code (byte-identical exports for 8-bit content) and anything between
   // codes dithers instead of banding. Paste after `out vec4 fragColor;`.
   extern const char* const kOutputDitherGlsl;

   // GLSL: `vec3 SrgbDecode(vec3)` / `vec3 SrgbEncode(vec3)`, the IEC
   // 61966-2-1 transfer pair for nodes that do their maths in linear light
   // (FilterNode's "Linear light", Blend, Layer Stack). Decode is a fit, exact
   // at 8 bits and cheap enough to call per blur tap; encode is exact.
   extern const char* const kSrgbGlsl;

   // Compiles fragSrc against a shared fullscreen-quad vertex shader (attributes
   // aPos/aUv at locations 0/1). Returns 0 on failure; if outError is non-null the
   // compile/link log is written there instead of stderr (used by FormulaNode to
   // show the user their own typo inline).
   unsigned int CompileProgram(const char* fragSrc, std::string* outError = nullptr);

   // Compiles fragSrc's fragment stage into CompileProgram's stage cache
   // without building a program, so a later CompileProgram of the same source
   // only links. A source that doesn't compile is ignored here.
   void WarmFragmentStage(const char* fragSrc);

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
   // color where alpha is 0. transparentWindow (window made with a transparent
   // framebuffer) instead writes premultiplied alpha, clears to alpha 0 and
   // draws no checkerboard, so the window itself is see-through.
   void DrawTextureToScreen(unsigned int tex, int windowW, int windowH,
                             int texW = 0, int texH = 0, bool checkerBg = false,
                             bool transparentWindow = false);

   // Reads an existing GPU texture's pixels back to the CPU as RGBA floats,
   // for nodes that need to sample a texture per-vertex rather than per-pixel
   // (e.g. displacing a mesh). `scratchFbo` is a caller-owned, lazily-created
   // framebuffer name reused across calls; `srcTex` is attached to it
   // temporarily rather than copied, so this does not take ownership of it.
   // Call once per texture change, not per vertex - a glReadPixels per vertex
   // would serialise the whole pipeline instead of one readback per rebuild.
   bool ReadTexturePixels(unsigned int& scratchFbo, unsigned int srcTex, int w, int h,
                          std::vector<float>& outRGBA);

   // Deletes the per-context GL objects this namespace lazily created for the
   // context that is current RIGHT NOW (today: the fullscreen-quad VAO).
   // Call it with a secondary context current, immediately before that
   // context is destroyed - otherwise its map entry lives on for a context
   // that no longer exists and the map grows once per projector-window
   // open/close cycle. The shared VBO and the blit programs are share-group
   // objects owned by the main context and are deliberately NOT touched.
   void ReleaseCurrentContextResources();
}
