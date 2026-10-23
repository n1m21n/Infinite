#version 450 core
// Turbo 0.71 (S4): Render 3D HQ - one point sprite per PointZ point in the
// (orthographic) shadow map, read straight from the PointZ's GPU buffers;
// see hq_sphere_shadow.frag.
layout(location = 0) in vec3 aP;
layout(location = 1) in float aPScale;
layout(location = 3) in float aActive;

uniform mat4 uViewProj;
uniform mat4 uModel;
uniform float uRadiusScale;   // as hq_sphere.vert
uniform float uTexelsPerUnit; // shadow map texels per world unit

void main()
{
   vec3 c = (uModel * vec4(aP, 1.0)).xyz;
   float r = aPScale * uRadiusScale;
   if (aActive < 0.5 || r <= 0.0)
   {
      gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
      gl_PointSize = 1.0;
      return;
   }
   gl_Position = uViewProj * vec4(c, 1.0);
   gl_PointSize = max(1.0, 2.0 * r * uTexelsPerUnit);
}
