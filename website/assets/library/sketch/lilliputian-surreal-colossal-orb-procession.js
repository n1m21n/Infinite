// Lilliputian Surreal Colossal Orb Procession - Infinite Library (Sketch 2D)
param("pilgrim_count", 16, 6, 32);
param("orb_scale", 0.56, 0.35, 0.72);
param("arch_ribs", 10, 4, 18);
param("drift_speed", 0.04, 0.0, 0.2);

function draw(t) {
  // Deep slate navy background (#1a2a3a)
  background(0.102, 0.165, 0.227);

  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.43;

  // Vaulted architectural perspective ribs in muted slate-ivory
  noFill();
  stroke(0.24, 0.33, 0.42, 0.85);
  strokeWeight(2.0);
  const ribs = floor(arch_ribs);
  for (let i = 0; i < ribs; i++) {
    const r = (i + 1) / ribs;
    const rw = w * (0.28 + r * 0.68);
    const rh = h * (0.28 + r * 0.66);
    rect(cx - rw * 0.5, cy - rh * 0.48, rw, rh);
  }

  // Horizon perspective rays converging behind the colossal relic
  stroke(0.28, 0.37, 0.46, 0.55);
  strokeWeight(1.5);
  for (let a = 0; a < 16; a++) {
    const ang = (a / 16) * Math.PI * 2.0;
    line(cx + Math.cos(ang) * 90, cy + Math.sin(ang) * 90,
         cx + Math.cos(ang) * w * 0.75, cy + Math.sin(ang) * h * 0.75);
  }

  // Shadow halo behind the titanic alabaster sphere
  noStroke();
  fill(0.06, 0.10, 0.15, 0.85);
  circle(cx + 18, cy + 22, min(w, h) * orb_scale * 1.06);

  // Colossal alabaster sphere (#e8d8c8)
  fill(0.910, 0.847, 0.784);
  circle(cx, cy, min(w, h) * orb_scale);

  // Subtle crescent shadow and meridian rings on the sphere
  fill(0.76, 0.68, 0.61, 0.45);
  circle(cx - w * 0.04, cy - h * 0.03, min(w, h) * orb_scale * 0.78);
  noFill();
  stroke(0.102, 0.165, 0.227, 0.35);
  strokeWeight(2.0);
  ellipse(cx, cy, min(w, h) * orb_scale * 0.92, min(w, h) * orb_scale * 0.32);

  // Suspended causeway deck below the sphere
  const deckY = h * 0.78;
  noStroke();
  fill(0.910, 0.847, 0.784);
  rect(0, deckY, w, 16);
  fill(0.06, 0.10, 0.15);
  rect(0, deckY + 16, w, h - deckY - 16);

  // Support piers under causeway
  fill(0.22, 0.31, 0.40);
  for (let p = 1; p < 8; p++) {
    const px = (p / 8) * w;
    rect(px - 6, deckY + 16, 12, h - deckY - 16);
  }

  // Miniature vermilion pilgrims (#b84a39) traversing the causeway
  const n = floor(pilgrim_count);
  for (let i = 0; i < n; i++) {
    const prog = (i / n + t * drift_speed) % 1.0;
    const fx = 60 + prog * (w - 120);
    const bob = Math.abs(Math.sin(t * 2.5 + i * 1.7)) * 3.0;
    const fy = deckY - bob;

    // Long surrealist shadow cast onto the deck
    fill(0.08, 0.12, 0.18, 0.55);
    rect(fx - 14, deckY + 2, 16, 4);

    // Tiny terracotta body and head
    fill(0.722, 0.290, 0.224);
    rect(fx - 3, fy - 18, 6, 18);
    circle(fx, fy - 23, 7);
  }
}
