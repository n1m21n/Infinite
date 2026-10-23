#version 450 core
// Turbo 0.71 (S1): Render 3D HQ - triangles, shared by the depth / normal
// prepass and the shading pass (invariant: both write the same depth).
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;

uniform mat4 uViewProj;
uniform mat4 uModel;
uniform mat3 uNormalMatrix;
// S6 (prepass velocity): this frame's camera without the TAA jitter, last
// frame's camera and model matrix.
uniform mat4 uCurViewProj;
uniform mat4 uPrevViewProj;
uniform mat4 uPrevModel;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUv;
out vec3 vLocalPos; // Turbo 0.71 (S2): Mapping (generated / object space)
out vec4 vCurClip;
out vec4 vPrevClip;
invariant gl_Position;

void main()
{
   vec4 world = uModel * vec4(aPos, 1.0);
   vWorldPos = world.xyz;
   vNormal = uNormalMatrix * aNormal;
   vUv = aUv;
   vLocalPos = aPos;
   vCurClip = uCurViewProj * world;
   vPrevClip = uPrevViewProj * (uPrevModel * vec4(aPos, 1.0));
   gl_Position = uViewProj * world;
}
