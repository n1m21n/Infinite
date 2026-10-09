#pragma once
// Built-in Sketch presets. Header-only so SketchNode and sketch-check share one list.
// Every preset is a pure function of (t, params, width, height): no state carried
// between frames, so scrubbing and export give the same pixels.
namespace SketchPresets
{
struct Entry { const char* name; const char* code; };

inline const Entry* All(int& n)
{
   static const Entry k[] = {
{"Radial burst", R"(// Radial burst. Every param() is a knob you can modulate.
param("count", 12, 1, 64);
param("spin", 0.2, 0, 2);
param("size", 48, 4, 160);

function draw(t) {
  background(0.05);
  translate(width / 2, height / 2);
  noStroke();
  const r = min(width, height) * 0.3;
  for (let i = 0; i < count; i++) {
    rotate(TAU / count + spin * t * 0.1);
    fill(hsl(i / count, 0.7, 0.6));
    circle(r * 1.2 + r * 0.4 * sin(t + i), 0, size);
  }
}
)"},
{"Flow field", R"(// Flow-field lines.
param("lines", 220, 20, 600);
param("scale", 2.5, 0.5, 8);
param("weight", 1.5, 0.5, 6);

function draw(t) {
  background(0.04, 0.05, 0.08);
  noFill();
  strokeWeight(weight);
  for (let i = 0; i < lines; i++) {
    let x = ((i * 0.6180339) % 1) * width;
    let y = ((i * 0.7548776) % 1) * height;
    stroke(hsl(0.55 + 0.2 * (i / lines), 0.6, 0.65), 0.7);
    beginShape();
    for (let s = 0; s < 40; s++) {
      vertex(x, y);
      const a = noise(x / width * scale, y / height * scale, t * 0.2) * TAU * 2;
      x += cos(a) * 10;
      y += sin(a) * 10;
    }
    endShape();
  }
}
)"},
{"Tree", R"(// Recursive tree.
param("depth", 9, 2, 12);
param("angle", 0.45, 0.1, 1.2);
param("sway", 0.08, 0, 0.4);

let T = 0;
function branch(len, d) {
  strokeWeight(max(1, d * 0.9));
  stroke(0.35 + 0.05 * (12 - d), 0.55, 0.3 + 0.04 * (12 - d));
  line(0, 0, 0, -len);
  translate(0, -len);
  if (d <= 0) return;
  push(); rotate(angle + sway * sin(T * 1.3 + d)); branch(len * 0.74, d - 1); pop();
  push(); rotate(-angle + sway * sin(T * 1.1 + d)); branch(len * 0.74, d - 1); pop();
}

function draw(t) {
  T = t;
  background(0.93, 0.92, 0.88);
  translate(width / 2, height * 0.95);
  branch(height * 0.2, depth);
}
)"},
{"Kinetic type", R"(// Words that ride a wave. Change the string, keep the knobs.
param("size", 260, 24, 500);
param("wave", 40, 0, 200);
param("speed", 1, 0, 4);

const word = "INFINITE";
function draw(t) {
  background(0.06);
  textSize(size);
  noStroke();
  const total = textWidth(word);
  let x = (width - total) / 2;
  for (let i = 0; i < word.length; i++) {
    const ch = word[i];
    const y = height / 2 + wave * sin(t * speed * 2 + i * 0.7);
    fill(hsl(0.08 + i * 0.06, 0.8, 0.65));
    text(ch, x, y);
    x += textWidth(ch);
  }
}
)"},
{"Grid poster", R"(// Swiss-style grid of rotating squares.
param("cols", 8, 2, 24);
param("gap", 0.15, 0, 0.5);
param("twist", 1, 0, 4);

function draw(t) {
  background(0.95, 0.94, 0.9);
  const rows = Math.max(1, Math.round(cols * height / width));
  const cw = width / cols, ch = height / rows;
  noStroke();
  for (let r = 0; r < rows; r++) {
    for (let c = 0; c < cols; c++) {
      const k = (r * cols + c) / (rows * cols);
      push();
      translate((c + 0.5) * cw, (r + 0.5) * ch);
      rotate(twist * sin(t * 0.6 + k * TAU));
      fill(hsl(0.02 + 0.1 * k, 0.8, 0.5 + 0.1 * sin(t + k * 9)));
      const s = Math.min(cw, ch) * (1 - gap);
      rect(-s / 2, -s / 2, s, s);
      pop();
    }
  }
}
)"},
{"Particles", R"(// A particle swarm. Each particle is a pure function of t, so it scrubs.
param("count", 300, 10, 1500);
param("orbit", 0.45, 0.05, 0.6);
param("dot", 7, 1, 20);

function hash(n) { const s = Math.sin(n * 127.1 + 311.7) * 43758.5453; return s - Math.floor(s); }

function draw(t) {
  background(0.02, 0.02, 0.05);
  noStroke();
  const R = min(width, height);
  for (let i = 0; i < count; i++) {
    const r = R * orbit * (0.3 + 0.7 * hash(i));
    const w = (0.2 + hash(i + 17)) * (hash(i + 5) < 0.5 ? 1 : -1);
    const a = hash(i + 91) * TAU + t * w;
    const x = width / 2 + r * cos(a), y = height / 2 + r * sin(a) * 0.8;
    fill(hsl(0.55 + 0.3 * hash(i + 3), 0.7, 0.7), 0.8);
    circle(x, y, dot * (0.5 + hash(i + 40)));
  }
}
)"},
{"Lissajous", R"(// A harmonograph: two frequencies trace a looping curve.
param("a", 3, 1, 9);
param("b", 4, 1, 9);
param("trail", 400, 50, 1200);

function draw(t) {
  background(0.03, 0.03, 0.06);
  noFill();
  strokeWeight(3);
  stroke(hsl(0.12 + 0.1 * sin(t * 0.3), 0.8, 0.65));
  const R = min(width, height) * 0.42;
  beginShape();
  for (let i = 0; i < trail; i++) {
    const u = t * 0.5 - i * 0.01;
    vertex(width / 2 + R * sin(a * u + 1.2), height / 2 + R * sin(b * u));
  }
  endShape();
}
)"},
{"Rings", R"(// Concentric pulsing rings.
param("rings", 14, 2, 40);
param("pulse", 0.5, 0, 1);
param("weight", 4, 1, 20);

function draw(t) {
  background(0.97);
  noFill();
  strokeWeight(weight);
  const R = min(width, height) * 0.48;
  for (let i = 1; i <= rings; i++) {
    const k = i / rings;
    stroke(hsl(0.6 - 0.5 * k, 0.6, 0.45), 0.9);
    circle(width / 2, height / 2, 2 * R * k * (1 + pulse * 0.1 * sin(t * 2 - k * 6)));
  }
}
)"},
   };
   n = (int)(sizeof(k) / sizeof(k[0]));
   return k;
}
} // namespace SketchPresets
