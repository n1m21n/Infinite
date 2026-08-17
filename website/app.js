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
// 2. Connected by Nature: Dynamic Connected Network Graph
// Guaranteed zero text overlap with clean pill badges and signal pulses
// ==========================================================================
const natureCanvas = document.getElementById('nature-branch-canvas');
const natureCtx = natureCanvas ? natureCanvas.getContext('2d') : null;

let netWidth = 0, netHeight = 0;
let lastNetRectW = 0, lastNetRectH = 0;
let networkNodes = [];
let networkEdges = [];
let networkPulses = [];
let hoveredNode = null;

const NETWORK_ITEMS = [
  { id: 'sound', label: 'Sound', color: '#c2593f', bg: 'rgba(194, 89, 63, 0.12)', dtX: 0.18, dtY: 0.26, mbX: 0.22, mbY: 0.18 },
  { id: 'music', label: 'Music', color: '#d97736', bg: 'rgba(217, 119, 54, 0.12)', dtX: 0.18, dtY: 0.72, mbX: 0.20, mbY: 0.44 },
  { id: 'nodebased', label: 'Node-Based', color: '#c2593f', bg: 'rgba(194, 89, 63, 0.12)', dtX: 0.38, dtY: 0.48, mbX: 0.50, mbY: 0.60 },
  { id: 'generative', label: 'Generative', color: '#6b6b99', bg: 'rgba(107, 107, 153, 0.12)', dtX: 0.58, dtY: 0.24, mbX: 0.78, mbY: 0.42 },
  { id: 'art', label: 'Art', color: '#b8860b', bg: 'rgba(184, 134, 11, 0.12)', dtX: 0.50, dtY: 0.80, mbX: 0.22, mbY: 0.84 },
  { id: 'geometry', label: 'Geometry', color: '#4d7c67', bg: 'rgba(77, 124, 103, 0.12)', dtX: 0.80, dtY: 0.30, mbX: 0.76, mbY: 0.18 },
  { id: 'realtime', label: 'Real-Time', color: '#2563eb', bg: 'rgba(37, 99, 235, 0.12)', dtX: 0.82, dtY: 0.74, mbX: 0.76, mbY: 0.84 }
];

const NETWORK_CONNECTIONS = [
  ['sound', 'music'],
  ['sound', 'nodebased'],
  ['music', 'nodebased'],
  ['nodebased', 'generative'],
  ['nodebased', 'art'],
  ['generative', 'geometry'],
  ['generative', 'realtime'],
  ['art', 'realtime'],
  ['art', 'generative'],
  ['geometry', 'realtime']
];

function resizeBranchCanvas() {
  if (!natureCanvas) return;
  const rect = natureCanvas.getBoundingClientRect();
  if (rect.width < 10 || rect.height < 10) return;

  if (Math.abs(rect.width - lastNetRectW) < 2 && Math.abs(rect.height - lastNetRectH) < 2) return;
  lastNetRectW = rect.width;
  lastNetRectH = rect.height;

  const dpr = Math.min(window.devicePixelRatio || 1, 2.5);
  natureCanvas.width = rect.width * dpr;
  natureCanvas.height = rect.height * dpr;
  netWidth = natureCanvas.width;
  netHeight = natureCanvas.height;

  buildNetworkStructure();
}

function buildNetworkStructure() {
  const w = netWidth;
  const h = netHeight;
  const isMobile = w < 620 * (window.devicePixelRatio || 1);

  networkNodes = NETWORK_ITEMS.map((item, idx) => {
    const baseX = isMobile ? item.mbX * w : item.dtX * w;
    const baseY = isMobile ? item.mbY * h : item.dtY * h;
    return {
      ...item,
      baseX,
      baseY,
      x: baseX,
      y: baseY,
      phase: idx * 0.9,
      floatSpeed: 0.8 + (idx % 3) * 0.2
    };
  });

  const nodeMap = {};
  networkNodes.forEach(n => { nodeMap[n.id] = n; });

  networkEdges = NETWORK_CONNECTIONS.map(([srcId, dstId]) => ({
    src: nodeMap[srcId],
    dst: nodeMap[dstId]
  })).filter(e => e.src && e.dst);

  // Initialize roaming signal pulses
  networkPulses = [];
  for (let i = 0; i < networkEdges.length * 2; i++) {
    const edge = networkEdges[i % networkEdges.length];
    networkPulses.push({
      edge,
      progress: Math.random(),
      speed: 0.003 + Math.random() * 0.005,
      forward: Math.random() > 0.5,
      size: 2.5 + Math.random() * 1.5
    });
  }
}

