// Grass maze for Sketch 3D. Ground at y = 0, maze centred on the origin.
// Sketch 3D has no image textures, so "grass" is built from geometry and vertex colour:
//   - a patchy lawn floor (noise-tinted tiles with mown stripes)
//   - hedge walls with a leafy colour mix
//   - thousands of tapered blades (dark root to bright tip) on the lawn and hedge tops
// Patch the node into Render 3D. Cable "sway" or "grow" to an LFO for a breeze.

param("cells", 11, 5, 21);          // maze cells per side
param("seed", 7, 1, 999);          // new layout
param("wall_h", 0.9, 0.3, 1.6);    // hedge height
param("wall_t", 0.22, 0.08, 0.4);  // hedge thickness
param("blades", 14, 0, 40);        // lawn blades per cell
param("hedge_fuzz", 5, 0, 12);     // blades on each hedge top segment
param("blade_h", 0.16, 0.05, 0.4); // blade length
param("sway", 0.35, 0, 1);         // wind
param("hue", 0.29, 0.15, 0.45);    // green hue (0.29 fresh, 0.2 dry, 0.38 deep)
param("light", 0.34, 0.15, 0.6);   // overall brightness
param("stripes", 0.12, 0, 0.4);    // mown stripe contrast

// own deterministic hash so layout does not depend on random() call order
function h2(x, y, s) {
  var n = Math.sin(x * 127.1 + y * 311.7 + s * 74.7) * 43758.5453;
  return n - Math.floor(n);
}
function clamp(v, a, b) { return Math.max(a, Math.min(b, v)); }
function green(h, s, l) { fill.apply(null, hsl(h, s, clamp(l, 0, 1))); }
function gv(x, y, z, h, s, l) { green(h, s, l); vertex(x, y, z); }

// ---- maze: recursive backtracker; E[c] / S[c] = wall east / south of cell c -----
function buildMaze(n, seed) {
  var E = [], S = [], vis = [], i, st = [0], x, y;
  for (i = 0; i < n * n; i++) { E.push(1); S.push(1); vis.push(0); }
  vis[0] = 1; var k = 0;
  while (st.length) {
    var c = st[st.length - 1]; x = c % n; y = (c - x) / n;
    var nb = [];
    if (x + 1 < n && !vis[c + 1]) nb.push(0);
    if (y + 1 < n && !vis[c + n]) nb.push(1);
    if (x > 0 && !vis[c - 1]) nb.push(2);
    if (y > 0 && !vis[c - n]) nb.push(3);
    if (!nb.length) { st.pop(); continue; }
    var d = nb[Math.floor(h2(c, k++, seed) * nb.length) % nb.length], t;
    if (d === 0) { E[c] = 0; t = c + 1; }
    else if (d === 1) { S[c] = 0; t = c + n; }
    else if (d === 2) { E[c - 1] = 0; t = c - 1; }
    else { S[c - n] = 0; t = c - n; }
    vis[t] = 1; st.push(t);
  }
  return { E: E, S: S };
}

// one grass blade: tapered triangle (both faces), root colour -> tip colour
function blade(x, y, z, ang, len, bend, hu, wid) {
  var dx = Math.cos(ang), dz = Math.sin(ang), px = -dz, pz = dx;
  var tx = x + dx * bend, tz = z + dz * bend, ty = y + len;
  var lx = x - px * wid, lz = z - pz * wid, rx = x + px * wid, rz = z + pz * wid;
  var rl = 0.17 + 0.05 * hu, tl = 0.40 + 0.18 * hu, hh = ghue + (hu - 0.5) * 0.06;
  gv(lx, y, lz, hh, 0.7, rl); gv(rx, y, rz, hh, 0.7, rl); gv(tx, ty, tz, hh - 0.01, 0.75, tl);
  gv(rx, y, rz, hh, 0.7, rl); gv(lx, y, lz, hh, 0.7, rl); gv(tx, ty, tz, hh - 0.01, 0.75, tl);
}

var ghue = 0.29;

