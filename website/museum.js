// Infinite — Art Gallery museum.
// One set of ten framed pieces, three rooms (hall / rotunda / wall). Switching rooms re-hangs the
// same frames along arcs with a stagger, so the pieces stay the same objects, not a cut.
import * as THREE from 'three';

const $ = (id) => document.getElementById(id);
const reduceMotion = matchMedia('(prefers-reduced-motion: reduce)').matches;
const coarse = matchMedia('(pointer: coarse)').matches;

// ---------------------------------------------------------------------------------------------
// Pieces. Edit titles / years here. `file` is the number of the file in assets/museum.
// ---------------------------------------------------------------------------------------------
const WORKS = [
  { file: '01', kind: 'video', w: 1.2, h: 1.6, title: 'Reservoir',  meta: 'Infinite, 2026 · 4 s loop' },
  { file: '02', kind: 'video', w: 1.2, h: 1.6, title: 'Mirror',     meta: 'Infinite, 2026 · 10 s loop' },
  { file: '06', kind: 'video', w: 1.2, h: 1.6, title: 'Spore',      meta: 'Infinite, 2026 · 10 s loop' },
  { file: '04', kind: 'video', w: 1.2, h: 1.6, title: 'Meadow',     meta: 'Infinite, 2026 · 10 s loop' },
  { file: '05', kind: 'video', w: 1.2, h: 1.6, title: 'Siren',      meta: 'Infinite, 2026 · 15 s loop' },
  { file: '07', kind: 'video', w: 1.2, h: 1.6, title: 'Chrome',     meta: 'Infinite, 2026 · 4 s loop' },
  { file: '08', kind: 'video', w: 1.2, h: 1.6, title: 'Halftone',   meta: 'Infinite, 2026 · 15 s loop' },
  { file: '09', kind: 'still', w: 1.2, h: 1.6, title: 'Eclipse',    meta: 'Infinite, 2026 · still' },
  { file: '10', kind: 'video', w: 1.2, h: 1.6, title: 'Distortion', meta: 'Infinite, 2026 · 11 s loop' },
];
const N = WORKS.length;
const EYE = 1.65;           // camera height (m)
const ART_Y = 1.65;         // centre of every piece (m)
const POOL = coarse ? 3 : 6; // simultaneous playing videos

// ---------------------------------------------------------------------------------------------
// Math helpers
// ---------------------------------------------------------------------------------------------
const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
const lerp = (a, b, t) => a + (b - a) * t;
const wrapPi = (a) => { a = (a + Math.PI) % (Math.PI * 2); if (a < 0) a += Math.PI * 2; return a - Math.PI; };
const easeIO = (t) => (t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2);
const damp = (cur, tgt, lambda, dt) => lerp(cur, tgt, 1 - Math.exp(-lambda * dt));
// Critically damped spring (smoothDamp): smooth start and stop, never overshoots, interruptible.
function smooth(cur, tgt, st, key, time, dt) {
  const omega = 2 / Math.max(0.0001, time), x = omega * dt;
  const ex = 1 / (1 + x + 0.48 * x * x + 0.235 * x * x * x);
  const change = cur - tgt, tmp = ((st[key] || 0) + omega * change) * dt;
  st[key] = ((st[key] || 0) - omega * tmp) * ex;
  return tgt + (change + tmp) * ex;
}
const soft = (v, lo, hi, k = 0.35) => (v < lo ? lo + (v - lo) * k : v > hi ? hi + (v - hi) * k : v); // rubber band

// ---------------------------------------------------------------------------------------------
// Renderer / scene
// ---------------------------------------------------------------------------------------------
const canvas = $('stage');
let renderer;
try {
  renderer = new THREE.WebGLRenderer({ canvas, antialias: true, powerPreference: 'high-performance' });
} catch (e) {
  document.body.classList.remove('is-loading');
  document.body.classList.add('nogl');
  throw e;
}
renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
renderer.outputColorSpace = THREE.SRGBColorSpace;

const BG = new THREE.Color('#EFEBE0');
const scene = new THREE.Scene();
scene.background = BG;
scene.fog = new THREE.Fog(BG, 9, 26);

const camera = new THREE.PerspectiveCamera(50, 1, 0.05, 80);
camera.rotation.order = 'YXZ';

scene.add(new THREE.HemisphereLight(0xfff8ee, 0xf0e8d8, 2.15));
const sun = new THREE.DirectionalLight(0xfff1dc, 1.1);
sun.position.set(-2, 6, 3);
scene.add(sun);

const maxAniso = renderer.capabilities.getMaxAnisotropy();

function canvasTex(w, h, draw, { srgb = true, repeat } = {}) {
  const c = document.createElement('canvas');
  c.width = w; c.height = h;
  draw(c.getContext('2d'), w, h);
  const t = new THREE.CanvasTexture(c);
  if (srgb) t.colorSpace = THREE.SRGBColorSpace;
  t.anisotropy = maxAniso;
  if (repeat) { t.wrapS = t.wrapT = THREE.RepeatWrapping; t.repeat.set(repeat[0], repeat[1]); }
  return t;
}

// ---------------------------------------------------------------------------------------------
// Environment: floor + three rooms. Each room has a fade value so rooms dissolve into each other.
// ---------------------------------------------------------------------------------------------
const WALL_C = '#F3EFE6';