let netTime = 0;
function animateNatureBranches() {
  if (!natureCtx || !natureCanvas || networkNodes.length === 0) return;
  netTime += 0.016;
  const w = netWidth;
  const h = netHeight;
  const dpr = window.devicePixelRatio || 1;

  natureCtx.clearRect(0, 0, w, h);

  // Update floating positions with gentle organic breathing
  networkNodes.forEach(node => {
    const floatX = Math.sin(netTime * node.floatSpeed + node.phase) * (4 * dpr);
    const floatY = Math.cos(netTime * node.floatSpeed * 0.8 + node.phase) * (4 * dpr);
    node.x = node.baseX + floatX;
    node.y = node.baseY + floatY;
  });

  // 1. Draw Network Connection Edges
  networkEdges.forEach(edge => {
    natureCtx.beginPath();
    natureCtx.moveTo(edge.src.x, edge.src.y);
    natureCtx.lineTo(edge.dst.x, edge.dst.y);
    natureCtx.strokeStyle = 'rgba(60, 50, 40, 0.14)';
    natureCtx.lineWidth = 1.6 * dpr;
    natureCtx.stroke();
  });

  // 2. Draw Traveling Signal Pulses
  networkPulses.forEach(p => {
    p.progress += p.speed;
    if (p.progress > 1) p.progress = 0;
    const t = p.forward ? p.progress : 1 - p.progress;

    const px = p.edge.src.x + (p.edge.dst.x - p.edge.src.x) * t;
    const py = p.edge.src.y + (p.edge.dst.y - p.edge.src.y) * t;

    natureCtx.beginPath();
    natureCtx.arc(px, py, p.size * dpr, 0, Math.PI * 2);
    natureCtx.fillStyle = p.edge.src.color;
    natureCtx.globalAlpha = 0.75;
    natureCtx.fill();
    natureCtx.globalAlpha = 1.0;
  });

  // 3. Draw Nodes with Clean Protected Badge Clearance (Zero Overlap Guarantee)
  const isMobile = w < 620 * dpr;
  const fontSize = (isMobile ? 12 : 13.5) * dpr;
  natureCtx.font = `600 ${fontSize}px Inter, -apple-system, sans-serif`;
  natureCtx.textBaseline = 'middle';

  networkNodes.forEach(node => {
    const textWidth = natureCtx.measureText(node.label).width;
    const padX = 10 * dpr;
    const padY = 6 * dpr;
    const badgeH = fontSize + padY * 2;
    const dotRadius = 4.5 * dpr;
    const badgeW = textWidth + padX * 2 + dotRadius * 2 + 6 * dpr;

    const badgeX = node.x - badgeW / 2;
    const badgeY = node.y - badgeH / 2;
    const radius = badgeH / 2;

    // Outer subtle shadow / glow
    natureCtx.fillStyle = '#ffffff';
    natureCtx.strokeStyle = 'rgba(45, 35, 25, 0.12)';
    natureCtx.lineWidth = 1.2 * dpr;

    // Draw pill background so text NEVER collides or gets crossed by lines
    natureCtx.beginPath();
    natureCtx.roundRect(badgeX, badgeY, badgeW, badgeH, radius);
    natureCtx.fill();
    natureCtx.stroke();

    // Node Colored Dot Indicator
    const dotX = badgeX + padX + dotRadius;
    const dotY = node.y;

    // Halo around dot
    natureCtx.beginPath();
    natureCtx.arc(dotX, dotY, dotRadius + 2.5 * dpr, 0, Math.PI * 2);
    natureCtx.fillStyle = node.bg;
    natureCtx.fill();

    // Core dot
    natureCtx.beginPath();
    natureCtx.arc(dotX, dotY, dotRadius, 0, Math.PI * 2);
    natureCtx.fillStyle = node.color;
    natureCtx.fill();

    // Label Text
    natureCtx.textAlign = 'left';
    natureCtx.fillStyle = '#1f1d1a';
    natureCtx.fillText(node.label, dotX + dotRadius + 6 * dpr, node.y);
  });

  requestAnimationFrame(animateNatureBranches);
}


