'use strict';
// Writes hwtests/refresh-store3/main.cpp from hwtests/refresh-store's harness: node hwtests/refresh-store3/gen.js
const fs = require('fs'), path = require('path');
const src = fs.readFileSync(path.join(__dirname, '..', 'refresh-store', 'main.cpp'), 'utf8');
const HEAD = `/*
 * cpu/refresh-store3 - an on-die read behind a store at every offset from a DRAM refresh, Timer 2 against I_STAT, with and without a store
 * in the read's delay slot (2026-10-30). refresh-store's harness.
 *
 * WHY. A refresh due a cycle into a store costs what is behind it 6 (refresh-io2's S rows: sw; k nops; lhu I_STAT reads 19 there, on all
 * seven consoles). Charged that way, hwtests/cpu-loop-tick10's DS-1 (sw A+4; lhu T2; sw A+12; nop) reads 13.31 where the consoles and the
 * core without the rule read 13.19, 0.1 a turn, and DS-SAMEW and DS-SH with it: what differs from the S rows? The read is Timer 2, not
 * I_STAT, nothing reads its value, and a store sits in its delay slot. refresh-store's header says a Timer 2 read goes ahead of queued
 * stores, and no program times this shape against the refresh.
 *
 * WHAT IT DOES. refresh-store's harness and calibration, each case at the 72 phases from F - 32 to F + 39, the last line a load of the last
 * stored word so nothing passes the queue unseen:
 *   T0  sw A; lhu T2; lw A                    Timer 2 behind a store
 *   I0  sw A; lhu I_STAT; lw A                the same, I_STAT
 *   T1  sw A; lhu T2; sw A+8; lw A+8          DS-1's shape, the store in the read's delay slot
 *   I1  sw A; lhu I_STAT; sw A+8; lw A+8      the same, I_STAT
 *   T2  sw A; lhu T2; nop; sw A+8; lw A+8     the store out of the delay slot
 *   I2  sw A; lhu I_STAT; nop; sw A+8; lw A+8
 *   T3  sw A; lhu T2; sw A+8; nop; lw A+8     DS-1 itself, its nop
 *   U1  sw A; lhu T2; addu t2, t2, zero; sw A+8; lw A+8    a reader in the store's place
 * Each prints its commonest cost, then the cost at each phase from the refresh, sixteen to a line ('.' where no trial landed).
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: \`node hwtests/run.js refresh-store3 --frames=2000 --quiet=600\`.
 */
`;
const CASES = [
  ['caseT0', 'T0 sw; lhu T2                 ', ['sw $zero, 0(%1)', 'lhu $t2, 0x1120($t4)', 'lw $t2, 0(%1)']],
  ['caseI0', 'I0 sw; lhu I_STAT             ', ['sw $zero, 0(%1)', 'lhu $t2, 0x1070($t4)', 'lw $t2, 0(%1)']],
  ['caseT1', 'T1 sw; lhu T2; sw             ', ['sw $zero, 0(%1)', 'lhu $t2, 0x1120($t4)', 'sw $zero, 8(%1)', 'lw $t2, 8(%1)']],
  ['caseI1', 'I1 sw; lhu I_STAT; sw         ', ['sw $zero, 0(%1)', 'lhu $t2, 0x1070($t4)', 'sw $zero, 8(%1)', 'lw $t2, 8(%1)']],
  ['caseT2', 'T2 sw; lhu T2; nop; sw        ', ['sw $zero, 0(%1)', 'lhu $t2, 0x1120($t4)', 'nop', 'sw $zero, 8(%1)', 'lw $t2, 8(%1)']],
  ['caseI2', 'I2 sw; lhu I_STAT; nop; sw    ', ['sw $zero, 0(%1)', 'lhu $t2, 0x1070($t4)', 'nop', 'sw $zero, 8(%1)', 'lw $t2, 8(%1)']],
  ['caseT3', 'T3 sw; lhu T2; sw; nop        ', ['sw $zero, 0(%1)', 'lhu $t2, 0x1120($t4)', 'sw $zero, 8(%1)', 'nop', 'lw $t2, 8(%1)']],
  ['caseU1', 'U1 sw; lhu T2; reader; sw     ', ['sw $zero, 0(%1)', 'lhu $t2, 0x1120($t4)', 'addu $t2, $t2, $zero', 'sw $zero, 8(%1)', 'lw $t2, 8(%1)']],
];
let s = src.slice(src.indexOf('#include <common.h>'));
const a = s.indexOf('CASE(caseA,'), b = s.indexOf('typedef uint32_t (*CaseFn)');
s = s.slice(0, a) + CASES.map(([n, , body]) => `CASE(${n}, ${body.map((l) => `"${l}\\n"`).join(' ')})`).join('\n') + '\n\n' + s.slice(b);
const r0 = s.indexOf('    report("A  one store'), r1 = s.indexOf('\n', s.indexOf('    report("R  two loads'));
s = s.slice(0, r0) + CASES.map(([n, t]) => `    report("${t}", ${n}, F);`).join('\n') + s.slice(r1);
s = s.replace('printf("\\ncpu/refresh-store\\n");', 'printf("\\ncpu/refresh-store3\\n");')
  .replace('What a DRAM refresh costs a run of RAM stores at each phase from it (Timer 2 cycles between the two T2 reads).', 'What a DRAM refresh costs an on-die read behind a store, Timer 2 and I_STAT, at each phase from it (Timer 2 cycles).');
fs.writeFileSync(path.join(__dirname, 'main.cpp'), HEAD + s);
console.log('main.cpp written');
