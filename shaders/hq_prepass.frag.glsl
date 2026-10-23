#version 450 core
// Turbo 0.71 (S1): Render 3D HQ prepass - depth, plus the view-space normal
// and roughness for the screen-space passes (GTAO, contact shadows: S4, S5).
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUv;
in vec4 vCurClip;
in vec4 vPrevClip;

uniform mat4 uView;
uniform float uRoughness;
uniform vec2 uFrameSize;

layout(location = 1) out vec4 outNormal;
// S6: how far this surface moved on screen since last frame (pixels,
// unjittered, now - last; B = 1: Render3DTemporal::MotionVectors takes it
// over the camera-only motion).
layout(location = 2) out vec4 outVelocity;

void main()
{
   vec3 n = normalize(vNormal);
   if (!gl_FrontFacing)
      n = -n;
   outNormal = vec4(normalize(mat3(uView) * n), uRoughness);
   vec2 v = vPrevClip.w > 1e-6 ? (vCurClip.xy / vCurClip.w - vPrevClip.xy / vPrevClip.w) * 0.5 * uFrameSize
                               : vec2(0.0);
   outVelocity = vec4(v, 1.0, 0.0);
}
