/**
 * Nocturne world: a lakeside meadow built from real scanned assets.
 * Sky, ground, rocks, ferns and trees are Poly Haven scans (CC0), the water is
 * the three.js planar-reflection Water. Sources: ../assets/nocturne/SOURCES.md
 *
 * Public surface (used by app.js):
 *   const world = new NocturneWorld(el, { onProgress, onReady });
 *   world.setMode('midnight' | 'sunset')
 *   world.pulseNote({ midi, velocity, register })
 *   world.setWeather({ rain, wind })
 *   world.lightning(intensity)
 *   world.onInteract(cb)
 */
import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { Water } from 'three/addons/objects/Water.js';

const ASSETS = new URL('../assets/nocturne/', import.meta.url).href;

const WORLD = 400;          // terrain edge, metres
const SEG = 256;            // terrain segments per side
const EYE = 1.65;           // eye height, metres
const LAKE = { x: 0, z: -48 };
const HOME = { x: 0, z: 12 };
const WALK_LIMIT = 120;
const MIN_H = -4;           // the lake floor is walkable: wade, swim, dive
const WADE_MAX = 0.9;       // click-to-walk reaches the shallows only
const LAYER_DETAIL = 1;     // small things the lake does not need to mirror

// what the lake looks like from below, per sky
const UNDERWATER = {
  midnight: { fog: 0x0a3046, density: 0.07, ceiling: 0x14506b },
  sunset: { fog: 0x1f6070, density: 0.052, ceiling: 0x3f93a3 }
};

const MODES = {
  midnight: {
    sky: 'sky_midnight.jpg', sunUV: [0.5978, 0.4141],
    background: 0.55, env: 0.85, exposure: 0.95,
    light: 0xbcd0ff, lightPower: 1.5,
    fog: 0x16284a, fogDensity: 0.0085,
    water: 0x06121f, grassAmbient: [0.11, 0.17, 0.3], fireflies: 1.0
  },
  sunset: {
    sky: 'sky_sunset.jpg', sunUV: [0.6339, 0.4545],
    background: 1.25, env: 1.0, exposure: 0.95,
    light: 0xffb07a, lightPower: 3.2,
    fog: 0x8f7f8e, fogDensity: 0.0052,
    water: 0x1a2530, grassAmbient: [0.55, 0.5, 0.56], fireflies: 0.25
  }
};

/* ---------- terrain maths (shared by mesh, grass, scatter and camera) ---------- */

function hash(ix, iz) {
  let h = Math.imul(ix, 374761393) ^ Math.imul(iz, 668265263);
  h = Math.imul(h ^ (h >>> 13), 1274126177);
  return ((h ^ (h >>> 16)) >>> 0) / 4294967295;
}
function vnoise(x, z) {
  const ix = Math.floor(x), iz = Math.floor(z);
  const fx = x - ix, fz = z - iz;
  const ux = fx * fx * (3 - 2 * fx), uz = fz * fz * (3 - 2 * fz);
  const a = hash(ix, iz), b = hash(ix + 1, iz), c = hash(ix, iz + 1), d = hash(ix + 1, iz + 1);
  return a + (b - a) * ux + (c - a) * uz + (a - b - c + d) * ux * uz;
}
function fbm(x, z, octaves = 4) {
  let s = 0, a = 0.5, f = 1;
  for (let i = 0; i < octaves; i++) { s += a * vnoise(x * f, z * f); f *= 2.03; a *= 0.5; }
  return s;
}
const smooth = (a, b, v) => { const t = Math.min(1, Math.max(0, (v - a) / (b - a))); return t * t * (3 - 2 * t); };
const lerp = (a, b, t) => a + (b - a) * t;

function groundHeight(x, z) {
  const dl = Math.hypot((x - LAKE.x) / 1.35, z - LAKE.z);
  const wobble = (fbm(x * 0.045 + 7, z * 0.045 + 3) - 0.5) * 10;
  const land = smooth(38 + wobble, 52 + wobble, dl);
  const meadow = 1 - smooth(18, 42, Math.hypot(x - HOME.x, z - HOME.z));
  const rolling = 1.4 + (fbm(x * 0.018, z * 0.018) - 0.5) * 3.4 + (fbm(x * 0.09, z * 0.09, 2) - 0.5) * 0.4;
  const hills = smooth(70, 190, Math.hypot(x, z + 30)) * (6 + fbm(x * 0.008 + 20, z * 0.008) * 26);
  const h = lerp(rolling, 1.55, meadow * 0.7) + hills;
  return lerp(-3.2, h, land);
}

