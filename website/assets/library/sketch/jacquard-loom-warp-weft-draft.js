// Jacquard Loom Warp Weft Draft - Infinite Library (Sketch 2D)
param("grid_count", 16, 10, 24);
param("twill_shift", 3, 1, 7);
param("shuttle_speed", 0.55, 0.0, 2.0);
param("thread_gap", 0.14, 0.04, 0.30);

function draw(t) {
  // Unbleached linen ground (#F3EFE6)
  background(0.953, 0.937, 0.902);

  const w = width;
  const h = height;
  const n = floor(grid_count);
  const shift = floor(twill_shift);

  // Subtle linen warp/weft fibre grain
  strokeWeight(1.0);
  for (let i = 0; i < 64; i++) {
    const u = i / 64;
    stroke(0.106, 0.125, 0.157, 0.035);
    line(w * u, h * 0.03, w * u, h * 0.97);
    line(w * 0.03, h * u, w * 0.97, h * u);
  }

  // Outer architectural loom draft frame
  noFill();
  stroke(0.106, 0.125, 0.157, 0.85);
  strokeWeight(2.5);
  rect(w * 0.05, h * 0.05, w * 0.90, h * 0.90);

  // Main drawdown region (left-bottom 68% x 68%)
  const mx = w * 0.09;
  const my = h * 0.24;
  const mw = w * 0.62;
  const mh = h * 0.62;
  const cw = mw / n;
  const ch = mh / n;
  const phase = floor(t * shuttle_speed * 4.0);

  // Drawdown warp/weft interlacing cells
  for (let r = 0; r < n; r++) {
    for (let c = 0; c < n; c++) {
      const x = mx + c * cw;
      const y = my + r * ch;
      const pad = Math.min(cw, ch) * thread_gap;

      // Jacquard twill + satin hybrid lift rule
      const diag1 = (c + r * shift + phase) % 6;
      const diag2 = (c * 2 - r + n * 4) % 5;
      const warpUp = (diag1 < 3) !== (diag2 === 0);

      noStroke();
      if (warpUp) {
        // Warp thread (indigo or charcoal)
        if ((c % 4) === 1) {
          fill(0.180, 0.353, 0.408, 0.94); // #2E5A68 indigo
        } else {
          fill(0.106, 0.125, 0.157, 0.92); // #1B2028 charcoal
        }
        rect(x + pad, y + 1.0, cw - pad * 2.0, ch - 2.0);
        // Subtle yarn highlight rib
        fill(0.953, 0.937, 0.902, 0.18);
        rect(x + cw * 0.42, y + 2.0, cw * 0.14, ch - 4.0);
      } else {
        // Weft thread (madder terracotta or natural linen shadow)
        if ((r % 5) === 2) {
          fill(0.722, 0.290, 0.196, 0.92); // #B84A32 madder terracotta
        } else {
          fill(0.845, 0.815, 0.755, 0.95);
        }
        rect(x + 1.0, y + pad, cw - 2.0, ch - pad * 2.0);
        fill(0.106, 0.125, 0.157, 0.12);
        rect(x + 2.0, y + ch * 0.72, cw - 4.0, ch * 0.12);
      }

      // Fine grid rule
      noFill();
      stroke(0.106, 0.125, 0.157, 0.16);
      strokeWeight(0.8);
      rect(x, y, cw, ch);
    }
  }

  // Top threading draft strip (secondary counter-structure)
  const ty = h * 0.09;
  const th = h * 0.11;
  const shafts = 4;
  const sh = th / shafts;
  for (let c = 0; c < n; c++) {
    const shaftIdx = (c + floor(c / 4)) % shafts;
    const x = mx + c * cw;
    for (let s = 0; s < shafts; s++) {
      noFill();
      stroke(0.106, 0.125, 0.157, 0.28);
      strokeWeight(1.0);
      rect(x, ty + s * sh, cw, sh);
      if (s === shaftIdx) {
        noStroke();
        fill((c % 4) === 1 ? 0.180 : 0.106, (c % 4) === 1 ? 0.353 : 0.125, (c % 4) === 1 ? 0.408 : 0.157, 0.92);
        circle(x + cw * 0.5, ty + s * sh + sh * 0.5, Math.min(cw, sh) * 0.46);
      }
    }
  }

  // Right treadling & tie-up column
  const rx = w * 0.75;
  const rw = w * 0.16;
  const tw = rw / shafts;
  // Tie-up box (top right)
  for (let s = 0; s < shafts; s++) {
    for (let tr = 0; tr < shafts; tr++) {
      noFill();
      stroke(0.106, 0.125, 0.157, 0.45);
      strokeWeight(1.2);
      rect(rx + tr * tw, ty + s * sh, tw, sh);
      if ((s + tr) % 2 === 0 || (s === 0 && tr === 3)) {
        noStroke();
        fill(0.722, 0.290, 0.196, 0.90);
        rect(rx + tr * tw + tw * 0.22, ty + s * sh + sh * 0.22, tw * 0.56, sh * 0.56);
      }
    }
  }
  // Treadling punch strip (bottom right)
  for (let r = 0; r < n; r++) {
    const trIdx = (r * shift + phase) % shafts;
    const y = my + r * ch;
    for (let tr = 0; tr < shafts; tr++) {
      noFill();
      stroke(0.106, 0.125, 0.157, 0.25);
      strokeWeight(1.0);
      rect(rx + tr * tw, y, tw, ch);
      if (tr === trIdx) {
        noStroke();
        fill(0.722, 0.290, 0.196, 0.92);
        circle(rx + tr * tw + tw * 0.5, y + ch * 0.5, Math.min(tw, ch) * 0.48);
      }
    }
  }

  // Selvedge shuttle marker bar
  const activeRow = (phase % n);
  const sy = my + activeRow * ch + ch * 0.5;
  stroke(0.722, 0.290, 0.196, 0.88);
  strokeWeight(2.4);
  line(w * 0.06, sy, mx - w * 0.012, sy);
  noStroke();
  fill(0.722, 0.290, 0.196, 0.95);
  circle(mx - w * 0.012, sy, 8.0);
}
