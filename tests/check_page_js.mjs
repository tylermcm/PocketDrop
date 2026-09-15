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
