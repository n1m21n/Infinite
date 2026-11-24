## What's new in v2.1.0

Infinite 2.1 adds fog, shaped lights, IES profiles, light and shadow linking, caustics and skin to Render 3D. The 2D View gets one Look stack for effects, styles and adjustments. Plugins use much less CPU while their editors are closed, live Audio In latency is lower, and the whole UI now runs on one design system: one shortcut table, one tooltip, one knob and one set of units.

### Render 3D and the 3D View
- Fog: a shaped puff with falloff, light shafts through shadow maps, sky light scattering into the fog, and lights inside the fog that scatter and fill the surfaces around them. Fog stops at the first surface, so objects in fog keep their contact shadows.
- Lights: Disc and Ellipse area lights, a Spread control, and IES profiles for point and spot lights.
- Light linking and shadow linking: a light or a shadow can be limited to a list of objects.
- Caustics through glass and water in the final (settle) render.
- Skin: subsurface scattering that enters through the surface the way Cycles' Random Walk does.
- Weathering recipes: Rust and Wet, beside Edge Wear, Dirt and Dust.
- City block preset, and a GPU memory readout on Render 3D.
- Group 3D moves, rotates and scales the whole group about a pivot.
- Curve takes hand-placed control points; Sweep takes a list of tapers.
- Instance on Points takes a point cloud; Material maps repeat by default, so Mapping tiles them.
- Shade nodes: Checker, Noise, Voronoi, Brick, Bump and Normal Map knobs get pins, and a wired pin overrides its knob.
- Add Mesh primitives in the 3D View, on a rail you can drag.
- The 3D View keeps your view habits in your preferences and no longer writes them into the scene.
- Fixed: a crash on an empty mesh on Metal, and fur bounds.

### 2D View
- One Look stack: Add covers effects, styles and adjustments, with one Presets menu and styles below effects.
- Look cards are just a header; every setting opens in a floating settings window that resizes both ways.
- Effect, style and adjustment cards share one header layout. Effects have named parameters, dropdowns for choices, angles in degrees, a More fold and a Mix line.
- The rail has four stops: Layers, Layer, Look and Document.
- Cards reorder live as you drag, without the layout jumping.
- Copy and cut take the layer you picked.
- A link that would feed a 2D Document back into itself is refused.
- Fixed: a crash when dropping a card.

### Audio and plugins
- A plugin whose editor is closed no longer keeps a core busy (main thread about 100% down to about 2.5%).
- Several plugin instances: closed editors idle, a smaller launch window, and fonts and the sample scanner load only when first needed.
- Audio In on macOS captures every audio cycle: 16 to 20 ms less latency.
- Live Audio In latency stays flat under clock drift, and recorders never skip.
- Faster Chorus and Phaser, and less work per frame for parameters, links and shaders.
- Sample Studio: a slice click no longer clicks audibly, a playhead, Space to play, Shift+D, paste at the playhead, Shift-drag to stretch.
- Picking a Field preset starts its knobs at that preset's own defaults.

### Interface
- One shortcut table: every panel reads the same keys, and Esc, Enter, the arrows and Tab behave the same everywhere.
- One tooltip style, one knob, one fold header and one set of on/off rows (box first, label on the right).
- Units on screen come from one formatter, and translated languages get their own unit words.
- Context menus share one order.
- The Minimap no longer disappears.
- Wide Shade nodes draw correctly, and a new patch starts with a 4-bar loop.
- The Field Patcher, when maximised, fills the canvas row below the top bar.
- Redrawn icons for check, trash, download, dock, Terminal, Chat and the update badge.

### Headless, command line and agents
- `--serve` keeps a warm headless session running.
- A crash in headless mode prints an E_CRASH line with the stage and a backtrace, then ends the process.
- Render lint checks 3D View scenes, hints at rotation units and reports GPU command failures (E_GPU).
- The agent check now says whether an edit worked, not only whether it was valid.

### Linux
- File dialogs start in a real folder and remember the last one (#78).