// notebook graph-paper: one tile = 5 x 5 cells = 2 m, so a cell is 40 cm
const GRID_TILE = 2;
const gridBase = canvasTex(640, 640, (g, w, h) => {
  g.fillStyle = '#ffffff'; g.fillRect(0, 0, w, h);
  for (let k = 0; k < 5; k++) {
    const major = k === 0;
    g.strokeStyle = major ? 'rgba(96,126,170,0.34)' : 'rgba(96,126,170,0.17)';
    g.lineWidth = major ? 3 : 2;
    const p = k * 128 + (major ? 1.5 : 1);
    g.beginPath(); g.moveTo(p, 0); g.lineTo(p, h); g.moveTo(0, p); g.lineTo(w, p); g.stroke();
  }
});
gridBase.wrapS = gridBase.wrapT = THREE.RepeatWrapping;
function gridMap(w, h) { // a scaled plane needs its own repeat so cells stay square
  const t = gridBase.clone(); t.needsUpdate = true;
  t.wrapS = t.wrapT = THREE.RepeatWrapping; t.repeat.set(w / GRID_TILE, h / GRID_TILE);
  return t;
}
const wallMat = () => new THREE.MeshLambertMaterial({ color: WALL_C, map: gridMap(1, 1), transparent: true });
const setGrid = (mesh, w, h) => { mesh.material.map.repeat.set(w / GRID_TILE, h / GRID_TILE); };
const floorTex = gridMap(72, 72);
const floorMat = new THREE.MeshStandardMaterial({ color: '#E4E0D6', map: floorTex, roughness: 0.45, metalness: 0, transparent: true, opacity: 0.86 });
const floor = new THREE.Mesh(new THREE.PlaneGeometry(72, 72), floorMat);
floor.rotation.x = -Math.PI / 2;
floor.renderOrder = 2;
scene.add(floor);

const rooms = { hall: new THREE.Group(), rotunda: new THREE.Group(), wall: new THREE.Group() };
Object.values(rooms).forEach((g) => scene.add(g));
const roomFade = { hall: 0, rotunda: 0, wall: 0 };
let floorFade = 1;

// --- hall (dimensions are set in layoutParams) ---
const hallLeft = new THREE.Mesh(new THREE.PlaneGeometry(1, 5), wallMat());
const hallRight = new THREE.Mesh(new THREE.PlaneGeometry(1, 5), wallMat());
const hallEnd = new THREE.Mesh(new THREE.PlaneGeometry(1, 5), wallMat());
const hallStart = new THREE.Mesh(new THREE.PlaneGeometry(1, 5), wallMat());
const hallCeil = new THREE.Mesh(new THREE.PlaneGeometry(1, 1), new THREE.MeshLambertMaterial({ color: '#F1ECE0', map: gridMap(1, 1), transparent: true }));
const panelMat = new THREE.MeshBasicMaterial({ color: '#FFFDF6', transparent: true, fog: false });
const hallPanels = Array.from({ length: 9 }, () => new THREE.Mesh(new THREE.PlaneGeometry(1, 1), panelMat));
rooms.hall.add(hallLeft, hallRight, hallEnd, hallStart, hallCeil, ...hallPanels);

// --- rotunda ---
const ROT_R = 3.7;
const rotWall = new THREE.Mesh(new THREE.CylinderGeometry(ROT_R + 0.55, ROT_R + 0.55, 5.4, 96, 1, true), new THREE.MeshLambertMaterial({ color: WALL_C, side: THREE.BackSide, transparent: true }));
rotWall.position.y = 2.7;
const rotCeil = new THREE.Mesh(new THREE.RingGeometry(1.5, ROT_R + 0.55, 96), new THREE.MeshLambertMaterial({ color: '#F1ECE0', side: THREE.DoubleSide, transparent: true }));
rotCeil.rotation.x = Math.PI / 2; rotCeil.position.y = 5.4;
const rotHalo = new THREE.Mesh(new THREE.RingGeometry(1.5, 1.9, 96), new THREE.MeshBasicMaterial({ color: '#FFFDF6', side: THREE.DoubleSide, transparent: true, fog: false }));
rotHalo.rotation.x = Math.PI / 2; rotHalo.position.y = 5.39;
rooms.rotunda.add(rotWall, rotCeil, rotHalo);

// --- wall ---
const bigWall = new THREE.Mesh(new THREE.PlaneGeometry(90, 16), wallMat());
rooms.wall.add(bigWall);

const fadables = [];
function collectFadables() {
  Object.entries(rooms).forEach(([name, g]) => g.traverse((o) => {
    if (o.material) fadables.push({ room: name, mat: o.material, base: o.material.opacity ?? 1, obj: o });
  }));
}
collectFadables();

// ---------------------------------------------------------------------------------------------
// Pieces
// ---------------------------------------------------------------------------------------------
const shadowTex = canvasTex(256, 256, (g, w, h) => {
  g.shadowColor = 'rgba(40,28,14,0.55)'; g.shadowBlur = 38;
  g.shadowOffsetX = 2000; g.shadowOffsetY = 2000;
  g.fillStyle = '#000';
  g.fillRect(70 - 2000, 70 - 2000, w - 140, h - 140);
});
const washTex = canvasTex(256, 256, (g, w, h) => {
  const r = g.createRadialGradient(w / 2, h / 2, 0, w / 2, h / 2, w / 2);
  r.addColorStop(0, 'rgba(255,252,244,0.85)'); r.addColorStop(0.5, 'rgba(255,250,240,0.32)'); r.addColorStop(1, 'rgba(255,250,240,0)');
  g.fillStyle = r; g.fillRect(0, 0, w, h);
});
// canvas top = v 1 (art top, deepest under the floor) fades out; canvas bottom = v 0 (nearest the floor) is solid
const reflAlphaFixed = canvasTex(4, 128, (g, w, h) => {
  const gr = g.createLinearGradient(0, 0, 0, h);
  gr.addColorStop(0, '#000'); gr.addColorStop(1, '#fff');
  g.fillStyle = gr; g.fillRect(0, 0, w, h);
}, { srgb: false });

function plaqueTex(no, title, meta) {
  return canvasTex(640, 240, (g, w, h) => {
    g.fillStyle = '#FBFAF6'; g.fillRect(0, 0, w, h);
    g.strokeStyle = 'rgba(45,35,25,0.14)'; g.lineWidth = 4; g.strokeRect(2, 2, w - 4, h - 4);
    g.textBaseline = 'alphabetic';
    g.fillStyle = '#F57F66'; g.font = '500 34px "Geist Mono", ui-monospace, monospace';
    g.fillText(`NO. ${no}`, 34, 62);
    g.fillStyle = '#1F1D1A'; g.font = '700 92px "Caveat", cursive';
    g.fillText(title, 34, 146);
    g.fillStyle = '#857C74'; g.font = '400 30px "Geist Mono", ui-monospace, monospace';
    g.fillText(meta, 34, 196);
  });
}

