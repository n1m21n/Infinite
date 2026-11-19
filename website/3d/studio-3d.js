// Infinite 3D Text & Logo Loop Maker
// True WebGL2 PBR 3D rendering powered by Three.js r160 (PMREM studio lighting, bevelled ExtrudeGeometry, MeshPhysicalMaterial)
import * as THREE from '../vendor/three.module.min.js';

const $ = (id) => document.getElementById(id);

const canvas = $('c3d');
const stageBox = $('stage-box');
const statusEl = $('st-status');
const hudPill = $('hud-3d');
const hudText = $('hud-3d-text');
const hudMeta = $('hud-3d-meta');

const state = {
  text: 'INFINITE',
  font: 'sans',
  mat: 'chrome',
  mot: 'orbit',
  bg: 'midnight',
  aspect: '16:9',
  depth: 0.55,
  bevel: 0.08,
  speed: 1.0,
  logoImg: null,
  logoName: ''
};

// Interactive user orbit offsets
const orbit = {
  yaw: 0,
  pitch: 0.08,
  zoom: 7.4,
  dragging: false,
  lastX: 0,
  lastY: 0
};

// ---------------- WebGL Renderer ----------------
const renderer = new THREE.WebGLRenderer({
  canvas,
  antialias: true,
  alpha: true,
  preserveDrawingBuffer: true,
  powerPreference: 'high-performance'
});
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 1.18;
renderer.outputColorSpace = THREE.SRGBColorSpace;
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;

const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(34, 16 / 9, 0.1, 100);
camera.position.set(0, 0.5, orbit.zoom);

// ---------------- Procedural HDR Studio Environment (PMREM) ----------------
function buildStudioEnvMap() {
  const pmrem = new THREE.PMREMGenerator(renderer);
  pmrem.compileEquirectangularShader();

  const envScene = new THREE.Scene();
  envScene.background = new THREE.Color(0x0b0e1d);

  // Subtle architectural dome gradient
  const domeGeo = new THREE.SphereGeometry(28, 32, 24);
  const domeMat = new THREE.MeshBasicMaterial({ color: 0x151b36, side: THREE.BackSide });
  envScene.add(new THREE.Mesh(domeGeo, domeMat));

  // Emissive softbox panels for crisp specular reflections on Chrome, Gold & Glass
  const panels = [
    { pos: [0, 10, 4],     rot: [Math.PI / 2.2, 0, 0],  size: [18, 6],  color: 0xffffff, mul: 3.4 }, // overhead key strip
    { pos: [-11, 3, 6],    rot: [0, Math.PI / 2.6, 0],  size: [12, 8],  color: 0xa7b1ff, mul: 2.6 }, // cool Signal fill
    { pos: [11, 2, 5],     rot: [0, -Math.PI / 2.5, 0], size: [11, 7],  color: 0xff8c5a, mul: 2.8 }, // warm Ember rim
    { pos: [0, 6, -12],    rot: [0, 0, 0],              size: [20, 5],  color: 0xdfe7ff, mul: 3.0 }, // back kicker
    { pos: [0, -9, 2],     rot: [-Math.PI / 2, 0, 0],   size: [20, 14], color: 0x20274a, mul: 1.2 }, // subtle floor bounce
    { pos: [-5, -2, 11],   rot: [0, Math.PI, 0],        size: [7, 3],   color: 0x8fe3d0, mul: 1.9 }  // subtle cyan accent
  ];

  panels.forEach((p) => {
    const c = new THREE.Color(p.color).multiplyScalar(p.mul);
    const mesh = new THREE.Mesh(
      new THREE.PlaneGeometry(p.size[0], p.size[1]),
      new THREE.MeshBasicMaterial({ color: c, side: THREE.DoubleSide })
    );
    mesh.position.set(...p.pos);
    mesh.rotation.set(...p.rot);
    envScene.add(mesh);
  });

  const rt = pmrem.fromScene(envScene, 0.032);
  pmrem.dispose();
  return rt.texture;
}

const envTex = buildStudioEnvMap();
scene.environment = envTex;

