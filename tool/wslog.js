// wslog.js — capture the device's live log stream over its WebSocket.
//
// The firmware pushes every LOG_* line to all WS clients (webSocket.textAll()),
// which is the only remote log sink besides USB serial. Used by the benchmark
// closed-loop regression runs.
//
// usage: node tool/wslog.js <ip> <outfile> <seconds>
//    e.g. node tool/wslog.js 192.168.124.34 tmp/bm-34.log 90
const fs = require('fs');
const path = require('path');
// `ws` is not a top-level dependency of this repo; it ships inside the web app's
// toolchain, so resolve it from there.
const WebSocket = require(path.join(__dirname, '..', 'src', 'web', 'axe-os', 'node_modules', 'ws'));

const [, , ip, outfile, secondsArg] = process.argv;
if (!ip || !outfile) {
  console.error('usage: node wslog.js <ip> <outfile> <seconds>');
  process.exit(2);
}
const seconds = Number(secondsArg || 60);

fs.mkdirSync(path.dirname(outfile), { recursive: true });
const out = fs.createWriteStream(outfile, { flags: 'w' });
const stamp = () => new Date().toISOString().substring(11, 23);
const write = (s) => out.write(s);

const ws = new WebSocket(`ws://${ip}/ws`);
let lines = 0;

ws.on('open', () => write(`# [${stamp()}] connected to ${ip}\n`));
ws.on('message', (m) => {
  const text = m.toString();
  for (const line of text.split(/\r?\n/)) {
    if (line.length) { write(`[${stamp()}] ${line}\n`); lines++; }
  }
});
ws.on('error', (e) => write(`# [${stamp()}] error: ${e.message}\n`));
ws.on('close', () => write(`# [${stamp()}] closed after ${lines} lines\n`));

setTimeout(() => {
  write(`# [${stamp()}] timeout after ${seconds}s, ${lines} lines\n`);
  try { ws.close(); } catch {}
  out.end(() => process.exit(0));
}, seconds * 1000);
