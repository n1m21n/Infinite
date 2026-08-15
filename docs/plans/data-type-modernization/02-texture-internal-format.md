In /Users/namansoni/infinte, make the GL texture internal format used for
node output FBOs configurable instead of hardcoded, so nodes that need to
store simulation state or HDR/emissive values in a texture aren't forced
through 8-bit lossy quantization.

Current state (verified): `GLUtil::Fbo` (src/core/GLUtil.h:12-18) has only
`fbo/tex/w/h`, no format field. `GLUtil::EnsureFbo` (src/core/GLUtil.cpp:
55-92) hardcodes `GL_RGBA8`/`GL_UNSIGNED_BYTE` at line 69, and its
size-only cache check at line 60 (`fbo.w == w && fbo.h == h`) means if a
format field is added without updating this check, a live format change on
an existing Fbo would be silently ignored.

Changes:
1. Add `unsigned int internalFormat = GL_RGBA8;` to the `Fbo` struct
   (GLUtil.h:12-18).
2. Change `EnsureFbo`'s signature to
   `bool EnsureFbo(Fbo& fbo, int w, int h, GLenum internalFormat = GL_RGBA8)`
   — the default preserves every existing call site's behavior with zero
   changes needed there.
3. Update the cache check at GLUtil.cpp:60 to also require
   `fbo.internalFormat == internalFormat`, else fall through to
   DestroyFbo + recreate.
4. Derive `format`/`type` for `glTexImage2D` from `internalFormat`:
   `GL_RGBA8` → `GL_RGBA`/`GL_UNSIGNED_BYTE` (current behavior),
   `GL_RGBA16F` → `GL_RGBA`/`GL_FLOAT`. Store the resolved
   `internalFormat` into `fbo.internalFormat` alongside `fbo.w`/`fbo.h`
   at the end of the function (mirroring lines 78-79).
5. Do NOT touch any of the 25 existing `GLUtil::EnsureFbo` call sites
   (RampNode.cpp:103, GenerativeNodes.cpp:435, FitNode.cpp:70,
   OutputNode.cpp:144, BlendNode.cpp:72, NoiseNode.cpp:134,
   ColorRampNode.cpp:190, LayerStackNode.cpp:113, FormulaNode.cpp:420,
   RemoveBgNode.cpp:134, PaletteNode.cpp:494, ShapeNode.cpp:198,
   FilterNode.cpp:73, SwitcherNode.cpp:113, ResynthNode.cpp:332/410/411,
   TextureNode.cpp:406, CurvesNode.cpp:252,
   DrawNode.cpp:281/283/284, FeedbackNodes.cpp:50/52/151/153/303/304/
   340/342/343) — the default parameter means they keep compiling and
   behaving identically unchanged.
6. `GLUtil::ReadTexturePixels` (GLUtil.cpp:281-308) needs no changes — it
   already reads via `glReadPixels(..., GL_RGBA, GL_FLOAT, ...)`
   regardless of the source texture's storage format.
7. Do NOT touch `NodeViewport::EnsureFbo` (src/core/NodeViewport.cpp:280,
   its own hardcoded GL_RGBA8 at lines 149/289) — that's a separate,
   unrelated class method, out of scope for this change.
8. As the one concrete opt-in demonstrating this works, change
   FeedbackNodes.cpp (the double/triple-buffered feedback node family,
   the most natural candidate for storing non-clamped simulation state)
   to request `GL_RGBA16F` for its buffers. Note its existing
   `clamp(...,0,1)` calls at FeedbackNodes.cpp:239 and :251 will still
   clamp values into 0..1 regardless of the storage format upgrade —
   getting actual HDR range (not just extra precision within 0..1) out
   of this node would require removing those clamps too. Leave that as
   a separate follow-up; do not remove those clamps as part of this
   change unless asked.

Explicitly out of scope: revisiting per-node GLSL `clamp(col, 0.0, 1.0)`
calls in RampNode.cpp:70, ResynthNode.cpp:137, ColorRampNode.cpp:24,
TextureNode.cpp:358 — those are separate, node-by-node decisions about
whether each node's *output range*, not just its storage precision,
should go HDR.

Build with:
  cmake --build build -j"$(sysctl -n hw.ncpu)"
Confirm it compiles clean, then visually confirm (run the app, or use
the `run` skill) that every node using the GL_RGBA8 default renders
identically to before — this change must be invisible except where
FeedbackNodes.cpp was explicitly opted into GL_RGBA16F.