// ---------------- Procedural Brushed Metal Texture ----------------
function makeBrushedTexture() {
  const c = document.createElement('canvas');
  c.width = 512;
  c.height = 512;
  const g = c.getContext('2d');
  /* brand-lint: off */
  g.fillStyle = '#808080';
  /* brand-lint: on */
  g.fillRect(0, 0, 512, 512);
  for (let y = 0; y < 512; y++) {
    const v = 112 + Math.floor(Math.random() * 36);
    g.fillStyle = `rgb(${v},${v},${v})`;
    g.fillRect(0, y, 512, 1);
  }
  const tex = new THREE.CanvasTexture(c);
  tex.wrapS = tex.wrapT = THREE.RepeatWrapping;
  tex.repeat.set(3, 3);
  return tex;
}
const brushedTex = makeBrushedTexture();

// ---------------- Studio Lighting Rig ----------------
const keyLight = new THREE.DirectionalLight(0xfff7ee, 2.2);
keyLight.position.set(4.5, 6.5, 6.5);
keyLight.castShadow = true;
keyLight.shadow.mapSize.set(1024, 1024);
keyLight.shadow.camera.near = 1;
keyLight.shadow.camera.far = 25;
keyLight.shadow.camera.left = -6;
keyLight.shadow.camera.right = 6;
keyLight.shadow.camera.top = 5;
keyLight.shadow.camera.bottom = -5;
keyLight.shadow.bias = -0.0005;
keyLight.shadow.radius = 4;
scene.add(keyLight);

const rimSignal = new THREE.PointLight(0xa7b1ff, 18, 22, 1.5);
rimSignal.position.set(-5.5, 2.4, -3.5);
scene.add(rimSignal);

const rimEmber = new THREE.PointLight(0xff6b35, 16, 22, 1.5);
rimEmber.position.set(5.5, -1.2, -3.2);
scene.add(rimEmber);

const fillLight = new THREE.HemisphereLight(0xdbe3ff, 0x151930, 0.55);
scene.add(fillLight);

// ---------------- Stage Floor & Cyber Grid ----------------
const floorMat = new THREE.MeshStandardMaterial({
  color: 0x111528,
  roughness: 0.28,
  metalness: 0.45,
  envMapIntensity: 0.85
});
const floorMesh = new THREE.Mesh(new THREE.CircleGeometry(18, 64), floorMat);
floorMesh.rotation.x = -Math.PI / 2;
floorMesh.position.y = -1.65;
floorMesh.receiveShadow = true;
scene.add(floorMesh);

const gridHelper = new THREE.GridHelper(32, 32, 0xff6b35, 0x3b4880);
gridHelper.position.y = -1.62;
gridHelper.material.opacity = 0.45;
gridHelper.material.transparent = true;
gridHelper.visible = false;
scene.add(gridHelper);

// ---------------- PBR Material Library ----------------
function createMaterial(kind) {
  switch (kind) {
    case 'chrome':
      return new THREE.MeshPhysicalMaterial({
        color: 0xf4f7ff,
        metalness: 1.0,
        roughness: 0.055,
        clearcoat: 1.0,
        clearcoatRoughness: 0.04,
        envMapIntensity: 1.95
      });
    case 'glass':
      return new THREE.MeshPhysicalMaterial({
        color: 0xe6f2ff,
        metalness: 0.02,
        roughness: 0.05,
        transmission: 0.92,
        ior: 1.52,
        thickness: 1.5,
        iridescence: 1.0,
        iridescenceIOR: 1.38,
        iridescenceThicknessRange: [180, 560],
        clearcoat: 1.0,
        clearcoatRoughness: 0.05,
        envMapIntensity: 1.75
      });
    case 'gold':
      return new THREE.MeshPhysicalMaterial({
        color: 0xf6c34d,
        metalness: 1.0,
        roughness: 0.22,
        roughnessMap: brushedTex,
        bumpMap: brushedTex,
        bumpScale: 0.008,
        clearcoat: 0.4,
        clearcoatRoughness: 0.16,
        envMapIntensity: 1.7
      });
    case 'neon':
      return new THREE.MeshPhysicalMaterial({
        color: 0x1a203d,
        metalness: 0.88,
        roughness: 0.18,
        emissive: 0xff5a22,
        emissiveIntensity: 0.72,
        clearcoat: 1.0,
        clearcoatRoughness: 0.08,
        envMapIntensity: 1.5
      });
    case 'matte':
      return new THREE.MeshPhysicalMaterial({
        color: 0xf2efe9,
        metalness: 0.0,
        roughness: 0.36,
        clearcoat: 0.65,
        clearcoatRoughness: 0.18,
        sheen: 0.4,
        sheenColor: new THREE.Color(0xffffff),
        envMapIntensity: 1.05
      });
    case 'holo':
      return new THREE.MeshPhysicalMaterial({
        color: 0x202748,
        metalness: 0.92,
        roughness: 0.10,
        iridescence: 1.0,
        iridescenceIOR: 1.85,
        iridescenceThicknessRange: [220, 760],
        clearcoat: 1.0,
        clearcoatRoughness: 0.05,
        envMapIntensity: 1.95
      });
    default:
      return createMaterial('chrome');
  }
}

