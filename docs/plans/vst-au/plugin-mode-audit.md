# Plugin-mode UI audit

Owner rule: nothing confusing left inside a DAW; anything that does not really work there must go.

The table below lists every user-facing entry point in `src/app` and `src/arrange`: menus, Settings tabs and sections, buttons and tiles, rail items, shortcuts, drop handlers and the node spawn list. For each one it says whether a `Platform::PluginModeActive()` guard covers it, and its class. `tools/plugin/plugin_mode_audit.py` generates the table, and `tools/plugin/validate.sh` runs it as the "plugin-mode UI audit" row. Regenerate it with:

    python3 tools/plugin/plugin_mode_audit.py --md docs/plans/vst-au/plugin-mode-audit.md

Classes:

| Class | Meaning | Must be guarded |
|---|---|---|
| works | works inside a DAW, kept | no |
| host | the DAW owns it (transport, devices, click, recording, saving, updates) | yes |
| deferred | deferred or broken inside a DAW (plugin hosting, render, Terminal/Chat) | yes |
| unreachable | sits in a panel or window plugin mode never opens | no (the opener is guarded) |
| debug | `#ifndef NDEBUG` or review-hook only | no |

The tool counts an item as guarded when any of these covers it:

- an enclosing `if` or `else`
- the item's own statement
- an early `return` or `continue` earlier in the same block
- a local alias such as `const bool canRecord = !Platform::PluginModeActive();`
- a function it calls that returns early in plugin mode ("yes (callee)")
- every call site of the function it sits in ("yes (caller)")
- a Settings tab that plugin mode hides

The tool exits 1 when a host or deferred item is unguarded, or when an item matches no rule.

## Hidden in this pass (2026-11-08)

