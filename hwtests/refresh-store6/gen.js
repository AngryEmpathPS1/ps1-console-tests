'use strict';
// Writes hwtests/refresh-store6/main.cpp from hwtests/refresh-store's harness: node hwtests/refresh-store6/gen.js
const fs = require('fs'), path = require('path');
const src = fs.readFileSync(path.join(__dirname, '..', 'refresh-store', 'main.cpp'), 'utf8');
const HEAD = `/*
 * cpu/refresh-store6 - why a nop makes a load behind a delay-slot store FASTER: where the load goes, and whether the read before it waited (2026-10-30). refresh-store's harness.
 *
 * WHY. hwtests/refresh-store3 and 4 on the SCPH-5501 #2: sw A; on-die read; sw A+8; lw A+8 reads 24, with 1, 2 or 3 nops before the lw 23. The load waits for that store either way, so a nop should
 * change nothing, and on the console it saves a cycle. This core reads 23 for all four. A second opinion (an AI's report, not included; its explanation is
 * its own hypothesis) names what nothing here ever varied: the load is the very word just stored, Sony's documented case (a read of a pending write waits for it). Every row of T1's family has
 * that load. Psy-Q's Hardware.pdf ch.2 and that report give the same rule. So: is it the address?
 *
 * WHAT IT DOES. refresh-store's harness and calibration, each case at the 72 phases from F - 32 to F + 39. T = lhu T2.
 *   X0 / Y0   sw A; T; sw A+8; [nop;] lw A+8       the word just stored (refresh-store3's T1 / T3, the control)
 *   X1 / Y1   ... lw A+12                          the next word
 *   X2 / Y2   ... lw A+72                          another 64-byte block, the same 1 KB
 *   X3 / Y3   ... lw A+2048                        another 1 KB block
 *   X4 / Y4   ... lw A+16K                         another page
 *   S0 / S1   sw A; T; sw SP+8; [nop;] lw SP+8     the second store and the load on the scratchpad: no queue at all
 *   W0 / W1   sw A; 4 nops; T; sw A+8; [nop;] lw A+8   the read that did not wait: no hold, the first store done
 *   B0 / B1   T; sw A+8; [nop;] lw A+8             no first store at all
 *   D0 / D1   sw A; T; sw A+8; [nop;] lw A         the FIRST store's word
 * Each prints its commonest cost, then the cost at each phase from the refresh, sixteen to a line ('.' where no trial landed).
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: \`node hwtests/run.js refresh-store6 --frames=3000 --quiet=600\`.
 */
`;
const T = 'lhu $t2, 0x1120($t4)', ST = 'sw $zero, 0(%1)', S8 = 'sw $zero, 8(%1)';
const pad = (s) => s.padEnd(30);
const pair = (a, b, label, head, st, ld) => [
  [`case${a}`, pad(`${a} ${label}`), [...head, st, ld]],
  [`case${b}`, pad(`${b} ${label} (nop)`), [...head, st, 'nop', ld]],
];
const CASES = [
  ...pair('X0', 'Y0', 'lw the word stored', [ST, T], S8, 'lw $t2, 8(%1)'),
  ...pair('X1', 'Y1', 'lw the next word', [ST, T], S8, 'lw $t2, 12(%1)'),
  ...pair('X2', 'Y2', 'lw other 64 bytes', [ST, T], S8, 'lw $t2, 72(%1)'),
  ...pair('X3', 'Y3', 'lw other 1 KB', [ST, T], S8, 'lw $t2, 2048(%1)'),
  ...pair('X4', 'Y4', 'lw other page', [ST, T], S8, 'lw $t2, 0x4000(%1)'),
  ...pair('S0', 'S1', 'scratchpad', [ST, T], 'sw $zero, 8($t4)', 'lw $t2, 8($t4)'),
  ...pair('W0', 'W1', 'read did not wait', [ST, 'nop', 'nop', 'nop', 'nop', T], S8, 'lw $t2, 8(%1)'),
  ...pair('B0', 'B1', 'no first store', [T], S8, 'lw $t2, 8(%1)'),
  ...pair('D0', 'D1', 'lw the first word', [ST, T], S8, 'lw $t2, 0(%1)'),
];
let s = src.slice(src.indexOf('#include <common.h>'));
const a = s.indexOf('CASE(caseA,'), b = s.indexOf('typedef uint32_t (*CaseFn)');
s = s.slice(0, a) + CASES.map(([n, , body]) => `CASE(${n}, ${body.map((l) => `"${l}\\n"`).join(' ')})`).join('\n') + '\n\n' + s.slice(b);
const r0 = s.indexOf('    report("A  one store'), r1 = s.indexOf('\n', s.indexOf('    report("R  two loads'));
s = s.slice(0, r0) + CASES.map(([n, t]) => `    report("${t}", ${n}, F);`).join('\n') + s.slice(r1);
s = s.replace('printf("\\ncpu/refresh-store\\n");', 'printf("\\ncpu/refresh-store6\\n");')
  .replace('What a DRAM refresh costs a run of RAM stores at each phase from it (Timer 2 cycles between the two T2 reads).', 'A load behind a delay-slot store, by where it goes, at each phase from a refresh (Timer 2 cycles).');
fs.writeFileSync(path.join(__dirname, 'main.cpp'), HEAD + s);
console.log('main.cpp written');
