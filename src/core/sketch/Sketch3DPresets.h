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
{"City block", R"(// A city by rules: blocks of lots between streets, towers taller toward the centre.
param("blocks", 4, 1, 8);
param("lots", 3, 1, 5);
param("height", 1.6, 0.2, 4);
param("setback", 0.5, 0, 1);

function building(w, d, h) {
  fill(hsl(0.08 + 0.06 * random(), 0.12, 0.45 + 0.25 * random()));
  box(w, h, d);
  if (h > 0.6 && random() < setback) {
    const h2 = h * random(0.2, 0.5);
    push(); translate(0, h / 2 + h2 / 2, 0); box(w * 0.7, h2, d * 0.7); pop();
  }
  push(); translate(0, h / 2 + 0.02, 0); fill(0.3, 0.3, 0.32); box(w * 0.3, 0.04, d * 0.3); pop();
}

function draw(t) {
  const street = 0.12, bw = 3 / blocks, lw = (bw - street) / lots;
  push(); translate(0, -0.5, 0); rotateX(-TAU / 4); fill(0.2, 0.2, 0.22); plane(3.4, 3.4); pop();
  for (let bi = 0; bi < blocks; bi++)
    for (let bj = 0; bj < blocks; bj++)
      for (let i = 0; i < lots; i++)
        for (let j = 0; j < lots; j++) {
          const x = -1.5 + bi * bw + street / 2 + (i + 0.5) * lw;
          const z = -1.5 + bj * bw + street / 2 + (j + 0.5) * lw;
          const c = Math.max(0, 1 - Math.sqrt(x * x + z * z) / 2);
          const h = 0.08 + height * (0.15 + c * c) * (0.4 + 0.6 * noise(x * 2, z * 2, 0));
          push();
          translate(x, -0.5 + h / 2, z);
          building(lw * 0.82, lw * 0.82, h);
          pop();
        }
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
