'use strict';
// Writes main.cpp beside it: an instruction-cache miss inside a load's shadow - the counter read (an on-die load), k
// nops, a jump into a cold line - and the same with a RAM load after the counter read.
// node hwtests/shadow-miss/gen.js, then node hwtests/build.js shadow-miss.
const fs = require('fs'), path = require('path');
const NL = '\\n';
const blocks = [];
for (const load of ['io', 'ram']) for (const b of [2, 0]) for (let k = 0; k < 5; k++) blocks.push({ load, b, k, name: `${load} b${b} k${k}` });
const N = blocks.length;
let a = '';
a += '.text' + NL + '.set push' + NL + '.set noreorder' + NL + '.set noat' + NL + '.balign 4096' + NL;
blocks.forEach((bl, i) => {
  a += `shm_cold_${i}:` + NL;
  for (let k = 0; k < bl.b; k++) a += 'nop' + NL;
  a += `beq $zero, $zero, shm_tgt_${i}` + NL + 'nop' + NL;
  for (let k = bl.b + 2; k < 8; k++) a += 'nop' + NL;
});
a += '.balign 4096' + NL;
blocks.forEach((bl, i) => { a += `shm_conf_${i}:` + NL + 'nop' + NL + 'nop' + NL + 'nop' + NL + 'nop' + NL + 'jr $ra' + NL + 'nop' + NL + 'nop' + NL + 'nop' + NL; });
a += '.balign 1024' + NL;
blocks.forEach((bl, i) => {
  a += `shm_entry_${i}:` + NL + 'lui $t9, 0x1f80' + NL + 'la $a0, shmWord' + NL + 'move $a3, $ra' + NL;
  a += 'lhu $t8, 0x1120($t9)' + NL;
  if (bl.load === 'ram') a += 'lw $t6, 0($a0)' + NL;
  for (let k = 0; k < bl.k; k++) a += 'nop' + NL;
  a += `j shm_cold_${i}` + NL + 'nop' + NL;
  a += `shm_tgt_${i}:` + NL + 'lhu $t7, 0x1120($t9)' + NL + 'nop' + NL + 'subu $v0, $t7, $t8' + NL + 'jr $a3' + NL + 'andi $v0, $v0, 0xffff' + NL;
  a += `.global shmRun${i}` + NL + `shmRun${i}:` + NL + 'move $a2, $ra' + NL;
  a += `jal shm_entry_${i}` + NL + 'nop' + NL + `jal shm_conf_${i}` + NL + 'nop' + NL;
  a += `jal shm_entry_${i}` + NL + 'nop' + NL + 'move $a1, $v0' + NL;
  a += `jal shm_entry_${i}` + NL + 'nop' + NL + 'move $v1, $v0' + NL + 'move $v0, $a1' + NL + 'jr $a2' + NL + 'nop' + NL;
});
a += '.set pop' + NL;
const src = `/*
 * cpu/shadow-miss - an instruction-cache miss inside a load's shadow: does the fill start where the CPU really is?
 * (2026-10-11 night)
 *
 * WHY. hwtests/cold-branch on the SCPH-9001: a Timer 2 read, a jump and its delay slot, then a cold line - its fill
 * reads as if asked for two cycles before this core asks: the on-die load's shadow (its last two cycles, which the
 * instructions 3 and 4 behind it run free under, M207/v0.377.26) seems to hold the miss too. scratch/patch_v85.js gives
 * a miss at a shadowed position the shadow's refunds. Here the miss walks out of the shadow: k nops between the read and
 * the jump put the cold line's first word at position 3 + k. And a RAM load after the read instead (its own shadow, the
 * RAM bus busy): does its shadow hold a fill?
 *
 * WHAT IT DOES. As cold-branch: Timer 2, interrupts off; the counter read (and for "ram" an lw after it), k nops, a j to
 * word 0 of a line made cold by code 4 KB above it, nops to word b, beq back to cached code that reads the counter; WARM
 * the same path again. b = 2 puts the delay slot at the line's last word (no wait for the rest of the line), b = 0 waits.
 * Four runs each, lowest..highest.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: \`node hwtests/run.js shadow-miss\`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE  0x1F801124u
extern "C" { uint32_t shmWord = 0; }
__asm__(
${a.split(NL).filter(Boolean).map((l) => `    "${l}${NL}"`).join('\n')}
);
${blocks.map((_, i) => `extern "C" unsigned long long shmRun${i}(void);`).join('\n')}
typedef unsigned long long (*Run)(void);
static const Run R[${N}] = { ${blocks.map((_, i) => 'shmRun' + i).join(', ')} };
static const char *const NAME[${N}] = { ${blocks.map((b) => JSON.stringify(b.name)).join(', ')} };

int main() {
    printf("${NL}cpu/shadow-miss${NL}");
    printf("A miss k instructions behind a load: cold and warm counters, lowest..highest of four.${NL}");
    printf("There are no assertions - compare against your emulator.${NL}${NL}");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    printf("  block          COLD        WARM      COLD-WARM${NL}");
    for (int i = 0; i < ${N}; i++) {
        uint32_t clo = 0xFFFFFFFFu, chi = 0, wlo = 0xFFFFFFFFu, whi = 0;
        for (int k = 0; k < 4; k++) {
            const unsigned long long r = R[i]();
            const uint32_t c = (uint32_t)r & 0xFFFF, w = (uint32_t)(r >> 32) & 0xFFFF;
            if (c < clo) clo = c; if (c > chi) chi = c; if (w < wlo) wlo = w; if (w > whi) whi = w;
        }
        printf("  %-12s %4u..%-4u  %4u..%-4u  %4d${NL}", NAME[i], (unsigned)clo, (unsigned)chi, (unsigned)wlo, (unsigned)whi, (int)clo - (int)wlo);
    }
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("${NL}Done.${NL}");
    for (;;) {}
}
`;
fs.writeFileSync(path.join(__dirname, 'main.cpp'), src);
fs.writeFileSync(path.join(__dirname, 'Makefile'), 'TARGET = shadow-miss.elf\n\ninclude ../../common-test.mk\n');
console.log('wrote', N, 'blocks');