// ---------------- High-Res Alpha Silhouette -> THREE.Shape[] Vectorizer ----------------
// Marching squares + Chaikin smoothing + Douglas-Peucker + hole nesting
function traceCanvasToShapes(srcCanvas) {
  const w = srcCanvas.width;
  const h = srcCanvas.height;
  const data = srcCanvas.getContext('2d').getImageData(0, 0, w, h).data;
  const alpha = (x, y) => {
    if (x < 0 || y < 0 || x >= w || y >= h) return 0;
    return data[(y * w + x) * 4 + 3] / 255;
  };

  const T = 0.5;
  const segs = [];

  // Interpolated marching squares edges
  const interp = (v1, v2) => {
    const d = v2 - v1;
    if (Math.abs(d) < 1e-5) return 0.5;
    return Math.max(0, Math.min(1, (T - v1) / d));
  };

  for (let y = 0; y < h - 1; y++) {
    for (let x = 0; x < w - 1; x++) {
      const tl = alpha(x, y);
      const tr = alpha(x + 1, y);
      const br = alpha(x + 1, y + 1);
      const bl = alpha(x, y + 1);
      const idx = (tl >= T ? 8 : 0) | (tr >= T ? 4 : 0) | (br >= T ? 2 : 0) | (bl >= T ? 1 : 0);
      if (idx === 0 || idx === 15) continue;

      const top    = [x + interp(tl, tr), y];
      const right  = [x + 1, y + interp(tr, br)];
      const bottom = [x + interp(bl, br), y + 1];
      const left   = [x, y + interp(tl, bl)];

      switch (idx) {
        case 1:  segs.push([left, bottom]); break;
        case 2:  segs.push([bottom, right]); break;
        case 3:  segs.push([left, right]); break;
        case 4:  segs.push([top, right]); break;
        case 5:  segs.push([left, top], [bottom, right]); break;
        case 6:  segs.push([top, bottom]); break;
        case 7:  segs.push([left, top]); break;
        case 8:  segs.push([top, left]); break;
        case 9:  segs.push([top, bottom]); break;
        case 10: segs.push([top, right], [left, bottom]); break;
        case 11: segs.push([top, right]); break;
        case 12: segs.push([right, left]); break;
        case 13: segs.push([bottom, right]); break;
        case 14: segs.push([left, bottom]); break;
      }
    }
  }

  // Chain segments into closed loops using spatial hash
  const keyOf = (p) => `${Math.round(p[0] * 100)},${Math.round(p[1] * 100)}`;
  const adj = new Map();
  segs.forEach((s, i) => {
    const k0 = keyOf(s[0]), k1 = keyOf(s[1]);
    if (!adj.has(k0)) adj.set(k0, []);
    if (!adj.has(k1)) adj.set(k1, []);
    adj.get(k0).push([i, 0]);
    adj.get(k1).push([i, 1]);
  });

  const used = new Uint8Array(segs.length);
  const loops = [];

  for (let i = 0; i < segs.length; i++) {
    if (used[i]) continue;
    used[i] = 1;
    const pts = [segs[i][0], segs[i][1]];
    let cur = segs[i][1];
    const startKey = keyOf(pts[0]);

    for (let guard = 0; guard < 12000; guard++) {
      const ck = keyOf(cur);
      if (ck === startKey && pts.length > 3) break;
      const list = adj.get(ck);
      if (!list) break;
      let found = false;
      for (let j = 0; j < list.length; j++) {
        const [si, endIdx] = list[j];
        if (used[si]) continue;
        used[si] = 1;
        cur = endIdx === 0 ? segs[si][1] : segs[si][0];
        pts.push(cur);
        found = true;
        break;
      }
      if (!found) break;
    }

    if (pts.length >= 6) loops.push(pts);
  }

  // Chaikin smoothing (1 pass) + Douglas-Peucker simplification
  function chaikin(pts) {
    const out = [];
    const n = pts.length;
    for (let i = 0; i < n; i++) {
      const a = pts[i], b = pts[(i + 1) % n];
      out.push([a[0] * 0.75 + b[0] * 0.25, a[1] * 0.75 + b[1] * 0.25]);
      out.push([a[0] * 0.25 + b[0] * 0.75, a[1] * 0.25 + b[1] * 0.75]);
    }
    return out;
  }

  function simplifyDP(pts, eps) {
    if (pts.length <= 4) return pts;
    const eps2 = eps * eps;
    function dp(a, b, out) {
      let maxD = 0, idx = -1;
      const ax = pts[a][0], ay = pts[a][1];
      const dx = pts[b][0] - ax, dy = pts[b][1] - ay;
      const len2 = dx * dx + dy * dy || 1e-9;
      for (let i = a + 1; i < b; i++) {
        const t = Math.max(0, Math.min(1, ((pts[i][0] - ax) * dx + (pts[i][1] - ay) * dy) / len2));
        const px = ax + t * dx - pts[i][0];
        const py = ay + t * dy - pts[i][1];
        const d2 = px * px + py * py;
        if (d2 > maxD) { maxD = d2; idx = i; }
      }
      if (maxD > eps2 && idx > a) {
        dp(a, idx, out);
        dp(idx, b, out);
      } else {
        out.push(pts[a]);
      }
    }
    const res = [];
    dp(0, pts.length - 1, res);
    return res;
  }

  function polyArea(pts) {
    let a = 0;
    for (let i = 0, n = pts.length; i < n; i++) {
      const p = pts[i], q = pts[(i + 1) % n];
      a += p[0] * q[1] - q[0] * p[1];
    }
    return a * 0.5;
  }

  function pointInPoly(pt, poly) {
    let inside = false;
    for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
      const xi = poly[i][0], yi = poly[i][1];
      const xj = poly[j][0], yj = poly[j][1];
      const intersect = ((yi > pt[1]) !== (yj > pt[1])) &&
        (pt[0] < ((xj - xi) * (pt[1] - yi)) / (yj - yi + 1e-12) + xi);
      if (intersect) inside = !inside;
    }
    return inside;
  }

  const polys = loops
    .map((pts) => simplifyDP(chaikin(pts), 0.72))
    .filter((pts) => pts.length >= 3 && Math.abs(polyArea(pts)) > 14)
    .map((pts) => ({ pts, area: Math.abs(polyArea(pts)) }))
    .sort((a, b) => b.area - a.area);

  // Nesting depth determines outer vs hole
  const depth = polys.map((p, i) => {
    let d = 0;
    for (let j = 0; j < polys.length; j++) {
      if (i === j || polys[j].area <= p.area) continue;
      if (pointInPoly(p.pts[0], polys[j].pts)) d++;
    }
    return d;
  });

  const scale = 1 / 62;
  const cx = w * 0.5, cy = h * 0.5;
  const toVec2 = (pts) => pts.map((p) => new THREE.Vector2((p[0] - cx) * scale, (cy - p[1]) * scale));

  const outerMap = new Map();
  const shapes = [];

  polys.forEach((p, i) => {
    if (depth[i] % 2 === 0) {
      const sh = new THREE.Shape(toVec2(p.pts));
      outerMap.set(i, sh);
      shapes.push(sh);
    }
  });

  polys.forEach((p, i) => {
    if (depth[i] % 2 === 1) {
      // Find smallest enclosing outer polygon
      let parentIdx = -1, bestArea = Infinity;
      for (let j = 0; j < polys.length; j++) {
        if (depth[j] === depth[i] - 1 && polys[j].area > p.area && polys[j].area < bestArea) {
          if (pointInPoly(p.pts[0], polys[j].pts)) {
            bestArea = polys[j].area;
            parentIdx = j;
          }
        }
      }
      if (parentIdx >= 0 && outerMap.has(parentIdx)) {
        outerMap.get(parentIdx).holes.push(new THREE.Path(toVec2(p.pts)));
      }
    }
  });

  return shapes;
}

