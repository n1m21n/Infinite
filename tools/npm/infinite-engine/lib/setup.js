// Adds the Infinite MCP server to the AI apps found on this machine.
// Safe by default: only apps that are already installed, a backup before any change, no edit to a file that
// cannot be read back exactly (comments, trailing commas), and no overwrite of an existing "infinite" entry.
const fs = require("fs");
const path = require("path");
const os = require("os");
const { findBinary, ATTRIBUTION } = require("./engine");

// app: the folder whose presence means the app is installed; file: the config to edit
function getTargets(opts = {}) {
  const home = opts.home || os.homedir();
  const plat = opts.platform || process.platform;
  const list = [];
  if (plat === "darwin") {
    list.push({ id: "claude", name: "claude desktop", app: path.join(home, "Library/Application Support/Claude"), file: path.join(home, "Library/Application Support/Claude/claude_desktop_config.json") });
  } else if (plat === "win32") {
    const appdata = opts.appdata || process.env.APPDATA;
    if (appdata) list.push({ id: "claude", name: "claude desktop", app: path.join(appdata, "Claude"), file: path.join(appdata, "Claude", "claude_desktop_config.json") });
  } else {
    list.push({ id: "claude", name: "claude desktop", app: path.join(home, ".config/Claude"), file: path.join(home, ".config/Claude/claude_desktop_config.json") });
  }
  list.push({ id: "cursor", name: "cursor (global)", app: path.join(home, ".cursor"), file: path.join(home, ".cursor/mcp.json") });
  const cwd = opts.cwd || process.cwd();
  if (fs.existsSync(path.join(cwd, ".cursor"))) list.push({ id: "cursor", name: "cursor (this folder)", app: path.join(cwd, ".cursor"), file: path.join(cwd, ".cursor/mcp.json") });
  list.push({ id: "windsurf", name: "windsurf", app: path.join(home, ".codeium"), file: path.join(home, ".codeium/windsurf/mcp_config.json") });
  return list;
}

// Claude Desktop on Windows cannot start npx directly; it needs cmd /c.
function serverEntry(plat = process.platform) {
  return plat === "win32"
    ? { command: "cmd", args: ["/c", "npx", "-y", "infinite-engine", "serve"] }
    : { command: "npx", args: ["-y", "infinite-engine", "serve"] };
}

const sameEntry = (a, b) => JSON.stringify(a) === JSON.stringify(b);

// Returns { status, note } and writes only when status is "added" or "updated" and not a dry run.
function configureTarget(t, opts = {}) {
  const entry = serverEntry(opts.platform);
  let data = {};
  const exists = fs.existsSync(t.file);
  if (exists) {
    const text = fs.readFileSync(t.file, "utf-8");
    if (text.trim()) {
      try {
        data = JSON.parse(text);
      } catch (e) {
        return { status: "skipped", note: "the file is not plain JSON (comments or a trailing comma?). It was left alone. Add this by hand: " + JSON.stringify({ mcpServers: { infinite: entry } }) };
      }
    }
    if (data === null || typeof data !== "object" || Array.isArray(data)) return { status: "skipped", note: "the file is not a JSON object. It was left alone." };
  }
  if (data.mcpServers !== undefined && (data.mcpServers === null || typeof data.mcpServers !== "object" || Array.isArray(data.mcpServers))) {
    return { status: "skipped", note: "mcpServers is not an object. The file was left alone." };
  }
  const current = data.mcpServers && data.mcpServers.infinite;
  if (current && sameEntry(current, entry)) return { status: "unchanged", note: "already set up" };
  if (current && !opts.force) return { status: "skipped", note: "an infinite entry already exists with other settings. Use --force to replace it." };
  if (opts.dryRun) return { status: current ? "would update" : "would add", note: "" };

  data.mcpServers = data.mcpServers || {};
  data.mcpServers.infinite = entry;
  fs.mkdirSync(path.dirname(t.file), { recursive: true });
  let backup = null;
  if (exists) {
    backup = t.file + ".infinite-backup";
    fs.copyFileSync(t.file, backup);
  }
  const tmp = t.file + ".infinite-tmp";
  fs.writeFileSync(tmp, JSON.stringify(data, null, 2) + "\n", "utf-8");
  fs.renameSync(tmp, t.file);
  return { status: current ? "updated" : "added", note: backup ? "previous file kept as " + backup : "" };
}

// opts: { only: "claude"|"cursor"|"windsurf", all, force, dryRun } (home, platform, cwd, appdata exist for tests)
function setup(opts = {}) {
  const results = [];
  for (const t of getTargets(opts)) {
    if (opts.only && t.id !== opts.only) continue;
    const installed = fs.existsSync(t.app);
    if (!installed && !opts.all) {
      results.push({ target: t, status: "not found", note: "not installed here, skipped (use --all to create its config anyway)" });
      continue;
    }
    try {
      results.push({ target: t, ...configureTarget(t, opts) });
    } catch (err) {
      results.push({ target: t, status: "error", note: err.message });
    }
  }
  const binary = findBinary();
  return {
    ok: results.some((r) => ["added", "updated", "unchanged", "would add", "would update"].includes(r.status)),
    results,
    engine_binary_found: Boolean(binary),
    engine_binary_path: binary,
    attribution: ATTRIBUTION
  };
}

module.exports = { setup, getTargets, configureTarget, serverEntry };
