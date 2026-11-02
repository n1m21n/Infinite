# 3D View parity: test checklist

Open `~/Desktop/Infinite-dev.app`. Add 3+ objects to a 3D View. Right-click empty space or an object for the menus.

## Select
- [ ] Right-click > Select > All / None / Invert (Ctrl+I) behave like Blender
- [ ] Select Random picks about half; pressing again picks a different set
- [ ] Checker Deselect keeps every other selected object
- [ ] Select Linked adds objects in the same group (Ctrl+G first)
- [ ] Select Same Type with a light active adds all lights
- [ ] Ctrl+drag on empty space draws a loop and selects objects whose centre is inside; Shift+Ctrl+drag adds
- [ ] Ctrl+click on an object still selects it, no loop

## Object menu (right-click > Object)
- [ ] Snap > Selection to Grid rounds positions to the snap step
- [ ] Selection to Cursor, and Keep Offset (whole selection moves together, relative layout kept)
- [ ] Selection to Active (needs 2+ selected, last clicked = active)
- [ ] Cursor to Selected / Active / World Origin
- [ ] Mirror X/Y/Z Global flips the object across that Z-up axis
- [ ] Hide Selected (H), Hide Unselected (Shift+H); Alt+H shows all

## View menu (right-click > View)
- [ ] Top, Bottom, Front, Back, Right, Left match Blender's views, with smooth glide
- [ ] Perspective/Orthographic toggles
- [ ] Frame Selected, Frame All, Toggle Local View
- [ ] Align Active Camera to View, and to Selected (aims the camera at the selection)
- [ ] Center Cursor and Frame All
- [ ] Zoom In / Out

## Crash fix
- [ ] Right-click with nothing / 2+ selected, then close the menu: no crash

## Proportional editing
- [ ] O toggles it (also Sidebar > View > Proportional Editing); Falloff and Size fields work
- [ ] Select one object, press G, move: nearby unselected objects follow, weaker with distance
- [ ] A faint circle shows the reach during G/R/S; mouse wheel or PageUp/PageDown resizes it (no zoom while transforming)
- [ ] R and S also pull neighbours; Esc/right-click restores every object; one Ctrl+Z undoes all
- [ ] Constant falloff moves everything inside the circle equally
- [ ] Off by default; saved with the scene

## Lock transform (sidebar Object tab > Lock)
- [ ] Tick X under Location, then G-move the object: it will not move along X; Y and Z still move
- [ ] Same for Rotation and Scale per axis (axes read X/Y/Z in the Z-up display)
- [ ] Typing a value in the Location/Rotation/Scale fields still works while locked (as in Blender)
- [ ] Locks survive save and reopen; Ctrl+Z undoes a lock change

## Overlays, gizmos, navigation, snapping (sidebar View tab)
- [ ] Navigation > Invert zoom direction flips the wheel
- [ ] Overlays > Text info off hides the view name; Statistics shows object, camera and light counts; Origins (all) shows an orange dot on every item
- [ ] Gizmos > Navigate off hides the axis ball; Move, rotate, scale off hides the handles and tool pad
- [ ] Snap > To other objects, with Snap on: a free G move jumps onto another object's origin when within about 24 px

## Roll, dimensions, camera zoom
- [ ] Shift+Numpad 4 / 6 rolls the view 15 deg about the line of sight (also View > Roll Left / Right); any orbit or numpad view straightens it
- [ ] Object tab > Transform > Dimensions shows the object's size in the Z-up axes; editing one rescales it (greyed out when the object is rotated)
- [ ] In camera view, View > Zoom Camera 1:1 resets the frame zoom and pan

## Repeat, cycle, multi-edit
- [ ] Move or rotate or scale an object (G/R/S), select another, press Shift+R: the same move/turn/scale is applied again, one undo step
- [ ] Two objects overlap on screen: click selects the front one; click again at the same spot selects the one behind; keeps cycling
- [ ] Select 3 objects, change Location X in the Object tab holding Alt: all three get that value. Without Alt only the active one changes
- [ ] The Object tab with several selected shows the last-clicked (active) object

## Look at
- [ ] Select a camera, Data tab > Look at > pick an object: the camera keeps facing it while you move the object (also in the rendered output)
- [ ] Same for a spot or sun light; set back to None to release it; saved with the scene

## Select modes, Ctrl+L, render slots
- [ ] Right-click the Select chip: Box, Lasso, Cursor, Measure listed. Lasso drags a free outline without Ctrl.
- [ ] Cursor mode: click empty space puts the 3D cursor on the plane through the orbit target; undo moves it back.
- [ ] Measure mode: drag draws a gold line with length and X/Y/Z deltas; Esc or switching tool clears it.
- [ ] Select two or more items, Ctrl+L (Object > Copy Transforms from Active): all take the active item's location, rotation, scale; undo restores.
- [ ] Render two stills: the Render result card shows Reveal Latest and Reveal Previous, each opening its own file.

## Infinite only (manual)
- [ ] 3D View image to the projector through an Output node: note fps.
- [ ] 50, 128, 256 objects, panel open and closed: note fps.
- [ ] Save, reopen: placements, groups, locks, panel scenes come back; plays and looks right.