// ---------------- Build Extruded 3D Mesh ----------------
const heroGroup = new THREE.Group();
scene.add(heroGroup);

let activeMesh = null;
let activeMat = createMaterial(state.mat);

function fontSpec(kind, size) {
  switch (kind) {
    case 'serif':   return `700 ${size}px Georgia, 'Times New Roman', serif`;
    case 'mono':    return `700 ${size}px 'Geist Mono', ui-monospace, monospace`;
    case 'italic':  return `italic 800 ${size}px 'Geist', system-ui, sans-serif`;
    default:        return `800 ${size}px 'Geist', system-ui, sans-serif`;
  }
}

function rebuildGeometry() {
  const off = document.createElement('canvas');
  const g = off.getContext('2d');

  if (state.logoImg) {
    off.width = 280;
    off.height = 280;
    g.clearRect(0, 0, off.width, off.height);
    const pad = 28;
    const iw = state.logoImg.naturalWidth || state.logoImg.width || 1;
    const ih = state.logoImg.naturalHeight || state.logoImg.height || 1;
    const sc = Math.min((off.width - pad * 2) / iw, (off.height - pad * 2) / ih);
    const dw = iw * sc, dh = ih * sc;
    g.drawImage(state.logoImg, (off.width - dw) * 0.5, (off.height - dh) * 0.5, dw, dh);
  } else {
    const txt = (state.text || 'INFINITE').trim() || 'INFINITE';
    const fontSize = txt.length <= 6 ? 112 : txt.length <= 12 ? 88 : 68;
    off.width = 640;
    off.height = 180;
    g.clearRect(0, 0, off.width, off.height);
    g.fillStyle = '#ffffff';
    g.font = fontSpec(state.font, fontSize);
    g.textAlign = 'center';
    g.textBaseline = 'middle';
    g.fillText(txt, off.width * 0.5, off.height * 0.52, off.width - 36);
  }

  const shapes = traceCanvasToShapes(off);
  if (!shapes.length) return;

  const geo = new THREE.ExtrudeGeometry(shapes, {
    depth: state.depth,
    bevelEnabled: state.bevel > 0.005,
    bevelThickness: state.bevel,
    bevelSize: state.bevel * 0.85,
    bevelOffset: 0,
    bevelSegments: 5,
    curveSegments: 6
  });

  geo.computeBoundingBox();
  const bb = geo.boundingBox;
  const center = new THREE.Vector3();
  const size = new THREE.Vector3();
  bb.getCenter(center);
  bb.getSize(size);
  geo.translate(-center.x, -center.y, -center.z);

  // Normalize scale so any text length or logo fills the hero framing nicely
  const targetW = state.aspect === '9:16' ? 3.5 : 5.1;
  const targetH = 2.35;
  const fit = Math.min(targetW / Math.max(0.1, size.x), targetH / Math.max(0.1, size.y));
  geo.scale(fit, fit, fit);
  geo.computeVertexNormals();

  if (activeMesh) {
    heroGroup.remove(activeMesh);
    activeMesh.geometry.dispose();
  }

  activeMesh = new THREE.Mesh(geo, activeMat);
  activeMesh.castShadow = true;
  activeMesh.receiveShadow = true;
  heroGroup.add(activeMesh);

  const tris = geo.index ? geo.index.count / 3 : geo.attributes.position.count / 3;
  hudMeta.textContent = `${Math.round(tris).toLocaleString()} tris · WebGL2 PBR`;
}

