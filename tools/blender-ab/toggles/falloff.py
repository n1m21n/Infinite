# A/B toggle: drop raster/live's 20x cap on hard inverse-square falloff (Cycles and GL path tracer have none).
import sys
R='/Users/namansoni/Infinite/infinte-base/'
E=[('src/engine/rhi/MetalDevice.mm','return min(1.0 / max(dist * dist * -k, 1e-4), 20.0); }','return 1.0 / max(dist * dist * -k, 1e-4); }'),
   ('src/nodes/Geometry3DNodes.cpp','"   return min(1.0 / max(dist * dist * -k, 1e-4), 20.0);\\n"','"   return 1.0 / max(dist * dist * -k, 1e-4);\\n"')]
on=sys.argv[1]=='on'
for f,a,b in E:
    t=open(R+f).read(); s,d=(a,b) if on else (b,a)
    if d in t: continue
    assert t.count(s)==1,(f,s); open(R+f,'w').write(t.replace(s,d))
print('falloff cap', 'removed' if on else 'kept')
