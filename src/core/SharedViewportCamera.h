#pragma once

// A node's own default-camera rotation (azimuth/elevation only, in degrees).
// main.cpp keeps one of these per GraphNode index (gNodeCameras), so a
// node's inline mini-viewport and its viewport-panel card - which are
// otherwise separate NodeViewport instances/FBOs, see the comment above
// gPanelViewports in main.cpp - agree on rotation, while two unrelated
// nodes stay fully independent. Render3DNode is deliberately NOT part of
// this: it keeps its own separate camAzimuth/camElevation (Geometry3DNodes.h)
// regardless of what any upstream node's viewport is doing - the final
// output's framing is a deliberate setup, not something casual per-node
// viewport orbiting should ever move.
//
// Only azimuth/elevation live here - distance and pivot target stay
// per-NodeViewport-instance. NodeViewport auto-frames its own mDistance/
// mTarget to each mesh's bounding size the first time it renders that mesh
// (a thumbnail showing a small sphere needs a very different distance than
// one showing a huge terrain), so sharing distance would fight that framing.
// Rotation has no such per-mesh dependency, so it's the part worth sharing.
//
// Degrees, matching Render3DNode::camAzimuth/camElevation's existing
// convention (Geometry3DNodes.h) and the drag math in main.cpp's
// DrawPreview - NodeViewport converts to radians internally for its own trig.
struct SharedViewportCamera
{
   float azimuth = 34.3775f;
   float elevation = 22.9183f;
};
