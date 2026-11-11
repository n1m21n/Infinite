# A/B toggle: exact dielectric Fresnel (Render3DFresnel.h) for the main specular lobe in GL forward, GL PT, Metal.
import sys
R='/Users/namansoni/Infinite/infinte-base/'
M='src/engine/rhi/MetalDevice.mm'; G='src/nodes/Geometry3DNodes.cpp'; P='src/nodes/Render3DPathTrace.cpp'
E=[(M,'#include "nodes/Render3DSheen.h"\n','#include "nodes/Render3DSheen.h"\n#include "nodes/Render3DFresnel.h"\n'),
   (M,'float3 fresnel(float cosT, float3 f0) { return f0 + (1.0 - f0) * pow(clamp(1.0 - cosT, 0.0, 1.0), 5.0); }',
      ')" R3D_FRESNEL_SRC R"(\nfloat3 fresnel(float cosT, float3 f0) { return fresnelExact(cosT, f0); }'),
   (G,'#include "Render3DSheen.h"\n','#include "Render3DSheen.h"\n#include "Render3DFresnel.h"\n'),
   (G,'      R3D_SHEEN_GLSL\n','      R3D_SHEEN_GLSL\n      R3D_FRESNEL_SRC\n'),
   (G,'"   return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);\\n"','"   return fresnelExact(cosTheta, f0);\\n"'),
   (P,'#include "Render3DSheen.h"\n','#include "Render3DSheen.h"\n#include "Render3DFresnel.h"\n'),
   (P,'      R3D_SHEEN_GLSL\n','      R3D_SHEEN_GLSL\n      R3D_FRESNEL_SRC\n'),
   (P,'"   vec3 F = s.f0 + (vec3(1.0) - s.f0) * schlick5(vDotH);\\n"','"   vec3 F = fresnelExact(vDotH, s.f0);\\n"')]
on=sys.argv[1]=='on'
for f,a,b in E:
    t=open(R+f).read(); s,d=(a,b) if on else (b,a)
    if t.count(d)==1 and t.count(s)==0: continue
    assert t.count(s)==1,(f,s[:60],t.count(s)); open(R+f,'w').write(t.replace(s,d))
print('fresnel', 'exact' if on else 'schlick')
