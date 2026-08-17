/**
 * Infinite — Web Application Logic
 * Cosmic Dust, Growing Nature Tree Branches with Tip Labels, 4 Mini-Canvas Animations,
 * Minimal Audio Player, and 3 Interactive Node Ticker Tapes.
 */

// ==========================================================================
// 1. Cosmic Dust Canvas (Subtle Ink Particles on Paper)
// ==========================================================================
const cosmicCanvas = document.getElementById('cosmic-canvas');
const cosmicCtx = cosmicCanvas ? cosmicCanvas.getContext('2d') : null;
let cWidth, cHeight;
let cosmicStars = [];
let cosmicNebulae = [];

function resizeCosmicCanvas() {
  if (!cosmicCanvas) return;
  cWidth = cosmicCanvas.width = window.innerWidth;
  cHeight = cosmicCanvas.height = window.innerHeight;
  initCosmos();
}

function initCosmos() {
  cosmicStars = [];
  cosmicNebulae = [];

  const starCount = Math.floor(Math.min(cWidth, 1400) / 16);
  for (let i = 0; i < starCount; i++) {
    cosmicStars.push({
      x: Math.random() * cWidth,
      y: Math.random() * cHeight,
      size: Math.random() * 2 + 0.8,
      alpha: Math.random() * 0.22 + 0.08,
      pulseSpeed: 0.008 + Math.random() * 0.015,
      pulseOffset: Math.random() * Math.PI * 2,
      vx: (Math.random() - 0.5) * 0.15,
      vy: (Math.random() - 0.5) * 0.15,
      color: ['#2b2621', '#c2593f', '#d97736', '#4d7c67', '#6b6b99'][Math.floor(Math.random() * 5)]
    });
  }

  for (let i = 0; i < 4; i++) {
    cosmicNebulae.push({
      x: Math.random() * cWidth,
      y: Math.random() * cHeight,
      radius: Math.random() * 200 + 180,
      vx: (Math.random() - 0.5) * 0.08,
      vy: (Math.random() - 0.5) * 0.08,
      color: ['rgba(194, 89, 63, 0.035)', 'rgba(217, 119, 54, 0.035)', 'rgba(77, 124, 103, 0.035)', 'rgba(107, 107, 153, 0.035)'][i % 4]
    });
  }
}

let cosmicTime = 0;
function animateCosmos() {
  if (!cosmicCtx) return;
  cosmicTime++;
  cosmicCtx.clearRect(0, 0, cWidth, cHeight);

  cosmicNebulae.forEach(n => {
    n.x += n.vx;
    n.y += n.vy;
    if (n.x < -n.radius) n.x = cWidth + n.radius;
    if (n.x > cWidth + n.radius) n.x = -n.radius;
    if (n.y < -n.radius) n.y = cHeight + n.radius;
    if (n.y > cHeight + n.radius) n.y = -n.radius;

    const grad = cosmicCtx.createRadialGradient(n.x, n.y, 0, n.x, n.y, n.radius);
    grad.addColorStop(0, n.color);
    grad.addColorStop(1, 'transparent');

    cosmicCtx.fillStyle = grad;
    cosmicCtx.beginPath();
    cosmicCtx.arc(n.x, n.y, n.radius, 0, Math.PI * 2);
    cosmicCtx.fill();
  });

  cosmicStars.forEach(s => {
    s.x += s.vx;
    s.y += s.vy;
    if (s.x < 0) s.x = cWidth;
    if (s.x > cWidth) s.x = 0;
    if (s.y < 0) s.y = cHeight;
    if (s.y > cHeight) s.y = 0;

    const pulse = Math.sin(cosmicTime * s.pulseSpeed + s.pulseOffset) * 0.3 + 0.7;
    cosmicCtx.globalAlpha = s.alpha * pulse;
    cosmicCtx.fillStyle = s.color;
    cosmicCtx.beginPath();
    cosmicCtx.arc(s.x, s.y, s.size, 0, Math.PI * 2);
    cosmicCtx.fill();
  });
  cosmicCtx.globalAlpha = 1.0;

  requestAnimationFrame(animateCosmos);
}


