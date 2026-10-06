# Prior Art & Acknowledgements

Infinite is its own codebase, but its ideas stand on a long line of tools that came before it. We want to name them plainly.

**Node-based audio and visual environments.** Patching signal through a graph of modules is the tradition of [Max/MSP and Jitter](https://cycling74.com/), [Pure Data](https://puredata.info/), [Reaktor](https://www.native-instruments.com/en/products/komplete/synths/reaktor-6/), [Bitwig's The Grid](https://www.bitwig.com/the-grid/), [VCV Rack](https://vcvrack.com/) and [BespokeSynth](https://www.bespokesynth.com/). Mixing audio, video and 3D in one graph is the territory of [TouchDesigner](https://derivative.ca/), Max/Jitter and [Houdini](https://www.sidefx.com/). Infinite's first building blocks (the node registry, the base node interface, pull-based per-frame cooking, the OpenGL render-pass helpers and a single node shell that hosts many effect types) were written with BespokeSynth's source open as a design reference, under an explicit instruction not to copy its code. The node registry and shader compiler have since been rewritten from scratch, and the rest has grown far past those first versions.

**Geometry.** The per-element attribute model (`P`, `N`, `uv`, `Cd`) follows Houdini's SOP conventions. Several geometry nodes follow behaviour a [Blender Geometry Nodes](https://docs.blender.org/manual/en/latest/modeling/geometry_nodes/) user would expect, and some compositing nodes are named after their TouchDesigner equivalents (Fit, for example).

**Field.** Field's building blocks all have precedent:
- per-element kernels over geometry attributes come from Houdini VEX, and an early draft even used VEX's `@` sigil before it was removed;
- running parts of a program at different rates follows Faust's computation levels and the rate model in V. Norilo, *"Kronos: A Declarative Metaprogramming Language for Digital Signal Processing"*, Computer Music Journal 39:4 (2015);
- `param` declarations echo Houdini's `chf()` and Cabbage's markup;
- pixel kernels compile to GLSL in the way Shadertoy-style tools work.

What Field adds is putting these together: **one kernel syntax whose domain (graph, frame, element, pixel or sample) is inferred from what the code touches**, compiled to the matching backend and wired into a live audiovisual node graph as real, modulatable, savable nodes.

**How it is built.** Infinite is developed by n1m21n with extensive AI coding assistance from Anthropic's Claude. AI-assisted commits carry a `Co-Authored-By` trailer in the git history.

**Third-party libraries** are vendored under `external/` and `third_party/`, each with its own licence (Dear ImGui, imgui-node-editor, miniaudio, dr_libs, stb, nlohmann/json, tinyfiledialogs and others). The Steinberg VST3 SDK is covered under [License](#license) below, and libraries that carry extra distribution obligations (FFmpeg, x264, Signalsmith Stretch) are documented in [THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES).

People whose work or ideas shaped Infinite are credited in [CONTRIBUTING.md](CONTRIBUTING.md#community-and-contributors).
