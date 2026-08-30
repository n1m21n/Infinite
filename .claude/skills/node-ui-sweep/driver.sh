#!/usr/bin/env bash
# Node UI/UX sweep: pins, cables, dragging, collapsing, grouping, undo,
# in-node editors, node-local viewports, drag-and-drop. See SKILL.md.
SWEEP_NAME="node UI/UX sweep"
SWEEP_ARGS=("$@")

ASSERT=(
  # Pin identity: no node may emit the same pin id twice in one frame. A
  # duplicate makes cables land on the wrong control, or vanish.
  "PINDUPTEST:8"
  # Canvas pan vs node drag: dragging empty canvas pans the view and leaves the
  # node still; dragging a node title moves the node.
  "DRAGTEST:22"
  # Collapsing a node's params must not drop its modulation bindings, must keep
  # drawing the cable on the collapsed body, and must keep applying the value.
  "HIDETEST:14"
  # In-node editors that are dragged directly rather than through a widget.
  "WTDRAGTEST:80"          # wavetable table scrub, amp sustain, per-slot filter
  "EQDRAGTEST:80"          # EQ curve dot drag (freq), diamond drag (Q),
                           # double-click band bypass
  # Node-local 3D viewport renders, and tracks a transform change.
  "MINIVIEWPORTTEST:12"
  # Undo/redo across spawn, param edit, and delete-with-a-connection.
  "UNDOTEST:8"
  # Group boxes: they resize to members, do not steal an overlapping group's
  # members, and ungroup frees members without freeing nodes.
  "GROUPTEST:28"
  # Drag-and-drop from the browser onto a node. These wait on an asynchronous
  # media/plugin scan before their first phase, so the budget is deliberately
  # generous - they exit as soon as their verdict prints.
  "SAMPLERDRAGTEST:600"
  "MEDIADRAGTEST:600"
  "PLUGINDRAGTEST:600"
)

OBSERVE=(
  # Node box size per frame while a body opens/closes. Read it for a size that
  # oscillates or grows without bound; it asserts nothing.
  "SIZETEST:16"
  # Select / copy / paste / delete counts. Read that paste produced a real
  # duplicate with the source node's param values, not a default-constructed one.
  "INPUTTEST:12"
)

node_ui_static() {
  # Ratchet on unreachable controls. Every on-screen value control should go
  # through a Mod* wrapper so it draws a pin, takes a cable and shows up in the
  # performance matrix picker; "plain" counts the ones that do not. Baseline
  # when this skill was written: 16. Raising it is a regression, so this fails
  # if the count grows - lower the number here when you fix some.
  local baseline=16
  local line
  line="$(python3 scripts/audit_node_params.py --out docs/node_param_audit.md 2>&1 | tail -1)"
  echo "  $line"
  local plain
  plain="$(printf '%s' "$line" | sed -n 's/.*, \([0-9]*\) plain.*/\1/p')"
  if [ -z "$plain" ]; then
    echo "  could not parse the audit summary - check scripts/audit_node_params.py"
    return 1
  fi
  if [ "$plain" -gt "$baseline" ]; then
    echo "  $plain non-modulatable controls, baseline is $baseline - a new control"
    echo "  is using a raw ImGui widget instead of a Mod* wrapper. See node-param-audit."
    return 1
  fi
  return 0
}
SWEEP_EXTRA=node_ui_static

source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)/scripts/sweep_runner.sh"
