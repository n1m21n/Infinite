"""Row 7.2, case carpet_pile: N thin cones (default 200 k) scattered on a 2 x 2 plane (Distribute Points on Faces, local density = N on the unit plane, cap 200 k) through Instance on Points.
No Blender truth: the gate is T renders under the memory cap and L holds 30 fps at 1080p.
python3 carpet_pile.py out.inf [N] [size]"""
import sys

out = sys.argv[1]
n = int(sys.argv[2]) if len(sys.argv) > 2 else 200000
size = int(sys.argv[3]) if len(sys.argv) > 3 else 1080
w = size * 16 // 9 if size >= 720 else size
open(out, "w").write(f"""infinite-patch 1
node 1 3D Plane
  f rotX -1.5707963
  f scaleX 2
  f scaleY 2
  c color 0.35 0.3 0.25
  f roughness 0.9
end
node 2 3D Cone
  f scaleX 0.12
  f scaleZ 0.12
  f scaleY 1
end
node 3 3D Instance on Points
  i pointMode 0
  i maxPoints {n}
  f instanceScale 0.05
  f scaleRandom 0.35
  f rotationRandom 0.15
  b alignToNormal 1
  b inheritMaterial 0
  c color 0.62 0.2 0.16
  f metallic 0
  f roughness 0.7
end
node 4 3D Camera
  f fov 40
  f distance 1.6
  f azimuth 30
  f elevation 28
  f targetX 0
  f targetY 0
  f targetZ 0
end
node 5 3D Light
  i type Directional
  f azimuth 40
  f elevation 35
  c color 1 0.96 0.9
  f intensity 3
end
node 6 3D Render 3D
  f width {w}
  f height {size}
  i tonemap None
  b shadows 1
  c ambient 0.35 0.37 0.42
  i ptSamples 64
  i ptBounces 4
end
node 7 Utility Output
end
node 8 3D Distribute Points on Faces
  f density {n}
  f seed 7
  f pointSize 2
end
geo 6 0 1
geo 8 0 1
geo 3 points 1
geo 3 2 8
geo 3 shape 2
geo 6 1 3
geo 6 camera 4
geo 6 light_1 5
cable 7 0 6
""")
