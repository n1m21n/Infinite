// npm test: runs without the Infinite app. Checks the config writer on a throwaway home and the MCP handshake.
const test = require("node:test");
const assert = require("node:assert");
const fs = require("fs");
const os = require("os");
const path = require("path");
const { spawnSync } = require("child_process");
const { setup, serverEntry } = require("../lib/setup");

const home = () => fs.mkdtempSync(path.join(os.tmpdir(), "ie-test-"));

test("init skips apps that are not installed and writes nothing", () => {
  const h = home();
  const r = setup({ home: h, platform: "linux", cwd: h });
  assert.strictEqual(r.ok, false);
  assert.deepStrictEqual(fs.readdirSync(h), []);
});

test("init adds the server, keeps other servers and makes a backup", () => {
  const h = home();
  fs.mkdirSync(path.join(h, ".cursor"));
  const f = path.join(h, ".cursor/mcp.json");
  fs.writeFileSync(f, JSON.stringify({ mcpServers: { other: { command: "x" } }, theme: 1 }));
  const r = setup({ home: h, platform: "linux", cwd: h });
  assert.ok(r.ok);
  const out = JSON.parse(fs.readFileSync(f, "utf-8"));
  assert.deepStrictEqual(out.mcpServers.infinite, serverEntry("linux"));
  assert.deepStrictEqual(out.mcpServers.other, { command: "x" });
  assert.strictEqual(out.theme, 1);
  assert.ok(fs.existsSync(f + ".infinite-backup"));
});

test("init leaves a file with comments untouched", () => {
  const h = home();
  fs.mkdirSync(path.join(h, ".cursor"));
  const f = path.join(h, ".cursor/mcp.json");
  const text = '{ "mcpServers": { "other": {} }, // mine\n}';
  fs.writeFileSync(f, text);
  const r = setup({ home: h, platform: "linux", cwd: h });
  assert.strictEqual(fs.readFileSync(f, "utf-8"), text);
  assert.strictEqual(r.results.find((x) => x.target.file === f).status, "skipped");
});

test("init does not replace a different infinite entry without --force, and dry run writes nothing", () => {
  const h = home();
  fs.mkdirSync(path.join(h, ".cursor"));
  const f = path.join(h, ".cursor/mcp.json");
  const text = JSON.stringify({ mcpServers: { infinite: { command: "mine" } } });
  fs.writeFileSync(f, text);
  setup({ home: h, platform: "linux", cwd: h });
  assert.strictEqual(fs.readFileSync(f, "utf-8"), text);
  setup({ home: h, platform: "linux", cwd: h, force: true, dryRun: true });
  assert.strictEqual(fs.readFileSync(f, "utf-8"), text);
  setup({ home: h, platform: "linux", cwd: h, force: true });
  assert.deepStrictEqual(JSON.parse(fs.readFileSync(f, "utf-8")).mcpServers.infinite, serverEntry("linux"));
});

test("windows config starts npx through cmd", () => {
  assert.strictEqual(serverEntry("win32").command, "cmd");
});

test("serve answers initialize and lists nine tools", () => {
  const cli = path.join(__dirname, "../bin/cli.js");
  const input = [{ jsonrpc: "2.0", id: 1, method: "initialize", params: {} }, { jsonrpc: "2.0", id: 2, method: "tools/list" }].map((x) => JSON.stringify(x)).join("\n") + "\n";
  const r = spawnSync(process.execPath, [cli, "serve"], { input, encoding: "utf-8", timeout: 10000 });
  const lines = r.stdout.trim().split("\n").map((l) => JSON.parse(l));
  assert.strictEqual(lines[0].result.serverInfo.name, "infinite-engine");
  assert.strictEqual(lines[1].result.tools.length, 9);
});

test("a tool call without the Infinite app reports an error, not success", () => {
  const cli = path.join(__dirname, "../bin/cli.js");
  const call = { jsonrpc: "2.0", id: 3, method: "tools/call", params: { name: "infinite_validate_patch", arguments: { patch_text: "infinite-patch 1\n" } } };
  const r = spawnSync(process.execPath, [cli, "serve"], { input: JSON.stringify(call) + "\n", encoding: "utf-8", timeout: 10000, env: { ...process.env, INFINITE_BIN: "", INFINITE_APP: "", PATH: "/nonexistent", HOME: home() } });
  const out = JSON.parse(JSON.parse(r.stdout.trim()).result.content[0].text);
  assert.strictEqual(out.ok, false);
});

test("the handshake carries credit instructions and every tool result carries the credit", () => {
  const cli = path.join(__dirname, "../bin/cli.js");
  const msgs = [
    { jsonrpc: "2.0", id: 1, method: "initialize", params: {} },
    { jsonrpc: "2.0", id: 2, method: "tools/call", params: { name: "infinite_validate_patch", arguments: { patch_text: "infinite-patch 1\n" } } }
  ];
  const input = msgs.map((x) => JSON.stringify(x)).join("\n") + "\n";
  const r = spawnSync(process.execPath, [cli, "serve"], { input, encoding: "utf-8", timeout: 10000, env: { ...process.env, INFINITE_BIN: "", INFINITE_APP: "", PATH: "/nonexistent", HOME: home() } });
  const lines = r.stdout.trim().split("\n").map((l) => JSON.parse(l));
  assert.match(lines[0].result.instructions, /Naman Soni \(n1m21n\)/);
  const content = lines[1].result.content;
  const body = JSON.parse(content[0].text);
  assert.strictEqual(body.attribution.credit_line, "Infinite, by Naman Soni (n1m21n), https://infinitedevices.org");
  assert.match(content[1].text, /Credit: Infinite, by Naman Soni/);
});
