'use strict';
// Writes hwtests/refresh-store5/main.cpp from hwtests/refresh-store's harness: node hwtests/refresh-store5/gen.js
const fs = require('fs'), path = require('path');
const src = fs.readFileSync(path.join(__dirname, '..', 'refresh-store', 'main.cpp'), 'utf8');
const HEAD = `/*
 * cpu/refresh-store5 - what follows a slow read that waited behind a store: a bus access at each distance, a RAM load, another slow read, a read that went ahead of a
 * second store (2026-10-30). refresh-store's harness.
 *
 * WHY. hwtests/refresh-store4 (the SCPH-5501 #2) and refresh-store2: sw; k nops; R; 2 nops; lhu T2 read a cycle less than the core at k = 0..2, where the read waits, for
 * SPUSTAT and the CD alike, in or out of a delay slot, and exactly the core at k >= 3. experiments/_mkwrb220.js gives the load behind such a read the cycle a writer of its
 * register in the delay slot already had (slowEarly), and reads both programs exact at every phase, and refresh-io2 on all seven consoles. But the only follower those
 * programs hold is Timer 2 three on. Unmeasured: the same at one, two, four and more on; a RAM load there; a second slow read; and a read that WENT AHEAD of a store
 * behind it, where the writer takes 2 (SLOW_JUMP), not 1.
 *
 * WHAT IT DOES. refresh-store's harness and calibration, each case at the 72 phases from F - 32 to F + 39. R = lhu SPUSTAT. The bracket's closing Timer 2 read is the follower
 * of the first rows, each with its control:
 *   Z0 Z1 Z3 Z4 Z6     R; k nops                       no store
 *   A0 A1 A3 A4 A6     sw A; R; k nops                 the closing read at k + 1 on (A2 is refresh-store2's P0)
 *   B0 B2              sw A; R; k nops; lw A+8         a RAM load one and three on, then the closing read
 *   D2                 sw A; R; 2 nops; R              a second slow read
 *   S2                 sw A; sw A+16K; R; 2 nops       two pages: the read goes ahead of the second store
 *   S0                 sw A; sw A+16K; R
 *   V2                 sw A; R; nop; addu t2, t2; nop  a reader of the value two on, the closing read behind it
 *   W2                 sw A; R; andi t2, t2; 2 nops    a writer in the delay slot, the closing read three on
 * Each prints its commonest cost, then the cost at each phase from the refresh, sixteen to a line ('.' where no trial landed).
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: \`node hwtests/run.js refresh-store5 --frames=3000 --quiet=600\`.
 */
`;
const nops = (k) => Array(k).fill('nop');
const R = 'lhu $t2, 0x1dae($t4)', ST = 'sw $zero, 0(%1)', ST2 = 'sw $zero, 0x4000(%1)';
const pad = (s) => s.padEnd(30);
const CASES = [
  ...[0, 1, 3, 4, 6].map((k) => [`caseZ${k}`, pad(`Z${k} R; ${k} nops`), [R, ...nops(k)]]),
  ...[0, 1, 3, 4, 6].map((k) => [`caseA${k}`, pad(`A${k} sw; R; ${k} nops`), [ST, R, ...nops(k)]]),
  ['caseB0', pad('B0 sw; R; lw'), [ST, R, 'lw $t2, 8(%1)']],
  ['caseB2', pad('B2 sw; R; 2 nops; lw'), [ST, R, 'nop', 'nop', 'lw $t2, 8(%1)']],
  ['caseD2', pad('D2 sw; R; 2 nops; R'), [ST, R, 'nop', 'nop', R]],
  ['caseS2', pad('S2 sw; sw far; R; 2 nops'), [ST, ST2, R, 'nop', 'nop']],
  ['caseS0', pad('S0 sw; sw far; R'), [ST, ST2, R]],
  ['caseV2', pad('V2 sw; R; nop; reader; nop'), [ST, R, 'nop', 'addu $t2, $t2, $zero', 'nop']],
  ['caseW2', pad('W2 sw; R; writer; 2 nops'), [ST, R, 'andi $t2, $t2, 0xffff', 'nop', 'nop']],
];
let s = src.slice(src.indexOf('#include <common.h>'));
const a = s.indexOf('CASE(caseA,'), b = s.indexOf('typedef uint32_t (*CaseFn)');
s = s.slice(0, a) + CASES.map(([n, , body]) => `CASE(${n}, ${body.map((l) => `"${l}\\n"`).join(' ')})`).join('\n') + '\n\n' + s.slice(b);
const r0 = s.indexOf('    report("A  one store'), r1 = s.indexOf('\n', s.indexOf('    report("R  two loads'));
s = s.slice(0, r0) + CASES.map(([n, t]) => `    report("${t}", ${n}, F);`).join('\n') + s.slice(r1);
s = s.replace('printf("\\ncpu/refresh-store\\n");', 'printf("\\ncpu/refresh-store5\\n");')
  .replace('What a DRAM refresh costs a run of RAM stores at each phase from it (Timer 2 cycles between the two T2 reads).', 'What follows a slow read that waited behind a store, at each phase from a refresh (Timer 2 cycles).');
fs.writeFileSync(path.join(__dirname, 'main.cpp'), HEAD + s);
console.log('main.cpp written');
