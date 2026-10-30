'use strict';
// Writes main.cpp beside it: an instruction-cache miss inside a load's shadow - the counter read (an on-die load), k
// nops, a jump into a cold line - and the same with a RAM load after the counter read.
// node hwtests/shadow-miss/gen.js, then node hwtests/build.js shadow-miss.
const fs = require('fs'), path = require('path');
const NL = '\\n';
const blocks = [];
for (const load of ['spu', 'cd', 'rom']) for (const b of [2, 0]) for (let k = 0; k < (b === 2 ? 7 : 3); k++) blocks.push({ load, b, k, name: `${load} b${b} k${k}` });
const N = blocks.length;
let a = '';
a += '.text' + NL + '.set push' + NL + '.set noreorder' + NL + '.set noat' + NL + '.balign 4096' + NL;
blocks.forEach((bl, i) => {
  a += `sm2_cold_${i}:` + NL;
  for (let k = 0; k < bl.b; k++) a += 'nop' + NL;
  a += `beq $zero, $zero, sm2_tgt_${i}` + NL + 'nop' + NL;
  for (let k = bl.b + 2; k < 8; k++) a += 'nop' + NL;
});
a += '.balign 4096' + NL;
blocks.forEach((bl, i) => { a += `sm2_conf_${i}:` + NL + 'nop' + NL + 'nop' + NL + 'nop' + NL + 'nop' + NL + 'jr $ra' + NL + 'nop' + NL + 'nop' + NL + 'nop' + NL; });
a += '.balign 1024' + NL;
blocks.forEach((bl, i) => {
  a += `sm2_entry_${i}:` + NL + 'lui $t9, 0x1f80' + NL + 'lui $t5, 0xbfc0' + NL + 'la $a0, sm2Word' + NL + 'move $a3, $ra' + NL;
  a += 'lhu $t8, 0x1120($t9)' + NL;
  if (bl.load === 'spu') a += 'lhu $t6, 0x1dae($t9)' + NL; else if (bl.load === 'cd') a += 'lbu $t6, 0x1800($t9)' + NL; else a += 'lw $t6, 0($t5)' + NL;
  for (let k = 0; k < bl.k; k++) a += 'nop' + NL;
  a += `j sm2_cold_${i}` + NL + 'nop' + NL;
  a += `sm2_tgt_${i}:` + NL + 'lhu $t7, 0x1120($t9)' + NL + 'nop' + NL + 'subu $v0, $t7, $t8' + NL + 'jr $a3' + NL + 'andi $v0, $v0, 0xffff' + NL;
  a += `.global sm2Run${i}` + NL + `sm2Run${i}:` + NL + 'move $a2, $ra' + NL;
  a += `jal sm2_entry_${i}` + NL + 'nop' + NL + `jal sm2_conf_${i}` + NL + 'nop' + NL;
  a += `jal sm2_entry_${i}` + NL + 'nop' + NL + 'move $a1, $v0' + NL;
  a += `jal sm2_entry_${i}` + NL + 'nop' + NL + 'move $v1, $v0' + NL + 'move $v0, $a1' + NL + 'jr $a2' + NL + 'nop' + NL;
});
a += '.set pop' + NL;
const src = `/*
 * cpu/shadow-miss2 - an instruction-cache miss behind a SLOW read (SPU, CD, BIOS ROM): how much of the read hides in the fill?
 * (2026-10-11 night)
 *
 * WHY. hwtests/shadow-miss on the SCPH-9001: a miss inside an on-die read's shadow starts its fill where the CPU really
 * is (behind the Timer 2 read k = 0 and 1 read alike), and behind a RAM load the fill waits for the load's bus. LSI's
 * LR33300 manual: with load scheduling the CPU "does not stall in the MEM stage" while no instruction needs the data. A
 * slow read (the sub-bus: SPU, CD, BIOS ROM) takes 6-18 cycles; hwtests/load-fill2's one SPU read in cold code costs
 * 14.75 a unit on the 7501 where v0.377.75 charges 16.75 (it gives a slow read no overlap with the next fill), and a
 * scratch core giving it the on-die shadow's two cycles reads 14.37. This walks the miss out of a slow read's shadow.
 *
 * WHAT IT DOES. As shadow-miss: Timer 2, interrupts off; the counter read, then a slow read - lhu SPUSTAT (1F801DAEh),
 * lbu the CD's status (1F801800h), or lw the BIOS ROM's first word - k nops, a j to word 0 of a line made cold by code
 * 4 KB above it, nops to word b, beq back to cached code that reads the counter; WARM the same path again. b = 2 puts the
 * delay slot at the line's last word, b = 0 waits for the rest of the line. Four runs each, lowest..highest.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: \`node hwtests/run.js shadow-miss2\`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE  0x1F801124u
extern "C" { uint32_t sm2Word = 0; }
__asm__(
${a.split(NL).filter(Boolean).map((l) => `    "${l}${NL}"`).join('\n')}
);
${blocks.map((_, i) => `extern "C" unsigned long long sm2Run${i}(void);`).join('\n')}
typedef unsigned long long (*Run)(void);
static const Run R[${N}] = { ${blocks.map((_, i) => 'sm2Run' + i).join(', ')} };
static const char *const NAME[${N}] = { ${blocks.map((b) => JSON.stringify(b.name)).join(', ')} };

int main() {
    printf("${NL}cpu/shadow-miss2${NL}");
    printf("A miss k instructions behind a slow read: cold and warm counters, lowest..highest of four.${NL}");
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
fs.writeFileSync(path.join(__dirname, 'Makefile'), 'TARGET = shadow-miss2.elf\n\ninclude ../../common-test.mk\n');
console.log('wrote', N, 'blocks');
