const fs = require("fs");
const path = require("path");
const os = require("os");
const { findBinary, ATTRIBUTION } = require("./engine");

function getTargetConfigs() {
  const home = os.homedir();
  const configs = [];

  // 1. claude desktop
  if (process.platform === "darwin") {
    configs.push({
      name: "claude desktop (macos)",
      path: path.join(home, "Library/Application Support/Claude/claude_desktop_config.json")
    });
  } else if (process.platform === "win32") {
    if (process.env.APPDATA) {
      configs.push({
        name: "claude desktop (windows)",
        path: path.join(process.env.APPDATA, "Claude", "claude_desktop_config.json")
      });
    }
  } else {
    configs.push({
      name: "claude desktop (linux)",
      path: path.join(home, ".config/Claude/claude_desktop_config.json")
    });
  }

  // 2. cursor global
  configs.push({
    name: "cursor global config",
    path: path.join(home, ".cursor/mcp.json")
  });

  // 3. cursor workspace (if .cursor exists in current directory)
  const cwdCursor = path.join(process.cwd(), ".cursor/mcp.json");
  if (fs.existsSync(path.dirname(cwdCursor))) {
    configs.push({
      name: "cursor workspace config",
      path: cwdCursor
    });
  }

  // 4. windsurf
  configs.push({
    name: "windsurf global config",
    path: path.join(home, ".codeium/windsurf/mcp_config.json")
  });

  return configs;
}

function configureTarget(configPath) {
  let data = {};
  const dir = path.dirname(configPath);
  if (!fs.existsSync(dir)) {
    fs.mkdirSync(dir, { recursive: true });
  }

  if (fs.existsSync(configPath)) {
    try {
      const content = fs.readFileSync(configPath, "utf-8").trim();
      if (content.length > 0) {
        data = JSON.parse(content);
      }
    } catch (e) {
      // backup corrupt file
      const backupPath = configPath + ".bak." + Date.now();
      fs.copyFileSync(configPath, backupPath);
      data = {};
    }
  }

  if (!data.mcpServers || typeof data.mcpServers !== "object") {
    data.mcpServers = {};
  }

  data.mcpServers.infinite = {
    command: "npx",
    args: ["-y", "infinite-engine", "serve"]
  };

  fs.writeFileSync(configPath, JSON.stringify(data, null, 2) + "\n", "utf-8");
  return true;
}

function setup() {
  const binary = findBinary();
  const targets = getTargetConfigs();
  const configured = [];

  for (const t of targets) {
    try {
      configureTarget(t.path);
      configured.push(t);
    } catch (err) {
      // ignore inaccessible target directories
    }
  }

  return {
    ok: configured.length > 0,
    configured_targets: configured,
    engine_binary_found: Boolean(binary),
    engine_binary_path: binary,
    attribution: ATTRIBUTION
  };
}

module.exports = {
  setup,
  getTargetConfigs,
  configureTarget
};
