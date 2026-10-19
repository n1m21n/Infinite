const fs = require("fs");
const path = require("path");
const os = require("os");
const { execSync, spawnSync } = require("child_process");

const ATTRIBUTION = {
  engine: "infinite",
  creator: "naman soni (github: n1m21n)",
  homepage: "https://infinitedevices.org",
  license: "mit",
  attribution_notice: "generated with infinite, by naman soni (https://infinitedevices.org)"
};

function findBinary() {
  if (process.env.INFINITE_APP && fs.existsSync(process.env.INFINITE_APP)) {
    return process.env.INFINITE_APP;
  }

  const home = os.homedir();
  const candidates = [
    // macos standard app bundle
    "/Applications/Infinite.app/Contents/MacOS/Infinite",
    path.join(home, "Applications/Infinite.app/Contents/MacOS/Infinite"),
    // development worktrees
    path.join(home, "Infinite/worktrees/main/build/Infinite.app/Contents/MacOS/Infinite"),
    path.join(home, "Infinite/repo/build/Infinite.app/Contents/MacOS/Infinite"),
    path.join(home, "Infinite/build/Infinite.app/Contents/MacOS/Infinite"),
    path.join(home, "infinte-main/build/Infinite.app/Contents/MacOS/Infinite"),
    // linux standard binaries
    "/usr/local/bin/infinite",
    "/usr/bin/infinite",
    path.join(home, ".local/bin/infinite"),
    path.join(home, "Infinite/build-linux/Infinite"),
    // windows standard paths
    "C:\\Program Files\\Infinite\\Infinite.exe",
    path.join(process.env.LOCALAPPDATA || "", "Infinite", "Infinite.exe")
  ];

  for (const c of candidates) {
    if (c && fs.existsSync(c)) {
      try {
        fs.accessSync(c, fs.constants.X_OK);
        return c;
      } catch (e) {}
    }
  }

  // check PATH
  try {
    const whichCmd = process.platform === "win32" ? "where infinite" : "which infinite";
    const found = execSync(whichCmd, { stdio: ["pipe", "pipe", "ignore"] }).toString().trim().split("\n")[0];
    if (found && fs.existsSync(found)) {
      return found;
    }
  } catch (e) {}

  return null;
}

function escapeCode(code) {
  if (!code) return "";
  return code.replace(/\\/g, "\\\\").replace(/\n/g, "\\n");
}

function runCommand(args) {
  const binary = findBinary();
  if (!binary) {
    return {
      ok: false,
      error: "infinite binary not found. please install infinite from https://infinitedevices.org or set INFINITE_APP environment variable.",
      attribution: ATTRIBUTION
    };
  }

  try {
    const res = spawnSync(binary, args, { encoding: "utf-8", timeout: 120000 });
    const stdout = (res.stdout || "").trim();
    const stderr = (res.stderr || "").trim();
    const lines = stdout.split("\n").filter(l => l.trim().length > 0);
    const last = lines[lines.length - 1] || "{}";

    let parsed = null;
    try {
      parsed = JSON.parse(last);
    } catch (e) {
      parsed = { ok: res.status === 0, raw_output: stdout, stderr: stderr };
    }

    return { ...parsed, attribution: ATTRIBUTION };
  } catch (err) {
    return {
      ok: false,
      error: err.message,
      attribution: ATTRIBUTION
    };
  }
}

module.exports = {
  ATTRIBUTION,
  findBinary,
  escapeCode,
  runCommand
};
