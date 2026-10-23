#version 430
// Turbo 0.70 (R5): AMD FidelityFX Super Resolution 1, RCAS (robust
// contrast-adaptive sharpening, MIT), run on EASU's output to restore the
// detail upsampling softens. See fsr1_easu.comp.glsl for the //@include lines.
#define A_GPU 1
#define A_GLSL 1
//@include ffx_a.h

layout(local_size_x = 64) in;
layout(binding = 0) uniform sampler2D uInput;
layout(rgba8, binding = 0) uniform writeonly image2D uOutput;
uniform uvec4 uCon0;
uniform ivec2 uOutSize;

#define FSR_RCAS_F 1
AF4 FsrRcasLoadF(ASU2 p) { return texelFetch(uInput, clamp(ivec2(p), ivec2(0), uOutSize - 1), 0); }
void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b) {}
//@include ffx_fsr1.h

void Filter(AU2 pos)
{
   if (pos.x >= uint(uOutSize.x) || pos.y >= uint(uOutSize.y))
      return;
   AF3 c;
   FsrRcasF(c.r, c.g, c.b, pos, uCon0);
   imageStore(uOutput, ivec2(pos), vec4(c, texelFetch(uInput, ivec2(pos), 0).a));
}

void main()
{
   AU2 gxy = ARmp8x8(gl_LocalInvocationID.x) + AU2(gl_WorkGroupID.x << 4u, gl_WorkGroupID.y << 4u);
   Filter(gxy);
   gxy.x += 8u;
   Filter(gxy);
   gxy.y += 8u;
   Filter(gxy);
   gxy.x -= 8u;
   Filter(gxy);
}
