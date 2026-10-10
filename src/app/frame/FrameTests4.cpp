// Per-frame self-test blocks moved verbatim out of the main loop in main.cpp.
#include "app/AppShared.h"

namespace app
{

void FrameTest_TRANSFORMSWEEPTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_TRANSFORMSWEEPTEST") != nullptr && frameId == 6)
      {
         // Stands in for any upstream source: forwards mesh/material/etc to a
         // real node, but returns an arbitrary injected matrix from
         // GetModelMatrix(). This decouples "does the consumer apply/track its
         // input's transform" from needing to know that input's own field
         // names (posX vs pos[0] vs no transform at all) - the one thing every
         // IGeometrySource is required to answer is GetModelMatrix().
         struct TransformProbeSource : public IGeometrySource
         {
            IGeometrySource* wrapped = nullptr;
            Mat4 matrix;
            const Mesh& GetMesh() override { return wrapped->GetMesh(); }
            unsigned long long MeshRevision() override { return wrapped->MeshRevision(); }
            Mat4 GetModelMatrix() const override { return matrix; }
            Material GetMaterial() const override { return wrapped->GetMaterial(); }
            unsigned int GetSurfaceTexture() override { return wrapped->GetSurfaceTexture(); }
         };

         GeometryNode probeMesh;
         probeMesh.shape = 1; // cube: closed, several vertices, no degenerate cases
         probeMesh.detail = 4;
         TransformProbeSource probe;
         probe.wrapped = &probeMesh;

         int frame = 20000;
         auto cook = [&](IGeometrySource* g) {
            if (auto* n = dynamic_cast<INode*>(g)) n->CookIfNeeded(frame);
            frame++;
         };

         struct Result { std::string name; bool ok; bool hadGeometry; float dx, dy, dz; };
         std::vector<Result> results;

         // The shared invariant: whatever a node's GetMesh() contains, once
         // combined with its own GetModelMatrix() the result is where the
         // object actually sits in the scene - that combination is exactly
         // what Render 3D uses to draw it. Nudging the probe's matrix by a
         // distinct, non-uniform amount per axis catches an axis swap or a
         // dropped component, not just "moved by something".
         auto checkGeneric = [&](const char* name, IGeometrySource* node)
         {
            probe.matrix = Mat4::Identity();
            cook(node);
            const Mesh before = MeshOps::Transform(node->GetMesh(), node->GetModelMatrix());

            probe.matrix = Mat4::Translation(5.0f, 7.0f, 3.0f);
            cook(node);
            const Mesh after = MeshOps::Transform(node->GetMesh(), node->GetModelMatrix());

            const bool hadGeometry = !before.vertices.empty() && !after.vertices.empty() &&
                                     before.vertices.size() == after.vertices.size();
            float dx = 0, dy = 0, dz = 0;
            bool ok = false;
            if (hadGeometry)
            {
               dx = after.vertices[0].px - before.vertices[0].px;
               dy = after.vertices[0].py - before.vertices[0].py;
               dz = after.vertices[0].pz - before.vertices[0].pz;
               ok = std::fabs(dx - 5.0f) < 1e-3f && std::fabs(dy - 7.0f) < 1e-3f &&
                    std::fabs(dz - 3.0f) < 1e-3f;
            }
            results.push_back({ name, ok, hadGeometry, dx, dy, dz });
         };

         GeometryOpNode opNode; opNode.op = GeometryOpNode::kTransform; opNode.input = &probe;
         checkGeneric("GeometryOpNode", &opNode);

         DisplacementNode dispNode; dispNode.input = &probe;
         checkGeneric("DisplacementNode", &dispNode);

         AudioDisplacementNode adispNode; adispNode.input = &probe;
         checkGeneric("AudioDisplacementNode", &adispNode);

         SetColorNode setColorNode; setColorNode.input = &probe;
         checkGeneric("SetColorNode", &setColorNode);

         MeshResynthNode resynthNode; resynthNode.input = &probe;
         checkGeneric("MeshResynthNode", &resynthNode);

         MeshToPointsNode m2pNode; m2pNode.input = &probe; m2pNode.mode = 0;
         checkGeneric("MeshToPointsNode", &m2pNode);

         Null3DNode nullNode; nullNode.input = &probe;
         checkGeneric("Null3DNode", &nullNode);

         MaterialNode matNode; matNode.input = &probe;
         checkGeneric("MaterialNode", &matNode);

         MappingNode mapNode; mapNode.input = &probe;
         checkGeneric("MappingNode", &mapNode);

         JoinGeometryNode joinNode; joinNode.mode = JoinGeometryNode::kMerge; joinNode.inputs[0] = &probe;
         checkGeneric("JoinGeometryNode", &joinNode);

         DistributePointsOnFacesNode distFacesNode; distFacesNode.input = &probe;
         checkGeneric("DistributePointsOnFacesNode", &distFacesNode);

         PointsToVerticesNode p2vNode; p2vNode.input = &probe;
         checkGeneric("PointsToVerticesNode", &p2vNode);

         MergeByDistanceNode mergeNode; mergeNode.input = &probe; mergeNode.threshold = 0.0f;
         checkGeneric("MergeByDistanceNode", &mergeNode);

         // Wrap needs a real, non-degenerate target to project onto - a
         // stand-in sphere, fixed in place (not run through the probe matrix,
         // since the invariant under test is "does the *source* input's
         // transform reach the output", not the target's).
         GeometryNode probe2;
         probe2.shape = 2; // sphere: real surface for the source to snap onto
         probe2.detail = 4;
         WrapNode wrapNode; wrapNode.sourceInput = &probe; wrapNode.targetInput = &probe2;
         // Snapping onto a fixed target is inherently non-linear (the closest
         // point on the target can change discontinuously as the source
         // moves), so it would not reproduce the exact +5/+7/+3 shift this
         // check looks for even when working correctly. Blend 0 isolates the
         // one thing this sweep actually checks - that the source's own
         // transform reaches the output - from the wrap projection itself,
         // which has its own coverage.
         wrapNode.blend = 0.0f;
         checkGeneric("WrapNode", &wrapNode);

         ClothNode clothNode;
         clothNode.input = &probe;
         clothNode.pinMode = ClothNode::kPinNone;
         clothNode.gravityX = clothNode.gravityY = clothNode.gravityZ = 0.0f;
         clothNode.windX = clothNode.windY = clothNode.windZ = 0.0f;
         checkGeneric("ClothNode", &clothNode);

         // Pinned to the probe's slot so the sweep exercises the forwarding
         // path, not the clock-driven switching itself (covered separately).
         Switcher3DNode sw3Node;
         sw3Node.inputs[0] = &probe;
         sw3Node.manual = true;
         sw3Node.manualSlot = 0;
         checkGeneric("Switcher3DNode", &sw3Node);

         // Instance on Points draws through per-instance transforms rather
         // than GetMesh()+GetModelMatrix(), so it needs its own probe on each
         // of its two geometry slots instead of the generic check.
         auto checkInstancing = [&](const char* name, bool probeIsPointSource)
         {
            GeometryNode otherSide;
            otherSide.shape = 2; // sphere
            InstanceOnPointsNode inst;
            inst.pointSource = probeIsPointSource ? (IGeometrySource*)&probe : (IGeometrySource*)&otherSide;
            inst.instanceShape = probeIsPointSource ? (IGeometrySource*)&otherSide : (IGeometrySource*)&probe;
            inst.pointMode = 0; // vertices
            inst.maxPoints = 50;
            inst.instanceScale = 1.0f;
            inst.scaleRandom = 0.0f;
            inst.rotationRandom = 0.0f;
            inst.alignToNormal = false;

            probe.matrix = Mat4::Identity();
            cook(&inst);
            const bool hadBefore = inst.InstanceCount() > 0;
            const Mat4 before = hadBefore ? inst.InstanceTransforms()[0] : Mat4::Identity();

            probe.matrix = Mat4::Translation(5.0f, 7.0f, 3.0f);
            cook(&inst);
            const bool hadAfter = inst.InstanceCount() > 0;
            const Mat4 after = hadAfter ? inst.InstanceTransforms()[0] : Mat4::Identity();

            const bool hadGeometry = hadBefore && hadAfter;
            float dx = 0, dy = 0, dz = 0;
            bool ok = false;
            if (hadGeometry)
            {
               dx = after.m[12] - before.m[12];
               dy = after.m[13] - before.m[13];
               dz = after.m[14] - before.m[14];
               ok = std::fabs(dx - 5.0f) < 1e-3f && std::fabs(dy - 7.0f) < 1e-3f &&
                    std::fabs(dz - 3.0f) < 1e-3f;
            }
            results.push_back({ name, ok, hadGeometry, dx, dy, dz });
         };
         checkInstancing("InstanceOnPointsNode(pointSource)", true);
         checkInstancing("InstanceOnPointsNode(instanceShape)", false);

         // Path-follow drives a modulator output rather than a mesh, so its
         // check reads CurrentPoint() instead of GetMesh()+GetModelMatrix().
         // Boundary-follow needs an open mesh (a closed cube has no boundary
         // loop), so the probe is switched to wrap a plane for this one check
         // - nothing after this reuses probeMesh/probe.
         {
            probeMesh.shape = 0; // plane
            PathNode path;
            path.geometrySource = &probe;
            path.followMode = PathNode::kFollowBoundary;
            path.speed = 0.0f; path.phase = 0.0f; path.pingPong = false;
            path.sizeX = path.sizeY = path.sizeZ = 1.0f;

            probe.matrix = Mat4::Identity();
            path.CookIfNeeded(frame++);
            float before[3]; path.CurrentPoint(before);
            const bool hadBefore = path.IsFollowing();

            probe.matrix = Mat4::Translation(5.0f, 7.0f, 3.0f);
            path.CookIfNeeded(frame++);
            float after[3]; path.CurrentPoint(after);
            const bool hadAfter = path.IsFollowing();

            const bool hadGeometry = hadBefore && hadAfter;
            float dx = 0, dy = 0, dz = 0;
            bool ok = false;
            if (hadGeometry)
            {
               dx = after[0] - before[0]; dy = after[1] - before[1]; dz = after[2] - before[2];
               ok = std::fabs(dx - 5.0f) < 1e-3f && std::fabs(dy - 7.0f) < 1e-3f &&
                    std::fabs(dz - 3.0f) < 1e-3f;
            }
            results.push_back({ "PathNode(follow)", ok, hadGeometry, dx, dy, dz });
         }

         // GeometryTableNode also drives modulator outputs rather than a mesh,
         // so like PathNode(follow) it gets its own variant reading a probe
         // accessor - SampleRow() - instead of GetMesh()+GetModelMatrix().
         // space is pinned to Fixed: Bounds mode self-scales to the sampled
         // set's own bounding box, so it legitimately does not move under a
         // rigid translation (§4.3) and would fail this check for a reason
         // that isn't a bug. A fresh cube/probe pair is used rather than
         // reusing probeMesh/probe, both of which the PathNode(follow) block
         // above already repurposed.
         {
            GeometryNode tableProbeMesh;
            tableProbeMesh.shape = 1; // cube
            tableProbeMesh.detail = 4;
            TransformProbeSource tableProbe;
            tableProbe.wrapped = &tableProbeMesh;

            GeometryTableNode table;
            table.geometrySource = &tableProbe;
            table.sampleMode = GeometryTableNode::kVertex;
            table.space = GeometryTableNode::kSpaceFixed;
            table.smooth = 0.0f;

            tableProbe.matrix = Mat4::Identity();
            table.CookIfNeeded(frame++);
            float before[3]; table.SampleRow(0, before);
            const bool hadBefore = table.HasSamples();

            tableProbe.matrix = Mat4::Translation(5.0f, 7.0f, 3.0f);
            table.CookIfNeeded(frame++);
            float after[3]; table.SampleRow(0, after);
            const bool hadAfter = table.HasSamples();

            const bool hadGeometry = hadBefore && hadAfter;
            float dx = 0, dy = 0, dz = 0;
            bool ok = false;
            if (hadGeometry)
            {
               dx = after[0] - before[0]; dy = after[1] - before[1]; dz = after[2] - before[2];
               ok = std::fabs(dx - 5.0f) < 1e-3f && std::fabs(dy - 7.0f) < 1e-3f &&
                    std::fabs(dz - 3.0f) < 1e-3f;
            }
            results.push_back({ "GeometryTableNode", ok, hadGeometry, dx, dy, dz });
         }

         bool allOk = true;
         for (const Result& r : results)
         {
            if (!r.hadGeometry)
            {
               printf("  [SKIP] %-32s — produced no comparable geometry\n", r.name.c_str());
               continue;
            }
            printf("  [%s] %-32s dx=%.2f dy=%.2f dz=%.2f (want 5.00 7.00 3.00)\n",
                   r.ok ? "pass" : "FAIL", r.name.c_str(), r.dx, r.dy, r.dz);
            if (!r.ok)
               allOk = false;
         }
         printf("%s\n", allOk ? "TRANSFORM SWEEP OK" : "TRANSFORM SWEEP FAIL");
      }
}

void FrameTest_MAPPINGSWEEPTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_MAPPINGSWEEPTEST") != nullptr && frameId == 6)
      {
         struct MappingProbeSource : public IGeometrySource
         {
            IGeometrySource* wrapped = nullptr;
            MappingTransform mapping;
            const Mesh& GetMesh() override { return wrapped->GetMesh(); }
            unsigned long long MeshRevision() override { return wrapped->MeshRevision(); }
            Mat4 GetModelMatrix() const override { return wrapped->GetModelMatrix(); }
            Material GetMaterial() const override { return wrapped->GetMaterial(); }
            unsigned int GetSurfaceTexture() override { return wrapped->GetSurfaceTexture(); }
            MappingTransform GetMappingTransform() const override { return mapping; }
         };

         GeometryNode probeMesh;
         probeMesh.shape = 1; // cube
         probeMesh.detail = 4;
         MappingProbeSource probe;
         probe.wrapped = &probeMesh;
         // Non-identity and non-uniform on every field, so a node that forwards
         // only part of MappingTransform (say space but not scale) still fails.
         probe.mapping.space = kMapSpaceGenerated;
         probe.mapping.translate[0] = 1.5f; probe.mapping.translate[1] = -2.5f; probe.mapping.translate[2] = 0.75f;
         probe.mapping.rotate[0] = 0.3f; probe.mapping.rotate[1] = 0.6f; probe.mapping.rotate[2] = 0.9f;
         probe.mapping.scale[0] = 2.0f; probe.mapping.scale[1] = 3.0f; probe.mapping.scale[2] = 4.0f;
         probe.mapping.triplanarBlend = 0.45f;

         int frame = 21000;
         auto cook = [&](IGeometrySource* g) {
            if (auto* n = dynamic_cast<INode*>(g)) n->CookIfNeeded(frame);
            frame++;
         };
         auto matches = [&](const MappingTransform& a, const MappingTransform& b) {
            if (a.space != b.space)
               return false;
            if (std::fabs(a.triplanarBlend - b.triplanarBlend) > 1e-4f)
               return false;
            for (int i = 0; i < 3; i++)
            {
               if (std::fabs(a.translate[i] - b.translate[i]) > 1e-4f) return false;
               if (std::fabs(a.rotate[i] - b.rotate[i]) > 1e-4f) return false;
               if (std::fabs(a.scale[i] - b.scale[i]) > 1e-4f) return false;
            }
            return true;
         };

         struct Result { std::string name; bool ok; };
         std::vector<Result> results;
         auto checkForwarding = [&](const char* name, IGeometrySource* node)
         {
            cook(node);
            results.push_back({ name, matches(node->GetMappingTransform(), probe.mapping) });
         };

         GeometryOpNode opNode; opNode.op = GeometryOpNode::kTransform; opNode.input = &probe;
         checkForwarding("GeometryOpNode", &opNode);

         DisplacementNode dispNode; dispNode.input = &probe;
         checkForwarding("DisplacementNode", &dispNode);

         AudioDisplacementNode adispNode; adispNode.input = &probe;
         checkForwarding("AudioDisplacementNode", &adispNode);

         SetColorNode setColorNode; setColorNode.input = &probe;
         checkForwarding("SetColorNode", &setColorNode);

         MeshResynthNode resynthNode; resynthNode.input = &probe;
         checkForwarding("MeshResynthNode", &resynthNode);

         MeshToPointsNode m2pNode; m2pNode.input = &probe; m2pNode.mode = 0;
         checkForwarding("MeshToPointsNode", &m2pNode);

         Null3DNode nullNode; nullNode.input = &probe;
         checkForwarding("Null3DNode", &nullNode);

         MaterialNode matNode; matNode.input = &probe;
         checkForwarding("MaterialNode", &matNode);

         JoinGeometryNode joinNode; joinNode.mode = JoinGeometryNode::kMerge; joinNode.inputs[0] = &probe;
         checkForwarding("JoinGeometryNode", &joinNode);

         DistributePointsOnFacesNode distFacesNode; distFacesNode.input = &probe;
         checkForwarding("DistributePointsOnFacesNode", &distFacesNode);

         PointsToVerticesNode p2vNode; p2vNode.input = &probe;
         checkForwarding("PointsToVerticesNode", &p2vNode);

         MergeByDistanceNode mergeNode; mergeNode.input = &probe; mergeNode.threshold = 0.0f;
         checkForwarding("MergeByDistanceNode", &mergeNode);

         GeometryNode probe2;
         probe2.shape = 2; // sphere
         probe2.detail = 4;
         WrapNode wrapNode; wrapNode.sourceInput = &probe; wrapNode.targetInput = &probe2; wrapNode.blend = 0.0f;
         checkForwarding("WrapNode", &wrapNode);

         ClothNode clothNode;
         clothNode.input = &probe;
         clothNode.pinMode = ClothNode::kPinNone;
         clothNode.gravityX = clothNode.gravityY = clothNode.gravityZ = 0.0f;
         clothNode.windX = clothNode.windY = clothNode.windZ = 0.0f;
         checkForwarding("ClothNode", &clothNode);

         Switcher3DNode sw3Node;
         sw3Node.inputs[0] = &probe;
         sw3Node.manual = true;
         sw3Node.manualSlot = 0;
         checkForwarding("Switcher3DNode", &sw3Node);

         // Phase 5: InstanceOnPointsNode now forwards GetMappingTransform()
         // from instanceShape (the stamp), the same shape as GetMaterial()/
         // GetMaterialTexture() just above - so unlike PathNode and
         // GeometryTableNode below, it *does* reduce to a plain
         // GetMappingTransform() read once wired to the probe as the stamp.
         GeometryNode instPoints;
         instPoints.shape = 1; // cube
         instPoints.detail = 2;
         InstanceOnPointsNode instNode;
         instNode.pointSource = &instPoints;
         instNode.instanceShape = &probe;
         instNode.pointMode = 0; // vertices
         instNode.maxPoints = 50;
         instNode.instanceScale = 1.0f;
         instNode.scaleRandom = 0.0f;
         checkForwarding("InstanceOnPointsNode", &instNode);

         // MappingNode itself is excluded on purpose: it *sets* the mapping
         // transform from its own params rather than forwarding one, so it is
         // not a passthrough case this check applies to. PathNode and
         // GeometryTableNode are excluded because neither is an
         // IGeometrySource at all - they consume one and emit modulator
         // outputs, so GetMappingTransform() isn't a method that exists to
         // call on them; TRANSFORMSWEEPTEST gives them their own variant.
         // GeometryTableNode specifically forwards no mapping transform
         // because it forwards no geometry at all - nothing downstream ever
         // reads a mapping off it.
         bool allOk = true;
         for (const Result& r : results)
         {
            printf("  [%s] %-24s\n", r.ok ? "pass" : "FAIL", r.name.c_str());
            if (!r.ok)
               allOk = false;
         }
         printf("%s\n", allOk ? "MAPPING SWEEP OK" : "MAPPING SWEEP FAIL");
      }
}