function draw(t) {
  var n = Math.round(cells), cs = 1, half = n * cs / 2;
  var lawnL = light;
  ghue = hue;
  var M = buildMaze(n, Math.round(seed));
  var wt = wall_t, wh = wall_h;
  var X0 = -half, Z0 = -half;

  // ---- lawn: noise-tinted tiles with alternating mown stripes (4 tiles per cell) ----
  var sub = 3, ts = cs / sub, T = n * sub;
  beginShape(TRIANGLES);
  for (var iz = 0; iz < T; iz++) {
    for (var ix = 0; ix < T; ix++) {
      var x = X0 + ix * ts, z = Z0 + iz * ts;
      var nz = noise(ix * 0.35, iz * 0.35, 3) - 0.5;
      var stripe = (Math.floor(ix / sub) % 2 ? 1 : -1) * stripes * 0.5;
      var l = lawnL * 0.62 + nz * 0.16 + stripe * 0.5;
      var hh = ghue + nz * 0.05;
      green(hh, 0.62, l);
      vertex(x, 0, z); vertex(x, 0, z + ts); vertex(x + ts, 0, z);
      vertex(x + ts, 0, z); vertex(x, 0, z + ts); vertex(x + ts, 0, z + ts);
    }
  }
  endShape();

  // ---- lawn blades, kept out of the hedge footprints -------------------------------
  var per = Math.round(blades), sw = sway;
  beginShape(TRIANGLES);
  for (var cz = 0; cz < n; cz++) {
    for (var cx = 0; cx < n; cx++) {
      for (var b = 0; b < per; b++) {
        var u = h2(cx * 13 + b, cz * 7, 11), v = h2(cx * 5, cz * 17 + b, 23);
        var bx = X0 + (cx + 0.12 + u * 0.76) * cs, bz = Z0 + (cz + 0.12 + v * 0.76) * cs;
        var a = h2(b, cx + cz * 31, 5) * 6.283;
        var len = blade_h * (0.55 + 0.9 * h2(cx + b, cz, 41));
        var wind = Math.sin(t * 2.2 + bx * 1.3 + bz * 0.9) * sw * len * 0.8;
        blade(bx, 0, bz, a, len, len * 0.35 + wind, h2(b, cx, cz), 0.012);
      }
    }
  }
  endShape();

  // ---- hedges: boxes shaded per segment, plus blades on the top -------------------
  var segs = [];
  function addH(cx, z) { segs.push([X0 + (cx + 0.5) * cs, z, cs + wt, wt]); }       // runs along x
  function addV(x, cz) { segs.push([x, Z0 + (cz + 0.5) * cs, wt, cs + wt]); }       // runs along z
  var i;
  for (i = 0; i < n; i++) {
    // outer border; gaps are the entrance (left, row 0) and the exit (right, last row)
    if (i !== 0) addV(X0, i);
    if (i !== n - 1) addV(X0 + n * cs, i);
    addH(i, Z0);
    addH(i, Z0 + n * cs);
  }
  for (var cy = 0; cy < n; cy++) {
    for (var cx2 = 0; cx2 < n; cx2++) {
      var c = cy * n + cx2;
      if (cx2 < n - 1 && M.E[c]) addV(X0 + (cx2 + 1) * cs, cy);
      if (cy < n - 1 && M.S[c]) addH(cx2, Z0 + (cy + 1) * cs);
    }
  }

  for (i = 0; i < segs.length; i++) {
    var s = segs[i], nz2 = noise(s[0] * 0.8, s[1] * 0.8, 9);
    green(ghue - 0.02 + nz2 * 0.05, 0.6, lawnL * 0.55 + nz2 * 0.1);
    push(); translate(s[0], wh / 2, s[1]); box(s[2], wh, s[3]); pop();
  }

  // leafy top and side tufts
  var fz = Math.round(hedge_fuzz);
  beginShape(TRIANGLES);
  for (i = 0; i < segs.length; i++) {
    var q = segs[i];
    for (var f = 0; f < fz * 3; f++) {
      var fx = q[0] + (h2(i, f, 2) - 0.5) * q[2], fz2 = q[1] + (h2(f, i, 3) - 0.5) * q[3];
      var onTop = f < fz * 2;
      var y0 = onTop ? wh : wh * (0.2 + 0.7 * h2(i, f, 4));
      if (!onTop) {                                  // push side tufts out to the hedge face
        if (q[2] > q[3]) fz2 = q[1] + (f % 2 ? 1 : -1) * q[3] / 2; else fx = q[0] + (f % 2 ? 1 : -1) * q[2] / 2;
      }
      var len2 = blade_h * (0.5 + 0.8 * h2(i, f, 6));
      var ang = onTop ? h2(f, i, 8) * 6.283 : ((f % 2) ? (q[2] > q[3] ? 1.571 : 0) : (q[2] > q[3] ? -1.571 : 3.142));
      var wnd = Math.sin(t * 2 + fx * 1.7 + fz2) * sw * len2 * 0.8;
      blade(fx, y0, fz2, ang, len2, len2 * 0.4 + wnd, h2(f, i, 9), 0.014);
    }
  }
  endShape();

  // goal marker in the far corner: a little stone pillar so the exit reads from above
  green(0.1, 0.05, 0.6);
  push(); translate(X0 + (n - 0.5) * cs, 0.15, Z0 + (n - 0.5) * cs); box(0.25, 0.3, 0.25); pop();
}
