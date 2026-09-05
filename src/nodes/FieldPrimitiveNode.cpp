#include "FieldPrimitiveNode.h"
#include "field/FieldLex.h"
#include "field/FieldParse.h"
#include "Transport.h"

#include <algorithm>
#include <cmath>

const std::vector<FieldPrimitiveNode::Preset>& FieldPrimitiveNode::Presets()
{
   static const std::vector<Preset> kPresets = {
      { "Solid Terrain Plane",
        PrimitiveTopology::Plane,
        2500,
        "param float size = 2.5 [0.5, 10.0]\n"
        "param float height = 0.40 [0.0, 2.0]\n"
        "param float freq = 2.5 [0.5, 8.0]\n"
        "param float speed = 1.0 [0.0, 5.0]\n"
        "x = (uv.x - 0.5) * size\n"
        "z = (uv.y - 0.5) * size\n"
        "dist = sqrt(x * x + z * z)\n"
        "wave = sin(dist * freq - (t + 1.0) * speed)\n"
        "y = wave * height * exp(-dist * 0.4)\n"
        "P = vec3(x, y, z)\n"
        "slope = cos(dist * freq - (t + 1.0) * speed) * freq * height * exp(-dist * 0.4)\n"
        "nx = if(dist > 0.001, -(x / dist) * slope, 0.0)\n"
        "nz = if(dist > 0.001, -(z / dist) * slope, 0.0)\n"
        "N = normalize(vec3(nx, 1.0, nz))\n"
        "Cd = vec3(0.15 + 0.35 * (y + 0.5), 0.55 + 0.4 * sin(dist * 3.0), 0.85)\n"
        "publish = sin((t + 1.0) * speed)\n" },
      { "Solid UV Sphere",
        PrimitiveTopology::Sphere,
        2400,
        "param float radius = 1.2 [0.2, 5.0]\n"
        "param float ripple = 0.15 [0.0, 0.6]\n"
        "param float freq = 6.0 [1.0, 16.0]\n"
        "param float speed = 1.5 [0.0, 5.0]\n"
        "phi = uv.x * 6.2831853\n"
        "theta = (uv.y - 0.5) * 3.14159265\n"
        "disp = 1.0 + ripple * sin(phi * freq + (t + 1.0) * speed) * cos(theta * freq)\n"
        "r = radius * disp\n"
        "px = cos(theta) * cos(phi) * r\n"
        "py = sin(theta) * r\n"
        "pz = cos(theta) * sin(phi) * r\n"
        "P = vec3(px, py, pz)\n"
        "N = normalize(P)\n"
        "Cd = vec3(0.5 + 0.5 * N.x, 0.5 + 0.5 * N.y, 0.5 + 0.5 * N.z)\n"
        "publish = sin((t + 1.0) * speed)\n" },
      { "Solid Torus",
        PrimitiveTopology::Torus,
        2500,
        "param float rMajor = 1.3 [0.3, 4.0]\n"
        "param float rMinor = 0.45 [0.05, 1.5]\n"
        "param float twist = 3.0 [0.0, 10.0]\n"
        "param float speed = 1.2 [0.0, 5.0]\n"
        "phi = uv.x * 6.2831853\n"
        "theta = uv.y * 6.2831853 + phi * twist + (t + 1.0) * speed\n"
        "r = rMajor + rMinor * cos(theta)\n"
        "P = vec3(r * cos(phi), rMinor * sin(theta), r * sin(phi))\n"
        "N = vec3(cos(theta) * cos(phi), sin(theta), cos(theta) * sin(phi))\n"
        "Cd = vec3(0.5 + 0.5 * cos(phi), 0.5 + 0.5 * sin(theta), 0.9)\n"
        "publish = sin((t + 1.0) * speed)\n" },
      { "Solid Cylinder",
        PrimitiveTopology::Cylinder,
        2400,
        "param float radius = 0.8 [0.1, 3.0]\n"
        "param float height = 2.4 [0.2, 8.0]\n"
        "param float taper = 0.3 [0.0, 0.9]\n"
        "param float twist = 2.0 [0.0, 8.0]\n"
        "param float speed = 1.0 [0.0, 5.0]\n"
        "phi = uv.x * 6.2831853 + uv.y * twist + (t + 1.0) * speed\n"
        "y = (uv.y - 0.5) * height\n"
        "r = radius * (1.0 - uv.y * taper)\n"
        "P = vec3(cos(phi) * r, y, sin(phi) * r)\n"
        "N = vec3(cos(phi), taper * 0.2, sin(phi))\n"
        "Cd = vec3(0.2, 0.5 + 0.5 * uv.y, 0.9)\n"
        "publish = sin((t + 1.0) * speed)\n" },
      { "Solid Mobius Ribbon",
        PrimitiveTopology::Plane,
        2500,
        "param float radius = 1.3 [0.3, 4.0]\n"
        "param float width = 0.5 [0.05, 2.0]\n"
        "param float twists = 1.0 [1.0, 5.0]\n"
        "param float speed = 1.0 [0.0, 4.0]\n"
        "u = uv.x * 6.2831853 + (t + 1.0) * speed\n"
        "v = (uv.y - 0.5) * width\n"
        "halfA = u * twists * 0.5\n"
        "r = radius + v * cos(halfA)\n"
        "P = vec3(r * cos(u), v * sin(halfA), r * sin(u))\n"
        "N = vec3(-sin(u), cos(halfA), cos(u))\n"
        "Cd = vec3(0.5 + 0.5 * cos(u), 0.5 + 0.5 * sin(halfA), 0.8)\n"
        "publish = sin((t + 1.0) * speed)\n" },
      { "Fibonacci Sphere Points",
        PrimitiveTopology::Points,
        2000,
        "param float radius = 1.2 [0.2, 5.0]\n"
        "param float speed = 0.5 [0.0, 3.0]\n"
        "phi = 2.39996323\n"
        "y = 1.0 - (i / max(1.0, count - 1.0)) * 2.0\n"
        "rAtY = sqrt(max(0.0, 1.0 - y * y))\n"
        "theta = phi * i + t * speed\n"
        "P = vec3(cos(theta) * rAtY * radius, y * radius, sin(theta) * rAtY * radius)\n"
        "N = normalize(P)\n"
        "Cd = N * 0.5 + 0.5\n"
        "publish = sin(t * speed)\n" },
      { "Spiral Curve",
        PrimitiveTopology::Points,
        1000,
        "param float turns = 4.0 [1.0, 20.0]\n"
        "param float maxRadius = 1.5 [0.2, 5.0]\n"
        "param float height = 1.0 [0.0, 4.0]\n"
        "param float speed = 1.5 [0.0, 8.0]\n"
        "u = i / count\n"
        "angle = u * turns * 6.2831853 + t * speed\n"
        "r = u * maxRadius\n"
        "P = vec3(cos(angle) * r, (u - 0.5) * height, sin(angle) * r)\n"
        "Cd = vec3(u, 0.7, 1.0 - u)\n"
        "publish = sin(t * speed)\n" },
      { "Helix Curve",
        PrimitiveTopology::Points,
        1000,
        "param float radius = 0.8 [0.1, 3.0]\n"
        "param float pitch = 2.0 [0.2, 8.0]\n"
        "param float turns = 3.0 [1.0, 15.0]\n"
        "param float speed = 2.0 [0.0, 10.0]\n"
        "u = i / count\n"
        "a = u * turns * 6.2831853 + t * speed\n"
        "P = vec3(cos(a) * radius, (u - 0.5) * pitch, sin(a) * radius)\n"
        "Cd = vec3(0.3, 0.5 + 0.5 * sin(a), 0.9)\n"
        "publish = sin(t * speed)\n" },
      { "Pine Tree",
        PrimitiveTopology::Points,
        2800,
        "param float trunkHeight = 1.4 [0.5, 3.0]\n"
        "param float trunkRadius = 0.06 [0.02, 0.2]\n"
        "param float branchRadius = 0.9 [0.3, 2.0]\n"
        "param float tierSpacing = 0.34 [0.15, 0.7]\n"
        "param float speed = 0.6 [0.0, 3.0]\n"
        "goldenAngle = 2.3999632\n"
        "u = i / count\n"
        "theta = goldenAngle * i\n"
        "b0 = 0.30\n"
        "b1 = 0.475\n"
        "b2 = 0.65\n"
        "b3 = 0.825\n"
        "isTrunk = 1.0 - step(b0, u)\n"
        "isTier1 = step(b0, u) - step(b1, u)\n"
        "isTier2 = step(b1, u) - step(b2, u)\n"
        "isTier3 = step(b2, u) - step(b3, u)\n"
        "isTier4 = step(b3, u)\n"
        "uTrunk = u / b0\n"
        "rTrunk = trunkRadius * (1.0 - 0.2 * uTrunk)\n"
        "posTrunk = vec3(cos(theta) * rTrunk, uTrunk * trunkHeight, sin(theta) * rTrunk)\n"
        "tierY0 = trunkHeight * 0.55\n"
        "tierY1 = tierY0 + tierSpacing\n"
        "tierY2 = tierY1 + tierSpacing\n"
        "tierY3 = tierY2 + tierSpacing\n"
        "tierH = tierSpacing * 0.9\n"
        "tierR0 = branchRadius\n"
        "tierR1 = branchRadius * 0.78\n"
        "tierR2 = branchRadius * 0.56\n"
        "tierR3 = branchRadius * 0.34\n"
        "ut1 = (u - b0) / (b1 - b0)\n"
        "ut2 = (u - b1) / (b2 - b1)\n"
        "ut3 = (u - b2) / (b3 - b2)\n"
        "ut4 = (u - b3) / (1.0 - b3)\n"
        "r1 = tierR0 * (1.0 - ut1)\n"
        "r2 = tierR1 * (1.0 - ut2)\n"
        "r3 = tierR2 * (1.0 - ut3)\n"
        "r4 = tierR3 * (1.0 - ut4)\n"
        "posTier1 = vec3(cos(theta) * r1, tierY0 + ut1 * tierH, sin(theta) * r1)\n"
        "posTier2 = vec3(cos(theta) * r2, tierY1 + ut2 * tierH, sin(theta) * r2)\n"
        "posTier3 = vec3(cos(theta) * r3, tierY2 + ut3 * tierH, sin(theta) * r3)\n"
        "posTier4 = vec3(cos(theta) * r4, tierY3 + ut4 * tierH, sin(theta) * r4)\n"
        "breathe = 1.0 + 0.015 * sin(t * speed * 2.0)\n"
        "P = (posTrunk * isTrunk + posTier1 * isTier1 + posTier2 * isTier2 + posTier3 * isTier3 + posTier4 * isTier4) * breathe\n"
        "trunkColor = vec3(0.35, 0.22, 0.12)\n"
        "tierColor1 = vec3(0.05, 0.22, 0.08)\n"
        "tierColor2 = vec3(0.10, 0.32, 0.10)\n"
        "tierColor3 = vec3(0.16, 0.42, 0.13)\n"
        "tierColor4 = vec3(0.28, 0.55, 0.18)\n"
        "Cd = trunkColor * isTrunk + tierColor1 * isTier1 + tierColor2 * isTier2 + tierColor3 * isTier3 + tierColor4 * isTier4\n"
        "publish = sin(t * speed)\n" },
      { "Mushroom",
        PrimitiveTopology::Points,
        2200,
        "param float capRadius = 1.1 [0.3, 3.0]\n"
        "param float capHeight = 0.55 [0.1, 2.0]\n"
        "param float stemHeight = 0.9 [0.2, 3.0]\n"
        "param float stemRadius = 0.18 [0.05, 0.6]\n"
        "param float spotSize = 0.22 [0.05, 0.6]\n"
        "param float speed = 0.5 [0.0, 3.0]\n"
        "goldenAngle = 2.3999632\n"
        "u = i / count\n"
        "theta = goldenAngle * i\n"
        "b0 = 0.75\n"
        "isCap = 1.0 - step(b0, u)\n"
        "isStem = step(b0, u)\n"
        "capY = u / b0\n"
        "rAtY = sqrt(max(0.0, 1.0 - capY * capY))\n"
        "posCap = vec3(cos(theta) * rAtY * capRadius, stemHeight + capY * capHeight, sin(theta) * rAtY * capRadius)\n"
        "uStem = (u - b0) / (1.0 - b0)\n"
        "rStem = stemRadius * (1.0 + 0.15 * uStem)\n"
        "posStem = vec3(cos(theta) * rStem, uStem * stemHeight, sin(theta) * rStem)\n"
        "breathe = 1.0 + 0.015 * sin(t * speed * 2.0)\n"
        "P = (posCap * isCap + posStem * isStem) * breathe\n"
        "ancTheta1 = 0.6\n"
        "ancY1 = 0.7\n"
        "ancR1 = sqrt(max(0.0, 1.0 - ancY1 * ancY1))\n"
        "anchorPos1 = vec3(cos(ancTheta1) * ancR1 * capRadius, stemHeight + ancY1 * capHeight, sin(ancTheta1) * ancR1 * capRadius)\n"
        "ancTheta2 = 2.4\n"
        "ancY2 = 0.55\n"
        "ancR2 = sqrt(max(0.0, 1.0 - ancY2 * ancY2))\n"
        "anchorPos2 = vec3(cos(ancTheta2) * ancR2 * capRadius, stemHeight + ancY2 * capHeight, sin(ancTheta2) * ancR2 * capRadius)\n"
        "ancTheta3 = 4.1\n"
        "ancY3 = 0.65\n"
        "ancR3 = sqrt(max(0.0, 1.0 - ancY3 * ancY3))\n"
        "anchorPos3 = vec3(cos(ancTheta3) * ancR3 * capRadius, stemHeight + ancY3 * capHeight, sin(ancTheta3) * ancR3 * capRadius)\n"
        "ancTheta4 = 5.5\n"
        "ancY4 = 0.4\n"
        "ancR4 = sqrt(max(0.0, 1.0 - ancY4 * ancY4))\n"
        "anchorPos4 = vec3(cos(ancTheta4) * ancR4 * capRadius, stemHeight + ancY4 * capHeight, sin(ancTheta4) * ancR4 * capRadius)\n"
        "spotD1 = distance(posCap, anchorPos1)\n"
        "spotD2 = distance(posCap, anchorPos2)\n"
        "spotD3 = distance(posCap, anchorPos3)\n"
        "spotD4 = distance(posCap, anchorPos4)\n"
        "spotGlow1 = 1.0 - smoothstep(0.0, spotSize, spotD1)\n"
        "spotGlow2 = 1.0 - smoothstep(0.0, spotSize, spotD2)\n"
        "spotGlow3 = 1.0 - smoothstep(0.0, spotSize, spotD3)\n"
        "spotGlow4 = 1.0 - smoothstep(0.0, spotSize, spotD4)\n"
        "spotGlowTotal = max(max(spotGlow1, spotGlow2), max(spotGlow3, spotGlow4))\n"
        "capColor = mix(vec3(0.55, 0.06, 0.08), vec3(0.85, 0.25, 0.15), capY)\n"
        "capColorWithSpots = mix(capColor, vec3(0.95, 0.95, 0.9), spotGlowTotal)\n"
        "stemColor = vec3(0.92, 0.88, 0.78)\n"
        "Cd = capColorWithSpots * isCap + stemColor * isStem\n"
        "publish = sin(t * speed)\n" },
      { "Flower",
        PrimitiveTopology::Points,
        3000,
        "param float centerRadius = 0.35 [0.1, 1.0]\n"
        "param float petalLength = 1.2 [0.3, 3.0]\n"
        "param float petalWidth = 0.55 [0.1, 1.5]\n"
        "param float petalCurl = 0.25 [0.0, 1.0]\n"
        "param float speed = 0.4 [0.0, 3.0]\n"
        "goldenAngle = 2.3999632\n"
        "u = i / count\n"
        "crossAngle = goldenAngle * i\n"
        "b0 = 0.22\n"
        "b1 = 0.35\n"
        "b2 = 0.48\n"
        "b3 = 0.61\n"
        "b4 = 0.74\n"
        "b5 = 0.87\n"
        "isCenter = 1.0 - step(b0, u)\n"
        "isP1 = step(b0, u) - step(b1, u)\n"
        "isP2 = step(b1, u) - step(b2, u)\n"
        "isP3 = step(b2, u) - step(b3, u)\n"
        "isP4 = step(b3, u) - step(b4, u)\n"
        "isP5 = step(b4, u) - step(b5, u)\n"
        "isP6 = step(b5, u)\n"
        "uc = u / b0\n"
        "rc = sqrt(uc) * centerRadius\n"
        "centerDome = centerRadius * 0.3\n"
        "yDome = centerDome * sqrt(max(0.0, 1.0 - (rc / max(0.001, centerRadius)) * (rc / max(0.001, centerRadius))))\n"
        "posCenter = vec3(cos(crossAngle) * rc, yDome, sin(crossAngle) * rc)\n"
        "a1 = 0.0\n"
        "a2 = 1.0471976\n"
        "a3 = 2.0943951\n"
        "a4 = 3.1415927\n"
        "a5 = 4.1887902\n"
        "a6 = 5.2359878\n"
        "up1 = (u - b0) / (b1 - b0)\n"
        "up2 = (u - b1) / (b2 - b1)\n"
        "up3 = (u - b2) / (b3 - b2)\n"
        "up4 = (u - b3) / (b4 - b3)\n"
        "up5 = (u - b4) / (b5 - b4)\n"
        "up6 = (u - b5) / (1.0 - b5)\n"
        "taper1 = sin(clamp(up1, 0.0, 1.0) * 3.14159265)\n"
        "taper2 = sin(clamp(up2, 0.0, 1.0) * 3.14159265)\n"
        "taper3 = sin(clamp(up3, 0.0, 1.0) * 3.14159265)\n"
        "taper4 = sin(clamp(up4, 0.0, 1.0) * 3.14159265)\n"
        "taper5 = sin(clamp(up5, 0.0, 1.0) * 3.14159265)\n"
        "taper6 = sin(clamp(up6, 0.0, 1.0) * 3.14159265)\n"
        "crossR1 = petalWidth * 0.5 * taper1\n"
        "crossR2 = petalWidth * 0.5 * taper2\n"
        "crossR3 = petalWidth * 0.5 * taper3\n"
        "crossR4 = petalWidth * 0.5 * taper4\n"
        "crossR5 = petalWidth * 0.5 * taper5\n"
        "crossR6 = petalWidth * 0.5 * taper6\n"
        "lx1 = centerRadius + up1 * petalLength\n"
        "lx2 = centerRadius + up2 * petalLength\n"
        "lx3 = centerRadius + up3 * petalLength\n"
        "lx4 = centerRadius + up4 * petalLength\n"
        "lx5 = centerRadius + up5 * petalLength\n"
        "lx6 = centerRadius + up6 * petalLength\n"
        "lz1 = crossR1 * cos(crossAngle)\n"
        "lz2 = crossR2 * cos(crossAngle)\n"
        "lz3 = crossR3 * cos(crossAngle)\n"
        "lz4 = crossR4 * cos(crossAngle)\n"
        "lz5 = crossR5 * cos(crossAngle)\n"
        "lz6 = crossR6 * cos(crossAngle)\n"
        "ly1 = crossR1 * 0.25 * sin(crossAngle) + petalCurl * up1 * up1\n"
        "ly2 = crossR2 * 0.25 * sin(crossAngle) + petalCurl * up2 * up2\n"
        "ly3 = crossR3 * 0.25 * sin(crossAngle) + petalCurl * up3 * up3\n"
        "ly4 = crossR4 * 0.25 * sin(crossAngle) + petalCurl * up4 * up4\n"
        "ly5 = crossR5 * 0.25 * sin(crossAngle) + petalCurl * up5 * up5\n"
        "ly6 = crossR6 * 0.25 * sin(crossAngle) + petalCurl * up6 * up6\n"
        "posP1 = vec3(lx1 * cos(a1) - lz1 * sin(a1), ly1, lx1 * sin(a1) + lz1 * cos(a1))\n"
        "posP2 = vec3(lx2 * cos(a2) - lz2 * sin(a2), ly2, lx2 * sin(a2) + lz2 * cos(a2))\n"
        "posP3 = vec3(lx3 * cos(a3) - lz3 * sin(a3), ly3, lx3 * sin(a3) + lz3 * cos(a3))\n"
        "posP4 = vec3(lx4 * cos(a4) - lz4 * sin(a4), ly4, lx4 * sin(a4) + lz4 * cos(a4))\n"
        "posP5 = vec3(lx5 * cos(a5) - lz5 * sin(a5), ly5, lx5 * sin(a5) + lz5 * cos(a5))\n"
        "posP6 = vec3(lx6 * cos(a6) - lz6 * sin(a6), ly6, lx6 * sin(a6) + lz6 * cos(a6))\n"
        "breathe = 1.0 + 0.015 * sin(t * speed * 2.0)\n"
        "P = (posCenter * isCenter + posP1 * isP1 + posP2 * isP2 + posP3 * isP3 + posP4 * isP4 + posP5 * isP5 + posP6 * isP6) * breathe\n"
        "centerColor = mix(vec3(0.55, 0.35, 0.08), vec3(0.35, 0.20, 0.04), rc / max(0.001, centerRadius))\n"
        "petalColor1 = mix(vec3(0.85, 0.15, 0.45), vec3(0.98, 0.75, 0.85), up1)\n"
        "petalColor2 = mix(vec3(0.85, 0.15, 0.45), vec3(0.98, 0.75, 0.85), up2)\n"
        "petalColor3 = mix(vec3(0.85, 0.15, 0.45), vec3(0.98, 0.75, 0.85), up3)\n"
        "petalColor4 = mix(vec3(0.85, 0.15, 0.45), vec3(0.98, 0.75, 0.85), up4)\n"
        "petalColor5 = mix(vec3(0.85, 0.15, 0.45), vec3(0.98, 0.75, 0.85), up5)\n"
        "petalColor6 = mix(vec3(0.85, 0.15, 0.45), vec3(0.98, 0.75, 0.85), up6)\n"
        "Cd = centerColor * isCenter + petalColor1 * isP1 + petalColor2 * isP2 + petalColor3 * isP3 + petalColor4 * isP4 + petalColor5 * isP5 + petalColor6 * isP6\n"
        "publish = sin(t * speed)\n" },
      { "Pinecone",
        PrimitiveTopology::Points,
        2200,
        "param float coneHeight = 1.6 [0.4, 4.0]\n"
        "param float coneRadius = 0.55 [0.1, 2.0]\n"
        "param float bractRows = 14.0 [4.0, 30.0]\n"
        "param float bractAmp = 0.05 [0.0, 0.25]\n"
        "param float speed = 0.5 [0.0, 3.0]\n"
        "goldenAngle = 2.3999632\n"
        "u = i / count\n"
        "theta = goldenAngle * i\n"
        "b0 = 0.92\n"
        "isBody = 1.0 - step(b0, u)\n"
        "isStem = step(b0, u)\n"
        "uBody = u / b0\n"
        "yB = uBody * coneHeight\n"
        "rProfile = coneRadius * sqrt(max(0.0, 1.0 - uBody * uBody)) * (1.0 - 0.15 * uBody)\n"
        "ring = floor(uBody * bractRows)\n"
        "bump = bractAmp * sin(ring * 2.399632 + theta * 3.0)\n"
        "rBody = max(0.02, rProfile + bump)\n"
        "posBody = vec3(cos(theta) * rBody, yB, sin(theta) * rBody)\n"
        "stemLength = coneHeight * 0.12\n"
        "stemRadius = coneRadius * 0.18\n"
        "uStem = (u - b0) / (1.0 - b0)\n"
        "rStem = stemRadius\n"
        "posStem = vec3(cos(theta) * rStem, -uStem * stemLength, sin(theta) * rStem)\n"
        "breathe = 1.0 + 0.015 * sin(t * speed * 2.0)\n"
        "P = (posBody * isBody + posStem * isStem) * breathe\n"
        "baseColor = mix(vec3(0.30, 0.16, 0.06), vec3(0.55, 0.35, 0.15), uBody)\n"
        "bractShade = smoothstep(-bractAmp, bractAmp, bump)\n"
        "bodyColor = mix(baseColor * 0.85, baseColor * 1.15, bractShade)\n"
        "stemColor = vec3(0.25, 0.15, 0.08)\n"
        "Cd = bodyColor * isBody + stemColor * isStem\n"
        "publish = sin(t * speed)\n" },
      { "Rocket",
        PrimitiveTopology::Points,
        2600,
        "param float bodyHeight = 1.8 [0.5, 4.0]\n"
        "param float noseHeight = 0.7 [0.2, 2.0]\n"
        "param float radius = 0.35 [0.1, 1.0]\n"
        "param float finSpan = 0.5 [0.1, 1.5]\n"
        "param float finLength = 0.6 [0.1, 2.0]\n"
        "param float speed = 0.7 [0.0, 4.0]\n"
        "goldenAngle = 2.3999632\n"
        "u = i / count\n"
        "theta = goldenAngle * i\n"
        "b0 = 0.22\n"
        "b1 = 0.64\n"
        "b2 = 0.76\n"
        "b3 = 0.88\n"
        "isNose = 1.0 - step(b0, u)\n"
        "isBody = step(b0, u) - step(b1, u)\n"
        "isFin1 = step(b1, u) - step(b2, u)\n"
        "isFin2 = step(b2, u) - step(b3, u)\n"
        "isFin3 = step(b3, u)\n"
        "uNose = u / b0\n"
        "yNose = bodyHeight + uNose * noseHeight\n"
        "rNose = radius * (1.0 - uNose)\n"
        "posNose = vec3(cos(theta) * rNose, yNose, sin(theta) * rNose)\n"
        "uBody = (u - b0) / (b1 - b0)\n"
        "posBody = vec3(cos(theta) * radius, uBody * bodyHeight, sin(theta) * radius)\n"
        "finWidthHalf = radius * 0.35\n"
        "fa1 = 0.0\n"
        "fa2 = 2.0943951\n"
        "fa3 = 4.1887902\n"
        "s1 = (u - b1) / (b2 - b1)\n"
        "s2 = (u - b2) / (b3 - b2)\n"
        "s3 = (u - b3) / (1.0 - b3)\n"
        "rLine1 = radius + s1 * finSpan\n"
        "rLine2 = radius + s2 * finSpan\n"
        "rLine3 = radius + s3 * finSpan\n"
        "yLine1 = -s1 * finLength\n"
        "yLine2 = -s2 * finLength\n"
        "yLine3 = -s3 * finLength\n"
        "yOff1 = sin(theta) * finWidthHalf * (1.0 - s1)\n"
        "yOff2 = sin(theta) * finWidthHalf * (1.0 - s2)\n"
        "yOff3 = sin(theta) * finWidthHalf * (1.0 - s3)\n"
        "ly1 = yLine1 + yOff1\n"
        "ly2 = yLine2 + yOff2\n"
        "ly3 = yLine3 + yOff3\n"
        "posFin1 = vec3(rLine1 * cos(fa1), ly1, rLine1 * sin(fa1))\n"
        "posFin2 = vec3(rLine2 * cos(fa2), ly2, rLine2 * sin(fa2))\n"
        "posFin3 = vec3(rLine3 * cos(fa3), ly3, rLine3 * sin(fa3))\n"
        "breathe = 1.0 + 0.015 * sin(t * speed * 2.0)\n"
        "P = (posNose * isNose + posBody * isBody + posFin1 * isFin1 + posFin2 * isFin2 + posFin3 * isFin3) * breathe\n"
        "engineGlowSize = radius * 1.2\n"
        "distToEngine = distance(posBody, vec3(0.0, 0.0, 0.0))\n"
        "engineGlow = 1.0 - smoothstep(0.0, engineGlowSize, distToEngine)\n"
        "silverColor = vec3(0.75, 0.75, 0.78)\n"
        "engineColor = vec3(1.0, 0.55, 0.05)\n"
        "noseColor = mix(silverColor, vec3(0.85, 0.1, 0.1), uNose)\n"
        "bodyColor = mix(silverColor, engineColor, engineGlow)\n"
        "finColor = vec3(0.25, 0.25, 0.28)\n"
        "Cd = noseColor * isNose + bodyColor * isBody + finColor * isFin1 + finColor * isFin2 + finColor * isFin3\n"
        "publish = sin(t * speed)\n" }
   };
   return kPresets;
}

