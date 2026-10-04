# Contributing to Infinite

Thanks for helping. Bug reports, feature ideas, patches and nodes are all welcome.

## Ways to help

- **Report a bug or ask for a feature** in [GitHub Issues](https://github.com/n1m21n/Infinite/issues). For a bug, include your OS and version, your GPU and audio device, and the steps that trigger it.
- **Share patches and ask questions** in the [Discord community](https://discord.gg/7cpQfCxnx).
- **Send a pull request.** Small, focused PRs are easiest to review. If a PR bundles several unrelated areas, we may take the ideas and build them natively instead of merging it as one piece. In that case we say so in the PR and credit you (see below).

## Code rules

- Infinite is [MIT licensed](LICENSE). Contributions must be clean-room and MIT-compatible: do not copy code from GPL or AGPL projects, and do not base work on their source.
- Follow [docs/CODE_STANDARDS.md](docs/CODE_STANDARDS.md).
- Every platform function has macOS, Windows and Linux sides. See [docs/WINDOWS_COMPATIBILITY_STANDARDS.md](docs/WINDOWS_COMPATIBILITY_STANDARDS.md).
- Build and test instructions are in the [README](README.md).

## Community and contributors

Infinite is developed by [n1m21n](https://github.com/n1m21n) with extensive AI coding assistance from Anthropic's Claude.

When someone's work or ideas shape a feature, we credit them here and in [ACKNOWLEDGEMENTS.md](ACKNOWLEDGEMENTS.md), even when we build the feature ourselves.

- **[Ricardo Palmieri](https://github.com/ricardopalmieri)** ([@ricardopalmieri](https://www.instagram.com/ricardopalmieri/)) built a Windows-focused fork and opened [PR #24](https://github.com/n1m21n/Infinite/pull/24). It is the origin of the ideas behind Windows low-latency audio, the Looper, the 16-pad MPC sampler and per-parameter MIDI learn. His fork also pointed to a drum groove library, a Clip Matrix, a Chord Progression node, a MIDI File node and an MCP bridge for AI assistants. He also supplied the reproduction details for the Auto-Tune Pro editor freeze.
- **[IcedQuinn](https://github.com/IcedQuinn)** reported [issue #26](https://github.com/n1m21n/Infinite/issues/26) (transparent Output windows for OBS window capture on Linux and Windows).

Your name belongs on this list if you contribute. If we missed crediting you, open an issue and we will fix it.