void FrameTest_MATERIALSWEEPTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_MATERIALSWEEPTEST") != nullptr && frameId == 6)
      {
         struct MaterialProbeSource : public IGeometrySource
         {
            IGeometrySource* wrapped = nullptr;
            Material material;
            const Mesh& GetMesh() override { return wrapped->GetMesh(); }
            unsigned long long MeshRevision() override { return wrapped->MeshRevision(); }
            Mat4 GetModelMatrix() const override { return wrapped->GetModelMatrix(); }
            Material GetMaterial() const override { return material; }
            unsigned int GetSurfaceTexture() override { return wrapped->GetSurfaceTexture(); }
            MappingTransform GetMappingTransform() const override { return wrapped->GetMappingTransform(); }
         };

         GeometryNode probeMesh;
         probeMesh.shape = 1; // cube
         probeMesh.detail = 4;
         MaterialProbeSource probe;
         probe.wrapped = &probeMesh;
         // Distinctive on every field, not just colour, so a node that
         // forwards color[] but drops e.g. clearcoat still fails.
         probe.material.color[0] = 0.11f; probe.material.color[1] = 0.62f; probe.material.color[2] = 0.33f;
         probe.material.metallic = 0.71f;
         probe.material.roughness = 0.82f;
         probe.material.opacity = 0.63f;
         probe.material.shading = 2;
         probe.material.emissionColor[0] = 0.44f; probe.material.emissionColor[1] = 0.15f; probe.material.emissionColor[2] = 0.88f;
         probe.material.emission = 1.7f;
         probe.material.ior = 1.9f;
         probe.material.transmission = 0.35f;
         probe.material.transmissionRoughness = 0.28f;
         probe.material.specular = 0.19f;
         probe.material.clearcoat = 0.53f;
         probe.material.clearcoatRoughness = 0.47f;
         probe.material.subsurface = 0.61f;
         probe.material.subsurfaceColor[0] = 0.77f; probe.material.subsurfaceColor[1] = 0.22f; probe.material.subsurfaceColor[2] = 0.09f;
         probe.material.subsurfaceRadius = 0.38f;
         probe.material.sheen = 0.44f;
         probe.material.sheenColor[0] = 0.66f; probe.material.sheenColor[1] = 0.11f; probe.material.sheenColor[2] = 0.99f;
         probe.material.sheenRoughness = 0.29f;
         probe.material.iridescence = 0.31f;
         probe.material.iridescenceIor = 1.6f;
         probe.material.iridescenceThickness = 512.0f;
         probe.material.anisotropy = 0.4f;
         probe.material.anisotropyRotation = 0.25f;
         probe.material.dispersion = 0.18f;
         probe.material.alphaCutoff = 0.4f;
         probe.material.textureAlpha = true;
         probe.material.normalStrength = 1.7f;
         probe.material.normalBump = 1;

         int frame = 22000;
         auto cook = [&](IGeometrySource* g) {
            if (auto* n = dynamic_cast<INode*>(g)) n->CookIfNeeded(frame);
            frame++;
         };
         auto matches = [&](const Material& a, const Material& b) {
            auto near = [](float x, float y) { return std::fabs(x - y) < 1e-4f; };
            auto near3 = [&](const float* x, const float* y) { return near(x[0], y[0]) && near(x[1], y[1]) && near(x[2], y[2]); };
            return near3(a.color, b.color) && near(a.metallic, b.metallic) && near(a.roughness, b.roughness) &&
                   near(a.opacity, b.opacity) && a.shading == b.shading &&
                   near3(a.emissionColor, b.emissionColor) && near(a.emission, b.emission) &&
                   near(a.ior, b.ior) && near(a.transmission, b.transmission) &&
                   near(a.transmissionRoughness, b.transmissionRoughness) && near(a.specular, b.specular) &&
                   near(a.clearcoat, b.clearcoat) && near(a.clearcoatRoughness, b.clearcoatRoughness) &&
                   near(a.subsurface, b.subsurface) && near3(a.subsurfaceColor, b.subsurfaceColor) &&
                   near(a.subsurfaceRadius, b.subsurfaceRadius) && near(a.sheen, b.sheen) &&
                   near3(a.sheenColor, b.sheenColor) && near(a.sheenRoughness, b.sheenRoughness) &&
                   near(a.iridescence, b.iridescence) && near(a.iridescenceIor, b.iridescenceIor) &&
                   near(a.iridescenceThickness, b.iridescenceThickness) && near(a.anisotropy, b.anisotropy) &&
                   near(a.anisotropyRotation, b.anisotropyRotation) && near(a.dispersion, b.dispersion) &&
                   near(a.alphaCutoff, b.alphaCutoff) && a.textureAlpha == b.textureAlpha &&
                   near(a.normalStrength, b.normalStrength) && a.normalBump == b.normalBump;
         };

         struct Result { std::string name; bool ok; };
         std::vector<Result> results;
         auto checkForwarding = [&](const char* name, IGeometrySource* node)
         {
            cook(node);
            results.push_back({ name, matches(node->GetMaterial(), probe.material) });
         };

         GeometryOpNode opNode; opNode.op = GeometryOpNode::kTransform; opNode.input = &probe;
         checkForwarding("GeometryOpNode", &opNode);

         DisplacementNode dispNode; dispNode.input = &probe;
         checkForwarding("DisplacementNode", &dispNode);

         AudioDisplacementNode adispNode; adispNode.input = &probe;
         checkForwarding("AudioDisplacementNode", &adispNode);

         SetColorNode setColorNode; setColorNode.input = &probe;
         checkForwarding("SetColorNode", &setColorNode);

         MeshResynthNode resynthNode; resynthNode.input = &probe;
         checkForwarding("MeshResynthNode", &resynthNode);

         MeshToPointsNode m2pNode; m2pNode.input = &probe; m2pNode.mode = 0;
         checkForwarding("MeshToPointsNode", &m2pNode);

         Null3DNode nullNode; nullNode.input = &probe;
         checkForwarding("Null3DNode", &nullNode);

         // MaterialNode is excluded from this sweep entirely: unbypassed, it
         // originates A from its own params by design (same reason MappingNode
         // is excluded from MAPPINGSWEEPTEST); bypassed, it fails the one-input
         // CanBypass() rule (it has a mesh input plus per-map texture pins), so
         // it can never actually be bypassed in the live graph - any stale
         // bypassed=true gets force-cleared on load - and MaterialNode::GetMaterial
         // no longer has a forwarding branch to exercise (removed as unreachable
         // dead code in 140221d). There is no reachable state left in which this
         // node forwards its input's material, so there is nothing here to sweep.

         MappingNode mapNode; mapNode.input = &probe;
         checkForwarding("MappingNode", &mapNode);

         JoinGeometryNode joinNode; joinNode.mode = JoinGeometryNode::kMerge; joinNode.inputs[0] = &probe; joinNode.materialFrom = 0;
         checkForwarding("JoinGeometryNode", &joinNode);

         DistributePointsOnFacesNode distFacesNode; distFacesNode.input = &probe;
         checkForwarding("DistributePointsOnFacesNode", &distFacesNode);

         PointsToVerticesNode p2vNode; p2vNode.input = &probe;
         checkForwarding("PointsToVerticesNode", &p2vNode);

         MergeByDistanceNode mergeNode; mergeNode.input = &probe; mergeNode.threshold = 0.0f;
         checkForwarding("MergeByDistanceNode", &mergeNode);

         GeometryNode probe2;
         probe2.shape = 2; // sphere
         probe2.detail = 4;
         WrapNode wrapNode; wrapNode.sourceInput = &probe; wrapNode.targetInput = &probe2; wrapNode.blend = 0.0f;
         checkForwarding("WrapNode", &wrapNode);

         ClothNode clothNode;
         clothNode.input = &probe;
         clothNode.pinMode = ClothNode::kPinNone;
         clothNode.gravityX = clothNode.gravityY = clothNode.gravityZ = 0.0f;
         clothNode.windX = clothNode.windY = clothNode.windZ = 0.0f;
         checkForwarding("ClothNode", &clothNode);

         Switcher3DNode sw3Node;
         sw3Node.inputs[0] = &probe;
         sw3Node.manual = true;
         sw3Node.manualSlot = 0;
         checkForwarding("Switcher3DNode", &sw3Node);

         GeometryNode instPoints;
         instPoints.shape = 1; // cube
         instPoints.detail = 2;
         InstanceOnPointsNode instNode;
         instNode.pointSource = &instPoints;
         instNode.instanceShape = &probe;
         instNode.pointMode = 0;
         instNode.maxPoints = 50;
         instNode.instanceScale = 1.0f;
         instNode.scaleRandom = 0.0f;
         checkForwarding("InstanceOnPointsNode", &instNode);

         bool allOk = true;
         for (const Result& r : results)
         {
            printf("  [%s] %-24s\n", r.ok ? "pass" : "FAIL", r.name.c_str());
            if (!r.ok)
               allOk = false;
         }
         printf("%s\n", allOk ? "MATERIAL SWEEP OK" : "MATERIAL SWEEP FAIL");
      }
}

void FrameTest_TEXTURESWEEPTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_TEXTURESWEEPTEST") != nullptr && frameId == 6)
      {
         struct TextureProbeSource : public IGeometrySource
         {
            IGeometrySource* wrapped = nullptr;
            unsigned int texture = 0;
            const Mesh& GetMesh() override { return wrapped->GetMesh(); }
            unsigned long long MeshRevision() override { return wrapped->MeshRevision(); }
            Mat4 GetModelMatrix() const override { return wrapped->GetModelMatrix(); }
            Material GetMaterial() const override { return wrapped->GetMaterial(); }
            unsigned int GetSurfaceTexture() override { return texture; }
         };

         GeometryNode probeMesh;
         probeMesh.shape = 1; // cube
         probeMesh.detail = 4;
         TextureProbeSource probe;
         probe.wrapped = &probeMesh;
         // A GL handle unlikely to collide with anything a headless test run
         // actually allocates, and never zero (which means "no texture").
         probe.texture = 0xBEEF1234u;

         int frame = 23000;
         auto cook = [&](IGeometrySource* g) {
            if (auto* n = dynamic_cast<INode*>(g)) n->CookIfNeeded(frame);
            frame++;
         };

         struct Result { std::string name; bool ok; };
         std::vector<Result> results;
         auto checkForwarding = [&](const char* name, IGeometrySource* node)
         {
            cook(node);
            results.push_back({ name, node->GetSurfaceTexture() == probe.texture });
         };

         GeometryOpNode opNode; opNode.op = GeometryOpNode::kTransform; opNode.input = &probe;
         checkForwarding("GeometryOpNode", &opNode);

         DisplacementNode dispNode; dispNode.input = &probe;
         checkForwarding("DisplacementNode", &dispNode);

         AudioDisplacementNode adispNode; adispNode.input = &probe;
         checkForwarding("AudioDisplacementNode", &adispNode);

         SetColorNode setColorNode; setColorNode.input = &probe;
         checkForwarding("SetColorNode", &setColorNode);

         MeshResynthNode resynthNode; resynthNode.input = &probe;
         checkForwarding("MeshResynthNode", &resynthNode);

         MeshToPointsNode m2pNode; m2pNode.input = &probe; m2pNode.mode = 0;
         checkForwarding("MeshToPointsNode", &m2pNode);

         Null3DNode nullNode; nullNode.input = &probe;
         checkForwarding("Null3DNode", &nullNode);

         // Unlike MATERIALSWEEPTEST, no bypassed=true needed here: an
         // unconnected material map always falls through to the input
         // regardless of bypass (UtilityNodes.cpp:104-106).
         MaterialNode matNode; matNode.input = &probe;
         checkForwarding("MaterialNode", &matNode);

         MappingNode mapNode; mapNode.input = &probe;
         checkForwarding("MappingNode", &mapNode);

         JoinGeometryNode joinNode; joinNode.mode = JoinGeometryNode::kMerge; joinNode.inputs[0] = &probe; joinNode.materialFrom = 0;
         checkForwarding("JoinGeometryNode", &joinNode);

         DistributePointsOnFacesNode distFacesNode; distFacesNode.input = &probe;
         checkForwarding("DistributePointsOnFacesNode", &distFacesNode);

         PointsToVerticesNode p2vNode; p2vNode.input = &probe;
         checkForwarding("PointsToVerticesNode", &p2vNode);

         MergeByDistanceNode mergeNode; mergeNode.input = &probe; mergeNode.threshold = 0.0f;
         checkForwarding("MergeByDistanceNode", &mergeNode);

         GeometryNode probe2;
         probe2.shape = 2; // sphere
         probe2.detail = 4;
         WrapNode wrapNode; wrapNode.sourceInput = &probe; wrapNode.targetInput = &probe2; wrapNode.blend = 0.0f;
         checkForwarding("WrapNode", &wrapNode);

         ClothNode clothNode;
         clothNode.input = &probe;
         clothNode.pinMode = ClothNode::kPinNone;
         clothNode.gravityX = clothNode.gravityY = clothNode.gravityZ = 0.0f;
         clothNode.windX = clothNode.windY = clothNode.windZ = 0.0f;
         checkForwarding("ClothNode", &clothNode);

         Switcher3DNode sw3Node;
         sw3Node.inputs[0] = &probe;
         sw3Node.manual = true;
         sw3Node.manualSlot = 0;
         checkForwarding("Switcher3DNode", &sw3Node);

         GeometryNode instPoints;
         instPoints.shape = 1; // cube
         instPoints.detail = 2;
         InstanceOnPointsNode instNode;
         instNode.pointSource = &instPoints;
         instNode.instanceShape = &probe;
         instNode.pointMode = 0;
         instNode.maxPoints = 50;
         instNode.instanceScale = 1.0f;
         instNode.scaleRandom = 0.0f;
         checkForwarding("InstanceOnPointsNode", &instNode);

         bool allOk = true;
         for (const Result& r : results)
         {
            printf("  [%s] %-24s\n", r.ok ? "pass" : "FAIL", r.name.c_str());
            if (!r.ok)
               allOk = false;
         }
         printf("%s\n", allOk ? "TEXTURE SWEEP OK" : "TEXTURE SWEEP FAIL");
      }
}

void FrameTest_POINTCLOUDSWEEPTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_POINTCLOUDSWEEPTEST") != nullptr && frameId == 6)
      {
         struct CurveProbe : public IGeometrySource
         {
            Polyline line;
            const Mesh& GetMesh() override { static Mesh empty; return empty; }
            unsigned long long MeshRevision() override { return 0; }
            Mat4 GetModelMatrix() const override { return Mat4::Identity(); }
            Material GetMaterial() const override { return Material(); }
            const Polyline* GetCurve() override { return &line; }
            unsigned long long CurveStamp() override { return 7; }
         };

         GeometryNode cube;
         cube.shape = 1;
         cube.detail = 2;
         MeshToPointsNode pts;
         pts.input = &cube;
         pts.mode = 0;
         pts.CookIfNeeded(24000);

         GeometryOpNode xf;
         xf.op = GeometryOpNode::kTransform;
         xf.input = &pts;
         xf.offsetX = 0.0f;

         bool allOk = true;
         auto report = [&](const char* name, bool ok) {
            printf("  [%s] %s\n", ok ? "pass" : "FAIL", name);
            if (!ok) allOk = false;
         };

         const std::vector<Particle>* src = pts.GetPointCloud();
         const std::vector<Particle> before = xf.GetPointCloud() ? *xf.GetPointCloud() : std::vector<Particle>();
         report("cloud reaches Transform's output", src != nullptr && !src->empty() && before.size() == src->size());

         const unsigned long long rev1 = xf.PointCloudRevision();
         const unsigned long long rev2 = xf.PointCloudRevision();
         report("revision stable while nothing changes", rev1 == rev2);

         xf.offsetX = 4.0f;
         const std::vector<Particle>* movedPtr = xf.GetPointCloud();
         bool exact = movedPtr != nullptr && movedPtr->size() == before.size() && !before.empty();
         for (size_t i = 0; exact && i < before.size(); i++)
            exact = std::fabs(((*movedPtr)[i].px - before[i].px) - 4.0f) < 1e-4f &&
                    std::fabs((*movedPtr)[i].py - before[i].py) < 1e-4f &&
                    std::fabs((*movedPtr)[i].pz - before[i].pz) < 1e-4f;
         report("kTransform moves every point by exactly the offset", exact);
         report("revision moves when the offset changes", xf.PointCloudRevision() != rev1);

         xf.bypassed = true;
         const std::vector<Particle>* bypassedPtr = xf.GetPointCloud();
         report("bypassed Transform passes the input cloud through untouched", bypassedPtr == src);
         xf.bypassed = false;

         GeometryOpNode arr;
         arr.op = GeometryOpNode::kArray;
         arr.input = &pts;
         report("non-Transform op forwards the cloud unchanged", arr.GetPointCloud() == src);

         CurveProbe curve;
         curve.line.points = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f };
         GeometryOpNode curveXf;
         curveXf.op = GeometryOpNode::kTransform;
         curveXf.input = &curve;
         curveXf.offsetX = 0.0f;
         const Polyline* curveId = curveXf.GetCurve();
         report("curve survives an identity Transform with its points intact",
                curveId != nullptr && curveId->points == curve.line.points);
         const unsigned long long curveStamp1 = curveXf.CurveStamp();
         report("curve stamp stable while nothing changes", curveStamp1 == curveXf.CurveStamp());
         curveXf.offsetX = 4.0f; curveXf.offsetY = 7.0f; curveXf.offsetZ = 3.0f;
         const Polyline* curveMoved = curveXf.GetCurve();
         bool curveExact = curveMoved != nullptr && curveMoved->points.size() == curve.line.points.size() &&
                           curveMoved->closed == curve.line.closed;
         for (size_t i = 0; curveExact && i + 2 < curve.line.points.size(); i += 3)
            curveExact = std::fabs(curveMoved->points[i]     - (curve.line.points[i]     + 4.0f)) < 1e-4f &&
                         std::fabs(curveMoved->points[i + 1] - (curve.line.points[i + 1] + 7.0f)) < 1e-4f &&
                         std::fabs(curveMoved->points[i + 2] - (curve.line.points[i + 2] + 3.0f)) < 1e-4f;
         report("kTransform moves every curve point by exactly the offset", curveExact);
         report("curve stamp moves when the offset changes", curveXf.CurveStamp() != curveStamp1);
         curveXf.bypassed = true;
         report("bypassed Transform passes the curve through untouched", curveXf.GetCurve() == &curve.line);
         curveXf.bypassed = false;
         GeometryOpNode curveArr;
         curveArr.op = GeometryOpNode::kArray;
         curveArr.input = &curve;
         report("non-Transform op forwards the curve unchanged",
                curveArr.GetCurve() == &curve.line && curveArr.CurveStamp() == 7);

         GeometryOpNode bare;
         report("no input -> no cloud, no curve", bare.GetPointCloud() == nullptr && bare.GetCurve() == nullptr);

         // R484: a vertices-only mesh is vertices, not "nothing".
         struct VertsProbe : public IGeometrySource
         {
            Mesh mesh;
            const Mesh& GetMesh() override { return mesh; }
            unsigned long long MeshRevision() override { return 1; }
            Mat4 GetModelMatrix() const override { return Mat4::Identity(); }
            Material GetMaterial() const override { return Material(); }
         };
         VertsProbe verts;
         verts.mesh.vertices.resize(3);
         const std::string vertsMsg = DescribeGeometryMismatch(&verts, GeometryRequirement::kMeshSurface);
         report("vertices-only input reported as vertices, not nothing",
                vertsMsg.find("vertices only") != std::string::npos);
         report("vertices-only input satisfies kMeshVertices",
                DescribeGeometryMismatch(&verts, GeometryRequirement::kMeshVertices).empty());

         // Pin contract: points-only and curve-only nodes flag the wrong
         // domain and make nothing from it.
         {
            DelaunayMeshNode dn;
            dn.input = &verts;
            const bool meshEmpty = dn.GetMesh().vertices.empty();
            report("Delaunay Mesh flags a mesh and ignores it",
                   meshEmpty && dn.CookWarning().find("point cloud") != std::string::npos);
            dn.input = &pts;
            dn.GetMesh();
            report("Delaunay Mesh accepts a point cloud without warning", dn.CookWarning().empty());
            VoronoiCellsNode vn;
            vn.input = &verts;
            vn.GetMesh();
            report("Voronoi Cells flags a mesh", !vn.CookWarning().empty());
            CurveOpsNode cn;
            cn.input = &verts;
            const bool curveEmpty = cn.GetCurve() == nullptr;
            report("Curve Ops flags a mesh and emits no curve",
                   curveEmpty && cn.CookWarning().find("curve") != std::string::npos);
            cn.input = &pts;
            cn.GetCurve();
            report("Curve Ops flags a point cloud", !cn.CookWarning().empty());
            cn.input = &curve;
            cn.GetCurve();
            report("Curve Ops accepts a curve without warning", cn.CookWarning().empty());
         }

         // R484: Switcher 3D and Set Color forward cloud / curve.
         Switcher3DNode sw;
         sw.manual = true; sw.manualSlot = 1;
         sw.inputs[1] = &pts;
         report("Switcher 3D forwards the active slot's cloud",
                sw.GetPointCloud() == pts.GetPointCloud() && sw.PointCloudRevision() == pts.PointCloudRevision());
         sw.inputs[2] = &curve; sw.manualSlot = 2;
         report("Switcher 3D forwards the active slot's curve",
                sw.GetCurve() == &curve.line && sw.CurveStamp() == 7);
         SetColorNode setCol; setCol.input = &curve;
         report("Set Color forwards the curve", setCol.GetCurve() == &curve.line && setCol.CurveStamp() == 7);

         // R505: a passthrough must not reset the cloud's base size to 1.
         struct BaseSizeProbe : public CurveProbe
         {
            float PointBaseSize() const override { return 3.0f; }
         };
         BaseSizeProbe sized;
         MaterialNode bsMat; bsMat.input = &sized;
         Null3DNode bsNull; bsNull.input = &sized;
         MappingNode bsMap; bsMap.input = &sized;
         SetColorNode bsSet; bsSet.input = &sized;
         GeometryOpNode bsOp; bsOp.input = &sized;
         ClothNode bsCloth; bsCloth.input = &sized;
         report("Material/Null3D/Mapping/SetColor/GeometryOp/Cloth forward PointBaseSize",
                bsMat.PointBaseSize() == 3.0f && bsNull.PointBaseSize() == 3.0f &&
                bsMap.PointBaseSize() == 3.0f && bsSet.PointBaseSize() == 3.0f &&
                bsOp.PointBaseSize() == 3.0f && bsCloth.PointBaseSize() == 3.0f);

         // R482: tint is baked into the colour, so the albedo is neutral.
         DepthProjectionNode depthProj; depthProj.tint[0] = 0.5f;
         ImageToPointsNode img2pts; img2pts.tint[0] = 0.5f;
         report("Depth Projection / Image to Points report neutral albedo",
                depthProj.GetMaterial().color[0] == 1.0f && img2pts.GetMaterial().color[0] == 1.0f);

         // R483: a material change upstream must rebuild Distribute on Faces.
         MaterialNode matUp; matUp.input = &cube;
         matUp.color[0] = 1.0f; matUp.color[1] = 0.0f; matUp.color[2] = 0.0f;
         DistributePointsOnFacesNode dist; dist.input = &matUp; dist.inheritMaterial = true;
         dist.CookIfNeeded(24100);
         const std::vector<Particle>* d1 = dist.GetPointCloud();
         const float redBefore = (d1 && !d1->empty()) ? (*d1)[0].g : -1.0f;
         matUp.color[1] = 1.0f;
         dist.CookIfNeeded(24101);
         const std::vector<Particle>* d2 = dist.GetPointCloud();
         report("Distribute on Faces re-bakes when the upstream material changes",
                d2 && !d2->empty() && redBefore == 0.0f && (*d2)[0].g == 1.0f);

         printf("%s\n", allOk ? "POINTCLOUD SWEEP OK" : "POINTCLOUD SWEEP FAIL");
      }
}