// ==========================================================================
// 3. Creative Streams Mini-Canvas Animations
// ==========================================================================

// Animation 1: Authentic GLSL Fragment Shader from Infinite (FormulaNode.cpp)
function initWavesAnimation() {
  const canvas = document.getElementById('anim-waves');
  if (!canvas) return;

  const gl = canvas.getContext('webgl') || canvas.getContext('experimental-webgl');
  if (!gl) {
    // 2D Canvas Fallback
    initWaves2DFallback(canvas);
    return;
  }

  const vsSource = `
    attribute vec2 position;
    void main() {
      gl_Position = vec4(position, 0.0, 1.0);
    }
  `;

  // Exact GLSL formula from Infinite app FormulaNode.cpp (Interference Waves Preset)
  const fsSource = `
    precision highp float;
    uniform vec2 u_resolution;
    uniform float u_time;
    uniform float uA;
    uniform float uB;
    uniform float uC;
    uniform float uD;

    void main() {
      vec2 uv = gl_FragCoord.xy / u_resolution.xy;
      vec2 p = uv - vec2(0.5);
      p.x *= u_resolution.x / u_resolution.y;
      float t = u_time;
      
      float v = 0.0;
      for (int i = 0; i < 6; i++) {
        float fi = float(i);
        vec2 src = 0.35 * vec2(cos(fi * 1.7 + t * 0.2 * (0.8 + uB)), sin(fi * 2.3 + t * 0.15 * (0.8 + uB)));
        v += sin(length(p - src) * (26.0 + uA * 50.0) - t * 2.8);
      }
      v /= 6.0;
      
      vec3 col = 0.5 + 0.5 * cos(6.2831 * (vec3(0.02, 0.28, 0.55) + v * 1.2 + uC));
      // Subtle vignette & dark tone curve
      col *= 1.0 - dot(p, p) * 0.45;
      gl_FragColor = vec4(col, 1.0);
    }
  `;

  function createShader(gl, type, source) {
    const shader = gl.createShader(type);
    gl.shaderSource(shader, source);
    gl.compileShader(shader);
    if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
      console.warn(gl.getShaderInfoLog(shader));
      gl.deleteShader(shader);
      return null;
    }
    return shader;
  }

  const vertexShader = createShader(gl, gl.VERTEX_SHADER, vsSource);
  const fragmentShader = createShader(gl, gl.FRAGMENT_SHADER, fsSource);
  if (!vertexShader || !fragmentShader) {
    initWaves2DFallback(canvas);
    return;
  }

  const program = gl.createProgram();
  gl.attachShader(program, vertexShader);
  gl.attachShader(program, fragmentShader);
  gl.linkProgram(program);
  if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
    console.warn(gl.getProgramInfoLog(program));
    initWaves2DFallback(canvas);
    return;
  }

  const positionBuffer = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, positionBuffer);
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
    -1, -1,
     1, -1,
    -1,  1,
    -1,  1,
     1, -1,
     1,  1
  ]), gl.STATIC_DRAW);

  const posAttr = gl.getAttribLocation(program, 'position');
  const resUniform = gl.getUniformLocation(program, 'u_resolution');
  const timeUniform = gl.getUniformLocation(program, 'u_time');
  const uAUniform = gl.getUniformLocation(program, 'uA');
  const uBUniform = gl.getUniformLocation(program, 'uB');
  const uCUniform = gl.getUniformLocation(program, 'uC');
  const uDUniform = gl.getUniformLocation(program, 'uD');

  function resize() {
    const rect = canvas.getBoundingClientRect();
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    canvas.width = Math.floor(rect.width * dpr);
    canvas.height = Math.floor(rect.height * dpr);
    gl.viewport(0, 0, canvas.width, canvas.height);
  }
  resize();

  let startTime = performance.now();
  function render() {
    const now = performance.now();
    const elapsed = (now - startTime) * 0.001;

    gl.useProgram(program);
    gl.enableVertexAttribArray(posAttr);
    gl.bindBuffer(gl.ARRAY_BUFFER, positionBuffer);
    gl.vertexAttribPointer(posAttr, 2, gl.FLOAT, false, 0, 0);

    gl.uniform2f(resUniform, canvas.width, canvas.height);
    gl.uniform1f(timeUniform, elapsed);
    gl.uniform1f(uAUniform, 0.45 + 0.3 * Math.sin(elapsed * 0.4));
    gl.uniform1f(uBUniform, 0.5 + 0.3 * Math.cos(elapsed * 0.3));
    gl.uniform1f(uCUniform, (elapsed * 0.08) % 1.0);
    gl.uniform1f(uDUniform, 0.5);

    gl.drawArrays(gl.TRIANGLES, 0, 6);
    requestAnimationFrame(render);
  }
  render();
  window.addEventListener('resize', resize);
}

