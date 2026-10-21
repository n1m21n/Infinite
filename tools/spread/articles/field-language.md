# designing a domain-inferred kernel language for shaders, javascript sketches, and audio dsp

> how infinite infers execution backends (glsl, javascript 2d sketches, or sample-level dsp) from context.

![Field Live Code Editor in Infinite](https://raw.githubusercontent.com/n1m21n/infinite/main/website/assets/field_pixel_editor.png)

most nodes in a modular workstation do one fixed thing: a blur node blurs, a delay node delays. but creative coding requires flexibility. instead of forcing artists to write separate code in glsl for shaders, c++ for audio dsp, and compute shaders for mesh deformers, we built **field**, an embedded domain-inferred kernel language inside [infinite](https://infinitedevices.org), alongside native **javascript sketches**.

### 1. the core concept: write the kernel, infer the domain

in field, you write a short kernel that runs once per element of a domain forever. you never explicitly declare where it runs:

```glsl
// once per pixel, on the gpu: a shader
p = (uv - 0.5) * aspect;
col = vec3(sin(p.x * 12.0 + t), cos(p.y * 8.0 - t), sin(t * 0.5));

// once per point in a 3d mesh: a geometry deformer
p.y += sin(p.x * 4.0 + t) * 0.2;

// once per audio sample: a dsp effect
out = in * gain;
```

from the variables your code accesses, field infers whether to compile the kernel straight into glsl for the gpu pipeline or into a high-performance vector loop for sample-level audio processing.

### 2. javascript sketches: p5-style algorithmic art

for developers who prefer procedural 2d algorithmic art, strange attractors, and cellular automata, infinite includes a native **sketch** node running embedded javascript:

![JavaScript Sketch Showcase in Infinite](https://raw.githubusercontent.com/n1m21n/infinite/main/art/sketch_showcase_preview.png)

```javascript
param("points", 9000, 1000, 30000);
param("drift", 0.15, 0, 1);
param("size", 1.6, 0.5, 4);

function draw(t) {
  background(0.03, 0.03, 0.06);
  const a = -1.4 + 0.25 * sin(t * drift), b = 1.6 + 0.2 * cos(t * drift * 0.8);
  const c = 1.0 + 0.2 * sin(t * drift * 0.6), d = 0.7;
  let x = 0.1, y = 0.1;
  const k = min(width, height) * 0.2;
  noStroke();
  for (let i = 0; i < points; i++) {
    const nx = sin(a * y) + c * cos(a * x), ny = sin(b * x) + d * cos(b * y);
    x = nx; y = ny;
    if (i > 20) {
      fill(hsl(0.55 + 0.25 * (i / points), 0.7, 0.62, 0.35));
      circle(width / 2 + x * k, height / 2 + y * k, size);
    }
  }
}
```

### 3. dynamic pins without c++ recompilation

field nodes can declare dynamic input and output pins directly in code:
```
param float size = 0.3 [0.05, 0.6];
param float feedback = 0.7 [0.0, 1.0];
```
infinite parses these declarations and immediately exposes real, cable-wireable pins on the node canvas that can connect to any lfo, midi controller, or hosted vst3 plugin slider.

- read the manual and try it: https://infinitedevices.org
- full repository: https://github.com/n1m21n/infinite
