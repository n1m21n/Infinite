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
let lastBranchRectW = 0, lastBranchRectH = 0;

function resizeBranchCanvas() {
  if (!branchCanvas) return;
  const rect = branchCanvas.getBoundingClientRect();
  // Guard against a 0×0 measurement (fires on some mobile browsers before
  // fonts/layout settle, with no later 'resize' event to recover from it).
  if (rect.width < 10 || rect.height < 10) return;

  // Mobile browsers fire 'resize'/ResizeObserver repeatedly during a scroll
  // gesture (address bar collapsing, etc). Rebuilding on every call reset
  // branchProgress back to 0 each time, so the tree kept restarting and
  // never finished growing. Only rebuild when the size actually changed.
  if (Math.abs(rect.width - lastBranchRectW) < 2 && Math.abs(rect.height - lastBranchRectH) < 2) return;
  lastBranchRectW = rect.width;
  lastBranchRectH = rect.height;

  const dpr = Math.min(window.devicePixelRatio || 1, 2.5);
  branchCanvas.width = rect.width * dpr;
  branchCanvas.height = rect.height * dpr;
  branchWidth = branchCanvas.width;
  branchHeight = branchCanvas.height;
  branchProgress = 0;
  buildTreeStructure();
}

function getQuadPoint(p0, p1, p2, t) {
  const mt = 1 - t;
  return {
    x: mt * mt * p0.x + 2 * mt * t * p1.x + t * t * p2.x,
    y: mt * mt * p0.y + 2 * mt * t * p1.y + t * t * p2.y
  };
}

// One directed branch reaching a fixed tip position, plus a couple of short
// "twig" offshoots partway along its length for a real branching silhouette
// — without the fully-recursive fork exploding into an illegible tangle.
function buildOneBranch(p0, targetX, targetY, color, label) {
  const midX = p0.x + (targetX - p0.x) * 0.5;
  const midY = p0.y + (targetY - p0.y) * 0.35;
  const main = { p0, p1: { x: midX, y: midY }, p2: { x: targetX, y: targetY } };

  const twigs = [0.42, 0.68].map((at, i) => {
    const base = getQuadPoint(main.p0, main.p1, main.p2, at);
    const dir = i === 0 ? -1 : 1;
    const spread = 22 + Math.random() * 14;
    return {
      startAt: at,
      p0: base,
      p1: { x: base.x + (targetX > p0.x ? spread : -spread), y: base.y + dir * spread * 0.4 },
      p2: { x: base.x + (targetX > p0.x ? spread * 1.8 : -spread * 1.8), y: base.y + dir * spread * 1.3 }
    };
  });

  return { main, twigs, color, label, tipX: targetX, tipY: targetY, fromLeft: p0.x === 0 };
}

function buildTreeStructure() {
  const w = branchWidth;
  const h = branchHeight;
  const isMobile = w < 600 * (window.devicePixelRatio || 1);

  const leftTips = isMobile ? [
    { label: 'Sound', x: w * 0.36, y: h * 0.24, color: '#c2593f' },
    { label: 'Music', x: w * 0.42, y: h * 0.50, color: '#d97736' },
    { label: 'Art', x: w * 0.34, y: h * 0.78, color: '#b8860b' }
  ] : [
    { label: 'Sound', x: w * 0.30, y: h * 0.24, color: '#c2593f' },
    { label: 'Music', x: w * 0.38, y: h * 0.50, color: '#d97736' },
    { label: 'Art', x: w * 0.28, y: h * 0.78, color: '#b8860b' }
  ];

  const rightTips = isMobile ? [
    { label: 'Geometry', x: w * 0.64, y: h * 0.20, color: '#4d7c67' },
    { label: 'Generative', x: w * 0.70, y: h * 0.42, color: '#6b6b99' },
    { label: 'Node-Based', x: w * 0.60, y: h * 0.68, color: '#c2593f' },
    { label: 'Real-Time', x: w * 0.68, y: h * 0.86, color: '#2563eb' }
  ] : [
    { label: 'Geometry', x: w * 0.70, y: h * 0.20, color: '#4d7c67' },
    { label: 'Generative', x: w * 0.78, y: h * 0.42, color: '#6b6b99' },
    { label: 'Node-Based', x: w * 0.66, y: h * 0.68, color: '#c2593f' },
    { label: 'Real-Time', x: w * 0.76, y: h * 0.86, color: '#2563eb' }
  ];

  const leftBranches = leftTips.map(tip => buildOneBranch({ x: 0, y: h * 0.5 }, tip.x, tip.y, tip.color, tip.label));
  const rightBranches = rightTips.map(tip => buildOneBranch({ x: w, y: h * 0.5 }, tip.x, tip.y, tip.color, tip.label));

  branchTreeData = { leftBranches, rightBranches, isMobile };
}

