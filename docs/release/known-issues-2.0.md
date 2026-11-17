# Known issues in the 2.0 manual shots

## Boolean and instance nodes show a red error outline in the node shots

Affected nodes (captured with the 2.0 release-copy app, scratch HOME):

- 3D Intersect (closed and open)
- 3D Union (closed and open)
- 3D Difference (open)
- 3D Instance on Points (open; closed flagged in one run)

Repro: create the node, wire two closed sphere/cube primitives into its geometry inputs (Sphere into slot 0, Cube into slot 1), fit the view and capture the node. The outline is red. The same red outline shows with the inputs unwired.

Assessment: the red outline appears even when the inputs are valid closed solids, so it is likely a real bug (a boolean on valid inputs showing an error) and not a missing-input state. The node body still renders the boolean result, so the shot is kept and not replaced with an unwired picture. Treat as a bug to triage before 2.0 ships; the manual shot should be recaptured once the outline is fixed.

Update (gate lane): the warning did NOT reproduce through the RPC probe. Sphere+Cube into Join Geometry under Intersect, Union and Difference, wired and unwired, report an empty cook warning, so the red outline in these shots is most likely a capture-harness artefact (node cooked outside a full patch), not a product bug. Instance on Points was not probed. Recapture or confirm before relying on these pictures.

Status: open (low). Recorded in docs/shots/manifest.json under "exceptions".


## Golden regressions shipped in 2.0.0 (fix in 2.0.1)

Three render-goldens differ from their baselines. They are shipped as known issues; the goldens are not refreshed.

- render3d: max diff 1.349 (255 scale).
- voice-spread: +1.3 dB.
- envelope-per-voice: +1.05 dB.

Suspects for 2.0.1: perf commits 18409229, 81c20e9e, 8c436ca0, c9db277d, b8f056cc and the 3D View background change 899f6b4e. Not bisected before release.

Status: open. Fix in 2.0.1.
