# Playbook (used by daily slot 21): not a separate schedule

Read COMMON.md first.

1. Coverage (A-F): run `python3 tools/bughunt/coverage.py` over the last runs; list zero-hit nodes/files. File ONE issue "[coverage] gaps this month" with the zero-hit list and which harness should aim at them.
2. Security and I/O (K): OSC malformed-packet fuzz to 127.0.0.1:9100 (python UDP, local only); MIDI bad input; plugin load of a corrupt/unsigned plugin file (use a synthetic bad file, never download one); self-update path verification logic; grep the built bundle for secrets (tokens, keys, `client_secret`, `.pickle`, absolute /Users paths). Never send anything to the network beyond localhost.
3. Old-patch compatibility (C): if `tools/release/legacy_diff.py` exists run it; otherwise run patch_variants on the oldest shipped patches and note "legacy_diff not built".
4. Release artefacts (L): `python3 scripts/release_version_gate.py`; skill release-notes-audit; manual PDFs (Infinite_Node_Reference_Manual.pdf, Field_Language_Manual.pdf) vs `--describe`; website links; installer scripts; THIRD_PARTY_NOTICES vs third_party/ and external/ (licences).
5. Tool health: run each tool's `selftest` / `--self-test`; a broken tool is a finding because it makes every hunt blind.
6. Owner-only checks: open ONE issue titled `[manual] checks due <month>` labelled auto-bughunt, listing only what a person must do: listening pass on 10 example patches, Windows manual pass (docs/WINDOWS_VERIFICATION.md), Linux real-desktop pass, MIDI/audio hardware pass, UI feel and first-run walk-through, one soak (>=2 h with a live patch). Close the previous month's issue only if the owner ticked it.