| Item | Where | Why |
|---|---|---|
| Metronome tile, its popup and its cell in the transport display; the click is forced off | top bar | The DAW plays its own click, and ours would be mixed into the track and its bounces |
| Audio Settings tab (only MIDI Inputs was left on it) | Settings | MIDI comes only from the DAW track (device `Host`) |
| Agents & Terminal Settings tab | Settings | No agent Terminal inside a DAW (features table) |
| Autosave section | Settings > General | Engine autosave never runs in plugin mode; the DAW project saves the patch |
| "Infinite x is available / Update" row | Settings > Extensions | The app updates; the plugin follows (8.11) |
| Terminal rail tile, View > Panels > Terminal, Ctrl+` | rail, menu, keys | No Terminal inside a DAW. On load the engine keeps it closed and writes the app's saved value back unchanged |
| "Reference in chat" | node context menu | No Chat inside a DAW |
| Output node "Record video" / "Stop recording" | node body | Video recording is Out (features table) |
| Audio Out and Spatial Mixer Record / Choose..., format and bit-depth buttons | node bodies | The DAW records the plugin's output |
| Timeline play, rewind and loop buttons | Arrange top bar | `LaunchRequest*` is a no-op in plugin mode, and `Transport::FollowHost` suspends the Arrange loop |
| Session play button | Session grid | Same no-op |
| Render Group | Timeline group menu | The DAW bounces (Render Track was already hidden) |

## Owner items, settled 2026-11-08

- **Seeks and loop.** Settings > General > DAW sync picks who owns tempo, playhead and loop (DAW by default). Playhead Infinite: a seek in Infinite sticks, and DAW jumps and loops move Infinite by the same amount. Loop Infinite: Infinite's loop region wraps and the Timeline loop toggle shows. Tempo Infinite makes all three Infinite's, and the tempo and meter fields become editable. Play and stop always follow the DAW.
- **Session Record tooltip.** In plugin mode it drops "ready to render with Render Take".
- **Settings.** The plugin keeps its own settings, layout, theme and recents in the app support folder's `Plugin` subfolder (`AppPaths::SettingsDir`). Licence, presets, devices, samples, grooves, extensions and learned taste stay shared.
- **Recording.** Node recorders (Sampler, Looper, Granular, Molder, Grain Molder) record their audio input: wire Audio In (Host main or sidechain) to record the track. Sample Studio's Record took the microphone; inside a DAW it now records the track (the host main bus, `PluginEngine::ReadHostTap`) and hides the device picker. Piano Roll records MIDI from the host.

## Table

| Status | Kind | Item | File:line | Guarded | Class | Why |
|---|---|---|---|---|---|---|
| ok | button | Save##fdd_ | src/app/AppShared.h:2852 | no | works | acts on the patch or the engine's own UI |
| ok | button | Export##fdd_ | src/app/AppShared.h:2868 | no | works | acts on the patch or the engine's own UI |
| ok | button | Import##fdd_ | src/app/AppShared.h:2875 | no | works | acts on the patch or the engine's own UI |
| ok | button | unload | src/app/bodies/AudioNodeBodies.cpp:83 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | map | src/app/bodies/AudioNodeBodies.cpp:114 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | x | src/app/bodies/AudioNodeBodies.cpp:164 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | live##audioOutLive | src/app/bodies/AudioNodeBodies.cpp:286 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Choose...##audioOutChoose | src/app/bodies/AudioNodeBodies.cpp:327 | yes | host | the DAW records the plugin's output |
| ok | button | 24 bit##spatialDepth | src/app/bodies/AudioNodeBodies.cpp:403 | yes | host | the DAW records the plugin's output |
| ok | button | live##spatialLive | src/app/bodies/AudioNodeBodies.cpp:418 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Choose...##spatialChoose | src/app/bodies/AudioNodeBodies.cpp:462 | yes | host | the DAW records the plugin's output |
| ok | button | Edit Field... | src/app/bodies/FieldParams.cpp:261 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Edit Field... | src/app/bodies/FieldParams.cpp:336 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Edit Field... | src/app/bodies/FieldParams.cpp:415 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Edit Field... | src/app/bodies/FieldParams.cpp:480 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Edit Field... | src/app/bodies/FieldParams.cpp:553 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Edit Field... | src/app/bodies/FieldParams.cpp:604 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Regenerate | src/app/bodies/FieldParams.cpp:614 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Unpack to Canvas | src/app/bodies/FieldParams.cpp:626 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Edit Field... | src/app/bodies/FieldParams.cpp:682 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Choose audio... | src/app/bodies/GeometryParams.cpp:68 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Pause | src/app/bodies/GeometryParams.cpp:85 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Play | src/app/bodies/GeometryParams.cpp:88 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Restart | src/app/bodies/GeometryParams.cpp:93 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Stop listening | src/app/bodies/GeometryParams.cpp:138 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Start listening | src/app/bodies/GeometryParams.cpp:141 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | step | src/app/bodies/GeometryParams.cpp:970 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | reset | src/app/bodies/GeometryParams.cpp:972 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | randomise | src/app/bodies/GeometryParams.cpp:974 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Clear | src/app/bodies/GeometryParams.cpp:1135 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Open in 3D View | src/app/bodies/GeometryParams.cpp:1192 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Clear | src/app/bodies/GeometryParams.cpp:1272 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Clear | src/app/bodies/GeometryParams.cpp:1286 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Reset | src/app/bodies/GeometryParams.cpp:1792 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | -##mixCh | src/app/bodies/NoteBodies.cpp:216 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | +##mixCh | src/app/bodies/NoteBodies.cpp:222 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Start MIDI | src/app/bodies/NoteBodies.cpp:490 | no | works | node bodies act on the node; inputs come from the host |
| ok | shortcut | `ImGuiKey_Minus` | src/app/bodies/NoteBodies.cpp:729 | no | works | node bodies act on the node; inputs come from the host |
| ok | shortcut | `ImGuiKey_Equal` | src/app/bodies/NoteBodies.cpp:729 | no | works | node bodies act on the node; inputs come from the host |
| ok | shortcut | `ImGuiKey_Equal` | src/app/bodies/NoteBodies.cpp:731 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | panic##midiOutPanic | src/app/bodies/NoteBodies.cpp:888 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Open##pianoOpen | src/app/bodies/NoteBodies.cpp:2163 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Load... | src/app/bodies/NoteBodies.cpp:2183 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Record | src/app/bodies/NoteBodies.cpp:2635 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Play | src/app/bodies/NoteBodies.cpp:2643 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Stop | src/app/bodies/NoteBodies.cpp:2651 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Clear | src/app/bodies/NoteBodies.cpp:2658 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Strike | src/app/bodies/OscBodies.cpp:1787 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Trig | src/app/bodies/OscBodies.cpp:2971 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Choose image... | src/app/bodies/ParamBodies1.cpp:58 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Choose folder... | src/app/bodies/ParamBodies1.cpp:84 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Refresh | src/app/bodies/ParamBodies1.cpp:89 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Refresh Servers | src/app/bodies/ParamBodies1.cpp:162 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Choose HDRI... | src/app/bodies/ParamBodies1.cpp:307 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Choose video... | src/app/bodies/ParamBodies1.cpp:499 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Reset Corners | src/app/bodies/ParamBodies1.cpp:800 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Reset All | src/app/bodies/ParamBodies1.cpp:806 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Flip H | src/app/bodies/ParamBodies1.cpp:812 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Flip V | src/app/bodies/ParamBodies1.cpp:818 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Stop rec | src/app/bodies/ParamBodies1.cpp:1480 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Rec path | src/app/bodies/ParamBodies1.cpp:1485 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Stop | src/app/bodies/ParamBodies1.cpp:1491 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Play path | src/app/bodies/ParamBodies1.cpp:1496 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | clear | src/app/bodies/ParamBodies1.cpp:1501 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Iterate | src/app/bodies/ParamBodies1.cpp:1513 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Reset | src/app/bodies/ParamBodies1.cpp:1516 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Randomise FX | src/app/bodies/ParamBodies1.cpp:1518 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | bang##macrotriggerbtn | src/app/bodies/ParamBodies1.cpp:1738 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Listening... move a control | src/app/bodies/ParamBodies1.cpp:1887 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Listening... hit a pad | src/app/bodies/ParamBodies1.cpp:1919 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Stop rec | src/app/bodies/ParamBodies1.cpp:2002 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Rec path | src/app/bodies/ParamBodies1.cpp:2005 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Stop | src/app/bodies/ParamBodies1.cpp:2012 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Play path | src/app/bodies/ParamBodies1.cpp:2015 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Clear | src/app/bodies/ParamBodies1.cpp:2020 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Reset channel | src/app/bodies/ParamBodies2.cpp:155 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Reset | src/app/bodies/ParamBodies2.cpp:188 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Remove Background | src/app/bodies/ParamBodies2.cpp:402 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Clear | src/app/bodies/ParamBodies2.cpp:440 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Reset | src/app/bodies/ParamBodies2.cpp:482 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Reseed | src/app/bodies/ParamBodies2.cpp:561 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Choose reference... | src/app/bodies/ParamBodies2.cpp:624 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Re-extract | src/app/bodies/ParamBodies2.cpp:657 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | + stop | src/app/bodies/ParamBodies2.cpp:864 | no | works | node bodies act on the node; inputs come from the host |
| ok | shortcut | `ImGuiKey_Enter` | src/app/bodies/PreviewBodies.cpp:210 | no | works | node bodies act on the node; inputs come from the host |
| ok | shortcut | `ImGuiKey_KeypadEnter` | src/app/bodies/PreviewBodies.cpp:210 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Clear canvas | src/app/bodies/PreviewBodies.cpp:543 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Stop rec | src/app/bodies/PreviewBodies.cpp:549 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Rec strokes | src/app/bodies/PreviewBodies.cpp:552 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Stop | src/app/bodies/PreviewBodies.cpp:559 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Replay | src/app/bodies/PreviewBodies.cpp:562 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | clear recording | src/app/bodies/PreviewBodies.cpp:581 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Load... | src/app/bodies/SamplerBodies.cpp:351 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | cancel##gen | src/app/bodies/SamplerBodies.cpp:519 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | generate##gen | src/app/bodies/SamplerBodies.cpp:522 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | export wav##gen | src/app/bodies/SamplerBodies.cpp:533 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Load... | src/app/bodies/SamplerBodies.cpp:677 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | re-slice | src/app/bodies/SamplerBodies.cpp:711 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Load... | src/app/bodies/SamplerBodies.cpp:824 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Load... | src/app/bodies/SamplerBodies.cpp:1087 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Roll | src/app/bodies/SamplerBodies.cpp:1108 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Iterate | src/app/bodies/SamplerBodies.cpp:1114 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Reset##molder | src/app/bodies/SamplerBodies.cpp:1120 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Load...##grainMolder | src/app/bodies/SamplerBodies.cpp:1323 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Load... | src/app/bodies/SamplerBodies.cpp:1471 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | `##clearlane` | src/app/bodies/SamplerBodies.cpp:1637 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | <##drumgrvprev | src/app/bodies/SamplerBodies.cpp:1994 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | >##drumgrvnext | src/app/bodies/SamplerBodies.cpp:1996 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Randomise | src/app/bodies/SamplerBodies.cpp:2211 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Clear | src/app/bodies/SamplerBodies.cpp:2217 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Load... | src/app/bodies/SamplerBodies.cpp:2824 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Folder... | src/app/bodies/SamplerBodies.cpp:2827 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Clear | src/app/bodies/SamplerBodies.cpp:2838 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Edit Sketch 3D... | src/app/bodies/Sketch3DParams.cpp:15 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Edit Sketch... | src/app/bodies/SketchParams.cpp:15 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | recenter##spatialRecenter | src/app/bodies/SpatialMixerBody.cpp:330 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Load...##vmpc | src/app/bodies/VmpcBody.cpp:396 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Folder...##vmpc | src/app/bodies/VmpcBody.cpp:399 | no | works | node bodies act on the node; inputs come from the host |
| ok | button | Clear##vmpc | src/app/bodies/VmpcBody.cpp:410 | no | works | node bodies act on the node; inputs come from the host |
| ok | menu | Host automation (slots full) | src/app/frame/HostSlots.cpp:339 | yes | works | plugin-only host automation |
| ok | menu | Host automation | src/app/frame/HostSlots.cpp:343 | yes | works | plugin-only host automation |
| ok | drop | `kFieldExt` | src/app/frame/StageDropHandling.cpp:175 | no | works | patch, media and sample drops load into the engine |
| ok | drop | `kAudioExt` | src/app/frame/StageDropHandling.cpp:307 | no | works | patch, media and sample drops load into the engine |
| ok | drop | `kMidiExt` | src/app/frame/StageDropHandling.cpp:393 | no | works | patch, media and sample drops load into the engine |
| ok | drop | `kPluginBundleExt` | src/app/frame/StageDropHandling.cpp:419 | yes | deferred | no plugin hosting inside a DAW |
| ok | drop | `kGltfExt` | src/app/frame/StageDropHandling.cpp:464 | no | works | patch, media and sample drops load into the engine |
| ok | drop | `kModelExt` | src/app/frame/StageDropHandling.cpp:514 | no | works | patch, media and sample drops load into the engine |
| ok | drop | `kVideoExt` | src/app/frame/StageDropHandling.cpp:528 | no | works | patch, media and sample drops load into the engine |
| ok | button | Reload | src/app/frame/StageFloating.cpp:103 | no | works | acts on the patch or the engine's own UI |
| ok | button | Keep mine | src/app/frame/StageFloating.cpp:109 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_Escape` | src/app/frame/StageFloating.cpp:261 | no | works | acts on the patch or the engine's own UI |
| ok | switch | Include the open patch | src/app/frame/StageFloating.cpp:281 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_Escape` | src/app/frame/StageFloating.cpp:291 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_Escape` | src/app/frame/StageFloating.cpp:364 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_Slash` | src/app/frame/StageKeyboard.cpp:35 | no | works | canvas editing keys |
| ok | shortcut | `ImGuiKey_Space` | src/app/frame/StageKeyboard.cpp:63 | yes (callee) | works | routes to LaunchRequest*, a no-op in plugin mode |
| ok | shortcut | `ImGuiKey_V` | src/app/frame/StageKeyboard.cpp:79 | no | works | canvas editing keys |
| ok | shortcut | `ImGuiKey_L` | src/app/frame/StageKeyboard.cpp:153 | no | works | canvas editing keys |
| ok | shortcut | `ImGuiKey_Period` | src/app/frame/StageKeyboard.cpp:155 | no | works | canvas editing keys |
| ok | shortcut | `ImGuiKey_PageDown` | src/app/frame/StageKeyboard.cpp:159 | no | works | canvas editing keys |
| ok | shortcut | `ImGuiKey_PageUp` | src/app/frame/StageKeyboard.cpp:161 | no | works | canvas editing keys |
| ok | shortcut | `ImGuiKey_M` | src/app/frame/StageKeyboard.cpp:166 | no | works | canvas editing keys |
| ok | shortcut | `ImGuiKey_P` | src/app/frame/StageKeyboard.cpp:170 | no | works | canvas editing keys |
| ok | shortcut | `ImGuiKey_T` | src/app/frame/StageKeyboard.cpp:174 | no | works | canvas editing keys |
| ok | shortcut | `ImGuiKey_S` | src/app/frame/StageKeyboard.cpp:178 | no | works | canvas editing keys |
| ok | shortcut | `ImGuiKey_U` | src/app/frame/StageKeyboard.cpp:182 | no | works | canvas editing keys |
| ok | shortcut | `ImGuiKey_W` | src/app/frame/StageKeyboard.cpp:186 | no | works | canvas editing keys |
| ok | shortcut | `ImGuiKey_GraveAccent` | src/app/frame/StageKeyboard.cpp:195 | yes (callee) | deferred | opens the Terminal |
| ok | shortcut | `ImGuiKey_R` | src/app/frame/StageKeyboard.cpp:199 | no | works | canvas editing keys |
| ok | shortcut | `ImGuiKey_3` | src/app/frame/StageKeyboard.cpp:203 | no | works | canvas editing keys |
| ok | shortcut | `ImGuiKey_Y` | src/app/frame/StageKeyboard.cpp:207 | no | works | canvas editing keys |
| ok | shortcut | `ImGuiKey_N` | src/app/frame/StageKeyboard.cpp:222 | no | works | canvas editing keys |
| ok | shortcut | `ImGuiKey_H` | src/app/frame/StageKeyboard.cpp:255 | no | works | canvas editing keys |
| ok | shortcut | `ImGuiKey_K` | src/app/frame/StageKeyboard.cpp:294 | no | works | canvas editing keys |
| ok | shortcut | `ImGuiKey_X` | src/app/frame/StageKeyboard.cpp:313 | no | works | canvas editing keys |
| ok | shortcut | `ImGuiKey_Delete` | src/app/frame/StageKeyboard.cpp:364 | no | works | canvas editing keys |
| ok | shortcut | `ImGuiKey_Backspace` | src/app/frame/StageKeyboard.cpp:365 | no | works | canvas editing keys |
| ok | shortcut | `ImGuiKey_X` | src/app/frame/StageKeyboard.cpp:366 | no | works | canvas editing keys |
| ok | shortcut | `ImGuiKey_D` | src/app/frame/StageKeyboard.cpp:437 | no | works | canvas editing keys |
| ok | shortcut | `ImGuiKey_G` | src/app/frame/StageKeyboard.cpp:548 | no | works | canvas editing keys |
| ok | shortcut | `ImGuiKey_G` | src/app/frame/StageKeyboard.cpp:551 | no | works | canvas editing keys |
| ok | shortcut | `ImGuiKey_B` | src/app/frame/StageKeyboard.cpp:724 | no | works | canvas editing keys |
| ok | shortcut | `ImGuiKey_C` | src/app/frame/StageKeyboard.cpp:756 | no | works | canvas editing keys |
| ok | shortcut | `ImGuiKey_V` | src/app/frame/StageKeyboard.cpp:807 | no | works | canvas editing keys |
| ok | shortcut | `ImGuiKey_Z` | src/app/frame/StageLinks.cpp:567 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_Z` | src/app/frame/StageLinks.cpp:569 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_Y` | src/app/frame/StageLinks.cpp:570 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_A` | src/app/frame/StageLinks.cpp:577 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_LeftArrow` | src/app/frame/StageLinks.cpp:622 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_RightArrow` | src/app/frame/StageLinks.cpp:623 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_UpArrow` | src/app/frame/StageLinks.cpp:624 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_DownArrow` | src/app/frame/StageLinks.cpp:625 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_Tab` | src/app/frame/StageLinks.cpp:641 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_Escape` | src/app/frame/StageLinks.cpp:658 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_Enter` | src/app/frame/StageLinks.cpp:764 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_KeypadEnter` | src/app/frame/StageLinks.cpp:764 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_H` | src/app/frame/StageLinks.cpp:780 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_H` | src/app/frame/StageLinks.cpp:786 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_F` | src/app/frame/StageLinks.cpp:791 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_U` | src/app/frame/StageLinks.cpp:795 | no | works | acts on the patch or the engine's own UI |
| ok | rail | `##railLibrary` | src/app/frame/StageMenuBar.cpp:28 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | rail | `##railViewport` | src/app/frame/StageMenuBar.cpp:31 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | rail | `##railModMatrix` | src/app/frame/StageMenuBar.cpp:34 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | rail | `##railPerf` | src/app/frame/StageMenuBar.cpp:37 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | rail | `##railArrange` | src/app/frame/StageMenuBar.cpp:40 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | rail | `##railSession` | src/app/frame/StageMenuBar.cpp:43 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | rail | `##railStudio` | src/app/frame/StageMenuBar.cpp:46 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | rail | `##railView3D` | src/app/frame/StageMenuBar.cpp:49 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | rail | `##railPianoRoll` | src/app/frame/StageMenuBar.cpp:52 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | rail | `##railTerminal` | src/app/frame/StageMenuBar.cpp:56 | yes | deferred | no agent Terminal inside a DAW (8.10) |
| ok | rail | `##railSettings` | src/app/frame/StageMenuBar.cpp:63 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | rail | `##railHistory` | src/app/frame/StageMenuBar.cpp:66 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | New | src/app/frame/StageMenuBar.cpp:186 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | New Synth | src/app/frame/StageMenuBar.cpp:188 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | New Effect Chain | src/app/frame/StageMenuBar.cpp:190 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | New 3D Scene | src/app/frame/StageMenuBar.cpp:192 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | Template library... | src/app/frame/StageMenuBar.cpp:194 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | Open... | src/app/frame/StageMenuBar.cpp:196 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | Open Recent | src/app/frame/StageMenuBar.cpp:203 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | Save | src/app/frame/StageMenuBar.cpp:220 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | Save As... | src/app/frame/StageMenuBar.cpp:222 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | Save with media... | src/app/frame/StageMenuBar.cpp:224 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | Share Patch... | src/app/frame/StageMenuBar.cpp:226 | no | works | uploads the patch text; no app state involved |
| ok | menu | History | src/app/frame/StageMenuBar.cpp:260 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | Cut / Copy | src/app/frame/StageMenuBar.cpp:263 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | Paste | src/app/frame/StageMenuBar.cpp:265 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | Duplicate | src/app/frame/StageMenuBar.cpp:267 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | Delete | src/app/frame/StageMenuBar.cpp:269 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | Select All | src/app/frame/StageMenuBar.cpp:272 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | Bypass selection | src/app/frame/StageMenuBar.cpp:274 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | Group selection | src/app/frame/StageMenuBar.cpp:277 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | Ungroup | src/app/frame/StageMenuBar.cpp:279 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | Add Node... | src/app/frame/StageMenuBar.cpp:282 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | Add Note | src/app/frame/StageMenuBar.cpp:284 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | Settings... | src/app/frame/StageMenuBar.cpp:294 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | Panels | src/app/frame/StageMenuBar.cpp:299 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | Viewport panel | src/app/frame/StageMenuBar.cpp:301 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | Show viewport panel | src/app/frame/StageMenuBar.cpp:303 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | Clear cards | src/app/frame/StageMenuBar.cpp:320 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | Close viewport panel | src/app/frame/StageMenuBar.cpp:322 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | Modulation matrix | src/app/frame/StageMenuBar.cpp:328 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | Show modulation matrix | src/app/frame/StageMenuBar.cpp:336 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | Performance Matrix | src/app/frame/StageMenuBar.cpp:356 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | Show Performance Matrix | src/app/frame/StageMenuBar.cpp:360 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | Edit Mode | src/app/frame/StageMenuBar.cpp:376 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | Arrangement Timeline | src/app/frame/StageMenuBar.cpp:381 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | Show Arrangement Timeline | src/app/frame/StageMenuBar.cpp:383 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | Session | src/app/frame/StageMenuBar.cpp:418 | no | works | which driver feeds the plugin's output; works under host transport |
| ok | menu | Show Session | src/app/frame/StageMenuBar.cpp:420 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | Terminal | src/app/frame/StageMenuBar.cpp:456 | yes | deferred | no agent Terminal inside a DAW (8.10) |
| ok | menu | Show Terminal | src/app/frame/StageMenuBar.cpp:458 | yes | deferred | no agent Terminal inside a DAW (8.10) |
| ok | menu | Show Set | src/app/frame/StageMenuBar.cpp:481 | no | works | show tools act on the patch and the engine's own output |
| ok | menu | Show Set window | src/app/frame/StageMenuBar.cpp:483 | no | works | show tools act on the patch and the engine's own output |
| ok | menu | Next patch | src/app/frame/StageMenuBar.cpp:484 | no | works | show tools act on the patch and the engine's own output |
| ok | menu | Previous patch | src/app/frame/StageMenuBar.cpp:486 | no | works | show tools act on the patch and the engine's own output |
| ok | menu | Show lock | src/app/frame/StageMenuBar.cpp:489 | no | works | show tools act on the patch and the engine's own output |
| ok | menu | Resume after panic | src/app/frame/StageMenuBar.cpp:493 | no | works | show tools act on the patch and the engine's own output |
| ok | menu | Panic | src/app/frame/StageMenuBar.cpp:496 | no | works | show tools act on the patch and the engine's own output |
| ok | menu | Pre-show check | src/app/frame/StageMenuBar.cpp:498 | no | works | show tools act on the patch and the engine's own output |
| ok | menu | Nodes | src/app/frame/StageMenuBar.cpp:508 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | Show all params | src/app/frame/StageMenuBar.cpp:510 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | Hide all params | src/app/frame/StageMenuBar.cpp:515 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | All shortcuts... | src/app/frame/StageMenuBar.cpp:524 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | Help / module reference | src/app/frame/StageMenuBar.cpp:526 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | UI Debugger / Item Picker | src/app/frame/StageMenuBar.cpp:536 | debug | debug | #ifndef NDEBUG |
| ok | menu | UI Style Editor | src/app/frame/StageMenuBar.cpp:538 | debug | debug | #ifndef NDEBUG |
| ok | menu | Collect diagnostics... | src/app/frame/StageMenuBar.cpp:541 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | About Infinite | src/app/frame/StageMenuBar.cpp:543 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | menu | Check for updates | src/app/frame/StageMenuBar.cpp:548 | yes | host | the app updates; the DAW closes the plugin |
| ok | menu | Quit | src/app/frame/StageMenuBar.cpp:555 | yes | host | the app updates; the DAW closes the plugin |
| ok | button | `##transportplay` | src/app/frame/StageMenuBar.cpp:626 | yes (caller) | host | the DAW's transport plays and stops the plugin |
| ok | button | `##transportrewind` | src/app/frame/StageMenuBar.cpp:647 | yes (caller) | host | the DAW's transport plays and stops the plugin |
| ok | button | Timeline | src/app/frame/StageMenuBar.cpp:702 | no | works | which driver feeds the plugin's output; works under host transport |
| ok | button | Session##sessionbadge | src/app/frame/StageMenuBar.cpp:712 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | button | Studio##studiobadge | src/app/frame/StageMenuBar.cpp:722 | no | works | File/Edit/View items act on the patch, which the DAW saves in its project |
| ok | button | `##metronomeBtn` | src/app/frame/StageMenuBar.cpp:1035 | yes | host | the DAW's click; ours would mix into the track and its bounces |
| ok | menu | accent first beat | src/app/frame/StageMenuBar.cpp:1058 | yes | host | the DAW's click; ours would mix into the track and its bounces |
| ok | button | `##popout` | src/app/frame/StageMenuBar.cpp:1181 | yes | works | plugin-only tiles or update badge (guarded) |
| ok | button | `##picture` | src/app/frame/StageMenuBar.cpp:1193 | yes | works | plugin-only tiles or update badge (guarded) |
| ok | button | `##slots` | src/app/frame/StageMenuBar.cpp:1204 | yes | works | plugin-only tiles or update badge (guarded) |
| ok | button | .png##imgPng | src/app/frame/StageNodeBodies.cpp:1160 | no | works | node bodies act on the node |
| ok | button | .jpg##imgJpg | src/app/frame/StageNodeBodies.cpp:1177 | no | works | node bodies act on the node |
| ok | button | Export Image | src/app/frame/StageNodeBodies.cpp:1190 | no | works | node bodies act on the node |
| ok | button | .mp4##vidMp4 | src/app/frame/StageNodeBodies.cpp:1214 | no | works | node bodies act on the node |
| ok | button | .mov##vidMov | src/app/frame/StageNodeBodies.cpp:1231 | no | works | node bodies act on the node |
| ok | button | Finalizing... | src/app/frame/StageNodeBodies.cpp:1295 | yes | deferred | live video recording is Out inside a DAW (README features table) |
| ok | button | Stop recording | src/app/frame/StageNodeBodies.cpp:1306 | yes | deferred | live video recording is Out inside a DAW (README features table) |
| ok | button | Record video | src/app/frame/StageNodeBodies.cpp:1326 | yes | deferred | live video recording is Out inside a DAW (README features table) |
| ok | button | Rendering... | src/app/frame/StageNodeBodies.cpp:1402 | yes | works | node bodies act on the node |
| ok | button | Render | src/app/frame/StageNodeBodies.cpp:1408 | yes | works | node bodies act on the node |
| ok | menu | Add to 3D View | src/app/frame/StagePopupsA.cpp:82 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Rename | src/app/frame/StagePopupsA.cpp:98 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Ungroup | src/app/frame/StagePopupsA.cpp:104 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Duplicate | src/app/frame/StagePopupsA.cpp:113 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Delete Group | src/app/frame/StagePopupsA.cpp:126 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Edit Note | src/app/frame/StagePopupsA.cpp:139 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Font Size | src/app/frame/StagePopupsA.cpp:145 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Change Colour... | src/app/frame/StagePopupsA.cpp:160 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Duplicate | src/app/frame/StagePopupsA.cpp:169 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Ungroup | src/app/frame/StagePopupsA.cpp:180 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Delete Note | src/app/frame/StagePopupsA.cpp:200 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Duplicate | src/app/frame/StagePopupsA.cpp:226 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Presets | src/app/frame/StagePopupsA.cpp:237 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Open in Piano Roll | src/app/frame/StagePopupsA.cpp:252 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Hide params | src/app/frame/StagePopupsA.cpp:258 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Show params | src/app/frame/StagePopupsA.cpp:263 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Hide viewport | src/app/frame/StagePopupsA.cpp:271 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Show viewport | src/app/frame/StagePopupsA.cpp:276 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Open in viewport panel | src/app/frame/StagePopupsA.cpp:282 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Show modulation matrix | src/app/frame/StagePopupsA.cpp:292 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Add to Timeline | src/app/frame/StagePopupsA.cpp:299 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Video | src/app/frame/StagePopupsA.cpp:301 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Audio | src/app/frame/StagePopupsA.cpp:303 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Add to Session | src/app/frame/StagePopupsA.cpp:307 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Video | src/app/frame/StagePopupsA.cpp:309 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Audio | src/app/frame/StagePopupsA.cpp:311 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Add to Timeline | src/app/frame/StagePopupsA.cpp:318 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Add to Session | src/app/frame/StagePopupsA.cpp:320 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Output window | src/app/frame/StagePopupsA.cpp:326 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Fullscreen | src/app/frame/StagePopupsA.cpp:328 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Display | src/app/frame/StagePopupsA.cpp:330 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Close window | src/app/frame/StagePopupsA.cpp:345 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Open in new window | src/app/frame/StagePopupsA.cpp:352 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Reference in chat | src/app/frame/StagePopupsA.cpp:360 | yes | deferred | no Chat inside a DAW (8.10) |
| ok | menu | Help | src/app/frame/StagePopupsA.cpp:362 | yes | works | acts on the patch or the engine's own UI |
| ok | menu | Ungroup | src/app/frame/StagePopupsA.cpp:370 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Delete Node | src/app/frame/StagePopupsA.cpp:393 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_Minus` | src/app/frame/StagePopupsA.cpp:580 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Add to Performance Matrix | src/app/frame/StagePopupsA.cpp:609 | no | works | acts on the patch or the engine's own UI |
| ok | menu | View in Modulation Matrix | src/app/frame/StagePopupsA.cpp:613 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Full range | src/app/frame/StagePopupsA.cpp:659 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Around current | src/app/frame/StagePopupsA.cpp:661 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Invert | src/app/frame/StagePopupsA.cpp:669 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Unbind | src/app/frame/StagePopupsA.cpp:673 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Edit Expression | src/app/frame/StagePopupsA.cpp:687 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Add to Performance Matrix | src/app/frame/StagePopupsA.cpp:690 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Full range | src/app/frame/StagePopupsA.cpp:711 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Invert | src/app/frame/StagePopupsA.cpp:713 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Unbind | src/app/frame/StagePopupsA.cpp:717 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Cancel Recording | src/app/frame/StagePopupsA.cpp:731 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Add to Performance Matrix | src/app/frame/StagePopupsA.cpp:750 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Full range | src/app/frame/StagePopupsA.cpp:767 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Invert | src/app/frame/StagePopupsA.cpp:769 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Record Again | src/app/frame/StagePopupsA.cpp:772 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Unbind | src/app/frame/StagePopupsA.cpp:776 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Enter Value | src/app/frame/StagePopupsA.cpp:797 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Add to Performance Matrix | src/app/frame/StagePopupsA.cpp:800 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Enter Expression | src/app/frame/StagePopupsA.cpp:805 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Start Recording | src/app/frame/StagePopupsA.cpp:807 | no | works | acts on the patch or the engine's own UI |
| ok | drop | `kVideoExt` | src/app/frame/StagePopupsA.cpp:1136 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_Escape` | src/app/frame/StagePopupsB.cpp:187 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_Enter` | src/app/frame/StagePopupsB.cpp:284 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_Escape` | src/app/frame/StagePopupsB.cpp:468 | no | works | acts on the patch or the engine's own UI |
| ok | button | Save##fielddevicesaveconfirm | src/app/frame/StagePopupsB.cpp:532 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_Escape` | src/app/frame/StagePopupsB.cpp:953 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_Escape` | src/app/frame/StagePopupsB.cpp:1032 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_0` | src/app/frame/StagePresent.cpp:31 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_Equal` | src/app/frame/StagePresent.cpp:33 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_Minus` | src/app/frame/StagePresent.cpp:33 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_Equal` | src/app/frame/StagePresent.cpp:36 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_S` | src/app/frame/StagePresent.cpp:44 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_O` | src/app/frame/StagePresent.cpp:46 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_N` | src/app/frame/StagePresent.cpp:52 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_Escape` | src/app/frame/StageSidePanels.cpp:222 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Add to canvas | src/app/frame/StageSidePanels.cpp:567 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Add to canvas | src/app/frame/StageSidePanels.cpp:629 | no | works | acts on the patch or the engine's own UI |
| ok | button | Apply | src/app/frame/StageSidePanels.cpp:819 | no | works | acts on the patch or the engine's own UI |
| ok | button | Revert | src/app/frame/StageSidePanels.cpp:828 | no | works | acts on the patch or the engine's own UI |
| ok | button | Apply | src/app/frame/StageSidePanels.cpp:853 | no | works | acts on the patch or the engine's own UI |
| ok | button | Revert | src/app/frame/StageSidePanels.cpp:860 | no | works | acts on the patch or the engine's own UI |
| ok | button | Apply | src/app/frame/StageSidePanels.cpp:1073 | no | works | acts on the patch or the engine's own UI |
| ok | button | Revert | src/app/frame/StageSidePanels.cpp:1081 | no | works | acts on the patch or the engine's own UI |
| ok | button | Apply | src/app/frame/StageSidePanels.cpp:1137 | no | works | acts on the patch or the engine's own UI |
| ok | button | Revert | src/app/frame/StageSidePanels.cpp:1145 | no | works | acts on the patch or the engine's own UI |
| ok | button | Apply | src/app/frame/StageSidePanels.cpp:1186 | no | works | acts on the patch or the engine's own UI |
| ok | button | Revert | src/app/frame/StageSidePanels.cpp:1194 | no | works | acts on the patch or the engine's own UI |
| ok | button | Paste SVG | src/app/frame/StageSidePanels.cpp:1233 | no | works | acts on the patch or the engine's own UI |
| ok | button | Clear SVG | src/app/frame/StageSidePanels.cpp:1246 | no | works | acts on the patch or the engine's own UI |
| ok | button | Apply | src/app/frame/StageSidePanels.cpp:1274 | no | works | acts on the patch or the engine's own UI |
| ok | button | Revert | src/app/frame/StageSidePanels.cpp:1282 | no | works | acts on the patch or the engine's own UI |
| ok | button | Apply | src/app/frame/StageSidePanels.cpp:1331 | no | works | acts on the patch or the engine's own UI |
| ok | button | Revert | src/app/frame/StageSidePanels.cpp:1339 | no | works | acts on the patch or the engine's own UI |
| ok | button | Apply | src/app/frame/StageSidePanels.cpp:1409 | no | works | acts on the patch or the engine's own UI |
| ok | button | Revert | src/app/frame/StageSidePanels.cpp:1417 | no | works | acts on the patch or the engine's own UI |
| ok | button | Apply | src/app/frame/StageSidePanels.cpp:1473 | no | works | acts on the patch or the engine's own UI |
| ok | button | Revert | src/app/frame/StageSidePanels.cpp:1481 | no | works | acts on the patch or the engine's own UI |
| ok | button | Apply | src/app/frame/StageSidePanels.cpp:1537 | no | works | acts on the patch or the engine's own UI |
| ok | button | Revert | src/app/frame/StageSidePanels.cpp:1545 | no | works | acts on the patch or the engine's own UI |
| ok | button | Apply | src/app/frame/StageSidePanels.cpp:1601 | no | works | acts on the patch or the engine's own UI |
| ok | button | Revert | src/app/frame/StageSidePanels.cpp:1612 | no | works | acts on the patch or the engine's own UI |
| ok | button | Regenerate | src/app/frame/StageSidePanels.cpp:1615 | no | works | acts on the patch or the engine's own UI |
| ok | node | Stub Predictor | src/app/graph/NodeRegistry.cpp:15 | no | works | acts on the patch or the engine's own UI |
| ok | node | Image Source | src/app/graph/NodeRegistry.cpp:17 | no | works | acts on the patch or the engine's own UI |
| ok | node | Slideshow | src/app/graph/NodeRegistry.cpp:18 | no | works | acts on the patch or the engine's own UI |
| ok | node | Shape | src/app/graph/NodeRegistry.cpp:19 | no | works | acts on the patch or the engine's own UI |
| ok | node | Formula | src/app/graph/NodeRegistry.cpp:26 | no | works | acts on the patch or the engine's own UI |
| ok | node | Shadertoy | src/app/graph/NodeRegistry.cpp:27 | no | works | acts on the patch or the engine's own UI |
| ok | node | FieldPixel | src/app/graph/NodeRegistry.cpp:28 | no | works | acts on the patch or the engine's own UI |
| ok | node | Sketch | src/app/graph/NodeRegistry.cpp:29 | no | works | acts on the patch or the engine's own UI |
| ok | node | Text | src/app/graph/NodeRegistry.cpp:30 | no | works | acts on the patch or the engine's own UI |
| ok | node | Video | src/app/graph/NodeRegistry.cpp:31 | no | works | acts on the patch or the engine's own UI |
| ok | node | VMPC | src/app/graph/NodeRegistry.cpp:32 | no | works | acts on the patch or the engine's own UI |
| ok | node | Video In | src/app/graph/NodeRegistry.cpp:33 | no | works | acts on the patch or the engine's own UI |
| ok | node | Syphon In | src/app/graph/NodeRegistry.cpp:37 | no | works | acts on the patch or the engine's own UI |
| ok | node | NDI In | src/app/graph/NodeRegistry.cpp:38 | no | works | acts on the patch or the engine's own UI |
| ok | node | Noise | src/app/graph/NodeRegistry.cpp:39 | no | works | acts on the patch or the engine's own UI |
| ok | node | Texture | src/app/graph/NodeRegistry.cpp:40 | no | works | acts on the patch or the engine's own UI |
| ok | node | Ramp | src/app/graph/NodeRegistry.cpp:41 | no | works | acts on the patch or the engine's own UI |
| ok | node | Geometry | src/app/graph/NodeRegistry.cpp:42 | no | works | acts on the patch or the engine's own UI |
| ok | node | Model 3D | src/app/graph/NodeRegistry.cpp:50 | no | works | acts on the patch or the engine's own UI |
| ok | node | Text 3D | src/app/graph/NodeRegistry.cpp:51 | no | works | acts on the patch or the engine's own UI |
| ok | node | Null 3D | src/app/graph/NodeRegistry.cpp:52 | no | works | acts on the patch or the engine's own UI |
| ok | node | Ocean | src/app/graph/NodeRegistry.cpp:53 | no | works | acts on the patch or the engine's own UI |
| ok | node | Material | src/app/graph/NodeRegistry.cpp:54 | no | works | acts on the patch or the engine's own UI |
| ok | node | Displacement | src/app/graph/NodeRegistry.cpp:55 | no | works | acts on the patch or the engine's own UI |
| ok | node | Feedback 3D | src/app/graph/NodeRegistry.cpp:56 | no | works | acts on the patch or the engine's own UI |
| ok | node | Audio Displacement | src/app/graph/NodeRegistry.cpp:57 | no | works | acts on the patch or the engine's own UI |
| ok | node | Audio Ribbon | src/app/graph/NodeRegistry.cpp:58 | no | works | acts on the patch or the engine's own UI |
| ok | node | Mapping | src/app/graph/NodeRegistry.cpp:59 | no | works | acts on the patch or the engine's own UI |
| ok | node | Particle System | src/app/graph/NodeRegistry.cpp:60 | no | works | acts on the patch or the engine's own UI |
| ok | node | Cloth | src/app/graph/NodeRegistry.cpp:61 | no | works | acts on the patch or the engine's own UI |
| ok | node | Rigid Bodies | src/app/graph/NodeRegistry.cpp:62 | no | works | acts on the patch or the engine's own UI |
| ok | node | L-System | src/app/graph/NodeRegistry.cpp:63 | no | works | acts on the patch or the engine's own UI |
| ok | node | Join Geometry | src/app/graph/NodeRegistry.cpp:64 | no | works | acts on the patch or the engine's own UI |
| ok | node | Metaballs | src/app/graph/NodeRegistry.cpp:73 | no | works | acts on the patch or the engine's own UI |
| ok | node | Resynthesize 3D | src/app/graph/NodeRegistry.cpp:74 | no | works | acts on the patch or the engine's own UI |
| ok | node | Image to Points | src/app/graph/NodeRegistry.cpp:75 | no | works | acts on the patch or the engine's own UI |
| ok | node | Depth Projection | src/app/graph/NodeRegistry.cpp:76 | no | works | acts on the patch or the engine's own UI |
| ok | node | Curve | src/app/graph/NodeRegistry.cpp:77 | no | works | acts on the patch or the engine's own UI |
| ok | node | Instance on Points | src/app/graph/NodeRegistry.cpp:101 | no | works | acts on the patch or the engine's own UI |
| ok | node | Set Vertex Color | src/app/graph/NodeRegistry.cpp:102 | no | works | acts on the patch or the engine's own UI |
| ok | node | Wrap | src/app/graph/NodeRegistry.cpp:103 | no | works | acts on the patch or the engine's own UI |
| ok | node | Field Modifier | src/app/graph/NodeRegistry.cpp:104 | no | works | acts on the patch or the engine's own UI |
| ok | node | Field Primitive | src/app/graph/NodeRegistry.cpp:105 | no | works | acts on the patch or the engine's own UI |
| ok | node | Sketch 3D | src/app/graph/NodeRegistry.cpp:106 | no | works | acts on the patch or the engine's own UI |
| ok | node | Distribute Points on Faces | src/app/graph/NodeRegistry.cpp:107 | no | works | acts on the patch or the engine's own UI |
| ok | node | Points to Vertices | src/app/graph/NodeRegistry.cpp:108 | no | works | acts on the patch or the engine's own UI |
| ok | node | Proximity Lines | src/app/graph/NodeRegistry.cpp:109 | no | works | acts on the patch or the engine's own UI |
| ok | node | Point Trails | src/app/graph/NodeRegistry.cpp:110 | no | works | acts on the patch or the engine's own UI |
| ok | node | Curve Ops | src/app/graph/NodeRegistry.cpp:111 | no | works | acts on the patch or the engine's own UI |
| ok | node | Sweep | src/app/graph/NodeRegistry.cpp:112 | no | works | acts on the patch or the engine's own UI |
| ok | node | Armature | src/app/graph/NodeRegistry.cpp:113 | no | works | acts on the patch or the engine's own UI |
| ok | node | Revolve | src/app/graph/NodeRegistry.cpp:114 | no | works | acts on the patch or the engine's own UI |
| ok | node | Delaunay Mesh | src/app/graph/NodeRegistry.cpp:115 | no | works | acts on the patch or the engine's own UI |
| ok | node | Voronoi Cells | src/app/graph/NodeRegistry.cpp:116 | no | works | acts on the patch or the engine's own UI |
| ok | node | Distribute Points in Grid | src/app/graph/NodeRegistry.cpp:117 | no | works | acts on the patch or the engine's own UI |
| ok | node | Merge by Distance | src/app/graph/NodeRegistry.cpp:118 | no | works | acts on the patch or the engine's own UI |
| ok | node | Bake | src/app/graph/NodeRegistry.cpp:119 | no | works | acts on the patch or the engine's own UI |
| ok | node | Sculpt | src/app/graph/NodeRegistry.cpp:120 | no | works | acts on the patch or the engine's own UI |
| ok | node | Edit Mesh | src/app/graph/NodeRegistry.cpp:121 | no | works | acts on the patch or the engine's own UI |
| ok | node | Switcher 3D | src/app/graph/NodeRegistry.cpp:122 | no | works | acts on the patch or the engine's own UI |
| ok | node | Group 3D | src/app/graph/NodeRegistry.cpp:123 | no | works | acts on the patch or the engine's own UI |
| ok | node | 3D View | src/app/graph/NodeRegistry.cpp:124 | no | works | acts on the patch or the engine's own UI |
| ok | node | Camera | src/app/graph/NodeRegistry.cpp:125 | no | works | acts on the patch or the engine's own UI |
| ok | node | Light | src/app/graph/NodeRegistry.cpp:126 | no | works | acts on the patch or the engine's own UI |
| ok | node | HDRI | src/app/graph/NodeRegistry.cpp:127 | no | works | acts on the patch or the engine's own UI |
| ok | node | Render 3D | src/app/graph/NodeRegistry.cpp:128 | no | works | acts on the patch or the engine's own UI |
| ok | node | Draw | src/app/graph/NodeRegistry.cpp:129 | no | works | acts on the patch or the engine's own UI |
| ok | node | Resynthesize | src/app/graph/NodeRegistry.cpp:133 | no | works | acts on the patch or the engine's own UI |
| ok | node | Fit | src/app/graph/NodeRegistry.cpp:134 | no | works | acts on the patch or the engine's own UI |
| ok | node | Comment | src/app/graph/NodeRegistry.cpp:135 | no | works | acts on the patch or the engine's own UI |
| ok | node | Group | src/app/graph/NodeRegistry.cpp:136 | no | works | acts on the patch or the engine's own UI |
| ok | node | Null | src/app/graph/NodeRegistry.cpp:137 | no | works | acts on the patch or the engine's own UI |
| ok | node | Viewport | src/app/graph/NodeRegistry.cpp:138 | no | works | acts on the patch or the engine's own UI |
| ok | node | Curves | src/app/graph/NodeRegistry.cpp:142 | no | works | acts on the patch or the engine's own UI |
| ok | node | Color Ramp | src/app/graph/NodeRegistry.cpp:143 | no | works | acts on the patch or the engine's own UI |
| ok | node | Remove Background | src/app/graph/NodeRegistry.cpp:144 | no | works | acts on the patch or the engine's own UI |
| ok | node | Feedback | src/app/graph/NodeRegistry.cpp:145 | no | works | acts on the patch or the engine's own UI |
| ok | node | Trails | src/app/graph/NodeRegistry.cpp:146 | no | works | acts on the patch or the engine's own UI |
| ok | node | Optical Flow | src/app/graph/NodeRegistry.cpp:147 | no | works | acts on the patch or the engine's own UI |
| ok | node | Time Machine | src/app/graph/NodeRegistry.cpp:148 | no | works | acts on the patch or the engine's own UI |
| ok | node | Fluid | src/app/graph/NodeRegistry.cpp:149 | no | works | acts on the patch or the engine's own UI |
| ok | node | Volume | src/app/graph/NodeRegistry.cpp:150 | no | works | acts on the patch or the engine's own UI |
| ok | node | Reaction Diffusion | src/app/graph/NodeRegistry.cpp:151 | no | works | acts on the patch or the engine's own UI |
| ok | node | Blend | src/app/graph/NodeRegistry.cpp:152 | no | works | acts on the patch or the engine's own UI |
| ok | node | Layer Stack | src/app/graph/NodeRegistry.cpp:153 | no | works | acts on the patch or the engine's own UI |
| ok | node | Switcher | src/app/graph/NodeRegistry.cpp:154 | no | works | acts on the patch or the engine's own UI |
| ok | node | Output | src/app/graph/NodeRegistry.cpp:159 | no | works | acts on the patch or the engine's own UI |
| ok | node | Syphon Out | src/app/graph/NodeRegistry.cpp:160 | no | works | acts on the patch or the engine's own UI |
| ok | node | NDI Out | src/app/graph/NodeRegistry.cpp:161 | no | works | acts on the patch or the engine's own UI |
| ok | node | Projection | src/app/graph/NodeRegistry.cpp:162 | no | works | acts on the patch or the engine's own UI |
| ok | node | LFO | src/app/graph/NodeRegistry.cpp:163 | no | works | acts on the patch or the engine's own UI |
| ok | node | Random | src/app/graph/NodeRegistry.cpp:164 | no | works | acts on the patch or the engine's own UI |
| ok | node | Pattern | src/app/graph/NodeRegistry.cpp:165 | no | works | acts on the patch or the engine's own UI |
| ok | node | Math | src/app/graph/NodeRegistry.cpp:166 | no | works | acts on the patch or the engine's own UI |
| ok | node | Compare | src/app/graph/NodeRegistry.cpp:167 | no | works | acts on the patch or the engine's own UI |
| ok | node | Range to Range | src/app/graph/NodeRegistry.cpp:168 | no | works | acts on the patch or the engine's own UI |
| ok | node | Smoothing | src/app/graph/NodeRegistry.cpp:169 | no | works | acts on the patch or the engine's own UI |
| ok | node | Invert | src/app/graph/NodeRegistry.cpp:170 | no | works | acts on the patch or the engine's own UI |
| ok | node | Mod Depth | src/app/graph/NodeRegistry.cpp:171 | no | works | acts on the patch or the engine's own UI |
| ok | node | Mod Curve | src/app/graph/NodeRegistry.cpp:172 | no | works | acts on the patch or the engine's own UI |
| ok | node | MSEG | src/app/graph/NodeRegistry.cpp:173 | no | works | acts on the patch or the engine's own UI |
| ok | node | Voice Spread | src/app/graph/NodeRegistry.cpp:174 | no | works | acts on the patch or the engine's own UI |
| ok | node | Note Expression | src/app/graph/NodeRegistry.cpp:175 | no | works | acts on the patch or the engine's own UI |
| ok | node | Drift | src/app/graph/NodeRegistry.cpp:176 | no | works | acts on the patch or the engine's own UI |
| ok | node | Moves | src/app/graph/NodeRegistry.cpp:177 | no | works | acts on the patch or the engine's own UI |
| ok | node | Predictive Modulator | src/app/graph/NodeRegistry.cpp:178 | no | works | acts on the patch or the engine's own UI |
| ok | node | Predictive Coloring | src/app/graph/NodeRegistry.cpp:179 | no | works | acts on the patch or the engine's own UI |
| ok | node | CV to Pitch | src/app/graph/NodeRegistry.cpp:180 | no | works | acts on the patch or the engine's own UI |
| ok | node | Macro Knob | src/app/graph/NodeRegistry.cpp:181 | no | works | acts on the patch or the engine's own UI |
| ok | node | Macro Slider | src/app/graph/NodeRegistry.cpp:182 | no | works | acts on the patch or the engine's own UI |
| ok | node | Macro Bipolar Knob | src/app/graph/NodeRegistry.cpp:183 | no | works | acts on the patch or the engine's own UI |
| ok | node | Macro XY | src/app/graph/NodeRegistry.cpp:184 | no | works | acts on the patch or the engine's own UI |
| ok | node | Macro Toggle | src/app/graph/NodeRegistry.cpp:185 | no | works | acts on the patch or the engine's own UI |
| ok | node | Macro Trigger | src/app/graph/NodeRegistry.cpp:186 | no | works | acts on the patch or the engine's own UI |
| ok | node | Macro NumBox | src/app/graph/NodeRegistry.cpp:187 | no | works | acts on the patch or the engine's own UI |
| ok | node | Macro Radio Selector | src/app/graph/NodeRegistry.cpp:188 | no | works | acts on the patch or the engine's own UI |
| ok | node | Macro Step Gate | src/app/graph/NodeRegistry.cpp:189 | no | works | acts on the patch or the engine's own UI |
| ok | node | MIDI CC | src/app/graph/NodeRegistry.cpp:190 | no | works | acts on the patch or the engine's own UI |
| ok | node | MIDI Trigger | src/app/graph/NodeRegistry.cpp:191 | no | works | acts on the patch or the engine's own UI |
| ok | node | Path | src/app/graph/NodeRegistry.cpp:192 | no | works | acts on the patch or the engine's own UI |
| ok | node | Geometry Table | src/app/graph/NodeRegistry.cpp:193 | no | works | acts on the patch or the engine's own UI |
| ok | node | Constant | src/app/graph/NodeRegistry.cpp:194 | no | works | acts on the patch or the engine's own UI |
| ok | node | Null Modulator | src/app/graph/NodeRegistry.cpp:195 | no | works | acts on the patch or the engine's own UI |
| ok | node | Image Analyze | src/app/graph/NodeRegistry.cpp:196 | no | works | acts on the patch or the engine's own UI |
| ok | node | Hand Track | src/app/graph/NodeRegistry.cpp:197 | no | works | acts on the patch or the engine's own UI |
| ok | node | Face Track | src/app/graph/NodeRegistry.cpp:198 | no | works | acts on the patch or the engine's own UI |
| ok | node | Pose Track | src/app/graph/NodeRegistry.cpp:199 | no | works | acts on the patch or the engine's own UI |
| ok | node | Palette | src/app/graph/NodeRegistry.cpp:200 | no | works | acts on the patch or the engine's own UI |
| ok | node | Audio File | src/app/graph/NodeRegistry.cpp:201 | no | works | acts on the patch or the engine's own UI |
| ok | node | Audio Analyze | src/app/graph/NodeRegistry.cpp:202 | no | works | acts on the patch or the engine's own UI |
| ok | node | OSC Receive | src/app/graph/NodeRegistry.cpp:203 | no | works | acts on the patch or the engine's own UI |
| ok | node | OSC Send | src/app/graph/NodeRegistry.cpp:204 | no | works | acts on the patch or the engine's own UI |
| ok | node | Oscillator | src/app/graph/NodeRegistry.cpp:211 | no | works | acts on the patch or the engine's own UI |
| ok | node | Wavetable | src/app/graph/NodeRegistry.cpp:212 | no | works | acts on the patch or the engine's own UI |
| ok | node | Analog | src/app/graph/NodeRegistry.cpp:213 | no | works | acts on the patch or the engine's own UI |
| ok | node | FM | src/app/graph/NodeRegistry.cpp:214 | no | works | acts on the patch or the engine's own UI |
| ok | node | Partials | src/app/graph/NodeRegistry.cpp:215 | no | works | acts on the patch or the engine's own UI |
| ok | node | String | src/app/graph/NodeRegistry.cpp:216 | no | works | acts on the patch or the engine's own UI |
| ok | node | Wind | src/app/graph/NodeRegistry.cpp:217 | no | works | acts on the patch or the engine's own UI |
| ok | node | Piano | src/app/graph/NodeRegistry.cpp:218 | no | works | acts on the patch or the engine's own UI |
| ok | node | Wave Terrain | src/app/graph/NodeRegistry.cpp:219 | no | works | acts on the patch or the engine's own UI |
| ok | node | Equation Synth | src/app/graph/NodeRegistry.cpp:220 | no | works | acts on the patch or the engine's own UI |
| ok | node | Spectral Synth | src/app/graph/NodeRegistry.cpp:221 | no | works | acts on the patch or the engine's own UI |
| ok | node | Metallic | src/app/graph/NodeRegistry.cpp:222 | no | works | acts on the patch or the engine's own UI |
| ok | node | Sampler | src/app/graph/NodeRegistry.cpp:223 | no | works | acts on the patch or the engine's own UI |
| ok | node | Generate Samples | src/app/graph/NodeRegistry.cpp:224 | no | works | acts on the patch or the engine's own UI |
| ok | node | Slicer | src/app/graph/NodeRegistry.cpp:225 | no | works | acts on the patch or the engine's own UI |
| ok | node | PaulStretch | src/app/graph/NodeRegistry.cpp:226 | no | works | acts on the patch or the engine's own UI |
| ok | node | Molder | src/app/graph/NodeRegistry.cpp:227 | no | works | acts on the patch or the engine's own UI |
| ok | node | Grain Molder | src/app/graph/NodeRegistry.cpp:228 | no | works | acts on the patch or the engine's own UI |
| ok | node | Granular | src/app/graph/NodeRegistry.cpp:229 | no | works | acts on the patch or the engine's own UI |
| ok | node | Field Synth | src/app/graph/NodeRegistry.cpp:230 | no | works | acts on the patch or the engine's own UI |
| ok | node | Field Notes | src/app/graph/NodeRegistry.cpp:231 | no | works | acts on the patch or the engine's own UI |
| ok | node | Field Effect | src/app/graph/NodeRegistry.cpp:232 | no | works | acts on the patch or the engine's own UI |
| ok | node | Field Graph | src/app/graph/NodeRegistry.cpp:237 | no | works | acts on the patch or the engine's own UI |
| ok | node | Drum Sequencer | src/app/graph/NodeRegistry.cpp:238 | no | works | acts on the patch or the engine's own UI |
| ok | node | MPC | src/app/graph/NodeRegistry.cpp:239 | no | works | acts on the patch or the engine's own UI |
| ok | node | Looper | src/app/graph/NodeRegistry.cpp:240 | no | works | acts on the patch or the engine's own UI |
| ok | node | Plugin | src/app/graph/NodeRegistry.cpp:243 | yes | works | acts on the patch or the engine's own UI |
| ok | node | Gain | src/app/graph/NodeRegistry.cpp:246 | no | works | acts on the patch or the engine's own UI |
| ok | node | Audio Meter | src/app/graph/NodeRegistry.cpp:247 | no | works | acts on the patch or the engine's own UI |
| ok | node | Audio In | src/app/graph/NodeRegistry.cpp:248 | no | works | acts on the patch or the engine's own UI |
| ok | node | Audio Out | src/app/graph/NodeRegistry.cpp:249 | no | works | acts on the patch or the engine's own UI |
| ok | node | Mixer | src/app/graph/NodeRegistry.cpp:252 | no | works | acts on the patch or the engine's own UI |
| ok | node | Spatial Mixer | src/app/graph/NodeRegistry.cpp:253 | no | works | acts on the patch or the engine's own UI |
| ok | node | Splitter | src/app/graph/NodeRegistry.cpp:254 | no | works | acts on the patch or the engine's own UI |
| ok | node | Blend | src/app/graph/NodeRegistry.cpp:256 | no | works | acts on the patch or the engine's own UI |
| ok | node | Blend Audio | src/app/graph/NodeRegistry.cpp:257 | no | works | acts on the patch or the engine's own UI |
| ok | node | Audio Texture | src/app/graph/NodeRegistry.cpp:261 | no | works | acts on the patch or the engine's own UI |
| ok | node | Audio Color Ramp | src/app/graph/NodeRegistry.cpp:262 | no | works | acts on the patch or the engine's own UI |
| ok | node | MIDI Notes | src/app/graph/NodeRegistry.cpp:267 | no | works | acts on the patch or the engine's own UI |
| ok | node | MIDI Out | src/app/graph/NodeRegistry.cpp:268 | no | works | acts on the patch or the engine's own UI |
| ok | node | Keyboard | src/app/graph/NodeRegistry.cpp:271 | no | works | acts on the patch or the engine's own UI |
| ok | node | Note Filter | src/app/graph/NodeRegistry.cpp:272 | no | works | acts on the patch or the engine's own UI |
| ok | node | Note Transpose | src/app/graph/NodeRegistry.cpp:275 | no | works | acts on the patch or the engine's own UI |
| ok | node | Pitch Bend | src/app/graph/NodeRegistry.cpp:276 | no | works | acts on the patch or the engine's own UI |
| ok | node | Velocity Curve | src/app/graph/NodeRegistry.cpp:277 | no | works | acts on the patch or the engine's own UI |
| ok | node | Gate | src/app/graph/NodeRegistry.cpp:278 | no | works | acts on the patch or the engine's own UI |
| ok | node | Humanizer | src/app/graph/NodeRegistry.cpp:279 | no | works | acts on the patch or the engine's own UI |
| ok | node | Quantizer | src/app/graph/NodeRegistry.cpp:280 | no | works | acts on the patch or the engine's own UI |
| ok | node | Glide | src/app/graph/NodeRegistry.cpp:281 | no | works | acts on the patch or the engine's own UI |
| ok | node | Vibrato | src/app/graph/NodeRegistry.cpp:282 | no | works | acts on the patch or the engine's own UI |
| ok | node | Note Echo | src/app/graph/NodeRegistry.cpp:283 | no | works | acts on the patch or the engine's own UI |
| ok | node | Predictive Notes | src/app/graph/NodeRegistry.cpp:284 | no | works | acts on the patch or the engine's own UI |
| ok | node | Predictive Quantize | src/app/graph/NodeRegistry.cpp:285 | no | works | acts on the patch or the engine's own UI |
| ok | node | Predictive Velocity | src/app/graph/NodeRegistry.cpp:286 | no | works | acts on the patch or the engine's own UI |
| ok | node | Predictive Rhythm | src/app/graph/NodeRegistry.cpp:287 | no | works | acts on the patch or the engine's own UI |
| ok | node | Note Router | src/app/graph/NodeRegistry.cpp:288 | no | works | acts on the patch or the engine's own UI |
| ok | node | Note Merge | src/app/graph/NodeRegistry.cpp:289 | no | works | acts on the patch or the engine's own UI |
| ok | node | Note Switcher | src/app/graph/NodeRegistry.cpp:290 | no | works | acts on the patch or the engine's own UI |
| ok | node | Arpeggiator | src/app/graph/NodeRegistry.cpp:291 | no | works | acts on the patch or the engine's own UI |
| ok | node | Note Sequencer | src/app/graph/NodeRegistry.cpp:292 | no | works | acts on the patch or the engine's own UI |
| ok | node | Random Note Generator | src/app/graph/NodeRegistry.cpp:293 | no | works | acts on the patch or the engine's own UI |
| ok | node | MIDI File | src/app/graph/NodeRegistry.cpp:294 | no | works | acts on the patch or the engine's own UI |
| ok | node | Piano Roll | src/app/graph/NodeRegistry.cpp:295 | no | works | acts on the patch or the engine's own UI |
| ok | node | Chorder | src/app/graph/NodeRegistry.cpp:296 | no | works | acts on the patch or the engine's own UI |
| ok | node | Note Stack | src/app/graph/NodeRegistry.cpp:297 | no | works | acts on the patch or the engine's own UI |
| ok | node | Note Capturer | src/app/graph/NodeRegistry.cpp:298 | no | works | acts on the patch or the engine's own UI |
| ok | node | Bouncing Balls | src/app/graph/NodeRegistry.cpp:299 | no | works | acts on the patch or the engine's own UI |
| ok | node | Note Strum | src/app/graph/NodeRegistry.cpp:300 | no | works | acts on the patch or the engine's own UI |
| ok | node | Envelope | src/app/graph/NodeRegistry.cpp:301 | no | works | acts on the patch or the engine's own UI |
| ok | node | Note to CV | src/app/graph/NodeRegistry.cpp:302 | no | works | acts on the patch or the engine's own UI |
| ok | node | Velocity to CV | src/app/graph/NodeRegistry.cpp:303 | no | works | acts on the patch or the engine's own UI |
| ok | node | CV Recorder | src/app/graph/NodeRegistry.cpp:304 | no | works | acts on the patch or the engine's own UI |
| ok | node | Audio to CV | src/app/graph/NodeRegistry.cpp:305 | no | works | acts on the patch or the engine's own UI |
| ok | button | Choose folder and share##sharepatchgo | src/app/graph/PatchShare.cpp:230 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Rename | src/app/panels/ArrangePanel.cpp:128 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Color Tint | src/app/panels/ArrangePanel.cpp:139 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Assign Node... | src/app/panels/ArrangePanel.cpp:176 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Rename | src/app/panels/ArrangePanel.cpp:191 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Active | src/app/panels/ArrangePanel.cpp:199 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Sync to Tempo | src/app/panels/ArrangePanel.cpp:302 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Reset Sample BPM to Detected | src/app/panels/ArrangePanel.cpp:319 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Compositing | src/app/panels/ArrangePanel.cpp:368 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Color Grade | src/app/panels/ArrangePanel.cpp:399 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Color Tint | src/app/panels/ArrangePanel.cpp:443 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Assign Node... | src/app/panels/ArrangePanel.cpp:481 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Output | src/app/panels/ArrangePanel.cpp:493 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Group | src/app/panels/ArrangePanel.cpp:524 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Ungroup | src/app/panels/ArrangePanel.cpp:526 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Delete | src/app/panels/ArrangePanel.cpp:528 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | button | .mp4##arrRenderMp4 | src/app/panels/ArrangePanel.cpp:779 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | button | .mov##arrRenderMov | src/app/panels/ArrangePanel.cpp:785 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | button | Render Now | src/app/panels/ArrangePanel.cpp:861 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | button | Cancel | src/app/panels/ArrangePanel.cpp:868 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_Escape` | src/app/panels/ArrangePanel.cpp:1108 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_A` | src/app/panels/ArrangePanel.cpp:1133 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_C` | src/app/panels/ArrangePanel.cpp:1141 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_V` | src/app/panels/ArrangePanel.cpp:1143 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_D` | src/app/panels/ArrangePanel.cpp:1152 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_E` | src/app/panels/ArrangePanel.cpp:1157 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_G` | src/app/panels/ArrangePanel.cpp:1159 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_G` | src/app/panels/ArrangePanel.cpp:1164 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_R` | src/app/panels/ArrangePanel.cpp:1169 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_J` | src/app/panels/ArrangePanel.cpp:1171 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_K` | src/app/panels/ArrangePanel.cpp:1173 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_0` | src/app/panels/ArrangePanel.cpp:1176 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_Keypad0` | src/app/panels/ArrangePanel.cpp:1176 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_Delete` | src/app/panels/ArrangePanel.cpp:1178 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_Backspace` | src/app/panels/ArrangePanel.cpp:1178 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_M` | src/app/panels/ArrangePanel.cpp:1185 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_A` | src/app/panels/ArrangePanel.cpp:1188 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_T` | src/app/panels/ArrangePanel.cpp:1193 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_R` | src/app/panels/ArrangePanel.cpp:1198 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_B` | src/app/panels/ArrangePanel.cpp:1203 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_Z` | src/app/panels/ArrangePanel.cpp:1208 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_H` | src/app/panels/ArrangePanel.cpp:1213 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_P` | src/app/panels/ArrangePanel.cpp:1218 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_F` | src/app/panels/ArrangePanel.cpp:1225 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_Equal` | src/app/panels/ArrangePanel.cpp:1227 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_Minus` | src/app/panels/ArrangePanel.cpp:1229 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_L` | src/app/panels/ArrangePanel.cpp:1231 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_I` | src/app/panels/ArrangePanel.cpp:1233 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_O` | src/app/panels/ArrangePanel.cpp:1235 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_LeftArrow` | src/app/panels/ArrangePanel.cpp:1237 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_RightArrow` | src/app/panels/ArrangePanel.cpp:1239 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_LeftArrow` | src/app/panels/ArrangePanel.cpp:1241 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_RightArrow` | src/app/panels/ArrangePanel.cpp:1243 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_Home` | src/app/panels/ArrangePanel.cpp:1245 | no | works | routes to LaunchRequest*, a no-op in plugin mode |
| ok | shortcut | `ImGuiKey_End` | src/app/panels/ArrangePanel.cpp:1247 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_Escape` | src/app/panels/ArrangePanel.cpp:1249 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | button | `##arrangeshowviewport` | src/app/panels/ArrangePanel.cpp:1366 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | button | `##arrangeplaybtn` | src/app/panels/ArrangePanel.cpp:1400 | yes | host | the DAW's transport plays; LaunchRequest* is a no-op in plugin mode |
| ok | button | `##arrangerewindbtn` | src/app/panels/ArrangePanel.cpp:1420 | yes | host | the DAW's transport plays; LaunchRequest* is a no-op in plugin mode |
| ok | button | `##arrangetoolmodepicker` | src/app/panels/ArrangePanel.cpp:1460 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Select | src/app/panels/ArrangePanel.cpp:1503 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Range Selection | src/app/panels/ArrangePanel.cpp:1508 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Trim | src/app/panels/ArrangePanel.cpp:1513 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Pencil (Draw Clip) | src/app/panels/ArrangePanel.cpp:1518 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Cut | src/app/panels/ArrangePanel.cpp:1523 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Hand | src/app/panels/ArrangePanel.cpp:1528 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Zoom | src/app/panels/ArrangePanel.cpp:1533 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | button | `##arrangesnapbtn` | src/app/panels/ArrangePanel.cpp:1550 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | button | `##arrangeloopbtn` | src/app/panels/ArrangePanel.cpp:1613 | yes | host | Transport::FollowHost suspends the Arrange loop; the DAW loops |
| ok | button | `##arrangemarkerbtn` | src/app/panels/ArrangePanel.cpp:1632 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | button | `##arrangeresetrowh` | src/app/panels/ArrangePanel.cpp:1660 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | button | `##clipsettingstoggle` | src/app/panels/ArrangePanel.cpp:1687 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Dock Position | src/app/panels/ArrangePanel.cpp:1720 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Timeline at Bottom | src/app/panels/ArrangePanel.cpp:1723 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Timeline at Top | src/app/panels/ArrangePanel.cpp:1729 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Close Arrangement Timeline | src/app/panels/ArrangePanel.cpp:1738 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Viewport Position | src/app/panels/ArrangePanel.cpp:1746 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Dock Left | src/app/panels/ArrangePanel.cpp:1748 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Dock Right | src/app/panels/ArrangePanel.cpp:1750 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Viewport Position | src/app/panels/ArrangePanel.cpp:1875 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Dock Left | src/app/panels/ArrangePanel.cpp:1877 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Dock Right | src/app/panels/ArrangePanel.cpp:1879 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_Escape` | src/app/panels/ArrangePanel.cpp:2092 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Rename | src/app/panels/ArrangePanel.cpp:2337 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Delete Marker | src/app/panels/ArrangePanel.cpp:2359 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | button | + Add Track | src/app/panels/ArrangePanel.cpp:2920 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Video Track | src/app/panels/ArrangePanel.cpp:2928 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Audio Track | src/app/panels/ArrangePanel.cpp:2930 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_Escape` | src/app/panels/ArrangePanel.cpp:3716 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Add Clip | src/app/panels/ArrangePanel.cpp:4089 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Add Video Track | src/app/panels/ArrangePanel.cpp:4110 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Add Audio Track | src/app/panels/ArrangePanel.cpp:4115 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Rename Track | src/app/panels/ArrangePanel.cpp:4120 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Delete Track | src/app/panels/ArrangePanel.cpp:4125 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Remove from Group | src/app/panels/ArrangePanel.cpp:4130 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Convert to Group | src/app/panels/ArrangePanel.cpp:4136 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Group Selected | src/app/panels/ArrangePanel.cpp:4151 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Render Track | src/app/panels/ArrangePanel.cpp:4182 | yes | deferred | the DAW bounces |
| ok | menu | Add Video Track | src/app/panels/ArrangePanel.cpp:4218 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Add Audio Track | src/app/panels/ArrangePanel.cpp:4223 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Rename Group | src/app/panels/ArrangePanel.cpp:4228 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Duplicate Group | src/app/panels/ArrangePanel.cpp:4235 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Toggle Enabled | src/app/panels/ArrangePanel.cpp:4237 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Render Group | src/app/panels/ArrangePanel.cpp:4241 | yes | deferred | the DAW bounces |
| ok | menu | Ungroup (Keep Tracks) | src/app/panels/ArrangePanel.cpp:4252 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Delete Group + Tracks | src/app/panels/ArrangePanel.cpp:4254 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | button | + Add Track | src/app/panels/ArrangePanel.cpp:4428 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Add Video Track | src/app/panels/ArrangePanel.cpp:4457 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Add Audio Track | src/app/panels/ArrangePanel.cpp:4459 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Add Track Group | src/app/panels/ArrangePanel.cpp:4461 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | button | Cancel | src/app/panels/ArrangeRender.cpp:213 | no | unreachable | render progress window; no render can start in plugin mode (every Render entry is hidden) |
| ok | button | Cancel All | src/app/panels/ArrangeRender.cpp:218 | no | unreachable | render progress window; no render can start in plugin mode (every Render entry is hidden) |
| ok | button | Cancel | src/app/panels/ArrangeRender.cpp:281 | no | unreachable | render progress window; no render can start in plugin mode (every Render entry is hidden) |
| ok | button | Cancel All | src/app/panels/ArrangeRender.cpp:286 | no | unreachable | render progress window; no render can start in plugin mode (every Render entry is hidden) |
| ok | button | Make Unique##clipuniquevideo | src/app/panels/ArrangeRender.cpp:596 | no | works | clip, track and group editing in the Timeline |
| ok | button | Make Unique##clipuniqueaudio | src/app/panels/ArrangeRender.cpp:616 | no | works | clip, track and group editing in the Timeline |
| ok | button | Reset to Detected##clipbpmreset | src/app/panels/ArrangeRender.cpp:786 | no | works | clip, track and group editing in the Timeline |
| ok | button | Assign Different Node... | src/app/panels/ArrangeRender.cpp:894 | no | works | clip, track and group editing in the Timeline |
| ok | button | Clear Source | src/app/panels/ArrangeRender.cpp:896 | no | works | clip, track and group editing in the Timeline |
| ok | button | Assign Node... | src/app/panels/ArrangeRender.cpp:912 | no | works | clip, track and group editing in the Timeline |
| ok | button | Apply##bulkapplyrename | src/app/panels/ArrangeRender.cpp:938 | no | works | clip, track and group editing in the Timeline |
| ok | button | Ungroup | src/app/panels/ArrangeRender.cpp:1005 | no | works | clip, track and group editing in the Timeline |
| ok | button | Delete Selected Clips | src/app/panels/ArrangeRender.cpp:1011 | no | works | clip, track and group editing in the Timeline |
| ok | button | Duplicate Track | src/app/panels/ArrangeRender.cpp:1140 | no | works | clip, track and group editing in the Timeline |
| ok | button | Delete Track | src/app/panels/ArrangeRender.cpp:1146 | no | works | clip, track and group editing in the Timeline |
| ok | button | Add Video Track to Group | src/app/panels/ArrangeRender.cpp:1191 | no | works | clip, track and group editing in the Timeline |
| ok | button | Add Audio Track to Group | src/app/panels/ArrangeRender.cpp:1198 | no | works | clip, track and group editing in the Timeline |
| ok | button | Ungroup (Keep Tracks) | src/app/panels/ArrangeRender.cpp:1205 | no | works | clip, track and group editing in the Timeline |
| ok | button | Delete Group + Tracks | src/app/panels/ArrangeRender.cpp:1212 | no | works | clip, track and group editing in the Timeline |
| ok | menu | Add Video Track | src/app/panels/ArrangeSessionGrid.cpp:83 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Add Audio Track | src/app/panels/ArrangeSessionGrid.cpp:85 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Group This Track | src/app/panels/ArrangeSessionGrid.cpp:95 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Ungroup Track | src/app/panels/ArrangeSessionGrid.cpp:98 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Stop Track | src/app/panels/ArrangeSessionGrid.cpp:100 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Delete Track | src/app/panels/ArrangeSessionGrid.cpp:103 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Add Clip | src/app/panels/ArrangeSessionGrid.cpp:333 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Paste | src/app/panels/ArrangeSessionGrid.cpp:336 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Launch Row in Group | src/app/panels/ArrangeSessionGrid.cpp:338 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Paste Selected Timeline Clip | src/app/panels/ArrangeSessionGrid.cpp:341 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Stop Track | src/app/panels/ArrangeSessionGrid.cpp:359 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Launch Quantize | src/app/panels/ArrangeSessionGrid.cpp:499 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Keep Other Tracks Playing | src/app/panels/ArrangeSessionGrid.cpp:513 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Then Play Next Scene | src/app/panels/ArrangeSessionGrid.cpp:517 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Next Scene After | src/app/panels/ArrangeSessionGrid.cpp:521 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Send Scene to Timeline | src/app/panels/ArrangeSessionGrid.cpp:540 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Insert Scene Above | src/app/panels/ArrangeSessionGrid.cpp:546 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Duplicate Scene | src/app/panels/ArrangeSessionGrid.cpp:549 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Delete Scene | src/app/panels/ArrangeSessionGrid.cpp:551 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | button | + Video Track##sessaddv | src/app/panels/ArrangeSessionGrid.cpp:571 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | button | + Audio Track##sessadda | src/app/panels/ArrangeSessionGrid.cpp:574 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | button | +##sessaddtrack | src/app/panels/ArrangeSessionGrid.cpp:639 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | button | + Scene##sessaddscene | src/app/panels/ArrangeSessionGrid.cpp:651 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | shortcut | `ImGuiKey_Escape` | src/app/panels/ArrangeSessionGrid.cpp:954 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Launch Row in Group | src/app/panels/ArrangeSessionGrid.cpp:1271 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Follow Action | src/app/panels/ArrangeSessionGrid.cpp:1273 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Follow After | src/app/panels/ArrangeSessionGrid.cpp:1287 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Loop | src/app/panels/ArrangeSessionGrid.cpp:1306 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Loop Length | src/app/panels/ArrangeSessionGrid.cpp:1314 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Launch Quantize | src/app/panels/ArrangeSessionGrid.cpp:1330 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Launch Mode | src/app/panels/ArrangeSessionGrid.cpp:1343 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Cut | src/app/panels/ArrangeSessionGrid.cpp:1359 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Copy | src/app/panels/ArrangeSessionGrid.cpp:1361 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Paste | src/app/panels/ArrangeSessionGrid.cpp:1364 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Duplicate | src/app/panels/ArrangeSessionGrid.cpp:1366 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Delete | src/app/panels/ArrangeSessionGrid.cpp:1368 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Place on Timeline at Playhead | src/app/panels/ArrangeSessionGrid.cpp:1371 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | button | `##sessionmonitortoggle` | src/app/panels/ArrangeSessionGrid.cpp:1702 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | button | `##sessionplay` | src/app/panels/ArrangeSessionGrid.cpp:1728 | yes | host | the DAW's transport plays; LaunchRequest* is a no-op in plugin mode |
| ok | button | `##sessionrecord` | src/app/panels/ArrangeSessionGrid.cpp:1747 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | button | `##sessionstopall` | src/app/panels/ArrangeSessionGrid.cpp:1760 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | button | `##sessioninspectortoggle` | src/app/panels/ArrangeSessionGrid.cpp:1792 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Dock Position | src/app/panels/ArrangeSessionGrid.cpp:1848 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Session at Bottom | src/app/panels/ArrangeSessionGrid.cpp:1851 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Session at Top | src/app/panels/ArrangeSessionGrid.cpp:1857 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | menu | Close Session | src/app/panels/ArrangeSessionGrid.cpp:1866 | no | works | clips and lanes edit the patch; playback follows the host transport |
| ok | drop | `kAudioExt` | src/app/panels/ArrangeSupport2.cpp:814 | no | works | acts on the patch or the engine's own UI |
| ok | button | Submit | src/app/panels/ChatPanel.cpp:684 | no | unreachable | inside the Terminal/Chat panel: gTerminalOpen is forced off on load and every opener is guarded |
| ok | button | Settings | src/app/panels/ChatPanel.cpp:1559 | no | unreachable | inside the Terminal/Chat panel: gTerminalOpen is forced off on load and every opener is guarded |
| ok | shortcut | `ImGuiKey_UpArrow` | src/app/panels/ChatPanel.cpp:2078 | no | unreachable | inside the Terminal/Chat panel: gTerminalOpen is forced off on load and every opener is guarded |
| ok | shortcut | `ImGuiKey_DownArrow` | src/app/panels/ChatPanel.cpp:2078 | no | unreachable | inside the Terminal/Chat panel: gTerminalOpen is forced off on load and every opener is guarded |
| ok | shortcut | `ImGuiKey_UpArrow` | src/app/panels/ChatPanel.cpp:2080 | no | unreachable | inside the Terminal/Chat panel: gTerminalOpen is forced off on load and every opener is guarded |
| ok | shortcut | `ImGuiKey_Escape` | src/app/panels/ChatPanel.cpp:2726 | no | unreachable | inside the Terminal/Chat panel: gTerminalOpen is forced off on load and every opener is guarded |
| ok | shortcut | `ImGuiMod_Ctrl \| ImGuiKey_C` | src/app/panels/ChatPanel.cpp:4154 | no | unreachable | inside the Terminal/Chat panel: gTerminalOpen is forced off on load and every opener is guarded |
| ok | shortcut | `ImGuiKey_Escape` | src/app/panels/ChatPanel.cpp:4156 | no | unreachable | inside the Terminal/Chat panel: gTerminalOpen is forced off on load and every opener is guarded |
| ok | shortcut | `ImGuiKey_V` | src/app/panels/ChatPanel.cpp:4948 | no | unreachable | inside the Terminal/Chat panel: gTerminalOpen is forced off on load and every opener is guarded |
| ok | shortcut | `ImGuiKey_Escape` | src/app/panels/ChatPanel.cpp:4968 | no | unreachable | inside the Terminal/Chat panel: gTerminalOpen is forced off on load and every opener is guarded |
| ok | section | Agents | src/app/panels/CliSetupSection.cpp:71 | yes (caller) | unreachable | drawn only on the Agents & Terminal Settings tab, hidden in plugin mode |
| ok | button | Cancel | src/app/panels/CliSetupSection.cpp:305 | yes (caller) | unreachable | drawn only on the Agents & Terminal Settings tab, hidden in plugin mode |
| ok | button | Delete | src/app/panels/CliSetupSection.cpp:308 | yes (caller) | unreachable | drawn only on the Agents & Terminal Settings tab, hidden in plugin mode |
| ok | section | DAW plugin | src/app/panels/DawPluginSection.cpp:36 | yes (caller) | host | the app installs the plugin |
| ok | button | Copy path | src/app/panels/DawPluginSection.cpp:55 | yes (caller) | host | the app installs the plugin |
| ok | button | Uninstall | src/app/panels/DawPluginSection.cpp:76 | yes (caller) | host | the app installs the plugin |
| ok | button | Reveal | src/app/panels/DawPluginSection.cpp:85 | yes (caller) | host | the app installs the plugin |
| ok | shortcut | `ImGuiKey_F` | src/app/panels/FindOverlay.cpp:96 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_Enter` | src/app/panels/FindOverlay.cpp:142 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_KeypadEnter` | src/app/panels/FindOverlay.cpp:142 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_DownArrow` | src/app/panels/FindOverlay.cpp:147 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_UpArrow` | src/app/panels/FindOverlay.cpp:148 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_Escape` | src/app/panels/FindOverlay.cpp:159 | no | works | acts on the patch or the engine's own UI |
| ok | button | Clear | src/app/panels/HelpWindows.cpp:817 | no | works | acts on the patch or the engine's own UI |
| ok | button | `##preview` | src/app/panels/LibraryPanels.cpp:553 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Add to canvas | src/app/panels/LibraryPanels.cpp:658 | no | works | acts on the patch or the engine's own UI |
| ok | button | Rescan plugins | src/app/panels/LibraryPanels.cpp:799 | no | works | acts on the patch or the engine's own UI |
| ok | button | Remove | src/app/panels/LibraryPanels.cpp:843 | no | works | acts on the patch or the engine's own UI |
| ok | button | Add VST3 folder... | src/app/panels/LibraryPanels.cpp:849 | no | works | acts on the patch or the engine's own UI |
| ok | button | Clear blocklist and retry | src/app/panels/LibraryPanels.cpp:868 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Add to canvas | src/app/panels/LibraryPanels.cpp:1129 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Add to canvas | src/app/panels/LibraryPanels.cpp:1333 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Linear (Reset) | src/app/panels/ModMatrixPanel.cpp:156 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Ease In (+0.50) | src/app/panels/ModMatrixPanel.cpp:162 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Ease Out (-0.50) | src/app/panels/ModMatrixPanel.cpp:168 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Steep Exp (+0.85) | src/app/panels/ModMatrixPanel.cpp:174 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Steep Log (-0.85) | src/app/panels/ModMatrixPanel.cpp:180 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Invert Curve | src/app/panels/ModMatrixPanel.cpp:186 | no | works | acts on the patch or the engine's own UI |
| ok | button | `##unbindmod` | src/app/panels/ModMatrixPanel.cpp:603 | no | works | acts on the patch or the engine's own UI |
| ok | button | `##unbindexpr` | src/app/panels/ModMatrixPanel.cpp:746 | no | works | acts on the patch or the engine's own UI |
| ok | button | `##unbindrec` | src/app/panels/ModMatrixPanel.cpp:879 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Close panel | src/app/panels/ModMatrixPanel.cpp:989 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Rename | src/app/panels/PerfPanel.cpp:496 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Type | src/app/panels/PerfPanel.cpp:502 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Knob (1x1) | src/app/panels/PerfPanel.cpp:504 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Vertical Fader (1x2) | src/app/panels/PerfPanel.cpp:505 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Horizontal Slider (2x1) | src/app/panels/PerfPanel.cpp:506 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Toggle (1x1) | src/app/panels/PerfPanel.cpp:507 | no | works | acts on the patch or the engine's own UI |
| ok | menu | XY Pad (2x2) | src/app/panels/PerfPanel.cpp:508 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Trigger / Bang (1x1) | src/app/panels/PerfPanel.cpp:509 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Number Box (1x1) | src/app/panels/PerfPanel.cpp:510 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Radio Selector (2x1) | src/app/panels/PerfPanel.cpp:511 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Bipolar Knob (1x1) | src/app/panels/PerfPanel.cpp:512 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Step Gate (3x1) | src/app/panels/PerfPanel.cpp:513 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Assign X Axis... | src/app/panels/PerfPanel.cpp:520 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Assign Y Axis... | src/app/panels/PerfPanel.cpp:534 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Clear Destinations | src/app/panels/PerfPanel.cpp:549 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Listening X... (Move MIDI CC) | src/app/panels/PerfPanel.cpp:565 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Listening Y... (Move MIDI CC) | src/app/panels/PerfPanel.cpp:592 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Clear MIDI Bindings | src/app/panels/PerfPanel.cpp:617 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Assign Parameter... | src/app/panels/PerfPanel.cpp:627 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Clear Destination | src/app/panels/PerfPanel.cpp:641 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Clear MIDI Binding | src/app/panels/PerfPanel.cpp:686 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Color Tint | src/app/panels/PerfPanel.cpp:700 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_Escape` | src/app/panels/PerfPanel.cpp:2035 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Listening... | src/app/panels/PerfPanel.cpp:2090 | no | works | acts on the patch or the engine's own UI |
| ok | menu | MIDI learn | src/app/panels/PerfPanel.cpp:2099 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_Escape` | src/app/panels/PerfPanel.cpp:2120 | no | works | acts on the patch or the engine's own UI |
| ok | button | Cancel##cancelmidilearn | src/app/panels/PerfPanel.cpp:2163 | no | works | acts on the patch or the engine's own UI |
| ok | button | + Add Control | src/app/panels/PerfPanel.cpp:2185 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Knob (1x1) | src/app/panels/PerfPanel.cpp:2190 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Vertical Fader (1x2) | src/app/panels/PerfPanel.cpp:2191 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Horizontal Slider (2x1) | src/app/panels/PerfPanel.cpp:2192 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Toggle (1x1) | src/app/panels/PerfPanel.cpp:2193 | no | works | acts on the patch or the engine's own UI |
| ok | menu | XY Pad (2x2) | src/app/panels/PerfPanel.cpp:2194 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Momentary Trigger / Bang (1x1) | src/app/panels/PerfPanel.cpp:2196 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Digital Number Box (1x1) | src/app/panels/PerfPanel.cpp:2197 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Radio Selector (2x1) | src/app/panels/PerfPanel.cpp:2198 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Bipolar Pan Knob (1x1) | src/app/panels/PerfPanel.cpp:2199 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Step Gate Ribbon (3x1) | src/app/panels/PerfPanel.cpp:2200 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Rename Page | src/app/panels/PerfPanel.cpp:2276 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Duplicate Page | src/app/panels/PerfPanel.cpp:2281 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Delete Page | src/app/panels/PerfPanel.cpp:2297 | no | works | acts on the patch or the engine's own UI |
| ok | button | +##addpagebtn | src/app/panels/PerfPanel.cpp:2321 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_Delete` | src/app/panels/PerfPanel.cpp:2408 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_Backspace` | src/app/panels/PerfPanel.cpp:2408 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_C` | src/app/panels/PerfPanel.cpp:2410 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_X` | src/app/panels/PerfPanel.cpp:2412 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_V` | src/app/panels/PerfPanel.cpp:2417 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_D` | src/app/panels/PerfPanel.cpp:2419 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_A` | src/app/panels/PerfPanel.cpp:2421 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_Escape` | src/app/panels/PerfPanel.cpp:2423 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Dock Position | src/app/panels/PerfPanel.cpp:2435 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Bottom | src/app/panels/PerfPanel.cpp:2437 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Right | src/app/panels/PerfPanel.cpp:2438 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Left | src/app/panels/PerfPanel.cpp:2439 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Top | src/app/panels/PerfPanel.cpp:2440 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Floating Window | src/app/panels/PerfPanel.cpp:2441 | no | works | acts on the patch or the engine's own UI |
| ok | menu | + Add Control | src/app/panels/PerfPanel.cpp:2445 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Knob (1x1) | src/app/panels/PerfPanel.cpp:2447 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Vertical Fader (1x2) | src/app/panels/PerfPanel.cpp:2448 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Horizontal Slider (2x1) | src/app/panels/PerfPanel.cpp:2449 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Toggle (1x1) | src/app/panels/PerfPanel.cpp:2450 | no | works | acts on the patch or the engine's own UI |
| ok | menu | XY Pad (2x2) | src/app/panels/PerfPanel.cpp:2451 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Momentary Trigger / Bang (1x1) | src/app/panels/PerfPanel.cpp:2453 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Digital Number Box (1x1) | src/app/panels/PerfPanel.cpp:2454 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Radio Selector (2x1) | src/app/panels/PerfPanel.cpp:2455 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Bipolar Pan Knob (1x1) | src/app/panels/PerfPanel.cpp:2456 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Step Gate Ribbon (3x1) | src/app/panels/PerfPanel.cpp:2457 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Clear Page Controls | src/app/panels/PerfPanel.cpp:2460 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Close Performance Matrix | src/app/panels/PerfPanel.cpp:2469 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Dock Position | src/app/panels/PianoRollPanel.cpp:542 | no | works | edits notes; plays under host transport |
| ok | menu | Piano Roll at Bottom | src/app/panels/PianoRollPanel.cpp:545 | no | works | edits notes; plays under host transport |
| ok | menu | Piano Roll at Top | src/app/panels/PianoRollPanel.cpp:547 | no | works | edits notes; plays under host transport |
| ok | menu | Close Piano Roll | src/app/panels/PianoRollPanel.cpp:552 | no | works | edits notes; plays under host transport |
| ok | button | `##pianoToolPicker` | src/app/panels/PianoRollPanel.cpp:834 | no | works | edits notes; plays under host transport |
| ok | menu | Select | src/app/panels/PianoRollPanel.cpp:847 | no | works | edits notes; plays under host transport |
| ok | menu | Draw | src/app/panels/PianoRollPanel.cpp:849 | no | works | edits notes; plays under host transport |
| ok | menu | Erase | src/app/panels/PianoRollPanel.cpp:851 | no | works | edits notes; plays under host transport |
| ok | menu | Cut | src/app/panels/PianoRollPanel.cpp:853 | no | works | edits notes; plays under host transport |
| ok | menu | Mute | src/app/panels/PianoRollPanel.cpp:855 | no | works | edits notes; plays under host transport |
| ok | menu | Hand | src/app/panels/PianoRollPanel.cpp:857 | no | works | edits notes; plays under host transport |
| ok | menu | Zoom In | src/app/panels/PianoRollPanel.cpp:860 | no | works | edits notes; plays under host transport |
| ok | menu | Zoom Out | src/app/panels/PianoRollPanel.cpp:862 | no | works | edits notes; plays under host transport |
| ok | menu | Fit Page | src/app/panels/PianoRollPanel.cpp:864 | no | works | edits notes; plays under host transport |
| ok | menu | New Page | src/app/panels/PianoRollPanel.cpp:911 | no | works | edits notes; plays under host transport |
| ok | menu | Rename | src/app/panels/PianoRollPanel.cpp:913 | no | works | edits notes; plays under host transport |
| ok | menu | Delete Page | src/app/panels/PianoRollPanel.cpp:919 | no | works | edits notes; plays under host transport |
| ok | menu | Bar | src/app/panels/PianoRollPanel.cpp:945 | no | works | edits notes; plays under host transport |
| ok | menu | Triplet | src/app/panels/PianoRollPanel.cpp:958 | no | works | edits notes; plays under host transport |
| ok | menu | Legato | src/app/panels/PianoRollPanel.cpp:1181 | no | works | edits notes; plays under host transport |
| ok | menu | Humanize | src/app/panels/PianoRollPanel.cpp:1183 | no | works | edits notes; plays under host transport |
| ok | menu | Reverse | src/app/panels/PianoRollPanel.cpp:1185 | no | works | edits notes; plays under host transport |
| ok | menu | Invert | src/app/panels/PianoRollPanel.cpp:1187 | no | works | edits notes; plays under host transport |
| ok | menu | Double Speed | src/app/panels/PianoRollPanel.cpp:1189 | no | works | edits notes; plays under host transport |
| ok | menu | Half Speed | src/app/panels/PianoRollPanel.cpp:1191 | no | works | edits notes; plays under host transport |
| ok | menu | Strum | src/app/panels/PianoRollPanel.cpp:1193 | no | works | edits notes; plays under host transport |
| ok | menu | Chop to Grid | src/app/panels/PianoRollPanel.cpp:1195 | no | works | edits notes; plays under host transport |
| ok | menu | Velocity Ramp | src/app/panels/PianoRollPanel.cpp:1197 | no | works | edits notes; plays under host transport |
| ok | menu | Fit to Scale | src/app/panels/PianoRollPanel.cpp:1199 | no | works | edits notes; plays under host transport |
| ok | menu | All MIDI inputs | src/app/panels/PianoRollPanel.cpp:1625 | no | works | edits notes; plays under host transport |
| ok | drop | `kMidi` | src/app/panels/PianoRollPanel.cpp:1932 | no | works | edits notes; plays under host transport |
| ok | switch | Ends | src/app/panels/PianoRollPanel.cpp:2530 | no | works | edits notes; plays under host transport |
| ok | switch | Mute | src/app/panels/PianoRollPanel.cpp:2571 | no | works | edits notes; plays under host transport |
| ok | switch | Loop | src/app/panels/PianoRollPanel.cpp:2661 | no | works | edits notes; plays under host transport |
| ok | switch | Scale snap | src/app/panels/PianoRollPanel.cpp:2710 | no | works | edits notes; plays under host transport |
| ok | switch | Drum rows | src/app/panels/PianoRollPanel.cpp:2719 | no | works | edits notes; plays under host transport |
| ok | switch | Audition | src/app/panels/PianoRollPanel.cpp:2727 | no | works | edits notes; plays under host transport |
| ok | button | Unlink | src/app/panels/PianoRollPanel.cpp:2737 | no | works | edits notes; plays under host transport |
| ok | switch | Lock seed | src/app/panels/PianoRollPanel.cpp:2782 | no | works | edits notes; plays under host transport |
| ok | switch | Personal | src/app/panels/PianoRollPanel.cpp:2804 | no | works | edits notes; plays under host transport |
| ok | switch | Lock rhythm | src/app/panels/PianoRollPanel.cpp:2805 | no | works | edits notes; plays under host transport |
| ok | switch | Lock pitch | src/app/panels/PianoRollPanel.cpp:2806 | no | works | edits notes; plays under host transport |
| ok | menu | Add Note | src/app/panels/PianoRollPanel.cpp:3371 | no | works | edits notes; plays under host transport |
| ok | menu | Add Clip | src/app/panels/PianoRollPanel.cpp:3381 | no | works | edits notes; plays under host transport |
| ok | menu | Settings | src/app/panels/PianoRollPanel.cpp:3385 | no | works | edits notes; plays under host transport |
| ok | menu | Duplicate | src/app/panels/PianoRollPanel.cpp:3390 | no | works | edits notes; plays under host transport |
| ok | menu | Mute | src/app/panels/PianoRollPanel.cpp:3392 | no | works | edits notes; plays under host transport |
| ok | menu | Delete | src/app/panels/PianoRollPanel.cpp:3407 | no | works | edits notes; plays under host transport |
| ok | menu | Quantise | src/app/panels/PianoRollPanel.cpp:3428 | no | works | edits notes; plays under host transport |
| ok | menu | Fx | src/app/panels/PianoRollPanel.cpp:3433 | no | works | edits notes; plays under host transport |
| ok | drop | `kExt` | src/app/panels/SampleStudioPanel.cpp:735 | no | works | records from Host main; results leave as nodes or files |
| ok | shortcut | `ImGuiKey_Z` | src/app/panels/SampleStudioPanel.cpp:1972 | no | works | records from Host main; results leave as nodes or files |
| ok | shortcut | `ImGuiKey_Z` | src/app/panels/SampleStudioPanel.cpp:1974 | no | works | records from Host main; results leave as nodes or files |
| ok | shortcut | `ImGuiKey_E` | src/app/panels/SampleStudioPanel.cpp:1976 | no | works | records from Host main; results leave as nodes or files |
| ok | shortcut | `ImGuiKey_C` | src/app/panels/SampleStudioPanel.cpp:1978 | no | works | records from Host main; results leave as nodes or files |
| ok | shortcut | `ImGuiKey_X` | src/app/panels/SampleStudioPanel.cpp:1980 | no | works | records from Host main; results leave as nodes or files |
| ok | shortcut | `ImGuiKey_V` | src/app/panels/SampleStudioPanel.cpp:1985 | no | works | records from Host main; results leave as nodes or files |
| ok | shortcut | `ImGuiKey_A` | src/app/panels/SampleStudioPanel.cpp:1987 | no | works | records from Host main; results leave as nodes or files |
| ok | shortcut | `ImGuiKey_Delete` | src/app/panels/SampleStudioPanel.cpp:1998 | no | works | records from Host main; results leave as nodes or files |
| ok | shortcut | `ImGuiKey_Backspace` | src/app/panels/SampleStudioPanel.cpp:1998 | no | works | records from Host main; results leave as nodes or files |
| ok | shortcut | `ImGuiKey_Escape` | src/app/panels/SampleStudioPanel.cpp:2002 | no | works | records from Host main; results leave as nodes or files |
| ok | shortcut | `ImGuiKey_Z` | src/app/panels/SampleStudioPanel.cpp:2010 | no | works | records from Host main; results leave as nodes or files |
| ok | shortcut | `ImGuiKey_Equal` | src/app/panels/SampleStudioPanel.cpp:2012 | no | works | records from Host main; results leave as nodes or files |
| ok | shortcut | `ImGuiKey_KeypadAdd` | src/app/panels/SampleStudioPanel.cpp:2012 | no | works | records from Host main; results leave as nodes or files |
| ok | shortcut | `ImGuiKey_Minus` | src/app/panels/SampleStudioPanel.cpp:2014 | no | works | records from Host main; results leave as nodes or files |
| ok | shortcut | `ImGuiKey_KeypadSubtract` | src/app/panels/SampleStudioPanel.cpp:2014 | no | works | records from Host main; results leave as nodes or files |
| ok | shortcut | `ImGuiKey_LeftArrow` | src/app/panels/SampleStudioPanel.cpp:2017 | no | works | records from Host main; results leave as nodes or files |
| ok | shortcut | `ImGuiKey_RightArrow` | src/app/panels/SampleStudioPanel.cpp:2019 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | Split here | src/app/panels/SampleStudioPanel.cpp:2074 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | Join | src/app/panels/SampleStudioPanel.cpp:2076 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | Copy | src/app/panels/SampleStudioPanel.cpp:2079 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | Paste after | src/app/panels/SampleStudioPanel.cpp:2081 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | Delete | src/app/panels/SampleStudioPanel.cpp:2083 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | Play slice | src/app/panels/SampleStudioPanel.cpp:2086 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | Mute | src/app/panels/SampleStudioPanel.cpp:2088 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | Solo | src/app/panels/SampleStudioPanel.cpp:2090 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | Reverse | src/app/panels/SampleStudioPanel.cpp:2092 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | Colour | src/app/panels/SampleStudioPanel.cpp:2094 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | Settings | src/app/panels/SampleStudioPanel.cpp:2100 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | Convert to node | src/app/panels/SampleStudioPanel.cpp:2103 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | Zoom to selection | src/app/panels/SampleStudioPanel.cpp:2106 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | Zoom out | src/app/panels/SampleStudioPanel.cpp:2108 | no | works | records from Host main; results leave as nodes or files |
| ok | button | Convert to node##sliceToSampler | src/app/panels/SampleStudioPanel.cpp:2534 | no | works | records from Host main; results leave as nodes or files |
| ok | switch | Fit to 16ths | src/app/panels/SampleStudioPanel.cpp:2585 | no | works | records from Host main; results leave as nodes or files |
| ok | button | Normalise##studioNorm | src/app/panels/SampleStudioPanel.cpp:2666 | no | works | records from Host main; results leave as nodes or files |
| ok | button | Whole sample##studioDeselect | src/app/panels/SampleStudioPanel.cpp:2737 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | By transients | src/app/panels/SampleStudioPanel.cpp:2833 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | By grid | src/app/panels/SampleStudioPanel.cpp:2849 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | 1 bar | src/app/panels/SampleStudioPanel.cpp:2852 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | 1/4 | src/app/panels/SampleStudioPanel.cpp:2854 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | 1/8 | src/app/panels/SampleStudioPanel.cpp:2856 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | 1/16 | src/app/panels/SampleStudioPanel.cpp:2858 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | Equal | src/app/panels/SampleStudioPanel.cpp:2862 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | Clear slices | src/app/panels/SampleStudioPanel.cpp:2870 | no | works | records from Host main; results leave as nodes or files |
| ok | button | `##studioToolPicker` | src/app/panels/SampleStudioPanel.cpp:2952 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | Select | src/app/panels/SampleStudioPanel.cpp:2966 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | Cut | src/app/panels/SampleStudioPanel.cpp:2968 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | Hand | src/app/panels/SampleStudioPanel.cpp:2970 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | Zoom In | src/app/panels/SampleStudioPanel.cpp:2974 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | Zoom Out | src/app/panels/SampleStudioPanel.cpp:2976 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | Fit | src/app/panels/SampleStudioPanel.cpp:2978 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | WAV, 24-bit | src/app/panels/SampleStudioPanel.cpp:3075 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | WAV, 16-bit | src/app/panels/SampleStudioPanel.cpp:3077 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | FLAC | src/app/panels/SampleStudioPanel.cpp:3079 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | MP3 | src/app/panels/SampleStudioPanel.cpp:3081 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | WAV, choose where... | src/app/panels/SampleStudioPanel.cpp:3084 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | Each slice to a folder... | src/app/panels/SampleStudioPanel.cpp:3090 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | Patch | src/app/panels/SampleStudioPanel.cpp:3096 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | Dock Position | src/app/panels/SampleStudioPanel.cpp:3206 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | Sample Studio at Bottom | src/app/panels/SampleStudioPanel.cpp:3209 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | Sample Studio at Top | src/app/panels/SampleStudioPanel.cpp:3211 | no | works | records from Host main; results leave as nodes or files |
| ok | menu | Close Sample Studio | src/app/panels/SampleStudioPanel.cpp:3216 | no | works | records from Host main; results leave as nodes or files |
| ok | settings tab | Appearance | src/app/panels/SettingsWindow.cpp:93 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | settings tab | Canvas & Workspace | src/app/panels/SettingsWindow.cpp:93 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | settings tab | Audio | src/app/panels/SettingsWindow.cpp:93 | yes | host | the DAW owns the audio device and MIDI inputs |
| ok | settings tab | General & Performance | src/app/panels/SettingsWindow.cpp:93 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | settings tab | Field Language | src/app/panels/SettingsWindow.cpp:93 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | settings tab | Expression Globals | src/app/panels/SettingsWindow.cpp:93 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | settings tab | Extensions | src/app/panels/SettingsWindow.cpp:93 | no | works | packs install into the shared folder the app reads too |
| ok | settings tab | Plugins | src/app/panels/SettingsWindow.cpp:93 | yes | deferred | no plugin hosting inside a DAW |
| ok | settings tab | Agents & Terminal | src/app/panels/SettingsWindow.cpp:93 | yes | deferred | no agent Terminal inside a DAW (8.10) |
| ok | section | Theme Preset & Palette | src/app/panels/SettingsWindow.cpp:187 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | button | Reset to Defaults | src/app/panels/SettingsWindow.cpp:250 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | section | Node Module Category Colors | src/app/panels/SettingsWindow.cpp:258 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | section | Cable Category Colors | src/app/panels/SettingsWindow.cpp:288 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | section | Node Card Styling | src/app/panels/SettingsWindow.cpp:318 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | section | Display | src/app/panels/SettingsWindow.cpp:351 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | switch | Help tooltips | src/app/panels/SettingsWindow.cpp:366 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | switch | Reduce motion | src/app/panels/SettingsWindow.cpp:371 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | section | Transparency Backdrop | src/app/panels/SettingsWindow.cpp:429 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | section | Grid & Canvas | src/app/panels/SettingsWindow.cpp:450 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | switch | Snap to grid | src/app/panels/SettingsWindow.cpp:451 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | switch | Show canvas grid | src/app/panels/SettingsWindow.cpp:453 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | section | Minimap | src/app/panels/SettingsWindow.cpp:466 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | switch | Show minimap | src/app/panels/SettingsWindow.cpp:467 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | section | Cable Visibility | src/app/panels/SettingsWindow.cpp:493 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | switch | Modulation cables | src/app/panels/SettingsWindow.cpp:498 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | switch | Audio & note cables | src/app/panels/SettingsWindow.cpp:503 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | switch | Image & geometry cables | src/app/panels/SettingsWindow.cpp:508 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | button | Show all cables | src/app/panels/SettingsWindow.cpp:516 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | button | Hide all cables | src/app/panels/SettingsWindow.cpp:522 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | section | Audio Devices & Format | src/app/panels/SettingsWindow.cpp:538 | yes | host | the DAW owns the device, rate and buffer |
| ok | switch | Credit Infinite in exported files | src/app/panels/SettingsWindow.cpp:728 | yes | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | button | Apply audio settings | src/app/panels/SettingsWindow.cpp:786 | yes | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | section | MIDI Inputs | src/app/panels/SettingsWindow.cpp:810 | yes | host | MIDI comes from the DAW track |
| ok | button | Start MIDI | src/app/panels/SettingsWindow.cpp:814 | yes | host | MIDI comes from the DAW track |
| ok | section | Autosave | src/app/panels/SettingsWindow.cpp:851 | yes | host | the DAW saves the patch in its project; engine autosave never runs in plugin mode |
| ok | switch | Autosave enabled | src/app/panels/SettingsWindow.cpp:864 | yes | host | the DAW saves the patch in its project; engine autosave never runs in plugin mode |
| ok | section | Performance & Rendering | src/app/panels/SettingsWindow.cpp:874 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | switch | Vsync | src/app/panels/SettingsWindow.cpp:905 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | section | Movement Log | src/app/panels/SettingsWindow.cpp:931 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | switch | Record movement log | src/app/panels/SettingsWindow.cpp:933 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | button | Open log folder | src/app/panels/SettingsWindow.cpp:937 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | section | Learning | src/app/panels/SettingsWindow.cpp:970 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | switch | Learn from my playing | src/app/panels/SettingsWindow.cpp:975 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | button | Open learning folder | src/app/panels/SettingsWindow.cpp:982 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | section | Diagnostics | src/app/panels/SettingsWindow.cpp:1013 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | switch | Cook times | src/app/panels/SettingsWindow.cpp:1016 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | button | Copy system info | src/app/panels/SettingsWindow.cpp:1020 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | button | Reveal logs | src/app/panels/SettingsWindow.cpp:1022 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | section | Application & Updates | src/app/panels/SettingsWindow.cpp:1029 | yes | host | the app updates; the plugin follows |
| ok | switch | Check on launch | src/app/panels/SettingsWindow.cpp:1033 | yes | host | the app updates; the plugin follows |
| ok | button | Check for updates... | src/app/panels/SettingsWindow.cpp:1037 | yes | host | the app updates; the plugin follows |
| ok | button | `##clearfieldsearch` | src/app/panels/SettingsWindow.cpp:1078 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | button | Install AI Skill##fieldLanguage | src/app/panels/SettingsWindow.cpp:1090 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | section | Expression Globals | src/app/panels/SettingsWindow.cpp:1401 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | button | Install AI Skill##exprGlobals | src/app/panels/SettingsWindow.cpp:1424 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | section | Globals | src/app/panels/SettingsWindow.cpp:1438 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | button | `##removeexprglobal` | src/app/panels/SettingsWindow.cpp:1499 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | button | Add global | src/app/panels/SettingsWindow.cpp:1525 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | button | Presets... | src/app/panels/SettingsWindow.cpp:1535 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | section | Infinite | src/app/panels/SettingsWindow.cpp:1630 | yes | host | app update offer on the Extensions page |
| ok | button | Update##extApp | src/app/panels/SettingsWindow.cpp:1633 | yes | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | section | Extensions | src/app/panels/SettingsWindow.cpp:1637 | no | works | packs install into the shared folder the app reads too |
| ok | button | Cancel##extCancel | src/app/panels/SettingsWindow.cpp:1699 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | button | Check for updates##extCheck | src/app/panels/SettingsWindow.cpp:1740 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | button | Install from file...##extFile | src/app/panels/SettingsWindow.cpp:1747 | no | works | look, canvas, field and globals settings act on the engine's own UI |
| ok | section | Plugins | src/app/panels/SettingsWindow.cpp:1763 | yes | deferred | no plugin hosting inside a DAW |
| ok | section | Scanned plugins | src/app/panels/SettingsWindow.cpp:1765 | yes | deferred | no plugin hosting inside a DAW |
| ok | section | Reference | src/app/panels/SettingsWindow.cpp:1778 | yes | deferred | Terminal / CLI reference; no Terminal inside a DAW |
| ok | menu | Dock Position | src/app/panels/TerminalPanel.cpp:681 | no | unreachable | inside the Terminal/Chat panel: gTerminalOpen is forced off on load and every opener is guarded |
| ok | menu | Floating Window | src/app/panels/TerminalPanel.cpp:687 | no | unreachable | inside the Terminal/Chat panel: gTerminalOpen is forced off on load and every opener is guarded |
| ok | menu | Close Terminal | src/app/panels/TerminalPanel.cpp:692 | no | unreachable | inside the Terminal/Chat panel: gTerminalOpen is forced off on load and every opener is guarded |
| ok | shortcut | `ImGuiKey_Enter` | src/app/panels/TerminalPanel.cpp:816 | no | unreachable | inside the Terminal/Chat panel: gTerminalOpen is forced off on load and every opener is guarded |
| ok | shortcut | `ImGuiKey_Escape` | src/app/panels/TerminalPanel.cpp:822 | no | unreachable | inside the Terminal/Chat panel: gTerminalOpen is forced off on load and every opener is guarded |
| ok | shortcut | `ImGuiKey_Escape` | src/app/panels/TerminalPanel.cpp:1366 | no | unreachable | inside the Terminal/Chat panel: gTerminalOpen is forced off on load and every opener is guarded |
| ok | menu | All | src/app/panels/View3DPanel.cpp:2066 | no | works | 3D View edits the patch |
| ok | menu | None | src/app/panels/View3DPanel.cpp:2067 | no | works | 3D View edits the patch |
| ok | menu | Invert | src/app/panels/View3DPanel.cpp:2068 | no | works | 3D View edits the patch |
| ok | menu | Select Random | src/app/panels/View3DPanel.cpp:2070 | no | works | 3D View edits the patch |
| ok | menu | Checker Deselect | src/app/panels/View3DPanel.cpp:2071 | no | works | 3D View edits the patch |
| ok | menu | Select Linked | src/app/panels/View3DPanel.cpp:2073 | no | works | 3D View edits the patch |
| ok | menu | Select Same Type | src/app/panels/View3DPanel.cpp:2074 | no | works | 3D View edits the patch |
| ok | menu | Active Camera | src/app/panels/View3DPanel.cpp:2075 | no | works | 3D View edits the patch |
| ok | menu | Select Pattern | src/app/panels/View3DPanel.cpp:2076 | no | works | 3D View edits the patch |
| ok | menu | Select | src/app/panels/View3DPanel.cpp:2081 | no | works | 3D View edits the patch |
| ok | menu | Selection to Grid | src/app/panels/View3DPanel.cpp:2090 | no | works | 3D View edits the patch |
| ok | menu | Selection to Cursor | src/app/panels/View3DPanel.cpp:2091 | no | works | 3D View edits the patch |
| ok | menu | Selection to Cursor (Keep Offset) | src/app/panels/View3DPanel.cpp:2092 | no | works | 3D View edits the patch |
| ok | menu | Selection to Active | src/app/panels/View3DPanel.cpp:2093 | no | works | 3D View edits the patch |
| ok | menu | Cursor to Selected | src/app/panels/View3DPanel.cpp:2095 | no | works | 3D View edits the patch |
| ok | menu | Cursor to Active | src/app/panels/View3DPanel.cpp:2096 | no | works | 3D View edits the patch |
| ok | menu | Cursor to World Origin | src/app/panels/View3DPanel.cpp:2097 | no | works | 3D View edits the patch |
| ok | menu | Cursor to Grid | src/app/panels/View3DPanel.cpp:2098 | no | works | 3D View edits the patch |
| ok | menu | Viewpoint | src/app/panels/View3DPanel.cpp:2104 | no | works | 3D View edits the patch |
| ok | menu | Top | src/app/panels/View3DPanel.cpp:2106 | no | works | 3D View edits the patch |
| ok | menu | Bottom | src/app/panels/View3DPanel.cpp:2107 | no | works | 3D View edits the patch |
| ok | menu | Front | src/app/panels/View3DPanel.cpp:2108 | no | works | 3D View edits the patch |
| ok | menu | Back | src/app/panels/View3DPanel.cpp:2109 | no | works | 3D View edits the patch |
| ok | menu | Right | src/app/panels/View3DPanel.cpp:2110 | no | works | 3D View edits the patch |
| ok | menu | Left | src/app/panels/View3DPanel.cpp:2111 | no | works | 3D View edits the patch |
| ok | menu | Persp / Ortho | src/app/panels/View3DPanel.cpp:2114 | no | works | 3D View edits the patch |
| ok | menu | Frame selected | src/app/panels/View3DPanel.cpp:2120 | no | works | 3D View edits the patch |
| ok | menu | Local view | src/app/panels/View3DPanel.cpp:2121 | no | works | 3D View edits the patch |
| ok | menu | Remove from local view | src/app/panels/View3DPanel.cpp:2122 | no | works | 3D View edits the patch |
| ok | menu | Lock to active | src/app/panels/View3DPanel.cpp:2123 | no | works | 3D View edits the patch |
| ok | menu | Camera | src/app/panels/View3DPanel.cpp:2125 | no | works | 3D View edits the patch |
| ok | menu | Align to view | src/app/panels/View3DPanel.cpp:2128 | no | works | 3D View edits the patch |
| ok | menu | Align to selected | src/app/panels/View3DPanel.cpp:2129 | no | works | 3D View edits the patch |
| ok | menu | Zoom 1:1 | src/app/panels/View3DPanel.cpp:2140 | no | works | 3D View edits the patch |
| ok | menu | Center | src/app/panels/View3DPanel.cpp:2145 | no | works | 3D View edits the patch |
| ok | menu | Cursor + frame all | src/app/panels/View3DPanel.cpp:2148 | no | works | 3D View edits the patch |
| ok | menu | Snap | src/app/panels/View3DPanel.cpp:2158 | no | works | 3D View edits the patch |
| ok | menu | Mirror | src/app/panels/View3DPanel.cpp:2163 | no | works | 3D View edits the patch |
| ok | menu | X Global | src/app/panels/View3DPanel.cpp:2165 | no | works | 3D View edits the patch |
| ok | menu | Y Global | src/app/panels/View3DPanel.cpp:2166 | no | works | 3D View edits the patch |
| ok | menu | Z Global | src/app/panels/View3DPanel.cpp:2167 | no | works | 3D View edits the patch |
| ok | menu | Copy Transforms from Active | src/app/panels/View3DPanel.cpp:2170 | no | works | 3D View edits the patch |
| ok | menu | Clear | src/app/panels/View3DPanel.cpp:2171 | no | works | 3D View edits the patch |
| ok | menu | Location | src/app/panels/View3DPanel.cpp:2173 | no | works | 3D View edits the patch |
| ok | menu | Rotation | src/app/panels/View3DPanel.cpp:2174 | no | works | 3D View edits the patch |
| ok | menu | Scale | src/app/panels/View3DPanel.cpp:2175 | no | works | 3D View edits the patch |
| ok | menu | Align | src/app/panels/View3DPanel.cpp:2178 | no | works | 3D View edits the patch |
| ok | menu | X | src/app/panels/View3DPanel.cpp:2180 | no | works | 3D View edits the patch |
| ok | menu | Y | src/app/panels/View3DPanel.cpp:2181 | no | works | 3D View edits the patch |
| ok | menu | Z | src/app/panels/View3DPanel.cpp:2182 | no | works | 3D View edits the patch |
| ok | menu | Follow Active | src/app/panels/View3DPanel.cpp:2185 | no | works | 3D View edits the patch |
| ok | menu | Clear Follow | src/app/panels/View3DPanel.cpp:2186 | no | works | 3D View edits the patch |
| ok | menu | Pose Mode | src/app/panels/View3DPanel.cpp:2187 | no | works | 3D View edits the patch |
| ok | menu | Sculpt Mode | src/app/panels/View3DPanel.cpp:2188 | no | works | 3D View edits the patch |
| ok | menu | Edit Mode | src/app/panels/View3DPanel.cpp:2189 | no | works | 3D View edits the patch |
| ok | menu | Add IK to Bone | src/app/panels/View3DPanel.cpp:2190 | no | works | 3D View edits the patch |
| ok | menu | Clear IK | src/app/panels/View3DPanel.cpp:2191 | no | works | 3D View edits the patch |
| ok | menu | Weight Paint | src/app/panels/View3DPanel.cpp:2192 | no | works | 3D View edits the patch |
| ok | menu | Clear Weight Paint | src/app/panels/View3DPanel.cpp:2193 | no | works | 3D View edits the patch |
| ok | menu | Copy Transform | src/app/panels/View3DPanel.cpp:2198 | no | works | 3D View edits the patch |
| ok | menu | Paste Transform | src/app/panels/View3DPanel.cpp:2199 | no | works | 3D View edits the patch |
| ok | menu | Randomize Transform | src/app/panels/View3DPanel.cpp:2200 | no | works | 3D View edits the patch |
| ok | menu | Move to Mouse | src/app/panels/View3DPanel.cpp:2201 | no | works | 3D View edits the patch |
| ok | menu | Copy Items | src/app/panels/View3DPanel.cpp:2203 | no | works | 3D View edits the patch |
| ok | menu | Paste Items | src/app/panels/View3DPanel.cpp:2204 | no | works | 3D View edits the patch |
| ok | menu | Show All in Renders | src/app/panels/View3DPanel.cpp:2206 | no | works | 3D View edits the patch |
| ok | menu | Render Only This Type | src/app/panels/View3DPanel.cpp:2207 | no | works | 3D View edits the patch |
| ok | menu | Hide Selected | src/app/panels/View3DPanel.cpp:2209 | no | works | 3D View edits the patch |
| ok | menu | Hide Unselected | src/app/panels/View3DPanel.cpp:2210 | no | works | 3D View edits the patch |
| ok | drop | `kModels` | src/app/panels/View3DPanel.cpp:5062 | no | works | 3D View edits the patch |
| ok | drop | `kWorlds` | src/app/panels/View3DPanel.cpp:5073 | no | works | 3D View edits the patch |
| ok | drop | `kPictures` | src/app/panels/View3DPanel.cpp:5083 | no | works | 3D View edits the patch |
| ok | shortcut | `ImGuiKey_Escape` | src/app/panels/View3DPanel.cpp:5406 | no | works | 3D View edits the patch |
| ok | shortcut | `ImGuiKey_Backspace` | src/app/panels/View3DPanel.cpp:5423 | no | works | 3D View edits the patch |
| ok | menu | Clear Rulers | src/app/panels/View3DPanel.cpp:5641 | no | works | 3D View edits the patch |
| ok | button | Show Active | src/app/panels/View3DPanel.cpp:5956 | no | works | 3D View edits the patch |
| ok | shortcut | `ImGuiKey_UpArrow` | src/app/panels/View3DPanel.cpp:5994 | no | works | 3D View edits the patch |
| ok | shortcut | `ImGuiKey_DownArrow` | src/app/panels/View3DPanel.cpp:5994 | no | works | 3D View edits the patch |
| ok | shortcut | `ImGuiKey_DownArrow` | src/app/panels/View3DPanel.cpp:5998 | no | works | 3D View edits the patch |
| ok | shortcut | `ImGuiKey_Delete` | src/app/panels/View3DPanel.cpp:6142 | no | works | 3D View edits the patch |
| ok | shortcut | `ImGuiKey_Backspace` | src/app/panels/View3DPanel.cpp:6142 | no | works | 3D View edits the patch |
| ok | shortcut | `ImGuiKey_X` | src/app/panels/View3DPanel.cpp:6142 | no | works | 3D View edits the patch |
| ok | shortcut | `ImGuiKey_C` | src/app/panels/View3DPanel.cpp:6144 | no | works | 3D View edits the patch |
| ok | shortcut | `ImGuiKey_D` | src/app/panels/View3DPanel.cpp:6146 | no | works | 3D View edits the patch |
| ok | shortcut | `ImGuiKey_D` | src/app/panels/View3DPanel.cpp:6146 | no | works | 3D View edits the patch |
| ok | shortcut | `ImGuiKey_V` | src/app/panels/View3DPanel.cpp:6150 | no | works | 3D View edits the patch |
| ok | button | Point at 3D Cursor | src/app/panels/View3DPanel.cpp:6208 | no | works | 3D View edits the patch |
| ok | button | Render through this camera | src/app/panels/View3DPanel.cpp:6424 | no | works | 3D View edits the patch |
| ok | button | Move camera to view | src/app/panels/View3DPanel.cpp:6431 | no | works | 3D View edits the patch |
| ok | button | Remove image | src/app/panels/View3DPanel.cpp:6469 | no | works | 3D View edits the patch |
| ok | button | Transforms to Deltas | src/app/panels/View3DPanel.cpp:6712 | no | works | 3D View edits the patch |
| ok | button | Reset motion | src/app/panels/View3DPanel.cpp:6825 | no | works | 3D View edits the patch |
| ok | button | Go to node | src/app/panels/View3DPanel.cpp:6838 | no | works | 3D View edits the patch |
| ok | button | Add Material | src/app/panels/View3DPanel.cpp:7035 | no | works | 3D View edits the patch |
| ok | button | Load HDRI... | src/app/panels/View3DPanel.cpp:7141 | no | works | 3D View edits the patch |
| ok | button | Load HDRI... | src/app/panels/View3DPanel.cpp:7150 | no | works | 3D View edits the patch |
| ok | button | Render Image | src/app/panels/View3DPanel.cpp:7483 | no | works | 3D View edits the patch |
| ok | button | Render Animation | src/app/panels/View3DPanel.cpp:7486 | yes | deferred | the DAW bounces; films are made in the app |
| ok | button | Cancel Render | src/app/panels/View3DPanel.cpp:7490 | no | works | 3D View edits the patch |
| ok | button | Create Orientation | src/app/panels/View3DPanel.cpp:7522 | no | works | 3D View edits the patch |
| ok | button | Delete Orientation | src/app/panels/View3DPanel.cpp:7526 | no | works | 3D View edits the patch |
| ok | button | Cursor to Selected | src/app/panels/View3DPanel.cpp:7637 | no | works | 3D View edits the patch |
| ok | button | Selection to Cursor | src/app/panels/View3DPanel.cpp:7639 | no | works | 3D View edits the patch |
| ok | menu | From canvas | src/app/panels/View3DPanel.cpp:7816 | no | works | 3D View edits the patch |
| ok | menu | Camera | src/app/panels/View3DPanel.cpp:7827 | no | works | 3D View edits the patch |
| ok | menu | HDRI... | src/app/panels/View3DPanel.cpp:7840 | no | works | 3D View edits the patch |
| ok | menu | New scene | src/app/panels/View3DPanel.cpp:7843 | no | works | 3D View edits the patch |
| ok | menu | Duplicate scene | src/app/panels/View3DPanel.cpp:7845 | no | works | 3D View edits the patch |
| ok | menu | Copy scene | src/app/panels/View3DPanel.cpp:7847 | no | works | 3D View edits the patch |
| ok | menu | Paste scene | src/app/panels/View3DPanel.cpp:7849 | no | works | 3D View edits the patch |
| ok | menu | Delete scene | src/app/panels/View3DPanel.cpp:7851 | no | works | 3D View edits the patch |
| ok | menu | Duplicate | src/app/panels/View3DPanel.cpp:7898 | no | works | 3D View edits the patch |
| ok | menu | Copy | src/app/panels/View3DPanel.cpp:7899 | no | works | 3D View edits the patch |
| ok | menu | Paste | src/app/panels/View3DPanel.cpp:7900 | no | works | 3D View edits the patch |
| ok | menu | Rename | src/app/panels/View3DPanel.cpp:7901 | no | works | 3D View edits the patch |
| ok | menu | Hide | src/app/panels/View3DPanel.cpp:7902 | no | works | 3D View edits the patch |
| ok | menu | Remove | src/app/panels/View3DPanel.cpp:7903 | no | works | 3D View edits the patch |
| ok | menu | Frame | src/app/panels/View3DPanel.cpp:7905 | no | works | 3D View edits the patch |
| ok | menu | Group | src/app/panels/View3DPanel.cpp:7910 | no | works | 3D View edits the patch |
| ok | menu | Ungroup | src/app/panels/View3DPanel.cpp:7911 | no | works | 3D View edits the patch |
| ok | menu | Source | src/app/panels/View3DPanel.cpp:7912 | no | works | 3D View edits the patch |
| ok | menu | Object | src/app/panels/View3DPanel.cpp:7919 | no | works | 3D View edits the patch |
| ok | menu | Go to node | src/app/panels/View3DPanel.cpp:7927 | no | works | 3D View edits the patch |
| ok | menu | Add | src/app/panels/View3DPanel.cpp:7937 | no | works | 3D View edits the patch |
| ok | menu | Select | src/app/panels/View3DPanel.cpp:7942 | no | works | 3D View edits the patch |
| ok | menu | Frame all | src/app/panels/View3DPanel.cpp:7948 | no | works | 3D View edits the patch |
| ok | menu | View | src/app/panels/View3DPanel.cpp:7949 | no | works | 3D View edits the patch |
| ok | menu | Dock Position | src/app/panels/View3DPanel.cpp:8183 | no | works | 3D View edits the patch |
| ok | menu | 3D View at Bottom | src/app/panels/View3DPanel.cpp:8186 | no | works | 3D View edits the patch |
| ok | menu | 3D View at Top | src/app/panels/View3DPanel.cpp:8188 | no | works | 3D View edits the patch |
| ok | menu | Close 3D View | src/app/panels/View3DPanel.cpp:8193 | no | works | 3D View edits the patch |
| ok | menu | Close panel | src/app/panels/ViewportPanel.cpp:491 | no | works | acts on the patch or the engine's own UI |
| ok | menu | Clear all cards | src/app/panels/ViewportPanel.cpp:495 | no | works | acts on the patch or the engine's own UI |
| ok | button | Same##canary | src/app/selftest/UiAudit.cpp:379 | no | debug | design review tools, opened only by INFINITE_* review hooks |
| ok | button | Same##canary | src/app/selftest/UiAudit.cpp:381 | no | debug | design review tools, opened only by INFINITE_* review hooks |
| ok | button | Over A | src/app/selftest/UiAudit.cpp:384 | no | debug | design review tools, opened only by INFINITE_* review hooks |
| ok | button | Over B | src/app/selftest/UiAudit.cpp:386 | no | debug | design review tools, opened only by INFINITE_* review hooks |
| ok | button | A label far too long for its button | src/app/selftest/UiAudit.cpp:393 | no | debug | design review tools, opened only by INFINITE_* review hooks |
| ok | button | Faint##canary | src/app/selftest/UiAudit.cpp:395 | no | debug | design review tools, opened only by INFINITE_* review hooks |
| ok | button | Cut off | src/app/selftest/UiAudit.cpp:398 | no | debug | design review tools, opened only by INFINITE_* review hooks |
| ok | button | g.it.cube | src/app/ui/design/UiGallery.cpp:58 | no | debug | design review tools, opened only by INFINITE_* review hooks |
| ok | button | g.it.cube.on | src/app/ui/design/UiGallery.cpp:60 | no | debug | design review tools, opened only by INFINITE_* review hooks |
| ok | button | g.it.view | src/app/ui/design/UiGallery.cpp:62 | no | debug | design review tools, opened only by INFINITE_* review hooks |
| ok | button | g.it.view.on | src/app/ui/design/UiGallery.cpp:64 | no | debug | design review tools, opened only by INFINITE_* review hooks |
| ok | shortcut | `ImGuiKey_Tab` | src/app/ui/design/components/LibraryParts.h:53 | no | works | acts on the patch or the engine's own UI |
| ok | shortcut | `ImGuiKey_DownArrow` | src/app/ui/design/components/LibraryParts.h:53 | no | works | acts on the patch or the engine's own UI |
