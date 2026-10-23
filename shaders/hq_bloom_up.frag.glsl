#version 450 core
// Turbo 0.71 (S8): Render 3D HQ - bloom upsampling into the next larger
// level. Ported from Filament filament/src/materials/bloom/bloomUpsample.mat
// @ 13be3649 (Apache License 2.0, Copyright Google LLC), the QUALITY >= HIGH
// path (3x3 tent, Jimenez). Changes: GL 4.6 full-screen pass; offsets from
// uDstTexel (the destination level's texel, Filament's resolution.zw); the
// destination level is read and added here (uBase) into a texture of its
// own, where Filament blends ONE, ONE into the level - the same sum without
// touching the blend state the 2D graph shares.
in vec2 vNdc;

uniform sampler2D uSource; // the next smaller level, with everything below it added
uniform sampler2D uBase;   // this level's downsampled content
uniform vec2 uDstTexel;

layout(location = 0) out vec4 outColor;

void main()
{
   vec2 uv = vNdc * 0.5 + 0.5;
   vec4 d = vec4(uDstTexel, -uDstTexel) * 1.0;
   vec3 c0, c1;
   c0 = textureLod(uSource, uv + d.zw, 0.0).rgb;
   c0 += textureLod(uSource, uv + d.xw, 0.0).rgb;
   c0 += textureLod(uSource, uv + d.xy, 0.0).rgb;
   c0 += textureLod(uSource, uv + d.zy, 0.0).rgb;
   c0 += 4.0 * textureLod(uSource, uv, 0.0).rgb;
   c1 = textureLod(uSource, uv + vec2(d.z, 0.0), 0.0).rgb;
   c1 += textureLod(uSource, uv + vec2(0.0, d.w), 0.0).rgb;
   c1 += textureLod(uSource, uv + vec2(d.x, 0.0), 0.0).rgb;
   c1 += textureLod(uSource, uv + vec2(0.0, d.y), 0.0).rgb;
   outColor = vec4(textureLod(uBase, uv, 0.0).rgb + (c0 + 2.0 * c1) * (1.0 / 16.0), 1.0);
}
