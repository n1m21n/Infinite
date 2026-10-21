# building a dual synchronized dag engine: audio dsp and gpu shaders in lockstep

> how infinite synchronizes a sample-accurate, pull-based audio graph with a real-time opengl texture pipeline in c++.

![Infinite Modular Workstation](https://raw.githubusercontent.com/n1m21n/infinite/main/press_kit/screenshots/01_infinite_main_canvas.png)

in real-time multimedia systems, video and audio are typically managed in separate universes. visual tools like touchdesigner or hydra excel at high-throughput gpu textures, while digital audio workstations like ableton live or bitwig focus on deterministic, sample-accurate audio buffers. when artists connect them via osc, midi clock, or syphon, latency and jitter inevitably creep in.

with [infinite](https://infinitedevices.org), we built a dual synchronized dag (directed acyclic graph) engine in modern c++ that runs audio and visuals in true lockstep on one infinite canvas.

### 1. the architectural challenge: pull vs. push

audio and video operate under fundamentally conflicting constraints:
- **audio dsp** is pull-based, real-time, and strictly deadline-driven. if a 128-sample buffer at 48khz is not ready in 2.6ms, the dac underflows and creates audible clicks.
- **gpu texture pipelines** are frame-based (typically 60 to 120 fps) and throughput-driven. a dropped frame is barely noticed, but high memory bandwidth and low shader compilation stalls are essential.

![Audio and Visual Graph Synchronization](https://raw.githubusercontent.com/n1m21n/infinite/main/website/assets/nodegraph.png)

### 2. lockstep synchronization via universal modulation

to bridge these two domains without latency penalties:
1. the master transport clock coordinates tick-accurate time across both sub-graphs.
2. audio buffers produce real-time 8-band fft analysis and onset peaks that write into atomic double-buffered modulation arrays.
3. shader uniforms (glsl) read directly from these modulation arrays during the render pass, eliminating frame-delay buffering.
4. hosted au and vst3 plugin parameters can be modulated directly by 3d scene coordinates, javascript sketches, or lfo generators.

### 3. open source and cross-platform

infinite is open source (mit/gplv3) and runs cross-platform on macos, windows, and linux.

- website: https://infinitedevices.org
- source code: https://github.com/n1m21n/infinite
