# Block J1: copy and paste between patches

Copy writes the selection twice: the in-process list (fast path, unchanged, used when you paste in the same session) and text on the system clipboard, `infinite-nodes v<N>` followed by the patch format of just those nodes (`src/app/graph/NodeClipboard.cpp`, `Patch::WriteText`). Paste uses the text when the system clipboard holds nodes this process did not copy itself; otherwise the fast path runs as before.

| Case | Behaviour |
|---|---|
| Params, cables, modulation, palette links, expressions between copied nodes | Carried |
| Cables, bindings or expressions to uncopied nodes | Not carried (filtered when copying) |
| Node uids | Never carried; fresh ones minted, so none collide |
| Node `id` names | Cleared (a name is unique per patch) |
| Field graph | Fresh uid, ownership remapped to the pasted children only |
| Comment, Group | Carried (group membership is not saved in the format; it is not restored across patches) |
| Missing media or plugin | Same load path as opening a patch: the Block F relink state and the missing-plugin state |
| Node type this build lacks | Skipped, counted in the status line, the rest still pastes |
| Newer `infinite-nodes` version | Refused with one status line, nothing changes |
| Older version | Read by the patch loader, which migrates |
| Undo | One step labelled Paste |
| Placement | Centred on the pointer over the canvas, else the view centre |

Check: `INFINITE_CLIPBOARDTEST=1` (copy, new patch, paste, refusals, unknown type).

Not checked: the manual two-window paste; gesture recordings and performance-panel bindings are not carried; Field graph copied *with* its children is covered by the ownership remap only, not a fixture; media relink state on paste was not shot.
