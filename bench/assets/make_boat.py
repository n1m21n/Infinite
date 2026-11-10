# Run: Blender -b -P bench/assets/make_boat.py -- <abs path>/bench/assets/boat
# Small wooden sloop. Model coords (x=length, bow +x; u=up, waterline u=0; s=side).
# Blender is Z-up, so V() maps (x,u,s)->(x,s,u); the OBJ export turns it back to Y-up.
import bpy, bmesh, math, sys
out = sys.argv[sys.argv.index("--")+1]
bpy.ops.wm.read_factory_settings(use_empty=True)
def V(x,u,s): return (x,s,u)
L=3.2; B=1.10
def hb(t):
    if t>=0.4: return B/2*math.sqrt(max(0.0,1-((t-0.4)/0.6)**2))
    return B/2*(0.80+0.20*math.sin(t/0.4*math.pi/2))
def sheer(t): return 0.30+0.14*t*t
def keel(t): return -0.06-0.24*math.sin(math.pi*min(1.0,t*0.92+0.04))**0.45
NS=64; NR=24; n=3.0
def obj_from_bm(name,bm):
    me=bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    o=bpy.data.objects.new(name,me); bpy.context.collection.objects.link(o); return o
bm=bmesh.new(); rows=[]
for i in range(NS+1):
    t=i/NS; x=(t-0.5)*L; h=max(hb(t),0.004); sh,kl=sheer(t),keel(t)
    half=[]
    for j in range(NR+1):
        a=j/NR*math.pi/2
        s=h*math.cos(a)**(2/n); u=sh-(sh-kl)*math.sin(a)**(2/n)
        half.append((x,u,s))
    ring=[(x,u,-s) for x,u,s in half]+[(x,u,s) for x,u,s in reversed(half[:-1])]
    rows.append([bm.verts.new(V(*p)) for p in ring])
for i in range(NS):
    a,b=rows[i],rows[i+1]
    for j in range(len(a)-1): bm.faces.new((a[j],a[j+1],b[j+1],b[j]))
bm.faces.new(rows[0])
hull=obj_from_bm("hull",bm)
for p in hull.data.polygons: p.use_smooth=True
# deck just under the sheer
bm=bmesh.new(); top=[((t-0.5)*L, sheer(t)-0.015, hb(t)*0.985) for t in [i/NS for i in range(NS+1)]]
vs=[bm.verts.new(V(x,u,-s)) for x,u,s in top]+[bm.verts.new(V(x,u,s)) for x,u,s in reversed(top[1:-1])]
f=bm.faces.new(vs); bmesh.ops.triangulate(bm,faces=[f]); deck=obj_from_bm("deck",bm)
def box(name,c,size,bevel=0.0):
    bm=bmesh.new(); bmesh.ops.create_cube(bm,size=1.0)
    for v in bm.verts:
        x,y,z=v.co; v.co=V(c[0]+x*size[0],c[1]+z*size[1],c[2]+y*size[2])
    o=obj_from_bm(name,bm)
    if bevel: m=o.modifiers.new("b","BEVEL"); m.width=bevel; m.segments=3
    return o
cab=box("cabin",(-0.30,0.43,0),(0.95,0.26,0.62),0.035)
roof=box("roof",(-0.30,0.575,0),(1.03,0.035,0.70),0.012)
winL=box("winL",(-0.26,0.46,0.312),(0.62,0.08,0.008)); winR=box("winR",(-0.26,0.46,-0.312),(0.62,0.08,0.008))
winF=box("winF",(0.177,0.46,0),(0.008,0.08,0.42))
fin=box("fin",(0.05,-0.42,0),(0.75,0.30,0.06),0.02)
rud=box("rudder",(-1.52,-0.12,0),(0.18,0.34,0.035),0.01)
def tube(name,pts,r):
    cu=bpy.data.curves.new(name,"CURVE"); cu.dimensions="3D"; cu.bevel_depth=r; cu.bevel_resolution=3; cu.use_fill_caps=True
    sp=cu.splines.new("POLY"); sp.points.add(len(pts)-1)
    for k,p in enumerate(pts): sp.points[k].co=(*V(*p),1)
    o=bpy.data.objects.new(name,cu); bpy.context.collection.objects.link(o); return o
rail=[((t-0.5)*L, sheer(t)+0.012, hb(t)) for t in [i/NS for i in range(2,NS-1)]]
rR=tube("railR",rail,0.02); rL=tube("railL",[(x,u,-s) for x,u,s in rail],0.02)
MX=0.40
mast=tube("mast",[(MX,0.30,0),(MX,4.0,0)],0.038)
boom=tube("boom",[(MX,0.80,0),(-1.15,0.86,0)],0.03)
fore=tube("forestay",[(MX,3.75,0),(1.58,0.42,0)],0.006)
back=tube("backstay",[(MX,4.0,0),(-1.58,0.32,0)],0.006)
post=tube("post",[(-1.45,0.30,0),(-1.45,0.65,0)],0.015)
shR=tube("shroudR",[(MX,3.6,0),(MX-0.05,0.32,0.53)],0.005); shL=tube("shroudL",[(MX,3.6,0),(MX-0.05,0.32,-0.53)],0.005)
def sail(name,tack,head,clew,belly):
    bm=bmesh.new(); N=18; rows=[]
    for i in range(N+1):
        a=i/N; row=[]
        for k in range(N+1-i):
            b=k/N; c=1-a-b
            p=[tack[q]*c+head[q]*a+clew[q]*b for q in range(3)]
            w=27*a*b*c  # 0 on edges, 1 in the middle
            p[2]+=belly*w
            row.append(bm.verts.new(V(*p)))
        rows.append(row)
    for i in range(N):
        A,Bb=rows[i],rows[i+1]
        for k in range(len(Bb)):
            bm.faces.new((A[k],A[k+1],Bb[k]))
            if k+1<len(Bb): bm.faces.new((A[k+1],Bb[k+1],Bb[k]))
    o=obj_from_bm(name,bm); m=o.modifiers.new("s","SOLIDIFY"); m.thickness=0.008
    for p in o.data.polygons: p.use_smooth=True
    return o
main=sail("main",(MX-0.04,0.84,0),(MX-0.04,3.9,0),(-1.10,0.90,0.10),0.16)
jib=sail("jib",(1.50,0.48,0),(MX+0.06,3.55,0),(0.55,0.62,0.28),0.12)
lan=None
bm=bmesh.new(); bmesh.ops.create_uvsphere(bm,u_segments=16,v_segments=10,radius=0.045)
for v in bm.verts: v.co=(v.co[0]+(-1.45), v.co[1]+0.0, v.co[2]+0.70)
lan=obj_from_bm("lantern",bm)
for p in lan.data.polygons: p.use_smooth=True
groups={"hull":[hull,fin,rud],"deck":[deck,roof],"cabin":[cab],"windows":[winL,winR,winF],
        "trim":[post,rR,rL,mast,boom,fore,back,shR,shL],"sail":[main,jib],"lantern":[lan]}
for name,objs in groups.items():
    for o in bpy.data.objects: o.select_set(o in objs)
    bpy.context.view_layer.objects.active=objs[0]
    bpy.ops.wm.obj_export(filepath=f"{out}/{name}.obj",export_selected_objects=True,export_materials=False,export_uv=False,apply_modifiers=True,export_triangulated_mesh=True,export_normals=True)
print("BOAT_OK")
