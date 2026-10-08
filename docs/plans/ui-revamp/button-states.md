# Button states: selected must not look like hover

Owner report (2026-10-08): a toggle/segment button that is already selected shows the accent
blue; hovering it changes colour, which reads as "something changed" when nothing did. Happens
app-wide.

Rule to adopt:
- Selected = accent fill. Hover on a selected button keeps the accent and only brightens by a
  few percent (no hue change). Hover on an unselected button may lift the neutral fill.
- Implement once in the shared button/toggle helpers (`AudioToggleButton`, the segment strip
  helpers, `LaneToggle`, the inline `Push/PopStyleColor(ImGuiCol_Button, AccentEmphasisSelected())`
  sites) by also pushing `ImGuiCol_ButtonHovered`/`ImGuiCol_ButtonActive` for the selected state.
- Sweep for hand-rolled pushes: `grep -rn "AccentEmphasisSelected" src`.
- Push/Pop must read one local `on` captured before the click (a click can flip the state
  between them; this leaked styles in the Spatial Mixer and Audio Out `live` buttons).

(No existing UI/UX revamp plan file was found in docs/plans; move this into it if it lives elsewhere.)