// ==========================================================================
// 2. Organic Growing Tree Branches Dividing Inward from Both Sides
// Text Labels pop up at the tips of the branches
// ==========================================================================
const branchCanvas = document.getElementById('nature-branch-canvas');
const branchCtx = branchCanvas ? branchCanvas.getContext('2d') : null;

let branchWidth, branchHeight;
let branchProgress = 0;
let branchTreeData = null;

function resizeBranchCanvas() {
  if (!branchCanvas) return;
  const rect = branchCanvas.getBoundingClientRect();
  const dpr = window.devicePixelRatio || 1;
  branchCanvas.width = rect.width * dpr;
  branchCanvas.height = rect.height * dpr;
  branchWidth = branchCanvas.width;
  branchHeight = branchCanvas.height;
  buildTreeStructure();
}

function buildTreeStructure() {
  const w = branchWidth;
  const h = branchHeight;
  const isMobile = w < 600 * (window.devicePixelRatio || 1);

  // Left Branches (growing from x=0 toward center-left)
  const leftTips = isMobile ? [
    { label: 'Sound', targetX: w * 0.38, targetY: h * 0.22, color: '#c2593f' },
    { label: 'Music', targetX: w * 0.44, targetY: h * 0.48, color: '#d97736' },
    { label: 'Art', targetX: w * 0.36, targetY: h * 0.78, color: '#b8860b' }
  ] : [
    { label: 'Sound', targetX: w * 0.32, targetY: h * 0.24, color: '#c2593f' },
    { label: 'Music', targetX: w * 0.40, targetY: h * 0.50, color: '#d97736' },
    { label: 'Art', targetX: w * 0.30, targetY: h * 0.78, color: '#b8860b' }
  ];

  // Right Branches (growing from x=w toward center-right)
  const rightTips = isMobile ? [
    { label: 'Geometry', targetX: w * 0.62, targetY: h * 0.20, color: '#4d7c67' },
    { label: 'Generative', targetX: w * 0.68, targetY: h * 0.42, color: '#6b6b99' },
    { label: 'Node-Based', targetX: w * 0.58, targetY: h * 0.68, color: '#c2593f' },
    { label: 'Real-Time', targetX: w * 0.66, targetY: h * 0.86, color: '#2563eb' }
  ] : [
    { label: 'Geometry', targetX: w * 0.66, targetY: h * 0.22, color: '#4d7c67' },
    { label: 'Generative', targetX: w * 0.74, targetY: h * 0.44, color: '#6b6b99' },
    { label: 'Node-Based', targetX: w * 0.62, targetY: h * 0.68, color: '#c2593f' },
    { label: 'Real-Time', targetX: w * 0.72, targetY: h * 0.84, color: '#2563eb' }
  ];

  // Left Tree Curves
  const leftCurves = leftTips.map(tip => ({
    p0: { x: 0, y: h * 0.5 },
    p1: { x: w * 0.12, y: h * (tip.targetY > h * 0.5 ? 0.65 : 0.35) },
    p2: { x: w * 0.22, y: tip.targetY + (h * 0.5 - tip.targetY) * 0.3 },
    p3: { x: tip.targetX, y: tip.targetY },
    label: tip.label,
    color: tip.color
  }));

  // Right Tree Curves
  const rightCurves = rightTips.map(tip => ({
    p0: { x: w, y: h * 0.5 },
    p1: { x: w * 0.88, y: h * (tip.targetY > h * 0.5 ? 0.68 : 0.32) },
    p2: { x: w * 0.78, y: tip.targetY + (h * 0.5 - tip.targetY) * 0.3 },
    p3: { x: tip.targetX, y: tip.targetY },
    label: tip.label,
    color: tip.color
  }));

  branchTreeData = { leftCurves, rightCurves, isMobile };
}

