// A tiny side-scroller on a 160x120 pixel grid. The hero hops once a second (two beats at 120 bpm),
// over a block, and grabs the coin at the top of the hop: the sound band plays on the same clock.
vec2 res = vec2(160.0, 120.0);
vec2 q = floor(uv * res);
float speed = 60.0;
float scroll = t * speed;
float groundY = 22.0;

// sky: indigo to pink, pixel bands
float h = q.y / res.y;
vec3 col = mix(vec3(1.0, 0.56, 0.45), vec3(0.09, 0.06, 0.24), floor(pow(h, 0.8) * 10.0) / 10.0);

// stars that twinkle
float st = rand(q);
if (st > 0.985 && h > 0.55) col = mix(col, vec3(1.0), 0.5 + 0.5 * sin(t * 4.0 + st * 80.0));

// striped sun
vec2 sc = vec2(112.0, 52.0);
float sd = length(q + 0.5 - sc);
if (sd < 22.0 && (q.y > 46.0 || mod(q.y, 4.0) > 1.0)) col = mix(vec3(1.0, 0.85, 0.35), vec3(1.0, 0.38, 0.52), clamp((sc.y + 22.0 - q.y) / 44.0, 0.0, 1.0));

// two hill layers, parallax
float x1 = q.x + scroll * 0.2;
float hill1 = 44.0 + 10.0 * sin(x1 * 0.045) + 5.0 * sin(x1 * 0.11 + 1.3);
if (q.y < floor(hill1)) col = vec3(0.36, 0.20, 0.50);
float x2 = q.x + scroll * 0.5;
float hill2 = 34.0 + 7.0 * sin(x2 * 0.07 + 2.0) + 3.0 * sin(x2 * 0.19);
if (q.y < floor(hill2)) col = vec3(0.20, 0.42, 0.55);

// ground: grass lip and brick dirt
float gx = q.x + floor(scroll);
if (q.y < groundY) {
   col = vec3(0.55, 0.30, 0.18);
   vec2 b = vec2(mod(gx + (mod(floor(q.y / 5.0), 2.0) * 5.0), 10.0), mod(q.y, 5.0));
   if (b.x < 1.0 || b.y < 1.0) col = vec3(0.36, 0.18, 0.12);
   if (q.y >= groundY - 3.0) col = mod(gx, 4.0) < 2.0 ? vec3(0.35, 0.86, 0.38) : vec3(0.27, 0.72, 0.30);
}

// blocks: one every hop, placed so the hero is over it at mid-hop
float k = floor((gx - 34.0) / speed);
float lx = gx - (k * speed + 64.0);
float bh = 10.0 + 6.0 * step(0.5, rand(vec2(k, 3.0)));
if (lx >= 0.0 && lx < 12.0 && q.y >= groundY && q.y < groundY + bh) {
   col = vec3(0.98, 0.72, 0.15);
   if (lx < 1.0 || lx > 10.0 || q.y < groundY + 1.0 || q.y > groundY + bh - 2.0) col = vec3(0.70, 0.36, 0.08);
   if (abs(lx - 5.5) < 1.0 && abs(q.y - groundY - bh * 0.5) < 2.0) col = vec3(1.0, 0.95, 0.75);
}

// hero hop
float ph = fract(t);
float hop = floor(4.0 * 34.0 * ph * (1.0 - ph));
vec2 hp = vec2(40.0, groundY + hop);

// coin at the top of each hop, gone once grabbed
float cx = lx - 6.0, cy = q.y - (groundY + 40.0);
float coinScreen = (k * speed + 70.0) - floor(scroll);
if (coinScreen > hp.x + 3.0) {
   float w = 3.0 * abs(cos(t * 6.0)) + 0.5;
   if (abs(cx) < w && abs(cy) < 4.0) col = abs(cx) < w - 1.0 ? vec3(1.0, 0.86, 0.2) : vec3(0.85, 0.5, 0.05);
}

// hero sprite, 10 x 10
vec2 s = q - hp + vec2(5.0, 0.0);
if (s.x >= 0.0 && s.x < 10.0 && s.y >= 0.0 && s.y < 10.0) {
   col = vec3(0.98, 0.97, 0.93);
   if (s.x < 1.0 || s.x > 8.0 || s.y < 1.0 || s.y > 8.0) col = vec3(0.12, 0.10, 0.20);
   if ((s.x == 6.0 || s.x == 8.0) && s.y > 5.0 && s.y < 8.0) col = vec3(0.12, 0.10, 0.20);
   if (s.y < 3.0 && s.y >= 1.0 && s.x > 1.0 && s.x < 8.0) col = vec3(0.93, 0.33, 0.30);
}

// CRT: scanlines, glow and a soft vignette
float line = 0.82 + 0.18 * step(0.5, fract(uv.y * res.y));
col *= line;
vec2 v = uv - 0.5;
col *= 1.0 - dot(v, v) * 0.9;
return vec4(col, 1.0);
