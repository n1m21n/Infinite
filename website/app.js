/**
 * Infinite — Web Application Logic, Interactive Audio & Visual Engines
 */

// ==========================================================================
// 1. Interactive Showcase Tab Switcher
// ==========================================================================
function initShowcaseSwitcher() {
  const tabs = document.querySelectorAll('.showcase-tab');
  const mediaPanels = document.querySelectorAll('.showcase-media');
  const titleEl = document.getElementById('showcase-title');

  const titles = {
    'media-daw': 'Infinite — Live Modular Graph & Audio-Visual Engine',
    'media-visual': 'Infinite — Live Real-Time Generative Shader Visuals',
    'media-motion': 'Infinite — Procedural 3D Physics, Soft-Body & Instancing',
    'media-graph': 'Infinite — High-Resolution Pull-Based Node DAG'
  };

  tabs.forEach(tab => {
    tab.addEventListener('click', () => {
      const targetId = tab.dataset.target;

      tabs.forEach(t => t.classList.remove('active'));
      mediaPanels.forEach(p => p.classList.remove('active'));

      tab.classList.add('active');
      const targetPanel = document.getElementById(targetId);
      if (targetPanel) {
        targetPanel.classList.add('active');
        const video = targetPanel.querySelector('video');
        if (video) video.play();
      }

      if (titleEl && titles[targetId]) {
        titleEl.textContent = titles[targetId];
      }
    });
  });
}

// ==========================================================================
// 2. Interactive In-Browser Web Audio Synthesizer & Oscilloscope
// ==========================================================================
let audioCtx = null;
let osc = null;
let filter = null;
let lfo = null;
let lfoGain = null;
let analyser = null;
let isAudioPlaying = false;
let currentWave = 'sawtooth';

const synthCanvas = document.getElementById('synth-scope-canvas');
const synthCtx = synthCanvas ? synthCanvas.getContext('2d') : null;
const synthToggleBtn = document.getElementById('synth-toggle-btn');
const synthStatusText = document.getElementById('synth-status-text');
const cutoffSlider = document.getElementById('synth-filter-slider');
const cutoffVal = document.getElementById('cutoff-val');
const lfoSlider = document.getElementById('synth-lfo-slider');
const lfoVal = document.getElementById('lfo-val');
const waveBtns = document.querySelectorAll('.wave-btn');

function initSynthAudio() {
  if (!audioCtx) {
    audioCtx = new (window.AudioContext || window.webkitAudioContext)();
  }

  if (audioCtx.state === 'suspended') {
    audioCtx.resume();
  }

  // Create Synth Graph: Osc -> Filter -> Analyser -> Master Gain -> Out
  osc = audioCtx.createOscillator();
  osc.type = currentWave;
  osc.frequency.setValueAtTime(110, audioCtx.currentTime); // A2 fundamental

  // Second detuned osc for warmth
  const subOsc = audioCtx.createOscillator();
  subOsc.type = 'sine';
  subOsc.frequency.setValueAtTime(55, audioCtx.currentTime); // A1 sub

  filter = audioCtx.createBiquadFilter();
  filter.type = 'lowpass';
  filter.frequency.setValueAtTime(parseFloat(cutoffSlider.value), audioCtx.currentTime);
  filter.Q.setValueAtTime(6.0, audioCtx.currentTime);

  // LFO Modulating filter cutoff
  lfo = audioCtx.createOscillator();
  lfo.frequency.setValueAtTime(parseFloat(lfoSlider.value), audioCtx.currentTime);
  lfoGain = audioCtx.createGain();
  lfoGain.gain.setValueAtTime(400, audioCtx.currentTime);

  lfo.connect(lfoGain);
  lfoGain.connect(filter.frequency);

  analyser = audioCtx.createAnalyser();
  analyser.fftSize = 1024;

  const masterGain = audioCtx.createGain();
  masterGain.gain.setValueAtTime(0.18, audioCtx.currentTime);

  osc.connect(filter);
  subOsc.connect(filter);
  filter.connect(analyser);
  analyser.connect(masterGain);
  masterGain.connect(audioCtx.destination);

  osc.start();
  subOsc.start();
  lfo.start();

  isAudioPlaying = true;
  if (synthStatusText) synthStatusText.style.display = 'none';
  if (synthToggleBtn) {
    synthToggleBtn.innerHTML = `
      <svg viewBox="0 0 24 24" width="16" height="16" fill="currentColor"><rect x="6" y="4" width="4" height="16"></rect><rect x="14" y="4" width="4" height="16"></rect></svg>
      <span>Mute Audio Demo</span>
    `;
    synthToggleBtn.classList.remove('btn-primary');
    synthToggleBtn.classList.add('btn-secondary');
  }

  renderOscilloscope();
}

