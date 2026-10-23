#version 450 core
// Turbo 0.71 (S1): Render 3D HQ prepass - depth, plus the view-space normal
// and roughness for the screen-space passes (GTAO, contact shadows: S4, S5).
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUv;

uniform mat4 uView;
uniform float uRoughness;

layout(location = 1) out vec4 outNormal;

void main()
{
   vec3 n = normalize(vNormal);
   if (!gl_FrontFacing)
      n = -n;
   outNormal = vec4(normalize(mat3(uView) * n), uRoughness);
}
