#pragma once

#include <string>
#include <vector>

// Shared blend-mode vocabulary: the Affinity-style mode list plus the GLSL that
// implements it. BlendNode (2 inputs) and LayerStackNode (N inputs) both splice
// kBlendGLSL into their fragment shaders so the modes stay identical between them.
namespace BlendModes
{
   const std::vector<std::string>& Names();

   // Defines: float Lum(vec3), vec3 blendMode(int mode, vec3 base, vec3 top)
   // Mode index 30 (Erase) is alpha-only and must be handled by the caller.
   extern const char* kBlendGLSL;

   const int kEraseMode = 30;
}