function getCubicBezierPoint(p0, p1, p2, p3, t) {
  const mt = 1 - t;
  const mt2 = mt * mt;
  const mt3 = mt2 * mt;
  const t2 = t * t;
  const t3 = t2 * t;

  return {
    x: mt3 * p0.x + 3 * mt2 * t * p1.x + 3 * mt * t2 * p2.x + t3 * p3.x,
    y: mt3 * p0.y + 3 * mt2 * t * p1.y + 3 * mt * t2 * p2.y + t3 * p3.y
  };
}

let branchTime = 0;
function animateNatureBranches() {
  if (!branchCtx || !branchCanvas || !branchTreeData) return;
  branchTime += 0.02;
  const w = branchWidth;
  const h = branchHeight;
  const dpr = window.devicePixelRatio || 1;

  branchCtx.clearRect(0, 0, w, h);

  // Smooth ease-in progress for branch growth
  if (branchProgress < 1) {
    branchProgress += 0.012;
  }
  const t = Math.min(1, branchProgress);

  const allCurves = [...branchTreeData.leftCurves, ...branchTreeData.rightCurves];

  // Draw main organic tree trunk / branches
  allCurves.forEach((curve, idx) => {
    branchCtx.beginPath();
    const steps = 60;
    const currentSteps = Math.floor(steps * t);

    for (let i = 0; i <= currentSteps; i++) {
      const u = i / steps;
      // Gentle natural breathing sway
      const sway = Math.sin(branchTime + idx + u * Math.PI) * (1.5 * dpr) * u;
      const pt = getCubicBezierPoint(curve.p0, curve.p1, curve.p2, curve.p3, u);
      const px = pt.x;
      const py = pt.y + sway;

      if (i === 0) branchCtx.moveTo(px, py);
      else branchCtx.lineTo(px, py);
    }

    branchCtx.lineWidth = Math.max(1.2 * dpr, 2.5 * dpr * (1 - t * 0.4));
    branchCtx.strokeStyle = 'rgba(30, 41, 59, 0.45)';
    branchCtx.lineCap = 'round';
    branchCtx.stroke();

    // When the branch tip has finished growing, show the organic tip dot & floating text
    if (t >= 0.92) {
      const tipAlpha = Math.min(1, (t - 0.92) / 0.08);
      const endSway = Math.sin(branchTime + idx + Math.PI) * (1.5 * dpr);
      const tipX = curve.p3.x;
      const tipY = curve.p3.y + endSway;

      // Small natural branch tip node dot
      branchCtx.beginPath();
      branchCtx.arc(tipX, tipY, 3.5 * dpr, 0, Math.PI * 2);
      branchCtx.fillStyle = curve.color;
      branchCtx.globalAlpha = tipAlpha;
      branchCtx.fill();

      // Clean typography label floating right at the tip
      const isLeft = curve.p0.x === 0;
      branchCtx.font = `600 ${13 * dpr}px Inter, -apple-system, sans-serif`;
      branchCtx.fillStyle = '#1f1d1a';
      branchCtx.textAlign = isLeft ? 'right' : 'left';
      branchCtx.textBaseline = 'middle';

      const textOffsetX = isLeft ? -9 * dpr : 9 * dpr;
      branchCtx.fillText(curve.label, tipX + textOffsetX, tipY);
      branchCtx.globalAlpha = 1.0;
    }
  });

  requestAnimationFrame(animateNatureBranches);
}


// ==========================================================================
// 3. Four Capability Mini-Canvas Animations (Proper Responsive DPR handling)
// ==========================================================================

