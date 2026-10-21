#!/usr/bin/env python3
"""DEPRECATED: use tools/seo/publish.py. Its canonical_url points at the homepage, not the original post, which weakens the original.

infinite technical article syndication agent.

what: prepares and publishes deep technical engineering articles about infinite's
      architecture, dual dag audio/visual synchronization, and field kernel language
      to developer platforms like dev.to and hashnode via official apis.
why:  in-depth technical articles about c++, glsl, and dsp rank on search engines,
      generate high developer credibility, and drive consistent inbound traffic
      to https://infinitedevices.org.

usage:
    # 1. export articles to local markdown files
    python3 tools/spread/syndicate.py --export

    # 2. publish directly to dev.to (requires DEVTO_API_KEY environment variable)
    python3 tools/spread/syndicate.py --devto --article audio-visual-dag

    # 3. publish directly to hashnode (requires HASHNODE_API_KEY and HASHNODE_PUBLICATION_ID)
    python3 tools/spread/syndicate.py --hashnode --article field-language
"""

import argparse
import json
import os
import sys
import urllib.request

ARTICLES = {
    "audio-visual-dag": {
        "title": "building a dual synchronized dag engine: audio dsp and gpu shaders in lockstep",
        "description": "how infinite synchronizes a sample-accurate, pull-based audio graph with a real-time opengl texture pipeline in c++.",
        "tags": ["cpp", "opengl", "dsp", "opensource"],
        "canonical_url": "https://infinitedevices.org",
        "content": """
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
""".strip()
    },

    "field-language": {
        "title": "designing a domain-inferred kernel language for shaders, javascript sketches, and audio dsp",
        "description": "how infinite infers execution backends (glsl, javascript 2d sketches, or sample-level dsp) from context.",
        "tags": ["programming", "compilers", "gamedev", "creativecoding"],
        "canonical_url": "https://infinitedevices.org",
        "content": """
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
""".strip()
    },

    "headless-daw": {
        "title": "building a headless daw: synthesizing 48khz modular audio and gpu visuals via cli",
        "description": "how infinite runs full node graphs headlessly to synthesize 48khz audio, analyze lufs, and render glsl frames without a window server.",
        "tags": ["audio", "cpp", "automation", "discord"],
        "canonical_url": "https://infinitedevices.org",
        "content": """
![Infinite Main Workstation UI](https://raw.githubusercontent.com/n1m21n/infinite/main/website/assets/screenshot.png)

most digital audio workstations are tightly coupled to their user interfaces. attempting to run ableton live, fl studio, or logic pro from a terminal or continuous integration pipeline either requires complex virtual display wrappers (xvfb) or is outright impossible.

when building [infinite](https://infinitedevices.org), we made the engine fully decoupled from the opengl ui. with the headless cli flag, you can validate patch graphs, synthesize multi-minute 48khz audio files, compute broadcast loudness metrics (integrated lufs, true peak), and render video frames in milliseconds directly from bash.

### 1. why headless audio synthesis matters

offline headless rendering opens up workflows that desktop-only daws cannot touch:
- **automated discord/telegram bots**: community members can type `!inf make fm` or upload a patch file and receive an mp3 export + visual snapshot in seconds.
- **ci/cd regression testing**: verify that dsp algorithms never clip, introduce dc offset, or change timbre across git commits.
- **cloud patch generation**: procedural sound design systems that output `.inf` patches and synthesize stems on demand.

### 2. sample-accurate execution in sub-200ms

because infinite's audio engine uses zero dynamic allocations in the block processing loop, offline rendering runs as fast as cpu cache lines permit:

```bash
Infinite --audio-summary patch.inf summary.json --wav out.wav --duration 4
```

in less than 200ms, the engine instantiates the node graph (oscillators, envelopes, filters, and delays), evaluates 192,000 frames at 48khz, and produces a complete json analysis report:

```json
{
  "ok": true,
  "audio": { "sample_rate": 48000, "frames": 192000 },
  "audio_summary": {
    "integrated_lufs": -9.79,
    "sample_peak_dbfs": -1.65,
    "bpm_estimate": 96.2,
    "clipped_frames": 0
  }
}
```

![Infinite Modular Canvas](https://raw.githubusercontent.com/n1m21n/infinite/main/press_kit/screenshots/01_infinite_main_canvas.png)

### 3. try it out

infinite is free, open source, and available for macos, windows, and linux.

- website & web embeds: https://infinitedevices.org
- github repository: https://github.com/n1m21n/infinite
""".strip()
    },

    "javascript-sketches": {
        "title": "creative coding with embedded javascript: running p5-style algorithmic art inside a native c++ daw",
        "description": "how infinite embeds a native javascript engine to execute procedural sketches, strange attractors, and cellular automata alongside synchronized audio.",
        "tags": ["javascript", "creativecoding", "gamedev", "webdev"],
        "canonical_url": "https://infinitedevices.org",
        "content": """
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
""".strip()
    },

    "dsp-without-juce": {
        "title": "writing sample-accurate audio dsp without juce in modern c++",
        "description": "architecting a real-time, lock-free audio engine and multi-format plugin host in pure modern c++ with zero audio-thread allocations.",
        "tags": ["cpp", "audio", "programming", "performance"],
        "canonical_url": "https://infinitedevices.org",
        "content": """
![Infinite Audio Graph](https://raw.githubusercontent.com/n1m21n/infinite/main/website/assets/nodegraph.png)

juce is the de facto standard for audio software development, but it brings substantial architectural gravity: a massive monolithic codebase, custom string classes, complex licensing, and heavy abstractions.

when building [infinite](https://infinitedevices.org), an open-source modular workstation, we chose to write the entire pull-based audio engine in modern standard c++ (c++20) from scratch.

here is what we learned and how the architecture is designed.

### 1. the golden rule: zero allocations on the audio thread

any audio callback deadline at 48khz with a 128-sample buffer is strict: you have exactly 2.66 milliseconds to finish processing before the dac underflows and creates an audible click.

to guarantee hard real-time safety:
- **zero `malloc` / `free`**: all audio buffers, voice allocations, and delay memory are pre-allocated during `preparetoplay()`.
- **zero mutexes**: cross-thread communication between the ui thread and audio thread uses atomic pointers, lock-free single-producer single-consumer (spsc) ring buffers, and double-buffered modulation arrays.
- **cache-line aligned processing**: audio frames are laid out contiguously in memory so SIMD instructions can vectorize filter stages without cache stalls.

### 2. native au and vst3 plugin hosting

instead of relying on framework wrappers, infinite speaks directly to plugin apis:
- **vst3**: loads the component bundle, queries `IComponent` and `IAudioProcessor`, maps parameters to continuous modulation arrays, and schedules sample-accurate note events.
- **audio unit (au)**: interacts directly with the coreaudio audio unit v2/v3 c-api on macos.

parameters on third-party plugins can be modulated in real time by any signal on the canvas—whether an audio lfo, an envelope follower, or a generative javascript sketch.

![Infinite Modular Canvas](https://raw.githubusercontent.com/n1m21n/infinite/main/press_kit/screenshots/01_infinite_main_canvas.png)

### 3. embedded dsp kernel language

to let users write custom filters and physical modeling synths without recompiling c++, we created **field**, an embedded dsp language. a 1-pole filter in field is 4 lines:

```
param float cutoff = 0.25 [0.01, 0.99]
state float z = 0
z += (in - z) * cutoff
out = z
```

the compiler translates this directly into optimized vector loops evaluated per-sample on the audio thread.

- try infinite: https://infinitedevices.org
- read the codebase: https://github.com/n1m21n/infinite
""".strip()
    }
}


