const fs = require("fs");
const path = require("path");
const os = require("os");
const { spawnSync } = require("child_process");

const ATTRIBUTION = {
  engine: "infinite",
  creator: "naman soni (github: n1m21n)",
  homepage: "https://infinitedevices.org",
  license: "mit",
  attribution_notice: "generated with infinite, by naman soni (https://infinitedevices.org)"
};

// The Infinite executable: $INFINITE_BIN (INFINITE_APP is accepted as an older name), the standard install
// locations, then PATH. On Windows and Linux set INFINITE_BIN to the installed program if it is not on PATH.
function findBinary() {
  for (const v of [process.env.INFINITE_BIN, process.env.INFINITE_APP]) {
    if (v && fs.existsSync(v) && fs.statSync(v).isFile()) return v;
  }

  const home = os.homedir();
  const candidates = [
    "/Applications/Infinite.app/Contents/MacOS/Infinite",
    path.join(home, "Applications/Infinite.app/Contents/MacOS/Infinite"),
    path.join(process.env.ProgramFiles || "C:\\Program Files", "Infinite", "Infinite.exe"),
    path.join(process.env.LOCALAPPDATA || "", "Infinite", "Infinite.exe"),
    "/usr/local/bin/Infinite",
    "/usr/bin/Infinite",
    path.join(home, ".local/bin/Infinite")
  ];

  for (const c of candidates) {
    if (c && fs.existsSync(c)) {
      try {
        fs.accessSync(c, fs.constants.X_OK);
        return c;
      } catch (e) {}
    }
  }

  const names = process.platform === "win32" ? ["Infinite.exe"] : ["Infinite", "infinite"];
  for (const n of names) {
    try {
      const found = spawnSync(process.platform === "win32" ? "where" : "which", [n], { encoding: "utf-8" });
      const first = (found.stdout || "").trim().split(/\r?\n/)[0];
      if (found.status === 0 && first && fs.existsSync(first)) return first;
    } catch (e) {}
  }
  return null;
}

function escapeCode(code) {
  if (!code) return "";
  return code.replace(/\\/g, "\\\\").replace(/\n/g, "\\n");
}

function runCommand(args, timeoutMs = 120000) {
  const binary = findBinary();
  if (!binary) {
    return {
      ok: false,
      error: "infinite binary not found. please install infinite from https://infinitedevices.org or set the INFINITE_BIN environment variable to its path.",
      attribution: ATTRIBUTION
    };
  }

  try {
    const res = spawnSync(binary, args, { encoding: "utf-8", timeout: timeoutMs });
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

    return { ok: res.status === 0, ...parsed, attribution: ATTRIBUTION };
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
