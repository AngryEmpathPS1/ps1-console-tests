'use strict';
/*
 * hwtests/at-romloop/mkblob.js (2026-11-23, the cold-io session) - the loop blob for the cart's flash, cut out of the built test
 * itself (hwtests/build/at-romloop.exe: blob.h's RL_BLOB, found by its 'AERLOOP1' magic), so the cart and the test can never
 * disagree; and an emulator-only cart image with it in.
 *   node hwtests/at-romloop/mkblob.js [--out=hwtests/at-romloop/blob.bin] [--image=<file> --at=0x10000]
 * blob.bin is what goes in the cart image: anywhere the test can read it - 1F000000h .. 1F07FFFFh, on a 16-byte boundary (it scans for it; a Datel image's second 128 KB is its second chip, at 1F040000h).
 * --image writes a 256 KB image, FFh but for the blob at --at, for `node hwtests/run.js at-romloop --cart=<image>`.
 */
const fs = require('fs'), path = require('path');
const argv = process.argv.slice(2);
const opt = (k, d) => { const a = argv.find((x) => x.startsWith('--' + k + '=')); return a ? a.slice(k.length + 3) : d; };
const exe = fs.readFileSync(path.join(__dirname, '..', 'build', 'at-romloop.exe'));
const magic = Buffer.from('AERLOOP1', 'latin1');
const at = exe.indexOf(magic);
if (at < 0 || exe.indexOf(magic, at + 1) >= 0) throw new Error('the magic must occur exactly once in the exe');
const n = exe.readUInt32LE(at + 8), words = 12 + n * 12;
const blob = exe.subarray(at, at + words * 4);
const out = opt('out', path.join(__dirname, 'blob.bin'));
fs.writeFileSync(out, blob);
console.log(`${out}: ${blob.length} bytes, ${n} functions`);
if (opt('image')) {
  const img = Buffer.alloc(0x40000, 0xFF), off = parseInt(opt('at', '0x10000'));
  if (off & 15) throw new Error('--at must be a multiple of 16');
  blob.copy(img, off);
  fs.writeFileSync(opt('image'), img);
  console.log(`${opt('image')}: 256 KB, the blob at ${off.toString(16)}h`);
}
