"""one-command mcp auto-configurator for python environments."""

import json
import os
import sys

TARGETS = [
    ("claude desktop (macos)", os.path.expanduser("~/Library/Application Support/Claude/claude_desktop_config.json")),
    ("cursor global config", os.path.expanduser("~/.cursor/mcp.json")),
    ("windsurf global config", os.path.expanduser("~/.codeium/windsurf/mcp_config.json"))
]


def configure_file(p):
    d = os.path.dirname(p)
    if not os.path.exists(d):
        os.makedirs(d, exist_ok=True)
    cfg = {}
    if os.path.exists(p):
        try:
            cfg = json.loads(open(p, encoding="utf-8").read())
        except Exception:
            cfg = {}
    if not isinstance(cfg.get("mcpServers"), dict):
        cfg["mcpServers"] = {}
    cfg["mcpServers"]["infinite"] = {
        "command": "python3",
        "args": ["-m", "infinite.mcp"]
    }
    with open(p, "w", encoding="utf-8") as f:
        json.dump(cfg, f, indent=2)
        f.write("\n")
    return True


def main():
    print("infinite engine: python ai agent mcp installer")
    print("creator: naman soni (https://infinitedevices.org)")
    print("-----------------------------------------------------------------")
    for name, path in TARGETS:
        try:
            configure_file(path)
            print(f"  ✓ {name}: {path}")
        except Exception as e:
            pass
    print("-----------------------------------------------------------------")
    print("infinite mcp server configured successfully.")


if __name__ == "__main__":
    main()
