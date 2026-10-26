# Facts safe to state

Check each against the repo before use; update when the app changes.

- Infinite is a free, open-source (MIT) DAW for visuals, music and code on one infinite canvas. macOS, Windows, Linux.
- Made by Naman Soni (GitHub n1m21n). Credit line: "Infinite, by Naman Soni (n1m21n), https://infinitedevices.org".
- Hosts AU and VST3 plugins; their parameters can be modulated by LFOs, envelopes, step sequencers, physics and audio analysis.
- Audio Analyze node: 8-band FFT and onset detection; the first-music-image template exposes level, low, mid, high.
- Field: the built-in language for shaders, geometry deformers and audio kernels. `param` declares knobs.
- Headless flags: `--validate`, `--frame`, `--frames-dir`, `--render`, `--audio-summary`, `--describe`, `--explain`, `--set`. Camera, microphone and MIDI patches are refused headless (exit code 4). Linux needs `xvfb-run -a`.
- Video output: Syphon (macOS), Spout (Windows), NDI.
- Patch format: plain text `.inf` (`infinite-patch 1`, `node`, `cable`, `mod`, `expr`).
- Links: site https://infinitedevices.org, source https://github.com/n1m21n/Infinite, Discord https://discord.gg/GMeQTyJqkH.

Do not claim: AAX support, mobile apps, anything about performance numbers we have not measured, user counts.