const std::vector<std::string>& FieldPrimitiveNode::PresetNames()
{
   static std::vector<std::string> kNames;
   if (kNames.empty())
   {
      for (const auto& p : Presets())
         kNames.push_back(p.name);
   }
   return kNames;
}

void FieldPrimitiveNode::LoadPreset(int index)
{
   const auto& presets = Presets();
   if (index >= 0 && index < (int)presets.size())
   {
      presetIndex = index;
      topology = (int)presets[index].topology;
      if (presets[index].defaultCount > 0)
         count = presets[index].defaultCount;
      code = presets[index].code;
      Apply();
   }
}

Field::DeviceFile FieldPrimitiveNode::ToDeviceFile() const
{
   Field::DeviceFile device;
   device.domain = "primitive";
   device.code = code;
   for (const auto& p : mParamTable.Params())
   {
      if (p.isDeclared)
         device.params[p.name] = p.value;
   }
   device.nodeSettings["maxElements"] = (double)maxElements;
   device.nodeSettings["count"] = (double)count;
   device.nodeSettings["topology"] = (double)topology;
   return device;
}

void FieldPrimitiveNode::LoadDeviceFile(const Field::DeviceFile& device)
{
   code = device.code;
   auto itMax = device.nodeSettings.find("maxElements");
   if (itMax != device.nodeSettings.end())
      maxElements = (int)itMax->second;
   auto itCount = device.nodeSettings.find("count");
   if (itCount != device.nodeSettings.end())
      count = (int)itCount->second;
   auto itTop = device.nodeSettings.find("topology");
   if (itTop != device.nodeSettings.end())
      topology = (int)itTop->second;
   Apply();
   for (const auto& kv : device.params)
   {
      Field::ParamEntry* p = mParamTable.Find(kv.first);
      if (p != nullptr)
         p->value = kv.second;
   }
}