const loader = new THREE.TextureLoader();
const artGroup = new THREE.Group();
scene.add(artGroup);
const reflGroup = new THREE.Group();
scene.add(reflGroup);
const hitMeshes = [];
const frameMat = new THREE.MeshLambertMaterial({ color: '#1A1816' });
const planeGeo = new THREE.PlaneGeometry(1, 1);
const boxGeo = new THREE.BoxGeometry(1, 1, 1);

const pieces = WORKS.map((w, i) => {
  const no = String(i + 1).padStart(2, '0');
  const g = new THREE.Group();
  const shadow = new THREE.Mesh(planeGeo, new THREE.MeshBasicMaterial({ map: shadowTex, transparent: true, depthWrite: false, opacity: 0.6 }));
  shadow.scale.set(w.w * 1.5, w.h * 1.4, 1); shadow.position.set(0, -0.06, -0.03);
  const wash = new THREE.Mesh(planeGeo, new THREE.MeshBasicMaterial({ map: washTex, transparent: true, depthWrite: false, fog: false }));
  wash.scale.set(w.w * 3.4, w.h * 2.8, 1); wash.position.set(0, 0, -0.04);
  const frame = new THREE.Mesh(boxGeo, frameMat);
  frame.scale.set(w.w + 0.06, w.h + 0.06, 0.06); frame.position.z = 0;
  const artMat = new THREE.MeshBasicMaterial({ color: 0xffffff, toneMapped: false });
  const art = new THREE.Mesh(planeGeo, artMat);
  art.scale.set(w.w, w.h, 1); art.position.z = 0.032;
  art.userData.index = i;
  const plq = new THREE.Mesh(planeGeo, new THREE.MeshBasicMaterial({ transparent: true }));
  const pw = 0.56; plq.scale.set(pw, pw * 240 / 640, 1);
  plq.position.set(-w.w / 2 + pw / 2 + 0.02, -w.h / 2 - 0.2, 0.01);
  g.add(shadow, wash, frame, art, plq);
  artGroup.add(g);
  hitMeshes.push(art);

  // reflection ghost on the floor
  const reflMat = new THREE.MeshBasicMaterial({ color: 0xffffff, toneMapped: false, transparent: true, opacity: 0.3, alphaMap: reflAlphaFixed, depthWrite: false });
  const refl = new THREE.Mesh(planeGeo, reflMat);
  refl.scale.set(w.w, -w.h, 1);
  const rg = new THREE.Group(); rg.add(refl); refl.renderOrder = 1;
  reflGroup.add(rg);

  const p = {
    i, def: w, group: g, art, artMat, reflMat, rg, plq, wash,
    cur: { x: 0, y: ART_Y, z: 0, ry: 0 }, from: null, to: { x: 0, y: ART_Y, z: 0, ry: 0 },
    t0: 0, delay: 0, dur: 1, arc: 0, hover: 0, poster: null, slot: null, ready: false,
  };
  p.setMap = (tex) => { artMat.map = tex; artMat.needsUpdate = true; reflMat.map = tex; reflMat.needsUpdate = true; };
  p.plaqueNo = no;
  return p;
});

// ---------------------------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------------------------
const view = { aspect: 1, portrait: false, fov: 50, W: 2.5, S: 3.2 };
const hall = { zStart: 2.2, max: 14, lastZ: -14 };
function hallPositions() {
  const { W, S } = view;
  return pieces.map((p, i) => {
    const side = i % 2 === 0 ? -1 : 1;
    const row = Math.floor(i / 2);
    const z = -(2.4 + row * S) - (side > 0 ? S / 2 : 0);
    return { x: side * (W - 0.0), y: ART_Y, z, ry: side < 0 ? Math.PI / 2 : -Math.PI / 2 };
  });
}
function rotundaPositions() {
  return pieces.map((p, i) => {
    const th = (i / N) * Math.PI * 2;
    return { x: Math.sin(th) * (ROT_R + 0.1), y: ART_Y, z: -Math.cos(th) * (ROT_R + 0.1), ry: -th };
  });
}
const WALL_Z = -5;
function wallPositions() {
  // salon hang: two rows, second row offset by half a column
  const colW = 2.05;
  const per = Math.ceil(N / 2);
  return pieces.map((p, i) => {
    const row = i % 2;
    const col = Math.floor(i / 2);
    const x = (col - (per - 1) / 2) * colW + (row ? colW / 2 : 0) - colW / 4;
    const y = row ? 0.86 : 3.08;
    return { x, y, z: WALL_Z, ry: 0 };
  });
}
const wallBounds = () => {
  const xs = wallPositions().map((p) => p.x);
  return { min: Math.min(...xs), max: Math.max(...xs) };
};
const LAYOUTS = { hall: hallPositions, rotunda: rotundaPositions, wall: wallPositions };