// Animation 1: Formula Interference Waves
function initWavesAnimation() {
  const canvas = document.getElementById('anim-waves');
  if (!canvas) return;
  const ctx = canvas.getContext('2d');
  let t = 0;

  function resize() {
    const rect = canvas.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    canvas.width = rect.width * dpr;
    canvas.height = rect.height * dpr;
  }
  resize();

  function draw() {
    t += 0.03;
    const w = canvas.width;
    const h = canvas.height;
    const dpr = window.devicePixelRatio || 1;
    ctx.fillStyle = '#0f1218';
    ctx.fillRect(0, 0, w, h);

    const step = 8 * dpr;
    ctx.lineWidth = 1.2 * dpr;

    for (let y = 0; y < h; y += step) {
      ctx.beginPath();
      ctx.strokeStyle = `rgba(217, 119, 54, ${0.35 + Math.sin(y * 0.05 + t) * 0.25})`;
      for (let x = 0; x < w; x += 4 * dpr) {
        const d1 = Math.hypot(x - w * 0.35, y - h * 0.5);
        const d2 = Math.hypot(x - w * 0.65, y - h * 0.5);
        const wave = Math.sin(d1 * (0.08 / dpr) - t * 2) + Math.sin(d2 * (0.08 / dpr) - t * 2);
        const py = y + wave * (6 * dpr);
        if (x === 0) ctx.moveTo(x, py);
        else ctx.lineTo(x, py);
      }
      ctx.stroke();
    }
    requestAnimationFrame(draw);
  }
  draw();
  window.addEventListener('resize', resize);
}

// Animation 2: 3D Wavetable Movement
function initWavetableAnimation() {
  const canvas = document.getElementById('anim-wavetable');
  if (!canvas) return;
  const ctx = canvas.getContext('2d');
  let t = 0;

  function resize() {
    const rect = canvas.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    canvas.width = rect.width * dpr;
    canvas.height = rect.height * dpr;
  }
  resize();

  function draw() {
    t += 0.025;
    const w = canvas.width;
    const h = canvas.height;
    const dpr = window.devicePixelRatio || 1;
    ctx.fillStyle = '#0f1218';
    ctx.fillRect(0, 0, w, h);

    const frames = 10;
    const sliceWidth = w * 0.7;

    for (let f = 0; f < frames; f++) {
      const depth = f / frames;
      const ox = w * 0.15 + (f - frames / 2) * (4 * dpr);
      const oy = h * 0.28 + f * (10 * dpr);
      const alpha = 0.2 + depth * 0.7;

      ctx.beginPath();
      ctx.strokeStyle = f === Math.floor((Math.sin(t) * 0.5 + 0.5) * (frames - 1)) 
        ? '#c2593f' 
        : `rgba(233, 196, 106, ${alpha})`;
      ctx.lineWidth = f === Math.floor((Math.sin(t) * 0.5 + 0.5) * (frames - 1)) ? 2.5 * dpr : 1.2 * dpr;

      for (let x = 0; x <= sliceWidth; x += 3 * dpr) {
        const u = x / sliceWidth;
        const morph = Math.sin(u * Math.PI * (2 + f * 0.5) + t * 2) * Math.cos(u * Math.PI + f * 0.3);
        const y = oy - morph * (22 * dpr);
        if (x === 0) ctx.moveTo(ox + x, y);
        else ctx.lineTo(ox + x, y);
      }
      ctx.stroke();
    }
    requestAnimationFrame(draw);
  }
  draw();
  window.addEventListener('resize', resize);
}

// Animation 3: Particle Cloud Simulation
function initParticlesAnimation() {
  const canvas = document.getElementById('anim-particles');
  if (!canvas) return;
  const ctx = canvas.getContext('2d');

  function resize() {
    const rect = canvas.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    canvas.width = rect.width * dpr;
    canvas.height = rect.height * dpr;
  }
  resize();

  const particles = [];
  for (let i = 0; i < 90; i++) {
    particles.push({
      angle: Math.random() * Math.PI * 2,
      radiusRatio: Math.random() * 0.7 + 0.1,
      speed: 0.015 + Math.random() * 0.02,
      yRatio: (Math.random() - 0.5) * 0.4,
      size: Math.random() * 2 + 1,
      color: ['#81b29a', '#b8b8d1', '#e9c46a'][Math.floor(Math.random() * 3)]
    });
  }

  let t = 0;
  function draw() {
    t += 0.02;
    const w = canvas.width;
    const h = canvas.height;
    const dpr = window.devicePixelRatio || 1;
    ctx.fillStyle = '#0f1218';
    ctx.fillRect(0, 0, w, h);

    const cx = w / 2;
    const cy = h / 2;
    const maxRadius = Math.min(w, h) * 0.4;

    particles.forEach(p => {
      p.angle += p.speed;
      const r = p.radiusRatio * maxRadius;
      const x = cx + Math.cos(p.angle) * r;
      const y = cy + Math.sin(p.angle) * (r * 0.45) + Math.sin(t + r) * (8 * dpr);

      ctx.beginPath();
      ctx.arc(x, y, p.size * dpr, 0, Math.PI * 2);
      ctx.fillStyle = p.color;
      ctx.fill();
    });

    requestAnimationFrame(draw);
  }
  draw();
  window.addEventListener('resize', resize);
}