function applyMaterial() {
  if (activeMat) activeMat.dispose();
  activeMat = createMaterial(state.mat);
  if (activeMesh) activeMesh.material = activeMat;
  rimEmber.intensity = state.mat === 'neon' ? 28 : 16;
  rimSignal.intensity = state.mat === 'holo' || state.mat === 'glass' ? 24 : 18;
}

function applyBackground() {
  stageBox.classList.toggle('is-checker', state.bg === 'clear');
  if (state.bg === 'clear') {
    scene.background = null;
    scene.fog = null;
    renderer.setClearColor(0x000000, 0);
    floorMesh.visible = false;
    gridHelper.visible = false;
  } else if (state.bg === 'paper') {
    scene.background = new THREE.Color(0xf3f2ee);
    scene.fog = new THREE.Fog(0xf3f2ee, 10, 26);
    floorMat.color.setHex(0xe8e7e2);
    floorMat.roughness = 0.72;
    floorMat.metalness = 0.05;
    floorMesh.visible = true;
    gridHelper.visible = false;
  } else if (state.bg === 'grid') {
    scene.background = new THREE.Color(0x090c18);
    scene.fog = new THREE.FogExp2(0x090c18, 0.075);
    floorMesh.visible = false;
    gridHelper.visible = true;
  } else if (state.bg === 'void') {
    scene.background = new THREE.Color(0x05070f);
    scene.fog = null;
    floorMesh.visible = false;
    gridHelper.visible = false;
  } else {
    // midnight studio
    scene.background = new THREE.Color(0x0d1020);
    scene.fog = new THREE.Fog(0x0d1020, 11, 28);
    floorMat.color.setHex(0x131830);
    floorMat.roughness = 0.28;
    floorMat.metalness = 0.45;
    floorMesh.visible = true;
    gridHelper.visible = false;
  }
}