function applyViewParams() {
  view.aspect = innerWidth / innerHeight;
  view.portrait = view.aspect < 1;
  const a = view.aspect;
  view.fov = a >= 1.2 ? 50 : lerp(80, 50, clamp((a - 0.45) / 0.75, 0, 1));
  view.W = a >= 1.2 ? 2.5 : lerp(1.55, 2.5, clamp((a - 0.45) / 0.75, 0, 1));
  view.S = 3.2;
  camera.aspect = a; camera.fov = view.fov; camera.updateProjectionMatrix();
  renderer.setSize(innerWidth, innerHeight, false);

  // hall geometry
  const hp = hallPositions();
  hall.lastZ = Math.min(...hp.map((p) => p.z));
  const zFront = hall.zStart + 8, zBack = hall.lastZ - 3.4, len = zFront - zBack, cz = (zFront + zBack) / 2;
  hallLeft.scale.set(len, 1, 1); hallLeft.rotation.y = Math.PI / 2; hallLeft.position.set(-view.W - 0.08, 2.5, cz);
  hallRight.scale.set(len, 1, 1); hallRight.rotation.y = -Math.PI / 2; hallRight.position.set(view.W + 0.08, 2.5, cz);
  hallEnd.scale.set(view.W * 2 + 0.3, 1, 1); hallEnd.position.set(0, 2.5, zBack);
  hallStart.scale.set(view.W * 2 + 0.3, 1, 1); hallStart.rotation.y = Math.PI; hallStart.position.set(0, 2.5, zFront);
  hallCeil.scale.set(view.W * 2 + 0.3, len, 1); hallCeil.rotation.x = Math.PI / 2; hallCeil.position.set(0, 4.6, cz);
  setGrid(hallLeft, len, 5); setGrid(hallRight, len, 5);
  setGrid(hallEnd, view.W * 2 + 0.3, 5); setGrid(hallStart, view.W * 2 + 0.3, 5);
  setGrid(hallCeil, view.W * 2 + 0.3, len);
  hallPanels.forEach((m, k) => {
    m.rotation.x = Math.PI / 2; m.scale.set(1.1, len / 9 * 0.55, 1);
    m.position.set(0, 4.59, cz - len / 2 + (k + 0.5) * (len / 9));
  });
  hall.max = hall.zStart - (hall.lastZ - 1.0);

  bigWall.position.set(0, 3, WALL_Z - 0.06);
  if (mode === 'wall') layout('wall', false);
  else if (mode === 'hall') layout('hall', false);
}

function layout(m, animate = true) {
  const to = LAYOUTS[m]();
  const now = performance.now() / 1000;
  pieces.forEach((p, i) => {
    p.to = to[i];
    if (!animate || reduceMotion) { p.cur = { ...to[i] }; p.from = null; return; }
    p.from = { ...p.cur };
    p.t0 = now;
    p.delay = i * 0.045;
    p.dur = 1.15;
    p.arc = 0.35;
  });
}

function updatePieces(now) {
  for (const p of pieces) {
    if (p.from) {
      const t = clamp((now - p.t0 - p.delay) / p.dur, 0, 1);
      const e = easeIO(t);
      p.cur.x = lerp(p.from.x, p.to.x, e);
      p.cur.y = lerp(p.from.y, p.to.y, e) + Math.sin(Math.PI * e) * p.arc;
      p.cur.z = lerp(p.from.z, p.to.z, e);
      p.cur.ry = p.from.ry + wrapPi(p.to.ry - p.from.ry) * e;
      if (t >= 1) { p.from = null; p.cur = { ...p.to }; }
    }
    const nx = Math.sin(p.cur.ry), nz = Math.cos(p.cur.ry); // outward normal of the art plane
    const lift = p.hover * 0.06;
    p.group.position.set(p.cur.x + nx * lift, p.cur.y, p.cur.z + nz * lift);
    p.group.rotation.y = p.cur.ry;
    const s = 1 + p.hover * 0.018;
    p.group.scale.set(s, s, 1);
    p.rg.position.set(p.cur.x + nx * lift, -p.cur.y, p.cur.z + nz * lift);
    p.rg.rotation.y = p.cur.ry;
    p.rg.position.x += nx * 0.04; p.rg.position.z += nz * 0.04;
  }
}

// ---------------------------------------------------------------------------------------------
// Video pool: only the pieces you can see are decoding; everyone else shows the poster frame.
// ---------------------------------------------------------------------------------------------
const slots = Array.from({ length: POOL }, () => {
  const v = document.createElement('video');
  v.muted = true; v.loop = true; v.playsInline = true; v.preload = 'auto'; v.crossOrigin = 'anonymous';
  v.setAttribute('playsinline', ''); v.setAttribute('muted', '');
  const tex = new THREE.VideoTexture(v);
  tex.colorSpace = THREE.SRGBColorSpace;
  tex.minFilter = THREE.LinearFilter; tex.generateMipmaps = false;
  const slot = { v, tex, piece: null };
  v.addEventListener('playing', () => { if (slot.piece && slot.piece.slot === slot) slot.piece.setMap(tex); });
  return slot;
});

function release(slot) {
  const p = slot.piece;
  if (p) { p.setMap(p.poster); p.slot = null; }
  slot.piece = null;
  slot.v.pause(); slot.v.removeAttribute('src'); slot.v.load();
}
function assign(slot, p) {
  slot.piece = p; p.slot = slot;
  slot.v.src = `assets/museum/${p.def.file}.mp4`;
  const play = slot.v.play(); if (play) play.catch(() => {});
}

const frustum = new THREE.Frustum(), pv = new THREE.Matrix4(), wp = new THREE.Vector3(), sphere = new THREE.Sphere();
let lastPick = 0;
function pickLive(now, force) {
  if (now - lastPick < 0.25 && !force) return;
  lastPick = now;
  pv.multiplyMatrices(camera.projectionMatrix, camera.matrixWorldInverse);
  frustum.setFromProjectionMatrix(pv);
  const cands = [];
  for (const p of pieces) {
    if (p.def.kind !== 'video') continue;
    p.art.getWorldPosition(wp);
    sphere.set(wp, Math.max(p.def.w, p.def.h) * 0.8);
    if (!frustum.intersectsSphere(sphere)) continue;
    const d = wp.distanceTo(camera.position);
    if (d > 16) continue;
    cands.push({ p, d: focusIdx === p.i ? -1 : d });
  }
  cands.sort((a, b) => a.d - b.d);
  const want = new Set(cands.slice(0, POOL).map((c) => c.p));
  for (const s of slots) if (s.piece && !want.has(s.piece)) release(s);
  for (const p of want) {
    if (p.slot) continue;
    const free = slots.find((s) => !s.piece);
    if (free) assign(free, p);
  }
}

