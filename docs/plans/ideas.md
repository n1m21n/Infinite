# Parked ideas

Concepts that got a working implementation but aren't being carried forward right now.
Kept here so the idea isn't lost when the branch is deleted — check `git log` on the
noted commit if the actual code is ever wanted back.

## Chroma Color (Synesthesia) node

Audio-reactive image node that turns whatever's playing into a 12-tone chromagram and
maps it to color/light. Was implemented on `feature/chroma-color` (commit `1a8f2b0`,
not merged) as `src/nodes/ChromaColorNode.{h,cpp}`, ~800 lines across those two files
plus `main.cpp` UI wiring.

What it did:
- Ran a 12-TET chromagram over the audio input (pitch-class energy per semitone,
  wrapped across octaves) and estimated the current musical key from it.
- Exposed root note, hue, "consonance," and energy, plus a per-pitch-class output
  (16 outputs total) for driving other nodes.
- Three palette presets — Circle of Fifths, Scriabin's color-to-key mapping, Newton's
  spectrum-to-note mapping — plus a custom palette, to turn detected pitch/key into
  color.
- Three layout modes for how the 12 pitch classes were arranged: chromatic order,
  circle-of-fifths order, or only the currently-sounding notes sorted by loudness.

Why parked: no immediate use case queued up; shelved rather than half-finished.

Revive by: `git log --all --oneline | grep 1a8f2b0` to find the commit, then
`git show 1a8f2b0` or cherry-pick it onto a fresh branch.