function applyAspect() {
  stageBox.setAttribute('data-aspect', state.aspect);
  let w = 1280, h = 720;
  if (state.aspect === '9:16') { w = 720; h = 1280; }
  else if (state.aspect === '1:1') { w = 1080; h = 1080; }
  renderer.setPixelRatio(1);
  renderer.setSize(w, h, false);
  camera.aspect = w / h;
  camera.updateProjectionMatrix();
  rebuildGeometry();
}

// ---------------- Interactive Orbit Camera ----------------
canvas.addEventListener('pointerdown', (e) => {
  orbit.dragging = true;
  orbit.lastX = e.clientX;
  orbit.lastY = e.clientY;
  canvas.setPointerCapture(e.pointerId);
});

canvas.addEventListener('pointermove', (e) => {
  if (!orbit.dragging) return;
  const dx = e.clientX - orbit.lastX;
  const dy = e.clientY - orbit.lastY;
  orbit.lastX = e.clientX;
  orbit.lastY = e.clientY;
  orbit.yaw += dx * 0.008;
  orbit.pitch = Math.max(-0.9, Math.min(0.9, orbit.pitch + dy * 0.006));
});

canvas.addEventListener('pointerup', () => { orbit.dragging = false; });
canvas.addEventListener('pointercancel', () => { orbit.dragging = false; });
canvas.addEventListener('dblclick', () => { orbit.yaw = 0; orbit.pitch = 0.08; orbit.zoom = 7.4; });

canvas.addEventListener('wheel', (e) => {
  e.preventDefault();
  orbit.zoom = Math.max(4.0, Math.min(12.5, orbit.zoom + e.deltaY * 0.005));
}, { passive: false });

// ---------------- UI Wiring ----------------
let rebuildTimer = null;
$('txt-input').addEventListener('input', (e) => {
  state.text = e.target.value || 'INFINITE';
  clearTimeout(rebuildTimer);
  rebuildTimer = setTimeout(rebuildGeometry, 60);
});

const svgInput = $('svg-file');
const clearLogoBtn = $('btn-clear-svg');
function loadLogo(file) {
  if (!file) return;
  const img = new Image();
  img.onload = () => {
    state.logoImg = img;
    state.logoName = file.name;
    clearLogoBtn.hidden = false;
    $('svg-label').textContent = `Loaded logo: ${file.name}`;
    rebuildGeometry();
    statusEl.textContent = `Extruded ${file.name} into real 3D bevelled geometry.`;
  };
  img.src = URL.createObjectURL(file);
}
svgInput.addEventListener('change', () => loadLogo(svgInput.files[0]));
const dropEl = $('drop-logo');
dropEl.addEventListener('dragover', (e) => { e.preventDefault(); dropEl.classList.add('is-dragover'); });
dropEl.addEventListener('dragleave', () => dropEl.classList.remove('is-dragover'));
dropEl.addEventListener('drop', (e) => {
  e.preventDefault();
  dropEl.classList.remove('is-dragover');
  loadLogo(e.dataTransfer.files[0]);
});
clearLogoBtn.addEventListener('click', () => {
  state.logoImg = null;
  state.logoName = '';
  clearLogoBtn.hidden = true;
  $('svg-label').textContent = 'Drop an SVG or PNG logo to extrude in 3D';
  rebuildGeometry();
});

function bindChips(id, attr, cb) {
  const box = $(id);
  box.querySelectorAll('.st-chip').forEach((btn) => {
    btn.addEventListener('click', () => {
      box.querySelectorAll('.st-chip').forEach((b) => b.classList.remove('is-active'));
      btn.classList.add('is-active');
      cb(btn.getAttribute(attr));
    });
  });
}

