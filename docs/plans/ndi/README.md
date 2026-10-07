# NDI video in/out (R562, closed)

Status: BUILT 2026-10-07 on feature/ndi-in-out (owner reopened and asked to finish). Steps 1-3 and 6 (help, branding in help text) done; loopback verified on macOS with the real runtime (INFINITE_NDITEST). Windows/Linux builds not run from here. Runtime is found via NDI_RUNTIME_DIR_V6/V5 or default paths, never bundled. NDI frames are lossy (SpeedHQ), so the test compares within a tolerance.

## Goal
Infinite publishes any image cable as a named NDI source on the local network, and takes any NDI source on the network
as an image input. Same user model as Syphon (macOS) and Spout (Windows), but across machines and on Linux.

## Licence (checked 2026-10-07 against docs.ndi.video; full SDK EULA text still to be read)
- NDI headers may live in an MIT open-source repo (MIT per NDI). Infinite is MIT, fine.
- Load the NDI runtime dynamically (NDIlib_v5_load style loader from the SDK headers; find the library via
  NDI_RUNTIME_DIR_V5 or the standard install paths). Do NOT bundle the NDI binaries: that needs our EULA to cover NDI's.
- NDI branding is required wherever the feature shows in UI. Read the exact wording in the SDK licence before building UI.
- Clean room: do not open obs-ndi or other GPL NDI wrappers (AGENTS.md invariant 1). Work from NDI's own SDK docs/headers only.
- If the runtime is missing: nodes show "NDI runtime not found (install from ndi.video)" and everything else works.

## Open questions, closed 2026-10-07 (from docs.ndi.video)
- Loader: single exported function `NDIlib_v5_load()` returns the NDIlib_v5 function-pointer struct (replaces my guessed API names).
- Library: Windows `Processing.NDI.Lib.x64.dll` (folder in env var NDILIB_REDIST_FOLDER; NDI_RUNTIME_DIR_V5 also documented),
  macOS `/usr/local/lib` (libndi.dylib), Linux `libndi.so` in `/usr/local/lib` (docs give this as the example path,
  not a guarantee for every distro: also try the default dlopen search). Verify the exact file names against the SDK.
- Branding (required): show "NDI(R) is a registered trademark of Vizrt NDI AB" in the About box and near first NDI use
  in the node help; link https://ndi.video/ next to the NDI source selector and in our docs/website; point users to
  https://ndi.video/tools/ for NDI Tools, never ship them. Logos come from the SDK LOGOS folder (needs the SDK).
- Residual, not blocking: the SDK itself (headers, logos) must be downloaded by the owner from ndi.video (registration
  form), then headers committed under external/ndi/ with the NDI licence file beside them.

## Starting point (verified in code)
- Syphon Out/In are the template: src/nodes/SyphonOutNode.{h,cpp}, SyphonInNode.{h,cpp}, registered at
  src/main.cpp REGISTER_NODE(SyphonOutNode / SyphonInNode, "Utility").
- Platform surface: Platform::SyphonServerCreate/Publish/HasClients etc. (src/platform/Platform.h ~1100); Windows backs it
  with Spout2 (PlatformWinSyphon.cpp), Linux has SyphonLinux.cpp.
- Do not reuse the Syphon names. Add a separate Platform::Ndi* surface so Syphon behaviour is untouched.

## Design
1. src/platform/ndi/NdiRuntime.{h,cpp}: dynamic loader, `bool Ndi::Available()`, init/destroy once, version string.
   One implementation for all three OSes (dlopen / LoadLibrary), no per-OS NDI code beyond the library name/paths.
2. NDI Out node: one image input, a name param (source name on the network), optional "alpha" toggle.
   Each cook: read back the input texture (glReadPixels via a PBO ring, 2 frames deep so the GPU never stalls),
   hand the BGRA frame to the NDI async send API (keep the previous frame alive until the next send call).
   Withdraw (destroy the sender) while bypassed, like SyphonOutNode::Withdraw.
3. NDI In node: source dropdown filled from NDI finder (poll on a worker thread, never the UI/audio thread), connect on
   select, a receiver thread pulls frames into a small mailbox, the cook uploads the latest frame to a texture.
   No frame yet: output stays black and the status line says "waiting".
4. Frame rate: send at the patch's output rate, receive whatever arrives; never block the cook waiting on the network.
5. Audio over NDI: out of scope for v1 (video only). Note it in the node help.
6. Perf: audio > projector > canvas > previews (perf initiative). The readback must not cost the audio thread anything;
   measure with the B8 bench route in run-infinite-hygiene before keeping it. Keep-only-if-better gate applies.

## Steps (one branch, commit per step)
1. Runtime loader + `INFINITE_NDITEST` that prints Available/version or "not installed" and exits 0 either way.
2. NDI Out node (no In yet), spawn, save/load of the name param, help-table entry, node-facts regeneration
   (tools/gen-patch-skill.py), node count census update (README, website).
3. NDI In node with finder + receiver.
4. Loopback test on one machine: Out publishes a test pattern, In receives it, compare a pixel (skip cleanly when the
   runtime is absent so CI stays green).
5. Windows and Linux parity pass: library names/paths, line endings, MSVC build, Linux AppImage does not bundle NDI.
6. Docs: Node Reference Manual flag (nodediff), release notes, branding line in UI per the SDK licence.

## Tests and sweeps
Param round trip (name), teardown (delete the node mid-stream, no crash, sender destroyed), bypass withdraws the source,
cable-logic-sweep (image in, no image out for Out), render-pipeline-sweep for the In node's texture output,
windows-parity and linux-parity reviews, B8 perf bench before/after.

## Risks
- Needs a second machine or an NDI tool (NDI Studio Monitor) for a real-network check; loopback test covers the code path.
- GPU readback cost at 4K; mitigate with the PBO ring and optional downscale param.
- Linux runtime path varies by distro; fall back to the default library search.

## Effort and order
L. After R555 (split main.cpp) so builds are fast. Skills: new-utility-node, new-source-node, windows-parity,
linux-parity, node-ui-pillars, render-pipeline-sweep, cable-logic-sweep, run-infinite-hygiene.
