# Node Reference Manual gap (R23)

Registered node types (`Infinite --describe`, 299 total) whose name does not appear anywhere in `Infinite_Node_Reference_Manual.pdf` (last revised in 2c12562, 2026-09-04). Names are compared with case, spaces and punctuation ignored, so a node the manual documents under a different title shows up here as a false positive: check by hand before writing a page.

The PDF is hand-maintained and not generated from source, so this list is the work order, not a fix.

| Category | Count | Missing types |
|---|---|---|
| 3D | 28 | Capsule, Pyramid, Prism, Supershape, Tetrahedron, Octahedron, Dodecahedron, Rounded Cube, Mobius Strip, Klein Bottle, Gear 3D, Star 3D, Arrow 3D, Depth Projection, Mesh to Points, Mesh to Edges, Mesh to Faces, Solidify, Wireframe, Triangulate, Normals, Explode, Screw, Delete Selected, Transform Selected, Extrude Selected, Delete, Set Vertex Color |
| AudioEffects | 3 | Resonator Bank, Cycle Shaper, Spec Blur |
| Compositing | 8 | show alpha, set alpha, alpha invert, alpha from luma, alpha levels, premultiply, chroma key, luma key |
| Effects | 4 | mirror tile, normal map, edge sobel, edge outline |
| Modulators | 2 | Velocity to CV, CV Recorder |
| Prediction | 6 | Predictive Modulator, Predictive Coloring, Predictive Notes, Predictive Quantize, Predictive Velocity, Predictive Rhythm |
| Source | 10 | Slideshow, Ellipse, Rectangle, Rounded Rect, Polygon, Hexagon, Crescent, Superellipse, Teardrop, Chevron |
| Synths | 1 | Looper |
| Utility | 2 | Field Graph, Audio Meter |

**64 of 299 types.** Help text for each lives in the hand-kept tables in `src/main.cpp` (`SpecificNodeHelpText`); reuse it as the page's first paragraph.