void FrameTest_COLOURSWEEPTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_COLOURSWEEPTEST") != nullptr && frameId == 6)
      {
         struct ColourMeshProbeSource : public IGeometrySource
         {
            IGeometrySource* wrapped = nullptr;
            bool colourful = false;
            Mesh mColoured;
            const Mesh& GetMesh() override
            {
               if (!colourful)
                  return wrapped->GetMesh();
               mColoured = wrapped->GetMesh();
               mColoured.vertexColor.assign(mColoured.vertices.size() * 3, 0.0f);
               for (size_t i = 0; i < mColoured.vertices.size(); i++)
               {
                  mColoured.vertexColor[i * 3 + 0] = 0.2f;
                  mColoured.vertexColor[i * 3 + 1] = 0.6f;
                  mColoured.vertexColor[i * 3 + 2] = 0.9f;
               }
               return mColoured;
            }
            unsigned long long MeshRevision() override
            {
               return wrapped->MeshRevision() * 2 + (colourful ? 1 : 0);
            }
            Mat4 GetModelMatrix() const override { return wrapped->GetModelMatrix(); }
            Material GetMaterial() const override { return wrapped->GetMaterial(); }
            unsigned int GetSurfaceTexture() override { return wrapped->GetSurfaceTexture(); }
         };

         struct ColourCloudProbeSource : public IGeometrySource
         {
            Mesh mEmpty;
            bool colourful = false;
            std::vector<Particle> mPoints;
            unsigned long long mRevision = 1;
            const Mesh& GetMesh() override { return mEmpty; }
            unsigned long long MeshRevision() override { return 0; }
            Mat4 GetModelMatrix() const override { return Mat4::Identity(); }
            Material GetMaterial() const override { return Material(); }
            const std::vector<Particle>* GetPointCloud() override
            {
               mPoints.clear();
               for (int i = 0; i < 8; i++)
               {
                  Particle p;
                  p.px = (float)i; p.py = 0.0f; p.pz = 0.0f;
                  p.nx = 0.0f; p.ny = 1.0f; p.nz = 0.0f;
                  p.scale = 1.0f;
                  p.alive = true;
                  if (colourful) { p.r = 0.3f; p.g = 0.7f; p.b = 0.1f; p.hasColor = true; }
                  mPoints.push_back(p);
               }
               return &mPoints;
            }
            unsigned long long PointCloudRevision() override { return mRevision + (colourful ? 1000000ull : 0ull); }
         };

         GeometryNode probeMesh;
         probeMesh.shape = 1; // cube
         probeMesh.detail = 4;
         ColourMeshProbeSource meshProbe;
         meshProbe.wrapped = &probeMesh;
         ColourCloudProbeSource cloudProbe;

         int frame = 24000;
         auto cook = [&](IGeometrySource* g) {
            if (auto* n = dynamic_cast<INode*>(g)) n->CookIfNeeded(frame);
            frame++;
         };
         auto meshColour = [](const Mesh& m) -> bool { return m.HasVertexColor(); };
         auto meshRgbOk = [](const Mesh& m) -> bool {
            if (!m.HasVertexColor() || m.vertices.empty())
               return false;
            for (size_t i = 0; i < m.vertices.size(); i++)
            {
               if (std::fabs(m.vertexColor[i * 3 + 0] - 0.2f) > 1e-3f) return false;
               if (std::fabs(m.vertexColor[i * 3 + 1] - 0.6f) > 1e-3f) return false;
               if (std::fabs(m.vertexColor[i * 3 + 2] - 0.9f) > 1e-3f) return false;
            }
            return true;
         };

         struct Result { std::string name; bool ok; bool skip; const char* skipReason; std::string note; };
         std::vector<Result> results;

         // Colourless-in-colourless-out AND coloured-in-coloured-out, for a
         // node whose GetMesh() should forward Mesh::vertexColor untouched.
         auto checkMeshForwarding = [&](const char* name, IGeometrySource* node)
         {
            meshProbe.colourful = false;
            cook(node);
            const Mesh& colourless = node->GetMesh();
            const bool colourlessOk = !meshColour(colourless);

            meshProbe.colourful = true;
            cook(node);
            const Mesh& coloured = node->GetMesh();
            const bool colouredOk = meshRgbOk(coloured);

            if (!colourlessOk)
               results.push_back({ std::string(name) + " (colourless-in)", false, false, nullptr });
            else
               results.push_back({ std::string(name) + " (colourless-in)", true, false, nullptr });
            results.push_back({ std::string(name) + " (coloured-in)", colouredOk, false, nullptr });
         };

         GeometryOpNode opNode; opNode.op = GeometryOpNode::kTransform; opNode.input = &meshProbe;
         checkMeshForwarding("GeometryOpNode", &opNode);

         DisplacementNode dispNode; dispNode.input = &meshProbe;
         checkMeshForwarding("DisplacementNode", &dispNode);

         AudioDisplacementNode adispNode; adispNode.input = &meshProbe;
         checkMeshForwarding("AudioDisplacementNode", &adispNode);

         MeshResynthNode resynthNode; resynthNode.input = &meshProbe;
         checkMeshForwarding("MeshResynthNode", &resynthNode);

         Null3DNode nullNode; nullNode.input = &meshProbe;
         checkMeshForwarding("Null3DNode", &nullNode);

         MaterialNode matNode; matNode.input = &meshProbe;
         checkMeshForwarding("MaterialNode", &matNode);

         MappingNode mapNode; mapNode.input = &meshProbe;
         checkMeshForwarding("MappingNode", &mapNode);

         // JoinGeometryNode's kMerge is NOT a plain forwarding passthrough:
         // a merge only bakes per-vertex colour when it genuinely has more
         // than one colour to carry - some input already has real vertex
         // colour, or the inputs' albedos differ (see the bakePerInputColour
         // decision in JoinGeometryNode::RebuildIfNeeded, UtilityNodes.cpp).
         // A colourless merge with nothing to distinguish must come out
         // colourless, same as the generic "colourless stays colourless"
         // contract checkMeshForwarding asserts elsewhere - a merge that
         // unconditionally filled vertexColor from its inputs' material
         // albedo regardless of that decision was a real bug: two merges
         // feeding a third through Material nodes rendered the first merge's
         // manufactured colour and ignored the Material nodes entirely
         // (`join -> material(red) -> join -> render` came out white).
         {
            struct ColourAlbedoProbeSource : public IGeometrySource
            {
               IGeometrySource* wrapped = nullptr;
               Material material;
               const Mesh& GetMesh() override { return wrapped->GetMesh(); }
               unsigned long long MeshRevision() override { return wrapped->MeshRevision(); }
               Mat4 GetModelMatrix() const override { return wrapped->GetModelMatrix(); }
               Material GetMaterial() const override { return material; }
               unsigned int GetSurfaceTexture() override { return wrapped->GetSurfaceTexture(); }
               MappingTransform GetMappingTransform() const override { return wrapped->GetMappingTransform(); }
            };

            ColourAlbedoProbeSource probeRed;
            probeRed.wrapped = &probeMesh;
            probeRed.material.color[0] = 0.9f; probeRed.material.color[1] = 0.1f; probeRed.material.color[2] = 0.1f;

            // Same albedo on both inputs: nothing to carry per-vertex, so the
            // merge must stay colourless - this is the exact shape of the
            // reported bug (a colourless mesh coming out "coloured").
            JoinGeometryNode joinSame;
            joinSame.mode = JoinGeometryNode::kMerge;
            joinSame.inputs[0] = &probeRed;
            joinSame.inputs[1] = &probeRed;
            cook(&joinSame);
            const bool sameOk = !meshColour(joinSame.GetMesh());
            results.push_back({ "JoinGeometryNode (equal albedos)", sameOk, false, nullptr,
                                 sameOk ? "" : "invented vertexColor from equal-albedo inputs" });

            // Two inputs whose albedos genuinely differ is the case merge's
            // per-input colour exists for, so here vertexColor *must* appear
            // - and must carry each input's own colour, not one shared
            // albedo.
            ColourAlbedoProbeSource probeBlue;
            probeBlue.wrapped = &probeMesh;
            probeBlue.material.color[0] = 0.1f; probeBlue.material.color[1] = 0.1f; probeBlue.material.color[2] = 0.9f;

            JoinGeometryNode joinDiff;
            joinDiff.mode = JoinGeometryNode::kMerge;
            joinDiff.inputs[0] = &probeRed;
            joinDiff.inputs[1] = &probeBlue;
            cook(&joinDiff);
            const Mesh& mixed = joinDiff.GetMesh();
            bool mixedOk = mixed.HasVertexColor();
            std::string mixedNote = mixedOk ? "" : "dropped per-input colour when albedos differ";
            if (mixedOk)
            {
               // First vertex belongs to input 0 (red), last to input 1 (blue).
               const size_t last = mixed.vertices.size() - 1;
               const bool firstRed = mixed.vertexColor[0] > 0.5f && mixed.vertexColor[2] < 0.5f;
               const bool lastBlue = mixed.vertexColor[last * 3 + 2] > 0.5f && mixed.vertexColor[last * 3 + 0] < 0.5f;
               if (!firstRed || !lastBlue)
               {
                  mixedOk = false;
                  mixedNote = "merged parts did not keep their own colours";
               }
               // Colour now lives in the vertices, so the reported albedo has
               // to be neutral or the shader multiplies materialFrom's colour
               // in a second time and tints the other part by it.
               const Material joined = joinDiff.GetMaterial();
               if (mixedOk && (joined.color[0] < 0.99f || joined.color[1] < 0.99f || joined.color[2] < 0.99f))
               {
                  mixedOk = false;
                  mixedNote = "baked per-input colour but still reports a tinted albedo";
               }
            }
            results.push_back({ "JoinGeometryNode (differing albedos)", mixedOk, false, nullptr, mixedNote });

            // The exact shape the user hit: merge -> material -> merge. The
            // downstream material must still decide the colour.
            JoinGeometryNode innerA, innerB;
            innerA.mode = JoinGeometryNode::kMerge; innerA.inputs[0] = &probeRed;
            innerB.mode = JoinGeometryNode::kMerge; innerB.inputs[0] = &probeRed;
            MaterialNode whiteMat, redMat;
            whiteMat.input = &innerA;
            whiteMat.color[0] = whiteMat.color[1] = whiteMat.color[2] = 1.0f;
            redMat.input = &innerB;
            redMat.color[0] = 0.9f; redMat.color[1] = 0.05f; redMat.color[2] = 0.05f;
            JoinGeometryNode outer;
            outer.mode = JoinGeometryNode::kMerge;
            outer.inputs[0] = &whiteMat;
            outer.inputs[1] = &redMat;
            cook(&outer);
            const Mesh& nested = outer.GetMesh();
            bool nestedOk = nested.HasVertexColor();
            std::string nestedNote = nestedOk ? "" : "nested merge lost the downstream materials entirely";
            if (nestedOk)
            {
               const size_t last = nested.vertices.size() - 1;
               const bool firstWhite = nested.vertexColor[0] > 0.9f && nested.vertexColor[2] > 0.9f;
               const bool lastRed = nested.vertexColor[last * 3 + 0] > 0.5f && nested.vertexColor[last * 3 + 2] < 0.5f;
               if (!firstWhite || !lastRed)
               {
                  nestedOk = false;
                  nestedNote = "downstream Material colour was ignored by the outer merge";
               }
            }
            results.push_back({ "JoinGeometryNode (merge -> material -> merge)", nestedOk, false, nullptr, nestedNote });

            // An instanced input: InstanceOnPoints always fills one colour
            // triple per instance (white when the source had none), so the
            // mere presence of instance colours must not count as authored
            // colour - or a merge of plain instances invents vertexColor and
            // freezes every downstream Material out.
            {
               GeometryNode instShape;
               instShape.shape = 1; // cube
               InstanceOnPointsNode plainInst;
               plainInst.pointSource = &probeMesh;
               plainInst.instanceShape = &instShape;
               plainInst.pointMode = 0; // vertices
               plainInst.maxPoints = 8;
               cook(&plainInst);

               JoinGeometryNode joinInst;
               joinInst.mode = JoinGeometryNode::kMerge;
               joinInst.inputs[0] = &plainInst;
               cook(&joinInst);
               const bool instOk = plainInst.InstanceCount() > 0 && !joinInst.GetMesh().Empty() &&
                                   !meshColour(joinInst.GetMesh());
               results.push_back({ "JoinGeometryNode (plain instances stay colourless)", instOk, false, nullptr,
                                    instOk ? "" : "merge invented vertexColor from white instance colours" });
            }
         }

         MergeByDistanceNode mergeNode; mergeNode.input = &meshProbe; mergeNode.threshold = 0.0f;
         checkMeshForwarding("MergeByDistanceNode", &mergeNode);

         GeometryNode probe2;
         probe2.shape = 2; // sphere
         probe2.detail = 4;
         WrapNode wrapNode; wrapNode.sourceInput = &meshProbe; wrapNode.targetInput = &probe2; wrapNode.blend = 0.0f;
         checkMeshForwarding("WrapNode", &wrapNode);

         // ClothNode deliberately skips a full rebuild when the input's mesh
         // *topology* (vertex/index count) hasn't changed, to keep the
         // simulation draping instead of snapping to rest on every re-cook -
         // see the comment at ClothNode::CookIfNeeded's topologyChanged
         // check. Toggling meshProbe.colourful alone doesn't change vertex
         // count, so reusing one ClothNode instance across both cooks (like
         // checkMeshForwarding does) would just read back the first cook's
         // cached mesh. Use one fresh instance per colour state instead, the
         // same way the chain fuzzer does, so each cook is a real rebuild.
         {
            auto makeCloth = [&]() {
               auto c = std::make_unique<ClothNode>();
               c->input = &meshProbe;
               c->pinMode = ClothNode::kPinNone;
               c->gravityX = c->gravityY = c->gravityZ = 0.0f;
               c->windX = c->windY = c->windZ = 0.0f;
               return c;
            };
            meshProbe.colourful = false;
            auto clothA = makeCloth();
            cook(clothA.get());
            results.push_back({ "ClothNode (colourless-in)", !meshColour(clothA->GetMesh()), false, nullptr });

            meshProbe.colourful = true;
            auto clothB = makeCloth();
            cook(clothB.get());
            results.push_back({ "ClothNode (coloured-in)", meshRgbOk(clothB->GetMesh()), false, nullptr });
         }

         Switcher3DNode sw3Node;
         sw3Node.inputs[0] = &meshProbe;
         sw3Node.manual = true;
         sw3Node.manualSlot = 0;
         checkMeshForwarding("Switcher3DNode", &sw3Node);

         FieldElementNode fieldElemNode;
         fieldElemNode.input = &meshProbe;
         checkMeshForwarding("FieldElementNode", &fieldElemNode);

         // SetColorNode is a paint operation, not a passthrough (same
         // reasoning as its unconditional hasColor=true in
         // GeometryOpNodes.cpp:1222-1223) - it always emits its own colour
         // regardless of the input's, so it gets its own check rather than
         // checkMeshForwarding's "colourless stays colourless" half.
         {
            meshProbe.colourful = false;
            SetColorNode setColorNode; setColorNode.input = &meshProbe;
            setColorNode.source = SetColorNode::kFlat;
            setColorNode.flatColor[0] = 0.2f; setColorNode.flatColor[1] = 0.6f; setColorNode.flatColor[2] = 0.9f;
            cook(&setColorNode);
            results.push_back({ "SetColorNode (paints own colour)", meshRgbOk(setColorNode.GetMesh()), false, nullptr });
         }

         // Mesh -> cloud crossing (D6's actual bug: PointsToVerticesNode and
         // the mesh->points direction both manufactured colour out of
         // colourless input before this session's D6 fix).
         auto checkMeshToCloud = [&](const char* name, IGeometrySource* node, std::function<const std::vector<Particle>*()> getCloud)
         {
            meshProbe.colourful = false;
            cook(node);
            const std::vector<Particle>* colourless = getCloud();
            bool colourlessOk = true;
            if (colourless)
               for (const Particle& p : *colourless)
                  if (p.hasColor) { colourlessOk = false; break; }

            meshProbe.colourful = true;
            cook(node);
            const std::vector<Particle>* coloured = getCloud();
            bool colouredOk = coloured != nullptr && !coloured->empty();
            if (coloured)
               for (const Particle& p : *coloured)
                  if (!p.hasColor || std::fabs(p.r - 0.2f) > 1e-3f || std::fabs(p.g - 0.6f) > 1e-3f || std::fabs(p.b - 0.9f) > 1e-3f)
                  { colouredOk = false; break; }

            results.push_back({ std::string(name) + " (colourless-in)", colourlessOk, false, nullptr });
            results.push_back({ std::string(name) + " (coloured-in)", colouredOk, false, nullptr });
         };

         // Both nodes tint their emitted particle colour by a material
         // colour (inheritMaterial=true by default, multiplying the probe's
         // own material into r/g/b) - neutralize that to identity so the
         // check below is actually reading the forwarded vertex colour
         // rather than that colour scaled by whatever the probe's default
         // material happens to be.
         MeshToPointsNode m2pNode; m2pNode.input = &meshProbe; m2pNode.mode = 0;
         m2pNode.inheritMaterial = false; m2pNode.color[0] = m2pNode.color[1] = m2pNode.color[2] = 1.0f;
         checkMeshToCloud("MeshToPointsNode", &m2pNode, [&]() { return m2pNode.GetPointCloud(); });

         DistributePointsOnFacesNode distFacesNode; distFacesNode.input = &meshProbe;
         distFacesNode.inheritMaterial = false; distFacesNode.color[0] = distFacesNode.color[1] = distFacesNode.color[2] = 1.0f;
         checkMeshToCloud("DistributePointsOnFacesNode", &distFacesNode, [&]() { return distFacesNode.GetPointCloud(); });

         // Cloud -> mesh crossing: PointsToVerticesNode's cloud branch,
         // PointDistributionNodes.cpp - the exact site D6 fixed
         // (previously emitted vertexColor for every particle, including a
         // colourless cloud, manufacturing colour from the white default).
         {
            cloudProbe.colourful = false;
            PointsToVerticesNode p2vNode; p2vNode.input = &cloudProbe;
            cook(&p2vNode);
            const bool colourlessOk = !meshColour(p2vNode.GetMesh());

            cloudProbe.colourful = true;
            cook(&p2vNode);
            // ColourCloudProbeSource paints its particles (0.3, 0.7, 0.1) -
            // a different fixed RGB than the mesh probe's (0.2, 0.6, 0.9) -
            // and PointsToVerticesNode forwards particle r/g/b untinted, so
            // check against the cloud's own values rather than meshRgbOk
            // (which asserts the mesh probe's colour and would legitimately
            // fail here even though nothing is being dropped).
            const Mesh& fromCloud = p2vNode.GetMesh();
            bool colouredOk = meshColour(fromCloud) && !fromCloud.vertices.empty();
            for (size_t i = 0; i < fromCloud.vertices.size() && colouredOk; i++)
            {
               if (std::fabs(fromCloud.vertexColor[i * 3 + 0] - 0.3f) > 1e-3f) colouredOk = false;
               if (std::fabs(fromCloud.vertexColor[i * 3 + 1] - 0.7f) > 1e-3f) colouredOk = false;
               if (std::fabs(fromCloud.vertexColor[i * 3 + 2] - 0.1f) > 1e-3f) colouredOk = false;
            }

            results.push_back({ "PointsToVerticesNode (colourless-in)", colourlessOk, false, nullptr });
            results.push_back({ "PointsToVerticesNode (coloured-in)", colouredOk, false, nullptr });
         }

         // MetaBallNode is a documented drop, not a bug: marching cubes
         // reads only cloud px/py/pz/scale, never r/g/b, so its output mesh
         // never carries vertexColor either way - both halves are expected
         // to report "no vertex colour", which is success here, not a skip.
         {
            cloudProbe.colourful = false;
            MetaBallNode metaNode; metaNode.cloudSource = &cloudProbe;
            metaNode.ballCount = 3; metaNode.resolution = 20; metaNode.threshold = 8.0f; metaNode.bounds = 4.0f;
            cook(&metaNode);
            const bool colourlessOk = !meshColour(metaNode.GetMesh());

            cloudProbe.colourful = true;
            cook(&metaNode);
            const bool neverManufactures = !meshColour(metaNode.GetMesh());

            results.push_back({ "MetaBallNode (never carries B, by design)", colourlessOk && neverManufactures, false, nullptr });
         }

         // Terminal originator: tint is an always-on authored channel (a
         // Color param, not a conditional connection), so unlike the "never
         // manufactures" producers below, DistributeInGrid legitimately
         // emits hasColor=true unconditionally - see PointDistributionNodes
         // where p.hasColor is set right after the tint assignment.
         {
            DistributePointsInGridNode gridNode;
            gridNode.countX = gridNode.countY = 4;
            gridNode.tint[0] = 0.4f; gridNode.tint[1] = 0.5f; gridNode.tint[2] = 0.6f;
            cook(&gridNode);
            const std::vector<Particle>* pts = gridNode.GetPointCloud();
            bool ok = pts != nullptr && !pts->empty();
            if (pts)
               for (const Particle& p : *pts)
                  if (!p.hasColor) { ok = false; break; }
            results.push_back({ "DistributePointsInGridNode (always authored)", ok, false, nullptr });
         }

         // Terminal, colour-agnostic producers: no colour concept at all, so
         // the only meaningful check is "never manufactures" - GetMesh()
         // should never carry vertexColor regardless of any other param.
         auto checkNeverManufactures = [&](const char* name, IGeometrySource* node)
         {
            cook(node);
            const Mesh& m = node->GetMesh();
            if (!m.HasGeometry())
            {
               results.push_back({ name, true, true, "produced no geometry in a headless run" });
               return;
            }
            results.push_back({ std::string(name) + " (never manufactures)", !meshColour(m), false, nullptr });
         };

         GeometryNode genNode; genNode.shape = 1; genNode.detail = 4;
         checkNeverManufactures("GeometryNode", &genNode);

         Text3DNode textNode;
         checkNeverManufactures("Text3DNode", &textNode);

         OceanNode oceanNode;
         checkNeverManufactures("OceanNode", &oceanNode);

         AudioRibbonNode ribbonNode;
         checkNeverManufactures("AudioRibbonNode", &ribbonNode);

         // FieldPrimitiveNode is excluded here, not another "never
         // manufactures" case: unlike the terminal producers above, it runs
         // a user-authored Field program, and its own default preset
         // ("Solid Terrain Plane", FieldPrimitiveNode.cpp) explicitly writes
         // `Cd = vec3(...)` - deliberate, program-authored colour, the same
         // class of thing as SetColorNode's paint or GeometryNode's
         // `colourful` toggle, not a default that leaked out unauthored.

         bool allOk = true;
         for (const Result& r : results)
         {
            if (r.skip)
            {
               printf("  [SKIP] %-40s — %s\n", r.name.c_str(), r.skipReason);
               continue;
            }
            printf("  [%s] %-44s %s\n", r.ok ? "pass" : "FAIL", r.name.c_str(), r.note.c_str());
            if (!r.ok)
               allOk = false;
         }
         printf("%s\n", allOk ? "COLOUR SWEEP OK" : "COLOUR SWEEP FAIL");
      }
}