function stopSynthAudio() {
  if (audioCtx) {
    audioCtx.close();
    audioCtx = null;
  }
  isAudioPlaying = false;
  if (synthStatusText) {
    synthStatusText.textContent = 'Audio muted. Click to activate Web DSP.';
    synthStatusText.style.display = 'block';
  }
  if (synthToggleBtn) {
    synthToggleBtn.innerHTML = `
      <svg viewBox="0 0 24 24" width="16" height="16" fill="currentColor"><polygon points="5 3 19 12 5 21 5 3"></polygon></svg>
      <span>Play Live Audio Demo</span>
    `;
    synthToggleBtn.classList.remove('btn-secondary');
    synthToggleBtn.classList.add('btn-primary');
  }
}

if (synthToggleBtn) {
  synthToggleBtn.addEventListener('click', () => {
    if (isAudioPlaying) {
      stopSynthAudio();
    } else {
      initSynthAudio();
    }
  });
}

if (cutoffSlider) {
  cutoffSlider.addEventListener('input', (e) => {
    const val = parseFloat(e.target.value);
    if (cutoffVal) cutoffVal.textContent = val;
    if (filter && audioCtx) {
      filter.frequency.setTargetAtTime(val, audioCtx.currentTime, 0.05);
    }
  });
}

if (lfoSlider) {
  lfoSlider.addEventListener('input', (e) => {
    const val = parseFloat(e.target.value);
    if (lfoVal) lfoVal.textContent = `${val} Hz`;
    if (lfo && audioCtx) {
      lfo.frequency.setTargetAtTime(val, audioCtx.currentTime, 0.05);
    }
  });
}

waveBtns.forEach(btn => {
  btn.addEventListener('click', () => {
    waveBtns.forEach(b => b.classList.remove('active'));
    btn.classList.add('active');
    currentWave = btn.dataset.wave;
    if (osc) osc.type = currentWave;
  });
});

function renderOscilloscope() {
  if (!synthCanvas || !synthCtx) return;

  synthCanvas.width = synthCanvas.clientWidth;
  synthCanvas.height = synthCanvas.clientHeight;

  const w = synthCanvas.width;
  const h = synthCanvas.height;

  function draw() {
    if (!isAudioPlaying || !analyser) {
      // Idle grid
      synthCtx.fillStyle = '#020408';
      synthCtx.fillRect(0, 0, w, h);
      synthCtx.strokeStyle = 'rgba(255, 255, 255, 0.06)';
      synthCtx.lineWidth = 1;
      synthCtx.beginPath();
      synthCtx.moveTo(0, h / 2);
      synthCtx.lineTo(w, h / 2);
      synthCtx.stroke();
      return;
    }

    requestAnimationFrame(draw);

    const bufferLength = analyser.frequencyBinCount;
    const timeData = new Uint8Array(bufferLength);
    analyser.getByteTimeDomainData(timeData);

    synthCtx.fillStyle = '#020408';
    synthCtx.fillRect(0, 0, w, h);

    // Subtle background grid
    synthCtx.strokeStyle = 'rgba(0, 242, 254, 0.08)';
    synthCtx.lineWidth = 1;
    for (let y = 0; y < h; y += 30) {
      synthCtx.beginPath();
      synthCtx.moveTo(0, y);
      synthCtx.lineTo(w, y);
      synthCtx.stroke();
    }

    // Oscilloscope wave
    synthCtx.lineWidth = 2;
    synthCtx.strokeStyle = '#00f2fe';
    synthCtx.shadowBlur = 10;
    synthCtx.shadowColor = '#00f2fe';
    synthCtx.beginPath();

    const sliceWidth = w / bufferLength;
    let x = 0;

    for (let i = 0; i < bufferLength; i++) {
      const v = timeData[i] / 128.0;
      const y = (v * h) / 2;

      if (i === 0) {
        synthCtx.moveTo(x, y);
      } else {
        synthCtx.lineTo(x, y);
      }
      x += sliceWidth;
    }

    synthCtx.stroke();
    synthCtx.shadowBlur = 0;
  }

  draw();
}


// ==========================================================================
// 3. Interactive Patch Cable Background Canvas
// ==========================================================================
const bgCanvas = document.getElementById('patch-canvas');
const bgCtx = bgCanvas ? bgCanvas.getContext('2d') : null;
let bgWidth, bgHeight;
let patchNodes = [];
let patchCables = [];

