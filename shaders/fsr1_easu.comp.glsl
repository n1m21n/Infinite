#version 430
// Turbo 0.70 (R5): AMD FidelityFX Super Resolution 1, EASU (edge-adaptive
// spatial upsampling, MIT), from Render 3D's internal resolution to its
// output size. Runs on the tonemapped sRGB image, as FSR 1 expects. The
// //@include lines are replaced with the embedded AMD headers at load time
// (Render3DTemporal.cpp); the structure follows AMD's FSR_Pass.glsl sample.
#define A_GPU 1
#define A_GLSL 1
//@include ffx_a.h

layout(local_size_x = 64) in;
layout(binding = 0) uniform sampler2D uInput;
layout(rgba8, binding = 0) uniform writeonly image2D uOutput;
uniform uvec4 uCon0;
uniform uvec4 uCon1;
uniform uvec4 uCon2;
uniform uvec4 uCon3;
uniform ivec2 uOutSize;

#define FSR_EASU_F 1
AF4 FsrEasuRF(AF2 p) { return textureGather(uInput, p, 0); }
AF4 FsrEasuGF(AF2 p) { return textureGather(uInput, p, 1); }
AF4 FsrEasuBF(AF2 p) { return textureGather(uInput, p, 2); }
//@include ffx_fsr1.h

void Filter(AU2 pos)
{
   if (pos.x >= uint(uOutSize.x) || pos.y >= uint(uOutSize.y))
      return;
   AF3 c;
   FsrEasuF(c, pos, uCon0, uCon1, uCon2, uCon3);
   // EASU is colour only; alpha (the background opacity) is filtered plainly.
   float a = textureLod(uInput, (vec2(pos) + 0.5) / vec2(uOutSize), 0.0).a;
   imageStore(uOutput, ivec2(pos), vec4(c, a));
}

void main()
{
   // AMD's 8x8 remap of a 64-thread group onto a 16x16 tile.
   AU2 gxy = ARmp8x8(gl_LocalInvocationID.x) + AU2(gl_WorkGroupID.x << 4u, gl_WorkGroupID.y << 4u);
   Filter(gxy);
   gxy.x += 8u;
   Filter(gxy);
   gxy.y += 8u;
   Filter(gxy);
   gxy.x -= 8u;
   Filter(gxy);
}
