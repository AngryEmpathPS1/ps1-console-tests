'use strict';
// Writes hwtests/refresh-store7/main.cpp from hwtests/refresh-store's harness: node hwtests/refresh-store7/gen.js
const fs = require('fs'), path = require('path');
const src = fs.readFileSync(path.join(__dirname, '..', 'refresh-store', 'main.cpp'), 'utf8');
const HEAD = `/*
 * cpu/refresh-store7 - a load behind a delay-slot store: is the cycle the REGISTER? (2026-10-30). refresh-store's harness.
 *
 * WHY. hwtests/refresh-store6 on the SCPH-5501 #2 (three runs alike): sw A; T; sw A+8; [nop;] lw reads 24 / 23 wherever the load goes, the word stored or another page, so it is not the address (a second
 * opinion's guess). refresh-io2's R rows, sw; [k nops;] lw, read 18 18 18 18 with a Timer 2 read in the same place: no such step. What differs: every row of T1's family,
 * from refresh-store3 on, reads Timer 2 into t2 and loads into t2 too, a load at position 2 writing the on-die load's register. refresh-io2's bracket read goes to t1. That was an accident of the
 * harness. v0.377.210 found a writer at position 1 or 2 ends an on-die load's shadow; nothing asks whether a LOAD is such a writer.
 *
 * WHAT IT DOES. refresh-store's harness and calibration, each case at the 72 phases from F - 32 to F + 39. T = lhu Timer 2 into t2.
 *   E0 / E1   sw A; T; sw A+8; [nop;] lw t2, A+8      T1 and T3, the control
 *   F0 / F1   the same, lw t5                         the load another register
 *   G0 / G1   sw A; T; sw A+8; [nop;] addu t2, zero, zero; nop; lw t5, A+8     an ALU writer of T's register in that place
 *   H0 / H1   sw A; T; nop; [nop;] lw t2, A           no second store, t2
 *   J0 / J1   the same, lw t5
 *   K0 / K1   T; sw A+8; [nop;] lw t5                 refresh-store6's B rows, another register
 *   S0 / S1   sw A; T; sw SP+8; [nop;] lw t5, SP+8    its S rows, another register
 * Each prints its commonest cost, then the cost at each phase from the refresh, sixteen to a line ('.' where no trial landed).
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: \`node hwtests/run.js refresh-store7 --frames=3000 --quiet=600\`.
 */
`;
const T = 'lhu $t2, 0x1120($t4)', ST = 'sw $zero, 0(%1)', S8 = 'sw $zero, 8(%1)';
const pad = (s) => s.padEnd(30);
const pair = (a, b, label, head, tail) => [
  [`case${a}`, pad(`${a} ${label}`), [...head, ...tail]],
  [`case${b}`, pad(`${b} ${label} (nop)`), [...head, 'nop', ...tail]],
];
const CASES = [
  ...pair('E0', 'E1', 'lw the same reg', [ST, T, S8], ['lw $t2, 8(%1)']),
  ...pair('F0', 'F1', 'lw another reg', [ST, T, S8], ['lw $t5, 8(%1)']),
  ...pair('G0', 'G1', 'ALU writer of T', [ST, T, S8], ['addu $t2, $zero, $zero', 'nop', 'lw $t5, 8(%1)']),
  ...pair('H0', 'H1', 'no store, same reg', [ST, T, 'nop'], ['lw $t2, 0(%1)']),
  ...pair('J0', 'J1', 'no store, other', [ST, T, 'nop'], ['lw $t5, 0(%1)']),
  ...pair('K0', 'K1', 'no first store, other', [T, S8], ['lw $t5, 8(%1)']),
  ...pair('S0', 'S1', 'scratchpad, other', [ST, T, 'sw $zero, 8($t4)'], ['lw $t5, 8($t4)']),
];
let s = src.slice(src.indexOf('#include <common.h>'));
const a = s.indexOf('CASE(caseA,'), b = s.indexOf('typedef uint32_t (*CaseFn)');
s = s.slice(0, a) + CASES.map(([n, , body]) => `CASE(${n}, ${body.map((l) => `"${l}\\n"`).join(' ')})`).join('\n') + '\n\n' + s.slice(b);
const r0 = s.indexOf('    report("A  one store'), r1 = s.indexOf('\n', s.indexOf('    report("R  two loads'));
s = s.slice(0, r0) + CASES.map(([n, t]) => `    report("${t}", ${n}, F);`).join('\n') + s.slice(r1);
const clob = `"t1", "t2", "t3", "t4", "memory"`;
if (s.split(clob).length !== 2) throw new Error('clobbers');
s = s.replace(clob, `"t1", "t2", "t3", "t4", "t5", "memory"`);
s = s.replace('printf("\\ncpu/refresh-store\\n");', 'printf("\\ncpu/refresh-store7\\n");')
  .replace('What a DRAM refresh costs a run of RAM stores at each phase from it (Timer 2 cycles between the two T2 reads).', 'A load behind an on-die read, the same register or another, at each phase from a refresh (Timer 2 cycles).');
fs.writeFileSync(path.join(__dirname, 'main.cpp'), HEAD + s);
console.log('main.cpp written');