function resizeBgCanvas() {
  if (!bgCanvas) return;
  bgWidth = bgCanvas.width = window.innerWidth;
  bgHeight = bgCanvas.height = window.innerHeight;
  initBgNetwork();
}

function initBgNetwork() {
  patchNodes = [];
  patchCables = [];
  const count = Math.floor(Math.min(bgWidth, 1400) / 110);

  for (let i = 0; i < count; i++) {
    patchNodes.push({
      x: Math.random() * bgWidth,
      y: Math.random() * bgHeight,
      vx: (Math.random() - 0.5) * 0.35,
      vy: (Math.random() - 0.5) * 0.35,
      radius: Math.random() * 2 + 2.5,
      color: ['#00f2fe', '#4facfe', '#f59e0b', '#a855f7', '#10b981', '#f43f5e'][Math.floor(Math.random() * 6)]
    });
  }

  for (let i = 0; i < patchNodes.length; i++) {
    for (let j = i + 1; j < patchNodes.length; j++) {
      const dist = Math.hypot(patchNodes[i].x - patchNodes[j].x, patchNodes[i].y - patchNodes[j].y);
      if (dist < 180 && Math.random() > 0.45) {
        patchCables.push({
          from: patchNodes[i],
          to: patchNodes[j],
          pulse: Math.random(),
          speed: 0.004 + Math.random() * 0.008,
          color: patchNodes[i].color
        });
      }
    }
  }
}

function animateBgCanvas() {
  if (!bgCtx) return;
  bgCtx.clearRect(0, 0, bgWidth, bgHeight);

  patchCables.forEach(c => {
    const dist = Math.hypot(c.from.x - c.to.x, c.from.y - c.to.y);
    const alpha = Math.max(0, 1 - dist / 220) * 0.25;

    if (alpha > 0) {
      bgCtx.beginPath();
      bgCtx.strokeStyle = `rgba(255, 255, 255, ${alpha * 0.35})`;
      bgCtx.lineWidth = 1;
      bgCtx.moveTo(c.from.x, c.from.y);
      bgCtx.lineTo(c.to.x, c.to.y);
      bgCtx.stroke();

      c.pulse = (c.pulse + c.speed) % 1;
      const px = c.from.x + (c.to.x - c.from.x) * c.pulse;
      const py = c.from.y + (c.to.y - c.from.y) * c.pulse;

      bgCtx.beginPath();
      bgCtx.fillStyle = c.color;
      bgCtx.arc(px, py, 2, 0, Math.PI * 2);
      bgCtx.fill();
    }
  });

  patchNodes.forEach(node => {
    node.x += node.vx;
    node.y += node.vy;

    if (node.x < 0 || node.x > bgWidth) node.vx *= -1;
    if (node.y < 0 || node.y > bgHeight) node.vy *= -1;

    bgCtx.beginPath();
    bgCtx.arc(node.x, node.y, node.radius, 0, Math.PI * 2);
    bgCtx.fillStyle = node.color;
    bgCtx.fill();
  });

  requestAnimationFrame(animateBgCanvas);
}


