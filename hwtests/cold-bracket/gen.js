'use strict';
// Writes main.cpp beside it: ps1-tests' cpu/access-time measure block, instruction for instruction, at each word of the
// cache line, printing the counter it reads instead of a difference.
// node hwtests/cold-bracket/gen.js, then node hwtests/build.js cold-bracket.
const fs = require('fs'), path = require('path');
const NL = '\\n';   // a C string's newline escape
// what runs inside the counted loop, and how many times
const VARIANTS = [
  { key: 'nop100', load: null, count: 100 },
  { key: 'joy100', load: 'lbu $a0, 0x1044($v1)', count: 100 },    // JOY_STAT, an on-die register
  { key: 'spad100', load: 'lbu $a0, 0x0000($v1)', count: 100 },   // the scratchpad
  { key: 'nop1', load: null, count: 1 },
  { key: 'joy1', load: 'lbu $a0, 0x1044($v1)', count: 1 },
];
// what the jal before the block calls: a two-instruction stub (the line it returns into stays as the delay slot left
// it), or the kernel's EnterCriticalSection as access-time calls it (its handler can evict that line)
const CALLS = [{ key: 'stub', target: 'cbStub' }, { key: 'kernel', target: 'EnterCriticalSection' }];
const blocks = [];
for (const c of CALLS) for (const v of VARIANTS) for (let a = 0; a < 4; a++) {
  const pad = (a + 2) & 3;   // pad nops, the jal, its delay slot, then the return lands at word a
  const lines = [];
  lines.push('lui $s0, 0x1f80');
  lines.push('.balign 64');
  for (let i = 0; i < pad; i++) lines.push('nop');
  lines.push(`jal ${c.target}`, 'nop');
  // access-time's measure, as its build-106 exe has it (scratch/coldbus/scratch_atfind.js): the mode read, the mode
  // write, the mode read again, the counter reset (START), the loop, the counter read (END) and the overflow read
  lines.push('lhu $v1, 0x1124($s0)', 'sh $zero, 0x1124($s0)', 'lhu $v0, 0x1124($s0)', 'andi $v1, $v1, 0xffff', 'sh $zero, 0x1120($s0)');
  lines.push(`addiu $v0, $zero, ${v.count}`);
  if (v.load) {
    lines.push(v.key.startsWith('spad') ? 'lui $v1, 0x1f80' : 'lui $v1, 0x1f80');
    lines.push('1:', 'nop', 'addiu $v0, $v0, -1', v.load, 'bnez $v0, 1b', 'nop');
    lines.push('lhu %0, 0x1120($v1)', 'lhu $v0, 0x1124($v1)');
  } else {
    lines.push('1:', 'nop', 'addiu $v0, $v0, -1', 'bnez $v0, 1b', 'nop');
    lines.push('lui $v0, 0x1f80', 'lhu %0, 0x1120($v0)', 'lhu $v0, 0x1124($v0)');
  }
  lines.push('nop');
  blocks.push({ name: `${c.key} ${v.key} w${a}`, body: lines });
}
let defs = '';
blocks.forEach((b, i) => {
  defs += `static uint32_t __attribute__((noinline, aligned(64))) b${i}(void) { /* ${b.name} */\n` +
    `    uint32_t r;\n    __asm__ volatile(".set push${NL}.set noreorder${NL}.set noat${NL}"\n` +
    b.body.map((l) => `        "${l}${NL}"\n`).join('') +
    `        ".set pop${NL}" : "=&r"(r) : : "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "t9", "s0", "ra", "memory");\n` +
    `    return r & 0xFFFF; }\n`;
});
const src = `/*
 * cpu/cold-bracket - ps1-tests' cpu/access-time measure block at each word of the cache line, printing the counter
 * (2026-10-10 night)
 *
 * WHY. access-time prints, for each address, (counter with a load in the loop) - (counter with a nop loop), and every
 * console we have prints the same rows to the cycle (12 runs, 4 boards): its on-die registers fall in four classes by
 * where the code sits in the cache line - 3.08 3.01 3.02 2.93 - a cold pass worth +8 +1 +2 -7 cycles. The cold-code
 * model (scratch/coldbus) fits load-fill 1-6 to a cycle and gets these 7 cycles RMS wrong; a difference of two cold
 * passes cannot say which one. This prints each.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 on the system clock. Each block: .balign 64, pad nops, a jal (to a two-word
 * stub, or to EnterCriticalSection as access-time does), its nop, then access-time's sequence instruction for instruction
 * - read Timer 2's mode, write it 0, read it, andi, reset the counter (START), the loop of 100 (or 1), read the counter
 * (END), read the mode. "wA" is the word the jal returns to, which is the word START sits at. Before each run the
 * instruction cache is filled with other code (a 4.5 KB run of nops), so the block runs cold, as access-time's does.
 * Four runs each, lowest..highest of the counter.
 *
 * Variants: nop100 / joy100 / spad100 - access-time's nop loop, its lbu JOY_STAT loop, its lbu scratchpad loop; nop1 /
 * joy1 - the same with the loop run once (the cold pass alone).
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: \`node hwtests/run.js cold-bracket\`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE  0x1F801124u
typedef uint32_t (*Block)(void);
extern "C" void __attribute__((noinline, aligned(64))) cbStub(void) { __asm__ volatile(""); }
static void __attribute__((noinline, aligned(64))) evict(void) {
    __asm__ volatile(".set push${NL}.set noreorder${NL}.rept 1152${NL}nop${NL}.endr${NL}.set pop${NL}");
}
${defs}
static const Block B[${blocks.length}] = { ${blocks.map((_, i) => 'b' + i).join(', ')} };
static const char *const NAME[${blocks.length}] = { ${blocks.map((b) => JSON.stringify(b.name)).join(', ')} };

int main() {
    printf("${NL}cpu/cold-bracket${NL}");
    printf("access-time's measure block at each word of the line, cold: the counter it reads, lowest..highest of four.${NL}");
    printf("There are no assertions - compare against your emulator.${NL}${NL}");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    for (int i = 0; i < ${blocks.length}; i++) {
        uint32_t lo = 0xFFFFFFFFu, hi = 0;
        for (int k = 0; k < 4; k++) { evict(); const uint32_t t = B[i](); if (t < lo) lo = t; if (t > hi) hi = t; }
        printf("  %-20s %5u..%-5u  at %08x${NL}", NAME[i], (unsigned)lo, (unsigned)hi, (unsigned)(uint32_t)B[i]);
    }
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("${NL}Done.${NL}");
    for (;;) {}
}
`;
fs.writeFileSync(path.join(__dirname, 'main.cpp'), src);
fs.writeFileSync(path.join(__dirname, 'Makefile'), 'TARGET = cold-bracket.elf\n\ninclude ../../common-test.mk\n');
console.log('wrote', blocks.length, 'blocks');
