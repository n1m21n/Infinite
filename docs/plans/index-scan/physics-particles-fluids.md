# Index scan: physics, particles, fluids, cloth, soft bodies

Note: indexopensource.com was blocked (403), so candidates were NOT cross-checked against the index list. Peers were chosen from memory and the first-pass doc (docs/plans/index-opensource-scan.md:22).

## Projects read (licence verified from raw LICENSE before reading source)
| Repo | Licence | Branch | Files read |
|---|---|---|---|
| jrouwe/JoltPhysics | MIT | master | Jolt/Physics/SoftBody/SoftBodyMotionProperties.cpp (ApplyPressure 291, IntegratePositions 325, ApplyVolumeConstraints 458, ApplyEdgeConstraints 573, ApplyLRAConstraints 679, collision 700) |
| InteractiveComputerGraphics/PositionBasedDynamics | MIT | master | PositionBasedDynamics/PositionBasedDynamics.cpp (solve_IsometricBendingConstraint 186, solve_ShapeMatchingConstraint 501) |
| InteractiveComputerGraphics/SPlisHSPlasH | MIT | master | SPlisHSPlasH/PBF/TimeStepPBF.cpp (density constraint / lambda ~285-340) |
| PavelDoGreat/WebGL-Fluid-Simulation | MIT | master | script.js (advection 746, divergence ~790, vorticity 835, pressure 868, step() 1231) |

Verified MIT but not read (time): erincatto/box2d (MIT), doyubkim/fluid-engine-dev (MIT).
Skipped: bulletphysics/bullet3 (zlib, permitted, but LICENSE says Extras/examples/ThirdPartyLibs differ, so skipped to stay safe); matthias-research 10-Minute-Physics (LICENSE URL 404, could not verify). Not fetched: any GPL/AGPL project.

## What Infinite does today
- The brief's "10 existing rigid hits" are NOT physics. grep -i rigid in src/: all are "rigid transform / rigid group" comments (src/nodes/GeometryOpNodes.h:208, Geometry3DNodes.h:254, Mesh.h:469, MeshModalSolver.h:22, etc.). There is NO rigid-body node, no collision shapes, no Box2D-like solver (grep for rigid, box2d, jolt, collid in src/nodes and src/core found nothing physics related).
- Two simulation nodes only, src/nodes/SimulationNodes.h/.cpp (CMakeLists.txt:617; registered NodeRegistry.cpp:51-52):
  - ParticleSystemNode (SimulationNodes.h:26): emitter (point/sphere/box/disc, h:29), gravity, drag, hash-noise turbulence (cpp:46, comment admits not curl noise), life-based size/colour lerp (cpp:196). Fixed 120 Hz step with accumulator (cpp:17, 237-252), Transport-driven, reset on rewind (cpp:221). Outputs Particle point cloud (Mesh.h:265). Particles are fully independent: no particle-particle interaction, no collision, no force from other geometry, no emit-from-mesh. Emit() searches linearly for a dead slot per particle (cpp:141-150).
  - ClothNode (h:125): position-based dynamics, Verlet, Gauss-Seidel on edge distance constraints only (cpp:337-357, 465-493), welded vertices (cpp:304), pin modes, gravity/wind, ground plane with bounce/friction (cpp:508-525), "shapeRetention" pull (cpp:495). Stiffness is `k` per iteration, so it depends on iterations and timestep (not compliance based).
- 2D "fluid" exists only as Field Pixel presets: "Advected Smoke / Vortex" with an analytic swirl flow (FieldPixelNode.cpp:332-349), Reaction Diffusion node (NodeRegistry.cpp:123). No pressure solve, no divergence-free velocity (grep advect/divergence/vorticity hits only these presets).
- Docs mismatch: HelpWindows.cpp:824 says Particle System has "collision planes and spring-mass dynamics" and Cloth is a "mass-spring sheet with wind and collision"; code has no particle collision planes and cloth is PBD (SimulationNodes.h:120-124). Help text (HelpWindows.cpp:189 is accurate-ish) should be fixed.

