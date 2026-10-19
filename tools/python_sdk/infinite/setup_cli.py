"""Adds the Infinite MCP server to the AI apps found on this machine (Claude Desktop, Cursor, Windsurf).

    infinite-init [--dry-run] [--only claude|cursor|windsurf] [--all] [--force]

Safe by default: only apps that are already installed, a backup before any change, no edit to a file that cannot
be read back exactly (comments, trailing commas), and no overwrite of an existing "infinite" entry.
"""

import argparse
import json
import os
import shutil
import sys


def targets(home=None, platform=None, cwd=None, appdata=None):
    home = home or os.path.expanduser("~")
    platform = platform or sys.platform
    cwd = cwd or os.getcwd()
    out = []
    if platform == "darwin":
        d = os.path.join(home, "Library", "Application Support", "Claude")
        out.append(("claude", "claude desktop", d, os.path.join(d, "claude_desktop_config.json")))
    elif platform.startswith("win"):
        ad = appdata or os.environ.get("APPDATA")
        if ad:
            d = os.path.join(ad, "Claude")
            out.append(("claude", "claude desktop", d, os.path.join(d, "claude_desktop_config.json")))
    else:
        d = os.path.join(home, ".config", "Claude")
        out.append(("claude", "claude desktop", d, os.path.join(d, "claude_desktop_config.json")))
    d = os.path.join(home, ".cursor")
    out.append(("cursor", "cursor (global)", d, os.path.join(d, "mcp.json")))
    if os.path.isdir(os.path.join(cwd, ".cursor")):
        d = os.path.join(cwd, ".cursor")
        out.append(("cursor", "cursor (this folder)", d, os.path.join(d, "mcp.json")))
    d = os.path.join(home, ".codeium")
    out.append(("windsurf", "windsurf", d, os.path.join(d, "windsurf", "mcp_config.json")))
    return out


def server_entry():
    """From a checkout, run this package's server. Anywhere else, use the self-contained npm server."""
    from . import mcp
    if os.path.exists(mcp.MCP_PATH):
        return {"command": sys.executable, "args": ["-m", "infinite.mcp"]}
    if sys.platform.startswith("win"):
        return {"command": "cmd", "args": ["/c", "npx", "-y", "infinite-engine", "serve"]}
    return {"command": "npx", "args": ["-y", "infinite-engine", "serve"]}


def configure_file(path, entry, dry_run=False, force=False):
    """Returns (status, note). Writes only for status "added" or "updated"."""
    data = {}
    exists = os.path.exists(path)
    if exists:
        with open(path, encoding="utf-8") as f:
            text = f.read()
        if text.strip():
            try:
                data = json.loads(text)
            except ValueError:
                return "skipped", "the file is not plain JSON (comments or a trailing comma?). It was left alone. Add this by hand: " + json.dumps({"mcpServers": {"infinite": entry}})
        if not isinstance(data, dict):
            return "skipped", "the file is not a JSON object. It was left alone."
    servers = data.get("mcpServers")
    if "mcpServers" in data and not isinstance(servers, dict):
        return "skipped", "mcpServers is not an object. The file was left alone."
    current = (servers or {}).get("infinite")
    if current == entry:
        return "unchanged", "already set up"
    if current is not None and not force:
        return "skipped", "an infinite entry already exists with other settings. Use --force to replace it."
    if dry_run:
        return ("would update" if current is not None else "would add"), ""
    data.setdefault("mcpServers", {})["infinite"] = entry
    os.makedirs(os.path.dirname(path), exist_ok=True)
    backup = None
    if exists:
        backup = path + ".infinite-backup"
        shutil.copyfile(path, backup)
    tmp = path + ".infinite-tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)
        f.write("\n")
    os.replace(tmp, path)
    return ("updated" if current is not None else "added"), ("previous file kept as " + backup if backup else "")


def setup(only=None, all_apps=False, dry_run=False, force=False, **where):
    entry = server_entry()
    results = []
    for tid, name, app_dir, path in targets(**where):
        if only and tid != only:
            continue
        if not os.path.isdir(app_dir) and not all_apps:
            results.append((name, path, "not found", "not installed here, skipped (use --all to create its config anyway)"))
            continue
        try:
            status, note = configure_file(path, entry, dry_run, force)
        except OSError as e:
            status, note = "error", str(e)
        results.append((name, path, status, note))
    return results


def main(argv=None):
    ap = argparse.ArgumentParser(prog="infinite-init", description="Add Infinite to Claude Desktop, Cursor and Windsurf.")
    ap.add_argument("--dry-run", action="store_true", help="show what would change and write nothing")
    ap.add_argument("--only", choices=["claude", "cursor", "windsurf"])
    ap.add_argument("--all", action="store_true", help="also create the config for apps that are not installed")
    ap.add_argument("--force", action="store_true", help="replace an existing infinite entry that has other settings")
    a = ap.parse_args(argv)
    results = setup(a.only, a.all, a.dry_run, a.force)
    print("infinite-init (dry run, nothing is written)" if a.dry_run else "infinite-init")
    for name, path, status, note in results:
        print(f"  {status:<12} {name}: {path}" + (f"\n               {note}" if note else ""))
    ok = any(r[2] in ("added", "updated", "unchanged", "would add", "would update") for r in results)
    if not ok:
        print("\nno app was set up. Install Claude Desktop, Cursor or Windsurf first, or run again with --all.")
        return 1
    print("\nrestart the app to load the server. The Infinite app must be installed; set INFINITE_BIN to its path if it is not found.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