// Animation 4: 4 Modulation Knobs with Cables
function initKnobsAnimation() {
  const canvas = document.getElementById('anim-knobs');
  if (!canvas) return;
  const ctx = canvas.getContext('2d');
  let t = 0;

  function resize() {
    const rect = canvas.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    canvas.width = rect.width * dpr;
    canvas.height = rect.height * dpr;
  }
  resize();

  function draw() {
    t += 0.03;
    const w = canvas.width;
    const h = canvas.height;
    const dpr = window.devicePixelRatio || 1;
    ctx.fillStyle = '#0f1218';
    ctx.fillRect(0, 0, w, h);

    const knobPositions = [
      { x: w * 0.25, y: h * 0.38, speed: 1.0, color: '#e07a5f' },
      { x: w * 0.75, y: h * 0.38, speed: -1.3, color: '#f4a261' },
      { x: w * 0.38, y: h * 0.72, speed: 0.8, color: '#81b29a' },
      { x: w * 0.68, y: h * 0.72, speed: -0.9, color: '#b8b8d1' }
    ];

    ctx.lineWidth = 1.8 * dpr;
    ctx.beginPath();
    ctx.strokeStyle = 'rgba(244, 162, 97, 0.4)';
    ctx.moveTo(knobPositions[0].x, knobPositions[0].y);
    ctx.bezierCurveTo(w * 0.5, h * 0.1, w * 0.5, h * 0.6, knobPositions[1].x, knobPositions[1].y);
    ctx.stroke();

    ctx.beginPath();
    ctx.strokeStyle = 'rgba(129, 178, 154, 0.4)';
    ctx.moveTo(knobPositions[2].x, knobPositions[2].y);
    ctx.bezierCurveTo(w * 0.4, h * 0.95, w * 0.6, h * 0.95, knobPositions[3].x, knobPositions[3].y);
    ctx.stroke();

    knobPositions.forEach((k, idx) => {
      const angle = t * k.speed + idx * 1.5;
      const r = 20 * dpr;

      ctx.beginPath();
      ctx.arc(k.x, k.y, r, 0, Math.PI * 2);
      ctx.fillStyle = '#1c212a';
      ctx.fill();
      ctx.lineWidth = 2 * dpr;
      ctx.strokeStyle = k.color;
      ctx.stroke();

      const px = k.x + Math.cos(angle) * (r * 0.75);
      const py = k.y + Math.sin(angle) * (r * 0.75);
      ctx.beginPath();
      ctx.moveTo(k.x, k.y);
      ctx.lineTo(px, py);
      ctx.lineWidth = 2.5 * dpr;
      ctx.strokeStyle = '#ffffff';
      ctx.stroke();
    });

    requestAnimationFrame(draw);
  }
  draw();
  window.addEventListener('resize', resize);
}


