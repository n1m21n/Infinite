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

// Viewport Visibility Observer Helper: Automatically halts RAF rendering when off-screen
function createViewportLoop(element, renderFn) {
  if (!element) return;
  let isVisible = true;
  let isRunning = false;
  let rafId = null;

  function loop(ts) {
    if (!isVisible) {
      isRunning = false;
      return;
    }
    renderFn(ts);
    rafId = requestAnimationFrame(loop);
  }

  function start() {
    if (!isRunning && isVisible) {
      isRunning = true;
      rafId = requestAnimationFrame(loop);
    }
  }

  if ('IntersectionObserver' in window) {
    const observer = new IntersectionObserver((entries) => {
      isVisible = entries[0].isIntersecting;
      if (isVisible) {
        start();
      } else if (rafId) {
        cancelAnimationFrame(rafId);
        isRunning = false;
      }
    }, { rootMargin: '120px 0px' });
    observer.observe(element);
  }

  start();
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
}


// ==========================================================================
// ==========================================================================
// 2. Connected by Nature: Dynamic Interactive Network Graph
// Every node is draggable (touch on mobile, mouse on desktop).
// Moving any node causes all connected nodes to organically rearrange.
// ==========================================================================
const natureCanvas = document.getElementById('nature-branch-canvas');
const natureCtx = natureCanvas ? natureCanvas.getContext('2d') : null;

let netWidth = 0, netHeight = 0;
let networkNodes = [];
let networkEdges = [];
let networkPulses = [];
let hoveredNode = null;
let draggedNode = null;
let dragPos = { x: 0, y: 0 };
let dragOffset = { x: 0, y: 0 };
let netMouse = { x: -1000, y: -1000, active: false };
let cachedCanvasRect = null;

const NETWORK_ITEMS = [
  { id: 'sound', label: 'Sound', color: '#c2593f', bg: 'rgba(194, 89, 63, 0.12)', dtX: 0.12, dtY: 0.26, mbX: 0.20, mbY: 0.15 },
  { id: 'music', label: 'Music', color: '#d97736', bg: 'rgba(217, 119, 54, 0.12)', dtX: 0.12, dtY: 0.74, mbX: 0.20, mbY: 0.38 },
  { id: 'physics', label: 'Physics', color: '#5a6b7c', bg: 'rgba(90, 107, 124, 0.12)', dtX: 0.31, dtY: 0.32, mbX: 0.80, mbY: 0.38 },
  { id: 'math', label: 'Math', color: '#c2593f', bg: 'rgba(194, 89, 63, 0.12)', dtX: 0.31, dtY: 0.70, mbX: 0.50, mbY: 0.52 },
  { id: 'art', label: 'Art', color: '#b8860b', bg: 'rgba(184, 134, 11, 0.12)', dtX: 0.50, dtY: 0.46, mbX: 0.20, mbY: 0.70 },
  { id: 'motion', label: 'Motion', color: '#2563eb', bg: 'rgba(37, 99, 235, 0.12)', dtX: 0.69, dtY: 0.70, mbX: 0.22, mbY: 0.88 },
  { id: 'light', label: 'Light', color: '#d97736', bg: 'rgba(217, 119, 54, 0.12)', dtX: 0.69, dtY: 0.32, mbX: 0.80, mbY: 0.70 },
  { id: 'geometry', label: 'Geometry', color: '#4d7c67', bg: 'rgba(77, 124, 103, 0.12)', dtX: 0.88, dtY: 0.26, mbX: 0.80, mbY: 0.15 },
  { id: 'color', label: 'Color', color: '#6b6b99', bg: 'rgba(107, 107, 153, 0.12)', dtX: 0.88, dtY: 0.74, mbX: 0.78, mbY: 0.88 }
];

function updateNatureCanvasRect() {
  if (natureCanvas) {
    cachedCanvasRect = natureCanvas.getBoundingClientRect();
  }
}

function resizeBranchCanvas() {
  if (!natureCanvas) return;
  const rect = natureCanvas.getBoundingClientRect();
  cachedCanvasRect = rect;
  const rawW = rect.width > 10 ? rect.width : (natureCanvas.parentElement ? natureCanvas.parentElement.clientWidth : 960);
  const rawH = rect.height > 10 ? rect.height : 460;

  const dpr = Math.min(window.devicePixelRatio || 1, 2.5);
  const targetW = Math.round((rawW || 960) * dpr);
  const targetH = Math.round((rawH || 460) * dpr);

  if (Math.abs(targetW - netWidth) < 2 && Math.abs(targetH - netHeight) < 2) {
    return;
  }

  const prevW = netWidth || targetW;
  const prevH = netHeight || targetH;

  natureCanvas.width = targetW;
  natureCanvas.height = targetH;
  netWidth = targetW;
  netHeight = targetH;

  if (networkNodes.length === 0) {
    buildNetworkStructure();
  } else {
    // Seamless proportional scaling: preserve dragged node layout & dynamic state without resetting!
    const scaleX = targetW / prevW;
    const scaleY = targetH / prevH;
    const isMobile = targetW < 620 * dpr;
    const fontSize = (isMobile ? 12 : 14.5) * dpr;
    if (natureCtx) {
      natureCtx.font = `600 ${fontSize}px Geist, -apple-system, BlinkMacSystemFont, sans-serif`;
    }
    networkNodes.forEach(node => {
      node.x *= scaleX;
      node.y *= scaleY;
      node.baseX *= scaleX;
      node.baseY *= scaleY;
      node.vx = 0;
      node.vy = 0;
      const textW = natureCtx ? natureCtx.measureText(node.label).width : 50 * dpr;
      const padX = (isMobile ? 10 : 13) * dpr;
      const padY = (isMobile ? 6 : 8) * dpr;
      const dotRadius = (isMobile ? 4.2 : 5.2) * dpr;
      node.badgeW = textW + padX * 2 + dotRadius * 2 + 6 * dpr;
      node.badgeH = fontSize + padY * 2;
    });
  }
}

function buildNetworkStructure() {
  const w = netWidth;
  const h = netHeight;
  const dpr = Math.min(window.devicePixelRatio || 1, 2.5);
  const isMobile = w < 620 * dpr;
  const fontSize = (isMobile ? 12 : 14.5) * dpr;

  if (natureCtx) {
    natureCtx.font = `600 ${fontSize}px Geist, -apple-system, BlinkMacSystemFont, sans-serif`;
  }

  networkNodes = NETWORK_ITEMS.map((item, idx) => {
    const baseX = isMobile ? item.mbX * w : item.dtX * w;
    const baseY = isMobile ? item.mbY * h : item.dtY * h;
    const textW = natureCtx ? natureCtx.measureText(item.label).width : 50 * dpr;
    const padX = (isMobile ? 10 : 13) * dpr;
    const padY = (isMobile ? 6 : 8) * dpr;
    const dotRadius = (isMobile ? 4.2 : 5.2) * dpr;
    const badgeW = textW + padX * 2 + dotRadius * 2 + 6 * dpr;
    const badgeH = fontSize + padY * 2;

    return {
      ...item,
      baseX,
      baseY,
      x: baseX,
      y: baseY,
      vx: 0,
      vy: 0,
      phase: idx * 0.75,
      floatSpeed: 0.55 + (idx % 4) * 0.12,
      badgeW,
      badgeH
    };
  });

  // Full Mesh: all 36 pairwise connections with harmonic alpha
  networkEdges = [];
  const PRIMARY_SET = new Set([
    'sound-music', 'sound-physics', 'sound-art', 'music-math', 'physics-math', 'physics-art',
    'math-motion', 'art-motion', 'art-light', 'art-geometry', 'geometry-light',
    'geometry-color', 'motion-color', 'light-color', 'music-sound', 'art-physics'
  ]);

  for (let i = 0; i < networkNodes.length; i++) {
    for (let j = i + 1; j < networkNodes.length; j++) {
      const src = networkNodes[i];
      const dst = networkNodes[j];
      const key1 = `${src.id}-${dst.id}`;
      const key2 = `${dst.id}-${src.id}`;
      const isPrimary = PRIMARY_SET.has(key1) || PRIMARY_SET.has(key2);

      networkEdges.push({
        src,
        dst,
        isPrimary,
        baseAlpha: isPrimary ? 0.15 : 0.055,
        currentAlpha: isPrimary ? 0.15 : 0.055
      });
    }
  }

  // Organic Traveling Signal Pulses
  networkPulses = [];
  const pulseCount = 27;
  for (let i = 0; i < pulseCount; i++) {
    const edge = networkEdges[Math.floor(Math.random() * networkEdges.length)];
    networkPulses.push({
      edge,
      progress: Math.random(),
      speed: 0.0012 + Math.random() * 0.0022,
      forward: Math.random() > 0.5,
      size: 2.0 + Math.random() * 1.4,
      color: edge.src.color
    });
  }
}

