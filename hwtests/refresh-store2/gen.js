'use strict';
// Writes hwtests/refresh-store2/main.cpp from hwtests/refresh-store's harness: node hwtests/refresh-store2/gen.js
const fs = require('fs'), path = require('path');
const src = fs.readFileSync(path.join(__dirname, '..', 'refresh-store', 'main.cpp'), 'utf8');
const HEAD = `/*
 * cpu/refresh-store2 - what a DRAM refresh does to two stores to one page with a gap between them, and to a slow read behind a
 * store, at every offset from it (2026-10-30). refresh-store's harness.
 *
 * WHY. hwtests/refresh-store, refresh-io2 and sw-ioread2 on the consoles (STATUS "sw-ioread2 row by row ...", v0.377.220's
 * entry). sw-ioread2's D rows, sw; nop; nop; sw, sit on the edge WRITE_QUEUE_CONT_LEAD was fitted to: at rest the second
 * store issues 3 cycles before the first word's end and starts a word of its own. This core lets a refresh that delays the
 * first word push the second inside the lead, and it continues the burst: 24 / 25 / 26 at 248 of 58 x 256 row-shifts
 * where every console's sixteen trials read 27 (experiments/rowshift.js). Those sixteen sample a few phases of 256, so that
 * cannot say whether the consoles do it too: refresh-store times back-to-back stores (its D) and two pages (its H), not this
 * shape. And refresh-io2's P3 (sw; 3 nops; lhu SPUSTAT) reads the store ramp a cycle lower than this core, the one row
 * of that program left off.
 *
 * WHAT IT DOES. refresh-store's harness and calibration, each case at the 72 phases from F - 32 to F + 39:
 *   N1  sw A; nop; sw A+4; lw A+4                        a burst with a gap of one
 *   N2  sw A; 2 nops; sw A+4; lw A+4                     sw-ioread2's D shape: no burst at rest
 *   N3  sw A; 3 nops; sw A+4; lw A+4                     one further
 *   J1  sw A; nop; sw A+4; lbu JOY_STAT; nop; its reader; lw A+4
 *   J2  sw A; 2 nops; sw A+4; lbu JOY_STAT; nop; its reader; lw A+4      sw-ioread2's D0r1, ended by the load
 *   K2  sw A; 2 nops; sw A+4; lbu JOY_STAT; 3 nops; its reader; lw A+4   its D0r3
 *   P0 .. P3  sw A; k nops; lhu SPUSTAT; 2 nops          refresh-io2's P3 at k = 0 .. 3
 * Each prints its commonest cost, then the cost at each phase from the refresh, sixteen to a line ('.' where no trial
 * landed).
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: \`node hwtests/run.js refresh-store2 --frames=2000 --quiet=600\`.
 */
`;
const CASES = [
  ['caseN1', 'N1 sw; nop; sw (page)         ', ['sw $zero, 0(%1)', 'nop', 'sw $zero, 4(%1)', 'lw $t2, 4(%1)']],
  ['caseN2', 'N2 sw; 2 nops; sw (page)      ', ['sw $zero, 0(%1)', 'nop', 'nop', 'sw $zero, 4(%1)', 'lw $t2, 4(%1)']],
  ['caseN3', 'N3 sw; 3 nops; sw (page)      ', ['sw $zero, 0(%1)', 'nop', 'nop', 'nop', 'sw $zero, 4(%1)', 'lw $t2, 4(%1)']],
  ['caseJ1', 'J1 sw; nop; sw; JOY read r1   ', ['sw $zero, 0(%1)', 'nop', 'sw $zero, 4(%1)', 'lbu $t2, 0x1044($t4)', 'nop', 'addu $t2, $t2, $zero', 'lw $t2, 4(%1)']],
  ['caseJ2', 'J2 sw; 2 nops; sw; JOY read r1', ['sw $zero, 0(%1)', 'nop', 'nop', 'sw $zero, 4(%1)', 'lbu $t2, 0x1044($t4)', 'nop', 'addu $t2, $t2, $zero', 'lw $t2, 4(%1)']],
  ['caseK2', 'K2 sw; 2 nops; sw; JOY read r3', ['sw $zero, 0(%1)', 'nop', 'nop', 'sw $zero, 4(%1)', 'lbu $t2, 0x1044($t4)', 'nop', 'nop', 'nop', 'addu $t2, $t2, $zero', 'lw $t2, 4(%1)']],
  ['caseP0', 'P0 sw; lhu SPUSTAT            ', ['sw $zero, 4(%1)', 'lhu $t2, 0x1dae($t4)', 'nop', 'nop']],
  ['caseP1', 'P1 sw; nop; lhu SPUSTAT       ', ['sw $zero, 4(%1)', 'nop', 'lhu $t2, 0x1dae($t4)', 'nop', 'nop']],
  ['caseP2', 'P2 sw; 2 nops; lhu SPUSTAT    ', ['sw $zero, 4(%1)', 'nop', 'nop', 'lhu $t2, 0x1dae($t4)', 'nop', 'nop']],
  ['caseP3', 'P3 sw; 3 nops; lhu SPUSTAT    ', ['sw $zero, 4(%1)', 'nop', 'nop', 'nop', 'lhu $t2, 0x1dae($t4)', 'nop', 'nop']],
];
let s = src.slice(src.indexOf('#include <common.h>'));
// the cases: refresh-store's CASE macro, its calibration, these bodies
const a = s.indexOf('CASE(caseA,'), b = s.indexOf('typedef uint32_t (*CaseFn)');
s = s.slice(0, a) + CASES.map(([n, , body]) => `CASE(${n}, ${body.map((l) => `"${l}\\n"`).join(' ')})`).join('\n') + '\n\n' + s.slice(b);
const r0 = s.indexOf('    report("A  one store'), r1 = s.indexOf('\n', s.indexOf('    report("R  two loads'));
s = s.slice(0, r0) + CASES.map(([n, t]) => `    report("${t}", ${n}, F);`).join('\n') + s.slice(r1);
s = s.replace('printf("\\ncpu/refresh-store\\n");', 'printf("\\ncpu/refresh-store2\\n");')
  .replace('What a DRAM refresh costs a run of RAM stores at each phase from it (Timer 2 cycles between the two T2 reads).', 'What a DRAM refresh costs two stores with a gap, and a slow read behind a store, at each phase from it (Timer 2 cycles).');
fs.writeFileSync(path.join(__dirname, 'main.cpp'), HEAD + s);
console.log('main.cpp written');
