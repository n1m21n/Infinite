#pragma once

#include <string>
#include <vector>

// Loads 3D Gaussian Splatting point clouds (.ply from 3DGS trainers, and
// antimatter15's compact .splat format) into a plain CPU-side struct. No GL,
// no node - see docs/plans/gaussian-splat-node.md for the full design and
// docs/plans/gaussian-splat-prompt.md for the phased build plan this file is
// phase 1 of. Rendering (phase 2) and the node wrapper (phase 3) consume
// SplatCloud but live elsewhere.
namespace SplatIO
{
   // One Gaussian. `cov` is precomputed once here on load - Sigma = R S S^T R^T,
   // upper triangle (xx,xy,xz,yy,yz,zz) - so the render path never redoes that
   // per frame; it is the only field the GPU texture is built from (see
   // gaussian-splat-node.md S6, "GPU data layout"). `scale`/`rot` are kept on
   // the CPU struct purely for a future Field pass that needs the ellipsoid
   // shape directly - the render path must never read them.
   struct Splat
   {
      float px = 0.0f, py = 0.0f, pz = 0.0f;
      float cov[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}; // xx, xy, xz, yy, yz, zz
      float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;        // linear color, opacity already sigmoid'd

      float sx = 1.0f, sy = 1.0f, sz = 1.0f;               // ellipsoid radii, already exp()'d
      float qw = 1.0f, qx = 0.0f, qy = 0.0f, qz = 0.0f;    // rotation, already normalized, wxyz order
   };

   struct SplatCloud
   {
      std::vector<Splat> splats;
      float boundsMin[3] = {0.0f, 0.0f, 0.0f};
      float boundsMax[3] = {0.0f, 0.0f, 0.0f};

      // True for a real 3DGS training export (has f_dc_0, opacity and
      // scale_0/1/2). LoadSplatPly sets this false when any of those were
      // missing and it had to fall back to plain-point-cloud heuristics
      // (red/green/blue color, opaque alpha, density-estimated radius) - see
      // LoadSplatPly's header comment. Always true out of LoadSplatFile,
      // since the .splat format has no optional fields.
      bool hadTrainedFields = true;

      bool Empty() const { return splats.empty(); }
   };

   // Reads a 3D Gaussian Splatting .ply (binary_little_endian only - the
   // universal convention for 3DGS exports). Parses the header into a
   // property-name -> byte-offset map rather than assuming field order:
   // Postshot, Nerfstudio and INRIA exports all differ. Returns false and
   // fills outError on failure (missing file, ASCII/big-endian format,
   // missing x/y/z).
   //
   // Most .ply files people actually have lying around are NOT trained 3DGS
   // output - they're plain colored point clouds (Sketchfab/photogrammetry/
   // CloudCompare/MeshLab exports) with only x,y,z,red,green,blue and none
   // of the SH-color/opacity/scale/rotation properties. Rather than decode
   // those to the SH-DC-missing/opacity-missing/scale-missing defaults
   // (which are correct for a genuinely-present-but-zero field, not an
   // absent one, and previously rendered as one solid gray blob), this
   // degrades gracefully per field when the trained property is absent:
   //   - color:   f_dc_0/1/2 missing -> use red/green/blue (0-255) directly
   //              if present, else flat gray as before.
   //   - opacity: opacity missing -> use alpha (0-255) directly if present,
   //              else fully opaque (1.0) instead of sigmoid(0)=0.5.
   //   - scale:   scale_0/1/2 missing -> instead of a fixed 1.0-world-unit
   //              radius, estimate a radius from the cloud's point density
   //              (bounding-box diagonal / cbrt(splat count)) so a
   //              photogrammetry scan's splats don't blow up into one blob.
   // SplatCloud::hadTrainedFields is set false when any of the above fired,
   // so callers can tell a "best-effort" load from a real trained one.
   bool LoadSplatPly(const std::string& path, SplatCloud& out, std::string& outError);

   // Reads antimatter15's compact 32-bytes-per-splat .splat format: position
   // (3x float32) and scale (3x float32, already linear) followed by color
   // (4x uint8 rgba) and rotation (4x uint8 quantized quaternion, wxyz,
   // byte = round(component * 128 + 128)). Secondary format - .ply is the
   // primary interchange target.
   bool LoadSplatFile(const std::string& path, SplatCloud& out, std::string& outError);
}
