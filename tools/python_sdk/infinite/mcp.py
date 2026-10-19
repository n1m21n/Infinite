"""The MCP server from a checkout of the repository: python3 -m infinite.mcp (or the infinite-mcp command).

The server file lives in tools/mcp, outside this package, so it is only found when the package is used from a
checkout (pip install -e tools/python_sdk). From a plain pip install, use the npm server instead:
npx -y infinite-engine serve. infinite-init picks the right one."""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MCP_PATH = os.path.abspath(os.path.join(HERE, "..", "..", "mcp", "infinite_mcp.py"))


def main():
    if not os.path.exists(MCP_PATH):
        sys.stderr.write("infinite-mcp: the server file was not found next to this package (it is only in a checkout of "
                         "https://github.com/n1m21n/Infinite).\nUse the npm server instead: npx -y infinite-engine serve\n")
        return 1
    import runpy
    runpy.run_path(MCP_PATH, run_name="__main__")
    return 0


if __name__ == "__main__":
    sys.exit(main())
