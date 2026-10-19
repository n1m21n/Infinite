#pragma once

#include <string>
#include <vector>

// Shared blend-mode vocabulary: the Affinity-style mode list plus the GLSL that
// implements it. BlendNode (2 inputs) and LayerStackNode (N inputs) both splice
// kBlendGLSL into their fragment shaders so the modes stay identical between them.
namespace BlendModes
{
   const std::vector<std::string>& Names();

   // Names a dropdown should offer for a layer whose current mode is `current`:
   // the 32 live modes, plus the three "(old)" ones only while one of them is
   // already selected (so a patch saved before the fix still shows its look but
   // nobody can pick them fresh).
   const std::vector<std::string>& NamesFor(int current);

   // Defines: float Lum(vec3), vec3 blendMode(int mode, vec3 base, vec3 top)
   // Mode index 30 (Erase) and mode index 31 (Anti-Erase, the exact complement
   // of Erase) are both alpha-only and must be handled by the caller.
   extern const char* kBlendGLSL;

   const int kEraseMode = 30;
   const int kAntiEraseMode = 31;

   // Modes 32..34 are the pre-fix Overlay / Soft Light / Hard Light (the old
   // shader had Overlay and Hard Light swapped and Soft Light's pivot inverted).
   // Patches saved before the fix are mapped onto them so they keep their look.
   inline int LegacyMode(int mode) { return (mode >= 11 && mode <= 13) ? mode + 21 : mode; }
}