bindChips('font-chips', 'data-font', (v) => { state.font = v; rebuildGeometry(); });
bindChips('mat-chips', 'data-mat', (v) => { state.mat = v; applyMaterial(); });
bindChips('mot-chips', 'data-mot', (v) => { state.mot = v; });
bindChips('bg-chips', 'data-bg', (v) => { state.bg = v; applyBackground(); });
bindChips('aspect-chips', 'data-aspect', (v) => { state.aspect = v; applyAspect(); });

function setSliderFill(el) {
  if (!el) return;
  const min = parseFloat(el.min) || 0, max = parseFloat(el.max) || 1, v = parseFloat(el.value);
  el.style.setProperty('--p', (((v - min) / (max - min || 1)) * 100).toFixed(2) + '%');
}

$('ctrl-depth').addEventListener('input', (e) => {
  state.depth = parseFloat(e.target.value);
  $('val-depth').textContent = state.depth.toFixed(2);
  setSliderFill(e.target);
  clearTimeout(rebuildTimer);
  rebuildTimer = setTimeout(rebuildGeometry, 40);
});

$('ctrl-bevel').addEventListener('input', (e) => {
  state.bevel = parseFloat(e.target.value);
  $('val-bevel').textContent = state.bevel.toFixed(2);
  setSliderFill(e.target);
  clearTimeout(rebuildTimer);
  rebuildTimer = setTimeout(rebuildGeometry, 40);
});

$('ctrl-speed').addEventListener('input', (e) => {
  state.speed = parseFloat(e.target.value);
  $('val-speed').textContent = state.speed.toFixed(2) + 'x';
  setSliderFill(e.target);
});

setSliderFill($('ctrl-depth'));
setSliderFill($('ctrl-bevel'));
setSliderFill($('ctrl-speed'));

$('btn-gen').addEventListener('click', () => {
  const mats = ['chrome', 'glass', 'gold', 'neon', 'matte', 'holo'];
  const mots = ['orbit', 'float', 'pulse', 'pendulum', 'tilt'];
  const bgs = ['midnight', 'paper', 'grid', 'void'];
  state.mat = mats[Math.floor(Math.random() * mats.length)];
  state.mot = mots[Math.floor(Math.random() * mots.length)];
  state.bg = bgs[Math.floor(Math.random() * bgs.length)];
  state.depth = +(0.3 + Math.random() * 0.7).toFixed(2);
  state.bevel = +(0.04 + Math.random() * 0.1).toFixed(2);

  document.querySelectorAll('#mat-chips .st-chip').forEach((b) => b.classList.toggle('is-active', b.dataset.mat === state.mat));
  document.querySelectorAll('#mot-chips .st-chip').forEach((b) => b.classList.toggle('is-active', b.dataset.mot === state.mot));
  document.querySelectorAll('#bg-chips .st-chip').forEach((b) => b.classList.toggle('is-active', b.dataset.bg === state.bg));
  $('ctrl-depth').value = state.depth; $('val-depth').textContent = state.depth.toFixed(2); setSliderFill($('ctrl-depth'));
  $('ctrl-bevel').value = state.bevel; $('val-bevel').textContent = state.bevel.toFixed(2); setSliderFill($('ctrl-bevel'));
  applyMaterial();
  applyBackground();
  rebuildGeometry();
  statusEl.textContent = `Generated ${state.mat} · ${state.mot} · ${state.bg}.`;
});

// ---------------- Seamless 1-Loop Recording & Exports ----------------
let recorder = null;
let chunks = [];
let loopStopTimer = null;
const recBtn = $('btn-rec');

function stopRecording() {
  clearTimeout(loopStopTimer);
  if (recorder && recorder.state === 'recording') recorder.stop();
  recBtn.querySelector('span').textContent = 'Record';
  recBtn.classList.remove('is-active');
  hudPill.classList.remove('is-rec');
  hudPill.hidden = true;
}

