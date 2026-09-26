# System 2: Systems Thinking, Formal Causal Logic & Invariant Proofs

System 2 is the analytical, deliberative cognitive engine. It maps state transitions, constructs causal dependency Directed Acyclic Graphs (DAGs), traces blast radii, and mathematically audits invariants.

---

## 1. The 9-Question Blast Radius Framework

Before any code modification or bugfix is approved, System 2 executes the 9-Question Blast Radius Analysis:

```
                  ┌───────────────────────────────┐
                  │       IDENTIFIED DEFECT       │
                  └───────────────┬───────────────┘
                                  │
    ┌─────────────────────────────┼─────────────────────────────┐
    ▼                             ▼                             ▼
Q1: Owning Node               Q2: Faulty Logic             Q3: Caller Graph
Who owns this state?          Which exact function fails?   Who calls this logic?
    │                             │                             │
    ▼                             ▼                             ▼
Q4: Silent Degradation        Q5: Codebase Multiplicity    Q6: Platform Divergence
What else silently degrades?   Is this pattern elsewhere?   macOS vs Win vs Linux?
    │                             │                             │
    ▼                             ▼                             ▼
Q7: Fix Loophole / Side-effect Q8: Historical Origin        Q9: Test Harness Blindspot
Does the fix break an invariant? When was it introduced?     Why did tests miss this?
```

1. **Owning Node / Subsystem**: Which subsystem owns the memory, lifecycle, or clock?
2. **Faulty Logic**: What is the exact mathematical or logic defect (not just the symptom)?
3. **Caller Graph**: Who else invokes this function or reads this buffer?
4. **Silent Degradation**: What downstream node or visualizer silently receives corrupted or stale data without hard crashing?
5. **Codebase Multiplicity**: Is this same pattern copy-pasted in sibling nodes (e.g. across Filter, Drive, Reverb, Delay)?
6. **Platform Divergence**: Does this behave differently on macOS (CoreAudio/Metal/POSIX) vs Windows (WASAPI/DirectX/Win32) vs Linux (PipeWire/X11/ALSA)?
7. **Fix Loophole / Side-Effects**: Does fixing this symptom break an established guarantee (e.g. parameter mailbox delivery, undo snapshot)?
8. **Historical Origin & Bad State**: When was this introduced, and what invalid serialized patch state does it leave behind?
9. **Test Harness Blindspot**: Why did `run-infinite-hygiene` or existing sweep fixtures miss this? (What test must be added?)

---

## 2. Invariant Interaction Audit (The Sibling-Undo Rule)

Whenever code establishes a guarantee:
$$\text{Guarantee } G \text{ established at stage } t_1$$

System 2 searches every subsequent stage $t_2 > t_1$:
$$\forall \text{ control/stage } C \text{ where } \text{execution}(C) > t_1: \quad \text{Assert}(C \text{ does not invalidate } G)$$

### Concrete Examples in Infinite:
* **Quantization Guarantee**: If `QuantizeToScale()` forces pitch to C Minor at $t_1$, check whether the `Detune` slider, `Transpose` knob, or `Glide` smoothing runs at $t_2$ and undoes scale quantization.
* **Normalization Guarantee**: If audio signal is clamped/normalized to $[-1.0, 1.0]$ at $t_1$, check whether downstream `Drive` or `StereoSpread` clips or wraps out of bounds.
* **UID Monotonicity**: If node UID is minted at $t_1$, verify that duplicating, copy-pasting, or undo/redo at $t_2$ doesn't collide with existing IDs.
* **Skip Gate vs Its Writers (hand-kept list drift)**: Any "skip this work unless ..." gate (off-screen cull, collapsed register-only pass, idle cook skip) is a guarantee that *the skipped work carried nothing needed*. When the gate's exemption list is written by hand, it must be derived from the full set of writers that depend on the skipped work, not copied from a sibling gate. Real bug (fixed in `faf6a2e`): the v0.4.4 off-screen cull copied its must-draw list from the collapsed pass. Neither list had gesture loops, which write params only through ParamRefs registered by drawing. Result: gesture-animated nodes stepped every 30 frames when zoomed in (in renders too), and froze with the eye closed. Rule: one predicate (`GraphNode::IsParamDriven`) that every gate calls, with flags set right next to the writers (`RefreshParamDriverFlags` beside `ApplyModulationAndPalette`). Pair every such perf skip with a correctness fixture: the skipped item must still change every frame (`CULLDRIVENTEST`), not just a frame-time number.

---

## 3. Causal State Machine & Lifecycle Verification

Every node lifecycle follows a strict state transition proof:

$$\text{Spawn} \longrightarrow \text{Wire} \longrightarrow \text{Cook} \longrightarrow \text{Serialize} \longrightarrow \text{Bypass} \longrightarrow \text{Unbind} \longrightarrow \text{Destroy}$$

* **Cook Memoization Proof**: A node must only re-cook when upstream `TextureRevision` / `GeometryRevision` / parameters change, or when it holds internal temporal state (e.g., Feedback, Diffusion, Transport).
* **Bypass Contract**: When bypassed, a node must pass its primary input through bit-for-bit without allocating or altering texture dimensions.
* **Teardown Proof**: Deleting a node mid-playback must atomically detach all cables, unbind all modulation targets, flush parameter mailboxes, and release GPU textures without dangling pointers.