// ==========================================================================
// 4. Minimal Soundscape Audio Player
// ==========================================================================
function initMinimalAudioPlayer() {
  const audio = document.getElementById('m-audio-element');
  const playBtn = document.getElementById('minimal-play-btn');
  const playIcon = document.getElementById('m-play-icon');
  const pauseIcon = document.getElementById('m-pause-icon');
  const progressContainer = document.getElementById('m-progress-container');
  const progressFill = document.getElementById('m-progress-fill');
  const timeText = document.getElementById('m-track-time');

  if (!audio || !playBtn) return;

  function togglePlay() {
    if (audio.paused) {
      audio.play().then(() => {
        playIcon.style.display = 'none';
        pauseIcon.style.display = 'block';
      }).catch(() => {});
    } else {
      audio.pause();
      playIcon.style.display = 'block';
      pauseIcon.style.display = 'none';
    }
  }

  playBtn.addEventListener('click', togglePlay);

  audio.addEventListener('timeupdate', () => {
    if (audio.duration) {
      const pct = (audio.currentTime / audio.duration) * 100;
      if (progressFill) progressFill.style.width = `${pct}%`;

      const curM = Math.floor(audio.currentTime / 60);
      const curS = Math.floor(audio.currentTime % 60).toString().padStart(2, '0');
      const durM = Math.floor(audio.duration / 60);
      const durS = Math.floor(audio.duration % 60).toString().padStart(2, '0');
      if (timeText) timeText.textContent = `${curM}:${curS} / ${durM}:${durS}`;
    }
  });

  audio.addEventListener('ended', () => {
    playIcon.style.display = 'block';
    pauseIcon.style.display = 'none';
    if (progressFill) progressFill.style.width = '0%';
  });

  if (progressContainer) {
    progressContainer.addEventListener('click', (e) => {
      const rect = progressContainer.getBoundingClientRect();
      const clickX = e.clientX - rect.left;
      if (audio.duration) {
        audio.currentTime = (clickX / rect.width) * audio.duration;
      }
    });
  }
}


// ==========================================================================
// 5. 3 Continuous Ticker Tapes & Interactive Node Modal
// ==========================================================================
const ALL_NODES = [
  { name: 'Wavetable Synth', cat: 'Audio', desc: 'Multi-frame morphing oscillator with 12 factory tables, unison detune, and sub-oscillators.' },
  { name: 'Cloth Simulation', cat: '3D Physics', desc: 'Position-Based Dynamics (PBD) soft-body solver with wind forces and collision pinning.' },
  { name: 'PaulStretch', cat: 'Audio DSP', desc: 'Spectral FFT extreme time-stretching up to 50x for ambient atmospheric soundscapes.' },
  { name: 'Formula GLSL', cat: 'Shaders', desc: 'Live GLSL 150 fragment shader editor with 16 presets and live uniform CV inputs.' },
  { name: 'Remove Background', cat: 'AI Vision', desc: 'On-device Apple Vision neural segmentation with zero network latency.' },
  { name: 'Ocean Surface', cat: '3D & Physics', desc: 'Procedural Gerstner wave simulation with foam, choppiness, and wind vectors.' },
  { name: 'Modal Resonator', cat: 'Physical Modeling', desc: 'Acoustic simulation of struck bells, metal plates, tubes, and glass chimes.' },
  { name: 'Audio Analyze', cat: 'Modulation', desc: 'Extracts 8-band FFT spectrum and transient onset energy to drive visual shaders.' },
  { name: 'Granular Engine', cat: 'Audio', desc: 'Real-time granular cloud texture engine with live playhead scrubbing and grain spray.' },
  { name: 'Instance on Points', cat: '3D Geometry', desc: 'Scatter tens of thousands of 3D meshes in a single GPU instancing draw call.' },
  { name: 'Syphon In / Out', cat: 'Video I/O', desc: 'Zero-copy GPU video streaming between Infinite, OBS, Resolume, and TouchDesigner.' },
  { name: 'Color Curves', cat: 'Color Grading', desc: 'Photoshop-style cubic spline color grading across Master, RGB, and Luma channels.' },
  { name: 'LUT 3D', cat: 'Color', desc: 'Industry-standard .cube 3D lookup tables for cinematic film emulation.' },
  { name: 'Arpeggiator', cat: 'MIDI & Notes', desc: 'Tempo-synced pattern arpeggiator with Up, Down, Random, and multi-octave ranges.' },
  { name: 'Bouncing Balls', cat: 'MIDI', desc: 'Physics-based gravity simulation generating polyphonic notes mapped to musical scales.' },
  { name: 'Drum Sequencer', cat: 'Audio', desc: '8-track step sequencer with lane mutes, swing timing, and sample pattern chaining.' },
  { name: 'AU & VST3 Host', cat: 'Plugin', desc: 'Host third-party instrument and effect plugins with native GUI windows and mapped knobs.' },
  { name: 'Bloom & Glow', cat: '2D Effects', desc: 'Multi-pass thresholded anamorphic light bloom and diffuse optical glow.' },
  { name: 'Poisson Scattering', cat: '3D Geometry', desc: 'Distribute points smoothly across complex 3D meshes without clustering.' },
  { name: 'XY Motion Pad', cat: 'Modulation', desc: 'Record expressive 2D mouse gestures with spring physics and looping playback.' }
];

