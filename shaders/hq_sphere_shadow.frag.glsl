#version 450 core
// Turbo 0.71 (S4): Render 3D HQ - sphere impostors in the shadow map. The
// light is orthographic, so a sphere's shadow is a disc of a fixed size in
// texels: one point sprite per particle (hq_sphere_shadow.vert), cut round
// here. Its depth is the centre's, so the lit half of the sphere is in front
// of the disc (lit) and the far half behind it (shadowed), without writing
// gl_FragDepth - early depth rejection stays on, and there is no instancing:
// a million moving particles are one vertex each, every frame.
void main()
{
   vec2 d = gl_PointCoord * 2.0 - 1.0;
   if (dot(d, d) > 1.0)
      discard;
}