function initWaves2DFallback(canvas) {
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
    const w = canvas.width, h = canvas.height, dpr = window.devicePixelRatio || 1;
    ctx.fillStyle = '#0f1218';
    ctx.fillRect(0, 0, w, h);
    ctx.lineWidth = 1.2 * dpr;
    for (let y = 0; y < h; y += 8 * dpr) {
      ctx.beginPath();
      ctx.strokeStyle = `rgba(217, 119, 54, ${0.35 + Math.sin(y * 0.05 + t) * 0.25})`;
      for (let x = 0; x < w; x += 4 * dpr) {
        const d1 = Math.hypot(x - w * 0.35, y - h * 0.5);
        const d2 = Math.hypot(x - w * 0.65, y - h * 0.5);
        const wave = Math.sin(d1 * (0.08 / dpr) - t * 2) + Math.sin(d2 * (0.08 / dpr) - t * 2);
        const py = y + wave * (6 * dpr);
        if (x === 0) ctx.moveTo(x, py); else ctx.lineTo(x, py);
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

// Animation 3: 3D Geometry Rotating Cube Render with Dynamic Particle System
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

  // Define 8 vertices of a 3D Cube centered at origin
  const cubeVertices = [
    { x: -1, y: -1, z: -1 },
    { x:  1, y: -1, z: -1 },
    { x:  1, y:  1, z: -1 },
    { x: -1, y:  1, z: -1 },
    { x: -1, y: -1, z:  1 },
    { x:  1, y: -1, z:  1 },
    { x:  1, y:  1, z:  1 },
    { x: -1, y:  1, z:  1 }
  ];

  // 12 Edges connecting vertices
  const cubeEdges = [
    [0, 1], [1, 2], [2, 3], [3, 0],
    [4, 5], [5, 6], [6, 7], [7, 4],
    [0, 4], [1, 5], [2, 6], [3, 7]
  ];

  // 6 Faces (vertex index quads)
  const cubeFaces = [
    [0, 1, 2, 3], // Back
    [4, 5, 6, 7], // Front
    [0, 1, 5, 4], // Bottom
    [2, 3, 7, 6], // Top
    [0, 3, 7, 4], // Left
    [1, 2, 6, 5]  // Right
  ];

  // 3D Orbiting Particle System
  const particleCount = 42;
  const particles = [];
  for (let i = 0; i < particleCount; i++) {
    const radius = 1.3 + Math.random() * 1.6;
    const theta = Math.random() * Math.PI * 2;
    const phi = (Math.random() - 0.5) * Math.PI;
    const speed = (0.012 + Math.random() * 0.016) * (Math.random() > 0.5 ? 1 : -1);
    particles.push({
      radius,
      theta,
      phi,
      speed,
      size: 1.2 + Math.random() * 1.8,
      color: ['#81b29a', '#e9c46a', '#e07a5f', '#6b6b99'][i % 4]
    });
  }

  let rotX = 0.4;
  let rotY = 0.6;
  let rotZ = 0.2;

  function project3D(v, w, h, dpr) {
    // 3D rotation
    let x1 = v.x * Math.cos(rotY) + v.z * Math.sin(rotY);
    let z1 = -v.x * Math.sin(rotY) + v.z * Math.cos(rotY);

    let y2 = v.y * Math.cos(rotX) - z1 * Math.sin(rotX);
    let z2 = v.y * Math.sin(rotX) + z1 * Math.cos(rotX);

    let x3 = x1 * Math.cos(rotZ) - y2 * Math.sin(rotZ);
    let y3 = x1 * Math.sin(rotZ) + y2 * Math.cos(rotZ);

    const fov = 3.6;
    const scale = (Math.min(w, h) * 0.26) / (fov + z2);

    return {
      x: w * 0.5 + x3 * scale,
      y: h * 0.5 + y3 * scale,
      z: z2,
      scale
    };
  }

  function draw() {
    rotX += 0.012;
    rotY += 0.016;
    rotZ += 0.007;

    const w = canvas.width;
    const h = canvas.height;
    const dpr = window.devicePixelRatio || 1;

    ctx.fillStyle = '#0f1218';
    ctx.fillRect(0, 0, w, h);

    // Project Cube Vertices
    const projectedVertices = cubeVertices.map(v => project3D(v, w, h, dpr));

    // Draw Semi-Transparent Depth-Sorted Cube Faces
    const faceDepths = cubeFaces.map(face => {
      const avgZ = (projectedVertices[face[0]].z + projectedVertices[face[1]].z + projectedVertices[face[2]].z + projectedVertices[face[3]].z) / 4;
      return { face, avgZ };
    });
    faceDepths.sort((a, b) => b.avgZ - a.avgZ);

    faceDepths.forEach(({ face, avgZ }) => {
      ctx.beginPath();
      ctx.moveTo(projectedVertices[face[0]].x, projectedVertices[face[0]].y);
      ctx.lineTo(projectedVertices[face[1]].x, projectedVertices[face[1]].y);
      ctx.lineTo(projectedVertices[face[2]].x, projectedVertices[face[2]].y);
      ctx.lineTo(projectedVertices[face[3]].x, projectedVertices[face[3]].y);
      ctx.closePath();
      const faceAlpha = Math.max(0.04, 0.14 - avgZ * 0.06);
      ctx.fillStyle = `rgba(77, 124, 103, ${faceAlpha})`;
      ctx.fill();
    });

    // Draw Cube Wireframe Edges
    ctx.lineWidth = 1.4 * dpr;
    cubeEdges.forEach(([i, j]) => {
      const p1 = projectedVertices[i];
      const p2 = projectedVertices[j];
      const edgeDepth = (p1.z + p2.z) * 0.5;
      const alpha = Math.max(0.25, 0.7 - edgeDepth * 0.2);

      ctx.beginPath();
      ctx.moveTo(p1.x, p1.y);
      ctx.lineTo(p2.x, p2.y);
      ctx.strokeStyle = `rgba(233, 196, 106, ${alpha})`;
      ctx.stroke();
    });

    // Draw Cube Vertices Dots
    projectedVertices.forEach(p => {
      ctx.beginPath();
      ctx.arc(p.x, p.y, 2.5 * dpr, 0, Math.PI * 2);
      ctx.fillStyle = '#e07a5f';
      ctx.fill();
    });

    // Update and Draw Orbiting Particles with Depth Attenuation
    particles.forEach(pt => {
      pt.theta += pt.speed;
      const p3x = pt.radius * Math.cos(pt.theta) * Math.cos(pt.phi);
      const p3y = pt.radius * Math.sin(pt.phi);
      const p3z = pt.radius * Math.sin(pt.theta) * Math.cos(pt.phi);

      const proj = project3D({ x: p3x, y: p3y, z: p3z }, w, h, dpr);
      const pAlpha = Math.max(0.2, Math.min(0.9, 0.65 - proj.z * 0.22));

      ctx.beginPath();
      ctx.arc(proj.x, proj.y, pt.size * dpr, 0, Math.PI * 2);
      ctx.fillStyle = pt.color;
      ctx.globalAlpha = pAlpha;
      ctx.fill();
    });
    ctx.globalAlpha = 1.0;

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
    const quad = [...group, ...group, ...group, ...group];
    return quad.map(n => `
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
