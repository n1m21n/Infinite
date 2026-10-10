#pragma once
// Built-in Sketch 3D presets. Each is a pure function of (t, params): no state carried
// between frames, so scrubbing and export give the same mesh.
namespace Sketch3DPresets
{
struct Entry { const char* name; const char* code; };

inline const Entry* All(int& n)
{
   static const Entry k[] = {
{"Orbiting cubes", R"(// Cubes on a ring. Every param() is a knob you can modulate.
param("count", 12, 1, 48);
param("radius", 1.2, 0.2, 3);
param("size", 0.25, 0.05, 0.8);
param("spin", 0.3, 0, 2);

function draw(t) {
  for (let i = 0; i < count; i++) {
    push();
    rotateY(TAU * i / count + spin * t);
    translate(radius, 0.3 * sin(t * 2 + i), 0);
    rotateX(t + i);
    fill(hsl(i / count, 0.65, 0.6));
    box(size);
    pop();
  }
}
)"},
{"Helix of spheres", R"(// A rising helix of spheres.
param("turns", 3, 1, 8);
param("count", 48, 4, 120);
param("rise", 2.4, 0.5, 5);

function draw(t) {
  for (let i = 0; i < count; i++) {
    const u = i / count, a = u * TAU * turns + t;
    push();
    translate(cos(a) * 0.8, (u - 0.5) * rise, sin(a) * 0.8);
    fill(hsl(u + t * 0.05, 0.7, 0.6));
    sphere(0.06 + 0.05 * (0.5 + 0.5 * sin(a * 2)), 10);
    pop();
  }
}
)"},
{"Wave grid", R"(// A grid of columns rippling with noise.
param("n", 14, 3, 30);
param("height", 1.2, 0.1, 3);
param("speed", 0.4, 0, 2);

function draw(t) {
  const s = 3 / n;
  for (let i = 0; i < n; i++)
    for (let j = 0; j < n; j++) {
      const h = 0.1 + height * noise(i * 0.3, j * 0.3, t * speed);
      push();
      translate((i - n / 2 + 0.5) * s, h / 2 - 0.5, (j - n / 2 + 0.5) * s);
      fill(hsl(0.55 + 0.2 * h, 0.5, 0.35 + 0.3 * h));
      box(s * 0.85, h, s * 0.85);
      pop();
    }
}
)"},
{"Recursive tree", R"(// A 3D tree built from tubes.
param("depth", 5, 1, 7);
param("spread", 0.55, 0.2, 1.2);
param("sway", 0.08, 0, 0.3);

function branch(len, d, t) {
  tube(0, 0, 0, 0, len, 0, 0.02 * d + 0.004, 6);
  translate(0, len, 0);
  if (d <= 1) { fill(0.4, 0.8, 0.3); sphere(0.05, 6); return; }
  for (let k = 0; k < 3; k++) {
    push();
    rotateY(k * TAU / 3 + d);
    rotateZ(spread + sway * sin(t + d + k));
    fill(0.35 + 0.1 * d / depth, 0.25, 0.15);
    branch(len * 0.7, d - 1, t);
    pop();
  }
}

function draw(t) {
  translate(0, -1, 0);
  fill(0.4, 0.28, 0.15);
  branch(0.7, depth, t);
}
)"},
{"Torus knot", R"(// A torus knot from many short tubes.
param("p", 2, 1, 7);
param("q", 3, 1, 7);
param("segs", 160, 24, 400);
param("thick", 0.06, 0.01, 0.2);

function pt(u, t) {
  const r = 0.6 + 0.25 * cos(q * u);
  return [r * cos(p * u + t * 0.2), 0.25 * sin(q * u), r * sin(p * u + t * 0.2)];
}

function draw(t) {
  let a = pt(0, t);
  for (let i = 1; i <= segs; i++) {
    const b = pt(TAU * i / segs, t);
    fill(hsl(i / segs, 0.7, 0.6));
    tube(a[0], a[1], a[2], b[0], b[1], b[2], thick, 8);
    a = b;
  }
}
)"},
{"Spiky ball", R"(// Cones fanned over a sphere.
param("count", 120, 10, 400);
param("length", 0.5, 0.1, 1.2);
param("pulse", 0.2, 0, 0.6);

function draw(t) {
  for (let i = 0; i < count; i++) {
    const y = 1 - 2 * (i + 0.5) / count, r = sqrt(1 - y * y), a = i * 2.39996323 + t * 0.2;
    const dx = r * cos(a), dz = r * sin(a);
    push();
    translate(dx * 0.6, y * 0.6, dz * 0.6);
    // point the cone's +y at the outward direction
    rotate(acos(y), dz, 0, -dx);
    fill(hsl(i / count, 0.6, 0.6));
    cone(0.04, length * (1 + pulse * sin(t * 2 + i * 0.5)), 6);
    pop();
  }
  fill(0.15);
  sphere(0.6, 20);
}
)"},
   };
   n = (int)(sizeof(k) / sizeof(k[0]));
   return k;
}
} // namespace Sketch3DPresets
