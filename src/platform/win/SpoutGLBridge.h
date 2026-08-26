// Tiny GL-side helper for the Windows Syphon-via-Spout implementation
// (PlatformWin.cpp). Kept in its own translation unit so that PlatformWin.cpp
// - which must include SpoutGL's headers, and those drag in the legacy
// Microsoft <GL/gl.h> - never has glad (gl3.h) and <GL/gl.h> visible in the
// same TU. This file uses glad exclusively, the same way every other GL call
// site in this codebase (GLUtil.cpp, node .cpp files) already does.
//
// Spout hands back a plain GL_TEXTURE_2D; the Syphon node code
// (SyphonInNode.cpp) is hardcoded for GL_TEXTURE_RECTANGLE, matching macOS's
// Syphon contract. This bridge blits the former into an owned rectangle
// target so that contract stays true on Windows without touching node code.
#pragma once

namespace SpoutGLBridge
{
   // Allocates (or resizes) a GL_TEXTURE_2D for Spout's ReceiveTexture() to
   // copy into. `tex` must start at 0; it is (re)created on size change.
   void EnsureReceiveTexture(unsigned int& tex, int& curW, int& curH, int w, int h);

   // Allocates (or resizes) a GL_TEXTURE_RECTANGLE color target plus its
   // owning FBO. `fbo`/`tex` must start at 0. Returns false if the FBO is
   // incomplete.
   bool EnsureRectangleTarget(unsigned int& fbo, unsigned int& tex, int& curW, int& curH, int w, int h);

   // Blits `srcTex2D` (a GL_TEXTURE_2D sized srcW x srcH) into `dstFbo`'s
   // GL_TEXTURE_RECTANGLE attachment, which must already be that size.
   bool BlitToRectangle(unsigned int dstFbo, unsigned int srcTex2D, int srcW, int srcH);

   void DeleteTexture(unsigned int& tex);
   void DeleteFbo(unsigned int& fbo);
}
