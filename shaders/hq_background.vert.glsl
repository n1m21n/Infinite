#version 450 core
// Turbo 0.71 (S1): Render 3D HQ - one triangle over the screen, on the far
// plane (drawn where the prepass left depth 1).
out vec2 vNdc;

void main()
{
   vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
   vNdc = p;
   gl_Position = vec4(p, 1.0, 1.0);
}
