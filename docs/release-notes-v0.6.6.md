## What's new in v0.6.6

v0.6.6 fixes Infinite not opening for some Windows users, and the Windows VST3 scanner rejecting plugins that are fine. It also lets you reorder the Session grid by dragging.

### Fixes

- Windows: Infinite no longer fails to open when its saved sample or media list cannot be read. A bad entry in the cached index is skipped, and the folders are rescanned on request
- Windows VST3 scanning: a plugin that crashes while it is being unloaded is no longer blocklisted.

### Session view

- Drag a scene or a track header to reorder it; the launch schedule follows the clips
- Delete Track in a track's right-click menu

### Updates

- The Downloading dialog shows the Infinite mark drawing itself as the update arrives
