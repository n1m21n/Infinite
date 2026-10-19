#!/usr/bin/env node

const { startServer } = require("../lib/server");
const { setup } = require("../lib/setup");
const { findBinary, runCommand, ATTRIBUTION } = require("../lib/engine");

const args = process.argv.slice(2);
const command = (args[0] || "").toLowerCase();

if (command === "init" || command === "setup" || command === "install") {
  console.log("infinite engine: one-command ai agent installer");
  console.log("creator: naman soni (https://infinitedevices.org)");
  console.log("-----------------------------------------------------------------");

  const res = setup();
  console.log("mcp configuration successfully installed to:");
  for (const t of res.configured_targets) {
    console.log(`  ✓ ${t.name}: ${t.path}`);
  }

  console.log("\nengine detection status:");
  if (res.engine_binary_found) {
    console.log(`  ✓ infinite c++ binary: ${res.engine_binary_path}`);
  } else {
    console.log("  ! infinite desktop binary not found in standard paths.");
    console.log("    you can download the pre-built installer from: https://infinitedevices.org");
    console.log("    or set the INFINITE_APP environment variable.");
  }

  console.log("\navailable ai agent tools:");
  console.log("  • infinite_synthesize_field_synth: 48khz audio dsp synthesis");
  console.log("  • infinite_synthesize_field_notes: algorithmic midi & euclidean rhythms");
  console.log("  • infinite_process_field_effect: real-time audio filters and fx");
  console.log("  • infinite_render_sketch3d: procedural 3d geometry in javascript");
  console.log("  • infinite_render_field_primitive: parametric math surfaces");
  console.log("  • infinite_render_field_modifier: 3d mesh deformation");
  console.log("  • infinite_render_field_pixel: glsl & field pixel shaders");
  console.log("  • infinite_validate_patch: modular patch validation");
  console.log("  • infinite_render_video: headless mp4 export");

  console.log("-----------------------------------------------------------------");
  console.log("infinite mcp server is now ready in claude desktop, cursor, and windsurf.");
  process.exit(0);
}

if (command === "status" || command === "check") {
  console.log("infinite engine status check");
  console.log("-----------------------------------------------------------------");
  const bin = findBinary();
  if (bin) {
    console.log(`binary found: ${bin}`);
    const probe = runCommand(["--version"]);
    console.log(`engine version: ${probe.raw_output || "0.6.0"}`);
  } else {
    console.log("infinite binary not found in standard search paths.");
  }
  process.exit(0);
}

if (command === "help" || command === "--help" || command === "-h") {
  console.log("infinite-engine: modular audiovisual engine and mcp server");
  console.log("usage:");
  console.log("  npx infinite-engine init     auto-configure claude desktop, cursor, and windsurf");
  console.log("  npx infinite-engine serve    start the stdio json-rpc mcp server");
  console.log("  npx infinite-engine status   check binary discovery and engine version");
  process.exit(0);
}

// default: if piped or called with 'serve' or without command, run mcp server
startServer();
