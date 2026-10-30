'use strict';
// Writes hwtests/refresh-store4/main.cpp from hwtests/refresh-store's harness: node hwtests/refresh-store4/gen.js
const fs = require('fs'), path = require('path');
const src = fs.readFileSync(path.join(__dirname, '..', 'refresh-store', 'main.cpp'), 'utf8');
const HEAD = `/*
 * cpu/refresh-store4 - a slow read behind a store at more distances, the store in and out of the bracket read's delay slot, and a load right behind a
 * delay-slot store (2026-10-30). refresh-store's harness.
 *
 * WHY. v0.377.220 left two shapes a cycle off. refresh-store2 on the SCPH-5501 #2: sw; k nops; lhu SPUSTAT; 2 nops reads 28 28 28 29 for k = 0 .. 3, this
 * core 29 29 29 29, and refresh-io2's P3 ramp (all seven consoles) 34 33 .. against 35 34 ... The core's trace (experiments/rs2trace.js) says wait + k is
 * max(23, 19 + k) at the read; the console's four numbers fit max(22, 20 + k), a wait a cycle shorter and a floor a cycle higher, or something else: k stops at
 * 3, where the two lines cross. cpu-loop-tick12 / 13's one-store rows (sw; R; ...) are the core's to 0.05 a turn with this wait, and a cycle off every slow read's wait
 * broke them (the race, STATUS "The race read the fourth rule wrong ..."). What differs: their store follows a nop, the P rows' sits in the bracket's Timer 2
 * read's delay slot; and they run around a loop. And refresh-store3: sw; on-die read; sw; lw reads 24, the core 23, with a nop before the lw both 23: a nop
 * makes it FASTER on the console.
 *
 * WHAT IT DOES. refresh-store's harness and calibration, each case at the 72 phases from F - 32 to F + 39:
 *   N0 N3       k nops; lhu SPUSTAT; 2 nops               no store: the read alone, k = 0, 3
 *   P4 P5 P6 P8 sw A; k nops; lhu SPUSTAT; 2 nops         where the P rows' line goes past k = 3
 *   Q0 .. Q3    nop; sw A; k nops; lhu SPUSTAT; 2 nops    the store out of the bracket's delay slot
 *   C0 .. C3    sw A; k nops; lbu CD status; 2 nops       the CD's register, the other slow read
 *   T4 T5       sw A; lhu T2; sw A+8; 2 / 3 nops; lw A+8  refresh-store3's T1 (0 nops, 24) and T3 (1 nop, 23) further on
 * Each prints its commonest cost, then the cost at each phase from the refresh, sixteen to a line ('.' where no trial landed).
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: \`node hwtests/run.js refresh-store4 --frames=3000 --quiet=600\`.
 */
`;
const nops = (k) => Array(k).fill('nop');
const P = (k) => ['sw $zero, 0(%1)', ...nops(k), 'lhu $t2, 0x1dae($t4)', 'nop', 'nop'];
const Q = (k) => ['nop', 'sw $zero, 0(%1)', ...nops(k), 'lhu $t2, 0x1dae($t4)', 'nop', 'nop'];
const C = (k) => ['sw $zero, 0(%1)', ...nops(k), 'lbu $t2, 0x1800($t4)', 'nop', 'nop'];
const T = (k) => ['sw $zero, 0(%1)', 'lhu $t2, 0x1120($t4)', 'sw $zero, 8(%1)', ...nops(k), 'lw $t2, 8(%1)'];
const pad = (s) => s.padEnd(30);
const CASES = [
  ['caseN0', pad('N0 lhu SPUSTAT'), [...nops(0), 'lhu $t2, 0x1dae($t4)', 'nop', 'nop']],
  ['caseN3', pad('N3 3 nops; lhu SPUSTAT'), [...nops(3), 'lhu $t2, 0x1dae($t4)', 'nop', 'nop']],
  ...[4, 5, 6, 8].map((k) => [`caseP${k}`, pad(`P${k} sw; ${k} nops; lhu SPUSTAT`), P(k)]),
  ...[0, 1, 2, 3].map((k) => [`caseQ${k}`, pad(`Q${k} nop; sw; ${k} nops; SPUSTAT`), Q(k)]),
  ...[0, 1, 2, 3].map((k) => [`caseC${k}`, pad(`C${k} sw; ${k} nops; lbu CD`), C(k)]),
  ['caseT4', pad('T4 sw; lhu T2; sw; 2 nops'), T(2)],
  ['caseT5', pad('T5 sw; lhu T2; sw; 3 nops'), T(3)],
];
let s = src.slice(src.indexOf('#include <common.h>'));
const a = s.indexOf('CASE(caseA,'), b = s.indexOf('typedef uint32_t (*CaseFn)');
s = s.slice(0, a) + CASES.map(([n, , body]) => `CASE(${n}, ${body.map((l) => `"${l}\\n"`).join(' ')})`).join('\n') + '\n\n' + s.slice(b);
const r0 = s.indexOf('    report("A  one store'), r1 = s.indexOf('\n', s.indexOf('    report("R  two loads'));
s = s.slice(0, r0) + CASES.map(([n, t]) => `    report("${t}", ${n}, F);`).join('\n') + s.slice(r1);
s = s.replace('printf("\\ncpu/refresh-store\\n");', 'printf("\\ncpu/refresh-store4\\n");')
  .replace('What a DRAM refresh costs a run of RAM stores at each phase from it (Timer 2 cycles between the two T2 reads).', 'A slow read behind a store at more distances, and a load behind a delay-slot store, at each phase from a refresh (Timer 2 cycles).');
fs.writeFileSync(path.join(__dirname, 'main.cpp'), HEAD + s);
console.log('main.cpp written');
