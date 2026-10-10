# Node Reference Manual gap

Status at v0.5.0: closed. The Node Field Guide (`Infinite_Node_Reference_Manual.pdf`, built by `tools/manual/build.py` from `tools/manual/cards.py`) now has a card or shelf entry for every spawnable node type, 303 in total, across 26 chapters. Chapters 25 and 26 cover Sketch, Sketch 3D, Field Notes, MIDI File, MIDI Out, Spatial Mixer, Hand/Face/Pose Track, Delaunay Mesh, Voronoi Cells, Decimate, Curve Ops, Dither and NDI In/Out.

The only registered types whose name is not in the PDF are the ones the spawn menu hides (Delete Selected, Transform Selected, Extrude Selected, Group) and Field Graph, which the Field Language Manual covers instead.

To re-check after adding a node: dump the registered types with `Infinite --describe --json out.json` (or `tools/gen-patch-skill.py`, which lists them in the patch-authoring skill), compare each name to `pdftotext Infinite_Node_Reference_Manual.pdf`, and add a card to `tools/manual/cards.py` for any that are missing. Reuse the node's text from `SpecificNodeHelpText` in `src/app/panels/HelpWindows.cpp` as the card description, then rerun `python3 tools/manual/build.py`.
