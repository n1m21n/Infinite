#version 430
// Turbo 0.70 (R1): sorts Render 3D's instances into what to draw. Each
// instance's bounding sphere (the mesh's own sphere through its matrix) is
// tested against the camera's frustum, and, with a LOD size set, one that
// covers fewer pixels than that goes to the small list (drawn as a
// camera-facing quad) instead of the full one. Writes two 0 / 1 flags per
// instance; the stable compaction and gathers after it (pop::gpu) keep the
// instances' order, so what is drawn is the same as without culling.
layout(local_size_x = 256) in;

// Matrix columns (vec4 each): column c of instance i is at
// col_c[uOffset[c] + i * uStride[c]] (separate PointZ buffers, or one mat4 buffer).
layout(std430, binding = 0) readonly buffer Col0 { float col0[]; };
layout(std430, binding = 1) readonly buffer Col1 { float col1[]; };
layout(std430, binding = 2) readonly buffer Col2 { float col2[]; };
layout(std430, binding = 3) readonly buffer Col3 { float col3[]; };
layout(std430, binding = 4) writeonly buffer Full { uint fullFlags[]; };
layout(std430, binding = 5) writeonly buffer Small { uint smallFlags[]; };

uniform int uCount;
uniform ivec4 uStride;
uniform ivec4 uOffset;
uniform vec4 uLocalSphere;  // the mesh's bounding sphere: centre, radius
uniform vec4 uPlanes[6];    // frustum, inside when dot(xyz, p) + w >= 0
uniform int uFrustum;
uniform mat4 uViewProj;
uniform float uProjScaleY;  // proj[1][1] * viewport height / 2: world size at w = 1 -> pixels
uniform float uLodPixels;   // 0 = no small list

vec4 Column(int c, uint i)
{
   uint b = uint(uOffset[c]) + i * uint(uStride[c]);
   if (c == 0) return vec4(col0[b], col0[b + 1u], col0[b + 2u], col0[b + 3u]);
   if (c == 1) return vec4(col1[b], col1[b + 1u], col1[b + 2u], col1[b + 3u]);
   if (c == 2) return vec4(col2[b], col2[b + 1u], col2[b + 2u], col2[b + 3u]);
   return vec4(col3[b], col3[b + 1u], col3[b + 2u], col3[b + 3u]);
}

void main()
{
   uint i = gl_GlobalInvocationID.x;
   if (i >= uint(uCount))
      return;
   mat4 m = mat4(Column(0, i), Column(1, i), Column(2, i), Column(3, i));
   vec3 centre = (m * vec4(uLocalSphere.xyz, 1.0)).xyz;
   float scale = max(length(m[0].xyz), max(length(m[1].xyz), length(m[2].xyz)));
   float radius = uLocalSphere.w * scale;
   // A collapsed instance (Active = 0, or zero scale) draws nothing either way.
   bool visible = scale > 0.0;
   if (visible && uFrustum == 1)
      for (int k = 0; k < 6; k++)
         if (dot(uPlanes[k].xyz, centre) + uPlanes[k].w < -radius)
            visible = false;
   bool small = false;
   if (visible && uLodPixels > 0.0)
   {
      float w = (uViewProj * vec4(centre, 1.0)).w;
      if (w > 1e-4)
         small = 2.0 * radius * uProjScaleY / w < uLodPixels;
   }
   fullFlags[i] = (visible && !small) ? 1u : 0u;
   smallFlags[i] = (visible && small) ? 1u : 0u;
}