function initTickerTapes() {
  const t1 = document.getElementById('ticker-1');
  const t2 = document.getElementById('ticker-2');
  const t3 = document.getElementById('ticker-3');

  if (!t1 || !t2 || !t3) return;

  const g1 = ALL_NODES.slice(0, 7);
  const g2 = ALL_NODES.slice(7, 14);
  const g3 = ALL_NODES.slice(14);

  function createPillsHtml(group) {
    const tri = [...group, ...group, ...group];
    return tri.map(n => `
      <div class="ticker-pill" data-name="${escapeHtml(n.name)}" data-cat="${escapeHtml(n.cat)}" data-desc="${escapeHtml(n.desc)}">
        ${escapeHtml(n.name)}
      </div>
    `).join('');
  }

  t1.innerHTML = createPillsHtml(g1);
  t2.innerHTML = createPillsHtml(g2);
  t3.innerHTML = createPillsHtml(g3);

  document.querySelectorAll('.ticker-pill').forEach(pill => {
    pill.addEventListener('click', () => {
      openNodeModal(pill.dataset.name, pill.dataset.cat, pill.dataset.desc);
    });
  });
}

function openNodeModal(name, cat, desc) {
  const modal = document.getElementById('node-modal');
  const nameEl = document.getElementById('modal-name');
  const catEl = document.getElementById('modal-cat');
  const descEl = document.getElementById('modal-desc');

  if (nameEl) nameEl.textContent = name;
  if (catEl) catEl.textContent = cat;
  if (descEl) descEl.textContent = desc;

  if (modal) {
    modal.classList.add('active');
    modal.setAttribute('aria-hidden', 'false');
  }
}

function initModal() {
  const modal = document.getElementById('node-modal');
  const closeBtn = document.getElementById('modal-close-btn');

  if (closeBtn && modal) {
    closeBtn.addEventListener('click', () => {
      modal.classList.remove('active');
      modal.setAttribute('aria-hidden', 'true');
    });
  }

  if (modal) {
    modal.addEventListener('click', (e) => {
      if (e.target === modal) {
        modal.classList.remove('active');
        modal.setAttribute('aria-hidden', 'true');
      }
    });
  }
}

function escapeHtml(str) {
  return str.replace(/[&<>"']/g, m => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
  })[m]);
}

// Copy inline helpers
function initCopyButtons() {
  document.querySelectorAll('.copy-inline-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      const text = btn.dataset.code;
      if (!text) return;
      navigator.clipboard.writeText(text).then(() => {
        const original = btn.innerText;
        btn.innerText = 'Copied!';
        btn.style.color = 'var(--pastel-terracotta)';
        setTimeout(() => {
          btn.innerText = original;
          btn.style.color = '';
        }, 2000);
      });
    });
  });
}

// DOM Ready initialization
document.addEventListener('DOMContentLoaded', () => {
  window.addEventListener('resize', () => {
    resizeCosmicCanvas();
    resizeBranchCanvas();
  });

  resizeCosmicCanvas();
  requestAnimationFrame(animateCosmos);

  resizeBranchCanvas();
  requestAnimationFrame(animateNatureBranches);

  initWavesAnimation();
  initWavetableAnimation();
  initParticlesAnimation();
  initKnobsAnimation();

  initMinimalAudioPlayer();
  initTickerTapes();
  initModal();
  initCopyButtons();
});
