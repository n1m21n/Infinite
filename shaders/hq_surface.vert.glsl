#version 450 core
// Turbo 0.71 (S1): Render 3D HQ - triangles, shared by the depth / normal
// prepass and the shading pass (invariant: both write the same depth).
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;

uniform mat4 uViewProj;
uniform mat4 uModel;
uniform mat3 uNormalMatrix;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUv;
out vec3 vLocalPos; // Turbo 0.71 (S2): Mapping (generated / object space)
invariant gl_Position;

void main()
{
   vec4 world = uModel * vec4(aPos, 1.0);
   vWorldPos = world.xyz;
   vNormal = uNormalMatrix * aNormal;
   vUv = aUv;
   vLocalPos = aPos;
   gl_Position = uViewProj * world;
}
