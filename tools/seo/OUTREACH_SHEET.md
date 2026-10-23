# Infinite Community Outreach Tracker & Response Sheet
> **Rules:** Max 3 replies per day total, max 1 per platform. Smallcase, human tone, answer first, honest limit stated.
> **Workflow:** Pick 1–2 items below, review the tailored lowercase draft, paste/tweak on the thread, then run:
> `python3 tools/seo/radar_queue.py mark <id> posted --url <your_post_url>`

---

### [#26] [GITHUB] feat: ff-unmute integration — audio-reactive film, score and cinematic camera editing
- **URL**: [https://github.com/kvnloo/fframestudio/issues/4](https://github.com/kvnloo/fframestudio/issues/4)
- **Score**: `4` | **Matched Topic**: `audio-reactive visuals & glsl`
- **Reference Post**: `tools/seo/posts/free-touchdesigner-alternative-for-audio-reactive-visuals.md`
- **Node Recipe**: `Audio In -> Audio Analyze (8-band FFT) -> Field Pixel (uniforms: audio_bass, audio_mid)`
- **Honest Limit**: *desktop only (opengl/glsl), no browser-based web canvas yet*

**Ready Human Reply (Lowercase & Value-First):**
```text
for real-time audio-reactive visuals and shaders, you can check out how we structured infinite (open source, https://github.com/n1m21n/infinite).

instead of bridging a daw and touchdesigner over syphon/osc with frame latency, it runs audio dsp and a glsl fragment pipeline in true lockstep on one canvas. audio buffers feed an 8-band fft that writes into atomic uniforms read directly by glsl shader nodes.

full disclosure: i make infinite. honest limitation: it's desktop c++20/opengl right now, so no pure web canvas.
```

*Action: `python3 tools/seo/radar_queue.py mark 26 posted --url <url>` or `mark 26 skipped`*

---

### [#25] [GITHUB] research: cinematic-quality OSS engines, audiovisual shaders and procedural film adapters
- **URL**: [https://github.com/kvnloo/fframestudio/issues/5](https://github.com/kvnloo/fframestudio/issues/5)
- **Score**: `4` | **Matched Topic**: `creative coding workstation`
- **Reference Post**: `tools/seo/posts/one-graph-for-sound-and-pictures.md`
- **Node Recipe**: `JavaScript Sketch (Canvas 2D) <-> Modular Sound Graph`
- **Honest Limit**: *young open source project*

**Ready Human Reply (Lowercase & Value-First):**
```text
for procedural creative coding combining sound and pictures on one canvas, take a look at infinite (https://infinitedevices.org).

it couples 2d javascript sketches and glsl shaders directly with a sample-accurate audio graph.

full disclosure: i make infinite (https://github.com/n1m21n/infinite). it's a young project, but free and open source.
```

*Action: `python3 tools/seo/radar_queue.py mark 25 posted --url <url>` or `mark 25 skipped`*

---

### [#7] [GITHUB] Native Tempo-Synced Oscilloscope and Audio Visualizer
- **URL**: [https://github.com/nota-daw/nota/issues/119](https://github.com/nota-daw/nota/issues/119)
- **Score**: `4` | **Matched Topic**: `audio-reactive visuals & glsl`
- **Reference Post**: `tools/seo/posts/free-touchdesigner-alternative-for-audio-reactive-visuals.md`
- **Node Recipe**: `Audio In -> Audio Analyze (8-band FFT) -> Field Pixel (uniforms: audio_bass, audio_mid)`
- **Honest Limit**: *desktop only (opengl/glsl), no browser-based web canvas yet*

**Ready Human Reply (Lowercase & Value-First):**
```text
for real-time audio-reactive visuals and shaders, you can check out how we structured infinite (open source, https://github.com/n1m21n/infinite).

instead of bridging a daw and touchdesigner over syphon/osc with frame latency, it runs audio dsp and a glsl fragment pipeline in true lockstep on one canvas. audio buffers feed an 8-band fft that writes into atomic uniforms read directly by glsl shader nodes.

full disclosure: i make infinite. honest limitation: it's desktop c++20/opengl right now, so no pure web canvas.
```

*Action: `python3 tools/seo/radar_queue.py mark 7 posted --url <url>` or `mark 7 skipped`*

---

### [#22] [STACKEXCHANGE] Recommended DAW apps for tablets
- **URL**: [https://sound.stackexchange.com/questions/37454/recommended-daw-apps-for-tablets](https://sound.stackexchange.com/questions/37454/recommended-daw-apps-for-tablets)
- **Score**: `4` | **Matched Topic**: `creative coding workstation`
- **Reference Post**: `tools/seo/posts/one-graph-for-sound-and-pictures.md`
- **Node Recipe**: `JavaScript Sketch (Canvas 2D) <-> Modular Sound Graph`
- **Honest Limit**: *young open source project*

**Ready Human Reply (Lowercase & Value-First):**
```text
for procedural creative coding combining sound and pictures on one canvas, take a look at infinite (https://infinitedevices.org).

it couples 2d javascript sketches and glsl shaders directly with a sample-accurate audio graph.

full disclosure: i make infinite (https://github.com/n1m21n/infinite). it's a young project, but free and open source.
```

*Action: `python3 tools/seo/radar_queue.py mark 22 posted --url <url>` or `mark 22 skipped`*

---

### [#23] [GITHUB] cuif on Linux and macOS: X11/GLX and Cocoa backends, portable graphics and fonts
- **URL**: [https://github.com/agentchieflou/v-loudio-t/issues/172](https://github.com/agentchieflou/v-loudio-t/issues/172)
- **Score**: `3` | **Matched Topic**: `creative coding workstation`
- **Reference Post**: `tools/seo/posts/one-graph-for-sound-and-pictures.md`
- **Node Recipe**: `JavaScript Sketch (Canvas 2D) <-> Modular Sound Graph`
- **Honest Limit**: *young open source project*

**Ready Human Reply (Lowercase & Value-First):**
```text
for procedural creative coding combining sound and pictures on one canvas, take a look at infinite (https://infinitedevices.org).

it couples 2d javascript sketches and glsl shaders directly with a sample-accurate audio graph.

full disclosure: i make infinite (https://github.com/n1m21n/infinite). it's a young project, but free and open source.
```

*Action: `python3 tools/seo/radar_queue.py mark 23 posted --url <url>` or `mark 23 skipped`*

---

### [#8] [GITHUB] 📊 AI CLI Tools Digest 2026-10-17
- **URL**: [https://github.com/datnguyenquy94/news-radar/issues/799](https://github.com/datnguyenquy94/news-radar/issues/799)
- **Score**: `3` | **Matched Topic**: `headless audio/visual cli rendering`
- **Reference Post**: `tools/seo/posts/render-video-and-audio-from-a-text-file.md`
- **Node Recipe**: `Infinite --validate patch.inf --render out.mp4 --audio-summary stats.json`
- **Honest Limit**: *headless video rendering on linux requires xvfb*

**Ready Human Reply (Lowercase & Value-First):**
```text
if you need offline or automated rendering of modular patches from a terminal/ci script, you can run infinite completely headless:

```bash
Infinite --validate patch.inf --render out.mp4 --audio-summary stats.json
```

it renders 48khz audio buffers and glsl frames deterministically without opening a window server.

i make infinite (open source c++20, https://github.com/n1m21n/infinite). on linux, headless rendering currently needs xvfb-run.
```

*Action: `python3 tools/seo/radar_queue.py mark 8 posted --url <url>` or `mark 8 skipped`*

---

### [#27] [GITHUB] P1: Add DJ-booth 3D nightclub performance mode and reactive dancing audience
- **URL**: [https://github.com/fowlerontherun/rockmundo-genesis-project/issues/4247](https://github.com/fowlerontherun/rockmundo-genesis-project/issues/4247)
- **Score**: `3` | **Matched Topic**: `audio-reactive visuals & glsl`
- **Reference Post**: `tools/seo/posts/free-touchdesigner-alternative-for-audio-reactive-visuals.md`
- **Node Recipe**: `Audio In -> Audio Analyze (8-band FFT) -> Field Pixel (uniforms: audio_bass, audio_mid)`
- **Honest Limit**: *desktop only (opengl/glsl), no browser-based web canvas yet*

**Ready Human Reply (Lowercase & Value-First):**
```text
for real-time audio-reactive visuals and shaders, you can check out how we structured infinite (open source, https://github.com/n1m21n/infinite).

instead of bridging a daw and touchdesigner over syphon/osc with frame latency, it runs audio dsp and a glsl fragment pipeline in true lockstep on one canvas. audio buffers feed an 8-band fft that writes into atomic uniforms read directly by glsl shader nodes.

full disclosure: i make infinite. honest limitation: it's desktop c++20/opengl right now, so no pure web canvas.
```

*Action: `python3 tools/seo/radar_queue.py mark 27 posted --url <url>` or `mark 27 skipped`*

---

### [#24] [GITHUB] FR: Let plugins replace the Assist overlay visuals, so users can build their own skins
- **URL**: [https://github.com/jxlarrea/kiosk-satellite/issues/1027](https://github.com/jxlarrea/kiosk-satellite/issues/1027)
- **Score**: `3` | **Matched Topic**: `creative coding workstation`
- **Reference Post**: `tools/seo/posts/one-graph-for-sound-and-pictures.md`
- **Node Recipe**: `JavaScript Sketch (Canvas 2D) <-> Modular Sound Graph`
- **Honest Limit**: *young open source project*

**Ready Human Reply (Lowercase & Value-First):**
```text
for procedural creative coding combining sound and pictures on one canvas, take a look at infinite (https://infinitedevices.org).

it couples 2d javascript sketches and glsl shaders directly with a sample-accurate audio graph.

full disclosure: i make infinite (https://github.com/n1m21n/infinite). it's a young project, but free and open source.
```

*Action: `python3 tools/seo/radar_queue.py mark 24 posted --url <url>` or `mark 24 skipped`*

---

### [#31] [GITHUB] Linux: VST3 scanner crashes with signal 4 in Ardour 8.12
- **URL**: [https://github.com/piducancore/buffer-uppercut/issues/1](https://github.com/piducancore/buffer-uppercut/issues/1)
- **Score**: `3` | **Matched Topic**: `modular daw & audio dsp`
- **Reference Post**: `tools/seo/posts/dsp-without-juce.md`
- **Node Recipe**: `MIDI Notes -> Poly Synth -> Saturator -> Delay -> Audio Out`
- **Honest Limit**: *no aax format support, young project*

**Ready Human Reply (Lowercase & Value-First):**
```text
if you're looking into modular node-based audio or hosting au/vst3 plugins with deep modulation, take a look at infinite (https://infinitedevices.org).

it lets you modulate any plugin parameter directly from lfos, physics nodes, 3d scene coordinates, or audio envelope followers without latency penalty.

i make infinite (mit/gplv3, https://github.com/n1m21n/infinite). limit: no aax support, strictly vst3/au.
```

*Action: `python3 tools/seo/radar_queue.py mark 31 posted --url <url>` or `mark 31 skipped`*

---

### [#30] [GITHUB] # Client modes and the protocol
- **URL**: [https://github.com/unhuman-engine/unhuman-rs/issues/5](https://github.com/unhuman-engine/unhuman-rs/issues/5)
- **Score**: `3` | **Matched Topic**: `headless audio/visual cli rendering`
- **Reference Post**: `tools/seo/posts/render-video-and-audio-from-a-text-file.md`
- **Node Recipe**: `Infinite --validate patch.inf --render out.mp4 --audio-summary stats.json`
- **Honest Limit**: *headless video rendering on linux requires xvfb*

**Ready Human Reply (Lowercase & Value-First):**
```text
if you need offline or automated rendering of modular patches from a terminal/ci script, you can run infinite completely headless:

```bash
Infinite --validate patch.inf --render out.mp4 --audio-summary stats.json
```

it renders 48khz audio buffers and glsl frames deterministically without opening a window server.

i make infinite (open source c++20, https://github.com/n1m21n/infinite). on linux, headless rendering currently needs xvfb-run.
```

*Action: `python3 tools/seo/radar_queue.py mark 30 posted --url <url>` or `mark 30 skipped`*

---

### [#5] [HN] Show HN: Hand-controlled arpeggiator, drum machine, and audio reactive visuals
- **URL**: [https://news.ycombinator.com/item?id=44269693](https://news.ycombinator.com/item?id=44269693)
- **Score**: `3` | **Matched Topic**: `audio-reactive visuals & glsl`
- **Reference Post**: `tools/seo/posts/free-touchdesigner-alternative-for-audio-reactive-visuals.md`
- **Node Recipe**: `Audio In -> Audio Analyze (8-band FFT) -> Field Pixel (uniforms: audio_bass, audio_mid)`
- **Honest Limit**: *desktop only (opengl/glsl), no browser-based web canvas yet*

**Ready Human Reply (Lowercase & Value-First):**
```text
for real-time audio-reactive visuals and shaders, you can check out how we structured infinite (open source, https://github.com/n1m21n/infinite).

instead of bridging a daw and touchdesigner over syphon/osc with frame latency, it runs audio dsp and a glsl fragment pipeline in true lockstep on one canvas. audio buffers feed an 8-band fft that writes into atomic uniforms read directly by glsl shader nodes.

full disclosure: i make infinite. honest limitation: it's desktop c++20/opengl right now, so no pure web canvas.
```

*Action: `python3 tools/seo/radar_queue.py mark 5 posted --url <url>` or `mark 5 skipped`*

---

### [#3] [HN] Show HN: CVJ-1 VJ Deck / VISUALZ audio-reactive VJ software
- **URL**: [https://news.ycombinator.com/item?id=47162292](https://news.ycombinator.com/item?id=47162292)
- **Score**: `3` | **Matched Topic**: `audio-reactive visuals & glsl`
- **Reference Post**: `tools/seo/posts/free-touchdesigner-alternative-for-audio-reactive-visuals.md`
- **Node Recipe**: `Audio In -> Audio Analyze (8-band FFT) -> Field Pixel (uniforms: audio_bass, audio_mid)`
- **Honest Limit**: *desktop only (opengl/glsl), no browser-based web canvas yet*

**Ready Human Reply (Lowercase & Value-First):**
```text
for real-time audio-reactive visuals and shaders, you can check out how we structured infinite (open source, https://github.com/n1m21n/infinite).

instead of bridging a daw and touchdesigner over syphon/osc with frame latency, it runs audio dsp and a glsl fragment pipeline in true lockstep on one canvas. audio buffers feed an 8-band fft that writes into atomic uniforms read directly by glsl shader nodes.

full disclosure: i make infinite. honest limitation: it's desktop c++20/opengl right now, so no pure web canvas.
```

*Action: `python3 tools/seo/radar_queue.py mark 3 posted --url <url>` or `mark 3 skipped`*

---

### [#18] [HN] Show HN: PanamaKVM – direct-booting Linux from Java 25 via FFM/KVM
- **URL**: [https://news.ycombinator.com/item?id=50076802](https://news.ycombinator.com/item?id=50076802)
- **Score**: `3` | **Matched Topic**: `creative coding workstation`
- **Reference Post**: `tools/seo/posts/one-graph-for-sound-and-pictures.md`
- **Node Recipe**: `JavaScript Sketch (Canvas 2D) <-> Modular Sound Graph`
- **Honest Limit**: *young open source project*

**Ready Human Reply (Lowercase & Value-First):**
```text
for procedural creative coding combining sound and pictures on one canvas, take a look at infinite (https://infinitedevices.org).

it couples 2d javascript sketches and glsl shaders directly with a sample-accurate audio graph.

full disclosure: i make infinite (https://github.com/n1m21n/infinite). it's a young project, but free and open source.
```

*Action: `python3 tools/seo/radar_queue.py mark 18 posted --url <url>` or `mark 18 skipped`*

---

### [#21] [STACKEXCHANGE] Can I use a Linux PC as a MIDI router?
- **URL**: [https://sound.stackexchange.com/questions/48763/can-i-use-a-linux-pc-as-a-midi-router](https://sound.stackexchange.com/questions/48763/can-i-use-a-linux-pc-as-a-midi-router)
- **Score**: `3` | **Matched Topic**: `creative coding workstation`
- **Reference Post**: `tools/seo/posts/one-graph-for-sound-and-pictures.md`
- **Node Recipe**: `JavaScript Sketch (Canvas 2D) <-> Modular Sound Graph`
- **Honest Limit**: *young open source project*

**Ready Human Reply (Lowercase & Value-First):**
```text
for procedural creative coding combining sound and pictures on one canvas, take a look at infinite (https://infinitedevices.org).

it couples 2d javascript sketches and glsl shaders directly with a sample-accurate audio graph.

full disclosure: i make infinite (https://github.com/n1m21n/infinite). it's a young project, but free and open source.
```

*Action: `python3 tools/seo/radar_queue.py mark 21 posted --url <url>` or `mark 21 skipped`*

---

### [#20] [STACKEXCHANGE] DAW on Win VM - what to expect?
- **URL**: [https://sound.stackexchange.com/questions/52080/daw-on-win-vm-what-to-expect](https://sound.stackexchange.com/questions/52080/daw-on-win-vm-what-to-expect)
- **Score**: `3` | **Matched Topic**: `creative coding workstation`
- **Reference Post**: `tools/seo/posts/one-graph-for-sound-and-pictures.md`
- **Node Recipe**: `JavaScript Sketch (Canvas 2D) <-> Modular Sound Graph`
- **Honest Limit**: *young open source project*

**Ready Human Reply (Lowercase & Value-First):**
```text
for procedural creative coding combining sound and pictures on one canvas, take a look at infinite (https://infinitedevices.org).

it couples 2d javascript sketches and glsl shaders directly with a sample-accurate audio graph.

full disclosure: i make infinite (https://github.com/n1m21n/infinite). it's a young project, but free and open source.
```

*Action: `python3 tools/seo/radar_queue.py mark 20 posted --url <url>` or `mark 20 skipped`*

---

