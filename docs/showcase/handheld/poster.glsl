// Poster ground behind the console, 1080 x 1350, y up. Paper, a checkerboard "transparency" band,
// two black panels of lime glyphs that re-roll every beat, and three old-OS windows.
// uA is the kick (Audio Analyze): it flashes the glyphs. 120 bpm, so beat = t * 2.
vec2 px = uv * vec2(1080.0, 1350.0);
float beat = t * 2.0;
vec3 paper = vec3(0.905, 0.905, 0.89);
vec3 ink = vec3(0.05, 0.05, 0.06);
vec3 lime = vec3(0.78, 1.0, 0.0);
vec3 col = paper - 0.025 * rand(floor(px / 2.0));

// checkerboard band, top-left to right, drifting slowly along itself
vec2 dir = normalize(vec2(1.0, -0.62));
vec2 nrm = vec2(-dir.y, dir.x);
float across = dot(px - vec2(0.0, 1180.0), nrm);
if (abs(across) < 150.0) {
   vec2 c = floor((px + dir * t * 24.0) / 24.0);
   col = mod(c.x + c.y, 2.0) < 1.0 ? vec3(0.97) : vec3(0.80);
}

// glyph panels: black rects filled with lime glyphs on a 30 px grid
float inA = step(0.0, px.x) * step(px.x, 430.0) * step(110.0, px.y) * step(px.y, 560.0);
float inB = step(650.0, px.x) * step(px.y, 980.0) * step(430.0, px.y);
if (inA + inB > 0.0) {
   col = ink;
   vec2 cell = floor(px / 30.0);
   vec2 f = px - cell * 30.0 - 15.0;
   float r = length(f);
   float roll = floor(beat);
   float dens = rand(floor(cell / 4.0) + vec2(floor(beat / 4.0), 7.0));
   float g = floor(rand(cell + vec2(roll * 0.37, roll * 1.13)) * 8.0);
   float on = 0.0;
   if (dens > 0.25) {
      if (g < 1.0) on = step(abs(r - 10.0), 1.6) + step(r, 4.0);                     // target
      else if (g < 2.0) on = step(abs(r - 10.0), 1.6);                                // ring
      else if (g < 3.0) on = step(max(abs(f.x), abs(f.y)), 10.0) - step(max(abs(f.x), abs(f.y)), 7.0);  // square
      else if (g < 4.0) on = step(min(abs(f.x - f.y), abs(f.x + f.y)), 2.2) * step(max(abs(f.x), abs(f.y)), 10.0); // x
      else if (g < 5.0) on = step(min(abs(f.x), abs(f.y)), 1.6) * step(max(abs(f.x), abs(f.y)), 7.0);    // plus
      else if (g < 6.0) on = step(r, 2.2);                                            // dot
      else if (g < 7.0) on = step(abs(r - 10.0), 1.6) + step(abs(r - 5.0), 1.6);      // double ring
      else on = step(max(abs(f.x), abs(f.y)), 10.0) - step(max(abs(f.x), abs(f.y)), 7.0) + step(r, 3.0);
   } else if (dens > 0.12) {
      on = step(r, 1.6);                                                               // sparse dots
   }
   col = mix(col, lime * (0.85 + 0.6 * uA), clamp(on, 0.0, 1.0));
   // solid lime blocks that jump every bar
   float bar = floor(beat / 4.0);
   vec2 bA = vec2(floor(rand(vec2(bar, 1.0)) * 10.0) * 30.0, 110.0 + floor(rand(vec2(bar, 2.0)) * 10.0) * 30.0);
   vec2 bB = vec2(650.0 + floor(rand(vec2(bar, 3.0)) * 9.0) * 30.0, 430.0 + floor(rand(vec2(bar, 4.0)) * 12.0) * 30.0);
   if (inA > 0.0 && px.x > bA.x && px.x < bA.x + 150.0 && px.y > bA.y && px.y < bA.y + 120.0) col = lime;
   if (inB > 0.0 && px.x > bB.x && px.x < bB.x + 180.0 && px.y > bB.y && px.y < bB.y + 210.0) col = lime;
}

// old-OS windows: grey frame, blue title bar, menu strip, checkerboard inside
vec4 win[3];
win[0] = vec4(56.0, 930.0, 330.0, 220.0);
win[1] = vec4(770.0, 90.0, 260.0, 200.0);
win[2] = vec4(40.0, 640.0, 340.0, 110.0);
for (int i = 0; i < 3; i++) {
   vec4 w = win[i];
   w.xy += vec2(0.0, 6.0 * step(0.5, fract(beat * 0.25 + float(i) * 0.33)));   // a small hop on the off-bar
   vec2 q = px - w.xy;
   if (q.x >= -2.0 && q.x <= w.z + 2.0 && q.y >= -2.0 && q.y <= w.w + 2.0) {
      col = vec3(0.30);
      if (q.x >= 0.0 && q.x <= w.z && q.y >= 0.0 && q.y <= w.w) {
         vec2 c = floor(q / 14.0);
         col = mod(c.x + c.y, 2.0) < 1.0 ? vec3(0.98) : vec3(0.84);
         if (q.y > w.w - 30.0) col = vec3(0.92);                                  // menu strip
         if (q.y > w.w - 30.0 && q.y < w.w - 18.0 && mod(q.x - 8.0, 46.0) < 30.0 && q.x < 200.0) col = vec3(0.35);
         if (q.y > w.w - 16.0) col = mix(vec3(0.0, 0.0, 0.50), vec3(0.06, 0.52, 0.82), q.x / w.z); // title bar
         vec2 b = vec2(w.z - q.x, q.y - (w.w - 16.0));
         if (b.y > 2.0 && b.y < 13.0 && mod(b.x - 3.0, 14.0) < 11.0 && b.x < 44.0) col = vec3(0.78);  // buttons
      }
   }
}
return vec4(col, 1.0);
