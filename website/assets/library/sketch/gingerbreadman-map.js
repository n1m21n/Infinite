// Gingerbreadman Map - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Robert Devaney's piecewise linear chaotic map with polygonal orbits, stability islands, and crystalline diffusion.
param("steps", 25000, 5000, 60000);
param("scale", 52.0, 20.0, 100.0);
param("twist", 0.08, -0.4, 0.4);
param("alpha", 0.75, 0.2, 1.0);
param("seeds", 6, 2, 16);

function draw(t) {
  background(0.082, 0.098, 0.188); // brand midnight #151930
  const cx = width * 0.48;
  const cy = height * 0.52;
  const s = scale * (min(width, height) / 600.0);
  const totalSteps = floor(steps / seeds);
  const rot = t * 0.15;
  const dynTwist = twist * Math.sin(t * 1.5);

  strokeWeight(1.15);

  // Trace multiple orbits from initial seed points
  const initialSeeds = [
    [-0.1, 0.0],
    [-0.5, 0.3],
    [-1.2, 0.7],
    [-2.1, 1.4],
    [-2.8, 1.9],
    [-3.5, 2.6],
    [-0.8, -0.6],
    [-1.6, -1.2]
  ];

  for (let sid = 0; sid < min(seeds, initialSeeds.length); sid++) {
    let x = initialSeeds[sid][0];
    let y = initialSeeds[sid][1];

    const seedFrac = sid / seeds;
    const isOdd = sid % 2 === 1;

    for (let i = 0; i < totalSteps; i++) {
      // Devaney's gingerbreadman recurrence with dynamic phase modulation
      const nextX = 1.0 - y + Math.abs(x) + dynTwist * Math.sin(x * 0.4 + rot);
      const nextY = x;

      x = nextX;
      y = nextY;

      if (i > 15) {
        // Subtle global rotation for dynamic movement
        const rx = x * Math.cos(rot) - y * Math.sin(rot);
        const ry = x * Math.sin(rot) + y * Math.cos(rot);

        const px = cx + rx * s;
        const py = cy - ry * s;

        if (px >= 0 && px < width && py >= 0 && py < height) {
          const dist = Math.sqrt(x * x + y * y) / 6.0;
          if (isOdd) {
            // Signal #A7B1FF to emerald #4EF2B8
            stroke(0.655 - dist * 0.2, 0.694 + dist * 0.2, 1.0, alpha * 0.65);
          } else {
            // Ember #FF6B35 to white
            stroke(1.0, 0.42 + dist * 0.3, 0.208 + dist * 0.5, alpha * 0.65);
          }
          point(px, py);
        }
      }
    }
  }
}