// ==========================================================================
// 4. Comprehensive Infinite Node Database (160+ Nodes)
// ==========================================================================
const NODES_DATABASE = [
  // Source
  { name: 'Image', category: 'source', catLabel: 'Source', desc: 'Static image loader supporting PNG, JPG, EXR, HDR, and TIFF with live aspect ratio fitting.' },
  { name: 'Video', category: 'source', catLabel: 'Source', desc: 'Hardware-accelerated AVFoundation video decoder with loop, scrubbing, speed, and timecode sync.' },
  { name: 'Syphon In', category: 'source', catLabel: 'Source', desc: 'Zero-copy GPU video receiver streaming live frames from OBS, Resolume, or TouchDesigner.' },
  { name: 'Formula', category: 'source', catLabel: 'Source', desc: 'Live GLSL fragment shader editor with 16 presets, syntax errors, and custom uniform inputs.' },
  { name: 'Draw', category: 'source', catLabel: 'Source', desc: 'Paintable canvas with 6 procedural brushes, eraser, pressure support, and transport stroke recording.' },
  { name: 'Noise', category: 'source', catLabel: 'Source', desc: 'Real-time procedural noise: Value, Perlin, Voronoi, Ridged Multi-fractal, Simplex, and White noise.' },
  { name: 'Shape', category: 'source', catLabel: 'Source', desc: 'Signed Distance Field (SDF) 2D primitives: Circle, Box, Polygon, Star, Heart, Ring, Rounded Box.' },
  { name: 'Texture', category: 'source', catLabel: 'Source', desc: 'Procedural patterns including Brick, Magic Texture, Wave, Musgrave, and Voronoi cells.' },
  { name: 'Ramp', category: 'source', catLabel: 'Source', desc: 'Gradient generator with 5 spatial modes: Linear, Radial, Angle, Diamond, and Box.' },
  { name: 'Text', category: 'source', catLabel: 'Source', desc: 'System typography renderer with CoreText/CoreGraphics, kerning, line spacing, and alignment.' },

  // 2D Effects & Color
  { name: 'Remove BG', category: 'effects2d', catLabel: '2D & Color', desc: 'On-device Apple Vision ML neural segmentation — zero latency, private, and instant.' },
  { name: 'Blur', category: 'effects2d', catLabel: '2D & Color', desc: 'Separable Gaussian, Box, Motion, and Radial blur with HDR support and edge clamp.' },
  { name: 'Bloom & Glow', category: 'effects2d', catLabel: '2D & Color', desc: 'Multi-pass thresholded bloom with threshold, knee, anamorphic spread, and diffuse glow.' },
  { name: 'Curves', category: 'effects2d', catLabel: '2D & Color', desc: 'Interactive Photoshop-style cubic spline color curves for Master, RGB, and Luma channels.' },
  { name: 'Color Ramp', category: 'effects2d', catLabel: '2D & Color', desc: 'Up to 32 editable gradient stops with linear, ease, and constant interpolation modes.' },
  { name: 'Blend', category: 'effects2d', catLabel: '2D & Color', desc: '31 compositing blend modes including Multiply, Screen, Overlay, Color Dodge, Soft Light, etc.' },
  { name: 'Layer Stack', category: 'effects2d', catLabel: '2D & Color', desc: '4 reorderable Photoshop-like layers with individual blend modes, opacities, and soloing.' },
  { name: 'Displace & Liquify', category: 'effects2d', catLabel: '2D & Color', desc: 'Texture-driven coordinate warping and interactive mouse push/pull brush liquify.' },
  { name: 'Glitch', category: 'effects2d', catLabel: '2D & Color', desc: '6 glitch modes: RGB Split, Block Mosh, Scanline Jitter, Bit Error, Analog VHS, Wave Tear.' },
  { name: 'Kaleidoscope', category: 'effects2d', catLabel: '2D & Color', desc: 'Radial mirror symmetry with segment count, rotation, pinch, and reflection offsets.' },
  { name: 'LUT 3D', category: 'effects2d', catLabel: '2D & Color', desc: 'Industry-standard .cube 3D lookup table color grading and film emulation.' },
  { name: 'Palette Extraction', category: 'effects2d', catLabel: '2D & Color', desc: 'Oklab color space k-means clustering to extract dominant color palettes from live video.' },

  // 3D Geometry & Physics
  { name: 'Geometry Source', category: 'geometry3d', catLabel: '3D & Physics', desc: 'Procedural 3D primitives: Cube, Sphere, Icosphere, Cylinder, Cone, Torus, Plane, Disc.' },
  { name: 'Model 3D', category: 'geometry3d', catLabel: '3D & Physics', desc: 'Import 3D models via Apple ModelIO: OBJ, PLY, STL, and USD/USDZ formats with materials.' },
  { name: 'Instance on Points', category: 'geometry3d', catLabel: '3D & Physics', desc: 'Scatter tens of thousands of meshes using single-draw-call GPU instanced rendering.' },
  { name: 'Distribute Points', category: 'geometry3d', catLabel: '3D & Physics', desc: 'Poisson disk surface scattering, grid arrays, and mesh vertex deconstruction.' },
  { name: 'Cloth & Soft Body', category: 'geometry3d', catLabel: '3D & Physics', desc: 'Position-Based Dynamics (PBD) soft-body and cloth solver with pin constraints.' },
  { name: 'Ocean Simulation', category: 'geometry3d', catLabel: '3D & Physics', desc: 'Gerstner wave procedural ocean mesh with foam, choppiness, and wind direction.' },
  { name: 'Particle System', category: 'geometry3d', catLabel: '3D & Physics', desc: 'GPU accelerated particle emitter with turbulence fields, gravity, drag, and bounce floor.' },
  { name: 'Render 3D', category: 'geometry3d', catLabel: '3D & Physics', desc: 'Cook-Torrance GGX PBR renderer, 32-bit HDRI environment reflections, ACES tonemapping, 8x MSAA.' },
  { name: 'Camera & Lights', category: 'geometry3d', catLabel: '3D & Physics', desc: 'Orbit & fly cameras with Perspective/Orthographic modes, Directional, Point, and Spot lights.' },
  { name: 'Taubin Smooth & Subdivide', category: 'geometry3d', catLabel: '3D & Physics', desc: 'Volume-preserving mesh smoothing, Loop subdivision, and normals recalculation.' },

  // Synths & Audio DSP
  { name: 'Wavetable Synth', category: 'synths', catLabel: 'Synth & DSP', desc: '12 factory tables, 8 morphable frames, bandlimited mipmaps, unison detune, and sub-oscillator.' },
  { name: 'Granular Synth', category: 'synths', catLabel: 'Synth & DSP', desc: 'Real-time granular texture engine with live playhead scrubbing, grain jitter, spray, and density.' },
  { name: 'Metallic Resonator', category: 'synths', catLabel: 'Synth & DSP', desc: 'Modal physical modeling synthesis simulating bells, metallic plates, tubes, mallets, and bars.' },
  { name: 'PaulStretch', category: 'synths', catLabel: 'Synth & DSP', desc: 'Extreme real-time spectral FFT phase-randomized time-stretcher for ambient soundscapes.' },
  { name: 'Drum Sequencer', category: 'synths', catLabel: 'Synth & DSP', desc: '8-track step sequencer with per-lane sample triggers, swing, velocity accent, and pattern chaining.' },
  { name: 'Sampler', category: 'synths', catLabel: 'Synth & DSP', desc: 'Multi-folder disk sample scanner with pitch tracking, root note detection, and loop points.' },
  { name: 'Plugin Host (AU/VST3)', category: 'synths', catLabel: 'Synth & DSP', desc: 'Host native macOS Audio Unit & VST3 plugins with native UI windows and parameter CV mapping.' },
  { name: 'Parametric EQ', category: 'synths', catLabel: 'Synth & DSP', desc: 'Multi-band parametric equalizer with interactive frequency response visualizer.' },
  { name: 'Reverb & Delay', category: 'synths', catLabel: 'Synth & DSP', desc: 'Algorithmic diffusion reverb and tempo-synced stereo ping-pong delay with filter feedback.' },
  { name: 'Formant Filter', category: 'synths', catLabel: 'Synth & DSP', desc: 'Vocal vowel morphing filter (A-E-I-O-U) with formant frequency modulation.' },

  // Notes & MIDI
  { name: 'MIDI Input', category: 'midi', catLabel: 'Notes & MIDI', desc: 'Live USB/Bluetooth CoreMIDI keyboard input with velocity, pitch bend, and clock sync.' },
  { name: 'Arpeggiator', category: 'midi', catLabel: 'Notes & MIDI', desc: 'Tempo-synced pattern arpeggiator with Up, Down, Random, Ping-Pong, and octave ranges.' },
  { name: 'Bouncing Balls', category: 'midi', catLabel: 'Notes & MIDI', desc: 'Physics-based gravity bouncing polyphonic note generator with musical scale quantization.' },
  { name: 'Chord Generator', category: 'midi', catLabel: 'Notes & MIDI', desc: 'Harmonic chord builder supporting major, minor, 7th, 9th, sus4, and custom modal voicings.' },
  { name: 'Note Sequencer', category: 'midi', catLabel: 'Notes & MIDI', desc: 'Visual piano roll and step note sequence generator with gate and tie controls.' },
  { name: 'Humanizer & Quantize', category: 'midi', catLabel: 'Notes & MIDI', desc: 'Timing and velocity micro-randomization with scale snapping across 24 musical modes.' },

  // Modulation & CV
  { name: 'Audio Analyze', category: 'modulation', catLabel: 'Modulation & CV', desc: 'Extracts 8-band FFT spectrum, energy peaks, and transient onsets to modulate shaders & 3D.' },
  { name: 'Image Analyze', category: 'modulation', catLabel: 'Modulation & CV', desc: 'Extracts brightness, RGB color channels, motion vectors, and centroids to drive audio synths.' },
  { name: 'LFO', category: 'modulation', catLabel: 'Modulation & CV', desc: 'Tempo-synced low frequency oscillator with Sine, Triangle, Saw, Square, and Perlin waveforms.' },
  { name: 'Macro XY Pad', category: 'modulation', catLabel: 'Modulation & CV', desc: '2D modulation surface with live mouse motion recording, spring physics, and looping playback.' },
  { name: 'Math & Logic', category: 'modulation', catLabel: 'Modulation & CV', desc: 'Control voltage math operators (+, -, *, /, min, max, clamp, smooth lag, invert, range-map).' },
  { name: 'Audio to CV / Note to CV', category: 'modulation', catLabel: 'Modulation & CV', desc: 'Converts audio amplitude envelopes, pitch tracking, and MIDI velocities to normalized CV.' }
];

