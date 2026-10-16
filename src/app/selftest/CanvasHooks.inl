// Part of src/main.cpp, included there in order (one translation unit).
// INFINITE_*TEST canvas hooks (dev runs only)

      if (getenv("INFINITE_COLORTEST") != nullptr)
      {
         auto* sh = static_cast<ShapeNode*>(gNodes[0].node.get());
         if (frameId == 7)
            printf("before click: fill=(%.2f, %.2f, %.2f)\n", sh->fillColor[0], sh->fillColor[1], sh->fillColor[2]);
         if (frameId == 11)
         {
            printf("after  click: fill=(%.2f, %.2f, %.2f)  %s\n",
                   sh->fillColor[0], sh->fillColor[1], sh->fillColor[2],
                   (std::fabs(sh->fillColor[0] - 0.9f) > 0.02f ||
                    std::fabs(sh->fillColor[1] - 0.35f) > 0.02f ||
                    std::fabs(sh->fillColor[2] - 0.2f) > 0.02f) ? "PICKER RESPONDED OK" : "NO CHANGE - BUG");
         }
      }

      if (getenv("INFINITE_RECTEST") != nullptr)
      {
         auto* out = static_cast<OutputNode*>(gNodes[1].node.get());
         if (frameId == 2)
         {
            out->recordFps = 30;
            bool started = out->StartRecording("/tmp/infinite_rectest.mov");
            printf("start recording: %d (%s)\n", (int)started, out->RecordStatus().c_str());
         }
         if (frameId == 5)
         {
            // pause mid-take: the clock must freeze, but frames keep encoding
            Transport::Instance().SetPlaying(false);
            printf("paused at beats=%.4f\n", Transport::Instance().Beats());
         }
         if (frameId == 12)
            printf("beats while paused=%.4f (should be unchanged)\n", Transport::Instance().Beats());
         if (frameId == 13)
            Transport::Instance().SetPlaying(true);
         if (frameId == 40)
         {
            printf("frames captured: %d\n", out->RecordedFrames());
            out->StopRecording();
            printf("stop: %s\n", out->RecordStatus().c_str());
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      if (getenv("INFINITE_RESYNTHTEST") != nullptr)
      {
         auto* rs = static_cast<ResynthNode*>(gNodes[1].node.get());
         auto sample = [](INode* n, unsigned char* out)
         {
            GLuint fbo = 0;
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, n->GetOutputTexture(), 0);
            glReadPixels(n->GetOutputWidth() / 3, n->GetOutputHeight() / 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, out);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &fbo);
         };
         static unsigned char prev[4] = { 0, 0, 0, 0 };
         if (frameId == 2)
         {
            rs->chaos = 0.8f; rs->mutation = 0.8f; rs->sourcePull = 0.02f;
            rs->Randomise();
            sample(rs, prev);
            printf("gen %-3d pixel=(%d,%d,%d)\n", rs->Generation(), prev[0], prev[1], prev[2]);
         }
         if (frameId >= 3 && frameId <= 12)
         {
            rs->StepOnce();
            unsigned char now[4];
            sample(rs, now);
            int drift = abs(now[0]-prev[0]) + abs(now[1]-prev[1]) + abs(now[2]-prev[2]);
            printf("gen %-3d pixel=(%d,%d,%d) drift=%d\n", rs->Generation(), now[0], now[1], now[2], drift);
            memcpy(prev, now, 4);
         }
         if (frameId == 14)
            glfwSetWindowShouldClose(window, GLFW_TRUE);
      }

      if (getenv("INFINITE_BYPASSTEST") != nullptr)
      {
         auto sample = [](INode* n, unsigned char* out)
         {
            GLuint fbo = 0;
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, n->GetOutputTexture(), 0);
            glReadPixels(n->GetOutputWidth()/2, n->GetOutputHeight()/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, out);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &fbo);
         };
         unsigned char px[4];
         if (frameId == 3)
         {
            sample(gNodes[2].node.get(), px);
            printf("invert active:   output=(%d,%d,%d) %s\n", px[0], px[1], px[2],
                   px[0] < 40 ? "inverted OK" : "UNEXPECTED");
            gNodes[1].node->bypassed = true;
         }
         if (frameId == 6)
         {
            sample(gNodes[2].node.get(), px);
            printf("invert bypassed: output=(%d,%d,%d) %s\n", px[0], px[1], px[2],
                   px[0] > 200 ? "PASSED THROUGH OK" : "STILL INVERTED - BUG");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      if (getenv("INFINITE_PHASEATEST") != nullptr && frameId == 4)
      {
         // Every primitive and 2D shape must be spawnable by its own name and
         // come out preset to that shape, not to the class default.
         bool allShapes = true;
         for (int i = 0; i < (int)GeometryNode::ShapeNames().size(); i++)
         {
            const std::string& name = GeometryNode::ShapeNames()[i];
            INode* made = NodeFactory::Instance().MakeNode(name);
            auto* geo = dynamic_cast<GeometryNode*>(made);
            // GetMesh() first: the mesh is built lazily, so TriangleCount is 0
            // until something asks for it.
            const bool ok = geo != nullptr && geo->shape == i &&
                            !geo->GetMesh().Empty();
            if (!ok) { allShapes = false; printf("  failed: %s\n", name.c_str()); }
            delete made;
         }
         bool allShapes2D = true;
         for (int i = 0; i < (int)ShapeNode::ShapeNames().size(); i++)
         {
            INode* made = NodeFactory::Instance().MakeNode(ShapeNode::ShapeNames()[i]);
            auto* sh = dynamic_cast<ShapeNode*>(made);
            if (sh == nullptr || sh->shapeType != i) allShapes2D = false;
            delete made;
         }
         printf("primitives spawnable by name: 3D=%d 2D=%d\n", (int)allShapes, (int)allShapes2D);
         // Two nodes sharing a name is a silent bug: one of them becomes
         // unspawnable and any patch naming it loads the wrong node.
         const std::vector<std::string>& dupes = NodeFactory::Instance().DuplicateNames();
         for (const std::string& d : dupes)
            printf("  duplicate node name: %s\n", d.c_str());
         printf("unique node names: %s\n", dupes.empty() ? "OK" : "FAIL");
         allShapes = allShapes && dupes.empty();

         // Bevel must actually round a cube - more triangles, and a smaller
         // extent than the original since the corners get pulled in.
         const Mesh cube = Primitives::Cube(1);
         const Mesh rounded = MeshOps::Bevel(cube, 0.6f, 2);
         // Measured as the furthest vertex from the centre, not the bounding
         // box: a bevel rounds the corners while leaving the flat faces exactly
         // where they were, so the box does not shrink at all. The corners are
         // the only thing that moves, and they are what this catches.
         auto maxRadius = [](const Mesh& m) {
            float worst = 0.0f;
            for (const Vertex& v : m.vertices)
               worst = std::max(worst, std::sqrt(v.px*v.px + v.py*v.py + v.pz*v.pz));
            return worst;
         };
         const float r0 = maxRadius(cube), r1 = maxRadius(rounded);
         printf("bevel: %zu -> %zu tris, corner radius %.3f -> %.3f (faces stay put)\n",
                cube.indices.size() / 3, rounded.indices.size() / 3, r0, r1);
         const bool bevelled = rounded.indices.size() > cube.indices.size() &&
                               r1 < r0 - 0.02f && r1 > 0.3f;

         printf("%s\n", (allShapes && allShapes2D && bevelled) ? "PHASE A OK" : "SUSPECT");
      }

      if (getenv("INFINITE_SELECTTEST") != nullptr && frameId == 4)
      {
         const Mesh cube = Primitives::Cube(1);
         printf("cube: %zu faces, %zu selected by default\n",
                cube.FaceCount(), cube.SelectedCount());
         // An empty mask has to mean "everything", or every operator would need
         // to special-case a mesh that has never been through a Select.
         const bool defaultAll = cube.SelectedCount() == cube.FaceCount();

         // The top of a cube is two triangles: selecting by normal must find
         // exactly those, which is the check that the mode means what it says.
         const Mesh top = MeshOps::Select(cube, MeshOps::kSelectNormal,
                                          0.9f, 0.0f, 1.0f, 1, 0.0f, false, false);
         printf("faces pointing +Y: %zu of %zu  %s\n", top.SelectedCount(), top.FaceCount(),
                top.SelectedCount() == 2 ? "OK" : "FAIL");

         // Deleting them must remove exactly those two and nothing else.
         const Mesh cut = MeshOps::DeleteSelected(top, false);
         printf("delete the selection: %zu faces  %s\n", cut.FaceCount(),
                cut.FaceCount() == cube.FaceCount() - 2 ? "OK" : "FAIL");

         // Keeping instead of deleting is the exact complement.
         const Mesh kept = MeshOps::DeleteSelected(top, true);
         printf("keep instead: %zu faces  %s\n", kept.FaceCount(),
                kept.FaceCount() == 2 ? "OK" : "FAIL");

         // Moving the selection must move only the top, so the maximum y rises
         // while the minimum stays exactly where it was.
         auto rangeY = [](const Mesh& m) {
            float lo = 1e30f, hi = -1e30f;
            for (const Vertex& v : m.vertices) { lo = std::min(lo, v.py); hi = std::max(hi, v.py); }
            return std::pair<float,float>(lo, hi);
         };
         const Mesh moved = MeshOps::TransformSelected(top, Mat4::Identity(), true, 0.5f);
         const auto before = rangeY(cube);
         const auto after = rangeY(moved);
         printf("move top by 0.5: y %.2f..%.2f -> %.2f..%.2f  %s\n",
                before.first, before.second, after.first, after.second,
                (std::fabs(after.second - (before.second + 0.5f)) < 0.01f &&
                 std::fabs(after.first - before.first) < 0.01f) ? "OK" : "FAIL");
         const bool movedOk = std::fabs(after.second - (before.second + 0.5f)) < 0.01f &&
                              std::fabs(after.first - before.first) < 0.01f;

         // Extruding the selection adds a cap and walls but leaves the rest.
         const Mesh extruded = MeshOps::ExtrudeSelected(top, 0.4f, 0.2f);
         printf("extrude top: %zu -> %zu faces  %s\n", cube.FaceCount(), extruded.FaceCount(),
                extruded.FaceCount() > cube.FaceCount() ? "OK" : "FAIL");

         // Random selection has to be reproducible from its seed, or a patch
         // would look different every time it was opened.
         const Mesh r1 = MeshOps::Select(cube, MeshOps::kSelectRandom, 0.5f, 0, 0, 1, 7.0f, false, false);
         const Mesh r2 = MeshOps::Select(cube, MeshOps::kSelectRandom, 0.5f, 0, 0, 1, 7.0f, false, false);
         const Mesh r3 = MeshOps::Select(cube, MeshOps::kSelectRandom, 0.5f, 0, 0, 1, 9.0f, false, false);
         const bool reproducible = r1.faceMask == r2.faceMask && r1.faceMask != r3.faceMask;
         printf("random selection reproducible from seed: %d\n", (int)reproducible);

         // Mesh to Points billboards each point as two triangles. Selecting at
         // ~50% must choose or skip both triangles of a point together - never
         // one triangle selected and its partner not, which is what tore
         // points in half before points carried a selectionGroup tag.
         const std::vector<MeshPoint> cloud = MeshOps::ToPoints(cube, 0, 10000);
         const Mesh billboards = MeshOps::PointsToFaces(cloud, 0.2f);
         const Mesh pointSel = MeshOps::Select(billboards, MeshOps::kSelectRandom,
                                                0.5f, 0, 0, 1, 3.0f, false, false);
         bool quadsIntact = !pointSel.faceMask.empty();
         for (size_t f = 0; f + 1 < pointSel.FaceCount(); f += 2)
            if (pointSel.faceMask[f] != pointSel.faceMask[f + 1])
               quadsIntact = false;
         printf("point selection keeps quads whole: %zu points, %zu tris  %s\n",
                cloud.size(), pointSel.FaceCount(), quadsIntact ? "OK" : "FAIL");

         const bool ok = defaultAll && top.SelectedCount() == 2 &&
                         cut.FaceCount() == cube.FaceCount() - 2 && kept.FaceCount() == 2 &&
                         movedOk && extruded.FaceCount() > cube.FaceCount() && reproducible &&
                         quadsIntact;
         printf("%s\n", ok ? "SELECTION OK" : "SUSPECT");
      }

      if (getenv("INFINITE_DISTRIBUTETEST") != nullptr && frameId == 4)
      {
         // Phase 6: point distribution. A UV sphere so pole clumping (the bug
         // Distribute Points on Faces exists to fix) has something to show up
         // on; a coarse one so the check runs fast.
         const Mesh sphere = Primitives::Sphere(24, 24);

         // Coverage should be roughly uniform per unit area: split the sphere
         // by hemisphere (near the pole vs. near the equator band) using the
         // sampled points' own y, and the two counts should be much closer to
         // each other than ToPoints' index-stride sampling would give, since
         // Primitives::Sphere bunches rings toward the poles.
         const std::vector<MeshPoint> scattered =
            MeshOps::DistributeOnFaces(sphere, 400.0f, 1.0f, MeshOps::kDistributeRandom, 0.0f);
         // Primitives::Sphere bakes radius 0.5 into its positions (py in
         // -0.5..0.5), so the pole/equator split is relative to that, not 1.0.
         int poleCount = 0, equatorCount = 0;
         for (const MeshPoint& p : scattered)
         {
            if (std::fabs(p.py) > 0.35f) poleCount++;
            else if (std::fabs(p.py) < 0.15f) equatorCount++;
         }
         // Both bands cover a comparable fraction of the sphere's surface
         // area, so a uniform scatter should land within roughly 2x of each
         // other; the old index-stride sampler leaves the poles nearly empty
         // by comparison.
         const bool uniformCoverage = poleCount > 0 && equatorCount > 0 &&
            (float)poleCount / (float)equatorCount > 0.4f &&
            (float)poleCount / (float)equatorCount < 2.5f;
         printf("distribute on faces: %zu points, pole=%d equator=%d  %s\n",
                scattered.size(), poleCount, equatorCount, uniformCoverage ? "OK" : "FAIL");

         // Same seed -> identical scatter (a reopened patch must not reshuffle
         // its points); a different seed -> a different one.
         const std::vector<MeshPoint> s1 =
            MeshOps::DistributeOnFaces(sphere, 200.0f, 5.0f, MeshOps::kDistributeRandom, 0.0f);
         const std::vector<MeshPoint> s2 =
            MeshOps::DistributeOnFaces(sphere, 200.0f, 5.0f, MeshOps::kDistributeRandom, 0.0f);
         const std::vector<MeshPoint> s3 =
            MeshOps::DistributeOnFaces(sphere, 200.0f, 6.0f, MeshOps::kDistributeRandom, 0.0f);
         bool sameSeedIdentical = s1.size() == s2.size() && !s1.empty();
         for (size_t i = 0; sameSeedIdentical && i < s1.size(); i++)
            if (s1[i].px != s2[i].px || s1[i].py != s2[i].py || s1[i].pz != s2[i].pz)
               sameSeedIdentical = false;
         bool diffSeedDiffers = s1.size() != s3.size();
         if (!diffSeedDiffers)
            for (size_t i = 0; i < s1.size(); i++)
               if (s1[i].px != s3[i].px || s1[i].py != s3[i].py || s1[i].pz != s3[i].pz)
                  { diffSeedDiffers = true; break; }
         printf("scatter reproducible from seed: same=%d diff=%d  %s\n",
                (int)sameSeedIdentical, (int)diffSeedDiffers,
                (sameSeedIdentical && diffSeedDiffers) ? "OK" : "FAIL");

         // Poisson disk must respect minDistance (no two accepted points
         // closer than it) and must stop adding points once spacing
         // saturates, rather than packing them arbitrarily tight.
         const std::vector<MeshPoint> poisson =
            MeshOps::DistributeOnFaces(sphere, 5000.0f, 2.0f, MeshOps::kDistributePoisson, 0.15f);
         bool minDistanceHeld = true;
         for (size_t i = 0; minDistanceHeld && i < poisson.size(); i++)
            for (size_t j = i + 1; minDistanceHeld && j < poisson.size(); j++)
            {
               const float dx = poisson[i].px - poisson[j].px;
               const float dy = poisson[i].py - poisson[j].py;
               const float dz = poisson[i].pz - poisson[j].pz;
               if (std::sqrt(dx*dx + dy*dy + dz*dz) < 0.15f - 1e-4f)
                  minDistanceHeld = false;
            }
         // A far higher density than the packing can actually hold - if
         // saturation didn't stop the search, this would either hang or spill
         // past what minDistance allows.
         const std::vector<MeshPoint> poissonSaturated =
            MeshOps::DistributeOnFaces(sphere, 200000.0f, 2.0f, MeshOps::kDistributePoisson, 0.15f);
         const bool saturates = poissonSaturated.size() < 5000;
         printf("poisson disk: %zu points, min-distance held=%d, saturates at high density=%d (%zu)  %s\n",
                poisson.size(), (int)minDistanceHeld, (int)saturates, poissonSaturated.size(),
                (minDistanceHeld && saturates) ? "OK" : "FAIL");

         // Merge by Distance: zero threshold is a no-op; a large one collapses
         // a subdivided cube's duplicated seam vertices.
         const Mesh cube = Primitives::Cube(1);
         const Mesh subdivided = MeshOps::Subdivide(cube, 2, 0.0f);
         const Mesh noOp = MeshOps::MergeByDistance(subdivided, 0.0f);
         const Mesh merged = MeshOps::MergeByDistance(subdivided, 2.0f);
         const bool mergeNoOp = noOp.vertices.size() == subdivided.vertices.size() &&
                                noOp.indices.size() == subdivided.indices.size();
         const bool mergeCollapses = merged.vertices.size() < subdivided.vertices.size() &&
                                     merged.indices.size() < subdivided.indices.size();
         printf("merge by distance: %zu verts -> noop %zu, threshold=2 %zu  %s\n",
                subdivided.vertices.size(), noOp.vertices.size(), merged.vertices.size(),
                (mergeNoOp && mergeCollapses) ? "OK" : "FAIL");

         // Points to Vertices: a vertices-only mesh trips Mesh::Empty() (no
         // indices) but must still report HasGeometry(), carry every alive
         // particle's position/colour, and drop dead ones when asked to.
         struct CloudProbe : public IGeometrySource
         {
            std::vector<Particle> cloud;
            Mesh dummy;
            const Mesh& GetMesh() override { return dummy; }
            unsigned long long MeshRevision() override { return 1; }
            const std::vector<Particle>* GetPointCloud() override { return &cloud; }
            unsigned long long PointCloudRevision() override { return 1; }
            Mat4 GetModelMatrix() const override { return Mat4::Identity(); }
            Material GetMaterial() const override { return Material(); }
            unsigned int GetSurfaceTexture() override { return 0; }
         };
         CloudProbe cloudProbe;
         for (int i = 0; i < 5; i++)
         {
            Particle p;
            p.px = (float)i; p.r = 0.25f * i;
            p.alive = (i != 2); // one dead particle in the middle
            cloudProbe.cloud.push_back(p);
         }
         PointsToVerticesNode p2v;
         p2v.input = &cloudProbe;
         p2v.aliveOnly = true;
         p2v.CookIfNeeded(40000);
         const Mesh& p2vMesh = p2v.GetMesh();
         const bool p2vOk = p2vMesh.Empty() && p2vMesh.HasGeometry() &&
                            p2vMesh.vertices.size() == 4 && p2vMesh.HasVertexColor();
         printf("points to vertices: %zu verts (want 4, dead dropped), Empty=%d HasGeometry=%d  %s\n",
                p2vMesh.vertices.size(), (int)p2vMesh.Empty(), (int)p2vMesh.HasGeometry(),
                p2vOk ? "OK" : "FAIL");

         // Distribute Points in Grid: exact count, and row-major/cell-centre
         // ordering matching ImageToPointsNode's convention (see
         // GenerativeNodes.cpp) so the two correspond point-for-point.
         DistributePointsInGridNode grid;
         grid.countX = 4; grid.countY = 3;
         grid.spacingX = 0.5f; grid.spacingY = 0.5f;
         grid.jitter = 0.0f;
         grid.CookIfNeeded(41000);
         const std::vector<Particle>& gridPoints = grid.GetPoints();
         bool gridOrderOk = gridPoints.size() == 12;
         if (gridOrderOk)
         {
            // Index 1 is (gx=1, gy=0): one spacing right of index 0, same row.
            const float dx = gridPoints[1].px - gridPoints[0].px;
            const float dy = gridPoints[1].py - gridPoints[0].py;
            // Index 4 is (gx=0, gy=1): one spacing up from index 0, same column.
            const float dxRow = gridPoints[4].px - gridPoints[0].px;
            const float dyRow = gridPoints[4].py - gridPoints[0].py;
            gridOrderOk = std::fabs(dx - 0.5f) < 1e-3f && std::fabs(dy) < 1e-3f &&
                          std::fabs(dxRow) < 1e-3f && std::fabs(dyRow - 0.5f) < 1e-3f;
         }
         printf("distribute in grid: %zu points (want 12), row-major order=%d  %s\n",
                gridPoints.size(), (int)gridOrderOk, gridOrderOk ? "OK" : "FAIL");

         // Render3D's cloud sprite path scales its unit quad by Particle::scale
         // directly (drawCloudSlot), not by pointSize - a node that forgets to
         // derive scale from pointSize renders every sprite at a fixed size
         // regardless of the param, which the mini-viewport preview (built off
         // GetMesh(), a different code path) would not have caught either.
         const bool gridScaleOk = !gridPoints.empty() &&
            std::fabs(gridPoints[0].scale - grid.pointSize * 0.5f) < 1e-4f;
         printf("distribute in grid: particle scale %.4f (want %.4f from pointSize)  %s\n",
                gridPoints.empty() ? -1.0f : gridPoints[0].scale, grid.pointSize * 0.5f,
                gridScaleOk ? "OK" : "FAIL");

         // Same check for Distribute Points on Faces' own particle output, not
         // just the raw MeshOps::DistributeOnFaces samples checked above.
         GeometryNode probeMeshForGrid;
         probeMeshForGrid.shape = 2; // sphere
         probeMeshForGrid.detail = 4;
         DistributePointsOnFacesNode distFacesScaleNode;
         distFacesScaleNode.input = &probeMeshForGrid;
         distFacesScaleNode.pointSize = 0.2f;
         distFacesScaleNode.density = 20.0f;
         distFacesScaleNode.CookIfNeeded(42000);
         const std::vector<Particle>& facesPoints = distFacesScaleNode.GetPoints();
         const bool facesScaleOk = !facesPoints.empty() &&
            std::fabs(facesPoints[0].scale - distFacesScaleNode.pointSize * 0.5f) < 1e-4f;
         printf("distribute on faces: particle scale %.4f (want %.4f from pointSize)  %s\n",
                facesPoints.empty() ? -1.0f : facesPoints[0].scale,
                distFacesScaleNode.pointSize * 0.5f, facesScaleOk ? "OK" : "FAIL");

         const bool ok = uniformCoverage && sameSeedIdentical && diffSeedDiffers &&
                         minDistanceHeld && saturates && mergeNoOp && mergeCollapses &&
                         p2vOk && gridOrderOk && gridScaleOk && facesScaleOk;
         printf("%s\n", ok ? "DISTRIBUTE OK" : "SUSPECT");
      }

      if (getenv("INFINITE_PHASE4TEST") != nullptr && frameId == 4)
      {
         // Phase 4 (selection as an input): GeometryOpNode::selectionOnly.
         GeometryNode cube;
         cube.shape = 1; cube.detail = 0; // plain 12-triangle cube
         cube.CookIfNeeded(30000);

         GeometryOpNode select;
         select.input = &cube;
         select.op = GeometryOpNode::kSelect;
         select.selectMode = MeshOps::kSelectNormal;
         select.axis = 1; select.selectA = 0.9f; select.selectC = 1.0f; // +Y top

         auto rangeY = [](const Mesh& m) {
            float lo = 1e30f, hi = -1e30f;
            for (const Vertex& v : m.vertices) { lo = std::min(lo, v.py); hi = std::max(hi, v.py); }
            return std::pair<float, float>(lo, hi);
         };

         // Done means #6: Cube -> Select(+Y) -> Transform, selectionOnly on
         // moves only the top; off moves the whole cube.
         GeometryOpNode moveOn;
         moveOn.input = &select; moveOn.op = GeometryOpNode::kTransform;
         moveOn.selectionOnly = true; moveOn.offsetY = 0.5f;
         // Isolate the matrix-only effect being checked below - the default
         // moveAlongNormals=true would add its own +0.2 on top of offsetY.
         moveOn.moveAlongNormals = false;
         moveOn.CookIfNeeded(30001);
         const auto onRange = rangeY(moveOn.GetMesh());
         const auto baseRange = rangeY(cube.GetMesh());
         const bool onOk = std::fabs(onRange.second - (baseRange.second + 0.5f)) < 0.01f &&
                           std::fabs(onRange.first - baseRange.first) < 0.01f;

         GeometryOpNode moveOff;
         moveOff.input = &select; moveOff.op = GeometryOpNode::kTransform;
         moveOff.selectionOnly = false; moveOff.offsetY = 0.5f;
         moveOff.CookIfNeeded(30002);
         const auto offRange = rangeY(moveOff.GetMesh());
         const bool offOk = std::fabs(offRange.second - (baseRange.second + 0.5f)) < 0.01f &&
                            std::fabs(offRange.first - (baseRange.first + 0.5f)) < 0.01f;
         printf("transform: selectionOnly on moves top only (%.2f..%.2f), off moves whole cube (%.2f..%.2f)  %s\n",
                onRange.first, onRange.second, offRange.first, offRange.second,
                (onOk && offOk) ? "OK" : "FAIL");

         // kDelete: on removes just the 2 selected faces, off removes all 12
         // (see the Op enum comment - "useless but consistent").
         GeometryOpNode delOn;
         delOn.input = &select; delOn.op = GeometryOpNode::kDelete; delOn.selectionOnly = true;
         delOn.CookIfNeeded(30003);
         const size_t delOnFaces = delOn.GetMesh().FaceCount();

         GeometryOpNode delOff;
         delOff.input = &select; delOff.op = GeometryOpNode::kDelete; delOff.selectionOnly = false;
         delOff.CookIfNeeded(30004);
         const size_t delOffFaces = delOff.GetMesh().FaceCount();
         const bool deleteOk = delOnFaces == cube.GetMesh().FaceCount() - 2 && delOffFaces == 0;
         printf("delete: selectionOnly on -> %zu faces, off -> %zu faces  %s\n",
                delOnFaces, delOffFaces, deleteOk ? "OK" : "FAIL");

         // kExtrude: on only extrudes the top (small triangle gain), off
         // extrudes the entire mesh as one region (bigger gain, and the two
         // must differ - both use MeshOps::Extrude, gated by whether the mask
         // reaches it or gets cleared first).
         GeometryOpNode extOn;
         extOn.input = &select; extOn.op = GeometryOpNode::kExtrude;
         extOn.selectionOnly = true; extOn.thickness = 0.3f;
         extOn.CookIfNeeded(30005);
         const size_t extOnFaces = extOn.GetMesh().FaceCount();

         GeometryOpNode extOff;
         extOff.input = &select; extOff.op = GeometryOpNode::kExtrude;
         extOff.selectionOnly = false; extOff.thickness = 0.3f;
         extOff.CookIfNeeded(30006);
         const size_t extOffFaces = extOff.GetMesh().FaceCount();
         const bool extrudeOk = extOnFaces > cube.GetMesh().FaceCount() &&
                                extOffFaces > cube.GetMesh().FaceCount() &&
                                extOnFaces != extOffFaces;
         printf("extrude: selectionOnly on -> %zu faces, off -> %zu faces  %s\n",
                extOnFaces, extOffFaces, extrudeOk ? "OK" : "FAIL");

         // Done means #5, the highest-risk check in the phase: a patch saved
         // before this change used the (now-deprecated) kTransformSelected/
         // kDeleteSelected/kExtrudeSelected ops with no `selectionOnly` key at
         // all - that param didn't exist yet. Simulate exactly that save,
         // load it the way ApplyPatchData does (LoadParams then
         // MigrateDeprecatedOp), and check both that the migration lands on
         // the right (op, selectionOnly) and that the geometry it produces
         // matches what the old deprecated op (still handled in GetMesh's
         // switch, for defence in depth) would have produced.
         auto stripSelectionOnly = [](std::vector<std::pair<std::string, std::string>>& params) {
            params.erase(std::remove_if(params.begin(), params.end(),
               [](const std::pair<std::string, std::string>& kv) { return kv.first == "selectionOnly"; }),
               params.end());
         };
         auto sameMesh = [](const Mesh& a, const Mesh& b) {
            if (a.vertices.size() != b.vertices.size() || a.indices.size() != b.indices.size())
               return false;
            for (size_t i = 0; i < a.vertices.size(); i++)
               if (std::fabs(a.vertices[i].px - b.vertices[i].px) > 1e-5f ||
                   std::fabs(a.vertices[i].py - b.vertices[i].py) > 1e-5f ||
                   std::fabs(a.vertices[i].pz - b.vertices[i].pz) > 1e-5f)
                  return false;
            return true;
         };

         bool migrationOk = true;
         {
            GeometryOpNode oldNode;
            oldNode.input = &select; oldNode.op = GeometryOpNode::kTransformSelected;
            oldNode.offsetY = 0.5f; oldNode.moveAlongNormals = true; oldNode.normalAmount = 0.3f;
            oldNode.CookIfNeeded(30010);
            const Mesh preMigration = oldNode.GetMesh();

            std::vector<std::pair<std::string, std::string>> params;
            Patch::SaveParams(&oldNode, params);
            stripSelectionOnly(params);

            GeometryOpNode migrated;
            Patch::LoadParams(&migrated, params);
            migrated.MigrateDeprecatedOp();
            migrated.input = &select;
            const bool fieldsOk = migrated.op == GeometryOpNode::kTransform && migrated.selectionOnly;
            migrated.CookIfNeeded(30011);
            const bool meshOk = sameMesh(preMigration, migrated.GetMesh());
            migrationOk = migrationOk && fieldsOk && meshOk;
            printf("migrate Transform Selected -> Transform+selectionOnly: fields=%d mesh=%d  %s\n",
                   fieldsOk, meshOk, (fieldsOk && meshOk) ? "OK" : "FAIL");
         }
         {
            GeometryOpNode oldNode;
            oldNode.input = &select; oldNode.op = GeometryOpNode::kDeleteSelected;
            oldNode.keepSelected = true;
            oldNode.CookIfNeeded(30012);
            const Mesh preMigration = oldNode.GetMesh();

            std::vector<std::pair<std::string, std::string>> params;
            Patch::SaveParams(&oldNode, params);
            stripSelectionOnly(params);

            GeometryOpNode migrated;
            Patch::LoadParams(&migrated, params);
            migrated.MigrateDeprecatedOp();
            migrated.input = &select;
            const bool fieldsOk = migrated.op == GeometryOpNode::kDelete && migrated.selectionOnly;
            migrated.CookIfNeeded(30013);
            const bool meshOk = sameMesh(preMigration, migrated.GetMesh());
            migrationOk = migrationOk && fieldsOk && meshOk;
            printf("migrate Delete Selected -> Delete+selectionOnly: fields=%d mesh=%d  %s\n",
                   fieldsOk, meshOk, (fieldsOk && meshOk) ? "OK" : "FAIL");
         }
         {
            GeometryOpNode oldNode;
            oldNode.input = &select; oldNode.op = GeometryOpNode::kExtrudeSelected;
            oldNode.thickness = 0.3f; oldNode.inset = 0.1f;
            oldNode.CookIfNeeded(30014);
            const Mesh preMigration = oldNode.GetMesh();

            std::vector<std::pair<std::string, std::string>> params;
            Patch::SaveParams(&oldNode, params);
            stripSelectionOnly(params);

            GeometryOpNode migrated;
            Patch::LoadParams(&migrated, params);
            migrated.MigrateDeprecatedOp();
            migrated.input = &select;
            const bool fieldsOk = migrated.op == GeometryOpNode::kExtrude && migrated.selectionOnly;
            migrated.CookIfNeeded(30015);
            // kExtrude (general) is region-merged while ExtrudeSelected is a
            // simpler per-face wall - not byte-identical, but must select the
            // same faces and add geometry rather than silently no-op.
            const Mesh& migratedMesh = migrated.GetMesh();
            const bool meshOk = migratedMesh.FaceCount() > cube.GetMesh().FaceCount() &&
                                preMigration.FaceCount() > cube.GetMesh().FaceCount();
            migrationOk = migrationOk && fieldsOk && meshOk;
            printf("migrate Extrude Selected -> Extrude+selectionOnly: fields=%d mesh=%d  %s\n",
                   fieldsOk, meshOk, (fieldsOk && meshOk) ? "OK" : "FAIL");
         }

         const bool phase4Ok = onOk && offOk && deleteOk && extrudeOk && migrationOk;
         printf("%s\n", phase4Ok ? "PHASE 4 OK" : "SUSPECT");
      }

      if (getenv("INFINITE_PADPATHTEST") != nullptr && frameId == 4)
      {
         // A real recorded performance, not the generic mutator's mangled
         // text, round-tripping through both save/load and copy/paste.
         ShapeNode src;
         src.shapeType = 0;
         src.width = 64; src.height = 64;
         src.CookIfNeeded(9500);

         ResynthNode a;
         a.Input().Connect(&src);
         Transport::Instance().SetPlaying(true);
         Transport::Instance().Rewind();
         a.StartRecording();
         const float xs[] = { 0.1f, 0.3f, 0.6f, 0.9f, 0.2f };
         for (int i = 0; i < 5; i++)
         {
            a.padX = xs[i];
            a.padY = 1.0f - xs[i];
            a.CookIfNeeded(9510 + i);
            Transport::Instance().Tick(0.3f);
         }
         a.StopRecording();
         const size_t recorded = a.Path().size();

         auto pathsMatch = [](const std::vector<ResynthNode::PadPoint>& p,
                              const std::vector<ResynthNode::PadPoint>& q) {
            if (p.size() != q.size())
               return false;
            for (size_t i = 0; i < p.size(); i++)
               if (std::fabs(p[i].x - q[i].x) > 1e-5f || std::fabs(p[i].y - q[i].y) > 1e-5f ||
                   std::fabs(p[i].beat - q[i].beat) > 1e-6)
                  return false;
            return true;
         };

         ResynthNode b;
         CopyParams(&b, &a);
         const bool copyOk = pathsMatch(a.Path(), b.Path());
         printf("resynth pad path: %zu points recorded, copy/paste preserved %zu  %s\n",
                recorded, b.Path().size(), copyOk && recorded > 0 ? "OK" : "FAIL");

         std::vector<std::pair<std::string, std::string>> params;
         Patch::SaveParams(&a, params);
         ResynthNode c;
         Patch::LoadParams(&c, params);
         const bool loadOk = pathsMatch(a.Path(), c.Path());
         printf("resynth pad path: save/load preserved %zu  %s\n",
                c.Path().size(), loadOk ? "OK" : "FAIL");

         // Same check for Macro XY's pad, which records identically.
         MacroXYNode m;
         Transport::Instance().Rewind();
         m.StartRecording();
         for (int i = 0; i < 5; i++)
         {
            m.padX = xs[i];
            m.padY = 1.0f - xs[i];
            m.CookIfNeeded(9520 + i);
            Transport::Instance().Tick(0.3f);
         }
         m.StopRecording();
         const size_t mRecorded = m.Path().size();

         auto macroPathsMatch = [](const std::vector<MacroXYNode::PadPoint>& p,
                                   const std::vector<MacroXYNode::PadPoint>& q) {
            if (p.size() != q.size())
               return false;
            for (size_t i = 0; i < p.size(); i++)
               if (std::fabs(p[i].x - q[i].x) > 1e-5f || std::fabs(p[i].y - q[i].y) > 1e-5f ||
                   std::fabs(p[i].beat - q[i].beat) > 1e-6)
                  return false;
            return true;
         };
         MacroXYNode m2;
         CopyParams(&m2, &m);
         const bool mCopyOk = macroPathsMatch(m.Path(), m2.Path());
         printf("macro xy pad path: %zu points recorded, copy/paste preserved %zu  %s\n",
                mRecorded, m2.Path().size(), mCopyOk && mRecorded > 0 ? "OK" : "FAIL");

         const bool ok = copyOk && loadOk && recorded > 0 && mCopyOk && mRecorded > 0;
         printf("%s\n", ok ? "PAD PATH OK" : "SUSPECT");
      }

      if (getenv("INFINITE_UNDOTEST") != nullptr && frameId == 4)
      {
         NewPatch(); // also clears the undo/redo stacks - a clean baseline

         bool ok = true;

         // --- spawn / undo / redo ---
         GraphNode* cube = SpawnNode("Cube", "3D", 0.0f, 0.0f);
         const bool spawnedOne = gNodes.size() == 1 && cube != nullptr;
         Undo();
         const bool undoRemovedIt = gNodes.empty();
         Redo();
         const bool redoBroughtItBack = gNodes.size() == 1;
         printf("spawn: 1 node -> undo -> %zu nodes -> redo -> %zu nodes  %s\n",
                (size_t)0, gNodes.size(),
                (spawnedOne && undoRemovedIt && redoBroughtItBack) ? "OK" : "FAIL");
         ok = ok && spawnedOne && undoRemovedIt && redoBroughtItBack;

         // --- param edit / undo / redo, via the same checkpoint the UI widgets use ---
         // Re-fetched after every Undo()/Redo(): each one runs ApplyPatchData,
         // which rebuilds the graph from scratch via NewPatch() + respawn, so
         // gNodes[0] is a genuinely new GeometryNode object each time - a
         // pointer captured before the call is stale afterward (NewPatch
         // retires rather than frees its old nodes immediately, precisely so
         // a stale pointer like that stays inert instead of dangling).
         auto* geo = static_cast<GeometryNode*>(gNodes[0].node.get());
         geo->detail = 24; // known starting value
         PushUndoCheckpoint(); // what ModSlider does on IsItemActivated, before the edit
         geo->detail = 91;
         Undo();
         geo = static_cast<GeometryNode*>(gNodes[0].node.get());
         const int afterUndo = geo->detail;
         const bool undoRestoredParam = afterUndo == 24;
         Redo();
         geo = static_cast<GeometryNode*>(gNodes[0].node.get());
         const int afterRedo = geo->detail;
         const bool redoReappliedParam = afterRedo == 91;
         printf("param edit: 24 -> 91 -> undo -> %d -> redo -> %d  %s\n",
                afterUndo, afterRedo,
                (undoRestoredParam && redoReappliedParam) ? "OK" : "FAIL");
         ok = ok && undoRestoredParam && redoReappliedParam;

         // --- delete (with a connection) / undo restores both the node and the wire ---
         GraphNode* smoothGn = SpawnNode("Smooth", "3D", 200.0f, 0.0f);
         auto* smooth = static_cast<GeometryOpNode*>(smoothGn->node.get());
         smooth->input = geo; // the live Cube, re-fetched just above
         const int smoothIndex = smoothGn->index;
         RemoveNodeByIndex(smoothIndex);
         const bool deleted = FindNodeByIndex(smoothIndex) == nullptr;
         Undo();
         GraphNode* restored = nullptr;
         for (GraphNode& gn : gNodes)
            if (gn.typeName == "Smooth")
               restored = &gn;
         const bool nodeRestored = restored != nullptr;
         const bool connectionRestored = nodeRestored &&
            static_cast<GeometryOpNode*>(restored->node.get())->input ==
               dynamic_cast<IGeometrySource*>(gNodes[0].node.get());
         printf("delete with connection: deleted=%d, undo restores node=%d and wire=%d  %s\n",
                (int)deleted, (int)nodeRestored, (int)connectionRestored,
                (deleted && nodeRestored && connectionRestored) ? "OK" : "FAIL");
         ok = ok && deleted && nodeRestored && connectionRestored;

         // --- a new action after an undo must drop the stale redo history ---
         Undo(); // back to just the cube, no Smooth
         const size_t redoDepthBeforeNewAction = gRedoStack.size();
         SpawnNode("Sphere", "3D", 400.0f, 0.0f);
         const bool redoClearedByNewAction = gRedoStack.empty();
         printf("redo stack: %zu entries before a new action -> %zu after  %s\n",
                redoDepthBeforeNewAction, gRedoStack.size(),
                (redoDepthBeforeNewAction > 0 && redoClearedByNewAction) ? "OK" : "FAIL");
         ok = ok && redoDepthBeforeNewAction > 0 && redoClearedByNewAction;

         // --- undo/redo themselves must not pollute the stacks they read from ---
         const size_t depthBefore = gUndoStack.size();
         Undo();
         Redo();
         const bool statOfSizeUnaffected = gUndoStack.size() == depthBefore;
         printf("undo/redo do not grow their own stacks: %zu -> %zu  %s\n",
                depthBefore, gUndoStack.size(), statOfSizeUnaffected ? "OK" : "FAIL");
         ok = ok && statOfSizeUnaffected;

         // --- opening a patch from disk clears history; undoing past it is not a thing ---
         SavePatchTo("/tmp/infinite_undotest.infinite");
         LoadPatchFrom("/tmp/infinite_undotest.infinite");
         const bool loadClearsUndo = gUndoStack.empty() && gRedoStack.empty();
         printf("loading a patch clears undo history: %zu undo, %zu redo  %s\n",
                gUndoStack.size(), gRedoStack.size(), loadClearsUndo ? "OK" : "FAIL");
         ok = ok && loadClearsUndo;

         printf("%s\n", ok ? "UNDO REDO OK" : "SUSPECT");
      }

      // Bring the comment into view for the screenshot check. Frame 2 rather
      // than frame 0 only so the node has settled at its spawn position and
      // measured its own size first; the fit itself runs at the end of this
      // same frame and lands on the frame after.
      if (getenv("INFINITE_COMMENTTEST") != nullptr && frameId == 2)
         gRequestFitView = true;

      // Slash mode: one keystroke had to produce a comment already taking the
      // keyboard, with no click of any kind in between, and the "/" itself must
      // not have ended up in the note.
      {
         const char* mode = getenv("INFINITE_COMMENTTEST");
         if (mode != nullptr && std::string(mode) == "slash" && frameId == 15)
         {
            CommentNode* c = gNodes.size() == 1
                                ? dynamic_cast<CommentNode*>(gNodes[0].node.get())
                                : nullptr;
            const bool spawned = c != nullptr;
            const bool typed = spawned && c->text == "lighting\nrim light too hot";
            printf("slash spawned a comment=%d, text=\"%s\"  %s\n", (int)spawned,
                   spawned ? c->text.c_str() : "(none)",
                   (spawned && typed) ? "SLASH COMMENT OK" : "FAIL");
            if (getenv("IMAGERESYNTH_SCREENSHOT") == nullptr)
               glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      // In edit mode (see the synthetic double-click and typing above) report
      // whether the note's only way in works end to end.
      {
         const char* mode = getenv("INFINITE_COMMENTTEST");
         if (mode != nullptr && std::string(mode) == "edit" && frameId == 19)
         {
            auto* c = static_cast<CommentNode*>(gNodes[0].node.get());
            const bool opened = gCommentEdit.target == c;
            const bool typed = c->text.find('!') != std::string::npos;
            const size_t lines = (size_t)std::count(c->text.begin(), c->text.end(), '\n') + 1;
            printf("double-click opens editor=%d, typing reaches the note=%d, %zu lines  %s\n",
                   (int)opened, (int)typed, lines,
                   (opened && typed && lines == 4) ? "COMMENT EDIT OK" : "FAIL");
            if (getenv("IMAGERESYNTH_SCREENSHOT") == nullptr)
               glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      // A note's line breaks are its content, and one param round trip backs
      // saving, undo/redo and copy/paste alike - so a comment that survives
      // being written to disk and read back survives all three. Checked here
      // rather than by eye because a screenshot cannot tell "kept the line
      // breaks" from "happens to be short enough to wrap the same way".
      //
      // Runs before the screenshot frame, and puts the note back the way it
      // found it, so the picture still shows the comment as authored. Skipped
      // in edit mode: reloading the patch would delete the node whose text is
      // being edited and close the popup under test.
      if (getenv("INFINITE_COMMENTTEST") != nullptr && frameId == 11 &&
          std::string(getenv("INFINITE_COMMENTTEST")) != "edit" &&
          std::string(getenv("INFINITE_COMMENTTEST")) != "slash")
      {
         auto findComment = []() -> CommentNode*
         {
            for (GraphNode& gn : gNodes)
            {
               if (auto* c = dynamic_cast<CommentNode*>(gn.node.get()))
                  return c;
            }
            return nullptr;
         };
         auto lineCount = [](const std::string& s)
         {
            return (size_t)std::count(s.begin(), s.end(), '\n') + 1;
         };

         bool ok = true;
         CommentNode* c = findComment();
         const std::string original = c != nullptr ? c->text : std::string();

         SavePatchTo("/tmp/infinite_commenttest.infinite");
         LoadPatchFrom("/tmp/infinite_commenttest.infinite");
         CommentNode* reloaded = findComment(); // load rebuilt every node
         const bool savedOk = reloaded != nullptr && reloaded->text == original;
         printf("comment save/load: %zu lines -> %zu lines  %s\n",
                lineCount(original),
                reloaded != nullptr ? lineCount(reloaded->text) : (size_t)0,
                savedOk ? "OK" : "FAIL");
         ok = ok && savedOk;

         // The same edit-then-undo the popup performs: the checkpoint is pushed
         // when the editor opens, the text changes while it is open.
         if (reloaded != nullptr)
         {
            PushUndoCheckpoint();
            reloaded->text = "scribbled over";
            Undo();
            CommentNode* undone = findComment();
            const bool undoOk = undone != nullptr && undone->text == original;
            printf("comment undo: text back to %zu lines  %s\n",
                   undone != nullptr ? lineCount(undone->text) : (size_t)0,
                   undoOk ? "OK" : "FAIL");
            ok = ok && undoOk;

            Redo();
            CommentNode* redone = findComment();
            const bool redoOk = redone != nullptr && redone->text == "scribbled over";
            printf("comment redo: %s  %s\n",
                   redone != nullptr ? redone->text.c_str() : "(gone)",
                   redoOk ? "OK" : "FAIL");
            ok = ok && redoOk;

            Undo(); // leave the authored note on screen for the screenshot
         }

         printf("%s\n", ok ? "COMMENT OK" : "SUSPECT");
         if (getenv("IMAGERESYNTH_SCREENSHOT") == nullptr)
            glfwSetWindowShouldClose(window, GLFW_TRUE);
      }

      // Group auto-fit: the box must track its members in BOTH directions, so
      // dragging one out stretches it and dragging that one back shrinks it
      // to the size it had before. Driven here rather than by hand because
      // the whole behaviour is a fixed point between our own fitting pass and
      // the editor's group geometry, and eyeballing a screenshot cannot tell
      // "shrank back exactly" from "shrank back nearly".
      if (getenv("INFINITE_GROUPTEST") != nullptr && gNodes.size() >= 3)
      {
         static float baselineW = 0.0f;
         GraphNode* group = nullptr;
         for (GraphNode& g : gNodes)
         {
            if (dynamic_cast<GroupNode*>(g.node.get()) != nullptr)
               group = &g;
         }

         if (frameId == 4)
         {
            GraphNode* gn = SpawnNode("Group", "Compositing", 60.0f, 60.0f);
            auto* g = static_cast<GroupNode*>(gn->node.get());
            gGroupMembers[g] = { gNodes[0].index, gNodes[1].index, gNodes[2].index };
         }
         else if (frameId == 8 && group != nullptr)
         {
            auto* g = static_cast<GroupNode*>(group->node.get());
            baselineW = g->width;
            printf("baseline box: %.0f x %.0f\n", g->width, g->height);
            ed::SetNodePosition(gNodes[2].NodeId(), ImVec2(1600.0f, 100.0f));
         }
         else if (frameId == 12 && group != nullptr)
         {
            auto* g = static_cast<GroupNode*>(group->node.get());
            printf("after dragging a member out: %.0f wide  %s\n", g->width,
                   g->width > baselineW + 500.0f ? "GREW" : "FAIL");
            ed::SetNodePosition(gNodes[2].NodeId(), ImVec2(700.0f, 100.0f));
         }
         else if (frameId == 16 && group != nullptr)
         {
            auto* g = static_cast<GroupNode*>(group->node.get());
            printf("after dragging it back: %.0f wide  %s\n", g->width,
                   std::fabs(g->width - baselineW) < 1.0f ? "SHRANK BACK OK" : "FAIL");
            printf("members still owned: %zu\n", gGroupMembers[g].size());

            // A second group dropped right on top of the first must come up
            // empty: every node down there already belongs to group one, and
            // a group is never a member of a group. Both together are what
            // stop one group from swallowing another.
            GraphNode* second = SpawnNode("Group", "Compositing", 0.0f, 0.0f);
            auto* g2 = static_cast<GroupNode*>(second->node.get());
            g2->width = 2000.0f;
            g2->height = 800.0f;
         }
         else if (frameId == 20)
         {
            GroupNode* first = nullptr;
            GroupNode* second = nullptr;
            int secondIndex = -1;
            for (GraphNode& g : gNodes)
            {
               if (auto* asGroup = dynamic_cast<GroupNode*>(g.node.get()))
               {
                  if (first == nullptr)
                     first = asGroup;
                  else
                  {
                     second = asGroup;
                     secondIndex = g.index;
                  }
               }
            }
            printf("overlapping second group stole: %zu members  %s\n",
                   gGroupMembers[second].size(),
                   gGroupMembers[second].empty() && gGroupMembers[first].size() == 3
                      ? "NO STEALING OK" : "FAIL");

            // Ungroup is driven through the real path: select a *member*, not
            // the group's header, and let the shortcut handler find the owner.
            const int memberId = gNodes[0].NodeId();
            // The overlapping group goes first, otherwise it simply adopts the
            // nodes the moment ungroup frees them and the check below cannot
            // tell "freed" apart from "handed straight to the other group".
            ed::DeleteNode(ed::NodeId(secondIndex * GraphNode::kStride));
            RemoveNodeByIndex(secondIndex);
            ed::SelectNode(ed::NodeId(memberId));
         }
         else if (frameId == 22)
         {
            gRequestUngroup = true;
         }
         else if (frameId == 26)
         {
            size_t groupsLeft = 0;
            for (GraphNode& g : gNodes)
            {
               if (dynamic_cast<GroupNode*>(g.node.get()) != nullptr)
                  groupsLeft++;
            }
            // The group is gone, its three member nodes are untouched, and
            // they are unowned again rather than still bound to a dead group.
            const bool freed = GroupOwning(gNodes[0].index) == nullptr;
            printf("after ungroup: %zu groups left, %zu nodes, member freed=%d  %s\n",
                   groupsLeft, gNodes.size(), (int)freed,
                   (groupsLeft == 0 && gNodes.size() == 3 && freed) ? "UNGROUP OK" : "FAIL");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      if (getenv("INFINITE_LIVETEST") != nullptr && frameId == 4)
      {
         // Reproduces the reported bug exactly: Cube -> Select -> Delete
         // Selected, then reroll Select's random seed and confirm Delete
         // Selected's output actually changes without touching the graph.
         GeometryNode cube;
         cube.shape = 1; // cube
         cube.CookIfNeeded(9400);

         auto select = std::make_unique<GeometryOpNode>();
         select->op = GeometryOpNode::kSelect;
         select->input = &cube;
         select->selectMode = MeshOps::kSelectRandom;
         select->selectA = 0.5f;
         select->selectSeed = 1.0f;

         auto del = std::make_unique<GeometryOpNode>();
         del->op = GeometryOpNode::kDeleteSelected;
         del->input = select.get();

         // Copied by value: GetMesh() returns a reference into the node's own
         // cache, so holding two "const Mesh&" across the second call would
         // alias the same mutated object instead of comparing before/after.
         const Mesh firstOut = del->GetMesh();
         const size_t firstTris = firstOut.indices.size() / 3;

         select->selectSeed = 42.0f;
         const Mesh secondOut = del->GetMesh();
         const size_t secondTris = secondOut.indices.size() / 3;

         auto sameVertices = [](const Mesh& a, const Mesh& b) {
            if (a.vertices.size() != b.vertices.size() || a.indices.size() != b.indices.size())
               return false;
            for (size_t i = 0; i < a.vertices.size(); i++)
               if (std::fabs(a.vertices[i].px - b.vertices[i].px) > 1e-6f ||
                   std::fabs(a.vertices[i].py - b.vertices[i].py) > 1e-6f ||
                   std::fabs(a.vertices[i].pz - b.vertices[i].pz) > 1e-6f)
                  return false;
            return true;
         };
         const bool changed = !sameVertices(firstOut, secondOut);
         printf("delete selected: seed 1 -> %zu tris, seed 42 -> %zu tris, output changed=%d  %s\n",
                firstTris, secondTris, (int)changed, changed ? "OK" : "FAIL");

         // Same check one hop further downstream, through a second operator -
         // the bug would still be live if only the immediate child re-read the
         // revision and a grandchild did not.
         auto transform = std::make_unique<GeometryOpNode>();
         transform->op = GeometryOpNode::kTransformSelected;
         transform->input = select.get();
         select->selectSeed = 1.0f;
         const Mesh t1 = transform->GetMesh();
         select->selectSeed = 42.0f;
         const Mesh t2 = transform->GetMesh();
         const bool changed2 = !sameVertices(t1, t2);
         printf("transform selected also reacts to reselection  %s\n", changed2 ? "OK" : "FAIL");

         // And InstanceOnPointsNode, which had the identical bug on its own
         // point-source and instance-shape inputs.
         auto inst = std::make_unique<InstanceOnPointsNode>();
         inst->pointSource = select.get();
         inst->instanceShape = &cube;
         inst->pointMode = 2; // faces
         select->selectSeed = 1.0f;
         inst->CookIfNeeded(9401);
         const size_t instCount1 = inst->InstanceCount();
         select->selectSeed = 42.0f;
         inst->CookIfNeeded(9402);
         const size_t instCount2 = inst->InstanceCount();
         printf("instance on points: seed 1 -> %zu instances, seed 42 -> %zu instances  %s\n",
                instCount1, instCount2, instCount1 != instCount2 ? "OK" : "FAIL");

         const bool ok = changed && changed2 && instCount1 != instCount2;
         printf("%s\n", ok ? "LIVE UPDATE OK" : "SUSPECT");
      }

      if (getenv("INFINITE_ROUNDTRIPTEST") != nullptr && frameId == 4)
      {
         // Every node type that declares params must survive both paths that
         // restore a node from bare settings: copy/paste (CopyParams) and
         // patch load (Patch::LoadParams). This is what caught FormulaNode,
         // TextNode, NoiseNode and about eighteen others silently keeping the
         // base INode::VisitParams no-op, so save/load and copy/paste dropped
         // every value on them.
         struct MutateVisitor : public ParamVisitor
         {
            int index = 0;
            void Float(const char*, float& v) override { v += 1.0f + (float)(index++ % 5) * 0.1f; }
            void Int(const char*, int& v) override { v += 1 + (index++ % 3); }
            void Bool(const char*, bool& v) override { v = !v; index++; }
            void Text(const char*, std::string& v) override { v += "_x"; index++; }
            void Color(const char*, float rgb[3]) override
            {
               rgb[0] += 0.11f; rgb[1] += 0.07f; rgb[2] += 0.05f; index++;
            }
         };

         int typesTested = 0, typesSkipped = 0, copyFails = 0, loadFails = 0;
         std::vector<std::string> copyFailNames, loadFailNames;

         for (const std::string& category : NodeFactory::Instance().GetCategories())
         {
            for (const std::string& name : NodeFactory::Instance().GetNodesInCategory(category))
            {
               std::unique_ptr<INode> a(NodeFactory::Instance().MakeNode(name));
               if (a == nullptr)
                  continue;

               MutateVisitor mutator;
               a->VisitParams(mutator);
               if (mutator.index == 0)
               {
                  // Legitimately no params (Null, Null 3D, the boolean-mode
                  // Join Geometry variants) rather than a missed override -
                  // nothing to round-trip.
                  typesSkipped++;
                  continue;
               }
               typesTested++;

               std::vector<std::pair<std::string, std::string>> paramsA;
               Patch::SaveParams(a.get(), paramsA);

               std::unique_ptr<INode> b(NodeFactory::Instance().MakeNode(name));
               CopyParams(b.get(), a.get());
               std::vector<std::pair<std::string, std::string>> paramsB;
               Patch::SaveParams(b.get(), paramsB);
               if (paramsB != paramsA)
               {
                  copyFails++;
                  copyFailNames.push_back(name);
               }

               std::unique_ptr<INode> c(NodeFactory::Instance().MakeNode(name));
               Patch::LoadParams(c.get(), paramsA);
               std::vector<std::pair<std::string, std::string>> paramsC;
               Patch::SaveParams(c.get(), paramsC);
               if (paramsC != paramsA)
               {
                  loadFails++;
                  loadFailNames.push_back(name);
               }
            }
         }

         printf("round trip: %d types tested, %d with no params to test\n", typesTested, typesSkipped);
         for (const std::string& n : copyFailNames)
            printf("  copy/paste dropped values: %s\n", n.c_str());
         for (const std::string& n : loadFailNames)
            printf("  save/load dropped values: %s\n", n.c_str());
         printf("copy/paste: %d/%d types round trip  %s\n",
                typesTested - copyFails, typesTested, copyFails == 0 ? "OK" : "FAIL");
         printf("save/load:  %d/%d types round trip  %s\n",
                typesTested - loadFails, typesTested, loadFails == 0 ? "OK" : "FAIL");
         printf("%s\n", (copyFails == 0 && loadFails == 0) ? "ROUND TRIP OK" : "SUSPECT");
      }

      if (getenv("INFINITE_PHASEFTEST") != nullptr && frameId == 4)
      {
         // Signed volume by the divergence theorem. A closed mesh whose
         // triangles all wind outward has positive volume; if any face is
         // flipped the contributions cancel and it collapses. This is the exact
         // check that caught the shattered metaballs, so every new closed
         // primitive gets it rather than a triangle count that proves nothing.
         auto volumeOf = [](const Mesh& m) {
            double vol = 0.0;
            for (size_t t = 0; t + 2 < m.indices.size(); t += 3)
            {
               const Vertex& a = m.vertices[m.indices[t]];
               const Vertex& b = m.vertices[m.indices[t + 1]];
               const Vertex& c = m.vertices[m.indices[t + 2]];
               vol += ((double)a.px * ((double)b.py * c.pz - (double)c.py * b.pz)
                     - (double)a.py * ((double)b.px * c.pz - (double)c.px * b.pz)
                     + (double)a.pz * ((double)b.px * c.py - (double)c.px * b.py)) / 6.0;
            }
            return vol;
         };
         // Edges are matched by quantised *position*, not index: these solids are
         // flat-shaded, so adjacent faces never share a vertex index even when
         // they share an edge in space.
         auto openEdges = [](const Mesh& m) {
            auto key = [&](unsigned int i) {
               const Vertex& v = m.vertices[i];
               return std::to_string((long long)std::llround(v.px * 10000.0f)) + "," +
                      std::to_string((long long)std::llround(v.py * 10000.0f)) + "," +
                      std::to_string((long long)std::llround(v.pz * 10000.0f));
            };
            std::map<std::string, int> counts;
            for (size_t t = 0; t + 2 < m.indices.size(); t += 3)
               for (int e = 0; e < 3; e++)
               {
                  std::string a = key(m.indices[t + e]);
                  std::string b = key(m.indices[t + (e + 1) % 3]);
                  counts[a < b ? a + "|" + b : b + "|" + a]++;
               }
            int open = 0;
            for (const auto& kv : counts)
               if (kv.second != 2)
                  open++;
            return open;
         };

         bool ok = true;
         struct Solid { const char* name; Mesh mesh; size_t expectTris; };
         std::vector<Solid> solids = {
            { "tetrahedron", Primitives::Tetrahedron(), 12 },
            { "octahedron", Primitives::Octahedron(), 24 },
            // Twelve pentagons fanned from their centroids: 12 * 5.
            { "dodecahedron", Primitives::Dodecahedron(), 60 },
            { "rounded cube", Primitives::RoundedCube(8, 0.2f), 0 },
         };
         for (const Solid& s : solids)
         {
            const double vol = volumeOf(s.mesh);
            const int open = openEdges(s.mesh);
            const size_t tris = s.mesh.indices.size() / 3;
            const bool trisOk = s.expectTris == 0 || tris == s.expectTris;
            // Klein bottle is closed but non-orientable, so its signed volume
            // is meaningless - it is only required to be watertight.
            const bool volOk = (std::string(s.name) == "klein bottle") || vol > 1e-4;
            printf("%-14s %6zu tris, volume %+.4f, %d open edges  %s\n",
                   s.name, tris, vol, open,
                   (trisOk && volOk && open == 0) ? "OK" : "FAIL");
            ok = ok && trisOk && volOk && open == 0;
         }

         // The figure-8 Klein bottle is a closed surface, but its cross-section
         // passes through its own centre twice, so v = 0 and v = pi land on the
         // same point for every u. Position-keyed edge matching therefore sees
         // those rings as one and reports false tears. Index-keyed matching is
         // the correct test here: this surface is one shared grid, so equal
         // indices really do mean the same vertex.
         {
            const Mesh kb = Primitives::KleinBottle(24, 24);
            std::map<std::pair<unsigned int, unsigned int>, int> counts;
            for (size_t t = 0; t + 2 < kb.indices.size(); t += 3)
               for (int e = 0; e < 3; e++)
               {
                  unsigned int a = kb.indices[t + e];
                  unsigned int b = kb.indices[t + (e + 1) % 3];
                  counts[a < b ? std::make_pair(a, b) : std::make_pair(b, a)]++;
               }
            int open = 0;
            for (const auto& kv : counts)
               if (kv.second != 2)
                  open++;
            // And confirm the diagnosis rather than asserting it: exactly one
            // coincident partner per u ring, which is where the 24 phantom
            // open edges came from.
            std::set<std::string> distinct;
            for (const Vertex& v : kb.vertices)
               distinct.insert(std::to_string((long long)std::llround(v.px * 10000.0f)) + "," +
                               std::to_string((long long)std::llround(v.py * 10000.0f)) + "," +
                               std::to_string((long long)std::llround(v.pz * 10000.0f)));
            const size_t duplicates = kb.vertices.size() - distinct.size();
            // Index matching proves the seam is *joined*, not that it is joined
            // correctly - a naive wrap is equally closed but stitches mismatched
            // cross-sections together, which shows up as a few enormous quads
            // spanning the tube. Comparing the longest edge to the median is
            // what actually tests the (u + 2pi, v) = (u, -v) identification.
            double longest = 0.0;
            std::vector<double> lengths;
            for (const auto& kv : counts)
            {
               const Vertex& a = kb.vertices[kv.first.first];
               const Vertex& b = kb.vertices[kv.first.second];
               const double d = std::sqrt((double)(a.px-b.px)*(a.px-b.px) +
                                          (double)(a.py-b.py)*(a.py-b.py) +
                                          (double)(a.pz-b.pz)*(a.pz-b.pz));
               lengths.push_back(d);
               longest = std::max(longest, d);
            }
            std::sort(lengths.begin(), lengths.end());
            const double median = lengths[lengths.size() / 2];
            const bool seamOk = longest < median * 3.0;
            printf("klein bottle   %6zu tris, %d open edges by index, %zu self-touching vertices  %s\n",
                   kb.indices.size() / 3, open, duplicates,
                   (open == 0 && duplicates == 24) ? "OK" : "FAIL");
            printf("klein seam: longest edge %.4f vs median %.4f  %s\n",
                   longest, median, seamOk ? "OK" : "FAIL");
            ok = ok && open == 0 && duplicates == 24 && seamOk;
         }

         // A rounded cube must sit strictly between the cube it came from and
         // the sphere it would become - the one measurement that catches a
         // projection that rounds too much or not at all.
         const double rcVol = volumeOf(Primitives::RoundedCube(10, 0.2f));
         const bool rcOk = rcVol < 1.0 && rcVol > 0.5236;
         printf("rounded cube volume %.4f between sphere 0.5236 and cube 1.0  %s\n",
                rcVol, rcOk ? "OK" : "FAIL");
         ok = ok && rcOk;

         // The open surfaces are emitted with both windings, so their triangle
         // count is even and each is reachable from either side.
         const Mesh mob = Primitives::MobiusStrip(64, 4, 0.3f);
         const bool mobOk = !mob.Empty() && (mob.indices.size() / 3) % 2 == 0;
         printf("mobius strip %zu tris, doubled  %s\n", mob.indices.size() / 3,
                mobOk ? "OK" : "FAIL");
         ok = ok && mobOk;

         for (const auto& p : { std::make_pair("gear", Primitives::Gear(12, 0.3f, 0.25f, 0.15f)),
                                std::make_pair("star", Primitives::Star(5, 0.4f, 0.3f)),
                                std::make_pair("disc", Primitives::Disc(32, 0.0f)),
                                std::make_pair("arrow", Primitives::Arrow(16, 0.12f, 0.35f)) })
         {
            const bool nonEmpty = !p.second.Empty();
            printf("%-6s %zu tris  %s\n", p.first, p.second.indices.size() / 3,
                   nonEmpty ? "OK" : "FAIL");
            ok = ok && nonEmpty;
         }

         // --- 3D resynthesize ---
         GeometryNode src;
         src.shape = 2; // sphere
         src.CookIfNeeded(9001);

         auto evolve = [&](float seed, int steps) {
            auto node = std::make_unique<MeshResynthNode>();
            node->input = &src;
            node->seed = seed;
            node->chaos = 0.5f;
            for (int i = 0; i < steps; i++)
               node->StepOnce();
            node->CookIfNeeded(9002);
            return node;
         };
         auto a = evolve(3.0f, 5);
         auto b = evolve(3.0f, 5);
         auto c = evolve(11.0f, 5);

         auto samePositions = [](const Mesh& x, const Mesh& y) {
            if (x.vertices.size() != y.vertices.size())
               return false;
            for (size_t i = 0; i < x.vertices.size(); i++)
               if (std::fabs(x.vertices[i].px - y.vertices[i].px) > 1e-6f ||
                   std::fabs(x.vertices[i].py - y.vertices[i].py) > 1e-6f ||
                   std::fabs(x.vertices[i].pz - y.vertices[i].pz) > 1e-6f)
                  return false;
            return true;
         };

         // The whole promise of the node: a patch reopened tomorrow replays the
         // same evolution, and a different seed is genuinely a different one.
         const bool deterministic = samePositions(a->GetMesh(), b->GetMesh());
         const bool seedMatters = !samePositions(a->GetMesh(), c->GetMesh());
         printf("resynth generation %d, deterministic %d, seed changes result %d  %s\n",
                a->Generation(), (int)deterministic, (int)seedMatters,
                (deterministic && seedMatters && a->Generation() == 5) ? "OK" : "FAIL");
         ok = ok && deterministic && seedMatters && a->Generation() == 5;

         // And it must actually mutate: five generations that leave the sphere
         // untouched would pass every check above.
         const bool moved = !samePositions(a->GetMesh(), src.GetMesh());
         printf("resynth changed the mesh: %d  %s\n", (int)moved, moved ? "OK" : "FAIL");
         ok = ok && moved;

         // Reset must return to the source exactly, not approximately.
         a->Reset();
         a->CookIfNeeded(9003);
         const bool resetOk = samePositions(a->GetMesh(), src.GetMesh()) && a->Generation() == 0;
         printf("reset restores the input: %d  %s\n", (int)resetOk, resetOk ? "OK" : "FAIL");
         ok = ok && resetOk;

         // Subdivision at full weight, unattended, must stop at the budget.
         auto budget = std::make_unique<MeshResynthNode>();
         budget->input = &src;
         budget->weight[MeshResynthNode::kSubdivide] = 1.0f;
         budget->triangleBudget = 8000;
         for (int frame = 0; frame < 12; frame++)
         {
            for (int i = 0; i < 4; i++)
               budget->StepOnce();
            budget->CookIfNeeded(9100 + frame);
         }
         const size_t grown = budget->TriangleCount();
         const bool budgetOk = grown <= 8000;
         printf("subdivide budget: %zu tris, cap 8000  %s\n", grown, budgetOk ? "OK" : "FAIL");
         ok = ok && budgetOk;

         // --- image to point cloud ---
         ShapeNode circle;
         circle.shapeType = 0;
         circle.width = 256; circle.height = 256;
         // aa0462a split the single `size` field into sizeX/sizeY (uSize is
         // now a vec2 in the shader - see ShapeNode.cpp); `size` itself is
         // dead, read nowhere in CookIfNeeded any more. Setting only `size`
         // here left the circle at its sizeX/sizeY defaults (0.35 radius,
         // pi*0.35^2 = 38.5% coverage) instead of the intended 0.25 (19.6%),
         // which is what actually produced the 35.1%-vs-19.6% mismatch this
         // fixture reported - the shape code never regressed.
         circle.sizeX = 0.25f;
         circle.sizeY = 0.25f;
         circle.posY = 0.75f;   // deliberately off-centre, see below
         circle.CookIfNeeded(9200);

         ImageToPointsNode i2p;
         i2p.Input().Connect(&circle);
         i2p.density = 64;
         i2p.threshold = 0.3f;
         i2p.height = 2.0f;
         i2p.CookIfNeeded(9201);

         const size_t pts = i2p.PointCount();
         const size_t grid = 64 * 64;
         // A circle of radius 0.25 covers pi*r^2 of the frame. If the threshold
         // were ignored we would get all 4096 points; if it rejected everything,
         // zero. Both failure modes are outside this band.
         const double coverage = (double)pts / (double)grid;
         const bool coverageOk = coverage > 0.10 && coverage < 0.30;
         printf("image to points: %zu of %zu (%.1f%% vs expected 19.6%%)  %s\n",
                pts, grid, coverage * 100.0, coverageOk ? "OK" : "FAIL");
         ok = ok && coverageOk;

         // The circle was placed at v = 0.75, so the cloud's centre of mass must
         // land at the matching height. This is what catches a vertical flip -
         // a centred test image would pass either way round.
         double meanY = 0.0;
         for (const Particle& p : i2p.GetPoints())
            meanY += p.py;
         if (pts > 0)
            meanY /= (double)pts;
         const double expectedY = (0.75 - 0.5) * 2.0;
         const bool orientOk = std::fabs(meanY - expectedY) < 0.15;
         printf("cloud centre y %.3f, image centre y %.3f  %s\n",
                meanY, expectedY, orientOk ? "OK" : "FAIL");
         ok = ok && orientOk;

         // Formula presets are compiled at runtime, so a typo in one ships as a
         // preset that silently does nothing. Compile every one.
         {
            FormulaNode fx;
            int failed = 0;
            for (int i = 0; i < (int)FormulaNode::PresetNames().size(); i++)
            {
               fx.LoadPreset(i);
               if (!fx.Apply())
               {
                  failed++;
                  printf("  preset \"%s\" failed: %s\n",
                         FormulaNode::PresetNames()[i].c_str(), fx.LastError().c_str());
               }
            }
            printf("formula presets: %zu compiled, %d failed  %s\n",
                   FormulaNode::PresetNames().size(), failed, failed == 0 ? "OK" : "FAIL");
            ok = ok && failed == 0;
         }

         // Same for the 2D shapes: they share one shader, but a new branch that
         // never runs would leave the shape rendering as whatever the fallback
         // is. Cook each and confirm it puts something on screen.
         {
            int blank = 0;
            for (int i = 0; i < (int)ShapeNode::ShapeNames().size(); i++)
            {
               ShapeNode sh;
               sh.shapeType = i;
               sh.width = 64; sh.height = 64;
               sh.CookIfNeeded(9300 + i);
               unsigned char px[64 * 64 * 4] = { 0 };
               glBindTexture(GL_TEXTURE_2D, sh.GetOutputTexture());
               glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
               glBindTexture(GL_TEXTURE_2D, 0);
               int lit = 0;
               for (int q = 0; q < 64 * 64; q++)
                  if (px[q * 4] > 20 || px[q * 4 + 1] > 20 || px[q * 4 + 2] > 20)
                     lit++;
               // A shape that fills nothing is broken; one that fills the whole
               // frame means the distance field never went positive.
               const bool shapeOk = lit > 20 && lit < 64 * 64 - 20;
               if (!shapeOk)
               {
                  blank++;
                  printf("  2D shape \"%s\": %d of %d pixels lit  FAIL\n",
                         ShapeNode::ShapeNames()[i].c_str(), lit, 64 * 64);
               }
            }
            printf("2D shapes: %zu drawn, %d bad  %s\n",
                   ShapeNode::ShapeNames().size(), blank, blank == 0 ? "OK" : "FAIL");
            ok = ok && blank == 0;
         }

         printf("%s\n", ok ? "PHASE F OK" : "SUSPECT");
      }

      if (getenv("INFINITE_PHASEETEST") != nullptr && frameId == 4)
      {
         // Signed volume via the divergence theorem. Triangle counts prove
         // nothing about a boolean - only the enclosed volume says whether the
         // operation actually did what it claims.
         auto volumeOf = [](const Mesh& m) {
            double vol = 0.0;
            for (size_t t = 0; t + 2 < m.indices.size(); t += 3)
            {
               const Vertex& a = m.vertices[m.indices[t]];
               const Vertex& b = m.vertices[m.indices[t+1]];
               const Vertex& c = m.vertices[m.indices[t+2]];
               vol += ((double)a.px * ((double)b.py * c.pz - (double)b.pz * c.py) -
                       (double)a.py * ((double)b.px * c.pz - (double)b.pz * c.px) +
                       (double)a.pz * ((double)b.px * c.py - (double)b.py * c.px)) / 6.0;
            }
            return std::fabs(vol);
         };

         // Two unit cubes overlapping in exactly half their volume: the answers
         // are known in advance, which is what makes this a real check.
         const Mesh cubeA = Primitives::Cube(1);
         const Mesh cubeB = MeshOps::Transform(Primitives::Cube(1),
                                               Mat4::Translation(0.5f, 0.0f, 0.0f));
         const double vA = volumeOf(cubeA);
         const double overlap = 0.5;  // half of a unit cube

         const Mesh un = MeshOps::Boolean(cubeA, cubeB, MeshOps::kBooleanUnion);
         const Mesh inter = MeshOps::Boolean(cubeA, cubeB, MeshOps::kBooleanIntersect);
         const Mesh diff = MeshOps::Boolean(cubeA, cubeB, MeshOps::kBooleanDifference);

         const double vUnion = volumeOf(un);
         const double vInter = volumeOf(inter);
         const double vDiff = volumeOf(diff);

         printf("cube volume %.3f\n", vA);
         printf("  union      %.3f  (expect 1.500)  %s\n", vUnion,
                std::fabs(vUnion - 1.5) < 0.02 ? "OK" : "FAIL");
         printf("  intersect  %.3f  (expect 0.500)  %s\n", vInter,
                std::fabs(vInter - overlap) < 0.02 ? "OK" : "FAIL");
         printf("  difference %.3f  (expect 0.500)  %s\n", vDiff,
                std::fabs(vDiff - overlap) < 0.02 ? "OK" : "FAIL");

         // A difference must actually remove material, not just re-emit the
         // original, so its extent has to shrink on the side that was cut.
         float diffMax = -1e30f;
         for (const Vertex& v : diff.vertices)
            diffMax = std::max(diffMax, v.px);
         printf("  difference max x %.3f (cube was 0.500, cut from +x)  %s\n",
                diffMax, diffMax < 0.02f ? "OK" : "FAIL");

         const bool ok = std::fabs(vUnion - 1.5) < 0.02 &&
                         std::fabs(vInter - overlap) < 0.02 &&
                         std::fabs(vDiff - overlap) < 0.02 && diffMax < 0.02f;
         printf("%s\n", ok ? "PHASE E OK" : "SUSPECT");
      }

      if (getenv("INFINITE_POPTEST") != nullptr && frameId == 4)
      {
         // PointZ family on the real GPU: every operator cooks (its kernels
         // compile on this driver), GPU results match the CPU, Render 3D
         // draws PointZ faces, lines, sprites and instances without GL errors.
         bool all = true;
         int cooked = 0, failed = 0;
         auto report = [&](const std::string& what, bool ok)
         {
            printf("  %-62s %s\n", what.c_str(), ok ? "ok" : "FAIL");
            all = all && ok;
         };
         while (glGetError() != GL_NO_ERROR) {}
         std::unique_ptr<PopNode> sphere(static_cast<PopNode*>(PopNode::CreateFor("Sphere PointZ")));
         std::unique_ptr<PopNode> cloud(static_cast<PopNode*>(PopNode::CreateFor("Point Generator PointZ")));
         std::unique_ptr<PopNode> line(static_cast<PopNode*>(PopNode::CreateFor("Line PointZ")));
         int frame = 1000;
         sphere->CookIfNeeded(frame); cloud->CookIfNeeded(frame); line->CookIfNeeded(frame);
         std::vector<std::unique_ptr<PopNode>> keep;
         for (const pop::PopDef& def : pop::Registry())
         {
            for (int variant = 0; variant < 3; variant++)
            {
               std::unique_ptr<PopNode> n(new PopNode(&def));
               PopNode* src = variant == 0 ? sphere.get() : (variant == 1 ? cloud.get() : line.get());
               for (int i = 0; i < def.inputs && i < PopNode::kMaxInputs; i++)
                  n->inputs[i] = src;
               for (int f = 0; f < 3; f++)
                  n->CookIfNeeded(++frame);
               const GLenum glErr = glGetError();
               cooked++;
               if (!n->CookError().empty() || glErr != GL_NO_ERROR)
               {
                  failed++;
                  printf("  FAIL %-24s v%d: %s (gl 0x%x)\n", def.name.c_str(), variant, n->CookError().substr(0, 240).c_str(), glErr);
                  all = false;
               }
               if (def.inputs == 0)
                  break;
            }
         }
         printf("  %d cooks, %d with errors\n", cooked, failed);

         // GPU kernel vs CPU: Transform PointZ translate on a known grid.
         {
            std::unique_ptr<PopNode> grid(static_cast<PopNode*>(PopNode::CreateFor("Grid PointZ")));
            std::unique_ptr<PopNode> xf(static_cast<PopNode*>(PopNode::CreateFor("Transform PointZ")));
            const pop::PopDef& d = xf->Def();
            const int off = d.FloatOffset(d.ParamIndex("t"));
            xf->Values().f[(size_t)off] = 1.0f; xf->Values().f[(size_t)off + 1] = 2.0f; xf->Values().f[(size_t)off + 2] = 3.0f;
            xf->inputs[0] = grid.get();
            xf->CookIfNeeded(++frame);
            const std::vector<float> a = grid->GetPop()->Read(pop::attr::P, 3);
            const std::vector<float> b = xf->GetPop()->Read(pop::attr::P, 3);
            float maxErr = (a.size() == b.size() && !a.empty()) ? 0.0f : 1e9f;
            for (size_t i = 0; i + 2 < a.size() && i + 2 < b.size(); i += 3)
               maxErr = std::max(maxErr, std::max(std::fabs(b[i] - a[i] - 1.0f), std::max(std::fabs(b[i + 1] - a[i + 1] - 2.0f), std::fabs(b[i + 2] - a[i + 2] - 3.0f))));
            printf("  transform kernel max error %.6f\n", maxErr);
            report("Transform PointZ on the GPU matches the CPU", maxErr < 1e-4f);
            // GPU bounds reduction vs the CPU.
            float lo[3], hi[3];
            const bool okB = pop::GpuBounds(xf->GetPop()->Find(pop::attr::P)->buffer->Gpu(), xf->GetPop()->pointCount, 3, lo, hi);
            printf("  gpu bounds %.3f %.3f %.3f .. %.3f %.3f %.3f\n", lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]);
            report("GPU bounds reduction finds the moved grid", okB && std::fabs(lo[0] + 0.0f) < 1e-3f && std::fabs(hi[0] - 2.0f) < 1e-3f &&
                                                              std::fabs(lo[1] - 2.0f) < 1e-3f && std::fabs(lo[2] - 2.0f) < 1e-3f && std::fabs(hi[2] - 4.0f) < 1e-3f);
         }

         // Render 3D: faces, lines, sprites and Instance PointZ.
         {
            std::unique_ptr<PopNode> noise(static_cast<PopNode*>(PopNode::CreateFor("Noise PointZ")));
            noise->inputs[0] = sphere.get();
            std::unique_ptr<PopNode> inst(static_cast<PopNode*>(PopNode::CreateFor("Instance PointZ")));
            std::unique_ptr<PopNode> box(static_cast<PopNode*>(PopNode::CreateFor("Box PointZ")));
            inst->inputs[0] = cloud.get();
            inst->inputs[1] = box.get();
            // 0.58: Geometry COMP style instances (per-axis scale, offsets,
            // texture by instance number) and the GPU Trail drawn as lines.
            {
               auto setF = [](PopNode* n, const char* key, int comp, float v) {
                  const int i = n->Def().ParamIndex(key);
                  if (i >= 0) n->Values().f[(size_t)(n->Def().FloatOffset(i) + comp)] = v;
               };
               setF(inst.get(), "scaleXYZ", 1, 2.0f);
               setF(inst.get(), "rotOffset", 2, 45.0f);
               setF(inst.get(), "texMode", 2, 0.0f);
               setF(inst.get(), "texMode", 0, 2.0f);
               setF(inst.get(), "texFrom", 0, 1.0f);
            }
            std::unique_ptr<PopNode> trail(static_cast<PopNode*>(PopNode::CreateFor("Trail PointZ")));
            trail->inputs[0] = noise.get();
            Render3DNode render;
            render.width = 128.0f; render.height = 128.0f;
            render.geometry[0] = noise.get();
            render.geometry[1] = line.get();
            render.geometry[2] = cloud.get();
            render.geometry[3] = inst.get();
            for (int f = 0; f < 3; f++)
               render.CookIfNeeded(++frame);
            Render3DNode trailRender; // Render 3D has four geometry pins
            trailRender.width = 64.0f; trailRender.height = 64.0f;
            trailRender.geometry[0] = trail.get();
            for (int f = 0; f < 3; f++)
               trailRender.CookIfNeeded(++frame);
            printf("  trail: %d points, %zu strips\n", trail->PointCount(),
                   trail->GetPop() ? trail->GetPop()->Topo().StripCount() : 0);
            const GLenum e1 = glGetError();
            printf("  render: %zu triangles in %zu draw calls, gl 0x%x\n", render.LastTriangleCount(), render.LastDrawCalls(), e1);
            const size_t boxTris = box->TriangleCount() * (size_t)cloud->PointCount();
            report("Render 3D draws a PointZ mesh, line, cloud and instances", e1 == GL_NO_ERROR && render.LastDrawCalls() >= 4 &&
                                                                         render.LastTriangleCount() >= boxTris);
            report("Trail PointZ keeps its history on the GPU and draws as lines",
                   trail->PointCount() == noise->PointCount() * 30 && trail->CookError().empty());
            // Particle loop through a tagged Null with a radial force.
            std::unique_ptr<PopNode> part(static_cast<PopNode*>(PopNode::CreateFor("Particle PointZ")));
            std::unique_ptr<PopNode> force(static_cast<PopNode*>(PopNode::CreateFor("Force Radial PointZ")));
            std::unique_ptr<PopNode> null(static_cast<PopNode*>(PopNode::CreateFor("Null PointZ")));
            part->inputs[0] = cloud.get();
            force->inputs[0] = part.get();
            null->inputs[0] = force.get();
            null->Values().s[(size_t)null->Def().ParamIndex("tag")] = "poptestloop";
            part->Values().s[(size_t)part->Def().ParamIndex("target")] = "poptestloop";
            for (int f = 0; f < 30; f++)
            {
               part->CookIfNeeded(++frame);
               null->CookIfNeeded(frame);
            }
            printf("  particles alive after 30 frames: %d (warning '%s')\n", part->PointCount(), part->CookWarning().c_str());
            report("Particle PointZ loop runs through a tagged Null", part->PointCount() > 0 && part->CookError().empty() &&
                                                                     part->CookWarning().empty());
         }
         report("every PointZ operator cooks on this GPU", failed == 0);
         printf("%s\n", all ? "POPTEST OK" : "POPTEST FAIL - BUG");
      }

      if (getenv("INFINITE_GEOWAVEUTEST") != nullptr && frameId == 4)
      {
         // Wave U (3D from Naman's upstream): Group 3D flattening, the pin
         // contract warnings and the new geometry nodes, on probes with known
         // answers.
         bool all = true;
         auto report = [&](const char* what, bool ok)
         {
            printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
            all = all && ok;
         };
         struct CloudProbe : public IGeometrySource
         {
            std::vector<Particle> cloud;
            const Mesh& GetMesh() override { static const Mesh kEmpty; return kEmpty; }
            unsigned long long MeshRevision() override { return 0; }
            Mat4 GetModelMatrix() const override { return Mat4::Identity(); }
            Material GetMaterial() const override { return Material(); }
            const std::vector<Particle>* GetPointCloud() override { return &cloud; }
            unsigned long long PointCloudRevision() override { return 3; }
         };
         struct CurveProbe : public IGeometrySource
         {
            Polyline line;
            const Mesh& GetMesh() override { static const Mesh kEmpty; return kEmpty; }
            unsigned long long MeshRevision() override { return 0; }
            Mat4 GetModelMatrix() const override { return Mat4::Identity(); }
            Material GetMaterial() const override { return Material(); }
            const Polyline* GetCurve() override { return &line; }
            unsigned long long CurveStamp() override { return 7; }
         };
         struct VertsProbe : public IGeometrySource
         {
            Mesh mesh;
            const Mesh& GetMesh() override { return mesh; }
            unsigned long long MeshRevision() override { return 1; }
            Mat4 GetModelMatrix() const override { return Mat4::Identity(); }
            Material GetMaterial() const override { return Material(); }
         };
         CloudProbe pts;
         for (int i = 0; i < 64; i++)
         {
            Particle p;
            p.px = (float)(i % 8) + 0.13f * (float)((i * 7) % 5);
            p.py = (float)(i / 8) + 0.11f * (float)((i * 3) % 4);
            p.pz = 0.05f * (float)(i % 3);
            pts.cloud.push_back(p);
         }
         CurveProbe curve;
         for (int i = 0; i < 20; i++)
         {
            curve.line.points.push_back((float)i * 0.1f);
            curve.line.points.push_back(std::sin((float)i * 0.5f));
            curve.line.points.push_back(0.0f);
         }
         VertsProbe verts;
         verts.mesh.vertices.resize(3);

         report("vertices-only input reported as vertices, not nothing",
                DescribeGeometryMismatch(&verts, GeometryRequirement::kMeshSurface).find("vertices only") != std::string::npos);
         report("vertices-only input satisfies kMeshVertices",
                DescribeGeometryMismatch(&verts, GeometryRequirement::kMeshVertices).empty());

         DelaunayMeshNode dn;
         dn.input = &verts;
         report("Delaunay Mesh flags a mesh and ignores it",
                dn.GetMesh().vertices.empty() && dn.CookWarning().find("point cloud") != std::string::npos);
         dn.input = &pts;
         const size_t delTris = dn.GetMesh().indices.size() / 3;
         report("Delaunay Mesh triangulates a cloud without warning", dn.CookWarning().empty() && delTris > 64);
         VoronoiCellsNode vn;
         vn.input = &verts;
         vn.GetMesh();
         report("Voronoi Cells flags a mesh", !vn.CookWarning().empty());
         vn.input = &pts;
         const bool vorCells = !vn.GetMesh().indices.empty(); // rebuild first, then read its warning
         report("Voronoi Cells builds cells from a cloud", vorCells && vn.CookWarning().empty());

         CurveOpsNode cn;
         cn.input = &verts;
         report("Curve Ops flags a mesh and emits no curve",
                cn.GetCurve() == nullptr && cn.CookWarning().find("curve") != std::string::npos);
         cn.input = &curve;
         cn.mode = 0; cn.count = 50;
         const Polyline* resampled = cn.GetCurve();
         report("Curve Ops resamples a curve to the asked count",
                cn.CookWarning().empty() && resampled != nullptr && resampled->Count() == 50);

         GeometryOpNode arr; arr.op = GeometryOpNode::kArray; arr.input = &pts; arr.CookIfNeeded(1001);
         report("Array flags a point cloud", !arr.CookWarning().empty());
         GeometryOpNode xf; xf.op = GeometryOpNode::kTransform; xf.input = &pts; xf.CookIfNeeded(1002);
         report("Transform accepts a point cloud", xf.CookWarning().empty());
         JoinGeometryNode jn; jn.inputs[0] = &pts; jn.GetMesh();
         report("Join flags a non-surface input", !jn.CookWarning().empty());

         // Decimate keeps roughly the asked fraction of a dense sphere.
         GeometryNode sphere; sphere.shape = 2;
         GeometryOpNode dec; dec.op = GeometryOpNode::kDecimate; dec.input = &sphere; dec.decimateRatio = 0.25f;
         const size_t before = sphere.GetMesh().indices.size() / 3;
         const size_t after = dec.GetMesh().indices.size() / 3;
         printf("  decimate: %zu -> %zu triangles (keep 0.25)\n", before, after);
         report("Decimate reduces a sphere to about a quarter",
                before > 0 && after > 0 && after < before / 2);

         // Group 3D: six cubes reach one Render 3D through one pin, one group nested.
         std::vector<std::unique_ptr<GeometryNode>> cubes;
         for (int i = 0; i < 6; i++)
         {
            cubes.emplace_back(new GeometryNode());
            cubes.back()->shape = 1;
         }
         Group3DNode inner, outer;
         Render3DNode render;
         render.width = 64.0f; render.height = 64.0f;
         for (int i = 0; i < 4; i++) outer.inputs[i] = cubes[i].get();
         inner.inputs[0] = cubes[4].get();
         inner.inputs[1] = cubes[5].get();
         outer.inputs[4] = &inner;
         render.geometry[0] = &outer;
         const size_t perCube = cubes[0]->GetMesh().indices.size() / 3;
         report("Group 3D flattens six objects through one pin", render.FlattenedGeometry().size() == 6);
         render.CookIfNeeded(frameId);
         printf("  group render: %zu triangles (expect %zu)\n", render.LastTriangleCount(), perCube * 6);
         report("Render 3D draws every grouped object", perCube > 0 && render.LastTriangleCount() == perCube * 6);
         Group3DNode loop;
         loop.inputs[0] = &loop;
         loop.inputs[1] = cubes[0].get();
         Render3DNode loopRender;
         loopRender.geometry[0] = &loop;
         report("self-referencing group stays under the draw cap",
                loopRender.FlattenedGeometry().size() <= (size_t)Render3DNode::kMaxDraw);

         // Render passes: every pass renders without a GL error.
         bool passOk = true;
         for (int pass = 0; pass < (int)Render3DNode::RenderPassNames().size(); pass++)
         {
            render.renderPass = pass;
            render.CookIfNeeded(frameId + 100 + pass);
            passOk = passOk && glGetError() == GL_NO_ERROR && render.GetOutputTexture() != 0;
         }
         report("Render 3D renders every pass", passOk);

         printf("%s\n", all ? "GEOWAVEUTEST OK" : "GEOWAVEUTEST FAIL - BUG");
      }

      if (getenv("INFINITE_WRAPTEST") != nullptr && frameId == 4)
      {
         // The whole point of the cylindrical mode is that it is a coordinate
         // remap, not a projection: it must preserve arc length exactly. A
         // strip of known width W bent around a target of known radius R has
         // an answer worked out in advance - every point at distance R from
         // the axis, spanning exactly W/R radians - so this is a real check
         // and not just "did something move".
         const float R = 2.0f;
         const float W = 3.0f;
         const float H = 0.4f;

         // Thin flat strip in the XY plane, W wide and H tall, at z = 0.
         Mesh strip;
         const int kCols = 40;
         for (int i = 0; i <= kCols; i++)
         {
            const float x = -W * 0.5f + W * (float)i / (float)kCols;
            Vertex a; a.px = x; a.py = -H * 0.5f; a.pz = 0.0f;
            Vertex b; b.px = x; b.py =  H * 0.5f; b.pz = 0.0f;
            strip.vertices.push_back(a);
            strip.vertices.push_back(b);
         }
         for (int i = 0; i < kCols; i++)
         {
            const unsigned int base = (unsigned int)(i * 2);
            strip.indices.push_back(base); strip.indices.push_back(base + 1); strip.indices.push_back(base + 2);
            strip.indices.push_back(base + 1); strip.indices.push_back(base + 3); strip.indices.push_back(base + 2);
         }

         // Target: a sphere of radius R centred at the origin, so the bend
         // radius is read from real geometry rather than the override. The
         // primitive is unit *diameter*, hence the 2R scale.
         const Mesh sphere = MeshOps::Transform(Primitives::Sphere(24, 32),
                                                Mat4::Scale(2.0f * R, 2.0f * R, 2.0f * R));

         auto spanOf = [](const Mesh& m, float& minR, float& maxR, float& minA, float& maxA) {
            minR = 1e30f; maxR = -1e30f; minA = 1e30f; maxA = -1e30f;
            for (const Vertex& v : m.vertices)
            {
               const float rad = std::sqrt(v.px * v.px + v.pz * v.pz);
               const float ang = std::atan2(v.px, v.pz);
               minR = std::min(minR, rad); maxR = std::max(maxR, rad);
               minA = std::min(minA, ang); maxA = std::max(maxA, ang);
            }
         };

         const Mesh cyl = MeshOps::Wrap(strip, Mat4::Identity(), sphere, Mat4::Identity(),
                                        MeshOps::kWrapCylindrical, 0.0f, 1.0f, 0.0f, 1.0f, 1,
                                        false, false, false);
         float minR, maxR, minA, maxA;
         spanOf(cyl, minR, maxR, minA, maxA);
         const float radiusErr = std::max(std::fabs(minR - R), std::fabs(maxR - R));
         const float span = maxA - minA;
         const bool radiusOk = radiusErr < 1e-3f;
         const bool spanOk = std::fabs(span - W / R) < 1e-3f;
         printf("  cylindrical radius %.5f..%.5f (expect %.3f)  %s\n", minR, maxR, R,
                radiusOk ? "OK" : "FAIL");
         printf("  cylindrical span   %.5f rad (expect W/R = %.5f)  %s\n", span, W / R,
                spanOk ? "OK" : "FAIL");

         // fit around: the same strip must now close a full circle.
         const Mesh fit = MeshOps::Wrap(strip, Mat4::Identity(), sphere, Mat4::Identity(),
                                        MeshOps::kWrapCylindrical, 0.0f, 1.0f, 0.0f, 1.0f, 1,
                                        true, false, false);
         // atan2 wraps at +/-pi, so a full turn cannot be read off min/max.
         // Walk the strip column by column instead and accumulate unwrapped
         // angle deltas - that measures the real total sweep.
         const float kPi = 3.14159265358979f;
         float fitSweep = 0.0f, prevAng = std::atan2(fit.vertices[0].px, fit.vertices[0].pz);
         float fitRadErr = 0.0f;
         for (int i = 1; i <= kCols; i++)
         {
            const Vertex& v = fit.vertices[(size_t)i * 2];
            const float ang = std::atan2(v.px, v.pz);
            float d = ang - prevAng;
            while (d >  kPi) d -= 2.0f * kPi;
            while (d < -kPi) d += 2.0f * kPi;
            fitSweep += d;
            prevAng = ang;
            fitRadErr = std::max(fitRadErr,
                                 std::fabs(std::sqrt(v.px * v.px + v.pz * v.pz) - R));
         }
         const bool fitOk = std::fabs(std::fabs(fitSweep) - 2.0f * kPi) < 1e-3f && fitRadErr < 1e-3f;
         printf("  fit around sweep   %.5f rad (expect 2pi = %.5f)  %s\n", fitSweep,
                2.0f * kPi, fitOk ? "OK" : "FAIL");

         // Spherical must not push anything outside the shell it bends onto:
         // the strip is flat (zero depth), so R is the ceiling.
         const Mesh sph = MeshOps::Wrap(strip, Mat4::Identity(), sphere, Mat4::Identity(),
                                        MeshOps::kWrapSpherical, 0.0f, 1.0f, 0.0f, 1.0f, 1,
                                        false, false, false);
         float sphMax = 0.0f;
         for (const Vertex& v : sph.vertices)
            sphMax = std::max(sphMax, std::sqrt(v.px * v.px + v.py * v.py + v.pz * v.pz));
         const bool sphOk = sphMax <= R + 1e-3f;
         printf("  spherical max |p|  %.5f (expect <= %.3f)  %s\n", sphMax, R,
                sphOk ? "OK" : "FAIL");

         // A radius override with no target at all still has to bend, and the
         // override is used verbatim as the radius (radiusScale is ignored -
         // there is no derived radius for it to scale).
         const Mesh noTarget = MeshOps::Wrap(strip, Mat4::Identity(), Mesh(), Mat4::Identity(),
                                             MeshOps::kWrapCylindrical, 0.0f, 1.0f, R, 0.25f, 1,
                                             false, false, false);
         float ntMinR, ntMaxR, ntMinA, ntMaxA;
         spanOf(noTarget, ntMinR, ntMaxR, ntMinA, ntMaxA);
         const bool ntOk = std::fabs(ntMaxR - R) < 1e-3f && std::fabs((ntMaxA - ntMinA) - W / R) < 1e-3f;
         printf("  no target, radius %.1f: span %.5f rad  %s\n", R, ntMaxA - ntMinA,
                ntOk ? "OK" : "FAIL");

         // Nearest surface still works, and (unlike the bend) collapses the
         // strip's width - the behaviour the bend modes exist to avoid.
         const Mesh nearest = MeshOps::Wrap(strip, Mat4::Identity(), sphere, Mat4::Identity(),
                                            MeshOps::kWrapNearest, 0.0f, 1.0f, 0.0f, 1.0f, 1,
                                            false, false, false);
         const bool nearOk = nearest.vertices.size() == strip.vertices.size() && !nearest.Empty();
         printf("  nearest surface    %zu verts  %s\n", nearest.vertices.size(),
                nearOk ? "OK" : "FAIL");

         // The bug this guards: the radius must never stop tracking the
         // target. Scale the target's model matrix 2x with node params held
         // identical and the bend radius has to double on its own.
         const Mesh scaled = MeshOps::Wrap(strip, Mat4::Identity(), sphere, Mat4::Scale(2.0f, 2.0f, 2.0f),
                                           MeshOps::kWrapCylindrical, 0.0f, 1.0f, 0.0f, 1.0f, 1,
                                           false, false, false);
         float scMinR, scMaxR, scMinA, scMaxA;
         spanOf(scaled, scMinR, scMaxR, scMinA, scMaxA);
         const bool trackOk = std::fabs(scMaxR - 2.0f * R) < 1e-3f && std::fabs(scMinR - 2.0f * R) < 1e-3f;
         printf("  target scaled 2x:  radius %.5f..%.5f (was %.5f, expect %.3f)  %s\n",
                scMinR, scMaxR, maxR, 2.0f * R, trackOk ? "OK" : "FAIL");

         // And radius scale is a multiplier on that derived radius, not a
         // replacement for it.
         const Mesh halfScale = MeshOps::Wrap(strip, Mat4::Identity(), sphere, Mat4::Identity(),
                                              MeshOps::kWrapCylindrical, 0.0f, 1.0f, 0.0f, 0.5f, 1,
                                              false, false, false);
         float hsMinR, hsMaxR, hsMinA, hsMaxA;
         spanOf(halfScale, hsMinR, hsMaxR, hsMinA, hsMaxA);
         const bool scaleOk = std::fabs(hsMaxR - 0.5f * R) < 1e-3f && std::fabs(hsMinR - 0.5f * R) < 1e-3f;
         printf("  radius scale 0.5:  radius %.5f..%.5f (expect %.3f)  %s\n",
                hsMinR, hsMaxR, 0.5f * R, scaleOk ? "OK" : "FAIL");

         const bool ok = radiusOk && spanOk && fitOk && sphOk && ntOk && nearOk &&
                         trackOk && scaleOk;
         printf("%s\n", ok ? "WRAP OK" : "SUSPECT");
      }

      if (getenv("INFINITE_PHASEDTEST") != nullptr && frameId == 4)
      {
         // Curves of every kind must produce a usable polyline and a tube.
         bool allKinds = true;
         for (int kind = 0; kind < 4; kind++)
         {
            std::vector<float> control = { -1,0,0,  -0.3f,0.8f,0,  0.3f,-0.8f,0,  1,0,0 };
            const Polyline line = MeshOps::BuildCurve(control, kind, 16, false);
            const Mesh tube = MeshOps::TubeAlong(line, 0.05f, 8, 0.0f);
            const bool ok = line.Count() >= 4 && !tube.Empty();
            printf("  curve %-12s %zu points, %zu tris  %s\n",
                   CurveNode::KindNames()[kind].c_str(), line.Count(),
                   tube.indices.size() / 3, ok ? "OK" : "FAIL");
            if (!ok) allKinds = false;
         }

         // Arc-length sampling: equal steps in t must cover roughly equal
         // distance. Sampling by index instead would crawl where control points
         // bunch and race where they spread.
         std::vector<float> control = { -1,0,0,  -0.9f,0.1f,0,  0.9f,0.1f,0,  1,0,0 };
         const Polyline line = MeshOps::BuildCurve(control, MeshOps::kCurveCatmullRom, 24, false);
         float prev[3], tangent[3], minStep = 1e30f, maxStep = 0.0f;
         MeshOps::SamplePolyline(line, 0.0f, prev, tangent);
         for (int i = 1; i <= 20; i++)
         {
            float pos[3];
            MeshOps::SamplePolyline(line, (float)i / 20.0f, pos, tangent);
            const float d = std::sqrt((pos[0]-prev[0])*(pos[0]-prev[0]) +
                                      (pos[1]-prev[1])*(pos[1]-prev[1]) +
                                      (pos[2]-prev[2])*(pos[2]-prev[2]));
            minStep = std::min(minStep, d);
            maxStep = std::max(maxStep, d);
            prev[0] = pos[0]; prev[1] = pos[1]; prev[2] = pos[2];
         }
         const float ratio = (minStep > 1e-6f) ? maxStep / minStep : 1e30f;
         printf("arc-length sampling: step ratio %.2f (1.0 is perfectly even)  %s\n",
                ratio, ratio < 1.6f ? "OK" : "FAIL");
         const bool evenSpeed = ratio < 1.6f;

         // Slicing a sphere through its centre must give one closed contour,
         // and every point on it must sit at the sphere's radius - that is what
         // proves it is really following the surface and not something else.
         const Mesh sphere = Primitives::Sphere(24, 32);
         const std::vector<Polyline> contours = MeshOps::SliceContours(sphere, 1, 0.0f);
         bool sliceOk = false;
         if (!contours.empty())
         {
            float minR = 1e30f, maxR = 0.0f;
            for (size_t i = 0; i < contours[0].Count(); i++)
            {
               const float x = contours[0].points[i*3];
               const float z = contours[0].points[i*3+2];
               const float r = std::sqrt(x*x + z*z);
               minR = std::min(minR, r); maxR = std::max(maxR, r);
            }
            sliceOk = contours[0].closed && contours[0].Count() > 16 &&
                      std::fabs(maxR - 0.5f) < 0.02f && std::fabs(minR - 0.5f) < 0.02f;
            printf("sphere slice: %zu contours, %zu points, closed=%d, radius %.3f..%.3f  %s\n",
                   contours.size(), contours[0].Count(), (int)contours[0].closed,
                   minR, maxR, sliceOk ? "OK" : "FAIL");
         }
         else
         {
            printf("sphere slice: no contours  FAIL\n");
         }

         // A plane has a real boundary, so that path needs no fallback.
         const std::vector<Polyline> edges = MeshOps::BoundaryLoops(Primitives::Plane(4));
         const bool boundaryOk = !edges.empty() && edges[0].closed && edges[0].Count() >= 16;
         printf("plane boundary: %zu loops, %zu points, closed=%d  %s\n",
                edges.size(), edges.empty() ? 0 : edges[0].Count(),
                edges.empty() ? 0 : (int)edges[0].closed, boundaryOk ? "OK" : "FAIL");

         printf("%s\n", (allKinds && evenSpeed && sliceOk && boundaryOk)
                            ? "PHASE D OK" : "SUSPECT");
      }

      if (getenv("INFINITE_PHASECTEST") != nullptr && frameId == 4)
      {
         // Every new primitive must produce a finite, non-degenerate mesh.
         bool allPrims = true;
         for (int i = 0; i < (int)GeometryNode::ShapeNames().size(); i++)
         {
            INode* made = NodeFactory::Instance().MakeNode(GeometryNode::ShapeNames()[i]);
            auto* geo = dynamic_cast<GeometryNode*>(made);
            bool ok = false;
            if (geo != nullptr)
            {
               const Mesh& m = geo->GetMesh();
               bool finite = true;
               float lo = 1e30f, hi = -1e30f;
               for (const Vertex& v : m.vertices)
               {
                  if (!std::isfinite(v.px) || !std::isfinite(v.py) || !std::isfinite(v.pz))
                     finite = false;
                  lo = std::min(lo, v.px); hi = std::max(hi, v.px);
               }
               ok = finite && m.indices.size() >= 3 && (hi - lo) > 0.01f && (hi - lo) < 100.0f;
               printf("  %-12s %6zu tris  width %.2f  %s\n",
                      GeometryNode::ShapeNames()[i].c_str(), m.indices.size() / 3,
                      hi - lo, ok ? "OK" : "FAIL");
            }
            if (!ok) allPrims = false;
            delete made;
         }

         // Two balls far apart give two separate surfaces; brought together
         // they must merge into one. That merging is the entire reason to use
         // metaballs rather than joining two spheres, so it is what to test.
         std::vector<Primitives::MetaBall> apart = {
            { -1.1f, 0.0f, 0.0f, 0.25f }, { 1.1f, 0.0f, 0.0f, 0.25f }
         };
         std::vector<Primitives::MetaBall> together = {
            { -0.25f, 0.0f, 0.0f, 0.25f }, { 0.25f, 0.0f, 0.0f, 0.25f }
         };
         const Mesh mApart = Primitives::MetaBalls(apart, 40, 1.0f, 2.0f);
         const Mesh mTogether = Primitives::MetaBalls(together, 40, 1.0f, 2.0f);

         // Merged is detected by whether the surface spans the midpoint: apart,
         // nothing exists near x=0; merged, the bridge does.
         auto spansCentre = [](const Mesh& m) {
            for (const Vertex& v : m.vertices)
               if (std::fabs(v.px) < 0.05f)
                  return true;
            return false;
         };
         printf("metaballs: apart %zu tris (spans centre %d), together %zu tris (spans centre %d)\n",
                mApart.indices.size() / 3, (int)spansCentre(mApart),
                mTogether.indices.size() / 3, (int)spansCentre(mTogether));
         const bool merges = !mApart.Empty() && !mTogether.Empty() &&
                             !spansCentre(mApart) && spansCentre(mTogether);

         // Merging alone is not enough: the surface has to be closed and
         // consistently wound, or backface culling eats the wrongly-facing half
         // and the blob renders shattered. Every edge of a watertight, coherently
         // oriented mesh is traversed once in each direction, so a directed edge
         // seen twice the same way means two triangles disagree about facing.
         auto surfaceIntact = [](const Mesh& m, const char* label) {
            const std::vector<unsigned int> weld = MeshOps::BuildWeldMap(m);
            std::map<std::pair<unsigned int, unsigned int>, int> directed;
            for (size_t t = 0; t + 2 < m.indices.size(); t += 3)
               for (int e = 0; e < 3; e++)
                  directed[{ weld[m.indices[t + e]], weld[m.indices[t + (e + 1) % 3]] }]++;

            size_t boundary = 0, flipped = 0;
            for (const auto& entry : directed)
            {
               if (entry.second > 1)
                  flipped++;
               const auto opposite = directed.find({ entry.first.second, entry.first.first });
               if (opposite == directed.end())
                  boundary++;
            }
            const bool ok = boundary == 0 && flipped == 0;
            printf("  %s: %zu open edges, %zu inconsistently wound  %s\n",
                   label, boundary, flipped, ok ? "watertight" : "BROKEN");
            return ok;
         };
         const bool intact = surfaceIntact(mApart, "apart") &&
                             surfaceIntact(mTogether, "together");

         printf("%s\n", (allPrims && merges && intact) ? "PHASE C OK" : "SUSPECT");
      }

      if (getenv("INFINITE_MAPTEST") != nullptr && frameId >= 4 && frameId <= 16 && frameId % 4 == 0)
      {
         auto* mat = static_cast<MaterialNode*>(gNodes[1].node.get());
         auto* render = static_cast<Render3DNode*>(gNodes[3].node.get());
         auto* noise = gNodes[2].node.get();

         const int w = render->GetOutputWidth(), h = render->GetOutputHeight();
         std::vector<unsigned char> px((size_t)w * h * 4);
         GLuint fbo = 0;
         glGenFramebuffers(1, &fbo);
         glBindFramebuffer(GL_FRAMEBUFFER, fbo);
         glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                render->GetOutputTexture(), 0);
         glPixelStorei(GL_PACK_ALIGNMENT, 1);
         glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
         glBindFramebuffer(GL_FRAMEBUFFER, 0);
         glDeleteFramebuffers(1, &fbo);

         static std::vector<unsigned char> sBase;
         static int sPassed = 0;
         auto differs = [&](const char* label) {
            size_t changed = 0;
            for (size_t i = 0; i + 3 < px.size() && i < sBase.size(); i += 4)
               if (std::abs((int)px[i] - (int)sBase[i]) > 6) changed++;
            const bool ok = changed > 200;
            printf("  %-10s %zu px changed  %s\n", label, changed, ok ? "OK" : "FAIL");
            if (ok) sPassed++;
         };

         // Each channel is patched in turn against the same untouched baseline,
         // so a channel that silently did nothing shows up as zero change.
         if (frameId == 4)      { sBase = px; mat->MapInput(kMapRoughness).Connect(noise); }
         else if (frameId == 8) { differs("roughness"); mat->MapInput(kMapRoughness).Disconnect();
                                  mat->MapInput(kMapNormal).Connect(noise); }
         else if (frameId == 12){ differs("normal"); mat->MapInput(kMapNormal).Disconnect();
                                  mat->MapInput(kMapAmbientOcclusion).Connect(noise); }
         else if (frameId == 16){ differs("ao");
                                  printf("%s\n", sPassed == 3 ? "MATERIAL MAPS OK" : "SUSPECT"); }
      }

      if (getenv("INFINITE_SHADOWTEST") != nullptr && (frameId == 4 || frameId == 8))
      {
         auto* render = static_cast<Render3DNode*>(gNodes[3].node.get());
         const int w = render->GetOutputWidth(), h = render->GetOutputHeight();
         std::vector<unsigned char> px((size_t)w * h * 4);
         GLuint fbo = 0;
         glGenFramebuffers(1, &fbo);
         glBindFramebuffer(GL_FRAMEBUFFER, fbo);
         glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                render->GetOutputTexture(), 0);
         glPixelStorei(GL_PACK_ALIGNMENT, 1);
         glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
         glBindFramebuffer(GL_FRAMEBUFFER, 0);
         glDeleteFramebuffers(1, &fbo);

         static std::vector<unsigned char> sNoShadow;
         if (frameId == 4)
         {
            sNoShadow = px;
            render->shadowsEnabled = true;
            printf("shadow map: %dx%d\n", render->ActiveShadowSize(), render->ActiveShadowSize());
         }
         else
         {
            // Only some pixels should change, and every one of them should get
            // *darker*. A shadow that brightened anything would mean the depth
            // comparison is inverted; a change everywhere would mean the whole
            // image dimmed rather than a shadow being cast.
            size_t darker = 0, brighter = 0;
            for (size_t i = 0; i + 3 < px.size() && i < sNoShadow.size(); i += 4)
            {
               const int before = sNoShadow[i] + sNoShadow[i+1] + sNoShadow[i+2];
               const int after = px[i] + px[i+1] + px[i+2];
               if (after < before - 12) darker++;
               else if (after > before + 12) brighter++;
            }
            const double pct = 100.0 * (double)darker / (double)(w * h);
            printf("shadow: %zu px darker (%.1f%%), %zu brighter, active=%d\n",
                   darker, pct, brighter, render->ActiveShadowSize() > 0);
            const bool ok = darker > 500 && pct < 60.0 && brighter < darker / 10 &&
                            render->ActiveShadowSize() > 0;
            printf("%s\n", ok ? "SHADOWS OK" : "SUSPECT");
         }
      }

      if (getenv("INFINITE_BUGTEST") != nullptr && frameId == 6)
      {
         auto* geo = static_cast<GeometryNode*>(gNodes[0].node.get());
         auto* arr = static_cast<GeometryOpNode*>(gNodes[1].node.get());
         auto* ja = static_cast<GeometryNode*>(gNodes[3].node.get());
         auto* join = static_cast<JoinGeometryNode*>(gNodes[5].node.get());

         // 1. Saving from outside the editor context must not crash. This is
         //    the exact path the File menu and Cmd+S take.
         const bool saved = SavePatchTo("/tmp/infinite_bugtest.infinite");
         printf("save from outside editor context: %d\n", (int)saved);

         // 2. Moving a Geometry node must move what an operator downstream
         //    renders. The operator forwards the transform rather than
         //    flattening it to identity.
         geo->posX = 0.0f;
         const float before = arr->GetModelMatrix().m[12];
         geo->posX = 2.5f;
         const float after = arr->GetModelMatrix().m[12];
         printf("array transform follows input: %.2f -> %.2f  %s\n", before, after,
                std::fabs(after - 2.5f) < 1e-4f ? "OK" : "FAIL");

         // 3. Moving an input of Join Geometry must change the merged mesh,
         //    since the transform is baked into its vertices.
         ja->posX = 0.0f;
         join->GetMesh();
         float joinBefore = -1e30f;
         for (const Vertex& v : join->GetMesh().vertices)
            joinBefore = std::max(joinBefore, v.px);
         ja->posX = 3.0f;
         join->GetMesh();
         float joinAfter = -1e30f;
         for (const Vertex& v : join->GetMesh().vertices)
            joinAfter = std::max(joinAfter, v.px);
         printf("join rebuilds on input move: max x %.2f -> %.2f  %s\n",
                joinBefore, joinAfter, joinAfter > joinBefore + 2.0f ? "OK" : "FAIL");

         // 4. Instance on Points must follow both the point source's and the
         //    shape's own transform - previously both were dropped, so
         //    moving/scaling either object silently did nothing to the render.
         GeometryNode pointsSrc;
         pointsSrc.shape = 0; // plane
         pointsSrc.detail = 4;
         GeometryNode shapeSrc;
         shapeSrc.shape = 2; // sphere

         InstanceOnPointsNode inst;
         inst.pointSource = &pointsSrc;
         inst.instanceShape = &shapeSrc;
         inst.pointMode = 0; // vertices
         inst.maxPoints = 50;
         inst.instanceScale = 1.0f;
         inst.scaleRandom = 0.0f;
         inst.rotationRandom = 0.0f;
         inst.alignToNormal = false;
         inst.CookIfNeeded(9500);
         const bool hadInstances = inst.InstanceCount() > 0;
         const Mat4 firstBefore = hadInstances ? inst.InstanceTransforms()[0] : Mat4::Identity();

         pointsSrc.posX = 5.0f;
         inst.CookIfNeeded(9501);
         const Mat4 firstAfterPointMove = hadInstances ? inst.InstanceTransforms()[0] : Mat4::Identity();
         const float pointDx = firstAfterPointMove.m[12] - firstBefore.m[12];

         shapeSrc.posY = 7.0f;
         inst.CookIfNeeded(9502);
         const Mat4 firstAfterShapeMove = hadInstances ? inst.InstanceTransforms()[0] : Mat4::Identity();
         const float shapeDy = firstAfterShapeMove.m[13] - firstAfterPointMove.m[13];

         const bool instancingFollowsTransforms = hadInstances &&
            std::fabs(pointDx - 5.0f) < 1e-3f && std::fabs(shapeDy - 7.0f) < 1e-3f;
         printf("instance on points follows source transforms: point dx=%.2f (want 5.00) "
                "shape dy=%.2f (want 7.00)  %s\n", pointDx, shapeDy,
                instancingFollowsTransforms ? "OK" : "FAIL");

         // 5. Cloth must drape from the input's transformed rest pose, not its
         //    raw object-space one - previously the input's own transform was
         //    dropped entirely when seeding the simulation.
         GeometryNode clothSrc;
         clothSrc.shape = 0; // plane
         clothSrc.detail = 4;
         ClothNode cloth;
         cloth.input = &clothSrc;
         cloth.pinMode = ClothNode::kPinNone;
         cloth.gravityX = cloth.gravityY = cloth.gravityZ = 0.0f;
         cloth.windX = cloth.windY = cloth.windZ = 0.0f;
         cloth.CookIfNeeded(9500);
         const bool hadClothVerts = !cloth.GetMesh().vertices.empty();
         const float clothVxBefore = hadClothVerts ? cloth.GetMesh().vertices[0].px : 0.0f;

         clothSrc.posX = 5.0f;
         cloth.CookIfNeeded(9501);
         const float clothVxAfter = hadClothVerts ? cloth.GetMesh().vertices[0].px : 0.0f;
         const bool clothFollowsTransform = hadClothVerts &&
            std::fabs((clothVxAfter - clothVxBefore) - 5.0f) < 1e-3f;
         printf("cloth rest pose follows input transform: dx=%.2f (want 5.00)  %s\n",
                clothVxAfter - clothVxBefore, clothFollowsTransform ? "OK" : "FAIL");

         // 6. A path following a mesh boundary must follow it in world space -
         //    previously the geometry source's transform was dropped, so the
         //    followed contour stayed put when the source moved.
         GeometryNode pathSrc;
         pathSrc.shape = 0; // plane, open so it has a boundary loop
         pathSrc.detail = 4;
         PathNode path;
         path.geometrySource = &pathSrc;
         path.followMode = PathNode::kFollowBoundary;
         path.speed = 0.0f;
         path.phase = 0.0f;
         path.pingPong = false;
         path.sizeX = path.sizeY = path.sizeZ = 1.0f;
         path.CookIfNeeded(9500);
         float pathBefore[3]; path.CurrentPoint(pathBefore);
         const bool pathHasFollow = path.IsFollowing();

         pathSrc.posX = 5.0f;
         path.CookIfNeeded(9501);
         float pathAfter[3]; path.CurrentPoint(pathAfter);
         const bool pathFollowsTransform = pathHasFollow &&
            std::fabs((pathAfter[0] - pathBefore[0]) - 5.0f) < 1e-3f;
         printf("path follow tracks geometry source transform: dx=%.2f (want 5.00)  %s\n",
                pathAfter[0] - pathBefore[0], pathFollowsTransform ? "OK" : "FAIL");

         const bool allOk = saved && std::fabs(after - 2.5f) < 1e-4f &&
                            joinAfter > joinBefore + 2.0f && instancingFollowsTransforms &&
                            clothFollowsTransform && pathFollowsTransform;
         printf("%s\n", allOk ? "BUGFIXES OK" : "SUSPECT");
      }

      // Sweeps every node type that consumes an IGeometrySource, checking one
      // thing: does moving/rotating/scaling its upstream source actually move
      // the final world-space result? BUGTEST above proves specific fixtures
      // stay fixed; this proves the same property for every node that takes a
      // geometry input, generically, so a newly added node type is covered
      // without anyone having to remember to hand-write a fixture for it.
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

      // Sibling of TRANSFORMSWEEPTEST, same generic-probe approach, checking a
      // different side-channel: does GetMappingTransform() reach a node's
      // output from its input? Found via a real bug: ClothNode, MeshResynthNode
      // and MeshToPointsNode forwarded every other side-channel (material,
      // textures, model matrix) from their single geo input but not this one,
      // so a Mapping node patched upstream of any of them had its space/
      // translate/rotate/scale silently dropped before Render 3D ever saw it.
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

         // MappingNode itself is excluded on purpose: it *sets* the mapping
         // transform from its own params rather than forwarding one, so it is
         // not a passthrough case this check applies to. InstanceOnPointsNode
         // and PathNode are excluded for the same reason TRANSFORMSWEEPTEST
         // gives them their own variant - neither reduces to a plain
         // GetMappingTransform() a caller would read.
         bool allOk = true;
         for (const Result& r : results)
         {
            printf("  [%s] %-24s\n", r.ok ? "pass" : "FAIL", r.name.c_str());
            if (!r.ok)
               allOk = false;
         }
         printf("%s\n", allOk ? "MAPPING SWEEP OK" : "MAPPING SWEEP FAIL");
      }

      // A different bug class from the two sweeps above: not a dropped
      // side-channel, but a revision/generation stamp that bumps when nothing
      // actually changed. Found via a real bug: DisplacementNode bumped
      // mTexGeneration on every single cook while a texture was connected,
      // even when the texture's pixels were identical to last frame, which
      // made MeshRevision() change every frame and forced ClothNode
      // downstream to treat every frame as a topology change - the cloth sim
      // reset to rest pose continuously instead of ever draping.
      //
      // The check: cook the same node twice in a row with nothing about its
      // inputs changed, and assert MeshRevision() (or the equivalent stamp)
      // did not move between the two cooks. Any node with a texture input
      // gets a *connected, static* texture for the same reason the Displace
      // bug only showed up with one patched in - the bug is invisible with no
      // texture connected at all.
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

      // Regression test for Render3DNode's frame-cache signature missing the
      // instancing and per-source-transform stamps. Confirms neither a
      // Particle System -> Instance on Points chain (no MeshRevision/
      // PointCloudRevision/CurveStamp bump - all motion carried by
      // InstanceRevision(), which the signature didn't track) nor a
      // spinY-animated Geometry node (a live GetModelMatrix() with no
      // revision bump of its own) leaves Render 3D's output frozen across
      // advancing frames with nothing but the transport clock moving -
      // the same as a user just sitting there, not touching the viewport.
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

      // A different bug class from RENDER3DLIVETEST above: not a live-updating
      // source with no revision bump of its own, but the opposite direction -
      // a real upstream mesh/cloud/curve change that DOES bump a revision,
      // but never reaches Render 3D's rendered pixels because
      // BuildSceneSignature's per-slot geomRev used to XOR-fold
      // MeshRevision()/PointCloudRevision()/CurveStamp() into one value.
      // Several IGeometrySource implementations deliberately return the same
      // counter from two of those three accessors (mesh cache and point/curve
      // cache rebuilt together, sharing one stamp), which made the XOR
      // cancel to a constant 0 forever - the cache never invalidated and the
      // viewport froze on the first frame. See the SceneSignature comment in
      // Geometry3DNodes.h for the full story.
      //
      // The check, per geometry-consuming node type: cook Render 3D once,
      // capture NodeWorkCounter() (bumped only on a real, non-cached render -
      // Geometry3DNodes.cpp's CookIfNeeded increments it right after the
      // cache-hit early-return check), change something upstream that
      // genuinely alters the geometry, cook Render 3D again, and assert
      // NodeWorkCounter() advanced - i.e. the cache early-return was NOT
      // taken.
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

      if (getenv("INFINITE_AUDIORECTEST") != nullptr)
      {
         auto* out = static_cast<OutputNode*>(gNodes[1].node.get());
         if (frameId == 2)
         {
            printf("audio source loaded: %d (%s)\n", (int)static_cast<AudioFileNode*>(gNodes[2].node.get())->IsLoaded(),
                   static_cast<AudioFileNode*>(gNodes[2].node.get())->Status().c_str());
            const bool started = out->StartRecording("/tmp/infinite_audiorec.mov");
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

            const Platform::MovieInfo withAudio = Platform::InspectMovie("/tmp/infinite_audiorec.mov");
            const double expectedDuration = (double)frames / 30.0;
            printf("with-audio movie: video=%d audio=%d duration=%.2fs (expected ~%.2fs)\n",
                   withAudio.hasVideo, withAudio.hasAudio, withAudio.duration, expectedDuration);

            // A control recording with includeAudio off, so the difference is
            // attributable to the checkbox and not to something environmental.
            out->includeAudio = false;
            out->StartRecording("/tmp/infinite_videoonly.mov");
         }
         if (frameId == 122)
         {
            const int frames = out->RecordedFrames();
            out->StopRecording();
            const Platform::MovieInfo videoOnly = Platform::InspectMovie("/tmp/infinite_videoonly.mov");
            printf("video-only movie: video=%d audio=%d duration=%.2fs (%d frames)\n",
                   videoOnly.hasVideo, videoOnly.hasAudio, videoOnly.duration, frames);

            const Platform::MovieInfo withAudio = Platform::InspectMovie("/tmp/infinite_audiorec.mov");
            const bool ok = withAudio.hasVideo && withAudio.hasAudio &&
                            videoOnly.hasVideo && !videoOnly.hasAudio &&
                            withAudio.duration > 0.5;
            printf("%s\n", ok ? "AUDIO RECORDING OK" : "SUSPECT");
         }
      }

      if (getenv("INFINITE_PATCHTEST") != nullptr && frameId == 4)
      {
         const std::string path = "/tmp/infinite_roundtrip.infinite";
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

      if (getenv("INFINITE_DELETECRASHTEST") != nullptr && frameId == 4)
      {
         RemoveNodeByIndex(gNodes[0].index);
      }
      if (getenv("INFINITE_DELETECRASHTEST") != nullptr && frameId == 6)
      {
         Render3DNode* render = nullptr;
         InstanceOnPointsNode* inst = nullptr;
         MetaBallNode* meta = nullptr;
         PathNode* path = nullptr;
         for (GraphNode& gn : gNodes)
         {
            if (!render) render = dynamic_cast<Render3DNode*>(gn.node.get());
            if (!inst) inst = dynamic_cast<InstanceOnPointsNode*>(gn.node.get());
            if (!meta) meta = dynamic_cast<MetaBallNode*>(gn.node.get());
            if (!path) path = dynamic_cast<PathNode*>(gn.node.get());
            gn.node->CookIfNeeded(frameId); // must not crash on the freed source
         }
         const bool cleared = render && inst && meta && path &&
                              render->geometry[0] == nullptr &&
                              inst->pointSource == nullptr && inst->instanceShape == nullptr &&
                              inst->cloudSource == nullptr &&
                              meta->cloudSource == nullptr &&
                              path->curveSource == nullptr && path->geometrySource == nullptr;
         printf("delete-crash: 4 nodes survived cook, every field cleared=%d  %s\n",
                cleared, (render && inst && meta && path && cleared) ? "OK" : "FAIL");
      }

      // DELETECRASHTEST for the audio graph (docs/plans/audio/README.md §4/§7):
      // every node type DiscoverAudioSweepCandidates finds touching the audio/
      // note cable graph at all (not just ones with an AudioNode to drive -
      // this is broader than AUDIOPARAMSWEEPTEST's filter, so it also covers
      // pure terminals like Audio Out) gets spawned into the real editor
      // graph, wired with whatever companion nodes its shape needs, rendered
      // a few blocks "mid-playback", deleted via the real RemoveNodeByIndex
      // (which is what exercises DisconnectAllTo's generic AudioInputSlot/
      // NoteInputSlot loop), then rendered a few more blocks. Surviving to the
      // final printf is most of the proof - a dangling pointer to the freed
      // node's AudioNode crashes the very next ProcessOffline, the same way
      // DELETECRASHTEST's geometry nodes crash on the next cook.
      if (getenv("INFINITE_AUDIOTEARDOWNSWEEPTEST") != nullptr && frameId == 4)
      {
         std::vector<AudioSweepCandidate> candidates = DiscoverAudioSweepCandidates(
            [](const AudioNodeShape& s) { return s.ParticipatesInAudioGraph(); });

         std::vector<float> renderL(256), renderR(256);
         float* renderChans[2] = { renderL.data(), renderR.data() };
         AudioBuffer renderBuf;
         renderBuf.channels = renderChans;
         renderBuf.numChannels = 2;
         renderBuf.numFrames = 256;

         auto SpawnIndex = [&](const std::string& name, const std::string& category, float x, float y) -> int
         {
            GraphNode* gn = SpawnNode(name, category, x, y);
            return gn ? gn->index : -1;
         };

         bool overallOk = true;
         for (const AudioSweepCandidate& cand : candidates)
         {
            // Every spawn below happens first, collecting only indices -
            // GraphNode is stored by value in the gNodes vector, so each
            // SpawnNode's push_back can reallocate and invalidate *every*
            // GraphNode* taken from an earlier SpawnNode call in this same
            // iteration, not just across a RemoveNodeByIndex. Wiring re-
            // resolves each one via FindNodeByIndex only after all spawning
            // for this candidate is done.
            const int candidateIndex = SpawnIndex(cand.name, cand.category, 40.0f, 40.0f);
            bool ok = candidateIndex >= 0;
            std::vector<int> spawnedIndices;
            if (ok)
               spawnedIndices.push_back(candidateIndex);

            int upstreamNoteIndex = -1;
            int downstreamGainIndex = -1;
            int downstreamOutIndex = -1;
            int upstreamGainIndex = -1;
            int downstreamNoteSinkIndex = -1;

            if (ok && cand.shape.noteInputSlots > 0)
               upstreamNoteIndex = SpawnIndex("MIDI Notes", "Notes", 40.0f, 260.0f);
            if (ok && cand.shape.isAudioSource)
            {
               downstreamGainIndex = SpawnIndex("Gain", "AudioUtility", 320.0f, 40.0f);
               downstreamOutIndex = SpawnIndex("Audio Out", "AudioUtility", 600.0f, 40.0f);
            }
            else if (ok && cand.shape.audioInputSlots > 0)
            {
               // A pure audio terminal/effect that isn't itself an
               // IAudioSource (Audio Out is the only current example) still
               // needs an upstream feed so its own input cable has something
               // real to clear on delete.
               upstreamGainIndex = SpawnIndex("Gain", "AudioUtility", 40.0f, 260.0f);
            }
            if (ok && cand.shape.isNoteSource && cand.shape.noteInputSlots == 0)
            {
               // Pure note producer (MIDI Notes): give it a downstream
               // consumer so DisconnectAllTo's NoteInputSlot loop has a real
               // cable pointing at the candidate to clear, not just its own.
               // Note Filter, not Envelope: Envelope moved to a
               // ModulatorInputSlot-only shaper (03-envelope-to-shaper.md)
               // and no longer has a note pin to wire this cable into.
               downstreamNoteSinkIndex = SpawnIndex("Note Filter", "Notes", 320.0f, 480.0f);
            }
            for (int idx : { upstreamNoteIndex, downstreamGainIndex, downstreamOutIndex,
                              upstreamGainIndex, downstreamNoteSinkIndex })
               if (idx >= 0)
                  spawnedIndices.push_back(idx);

            // All spawning done - safe to hold pointers now, nothing below
            // pushes into gNodes again this iteration.
            GraphNode* cn = ok ? FindNodeByIndex(candidateIndex) : nullptr;
            if (upstreamNoteIndex >= 0)
            {
               if (GraphNode* upstreamNote = FindNodeByIndex(upstreamNoteIndex))
                  if (NoteCable* nc = cn->node->NoteInputSlot(cand.shape.firstNoteInputSlot))
                     nc->Connect(upstreamNote->node.get());
            }
            if (downstreamGainIndex >= 0 && downstreamOutIndex >= 0)
            {
               GraphNode* downstreamGain = FindNodeByIndex(downstreamGainIndex);
               GraphNode* downstreamOut = FindNodeByIndex(downstreamOutIndex);
               if (downstreamGain && downstreamOut)
               {
                  if (AudioCable* gainIn = downstreamGain->node->AudioInputSlot(0))
                     gainIn->Connect(cn->node.get());
                  if (AudioCable* outIn = downstreamOut->node->AudioInputSlot(0))
                     outIn->Connect(downstreamGain->node.get());
               }
            }
            if (upstreamGainIndex >= 0)
            {
               if (GraphNode* upstreamGain = FindNodeByIndex(upstreamGainIndex))
                  if (AudioCable* in0 = cn->node->AudioInputSlot(0))
                     in0->Connect(upstreamGain->node.get());
            }
            if (downstreamNoteSinkIndex >= 0)
            {
               if (GraphNode* downstreamNoteSink = FindNodeByIndex(downstreamNoteSinkIndex))
                  if (NoteCable* nc = downstreamNoteSink->node->NoteInputSlot(0))
                     nc->Connect(cn->node.get());
            }
            cn = nullptr; // about to RebuildAudioTopology/ProcessOffline; re-resolve if needed again

            RebuildAudioTopology();
            for (int i = 0; i < 4; i++)
               AudioEngine::Instance().ProcessOffline(renderBuf); // "mid-playback"

            if (ok)
               RemoveNodeByIndex(candidateIndex); // exercises DisconnectAllTo generically

            for (int i = 0; i < 6; i++)
               AudioEngine::Instance().ProcessOffline(renderBuf); // keep rendering post-delete

            // Cables that pointed at the deleted node must be cleared, not
            // left dangling - the exact use-after-free surface DELETECRASHTEST
            // checks on the geometry side. Re-resolved by index, since the
            // delete above may have shifted every GraphNode* taken earlier.
            bool cablesCleared = true;
            if (downstreamGainIndex >= 0)
            {
               GraphNode* downstreamGain = FindNodeByIndex(downstreamGainIndex);
               AudioCable* gainIn = downstreamGain ? downstreamGain->node->AudioInputSlot(0) : nullptr;
               cablesCleared = cablesCleared && (gainIn == nullptr || gainIn->GetSource() == nullptr);
            }
            if (downstreamNoteSinkIndex >= 0)
            {
               GraphNode* downstreamNoteSink = FindNodeByIndex(downstreamNoteSinkIndex);
               NoteCable* nc = downstreamNoteSink ? downstreamNoteSink->node->NoteInputSlot(0) : nullptr;
               cablesCleared = cablesCleared && (nc == nullptr || nc->GetSource() == nullptr);
            }

            // Reaching here at all - through the deletion, the rebuild, and
            // six more offline render blocks - is the "no crash" half of the
            // invariant; a dangling AudioNode* in the topology would have
            // segfaulted inside ProcessOffline above, not here.
            const bool nodeOk = ok && cablesCleared;
            printf("  [%s] %-24s spawned, wired, deleted mid-playback, kept rendering, cables cleared=%d\n",
                   nodeOk ? "pass" : "FAIL", cand.name.c_str(), cablesCleared);
            fflush(stdout);
            if (!nodeOk)
               overallOk = false;

            for (int idx : spawnedIndices)
            {
               if (idx == candidateIndex)
                  continue; // already removed above
               RemoveNodeByIndex(idx);
            }
         }

         printf("xruns=%llu\n", (unsigned long long)AudioEngine::Instance().XrunCount());
         printf("%s\n", overallOk ? "AUDIO TEARDOWN SWEEP OK" : "AUDIO TEARDOWN SWEEP FAIL");
      }

      // NoteEventQueue multi-consumer fanout regression (docs/plans/
      // fm-and-note-fanout-fixes.md item 11): before the fix, a single-cursor
      // queue meant the FIRST consumer to Pop() in topology order drained
      // every event, starving every other consumer wired to the same
      // producer. The bug report's exact patch - one note source feeding two
      // synths, one of which also feeds the other's FM input, so the FM
      // modulator (a dependency) necessarily runs before the synth it
      // modulates - made the second synth silent ("output peak=0.00000").
      // Goes through the real graph and the real RebuildAudioTopology (not a
      // hand-rolled inbox), because the thing under test is the topology
      // builder's ResetConsumers()/RegisterConsumer() wiring itself, not the
      // queue's own Push/Pop (already covered by NoteEventQueue's unit-level
      // use in the other fixtures above).
      if (getenv("INFINITE_NOTEFANOUTTEST") != nullptr && frameId == 4)
      {
         auto SpawnIndex = [&](const std::string& name, const std::string& category, float x, float y) -> int
         {
            GraphNode* gn = SpawnNode(name, category, x, y);
            return gn ? gn->index : -1;
         };

         // Renders `osc1` into `osc1Buf`, then `osc2` reading `osc1Buf` on
         // its fm-in slot (1) - the exact per-block order RebuildAudioTopology's
         // own CollectAudioChain walk would run them in, since osc2's audio
         // cable makes osc1 its dependency. Returns osc2's rendered L peak.
         auto RunFmPair = [&](AudioNode* osc1, AudioNode* osc2, float* osc2LOut, int numFrames) -> float
         {
            std::vector<float> o1L(numFrames), o1R(numFrames);
            float* o1Chans[2] = { o1L.data(), o1R.data() };
            AudioBuffer o1Buf;
            o1Buf.channels = o1Chans;
            o1Buf.numChannels = 2;
            o1Buf.numFrames = numFrames;

            std::vector<float> o2L(numFrames), o2R(numFrames);
            float* o2Chans[2] = { o2L.data(), o2R.data() };
            AudioBuffer o2Buf;
            o2Buf.channels = o2Chans;
            o2Buf.numChannels = 2;
            o2Buf.numFrames = numFrames;

            float peak = 0.0f;
            for (int b = 0; b < 10; b++)
            {
               osc1->ProcessBlock(nullptr, 0, o1Buf);
               const AudioBuffer* osc2Inputs[2] = { nullptr, &o1Buf };
               osc2->ProcessBlock(osc2Inputs, 2, o2Buf);
            }
            for (int i = 0; i < numFrames; i++)
               peak = std::max(peak, std::fabs(o2L[i]));
            if (osc2LOut != nullptr)
               std::copy(o2L.begin(), o2L.end(), osc2LOut);
            return peak;
         };

         bool overallOk = true;
         const int numFrames = 256;

         // Case 1: the bug report's exact patch - producer fans out to osc1
         // (FM modulator) and osc2 (fed by osc1's FM in), both wired from
         // the SAME producer outbox.
         {
            const int producerIdx = SpawnIndex("Note Stack", "Notes", 40.0f, 40.0f);
            const int osc1Idx = SpawnIndex("Oscillator", "Synths", 320.0f, 40.0f);
            const int osc2Idx = SpawnIndex("Oscillator", "Synths", 600.0f, 40.0f);
            GraphNode* producer = FindNodeByIndex(producerIdx);
            GraphNode* osc1 = FindNodeByIndex(osc1Idx);
            GraphNode* osc2 = FindNodeByIndex(osc2Idx);
            bool wired = producer && osc1 && osc2;
            if (wired)
            {
               osc1->node->NoteInputSlot(0)->Connect(producer->node.get());
               osc2->node->NoteInputSlot(0)->Connect(producer->node.get());
               osc2->node->AudioInputSlot(1)->Connect(osc1->node.get()); // osc1 -> osc2's fm in
               producer->node->CookIfNeeded(1);
               osc1->node->CookIfNeeded(1);
               osc2->node->CookIfNeeded(1);
            }

            RebuildAudioTopology();

            // Nothing upstream feeds the producer itself in this graph -
            // hand it a note-on directly the same way RunNoteStackFixture
            // drives NoteStackNode standalone. This has to happen AFTER
            // RebuildAudioTopology: the rebuild's wiring loop calls
            // SetNoteInbox(nullptr, -1) on every node with an unconnected
            // note-input slot, which would otherwise stomp this right back
            // out.
            NoteEventQueue producerInbox;
            AudioNode* producerAudio = wired ? AudioNodeOfAny(producer->node.get()) : nullptr;
            if (producerAudio)
               producerAudio->SetNoteInbox(&producerInbox, producerInbox.RegisterConsumer());

            AudioNode* osc1Audio = wired ? AudioNodeOfAny(osc1->node.get()) : nullptr;
            AudioNode* osc2Audio = wired ? AudioNodeOfAny(osc2->node.get()) : nullptr;

            bool caseOk = wired && producerAudio && osc1Audio && osc2Audio;
            int voices1 = 0, voices2 = 0;
            float peak2 = 0.0f;
            if (caseOk)
            {
               NoteEvent on;
               on.note = 69;
               on.velocity = 0.8f;
               on.isNoteOn = true;
               on.voiceId = NextVoiceId();
               producerInbox.Push(on);

               std::vector<float> dL(numFrames), dR(numFrames);
               float* dChans[2] = { dL.data(), dR.data() };
               AudioBuffer dBuf;
               dBuf.channels = dChans;
               dBuf.numChannels = 2;
               dBuf.numFrames = numFrames;
               producerAudio->ProcessBlock(nullptr, 0, dBuf);

               peak2 = RunFmPair(osc1Audio, osc2Audio, nullptr, numFrames);
               voices1 = static_cast<OscillatorNode*>(osc1->node.get())->ActiveVoices();
               voices2 = static_cast<OscillatorNode*>(osc2->node.get())->ActiveVoices();
            }

            caseOk = caseOk && voices1 > 0 && voices2 > 0 && peak2 > 1e-6f;
            printf("  [%s] fanout+FM (bug report patch): osc1 voices=%d osc2 voices=%d osc2 output peak=%.6f\n",
                   caseOk ? "pass" : "FAIL", voices1, voices2, peak2);
            overallOk = overallOk && caseOk;

            for (int idx : { producerIdx, osc1Idx, osc2Idx })
               if (idx >= 0)
                  RemoveNodeByIndex(idx);
         }

         // Case 2: three consumers on one producer - every one of them must
         // still see the note-on, not just the first two.
         {
            const int producerIdx = SpawnIndex("Note Stack", "Notes", 40.0f, 260.0f);
            const int oscIdx[3] = {
               SpawnIndex("Oscillator", "Synths", 320.0f, 200.0f),
               SpawnIndex("Oscillator", "Synths", 320.0f, 260.0f),
               SpawnIndex("Oscillator", "Synths", 320.0f, 320.0f),
            };
            GraphNode* producer = FindNodeByIndex(producerIdx);
            GraphNode* osc[3] = { FindNodeByIndex(oscIdx[0]), FindNodeByIndex(oscIdx[1]), FindNodeByIndex(oscIdx[2]) };
            bool wired = producer && osc[0] && osc[1] && osc[2];
            if (wired)
            {
               for (GraphNode* o : osc)
               {
                  o->node->NoteInputSlot(0)->Connect(producer->node.get());
                  o->node->CookIfNeeded(1);
               }
               producer->node->CookIfNeeded(1);
            }

            RebuildAudioTopology();

            NoteEventQueue producerInbox;
            AudioNode* producerAudio = wired ? AudioNodeOfAny(producer->node.get()) : nullptr;
            if (producerAudio)
               producerAudio->SetNoteInbox(&producerInbox, producerInbox.RegisterConsumer());

            bool caseOk = wired && producerAudio;
            int voices[3] = {};
            if (caseOk)
            {
               NoteEvent on;
               on.note = 64;
               on.velocity = 0.8f;
               on.isNoteOn = true;
               on.voiceId = NextVoiceId();
               producerInbox.Push(on);

               std::vector<float> dL(numFrames), dR(numFrames);
               float* dChans[2] = { dL.data(), dR.data() };
               AudioBuffer dBuf;
               dBuf.channels = dChans;
               dBuf.numChannels = 2;
               dBuf.numFrames = numFrames;
               producerAudio->ProcessBlock(nullptr, 0, dBuf);

               for (int i = 0; i < 3; i++)
               {
                  AudioNode* a = AudioNodeOfAny(osc[i]->node.get());
                  std::vector<float> oL(numFrames), oR(numFrames);
                  float* oChans[2] = { oL.data(), oR.data() };
                  AudioBuffer oBuf;
                  oBuf.channels = oChans;
                  oBuf.numChannels = 2;
                  oBuf.numFrames = numFrames;
                  a->ProcessBlock(nullptr, 0, oBuf);
                  voices[i] = static_cast<OscillatorNode*>(osc[i]->node.get())->ActiveVoices();
               }
            }

            caseOk = caseOk && voices[0] > 0 && voices[1] > 0 && voices[2] > 0;
            printf("  [%s] three-consumer fanout: voices=%d,%d,%d (all must see the note-on)\n",
                   caseOk ? "pass" : "FAIL", voices[0], voices[1], voices[2]);
            overallOk = overallOk && caseOk;

            for (int idx : { producerIdx, oscIdx[0], oscIdx[1], oscIdx[2] })
               if (idx >= 0)
                  RemoveNodeByIndex(idx);
         }

         // Case 3: one of two consumers is deleted mid-playback (the real
         // RemoveNodeByIndex, which triggers its own RebuildAudioTopology -
         // exercising ResetConsumers/RegisterConsumer's re-issue of cursor
         // ids on the surviving edge). The survivor must keep receiving
         // events with no crash and no dangling read.
         {
            const int producerIdx = SpawnIndex("Note Stack", "Notes", 40.0f, 480.0f);
            const int survivorIdx = SpawnIndex("Oscillator", "Synths", 320.0f, 460.0f);
            const int doomedIdx = SpawnIndex("Oscillator", "Synths", 320.0f, 520.0f);
            GraphNode* producer = FindNodeByIndex(producerIdx);
            GraphNode* survivor = FindNodeByIndex(survivorIdx);
            GraphNode* doomed = FindNodeByIndex(doomedIdx);
            bool wired = producer && survivor && doomed;
            if (wired)
            {
               survivor->node->NoteInputSlot(0)->Connect(producer->node.get());
               doomed->node->NoteInputSlot(0)->Connect(producer->node.get());
               producer->node->CookIfNeeded(1);
               survivor->node->CookIfNeeded(1);
               doomed->node->CookIfNeeded(1);
            }

            RebuildAudioTopology();

            NoteEventQueue producerInbox;
            AudioNode* producerAudio = wired ? AudioNodeOfAny(producer->node.get()) : nullptr;
            if (producerAudio)
               producerAudio->SetNoteInbox(&producerInbox, producerInbox.RegisterConsumer());

            bool caseOk = wired && producerAudio;
            if (caseOk)
            {
               std::vector<float> dL(numFrames), dR(numFrames);
               float* dChans[2] = { dL.data(), dR.data() };
               AudioBuffer dBuf;
               dBuf.channels = dChans;
               dBuf.numChannels = 2;
               dBuf.numFrames = numFrames;

               NoteEvent on1;
               on1.note = 60;
               on1.velocity = 0.8f;
               on1.isNoteOn = true;
               on1.voiceId = NextVoiceId();
               producerInbox.Push(on1);
               producerAudio->ProcessBlock(nullptr, 0, dBuf);

               AudioNode* survivorAudio = AudioNodeOfAny(survivor->node.get());
               std::vector<float> sL(numFrames), sR(numFrames);
               float* sChans[2] = { sL.data(), sR.data() };
               AudioBuffer sBuf;
               sBuf.channels = sChans;
               sBuf.numChannels = 2;
               sBuf.numFrames = numFrames;
               survivorAudio->ProcessBlock(nullptr, 0, sBuf);

               RemoveNodeByIndex(doomedIdx); // triggers a real RebuildAudioTopology

               // A fresh note-on after the delete - the survivor's cursor was
               // just re-issued by the rebuild above, so this proves it still
               // reads real events, not a stale/dangling cursor id.
               NoteEvent on2;
               on2.note = 67;
               on2.velocity = 0.8f;
               on2.isNoteOn = true;
               on2.voiceId = NextVoiceId();
               producerInbox.Push(on2);
               producerAudio->ProcessBlock(nullptr, 0, dBuf);
               survivorAudio->ProcessBlock(nullptr, 0, sBuf); // must not crash on a freed doomed

               // Re-resolve rather than reuse the pre-delete GraphNode* -
               // gNodes.erase() shifts every element after the erased one
               // down a slot, invalidating pointers taken before the call
               // (see the cutoff-mod teardown case's comment on the same
               // hazard).
               survivor = FindNodeByIndex(survivorIdx);
               const int survivorVoices =
                  survivor ? static_cast<OscillatorNode*>(survivor->node.get())->ActiveVoices() : -1;
               caseOk = survivorVoices > 0;
               printf("  [%s] consumer deleted mid-playback: survivor voices=%d after delete+re-note (no crash)\n",
                      caseOk ? "pass" : "FAIL", survivorVoices);
            }
            else
            {
               printf("  [FAIL] consumer deleted mid-playback: rig failed to build\n");
            }
            overallOk = overallOk && caseOk;

            for (int idx : { producerIdx, survivorIdx })
               if (idx >= 0)
                  RemoveNodeByIndex(idx);
         }

         printf("%s\n", overallOk ? "NOTE FANOUT TEST OK" : "NOTE FANOUT TEST FAIL");
      }

      // Audio Filter's "cutoff mod" sidechain input (slot 1), deleted
      // mid-playback: the generic AUDIOTEARDOWNSWEEPTEST above only ever
      // wires an upstream node into slot 0 (see its
      // "upstreamGainIndex"/AudioInputSlot(0) comment) - a modulator wired
      // specifically into a second/sidechain slot and then deleted is not
      // exercised there, so this covers that gap directly (docs/plans/
      // fm-and-note-fanout-fixes.md item 11 part C).
      if (getenv("INFINITE_NOTEFANOUTTEST") != nullptr && frameId == 4)
      {
         auto SpawnIndex = [&](const std::string& name, const std::string& category, float x, float y) -> int
         {
            GraphNode* gn = SpawnNode(name, category, x, y);
            return gn ? gn->index : -1;
         };

         const int modIdx = SpawnIndex("Oscillator", "Synths", 40.0f, 700.0f);
         const int filterIdx = SpawnIndex("Audio Filter", "AudioEffects", 320.0f, 700.0f);
         const int upstreamGainIdx = SpawnIndex("Gain", "AudioUtility", 40.0f, 760.0f);
         GraphNode* mod = FindNodeByIndex(modIdx);
         GraphNode* filter = FindNodeByIndex(filterIdx);
         GraphNode* upstreamGain = FindNodeByIndex(upstreamGainIdx);
         bool wired = mod && filter && upstreamGain;
         if (wired)
         {
            filter->node->AudioInputSlot(0)->Connect(upstreamGain->node.get()); // real signal to filter
            filter->node->AudioInputSlot(1)->Connect(mod->node.get());          // oscillator -> cutoff mod
            mod->node->CookIfNeeded(1);
            filter->node->CookIfNeeded(1);
            upstreamGain->node->CookIfNeeded(1);
         }

         RebuildAudioTopology();

         const int numFrames = 256;
         std::vector<float> fL(numFrames), fR(numFrames);
         float* fChans[2] = { fL.data(), fR.data() };
         AudioBuffer fBuf;
         fBuf.channels = fChans;
         fBuf.numChannels = 2;
         fBuf.numFrames = numFrames;

         bool ok = wired;
         if (ok)
         {
            AudioNode* filterAudio = AudioNodeOfAny(filter->node.get());
            AudioNode* modAudio = AudioNodeOfAny(mod->node.get());
            AudioNode* gainAudio = AudioNodeOfAny(upstreamGain->node.get());

            std::vector<float> mL(numFrames), mR(numFrames);
            float* mChans[2] = { mL.data(), mR.data() };
            AudioBuffer mBuf;
            mBuf.channels = mChans;
            mBuf.numChannels = 2;
            mBuf.numFrames = numFrames;

            std::vector<float> gL(numFrames), gR(numFrames);
            float* gChans[2] = { gL.data(), gR.data() };
            AudioBuffer gBuf;
            gBuf.channels = gChans;
            gBuf.numChannels = 2;
            gBuf.numFrames = numFrames;
            const AudioBuffer* gainInputs[1] = { &gBuf }; // Gain's own input is silence; fine, still exercises the filter

            for (int b = 0; b < 4; b++)
            {
               modAudio->ProcessBlock(nullptr, 0, mBuf);
               gainAudio->ProcessBlock(gainInputs, 1, gBuf);
               const AudioBuffer* filterInputs[2] = { &gBuf, &mBuf };
               filterAudio->ProcessBlock(filterInputs, 2, fBuf);
            }

            RemoveNodeByIndex(modIdx); // deletes the cutoff-mod source mid-playback

            // gNodes.erase() shifts every element after the erased one down
            // a slot, invalidating any GraphNode* taken before the call
            // (same hazard AUDIOTEARDOWNSWEEPTEST's own comment on SpawnNode
            // documents for push_back) - `filter` was resolved before the
            // removal above, so it must be re-resolved by index rather than
            // dereferenced directly, or this is exactly the same
            // dangling-pointer bug DELETECRASHTEST exists to catch.
            filter = FindNodeByIndex(filterIdx);

            // Filter must keep processing post-delete - DisconnectAllTo
            // clears its sidechain cable, so slot 1 reads back as
            // nullptr/inactive rather than a dangling AudioNode*.
            for (int b = 0; b < 4; b++)
            {
               gainAudio->ProcessBlock(gainInputs, 1, gBuf);
               const AudioBuffer* filterInputs[2] = { &gBuf, nullptr };
               filterAudio->ProcessBlock(filterInputs, 2, fBuf);
            }
            ok = filter != nullptr && filter->node->AudioInputSlot(1)->GetSource() == nullptr;
         }
         printf("%s\n", ok ? "FILTER CUTOFF MOD TEARDOWN OK" : "FILTER CUTOFF MOD TEARDOWN FAIL");

         for (int idx : { filterIdx, upstreamGainIdx })
            if (idx >= 0)
               RemoveNodeByIndex(idx);
      }

      if (getenv("INFINITE_AUDIOGRAPHTEST") != nullptr && frameId == 4)
      {
         const std::string path = "/tmp/infinite_audiographtest.infinite";
         const bool saved = SavePatchTo(path);
         const size_t nodesBefore = gNodes.size();
         const bool loaded = LoadPatchFrom(path);

         GainNode* gain = nullptr;
         AudioOutputNode* out = nullptr;
         WavetableNode* osc = nullptr;
         int gainIndex = -1;
         for (GraphNode& gn : gNodes)
         {
            if (!osc) osc = dynamic_cast<WavetableNode*>(gn.node.get());
            if (!gain) { gain = dynamic_cast<GainNode*>(gn.node.get()); if (gain) gainIndex = gn.index; }
            if (!out) out = dynamic_cast<AudioOutputNode*>(gn.node.get());
         }
         const bool wiring = osc != nullptr && gain != nullptr && out != nullptr &&
                             gain->input.GetSource() == osc && out->input.GetSource() == gain;

         printf("audio round trip: saved=%d loaded=%d nodes=%zu wiring=%d  %s\n",
                saved, loaded, gNodes.size(), wiring,
                (saved && loaded && nodesBefore == gNodes.size() && wiring) ? "OK" : "FAIL");

         // Mid-chain delete: remove Gain, confirm Audio Out's cable was
         // cleared generically (AudioInputSlot/DisconnectAllTo, step 7/8 of
         // P2) rather than left dangling, and that nothing crashes on the
         // next cook or the next topology rebuild.
         RemoveNodeByIndex(gainIndex);
         out = nullptr;
         for (GraphNode& gn : gNodes)
         {
            if (!out) out = dynamic_cast<AudioOutputNode*>(gn.node.get());
            gn.node->CookIfNeeded(frameId); // must not crash on the freed Gain
         }
         const bool clearedAfterDelete = out != nullptr && out->input.GetSource() == nullptr;
         printf("audio delete-crash: survived cook, cleared=%d  %s\n",
                clearedAfterDelete, (out != nullptr && clearedAfterDelete) ? "OK" : "FAIL");
      }

      if (getenv("INFINITE_MATFRAMETEST") != nullptr && frameId == 4)
      {
         auto* geo = static_cast<GeometryNode*>(gNodes[0].node.get());
         auto* mat = static_cast<MaterialNode*>(gNodes[1].node.get());
         auto* render = static_cast<Render3DNode*>(gNodes[2].node.get());

         // Material overrides the surface but must leave the mesh untouched,
         // stamp included, or it would defeat the upload cache.
         geo->color[0] = 1.0f; geo->color[1] = 0.0f; geo->color[2] = 0.0f;
         mat->color[0] = 0.0f; mat->color[1] = 0.0f; mat->color[2] = 1.0f;
         mat->metallic = 0.9f;
         const Material through = mat->GetMaterial();
         const bool overrides = through.color[2] > 0.9f && through.color[0] < 0.1f &&
                                through.metallic > 0.8f;
         const bool meshUntouched = &mat->GetMesh() == &geo->GetMesh() &&
                                    mat->MeshRevision() == geo->MeshRevision();
         printf("material overrides=%d, mesh passed through=%d\n", overrides, meshUntouched);

         // The geometry sits at (4,2,0) scaled 2x, well off-centre, so framing
         // has to move the target as well as pull the camera back.
         render->camDistance = 3.0f;
         render->targetX = 0.0f; render->targetY = 0.0f; render->targetZ = 0.0f;
         FrameSceneInView(render);
         const bool centred = std::fabs(render->targetX - 4.0f) < 0.2f &&
                              std::fabs(render->targetY - 2.0f) < 0.2f;
         const bool pulledBack = render->camDistance > 3.0f && render->camDistance < 30.0f;
         printf("frame: target (%.2f, %.2f, %.2f) distance %.2f\n",
                render->targetX, render->targetY, render->targetZ, render->camDistance);
         printf("%s\n", (overrides && meshUntouched && centred && pulledBack)
                           ? "MATERIAL + FRAME OK" : "SUSPECT");
      }

      if (getenv("INFINITE_ENVTEST") != nullptr &&
          (frameId == 4 || frameId == 5 || frameId == 7 || frameId == 8 || frameId == 12))
      {
         static int sEnvTestBaselineSphereR = 0;
         auto* env = static_cast<EnvironmentNode*>(gNodes[2].node.get());
         auto* render = static_cast<Render3DNode*>(gNodes[3].node.get());
         const int w = render->GetOutputWidth(), h = render->GetOutputHeight();
         std::vector<unsigned char> px((size_t)w * h * 4);
         GLuint fbo = 0;
         glGenFramebuffers(1, &fbo);
         glBindFramebuffer(GL_FRAMEBUFFER, fbo);
         glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                render->GetOutputTexture(), 0);
         glPixelStorei(GL_PACK_ALIGNMENT, 1);
         glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
         glBindFramebuffer(GL_FRAMEBUFFER, 0);
         glDeleteFramebuffers(1, &fbo);

         // Top-left corner, well away from the centred sphere: pure background.
         const size_t corner = ((size_t)(h - 4) * w + 4) * 4;
         const int bgR = px[corner], bgG = px[corner + 1], bgB = px[corner + 2];

         if (frameId == 4)
         {
            // HDRI patched in, used as background: the loaded sky (warm,
            // R>G>B) should read back, not the flat near-black default bgColor.
            const bool loaded = env->HasImage() && env->GetEnvironmentTexture() != 0;
            const bool sawSky = bgR > 60 && bgR > bgB;
            printf("env loaded=%d corner=(%d,%d,%d)  %s\n", loaded, bgR, bgG, bgB,
                   (loaded && sawSky) ? "HDRI BACKGROUND OK" : "SUSPECT");

            const GLenum err = glGetError();
            printf("gl error after env render: 0x%x  %s\n", err,
                   err == GL_NO_ERROR ? "CLEAN" : "SUSPECT");

            // A near-mirror sphere over a bright sky should read back brighter
            // than a flat grey ambient bake would ever give it - proof the
            // reflection actually sampled the HDRI rather than falling back to
            // the procedural gradient.
            const size_t centre = ((size_t)(h / 2) * w + w / 2) * 4;
            const int sphereR = px[centre], sphereG = px[centre + 1];
            printf("sphere centre=(%d,%d,%d)  %s\n", sphereR, sphereG, px[centre + 2],
                   (sphereR > 40) ? "REFLECTION SAMPLED HDRI OK" : "SUSPECT");

            // Every mip must be finite, not just mip 0. A single Inf/NaN texel
            // anywhere spreads under box-downsampling until the top of the
            // chain is entirely NaN, and the diffuse irradiance term reads
            // exactly that top mip - so a poisoned chain renders geometry
            // solid black or a flat garbage colour while the thumbnail and
            // background quad (both mip 0) still look perfect.
            int badMips = 0;
            glBindTexture(GL_TEXTURE_2D, env->GetEnvironmentTexture());
            for (int level = 0; level <= (int)env->MaxLod(); level++)
            {
               int lw = 0, lh = 0;
               glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_WIDTH, &lw);
               glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_HEIGHT, &lh);
               if (lw <= 0 || lh <= 0) { badMips++; continue; }
               std::vector<float> mip((size_t)lw * lh * 4);
               glGetTexImage(GL_TEXTURE_2D, level, GL_RGBA, GL_FLOAT, mip.data());
               for (float v : mip)
                  if (!std::isfinite(v)) { badMips++; break; }
            }
            glBindTexture(GL_TEXTURE_2D, 0);
            printf("env mip chain: %d level(s) non-finite  %s\n", badMips,
                   badMips == 0 ? "MIP CHAIN FINITE OK" : "SUSPECT");

            render->envAsBackground = false; // checked at frame 8
            sEnvTestBaselineSphereR = sphereR;
         }
         else if (frameId == 5)
         {
            // Regression check: dragging intensity must invalidate the cached
            // scene even though it doesn't touch TextureRevision() at all -
            // only the file load path does. No reconnect, no new texture,
            // same HDRI, and - unlike frame 4's envAsBackground flip above -
            // nothing else about the scene changes this frame, so this
            // isolates the intensity edit from any other cache-busting change.
            // Baseline is already blown out to 255 at intensity 1 (the fixture
            // HDRI is deliberately very bright), so a further increase
            // wouldn't show up in 8-bit readback - dim it down instead, which
            // has headroom to prove either direction of change. Checked at
            // frame 7, one frame after this one's own cook has run.
            env->intensity = 0.02f;
         }
         else if (frameId == 7)
         {
            const size_t centre = ((size_t)(h / 2) * w + w / 2) * 4;
            const int sphereR = px[centre];
            printf("intensity dim (no reconnect): sphere centre R %d -> %d  %s\n",
                   sEnvTestBaselineSphereR, sphereR,
                   (sphereR < sEnvTestBaselineSphereR) ? "INTENSITY LIVE-UPDATE OK" : "SUSPECT");
         }
         else if (frameId == 8)
         {
            // Same HDRI still drives lighting, but the background toggle is
            // off: the corner should fall back to the flat bgColor clear.
            const bool flatBg = bgR < 20 && bgG < 20 && bgB < 30;
            printf("background off: corner=(%d,%d,%d)  %s\n", bgR, bgG, bgB,
                   flatBg ? "BACKGROUND TOGGLE OK" : "SUSPECT");
            render->envAsBackground = true;
            render->envInput.Disconnect();
         }
         else if (frameId == 12)
         {
            // Env input fully disconnected: no HDRI texture is bound at all
            // any more, so the background quad is skipped outright and this
            // is back to the original flat bgColor clear - proving the HDRI
            // path is additive rather than a rewrite that left state behind
            // once its source node goes away.
            const bool flatBg = bgR < 20 && bgG < 20 && bgB < 30;
            printf("disconnected: corner=(%d,%d,%d)  %s\n", bgR, bgG, bgB,
                   flatBg ? "DISCONNECT FALLBACK OK" : "SUSPECT");
         }
      }

      if (getenv("INFINITE_PATHOCEANTEST") != nullptr && frameId == 4)
      {
         auto* path = static_cast<PathNode*>(gNodes[0].node.get());
         auto* ocean = static_cast<OceanNode*>(gNodes[1].node.get());
         Transport::Instance().SetPlaying(true);

         // Every path shape must stay in range and actually move over time.
         bool allShapes = true;
         for (int shape = 0; shape < PathNode::kShapeCount; shape++)
         {
            path->shape = shape;
            path->speed = 0.25f;
            float lo[3] = { 2, 2, 2 }, hi[3] = { -2, -2, -2 };
            bool inRange = true, finite = true;
            for (int step = 0; step < 64; step++)
            {
               // Phase is swept directly rather than waiting on the transport,
               // so a whole lap is covered inside one frame.
               path->phase = (float)step / 64.0f;
               path->CookIfNeeded(1000 + shape * 100 + step);
               float p[3];
               path->CurrentPoint(p);
               for (int k = 0; k < 3; k++)
               {
                  if (!std::isfinite(p[k])) finite = false;
                  lo[k] = std::min(lo[k], p[k]);
                  hi[k] = std::max(hi[k], p[k]);
               }
               for (int o = 0; o < 4; o++)
               {
                  IModulator* m = path->ModulatorOutput(o);
                  const float v = m ? m->Value01() : -1.0f;
                  if (v < 0.0f || v > 1.0f)
                     inRange = false;
               }
            }
            const float travel = std::max(hi[0]-lo[0], std::max(hi[1]-lo[1], hi[2]-lo[2]));
            const bool ok = finite && inRange && travel > 0.1f;
            allShapes &= ok;
            printf("  path %-10s travel %.2f  outputs in 0..1: %d  %s\n",
                   PathNode::ShapeNames()[shape].c_str(), travel, inRange, ok ? "OK" : "FAIL");
         }
         printf("%s\n", allShapes ? "PATH OK" : "SUSPECT");

         // The ocean must be a real displaced surface, not a flat plane, and it
         // has to change as the transport advances.
         const Mesh& m0 = ocean->GetMesh();
         float lo = 1e30f, hi = -1e30f;
         bool finite = true;
         for (const Vertex& v : m0.vertices)
         {
            if (!std::isfinite(v.py)) { finite = false; continue; }
            lo = std::min(lo, v.py); hi = std::max(hi, v.py);
         }
         const float relief = hi - lo;
         const size_t tris = m0.indices.size() / 3;
         const unsigned long long stampBefore = ocean->MeshRevision();

         Transport::Instance().Tick(2.0f);
         ocean->GetMesh();
         const unsigned long long stampAfter = ocean->MeshRevision();

         printf("ocean: %zu tris, wave relief %.3f, finite=%d, animates=%d\n",
                tris, relief, finite, stampAfter != stampBefore);
         printf("%s\n", (tris > 100 && finite && relief > 0.02f && stampAfter != stampBefore)
                           ? "OCEAN OK" : "SUSPECT");
      }

      if (getenv("INFINITE_PALETTETEST") != nullptr && frameId == 6)
      {
         auto* palette = static_cast<PaletteNode*>(gNodes[1].node.get());
         auto* twin = static_cast<PaletteNode*>(gNodes[3].node.get());
         auto* target = static_cast<RampNode*>(gNodes[2].node.get());

         float sw[3][3];
         for (int i = 0; i < 3; i++)
            palette->GetSwatch(i, sw[i]);
         printf("swatches: ");
         for (int i = 0; i < 3; i++)
            printf("(%.2f %.2f %.2f w=%.2f) ", sw[i][0], sw[i][1], sw[i][2],
                   palette->SwatchWeight(i));
         printf("\n");

         // 1. three genuinely different colours came out, not three shades of one
         float closest = 9.0f;
         for (int i = 0; i < 3; i++)
            for (int j = i + 1; j < 3; j++)
            {
               float d = 0.0f;
               for (int c = 0; c < 3; c++)
                  d += (sw[i][c] - sw[j][c]) * (sw[i][c] - sw[j][c]);
               closest = std::min(closest, std::sqrt(d));
            }
         const bool distinct = closest > 0.15f;

         // 2. the default order really is dark to light
         auto luma = [](const float* c) { return 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2]; };
         const bool ordered = luma(sw[0]) <= luma(sw[1]) + 1e-4f &&
                              luma(sw[1]) <= luma(sw[2]) + 1e-4f;

         // 3. same reference and seed give the same palette, or every binding
         //    downstream would drift on its own
         float deterministic = 0.0f;
         for (int i = 0; i < 3; i++)
         {
            float other[3];
            twin->GetSwatch(i, other);
            for (int c = 0; c < 3; c++)
               deterministic = std::max(deterministic, std::fabs(other[c] - sw[i][c]));
         }

         // 4. the extracted colours actually reach a bound swatch
         PaletteBinding& binding = PaletteBinding::Instance();
         for (int i = 0; i < 3; i++)
            binding.Bind(gNodes[2].index, i, gNodes[1].index, i);
         gPaletteTestPending = true;

         printf("distinct=%d (closest %.3f) ordered=%d deterministic drift=%.4f\n",
                distinct, closest, ordered, deterministic);
         gPaletteTestOk = distinct && ordered && deterministic < 1e-5f;
         (void)target;
      }

      if (getenv("INFINITE_PALETTETEST") != nullptr && frameId == 9)
      {
         auto* palette = static_cast<PaletteNode*>(gNodes[1].node.get());
         auto* target = static_cast<RampNode*>(gNodes[2].node.get());

         bool applied = true;
         for (int i = 0; i < 3; i++)
         {
            float expected[3];
            palette->GetSwatch(i, expected);
            for (int c = 0; c < 3; c++)
               if (std::fabs(target->stopColor[i][c] - expected[c]) > 1e-4f)
                  applied = false;
         }
         printf("bound stops: (%.2f %.2f %.2f) (%.2f %.2f %.2f) (%.2f %.2f %.2f)  applied=%d\n",
                target->stopColor[0][0], target->stopColor[0][1], target->stopColor[0][2],
                target->stopColor[1][0], target->stopColor[1][1], target->stopColor[1][2],
                target->stopColor[2][0], target->stopColor[2][1], target->stopColor[2][2],
                applied);

         // 5. shaping re-grades without re-clustering: saturation 0 must leave
         //    swatches neutral but still ordered by lightness
         palette->saturation = 0.0f;
         palette->CookIfNeeded(frameId + 500);
         float maxChroma = 0.0f;
         for (int i = 0; i < 3; i++)
         {
            float c[3];
            palette->GetSwatch(i, c);
            maxChroma = std::max(maxChroma,
                                 std::max(c[0], std::max(c[1], c[2])) -
                                    std::min(c[0], std::min(c[1], c[2])));
         }
         palette->saturation = 1.0f;
         printf("saturation 0 -> max channel spread %.3f\n", maxChroma);

         // 6. the binding survives a patch round trip
         Patch::Data data = BuildPatchData();
         const bool saved = data.palette.size() == 3;
         PaletteBinding::Instance().UnbindAllFor(gNodes[2].index);
         const bool cleared = PaletteBinding::Instance().Links().empty();
         for (const Patch::PaletteRecord& r : data.palette)
            PaletteBinding::Instance().Bind(r.dstIndex, r.dstColor, r.srcIndex, r.srcSwatch);
         const bool restored = PaletteBinding::Instance().Links().size() == 3;
         printf("bindings: saved=%d cleared=%d restored=%d\n", saved, cleared, restored);

         // 7. the headline path: a photo loaded from disk, not a cabled node.
         //    Two known colours in, the same two colours out.
         const char* shotDir = getenv("TMPDIR");
         const std::string refPath =
            std::string(shotDir ? shotDir : "/tmp") + "/infinite_palette_ref.png";
         const int side = 64;
         std::vector<unsigned char> ref((size_t)side * side * 4, 255);
         for (int y = 0; y < side; y++)
            for (int x = 0; x < side; x++)
            {
               const size_t i = ((size_t)y * side + x) * 4;
               const bool left = x < side / 2;
               ref[i + 0] = left ? 230 : 30;
               ref[i + 1] = left ? 40 : 90;
               ref[i + 2] = left ? 60 : 210;
            }
         stbi_flip_vertically_on_write(0);
         stbi_write_png(refPath.c_str(), side, side, 4, ref.data(), side * 4);

         auto* fromFile = static_cast<PaletteNode*>(gNodes[3].node.get());
         gNodes[3].node->bypassed = false;
         fromFile->Input().Disconnect();
         fromFile->swatchCount = 2;
         const bool loaded = fromFile->Load(refPath);
         fromFile->CookIfNeeded(frameId + 900);
         float a[3], b[3];
         fromFile->GetSwatch(0, a);
         fromFile->GetSwatch(1, b);
         // Sorted dark-to-light, so the blue half comes first.
         const bool fileOk = loaded && b[0] > 0.7f && b[2] < 0.4f &&
                             a[2] > 0.6f && a[0] < 0.4f;
         printf("from file: loaded=%d (%.2f %.2f %.2f) (%.2f %.2f %.2f) %s\n",
                loaded, a[0], a[1], a[2], b[0], b[1], b[2], fileOk ? "OK" : "SUSPECT");

         const bool ok = gPaletteTestOk && applied && maxChroma < 0.02f &&
                         saved && cleared && restored && fileOk;
         printf("%s\n", ok ? "PALETTE OK" : "SUSPECT");
      }

      if (getenv("INFINITE_TOPPARITYTEST") != nullptr && frameId == 4)
      {
         std::vector<std::string> failed;
         auto check = [&failed](bool ok, const std::string& what)
         {
            printf("  %-44s %s\n", what.c_str(), ok ? "ok" : "FAIL");
            if (!ok)
               failed.push_back(what);
         };
         // Each read cooks with a fresh frame id, so a param changed between
         // reads is always rendered.
         static int sCookId = 1000000;
         auto px = [](int i, float u, float v) -> std::array<float, 4>
         {
            INode* n = gNodes[i].node.get();
            n->CookIfNeeded(++sCookId);
            static unsigned int scratch = 0;
            std::vector<float> rgba;
            const int w = n->GetOutputWidth(), h = n->GetOutputHeight();
            if (w <= 0 || h <= 0 || !GLUtil::ReadTexturePixels(scratch, n->GetOutputTexture(), w, h, rgba))
               return { -1.0f, -1.0f, -1.0f, -1.0f };
            const int x = std::clamp((int)(u * w), 0, w - 1), y = std::clamp((int)(v * h), 0, h - 1);
            const size_t k = ((size_t)y * w + x) * 4;
            return { rgba[k], rgba[k + 1], rgba[k + 2], rgba[k + 3] };
         };
         auto near = [](float a, float b) { return std::fabs(a - b) < 0.03f; };
         auto size = [](int i) { return std::to_string(gNodes[i].node->GetOutputWidth()) + "x" +
                                        std::to_string(gNodes[i].node->GetOutputHeight()); };

         printf("TOP parity:\n");
         px(1, 0.5f, 0.5f);
         check(size(1) == "100x80", "crop Cut: 200x100 -> " + size(1));
         const auto cropCorner = px(1, 0.0f, 0.0f);
         check(near(cropCorner[0], 0.25f) && near(cropCorner[1], 0.1f), "crop Cut keeps the kept region's pixels");

         const auto zeroEdge = px(2, 0.05f, 0.5f);
         check(near(zeroEdge[3], 0.0f), "transform extend default (zero) leaves the edge empty");
         auto* xf = static_cast<FilterNode*>(gNodes[2].node.get());
         xf->common.extend = 1 + GLUtil::kExtendRepeat;
         const auto repeatEdge = px(2, 0.05f, 0.5f);
         check(near(repeatEdge[3], 1.0f) && near(repeatEdge[0], 0.55f), "transform extend repeat wraps the image");
         xf->common.extend = 1 + GLUtil::kExtendMirror;
         const auto mirrorEdge = px(2, 0.05f, 0.5f);
         check(near(mirrorEdge[3], 1.0f) && near(mirrorEdge[0], 0.45f), "transform extend mirror reflects the image");
         xf->common.passes = 2;
         px(2, 0.5f, 0.5f);
         check(size(2) == "200x100", "transform 2 passes keeps its size");

         const auto inv = px(3, 0.25f, 0.75f);
         check(size(3) == "100x50", "common resolution Half: " + size(3));
         check(gNodes[3].node->GetOutputFormat() == GL_RGBA16F, "common pixel format 16-bit float");
         check(near(inv[0], 0.75f) && near(inv[1], 0.75f), "channel mask: R inverted, G untouched");

         const auto blendBar = px(6, 0.05f, 0.5f), blendMid = px(6, 0.5f, 0.5f);
         check(size(6) == "200x100" && near(blendBar[3], 0.0f) && near(blendMid[3], 1.0f),
               "blend pre-fit Fit centres B, bars stay empty");

         const auto swMid = px(7, 0.5f, 0.5f);
         check(near(swMid[0], 0.5f), "switcher index 0.5 blends A and B");

         const auto fIn = px(8, 0.5f, 0.5f);
         check(size(8) == "100x100" && near(fIn[0], 1.0f), "formula reads uIn0 at in0's size");

         const auto recv = px(11, 0.5f, 0.5f);
         auto* recvImg = static_cast<ReceiveImageNode*>(gNodes[11].node.get());
         check(recvImg->SenderCount() == 2 && size(11) == "100x100" && near(recv[0], 1.0f),
               "receive image (first of 2 sends)");
         recvImg->mode = ReceiveImageNode::kIndex;
         recvImg->index = 1;
         const auto recv2 = px(11, 0.25f, 0.5f);
         check(size(11) == "200x100" && near(recv2[0], 0.25f), "receive image index 1");

         auto* recvCv = static_cast<ReceiveCVNode*>(gNodes[14].node.get());
         check(near(recvCv->Value01(), 0.3f), "receive CV = 0.3 (modulator pin restored by load path)");

         auto* recvGeo = static_cast<ReceiveGeometryNode*>(gNodes[17].node.get());
         auto* geo = dynamic_cast<IGeometrySource*>(gNodes[15].node.get());
         recvGeo->CookIfNeeded(++sCookId);
         check(!recvGeo->GetMesh().indices.empty() && recvGeo->MeshRevision() == geo->MeshRevision(),
               "receive geometry forwards mesh and stamp");
         check(static_cast<ReceiveGeometryNode*>(gNodes[20].node.get())->Source() == nullptr,
               "wireless geometry loop is refused, no hang");

         // Every FilterDef still compiles and renders with the new preamble
         // (srcExt, uExtend) and the channel-mask wrapper around its main().
         {
            int rendered = 0;
            std::string broken;
            for (const FilterDef& def : GetFilterDefs())
            {
               std::unique_ptr<FilterNode> f(static_cast<FilterNode*>(FilterNode::CreateFor(def)));
               f->Input().Connect(gNodes[0].node.get());
               if (def.inputs > 1)
                  f->Input2().Connect(gNodes[4].node.get());
               f->common.chanB = false; // exercise the mask path too
               f->CookIfNeeded(++sCookId);
               if (f->GetOutputTexture() != 0)
                  rendered++;
               else
                  broken += " " + def.name;
            }
            check(broken.empty(), "all " + std::to_string(rendered) + " filters compile and render" + broken);
         }

         AudioCable* hiddenAudio = gNodes[23].node->WirelessAudioInput(0);
         check(hiddenAudio != nullptr && hiddenAudio->GetSource() == gNodes[21].node.get(),
               "receive audio is fed by the send's source");
         NoteCable* hiddenNotes = gNodes[26].node->WirelessNoteInput(0);
         check(hiddenNotes != nullptr && hiddenNotes->GetSource() == gNodes[24].node.get(),
               "receive notes is fed by the send's source");

         if (failed.empty())
            printf("TOP PARITY OK\n");
         else
            printf("TOP PARITY FAIL (%zu)\n", failed.size());
      }

      if (getenv("INFINITE_UTILTEST") != nullptr && (frameId == 4 || frameId == 10))
      {
         auto* geo = static_cast<GeometryNode*>(gNodes[0].node.get());
         auto* null3d = static_cast<Null3DNode*>(gNodes[1].node.get());
         auto* m2p = static_cast<MeshToPointsNode*>(gNodes[2].node.get());
         auto* render = static_cast<Render3DNode*>(gNodes[3].node.get());
         auto* null2d = static_cast<NullNode*>(gNodes[5].node.get());
         auto* shape = gNodes[4].node.get();
         auto* out = static_cast<OutputNode*>(gNodes[6].node.get());

         if (frameId == 4)
         {
            // Null 3D must forward, not copy: same mesh, same stamp. A stamp of
            // its own would make Render 3D re-upload every single frame.
            const bool sameMesh = &null3d->GetMesh() == &geo->GetMesh();
            const bool sameStamp = null3d->MeshRevision() == geo->MeshRevision();
            printf("null 3D forwards: mesh=%d stamp=%d (%llu)\n",
                   sameMesh, sameStamp, null3d->MeshRevision());

            // Null 2D must report the upstream texture as its own, with no
            // framebuffer of its own in between.
            const bool sameTexture = null2d->GetOutputTexture() == shape->GetOutputTexture();
            const bool sameSize = null2d->GetOutputWidth() == shape->GetOutputWidth();
            const bool outputFed = out->GetOutputWidth() > 0;
            printf("null 2D passes: tex=%d size=%d, output %dx%d\n",
                   sameTexture, sameSize, out->GetOutputWidth(), out->GetOutputHeight());

            printf("mesh to points: %zu points -> %zu triangles\n",
                   m2p->PointCount(), m2p->TriangleCount());
            const bool sampled = m2p->PointCount() > 10 && m2p->TriangleCount() > 10;

            printf("%s\n", (sameMesh && sameStamp && sameTexture && sameSize &&
                            outputFed && sampled) ? "NULL + MESH TO POINTS OK" : "SUSPECT");
         }
         else
         {
            // Nothing animates here, so a settled frame must upload nothing -
            // proving the pass-through did not defeat the cache.
            printf("steady frame: %zu tris, %zu uploads  %s\n",
                   render->LastTriangleCount(), render->LastUploads(),
                   render->LastUploads() == 0 ? "PASS-THROUGH CACHE OK"
                                              : "SUSPECT - re-uploading through Null");
         }
      }

      if (getenv("INFINITE_TEXT3DTEST") != nullptr && frameId == 4)
      {
         auto* t = static_cast<Text3DNode*>(gNodes[0].node.get());
         auto* render = static_cast<Render3DNode*>(gNodes[1].node.get());

         // A square ring: one anticlockwise outline, one clockwise hole. If the
         // hole is filled in rather than cut, the area comes out as the full
         // square instead of the ring, so area is the thing worth measuring.
         {
            std::vector<MeshOps::Contour2D> c(2);
            c[0].points = { -1,-1,  1,-1,  1,1,  -1,1 };          // CCW outline
            c[1].points = { -0.5f,0.5f, 0.5f,0.5f, 0.5f,-0.5f, -0.5f,-0.5f }; // CW hole
            const Mesh ring = MeshOps::ExtrudeContours(c, 0.2f, 0.0f);

            // Front-facing area only, so the back face and walls do not count.
            double area = 0.0;
            for (size_t i = 0; i + 2 < ring.indices.size(); i += 3)
            {
               const Vertex& a = ring.vertices[ring.indices[i]];
               const Vertex& b = ring.vertices[ring.indices[i + 1]];
               const Vertex& v = ring.vertices[ring.indices[i + 2]];
               if (a.pz < 0.0f || b.pz < 0.0f || v.pz < 0.0f)
                  continue;
               area += std::fabs((b.px - a.px) * (v.py - a.py) -
                                 (v.px - a.px) * (b.py - a.py)) * 0.5;
            }
            // Outer 2x2 = 4, hole 1x1 = 1, so a correctly cut ring is 3.
            printf("ring: %zu tris, front area %.3f (expect 3.0, filled would be 4.0)  %s\n",
                   ring.indices.size() / 3, area,
                   std::fabs(area - 3.0) < 0.05 ? "HOLE CUT OK" : "SUSPECT");
         }

         // Then real glyphs. 'o' has a counter, so it must produce more than one
         // contour; a string of them must stay finite and bounded.
         const char* samples[] = { "o", "Infinite", "AWAY" };
         bool all = true;
         for (const char* sample : samples)
         {
            t->text = sample;
            const Mesh& mesh = t->GetMesh();
            bool finite = true;
            float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
            for (const Vertex& v : mesh.vertices)
            {
               const float p[3] = { v.px, v.py, v.pz };
               for (int k = 0; k < 3; k++)
               {
                  if (!std::isfinite(p[k])) { finite = false; continue; }
                  lo[k] = std::min(lo[k], p[k]); hi[k] = std::max(hi[k], p[k]);
               }
            }
            const bool ok = finite && mesh.indices.size() >= 3 && (hi[0] - lo[0]) > 0.05f;
            all &= ok;
            printf("  %-10s %6zu tris  width %.2f height %.2f  %s\n", sample,
                   mesh.indices.size() / 3, hi[0] - lo[0], hi[1] - lo[1], ok ? "OK" : "FAIL");
            printf("     %s\n", t->Status().c_str());
         }

         t->text = "Infinite";
         render->CookIfNeeded(frameId);
         printf("rendered %zu tris\n", render->LastTriangleCount());
         printf("%s\n", (all && render->LastTriangleCount() > 0) ? "TEXT 3D OK" : "SUSPECT");
      }

      if (getenv("INFINITE_MODELTEST") != nullptr && frameId == 4)
      {
         auto* model = static_cast<ModelSourceNode*>(gNodes[0].node.get());
         auto* render = static_cast<Render3DNode*>(gNodes[1].node.get());
         const char* files = getenv("INFINITE_MODELTEST");

         bool all = true;
         std::string list(files);
         size_t start = 0;
         while (start < list.size())
         {
            const size_t comma = list.find(',', start);
            const std::string path = list.substr(start, comma == std::string::npos
                                                          ? std::string::npos : comma - start);
            start = (comma == std::string::npos) ? list.size() : comma + 1;
            if (path.empty() || path == "1")
               continue;

            const bool loaded = model->Load(path);
            const Mesh& mesh = model->GetMesh();
            float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
            bool finite = true;
            for (const Vertex& v : mesh.vertices)
            {
               const float p[3] = { v.px, v.py, v.pz };
               for (int k = 0; k < 3; k++)
               {
                  if (!std::isfinite(p[k])) { finite = false; continue; }
                  lo[k] = std::min(lo[k], p[k]); hi[k] = std::max(hi[k], p[k]);
               }
            }
            const float extent = mesh.vertices.empty() ? 0.0f
               : std::max(hi[0]-lo[0], std::max(hi[1]-lo[1], hi[2]-lo[2]));
            // Normalisation should land every model on a unit box no matter
            // what scale it was authored at.
            const bool sane = loaded && finite && mesh.indices.size() >= 3 &&
                              std::fabs(extent - 1.0f) < 0.01f;
            printf("  %-28s %5zu tris  extent %.3f  %s\n",
                   path.c_str(), mesh.indices.size() / 3, extent, sane ? "OK" : "FAIL");
            printf("     status: %s\n", model->Status().c_str());
            all &= sane;
         }

         // And it must actually rasterise through the normal render path.
         render->CookIfNeeded(frameId);
         printf("rendered %zu tris in %zu draw calls\n",
                render->LastTriangleCount(), render->LastDrawCalls());
         printf("%s\n", (all && render->LastTriangleCount() > 0) ? "MODEL LOADING OK" : "SUSPECT");

         // A file that is not a model at all must fail cleanly, not crash.
         const bool rejected = !model->Load("/etc/hosts");
         printf("bad file rejected: %d (%s)\n", rejected, model->Status().c_str());
      }

      // Exercises every mesh operator directly, checking each produces a
      // non-degenerate mesh with finite coordinates and a sane bounding box.
      // A silently empty or NaN-riddled result is the failure mode that matters
      // here: the renderer draws nothing and says nothing.
      if (getenv("INFINITE_MESHOPTEST") != nullptr && frameId == 3)
      {
         auto check = [](const char* name, const Mesh& m, size_t minTris) {
            bool finite = true;
            float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
            for (const Vertex& v : m.vertices)
            {
               const float p[3] = { v.px, v.py, v.pz };
               for (int k = 0; k < 3; k++)
               {
                  if (!std::isfinite(p[k])) { finite = false; continue; }
                  lo[k] = std::min(lo[k], p[k]);
                  hi[k] = std::max(hi[k], p[k]);
               }
            }
            const size_t tris = m.indices.size() / 3;
            const float extent = (m.vertices.empty() || !finite)
                                    ? 0.0f
                                    : std::max(hi[0]-lo[0], std::max(hi[1]-lo[1], hi[2]-lo[2]));
            const bool ok = tris >= minTris && finite && extent > 0.001f && extent < 1000.0f;
            printf("  %-12s %7zu tris  extent %6.2f  %s\n", name, tris, extent,
                   ok ? "OK" : (!finite ? "FAIL non-finite" : "FAIL"));
            return ok;
         };

         printf("mesh operators:\n");
         const Mesh sphere = Primitives::Sphere(24, 32);
         const Mesh plane = Primitives::Plane(4);
         bool all = true;
         all &= check("source", sphere, 100);
         all &= check("subdivide", MeshOps::Subdivide(sphere, 1, 1.0f), 4 * (sphere.indices.size() / 3));
         all &= check("smooth", MeshOps::Smooth(sphere, 5, 0.8f), sphere.indices.size() / 3);
         all &= check("mirror", MeshOps::Mirror(sphere, 0, 1.0f, true, true), 2 * (sphere.indices.size() / 3));
         all &= check("screw", MeshOps::Screw(plane, 32, 1.0f, 0.4f, 0.6f, 1), 32);

         // Displace with a synthetic checkerboard buffer, standing in for a
         // readback of a Noise/Voronoi texture - exercises the bilinear
         // sampler and both modes without needing a live GL context here.
         {
            const int texW = 16, texH = 16;
            std::vector<float> tex((size_t)texW * texH * 4);
            for (int y = 0; y < texH; y++)
               for (int x = 0; x < texW; x++)
               {
                  const float v = ((x / 4 + y / 4) % 2 == 0) ? 1.0f : 0.0f;
                  const size_t i = ((size_t)y * texW + x) * 4;
                  tex[i + 0] = v; tex[i + 1] = 1.0f - v; tex[i + 2] = v * 0.5f; tex[i + 3] = 1.0f;
               }
            all &= check("displace scalar",
                         MeshOps::Displace(sphere, tex, texW, texH, 0, 0.3f, 0.5f, false, false),
                         sphere.indices.size() / 3);
            all &= check("displace vector",
                         MeshOps::Displace(sphere, tex, texW, texH, 1, 0.3f, 0.5f, false, false),
                         sphere.indices.size() / 3);

            // A hard-shaded primitive like Cube duplicates each corner once
            // per adjoining face - same position, different normal/UV - so
            // this checks that displacing it does not tear those duplicates
            // apart: every vertex that started at a shared position must
            // still be within epsilon of the others in its group afterward.
            const Mesh cube = Primitives::Cube(1);
            const Mesh displacedCube = MeshOps::Displace(cube, tex, texW, texH, 0, 0.8f, 0.5f, false, false);
            const std::vector<unsigned int> weld = MeshOps::BuildWeldMap(cube);
            std::map<unsigned int, std::array<float, 3>> first;
            float maxGap = 0.0f;
            for (size_t i = 0; i < displacedCube.vertices.size(); i++)
            {
               const unsigned int w = weld[i];
               const Vertex& v = displacedCube.vertices[i];
               auto it = first.find(w);
               if (it == first.end())
                  first[w] = { v.px, v.py, v.pz };
               else
               {
                  const float dx = v.px - it->second[0], dy = v.py - it->second[1], dz = v.pz - it->second[2];
                  maxGap = std::max(maxGap, std::sqrt(dx * dx + dy * dy + dz * dz));
               }
            }
            const bool seamsClosed = maxGap < 0.001f;
            printf("  %-12s max seam gap %.5f  %s\n", "displace cube", maxGap,
                   seamsClosed ? "OK" : "FAIL cracked at seams");
            all &= seamsClosed;
         }
         printf("%s\n", all ? "MESH OPERATORS OK" : "SUSPECT");

         // Taubin's whole point: repeated smoothing must not collapse the mesh.
         const Mesh heavy = MeshOps::Smooth(sphere, 20, 1.0f);
         float r0 = 0.0f, r1 = 0.0f;
         for (const Vertex& v : sphere.vertices)
            r0 = std::max(r0, std::sqrt(v.px*v.px + v.py*v.py + v.pz*v.pz));
         for (const Vertex& v : heavy.vertices)
            r1 = std::max(r1, std::sqrt(v.px*v.px + v.py*v.py + v.pz*v.pz));
         printf("smoothing 20x: radius %.4f -> %.4f (%.1f%% retained)\n", r0, r1, 100.0f * r1 / r0);
         printf("%s\n", (r1 > r0 * 0.9f) ? "TAUBIN NO-SHRINK OK"
                                         : "SUSPECT - smoothing is collapsing the mesh");

         // ToPoints weld/dissolve: pin detail=1 so unwelded duplicate corners
         // (24 verts / 30 tri-edges for Cube(1)) don't get mistaken for the
         // welded topology (8 verts / 12 real edges / 12 faces).
         {
            const Mesh cube1 = Primitives::Cube(1);
            const size_t unweldedVerts = MeshOps::ToPoints(cube1, 0, 100000, false).size();
            const size_t weldedVerts = MeshOps::ToPoints(cube1, 0, 100000, true).size();
            const size_t unweldedEdges = MeshOps::ToPoints(cube1, 1, 100000, false, 0.0f).size();
            const size_t weldedEdgesNoFilter = MeshOps::ToPoints(cube1, 1, 100000, true, 0.0f).size();
            const size_t weldedEdgesFiltered = MeshOps::ToPoints(cube1, 1, 100000, true, 1.0f).size();
            const size_t faceCentres = MeshOps::ToPoints(cube1, 2, 100000, true).size();
            const bool toPointsOk = unweldedVerts == 24 && weldedVerts == 8 &&
                                     unweldedEdges == 30 && weldedEdgesNoFilter == 18 &&
                                     weldedEdgesFiltered == 12 && faceCentres == 12;
            printf("to points (Cube(1)): verts %zu->%zu, edges %zu->%zu->%zu, faces %zu  %s\n",
                   unweldedVerts, weldedVerts, unweldedEdges, weldedEdgesNoFilter, weldedEdgesFiltered,
                   faceCentres, toPointsOk ? "OK" : "FAIL");
         }
      }

      // Frame limiter check: run uncapped, then at 30fps, and compare. Vsync is
      // off for this so the display refresh is not what is being measured.
      if (getenv("INFINITE_FPSTEST") != nullptr)
      {
         static double sUncapped = 0.0;
         if (frameId == 2) { gVsync = false; glfwSwapInterval(0); }
         if (frameId == 30) { sUncapped = gLastFrameMs; gTargetFps = 30; }
         if (frameId == 60)
         {
            printf("uncapped %.2f ms/frame -> capped %.2f ms/frame (target 30fps = 33.3 ms)\n",
                   sUncapped, gLastFrameMs);
            printf("%s\n", (gLastFrameMs > 30.0 && gLastFrameMs < 37.0)
                              ? "FRAME LIMITER OK" : "SUSPECT - limiter missed its budget");
         }
      }

      // P0 audio feasibility spike: does a live AVAudioSourceNode synthesis
      // callback coexist with the render loop without an FPS hit or glitches?
      // The heavy visual patch is spawned unconditionally above (before the
      // loop starts) so this measurement has real GPU load from frame 0.
      if (getenv("INFINITE_AUDIOSPIKE") != nullptr)
      {
         static bool sVsyncOff = false;
         static bool sStarted = false;
         static bool sFailed = false;
         static double sStartWallTime = 0.0;
         static double sBeforeSum = 0.0;
         static int sBeforeCount = 0;
         static double sAfterSum = 0.0;
         static int sAfterCount = 0;

         if (!sVsyncOff && frameId == 2)
         {
            gVsync = false;
            glfwSwapInterval(0);
            sVsyncOff = true;
         }

         if (!sStarted && !sFailed && frameId == 120)
         {
            std::string err;
            if (Platform::AudioSpikeStart(err))
            {
               sStarted = true;
               sStartWallTime = glfwGetTime();
               printf("AUDIOSPIKE: engine started at frame %d\n", frameId);
            }
            else
            {
               sFailed = true;
               printf("AUDIOSPIKE: engine failed to start: %s\n", err.c_str());
               printf("AUDIOSPIKE FAIL\n");
               glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
         }

         if (frameId > 2 && gLastFrameMs > 0.0)
         {
            if (!sStarted)
            {
               sBeforeSum += gLastFrameMs;
               sBeforeCount++;
            }
            else
            {
               sAfterSum += gLastFrameMs;
               sAfterCount++;
            }
         }

         if (sStarted && !sFailed && (glfwGetTime() - sStartWallTime) >= 65.0)
         {
            const Platform::AudioSpikeStats stats = Platform::AudioSpikeGetStats();
            Platform::AudioSpikeStop();

            const double beforeAvg = sBeforeCount > 0 ? sBeforeSum / sBeforeCount : 0.0;
            const double afterAvg = sAfterCount > 0 ? sAfterSum / sAfterCount : 0.0;
            const double deltaMs = afterAvg - beforeAvg;
            const double deltaPct = beforeAvg > 0.0 ? (deltaMs / beforeAvg) * 100.0 : 0.0;
            // Half a block period of jitter is a dropped-callback-adjacent
            // wobble, not a glitch; a full block period or more means the
            // audio thread missed its deadline outright.
            const double blockPeriodMs = stats.sampleRate > 0.0
                                             ? 1000.0 * (double)stats.blockSize / stats.sampleRate
                                             : 0.0;

            printf("AUDIOSPIKE stats: sampleRate=%.1f Hz  blockSize=%d  maxJitterMs=%.3f  callbacks=%llu\n",
                   stats.sampleRate, stats.blockSize, stats.maxJitterMs,
                   (unsigned long long)stats.callbackCount);
            printf("AUDIOSPIKE fps: before=%.3f ms/frame (n=%d)  after=%.3f ms/frame (n=%d)  delta=%.3f ms (%.2f%%)\n",
                   beforeAvg, sBeforeCount, afterAvg, sAfterCount, deltaMs, deltaPct);

            const bool fpsOk = std::fabs(deltaPct) < 5.0;
            const bool jitterOk = blockPeriodMs > 0.0 && stats.maxJitterMs < blockPeriodMs;
            const bool ranLongEnough = stats.callbackCount > 0 && sAfterCount > 0;
            const bool ok = fpsOk && jitterOk && ranLongEnough;
            printf("%s\n", ok ? "AUDIOSPIKE OK" : "AUDIOSPIKE SUSPECT");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      if (getenv("INFINITE_GEOTEST") != nullptr && (frameId == 4 || frameId == 10))
      {
         auto* inst = static_cast<InstanceOnPointsNode*>(gNodes[2].node.get());
         auto* op = static_cast<GeometryOpNode*>(gNodes[3].node.get());
         auto* r = static_cast<Render3DNode*>(gNodes[6].node.get());
         printf("frame %d: instances=%zu  arrayTris=%zu  rendered=%zu tris in %zu draw calls, %zu uploads, %.2f ms/frame\n",
                frameId, inst->InstanceCount(), op->TriangleCount(),
                r->LastTriangleCount(), r->LastDrawCalls(), r->LastUploads(), gLastFrameMs);
         if (frameId == 4)
            printf("%s\n", (inst->InstanceCount() > 100 && op->TriangleCount() > 100 &&
                             r->LastDrawCalls() <= 2) ? "INSTANCING + OPS OK" : "SUSPECT");
         else
            // Nothing in this fixture animates, so a steady frame must re-upload
            // nothing at all; any upload here means the mesh stamps are churning.
            printf("%s\n", r->LastUploads() == 0 ? "MESH UPLOAD CACHING OK"
                                                 : "SUSPECT - re-uploading a static mesh");
      }

      if (getenv("INFINITE_DISPLACETEST") != nullptr && frameId == 6)
      {
         auto* sub = static_cast<GeometryOpNode*>(gNodes[1].node.get());
         auto* disp = static_cast<DisplacementNode*>(gNodes[2].node.get());
         auto* r = static_cast<Render3DNode*>(gNodes[6].node.get());

         const Mesh& before = sub->GetMesh();
         const Mesh& after = disp->GetMesh();

         auto bounds = [](const Mesh& m, float lo[3], float hi[3]) {
            lo[0] = lo[1] = lo[2] = 1e30f;
            hi[0] = hi[1] = hi[2] = -1e30f;
            bool finite = true;
            for (const Vertex& v : m.vertices)
            {
               const float p[3] = { v.px, v.py, v.pz };
               for (int k = 0; k < 3; k++)
               {
                  if (!std::isfinite(p[k])) { finite = false; continue; }
                  lo[k] = std::min(lo[k], p[k]);
                  hi[k] = std::max(hi[k], p[k]);
               }
            }
            return finite;
         };

         float loB[3], hiB[3], loA[3], hiA[3];
         const bool finiteBefore = bounds(before, loB, hiB);
         const bool finiteAfter = bounds(after, loA, hiA);
         const float extentBefore = hiB[0] - loB[0];
         const float extentAfter = hiA[0] - loA[0];

         // Same triangle/vertex count - Displace moves points, it does not
         // remesh - but a different radius, since a unit sphere pushed by a
         // Noise texture must not still be a unit sphere.
         const bool sameTopology = before.indices.size() == after.indices.size() &&
                                   before.vertices.size() == after.vertices.size();
         const bool actuallyMoved = std::fabs(extentAfter - extentBefore) > 0.01f;

         r->CookIfNeeded(frameId);
         printf("before extent %.3f  after extent %.3f  tris=%zu  rendered=%zu tris\n",
                extentBefore, extentAfter, disp->TriangleCount(), r->LastTriangleCount());
         printf("%s\n", (finiteBefore && finiteAfter && sameTopology && actuallyMoved &&
                         r->LastTriangleCount() > 0)
                           ? "DISPLACEMENT OK" : "SUSPECT");
      }

      // NodeViewport (per-node mini viewport) exercised directly rather than
      // through the ImGui toggle/icon plumbing: this is the part that
      // actually uploads a mesh, renders it and draws the selection overlay,
      // so it is what is worth an automated check. gNodes[2] is the Select
      // node (has a face mask), gNodes[4] is the plain untouched cube (none).
      if (getenv("INFINITE_MINIVIEWPORTTEST") != nullptr && frameId == 10)
      {
         auto* select = dynamic_cast<IGeometrySource*>(gNodes[2].node.get());
         auto* plain = dynamic_cast<IGeometrySource*>(gNodes[4].node.get());

         static NodeViewport selectViewport;
         static NodeViewport plainViewport;
         static SharedViewportCamera selectCam;
         static SharedViewportCamera plainCam;
         const int vw = 256, vh = 256;
         const unsigned int selectTex = selectViewport.Render(select, selectCam, vw, vh, true);
         const unsigned int plainTex = plainViewport.Render(plain, plainCam, vw, vh, true);

         auto countTinted = [&](unsigned int tex) -> size_t {
            if (tex == 0)
               return 0;
            std::vector<unsigned char> px((size_t)vw * vh * 4);
            GLuint fbo = 0;
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, vw, vh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &fbo);
            size_t tinted = 0;
            for (size_t i = 0; i < px.size(); i += 4)
               if (px[i] > 90 && px[i] > px[i + 2] + 40)
                  tinted++;
            return tinted;
         };

         const size_t selectTinted = countTinted(selectTex);
         const size_t plainTinted = countTinted(plainTex);

         // A pos/rot/scale slider moves GetModelMatrix() without rebuilding
         // the mesh at all, so MeshRevision() alone would miss it - the exact
         // bug where the viewport froze on a live torus's transform sliders.
         // Confirmed here by moving the plain cube far off-frame with no
         // mesh rebuild and checking the render actually follows: mostly
         // background where it used to be mostly cube.
         auto countMesh = [&](unsigned int tex) -> size_t {
            if (tex == 0)
               return 0;
            std::vector<unsigned char> px((size_t)vw * vh * 4);
            GLuint fbo = 0;
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, vw, vh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &fbo);
            // Clear colour is (18,18,23)ish; anything meaningfully brighter is
            // the lit mesh rather than background.
            size_t mesh = 0;
            for (size_t i = 0; i < px.size(); i += 4)
               if (px[i] > 40 || px[i + 1] > 40 || px[i + 2] > 45)
                  mesh++;
            return mesh;
         };
         const size_t plainMeshBefore = countMesh(plainTex);

         auto* plainGeo = static_cast<GeometryNode*>(gNodes[4].node.get());
         plainGeo->posX += 20.0f; // far outside any reasonable auto-framing
         const unsigned int plainTexAfter = plainViewport.Render(plain, plainCam, vw, vh, false);
         const size_t plainMeshAfter = countMesh(plainTexAfter);

         const bool transformTracked = plainMeshBefore > 500 && plainMeshAfter < 50;
         const bool ok = selectTex != 0 && plainTex != 0 && selectTinted > 50 && plainTinted == 0 &&
                        transformTracked;
         printf("mini viewport: select tex=%u tinted=%zu, plain tex=%u tinted=%zu\n",
                selectTex, selectTinted, plainTex, plainTinted);
         printf("transform tracking: plain mesh px before move=%zu after move=%zu\n",
                plainMeshBefore, plainMeshAfter);
         printf("%s\n", ok ? "MINI VIEWPORT OK" : "SUSPECT");
         glfwSetWindowShouldClose(window, GLFW_TRUE);
      }

      // Points-render-as-points, phase 1 - see the INFINITE_PHASE1TEST spawn
      // block above for the scene. Checked at two points in time so the
      // Particle System check can prove the render keeps advancing rather
      // than freezing on its first frame (the exact bug a missing PointRevision()
      // in the scene cache signature would cause).
      static unsigned long long sPhase1FirstTextureRev = 0;
      static unsigned long long sPhase1FirstParticleRev = 0;
      if (getenv("INFINITE_PHASE1TEST") != nullptr && (frameId == 6 || frameId == 30))
      {
         auto* m2p = static_cast<MeshToPointsNode*>(gNodes[1].node.get());
         auto* particles = static_cast<ParticleSystemNode*>(gNodes[2].node.get());
         auto* i2p = static_cast<ImageToPointsNode*>(gNodes[4].node.get());
         auto* r = static_cast<Render3DNode*>(gNodes[5].node.get());

         const unsigned long long textureRev = r->TextureRevision();
         const unsigned long long particleRev = particles->PointRevision();

         if (frameId == 6)
         {
            sPhase1FirstTextureRev = textureRev;
            sPhase1FirstParticleRev = particleRev;

            const bool cloudsWired = r->geometry[0] == static_cast<IGeometrySource*>(m2p) &&
                                     r->geometry[1] == static_cast<IGeometrySource*>(particles) &&
                                     r->geometry[2] == static_cast<IGeometrySource*>(i2p) &&
                                     r->geometry[0]->GetPointCloud() != nullptr &&
                                     r->geometry[1]->GetPointCloud() != nullptr &&
                                     r->geometry[2]->GetPointCloud() != nullptr;
            printf("phase1 clouds wired: slot0=%d slot1(pure cloud)=%d slot2=%d  %s\n",
                   (int)(r->geometry[0]->GetPointCloud() != nullptr),
                   (int)(r->geometry[1]->GetPointCloud() != nullptr),
                   (int)(r->geometry[2]->GetPointCloud() != nullptr), cloudsWired ? "OK" : "FAIL");

            const size_t pointCount = m2p->GetPoints().size();
            printf("phase1 mesh to points sprites: %zu points, drawcalls=%zu triangles=%zu  %s\n",
                   pointCount, r->LastDrawCalls(), r->LastTriangleCount(),
                   (pointCount > 0 && r->LastDrawCalls() >= 3 && r->LastTriangleCount() > 0)
                      ? "OK" : "FAIL");

            printf("phase1 image to points: %zu points  %s\n", i2p->PointCount(),
                   i2p->PointCount() > 0 ? "OK" : "FAIL");
         }
         else // frameId == 30
         {
            const bool particleAdvanced = particleRev != sPhase1FirstParticleRev;
            const bool renderAdvanced = textureRev != sPhase1FirstTextureRev;
            printf("phase1 particle system animates through render: particleRev %llu->%llu, "
                   "renderRev %llu->%llu  %s\n",
                   sPhase1FirstParticleRev, particleRev, sPhase1FirstTextureRev, textureRev,
                   (particleAdvanced && renderAdvanced) ? "OK" : "FAIL");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      // antialiased silhouette from ordinary shading gradients; differencing
      // two renders of an identical scene can, because only the edges move.
      if (getenv("INFINITE_3DTEST") != nullptr &&
          (frameId == 4 || frameId == 8 || frameId == 12 || frameId == 16 ||
           frameId == 20 || frameId == 24 || frameId == 28))
      {
         auto* r = static_cast<Render3DNode*>(gNodes[2].node.get());
         const int w = r->GetOutputWidth(), h = r->GetOutputHeight();
         std::vector<unsigned char> px((size_t)w * h * 4);
         GLuint fbo = 0;
         glGenFramebuffers(1, &fbo);
         glBindFramebuffer(GL_FRAMEBUFFER, fbo);
         glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->GetOutputTexture(), 0);
         glPixelStorei(GL_PACK_ALIGNMENT, 1);
         glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
         glBindFramebuffer(GL_FRAMEBUFFER, 0);
         glDeleteFramebuffers(1, &fbo);

         // Count coverage rather than sampling points: a torus knot has holes,
         // and an earlier point-sample test reported "nothing drawn" for a
         // render that was in fact perfectly correct.
         size_t lit = 0;
         for (size_t i = 0; i < px.size(); i += 4)
            if (px[i] + px[i + 1] + px[i + 2] > 60)
               lit++;
         const double coverage = (double)lit / (double)(w * h);
         printf("render %dx%d samples=%d coverage=%.1f%%  %s\n", w, h, r->ActiveSamples(),
                coverage * 100.0,
                (coverage > 0.03 && coverage < 0.95) ? "GEOMETRY RASTERISED OK" : "SUSPECT");

         // The textured geometry above runs the mipmap/anisotropy path; an
         // unsupported enum or an incomplete mip chain would surface here.
         GLenum err = glGetError();
         printf("gl error after render: 0x%x  %s\n", err,
                err == GL_NO_ERROR ? "CLEAN" : "SUSPECT");

         // Mean luminance, used below to prove the exposure/tonemap uniforms
         // reach the shader rather than being silently optimised away.
         double meanLum = 0.0;
         for (size_t i = 0; i < px.size(); i += 4)
            meanLum += 0.2126 * px[i] + 0.7152 * px[i + 1] + 0.0722 * px[i + 2];
         meanLum /= (double)(w * h);

         static std::vector<unsigned char> sAliased;
         static double sMeanAtDefault = 0.0;
         if (frameId == 8)
         {
            sMeanAtDefault = meanLum;
            r->exposure = 2.5f;
            r->tonemap = 0; // none
         }
         else if (frameId == 12)
         {
            printf("mean luminance %.2f -> %.2f after exposure 1.0/ACES -> 2.5/none\n",
                   sMeanAtDefault, meanLum);
            printf("%s\n", std::fabs(meanLum - sMeanAtDefault) > 2.0
                              ? "EXPOSURE + TONEMAP UNIFORMS LIVE"
                              : "SUSPECT - shader ignored exposure/tonemap");
            // Ask for a 4000px export at 8x and check the budget clamp steps it
            // down instead of trying to allocate a gigabyte of renderbuffer.
            r->width = 4000.0f; r->height = 4000.0f;
            r->samples = 3; // 8x
         }
         else if (frameId == 16)
         {
            printf("export %dx%d requested 8x -> active %dx\n", w, h, r->ActiveSamples());
            printf("%s\n", (w == 4000 && h == 4000 && r->ActiveSamples() >= 1 &&
                            r->ActiveSamples() <= 4)
                              ? "HIGH-RES EXPORT + MSAA BUDGET OK"
                              : "SUSPECT");
            // Back to a cheap size, then sweep the PBR material knobs.
            r->width = 700.0f; r->height = 700.0f;
            r->exposure = 1.0f; r->tonemap = 1;
            auto* g = static_cast<GeometryNode*>(gNodes[0].node.get());
            g->metallic = 1.0f; g->roughness = 0.05f;
         }
         else if (frameId == 20)
         {
            sMeanAtDefault = meanLum; // polished metal
            auto* g = static_cast<GeometryNode*>(gNodes[0].node.get());
            g->metallic = 0.0f; g->roughness = 1.0f;
         }
         else if (frameId == 24)
         {
            printf("mean luminance: polished metal %.2f -> rough dielectric %.2f\n",
                   sMeanAtDefault, meanLum);
            printf("%s\n", std::fabs(meanLum - sMeanAtDefault) > 2.0
                              ? "GGX METALLIC/ROUGHNESS RESPOND"
                              : "SUSPECT - BRDF ignored metallic/roughness");
            sMeanAtDefault = meanLum;
            auto* g = static_cast<GeometryNode*>(gNodes[0].node.get());
            g->emission = 4.0f;
         }
         else if (frameId == 28)
         {
            printf("mean luminance: emission 0 %.2f -> emission 4 %.2f\n",
                   sMeanAtDefault, meanLum);
            printf("%s\n", meanLum > sMeanAtDefault + 5.0
                              ? "EMISSION OK" : "SUSPECT - emission had no effect");
         }

         if (frameId == 4)
         {
            sAliased = px;
            r->samples = 2; // index 2 == 4x
         }
         else if (frameId == 8 && sAliased.size() == px.size())
         {
            size_t changed = 0, maxDelta = 0;
            for (size_t i = 0; i < px.size(); i += 4)
            {
               const int d = std::abs((int)px[i] - (int)sAliased[i]);
               if (d > 8) changed++;
               if ((size_t)d > maxDelta) maxDelta = (size_t)d;
            }
            const double pct = 100.0 * (double)changed / (double)(w * h);
            printf("antialias diff: %zu px changed (%.2f%%), max delta %zu\n",
                   changed, pct, maxDelta);
            printf("%s\n", (r->ActiveSamples() > 1 && changed > 1000)
                              ? "MSAA OK" : "SUSPECT - multisampling had no effect");
         }
      }

      if (getenv("INFINITE_SIZETEST") != nullptr)
      {
         if (frameId >= 2 && frameId <= 14)
         {
            ImVec2 sz = ed::GetNodeSize(gNodes[0].NodeId());
            printf("f%-2d nodeSize=(%.1f, %.1f)\n", frameId, sz.x, sz.y);
         }
         if (frameId == 15)
            glfwSetWindowShouldClose(window, GLFW_TRUE);
      }

      if (getenv("INFINITE_SAMPLERDRAGTEST") != nullptr && !gNodes.empty())
      {
         // Recomputed every frame, post-ed::End() where GetNodePosition/
         // GetNodeSize have a live editor context (see the global's
         // comment) - the pre-Begin() input driver above only ever reads
         // this cached value, never calls these itself.
         const ImVec2 p = ed::GetNodePosition(gNodes[0].NodeId());
         const ImVec2 s = ed::GetNodeSize(gNodes[0].NodeId());
         // A freshly spawned node reports a sentinel/placeholder size for its
         // first frame or two, before ed:: has actually measured its content
         // - same reason SIZETEST above only trusts sizes from frameId>=2.
         // Skip those, leaving the sentinel -1 in place, rather than latch a
         // garbage target the driver would then aim the whole gesture at.
         if (s.x > 1.0f && s.x < 5000.0f && s.y > 1.0f && s.y < 5000.0f)
         {
            gSamplerDragTestTargetScreen = ed::CanvasToScreen(ImVec2(p.x + s.x * 0.5f, p.y + s.y * 0.5f));
            gSamplerDragTestTargetValid = true;
         }
      }

      if (getenv("INFINITE_PLUGINDRAGTEST") != nullptr && !gNodes.empty())
      {
         // Same recompute-every-frame rationale as the Sampler block above.
         const ImVec2 p = ed::GetNodePosition(gNodes[0].NodeId());
         const ImVec2 s = ed::GetNodeSize(gNodes[0].NodeId());
         if (s.x > 1.0f && s.x < 5000.0f && s.y > 1.0f && s.y < 5000.0f)
         {
            gPluginDragTestTargetScreen = ed::CanvasToScreen(ImVec2(p.x + s.x * 0.5f, p.y + s.y * 0.5f));
            gPluginDragTestTargetValid = true;
         }
      }

      if (getenv("INFINITE_MEDIADRAGTEST") != nullptr && !gNodes.empty())
      {
         // Same recompute-every-frame rationale as the Sampler block above.
         const ImVec2 p = ed::GetNodePosition(gNodes[0].NodeId());
         const ImVec2 s = ed::GetNodeSize(gNodes[0].NodeId());
         if (s.x > 1.0f && s.x < 5000.0f && s.y > 1.0f && s.y < 5000.0f)
         {
            gMediaDragTestTargetScreen = ed::CanvasToScreen(ImVec2(p.x + s.x * 0.5f, p.y + s.y * 0.5f));
            gMediaDragTestTargetValid = true;
         }
      }

      if (getenv("INFINITE_WTDRAGTEST") != nullptr)
      {
         // Unbuffered: this hook is watched from a pipe, and a run that hangs
         // instead of reaching its last frame would otherwise print nothing at
         // all rather than showing how far it got.
         static bool sUnbuffered = false;
         if (!sUnbuffered) { setvbuf(stdout, nullptr, _IONBF, 0); sUnbuffered = true; }
         if (getenv("INFINITE_WTDRAGTRACE") != nullptr)
            printf("WTDRAG frame %d rects=%zu screen=%zu\n", frameId, gWtTestRects.size(),
                   gWtTestScreen.size());
         gWtTestScreen.clear();
         for (const ImVec4& r : gWtTestRects)
         {
            const ImVec2 mn = ed::CanvasToScreen(ImVec2(r.x, r.y));
            const ImVec2 mx = ed::CanvasToScreen(ImVec2(r.z, r.w));
            gWtTestScreen.push_back(ImVec4(mn.x, mn.y, mx.x, mx.y));
         }
         static float sPosA = 0.0f, sPosB = 0.0f, sSusA = 0.0f, sSusB = 0.0f;
         static float sFiltA = 0.0f, sFiltB = 0.0f;
         auto* wt = static_cast<WavetableNode*>(gNodes[0].node.get());
         if (frameId == 3)
         {
            sPosA = wt->engines[0].position;
            sPosB = wt->engines[1].position;
            sSusA = wt->engines[0].ampSustain;
            sSusB = wt->engines[1].ampSustain;
            sFiltA = wt->engines[0].filterAttack;
            sFiltB = wt->engines[1].filterAttack;
            printf("WTDRAG start: rects=%zu  A.pos=%.3f B.pos=%.3f A.sus=%.3f B.sus=%.3f\n",
                   gWtTestRects.size(), sPosA, sPosB, sSusA, sSusB);
         }
         if (frameId == 10)
         {
            // The drag ended at 80% across the picture, so engine A's position
            // must land near there - and engine B's must not have moved at all.
            const float posA = wt->engines[0].position;
            const float posB = wt->engines[1].position;
            const bool ok = posA > 0.6f && std::fabs(posB - sPosB) < 1e-4f;
            printf("WTDRAG table scrub: A.pos %.3f -> %.3f, B.pos %.3f -> %.3f  %s\n", sPosA, posA,
                   sPosB, posB, ok ? "OK" : "FAIL");
            gWtDragOk &= ok;
         }
         if (frameId == 18)
         {
            const float susA = wt->engines[0].ampSustain;
            const float susB = wt->engines[1].ampSustain;
            const bool ok = susA > sSusA + 0.1f && std::fabs(susB - sSusB) < 1e-4f;
            printf("WTDRAG amp sustain: A.sus %.3f -> %.3f, B.sus %.3f -> %.3f  %s\n", sSusA, susA,
                   sSusB, susB, ok ? "OK" : "FAIL");
            gWtDragOk &= ok;
         }
         if (frameId == 26)
         {
            const float fa = wt->engines[0].filterAttack;
            const float fb = wt->engines[1].filterAttack;
            const bool ok = fb > sFiltB + 100.0f && std::fabs(fa - sFiltA) < 1e-4f;
            printf("WTDRAG b filter attack: B %.0f -> %.0f ms, A %.0f -> %.0f ms  %s\n", sFiltB, fb,
                   sFiltA, fa, ok ? "OK" : "FAIL");
            gWtDragOk &= ok;
            printf("%s\n", gWtDragOk ? "WTDRAG OK" : "WTDRAG FAIL");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      if (getenv("INFINITE_EQDRAGTEST") != nullptr)
      {
         static bool sUnbuffered = false;
         if (!sUnbuffered) { setvbuf(stdout, nullptr, _IONBF, 0); sUnbuffered = true; }

         // Unlike gWtTestRects (captured off a nested child-window widget
         // that needs the editor's pan/zoom applied explicitly), the EQ
         // curve's InvisibleButton is an ordinary node-body ImGui item -
         // imgui-node-editor already renders those through a scaled ImGui
         // context, so GetCursorScreenPos() inside DrawEqVisualizer returns
         // real, final screen pixels directly. A second ed::CanvasToScreen
         // pass on top of that double-transforms it (confirmed by comparing
         // DrawEqVisualizer's own logged origin against the "converted"
         // rect while building this fixture) - so this is a straight copy,
         // not a conversion.
         gEqTestScreen = gEqTestRect;

         static bool sEqDragOk = true;
         static float sSnap[25];
         AudioEffectNode* eq = gNodes.empty() ? nullptr : static_cast<AudioEffectNode*>(gNodes[0].node.get());
         auto snapshot = [&](float* dst) {
            int idx = 0;
            for (int b = 0; b < 5; b++)
            {
               dst[idx++] = eq->Param(kEqTypeParam[b]);
               dst[idx++] = eq->Param(kEqFreqParam[b]);
               dst[idx++] = eq->Param(kEqQParam[b]);
               dst[idx++] = eq->Param(kEqGainParam[b]);
               dst[idx++] = eq->Param(kEqOnParam[b]);
            }
         };
         // Per-param tolerance, not one blanket epsilon: type/on are
         // threshold-quantized (0/1 or an integer index) and must be exactly
         // unchanged, but freq/q/gain are read back off pixel-mapped mouse
         // coordinates through a log/pow round trip - even the intentionally-
         // held-constant axis of a drag (e.g. band3's own gain while only its
         // freq is being dragged, since UsesGain keeps rewriting gain from
         // the held Y every active frame) can drift by a fraction of a unit
         // from float/quantization noise alone, without indicating a real bug.
         auto sameExcept = [](const float* a, const float* b, int skipIdx) {
            static const float kTol[5] = { 1.0e-4f, 1.0f, 0.05f, 0.5f, 1.0e-4f }; // type,freq,q,gain,on
            for (int i = 0; i < 25; i++)
               if (i != skipIdx && std::fabs(a[i] - b[i]) > kTol[i % 5])
                  return false;
            return true;
         };
         const int band3FreqIdx = 2 * 5 + 1, band3QIdx = 2 * 5 + 2;
         const int band2OnIdx = 1 * 5 + 4;

         if (eq != nullptr && frameId == 53)
         {
            snapshot(sSnap);
            printf("EQDRAG start: rect=(%.0f,%.0f,%.0f,%.0f)\n", gEqTestScreen.x, gEqTestScreen.y,
                   gEqTestScreen.z, gEqTestScreen.w);
         }
         if (eq != nullptr && frameId == 60)
         {
            float now[25];
            snapshot(now);
            const bool ok = now[band3FreqIdx] > sSnap[band3FreqIdx] + 5.0f && sameExcept(sSnap, now, band3FreqIdx);
            printf("EQDRAG band3 dot drag: freq %.1f -> %.1f  %s\n", sSnap[band3FreqIdx], now[band3FreqIdx],
                   ok ? "OK" : "FAIL");
            sEqDragOk &= ok;
            std::copy(now, now + 25, sSnap);
         }
         if (eq != nullptr && frameId == 68)
         {
            float now[25];
            snapshot(now);
            const bool ok = now[band3QIdx] > sSnap[band3QIdx] + 0.1f && sameExcept(sSnap, now, band3QIdx);
            printf("EQDRAG band3 diamond drag: Q %.2f -> %.2f  %s\n", sSnap[band3QIdx], now[band3QIdx],
                   ok ? "OK" : "FAIL");
            sEqDragOk &= ok;
            std::copy(now, now + 25, sSnap);
         }
         if (eq != nullptr && frameId == 78)
         {
            float now[25];
            snapshot(now);
            const bool ok =
               std::fabs(now[band2OnIdx] - sSnap[band2OnIdx]) > 0.5f && sameExcept(sSnap, now, band2OnIdx);
            printf("EQDRAG band2 double-click bypass: on %.0f -> %.0f  %s\n", sSnap[band2OnIdx], now[band2OnIdx],
                   ok ? "OK" : "FAIL");
            sEqDragOk &= ok;
            printf("%s\n", sEqDragOk ? "EQDRAG OK" : "EQDRAG FAIL");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      if (getenv("INFINITE_DRAGTEST") != nullptr)
      {
         if (frameId == 2)
         {
            gDragTestNodePos = ed::GetNodePosition(gNodes[0].NodeId());
            gDragTestViewAnchor = ed::CanvasToScreen(ImVec2(0.0f, 0.0f));
            printf("start   node canvas pos = (%.0f, %.0f) viewAnchor=(%.0f,%.0f)\n",
                   gDragTestNodePos.x, gDragTestNodePos.y,
                   gDragTestViewAnchor.x, gDragTestViewAnchor.y);
         }
         if (frameId == 9)
         {
            ImVec2 now = ed::GetNodePosition(gNodes[0].NodeId());
            ImVec2 anchor = ed::CanvasToScreen(ImVec2(0.0f, 0.0f));
            bool nodeStill = std::fabs(now.x - gDragTestNodePos.x) < 0.5f &&
                             std::fabs(now.y - gDragTestNodePos.y) < 0.5f;
            bool viewMoved = std::fabs(anchor.x - gDragTestViewAnchor.x) > 20.0f ||
                             std::fabs(anchor.y - gDragTestViewAnchor.y) > 20.0f;
            printf("after canvas drag: node pos=(%.0f,%.0f) viewAnchor=(%.0f,%.0f) -> node %s, view %s : %s\n",
                   now.x, now.y, anchor.x, anchor.y,
                   nodeStill ? "still" : "MOVED",
                   viewMoved ? "panned" : "STATIC",
                   (nodeStill && viewMoved) ? "PAN OK" : "BUG");
            // aim at the node's title row: below the pins, above the preview
            ImVec2 p = ed::GetNodePosition(gNodes[0].NodeId());
            gDragTestNodeScreen = ed::CanvasToScreen(ImVec2(p.x + 60.0f, p.y + 42.0f));
            gDragTestNodePos = p;
         }
         if (frameId == 20)
         {
            ImVec2 now = ed::GetNodePosition(gNodes[0].NodeId());
            printf("after node drag:   node pos = (%.0f, %.0f)  %s\n", now.x, now.y,
                   (std::fabs(now.x - gDragTestNodePos.x) > 20.0f ||
                    std::fabs(now.y - gDragTestNodePos.y) > 20.0f) ? "MOVED OK" : "DID NOT MOVE - BUG");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      if (getenv("INFINITE_INPUTTEST") != nullptr)
      {
         if (frameId == 2)
         {
            auto* sh = static_cast<ShapeNode*>(gNodes[0].node.get());
            sh->shapeType = 6; // Star, so the paste can be checked for fidelity
            sh->sides = 7;
            ed::SelectNode(gNodes[0].NodeId(), false);
            printf("frame2: selected node0, nodes=%zu\n", gNodes.size());
         }
         if (frameId == 7)
         {
            printf("after paste: nodes=%zu\n", gNodes.size());
            for (GraphNode& gn : gNodes)
            {
               if (auto* sp = dynamic_cast<ShapeNode*>(gn.node.get()))
                  printf("  Shape idx=%d shapeType=%d sides=%d\n", gn.index, sp->shapeType, sp->sides);
            }
            ed::SelectNode(gNodes.back().NodeId(), false);
         }
         if (frameId == 10)
         {
            printf("after delete: nodes=%zu\n", gNodes.size());
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }
