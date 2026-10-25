// Ukiyo-e Cloud Pagoda 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Multi-tiered Edo architectural pagoda structure with swooping parametric curved roof eaves, surrounded by floating woodblock cloud ribbons and indigo strata.
param("tier_count", 5.0, 3.0, 9.0);
param("roof_flare", 1.8, 1.0, 3.0);
param("cloud_ribbons", 12.0, 4.0, 24.0);
param("spin_drift", 0.3, 0.0, 1.5);
param("strata_spacing", 0.45, 0.2, 0.8);

function draw(t) {
  push();
  const time = t * spin_drift;

  // Gentle rotating perspective
  rotateY(time * 0.5);
  rotateX(0.35 + 0.08 * sin(time * 0.4));

  const tiers = floor(tier_count);
  const totalHeight = tiers * strata_spacing;

  // 1. Central pagoda pavilion tiers
  for (let i = 0; i < tiers; i++) {
    const u = i / tiers;
    const y = (u - 0.4) * totalHeight;
    const tierScale = 1.0 - u * 0.45;

    push();
    translate(0, y, 0);

    // Architectural pillar room chamber
    fill(0.18, 0.12, 0.15); // Lacquer dark timber
    box(0.6 * tierScale, strata_spacing * 0.55, 0.6 * tierScale);

    // Swooping flared Japanese roof eave
    push();
    translate(0, strata_spacing * 0.28, 0);

    // Vermilion under-eave rafter bracket
    fill(1.0, 0.420, 0.208); // Ember / vermilion
    box(0.85 * tierScale, 0.05, 0.85 * tierScale);

    // Indigo glazed tile roof with flared corners
    fill(0.15, 0.20, 0.38); // Prussian indigo
    const roofBaseW = 1.25 * tierScale * (roof_flare * 0.6);
    box(roofBaseW, 0.08, roofBaseW);

    // Flared roof hip ridges (corner rafters)
    fill(0.655, 0.694, 1.0); // Signal ceramic ridge caps
    const cornerReach = roofBaseW * 0.58;
    tube(-cornerReach, 0.04, -cornerReach, cornerReach, 0.04, cornerReach, 0.02, 6);
    tube(cornerReach, 0.04, -cornerReach, -cornerReach, 0.04, cornerReach, 0.02, 6);

    pop();
    pop();
  }

  // 2. Pagoda sacred bronze spire finial (Sorin) on top roof
  const topY = (1.0 - 0.4) * totalHeight + strata_spacing * 0.35;
  push();
  translate(0, topY, 0);
  fill(0.933, 0.945, 0.980); // Gilded spire
  cylinder(0.04, 0.6, 8);

  // Spire rings (Kuruma)
  for (let r = 0; r < 7; r++) {
    push();
    translate(0, 0.1 + r * 0.06, 0);
    fill(1.0, 0.420, 0.208); // Bronze gold
    cylinder(0.09 - r * 0.006, 0.025, 8);
    pop();
  }
  pop();

  // 3. Floating Edo woodblock cloud ribbons (Kumogata)
  const numClouds = floor(cloud_ribbons);
  for (let c = 0; c < numClouds; c++) {
    const uc = c / numClouds;
    const aCloud = uc * 6.2831853 + time * (c % 2 === 0 ? 0.6 : -0.4);
    const cloudRad = 1.6 + 0.35 * sin(time * 1.5 + c);
    const cloudY = (sin(c * 2.3 + time) * 0.5) * totalHeight;

    const cx = cos(aCloud) * cloudRad;
    const cz = sin(aCloud) * cloudRad;

    push();
    translate(cx, cloudY, cz);
    rotateY(aCloud + 1.570796);

    // Layered woodblock print cloud segment
    fill(0.306, 0.949, 0.722); // Mint / celadon wash
    sphere(0.18, 8, 8);
    push();
    translate(0.18, -0.04, 0);
    fill(0.933, 0.945, 0.980); // White foam crest
    sphere(0.13, 8, 8);
    pop();
    push();
    translate(-0.18, -0.04, 0);
    fill(0.655, 0.694, 1.0); // Indigo mist
    sphere(0.14, 8, 8);
    pop();

    pop();
  }

  // 4. Ground foundation stone plinth
  push();
  translate(0, -0.45 * totalHeight, 0);
  fill(0.10, 0.12, 0.22); // Granite base
  box(2.4, 0.18, 2.4);
  pop();

  pop();
}
