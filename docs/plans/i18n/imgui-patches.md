# Vendored ImGui patches (ImGui 1.92.9)

Re-apply every row when upgrading ImGui. Marked `Infinite patch` in the source.

| File | Change | Why |
|---|---|---|
| `external/imgui/imgui_draw.cpp` | `ImFontCalcWordWrapPositionEx`: Han / kana / CJK punctuation / fullwidth are break opportunities between any two characters; closing punctuation never starts a line, opening brackets never end one (`ImCharIsCjkWrapW`, `ImCharIsCjkNoLineStartW`, `ImCharIsCjkNoLineEndW`). | Stock ImGui only breaks on blanks. `I18NTEST` checks it. |
| `external/imgui/backends/imgui_impl_glfw.{h,cpp}` | `ImGui_ImplGlfw_SetPointScale/GetPointScale`: DisplaySize, mouse and Win32 IME positions in points; framebuffer scale carries the rest. | Same point-based layout on every OS (`src/core/UiScale.h`). |
| `external/imgui/imgui.h`, `imgui_widgets.cpp` | `ImGui::ButtonLabelHook`, called from `ButtonEx`. | Headless `--describe` lists a node's buttons. |
| `external/imgui/imgui_widgets.cpp` | `SliderScalar`: double-click opens the typed-value box. | Type a value on any slider. |
| `external/imgui/imconfig.h` | `IM_DEBUG_BREAK()` is a no-op. | The UI Debugger's Item Picker raised SIGTRAP in a shipped app. |
| `external/imgui-node-editor/imgui_extra_math.inl` | Skip our `operator*(float, ImVec2)` when `IMGUI_DEFINE_MATH_OPERATORS` is set. | ImGui 1.92 defines it in `imgui.h`. |

No longer patched: rounded `Selectable` highlights (1.92 has `style.SelectableRounding`, set in `Startup.cpp`).