void FrameTest_INSTANCESWEEPTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_INSTANCESWEEPTEST") != nullptr && frameId == 6)
      {
         GeometryNode pointsMesh;
         pointsMesh.shape = 1; // cube
         pointsMesh.detail = 4;
         GeometryNode stampMesh;
         stampMesh.shape = 2; // sphere
         stampMesh.detail = 3;

         InstanceOnPointsNode inst;
         inst.pointSource = &pointsMesh;
         inst.instanceShape = &stampMesh;
         inst.pointMode = 0; // vertices
         inst.maxPoints = 50;
         inst.instanceScale = 1.0f;
         inst.scaleRandom = 0.0f;
         inst.rotationRandom = 0.0f;
         inst.alignToNormal = false;

         // The wrapping Transform whose move has to survive the node under
         // test - see GeometryOpNode::GetInstanceGroupMatrix.
         GeometryOpNode groupXform;
         groupXform.op = GeometryOpNode::kTransform;
         groupXform.input = &inst;
         groupXform.offsetX = 5.0f;
         groupXform.offsetY = 7.0f;
         groupXform.offsetZ = 3.0f;

         int frame = 23000;
         auto cook = [&](IGeometrySource* g) {
            if (auto* n = dynamic_cast<INode*>(g)) n->CookIfNeeded(frame);
            frame++;
         };
         // Same walk as Render3DNode::FindInstancer / NodeViewport's copy.
         auto reachesInstancer = [](IGeometrySource* s) {
            for (; s != nullptr; s = s->PassthroughSource())
               if (dynamic_cast<InstanceOnPointsNode*>(s) != nullptr)
                  return true;
            return false;
         };

         struct Result { std::string name; bool foundInstancer; bool groupOk; };
         std::vector<Result> results;
         // `node` is wired downstream of groupXform, so one check covers both
         // invariants at once: the chain walk has to reach the instancer
         // through it, and the Transform's 5/7/3 move has to arrive intact.
         auto checkPassthrough = [&](const char* name, IGeometrySource* node)
         {
            cook(node);
            const Mat4 g = node->GetInstanceGroupMatrix();
            const bool groupOk = std::fabs(g.m[12] - 5.0f) < 1e-3f &&
                                 std::fabs(g.m[13] - 7.0f) < 1e-3f &&
                                 std::fabs(g.m[14] - 3.0f) < 1e-3f;
            results.push_back({ name, reachesInstancer(node), groupOk });
         };

         GeometryOpNode opNode; opNode.op = GeometryOpNode::kSubdivide; opNode.input = &groupXform;
         checkPassthrough("GeometryOpNode", &opNode);

         Null3DNode nullNode; nullNode.input = &groupXform;
         checkPassthrough("Null3DNode", &nullNode);

         DisplacementNode dispNode; dispNode.input = &groupXform;
         checkPassthrough("DisplacementNode", &dispNode);

         MaterialNode matNode; matNode.input = &groupXform;
         checkPassthrough("MaterialNode", &matNode);

         SetColorNode setColorNode; setColorNode.input = &groupXform;
         checkPassthrough("SetColorNode", &setColorNode);

         MergeByDistanceNode mergeNode; mergeNode.input = &groupXform; mergeNode.threshold = 0.0f;
         checkPassthrough("MergeByDistanceNode", &mergeNode);

         GeometryNode wrapTarget;
         wrapTarget.shape = 2; // sphere
         wrapTarget.detail = 4;
         WrapNode wrapNode; wrapNode.sourceInput = &groupXform; wrapNode.targetInput = &wrapTarget;
         wrapNode.blend = 0.0f;
         checkPassthrough("WrapNode", &wrapNode);

         Switcher3DNode sw3Node;
         sw3Node.inputs[0] = &groupXform;
         sw3Node.manual = true;
         sw3Node.manualSlot = 0;
         checkPassthrough("Switcher3DNode", &sw3Node);

         MappingNode mappingNode; mappingNode.input = &groupXform;
         checkPassthrough("MappingNode", &mappingNode);

         MeshResynthNode resynthNode; resynthNode.input = &groupXform;
         checkPassthrough("MeshResynthNode", &resynthNode);

         AudioDisplacementNode audioDispNode; audioDispNode.input = &groupXform;
         checkPassthrough("AudioDisplacementNode", &audioDispNode);

         bool allOk = true;
         for (const Result& r : results)
         {
            const bool ok = r.foundInstancer && r.groupOk;
            printf("  [%s] %-24s %s%s\n", ok ? "pass" : "FAIL", r.name.c_str(),
                   r.foundInstancer ? "" : "instancer not reachable through PassthroughSource ",
                   r.groupOk ? "" : "group matrix dropped");
            if (!ok)
               allOk = false;
         }

         // Consumer-side realization checks: assert that nodes that consume an
         // instancer's output properly realize all instances across the scatter,
         // track the 5/7/3 group translation, and reduce count on instance deletion.
         auto checkConsumerRealization = [&](const char* name, bool ok, const char* detail)
         {
            printf("  [%s] %-24s %s\n", ok ? "pass" : "FAIL", name, ok ? "" : detail);
            if (!ok) allOk = false;
         };

         // 1. DistributePointsOnFacesNode realization & group tracking
         DistributePointsOnFacesNode distNode;
         distNode.input = &groupXform;
         cook(&distNode);
         const auto& distPts = distNode.GetPoints();
         float dMeanX = 0, dMeanY = 0, dMeanZ = 0;
         for (const auto& p : distPts) { dMeanX += p.px; dMeanY += p.py; dMeanZ += p.pz; }
         if (!distPts.empty()) { dMeanX /= distPts.size(); dMeanY /= distPts.size(); dMeanZ /= distPts.size(); }
         const bool distOk = distPts.size() > 50 &&
                             std::fabs(dMeanX - 5.0f) < 1.5f &&
                             std::fabs(dMeanY - 7.0f) < 1.5f &&
                             std::fabs(dMeanZ - 3.0f) < 1.5f;
         checkConsumerRealization("DistributePoints(scatter)", distOk, "points not realized or group translation lost");

         // 2. MeshToPointsNode realization & group tracking
         MeshToPointsNode m2pNode;
         m2pNode.input = &groupXform;
         cook(&m2pNode);
         const auto& m2pPts = m2pNode.GetPoints();
         float mMeanX = 0, mMeanY = 0, mMeanZ = 0;
         for (const auto& p : m2pPts) { mMeanX += p.px; mMeanY += p.py; mMeanZ += p.pz; }
         if (!m2pPts.empty()) { mMeanX /= m2pPts.size(); mMeanY /= m2pPts.size(); mMeanZ /= m2pPts.size(); }
         const bool m2pOk = m2pPts.size() > 50 &&
                            std::fabs(mMeanX - 5.0f) < 1.5f &&
                            std::fabs(mMeanY - 7.0f) < 1.5f &&
                            std::fabs(mMeanZ - 3.0f) < 1.5f;
         checkConsumerRealization("MeshToPoints(scatter)", m2pOk, "points not realized or group translation lost");

         // 3. PointsToVerticesNode realization
         PointsToVerticesNode p2vNode;
         p2vNode.input = &groupXform;
         cook(&p2vNode);
         const size_t p2vCount = p2vNode.GetMesh().vertices.size();
         const bool p2vOk = p2vCount > 50;
         checkConsumerRealization("PointsToVertices(scatter)", p2vOk, "vertices not realized across instances");

         // 4. JoinGeometryNode realization
         JoinGeometryNode joinNode;
         joinNode.inputs[0] = &groupXform;
         cook(&joinNode);
         const size_t joinCount = joinNode.GetMesh().vertices.size();
         const bool joinOk = joinCount > 50;
         checkConsumerRealization("JoinGeometry(scatter)", joinOk, "instances not realized in join");

         // 5. Upstream deletion propagation to DistributePoints
         GeometryOpNode selNode;
         selNode.op = GeometryOpNode::kSelect;
         selNode.selectMode = MeshOps::kSelectRandom;
         selNode.selectA = 0.5f;
         selNode.input = &inst;

         GeometryOpNode delNode;
         delNode.op = GeometryOpNode::kDelete;
         delNode.selectionOnly = true;
         delNode.keepSelected = false;
         delNode.input = &selNode;

         DistributePointsOnFacesNode distAfterDel;
         distAfterDel.input = &delNode;
         cook(&distAfterDel);
         const size_t distAfterDelCount = distAfterDel.GetPoints().size();
         const bool delOk = distAfterDelCount > 0 && distAfterDelCount < distPts.size();
         checkConsumerRealization("Delete->DistributePoints", delOk, "deletion did not reduce realized points");

         printf("%s\n", allOk ? "INSTANCE SWEEP OK" : "INSTANCE SWEEP FAIL");
      }
}

void FrameTest_CHAINFUZZTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_CHAINFUZZTEST") != nullptr && frameId == 6)
      {
         enum LinkType
         {
            kGeometryOp = 0, kDisplacement, kAudioDisplacement, kSetColor,
            kMeshResynth, kNull3D, kMaterial, kMapping, kMergeByDistance,
            kCloth, kFieldElement, kLinkTypeCount
         };
         static const char* kLinkNames[kLinkTypeCount] = {
            "GeometryOp", "Displacement", "AudioDisplacement", "SetColor",
            "MeshResynth", "Null3D", "Material", "Mapping", "MergeByDistance",
            "Cloth", "FieldElement"
         };

         // Every possible link, constructed fresh per attempt and chained
         // via `input`, is owned by one vector of nodes so the chain can be
         // any length without hand-declaring N locals. unique_ptr<INode>
         // would slice the IGeometrySource side, so a small tagged union of
         // real node instances is used instead - only the type in `kind`
         // is ever touched.
         struct Link
         {
            LinkType kind;
            GeometryOpNode geomOp;
            DisplacementNode disp;
            AudioDisplacementNode adisp;
            SetColorNode setColor;
            MeshResynthNode resynth;
            Null3DNode null3d;
            MaterialNode material;
            MappingNode mapping;
            MergeByDistanceNode merge;
            ClothNode cloth;
            FieldElementNode fieldElem;

            IGeometrySource* AsSource()
            {
               switch (kind)
               {
                  case kGeometryOp: return &geomOp;
                  case kDisplacement: return &disp;
                  case kAudioDisplacement: return &adisp;
                  case kSetColor: return &setColor;
                  case kMeshResynth: return &resynth;
                  case kNull3D: return &null3d;
                  case kMaterial: return &material;
                  case kMapping: return &mapping;
                  case kMergeByDistance: return &merge;
                  case kCloth: return &cloth;
                  case kFieldElement: return &fieldElem;
                  default: return nullptr;
               }
            }
            void Wire(IGeometrySource* upstream, unsigned int seed)
            {
               switch (kind)
               {
                  case kGeometryOp:
                     geomOp.input = upstream;
                     geomOp.op = (GeometryOpNode::Op)(seed % 3); // kTransform/kArray/kTwist-ish; cheap ops only
                     break;
                  case kDisplacement: disp.input = upstream; break;
                  case kAudioDisplacement: adisp.input = upstream; break;
                  case kSetColor:
                     setColor.input = upstream;
                     setColor.source = SetColorNode::kFlat;
                     setColor.flatColor[0] = ((seed % 7) / 7.0f);
                     setColor.flatColor[1] = ((seed % 11) / 11.0f);
                     setColor.flatColor[2] = ((seed % 13) / 13.0f);
                     break;
                  case kMeshResynth: resynth.input = upstream; break;
                  case kNull3D: null3d.input = upstream; break;
                  case kMaterial: material.input = upstream; material.bypassed = true; break;
                  case kMapping: mapping.input = upstream; break;
                  case kMergeByDistance: merge.input = upstream; merge.threshold = 0.0f; break;
                  case kCloth:
                     cloth.input = upstream; cloth.pinMode = ClothNode::kPinNone;
                     cloth.gravityX = cloth.gravityY = cloth.gravityZ = 0.0f;
                     cloth.windX = cloth.windY = cloth.windZ = 0.0f;
                     break;
                  case kFieldElement: fieldElem.input = upstream; break;
                  default: break;
               }
            }
         };

         auto meshColour = [](const Mesh& m) -> bool { return m.HasVertexColor(); };

         auto buildChain = [&](unsigned int seed, int length, GeometryNode& head,
                                std::vector<std::unique_ptr<Link>>& links, bool colourfulHead) -> IGeometrySource*
         {
            head.shape = 1; // cube
            head.detail = 3;
            IGeometrySource* upstream = &head;
            for (int i = 0; i < length; i++)
            {
               auto link = std::make_unique<Link>();
               unsigned int linkSeed = seed * 2654435761u + i * 40503u;
               link->kind = (LinkType)(linkSeed % kLinkTypeCount);
               link->Wire(upstream, linkSeed);
               upstream = link->AsSource();
               links.push_back(std::move(link));
            }
            (void)colourfulHead;
            return upstream;
         };

         struct FuzzResult { unsigned int seed; int length; bool deterministic; bool colourOk; bool noManufacture; std::string chainDesc; };
         std::vector<FuzzResult> results;
         std::mt19937 rng(0xC0FFEEu); // fixed seed - reproducible across runs
         int frame = 25000;
         const int kFuzzCount = 200;

         for (int trial = 0; trial < kFuzzCount; trial++)
         {
            unsigned int seed = rng();
            int length = 3 + (int)(rng() % 3); // 3..5

            std::string desc;
            bool chainHasSetColor = false;
            {
               unsigned int s2 = seed;
               for (int i = 0; i < length; i++)
               {
                  unsigned int linkSeed = s2 * 2654435761u + i * 40503u;
                  LinkType k = (LinkType)(linkSeed % kLinkTypeCount);
                  if (k == kSetColor) chainHasSetColor = true;
                  desc += kLinkNames[linkSeed % kLinkTypeCount];
                  if (i + 1 < length) desc += "->";
               }
            }

            // Build twice from the same seed - determinism check.
            GeometryNode headA; std::vector<std::unique_ptr<Link>> linksA;
            IGeometrySource* tailA = buildChain(seed, length, headA, linksA, false);
            if (auto* n = dynamic_cast<INode*>(tailA)) n->CookIfNeeded(frame); frame++;
            const Mesh meshA = tailA->GetMesh();

            GeometryNode headB; std::vector<std::unique_ptr<Link>> linksB;
            IGeometrySource* tailB = buildChain(seed, length, headB, linksB, false);
            if (auto* n = dynamic_cast<INode*>(tailB)) n->CookIfNeeded(frame); frame++;
            const Mesh meshB = tailB->GetMesh();

            bool deterministic = meshA.vertices.size() == meshB.vertices.size() &&
                                  meshA.HasVertexColor() == meshB.HasVertexColor();
            if (deterministic)
            {
               for (size_t i = 0; i < meshA.vertices.size() && deterministic; i++)
               {
                  if (std::fabs(meshA.vertices[i].px - meshB.vertices[i].px) > 1e-5f) deterministic = false;
                  if (std::fabs(meshA.vertices[i].py - meshB.vertices[i].py) > 1e-5f) deterministic = false;
                  if (std::fabs(meshA.vertices[i].pz - meshB.vertices[i].pz) > 1e-5f) deterministic = false;
               }
            }
            // A SetColorNode anywhere in the chain legitimately paints the
            // mesh - that's its job, not manufactured colour - so only a
            // colourless-head chain with no SetColor link is expected to
            // stay colourless end to end.
            bool noManufacture = chainHasSetColor || !meshColour(meshA);

            // Colour-in/colour-out: rebuild once more with a coloured head,
            // by wrapping headA in a colour-forcing probe - reuses
            // ColourMeshProbeSource's exact shape (a distinct RGB, checked
            // for survival unless a SetColorNode link overwrote it, which is
            // legitimate and excluded from the check below).
            bool colourOk = true;
            {
               struct ColourHeadProbe : public IGeometrySource
               {
                  GeometryNode* wrapped = nullptr;
                  Mesh mColoured;
                  const Mesh& GetMesh() override
                  {
                     mColoured = wrapped->GetMesh();
                     mColoured.vertexColor.assign(mColoured.vertices.size() * 3, 0.0f);
                     for (size_t i = 0; i < mColoured.vertices.size(); i++)
                     {
                        mColoured.vertexColor[i * 3 + 0] = 0.15f;
                        mColoured.vertexColor[i * 3 + 1] = 0.35f;
                        mColoured.vertexColor[i * 3 + 2] = 0.85f;
                     }
                     return mColoured;
                  }
                  unsigned long long MeshRevision() override { return wrapped->MeshRevision() + 1; }
                  Mat4 GetModelMatrix() const override { return wrapped->GetModelMatrix(); }
                  Material GetMaterial() const override { return wrapped->GetMaterial(); }
                  unsigned int GetSurfaceTexture() override { return wrapped->GetSurfaceTexture(); }
               };
               GeometryNode colourHeadMesh; colourHeadMesh.shape = 1; colourHeadMesh.detail = 3;
               ColourHeadProbe colourHead; colourHead.wrapped = &colourHeadMesh;

               std::vector<std::unique_ptr<Link>> linksC;
               IGeometrySource* upstream = &colourHead;
               bool sawSetColor = false;
               for (int i = 0; i < length; i++)
               {
                  auto link = std::make_unique<Link>();
                  unsigned int linkSeed = seed * 2654435761u + i * 40503u;
                  link->kind = (LinkType)(linkSeed % kLinkTypeCount);
                  if (link->kind == kSetColor) sawSetColor = true;
                  link->Wire(upstream, linkSeed);
                  upstream = link->AsSource();
                  linksC.push_back(std::move(link));
               }
               if (auto* n = dynamic_cast<INode*>(upstream)) n->CookIfNeeded(frame); frame++;
               const Mesh coloured = upstream->GetMesh();
               // A SetColorNode anywhere in the chain legitimately overwrites
               // the head's colour with its own - that's its job, not a bug -
               // so only assert survival when no link repainted it.
               if (!sawSetColor)
                  colourOk = meshColour(coloured);
            }

            results.push_back({ seed, length, deterministic, colourOk, noManufacture, desc });
         }

         bool allOk = true;
         int failCount = 0;
         for (const FuzzResult& r : results)
         {
            bool ok = r.deterministic && r.colourOk && r.noManufacture;
            if (!ok)
            {
               failCount++;
               allOk = false;
               // Full diagnostics on failure only - a fuzz test with no seed
               // logged is unreproducible and undebuggable.
               printf("  [FAIL] seed=0x%08X len=%d chain=%s det=%d colour=%d noManuf=%d\n",
                      r.seed, r.length, r.chainDesc.c_str(), r.deterministic, r.colourOk, r.noManufacture);
            }
         }
         printf("  %d/%d trials passed\n", (int)results.size() - failCount, (int)results.size());
         printf("%s\n", allOk ? "CHAIN FUZZ OK" : "CHAIN FUZZ FAIL");
      }
}

