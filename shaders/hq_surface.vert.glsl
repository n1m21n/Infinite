#version 450 core
// Turbo 0.71 (S1): Render 3D HQ - triangles, shared by the depth / normal
// prepass and the shading pass (invariant: both write the same depth).
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;

uniform mat4 uViewProj;
uniform mat4 uModel;
uniform mat3 uNormalMatrix;
// S7: Instance PointZ - one transform per point (pop::InstanceMatrices
// columns, the source's model already in them) and its colour.
layout(location = 3) in vec4 aInst0;
layout(location = 4) in vec4 aInst1;
layout(location = 5) in vec4 aInst2;
layout(location = 6) in vec4 aInst3;
layout(location = 7) in vec4 aInstColor;
uniform int uInstanced;
uniform int uInstColored;
out vec3 vInstColor;
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
   mat4 model = uModel;
   mat3 normalMatrix = uNormalMatrix;
   vInstColor = vec3(1.0);
   if (uInstanced == 1)
   {
      model = mat4(aInst0, aInst1, aInst2, aInst3);
      // The cofactor matrix: the inverse transpose up to a scale (normalised
      // later), right under non-uniform Scale, three cross products.
      normalMatrix = mat3(cross(model[1].xyz, model[2].xyz), cross(model[2].xyz, model[0].xyz),
                          cross(model[0].xyz, model[1].xyz));
      if (uInstColored == 1)
         vInstColor = aInstColor.rgb;
   }
   vec4 world = model * vec4(aPos, 1.0);
   vWorldPos = world.xyz;
   vNormal = normalMatrix * aNormal;
   vUv = aUv;
   vLocalPos = aPos;
   vCurClip = uCurViewProj * world;
   // Instances have no last-frame transform: their motion is the camera's.
   vPrevClip = uPrevViewProj * ((uInstanced == 1 ? model : uPrevModel) * vec4(aPos, 1.0));
   gl_Position = uViewProj * world;
}
