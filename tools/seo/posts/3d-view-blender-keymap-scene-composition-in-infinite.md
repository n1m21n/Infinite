---
title: Blender keymap 3D scene composition in Infinite's 3D View
seo_title: Blender keymap 3D scene composition | Infinite
description: Use the Blender keymap 3D scene composition workflow in Infinite's 3D View with 6 light types, Principled BSDF materials, HDRI worlds, and Metal path tracing.
date: 2026-11-17
category: product
tags: [3d view, blender keymap, scene composition, pbr, path tracing]
keywords: blender keymap 3d scene composition, scene3dnode, 3d view, principled bsdf, metal path tracing
image: assets/blog_3d-view-blender-keymap-scene-composition-in-infinite.png
image_alt: Infinite 3D View workspace showing a 3D scene with ring meshes, particle sparks, lights, camera, the Scene outliner tab on the right, and a node graph below
status: published
---

Setting up a real-time 3D scene inside an audio-visual graph often means learning an unfamiliar viewport control scheme just to move a camera or place a rim light. In [Infinite](https://infinitedevices.org/), **3D View** (`Shift+3`, backed by `Scene3DNode`) brings a familiar **Blender keymap 3D scene composition** workflow straight into the modular node workspace, so artists who already know Blender shortcuts can lay out meshes, lights, cameras, and materials without switching applications.

Instead of isolating 3D layout behind an offline export step, `3D View` sits directly alongside the patch canvas and modulation curves. Any node parameter in the scene can stay wired to LFOs, audio analysers, or procedural generators in the [Infinite node library](https://infinitedevices.org/library.html) while you compose shots interactively in the viewport.

![Infinite 3D View workspace showing a 3D scene with ring meshes, particle sparks, lights, camera, the Scene outliner tab on the right, and a node graph below](https://infinitedevices.org/assets/blog_3d-view-blender-keymap-scene-composition-in-infinite.png)

## Viewport navigation and the Blender keymap

`3D View` adopts standard Blender transform and camera shortcuts so muscle memory carries over immediately:

- **Transform tools**: Press `G`, `R`, or `S` to grab, rotate, or scale selected objects. Press `X`, `Y`, or `Z` to lock transforms to a single axis, or `Shift+X`, `Shift+Y`, or `Shift+Z` to constrain movement to the corresponding plane.
- **Orthographic and camera views**: Use `1`, `3`, and `7` to snap to front, right, and top views, `5` to toggle orthographic projection, `0` to jump into `Camera View`, and `F` or `Home` to frame the selection or scene.
- **Fly navigation**: Hold `Right-drag` and steer with `WASDQE` to fly through larger environments.
- **Interaction modes**: Press `Tab` to toggle `Edit Mode`, `Ctrl/Cmd+Tab` to enter `Pose` or `Sculpt Mode`, and `W` for `Weight Paint`.

These shortcuts run directly in the `3D View` pane (`Shift+3`), so framing a camera or nudging a light target takes a couple of keystrokes rather than hunting through nested inspector fields.

## Eight side tabs in 3D View

The right-hand sidebar of `3D View` organises scene controls into eight vertical tabs:

1. **Scene**: Outliner view listing meshes, cameras, lights, and visibility toggles in the active `Scene3DNode` graph.
2. **Render**: Engine selection and post-processing controls including `Bloom`, `Motion Blur`, and colour management.
3. **Output**: Render resolution and export settings for still frames or video sequences.
4. **World**: Environment background and HDRI lighting setup.
5. **Object**: Transform values, hierarchy, and per-object viewport flags.
6. **Light / Camera**: Contextual parameters for whichever light or camera is currently selected.
7. **Material**: Principled BSDF surface properties and texture slot assignments.
8. **Viewport**: Shading mode, grid overlays, and navigation preferences.

Keeping these eight tabs anchored beside the viewport keeps layout, lighting, shading, and output choices in one place while the node graph below stays focused on signal routing.

## Six light types, Kelvin colour, and light linking

Lighting in `Scene3DNode` supports six light types: `Directional`, `Point`, `Sun`, `Ambient`, `Spot`, and `Area`. Rather than relying purely on arbitrary RGB sliders, lights expose correlated colour temperature in Kelvin across `800..20000 K`, making it straightforward to balance warm tungsten accents against cool sky fill.

Each object also supports `Light Linking` through the `Lit by` control. You can restrict which lights illuminate specific hero meshes or background elements without duplicating geometry or rendering separate passes, which is especially useful for live visuals and stage-style compositions documented on the [Infinite artists page](https://infinitedevices.org/artists.html).

## Principled BSDF materials and HDRI worlds

Surfaces in `3D View` use a **Principled BSDF** physically based material model with ten dedicated texture map slots:

- `Base colour`
- `Roughness`
- `Metallic`
- `Normal`
- `AO`
- `Emission`
- `Clearcoat`
- `Sheen`
- `Opacity`
- `Transmission`

Global reflections and image-based lighting come from the **World** tab via `EnvironmentNode`, which loads `.hdr` and `.exr` HDRI maps directly into the scene. Combining an HDRI sky with selective `Spot` or `Area` lights gives metallic, transmissive, and clearcoated surfaces grounded specular response without manual reflection probes.

## Camera depth of field and render engines

The **Camera** tab provides physical lens controls including sensor presets and **Depth of Field** parameters: `F-Stop` (`0.7..128`), aperture `Blades` (`3..16`), and `Anamorphic Ratio` for oval bokeh behaviour.

Under the **Render** tab, you can switch between `Raster` and `Path trace` engines depending on whether you are blocking motion or evaluating physically based bounces. On Apple Silicon Macs using Metal, `3D View` also provides `Live` and `Live + settle` modes so interactive edits stay responsive and then refine cleanly when the camera stops moving. Final output passes through `Bloom`, `Motion Blur`, and **Colour Management** transforms with `ACES`, `AgX`, `Reinhard`, and `Neutral` view options. You can compare hardware throughput across configurations on the [Infinite benchmarks page](https://infinitedevices.org/benchmarks.html).

## Honest limits

- **Composition over full modelling**: `3D View` composes, lights, shades, and renders scenes in Infinite rather than replacing full polygon modelling suites.
- **Platform render modes**: `Raster` and `Path trace` engines are available in `3D View`, while `Live` and `Live + settle` progressive modes apply on Metal.
- **Light linking and node scope**: Scene lighting, `EnvironmentNode` HDRIs, and Principled BSDF material slots are configured inside `Scene3DNode` and the eight `3D View` tabs for scene assembly and output rendering.