void FrameTest_REVISIONSWEEPTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_REVISIONSWEEPTEST") != nullptr && frameId == 6)
      {
         GeometryNode probeMesh;
         probeMesh.shape = 1; // cube
         probeMesh.detail = 4;

         int frame = 22000;

         struct Result { std::string name; bool ok; };
         std::vector<Result> results;

         GeometryOpNode opNode; opNode.op = GeometryOpNode::kTransform; opNode.input = &probeMesh;
         opNode.CookIfNeeded(frame++);
         const unsigned long long opFirst = opNode.MeshRevision();
         opNode.CookIfNeeded(frame++);
         results.push_back({ "GeometryOpNode", opFirst == opNode.MeshRevision() });

         // The case that actually caught the bug: Displacement with a real,
         // static (speed=0, so no transport-time animation) texture connected.
         NoiseNode noise;
         noise.speed = 0.0f;
         noise.width = 64.0f; noise.height = 64.0f;
         noise.CookIfNeeded(frame++);

         DisplacementNode dispNode;
         dispNode.input = &probeMesh;
         dispNode.TextureInput().Connect(&noise);
         dispNode.CookIfNeeded(frame++);
         const unsigned long long dispFirst = dispNode.MeshRevision();
         dispNode.CookIfNeeded(frame++);
         const unsigned long long dispSecond = dispNode.MeshRevision();
         dispNode.CookIfNeeded(frame++);
         const unsigned long long dispThird = dispNode.MeshRevision();
         results.push_back({ "DisplacementNode(static texture)", dispFirst == dispSecond && dispSecond == dispThird });

         // AudioDisplacementNode's own analogous case: with no audio cable
         // connected, mCurrentEnergy sits at 0 forever, so its signature
         // (now keyed on quantised energy rather than an ever-incrementing
         // audio-frame counter - see item 13's fix) should stay flat across
         // repeated cooks instead of rebuilding every frame regardless of
         // whether anything actually changed.
         AudioDisplacementNode adispNode;
         adispNode.input = &probeMesh;
         adispNode.CookIfNeeded(frame++);
         const unsigned long long adispFirst = adispNode.MeshRevision();
         adispNode.CookIfNeeded(frame++);
         const unsigned long long adispSecond = adispNode.MeshRevision();
         adispNode.CookIfNeeded(frame++);
         const unsigned long long adispThird = adispNode.MeshRevision();
         results.push_back({ "AudioDisplacementNode(no audio)", adispFirst == adispSecond && adispSecond == adispThird });

         MeshResynthNode resynthNode;
         resynthNode.input = &probeMesh;
         resynthNode.CookIfNeeded(frame++);
         const unsigned long long resynthFirst = resynthNode.MeshRevision();
         resynthNode.CookIfNeeded(frame++);
         results.push_back({ "MeshResynthNode", resynthFirst == resynthNode.MeshRevision() });

         SetColorNode setColorNode;
         setColorNode.input = &probeMesh;
         setColorNode.CookIfNeeded(frame++);
         const unsigned long long setColorFirst = setColorNode.MeshRevision();
         setColorNode.CookIfNeeded(frame++);
         results.push_back({ "SetColorNode", setColorFirst == setColorNode.MeshRevision() });

         DistributePointsOnFacesNode distFacesNode;
         distFacesNode.input = &probeMesh;
         distFacesNode.CookIfNeeded(frame++);
         const unsigned long long distFacesFirst = distFacesNode.MeshRevision();
         distFacesNode.CookIfNeeded(frame++);
         results.push_back({ "DistributePointsOnFacesNode", distFacesFirst == distFacesNode.MeshRevision() });

         PointsToVerticesNode p2vNode;
         p2vNode.input = &probeMesh;
         p2vNode.CookIfNeeded(frame++);
         const unsigned long long p2vFirst = p2vNode.MeshRevision();
         p2vNode.CookIfNeeded(frame++);
         results.push_back({ "PointsToVerticesNode", p2vFirst == p2vNode.MeshRevision() });

         MergeByDistanceNode mergeNode;
         mergeNode.input = &probeMesh;
         mergeNode.CookIfNeeded(frame++);
         const unsigned long long mergeFirst = mergeNode.MeshRevision();
         mergeNode.CookIfNeeded(frame++);
         results.push_back({ "MergeByDistanceNode", mergeFirst == mergeNode.MeshRevision() });

         // Cloth's stamp is its own mMesh revision, bumped by Step() every
         // physics tick even while draping correctly, so it is not expected to
         // stay put between two cooks - what actually matters for Cloth is
         // covered by the topology-vs-position distinction directly, not a
         // stable-revision check. It is included with the *input's* revision
         // instead: confirms a static input doesn't force a rebuild by proxy.
         ClothNode clothNode;
         clothNode.input = &probeMesh;
         clothNode.pinMode = ClothNode::kPinNone;
         clothNode.gravityX = clothNode.gravityY = clothNode.gravityZ = 0.0f;
         clothNode.windX = clothNode.windY = clothNode.windZ = 0.0f;
         clothNode.CookIfNeeded(frame++);
         const size_t clothConstraintsFirst = clothNode.ConstraintCount();
         clothNode.CookIfNeeded(frame++);
         clothNode.CookIfNeeded(frame++);
         results.push_back({ "ClothNode(constraint count stable)", clothConstraintsFirst == clothNode.ConstraintCount() && clothConstraintsFirst > 0 });

         // Switcher3DNode's own bug class to guard against: bumping its
         // revision on every cook just because it has a live clock, rather
         // than only when the active slot (or that slot's own mesh) actually
         // changes. Pinned via manual/manualSlot so the clock plays no part
         // in either half of this check.
         GeometryNode probeMeshB;
         probeMeshB.shape = 2; // sphere: distinct from probeMesh's cube
         probeMeshB.detail = 4;
         Switcher3DNode sw3Static;
         sw3Static.inputs[0] = &probeMesh;
         sw3Static.inputs[1] = &probeMeshB;
         sw3Static.manual = true;
         sw3Static.manualSlot = 0;
         sw3Static.CookIfNeeded(frame++);
         const unsigned long long sw3First = sw3Static.MeshRevision();
         sw3Static.CookIfNeeded(frame++);
         sw3Static.CookIfNeeded(frame++);
         results.push_back({ "Switcher3DNode(static slot stable)", sw3First == sw3Static.MeshRevision() });

         // The other half of the same invariant: a real switch must bump the
         // revision, so a downstream cache actually re-cooks when the input
         // it's reading from changes underneath it.
         const unsigned long long sw3BeforeSwitch = sw3Static.MeshRevision();
         sw3Static.manualSlot = 1;
         sw3Static.CookIfNeeded(frame++);
         results.push_back({ "Switcher3DNode(switch bumps revision)", sw3Static.MeshRevision() != sw3BeforeSwitch });

         // The actual bug-report scenario, not covered by the manual/
         // manualSlot checks above: manual=false, letting Transport's clock
         // itself drive the switch across repeated idle cooks (no param
         // touched between them, the same as a user just sitting there).
         // Confirms both halves of the idle path - a bump when the clock
         // crosses an interval boundary, and no bump on an idle cook that
         // doesn't cross one - since a switcher that never advances live and
         // one that free-runs every frame would both slip past the
         // manual-only checks above.
         {
            // This check drives the clock by hand via Tick() with hardcoded
            // deltas, expecting exact control over Beats()/Seconds(). Since
            // P2.5, Tick() is only the fallback path - if the audio engine
            // is actually running (a real device opened at startup), the
            // clock is audio-driven and free-runs off real wall-clock time
            // instead, which would make this test's boundary-crossing math
            // meaningless. Stop the engine for the duration of this block
            // so Transport is guaranteed to be in fallback mode, then
            // restart it afterward.
            const bool audioWasRunning = AudioEngine::Instance().SampleRate() > 0.0;
            if (audioWasRunning)
               AudioEngine::Instance().Stop();

            const bool savedPlaying = Transport::Instance().IsPlaying();
            const float savedBpm = Transport::Instance().Tempo();
            Transport::Instance().SetPlaying(true);
            Transport::Instance().SetTempo(120.0f);
            Transport::Instance().Rewind();

            Switcher3DNode sw3Clock;
            sw3Clock.inputs[0] = &probeMesh;
            sw3Clock.inputs[1] = &probeMeshB;
            sw3Clock.manual = false;
            sw3Clock.unit = 1; // seconds
            sw3Clock.interval = 0.05f;

            Transport::Instance().Tick(0.01f);
            sw3Clock.CookIfNeeded(frame++);
            const unsigned long long clockFirst = sw3Clock.MeshRevision();
            const int slotFirst = sw3Clock.ActiveSlot();

            // Several idle cooks within the same interval bucket: no clock
            // advance worth crossing a boundary, so the revision must hold.
            Transport::Instance().Tick(0.001f);
            sw3Clock.CookIfNeeded(frame++);
            Transport::Instance().Tick(0.001f);
            sw3Clock.CookIfNeeded(frame++);
            results.push_back({ "Switcher3DNode(clock idle, no boundary crossed -> stable)",
                                 sw3Clock.MeshRevision() == clockFirst });

            // Now tick past several interval boundaries with nothing else
            // touched, exactly like the app sitting idle while the clock
            // free-runs - the revision and active slot must both move.
            bool sawBump = false;
            bool sawSlotChange = false;
            for (int i = 0; i < 20; i++)
            {
               Transport::Instance().Tick(0.02f);
               sw3Clock.CookIfNeeded(frame++);
               if (sw3Clock.MeshRevision() != clockFirst)
                  sawBump = true;
               if (sw3Clock.ActiveSlot() != slotFirst)
                  sawSlotChange = true;
            }
            results.push_back({ "Switcher3DNode(clock idle, boundary crossed -> revision bumps)", sawBump });
            results.push_back({ "Switcher3DNode(clock idle, boundary crossed -> active slot moves)", sawSlotChange });

            Transport::Instance().SetTempo(savedBpm);
            Transport::Instance().SetPlaying(savedPlaying);
            Transport::Instance().Rewind();

            if (audioWasRunning)
            {
               std::string audioRestartError;
               // StartAudioEngine, not a bare Start(): this Stop() up above
               // already dropped the device, so on restart every node's
               // ParamMailbox needs the same PrepareToPlay pass any other
               // restart gets - this test-only Stop/Start is still a real
               // engine-start call site, not exempt from bug 1's fix.
               if (!StartAudioEngine(audioRestartError))
                  fprintf(stderr, "audio device: %s\n", audioRestartError.c_str());
            }
         }

         bool allOk = true;
         for (const Result& r : results)
         {
            printf("  [%s] %-32s\n", r.ok ? "pass" : "FAIL", r.name.c_str());
            if (!r.ok)
               allOk = false;
         }
         printf("%s\n", allOk ? "REVISION SWEEP OK" : "REVISION SWEEP FAIL");
      }
}

void FrameTest_RENDER3DLIVETEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_RENDER3DLIVETEST") != nullptr && frameId == 6)
      {
         const bool audioWasRunning = AudioEngine::Instance().SampleRate() > 0.0;
         if (audioWasRunning)
            AudioEngine::Instance().Stop();
         const bool savedPlaying = Transport::Instance().IsPlaying();
         const float savedBpm = Transport::Instance().Tempo();
         Transport::Instance().SetPlaying(true);
         Transport::Instance().SetTempo(120.0f);
         Transport::Instance().Rewind();

         int frame = 30000;

         bool instancedLive = false;
         {
            ParticleSystemNode particles;
            particles.emitRate = 500.0f;
            particles.lifetime = 5.0f;
            GeometryNode cube;
            cube.shape = 1;
            InstanceOnPointsNode instancer;
            instancer.cloudSource = &particles;
            instancer.instanceShape = &cube;
            Render3DNode render;
            render.geometry[0] = &instancer;

            Transport::Instance().Tick(0.05f);
            render.CookIfNeeded(frame++);
            const unsigned long long revBefore = render.TextureRevision();

            for (int i = 0; i < 10 && !instancedLive; i++)
            {
               Transport::Instance().Tick(0.05f);
               render.CookIfNeeded(frame++);
               if (render.TextureRevision() != revBefore)
                  instancedLive = true;
            }
         }

         bool spinLive = false;
         {
            GeometryNode spinner;
            spinner.shape = 1;
            spinner.spinY = 90.0f;
            Render3DNode render;
            render.geometry[0] = &spinner;

            Transport::Instance().Tick(0.05f);
            render.CookIfNeeded(frame++);
            const unsigned long long revBefore = render.TextureRevision();

            for (int i = 0; i < 10 && !spinLive; i++)
            {
               Transport::Instance().Tick(0.05f);
               render.CookIfNeeded(frame++);
               if (render.TextureRevision() != revBefore)
                  spinLive = true;
            }
         }

         printf("  [%s] %-32s\n", instancedLive ? "pass" : "FAIL",
                "Render3DNode(Particles->InstanceOnPoints stays live)");
         printf("  [%s] %-32s\n", spinLive ? "pass" : "FAIL",
                "Render3DNode(spinY-animated source stays live)");
         printf("%s\n", (instancedLive && spinLive) ? "RENDER3D LIVE TEST OK" : "RENDER3D LIVE TEST FAIL");

         Transport::Instance().SetTempo(savedBpm);
         Transport::Instance().SetPlaying(savedPlaying);
         Transport::Instance().Rewind();
         if (audioWasRunning)
         {
            std::string audioRestartError;
            if (!StartAudioEngine(audioRestartError))
               fprintf(stderr, "audio device: %s\n", audioRestartError.c_str());
         }
      }
}

void FrameTest_RENDER3DCACHESWEEPTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_RENDER3DCACHESWEEPTEST") != nullptr && frameId == 6)
      {
         struct Result { std::string name; bool ok; };
         std::vector<Result> results;
         int frame = 32000;

         auto checkCacheInvalidates = [&](const char* name, IGeometrySource* source, std::function<void()> mutate)
         {
            Render3DNode render;
            render.geometry[0] = source;
            render.CookIfNeeded(frame++);
            const unsigned long long workBefore = NodeWorkCounter();
            mutate();
            render.CookIfNeeded(frame++);
            const unsigned long long workAfter = NodeWorkCounter();
            results.push_back({ name, workAfter != workBefore });
         };

         // MeshToPointsNode: mesh cache and point cache rebuilt together off
         // the same probe mesh, sharing mMeshRevision for both accessors.
         {
            GeometryNode probe;
            probe.shape = 1; // cube
            probe.detail = 2;
            MeshToPointsNode node;
            node.input = &probe;
            node.mode = 0;
            checkCacheInvalidates("MeshToPointsNode", &node, [&]() { probe.detail = 4; });
         }

         // DistributePointsOnFacesNode: same aliasing, both mMeshRevision.
         {
            GeometryNode probe;
            probe.shape = 1;
            probe.detail = 2;
            DistributePointsOnFacesNode node;
            node.input = &probe;
            checkCacheInvalidates("DistributePointsOnFacesNode", &node, [&]() { probe.detail = 4; });
         }

         // DistributePointsInGridNode: no mesh input at all - its own
         // countX/countY drive PointRevision()/MeshRevision() together.
         {
            DistributePointsInGridNode node;
            node.countX = 4;
            node.countY = 4;
            checkCacheInvalidates("DistributePointsInGridNode", &node, [&]() { node.countX = 8; });
         }

         // CurveNode: MeshRevision() and CurveStamp() both return mRevision.
         {
            CurveNode node;
            node.pointCount = 5;
            checkCacheInvalidates("CurveNode", &node, [&]() { node.pointCount = 7; });
         }

         bool allOk = true;
         for (const Result& r : results)
         {
            printf("  [%s] %-32s\n", r.ok ? "pass" : "FAIL", r.name.c_str());
            if (!r.ok)
               allOk = false;
         }
         printf("%s\n", allOk ? "RENDER3D CACHE SWEEP OK" : "RENDER3D CACHE SWEEP FAIL");
      }
}

void FrameTest_FIXTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_FIXTEST") != nullptr && frameId == 6)
      {
         auto* r0 = static_cast<RandomNode*>(gNodes[0].node.get());
         auto* r1 = static_cast<RandomNode*>(gNodes[1].node.get());
         auto* r2 = static_cast<RandomNode*>(gNodes[2].node.get());

         // Three independently spawned Random nodes must not agree. Sampled
         // across several steps, since any two can coincide on a single one.
         int identical01 = 0, identical02 = 0;
         for (int step = 0; step < 32; step++)
         {
            r0->rateBeats = r1->rateBeats = r2->rateBeats = 0.25f;
            Transport::Instance().Tick(0.12f);
            const float v0 = r0->Value01();
            const float v1 = r1->Value01();
            const float v2 = r2->Value01();
            if (std::fabs(v0 - v1) < 1e-6f) identical01++;
            if (std::fabs(v0 - v2) < 1e-6f) identical02++;
         }
         printf("random: %d/32 samples identical between node 0 and 1, %d/32 between 0 and 2\n",
                identical01, identical02);
         printf("seeds: %.1f %.1f %.1f\n", r0->seed, r1->seed, r2->seed);
         const bool independent = identical01 < 4 && identical02 < 4;
         printf("%s\n", independent ? "RANDOM INDEPENDENT OK"
                                    : "SUSPECT - Random nodes still correlated");

         // Same seed must still reproduce the same sequence exactly.
         r1->seed = r0->seed;
         const bool reproducible = std::fabs(r0->Value01() - r1->Value01()) < 1e-6f;
         printf("same seed reproduces: %d\n", (int)reproducible);

         auto* a = static_cast<GeometryNode*>(gNodes[3].node.get());
         auto* b = static_cast<GeometryNode*>(gNodes[4].node.get());
         auto* join = static_cast<JoinGeometryNode*>(gNodes[5].node.get());
         auto* render = static_cast<Render3DNode*>(gNodes[6].node.get());

         const size_t expected = a->GetMesh().indices.size() / 3 + b->GetMesh().indices.size() / 3;
         // The merged mesh must span both parts' placements, or the transforms
         // were dropped and everything collapsed onto the origin.
         float lo = 1e30f, hi = -1e30f;
         for (const Vertex& v : join->GetMesh().vertices)
         {
            lo = std::min(lo, v.px);
            hi = std::max(hi, v.px);
         }
         printf("join: %zu tris (expected %zu), x span %.2f..%.2f, %d inputs\n",
                join->TriangleCount(), expected, lo, hi, join->ConnectedCount());
         const bool merged = join->TriangleCount() == expected && lo < -0.5f && hi > 1.0f;
         printf("%s\n", (merged && render->LastTriangleCount() > 0)
                           ? "JOIN GEOMETRY OK" : "SUSPECT");
      }
}

void FrameTest_CLOTHTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_CLOTHTEST") != nullptr)
      {
         auto* geo = static_cast<GeometryNode*>(gNodes[0].node.get());
         auto* cloth = static_cast<ClothNode*>(gNodes[1].node.get());
         auto* render = static_cast<Render3DNode*>(gNodes[2].node.get());

         // Longest edge relative to its rest length. A position-based solver
         // should hold this near 1; a force-based one at this stiffness would
         // be visibly stretched, and an unstable one runs away to infinity.
         auto maxStretch = [&]() -> float
         {
            const Mesh& m = cloth->GetMesh();
            const Mesh& rest = geo->GetMesh();
            if (m.vertices.size() != rest.vertices.size())
               return -1.0f;
            float worst = 0.0f;
            for (size_t t = 0; t + 2 < m.indices.size(); t += 3)
            {
               for (int e = 0; e < 3; e++)
               {
                  const unsigned int a = m.indices[t + e];
                  const unsigned int b = m.indices[t + (e + 1) % 3];
                  auto len = [](const Vertex& p, const Vertex& q) {
                     const float dx = p.px-q.px, dy = p.py-q.py, dz = p.pz-q.pz;
                     return std::sqrt(dx*dx + dy*dy + dz*dz);
                  };
                  const float restLen = len(rest.vertices[a], rest.vertices[b]);
                  if (restLen < 1e-5f)
                     continue;
                  worst = std::max(worst, len(m.vertices[a], m.vertices[b]) / restLen);
               }
            }
            return worst;
         };

         static float sTopY = 0.0f;
         if (frameId == 3)
         {
            printf("cloth: %zu tris, %zu constraints\n",
                   cloth->TriangleCount(), cloth->ConstraintCount());
            float hi = -1e30f;
            for (const Vertex& v : cloth->GetMesh().vertices)
               hi = std::max(hi, v.py);
            sTopY = hi;
         }
         if (frameId == 60)
         {
            const Mesh& m = cloth->GetMesh();
            float lo = 1e30f, hi = -1e30f;
            bool finite = true;
            for (const Vertex& v : m.vertices)
            {
               if (!std::isfinite(v.px) || !std::isfinite(v.py) || !std::isfinite(v.pz))
                  finite = false;
               lo = std::min(lo, v.py);
               hi = std::max(hi, v.py);
            }
            const float stretch = maxStretch();
            printf("after 1s: finite=%d  y range %.3f..%.3f (top was %.3f)  max stretch %.3f\n",
                   (int)finite, lo, hi, sTopY, stretch);
            printf("rendered %zu tris\n", render->LastTriangleCount());

            // Pinned top edge must not have fallen, the rest must have, and no
            // edge may be stretched more than a few percent.
            const bool pinnedHeld = std::fabs(hi - sTopY) < 0.05f;
            const bool draped = lo < sTopY - 0.05f;
            const bool stable = finite && stretch > 0.5f && stretch < 1.2f;
            printf("pinned held=%d draped=%d stable=%d\n",
                   (int)pinnedHeld, (int)draped, (int)stable);
            printf("%s\n", (pinnedHeld && draped && stable && render->LastTriangleCount() > 0)
                              ? "CLOTH OK" : "SUSPECT");
            Transport::Instance().Rewind();
         }
         if (frameId == 63)
         {
            const float stretch = maxStretch();
            printf("after rewind: max stretch %.3f  %s\n", stretch,
                   std::fabs(stretch - 1.0f) < 0.01f ? "CLOTH REWIND RESETS OK" : "SUSPECT");
         }
      }
}

