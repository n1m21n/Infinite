# Parked ideas (dropped from the quest board 2026-10-07)

Not v0.4.7. Revive by adding a quest only when it is picked for a release.

- R21 Node tutorial film series, R29 250 GitHub stars (growth)
- R26 Prediction v2 modes, R31 test-suite tiering plan, R555 split main.cpp, R556 Metal compute spike
- R562 NDI in/out, R568 skip BGRA->RGBA convert, R574 IME candidate position
- R584 keyboard-only plan, R597 MPE input, R602 screen-reader support
- R611/R612/R613 audio sweep rigs, R615 Key-Snap pin + glyph
- R561 Host CLAP plugins

## Release v0.4.7 decisions (owner, 2026-10-07)
- Push main: not yet. Version bump 0.4.7 is committed locally (2817e882), nothing pushed, tagged or released.
- Tag, notes, DMG, CI builds: off. Owner: too much work left to ship; release only when called.
- v0.4.7 scope (PROPOSED by agent, owner to approve): ship what is merged, nothing from the parked list. v0.4.8 candidates: R555 first, then R556, R562, R597. Later: R602, R561, R568, R574.
- Before shipping: CI green on Windows and Linux, release-notes audit, Node Reference Manual PDF (MIDI File, Group 3D).
- R562 NDI licence check (2026-10-07, from docs.ndi.video, EULA text not read in full): headers may ship in an MIT open-source repo; load the NDI runtime dynamically (NDI_RUNTIME_DIR_V5), do not bundle binaries (that needs our EULA to cover NDI's). NDI branding required in UI. Todo: read the full SDK licence for the branding wording; confirm the Linux runtime story. Still v0.4.8.
- R597 MPE: plan written at docs/plans/mpe/README.md, v0.4.8.
- R562 NDI: execution plan at docs/plans/ndi/README.md, v0.4.8.
- DECISION (owner, 2026-10-07): MPE (R597) and NDI (R562) go INTO v0.4.7. Supersedes the earlier v0.4.8 proposal for these two. Release stays unpushed until both land and owner calls ship. Open questions in both plan docs closed.
- MPE (R597): owned by another session; do not start here. R633 dropped. NDI docs only in this session. NDI SDK download approved by owner (form, licence, download).
