#!/usr/bin/env python3
"""infinite awesome-list pr generator.

what: formats compliant markdown additions and pr descriptions for top github
      awesome lists in clean lowercase without emojis.
"""

import argparse
import sys

LISTS = {
    "creative-coding": {
        "repo": "teropa/awesome-creative-coding",
        "section": "visual programming environments",
        "entry": "- [infinite](https://infinitedevices.org/) - free, open-source audiovisual modular daw and node canvas with real-time gpu shaders, procedural 3d, and vst3 plugin hosting. (macos, windows, linux) `mit/gplv3`",
        "pr_title": "add infinite to visual programming environments",
        "pr_body": """
infinite is a free, open-source modular audiovisual workstation (c++/opengl core) that bridges real-time gpu compositing, procedural 3d geometry/physics, and a sample-accurate modular audio engine with native au/vst3 plugin hosting on one infinite canvas. it features its own domain-inferred kernel language (field) for live coding shaders and dsp.

- website: https://infinitedevices.org/
- repo: https://github.com/n1m21n/infinite
- license: mit / gplv3
- platforms: macos, windows, linux
""".strip(),
    },

    "vj": {
        "repo": "cblgh/awesome-vj",
        "section": "software / live visuals",
        "entry": "- [infinite](https://infinitedevices.org/) - open-source real-time audiovisual modular workstation with glsl shaders, 3d geometry, syphon/spout, ndi, and vst3 hosting.",
        "pr_title": "add infinite to software / live visuals",
        "pr_body": """
infinite is a free and open-source real-time audiovisual workstation built for live performance and installation work. features include syphon/spout zero-copy video i/o, ndi network video, hardware-accelerated video playback, 300+ modular nodes, and a dual synchronized audio-visual dag engine.

- link: https://infinitedevices.org/
- repository: https://github.com/n1m21n/infinite
""".strip(),
    },

    "audio-dsp": {
        "repo": "koreader/awesome-audio-dsp",
        "section": "modular / workstations",
        "entry": "- [infinite](https://infinitedevices.org/) - real-time audiovisual modular workstation with au/vst3 hosting, physical modeling synthesis, granular engine, and sample-accurate graph.",
        "pr_title": "add infinite modular audiovisual workstation",
        "pr_body": """
infinite is an open-source modular daw featuring a sample-accurate audio graph, multi-format plugin hosting (au, vst3), physical modeling resonators, granular synthesis, and an embedded dsl for per-sample dsp kernels.

- website: https://infinitedevices.org/
- repo: https://github.com/n1m21n/infinite
""".strip(),
    },

    "livecoding": {
        "repo": "toplap/awesome-livecoding",
        "section": "visual / audiovisual systems",
        "entry": "- [infinite](https://infinitedevices.org/) - real-time node canvas and field language kernel environment for live coding shaders, geometry deformers, and dsp.",
        "pr_title": "add infinite to live coding systems",
        "pr_body": """
infinite includes an embedded programming language called field where short kernels compile on-the-fly to gpu shaders, mesh deformers, or sample-level dsp depending on context.

- link: https://infinitedevices.org/
- source: https://github.com/n1m21n/infinite
""".strip(),
    },

    "cpp": {
        "repo": "fffaraz/awesome-cpp",
        "section": "audio and multimedia",
        "entry": "- [infinite](https://infinitedevices.org/) - open-source c++ modular audiovisual workstation with real-time opengl pipeline, audio graph, and vst3/au hosting.",
        "pr_title": "add infinite to audio and multimedia",
        "pr_body": """
infinite is a large-scale modern c++ multimedia application with custom opengl 3.2 core renderer, pull-based audio engine, and multi-platform support.

- source: https://github.com/n1m21n/infinite
- site: https://infinitedevices.org/
""".strip(),
    }
}


def print_list_item(key, item):
    github_pr_url = f"https://github.com/{item['repo']}/compare"
    print("-----------------------------------------------------------------")
    print(f"target repo: https://github.com/{item['repo']}")
    print(f"target section: {item['section']}")
    print("-----------------------------------------------------------------")
    print("\nmarkdown line to insert:")
    print("-----------------------------------------------------------------")
    print(item["entry"])
    print("-----------------------------------------------------------------")
    print("\npr title:")
    print(item["pr_title"])
    print("\npr description:")
    print(item["pr_body"])
    print("\nurl to open pr:")
    print(f"{github_pr_url}\n")


def main():
    parser = argparse.ArgumentParser(description="infinite awesome-list pr generator")
    parser.add_argument("--list", default="all", choices=list(LISTS.keys()) + ["all"],
                        help="specific list to generate or 'all'")

    args = parser.parse_args()

    if args.list == "all":
        print("generating compliant prs for top awesome lists:\n")
        for k, v in LISTS.items():
            print_list_item(k, v)
    else:
        print_list_item(args.list, LISTS[args.list])


if __name__ == "__main__":
    main()
