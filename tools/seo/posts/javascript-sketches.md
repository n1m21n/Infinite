---
title: JavaScript sketches as nodes: draw with code, wire it to anything
description: How Sketch nodes let you draw with JavaScript and still expose real pins.
date: 2026-10-17
category: product
tags: [javascript,creative coding,sketch,generative art]
status: draft
verify: Check Sketch node API names against the node manual; 60fps and point-count claims need a measured run
---

![JavaScript Sketch Showcase in Infinite](https://raw.githubusercontent.com/n1m21n/infinite/main/art/sketch_showcase_preview.png)

creative coders generally face a difficult compromise:
- **in the browser** (p5.js, three.js, canvas2d): rapid iteration, clean drawing apis, but limited audio precision, browser sandbox limits, and no native vst3/au plugin support.
- **in native c++** (openframeworks, cinder): high performance, but long compilation cycles that break creative flow on every small aesthetic tweak.

with [infinite](https://infinitedevices.org), we bridged these worlds by embedding a high-performance javascript runtime directly into a native c++ modular daw.

### 1. the p5-style draw loop

inside any `sketch` node, you write clean, declarative javascript that hot-reloads on every keystroke without compiling or restarting:

```javascript
// declare wireable parameters exposed on the node canvas
param("points", 8000, 1000, 20000);
param("drift", 0.15, 0, 1);
param("size", 1.5, 0.5, 4);

function draw(t) {
  background(0.04, 0.04, 0.07);
  const a = -1.4 + 0.25 * sin(t * drift);
  const b = 1.6 + 0.2 * cos(t * drift * 0.8);
  const c = 1.0 + 0.2 * sin(t * drift * 0.6);
  const d = 0.7;
  
  let x = 0.1, y = 0.1;
  const k = min(width, height) * 0.22;
  noStroke();
  
  for (let i = 0; i < points; i++) {
    const nx = sin(a * y) + c * cos(a * x);
    const ny = sin(b * x) + d * cos(b * y);
    x = nx; y = ny;
    if (i > 20) {
      fill(hsl(0.55 + 0.3 * (i / points), 0.7, 0.65, 0.4));
      circle(width / 2 + x * k, height / 2 + y * k, size);
    }
  }
}
```

### 2. wireable parameters: code meets modular graph

the `param(...)` calls do something unique: they dynamically create real, visual input pins on the node canvas. you can connect:
- a midi knob to modulate `drift`.
- an 8-band audio fft peak to pulse `size`.
- a low-frequency oscillator (lfo) to rotate the attractor phase.

![Infinite Main Canvas](https://raw.githubusercontent.com/n1m21n/infinite/main/press_kit/screenshots/01_infinite_main_canvas.png)

### 3. native rendering performance

unlike web wrappers, the generated vector drawing commands stream directly into hardware-accelerated opengl framebuffers. you can render tens of thousands of points per frame with steady 60fps pacing while multi-track 48khz audio synths run in parallel.

- download infinite free (macos, windows, linux): https://infinitedevices.org
- source code on github: https://github.com/n1m21n/infinite
