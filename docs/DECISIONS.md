# Decisions

The owner's standing decisions that still steer work. Newest first. Add one only when it will matter
to a later session; plan-level detail belongs in the plan doc, not here.

## Product and direction
- 2026-10-29 Owner: the Sample Studio is a self-contained "Sampler pro" editor. Editing (zoom, slices, per-slice gain/pitch/stretch/compress/fades/shaping, colours) happens inside it and never changes the canvas; the result goes into the patch as a node (Sampler, Slicer, ...) or as files, and modulation applies only after that. Studio work is saved in the patch. Spec: `docs/plans/sample-studio/SLICE-EDITOR.md`, phases 1-6 approved.
- 2026-10-29 Agent (owner delegated the call): sound words (C4) in three tiers, from `feel/words_eval2.json` (AUC held out by pack). Heard (>= 0.75, Infinite may tag a sound with it): sub, distorted, mellow, punchy, digital. Close (0.70-0.75, kept and worked on, never claimed yet): dark, warm, heavy, dusty, analog. Name-only (the word follows the pack, not the audio; search by preset name only, never a claim about the sound): clean, soft, airy, light, wide, metallic, thin, wet, smooth, and every other word under 0.70 until it reaches 0.75.
- 2026-10-24 Owner: Visual art and library generator work must be creative, grounded in authentic art movements (the art taxonomy in `tools/spread/art_taxonomy.py`: Risograph Manga, Ukiyo-e Woodblock, 1-Bit Ditherpunk, Swiss Neo-Brutalism, Chavetta Collage, Analog CRT, Liminal Void, etc.), and never default to esoteric mathematics or raw differential equations. Visuals do not need to be audio-reactive; pure visual and pixel art stands on its own.
- 2026-10-24 Owner: P8 Phase 3 is approved: Displacement gets a Noise source (5a), Feedback carries
  geometry (5b), and Point Trails is a new node (5c). Plan: `docs/plans/gpu-points/README.md`.
- 2026-10-23 Owner: a group is a block. There is no separate "Make block": every group has a power
- 2026-10-23 Owner: Session works like GarageBand's Live Loops. Session Audio is its own audio mode (twin of Timeline Audio On): only launched clips play, the Timeline's clips stay silent. Clicking a playing or queued clip or scene stops or takes it back on the grid; Stop All stays in the toolbar. The Session has its own launch quantize, 1 bar by default. Launch modes are Trigger and Gate (Toggle retired: Trigger now stops on a second click). Scenes can play in order with "Then Play Next Scene". Record writes the performance to the Timeline and Render Take renders that span, audio and video, to a folder the user picks.
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
