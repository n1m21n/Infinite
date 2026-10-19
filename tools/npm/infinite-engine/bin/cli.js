#!/usr/bin/env node

const { startServer } = require("../lib/server");
const { setup } = require("../lib/setup");
const { findBinary, runCommand, ATTRIBUTION } = require("../lib/engine");

const args = process.argv.slice(2);
const command = (args[0] || "").toLowerCase();
const flag = (n) => args.includes("--" + n);
const value = (n) => { const i = args.indexOf("--" + n); return i >= 0 ? args[i + 1] : undefined; };

const HELP = `infinite-engine: the Infinite audiovisual engine as an MCP server

usage:
  npx -y infinite-engine init [options]   add Infinite to Claude Desktop, Cursor and Windsurf
  npx -y infinite-engine serve            run the MCP server on stdin and stdout
  npx -y infinite-engine status           show which Infinite program was found

init options:
  --dry-run          show what would change and write nothing
  --only <app>       claude, cursor or windsurf
  --all              also create the config for apps that are not installed
  --force            replace an existing "infinite" entry that has other settings

Needs the Infinite app (https://infinitedevices.org). If it is not found, set INFINITE_BIN to its path.`;

if (command === "init" || command === "setup" || command === "install") {
  const only = value("only");
  if (only && !["claude", "cursor", "windsurf"].includes(only)) {
    console.error("--only takes claude, cursor or windsurf");
    process.exit(2);
  }
  const res = setup({ dryRun: flag("dry-run"), only, all: flag("all"), force: flag("force") });
  console.log(flag("dry-run") ? "infinite-engine init (dry run, nothing is written)" : "infinite-engine init");
  for (const r of res.results) {
    console.log(`  ${r.status.padEnd(12)} ${r.target.name}: ${r.target.file}${r.note ? "\n               " + r.note : ""}`);
  }
  console.log("");
  if (res.engine_binary_found) {
    console.log(`infinite program: ${res.engine_binary_path}`);
  } else {
    console.log("infinite program: not found. Install it from https://infinitedevices.org, or set INFINITE_BIN to its path.");
  }
  if (!res.ok) {
    console.log("\nno app was set up. Install Claude Desktop, Cursor or Windsurf first, or run again with --all.");
    process.exit(1);
  }
  console.log("\nrestart the app to load the server. Nine tools are available; see the README for the list.");
  process.exit(0);
}

if (command === "status" || command === "check") {
  const bin = findBinary();
  if (bin) {
    console.log(`infinite program: ${bin}`);
    const probe = runCommand(["--version"]);
    console.log(`version: ${probe.raw_output || "unknown"}`);
    process.exit(0);
  }
  console.log("infinite program not found. Install it from https://infinitedevices.org, or set INFINITE_BIN to its path.");
  process.exit(1);
}

if (command === "help" || command === "--help" || command === "-h") {
  console.log(HELP);
  process.exit(0);
}

if (command === "serve" || (command === "" && !process.stdin.isTTY)) {
  startServer();
} else {
  console.error(command ? `unknown command: ${command}\n` : "");
  console.error(HELP);
  process.exit(command ? 2 : 0);
}
