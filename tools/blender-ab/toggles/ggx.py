import os, sys
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../.."))
# python3 ggx.py on|off : the height-correlated Smith A/B (Block D 1.1b)
on = sys.argv[1] == 'on'
M='src/engine/rhi/MetalDevice.mm'; G='src/nodes/Geometry3DNodes.cpp'
pairs = {
 M: [('#include "nodes/Render3DTonemap.h"\n#include "nodes/Render3DBsdf.h"', '#include "nodes/Render3DTonemap.h"'),
     (')" R3D_BSDF_SRC R"(\n', 'float geomSchlick(float nDotV, float rough) { const float r = rough + 1.0, k = (r * r) / 8.0; return nDotV / (nDotV * (1.0 - k) + k); }\n'),
     ('ggxG2(nDotV, nDotL, r)', 'geomSchlick(nDotV, r) * geomSchlick(nDotL, r)'),
     ('ggxG2(nDotV, nDotL, rough)', 'geomSchlick(nDotV, rough) * geomSchlick(nDotL, rough)'),
     ('ggxG2(nDotV, max(nls, 1e-4), rough)', 'geomSchlick(nDotV, rough) * geomSchlick(max(nls, 1e-4), rough)')],
 G: [('      R3D_BSDF_SRC\n', '      "float geometrySchlickGGX(float nDotV, float rough) {\\n"\n      "   float r = rough + 1.0;\\n"\n      "   float k = (r * r) / 8.0;\\n"\n      "   return nDotV / (nDotV * (1.0 - k) + k);\\n"\n      "}\\n"\n'),
     ('ggxG2(nDotV, nDotL, rough)', 'geometrySchlickGGX(nDotV, rough) * geometrySchlickGGX(nDotL, rough)'),
     ('ggxG2(nDotV, nDotL, ccRough)', 'geometrySchlickGGX(nDotV, ccRough) * geometrySchlickGGX(nDotL, ccRough)'),
     ('#include "Render3DSheen.h"\n#include "Render3DBsdf.h"', '#include "Render3DSheen.h"')],
}
for f, ps in pairs.items():
    t = open(f).read()
    for a, b in ps:
        src, dst = (b, a) if on else (a, b)
        assert t.count(src) == 1, (f, src[:50], t.count(src))
        t = t.replace(src, dst)
    open(f, 'w').write(t)
print('ggxG2', 'on' if on else 'off')
