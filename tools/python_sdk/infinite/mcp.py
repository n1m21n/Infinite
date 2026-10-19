"""mcp server entry point for python sdk."""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MCP_PATH = os.path.abspath(os.path.join(HERE, "..", "..", "mcp", "infinite_mcp.py"))


def main():
    if os.path.exists(MCP_PATH):
        # execute infinite_mcp.py
        import runpy
        runpy.run_path(MCP_PATH, run_name="__main__")
    else:
        # fallback to node infinite-engine serve if available
        import subprocess
        subprocess.run(["npx", "-y", "infinite-engine", "serve"])


if __name__ == "__main__":
    main()