recBtn.addEventListener('click', () => {
  if (recorder && recorder.state === 'recording') {
    stopRecording();
    return;
  }
  const loopMs = Math.round((4000 / Math.max(0.25, state.speed)));
  chunks = [];
  const stream = canvas.captureStream(60);
  const mime = MediaRecorder.isTypeSupported('video/webm;codecs=vp9') ? 'video/webm;codecs=vp9' : 'video/webm';
  recorder = new MediaRecorder(stream, { mimeType: mime, videoBitsPerSecond: 10_000_000 });
  recorder.ondataavailable = (e) => { if (e.data.size) chunks.push(e.data); };
  recorder.onstop = () => {
    const blob = new Blob(chunks, { type: 'video/webm' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = `infinite-3d-${state.mat}-${state.aspect.replace(':', 'x')}.webm`;
    a.click();
  };
  loopPhaseOrigin = performance.now();
  recorder.start();
  recBtn.querySelector('span').textContent = 'Stop';
  recBtn.classList.add('is-active');
  hudPill.classList.add('is-rec');
  hudPill.hidden = false;
  hudText.textContent = `Recording ${(loopMs / 1000).toFixed(1)}s loop...`;
  loopStopTimer = setTimeout(stopRecording, loopMs);
});

$('btn-png').addEventListener('click', () => {
  renderer.render(scene, camera);
  const a = document.createElement('a');
  a.href = canvas.toDataURL('image/png');
  a.download = `infinite-3d-${state.mat}.png`;
  a.click();
  statusEl.textContent = state.bg === 'clear'
    ? 'Exported transparent PNG.'
    : 'Exported studio PNG frame.';
});

$('btn-inf').addEventListener('click', () => {
  const patch = [
    '# Made with Infinite 3D Loop Maker (https://infinitedevices.org/3d/), by Naman Soni (n1m21n)',
    'infinite-patch 1',
    'node 1 Visuals Sketch3D',
    '  id hero',
    `  s text "${state.text.replace(/"/g, '')}"`,
    `  s material "${state.mat}"`,
    `  s motion "${state.mot}"`,
    `  f uDepth ${state.depth}`,
    `  f uBevel ${state.bevel}`,
    `  f uSpeed ${state.speed}`,
    'end',
    'node 2 Output Video Out',
    '  id out',
    'end',
    'cable 1 1 2 1',
    ''
  ].join('\n');
  const a = document.createElement('a');
  a.href = URL.createObjectURL(new Blob([patch], { type: 'text/plain' }));
  a.download = 'infinite-3d-loop.inf';
  a.click();
  statusEl.textContent = 'Downloaded infinite-3d-loop.inf.';
});

// ---------------- Seamless Loop Render Loop ----------------
let loopPhaseOrigin = performance.now();

function animate(now) {
  requestAnimationFrame(animate);
  const elapsed = (now - loopPhaseOrigin) * 0.001 * state.speed;
  // Exact 4-second period = 2*PI radians for seamless looping
  const theta = (elapsed / 4.0) * Math.PI * 2;

  let ry = 0, rx = 0, rz = 0, py = 0, sc = 1;
  if (state.mot === 'orbit') {
    ry = theta;
    rx = Math.sin(theta) * 0.14;
  } else if (state.mot === 'float') {
    ry = Math.sin(theta) * 0.32;
    rx = Math.cos(theta) * 0.10;
    py = Math.sin(theta) * 0.22;
  } else if (state.mot === 'pulse') {
    ry = Math.sin(theta) * 0.22;
    const beat = Math.pow(Math.max(0, Math.sin(theta * 2)), 4);
    sc = 1 + 0.11 * beat;
    rx = -0.08 * beat;
  } else if (state.mot === 'pendulum') {
    ry = Math.sin(theta) * 0.72;
    rz = Math.sin(theta) * 0.08;
  } else {
    // Hero tilt
    ry = theta;
    rx = -0.22 + Math.cos(theta) * 0.14;
    py = Math.sin(theta * 2) * 0.10;
  }

  heroGroup.rotation.set(rx, ry, rz);
  heroGroup.position.y = py;
  heroGroup.scale.setScalar(sc);

  // Subtle rim light orbit so Chrome & Glass stay alive
  rimSignal.position.x = Math.cos(theta) * 6.0;
  rimSignal.position.z = Math.sin(theta) * 5.0;
  rimEmber.position.x = -Math.cos(theta) * 6.0;
  rimEmber.position.z = -Math.sin(theta) * 5.0;

  // Camera orbit
  const camX = Math.sin(orbit.yaw) * Math.cos(orbit.pitch) * orbit.zoom;
  const camY = Math.sin(orbit.pitch) * orbit.zoom + 0.25;
  const camZ = Math.cos(orbit.yaw) * Math.cos(orbit.pitch) * orbit.zoom;
  camera.position.set(camX, camY, camZ);
  camera.lookAt(0, 0, 0);

  renderer.render(scene, camera);
}

// Init
applyBackground();
applyMaterial();
applyAspect();
document.fonts?.ready?.then(() => rebuildGeometry());
requestAnimationFrame(animate);
