// Split out of main(): see docs/plans/main-split/README.md (Block C)
#include "app/frame/FrameCtx.h"

namespace app
{
void DrawDropHandling(FrameCtx& fc)
{
   auto& dropInsideArrangePanel = fc.dropInsideArrangePanel;

      if (!gDroppedFiles.empty() && !dropInsideArrangePanel)
      {
         // Everything ModelIO reads. Checked before video because "usdz" and
         // "abc" would otherwise fall through to the image branch and fail.
         static const std::vector<std::string> kAudioExt = {
            "wav", "aif", "aiff", "mp3", "m4a", "aac", "caf", "flac", "ogg"
         };
         static const std::vector<std::string> kModelExt = {
            "obj", "ply", "stl", "usd", "usda", "usdc", "usdz", "abc"
         };
         // glTF/GLB are handled by their own branch (below, checked before
         // kModelExt) rather than folded into it: on a fresh drop they
         // auto-spawn a whole Material + Image Source rig, not just a bare
         // Model 3D node - see GltfImport.h.
         static const std::vector<std::string> kGltfExt = { "gltf", "glb" };
         // Plugin bundles, not files - see the branch that consumes this.
         static const std::vector<std::string> kPluginBundleExt = { "component", "vst3" };
         // Field build step 17: portable device files. Its own extension
         // check must run before the "inf"/"infinite" branch below - "inf"
         // is a strict-suffix match (HasExtension splits on the last dot),
         // so "field" and "infdev" never collide with it, but must still get
         // their own branch since they aren't in kAudioExt/kModelExt/etc.
         static const std::vector<std::string> kFieldExt = { "field", "infdev" };
         ImVec2 canvasPos = ed::ScreenToCanvas(gDropPos);
         DrumSequencerNode* dropTargetDrum = FindNodeUnderCanvasPoint<DrumSequencerNode>(canvasPos);
         int dropTargetLane =
            dropTargetDrum != nullptr ? DrumSequencerLaneForCanvasPos(dropTargetDrum, canvasPos.x, canvasPos.y) : 0;
         static const std::vector<std::string> kMidiExt = { "mid", "midi" };
         MidiFileNode* dropTargetMidi = FindNodeUnderCanvasPoint<MidiFileNode>(canvasPos);
         SamplerNode* dropTargetSampler = FindNodeUnderCanvasPoint<SamplerNode>(canvasPos);
         MpcNode* dropTargetMpc = FindNodeUnderCanvasPoint<MpcNode>(canvasPos);
         std::vector<std::string> mpcDropPaths;
         SlicerNode* dropTargetSlicer = FindNodeUnderCanvasPoint<SlicerNode>(canvasPos);
         PaulStretchNode* dropTargetPaul = FindNodeUnderCanvasPoint<PaulStretchNode>(canvasPos);
         GranularNode* dropTargetGran = FindNodeUnderCanvasPoint<GranularNode>(canvasPos);
         MolderNode* dropTargetMolder = FindNodeUnderCanvasPoint<MolderNode>(canvasPos);
         GrainMolderNode* dropTargetGrainMolder = FindNodeUnderCanvasPoint<GrainMolderNode>(canvasPos);
         AudioFileNode* dropTargetAudioFile = FindNodeUnderCanvasPoint<AudioFileNode>(canvasPos);
         AudioPluginNode* dropTargetPlugin = FindNodeUnderCanvasPoint<AudioPluginNode>(canvasPos);
         ModelSourceNode* dropTargetModel = FindNodeUnderCanvasPoint<ModelSourceNode>(canvasPos);
         VideoSourceNode* dropTargetVideo = FindNodeUnderCanvasPoint<VideoSourceNode>(canvasPos);
         ImageSourceNode* dropTargetImage = FindNodeUnderCanvasPoint<ImageSourceNode>(canvasPos);
         FieldElementNode* dropTargetFieldElement = FindNodeUnderCanvasPoint<FieldElementNode>(canvasPos);
         FieldPrimitiveNode* dropTargetFieldPrimitive = FindNodeUnderCanvasPoint<FieldPrimitiveNode>(canvasPos);
         FieldPixelNode* dropTargetFieldPixel = FindNodeUnderCanvasPoint<FieldPixelNode>(canvasPos);
         FieldSampleNode* dropTargetFieldSample = FindNodeUnderCanvasPoint<FieldSampleNode>(canvasPos);
         FieldSynthNode* dropTargetFieldSynth = FindNodeUnderCanvasPoint<FieldSynthNode>(canvasPos);
         FieldGraphNode* dropTargetFieldGraph = FindNodeUnderCanvasPoint<FieldGraphNode>(canvasPos);
         FormulaNode* dropTargetFormula = FindNodeUnderCanvasPoint<FormulaNode>(canvasPos);
         float offset = 0.0f;
         std::vector<std::string> pendingAudioDropPaths;
         bool droppedCheckpointPushed = false;
         auto ensureDroppedCheckpoint = [&]()
         {
            if (!droppedCheckpointPushed)
            {
               PushUndoCheckpoint();
               droppedCheckpointPushed = true;
            }
         };
         for (std::string path : gDroppedFiles)
         {
            while (path.size() > 1 && (path.back() == '/' || path.back() == '\\'))
               path.pop_back();

            // Field build step 17: a dropped .field (or .infdev) hot-swaps whatever
            // matching-domain Field node sits under the drop point. Checked
            // before the "inf"/"infinite" patch-load branch below since
            // extensions are checked in order. No new node is spawned
            // when nothing matches under the cursor or the domain doesn't
            // match - same silent-no-op contract every other branch in this
            // dispatch already has for an unmatched drop.
            if (HasExtension(path, kFieldExt))
            {
               Field::DeviceFile device;
               std::string err;
               if (Field::LoadFromFieldFile(path, device, err))
               {
                   if (dropTargetFieldElement != nullptr && device.domain == "element")
                   {
                      ensureDroppedCheckpoint();
                      dropTargetFieldElement->LoadDeviceFile(device);
                      gPatchDirty = true;
                      continue;
                   }
                   if (dropTargetFieldPrimitive != nullptr && device.domain == "primitive")
                   {
                      ensureDroppedCheckpoint();
                      dropTargetFieldPrimitive->LoadDeviceFile(device);
                      gPatchDirty = true;
                      continue;
                   }
                  if (dropTargetFieldPixel != nullptr && device.domain == "pixel")
                  {
                     ensureDroppedCheckpoint();
                     dropTargetFieldPixel->LoadDeviceFile(device);
                     gPatchDirty = true;
                     continue;
                  }
                  if (dropTargetFieldSample != nullptr && device.domain == "sample")
                  {
                     ensureDroppedCheckpoint();
                     dropTargetFieldSample->LoadDeviceFile(device);
                     gPatchDirty = true;
                     continue;
                  }
                  if (dropTargetFieldSynth != nullptr && device.domain == "synth")
                  {
                     ensureDroppedCheckpoint();
                     dropTargetFieldSynth->LoadDeviceFile(device);
                     gPatchDirty = true;
                     continue;
                  }
                  if (dropTargetFieldGraph != nullptr && device.domain == "graph")
                  {
                     ensureDroppedCheckpoint();
                     dropTargetFieldGraph->LoadDeviceFile(device);
                     gPatchDirty = true;
                     continue;
                  }
                  if (dropTargetFormula != nullptr && device.domain == "formula")
                  {
                     ensureDroppedCheckpoint();
                     dropTargetFormula->LoadDeviceFile(device);
                     gPatchDirty = true;
                     continue;
                  }
               }
                  // If dropped on empty canvas, spawn a new Field node of that domain
                  GraphNode* spawned = nullptr;
                  if (device.domain == "element")
                     spawned = SpawnNode("Field Modifier", "3D", canvasPos.x + offset, canvasPos.y);
                  else if (device.domain == "pixel")
                     spawned = SpawnNode("FieldPixel", "Source", canvasPos.x + offset, canvasPos.y);
                  else if (device.domain == "sample")
                     spawned = SpawnNode("Field Effect", "AudioEffects", canvasPos.x + offset, canvasPos.y);
                  else if (device.domain == "synth")
                     spawned = SpawnNode("Field Synth", "Synths", canvasPos.x + offset, canvasPos.y);
                  else if (device.domain == "graph")
                     spawned = SpawnNode("Field Graph", "Utility", canvasPos.x + offset, canvasPos.y);
                  else if (device.domain == "primitive")
                     spawned = SpawnNode("Field Primitive", "3D", canvasPos.x + offset, canvasPos.y);
                  else if (device.domain == "formula")
                     spawned = SpawnNode("Formula", "Modulators", canvasPos.x + offset, canvasPos.y);

                  if (spawned != nullptr)
                  {
                     ensureDroppedCheckpoint();
                     if (auto* fe = dynamic_cast<FieldElementNode*>(spawned->node.get()))
                        fe->LoadDeviceFile(device);
                     else if (auto* fpn = dynamic_cast<FieldPrimitiveNode*>(spawned->node.get()))
                        fpn->LoadDeviceFile(device);
                     else if (auto* fp = dynamic_cast<FieldPixelNode*>(spawned->node.get()))
                        fp->LoadDeviceFile(device);
                     else if (auto* fs = dynamic_cast<FieldSampleNode*>(spawned->node.get()))
                        fs->LoadDeviceFile(device);
                     else if (auto* fsynth = dynamic_cast<FieldSynthNode*>(spawned->node.get()))
                        fsynth->LoadDeviceFile(device);
                     else if (auto* fg = dynamic_cast<FieldGraphNode*>(spawned->node.get()))
                        fg->LoadDeviceFile(device);
                     else if (auto* form = dynamic_cast<FormulaNode*>(spawned->node.get()))
                        form->LoadDeviceFile(device);
                     spawned->showParams = true;
                     offset += 240.0f;
                     gPatchDirty = true;
                     continue;
                  }
               // Unreadable file or unspawnable domain: fall through silently.
               continue;
            }

            if (HasExtension(path, std::vector<std::string> { "inf", "infinite" }))
            {
               GuardUnsavedChanges([path]() { LoadPatchFrom(path); });
               gRequestFitView = true;
               continue;
            }

            if (HasExtension(path, kAudioExt))
            {
               if (dropTargetDrum != nullptr)
               {
                  ensureDroppedCheckpoint();
                  dropTargetDrum->LoadFileToLane(dropTargetLane, path);
                  dropTargetLane = (dropTargetLane + 1) % DrumSequencerNode::kNumLanes;
                  gPatchDirty = true;
                  continue;
               }
               if (dropTargetMpc != nullptr)
               {
                  mpcDropPaths.push_back(path); // loaded together below, so a multi-file drop fills successive pads
                  continue;
               }
               if (dropTargetSampler != nullptr)
               {
                  ensureDroppedCheckpoint();
                  dropTargetSampler->LoadFile(path);
                  dropTargetSampler = nullptr;
                  gPatchDirty = true;
                  continue;
               }
               if (dropTargetSlicer != nullptr)
               {
                  ensureDroppedCheckpoint();
                  dropTargetSlicer->LoadFile(path);
                  dropTargetSlicer = nullptr;
                  gPatchDirty = true;
                  continue;
               }
               if (dropTargetPaul != nullptr)
               {
                  ensureDroppedCheckpoint();
                  dropTargetPaul->LoadFile(path);
                  dropTargetPaul = nullptr;
                  gPatchDirty = true;
                  continue;
               }
               if (dropTargetGran != nullptr)
               {
                  ensureDroppedCheckpoint();
                  dropTargetGran->LoadFile(path);
                  dropTargetGran = nullptr;
                  gPatchDirty = true;
                  continue;
               }
               if (dropTargetMolder != nullptr)
               {
                  ensureDroppedCheckpoint();
                  dropTargetMolder->LoadFile(path);
                  dropTargetMolder = nullptr;
                  gPatchDirty = true;
                  continue;
               }
               if (dropTargetGrainMolder != nullptr)
               {
                  ensureDroppedCheckpoint();
                  dropTargetGrainMolder->LoadFile(path);
                  dropTargetGrainMolder = nullptr;
                  gPatchDirty = true;
                  continue;
               }
               if (dropTargetAudioFile != nullptr)
               {
                  ensureDroppedCheckpoint();
                  dropTargetAudioFile->Open(path);
                  dropTargetAudioFile = nullptr;
                  gPatchDirty = true;
                  continue;
               }

               pendingAudioDropPaths.push_back(path);
               continue;
            }

            GraphNode* spawned = nullptr;
            if (HasExtension(path, kMidiExt))
            {
               if (dropTargetMidi != nullptr)
               {
                  ensureDroppedCheckpoint();
                  dropTargetMidi->path = path;
                  dropTargetMidi = nullptr;
                  gPatchDirty = true;
                  continue;
               }
               spawned = SpawnNode("MIDI File", "Notes", canvasPos.x + offset, canvasPos.y);
               if (spawned != nullptr)
                  static_cast<MidiFileNode*>(spawned->node.get())->path = path;
            }
            else if (HasExtension(path, kPluginBundleExt))
            {
               const bool isVst3 = HasExtension(path, std::vector<std::string> { "vst3" });
#if !INFINITE_ENABLE_VST3
               if (isVst3)
               {
                  printf("VST3 support is not compiled into this build (build with "
                         "-DINFINITE_ENABLE_VST3=ON): %s\n", path.c_str());
                  continue;
               }
#endif
               std::vector<Platform::PluginDesc> found;
               const bool resolved = isVst3
                  ? (Platform::DescribeVST3Bundle(path, found) && !found.empty())
                  : (Platform::DescribeAudioUnitBundle(path, found) && !found.empty());
               if (!resolved)
               {
                  printf("dropped plugin bundle could not be resolved to a plugin: %s\n",
                         path.c_str());
                  continue;
               }
               // Prefer whatever the scanner already knows about this identity:
               // its display name came from the component registry, which is
               // better than the bundle's own Info.plist string.
               Platform::PluginDesc desc = found.front();
               if (const PluginScanner::Entry* known = gPluginScanner.FindByIdentifier(desc.identifier))
               {
                  const std::string origPath = desc.path;
                  desc = *known;
                  if (desc.path.empty() && !origPath.empty())
                     desc.path = origPath;
               }

               if (dropTargetPlugin != nullptr)
               {
                  ensureDroppedCheckpoint();
                  dropTargetPlugin->LoadPlugin(desc);
                  dropTargetPlugin = nullptr;
                  gPatchDirty = true;
                  continue;
               }
               spawned = SpawnNode("Plugin", "AudioEffects", canvasPos.x + offset, canvasPos.y);
               if (spawned != nullptr)
                  static_cast<AudioPluginNode*>(spawned->node.get())->LoadPlugin(desc);
            }
            else if (HasExtension(path, kGltfExt))
            {
               if (dropTargetModel != nullptr)
               {
                  // Reload in place - do NOT auto-spawn a duplicate
                  // Material/texture rig on top of whatever is already wired.
                  ensureDroppedCheckpoint();
                  dropTargetModel->Load(path);
                  dropTargetModel = nullptr;
                  gPatchDirty = true;
                  continue;
               }

               // Fresh drop: Model 3D + Material + one Image Source per
               // present texture map, already wired - the whole point of
               // this feature is zero manual cabling. Everything from here
               // to gSuppressUndoCheckpoints=false is one undo step.
               ensureDroppedCheckpoint();
               gSuppressUndoCheckpoints = true;

               // GraphNode* from SpawnNode points into gNodes' own storage,
               // which a LATER SpawnNode call can reallocate (std::vector
               // growth) - holding modelNode/materialNode across the several
               // more SpawnNode calls below would dangle. Capture the
               // stable `index` field immediately instead and re-resolve
               // via FindNodeByIndex() each time a node is actually needed.
               int modelIndex = -1;
               {
                  GraphNode* modelNode = SpawnNode("Model 3D", "3D", canvasPos.x + offset, canvasPos.y);
                  if (modelNode != nullptr)
                  {
                     modelIndex = modelNode->index;
                     static_cast<ModelSourceNode*>(modelNode->node.get())->Load(path);
                  }
               }

               int materialIndex = -1;
               {
                  GraphNode* materialNode =
                     SpawnNode("Material", "3D", canvasPos.x + offset + 260.0f, canvasPos.y);
                  if (materialNode != nullptr)
                     materialIndex = materialNode->index;
               }

               if (modelIndex != -1 && materialIndex != -1)
               {
                  std::string wireErr;
                  ConnectNodes(modelIndex, 0, materialIndex, 0, wireErr);
               }

               std::string gltfErr;
               const GltfImport::GltfDecodePackage* pkg = GltfImport::DecodeCached(path, gltfErr);
               if (pkg != nullptr && materialIndex != -1)
               {
                  struct MapSlot
                  {
                     const GltfImport::GltfDecodedImage* img;
                     int mapIndex;
                     const char* slot;
                  };
                  const MapSlot maps[] = {
                     { &pkg->albedo, kMapAlbedo, "albedo" },
                     { &pkg->roughness, kMapRoughness, "roughness" },
                     { &pkg->metallic, kMapMetallic, "metallic" },
                     { &pkg->normalMap, kMapNormal, "normal" },
                     { &pkg->occlusion, kMapAmbientOcclusion, "ao" },
                     { &pkg->emissive, kMapEmission, "emission" },
                  };

                  const float texX = canvasPos.x + offset + 560.0f;
                  float texY = canvasPos.y;
                  for (const MapSlot& m : maps)
                  {
                     if (m.img->pixels.empty())
                        continue;

                     GraphNode* texNode = SpawnNode("Image Source", "Source", texX, texY);
                     if (texNode != nullptr)
                     {
                        auto* imgNode = static_cast<ImageSourceNode*>(texNode->node.get());
                        imgNode->LoadFromDecoded(m.img->pixels, m.img->width, m.img->height,
                                                 std::string("gltf://") + path + "#" + m.slot);
                        std::string wireErr;
                        ConnectNodes(texNode->index, 0, materialIndex, 1 + m.mapIndex, wireErr);
                     }
                     texY += 160.0f;
                  }
               }

               if (GraphNode* modelNode = (modelIndex != -1) ? FindNodeByIndex(modelIndex) : nullptr)
                  modelNode->showParams = true;
               if (GraphNode* materialNode = (materialIndex != -1) ? FindNodeByIndex(materialIndex) : nullptr)
                  materialNode->showParams = true;

               gSuppressUndoCheckpoints = false;
               gPatchDirty = true;
               offset += 240.0f;
               continue;
            }
            else if (HasExtension(path, kModelExt))
            {
               if (dropTargetModel != nullptr)
               {
                  ensureDroppedCheckpoint();
                  dropTargetModel->Load(path);
                  dropTargetModel = nullptr;
                  gPatchDirty = true;
                  continue;
               }
               spawned = SpawnNode("Model 3D", "3D", canvasPos.x + offset, canvasPos.y);
               if (spawned != nullptr)
                  static_cast<ModelSourceNode*>(spawned->node.get())->Load(path);
            }
            else if (HasExtension(path, kVideoExt))
            {
               if (dropTargetVideo != nullptr)
               {
                  ensureDroppedCheckpoint();
                  dropTargetVideo->Open(path);
                  dropTargetVideo = nullptr;
                  gPatchDirty = true;
                  continue;
               }
               spawned = SpawnNode("Video", "Source", canvasPos.x + offset, canvasPos.y);
               if (spawned != nullptr)
                  static_cast<VideoSourceNode*>(spawned->node.get())->Open(path);
            }
            else
            {
               if (dropTargetImage != nullptr)
               {
                  ensureDroppedCheckpoint();
                  dropTargetImage->Load(path);
                  dropTargetImage = nullptr;
                  gPatchDirty = true;
                  continue;
               }
               spawned = SpawnNode("Image Source", "Source", canvasPos.x + offset, canvasPos.y);
               if (spawned != nullptr)
                  static_cast<ImageSourceNode*>(spawned->node.get())->Load(path);
            }
            if (spawned != nullptr)
               spawned->showParams = true;
            offset += 240.0f;
         }
         if (dropTargetMpc != nullptr && !mpcDropPaths.empty())
         {
            ensureDroppedCheckpoint();
            MpcDropFiles(dropTargetMpc, canvasPos.x, canvasPos.y, mpcDropPaths);
            gPatchDirty = true;
         }
         if (!pendingAudioDropPaths.empty())
         {
            gAudioDropPicker.justOpened = true;
            gAudioDropPicker.canvasPos = canvasPos;
            gAudioDropPicker.screenPos = gDropPos;
            gAudioDropPicker.paths = std::move(pendingAudioDropPaths);
         }
         gDroppedFiles.clear();
      }}
}