def export_articles(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    for slug, art in ARTICLES.items():
        path = os.path.join(out_dir, f"{slug}.md")
        with open(path, "w") as f:
            f.write(f"# {art['title']}\n\n")
            f.write(f"> {art['description']}\n\n")
            f.write(art['content'] + "\n")
        print(f"exported article: {path}")


def publish_devto(article_key, api_key):
    art = ARTICLES.get(article_key)
    if not art:
        print(f"article '{article_key}' not found.")
        return

    payload = {
        "article": {
            "title": art["title"],
            "published": True,
            "body_markdown": art["content"],
            "tags": art["tags"],
            "canonical_url": art["canonical_url"],
            "description": art["description"],
        }
    }

    req = urllib.request.Request(
        "https://dev.to/api/articles",
        data=json.dumps(payload).encode("utf-8"),
        headers={
            "Content-Type": "application/json",
            "api-key": api_key,
            "User-Agent": "InfiniteSyndicate/1.0"
        }
    )

    try:
        with urllib.request.urlopen(req) as resp:
            data = json.loads(resp.read().decode("utf-8"))
            print(f"published to dev.to: {data.get('url')}")
    except Exception as e:
        print(f"dev.to publication error: {e}")


def main():
    parser = argparse.ArgumentParser(description="infinite technical article syndication agent")
    parser.add_argument("--export", action="store_true", help="export articles to markdown files")
    parser.add_argument("--out-dir", default="tools/spread/articles", help="export directory")
    parser.add_argument("--devto", action="store_true", help="publish to dev.to via api")
    parser.add_argument("--article", choices=list(ARTICLES.keys()), help="article key to publish")

    args = parser.parse_args()

    if args.export:
        export_articles(args.out_dir)
    elif args.devto:
        api_key = os.getenv("DEVTO_API_KEY")
        if not api_key:
            print("please set DEVTO_API_KEY environment variable.")
            sys.exit(1)
        if not args.article:
            print("please specify --article <key>")
            sys.exit(1)
        publish_devto(args.article, api_key)
    else:
        export_articles(args.out_dir)


if __name__ == "__main__":
    main()