// ---------------------------------------------------------------------------------------------
// Camera state per mode
// ---------------------------------------------------------------------------------------------
let mode = 'hall';
let washK = 1;
let focusIdx = -1;
const S = {
  hall: { pos: 0, tgt: 0, vel: 0, yaw: 0, yawT: 0, pitch: 0, pitchT: 0 },
  rotunda: { yaw: 0, tgt: 0, vel: 0, pitch: 0, pitchT: 0 },
  wall: { x: 0, xT: 0, vx: 0, y: 0, yT: 0, dist: 7, distT: 7 },
};
const cam = { x: 0, y: EYE, z: 8, yaw: 0, pitch: 0, st: {} };
let camTime = 0.7;       // spring time; relaxes after a transition
let lastInput = 0;
const ptr = { x: 0, y: 0, nx: 0, ny: 0 }; // pointer, normalised, for gentle parallax

function wallDistFit() {
  const t = Math.tan((view.fov * Math.PI) / 360);
  return clamp(2.35 / t, 3.4, 9.5);
}
function wallClampX(d) {
  const b = wallBounds();
  const halfW = Math.tan((view.fov * Math.PI) / 360) * d * view.aspect;
  const pad = 0.9;
  const lo = b.min + Math.min(halfW - pad, 0), hi = b.max - Math.min(halfW - pad, 0);
  return halfW - pad > (b.max - b.min) / 2 ? [0, 0] : [lo, hi];
}

// Screen space the close-up UI occupies: top chips, bottom plaque + buttons. The piece is fitted into what is left.
function focusReserve() {
  const small = innerWidth <= 640;
  return { top: small ? 78 : 76, bottom: small ? 218 : 128 };
}
function focusPose(i) {
  const p = pieces[i], t = Math.tan((view.fov * Math.PI) / 360);
  const { top, bottom } = focusReserve();
  const free = Math.max(120, innerHeight - top - bottom);
  const fy = (free / innerHeight) * 0.94;                       // share of the screen height the piece may use
  const dH = p.def.h / (2 * t * fy), dW = p.def.w / (2 * t * view.aspect * 0.9);
  const d = Math.max(dH, dW, 1.1);
  const lift = ((bottom - top) / 2) * (2 * t * d / innerHeight);  // lower the camera so the piece rises into the free band
  const nx = Math.sin(p.to.ry), nz = Math.cos(p.to.ry);
  return { x: p.to.x + nx * d, y: p.to.y - lift, z: p.to.z + nz * d, yaw: Math.atan2(nx, nz), pitch: 0 };
}

function basePose() {
  if (mode === 'hall') {
    const h = S.hall;
    return { x: 0, y: EYE, z: hall.zStart - h.pos, yaw: h.yaw, pitch: h.pitch };
  }
  if (mode === 'rotunda') {
    const r = S.rotunda;
    return { x: 0, y: EYE, z: 0, yaw: r.yaw, pitch: r.pitch };
  }
  const w = S.wall;
  return { x: w.x, y: 1.85 + w.y, z: WALL_Z + w.dist, yaw: 0, pitch: 0 };
}

function nearestIndex() {
  const f = new THREE.Vector3(-Math.sin(cam.yaw), 0, -Math.cos(cam.yaw));
  let best = 0, bd = 1e9;
  pieces.forEach((p, i) => {
    const dx = p.to.x - cam.x, dz = p.to.z - cam.z;
    const d = Math.hypot(dx, dz), front = (dx * f.x + dz * f.z) / (d || 1);
    const score = d + (front < 0 ? 6 : 0) + (1 - front) * 2;
    if (score < bd) { bd = score; best = i; }
  });
  return best;
}

// ---------------------------------------------------------------------------------------------
// Mode + focus
// ---------------------------------------------------------------------------------------------
const hintEl = $('hint');
const HINTS = {
  hall: coarse ? 'Drag up to walk · drag sideways to look · tap a piece' : 'Scroll to walk · drag to look around · click a piece',
  rotunda: coarse ? 'Drag to turn · tap a piece' : 'Drag or scroll to turn · click a piece',
  wall: coarse ? 'Drag to pan · pinch to zoom · tap a piece' : 'Drag to pan · pinch or ctrl-scroll to zoom · click a piece',
};
let hintTimer = 0;
function showHint(text) {
  clearTimeout(hintTimer);
  hintEl.textContent = text; hintEl.classList.add('show');
  hintTimer = setTimeout(() => hintEl.classList.remove('show'), 5200);
}
function hideHint() { clearTimeout(hintTimer); hintEl.classList.remove('show'); }


function setMode(m, { silent = false } = {}) {
  if (m === mode) return;
  const keep = focusIdx >= 0 ? focusIdx : nearestIndex();
  if (focusIdx >= 0) exitFocus(true);
  mode = m;
  // carry the piece you were looking at into the new room, so you arrive facing it
  const p = pieces[keep];
  if (m === 'hall') { S.hall.tgt = S.hall.pos = clamp(hall.zStart - hallPositions()[keep].z - 2.0, 0, hall.max); S.hall.vel = 0; S.hall.yaw = S.hall.yawT = 0; }
  if (m === 'rotunda') { const th = (keep / N) * Math.PI * 2; S.rotunda.yaw = cam.yaw; S.rotunda.tgt = -th + Math.round((cam.yaw + th) / (Math.PI * 2)) * Math.PI * 2; S.rotunda.vel = 0; }
  if (m === 'wall') {
    S.wall.dist = S.wall.distT = wallDistFit();
    const x = wallPositions()[keep].x; const [lo, hi] = wallClampX(S.wall.dist);
    S.wall.x = S.wall.xT = clamp(x, lo, hi); S.wall.y = S.wall.yT = 0;
  }
  layout(m, true);
  camTime = 0.85;
  if (!silent) showHint(HINTS[m]);
}

