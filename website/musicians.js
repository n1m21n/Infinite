/* Musicians page: a small patch bay in 3D. Three cables hang between six sockets; drag a plug to another socket and it clicks in with a note.
   The bubbles under it (shared with artists.js, which sends 'ax:pick') re-patch the cables. Reduced motion draws one still frame.
   Plain three.js, one canvas. */
import * as THREE from './vendor/three.module.min.js';

(function () {
  'use strict';
  var stage = document.querySelector('.ax-stage[data-hero="cables"]');
  if (!stage) return;
  var canvas = stage.querySelector('.ax-gl');
  var still = window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  var renderer;
  try { renderer = new THREE.WebGLRenderer({ canvas: canvas, antialias: true, alpha: true, powerPreference: 'low-power' }); }
  catch (err) { stage.classList.add('no-gl'); return; }
  renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
  renderer.setClearColor(0x000000, 0);

  var scene = new THREE.Scene(), rig = new THREE.Group(); scene.add(rig);
  var cam = new THREE.PerspectiveCamera(30, 1, 10, 5000);
  scene.add(new THREE.AmbientLight(0xb8c0ff, 0.9));
  var key = new THREE.DirectionalLight(0xffffff, 2.4); key.position.set(-300, 500, 700); scene.add(key);
  var rim = new THREE.PointLight(0xa7b1ff, 2.2, 1400, 1.4); rim.position.set(400, -200, 260); scene.add(rim);

  /* three presets, one per bubble: which right-hand socket each cable reaches, and the cable colours (node category colours) */
  var PRESETS = [
    { perm: [0, 1, 2], col: [0x4cd964, 0x6992f6, 0x53c7e3] },
    { perm: [1, 2, 0], col: [0xfacc15, 0x53c7e3, 0xa7b1ff] },
    { perm: [2, 0, 1], col: [0xa3e635, 0xa7b1ff, 0x6992f6] }
  ];
  var NOTES = [220, 246.94, 293.66, 329.63, 392, 440];   // A minor pentatonic, one note per socket

  var W = 0, H = 0, R = 24, socks = [], free = { x: 0, y: 0, w: 0, h: 0 };
  var sockGroup = new THREE.Group(); rig.add(sockGroup);
  var sockMat = new THREE.MeshPhysicalMaterial({ color: 0x2a3157, roughness: 0.35, metalness: 0.2, clearcoat: 0.6 });
  var holeMat = new THREE.MeshStandardMaterial({ color: 0x0b0e1c, roughness: 0.9 });
  var ledGeo = new THREE.SphereGeometry(1, 12, 8);

  /* cables */
  var N = 24, cables = [];
  function mkCable(i) {
    var c = { i: i, pts: [], prev: [], col: new THREE.Color(PRESETS[0].col[i]), tcol: new THREE.Color(PRESETS[0].col[i]), len: 300, mesh: null, ends: [i, 3 + PRESETS[0].perm[i]], plug: [], grab: [] };
    c.mat = new THREE.MeshPhysicalMaterial({ color: c.col, roughness: 0.32, metalness: 0.0, clearcoat: 0.9, clearcoatRoughness: 0.2 });
    for (var k = 0; k < 2; k++) {
      var g = new THREE.Group();
      var body = new THREE.Mesh(new THREE.CylinderGeometry(1, 1, 1, 20), new THREE.MeshPhysicalMaterial({ color: 0xdfe4f5, roughness: 0.25, metalness: 0.85 }));
      var collar = new THREE.Mesh(new THREE.CylinderGeometry(1, 1, 1, 20), c.mat);
      g.add(body); g.add(collar); g.userData = { body: body, collar: collar, x: 0, y: 0, vx: 0, vy: 0, drag: false, tx: 0, ty: 0 };
      rig.add(g); c.plug.push(g);
      var h = document.createElement('div'); h.className = 'mx-grab'; h.setAttribute('aria-hidden', 'true'); stage.appendChild(h); c.grab.push(h);
      bindGrab(c, k, h);
    }
    for (var j = 0; j < N; j++) { c.pts.push(new THREE.Vector3()); c.prev.push(new THREE.Vector3()); }
    cables.push(c); return c;
  }
  for (var ci = 0; ci < 3; ci++) mkCable(ci);

  function build() {
    var sr = stage.getBoundingClientRect(); W = sr.width; H = sr.height; if (!W || !H) return;
    var copy = stage.querySelector('.ax-copy'), low = stage.querySelector('.ax-low'), cta = stage.querySelector('.ax-cta');
    var x0 = 12, x1 = W - 12, y0, y1;
    if (W >= 900) { x0 = W * 0.4; y0 = 64; y1 = (cta ? cta.getBoundingClientRect().top - sr.top : H) - 16; }
    else { y0 = (copy ? copy.getBoundingClientRect().bottom - sr.top : 0) + 8; y1 = (low ? low.getBoundingClientRect().top - sr.top : H) - 8; }
    y1 = Math.max(y1, y0 + 150);
    free = { x: (x0 + x1) / 2 - W / 2, y: H / 2 - (y0 + y1) / 2, w: x1 - x0, h: y1 - y0 };   // centre in world units (1 unit = 1 css px at z = 0)
    renderer.setSize(W, H, false); cam.aspect = W / H; cam.position.set(0, 0, (H / 2) / Math.tan(THREE.MathUtils.degToRad(cam.fov / 2))); cam.updateProjectionMatrix();
    R = Math.max(14, Math.min(24, Math.min(free.h / 12, free.w / 12)));
    var dx = Math.max(R * 3.4, Math.min(free.w * 0.34, 190)), dy = Math.max(R * 2.6, Math.min(R * 3.4, (free.h - 2 * R - 90) / 2));
    sockGroup.clear();
    socks = [];
    for (var s = 0; s < 6; s++) {
      var left = s < 3, row = s % 3, g = new THREE.Group();
      var ring = new THREE.Mesh(new THREE.TorusGeometry(R, R * 0.2, 14, 36), sockMat), hole = new THREE.Mesh(new THREE.CircleGeometry(R * 0.82, 28), holeMat);
      hole.position.z = -R * 0.1;
      var led = new THREE.Mesh(ledGeo, new THREE.MeshBasicMaterial({ color: 0x4cd964 })); led.scale.setScalar(R * 0.12); led.position.set(0, R * 1.5, 0);
      g.add(ring); g.add(hole); g.add(led);
      var px = free.x + (left ? -dx : dx), py = free.y + (1 - row) * dy;
      g.position.set(px, py, 0); sockGroup.add(g);
      socks.push({ x: px, y: py, led: led, g: g });
    }
    cables.forEach(function (c) {
      var a = socks[c.ends[0]], b = socks[c.ends[1]];
      c.len = Math.hypot(a.x - b.x, a.y - b.y) * 1.06 + R * 1.5;
      c.plug.forEach(function (g, k) {
        var u = g.userData; u.body.scale.set(R * 0.5, R * 1.5, R * 0.5); u.body.position.y = R * 0.75; u.collar.scale.set(R * 0.64, R * 0.8, R * 0.64); u.collar.position.y = R * 1.7;
        var sk = socks[c.ends[k]]; u.x = sk.x; u.y = sk.y; u.vx = u.vy = 0;
      });
      for (var j = 0; j < N; j++) { var t = j / (N - 1), p = c.pts[j]; p.set(THREE.MathUtils.lerp(a.x, b.x, t), THREE.MathUtils.lerp(a.y, b.y, t) - Math.sin(t * Math.PI) * c.len * 0.2, 0); c.prev[j].copy(p); }
    });
    if (still) { for (var i = 0; i < 160; i++) step(1 / 60, 0); draw(); }
  }

  var DRAG = null, audio = null;
  function toWorld(e) { var r = stage.getBoundingClientRect(); return [e.clientX - r.left - W / 2, H / 2 - (e.clientY - r.top)]; }
  function ping(s) {
    try {
      audio = audio || new (window.AudioContext || window.webkitAudioContext)();
      if (audio.state === 'suspended') audio.resume();
      var t = audio.currentTime, o = audio.createOscillator(), g = audio.createGain();
      o.type = 'triangle'; o.frequency.value = NOTES[s]; g.gain.setValueAtTime(0.0001, t); g.gain.exponentialRampToValueAtTime(0.09, t + 0.01); g.gain.exponentialRampToValueAtTime(0.0001, t + 0.9);
      o.connect(g); g.connect(audio.destination); o.start(t); o.stop(t + 1);
    } catch (err) { /* sound is a bonus */ }
  }
  function bindGrab(c, k, h) {
    h.addEventListener('pointerdown', function (e) {
      var u = c.plug[k].userData; u.drag = true; DRAG = { c: c, k: k, id: e.pointerId };
      try { h.setPointerCapture(e.pointerId); } catch (err) { /* older WebKit */ }
      stage.classList.add('touched'); h.classList.add('on'); var w = toWorld(e); u.tx = w[0]; u.ty = w[1]; e.stopPropagation(); wake();
    });
    h.addEventListener('pointermove', function (e) { if (!DRAG || DRAG.c !== c || DRAG.k !== k) return; var w = toWorld(e), u = c.plug[k].userData; u.tx = w[0]; u.ty = w[1]; wake(); });
    function up() {
      if (!DRAG || DRAG.c !== c || DRAG.k !== k) return;
      var u = c.plug[k].userData; u.drag = false; DRAG = null; h.classList.remove('on');
      var best = -1, bd = R * 2.6;
      socks.forEach(function (s, i) { var d = Math.hypot(s.x - u.x, s.y - u.y); if (d < bd) { bd = d; best = i; } });
      var from = c.ends[k];
      if (best >= 0 && best !== from) {
        cables.forEach(function (o) { [0, 1].forEach(function (m) { if (o.ends[m] === best && !(o === c && m === k)) o.ends[m] = from; }); });
        c.ends[k] = best; ping(best);
      }
      wake();
    }
    h.addEventListener('pointerup', up); h.addEventListener('pointercancel', up);
  }

  function apply(k) {
    var p = PRESETS[k % PRESETS.length];
    cables.forEach(function (c, i) { c.ends = [i, 3 + p.perm[i]]; c.tcol.setHex(p.col[i]); });
    if (still) { for (var i = 0; i < 120; i++) step(1 / 60, 0); draw(); } else wake();
  }
  stage.addEventListener('ax:pick', function (e) { apply(e.detail); ping(e.detail * 2); });

  var tilt = [0, 0], ttilt = [0, 0];
  stage.addEventListener('pointermove', function (e) { var r = stage.getBoundingClientRect(); ttilt = [(e.clientX - r.left) / r.width - 0.5, (e.clientY - r.top) / r.height - 0.5]; wake(); }, { passive: true });

  /* physics: verlet rope between two plugs, plugs on springs */
  function step(dt, t) {
    cables.forEach(function (c) {
      var occupied = [];
      c.plug.forEach(function (g, k) {
        var u = g.userData, sk = socks[c.ends[k]], tx = u.drag ? u.tx : sk.x, ty = u.drag ? u.ty : sk.y;
        var kk = u.drag ? 220 : 120, dd = u.drag ? 22 : 15;
        u.vx += ((tx - u.x) * kk - u.vx * dd) * dt; u.vy += ((ty - u.y) * kk - u.vy * dd) * dt; u.x += u.vx * dt; u.y += u.vy * dt;
      });
      var A = c.plug[0].userData, B = c.plug[1].userData;
      var sway = Math.sin(t * 0.9 + c.i * 2.1) * 26;
      for (var j = 1; j < N - 1; j++) {
        var p = c.pts[j], q = c.prev[j], vx = (p.x - q.x) * 0.985, vy = (p.y - q.y) * 0.985;
        q.copy(p); p.x += vx + sway * dt * dt * 6; p.y += vy - 1500 * dt * dt;
      }
      c.pts[0].set(A.x, A.y, 0); c.pts[N - 1].set(B.x, B.y, 0);
      var seg = c.len / (N - 1);
      for (var it = 0; it < 12; it++) {
        for (var j2 = 0; j2 < N - 1; j2++) {
          var a = c.pts[j2], b = c.pts[j2 + 1], dx = b.x - a.x, dy = b.y - a.y, d = Math.hypot(dx, dy) || 0.0001, diff = (d - seg) / d * 0.5;
          var wa = j2 === 0 ? 0 : 1, wb = j2 + 1 === N - 1 ? 0 : 1, ws = wa + wb || 1;
          a.x += dx * diff * 2 * wa / ws; a.y += dy * diff * 2 * wa / ws; b.x -= dx * diff * 2 * wb / ws; b.y -= dy * diff * 2 * wb / ws;
        }
      }
      for (var sm = 0; sm < 2; sm++) for (var j4 = 1; j4 < N - 1; j4++) { var pa = c.pts[j4 - 1], pb = c.pts[j4 + 1], pc = c.pts[j4]; pc.x += ((pa.x + pb.x) / 2 - pc.x) * 0.18; pc.y += ((pa.y + pb.y) / 2 - pc.y) * 0.18; }   // a little stiffness so the rope bends instead of crumpling
      for (var j3 = 0; j3 < N; j3++) c.pts[j3].z = Math.sin(j3 / (N - 1) * Math.PI) * R * 1.6 + 6;
      c.col.lerp(c.tcol, 1 - Math.exp(-dt * 6)); c.mat.color.copy(c.col);
    });
  }
  function draw() {
    cables.forEach(function (c) {
      if (c.mesh) { c.mesh.geometry.dispose(); }
      var geo = new THREE.TubeGeometry(new THREE.CatmullRomCurve3(c.pts), 56, Math.max(3.2, R * 0.2), 10, false);
      if (!c.mesh) { c.mesh = new THREE.Mesh(geo, c.mat); rig.add(c.mesh); } else c.mesh.geometry = geo;
      c.plug.forEach(function (g, k) {
        var u = g.userData, p0 = c.pts[k ? N - 1 : 0], p1 = c.pts[k ? N - 3 : 2];
        g.position.set(u.x, u.y, 7 + (u.drag ? R * 0.9 : 0));
        var dir = new THREE.Vector3(p0.x - p1.x, p0.y - p1.y, -R * 0.6).normalize();   // from the cable toward its socket
        g.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), dir.negate());   // body extends away from the socket, back along the cable
        var h = c.grab[k]; h.style.transform = 'translate3d(' + (u.x + W / 2 - 28).toFixed(1) + 'px,' + (H / 2 - u.y - 28).toFixed(1) + 'px,0)';
      });
    });
    var used = {}; cables.forEach(function (c) { c.ends.forEach(function (e) { used[e] = c.col; }); });
    socks.forEach(function (s, i) { s.led.material.color.copy(used[i] || new THREE.Color(0x2a3157)); });
    rig.rotation.y = tilt[0] * 0.35; rig.rotation.x = tilt[1] * 0.22;
    renderer.render(scene, cam);
  }

  var raf = 0, last = 0, vis = true, idleT = 0;
  function loop(now) {
    raf = 0; var dt = Math.min(0.033, (now - last) / 1000 || 0.016); last = now; idleT += dt;
    tilt[0] += (ttilt[0] - tilt[0]) * (1 - Math.exp(-dt * 5)); tilt[1] += (ttilt[1] - tilt[1]) * (1 - Math.exp(-dt * 5));
    step(dt, idleT); step(dt, idleT); draw();
    if (!still && vis && !document.hidden) raf = requestAnimationFrame(loop);
    else if (still && (DRAG || Math.abs(tilt[0] - ttilt[0]) > 0.001)) raf = requestAnimationFrame(loop);
  }
  function wake() { if (!raf) raf = requestAnimationFrame(loop); }

  build(); addEventListener('resize', build); if (document.fonts && document.fonts.ready) document.fonts.ready.then(build);
  if ('ResizeObserver' in window) new ResizeObserver(build).observe(stage);
  if ('IntersectionObserver' in window) new IntersectionObserver(function (es) { vis = es[0].isIntersecting; if (vis) wake(); }).observe(stage);
  document.addEventListener('visibilitychange', wake);
  canvas.addEventListener('webglcontextlost', function (e) { e.preventDefault(); stage.classList.add('no-gl'); }, false);
  if (still) draw(); else wake();
})();
