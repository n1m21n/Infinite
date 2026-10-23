      // Turbo 0.60: patch compatibility probe. With INFINITE_LOADPATCH=<patch>
      // and INFINITE_PATCHCOMPAT=<out.json>, keeps the transport stopped and,
      // at frame INFINITE_PATCHCOMPAT_FRAME (default 20), writes a summary of
      // every node: output size, a 4x4 RGBA thumbnail, triangle and vertex
      // counts, the first modulator value. It also lists node types the
      // build does not know and saved param keys a known type does not read,
      // so two builds (or Turbo and upstream) can be diffed on the same patch.
      // The same file is compiled into the reference build, so keep it free
      // of anything newer than 0.59.
      if (const char* compatOut = getenv("INFINITE_PATCHCOMPAT"))
      {
         // Turbo 0.62: INFINITE_PATCHCOMPAT_PLAY=1 keeps the transport running
         // and adds frame and per-node cook times (a profile of the patch).
         const bool compatPlay = getenv("INFINITE_PATCHCOMPAT_PLAY") != nullptr;
         if (!compatPlay)
            Transport::Instance().SetPlaying(false);
         // Turbo 0.70: INFINITE_PERF_UNCAPPED=1 (perf.ps1) turns VSync and the
         // frame limiter off every frame (a loaded patch or settings may turn
         // them back on), so the frame time is the patch's cost, not the display's.
         if (getenv("INFINITE_PERF_UNCAPPED") != nullptr)
         {
            gVsync = false;
            gTargetFps = 0;
         }
         static auto sCompatT0 = std::chrono::steady_clock::now();
         static double sCompatMs = 0.0;
         static int sCompatFrames = 0;
         const char* frameEnv = getenv("INFINITE_PATCHCOMPAT_FRAME");
         const int dumpFrame = frameEnv != nullptr ? atoi(frameEnv) : 20;
         {
            const auto now = std::chrono::steady_clock::now();
            if (frameId > dumpFrame / 2) // skip the loading frames
            {
               sCompatMs += std::chrono::duration<double, std::milli>(now - sCompatT0).count();
               sCompatFrames++;
            }
            sCompatT0 = now;
         }
         if (frameId == dumpFrame)
         {
            auto num = [](double v) {
               if (!std::isfinite(v))
                  return std::string("null");
               char b[32];
               snprintf(b, sizeof(b), "%.3f", v);
               return std::string(b);
            };
            auto esc = [](const std::string& s) {
               std::string o;
               for (char c : s)
               {
                  if (c == '"' || c == '\\')
                     o += '\\';
                  if ((unsigned char)c >= 0x20)
                     o += c;
               }
               return o;
            };
            std::string js = "{\n \"nodes\": [\n";
            static unsigned int sCompatFbo = 0;
            bool first = true;
            for (GraphNode& gn : gNodes)
            {
               INode* n = gn.node.get();
               std::string row = "  {\"i\": " + std::to_string(gn.index) + ", \"type\": \"" + esc(gn.typeName) + "\"";
               const int w = n->GetOutputWidth(), h = n->GetOutputHeight();
               const unsigned int tex = n->GetOutputTexture();
               if (w > 0 && h > 0 && tex != 0)
               {
                  std::vector<float> px;
                  row += ", \"w\": " + std::to_string(w) + ", \"h\": " + std::to_string(h);
                  if (GLUtil::ReadTexturePixels(sCompatFbo, tex, w, h, px) && px.size() >= (size_t)w * h * 4)
                  {
                     // Turbo 0.61: INFINITE_DUMPNODE=<index> + INFINITE_DUMPPNG=<file> saves that node's output.
                     const char* dumpNode = getenv("INFINITE_DUMPNODE");
                     const char* dumpPng = getenv("INFINITE_DUMPPNG");
                     if (dumpNode != nullptr && dumpPng != nullptr && atoi(dumpNode) == gn.index)
                     {
                        std::vector<unsigned char> rgba(px.size());
                        for (size_t k = 0; k < px.size(); k++)
                           rgba[k] = (unsigned char)std::clamp(px[k] * 255.0f + 0.5f, 0.0f, 255.0f);
                        stbi_write_png(dumpPng, w, h, 4, rgba.data(), w * 4);
                     }
                     // Turbo 0.71 (S0): INFINITE_DUMPTYPE=<type name> + INFINITE_DUMPPNG=<base>
                     // saves every node of that type as <base>_<index>.png, upright and opaque
                     // (Render 3D vs Render 3D HQ BEFORE / AFTER captures).
                     const char* dumpType = getenv("INFINITE_DUMPTYPE");
                     if (dumpType != nullptr && dumpPng != nullptr && gn.typeName == dumpType)
                     {
                        std::vector<unsigned char> rgba(px.size());
                        for (int yy = 0; yy < h; yy++)
                           for (int xx = 0; xx < w; xx++)
                              for (int c = 0; c < 4; c++)
                              {
                                 const float v = c == 3 ? 1.0f : px[((size_t)(h - 1 - yy) * w + xx) * 4 + c];
                                 rgba[((size_t)yy * w + xx) * 4 + c] = (unsigned char)std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f);
                              }
                        const std::string file = std::string(dumpPng) + "_" + std::to_string(gn.index) + ".png";
                        stbi_write_png(file.c_str(), w, h, 4, rgba.data(), w * 4);
                     }
                     row += ", \"img\": [";
                     for (int gy = 0; gy < 4; gy++)
                        for (int gx = 0; gx < 4; gx++)
                        {
                           double acc[4] = { 0, 0, 0, 0 };
                           int cnt = 0;
                           for (int y = gy * h / 4; y < (gy + 1) * h / 4; y += std::max(1, h / 64))
                              for (int x = gx * w / 4; x < (gx + 1) * w / 4; x += std::max(1, w / 64))
                              {
                                 const size_t k = ((size_t)y * w + x) * 4;
                                 for (int c = 0; c < 4; c++)
                                    acc[c] += px[k + c];
                                 cnt++;
                              }
                           for (int c = 0; c < 4; c++)
                              row += ((gy | gx | c) ? "," : "") + num(cnt ? acc[c] / cnt : 0.0);
                        }
                     row += "]";
                  }
               }
               if (auto* g = dynamic_cast<IGeometrySource*>(n))
               {
                  const Mesh& m = g->GetMesh();
                  row += ", \"tris\": " + std::to_string(m.indices.size() / 3) + ", \"verts\": " + std::to_string(m.vertices.size());
                  if (const std::vector<Particle>* cloud = g->GetPointCloud())
                     row += ", \"points\": " + std::to_string(cloud->size());
               }
               if (compatPlay)
                  if (auto* pn = dynamic_cast<PopNode*>(n))
                     row += ", \"ms\": " + num(pn->TotalCookMs() / std::max(1, frameId)) + ", \"gpu\": " +
                            num(pn->TotalGpuMs() / std::max(1, frameId));
               if (IModulator* mod = ModulatorForOutput(n, 0))
                  row += ", \"mod\": " + num(mod->Value01());
               row += "}";
               js += (first ? "" : ",\n") + row;
               first = false;
            }
            js += "\n ],\n";
            if (compatPlay && sCompatFrames > 0)
               js += " \"frameMs\": " + num(sCompatMs / sCompatFrames) + ",\n";
            // Wiring as loaded: cables drawn on the canvas and modulation bindings.
            js += " \"links\": " + std::to_string(gLinks.size()) + ", \"mods\": " +
                  std::to_string(Modulation::Instance().Links().size()) + ",\n";

            // Types and saved keys this build does not read.
            std::string unknownTypes, unknownKeys;
            Patch::Data data;
            std::string err;
            const char* patchPath = getenv("INFINITE_LOADPATCH");
            if (patchPath != nullptr && Patch::Read(patchPath, data, err))
            {
               std::set<std::string> seenTypes;
               for (const Patch::NodeRecord& rec : data.nodes)
               {
                  std::unique_ptr<INode> probe(NodeFactory::Instance().MakeNode(rec.typeName));
                  if (!probe)
                  {
                     if (seenTypes.insert(rec.typeName).second)
                        unknownTypes += std::string(unknownTypes.empty() ? "" : ", ") + "\"" + esc(rec.typeName) + "\"";
                     continue;
                  }
                  std::vector<std::pair<std::string, std::string>> known;
                  Patch::SaveParams(probe.get(), known);
                  std::set<std::string> keys;
                  for (const auto& kv : known)
                     keys.insert(kv.first);
                  for (const auto& kv : rec.params)
                     if (keys.count(kv.first) == 0)
                        unknownKeys += std::string(unknownKeys.empty() ? "" : ", ") + "\"" + esc(rec.typeName + " / " + kv.first) + "\"";
               }
            }
            js += " \"unknownTypes\": [" + unknownTypes + "],\n \"unknownKeys\": [" + unknownKeys + "]\n}\n";
            if (FILE* f = fopen(compatOut, "wb"))
            {
               fwrite(js.data(), 1, js.size(), f);
               fclose(f);
            }
            printf("PATCHCOMPAT wrote %s (%zu nodes)\n", compatOut, gNodes.size());
         }
      }