function getNodeAtPoint(px, py, dpr) {
  for (let i = networkNodes.length - 1; i >= 0; i--) {
    const node = networkNodes[i];
    const halfW = (node.badgeW || 80) * 0.5 + 10 * dpr;
    const halfH = (node.badgeH || 26) * 0.5 + 10 * dpr;
    if (px >= node.x - halfW && px <= node.x + halfW &&
        py >= node.y - halfH && py <= node.y + halfH) {
      return node;
    }
  }
  return null;
}

function getCanvasPoint(clientX, clientY) {
  if (!natureCanvas) return { x: 0, y: 0 };
  if (!cachedCanvasRect) {
    cachedCanvasRect = natureCanvas.getBoundingClientRect();
  }
  const rectW = cachedCanvasRect.width || 1;
  const rectH = cachedCanvasRect.height || 1;
  return {
    x: (clientX - cachedCanvasRect.left) * (natureCanvas.width / rectW),
    y: (clientY - cachedCanvasRect.top) * (natureCanvas.height / rectH)
  };
}

// Interactive Modern Pointer & Drag Events (Instant Touch & Mouse Tracking)
if (natureCanvas) {
  natureCanvas.addEventListener('pointerdown', (e) => {
    updateNatureCanvasRect();
    const pt = getCanvasPoint(e.clientX, e.clientY);
    const dpr = Math.min(window.devicePixelRatio || 1, 2.5);
    const node = getNodeAtPoint(pt.x, pt.y, dpr);
    if (node) {
      draggedNode = node;
      hoveredNode = node;
      dragOffset.x = node.x - pt.x;
      dragOffset.y = node.y - pt.y;
      dragPos.x = pt.x + dragOffset.x;
      dragPos.y = pt.y + dragOffset.y;
      natureCanvas.style.cursor = 'grabbing';
      try {
        natureCanvas.setPointerCapture(e.pointerId);
      } catch (_) {}
      e.preventDefault();
    }
  });

  natureCanvas.addEventListener('pointermove', (e) => {
    const pt = getCanvasPoint(e.clientX, e.clientY);
    const dpr = Math.min(window.devicePixelRatio || 1, 2.5);

    if (draggedNode) {
      dragPos.x = pt.x + dragOffset.x;
      dragPos.y = pt.y + dragOffset.y;
      natureCanvas.style.cursor = 'grabbing';
      e.preventDefault();
    } else {
      hoveredNode = getNodeAtPoint(pt.x, pt.y, dpr);
      natureCanvas.style.cursor = hoveredNode ? 'grab' : 'default';
      netMouse.x = pt.x;
      netMouse.y = pt.y;
      netMouse.active = !!hoveredNode;
    }
  });

  const onPointerRelease = (e) => {
    if (draggedNode) {
      draggedNode.baseX = draggedNode.x;
      draggedNode.baseY = draggedNode.y;
      draggedNode.vx = 0;
      draggedNode.vy = 0;
      try {
        natureCanvas.releasePointerCapture(e.pointerId);
      } catch (_) {}
      draggedNode = null;
    }
    const pt = getCanvasPoint(e.clientX, e.clientY);
    const dpr = Math.min(window.devicePixelRatio || 1, 2.5);
    hoveredNode = getNodeAtPoint(pt.x, pt.y, dpr);
    natureCanvas.style.cursor = hoveredNode ? 'grab' : 'default';
  };

  natureCanvas.addEventListener('pointerup', onPointerRelease);
  natureCanvas.addEventListener('pointercancel', onPointerRelease);
  natureCanvas.addEventListener('pointerleave', () => {
    if (!draggedNode) {
      hoveredNode = null;
      netMouse.active = false;
      natureCanvas.style.cursor = 'default';
    }
  });

  window.addEventListener('scroll', updateNatureCanvasRect, { passive: true });
}

// Safe Canvas roundRect Polyfill
if (typeof CanvasRenderingContext2D !== 'undefined' && !CanvasRenderingContext2D.prototype.roundRect) {
  CanvasRenderingContext2D.prototype.roundRect = function (x, y, w, h, radii) {
    if (!radii) radii = 0;
    const r = typeof radii === 'number' ? radii : (Array.isArray(radii) ? radii[0] : 0);
    const radius = Math.min(r, Math.abs(w) / 2, Math.abs(h) / 2);
    this.beginPath();
    this.moveTo(x + radius, y);
    this.arcTo(x + w, y, x + w, y + h, radius);
    this.arcTo(x + w, y + h, x, y + h, radius);
    this.arcTo(x, y + h, x, y, radius);
    this.arcTo(x, y, x + w, y, radius);
    this.closePath();
    return this;
  };
}

let netTime = 0;
let lastNetTimestamp = 0;
let natureAnimating = true;

function animateNatureBranches(timestamp) {
  if (!natureCtx || !natureCanvas || !natureAnimating) return;
  if (networkNodes.length === 0) {
    resizeBranchCanvas();
    requestAnimationFrame(animateNatureBranches);
    return;
  }

  if (!lastNetTimestamp) lastNetTimestamp = timestamp || performance.now();
  const currentTs = timestamp || performance.now();
  const rawDt = (currentTs - lastNetTimestamp) / 1000;
  lastNetTimestamp = currentTs;
  const dt = Math.min(Math.max(rawDt, 0.008), 0.033);
  netTime += dt;

  const w = netWidth;
  const h = netHeight;
  const dpr = Math.min(window.devicePixelRatio || 1, 2.5);
  const isMobile = w < 620 * dpr;

  natureCtx.clearRect(0, 0, w, h);

  // 1. Force-Directed Physics Simulation (Organic Dynamic Rearrangement)
  // Soft pairwise repulsion to prevent overlapping without aggressive jitter
  const repulsionRadius = (isMobile ? 120 : 150) * dpr;
  for (let i = 0; i < networkNodes.length; i++) {
    for (let j = i + 1; j < networkNodes.length; j++) {
      const n1 = networkNodes[i];
      const n2 = networkNodes[j];
      const dx = n2.x - n1.x;
      const dy = n2.y - n1.y;
      const dist = Math.hypot(dx, dy) || 1;

      if (dist < repulsionRadius) {
        const norm = (repulsionRadius - dist) / repulsionRadius;
        const force = Math.min(norm * norm * (2.2 * dpr), 3.2 * dpr);
        const fx = (dx / dist) * force;
        const fy = (dy / dist) * force;
        if (n1 !== draggedNode) { n1.vx -= fx; n1.vy -= fy; }
        if (n2 !== draggedNode) { n2.vx += fx; n2.vy += fy; }
      }
    }
  }

  // Elastic spring tension along edges (differentiated primary vs secondary)
  networkEdges.forEach(edge => {
    const dx = edge.dst.x - edge.src.x;
    const dy = edge.dst.y - edge.src.y;
    const dist = Math.hypot(dx, dy) || 1;
    const restLength = (edge.isPrimary ? (isMobile ? 130 : 170) : (isMobile ? 210 : 280)) * dpr;
    const diff = dist - restLength;
    const springK = edge.isPrimary ? 0.0022 : 0.0004;
    const fx = (dx / dist) * diff * springK;
    const fy = (dy / dist) * diff * springK;

    if (edge.src !== draggedNode) { edge.src.vx += fx; edge.src.vy += fy; }
    if (edge.dst !== draggedNode) { edge.dst.vx += fx; edge.dst.vy += fy; }
  });

  // Balanced Home Anchor, Damping & Boundary Bounds Integration
  networkNodes.forEach(node => {
    const minX = (node.badgeW * 0.5 || 40) + 12 * dpr;
    const maxX = w - minX;
    const minY = (node.badgeH * 0.5 || 15) + 10 * dpr;
    const maxY = h - minY;

    if (node === draggedNode) {
      // 1:1 Instant Tracking with zero sluggishness
      const clampedX = Math.max(minX, Math.min(maxX, dragPos.x));
      const clampedY = Math.max(minY, Math.min(maxY, dragPos.y));
      node.x = clampedX;
      node.y = clampedY;
      node.baseX = clampedX;
      node.baseY = clampedY;
      node.vx = 0;
      node.vy = 0;
    } else {
      // Gentle anchor spring
      const anchorK = 0.018;
      node.vx += (node.baseX - node.x) * anchorK;
      node.vy += (node.baseY - node.y) * anchorK;

      // Organic gentle idle breathing
      const floatX = Math.sin(netTime * node.floatSpeed + node.phase) * (0.28 * dpr);
      const floatY = Math.cos(netTime * node.floatSpeed * 0.8 + node.phase) * (0.28 * dpr);
      node.vx += floatX;
      node.vy += floatY;

      // Smooth damping (friction)
      node.vx *= 0.87;
      node.vy *= 0.87;

      node.x += node.vx;
      node.y += node.vy;

      if (node.x < minX) { node.x = minX; node.vx = 0; }
      if (node.x > maxX) { node.x = maxX; node.vx = 0; }
      if (node.y < minY) { node.y = minY; node.vy = 0; }
      if (node.y > maxY) { node.y = maxY; node.vy = 0; }
    }
  });

  // 2. Draw Network Connection Edges (All Interconnected Filaments)
  networkEdges.forEach(edge => {
    const isConnectedToHover = hoveredNode && (edge.src === hoveredNode || edge.dst === hoveredNode);
    let targetAlpha = edge.baseAlpha;
    let lineWidth = (edge.isPrimary ? 1.5 : 0.9) * dpr;

    if (hoveredNode) {
      if (isConnectedToHover) {
        targetAlpha = 0.55;
        lineWidth = 2.2 * dpr;
      } else {
        targetAlpha = 0.025;
      }
    }

    edge.currentAlpha += (targetAlpha - edge.currentAlpha) * 0.15;

    natureCtx.beginPath();
    natureCtx.moveTo(edge.src.x, edge.src.y);
    natureCtx.lineTo(edge.dst.x, edge.dst.y);

    if (isConnectedToHover) {
      natureCtx.strokeStyle = hoveredNode.color;
      natureCtx.globalAlpha = edge.currentAlpha;
    } else {
      natureCtx.strokeStyle = `rgba(60, 50, 40, ${edge.currentAlpha})`;
      natureCtx.globalAlpha = 1.0;
    }
    natureCtx.lineWidth = lineWidth;
    natureCtx.stroke();
    natureCtx.globalAlpha = 1.0;
  });

  // 3. Draw Traveling Signal Pulses Across the Web (Slow, Organic Flow)
  networkPulses.forEach(p => {
    const isEdgeHovered = hoveredNode && (p.edge.src === hoveredNode || p.edge.dst === hoveredNode);
    const speed = isEdgeHovered ? p.speed * 2.0 : p.speed;
    p.progress += speed;
    if (p.progress > 1) p.progress = 0;
    const t = p.forward ? p.progress : 1 - p.progress;

    const px = p.edge.src.x + (p.edge.dst.x - p.edge.src.x) * t;
    const py = p.edge.src.y + (p.edge.dst.y - p.edge.src.y) * t;

    natureCtx.beginPath();
    natureCtx.arc(px, py, (isEdgeHovered ? p.size * 1.4 : p.size) * dpr, 0, Math.PI * 2);
    natureCtx.fillStyle = isEdgeHovered ? hoveredNode.color : p.edge.src.color;
    natureCtx.globalAlpha = isEdgeHovered ? 0.95 : (hoveredNode ? 0.35 : 0.7);
    natureCtx.fill();
    natureCtx.globalAlpha = 1.0;
  });

  // 4. Draw Nodes with Clean Protected Badge Clearance
  const fontSize = (isMobile ? 12 : 14.5) * dpr;
  natureCtx.font = `600 ${fontSize}px Geist, -apple-system, BlinkMacSystemFont, sans-serif`;
  natureCtx.textBaseline = 'middle';

  networkNodes.forEach(node => {
    const isHovered = (node === hoveredNode || node === draggedNode);
    const textWidth = natureCtx.measureText(node.label).width;
    const padX = (isMobile ? 10 : 13) * dpr;
    const padY = (isMobile ? 6 : 8) * dpr;
    const badgeH = fontSize + padY * 2;
    const dotRadius = (isHovered ? (isMobile ? 5.0 : 6.0) : (isMobile ? 4.2 : 5.2)) * dpr;
    const badgeW = textWidth + padX * 2 + dotRadius * 2 + 6 * dpr;

    node.badgeW = badgeW;
    node.badgeH = badgeH;

    const badgeX = node.x - badgeW / 2;
    const badgeY = node.y - badgeH / 2;
    const radius = badgeH / 2;

    // Draw pill background
    natureCtx.fillStyle = '#ffffff';
    natureCtx.strokeStyle = isHovered ? node.color : 'rgba(45, 35, 25, 0.14)';
    natureCtx.lineWidth = (isHovered ? 1.8 : 1.2) * dpr;

    natureCtx.beginPath();
    natureCtx.roundRect(badgeX, badgeY, badgeW, badgeH, radius);
    natureCtx.fill();
    natureCtx.stroke();

    // Node Colored Dot Indicator
    const dotX = badgeX + padX + dotRadius;
    const dotY = node.y;

    // Halo around dot
    natureCtx.beginPath();
    natureCtx.arc(dotX, dotY, dotRadius + (isHovered ? 3.5 : 2.2) * dpr, 0, Math.PI * 2);
    natureCtx.fillStyle = node.bg;
    natureCtx.fill();

    // Core dot
    natureCtx.beginPath();
    natureCtx.arc(dotX, dotY, dotRadius, 0, Math.PI * 2);
    natureCtx.fillStyle = node.color;
    natureCtx.fill();

    // Label Text
    natureCtx.textAlign = 'left';
    natureCtx.fillStyle = isHovered ? node.color : '#1f1d1a';
    natureCtx.fillText(node.label, dotX + dotRadius + 5 * dpr, node.y);
  });
}


// ==========================================================================
// 3. Creative Streams Mini-Canvas Animations (Fully Interactive)
// ==========================================================================

