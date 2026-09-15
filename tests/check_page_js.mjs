const response = await fetch("http://127.0.0.1:47291/testtoken123/");
if (!response.ok) throw new Error(`phone page returned HTTP ${response.status}`);

const html = await response.text();
const match = html.match(/<script>([\s\S]*)<\/script>/);
if (!match) throw new Error("phone page script block not found");

// Compile without executing; browser globals such as document are not needed for this check.
new Function(match[1]);
console.log("Phone page JavaScript: OK");