void FrameTest_PARTICLETEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_PARTICLETEST") != nullptr)
      {
         auto* ps = static_cast<ParticleSystemNode*>(gNodes[0].node.get());
         auto* inst = static_cast<InstanceOnPointsNode*>(gNodes[2].node.get());
         auto* render = static_cast<Render3DNode*>(gNodes[3].node.get());
         static size_t sAtPause = 0;
         static float sPausedY = 0.0f;

         if (frameId == 30)
         {
            printf("running: %zu alive, %zu instances, %zu tris rendered\n",
                   ps->AliveCount(), inst->InstanceCount(), render->LastTriangleCount());
            const bool running = ps->AliveCount() > 50 &&
                                 inst->InstanceCount() == ps->AliveCount() &&
                                 render->LastTriangleCount() > 0;
            printf("%s\n", running ? "PARTICLES SIMULATING OK" : "SUSPECT - not simulating");

            // Every particle must be finite: one NaN propagates through the
            // instance transforms and takes the whole render with it.
            bool finite = true;
            for (const Particle& p : ps->GetPoints())
               if (p.alive && (!std::isfinite(p.px) || !std::isfinite(p.py) || !std::isfinite(p.pz)))
                  finite = false;
            printf("all finite: %d\n", (int)finite);

            Transport::Instance().SetPlaying(false);
         }
         if (frameId == 33)
         {
            // Baseline taken a few frames after pausing, not on the same frame:
            // this block runs before the node cooks, so on the pause frame
            // itself there is still one step's worth of already-advanced clock
            // left to consume. Sampling here measures the frozen state.
            sAtPause = ps->AliveCount();
            for (const Particle& p : ps->GetPoints())
               if (p.alive) { sPausedY = p.py; break; }
         }
         if (frameId == 50)
         {
            // Paused means frozen, not merely not-drawn.
            float nowY = 0.0f;
            for (const Particle& p : ps->GetPoints())
               if (p.alive) { nowY = p.py; break; }
            const bool frozen = ps->AliveCount() == sAtPause &&
                                std::fabs(nowY - sPausedY) < 1e-6f;
            printf("after 17 paused frames: %zu alive (was %zu), y %.5f -> %.5f  %s\n",
                   ps->AliveCount(), sAtPause, sPausedY, nowY,
                   frozen ? "PAUSE FREEZES OK" : "SUSPECT - simulating while paused");
            Transport::Instance().SetPlaying(true);
            Transport::Instance().Rewind();
         }
         if (frameId == 52)
         {
            printf("after rewind: %zu alive  %s\n", ps->AliveCount(),
                   ps->AliveCount() < 50 ? "REWIND RESETS OK" : "SUSPECT - state survived rewind");
         }
      }
}

void FrameTest_AUDIORECTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_AUDIORECTEST") != nullptr)
      {
         auto* out = static_cast<OutputNode*>(gNodes[1].node.get());
         if (frameId == 2)
         {
            printf("audio source loaded: %d (%s)\n", (int)static_cast<AudioFileNode*>(gNodes[2].node.get())->IsLoaded(),
                   static_cast<AudioFileNode*>(gNodes[2].node.get())->Status().c_str());
            const bool started = out->StartRecording(TmpPath("infinite_audiorec.mov"));
            printf("start: %d (%s)\n", (int)started, out->RecordStatus().c_str());
         }
         if (frameId == 62) // ~2 seconds at 30fps
         {
            // The encoder occasionally reports not-ready and a frame is
            // skipped (pre-existing behaviour - the same fixture pattern in
            // INFINITE_RECTEST only ever printed its frame count, never
            // asserted it exactly), so the frame count is captured before
            // stopping rather than assumed, and duration is checked against
            // that real count rather than wall-clock elapsed time.
            const int frames = out->RecordedFrames();
            out->StopRecording();
            printf("recorded %d frames (of up to 60), status: %s\n", frames, out->RecordStatus().c_str());

            const Platform::MovieInfo withAudio = Platform::InspectMovie(TmpPath("infinite_audiorec.mov"));
            const double expectedDuration = (double)frames / 30.0;
            printf("with-audio movie: video=%d audio=%d duration=%.2fs (expected ~%.2fs)\n",
                   withAudio.hasVideo, withAudio.hasAudio, withAudio.duration, expectedDuration);

            // A control recording with includeAudio off, so the difference is
            // attributable to the checkbox and not to something environmental.
            out->includeAudio = false;
            out->StartRecording(TmpPath("infinite_videoonly.mov"));
         }
         if (frameId == 122)
         {
            const int frames = out->RecordedFrames();
            out->StopRecording();
            const Platform::MovieInfo videoOnly = Platform::InspectMovie(TmpPath("infinite_videoonly.mov"));
            printf("video-only movie: video=%d audio=%d duration=%.2fs (%d frames)\n",
                   videoOnly.hasVideo, videoOnly.hasAudio, videoOnly.duration, frames);

            const Platform::MovieInfo withAudio = Platform::InspectMovie(TmpPath("infinite_audiorec.mov"));
            const bool ok = withAudio.hasVideo && withAudio.hasAudio &&
                            videoOnly.hasVideo && !videoOnly.hasAudio &&
                            withAudio.duration > 0.5;
            printf("%s\n", ok ? "AUDIO RECORDING OK" : "SUSPECT");
         }
      }
}

void FrameTest_VIDEOAUDIOTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_VIDEOAUDIOTEST") != nullptr)
      {
         auto* out = static_cast<OutputNode*>(gNodes[1].node.get());
         static int sVaRecordedFrames = 0;
         if (frameId == 2)
         {
            const bool started = out->StartRecording(TmpPath("infinite_videoaudiotest.mov"));
            printf("start: %d (%s)\n", (int)started, out->RecordStatus().c_str());
         }
         if (frameId == 62) // ~2 seconds at 30fps, comfortably past the 1.5s tone
         {
            const int frames = out->RecordedFrames();
            sVaRecordedFrames = frames;
            out->StopRecording(); // blocks until AVAssetWriter finishes, so the file is complete by frame 64
            printf("recorded %d frames, status: %s\n", frames, out->RecordStatus().c_str());
         }
         if (frameId == 64)
         {
            SpawnNode("Video", "Source", 600.0f, 40.0f); // 3
            auto* video = static_cast<VideoSourceNode*>(gNodes[3].node.get());
            const bool opened = video->Open(TmpPath("infinite_videoaudiotest.mov"));
            printf("video open: %d (%s), duration=%.2fs, HasAudio=%d (%s)\n",
                   (int)opened, video->LastError().c_str(), video->Duration(),
                   (int)video->HasAudio(), video->AudioError().c_str());

            Platform::SampleBuffer buf;
            std::string decodeError;
            const bool decoded = Platform::DecodeVideoAudioTrackToBuffer(
               TmpPath("infinite_videoaudiotest.mov"), buf, decodeError);
            printf("direct decode: %d (%s), channels=%d sampleRate=%.0f numFrames=%d\n",
                   (int)decoded, decodeError.c_str(), buf.channels, buf.sampleRate, buf.numFrames);

            // Goertzel single-bin power at the recorded tone's frequency vs. a
            // control frequency well away from it (and from its harmonics) -
            // a phase-independent way to confirm the decoded samples still
            // carry that tone, without asserting exact per-sample values that
            // AAC's lossy re-encode and encoder priming delay would break.
            auto goertzelMagnitude = [](const float* samples, int n, double freqHz, double sr) -> double
            {
               const int k = (int)(0.5 + (double)n * freqHz / sr);
               const double w = 2.0 * M_PI * (double)k / (double)n;
               const double coeff = 2.0 * std::cos(w);
               double s0 = 0.0, s1 = 0.0, s2 = 0.0;
               for (int i = 0; i < n; i++)
               {
                  s0 = samples[i] + coeff * s1 - s2;
                  s2 = s1;
                  s1 = s0;
               }
               return std::sqrt(s1 * s1 + s2 * s2 - coeff * s1 * s2);
            };

            bool toneOk = false;
            double toneMag = 0.0, noiseMag = 0.0;
            if (decoded && buf.channels > 0 && buf.numFrames > 0)
            {
               const float* ch0 = buf.channelData.data();
               const double sr = buf.sampleRate > 0.0 ? buf.sampleRate : 48000.0;
               toneMag = goertzelMagnitude(ch0, buf.numFrames, 440.0, sr);
               noiseMag = goertzelMagnitude(ch0, buf.numFrames, 5000.0, sr); // far from 440Hz and its low harmonics
               toneOk = toneMag > noiseMag * 5.0;
            }
            printf("tone check: 440Hz magnitude=%.1f vs 5000Hz control=%.1f  %s\n",
                   toneMag, noiseMag, toneOk ? "TONE PRESENT" : "TONE MISSING");

            // The recorded audio track's real length is the *video's* duration
            // (60 frames / recordFps), not the 1.5s tone clip's own length:
            // AudioFileNode::loop defaults to true, and all three platforms'
            // recorders (Platform.mm's AppendAudioUpToLocked, MediaWin.cpp's
            // and MediaLinux.cpp's WriteFileAudioTrack) deliberately loop a
            // shorter file-audio source to fill the whole take rather than
            // truncate it early - confirmed identical across all three while
            // chasing a spurious Linux-only "1.5s" duration mismatch here.
            const double expectedDuration = (double)sVaRecordedFrames / std::max(1, out->recordFps);
            const bool durationOk = decoded && std::fabs((double)buf.numFrames / std::max(1.0, buf.sampleRate) - expectedDuration) < 0.3;

            const bool ok = opened && video->HasAudio() && decoded && buf.channels >= 1 && durationOk && toneOk;
            printf("%s\n", ok ? "VIDEOAUDIOTEST OK" : "VIDEOAUDIOTEST FAIL - BUG");
         }
      }
}

void FrameTest_VIDEOSPEEDTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_VIDEOSPEEDTEST") != nullptr)
      {
         // Reproduces "reverse freezes / +4 doesn't move" reports: drives a
         // real VideoSourceNode across many real rendered frames (not a tight
         // loop - CookIfNeeded derives its step from actual elapsed
         // Transport time) at speed -1 then +4, and checks FrameUpdateCount()
         // (bumped only when Platform::VideoFrameAt actually produces a new
         // displayed frame) keeps climbing throughout each phase - a stall
         // there is a real visual freeze even if Position() keeps advancing.
         // A repeat of the same source frame is not an update, so each phase
         // is held to the source frames it actually crossed, not to 60.
         auto* out = static_cast<OutputNode*>(gNodes[1].node.get());
         if (frameId == 2)
         {
            const bool started = out->StartRecording(TmpPath("infinite_videospeedtest.mov"));
            printf("start: %d (%s)\n", (int)started, out->RecordStatus().c_str());
         }
         if (frameId == 62)
         {
            out->StopRecording();
         }
         static VideoSourceNode* sVideo = nullptr;
         static int sReverseStartUpdates = 0, sForwardStartUpdates = 0;
         static double sReverseStartPos = 0.0, sForwardStartPos = 0.0;
         static double sReverseStartT = 0.0, sForwardStartT = 0.0, sReverseExpected = 0.0;
         // Source frames a phase crosses: elapsed transport time x |speed| x
         // the clip's rate, capped at one per rendered frame.
         auto expectedUpdates = [&](double startT, double absSpeed) {
            const double crossed = (Transport::Instance().Seconds() - startT) * absSpeed * (double)out->recordFps;
            return std::min(60.0, crossed);
         };
         if (frameId == 64)
         {
            SpawnNode("Video", "Source", 600.0f, 40.0f); // 3
            sVideo = static_cast<VideoSourceNode*>(gNodes[3].node.get());
            const bool opened = sVideo->Open(TmpPath("infinite_videospeedtest.mov"));
            printf("video open: %d, duration=%.2fs\n", (int)opened, sVideo->Duration());
            sVideo->loop = true;
            sVideo->speed = -1.0f;
            sReverseStartUpdates = sVideo->FrameUpdateCount();
            sReverseStartPos = sVideo->Position();
            sReverseStartT = Transport::Instance().Seconds();
         }
         if (frameId == 124 && sVideo != nullptr)
         {
            const int reverseUpdates = sVideo->FrameUpdateCount() - sReverseStartUpdates;
            const double reverseMoved = sVideo->Position() - sReverseStartPos;
            sReverseExpected = expectedUpdates(sReverseStartT, 1.0);
            printf("reverse (60 frames @ speed -1): updates=%d expected=%.0f posDelta=%.3f (from %.3f to %.3f)\n",
                   reverseUpdates, sReverseExpected, reverseMoved, sReverseStartPos, sVideo->Position());

            sVideo->speed = 4.0f;
            sForwardStartUpdates = sVideo->FrameUpdateCount();
            sForwardStartPos = sVideo->Position();
            sForwardStartT = Transport::Instance().Seconds();
         }
         if (frameId == 184 && sVideo != nullptr)
         {
            const int reverseUpdates = sVideo->FrameUpdateCount() - sReverseStartUpdates; // recompute isn't needed, kept for symmetry
            const int forwardUpdates = sVideo->FrameUpdateCount() - sForwardStartUpdates;
            const double forwardExpected = expectedUpdates(sForwardStartT, 4.0);
            printf("forward (60 frames @ speed +4): updates=%d expected=%.0f (from %.3f to %.3f)\n",
                   forwardUpdates, forwardExpected, sForwardStartPos, sVideo->Position());

            // A healthy run shows most of the source frames each phase
            // crossed - a handful of misses to decode hiccups is fine, a
            // near-zero count is the freeze.
            const int reversePhaseUpdates = reverseUpdates - forwardUpdates;
            const bool reverseOk = reversePhaseUpdates >= 10 && reversePhaseUpdates > sReverseExpected * 0.5;
            const bool forwardOk = forwardUpdates >= 10 && forwardUpdates > forwardExpected * 0.5;
            printf("%s\n", (reverseOk && forwardOk) ? "VIDEOSPEEDTEST OK" : "VIDEOSPEEDTEST FAIL - BUG");

            // Separate, additive check for the "loops once then freezes on
            // the last frame" bug: at speed +4 over 60 real frames the
            // unwrapped position (sForwardStartPos + elapsed*4) blows well
            // past this clip's ~1.5s duration, so a healthy loop must have
            // wrapped Position() back into [0, Duration()) at least once.
            // If CookIfNeeded's fmod/clamp branch is ever skipped (the
            // Windows bug this guards: mDuration <= 0 falls through to an
            // unbounded max(mPosition, 0.0)), Position() keeps growing past
            // Duration() instead of wrapping, and this fails even though
            // reverseOk/forwardOk above still pass (they only check that
            // frames keep decoding, not that the position is sane).
            const double duration = sVideo->Duration();
            const double position = sVideo->Position();
            const bool loopWrapOk =
               duration > 0.0 && position >= 0.0 && position < duration + 0.05;
            printf("loop wrap check: position=%.3f duration=%.3f (forward start was %.3f) %s\n",
                   position, duration, sForwardStartPos,
                   loopWrapOk ? "WRAPPED" : "NOT WRAPPED");
            printf("%s\n", loopWrapOk ? "VIDEOLOOPWRAPTEST OK" : "VIDEOLOOPWRAPTEST FAIL - BUG");
         }
      }
}

