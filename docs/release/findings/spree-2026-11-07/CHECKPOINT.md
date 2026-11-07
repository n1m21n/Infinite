# Spree 2026-11-07 — checkpoint

Scope: everything since the 2026-10-24 spree (v1.0.0, v1.1.0: 3D workspace, Sculpt, Bake, UV Project, Edit Mesh, Armature, path tracer, Piano/Wind/String, Piano Roll, Sample Studio, Terminal, Chat, ASIO).

| Finding | What | Status |
|---|---|---|
| B-L-001 | Geometry counts unbounded (Plane.detail 1e9 stall) | fixed |
| B-L-002 | Material map load crash, no editor context (v1.1.0 crash report) | fixed |
| B-L-003 | Grid countX/countY hang | fixed |
| B-L-004 | Curve Ops arcSteps unbounded | fixed |
| B-L-005 | Field Element/Primitive element counts unbounded | fixed |

| B-L-006 | RIGTEST could not find its .glb (`__FILE__` is build-dir relative under the dev generator) | fixed (test) |
| B-L-007 | TERMINALTEST hung silently: the panel opens on the Chat face, and a saved closed terminal re-closed it after startup | fixed (test) |

Sweeps:
- Frame fuzz, all 273 picture/3D types, 3631 rows (±1e9, -1, 0 per param): 2 HANG (B-L-003), 0 NaN/huge mesh/texture/GL error after fixes.
- Path tracer BVH: GPU walk caps (512 visits, 32 stack) hold on a 720k-tri sphere and a grazing 100 m floor (BvhTests 1b). Not a bug.
- Audio side, Piano model index: clean.
- code_lint: 8 new hits reviewed as harmless (sine multipliers, hashed seed, shader loops bounded upstream); baseline updated.

- New-feature selftests, 38 windowed (3D, bake, arm, rig, edit mesh, sculpt, chat, terminal, piano roll, string, sample, render 3D, 3D View): all pass after B-L-006/007. VIEW3DEDITTEST and VIEW3DPIVOTTEST failed once in the batch and pass alone (injected input raced real focus; flake, like PLUGINDRAGTEST). Most of these tests leave the window open, so a timeout with no FAIL line is normal.

Branch: bugfix/spree-2026-11-07, local, not pushed.