// Animation 1: Authentic GLSL Fragment Shader with Interactive Liquid Flow & Touch Swirl
function initWavesAnimation() {
  const canvas = document.getElementById('anim-waves');
  if (!canvas) return;

  const gl = canvas.getContext('webgl') || canvas.getContext('experimental-webgl');
  if (!gl) {
    initWaves2DFallback(canvas);
    return;
  }

  const vsSource = `
    attribute vec2 position;
    void main() {
      gl_Position = vec4(position, 0.0, 1.0);
    }
  `;

  // GLSL formula with interactive fluid liquid vortex & chromatic wave distortion
  const fsSource = `
    precision highp float;
    uniform vec2 u_resolution;
    uniform float u_time;
    uniform vec2 u_mouse;
    uniform vec2 u_velocity;
    uniform float u_touch;

    void main() {
      vec2 uv = gl_FragCoord.xy / u_resolution.xy;
      vec2 p = uv - vec2(0.5);
      p.x *= u_resolution.x / u_resolution.y;
      float t = u_time;

      // Interactive Liquid Flow disturbance
      vec2 m = u_mouse;
      m.x *= u_resolution.x / u_resolution.y;
      float dist = length(p - m);
      
      // Fluid vortex & pressure wave
      float fluidIntensity = u_touch * exp(-dist * 4.5);
      vec2 swirl = vec2(-(p.y - m.y), p.x - m.x) * fluidIntensity * 2.2;
      vec2 fluidDisplace = (swirl + u_velocity * fluidIntensity * 0.8) * 0.35;
      
      vec2 q = p - fluidDisplace;

      // Infinite Interference Wave Formula
      float v = 0.0;
      for (int i = 0; i < 6; i++) {
        float fi = float(i);
        vec2 src = 0.35 * vec2(cos(fi * 1.7 + t * 0.22), sin(fi * 2.3 + t * 0.16));
        v += sin(length(q - src) * (26.0 + fluidIntensity * 20.0) - t * 2.6 + fluidIntensity * 3.14);
      }
      v /= 6.0;

      // Color grading with liquid chromatic flare
      vec3 col = 0.5 + 0.5 * cos(6.2831 * (vec3(0.02, 0.28, 0.55) + v * 1.25 + fluidIntensity * 0.35));
      col += vec3(0.2, 0.08, 0.28) * fluidIntensity;
      
      // Vignette
      col *= 1.0 - dot(p, p) * 0.5;
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
    initWaves2DFallback(canvas);
    return;
  }

  const positionBuffer = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, positionBuffer);
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
    -1, -1,  1, -1, -1,  1,
    -1,  1,  1, -1,  1,  1
  ]), gl.STATIC_DRAW);

  const posAttr = gl.getAttribLocation(program, 'position');
  const resUniform = gl.getUniformLocation(program, 'u_resolution');
  const timeUniform = gl.getUniformLocation(program, 'u_time');
  const mouseUniform = gl.getUniformLocation(program, 'u_mouse');
  const velUniform = gl.getUniformLocation(program, 'u_velocity');
  const touchUniform = gl.getUniformLocation(program, 'u_touch');

  let mouseX = 0, mouseY = 0;
  let prevMouseX = 0, prevMouseY = 0;
  let velX = 0, velY = 0;
  let touchIntensity = 0.0;
  let isPointerActive = false;

  function updatePointer(e) {
    const rect = canvas.getBoundingClientRect();
    const clientX = e.touches ? e.touches[0].clientX : e.clientX;
    const clientY = e.touches ? e.touches[0].clientY : e.clientY;
    const nx = (clientX - rect.left) / rect.width - 0.5;
    const ny = -((clientY - rect.top) / rect.height - 0.5);

    velX = (nx - prevMouseX) * 8.0;
    velY = (ny - prevMouseY) * 8.0;
    prevMouseX = mouseX = nx;
    prevMouseY = mouseY = ny;
    touchIntensity = 1.0;
    isPointerActive = true;
  }

  canvas.addEventListener('mousemove', updatePointer, { passive: true });
  canvas.addEventListener('mousedown', (e) => { isPointerActive = true; updatePointer(e); }, { passive: true });
  window.addEventListener('mouseup', () => { isPointerActive = false; }, { passive: true });
  canvas.addEventListener('mouseleave', () => { isPointerActive = false; }, { passive: true });

  canvas.addEventListener('touchstart', (e) => { isPointerActive = true; updatePointer(e); }, { passive: true });
  canvas.addEventListener('touchmove', (e) => { updatePointer(e); }, { passive: true });
  canvas.addEventListener('touchend', () => { isPointerActive = false; }, { passive: true });

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

    // Smooth inertia and touch decay
    if (!isPointerActive) {
      touchIntensity += (0.0 - touchIntensity) * 0.04;
    }
    velX *= 0.88;
    velY *= 0.88;

    gl.useProgram(program);
    gl.enableVertexAttribArray(posAttr);
    gl.bindBuffer(gl.ARRAY_BUFFER, positionBuffer);
    gl.vertexAttribPointer(posAttr, 2, gl.FLOAT, false, 0, 0);

    gl.uniform2f(resUniform, canvas.width, canvas.height);
    gl.uniform1f(timeUniform, elapsed);
    gl.uniform2f(mouseUniform, mouseX, mouseY);
    gl.uniform2f(velUniform, velX, velY);
    gl.uniform1f(touchUniform, touchIntensity);

    gl.drawArrays(gl.TRIANGLES, 0, 6);
  }
  createViewportLoop(canvas, render);
  window.addEventListener('resize', resize, { passive: true });
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
        const wave = Math.sin(d1 * (0.08 / dpr) - t * 2);
        const py = y + wave * (6 * dpr);
        if (x === 0) ctx.moveTo(x, py); else ctx.lineTo(x, py);
      }
      ctx.stroke();
    }
  }
  createViewportLoop(canvas, draw);
  window.addEventListener('resize', resize, { passive: true });
}

// Animation 2: 3D Wavetable Synthesis with Interactive Position Scrubbing
function initWavetableAnimation() {
  const canvas = document.getElementById('anim-wavetable');
  if (!canvas) return;
  const ctx = canvas.getContext('2d');
  let t = 0;
  let targetPos = 0.5;
  let currentPos = 0.5;
  let isDragging = false;
  let userInteracting = false;

  function resize() {
    const rect = canvas.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    canvas.width = rect.width * dpr;
    canvas.height = rect.height * dpr;
  }
  resize();

  function updateScrub(e) {
    const rect = canvas.getBoundingClientRect();
    const clientX = e.touches ? e.touches[0].clientX : e.clientX;
    const clientY = e.touches ? e.touches[0].clientY : e.clientY;
    const relX = (clientX - rect.left) / rect.width;
    const relY = (clientY - rect.top) / rect.height;
    // Scrub diagonally across 3D isometric stack
    targetPos = Math.max(0, Math.min(1, relY * 0.7 + relX * 0.3));
    userInteracting = true;
  }

  canvas.addEventListener('mousedown', (e) => { isDragging = true; updateScrub(e); });
  window.addEventListener('mousemove', (e) => { if (isDragging) updateScrub(e); });
  window.addEventListener('mouseup', () => { isDragging = false; });

  canvas.addEventListener('touchstart', (e) => { isDragging = true; updateScrub(e); }, { passive: true });
  canvas.addEventListener('touchmove', (e) => { if (isDragging) updateScrub(e); }, { passive: true });
  canvas.addEventListener('touchend', () => { isDragging = false; });

  function draw() {
    t += 0.028;
    const w = canvas.width;
    const h = canvas.height;
    const dpr = window.devicePixelRatio || 1;

    // Smooth position interpolation
    if (!userInteracting && !isDragging) {
      targetPos = (Math.sin(t * 0.8) * 0.5 + 0.5);
    }
    currentPos += (targetPos - currentPos) * 0.12;

    ctx.fillStyle = '#0f1218';
    ctx.fillRect(0, 0, w, h);

    const frames = 12;
    const sliceWidth = w * 0.72;
    const activeFrameFloat = currentPos * (frames - 1);
    const activeFrameIdx = Math.round(activeFrameFloat);

    // Draw background 3D grid guidelines
    ctx.strokeStyle = 'rgba(255, 255, 255, 0.03)';
    ctx.lineWidth = 1 * dpr;
    for (let g = 0; g <= 4; g++) {
      const gx = w * 0.14 + (sliceWidth * (g / 4));
      ctx.beginPath();
      ctx.moveTo(gx - (frames / 2) * (5 * dpr), h * 0.22);
      ctx.lineTo(gx + (frames / 2) * (5 * dpr), h * 0.22 + (frames - 1) * (11 * dpr));
      ctx.stroke();
    }

    // Draw Stack of Wavetable Slices from Back to Front
    for (let f = 0; f < frames; f++) {
      const depth = f / (frames - 1);
      const ox = w * 0.14 + (f - frames / 2) * (5 * dpr);
      const oy = h * 0.24 + f * (11 * dpr);
      
      const distFromActive = Math.abs(f - activeFrameFloat);
      const isActive = distFromActive < 0.85;
      const proximityAlpha = Math.max(0.12, 1.0 - distFromActive * 0.35);

      // Gradient Fill under the active slice
      if (isActive) {
        ctx.beginPath();
        for (let x = 0; x <= sliceWidth; x += 4 * dpr) {
          const u = x / sliceWidth;
          const morph = (
            Math.sin(u * Math.PI * (2 + f * 0.45) + t * 1.8) * 0.65 +
            Math.sin(u * Math.PI * 4.0 + f * 0.8) * 0.35
          ) * Math.sin(u * Math.PI);
          const y = oy - morph * (26 * dpr);
          if (x === 0) ctx.moveTo(ox + x, oy);
          ctx.lineTo(ox + x, y);
        }
        ctx.lineTo(ox + sliceWidth, oy);
        ctx.closePath();
        const grad = ctx.createLinearGradient(ox, oy - 28 * dpr, ox, oy);
        grad.addColorStop(0, `rgba(224, 122, 95, ${0.35 * (1.0 - distFromActive)})`);
        grad.addColorStop(1, 'rgba(224, 122, 95, 0.0)');
        ctx.fillStyle = grad;
        ctx.fill();
      }

      // Draw Slice Stroke
      ctx.beginPath();
      if (isActive) {
        ctx.strokeStyle = `rgba(224, 122, 95, ${0.95 - distFromActive * 0.2})`;
        ctx.lineWidth = (2.6 - distFromActive * 1.0) * dpr;
      } else {
        ctx.strokeStyle = `rgba(233, 196, 106, ${proximityAlpha * 0.45})`;
        ctx.lineWidth = 1.1 * dpr;
      }

      for (let x = 0; x <= sliceWidth; x += 3 * dpr) {
        const u = x / sliceWidth;
        const morph = (
          Math.sin(u * Math.PI * (2 + f * 0.45) + t * 1.8) * 0.65 +
          Math.sin(u * Math.PI * 4.0 + f * 0.8) * 0.35
        ) * Math.sin(u * Math.PI);
        const y = oy - morph * (26 * dpr);
        if (x === 0) ctx.moveTo(ox + x, y);
        else ctx.lineTo(ox + x, y);
      }
      ctx.stroke();
    }

    // Active Position Scrubber Indicator Line
    const activeOy = h * 0.24 + activeFrameFloat * (11 * dpr);
    const activeOx = w * 0.14 + (activeFrameFloat - frames / 2) * (5 * dpr);
    ctx.beginPath();
    ctx.arc(activeOx + sliceWidth + 10 * dpr, activeOy, 3.5 * dpr, 0, Math.PI * 2);
    ctx.fillStyle = '#e07a5f';
    ctx.fill();

    // Subtle Position Notch Bar on Left
    ctx.fillStyle = 'rgba(255, 255, 255, 0.15)';
    ctx.fillRect(w * 0.06, h * 0.25, 2 * dpr, h * 0.5);
    ctx.fillStyle = '#e07a5f';
    ctx.fillRect(w * 0.05, h * 0.25 + currentPos * (h * 0.5 - 6 * dpr), 4 * dpr, 6 * dpr);
  }

  createViewportLoop(canvas, draw);
  window.addEventListener('resize', resize, { passive: true });
}

// Animation 3: 3D Geometry Rotating & Interactive Three.js Scene Cube
function initParticlesAnimation() {
  const canvas = document.getElementById('anim-particles');
  if (!canvas) return;

  // Use Three.js if available, otherwise pure WebGL 3D
  if (typeof THREE !== 'undefined') {
    initThreeJSCube(canvas);
  } else {
    initCanvas3DCube(canvas);
  }
}

function initThreeJSCube(canvas) {
  const renderer = new THREE.WebGLRenderer({ canvas, alpha: true, antialias: true });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));

  const scene = new THREE.Scene();
  const camera = new THREE.PerspectiveCamera(42, 1, 0.1, 100);
  camera.position.set(0, 0, 4.2);

  // Group for user rotation
  const cubeGroup = new THREE.Group();
  scene.add(cubeGroup);

  // 1. Faceted Glass Polyhedral Core (Beveled Cube)
  const boxGeo = new THREE.BoxGeometry(1.6, 1.6, 1.6);
  const boxMat = new THREE.MeshBasicMaterial({
    color: 0x4d7c67,
    wireframe: false,
    transparent: true,
    opacity: 0.16
  });
  const coreMesh = new THREE.Mesh(boxGeo, boxMat);
  cubeGroup.add(coreMesh);

  // 2. Wireframe Outline
  const edgesGeo = new THREE.EdgesGeometry(boxGeo);
  const lineMat = new THREE.LineBasicMaterial({ color: 0xe9c46a, linewidth: 2 });
  const wireframe = new THREE.LineSegments(edgesGeo, lineMat);
  cubeGroup.add(wireframe);

  // 3. Glowing Corner Vertex Dots
  const dotGeo = new THREE.BufferGeometry();
  const boxPos = boxGeo.attributes.position.array;
  const uniqueVerts = [];
  for (let i = 0; i < boxPos.length; i += 3) {
    const x = boxPos[i], y = boxPos[i+1], z = boxPos[i+2];
    if (!uniqueVerts.some(v => Math.hypot(v[0]-x, v[1]-y, v[2]-z) < 0.01)) {
      uniqueVerts.push([x, y, z]);
    }
  }
  const vertArray = new Float32Array(uniqueVerts.flat());
  dotGeo.setAttribute('position', new THREE.BufferAttribute(vertArray, 3));
  const dotMat = new THREE.PointsMaterial({ color: 0xe07a5f, size: 0.14 });
  const dotsMesh = new THREE.Points(dotGeo, dotMat);
  cubeGroup.add(dotsMesh);

  // 4. Orbiting Particle Halo
  const particleCount = 48;
  const partGeo = new THREE.BufferGeometry();
  const partPositions = new Float32Array(particleCount * 3);
  const partSpeeds = [];
  const partRadii = [];
  const partPhis = [];

  for (let i = 0; i < particleCount; i++) {
    const r = 1.4 + Math.random() * 1.4;
    const theta = Math.random() * Math.PI * 2;
    const phi = (Math.random() - 0.5) * Math.PI;
    partRadii.push(r);
    partPhis.push(phi);
    partSpeeds.push((0.01 + Math.random() * 0.015) * (Math.random() > 0.5 ? 1 : -1));

    partPositions[i * 3] = r * Math.cos(theta) * Math.cos(phi);
    partPositions[i * 3 + 1] = r * Math.sin(phi);
    partPositions[i * 3 + 2] = r * Math.sin(theta) * Math.cos(phi);
  }
  partGeo.setAttribute('position', new THREE.BufferAttribute(partPositions, 3));
  const particleMat = new THREE.PointsMaterial({
    color: 0x81b29a,
    size: 0.08,
    transparent: true,
    opacity: 0.85
  });
  const particlePoints = new THREE.Points(partGeo, particleMat);
  cubeGroup.add(particlePoints);

  // User Drag Rotation & Inertia
  let isDragging = false;
  let prevX = 0, prevY = 0;
  let velRotX = 0.008, velRotY = 0.012;

  function onPointerDown(e) {
    isDragging = true;
    prevX = e.touches ? e.touches[0].clientX : e.clientX;
    prevY = e.touches ? e.touches[0].clientY : e.clientY;
  }

  function onPointerMove(e) {
    if (!isDragging) return;
    const x = e.touches ? e.touches[0].clientX : e.clientX;
    const y = e.touches ? e.touches[0].clientY : e.clientY;
    const dx = x - prevX;
    const dy = y - prevY;
    velRotY = dx * 0.006;
    velRotX = dy * 0.006;
    prevX = x;
    prevY = y;
  }

  function onPointerUp() {
    isDragging = false;
  }

  canvas.addEventListener('mousedown', onPointerDown, { passive: true });
  window.addEventListener('mousemove', onPointerMove, { passive: true });
  window.addEventListener('mouseup', onPointerUp, { passive: true });

  canvas.addEventListener('touchstart', onPointerDown, { passive: true });
  canvas.addEventListener('touchmove', onPointerMove, { passive: true });
  canvas.addEventListener('touchend', onPointerUp, { passive: true });

  function resize() {
    const rect = canvas.getBoundingClientRect();
    const w = rect.width;
    const h = rect.height;
    camera.aspect = w / h;
    camera.updateProjectionMatrix();
    renderer.setSize(w, h, false);
  }
  resize();

  let pTime = 0;
  function render() {
    pTime++;
    cubeGroup.rotation.y += velRotY;
    cubeGroup.rotation.x += velRotX;

    // Friction / damping back to gentle idle rotation
    if (!isDragging) {
      velRotY += (0.008 - velRotY) * 0.04;
      velRotX += (0.004 - velRotX) * 0.04;
    }

    // Orbit particles
    const positions = partGeo.attributes.position.array;
    for (let i = 0; i < particleCount; i++) {
      const theta = pTime * partSpeeds[i] + i * 1.2;
      const r = partRadii[i];
      const phi = partPhis[i];
      positions[i * 3] = r * Math.cos(theta) * Math.cos(phi);
      positions[i * 3 + 1] = r * Math.sin(phi);
      positions[i * 3 + 2] = r * Math.sin(theta) * Math.cos(phi);
    }
    partGeo.attributes.position.needsUpdate = true;

    renderer.render(scene, camera);
  }

  createViewportLoop(canvas, render);
  window.addEventListener('resize', resize, { passive: true });
}

function initCanvas3DCube(canvas) {
  const ctx = canvas.getContext('2d');
  let rotX = 0.4, rotY = 0.6;
  let velX = 0.012, velY = 0.016;
  let isDragging = false, prevX = 0, prevY = 0;

  function resize() {
    const rect = canvas.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    canvas.width = rect.width * dpr;
    canvas.height = rect.height * dpr;
  }
  resize();

  const cubeVertices = [
    { x: -1, y: -1, z: -1 }, { x:  1, y: -1, z: -1 },
    { x:  1, y:  1, z: -1 }, { x: -1, y:  1, z: -1 },
    { x: -1, y: -1, z:  1 }, { x:  1, y: -1, z:  1 },
    { x:  1, y:  1, z:  1 }, { x: -1, y:  1, z:  1 }
  ];
  const cubeEdges = [
    [0,1],[1,2],[2,3],[3,0],[4,5],[5,6],[6,7],[7,4],[0,4],[1,5],[2,6],[3,7]
  ];

  function onDown(e) {
    isDragging = true;
    prevX = e.touches ? e.touches[0].clientX : e.clientX;
    prevY = e.touches ? e.touches[0].clientY : e.clientY;
  }
  function onMove(e) {
    if (!isDragging) return;
    const x = e.touches ? e.touches[0].clientX : e.clientX;
    const y = e.touches ? e.touches[0].clientY : e.clientY;
    velY = (x - prevX) * 0.008;
    velX = (y - prevY) * 0.008;
    prevX = x; prevY = y;
  }
  function onUp() { isDragging = false; }

  canvas.addEventListener('mousedown', onDown, { passive: true });
  window.addEventListener('mousemove', onMove, { passive: true });
  window.addEventListener('mouseup', onUp, { passive: true });
  canvas.addEventListener('touchstart', onDown, { passive: true });
  canvas.addEventListener('touchmove', onMove, { passive: true });
  canvas.addEventListener('touchend', onUp, { passive: true });

  function draw() {
    rotX += velX;
    rotY += velY;
    if (!isDragging) {
      velX += (0.008 - velX) * 0.04;
      velY += (0.012 - velY) * 0.04;
    }

    const w = canvas.width, h = canvas.height, dpr = window.devicePixelRatio || 1;
    ctx.fillStyle = '#0f1218';
    ctx.fillRect(0, 0, w, h);

    const projected = cubeVertices.map(v => {
      let x1 = v.x * Math.cos(rotY) + v.z * Math.sin(rotY);
      let z1 = -v.x * Math.sin(rotY) + v.z * Math.cos(rotY);
      let y2 = v.y * Math.cos(rotX) - z1 * Math.sin(rotX);
      let z2 = v.y * Math.sin(rotX) + z1 * Math.cos(rotX);
      const scale = (Math.min(w, h) * 0.42) / (3.2 + z2);
      return { x: w * 0.5 + x1 * scale, y: h * 0.5 + y2 * scale };
    });

    ctx.lineWidth = 1.4 * dpr;
    ctx.strokeStyle = '#e9c46a';
    cubeEdges.forEach(([i, j]) => {
      ctx.beginPath();
      ctx.moveTo(projected[i].x, projected[i].y);
      ctx.lineTo(projected[j].x, projected[j].y);
      ctx.stroke();
    });

    projected.forEach(p => {
      ctx.beginPath();
      ctx.arc(p.x, p.y, 2.5 * dpr, 0, Math.PI * 2);
      ctx.fillStyle = '#e07a5f';
      ctx.fill();
    });
  }
  createViewportLoop(canvas, draw);
  window.addEventListener('resize', resize, { passive: true });
}

// Animation 4: Universal Modulation with Fully Interactive Drag-to-Rotate Knobs & Live Cables
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

  const knobs = [
    { id: 0, relX: 0.25, relY: 0.36, val: 0.65, baseSpeed: 1.0, color: '#e07a5f', isDragging: false },
    { id: 1, relX: 0.75, relY: 0.36, val: 0.35, baseSpeed: -1.3, color: '#f4a261', isDragging: false },
    { id: 2, relX: 0.32, relY: 0.72, val: 0.80, baseSpeed: 0.8, color: '#81b29a', isDragging: false },
    { id: 3, relX: 0.72, relY: 0.72, val: 0.50, baseSpeed: -0.9, color: '#b8b8d1', isDragging: false }
  ];

  let activeKnob = null;
  let dragStartY = 0;
  let dragStartVal = 0;

  function getEventPos(e) {
    const rect = canvas.getBoundingClientRect();
    const clientX = e.touches ? e.touches[0].clientX : e.clientX;
    const clientY = e.touches ? e.touches[0].clientY : e.clientY;
    return {
      x: (clientX - rect.left) / rect.width,
      y: (clientY - rect.top) / rect.height,
      rawY: clientY
    };
  }

  function onPointerDown(e) {
    const pos = getEventPos(e);
    knobs.forEach(k => {
      const dist = Math.hypot(pos.x - k.relX, pos.y - k.relY);
      if (dist < 0.18) {
        activeKnob = k;
        k.isDragging = true;
        dragStartY = pos.rawY;
        dragStartVal = k.val;
      }
    });
  }

  function onPointerMove(e) {
    if (!activeKnob) return;
    const clientY = e.touches ? e.touches[0].clientY : e.clientY;
    const deltaY = dragStartY - clientY;
    // Drag up to increase value, down to decrease
    activeKnob.val = Math.max(0.0, Math.min(1.0, dragStartVal + deltaY * 0.007));
  }

  function onPointerUp() {
    if (activeKnob) {
      activeKnob.isDragging = false;
      activeKnob = null;
    }
  }

  canvas.addEventListener('mousedown', onPointerDown);
  window.addEventListener('mousemove', onPointerMove);
  window.addEventListener('mouseup', onPointerUp);

  canvas.addEventListener('touchstart', onPointerDown, { passive: true });
  canvas.addEventListener('touchmove', onPointerMove, { passive: true });
  canvas.addEventListener('touchend', onPointerUp, { passive: true });

  // Traveling signal pulses along patch cables
  const cablePulses = [
    { src: 0, dst: 1, prog: 0.0, speed: 0.015 },
    { src: 2, dst: 3, prog: 0.5, speed: 0.018 }
  ];

  function draw() {
    t += 0.025;
    const w = canvas.width;
    const h = canvas.height;
    const dpr = window.devicePixelRatio || 1;

    ctx.fillStyle = '#0f1218';
    ctx.fillRect(0, 0, w, h);

    const positions = knobs.map(k => ({
      x: k.relX * w,
      y: k.relY * h,
      val: k.val,
      color: k.color,
      isDragging: k.isDragging
    }));

    // 1. Draw Glowing Patch Cables
    // Cable 1: Top left to Top right
    ctx.lineWidth = 2.0 * dpr;
    ctx.beginPath();
    ctx.strokeStyle = 'rgba(244, 162, 97, 0.45)';
    ctx.moveTo(positions[0].x, positions[0].y);
    ctx.bezierCurveTo(w * 0.5, h * 0.1, w * 0.5, h * 0.58, positions[1].x, positions[1].y);
    ctx.stroke();

    // Cable 2: Bottom left to Bottom right
    ctx.beginPath();
    ctx.strokeStyle = 'rgba(129, 178, 154, 0.45)';
    ctx.moveTo(positions[2].x, positions[2].y);
    ctx.bezierCurveTo(w * 0.44, h * 0.94, w * 0.62, h * 0.94, positions[3].x, positions[3].y);
    ctx.stroke();

    // 2. Animate Traveling Signal Pulses on Cables
    cablePulses.forEach((pulse, idx) => {
      const pSrc = positions[pulse.src];
      const pDst = positions[pulse.dst];
      const speedMultiplier = 0.5 + (knobs[pulse.src].val * 1.5);
      pulse.prog = (pulse.prog + pulse.speed * speedMultiplier) % 1.0;

      let px, py;
      const u = pulse.prog;
      if (idx === 0) {
        // Cubic bezier interpolation for top cable
        const c1x = w * 0.5, c1y = h * 0.1, c2x = w * 0.5, c2y = h * 0.58;
        px = Math.pow(1-u, 3)*pSrc.x + 3*Math.pow(1-u, 2)*u*c1x + 3*(1-u)*Math.pow(u, 2)*c2x + Math.pow(u, 3)*pDst.x;
        py = Math.pow(1-u, 3)*pSrc.y + 3*Math.pow(1-u, 2)*u*c1y + 3*(1-u)*Math.pow(u, 2)*c2y + Math.pow(u, 3)*pDst.y;
      } else {
        const c1x = w * 0.44, c1y = h * 0.94, c2x = w * 0.62, c2y = h * 0.94;
        px = Math.pow(1-u, 3)*pSrc.x + 3*Math.pow(1-u, 2)*u*c1x + 3*(1-u)*Math.pow(u, 2)*c2x + Math.pow(u, 3)*pDst.x;
        py = Math.pow(1-u, 3)*pSrc.y + 3*Math.pow(1-u, 2)*u*c1y + 3*(1-u)*Math.pow(u, 2)*c2y + Math.pow(u, 3)*pDst.y;
      }

      ctx.beginPath();
      ctx.arc(px, py, 3.2 * dpr, 0, Math.PI * 2);
      ctx.fillStyle = pSrc.color;
      ctx.fill();
    });

    // 3. Draw 4 Interactive Rotary Knobs
    knobs.forEach((k, idx) => {
      const pos = positions[idx];
      const r = 24 * dpr;

      // Auto-modulate if not being dragged
      if (!k.isDragging) {
        k.val = Math.max(0, Math.min(1, k.val + Math.sin(t * k.baseSpeed + idx) * 0.003));
      }

      // Angle range: -135deg to +135deg (mapped from val 0.0 -> 1.0)
      const startAngle = Math.PI * 0.75;
      const totalSweep = Math.PI * 1.5;
      const currentAngle = startAngle + k.val * totalSweep;

      // Outer Track Arc (Background)
      ctx.beginPath();
      ctx.arc(pos.x, pos.y, r + 4 * dpr, startAngle, startAngle + totalSweep);
      ctx.strokeStyle = 'rgba(255, 255, 255, 0.08)';
      ctx.lineWidth = 2.5 * dpr;
      ctx.stroke();

      // Active Colored Value Arc
      ctx.beginPath();
      ctx.arc(pos.x, pos.y, r + 4 * dpr, startAngle, currentAngle);
      ctx.strokeStyle = k.color;
      ctx.lineWidth = 2.5 * dpr;
      ctx.stroke();

      // Knob Body Outer Bezel
      ctx.beginPath();
      ctx.arc(pos.x, pos.y, r, 0, Math.PI * 2);
      ctx.fillStyle = k.isDragging ? '#262d38' : '#1b2029';
      ctx.fill();
      ctx.strokeStyle = k.isDragging ? k.color : 'rgba(255, 255, 255, 0.12)';
      ctx.lineWidth = 1.5 * dpr;
      ctx.stroke();

      // Knob Inner Disc
      ctx.beginPath();
      ctx.arc(pos.x, pos.y, r * 0.75, 0, Math.PI * 2);
      ctx.fillStyle = '#141820';
      ctx.fill();

      // Indicator Needle Dot & Pointer
      const nx = pos.x + Math.cos(currentAngle) * (r * 0.65);
      const ny = pos.y + Math.sin(currentAngle) * (r * 0.65);
      ctx.beginPath();
      ctx.moveTo(pos.x, pos.y);
      ctx.lineTo(nx, ny);
      ctx.strokeStyle = '#ffffff';
      ctx.lineWidth = 2.2 * dpr;
      ctx.lineCap = 'round';
      ctx.stroke();

      // Glowing Needle Tip Dot
      ctx.beginPath();
      ctx.arc(nx, ny, 2.2 * dpr, 0, Math.PI * 2);
      ctx.fillStyle = k.color;
      ctx.fill();
    });
  }

  createViewportLoop(canvas, draw);
  window.addEventListener('resize', resize, { passive: true });
}

// Mobile Slider Navigator for Creative Streams
function initMobileCapabilitiesSlider() {
  const grid = document.getElementById('capabilities-grid');
  const dots = document.querySelectorAll('#cap-slider-dots .cap-dot');
  const prevBtn = document.getElementById('cap-prev-btn');
  const nextBtn = document.getElementById('cap-next-btn');
  const cards = document.querySelectorAll('.capabilities-grid .cap-card');

  if (!grid || cards.length === 0) return;

  let activeIndex = 0;

  function setActiveDot(idx) {
    activeIndex = Math.max(0, Math.min(cards.length - 1, idx));
    dots.forEach((dot, i) => {
      if (i === activeIndex) {
        dot.classList.add('active');
      } else {
        dot.classList.remove('active');
      }
    });
  }

  function scrollToCard(idx) {
    if (cards.length === 0) return;
    if (idx < 0) idx = cards.length - 1;
    if (idx >= cards.length) idx = 0;

    const targetCard = cards[idx];
    if (!targetCard) return;

    try {
      targetCard.scrollIntoView({
        behavior: 'smooth',
        inline: 'center',
        block: 'nearest'
      });
    } catch (err) {
      const targetLeft = targetCard.offsetLeft - 16;
      grid.scrollTo({ left: targetLeft, behavior: 'smooth' });
    }

    setActiveDot(idx);
  }

  function bindAction(el, action) {
    if (!el) return;
    let lastTime = 0;
    const handler = (e) => {
      const now = Date.now();
      if (now - lastTime < 250) return;
      lastTime = now;
      if (e && e.cancelable) {
        e.preventDefault();
      }
      action();
    };
    el.addEventListener('click', handler);
    el.addEventListener('touchend', handler);
  }

  dots.forEach((dot, idx) => {
    bindAction(dot, () => scrollToCard(idx));
  });

  if (prevBtn) {
    bindAction(prevBtn, () => scrollToCard(activeIndex - 1));
  }
  if (nextBtn) {
    bindAction(nextBtn, () => scrollToCard(activeIndex + 1));
  }

  // Active dot tracking with IntersectionObserver for rock-solid sync on all mobile devices
  if ('IntersectionObserver' in window) {
    const observer = new IntersectionObserver((entries) => {
      entries.forEach(entry => {
        if (entry.isIntersecting) {
          const idx = parseInt(entry.target.getAttribute('data-index') || '0', 10);
          setActiveDot(idx);
        }
      });
    }, {
      root: grid,
      threshold: 0.55
    });

    cards.forEach(card => observer.observe(card));
  } else {
    let scrollTimeout = null;
    grid.addEventListener('scroll', () => {
      if (scrollTimeout) return;
      scrollTimeout = requestAnimationFrame(() => {
        scrollTimeout = null;
        const scrollLeft = grid.scrollLeft;
        const cardWidth = (cards[0].offsetWidth || 280) + 14;
        const newIndex = Math.round(scrollLeft / cardWidth);
        setActiveDot(newIndex);
      });
    }, { passive: true });
  }
}





// ==========================================================================
// 4. Seamless Audio Player (Single Track + Reactive Amplitude Waveform)
// ==========================================================================
function initMinimalAudioPlayer() {
  const audio = document.getElementById('m-audio-element');
  const playBtn = document.getElementById('minimal-play-btn');
  const playIcon = document.getElementById('m-play-icon');
  const pauseIcon = document.getElementById('m-pause-icon');
  const waveCanvas = document.getElementById('waveform-canvas');
  const waveBox = document.getElementById('waveform-box');

  if (!audio || !playBtn || !waveCanvas) return;

  const ctx = waveCanvas.getContext('2d');
  let audioCtx = null;
  let analyser = null;
  let dataArray = null;
  let isWebAudioInitialized = false;

  const barCount = 52;
  const baseProfile = [];
  for (let i = 0; i < barCount; i++) {
    const u = i / barCount;
    const env = Math.sin(u * Math.PI) * 0.75 + 0.25;
    const detail = (Math.sin(u * 14.0) * 0.2 + Math.cos(u * 28.0) * 0.15 + Math.sin(u * 44.0) * 0.1);
    baseProfile.push(Math.max(0.15, Math.min(0.95, env + detail)));
  }

  let hoverPct = -1;

  function initWebAudio() {
    if (isWebAudioInitialized) return;
    try {
      const AudioContextClass = window.AudioContext || window.webkitAudioContext;
      if (!AudioContextClass) return;
      audioCtx = new AudioContextClass();
      analyser = audioCtx.createAnalyser();
      analyser.fftSize = 128;
      analyser.smoothingTimeConstant = 0.8;
      dataArray = new Uint8Array(analyser.frequencyBinCount);

      const source = audioCtx.createMediaElementSource(audio);
      source.connect(analyser);
      analyser.connect(audioCtx.destination);
      isWebAudioInitialized = true;
    } catch (e) {
      isWebAudioInitialized = false;
    }
  }

  function resizeCanvas() {
    if (!waveBox) return;
    const rect = waveBox.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    waveCanvas.width = (rect.width || 500) * dpr;
    waveCanvas.height = (rect.height || 48) * dpr;
  }
  resizeCanvas();
  window.addEventListener('resize', resizeCanvas);

  function togglePlay() {
    initWebAudio();
    if (audioCtx && audioCtx.state === 'suspended') {
      audioCtx.resume();
    }

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

  audio.addEventListener('ended', () => {
    playIcon.style.display = 'block';
    pauseIcon.style.display = 'none';
  });

  // Click & Hover on Waveform to Seek
  if (waveBox) {
    waveBox.addEventListener('click', (e) => {
      const rect = waveBox.getBoundingClientRect();
      const clickX = e.clientX - rect.left;
      const pct = Math.max(0, Math.min(1, clickX / rect.width));
      if (audio.duration) {
        audio.currentTime = pct * audio.duration;
      }
    });

    waveBox.addEventListener('mousemove', (e) => {
      const rect = waveBox.getBoundingClientRect();
      hoverPct = (e.clientX - rect.left) / rect.width;
    });

    waveBox.addEventListener('mouseleave', () => {
      hoverPct = -1;
    });
  }

  let waveTime = 0;
  function renderWaveform() {
    waveTime += 0.035;
    const w = waveCanvas.width;
    const h = waveCanvas.height;
    const dpr = window.devicePixelRatio || 1;

    ctx.clearRect(0, 0, w, h);

    const isPlaying = !audio.paused;
    let progressPct = 0;
    if (audio.duration) {
      progressPct = audio.currentTime / audio.duration;
    }

    const barW = (w / barCount) * 0.58;
    const gap = (w / barCount) * 0.42;
    const centerY = h * 0.5;

    for (let i = 0; i < barCount; i++) {
      const barU = i / (barCount - 1);
      const x = i * (barW + gap) + gap * 0.5;

      let rawAmp = baseProfile[i];

      if (isPlaying) {
        if (analyser && dataArray) {
          const binIdx = Math.floor(barU * (dataArray.length * 0.75));
          const binVal = (dataArray[binIdx] || 0) / 255;
          rawAmp = rawAmp * 0.45 + binVal * 0.65;
        } else {
          const pulse1 = Math.sin(waveTime * 3.5 + i * 0.4) * 0.28;
          const pulse2 = Math.cos(waveTime * 5.2 - i * 0.6) * 0.18;
          rawAmp = Math.max(0.18, Math.min(1.0, rawAmp + pulse1 + pulse2));
        }
      } else {
        const idle = Math.sin(waveTime * 0.8 + i * 0.2) * 0.06;
        rawAmp = Math.max(0.12, rawAmp * 0.75 + idle);
      }

      const barH = Math.max(4 * dpr, rawAmp * (h * 0.88));
      const y = centerY - barH * 0.5;
      const radius = Math.min(barW * 0.5, 3 * dpr);

      const isPlayed = barU <= progressPct;
      const isHoverScrub = hoverPct >= 0 && barU <= hoverPct;

      if (isPlayed) {
        const grad = ctx.createLinearGradient(0, y, 0, y + barH);
        grad.addColorStop(0, '#c2593f');
        grad.addColorStop(1, '#d97736');
        ctx.fillStyle = grad;
      } else if (isHoverScrub) {
        ctx.fillStyle = 'rgba(194, 89, 63, 0.45)';
      } else {
        ctx.fillStyle = 'rgba(45, 35, 25, 0.16)';
      }

      ctx.beginPath();
      if (ctx.roundRect) {
        ctx.roundRect(x, y, barW, barH, radius);
      } else {
        ctx.rect(x, y, barW, barH);
      }
      ctx.fill();
    }

    if (progressPct > 0 && progressPct <= 1) {
      const playheadX = progressPct * w;
      ctx.beginPath();
      ctx.arc(playheadX, centerY, 3.5 * dpr, 0, Math.PI * 2);
      ctx.fillStyle = '#c2593f';
      ctx.fill();
    }

    if (hoverPct >= 0 && hoverPct <= 1) {
      const scrubX = hoverPct * w;
      ctx.beginPath();
      ctx.moveTo(scrubX, 2 * dpr);
      ctx.lineTo(scrubX, h - 2 * dpr);
      ctx.strokeStyle = 'rgba(194, 89, 63, 0.5)';
      ctx.lineWidth = 1.2 * dpr;
      ctx.setLineDash([3 * dpr, 3 * dpr]);
      ctx.stroke();
      ctx.setLineDash([]);
    }

    requestAnimationFrame(renderWaveform);
  }
  requestAnimationFrame(renderWaveform);
}

// Recipes Show More / Show Less Toggle
function initRecipesToggle() {
  const btn = document.getElementById('toggle-recipes-btn');
  const extraRecipes = document.getElementById('extra-recipes');
  if (!btn || !extraRecipes) return;

  btn.addEventListener('click', () => {
    const isHidden = extraRecipes.classList.contains('hidden');
    if (isHidden) {
      extraRecipes.classList.remove('hidden');
      const span = btn.querySelector('span');
      if (span) span.textContent = 'Show Fewer Recipes';
    } else {
      extraRecipes.classList.add('hidden');
      const span = btn.querySelector('span');
      if (span) span.textContent = 'Show More Recipes (3)';
    }
  });
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
  let resizeTimer = null;
  window.addEventListener('resize', () => {
    clearTimeout(resizeTimer);
    resizeTimer = setTimeout(() => {
      resizeCosmicCanvas();
      resizeBranchCanvas();
    }, 100);
  }, { passive: true });

  resizeCosmicCanvas();
  createViewportLoop(cosmicCanvas, animateCosmos);

  resizeBranchCanvas();
  createViewportLoop(natureCanvas, animateNatureBranches);

  const natureWrapper = document.querySelector('.nature-canvas-wrapper');
  if (natureWrapper && 'ResizeObserver' in window) {
    let lastW = 0, lastH = 0;
    const ro = new ResizeObserver(entries => {
      const { width, height } = entries[0].contentRect;
      if (Math.abs(width - lastW) > 10 || Math.abs(height - lastH) > 10) {
        lastW = width; lastH = height;
        resizeBranchCanvas();
      }
    });
    ro.observe(natureWrapper);
  }
  if (document.fonts && document.fonts.ready) {
    document.fonts.ready.then(resizeBranchCanvas);
  }
  window.addEventListener('load', resizeBranchCanvas, { passive: true });

  // Initialize the 4 primary capabilities animations
  initWavesAnimation();
  initWavetableAnimation();
  initParticlesAnimation();
  initKnobsAnimation();
  initMobileCapabilitiesSlider();

  initMinimalAudioPlayer();
  initRecipesToggle();
  initTickerTapes();
  initModal();
  initCopyButtons();
});
