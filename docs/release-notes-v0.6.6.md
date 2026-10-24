## What's new in v0.6.6

v0.6.6 fixes Infinite not opening for some Windows users and takes a first step on Windows VST3 scanning, which is still an open issue. It also lets you reorder the Session grid by dragging.

### Fixes

- Windows: Infinite no longer fails to open when its saved sample or media list cannot be read. A bad entry in the cached index is skipped, and the folders are rescanned on request
- Windows VST3 scanning: a plugin that crashes while it is being unloaded is no longer blocklisted.

### Session view

- Drag a scene or a track header to reorder it; the launch schedule follows the clips
- Delete Track in a track's right-click menu

### Updates

- The Downloading dialog shows the Infinite mark drawing itself as the update arrives

### Help wanted: Windows VST3 scanning

Some Windows machines still reject plugins that load fine elsewhere. If Infinite blocklists a plugin of yours, please open an issue at https://github.com/n1m21n/Infinite/issues with:

- the plugin name, vendor and version
- your Windows version
- the exit code from running the scanner on that one plugin: `infinite-vst3-scanner.exe --batch "<path to the .vst3>"`, then `$LASTEXITCODE` in PowerShell (a crash shows as a large negative or hexadecimal number)

That tells us which plugins fail and where, so the fix targets real cases instead of guesses.