function enterFocus(i) {
  if (i < 0 || i >= N) return;
  const first = focusIdx < 0;
  focusIdx = i;
  document.body.classList.add('is-focus');
  const p = pieces[i];
  $('plaqueNo').textContent = `No. ${p.plaqueNo}`;
  $('plaqueTitle').textContent = p.def.title;
  $('plaqueMeta').textContent = p.def.meta;
  $('counterNo').textContent = p.plaqueNo;
  camTime = first ? 0.8 : 0.6;
  hideHint();
  pickLive(performance.now() / 1000, true);
}
function exitFocus(quick = false) {
  if (focusIdx < 0) return;
  focusIdx = -1;
  document.body.classList.remove('is-focus');
  camTime = quick ? 0.4 : 0.8;
}
const stepFocus = (d) => { if (focusIdx >= 0) enterFocus((focusIdx + d + N) % N); };

$('prevBtn').addEventListener('click', () => stepFocus(-1));
$('nextBtn').addEventListener('click', () => stepFocus(1));
$('closeBtn').addEventListener('click', () => exitFocus());

// ---------------------------------------------------------------------------------------------
// Gestures: drag, inertia, wheel, trackpad pinch, two-finger pinch, swipe in close-up
// ---------------------------------------------------------------------------------------------
const pointers = new Map();
let drag = null, pinch = null;
const ray = new THREE.Raycaster(), ndc = new THREE.Vector2();

function pickAt(cx, cy) {
  ndc.set((cx / innerWidth) * 2 - 1, -(cy / innerHeight) * 2 + 1);
  ray.setFromCamera(ndc, camera);
  const hit = ray.intersectObjects(hitMeshes, false)[0];
  return hit ? hit.object.userData.index : -1;
}

function dragBy(dx, dy) {
  if (focusIdx >= 0) return;
  if (mode === 'hall') {
    S.hall.tgt += -dy * 0.018;
    S.hall.yawT = clamp(S.hall.yawT + dx * 0.0035, -0.75, 0.75);
  } else if (mode === 'rotunda') {
    S.rotunda.tgt += dx * 0.0042;
    S.rotunda.pitchT = clamp(S.rotunda.pitchT + dy * 0.0014, -0.22, 0.22);
  } else {
    const k = (2 * Math.tan((view.fov * Math.PI) / 360) * S.wall.dist) / innerHeight;
    S.wall.xT += -dx * k; S.wall.yT += dy * k;
  }
}
function flingBy(vx, vy) { // px/s
  if (focusIdx >= 0) return;
  if (mode === 'hall') S.hall.vel = clamp(-vy * 0.018, -14, 14);
  else if (mode === 'rotunda') S.rotunda.vel = clamp(vx * 0.0042, -5, 5);
  else {
    const k = (2 * Math.tan((view.fov * Math.PI) / 360) * S.wall.dist) / innerHeight;
    S.wall.vx = clamp(-vx * k, -18, 18);
  }
}
function scrollBy(dx, dy) {
  if (focusIdx >= 0) return;
  if (mode === 'hall') {
    S.hall.tgt += dy * 0.0075;                                  // wheel / two-finger scroll up-down: walk
    S.hall.yawT = clamp(S.hall.yawT - dx * 0.0016, -0.75, 0.75); // trackpad sideways swipe: look
  }
  else if (mode === 'rotunda') S.rotunda.tgt += (dx + dy) * 0.0016;
  else {
    const k = (2 * Math.tan((view.fov * Math.PI) / 360) * S.wall.dist) / innerHeight;
    S.wall.xT += (dx + dy) * k;
  }
}
function zoomBy(factor) {
  if (mode !== 'wall' || focusIdx >= 0) return;
  S.wall.distT = clamp(S.wall.distT / factor, 3.2, 10);
}

canvas.addEventListener('pointerdown', (e) => {
  canvas.setPointerCapture(e.pointerId);
  pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
  lastInput = performance.now();
  if (pointers.size === 1) {
    drag = { x0: e.clientX, y0: e.clientY, t0: performance.now(), moved: 0, lx: e.clientX, ly: e.clientY, lt: performance.now(), vx: 0, vy: 0, sx: 0, sy: 0 };
    S.hall.vel = 0; S.rotunda.vel = 0; S.wall.vx = 0;
    canvas.classList.add('grabbing');
  } else if (pointers.size === 2) {
    const [a, b] = [...pointers.values()];
    pinch = { d: Math.hypot(a.x - b.x, a.y - b.y) };
    drag = null;
  }
  hideHint();
});
canvas.addEventListener('pointermove', (e) => {
  ptr.x = e.clientX; ptr.y = e.clientY;
  ptr.nx = (e.clientX / innerWidth) * 2 - 1; ptr.ny = (e.clientY / innerHeight) * 2 - 1;
  if (!pointers.has(e.pointerId)) { hoverAt(e.clientX, e.clientY); return; }
  const prev = pointers.get(e.pointerId);
  pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
  lastInput = performance.now();
  if (pinch && pointers.size >= 2) {
    const [a, b] = [...pointers.values()];
    const d = Math.hypot(a.x - b.x, a.y - b.y);
    if (pinch.d > 0) zoomBy(d / pinch.d);
    pinch.d = d;
    return;
  }
  if (!drag) return;
  const dx = e.clientX - prev.x, dy = e.clientY - prev.y, now = performance.now();
  drag.moved += Math.abs(dx) + Math.abs(dy);
  drag.sx += dx; drag.sy += dy;
  const dtm = Math.max(1, now - drag.lt);
  drag.vx = lerp(drag.vx, (dx / dtm) * 1000, 0.45); drag.vy = lerp(drag.vy, (dy / dtm) * 1000, 0.45);
  drag.lt = now;
  dragBy(dx, dy);
});
function endPointer(e) {
  pointers.delete(e.pointerId);
  canvas.classList.remove('grabbing');
  if (pointers.size < 2) pinch = null;
  if (!drag) return;
  const d = drag; drag = null;
  const dur = performance.now() - d.t0;
  if (d.moved < 8 && dur < 500) {            // a tap / click
    const i = pickAt(e.clientX, e.clientY);
    if (focusIdx >= 0) { if (i === focusIdx) return; if (i < 0) exitFocus(); else enterFocus(i); }
    else if (i >= 0) enterFocus(i);
    return;
  }
  if (focusIdx >= 0) {                        // swipe in close-up
    if (Math.abs(d.sx) > 60 && Math.abs(d.sx) > Math.abs(d.sy) * 1.2) stepFocus(d.sx < 0 ? 1 : -1);
    else if (d.sy > 80) exitFocus();
    return;
  }
  if (performance.now() - d.lt < 90) flingBy(d.vx, d.vy);
}
canvas.addEventListener('pointerup', endPointer);
canvas.addEventListener('pointercancel', endPointer);