// ==========================================================================
// 5. Node Directory Filtering & Search
// ==========================================================================
const nodesGrid = document.getElementById('nodes-grid');
const searchInput = document.getElementById('node-search-input');
const filterButtons = document.querySelectorAll('.node-filter-btn');

let currentFilter = 'all';
let searchQuery = '';

function renderNodes() {
  if (!nodesGrid) return;

  const filtered = NODES_DATABASE.filter(node => {
    const matchesCategory = (currentFilter === 'all') || (node.category === currentFilter);
    const matchesSearch = !searchQuery || 
      node.name.toLowerCase().includes(searchQuery) ||
      node.desc.toLowerCase().includes(searchQuery) ||
      node.catLabel.toLowerCase().includes(searchQuery);
    return matchesCategory && matchesSearch;
  });

  if (filtered.length === 0) {
    nodesGrid.innerHTML = `
      <div style="grid-column: 1 / -1; text-align: center; padding: 40px; color: var(--text-muted);">
        <p>No nodes matching "<strong>${escapeHtml(searchQuery)}</strong>"</p>
      </div>
    `;
    return;
  }

  nodesGrid.innerHTML = filtered.map(node => `
    <div class="node-pill-card">
      <div class="node-pill-header">
        <span class="node-pill-name">${escapeHtml(node.name)}</span>
        <span class="node-pill-cat">${escapeHtml(node.catLabel)}</span>
      </div>
      <p class="node-pill-desc">${escapeHtml(node.desc)}</p>
    </div>
  `).join('');
}

