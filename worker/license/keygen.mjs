// node keygen.mjs <out-dir> : makes an Ed25519 pair. private.b64 (PKCS8) goes to the Worker secret, never into git; public.hex goes into the plugin.
import { generateKeyPairSync } from "node:crypto";
import { writeFileSync, mkdirSync } from "node:fs";
const dir = process.argv[2]; if (!dir) { console.error("usage: node keygen.mjs <out-dir>"); process.exit(1); }
mkdirSync(dir, { recursive: true });
const { publicKey, privateKey } = generateKeyPairSync("ed25519");
writeFileSync(dir + "/private.b64", privateKey.export({ type: "pkcs8", format: "der" }).toString("base64"), { mode: 0o600 });
writeFileSync(dir + "/public.hex", publicKey.export({ type: "spki", format: "der" }).subarray(-32).toString("hex"));
console.log("wrote", dir + "/private.b64 and public.hex");
