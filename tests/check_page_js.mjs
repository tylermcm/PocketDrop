import { readFileSync } from "node:fs";

// server_test writes the current QR token here; opening it redirects to a phone session.
const token = readFileSync("build/server_test_out/token.txt", "utf8").trim();
const port = process.env.PD_TEST_PORT || "47291";
const response = await fetch(`http://127.0.0.1:${port}/${token}/`);
if (!response.ok) throw new Error(`phone page returned HTTP ${response.status}`);
if (!new URL(response.url).pathname.startsWith("/s/")) throw new Error(`expected a session URL, got ${response.url}`);

const html = await response.text();
const match = html.match(/<script>([\s\S]*)<\/script>/);
if (!match) throw new Error("phone page script block not found");

// Compile without executing; browser globals such as document are not needed for this check.
new Function(match[1]);
console.log("Phone page JavaScript: OK");

// The upload checksums must agree with zlib's CRC-32, including joining chunk CRCs into a file CRC.
import { crc32 } from "node:zlib";
const grab = (name) => {
  const m = match[1].match(new RegExp(`(?:const|async function|function) ${name}[\\s\\S]*?\\n(?=(?:const|async function|function|let) |\\s*$)`));
  if (!m) throw new Error(`page function ${name} not found`);
  return m[0];
};
const crcCode = ["crcTab", "crcOf", "gf2", "gf2sq", "crcJoin", "hex8"].map(grab).join("\n");
const { crcOf, crcJoin, hex8 } = new Function(`${crcCode}\nreturn {crcOf,crcJoin,hex8}`)();
for (const [sizeA, sizeB] of [[1, 1], [1000, 777], [1 << 20, 12345], [8388608, 1 << 20]]) {
  const a = Buffer.alloc(sizeA, 7).map((_, i) => (i * 31) & 255), b = Buffer.alloc(sizeB, 9).map((_, i) => (i * 17) & 255);
  const ca = await crcOf(new Blob([a])), cb = await crcOf(new Blob([b]));
  if (ca !== crc32(a) || cb !== crc32(b)) throw new Error("page CRC-32 differs from zlib");
  if (crcJoin(ca, cb, sizeB) !== crc32(Buffer.concat([a, b]))) throw new Error("page CRC join differs from zlib");
}
if (hex8(255) !== "000000ff") throw new Error("hex8");
console.log("Phone page checksums: OK");