function escapeHtml(str) {
  return str.replace(/[&<>"']/g, m => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
  })[m]);
}

if (filterButtons) {
  filterButtons.forEach(btn => {
    btn.addEventListener('click', () => {
      filterButtons.forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      currentFilter = btn.dataset.filter;
      renderNodes();
    });
  });
}

if (searchInput) {
  searchInput.addEventListener('input', (e) => {
    searchQuery = e.target.value.trim().toLowerCase();
    renderNodes();
  });
}

// ==========================================================================
// 6. Copy-to-Clipboard Functionality
// ==========================================================================
function setupCopyButtons() {
  const cloneBtn = document.getElementById('copy-clone-btn');
  if (cloneBtn) {
    cloneBtn.addEventListener('click', () => {
      copyTextToClipboard('git clone https://github.com/n1m21n/Infinite.git', cloneBtn);
    });
  }

  const buildBtn = document.getElementById('copy-build-btn');
  if (buildBtn) {
    buildBtn.addEventListener('click', () => {
      const code = `git clone https://github.com/n1m21n/Infinite.git\ncd Infinite\ncmake -B build -DCMAKE_BUILD_TYPE=Release\ncmake --build build -j8\nopen build/Infinite.app`;
      copyTextToClipboard(code, buildBtn);
    });
  }

  document.querySelectorAll('.copy-inline-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      const text = btn.dataset.code;
      copyTextToClipboard(text, btn);
    });
  });
}

function copyTextToClipboard(text, btnElement) {
  navigator.clipboard.writeText(text).then(() => {
    const originalText = btnElement.innerText;
    const originalHtml = btnElement.innerHTML;

    if (btnElement.tagName === 'BUTTON' && originalText) {
      btnElement.innerText = 'Copied!';
    } else {
      btnElement.style.color = 'var(--accent-emerald)';
    }

    setTimeout(() => {
      if (btnElement.tagName === 'BUTTON' && originalText) {
        btnElement.innerHTML = originalHtml;
      } else {
        btnElement.style.color = '';
      }
    }, 2000);
  });
}

// Initialize on DOM load
document.addEventListener('DOMContentLoaded', () => {
  initShowcaseSwitcher();
  setupCopyButtons();
  renderNodes();
  window.addEventListener('resize', resizeBgCanvas);
  resizeBgCanvas();
  requestAnimationFrame(animateBgCanvas);
});