let wheelCool = 0;
canvas.addEventListener('wheel', (e) => {
  e.preventDefault();
  lastInput = performance.now();
  hideHint();
  const k = e.deltaMode === 1 ? 32 : 1;
  if (focusIdx >= 0) {
    if (performance.now() > wheelCool && Math.abs(e.deltaY * k) > 24) { stepFocus(e.deltaY > 0 ? 1 : -1); wheelCool = performance.now() + 520; }
    return;
  }
  if (e.ctrlKey) { zoomBy(Math.exp(-e.deltaY * 0.012)); return; }
  scrollBy(e.deltaX * k, e.deltaY * k);
}, { passive: false });

let hoverIdx = -1, hoverTick = 0;
function hoverAt(x, y) {
  if (coarse) return;
  const now = performance.now();
  if (now - hoverTick < 40) return; hoverTick = now;
  hoverIdx = pickAt(x, y);
  canvas.classList.toggle('pointing', hoverIdx >= 0);
}
canvas.addEventListener('pointerleave', () => { hoverIdx = -1; canvas.classList.remove('pointing'); });

addEventListener('keydown', (e) => {
  if (e.metaKey || e.ctrlKey || e.altKey) return;
  const k = e.key;
  lastInput = performance.now(); hideHint();
  if (k === 'Escape') { if (focusIdx >= 0) exitFocus(); else location.href = 'index.html'; }
  else if (k === 'Enter' || k === ' ') { if (focusIdx < 0) { e.preventDefault(); enterFocus(nearestIndex()); } }
  else if (focusIdx >= 0) {
    if (k === 'ArrowRight' || k === 'ArrowDown') stepFocus(1); else if (k === 'ArrowLeft' || k === 'ArrowUp') stepFocus(-1);
  } else if (mode === 'hall') {
    const step = e.shiftKey ? 4.8 : 1.6;
    if (k === 'ArrowUp' || k === 'w' || k === 'W' || k === 'PageDown') S.hall.tgt += k === 'PageDown' ? 4.8 : step;
    else if (k === 'ArrowDown' || k === 's' || k === 'S' || k === 'PageUp') S.hall.tgt -= k === 'PageUp' ? 4.8 : step;
    else if (k === 'ArrowLeft' || k === 'a' || k === 'A') S.hall.yawT = clamp(S.hall.yawT + 0.35, -0.75, 0.75);
    else if (k === 'ArrowRight' || k === 'd' || k === 'D') S.hall.yawT = clamp(S.hall.yawT - 0.35, -0.75, 0.75);
    else if (k === 'Home') S.hall.tgt = 0;
    else if (k === 'End') S.hall.tgt = hall.max;
  } else if (mode === 'rotunda') {
    if (k === 'ArrowRight' || k === 'ArrowUp') S.rotunda.tgt -= (Math.PI * 2) / N; else if (k === 'ArrowLeft' || k === 'ArrowDown') S.rotunda.tgt += (Math.PI * 2) / N;
  } else {
    if (k === 'ArrowRight') S.wall.xT += 1.8; else if (k === 'ArrowLeft') S.wall.xT -= 1.8;
    else if (k === '+' || k === '=') zoomBy(1.25); else if (k === '-') zoomBy(0.8);
  }
});

// ---------------------------------------------------------------------------------------------
// Frame loop
// ---------------------------------------------------------------------------------------------
let tPrev = performance.now() / 1000, tNow = 0, raf = 0;
// rAF already idles in background tabs; just stop decoding video while hidden
document.addEventListener('visibilitychange', () => {
  tPrev = performance.now() / 1000;
  slots.forEach((sl) => { if (!sl.piece) return; if (document.hidden) sl.v.pause(); else sl.v.play().catch(() => {}); });
});

function stepModeState(dt) {
  const idle = (performance.now() - lastInput) / 1000;
  const h = S.hall;
  h.tgt += h.vel * dt; h.vel *= Math.exp(-2.6 * dt);
  const lo = 0, hi = hall.max;
  if (!drag) { if (h.tgt < lo) h.tgt = damp(h.tgt, lo, 5, dt); else if (h.tgt > hi) h.tgt = damp(h.tgt, hi, 5, dt); }
  h.pos = smooth(h.pos, soft(h.tgt, lo, hi), h, 'pv', 0.28, dt);
  if (idle > 1.1 && !drag) h.yawT = damp(h.yawT, 0, 1.4, dt);
  h.yaw = damp(h.yaw, h.yawT, 9, dt);
  h.pitchT = damp(h.pitchT, 0, 2, dt); h.pitch = damp(h.pitch, h.pitchT, 9, dt);

  const r = S.rotunda;
  r.tgt += r.vel * dt; r.vel *= Math.exp(-2.4 * dt);
  if (idle > 0.7 && !drag && Math.abs(r.vel) < 0.35 && mode === 'rotunda' && focusIdx < 0) { // settle on the nearest piece
    const step = (Math.PI * 2) / N, k = Math.round(r.tgt / step);
    r.tgt = damp(r.tgt, k * step, 3.2, dt);
  }
  r.yaw = smooth(r.yaw, r.tgt, r, 'yv', 0.3, dt);
  r.pitchT = damp(r.pitchT, 0, 2, dt); r.pitch = damp(r.pitch, r.pitchT, 9, dt);

  const w = S.wall;
  w.xT += w.vx * dt; w.vx *= Math.exp(-2.8 * dt);
  const [lo2, hi2] = wallClampX(w.distT);
  if (!drag) w.xT = damp(w.xT, clamp(w.xT, lo2, hi2), w.xT < lo2 || w.xT > hi2 ? 5 : 0, dt);
  const ylim = Math.max(0, 1.95 - Math.tan((view.fov * Math.PI) / 360) * w.distT);
  w.yT = clamp(w.yT, -ylim - 0.3, ylim + 0.3);
  if (idle > 1.2 && !drag) w.yT = damp(w.yT, clamp(w.yT, -ylim, ylim), 4, dt);
  w.x = smooth(w.x, soft(w.xT, lo2, hi2), w, 'xv', 0.26, dt);
  w.y = smooth(w.y, w.yT, w, 'yv', 0.26, dt);
  w.dist = smooth(w.dist, w.distT, w, 'dv', 0.3, dt);
}