## What the peers do differently
- Jolt soft body: compliance-based (XPBD style) constraints: edge `denom = length*(w0+w1+compliance/dt^2)` (SoftBodyMotionProperties.cpp:573-600), so stiffness is substep/iteration independent. Adds dihedral bend (367), tetra volume (458), pressure for inflatable bodies (291: impulse per face = coeff*cross, coeff = pressure*dt/volume), long-range-attachment tether constraints (679, stops far-from-pin stretch), skin/max-distance constraints (509), and collision vs. other bodies with velocity update (700).
- PositionBasedDynamics lib: isometric bending (186) and shape matching with polar decomposition (501) (rigid-ish/jelly soft body with no tetrahedralisation, only a point set).
- SPlisHSPlasH PBF: per-particle density constraint lambda = -C/(sum|grad C|^2+eps) over neighbours (TimeStepPBF.cpp ~338); needs a spatial hash neighbour search.
- WebGL-Fluid-Simulation: classic stable fluids as GL passes: curl -> vorticity confinement -> divergence -> Jacobi pressure iterations -> gradient subtract -> advect velocity and dye (script.js:1231-1300), 6 small shaders, ping-pong FBOs, splat injection.

## Concrete improvements
### Existing nodes
1. Cloth: switch to compliance (XPBD) constraints, substeps instead of iterations. Benefit: stiffness knob means the same at any BPM/framerate; cloth stops being rubbery with few iterations. Skill: new-geometry-node (plus param-truth-audit for new stiffness range). Effort S. Licence: Jolt MIT, near-port of formula needs MIT attribution; XPBD itself is an open paper.
2. Cloth: add dihedral/isometric bend constraint (bendStiffness) and optional pressure (inflate) and LRA tethers. Benefit: cloth folds like fabric not rubber sheet; balloons/pillows from a closed mesh; far less stretch from pins. Effort M. Licence: Jolt/PBD MIT; attribute if near-port.
3. Cloth: sphere/plane/box colliders (or collide with an upstream geometry via SDF/bounding primitives) and optional self-collision via spatial hash. Today only the ground plane exists (cpp:508). Effort M-L.
4. Particle System: true curl noise (divergence-free) instead of hash noise (cpp:46), because the comment already admits the gap. Effort S, no licence issue (standard math).
5. Particle System: collision plane/sphere with bounce, emit-from-mesh/point-cloud input, force-field input (attractor), fixed-array free-list for Emit (cpp:141). Effort S-M.
6. Fix stale help text HelpWindows.cpp:824. Effort S.
7. Cloth/soft body: shape matching mode from PBD lib (jelly body, no cloth topology needed; cheap) as a ClothNode mode or separate "Soft Body". Effort M.

### New nodes
- Fluid 2D (stable fluids): a Compositing/Source node running curl, vorticity, divergence, Jacobi pressure, advect as GL passes with ping-pong, inputs for velocity splat sources (image/mod). Matches new-source-node / new-compositing-node ping-pong rules. It gives real incompressible smoke/ink that the Field preset cannot do (no pressure step possible in a per-pixel kernel with single-pixel state; the preset only advects along a fixed flow, FieldPixelNode.cpp:343). Effort M-L. Licence MIT (WebGL-Fluid-Simulation), the algorithm is Stam 1999; write fresh, credit if shaders are adapted.
- Fluid particles (PBF/SPH) to point cloud: needs CPU spatial hash, 2-10k particles realistic; outputs same Particle cloud for Instance on Points. Effort L. Probably worth only after 2D fluid.
- Rigid body node (2D via Box2D-style, or 3D Jolt style): large dependency or large hand-written solver; vendoring Box2D (MIT, C) fits external/ pattern (codebase-navigation Vendoring). Effort L. Lower priority since no collision shape infra exists.

## Nothing to learn
Jolt/Box2D rigid solvers: the architecture (broadphase, islands, CCD) is out of scale for the current needs; only worth it if a rigid-body node is explicitly wanted.

## Candidates
Cloth XPBD compliance + substeps | stiffness independent of tempo/framerate, stable | S | MIT (Jolt) attribution if near-port
Cloth bend + pressure + LRA constraints | fabric folds, inflatable, less pin stretch | M | MIT (Jolt/PBD)
Particle curl noise + collision + mesh emit + attractor | richer particles | S-M | none/own code
Fluid 2D stable-fluids node | real smoke/ink, new capability | M-L | MIT (WebGL-Fluid-Simulation), algorithm Stam
Fix HelpWindows.cpp:824 stale physics description | docs accuracy | S | n/a