function strokeGrowingQuad(ctx, p0, p1, p2, t, width, color, sway) {
  if (t <= 0) return;
  const steps = 24;
  const currentSteps = Math.max(1, Math.floor(steps * t));
  ctx.beginPath();
  for (let i = 0; i <= currentSteps; i++) {
    const u = (i / steps) * t;
    const pt = getQuadPoint(p0, p1, p2, u);
    const y = pt.y + sway * u;
    if (i === 0) ctx.moveTo(pt.x, y);
    else ctx.lineTo(pt.x, y);
  }
  ctx.lineWidth = width;
  ctx.strokeStyle = color;
  ctx.lineCap = 'round';
  ctx.stroke();
}

let branchTime = 0;
function animateNatureBranches() {
  if (!branchCtx || !branchCanvas || !branchTreeData) return;
  branchTime += 0.015;
  const w = branchWidth;
  const h = branchHeight;
  const dpr = window.devicePixelRatio || 1;

  branchCtx.clearRect(0, 0, w, h);

  if (branchProgress < 1) branchProgress += 0.008;
  const t = Math.min(1, branchProgress);

  const allBranches = [...branchTreeData.leftBranches, ...branchTreeData.rightBranches];

  // Pass 1: draw every branch's main line + twigs first.
  allBranches.forEach((branch, idx) => {
    const sway = Math.sin(branchTime + idx) * (2 * dpr);
    strokeGrowingQuad(branchCtx, branch.main.p0, branch.main.p1, branch.main.p2, t, 2.4 * dpr, 'rgba(30, 41, 59, 0.5)', sway);

    branch.twigs.forEach((twig, twigIdx) => {
      const twigT = Math.max(0, Math.min(1, (t - twig.startAt) / (1 - twig.startAt)));
      strokeGrowingQuad(branchCtx, twig.p0, twig.p1, twig.p2, twigT, 1.3 * dpr, 'rgba(30, 41, 59, 0.32)', sway * 0.6);
    });
  });

  // Pass 2: tip dots + labels drawn last so nothing paints over them.
  if (t >= 0.94) {
    const tipAlpha = Math.min(1, (t - 0.94) / 0.06);
    allBranches.forEach((branch, idx) => {
      const sway = Math.sin(branchTime + idx) * (2 * dpr);
      const tipX = branch.tipX;
      const tipY = branch.tipY + sway;

      branchCtx.beginPath();
      branchCtx.arc(tipX, tipY, 3.5 * dpr, 0, Math.PI * 2);
      branchCtx.fillStyle = branch.color;
      branchCtx.globalAlpha = tipAlpha;
      branchCtx.fill();

      branchCtx.font = `600 ${13 * dpr}px Inter, -apple-system, sans-serif`;
      branchCtx.fillStyle = '#1f1d1a';
      branchCtx.textAlign = branch.fromLeft ? 'right' : 'left';
      branchCtx.textBaseline = 'middle';

      const textOffsetX = branch.fromLeft ? -9 * dpr : 9 * dpr;
      branchCtx.fillText(branch.label, tipX + textOffsetX, tipY);
      branchCtx.globalAlpha = 1.0;
    });
  }

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

// Animation 3: Draped Cloth (PBD-style) Grid in 3D-ish Perspective
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

  const cols = 14;
  const rows = 10;

  let t = 0;
  function draw() {
    t += 0.02;
    const w = canvas.width;
    const h = canvas.height;
    const dpr = window.devicePixelRatio || 1;
    ctx.fillStyle = '#0f1218';
    ctx.fillRect(0, 0, w, h);

    const marginX = w * 0.12;
    const marginY = h * 0.14;
    const gridW = w - marginX * 2;
    const gridH = h - marginY * 2;

    // Compute a draped-cloth height field: pinned along the top edge,
    // sagging and billowing under simulated wind turbulence below.
    const points = [];
    for (let r = 0; r < rows; r++) {
      const row = [];
      for (let c = 0; c < cols; c++) {
        const u = c / (cols - 1);
        const v = r / (rows - 1);
        const pin = v; // 0 = pinned top edge, 1 = free-hanging bottom
        const sag = Math.pow(pin, 1.6) * (14 * dpr);
        const wind = Math.sin(u * 5 + t * 1.4 + v * 3) * pin * (7 * dpr)
                   + Math.cos(u * 2.3 - t * 1.1 + v * 1.6) * pin * (4 * dpr);
        const px = marginX + u * gridW;
        const py = marginY + v * gridH + sag + wind;
        // Faux depth shading: crest catches light, trough falls into shadow
        const shade = 0.35 + 0.5 * (Math.sin(u * 5 + t * 1.4 + v * 3) * 0.5 + 0.5) * pin;
        row.push({ x: px, y: py, shade });
      }
      points.push(row);
    }

    // Draw the cloth as a shaded quad mesh (subtle fill) with wireframe on top
    for (let r = 0; r < rows - 1; r++) {
      for (let c = 0; c < cols - 1; c++) {
        const a = points[r][c], b = points[r][c + 1], cc = points[r + 1][c + 1], d = points[r + 1][c];
        const avgShade = (a.shade + b.shade + cc.shade + d.shade) / 4;
        ctx.beginPath();
        ctx.moveTo(a.x, a.y);
        ctx.lineTo(b.x, b.y);
        ctx.lineTo(cc.x, cc.y);
        ctx.lineTo(d.x, d.y);
        ctx.closePath();
        ctx.fillStyle = `rgba(129, 178, 154, ${0.05 + avgShade * 0.16})`;
        ctx.fill();
      }
    }

    ctx.lineWidth = 1 * dpr;
    ctx.strokeStyle = 'rgba(233, 196, 106, 0.5)';
    for (let r = 0; r < rows; r++) {
      ctx.beginPath();
      for (let c = 0; c < cols; c++) {
        const p = points[r][c];
        if (c === 0) ctx.moveTo(p.x, p.y);
        else ctx.lineTo(p.x, p.y);
      }
      ctx.stroke();
    }
    ctx.strokeStyle = 'rgba(129, 178, 154, 0.45)';
    for (let c = 0; c < cols; c++) {
      ctx.beginPath();
      for (let r = 0; r < rows; r++) {
        const p = points[r][c];
        if (r === 0) ctx.moveTo(p.x, p.y);
        else ctx.lineTo(p.x, p.y);
      }
      ctx.stroke();
    }

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

  // Mobile browsers can report a 0×0 wrapper on first measurement (before
  // fonts/layout settle) and never fire a 'resize' event afterward — watch
  // the wrapper directly so the tree still builds once it actually has size.
  const natureWrapper = document.querySelector('.nature-canvas-wrapper');
  if (natureWrapper && 'ResizeObserver' in window) {
    let lastW = 0, lastH = 0;
    const ro = new ResizeObserver(entries => {
      const { width, height } = entries[0].contentRect;
      if (Math.abs(width - lastW) > 4 || Math.abs(height - lastH) > 4) {
        lastW = width; lastH = height;
        resizeBranchCanvas();
      }
    });
    ro.observe(natureWrapper);
  }
  if (document.fonts && document.fonts.ready) {
    document.fonts.ready.then(resizeBranchCanvas);
  }
  window.addEventListener('load', resizeBranchCanvas);

  initWavesAnimation();
  initWavetableAnimation();
  initParticlesAnimation();
  initKnobsAnimation();

  initMinimalAudioPlayer();
  initTickerTapes();
  initModal();
  initCopyButtons();
});
