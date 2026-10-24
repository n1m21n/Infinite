#version 450 core
// Turbo 0.71 (S3): Render 3D HQ - one sphere impostor per PointZ point, drawn
// as an instanced 4-vertex strip read straight from the PointZ's GPU buffers
// (no CPU copy, no per-frame kernel). The quad lies in the plane tangent to
// the sphere at its point nearest the eye (perpendicular to the ray to the
// centre), exactly as large as the silhouette cone there:
// (d - r) * tan(asin(r / d)). The whole sphere is behind that plane, so every
// ray hit is deeper than the quad - hq_sphere.frag declares depth_greater and
// hidden impostor pixels are still rejected before shading.
layout(location = 0) in vec3 aP;       // P, divisor 1
layout(location = 1) in float aPScale; // PScale, divisor 1
layout(location = 2) in vec4 aColor;   // Color, divisor 1
layout(location = 3) in float aActive; // Active, divisor 1
layout(location = 4) in vec3 aVel;     // S6: PartVel (units / second; 0 when the PointZ has none), divisor 1

uniform mat4 uViewProj;
// S6 (prepass velocity): unjittered cameras and last frame's model; the
// centre last frame is P - PartVel * uVelDt.
uniform mat4 uCurViewProj;
uniform mat4 uPrevViewProj;
uniform mat4 uPrevModel;
uniform float uVelDt;
flat out vec4 vCurClip;
flat out vec4 vPrevClip;
uniform mat4 uModel;
uniform vec3 uCamPos;
uniform float uRadiusScale; // PointBaseSize (x 0.02 without PScale) x the model's scale
uniform int uOrtho;
uniform vec3 uViewDir;      // camera forward (orthographic rays)
// Turbo 0.71.1: "points as dots" - a grain uDotPixels x PScale pixels wide
// at any distance (sand) instead of a sphere of PScale world units; 0 =
// spheres. uPixelWorld: world size of one pixel at distance 1 (perspective)
// or anywhere (orthographic).
uniform float uDotPixels;
uniform float uPixelWorld;

out vec3 vWorldPos;
flat out vec3 vCentre;
flat out float vRadius;
flat out vec4 vColor;

void main()
{
   vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1)) * 2.0 - 1.0;
   vec3 c = (uModel * vec4(aP, 1.0)).xyz;
   vCurClip = uCurViewProj * vec4(c, 1.0);
   vPrevClip = uPrevViewProj * (uPrevModel * vec4(aP - aVel * uVelDt, 1.0));
   float r = aPScale * uRadiusScale;
   vec3 toC = (uOrtho == 1) ? uViewDir : c - uCamPos;
   float d = length(toC);
   if (uDotPixels > 0.0) // 0.71.1: the grain's pixels as a radius at its distance
      r = 0.5 * uDotPixels * aPScale * uPixelWorld * ((uOrtho == 1) ? 1.0 : d);
   // Hidden, zero-sized, or the eye inside it: off screen.
   if (aActive < 0.5 || r <= 0.0 || (uOrtho == 0 && d <= r * 1.001))
   {
      gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
      vWorldPos = c;
      vCentre = c;
      vRadius = 0.0;
      vColor = aColor;
      return;
   }
   vec3 f = toC / d;
   vec3 up0 = abs(f.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
   vec3 right = normalize(cross(f, up0));
   vec3 up = cross(right, f);
   float h = (uOrtho == 1) ? r : r * (d - r) / sqrt(d * d - r * r);
   vWorldPos = c - f * r + (right * corner.x + up * corner.y) * h;
   vCentre = c;
   vRadius = r;
   vColor = aColor;
   gl_Position = uViewProj * vec4(vWorldPos, 1.0);
}