FieldPrimitiveNode::FieldPrimitiveNode()
{
   mPublishOutput.owner = this;
   const auto& presets = Presets();
   if (!presets.empty())
   {
      code = presets[0].code;
      topology = (int)presets[0].topology;
      count = presets[0].defaultCount;
   }
   else
   {
      code = "u = i / count\nangle = u * 6.2831853\nP = vec3(cos(angle), sin(angle), 0.0)\n";
      topology = 0;
      count = 256;
   }
   Apply();
}

bool FieldPrimitiveNode::Apply()
{
   pinRefusal.clear();
   std::vector<Field::Token> tokens;
   Field::FieldError err;

   if (!Field::Lex(code, tokens, err))
   {
      mLastError = err.message + " at line " + std::to_string(err.span.line) + ", col " + std::to_string(err.span.col);
      return false;
   }

   Field::AstNodePtr astProgram;
   if (!Field::ParseProgram(tokens, astProgram, err))
   {
      mLastError = err.message + " at line " + std::to_string(err.span.line) + ", col " + std::to_string(err.span.col);
      return false;
   }

   Field::ElementIRProgram irProgram;
   if (!Field::LowerElementProgramToIR(astProgram, irProgram, err))
   {
      mLastError = err.message + " at line " + std::to_string(err.span.line) + ", col " + std::to_string(err.span.col);
      return false;
   }

   auto prog = std::make_shared<Field::ElementProgram>();
   if (!Field::EmitElementBytecode(irProgram, *prog, err))
   {
      mLastError = err.message + " at line " + std::to_string(err.span.line) + ", col " + std::to_string(err.span.col);
      return false;
   }

   {
      std::vector<Field::DeclaredPin> declOut, declIn;
      for (const auto& d : irProgram.declaredOutputs)
         declOut.push_back({ d.name, d.typeName, Field::DomainToString(d.domain), true });
      for (const auto& d : irProgram.declaredInputs)
         declIn.push_back({ d.name, d.typeName, Field::DomainToString(d.domain), false });

      std::string pinNotice, pinRefusal;
      bool outOk = Field::ReconcileFieldPins(mOutputPins, declOut, mNodeIndex, NativeOutputCount(), pinNotice, pinRefusal);
      // native input count is 0 for FieldPrimitiveNode (pure generator)
      bool inOk = outOk && Field::ReconcileFieldPins(mInputPins, declIn, mNodeIndex, /*nativeCount=*/0, pinNotice, pinRefusal);
      if (!outOk || !inOk)
      {
         mLastError = pinRefusal;
         this->pinRefusal = pinRefusal;
         return false;
      }
      if (!pinNotice.empty())
         mNotice = pinNotice;
   }

   mParamTable.Reconcile(prog->declaredParams, mNodeIndex, mNotice);

   Field::FieldState newState;
   for (const auto& ds : prog->declaredStates)
   {
      newState.DeclareCell(ds.name, ds.typeName, ds.type, ds.lanes, ds.initialValues, ds.domain);
   }
   size_t allocCount = (mActualElementCount > 0) ? (size_t)mActualElementCount : (size_t)std::max(1, maxElements);
   newState.Allocate(Field::Domain::Element, allocCount);
   newState.Transplant(mState);
   mState = std::move(newState);
   mState.FormatCost(mCostReadout, sizeof(mCostReadout), (int)allocCount);

   for (const auto& decl : prog->declaredAttribs)
   {
      mStore.DeclareAttrib(decl.first, decl.second);
   }

   mProgram = prog;
   mLastError.clear();
   mMeshRevision = NextMeshRevision();
   mLastBuiltCount = 0;
   mLastEvalT = -999999.0f;
   return true;
}

void FieldPrimitiveNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;

   unsigned long long currentEpoch = Transport::Instance().ResetEpoch();
   if (currentEpoch != mLastResetEpoch)
   {
      mState.ResetAll();
      mLastResetEpoch = currentEpoch;
   }

   if (!mProgram && mLastError.empty())
   {
      Apply();
   }

   int n = std::max(1, count);
   size_t boundCount = std::min((size_t)n, (size_t)std::max(1, maxElements));
   mWasTruncated = ((size_t)n > boundCount);
   mActualElementCount = (int)boundCount;

   double t = Transport::Instance().Seconds();

   std::map<std::string, float> paramValues = mParamTable.ValueMap();
   size_t paramHash = 0;
   for (const auto& kv : paramValues)
   {
      paramHash ^= std::hash<std::string>{}(kv.first) + 0x9e3779b9 + (paramHash << 6) + (paramHash >> 2);
      paramHash ^= std::hash<float>{}(kv.second) + 0x9e3779b9 + (paramHash << 6) + (paramHash >> 2);
   }

   bool needRebuild = ((unsigned long long)boundCount != mLastBuiltCount) ||
                      (topology != mLastBuiltTopology) ||
                      (mProgram && mProgram->isTimeDependent && (float)t != mLastEvalT) ||
                      (mOutMesh.vertices.empty()) ||
                      (!mState.Cells().empty()) ||
                      (paramHash != mLastParamHash);

   if (!needRebuild)
      return;

   mLastParamHash = paramHash;

   // Synthesize base scaffold mesh according to topology
   Mesh baseMesh;
   auto top = static_cast<PrimitiveTopology>(topology);
   if (top == PrimitiveTopology::Plane)
   {
      int cols = std::max(2, (int)std::round(std::sqrt((double)boundCount)));
      int rows = std::max(2, (int)(boundCount / (size_t)cols));
      baseMesh.vertices.resize(boundCount);
      for (size_t idx = 0; idx < boundCount; ++idx)
      {
         int r = std::min((int)(idx / (size_t)cols), rows - 1);
         int c = std::min((int)(idx % (size_t)cols), cols - 1);
         float u = (cols > 1) ? ((float)c / (float)(cols - 1)) : 0.0f;
         float v = (rows > 1) ? ((float)r / (float)(rows - 1)) : 0.0f;
         baseMesh.vertices[idx].px = (u - 0.5f) * 2.0f;
         baseMesh.vertices[idx].py = 0.0f;
         baseMesh.vertices[idx].pz = (v - 0.5f) * 2.0f;
         baseMesh.vertices[idx].nx = 0.0f;
         baseMesh.vertices[idx].ny = 1.0f;
         baseMesh.vertices[idx].nz = 0.0f;
         baseMesh.vertices[idx].u = u;
         baseMesh.vertices[idx].v = v;
      }
      for (int r = 0; r < rows - 1; ++r)
      {
         for (int c = 0; c < cols - 1; ++c)
         {
            unsigned int i00 = (unsigned int)(r * cols + c);
            unsigned int i10 = (unsigned int)(r * cols + (c + 1));
            unsigned int i01 = (unsigned int)((r + 1) * cols + c);
            unsigned int i11 = (unsigned int)((r + 1) * cols + (c + 1));
            if (i00 < boundCount && i10 < boundCount && i01 < boundCount && i11 < boundCount)
            {
               baseMesh.indices.push_back(i00);
               baseMesh.indices.push_back(i01);
               baseMesh.indices.push_back(i11);
               baseMesh.indices.push_back(i00);
               baseMesh.indices.push_back(i11);
               baseMesh.indices.push_back(i10);
            }
         }
      }
   }
   else if (top == PrimitiveTopology::Sphere)
   {
      int cols = std::max(4, (int)std::round(std::sqrt((double)boundCount * 1.5)));
      int rows = std::max(3, (int)(boundCount / (size_t)cols));
      baseMesh.vertices.resize(boundCount);
      for (size_t idx = 0; idx < boundCount; ++idx)
      {
         int r = std::min((int)(idx / (size_t)cols), rows - 1);
         int c = std::min((int)(idx % (size_t)cols), cols - 1);
         float v = (rows > 1) ? ((float)r / (float)(rows - 1)) : 0.0f;
         float theta = (v - 0.5f) * 3.14159265f;
         float cosTheta = std::cos(theta);
         float sinTheta = std::sin(theta);
         float u = (float)c / (float)cols;
         float phi = u * 6.2831853f;
         float cosPhi = std::cos(phi);
         float sinPhi = std::sin(phi);
         baseMesh.vertices[idx].px = cosTheta * cosPhi;
         baseMesh.vertices[idx].py = sinTheta;
         baseMesh.vertices[idx].pz = cosTheta * sinPhi;
         baseMesh.vertices[idx].nx = baseMesh.vertices[idx].px;
         baseMesh.vertices[idx].ny = baseMesh.vertices[idx].py;
         baseMesh.vertices[idx].nz = baseMesh.vertices[idx].pz;
         baseMesh.vertices[idx].u = u;
         baseMesh.vertices[idx].v = v;
      }
      for (int r = 0; r < rows - 1; ++r)
      {
         for (int c = 0; c < cols; ++c)
         {
            int nextC = (c + 1) % cols;
            unsigned int i00 = (unsigned int)(r * cols + c);
            unsigned int i10 = (unsigned int)(r * cols + nextC);
            unsigned int i01 = (unsigned int)((r + 1) * cols + c);
            unsigned int i11 = (unsigned int)((r + 1) * cols + nextC);
            if (i00 < boundCount && i10 < boundCount && i01 < boundCount && i11 < boundCount)
            {
               baseMesh.indices.push_back(i00);
               baseMesh.indices.push_back(i10);
               baseMesh.indices.push_back(i11);
               baseMesh.indices.push_back(i00);
               baseMesh.indices.push_back(i11);
               baseMesh.indices.push_back(i01);
            }
         }
      }
   }
   else if (top == PrimitiveTopology::Cylinder)
   {
      int cols = std::max(4, (int)std::round(std::sqrt((double)boundCount * 1.5)));
      int rows = std::max(2, (int)(boundCount / (size_t)cols));
      baseMesh.vertices.resize(boundCount);
      for (size_t idx = 0; idx < boundCount; ++idx)
      {
         int r = std::min((int)(idx / (size_t)cols), rows - 1);
         int c = std::min((int)(idx % (size_t)cols), cols - 1);
         float v = (rows > 1) ? ((float)r / (float)(rows - 1)) : 0.0f;
         float y = (v - 0.5f) * 2.0f;
         float u = (float)c / (float)cols;
         float phi = u * 6.2831853f;
         float cosPhi = std::cos(phi);
         float sinPhi = std::sin(phi);
         baseMesh.vertices[idx].px = cosPhi;
         baseMesh.vertices[idx].py = y;
         baseMesh.vertices[idx].pz = sinPhi;
         baseMesh.vertices[idx].nx = cosPhi;
         baseMesh.vertices[idx].ny = 0.0f;
         baseMesh.vertices[idx].nz = sinPhi;
         baseMesh.vertices[idx].u = u;
         baseMesh.vertices[idx].v = v;
      }
      for (int r = 0; r < rows - 1; ++r)
      {
         for (int c = 0; c < cols; ++c)
         {
            int nextC = (c + 1) % cols;
            unsigned int i00 = (unsigned int)(r * cols + c);
            unsigned int i10 = (unsigned int)(r * cols + nextC);
            unsigned int i01 = (unsigned int)((r + 1) * cols + c);
            unsigned int i11 = (unsigned int)((r + 1) * cols + nextC);
            if (i00 < boundCount && i10 < boundCount && i01 < boundCount && i11 < boundCount)
            {
               baseMesh.indices.push_back(i00);
               baseMesh.indices.push_back(i10);
               baseMesh.indices.push_back(i11);
               baseMesh.indices.push_back(i00);
               baseMesh.indices.push_back(i11);
               baseMesh.indices.push_back(i01);
            }
         }
      }
   }
   else if (top == PrimitiveTopology::Torus)
   {
      int cols = std::max(4, (int)std::round(std::sqrt((double)boundCount)));
      int rows = std::max(4, (int)(boundCount / (size_t)cols));
      baseMesh.vertices.resize(boundCount);
      float R = 1.0f;
      float rMinor = 0.35f;
      for (size_t idx = 0; idx < boundCount; ++idx)
      {
         int r = std::min((int)(idx / (size_t)cols), rows - 1);
         int c = std::min((int)(idx % (size_t)cols), cols - 1);
         float v = (float)r / (float)rows;
         float theta = v * 6.2831853f;
         float cosTheta = std::cos(theta);
         float sinTheta = std::sin(theta);
         float u = (float)c / (float)cols;
         float phi = u * 6.2831853f;
         float cosPhi = std::cos(phi);
         float sinPhi = std::sin(phi);
         float dist = R + rMinor * cosTheta;
         baseMesh.vertices[idx].px = dist * cosPhi;
         baseMesh.vertices[idx].py = rMinor * sinTheta;
         baseMesh.vertices[idx].pz = dist * sinPhi;
         baseMesh.vertices[idx].nx = cosTheta * cosPhi;
         baseMesh.vertices[idx].ny = sinTheta;
         baseMesh.vertices[idx].nz = cosTheta * sinPhi;
         baseMesh.vertices[idx].u = u;
         baseMesh.vertices[idx].v = v;
      }
      for (int r = 0; r < rows; ++r)
      {
         int nextR = (r + 1) % rows;
         for (int c = 0; c < cols; ++c)
         {
            int nextC = (c + 1) % cols;
            unsigned int i00 = (unsigned int)(r * cols + c);
            unsigned int i10 = (unsigned int)(r * cols + nextC);
            unsigned int i01 = (unsigned int)(nextR * cols + c);
            unsigned int i11 = (unsigned int)(nextR * cols + nextC);
            if (i00 < boundCount && i10 < boundCount && i01 < boundCount && i11 < boundCount)
            {
               baseMesh.indices.push_back(i00);
               baseMesh.indices.push_back(i01);
               baseMesh.indices.push_back(i10);
               baseMesh.indices.push_back(i10);
               baseMesh.indices.push_back(i01);
               baseMesh.indices.push_back(i11);
            }
         }
      }
   }
   else if (top == PrimitiveTopology::Disc)
   {
      int rings = std::max(2, (int)std::round(std::sqrt((double)boundCount * 0.5)));
      int sectors = std::max(3, (int)(boundCount / (size_t)rings));
      baseMesh.vertices.resize(boundCount);
      for (size_t idx = 0; idx < boundCount; ++idx)
      {
         int rg = std::min((int)(idx / (size_t)sectors), rings - 1);
         int s = std::min((int)(idx % (size_t)sectors), sectors - 1);
         float v = (rings > 1) ? ((float)rg / (float)(rings - 1)) : 0.0f;
         float rad = v;
         float u = (float)s / (float)sectors;
         float phi = u * 6.2831853f;
         baseMesh.vertices[idx].px = std::cos(phi) * rad;
         baseMesh.vertices[idx].py = 0.0f;
         baseMesh.vertices[idx].pz = std::sin(phi) * rad;
         baseMesh.vertices[idx].nx = 0.0f;
         baseMesh.vertices[idx].ny = 1.0f;
         baseMesh.vertices[idx].nz = 0.0f;
         baseMesh.vertices[idx].u = u;
         baseMesh.vertices[idx].v = v;
      }
      for (int rg = 0; rg < rings - 1; ++rg)
      {
         for (int s = 0; s < sectors; ++s)
         {
            int nextS = (s + 1) % sectors;
            unsigned int i00 = (unsigned int)(rg * sectors + s);
            unsigned int i10 = (unsigned int)(rg * sectors + nextS);
            unsigned int i01 = (unsigned int)((rg + 1) * sectors + s);
            unsigned int i11 = (unsigned int)((rg + 1) * sectors + nextS);
            if (i00 < boundCount && i10 < boundCount && i01 < boundCount && i11 < boundCount)
            {
               baseMesh.indices.push_back(i00);
               baseMesh.indices.push_back(i01);
               baseMesh.indices.push_back(i10);
               baseMesh.indices.push_back(i10);
               baseMesh.indices.push_back(i01);
               baseMesh.indices.push_back(i11);
            }
         }
      }
   }
   else // Points (topology == 0)
   {
      baseMesh.vertices.resize(boundCount);
      for (size_t i = 0; i < boundCount; ++i)
      {
         float u = (boundCount > 1) ? ((float)i / (float)(boundCount - 1)) : 0.0f;
         baseMesh.vertices[i].px = (u - 0.5f) * 2.0f;
         baseMesh.vertices[i].py = 0.0f;
         baseMesh.vertices[i].pz = 0.0f;
         baseMesh.vertices[i].nx = 0.0f;
         baseMesh.vertices[i].ny = 1.0f;
         baseMesh.vertices[i].nz = 0.0f;
         baseMesh.vertices[i].u = u;
         baseMesh.vertices[i].v = 0.0f;
      }
   }

   if (!mProgram)
   {
      mOutMesh = baseMesh;
      mMeshRevision = NextMeshRevision();
      mLastBuiltCount = (unsigned long long)boundCount;
      mLastBuiltTopology = topology;
      return;
   }

   if (mState.ElementStorageCount() != boundCount)
   {
      mState.Allocate(Field::Domain::Element, boundCount);
      mState.FormatCost(mCostReadout, sizeof(mCostReadout), (int)boundCount);
   }

   // 1. Gather (AoS -> SoA)
   mStore.FromMesh(baseMesh, boundCount);

   // 2. Execute Element VM
   Field::ExecutionEnv env;
   env.t = t;
   env.dt = (mLastEvalT > -900000.0f) ? (t - mLastEvalT) : (1.0 / 60.0);
   env.frame = (double)frameId;
   env.params = &paramValues;
   env.state = &mState;
   std::string vmErr;
   mVM.Execute(*mProgram, mStore, env, vmErr);

   // 3. Scatter (SoA -> AoS)
   mStore.ToMesh(mOutMesh, mProgram->WriteMask(), baseMesh, boundCount);

   mMeshRevision = NextMeshRevision();
   mLastBuiltCount = (unsigned long long)boundCount;
   mLastBuiltTopology = topology;
   mLastEvalT = (float)t;
}