function mulberry(seed) {
  return () => {
    seed |= 0; seed = (seed + 0x6D2B79F5) | 0;
    let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

export class NocturneWorld {
  constructor(container, options = {}) {
    this.container = container;
    this.options = options;
    this.mode = 'midnight';
    this.reduced = window.matchMedia('(prefers-reduced-motion: reduce)').matches;

    this.pulse = { bass: 0, mid: 0, treble: 0 };
    this.flash = 0;
    this.weather = { rain: 0, wind: 0.35 };
    this.fade = 1;            // exposure multiplier for mode changes
    this.fadeTarget = 1;
    this.pendingMode = null;
    this.interactCb = null;

    this.cam = { x: HOME.x, z: HOME.z, yaw: 0, pitch: -0.02, y: 0 };
    this.keys = new Set();
    this.lastInput = -1e9;
    this.goal = null;                       // a clicked spot the camera is walking to
    this.base = { x: HOME.x, z: HOME.z };   // where the idle drift circles
    this.ray = new THREE.Raycaster();
    this.glideT = 0;
    this.diveWant = false;   // wants to be under the surface
    this.dive = 0;           // 0 at the surface .. 1 fully down
    this.uw = 0;             // smoothed underwater look
    this.under = false;
    this.immersion = { depth: 0, swim: 0, under: false, uw: 0, dive: false, moving: false, run: false };
    this._fog = new THREE.Color();
    this._uwFog = new THREE.Color();

    this.uniforms = {
      uTime: { value: 0 },
      uWind: { value: 0.35 },
      uCam: { value: new THREE.Vector3() },
      uSunDir: { value: new THREE.Vector3(0, 1, 0) },
      uSunColor: { value: new THREE.Color() },
      uAmbient: { value: new THREE.Color() },
      uFogColor: { value: new THREE.Color() },
      uFogDensity: { value: 0.008 },
      uPulse: { value: 0 },
      uRain: { value: 0 },
      uFireflies: { value: 1 }
    };

    this.restPitch = 0.015;
    this._initRenderer();
    this._resize();
    this.cam.pitch = this.restPitch;
    this._buildHeights();
    this._load();
    this._bind();
  }

  /* ---------- setup ---------- */

  _initRenderer() {
    const w = this.container.clientWidth || window.innerWidth;
    const h = this.container.clientHeight || window.innerHeight;
    this.renderer = new THREE.WebGLRenderer({ antialias: true, powerPreference: 'high-performance' });
    // phones start a tier down; _adapt() steps further if frames run long
    this.small = window.matchMedia('(pointer: coarse)').matches;
    this.pixelRatio = Math.min(window.devicePixelRatio || 1, this.small ? 1.25 : 1.5);
    this.slow = { time: 0, frames: 0, steps: 0 };
    this.renderer.setPixelRatio(this.pixelRatio);
    this.renderer.setSize(w, h);
    this.renderer.outputColorSpace = THREE.SRGBColorSpace;
    this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    this.renderer.shadowMap.enabled = true;
    this.renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    this.container.appendChild(this.renderer.domElement);

    this.scene = new THREE.Scene();
    this.scene.fog = new THREE.FogExp2(MODES.midnight.fog, MODES.midnight.fogDensity);
    this.camera = new THREE.PerspectiveCamera(50, w / h, 0.1, 1600);
    this.camera.layers.enable(LAYER_DETAIL);
    this.camera.rotation.order = 'YXZ';

    this.light = new THREE.DirectionalLight(0xffffff, 1);
    this.light.castShadow = true;
    this.light.shadow.mapSize.set(1024, 1024);
    const sc = this.light.shadow.camera;
    sc.left = -28; sc.right = 28; sc.top = 28; sc.bottom = -28; sc.near = 1; sc.far = 220;
    this.light.shadow.bias = -0.0006;
    this.light.shadow.normalBias = 0.05;
    this.scene.add(this.light, this.light.target);

    this.clock = new THREE.Clock();
  }

  _buildHeights() {
    const n = SEG + 1;
    this.heights = new Float32Array(n * n);
    for (let j = 0; j < n; j++) {
      for (let i = 0; i < n; i++) {
        this.heights[j * n + i] = groundHeight((i / SEG - 0.5) * WORLD, (j / SEG - 0.5) * WORLD);
      }
    }
    this.heightTex = new THREE.DataTexture(this.heights, n, n, THREE.RedFormat, THREE.FloatType);
    this.heightTex.minFilter = THREE.NearestFilter;
    this.heightTex.magFilter = THREE.NearestFilter;
    this.heightTex.needsUpdate = true;
  }

  /** Bilinear height on the same grid the mesh uses, so feet and plants sit on it. */
  heightAt(x, z) {
    const n = SEG + 1;
    const fx = Math.min(SEG - 0.001, Math.max(0, (x / WORLD + 0.5) * SEG));
    const fz = Math.min(SEG - 0.001, Math.max(0, (z / WORLD + 0.5) * SEG));
    const ix = Math.floor(fx), iz = Math.floor(fz);
    const tx = fx - ix, tz = fz - iz;
    const h = this.heights;
    const a = h[iz * n + ix], b = h[iz * n + ix + 1], c = h[(iz + 1) * n + ix], d = h[(iz + 1) * n + ix + 1];
    return a + (b - a) * tx + (c - a) * tz + (a - b - c + d) * tx * tz;
  }

  _load() {
    const manager = new THREE.LoadingManager();
    manager.onProgress = (_url, done, total) => {
      if (this.options.onProgress) this.options.onProgress(done / total);
    };
    manager.onLoad = () => this._assemble();
    manager.onError = (url) => console.warn('Nocturne: could not load', url);

    const tl = new THREE.TextureLoader(manager);
    const gl = new GLTFLoader(manager);
    const tex = (file, srgb, repeat) => {
      const t = tl.load(ASSETS + file);
      if (srgb) t.colorSpace = THREE.SRGBColorSpace;
      if (repeat) { t.wrapS = t.wrapT = THREE.RepeatWrapping; t.anisotropy = 8; }
      return t;
    };
    this.tex = {
      sky: { midnight: tex(MODES.midnight.sky, true), sunset: tex(MODES.sunset.sky, true) },
      grassDiff: tex('ground_grass_diff.jpg', true, true),
      grassNor: tex('ground_grass_nor.jpg', false, true),
      grassRough: tex('ground_grass_rough.jpg', false, true),
      shoreDiff: tex('ground_shore_diff.jpg', true, true),
      shoreNor: tex('ground_shore_nor.jpg', false, true),
      waterNormals: tex('water_normals.jpg', false, true),
      trees: tex('trees.webp', true)
    };
    this.models = {};
    for (const name of ['rock_moss_set_01', 'fern_02', 'shrub_03']) {
      gl.load(`${ASSETS}models/${name}/${name}.gltf`, (g) => { this.models[name] = g.scene; });
    }
  }

  _assemble() {
    const pmrem = new THREE.PMREMGenerator(this.renderer);
    this.env = {};
    for (const key of Object.keys(MODES)) {
      const t = this.tex.sky[key];
      t.mapping = THREE.EquirectangularReflectionMapping;
      this.env[key] = pmrem.fromEquirectangular(t).texture;
    }
    pmrem.dispose();

    this._buildTerrain();
    this._buildWater();
    this._buildUnderside();
    this._buildTrees();
    this._buildScatter();
    this._buildGrass();
    this._buildFireflies();
    this._buildRain();
    this.marker = new THREE.Mesh(
      new THREE.RingGeometry(0.26, 0.34, 40).rotateX(-Math.PI / 2),
      new THREE.MeshBasicMaterial({ color: 0xA7B1FF, transparent: true, opacity: 0, depthWrite: false, fog: false })
    );
    this.marker.layers.set(LAYER_DETAIL);
    this.scene.add(this.marker);
    this._applyMode(this.mode);

    this.ready = true;
    this.renderer.setAnimationLoop(() => this._frame());
    if (this.options.onReady) this.options.onReady();
  }

  /* ---------- terrain ---------- */

  _buildTerrain() {
    const geo = new THREE.PlaneGeometry(WORLD, WORLD, SEG, SEG);
    geo.rotateX(-Math.PI / 2);
    const pos = geo.attributes.position;
    const shore = new Float32Array(pos.count);
    for (let i = 0; i < pos.count; i++) {
      const h = this.heights[i];
      pos.setY(i, h);
      shore[i] = 1 - smooth(0.2, 0.85, h);
    }
    geo.setAttribute('aShore', new THREE.BufferAttribute(shore, 1));
    geo.computeVertexNormals();

    const rep = 64;
    for (const t of [this.tex.grassDiff, this.tex.grassNor, this.tex.grassRough]) t.repeat.set(rep, rep);
    const mat = new THREE.MeshStandardMaterial({
      map: this.tex.grassDiff,
      normalMap: this.tex.grassNor,
      roughnessMap: this.tex.grassRough,
      roughness: 1,
      metalness: 0
    });
    const extra = { uShoreMap: { value: this.tex.shoreDiff }, uShoreNor: { value: this.tex.shoreNor } };
    mat.onBeforeCompile = (shader) => {
      Object.assign(shader.uniforms, extra);
      shader.vertexShader = shader.vertexShader
        .replace('#include <common>', '#include <common>\nattribute float aShore;\nvarying float vShore;')
        .replace('#include <begin_vertex>', '#include <begin_vertex>\nvShore = aShore;');
      shader.fragmentShader = shader.fragmentShader
        .replace('#include <common>', '#include <common>\nuniform sampler2D uShoreMap;\nuniform sampler2D uShoreNor;\nvarying float vShore;')
        .replace('#include <map_fragment>', [
          'vec4 gNear = texture2D( map, vMapUv );',
          'vec4 gFar = texture2D( map, vMapUv * 0.137 );',          // second scale hides the tiling
          'vec4 sampledDiffuseColor = vec4( mix( gNear.rgb, gFar.rgb, 0.45 ), 1.0 );',
          'vec4 pebbles = texture2D( uShoreMap, vMapUv * 1.9 );',
          'sampledDiffuseColor.rgb = mix( sampledDiffuseColor.rgb, pebbles.rgb, vShore );',
          'sampledDiffuseColor.rgb *= mix( 1.0, 0.45, vShore * vShore );',  // wet near the water
          'diffuseColor *= sampledDiffuseColor;'
        ].join('\n'))
        .replace('#include <roughnessmap_fragment>', '#include <roughnessmap_fragment>\nroughnessFactor = mix( roughnessFactor, 0.35, vShore * vShore );')
        .replace('#include <normal_fragment_maps>', [
          'vec3 mapN = texture2D( normalMap, vNormalMapUv ).xyz * 2.0 - 1.0;',
          'vec3 mapS = texture2D( uShoreNor, vNormalMapUv * 1.9 ).xyz * 2.0 - 1.0;',
          'mapN = mix( mapN, mapS, vShore );',
          'mapN.xy *= normalScale;',
          'normal = normalize( tbn * mapN );'
        ].join('\n'));
    };
    this.terrain = new THREE.Mesh(geo, mat);
    this.terrain.receiveShadow = true;
    this.scene.add(this.terrain);
  }

  /* ---------- water ---------- */

  /** The surface seen from below: a bright ceiling instead of the mirror. */
  _buildUnderside() {
    this.ceiling = new THREE.Mesh(
      new THREE.PlaneGeometry(3000, 3000).rotateX(Math.PI / 2),
      new THREE.MeshBasicMaterial({ color: UNDERWATER.midnight.ceiling, transparent: true, opacity: 0.8, fog: false, depthWrite: false })
    );
    this.ceiling.position.y = 0.002;
    this.ceiling.visible = false;
    this.scene.add(this.ceiling);
    this.uwBackground = new THREE.Color(UNDERWATER.midnight.fog);
  }

  _buildWater() {
    this.water = new Water(new THREE.PlaneGeometry(3000, 3000), {
      textureWidth: 512,
      textureHeight: 512,
      waterNormals: this.tex.waterNormals,
      sunDirection: new THREE.Vector3(0, 1, 0),
      sunColor: 0xffffff,
      waterColor: MODES.midnight.water,
      distortionScale: 0.9,
      fog: true
    });
    this.water.rotation.x = -Math.PI / 2;
    this.water.position.y = 0;
    this.water.material.uniforms.size.value = 5.5;   // small ripples: a lake, not a sea
    this.scene.add(this.water);
  }

  /* ---------- trees: scanned conifers baked to cards ---------- */

  _buildTrees() {
    const cells = 6;
    // two crossed cards, each drawn from both sides with the same soft normals
    const pos = [], uv = [], nor = [], idx = [];
    const addCard = (ax, az) => {
      for (const flip of [1, -1]) {
        const base = pos.length / 3;
        for (const [u, v] of [[0, 0], [1, 0], [1, 1], [0, 1]]) {
          const s = (u - 0.5);
          pos.push(ax * s, v, az * s);
          uv.push(u, v);
          const n = new THREE.Vector3(ax * s * 1.4, 0.55, az * s * 1.4).normalize();
          nor.push(n.x, n.y, n.z);
        }
        if (flip > 0) idx.push(base, base + 1, base + 2, base, base + 2, base + 3);
        else idx.push(base, base + 2, base + 1, base, base + 3, base + 2);
      }
    };
    addCard(1, 0);
    addCard(0.5, 0.866);
    addCard(-0.5, 0.866);
    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
    geo.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
    geo.setAttribute('normal', new THREE.Float32BufferAttribute(nor, 3));
    geo.setIndex(idx);

    const rnd = mulberry(21);
    const spots = [];
    let guard = 0;
    while (spots.length < 4200 && guard++ < 90000) {
      const x = (rnd() - 0.5) * (WORLD - 8), z = (rnd() - 0.5) * (WORLD - 8);
      const h = this.heightAt(x, z);
      if (h < 0.75) continue;
      const r = Math.hypot(x - HOME.x, z - HOME.z);
      if (r < 17 + fbm(x * 0.07, z * 0.07) * 10) continue;                 // the meadow clearing
      if (Math.abs(x) < 11 && z < HOME.z && z > -10) continue;              // open view to the lake
      let p = 0.2 + 0.8 * smooth(0.36, 0.58, fbm(x * 0.03 + 11, z * 0.03 + 5));
      if (r < 46) p *= 0.4;
      if (rnd() > p) continue;
      spots.push([x, h, z]);
    }
    // a dense belt round the far shore, so the treeline reads as forest
    guard = 0;
    let belt = 0;
    while (belt < 1500 && guard++ < 40000) {
      const a = rnd() * Math.PI * 2, r = 50 + Math.pow(rnd(), 1.6) * 34;
      const x = LAKE.x + Math.cos(a) * r * 1.35, z = LAKE.z + Math.sin(a) * r;
      const h = this.heightAt(x, z);
      if (h < 0.8 || z > -6) continue;                                      // never between the meadow and the water
      spots.push([x, h, z]);
      belt++;
    }

    const mat = new THREE.MeshStandardMaterial({
      map: this.tex.trees,
      alphaTest: 0.3,
      roughness: 0.95,
      metalness: 0,
      envMapIntensity: 0.45,
      color: 0x6f8266
    });
    mat.alphaToCoverage = true;
    this.tex.trees.anisotropy = 4;
    const u = this.uniforms;
    mat.onBeforeCompile = (shader) => {
      shader.uniforms.uTime = u.uTime;
      shader.uniforms.uWind = u.uWind;
      shader.vertexShader = shader.vertexShader
        .replace('#include <common>', '#include <common>\nattribute float aCell;\nuniform float uTime;\nuniform float uWind;')
        .replace('#include <uv_vertex>', `#include <uv_vertex>\nvMapUv = vec2( ( clamp( uv.x, 0.01, 0.99 ) + aCell ) / ${cells}.0, uv.y );`)
        .replace('#include <begin_vertex>', [
          '#include <begin_vertex>',
          'float sway = sin( uTime * 0.7 + aCell * 1.7 + instanceMatrix[3].x * 0.05 ) * 0.012 * ( 0.4 + uWind ) * uv.y * uv.y;',
          'transformed.x += sway; transformed.z += sway * 0.6;'
        ].join('\n'));
      shader.fragmentShader = shader.fragmentShader
        .replace('#include <alphatest_fragment>', 'diffuseColor.a = min( 1.0, diffuseColor.a * 2.2 );\n#include <alphatest_fragment>');
    };

    const mesh = new THREE.InstancedMesh(geo, mat, spots.length);
    const cell = new Float32Array(spots.length);
    const m = new THREE.Matrix4(), q = new THREE.Quaternion(), s = new THREE.Vector3(), p = new THREE.Vector3();
    const up = new THREE.Vector3(0, 1, 0), col = new THREE.Color();
    spots.forEach(([x, h, z], i) => {
      const height = 7 + Math.pow(rnd(), 1.5) * 12;
      q.setFromAxisAngle(up, rnd() * Math.PI);
      s.set(height * 0.56, height, height * 0.56);
      p.set(x, h - 0.25, z);
      mesh.setMatrixAt(i, m.compose(p, q, s));
      cell[i] = Math.floor(rnd() * cells);
      const shade = 0.72 + rnd() * 0.5;
      mesh.setColorAt(i, col.setRGB(shade, shade * (0.95 + rnd() * 0.1), shade * 0.92));
    });
    geo.setAttribute('aCell', new THREE.InstancedBufferAttribute(cell, 1));
    mesh.frustumCulled = false;
    this.scene.add(mesh);
    this.trees = mesh;
  }

  /* ---------- rocks, ferns, tufts: scanned meshes, instanced ---------- */

  _buildScatter() {
    const rnd = mulberry(7);
    const m = new THREE.Matrix4(), q = new THREE.Quaternion(), s = new THREE.Vector3(), p = new THREE.Vector3();
    const e = new THREE.Euler();

    const scatter = (root, perMesh, place, opts = {}) => {
      const meshes = [];
      root.traverse((o) => { if (o.isMesh) meshes.push(o); });
      for (const src of meshes) {
        const geo = src.geometry.clone();
        geo.computeBoundingBox();
        const minY = geo.boundingBox.min.y;
        const inst = new THREE.InstancedMesh(geo, src.material, perMesh);
        let n = 0, guard = 0;
        while (n < perMesh && guard++ < perMesh * 60) {
          const spot = place();
          if (!spot) continue;
          const [x, z, scale] = spot;
          const h = this.heightAt(x, z);
          e.set(opts.tilt ? (rnd() - 0.5) * 0.5 : 0, rnd() * Math.PI * 2, opts.tilt ? (rnd() - 0.5) * 0.5 : 0);
          q.setFromEuler(e);
          s.setScalar(scale);
          p.set(x, h - minY * scale - (opts.sink || 0) * scale, z);
          inst.setMatrixAt(n++, m.compose(p, q, s));
        }
        inst.count = n;
        inst.castShadow = !!opts.shadow;
        inst.receiveShadow = !!opts.shadow;
        inst.frustumCulled = false;
        if (opts.detail) inst.layers.set(LAYER_DETAIL);
        this.scene.add(inst);
      }
    };

    // rocks: most along the waterline, a few in the meadow
    const rocks = this.models.rock_moss_set_01;
    if (rocks) {
      scatter(rocks, 16, () => {
        const x = (rnd() - 0.5) * 150, z = -70 + rnd() * 110;
        const h = this.heightAt(x, z);
        const nearShore = h > -0.5 && h < 1.0;
        const meadow = h > 1.0 && rnd() < 0.05 && Math.hypot(x - HOME.x, z - HOME.z) < 60;
        if (!nearShore && !meadow) return null;
        return [x, z, 0.18 + Math.pow(rnd(), 2.2) * 1.1];
      }, { tilt: true, shadow: true, sink: 0.3 });
    }

    const ferns = this.models.fern_02;
    if (ferns) {
      ferns.traverse((o) => { if (o.isMesh) { o.material.alphaTest = 0.5; o.material.side = THREE.DoubleSide; } });
      scatter(ferns, 70, () => {
        const a = rnd() * Math.PI * 2, r = 6 + Math.pow(rnd(), 0.8) * 50;
        const x = HOME.x + Math.cos(a) * r, z = HOME.z + Math.sin(a) * r;
        if (this.heightAt(x, z) < 1.0) return null;
        if (fbm(x * 0.08 + 3, z * 0.08) < 0.5) return null;                 // ferns grow in patches
        return [x, z, 0.9 + rnd() * 1.3];
      }, { detail: true });
    }

    const tufts = this.models.shrub_03;
    if (tufts) {
      tufts.traverse((o) => { if (o.isMesh) { o.material.alphaTest = 0.5; o.material.side = THREE.DoubleSide; } });
      scatter(tufts, 90, () => {
        const a = rnd() * Math.PI * 2, r = 3 + Math.pow(rnd(), 0.7) * 38;
        const x = HOME.x + Math.cos(a) * r, z = HOME.z + Math.sin(a) * r;
        if (this.heightAt(x, z) < 0.7) return null;
        return [x, z, 1.4 + rnd() * 1.8];
      }, { detail: true });
    }
  }

  /* ---------- grass: instanced blades that follow the camera ---------- */

  _buildGrass() {
    const BLADES = 140000, PATCH = 40, SEGS = 3;
    const base = new THREE.BufferGeometry();
    const pos = [], idx = [];
    for (let i = 0; i <= SEGS; i++) {
      const t = i / SEGS;
      if (i < SEGS) pos.push(-0.5, t, 0, 0.5, t, 0);
      else pos.push(0, t, 0);
    }
    for (let i = 0; i < SEGS - 1; i++) {
      const a = i * 2;
      idx.push(a, a + 1, a + 2, a + 1, a + 3, a + 2);
    }
    const last = (SEGS - 1) * 2;
    idx.push(last, last + 1, last + 2);
    base.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
    base.setIndex(idx);

    const geo = new THREE.InstancedBufferGeometry();
    geo.index = base.index;
    geo.attributes.position = base.attributes.position;
    geo.instanceCount = BLADES;
    const off = new Float32Array(BLADES * 2), rand = new Float32Array(BLADES * 4);
    const rnd = mulberry(3);
    for (let i = 0; i < BLADES; i++) {
      off[i * 2] = rnd() * PATCH; off[i * 2 + 1] = rnd() * PATCH;
      rand[i * 4] = rnd(); rand[i * 4 + 1] = rnd(); rand[i * 4 + 2] = rnd(); rand[i * 4 + 3] = rnd();
    }
    geo.setAttribute('aOffset', new THREE.InstancedBufferAttribute(off, 2));
    geo.setAttribute('aRand', new THREE.InstancedBufferAttribute(rand, 4));

    const u = this.uniforms;
    const mat = new THREE.ShaderMaterial({
      side: THREE.DoubleSide,
      uniforms: {
        uTime: u.uTime, uWind: u.uWind, uCam: u.uCam, uSunDir: u.uSunDir, uSunColor: u.uSunColor,
        uAmbient: u.uAmbient, uFogColor: u.uFogColor, uFogDensity: u.uFogDensity,
        uHeight: { value: this.heightTex }, uTint: { value: this.tex.grassDiff },
        uPatch: { value: PATCH }, uWorld: { value: WORLD }, uGrid: { value: SEG }
      },
      vertexShader: `
        attribute vec2 aOffset;
        attribute vec4 aRand;
        uniform float uTime, uWind, uPatch, uWorld, uGrid;
        uniform vec3 uCam;
        uniform sampler2D uHeight;
        varying float vT;
        varying vec3 vNormal;
        varying vec3 vWorld;
        varying vec2 vRand;
        float texel( vec2 ij ) { return texture2D( uHeight, ( ij + 0.5 ) / ( uGrid + 1.0 ) ).r; }
        float groundAt( vec2 xz ) {
          vec2 g = clamp( ( xz / uWorld + 0.5 ) * uGrid, 0.0, uGrid - 0.001 );
          vec2 i = floor( g ), f = g - i;
          float a = texel( i ), b = texel( i + vec2( 1.0, 0.0 ) ), c = texel( i + vec2( 0.0, 1.0 ) ), d = texel( i + vec2( 1.0 ) );
          return mix( mix( a, b, f.x ), mix( c, d, f.x ), f.y );
        }
        void main() {
          vec2 base = mod( aOffset - uCam.xz, uPatch ) - 0.5 * uPatch + uCam.xz;
          float dist = length( base - uCam.xz );
          float ground = groundAt( base );
          float keep = ( 1.0 - smoothstep( uPatch * 0.34, uPatch * 0.49, dist ) )
                     * smoothstep( 0.4, 0.9, ground )
                     * step( abs( base.x ), uWorld * 0.49 ) * step( abs( base.y ), uWorld * 0.49 );
          float clump = 0.55 + 0.45 * sin( base.x * 0.9 + sin( base.y * 0.7 ) * 2.0 ) * sin( base.y * 1.1 );
          float height = ( 0.22 + aRand.y * 0.5 ) * ( 0.65 + 0.55 * clump ) * keep;
          float t = position.y;
          float ang = aRand.z * 6.2832;
          vec2 side = vec2( cos( ang ), sin( ang ) );
          vec2 lean = vec2( -side.y, side.x );
          float gust = sin( uTime * 1.3 + base.x * 0.35 + base.y * 0.27 ) * 0.5
                     + sin( uTime * 0.63 + base.x * 0.11 - base.y * 0.19 ) * 0.5;
          float bend = ( 0.2 + aRand.w * 0.45 ) * t * t;
          vec2 push = lean * bend + vec2( 0.8, 0.45 ) * gust * uWind * 0.55 * t * t;
          vec3 p = vec3( base.x, ground, base.y );
          p.xz += side * position.x * 0.03 * ( 1.0 - t * 0.7 ) * ( 0.7 + aRand.x * 0.6 );
          p.xz += push * height;
          p.y += t * height * ( 1.0 - 0.35 * length( push ) );
          vT = t;
          vRand = aRand.xy;
          vNormal = normalize( vec3( lean.x * 0.6 + side.x * position.x * 0.8, 0.75, lean.y * 0.6 + side.y * position.x * 0.8 ) );
          vWorld = p;
          gl_Position = projectionMatrix * viewMatrix * vec4( p, 1.0 );
        }`,
      fragmentShader: `
        uniform vec3 uCam, uSunDir, uSunColor, uAmbient, uFogColor;
        uniform float uFogDensity;
        uniform sampler2D uTint;
        varying float vT;
        varying vec3 vNormal;
        varying vec3 vWorld;
        varying vec2 vRand;
        void main() {
          vec3 soil = texture2D( uTint, vWorld.xz * 0.021 ).rgb;
          vec3 root = vec3( 0.018, 0.04, 0.012 );
          vec3 tip = mix( vec3( 0.09, 0.16, 0.035 ), vec3( 0.17, 0.19, 0.06 ), vRand.x * vRand.x );
          vec3 albedo = mix( root, tip, vT * vT ) * ( 0.7 + soil * 2.2 );
          vec3 n = normalize( vNormal );
          float wrap = clamp( dot( n, uSunDir ) * 0.6 + 0.4, 0.0, 1.0 );
          vec3 view = normalize( uCam - vWorld );
          float through = pow( clamp( dot( -view, uSunDir ), 0.0, 1.0 ), 5.0 ) * vT;
          vec3 col = albedo * ( uAmbient + uSunColor * ( wrap + through * 1.6 ) );
          col *= mix( 0.45, 1.0, vT );                         // blades shade their own roots
          float d = length( vWorld - uCam );
          float fog = 1.0 - exp( -uFogDensity * uFogDensity * d * d );
          gl_FragColor = vec4( mix( col, uFogColor, fog ), 1.0 );
          #include <tonemapping_fragment>
          #include <colorspace_fragment>
        }`
    });
    if (this.small) geo.instanceCount = BLADES * 0.5;
    this.grass = new THREE.Mesh(geo, mat);
    this.grass.frustumCulled = false;
    this.grass.layers.set(LAYER_DETAIL);
    this.scene.add(this.grass);
  }

  /* ---------- fireflies and rain ---------- */

  _buildFireflies() {
    const N = 260, rnd = mulberry(99);
    const pos = new Float32Array(N * 3), seed = new Float32Array(N * 3);
    for (let i = 0; i < N; i++) {
      const a = rnd() * Math.PI * 2, r = 4 + Math.pow(rnd(), 0.7) * 46;
      const x = HOME.x + Math.cos(a) * r, z = HOME.z - 6 + Math.sin(a) * r;
      pos[i * 3] = x; pos[i * 3 + 1] = Math.max(0.3, this.heightAt(x, z)) + 0.3 + rnd() * 2.4; pos[i * 3 + 2] = z;
      seed[i * 3] = rnd() * 100; seed[i * 3 + 1] = 0.4 + rnd(); seed[i * 3 + 2] = rnd();
    }
    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    geo.setAttribute('aSeed', new THREE.BufferAttribute(seed, 3));
    const u = this.uniforms;
    const mat = new THREE.ShaderMaterial({
      transparent: true, depthWrite: false, blending: THREE.AdditiveBlending,
      uniforms: { uTime: u.uTime, uPulse: u.uPulse, uFireflies: u.uFireflies, uScale: { value: 1 } },
      vertexShader: `
        attribute vec3 aSeed;
        uniform float uTime, uPulse, uFireflies, uScale;
        varying float vGlow;
        void main() {
          vec3 p = position;
          float t = uTime * 0.22 * aSeed.y + aSeed.x;
          p.x += sin( t ) * 1.4 + sin( t * 2.3 ) * 0.4;
          p.z += cos( t * 0.8 ) * 1.4;
          p.y += sin( t * 1.7 ) * 0.35;
          float blink = smoothstep( 0.35, 1.0, sin( uTime * ( 0.5 + aSeed.z ) + aSeed.x * 6.0 ) );
          vGlow = ( blink * 0.8 + uPulse * 0.9 * aSeed.z ) * uFireflies;
          vec4 mv = viewMatrix * vec4( p, 1.0 );
          gl_Position = projectionMatrix * mv;
          gl_PointSize = uScale * ( 0.09 + 0.05 * uPulse ) / -mv.z;
        }`,
      fragmentShader: `
        varying float vGlow;
        void main() {
          float d = length( gl_PointCoord - 0.5 );
          float a = exp( -d * d * 22.0 ) * vGlow;
          gl_FragColor = vec4( vec3( 1.0, 0.86, 0.42 ) * a * 2.2, a );
          #include <tonemapping_fragment>
          #include <colorspace_fragment>
        }`
    });
    this.fireflies = new THREE.Points(geo, mat);
    this.fireflies.frustumCulled = false;
    this.fireflies.layers.set(LAYER_DETAIL);
    this.scene.add(this.fireflies);
  }

  _buildRain() {
    const N = 2200, BOX = 26, rnd = mulberry(5);
    const pos = new Float32Array(N * 6), end = new Float32Array(N * 2);
    for (let i = 0; i < N; i++) {
      const x = rnd() * BOX, y = rnd() * BOX, z = rnd() * BOX;
      pos.set([x, y, z, x, y, z], i * 6);
      end[i * 2] = 0; end[i * 2 + 1] = 1;
    }
    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    geo.setAttribute('aEnd', new THREE.BufferAttribute(end, 1));
    const u = this.uniforms;
    const mat = new THREE.ShaderMaterial({
      transparent: true, depthWrite: false,
      uniforms: { uTime: u.uTime, uCam: u.uCam, uRain: u.uRain, uFogColor: u.uFogColor, uBox: { value: BOX } },
      vertexShader: `
        attribute float aEnd;
        uniform float uTime, uBox;
        uniform vec3 uCam;
        varying float vA;
        void main() {
          vec3 p = position;
          p.y -= uTime * 17.0;
          p.x -= uTime * 1.5;
          p = mod( p - uCam, uBox ) - 0.5 * uBox + uCam;
          p.y += aEnd * 0.5; p.x += aEnd * 0.045;
          vA = 1.0 - aEnd * 0.6;
          gl_Position = projectionMatrix * viewMatrix * vec4( p, 1.0 );
        }`,
      fragmentShader: `
        uniform float uRain;
        uniform vec3 uFogColor;
        varying float vA;
        void main() {
          gl_FragColor = vec4( uFogColor * 1.6 + 0.25, vA * 0.34 * uRain );
          #include <tonemapping_fragment>
          #include <colorspace_fragment>
        }`
    });
    this.rain = new THREE.LineSegments(geo, mat);
    this.rain.frustumCulled = false;
    this.rain.layers.set(LAYER_DETAIL);
    this.rain.visible = false;
    this.scene.add(this.rain);
  }

  /* ---------- modes ---------- */

  _applyMode(mode) {
    const c = MODES[mode];
    this.mode = mode;
    // turn the sky so the moon or sun hangs over the far shore, a little right of centre
    const az = (c.sunUV[0] - 0.5) * Math.PI * 2;
    const el = (0.5 - c.sunUV[1]) * Math.PI;
    const want = Math.atan2(-1, 0.28);
    const rot = az - want;
    this.scene.background = this.under ? this.uwBackground : this.tex.sky[mode];
    this.scene.environment = this.env[mode];
    this.scene.backgroundRotation.set(0, rot, 0);
    this.scene.environmentRotation.set(0, rot, 0);
    this.scene.backgroundIntensity = c.background;
    this.scene.environmentIntensity = c.env;

    const dir = new THREE.Vector3(Math.cos(el) * Math.cos(want), Math.sin(Math.max(el, 0.2)), Math.cos(el) * Math.sin(want)).normalize();
    this.sunDir = dir;
    this.light.color.set(c.light);
    this.light.intensity = c.lightPower;
    this.scene.fog.color.set(c.fog);
    this.scene.fog.density = c.fogDensity;

    const u = this.uniforms;
    u.uSunDir.value.copy(dir);
    u.uSunColor.value.set(c.light).multiplyScalar(c.lightPower * 0.32);
    u.uAmbient.value.setRGB(...c.grassAmbient);
    u.uFogColor.value.set(c.fog);
    u.uFogDensity.value = c.fogDensity;
    u.uFireflies.value = c.fireflies;

    const w = this.water.material.uniforms;
    w.sunDirection.value.copy(dir);
    w.sunColor.value.set(c.light);
    w.waterColor.value.set(c.water);
    this.baseExposure = c.exposure;
  }

  setMode(mode) {
    if (!MODES[mode] || mode === this.mode && !this.pendingMode) return;
    if (!this.ready) { this.mode = mode; return; }
    this.pendingMode = mode;        // dip to black, swap, come back
    this.fadeTarget = 0;
  }

  getMode() { return this.pendingMode || this.mode; }

  /* ---------- music and weather ---------- */

  pulseNote(note) {
    if (!note) return;
    const v = Math.min(1, Math.max(0.1, note.velocity || 0.5));
    const reg = note.register || (note.midi < 48 ? 'bass' : note.midi < 72 ? 'mid' : 'treble');
    this.pulse[reg] = Math.min(1.2, (this.pulse[reg] || 0) + v * 0.7);
  }

  setWeather(w) {
    if (!w) return;
    if (typeof w.rain === 'number') this.weather.rain = w.rain;
    if (typeof w.wind === 'number') this.weather.wind = w.wind;
  }

  lightning(intensity = 0.8) { this.flash = Math.max(this.flash, intensity); }

  onInteract(cb) { this.interactCb = cb; }

  /** Go under (true) or come up (false). Only works where the water is deep enough. */
  setDive(on) {
    if (on && this.immersion.swim < 0.5) return;
    this.diveWant = on;
    this.lastInput = performance.now();
  }
  toggleDive() { this.setDive(!this.diveWant); }

  /* ---------- input ---------- */

  _bind() {
    const el = this.renderer.domElement;
    el.style.touchAction = 'none';
    const pointers = new Map();
    let moved = 0, pinch = 0;

    el.addEventListener('pointerdown', (e) => {
      el.setPointerCapture(e.pointerId);
      pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
      moved = 0;
      if (pointers.size === 2) {
        const [a, b] = [...pointers.values()];
        pinch = Math.hypot(a.x - b.x, a.y - b.y);
      }
    });
    el.addEventListener('pointermove', (e) => {
      const p = pointers.get(e.pointerId);
      if (!p) return;
      const dx = e.clientX - p.x, dy = e.clientY - p.y;
      p.x = e.clientX; p.y = e.clientY;
      moved += Math.abs(dx) + Math.abs(dy);
      if (pointers.size === 1) {
        const k = 0.0032 * (this.camera.fov / 50);
        this.cam.yaw += dx * k;
        this.cam.pitch = Math.min(0.9, Math.max(-0.7, this.cam.pitch + dy * k));
      } else if (pointers.size === 2) {
        const [a, b] = [...pointers.values()];
        const d = Math.hypot(a.x - b.x, a.y - b.y);
        this._walk((d - pinch) * 0.03, 0);              // spread two fingers to step forward
        pinch = d;
      }
      this.lastInput = performance.now();
      el.classList.add('is-dragging');
    });
    const up = (e) => {
      if (!pointers.has(e.pointerId)) return;
      pointers.delete(e.pointerId);
      el.classList.remove('is-dragging');
      if (moved < 5 && e.type === 'pointerup' && pointers.size === 0) {
        const spot = this._pick(e.clientX, e.clientY);
        if (spot) {
          this.goal = spot;
          this.marker.position.set(spot.x, Math.max(this.heightAt(spot.x, spot.z), 0) + 0.06, spot.z);
          this.marker.material.opacity = 0.9;
          this.lastInput = performance.now();
        } else if (this.interactCb) {
          this.pulse.treble = 1;
          this.interactCb({ x: e.clientX / el.clientWidth, y: e.clientY / el.clientHeight });
        }
      }
    };
    el.addEventListener('pointerup', up);
    el.addEventListener('pointercancel', up);
    el.addEventListener('wheel', (e) => {
      e.preventDefault();
      this._walk(-e.deltaY * 0.012, 0);
      this.lastInput = performance.now();
    }, { passive: false });

    const WALK = { KeyW: 1, ArrowUp: 1, KeyS: 1, ArrowDown: 1, KeyA: 1, ArrowLeft: 1, KeyD: 1, ArrowRight: 1, ShiftLeft: 1, ShiftRight: 1 };
    window.addEventListener('keydown', (e) => {
      const tag = e.target && e.target.tagName;
      if (tag === 'INPUT' || tag === 'SELECT' || tag === 'TEXTAREA' || e.metaKey || e.ctrlKey) return;
      if (e.code === 'KeyQ') this.setDive(true);
      if (e.code === 'KeyE') this.setDive(false);
      if (WALK[e.code]) { this.keys.add(e.code); this.goal = null; if (e.code.startsWith('Arrow')) e.preventDefault(); }
    });
    window.addEventListener('keyup', (e) => this.keys.delete(e.code));
    window.addEventListener('blur', () => this.keys.clear());

    new ResizeObserver(() => this._resize()).observe(this.container);
  }

  _resize() {
    const w = this.container.clientWidth, h = this.container.clientHeight;
    if (!w || !h) return;
    this.renderer.setSize(w, h);
    this.camera.aspect = w / h;
    this.camera.fov = w < h ? 62 : 50;         // wider on a phone held upright
    this.restPitch = w < h ? -0.06 : 0.015;     // upright screens look down a little, to keep the shore in frame
    if (performance.now() - this.lastInput > 9000 && this.reduced) this.cam.pitch = this.restPitch;
    this.camera.updateProjectionMatrix();
  }

  /** Ground point under a screen position, or null for sky, water and far hills. */
  _pick(cx, cy) {
    const r = this.renderer.domElement.getBoundingClientRect();
    this.ray.setFromCamera(new THREE.Vector2(((cx - r.left) / r.width) * 2 - 1, 1 - ((cy - r.top) / r.height) * 2), this.camera);
    const o = this.ray.ray.origin, d = this.ray.ray.direction;
    if (d.y > -0.015) return null;
    for (let t = 1; t < 90; t += 0.4 + t * 0.02) {
      const x = o.x + d.x * t, z = o.z + d.z * t;
      const h = this.heightAt(x, z);
      if (o.y + d.y * t <= Math.max(h, 0)) return h > -WADE_MAX && Math.hypot(x, z) < WALK_LIMIT ? { x, z } : null;
    }
    return null;
  }

  /** Step along the view direction. */
  _walk(forward, strafe) {
    const sin = Math.sin(this.cam.yaw), cos = Math.cos(this.cam.yaw);
    return this._move(sin * forward + cos * strafe, -cos * forward + sin * strafe);
  }

  /** Step in world space; stays on land and inside the map. Returns false when blocked. */
  _move(dx, dz) {
    const nx = this.cam.x + dx;
    const nz = this.cam.z + dz;
    const ok = (x, z) => this.heightAt(x, z) > MIN_H && Math.hypot(x, z) < WALK_LIMIT;
    if (ok(nx, nz)) { this.cam.x = nx; this.cam.z = nz; }
    else if (ok(nx, this.cam.z)) this.cam.x = nx;       // slide along the shore
    else if (ok(this.cam.x, nz)) this.cam.z = nz;
    else return false;
    return true;
  }

  /* ---------- frame ---------- */

  /** Under ~35 fps for two seconds: drop resolution, then thin the grass. Three steps at most. */
  _adapt(raw) {
    const q = this.slow;
    if (q.steps >= 3 || raw > 0.25 || document.hidden) return;   // a stalled or hidden tab is not a slow GPU
    q.time += raw;
    if (++q.frames < 70) return;
    const avg = q.time / q.frames;
    q.time = 0; q.frames = 0;
    if (avg < 0.028) return;
    q.steps++;
    this.pixelRatio = Math.max(0.75, this.pixelRatio * 0.8);
    this.renderer.setPixelRatio(this.pixelRatio);
    this.grass.geometry.instanceCount = Math.floor(this.grass.geometry.instanceCount * 0.7);
    this._resize();
  }

  _frame() {
    const raw = this.clock.getDelta();
    this._adapt(raw);
    const dt = Math.min(0.05, raw);
    const t = this.clock.elapsedTime;
    const u = this.uniforms;

    // water: wading slows you, deep water floats you, Q goes under
    const h0 = this.heightAt(this.cam.x, this.cam.z);
    const depth = Math.max(0, -h0);
    const swimW = smooth(0.5, 1.3, depth);
    if (swimW < 0.45) this.diveWant = false;
    this.dive += ((this.diveWant ? 1 : 0) - this.dive) * Math.min(1, dt * 2.2);
    const drag = 1 - 0.45 * smooth(0, 1.4, depth);

    // walking
    let f = 0, s = 0;
    const k = this.keys;
    const run = k.has('ShiftLeft') || k.has('ShiftRight');
    if (k.has('KeyW') || k.has('ArrowUp')) f += 1;
    if (k.has('KeyS') || k.has('ArrowDown')) f -= 1;
    if (k.has('KeyD')) s += 1;
    if (k.has('KeyA')) s -= 1;
    const turn = (k.has('ArrowRight') ? 1 : 0) - (k.has('ArrowLeft') ? 1 : 0);
    if (turn) { this.cam.yaw += turn * 1.5 * dt; this.lastInput = performance.now(); }
    if (f || s) {
      const speed = (run ? 5.5 : 2.4) * drag * dt / Math.hypot(f, s);
      this._walk(f * speed, s * speed);
      this.lastInput = performance.now();
    }
    if (this.goal) {
      const gx = this.goal.x - this.cam.x, gz = this.goal.z - this.cam.z, gd = Math.hypot(gx, gz);
      const step = Math.min(gd, 3.4 * drag * dt);
      if (gd < 0.5 || !this._move(gx / gd * step, gz / gd * step)) this.goal = null;
      else { f = 1; this.lastInput = performance.now(); }
    }
    this.marker.material.opacity *= this.goal ? 1 : Math.exp(-dt * 6);
    this.marker.scale.setScalar(1 + Math.sin(t * 5) * 0.08);

    // idle: drift along a lemniscate through the meadow, facing the lake
    const idle = performance.now() - this.lastInput > 12000;
    if (!idle) { this.base.x = this.cam.x; this.base.z = this.cam.z; this.glideT = 0; }
    if (idle && !this.reduced) {
      this.glideT += dt;
      const ph = this.glideT * (Math.PI * 2 / 150);
      const den = 1 + Math.cos(ph) * Math.cos(ph);
      const tx = this.base.x + 9 * Math.sin(ph) / den;
      const tz = this.base.z + 6 * Math.sin(ph) * Math.cos(ph) / den;
      const ease = 1 - Math.exp(-dt * 0.35);
      if (this.heightAt(tx, tz) > 0.4) {
        this.cam.x += (tx - this.cam.x) * ease;
        this.cam.z += (tz - this.cam.z) * ease;
      }
      const wantYaw = Math.atan2(LAKE.x + 6 - this.cam.x, -(LAKE.z - this.cam.z)) + Math.sin(t * 0.05) * 0.22;
      let dy = wantYaw - this.cam.yaw;
      dy = Math.atan2(Math.sin(dy), Math.cos(dy));
      this.cam.yaw += dy * ease * 0.6;
      this.cam.pitch += (this.restPitch + Math.sin(t * 0.07) * 0.02 - this.cam.pitch) * ease * 0.6;
    }

    const wadeY = h0 + EYE;
    const diveY = Math.min(-0.3, Math.max(h0 + 0.55, -2.4));
    const surfY = lerp(wadeY, 0.3, swimW);
    const ground = lerp(surfY, diveY, this.dive * swimW);
    this.cam.y += (ground - this.cam.y) * (this.cam.y ? 1 - Math.exp(-dt * (this.dive > 0.02 ? 3 : 8)) : 1);
    const moving = !!(f || s);
    const bob = (moving ? Math.sin(t * 9) * 0.025 * (1 - swimW) : Math.sin(t * 0.8) * 0.008) + Math.sin(t * 1.3) * 0.035 * swimW * (1 - this.dive);
    this.camera.position.set(this.cam.x, this.cam.y + bob, this.cam.z);
    this.camera.rotation.set(this.cam.pitch, -this.cam.yaw, 0);

    // under the surface: murk, a bright ceiling, no sky
    const under = this.camera.position.y < -0.04;
    if (under !== this.under) {
      this.under = under;
      this.water.visible = !under;
      this.ceiling.visible = under;
      this.scene.background = under ? this.uwBackground : this.tex.sky[this.mode];
    }
    this.uw += ((under ? 1 : 0) - this.uw) * Math.min(1, dt * 7);
    const mc = MODES[this.mode], uc = UNDERWATER[this.mode];
    this._fog.set(mc.fog).lerp(this._uwFog.set(uc.fog), this.uw);
    const fogDensity = lerp(mc.fogDensity, uc.density, this.uw);
    this.scene.fog.color.copy(this._fog);
    this.scene.fog.density = fogDensity;
    this.uniforms.uFogColor.value.copy(this._fog);
    this.uniforms.uFogDensity.value = fogDensity;
    this.uwBackground.set(uc.fog);
    this.ceiling.material.color.set(uc.ceiling);

    const im = this.immersion;
    im.depth = depth; im.swim = swimW; im.under = under; im.uw = this.uw;
    im.dive = this.diveWant && swimW >= 0.45; im.moving = moving || !!this.goal; im.run = run;
    if (this.options.onImmersion) this.options.onImmersion(im);

    // pulses decay like a struck string
    const p = this.pulse;
    p.bass *= Math.exp(-dt * 1.6); p.mid *= Math.exp(-dt * 2.4); p.treble *= Math.exp(-dt * 3.2);
    this.flash *= Math.exp(-dt * 5);

    // mode change: dip, swap, return
    const fadeRate = this.reduced ? 60 : 4.5;
    this.fade += Math.sign(this.fadeTarget - this.fade) * Math.min(Math.abs(this.fadeTarget - this.fade), dt * fadeRate);
    if (this.pendingMode && this.fade <= 0.001) {
      this._applyMode(this.pendingMode);
      this.pendingMode = null;
      this.fadeTarget = 1;
    }

    const wind = 0.25 + this.weather.wind * 0.75 + this.weather.rain * 0.3;
    u.uTime.value = t;
    u.uWind.value += (wind - u.uWind.value) * Math.min(1, dt * 0.8);
    u.uCam.value.copy(this.camera.position);
    u.uPulse.value = Math.min(1, p.treble + p.mid * 0.6);
    u.uRain.value += (this.weather.rain - u.uRain.value) * Math.min(1, dt * 0.7);
    this.rain.visible = u.uRain.value > 0.02 && !under;
    this.fireflies.visible = !under;
    this.fireflies.material.uniforms.uScale.value = this.renderer.domElement.height * 1.2;

    const wu = this.water.material.uniforms;
    wu.time.value = t * 0.22;
    wu.distortionScale.value = 0.8 + u.uWind.value * 0.9 + p.bass * 0.7 + u.uRain.value * 1.2;

    this.light.intensity = MODES[this.mode].lightPower + this.flash * 9;
    this.light.target.position.set(this.cam.x, this.cam.y - EYE, this.cam.z);
    this.light.position.copy(this.light.target.position).addScaledVector(this.sunDir, 110);
    this.renderer.toneMappingExposure = this.baseExposure * this.fade * (1 + this.flash * 1.4);

    this.renderer.render(this.scene, this.camera);
  }
}

window.NocturneWorld = NocturneWorld;