function frame() {
  raf = requestAnimationFrame(frame);
  const nowMs = performance.now(); const now = nowMs / 1000;
  const dt = Math.min(0.05, now - tPrev); tPrev = now; tNow = now;

  stepModeState(dt);

  // desired camera pose
  let want = focusIdx >= 0 ? focusPose(focusIdx) : basePose();
  want.yaw = cam.yaw + wrapPi(want.yaw - cam.yaw);
  camTime = damp(camTime, 0.1, 0.9, dt);
  const ct = Math.max(0.06, camTime);
  cam.x = smooth(cam.x, want.x, cam.st, 'x', ct, dt);
  cam.y = smooth(cam.y, want.y, cam.st, 'y', ct, dt);
  cam.z = smooth(cam.z, want.z, cam.st, 'z', ct, dt);
  cam.yaw = smooth(cam.yaw, want.yaw, cam.st, 'yaw', ct, dt);
  cam.pitch = smooth(cam.pitch, want.pitch, cam.st, 'pitch', ct, dt);

  // life: a breath of sway and pointer parallax (off with reduced motion)
  let sx = 0, sy = 0;
  if (!reduceMotion) {
    const par = focusIdx >= 0 ? 0.05 : 0.0;
    sx = Math.sin(now * 0.55) * 0.012 + ptr.nx * par;
    sy = Math.sin(now * 0.43 + 1) * 0.008 - ptr.ny * par * 0.6;
  }
  const nyaw = cam.yaw, rx = Math.cos(nyaw), rz = -Math.sin(nyaw); // camera right vector
  camera.position.set(cam.x + rx * sx, cam.y + sy, cam.z + rz * sx);
  camera.rotation.set(cam.pitch, cam.yaw, 0);
  camera.updateMatrixWorld(); camera.matrixWorldInverse.copy(camera.matrixWorld).invert();

  // rooms fade
  for (const k of Object.keys(roomFade)) {
    const target = k === mode ? 1 : 0;
    roomFade[k] = damp(roomFade[k], target, reduceMotion ? 30 : 5.5, dt);
    rooms[k].visible = roomFade[k] > 0.01;
  }
  // the rotunda wall curves in front of a flat light pool; narrow the pools there so they are not clipped
  washK = damp(washK, mode === 'rotunda' ? 0.5 : 1, reduceMotion ? 30 : 5, dt);
  for (const p of pieces) { p.wash.scale.x = p.def.w * 3.4 * washK; }
  floorFade = damp(floorFade, mode === 'wall' ? 0 : 1, reduceMotion ? 30 : 5, dt);
  floor.visible = floorFade > 0.01; floorMat.opacity = 0.86 * floorFade;
  reflGroup.visible = floorFade > 0.2;
  for (const f of fadables) {
    const o = roomFade[f.room] * f.base;
    f.mat.opacity = o; f.mat.transparent = o < 0.999 || f.base < 1;
  }

  // hover spring
  for (const p of pieces) {
    const tgt = (hoverIdx === p.i && focusIdx < 0 && !drag) ? 1 : 0;
    p.hover = damp(p.hover, tgt, 10, dt);
    p.reflMat.opacity = 0.3 * floorFade;
  }

  updatePieces(now);
  for (const p of pieces) p.plq.visible = p.i !== focusIdx;   // the DOM plaque takes over in the close-up
  pickLive(now, false);
  renderer.render(scene, camera);
}

// ---------------------------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------------------------
function resize() { applyViewParams(); }
addEventListener('resize', resize);

async function boot() {
  const bar = $('introBar');
  const fontsReady = (document.fonts && document.fonts.load)
    ? Promise.all([document.fonts.load('600 40px "Geist"'), document.fonts.load('500 20px "Geist Mono"')]).catch(() => {})
    : Promise.resolve();
  await Promise.race([fontsReady, new Promise((r) => setTimeout(r, 1500))]);

  let loaded = 0;
  await Promise.all(pieces.map((p) => new Promise((res) => {
    loader.load(`assets/museum/${p.def.file}.jpg`, (t) => {
      t.colorSpace = THREE.SRGBColorSpace; t.anisotropy = maxAniso;
      p.poster = t; p.setMap(t); res();
    }, undefined, () => res());
  }).then(() => { loaded++; bar.style.transform = `scaleX(${loaded / N})`; })));

  pieces.forEach((p) => { p.plq.material.map = plaqueTex(p.plaqueNo, p.def.title, p.def.meta); p.plq.material.needsUpdate = true; });

  mode = 'hall';
  applyViewParams();
  layout('hall', false);
  roomFade.hall = 1; floorFade = 1;
  // opening shot: stand outside the hall, then glide in
  S.hall.pos = -5.5; S.hall.tgt = 0; camTime = 0.1;
  cam.x = 0; cam.y = EYE; cam.z = hall.zStart + 5.5; cam.yaw = 0;
  requestAnimationFrame(() => {
    frame();
    pickLive(performance.now() / 1000, true);
    setTimeout(() => {
      document.body.classList.remove('is-loading');
      camTime = reduceMotion ? 0.2 : 2.2;
      S.hall.tgt = 0.0;
      setTimeout(() => showHint(HINTS.hall), 1800);
    }, 450);
  });
}
boot();
