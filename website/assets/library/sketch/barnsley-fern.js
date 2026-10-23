// Barnsley Fern - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Michael Barnsley's iterated function system (IFS) affine fractal fern with recursive fronds and wind sway.
param("iterations", 20000, 5000, 45000);
param("scale", 55.0, 20.0, 110.0);
param("wind", 0.15, -0.6, 0.6);
param("curl", 0.04, -0.2, 0.2);
param("fade", 0.85, 0.2, 1.0);

function draw(t) {
  background(0.082, 0.098, 0.188); // brand midnight #151930
  const cx = width * 0.5;
  const cy = height * 0.92;
  const s = scale * (min(width, height) / 600.0);
  const sway = wind * Math.sin(t * 1.8);
  const sway2 = curl * Math.cos(t * 1.2);

  // Michael Barnsley 1988 IFS transformations
  let x = 0.0, y = 0.0;
  const n = floor(iterations);

  strokeWeight(1.1);
  for (let i = 0; i < n; i++) {
    const r = Math.random();
    let nx = 0.0, ny = 0.0;

    if (r < 0.01) {
      // f1: Stem / rachis
      nx = 0.0;
      ny = 0.16 * y;
    } else if (r < 0.86) {
      // f2: Successively smaller leaflets & fronds
      nx = 0.85 * x + (0.04 + sway * 0.08) * y;
      ny = -(0.04 + sway2 * 0.06) * x + 0.85 * y + 1.6;
    } else if (r < 0.93) {
      // f3: Largest left leaflet
      nx = 0.20 * x - 0.26 * y;
      ny = 0.23 * x + 0.22 * y + 1.6;
    } else {
      // f4: Largest right leaflet
      nx = -0.15 * x + 0.28 * y;
      ny = 0.26 * x + 0.24 * y + 0.44;
    }

    x = nx;
    y = ny;

    if (i > 30) {
      const px = cx + x * s;
      const py = cy - y * s;

      if (px >= 0 && px < width && py >= 0 && py < height) {
        // Height-dependent and leaflet-dependent colour gradient
        const frac = Math.min(Math.max(y / 10.0, 0.0), 1.0);
        if (r >= 0.86 && r < 0.93) {
          // Left leaflets - signal accent #A7B1FF
          stroke(0.655, 0.694, 1.0, 0.72 * fade);
        } else if (r >= 0.93) {
          // Right leaflets - ember accent #FF6B35
          stroke(1.0, 0.42, 0.208, 0.68 * fade);
        } else {
          // Main body - emerald gradient #4EF2B8 to white signal
          const rCol = 0.306 * (1.0 - frac) + 0.655 * frac;
          const gCol = 0.949 * (1.0 - frac) + 0.694 * frac;
          const bCol = 0.722 * (1.0 - frac) + 1.000 * frac;
          stroke(rCol, gCol, bCol, (0.45 + frac * 0.45) * fade);
        }
        point(px, py);
      }
    }
  }
}