void FrameTest_OFFLINERENDERTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_OFFLINERENDERTEST") != nullptr)
      {
         auto* out = static_cast<OutputNode*>(gNodes[1].node.get());
         static double sOfflineAudioSampleRate = 0.0;
         if (frameId == 2)
         {
            StartOfflineRenderSession(out);
            // Latched here: StartOfflineRenderSession detaches the device, so
            // AudioEngine::SampleRate() reads 0 for the rest of the take.
            sOfflineAudioSampleRate = out->OfflineAudioSampleRate();
            // INFINITE_OFFLINERENDER_QUEUEBYTES shrinks the encoder queue's
            // byte budget for this take, so the pump hits backpressure at a
            // fixture's resolution rather than only on a real 1080p patch -
            // and with it the deadlock that backpressure exposed: yielding
            // with audio still parked in the writer's backlog, where nothing
            // but another render step would ever flush it.
            if (const char* qb = getenv("INFINITE_OFFLINERENDER_QUEUEBYTES"))
               Platform::RecorderSetTestQueueByteBudget(out->OfflineRecorderForTest(),
                                                        (size_t)atoll(qb));
            printf("start: active=%d rate=%.0fHz status=%s\n", (int)gOfflineRender.active,
                   sOfflineAudioSampleRate, out->RecordStatus().c_str());
         }
         static bool sOfflineDone = false;
         static int sOfflineDoneFrame = -1;

         // Progress trace: a take that stops advancing is indistinguishable
         // from a slow one in the final verdict, so the fixture prints where
         // it got to as it goes.
         if (getenv("INFINITE_OFFLINERENDER_TRACE") != nullptr && frameId % 200 == 0)
            printf("trace frameId=%d rendered=%d/%d room=%d %s\n", frameId,
                   out->OfflineFramesDone(), out->OfflineFramesTotal(),
                   (int)out->OfflineEncoderHasRoom(),
                   Platform::RecorderDebugState(out->OfflineRecorderForTest()).c_str());

         // INFINITE_OFFLINERENDER_CANCEL=<frames>: cancel the take once it
         // has rendered that many frames, and measure how long the cancel
         // takes in main-loop frames. Cancel used to route through the same
         // drain-everything finalize as a completed take, so on a deep queue
         // it sat at "Finalizing..." for as long as finishing would have
         // taken - which reads as a hang, because the user has already said
         // they want it to stop.
         static int sCancelRequestedFrame = -1;
         if (const char* cancelEnv = getenv("INFINITE_OFFLINERENDER_CANCEL"))
         {
            const int cancelAt = std::max(1, atoi(cancelEnv));
            if (sCancelRequestedFrame < 0 && gOfflineRender.active &&
                out->OfflineFramesDone() >= cancelAt)
            {
               sCancelRequestedFrame = frameId;
               out->RequestFinishOfflineRender(true);
               printf("cancel requested at frameId=%d after %d rendered frames\n",
                      frameId, out->OfflineFramesDone());
            }
            if (sCancelRequestedFrame >= 0 && !gOfflineRender.active &&
                !out->IsOfflineFinalizing() && sOfflineDoneFrame < 0)
            {
               const int cost = frameId - sCancelRequestedFrame;
               const bool fileGone = !std::filesystem::exists(out->recordVideoPath);
               // 10 main-loop frames is a generous ceiling for "instant" -
               // the old drain path took hundreds on a backed-up queue.
               const bool ok = cost <= 10 && fileGone;
               printf("cancel took %d main-loop frames, partial file removed=%d, status=%s\n",
                      cost, (int)fileGone, out->RecordStatus().c_str());
               printf("%s\n", ok ? "OFFLINERENDERCANCELTEST OK"
                                  : "OFFLINERENDERCANCELTEST FAIL - BUG");
               sOfflineDoneFrame = frameId; // stop this block from reporting again
               sOfflineDone = true;
            }
         }
         // The offline pump can finish the whole take (all 10 frames, plus
         // preroll) within a single outer loop iteration - it's bounded by a
         // wall-clock budget, not a frame count - so completion is polled by
         // state rather than assumed to land on any particular frameId.
         if (!sOfflineDone && frameId >= 3 && !gOfflineRender.active && !out->IsOfflineFinalizing())
         {
            sOfflineDone = true;
            sOfflineDoneFrame = frameId;
            printf("finalize done at frameId=%d: frames=%d status=%s\n",
                   frameId, out->LastRecordedFrames(), out->RecordStatus().c_str());
         }
         // AVFoundation's asset metadata for a file this same process just
         // finished writing can lag the on-disk bytes by a beat or two (the
         // same reason INFINITE_AUDIORECTEST checks its movie ~2s/60 frames
         // after StopRecording rather than the instant it returns) - so the
         // actual inspection is deferred a further margin past finalize
         // rather than run in the very frame completion is first observed.
         // A cancelled take has no file to inspect (that is the point of
         // cancel), so the completion assertion below belongs only to a take
         // that was allowed to finish - the cancel branch above is that
         // variant's whole verdict.
         if (sOfflineDone && sOfflineDoneFrame >= 0 && frameId == sOfflineDoneFrame + 60 &&
             getenv("INFINITE_OFFLINERENDER_CANCEL") == nullptr)
         {
            const Platform::MovieInfo info = Platform::InspectMovie(out->recordVideoPath);
            // The take's own budget: 10 frames at 10fps is exactly 1 second
            // of video, so the audio track must be exactly 1 second of the
            // take's real device sample rate. Asserting the appended sample
            // count against that (rather than just "an audio track exists")
            // is what catches an audio track written at the wrong rate or
            // truncated per frame - either of which plays back off-speed
            // against the picture while still passing hasAudio.
            const double sr = sOfflineAudioSampleRate;
            const int expectedFrames = out->offlineFps * out->offlineDurationSeconds;
            const double takeSeconds = (double)expectedFrames / (double)out->offlineFps;
            const long long expectedAudio =
               (long long)std::llround((double)expectedFrames * sr / (double)out->offlineFps);
            const long long gotAudio = out->OfflineAudioFramesAppended();
            // 64 frames = 1.5ms at 44.1kHz: room for one block's rounding on
            // an fps that doesn't divide the rate, and nothing more. A
            // per-frame quota that drifts, truncates, or is budgeted at the
            // wrong rate misses this by orders of magnitude.
            const bool audioExact = sr > 0.0 ? (std::llabs(gotAudio - expectedAudio) <= 64) : info.hasAudio;
            const bool durationOk = info.duration > takeSeconds - 0.1 &&
                                     info.duration < takeSeconds + 0.15;
            // What landed in the FILE, not what was handed to the recorder:
            // frames can be dropped by the encoder queue's byte budget and
            // audio can be dropped by the writer's bounded flush at stop,
            // and both are invisible to the append-side counters above.
            // Two tracks that disagree in length IS the "audio is sped up"
            // symptom, so it is asserted directly.
            const bool tracksAgree = info.hasAudio && info.hasVideo &&
                                      std::fabs(info.audioDuration - info.videoDuration) < 0.1;
            printf("offline render %dfps x %ds movie(video=%d audio=%d duration=%.2fs "
                   "videoTrack=%.2fs audioTrack=%.2fs, frames=%d) "
                   "audio frames=%lld expected=%lld (%+lld) @%.0fHz\n",
                   out->offlineFps, out->offlineDurationSeconds,
                   info.hasVideo, info.hasAudio, info.duration,
                   info.videoDuration, info.audioDuration, out->LastRecordedFrames(),
                   gotAudio, expectedAudio, gotAudio - expectedAudio, sr);
            if (!tracksAgree)
               printf("  A/V MISMATCH: video track %.2fs vs audio track %.2fs (%.1f%% off)\n",
                      info.videoDuration, info.audioDuration,
                      info.videoDuration > 0.0
                         ? 100.0 * (info.audioDuration - info.videoDuration) / info.videoDuration
                         : 0.0);
            const bool ok = out->LastRecordedFrames() == expectedFrames && info.hasVideo &&
                            info.hasAudio && durationOk && audioExact && tracksAgree;
            printf("%s\n", ok ? "OFFLINERENDERTEST OK" : "OFFLINERENDERTEST FAIL - SUSPECT");
         }
      }
}

void FrameTest_OFFLINERENDERREFUSETEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_OFFLINERENDERREFUSETEST") != nullptr && frameId == 2)
      {
         auto* out = static_cast<OutputNode*>(gNodes[1].node.get());
         StartOfflineRenderSession(out);
         printf("start: active=%d status=%s\n", (int)gOfflineRender.active, out->RecordStatus().c_str());
         const bool refused = !gOfflineRender.active &&
                               out->RecordStatus().find("refused") != std::string::npos;
         printf("%s\n", refused ? "OFFLINERENDERREFUSETEST OK" : "OFFLINERENDERREFUSETEST FAIL - SUSPECT");
      }
}

void FrameTest_PATCHTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_PATCHTEST") != nullptr && frameId == 4)
      {
         const std::string path = TmpPath("infinite_roundtrip.infinite");
         const size_t nodesBefore = gNodes.size();
         const bool saved = SavePatchTo(path);

         // Wiped between save and load, so anything that appears afterwards
         // genuinely came out of the file rather than surviving in memory.
         NewPatch();
         const bool cleared = gNodes.empty();
         const bool loaded = LoadPatchFrom(path);

         printf("saved=%d cleared=%d loaded=%d  nodes %zu -> %zu\n",
                saved, cleared, loaded, nodesBefore, gNodes.size());

         GeometryNode* geo = nullptr;
         GeometryOpNode* smooth = nullptr;
         MaterialNode* mat = nullptr;
         Render3DNode* render = nullptr;
         PathNode* path3d = nullptr;
         OutputNode* out = nullptr;
         MetaBallNode* meta = nullptr;
         InstanceOnPointsNode* inst = nullptr;
         for (GraphNode& gn : gNodes)
         {
            if (!geo) geo = dynamic_cast<GeometryNode*>(gn.node.get());
            if (!smooth) smooth = dynamic_cast<GeometryOpNode*>(gn.node.get());
            if (!mat) mat = dynamic_cast<MaterialNode*>(gn.node.get());
            if (!render) render = dynamic_cast<Render3DNode*>(gn.node.get());
            if (!path3d) path3d = dynamic_cast<PathNode*>(gn.node.get());
            if (!out) out = dynamic_cast<OutputNode*>(gn.node.get());
            if (!meta) meta = dynamic_cast<MetaBallNode*>(gn.node.get());
            if (!inst) inst = dynamic_cast<InstanceOnPointsNode*>(gn.node.get());
         }

         const bool haveAll = geo && smooth && mat && render && path3d && out && meta && inst;
         bool params = false, wiring = false, mods = false, geomLinks = false;
         if (haveAll)
         {
            params = geo->shape == 4 && geo->detail == 33 &&
                     std::fabs(geo->posX - 1.25f) < 1e-5f &&
                     std::fabs(geo->color[1] - 0.22f) < 1e-5f &&
                     std::fabs(geo->emission - 2.5f) < 1e-5f &&
                     smooth->iterations == 7 && std::fabs(smooth->amount - 0.66f) < 1e-5f &&
                     std::fabs(mat->metallic - 0.77f) < 1e-5f &&
                     render->samples == 3 && std::fabs(render->exposure - 1.8f) < 1e-5f &&
                     std::fabs(render->width - 512.0f) < 1e-5f &&
                     path3d->shape == PathNode::kHelix &&
                     std::fabs(path3d->turns - 5.0f) < 1e-5f && path3d->pingPong &&
                     out->includeAudio;

            wiring = smooth->input == geo && mat->input == smooth &&
                     render->geometry[0] == mat && render->camera != nullptr &&
                     render->lights[0] != nullptr && out->Input().IsConnected() &&
                     out->AudioInput().IsConnected();

            // Every link this interface collapse touches: two geometry slots
            // on one Render 3D, both mesh-sampling pins plus the cloud pin on
            // Instance on Points, Metaballs' cloud, and Path's curve pin.
            geomLinks = render->geometry[1] == inst &&
                        inst->pointSource != nullptr && inst->instanceShape != nullptr &&
                        inst->cloudSource != nullptr &&
                        meta->cloudSource != nullptr &&
                        path3d->curveSource != nullptr;

            mods = !Modulation::Instance().Links().empty();
         }

         printf("params=%d wiring=%d geomLinks=%d modulation=%d\n", params, wiring, geomLinks, mods);
         printf("%s\n", (saved && cleared && loaded && nodesBefore == gNodes.size() &&
                         haveAll && params && wiring && geomLinks && mods)
                           ? "PATCH ROUND TRIP OK" : "SUSPECT");
      }
}

void FrameTest_AUTOSAVETEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_AUTOSAVETEST") != nullptr && frameId == 4)
      {
         const size_t nodesBefore = gNodes.size();
         const bool wrote = WriteAutosaveNow();

         // Wiped between write and read, so anything that appears afterwards
         // genuinely came out of the autosave rather than surviving in memory.
         NewPatch();
         const bool cleared = gNodes.empty();

         Patch::Data data;
         std::string error;
         const bool read = Patch::Read(AutosavePath(), data, error);
         if (read)
            ApplyPatchData(data);

         printf("wrote=%d cleared=%d read=%d  nodes %zu -> %zu\n",
                wrote, cleared, read, nodesBefore, gNodes.size());

         GeometryNode* geo = nullptr;
         GeometryOpNode* smooth = nullptr;
         MaterialNode* mat = nullptr;
         Render3DNode* render = nullptr;
         PathNode* path = nullptr;
         PaletteNode* palette = nullptr;
         RampNode* ramp = nullptr;
         WavetableNode* wave = nullptr;
         MidiNotesNode* midi = nullptr;
         NoteFilterNode* filter = nullptr;
         OutputNode* out = nullptr;
         for (GraphNode& gn : gNodes)
         {
            if (!geo) geo = dynamic_cast<GeometryNode*>(gn.node.get());
            if (!smooth) smooth = dynamic_cast<GeometryOpNode*>(gn.node.get());
            if (!mat) mat = dynamic_cast<MaterialNode*>(gn.node.get());
            if (!render) render = dynamic_cast<Render3DNode*>(gn.node.get());
            if (!path) path = dynamic_cast<PathNode*>(gn.node.get());
            if (!palette) palette = dynamic_cast<PaletteNode*>(gn.node.get());
            if (!ramp) ramp = dynamic_cast<RampNode*>(gn.node.get());
            if (!wave) wave = dynamic_cast<WavetableNode*>(gn.node.get());
            if (!midi) midi = dynamic_cast<MidiNotesNode*>(gn.node.get());
            if (!filter) filter = dynamic_cast<NoteFilterNode*>(gn.node.get());
            if (!out) out = dynamic_cast<OutputNode*>(gn.node.get());
         }

         const bool haveAll = geo && smooth && mat && render && path && palette &&
                              ramp && wave && midi && filter && out;
         bool params = false, wiring = false, mods = false, palettes = false,
              exprs = false, globals = false;
         if (haveAll)
         {
            params = geo->shape == 4 && geo->detail == 33 &&
                     std::fabs(geo->posX - 1.25f) < 1e-5f &&
                     std::fabs(geo->color[2] - 0.33f) < 1e-5f &&
                     std::fabs(geo->emission - 2.5f) < 1e-5f &&
                     smooth->iterations == 7 && std::fabs(smooth->amount - 0.66f) < 1e-5f &&
                     std::fabs(mat->metallic - 0.77f) < 1e-5f &&
                     std::fabs(mat->roughness - 0.11f) < 1e-5f &&
                     render->samples == 3 && std::fabs(render->exposure - 1.8f) < 1e-5f &&
                     std::fabs(render->width - 512.0f) < 1e-5f &&
                     palette->swatchCount == 3;

            wiring = smooth->input == geo && mat->input == smooth &&
                     render->geometry[0] == mat && render->camera != nullptr &&
                     render->lights[0] != nullptr && out->Input().IsConnected() &&
                     out->AudioInput().IsConnected() &&
                     filter->NoteInputSlot(0) != nullptr && filter->NoteInputSlot(0)->IsConnected();

            mods = !Modulation::Instance().Links().empty();
            palettes = !PaletteBinding::Instance().Links().empty();
            exprs = !Modulation::Instance().Expressions().empty();
            globals = !ExprGlobals::All().empty();
         }

         printf("params=%d wiring=%d modulation=%d palette=%d expr=%d globals=%d\n",
                params, wiring, mods, palettes, exprs, globals);
         printf("%s\n", (wrote && cleared && read && nodesBefore == gNodes.size() &&
                         haveAll && params && wiring && mods && palettes && exprs && globals)
                           ? "AUTOSAVE ROUND TRIP OK" : "SUSPECT");
      }
}

void FrameTest_LIVEISSUETEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_LIVEISSUETEST") != nullptr && frameId == 4)
      {
         const std::string path = "/tmp/infinite_liveissue_test.inf";
         {
            std::ofstream f(path);
            f << "infinite-patch 1\nnode 1 Source Shape\nend\nnode 2 Compositing Blend\nend\n"
                 "node 3 Utility Output\nend\nnode 4 Utility Output\nend\n"
                 "cable 2 0 1\ncable 3 0 2\n";
         }
         LoadPatchFrom(path);
         gLiveIssueEditTime = -10.0; // skip the 300 ms debounce
      }
}

void FrameTest_LIVEISSUETEST_2(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_LIVEISSUETEST") != nullptr && frameId == 8)
      {
         int openInput = 0, outputEmpty = 0, other = 0;
         for (const auto& kv : gLiveIssues)
            for (const Headless::Issue& w : kv.second)
            {
               if (w.code == "W_OPEN_INPUT") openInput++;
               else if (w.code == "W_OUTPUT_EMPTY") outputEmpty++;
               else other++;
            }
         const bool marked = openInput == 1 && outputEmpty == 1 && other == 0;
         // Wire Blend's B input and the second Output: nothing left to say.
         const std::string path = "/tmp/infinite_liveissue_test.inf";
         {
            std::ofstream f(path);
            f << "infinite-patch 1\nnode 1 Source Shape\nend\nnode 2 Compositing Blend\nend\n"
                 "node 3 Utility Output\nend\n"
                 "cable 2 0 1\ncable 2 1 1\ncable 3 0 2\n";
         }
         LoadPatchFrom(path);
         gLiveIssueEditTime = -10.0;
         RefreshLiveIssues();
         printf("LIVEISSUE marked open=%d empty=%d other=%d -> %s; after fix issues=%zu -> %s\n",
                openInput, outputEmpty, other, marked ? "OK" : "FAIL",
                gLiveIssues.size(), gLiveIssues.empty() ? "OK" : "FAIL");
      }
}

void FrameTest_SHAPERESGRAPHTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_SHAPERESGRAPHTEST") != nullptr && frameId == 4)
      {
         const std::string path = "/tmp/infinite_shaperes_graph.inf";
         {
            std::ofstream f(path);
            f << "infinite-patch 1\nnode 1 3D Cube\nend\n";
            for (int i = 0; i < 8; i++)
               f << "node " << (2 + i) << " AudioEffects Shape Resonator\nend\n";
            for (int i = 0; i < 8; i++)
               f << "geo " << (2 + i) << " 1 1\n";
         }
         LoadPatchFrom(path);
      }
}
}
