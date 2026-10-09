# Index scan: computer vision / ML cluster

Candidates were chosen from my own knowledge plus docs/plans/index-opensource-scan.md (Tier 2 optical flow, Tier 3 ML). They were NOT cross-checked against the indexopensource.com list (the site is blocked, 403).

## Projects read (licence verified from raw LICENSE, fetched into scratchpad/cv-ml/)
| Repo | Licence verified | Branch | What I read |
|---|---|---|---|
| opencv/opencv | Apache-2.0 (LICENSE, 4.x) | 4.x | modules/video/src/dis_flow.cpp (calc, ocl_calc pyramid loop, PatchInverseSearch / Densification / variational refine structure), lkpyramid.cpp fetched (1481 lines), only skimmed |
| google-ai-edge/mediapipe | Apache-2.0 (LICENSE, master) | master | mediapipe/tasks/cc/vision/hand_landmarker/hand_landmarker_graph.cc (graph design only); docs/solutions/models.md |
| DepthAnything/Depth-Anything-V2 | Code Apache-2.0 (LICENSE). README line 180: only the Small model is Apache-2.0; Base/Large/Giant are CC-BY-NC-4.0 | main | README only (no useful source to port; the net is run via ONNX) |
| microsoft/onnxruntime | MIT (LICENSE) | main | not read, already vendored |
| facebookresearch/sam2 | Apache-2.0 (LICENSE) | main | LICENSE only, not read (see SAM note below) |
| xuebinqin/U-2-Net | Apache-2.0 (LICENSE; Infinite's assets/models/NOTICE.txt already records this) | master | LICENSE only |

Skipped: none for licence reasons. The hand-landmark .tflite weights have no per-model licence that I verified (models.md links only a model card), so treat them as unverified until the model card is fetched. SAM 2 note: the first-pass scan flags "SAM-specific" licence; only the repo LICENSE was verified, per-checkpoint terms were not.

## What Infinite does today
- ONNX Runtime is already vendored and linked: CMakeLists.txt:123-195 (Windows DirectML NuGet, Linux tgz with SHA256), link at CMakeLists.txt:946-955. macOS has no ORT; it uses Vision (Platform.mm:3357).
- Shared engine: src/platform/common/SubjectMaskOnnx.cpp. The session is a single process-wide singleton (`EnsureOrtSession`, std::call_once, lines ~35-110) with the model path baked in at first call, one input/output (index 0), U2-Net ImageNet normalisation hard-wired (`ResizeAndNormalize`), and a provider hook for DirectML (`SetProviderHook`, SubjectMaskOnnx.h:14). Only one model can be loaded. A second model (depth, hand) would need the session refactored into a per-model class.
- Model shipping: assets/models/u2netp.onnx plus assets/models/NOTICE.txt (licence/attribution record); path resolved relative to the exe (PlatformLinux.cpp:737, PlatformWin.cpp:840); copied in CI (.github/workflows/build.yml:255-259).
- Node: src/nodes/RemoveBgNode.h/.cpp: worker thread, latest-only request slot, mask paired with the exact source frame (RemoveBgNode.h:54-111). This is the template for any slow ML node.
- Motion today: ImageAnalyzeNode `kMotion` output is only a mean absolute luma difference against the previous CPU frame (AnalyzeNodes.cpp:285-450, output at :495). No dense flow, no per-pixel motion vectors. Searched src/ for optical flow, Lucas, Horn, datamosh, motion vector: only hits are a Glitch "datamosh" kind (HelpWindows.cpp:334, row shuffle, not flow based), motionblur (a straight-line blur, HelpWindows.cpp:318), and FieldPixel/Formula noise "flow" presets (FieldPixelNode.cpp:342, FormulaNode.cpp:301). None estimate motion.
- Depth: src/nodes/DepthProjectionNode.h consumes a depth map as input. Nothing in src/ produces depth from a colour image (no depth-estimation hits for "depth" model/onnx outside it).
- Tracking: not on main. Branch origin/motion-track-node (not merged into HEAD, confirmed by merge-base --is-ancestor) holds src/nodes/MotionTrackNode.{h,cpp} (1334 lines) and docs/plans/motion-track-node.md. Its last commit is "WIP: Motion Track redesign as modulator-source node (abandoned)". It already contains hand-written Shi-Tomasi corners, an image Pyramid, forward-backward verified pyramidal LK (`TrackPointsKLT`, line 262), NCC coarse search (`CoarseNCCSearch`, line 469), and SubjectMask init via the RemoveBg approach.
- Hand/pose landmarks: src has `HeadTracker` (src/platform/HeadTracker.h) with a webcam source declared "not built yet". Searched src/ for hand landmark / pose / mediapipe: no hits.

## What the peers do differently
- OpenCV DIS (dis_flow.cpp): dense flow, CPU is the reference and a separate OpenCL path exists. Algorithm per pyramid level from coarsest to finest: precompute structure tensor over patches (precomputeStructureTensor, uses box-sum running totals, ~line 338), inverse-compositional patch search per grid patch (PatchInverseSearch_ParBody, line 772, Hessian inverse at ~939), densification of the sparse patch flows into a per-pixel field by weighted averaging by photometric error (Densification_ParBody, line 1006), optional variational refinement, then upsample flow x2 and multiply by 2 for the next level (ocl_calc loop ~1400-1430). Patch size and finest scale are auto-picked from image width (autoSelectPatchSizeAndScales, line 454). It is much faster than Horn-Schunck at similar quality and is parallel over patches, so it maps to a compute shader or to fragment passes per level.
- MediaPipe hand_landmarker_graph.cc: a detector runs only when tracking is lost; otherwise landmarks from the previous frame re-crop the next frame (comment lines 139-144). That detect-then-track gating is the point for a node that must stay cheap.
- Depth Anything V2: only Small is Apache-2.0 (README line 180); Base and up are non-commercial, so any bundled depth model must be the Small checkpoint, and the exact ONNX export must be checked for the same terms.

## Concrete improvements

### Existing-node upgrades
1. Image Analyze `motion` output from flow, not luma difference. Add mean flow vector, flow magnitude and dominant direction outputs next to kMotion (AnalyzeNodes.cpp:495), computed from a low-res (e.g. 160x90) dense flow. Benefit: motion that is directional (pans, a hand sweep) becomes usable modulation. Skill: new-modulator-node plus existing ImageAnalyze plumbing. Effort M. Licence: algorithm from papers (Horn-Schunck 1981, Lucas-Kanade 1981, DIS paper Kroeger 2016), so nothing to attribute if written from the papers.
2. Refactor SubjectMaskOnnx into a reusable `OrtModel` (per-model session, I/O names, normalisation constants, provider hook) so RemoveBg is one client. Prerequisite for every ML item below. Effort M. Licence: none (own code; ORT is MIT, already vendored).
3. Glitch "Datamosh" kind: if a flow node exists, add a mode that smears previous frame along flow (real datamosh) instead of row shuffle. Effort S after the flow node. No licence issue.

### New nodes
1. Optical Flow node (Analyze or Effects). Output a flow texture (RG, signed) plus magnitude/direction modulators. Options: Horn-Schunck (Jacobi iterations on a pyramid, easiest in GLSL), or a DIS-style patch search per OpenCV's structure. Consumers: displace/warp by flow, flow-driven particles (feed Field), datamosh, motion-reactive blur. Skill: new-effect-node (needs frame-persistent previous-frame texture so also new-compositing-node ping-pong). Effort M (HS) / L (DIS). Licence: reimplement from the papers; a near-port of dis_flow.cpp structure would need Apache-2.0 attribution, so prefer paper-only.
2. Motion Track: revive origin/motion-track-node rather than restart. It has working KLT/NCC code; the plan doc has the full design (modulator outputs x/y/scale/rotation/confidence, offline analysis pass). Risk: branch marked abandoned and diverged from main, so the rebase and the redesign-as-modulator state needs reading before reuse. Skill: new-modulator-node, windows-parity (video decode). Effort L. Licence: own code.
3. Depth Estimate node (image to depth map feeding DepthProjectionNode.h). Use Depth-Anything-V2-Small ONNX on the shared OrtModel, run on the RemoveBgNode worker pattern. Benefit: any video or photo becomes a 3D point cloud or relief. Needs the model weights shipped plus a NOTICE.txt entry, and a CPU speed budget check (ViT-S at 518 px on CPU is slow, so use low res and frame skipping). Skill: new-source-node/new-effect-node plus the worker pattern. Effort L. Licence: Small checkpoint Apache-2.0 only; Base and Large are CC-BY-NC and must not be shipped.
4. Hand Landmarks node (21 joints as modulator outputs). Needs two models (detector plus landmark), TFLite to ONNX conversion (model licence unverified), crop/re-crop logic like MediaPipe's detect-then-track gating, and the missing webcam path. Highest effort. Effort L. Licence: MediaPipe code Apache-2.0; model weights licence to be verified.
5. Segmentation prompts (SAM 2) as a RemoveBg mode: heavy, encoder too large for realtime. Effort L, not recommended now.

## Nothing to learn / not worth it
- Linking OpenCV: no (invariant from the first-pass scan: do not link it). Reading it for structure is fine under Apache-2.0.
- MediaPipe framework itself (Bazel graph runtime): nothing to port; only the detect-then-track idea.

## Candidates
Optical Flow node (Horn-Schunck GLSL first, DIS-style later) | flow texture + motion modulators, unlocks datamosh/warp/reactive effects | M | algorithm from papers; Apache-2.0 attribution only if DIS structure is ported
OrtModel refactor of SubjectMaskOnnx | prerequisite to depth/hand/any ML node, no user-visible change | M | own code, ORT MIT
Revive Motion Track from origin/motion-track-node | object position/scale as modulators, KLT/NCC already written | L | own code
Depth Estimate (Depth-Anything-V2-Small) | image to 3D relief via existing DepthProjectionNode | L | Small only is Apache-2.0; Base/Large CC-BY-NC, must not ship
