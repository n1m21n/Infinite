# Decisions

The owner's standing decisions that still steer work. Newest first. Add one only when it will matter
to a later session; plan-level detail belongs in the plan doc, not here.

## Product and direction
- 2026-10-23 Owner: a group is a block. There is no separate "Make block": every group has a power
  button in its frame, and off means its nodes stop cooking and go silent (fade out).
- 2026-10-23 Owner: ASIO on Windows ships under Steinberg's proprietary ASIO licence (the owner signs
  the agreement); the SDK is fetched at build time, never committed. Quantized restart (E1) stays.
  The Session view of the Timeline (P5) is approved to build.
- 2026-10-23 Owner: the NOW page, the quest ledger and the roadmap board are retired. Work is
  tracked in plan docs and git; owner decisions live here.
- 2026-10-22 Agent (owner asked): credit travels in three layers. Exported audio, images, video and
  patches carry a "Made with Infinite" note in their metadata (Settings > Audio > Credit Infinite in
  exported files, on by default). Metadata only: nothing drawn or mixed in, no copyright claim over
  user work. Agent tool results and the MCP handshake carry an `attribution` object. Wording lives in
  `src/core/Provenance.h`; `tools/credit/check_credit.py` checks it.
- 2026-10-14 Owner: Infinite is a design instrument and also a live instrument, balanced, never
  one-sided.
- 2026-10-13 Owner: reopening a patch need not be identical; the software evolves with the user and
  a show is a holistic art experience, not a replay. User patches only need to open, play and show
  a picture.
- 2026-10-13 Owner: pack and app updates must be seamless. The extensions catalog has its own
  release tag `extensions`; `tools/release/publish_extensions.py` is the one publish route. Each
  extension installs live into the running app; extensions never ship inside the app bundle.
- 2026-10-13 Owner: Analog's oscillators meet the aliasing probe targets (non-harmonic <= -80 dB at
  MIDI 60/96/108) even though saved Analog patches sound cleaner; no legacy flag for the old sound.
- 2026-10-12 Owner: a release is judged by quality (escaped fixes, reverts, known test failures,
  crashes, bug issues), not downloads; reach is context only.
- 2026-10-11 Owner: success order is income > reach > art/name > learning.
