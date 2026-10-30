'use strict';
// Writes main.cpp beside it: a taken branch out of a line that is still streaming in - each kind of branch, its delay
// slot at each word, the target cached, the counter read at the target or two instructions on.
// node hwtests/cold-branch/gen.js, then node hwtests/build.js cold-branch.
const fs = require('fs'), path = require('path');
const NL = '\\n';
const KINDS = ['beq', 'j', 'jr', 'bnez', 'ret'];   // beq $0,$0 / j / jr $t0 / bnez $t1 (t1 = 1) / jal into the line, jr $ra out
const TARGETS = [{ key: 'T0', pre: 0 }, { key: 'T2', pre: 2 }];
const blocks = [];
for (const kind of KINDS) for (const t of TARGETS) for (let p = 0; p < 4; p++) blocks.push({ kind, t, p, name: `${kind} ${t.key} b${p}` });
const N = blocks.length;
// one asm unit, laid out by hand:
//   cold region (4 KB aligned): 32 bytes a block - p nops, the branch at word p, its delay slot nop, nops to 32 bytes
//   conflict region (cold + 4096): 32 bytes a block - the same two cache lines: four nops, jr ra, nop, two nops
//   harness (cold + 4096 + 32 * N, rounded to the next 1 KB of index): per block an entry, a target and a runner, cached
let a = '';
a += '.text' + NL + '.set push' + NL + '.set noreorder' + NL + '.set noat' + NL + '.balign 4096' + NL;
a += '.global cbrCold' + NL + 'cbrCold:' + NL;
blocks.forEach((b, i) => {
  a += `cbr_cold_${i}:` + NL;
  for (let k = 0; k < b.p; k++) a += 'nop' + NL;
  if (b.kind === 'beq') a += `beq $zero, $zero, cbr_tgt_${i}` + NL;
  else if (b.kind === 'j') a += `j cbr_tgt_${i}` + NL;
  else if (b.kind === 'jr') a += 'jr $t0' + NL;
  else if (b.kind === 'bnez') a += `bnez $t1, cbr_tgt_${i}` + NL;
  else a += 'jr $ra' + NL;
  a += 'nop' + NL;
  for (let k = b.p + 2; k < 8; k++) a += 'nop' + NL;
});
a += '.balign 4096' + NL;
blocks.forEach((b, i) => {
  a += `cbr_conf_${i}:` + NL + 'nop' + NL + 'nop' + NL + 'nop' + NL + 'nop' + NL + 'jr $ra' + NL + 'nop' + NL + 'nop' + NL + 'nop' + NL;
});
a += '.balign 1024' + NL;   // the harness lands past the cold region's cache indices (32 * N bytes of them)
blocks.forEach((b, i) => {
  // entry: t9 = I/O base, t0 = the target (for jr), t1 = 1 (for bnez); read the counter; into the cold line
  a += `cbr_entry_${i}:` + NL + 'lui $t9, 0x1f80' + NL + `la $t0, cbr_tgt_${i}` + NL + 'li $t1, 1' + NL + 'move $a3, $ra' + NL;
  a += 'lhu $t8, 0x1120($t9)' + NL;
  if (b.kind === 'ret') a += `jal cbr_cold_${i}` + NL + 'nop' + NL;
  else a += `j cbr_cold_${i}` + NL + 'nop' + NL;
  a += `cbr_tgt_${i}:` + NL;
  for (let k = 0; k < b.t.pre; k++) a += 'nop' + NL;
  a += 'lhu $t7, 0x1120($t9)' + NL + 'nop' + NL + 'subu $v0, $t7, $t8' + NL + 'jr $a3' + NL + 'andi $v0, $v0, 0xffff' + NL;
  // the runner: warm the whole path, evict the block's two cold lines, time it cold, time it warm; v0 cold, v1 warm
  a += `.global cbrRun${i}` + NL + `cbrRun${i}:` + NL + 'move $a2, $ra' + NL;
  a += `jal cbr_entry_${i}` + NL + 'nop' + NL;
  a += `jal cbr_conf_${i}` + NL + 'nop' + NL;
  a += `jal cbr_entry_${i}` + NL + 'nop' + NL + 'move $a1, $v0' + NL;
  a += `jal cbr_entry_${i}` + NL + 'nop' + NL + 'move $v1, $v0' + NL + 'move $v0, $a1' + NL;
  a += 'jr $a2' + NL + 'nop' + NL;
});
a += '.set pop' + NL;
const decls = blocks.map((_, i) => `extern "C" unsigned long long cbrRun${i}(void);`).join('\n');
const src = `/*
 * cpu/cold-branch - a taken branch out of a line still streaming in: does the CPU wait for the rest of the line?
 * (2026-10-11 night)
 *
 * WHY. IDT's R3051 manual ("Instruction Streaming"): execution in a streaming line stops at "Taken branches ... the
 * processor re-enters simple cache refill until the rest of the block has been written into the cache". hwtests/
 * cold-bracket on the SCPH-9001 reads it: access-time's nop loop 100 times against once costs 99 x 4 plus 3 2 0 4 with
 * its delay slot at word 1 2 3 0 of a streaming line - the words still to come and a cycle - and nothing at word 3. But
 * hwtests/fill-gap's cold call (a jal to "jr ra; nop" at word 0) costs the consoles exactly what it costs a model with no
 * such wait. Which branches wait, and for what?
 *
 * WHAT IT DOES. Interrupts off, Timer 2 on the system clock. Each block reads the counter in cached code, goes to word 0
 * of a cold line - a j, or for "ret" a jal - runs nops to word b, takes a branch there (its delay slot at b + 1) back to
 * cached code, which reads the counter at once (T0) or after two nops (T2). The kinds: beq $0,$0 / j / jr $t0 /
 * bnez (taken) / ret (jr $ra back to the jal). The cold line is made cold by calling code exactly 4 KB above it (the
 * same cache line, nothing else evicted); the same path run again warm is the WARM column, and COLD - WARM is the line's
 * cost. Four runs each, lowest..highest.
 *
 * READ IT LIKE THIS. Without a wait COLD - WARM is the same at every b (the fill's latency); a wait for the rest of the
 * line adds the words still to come after the delay slot (2 1 0 at b = 0 1 2; a delay slot at word 0 of the next line,
 * b = 3, streams that line too).
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: \`node hwtests/run.js cold-branch\`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE  0x1F801124u
__asm__(
${a.split(NL).filter(Boolean).map((l) => `    "${l}${NL}"`).join('\n')}
);
${decls}
typedef unsigned long long (*Run)(void);
static const Run R[${N}] = { ${blocks.map((_, i) => 'cbrRun' + i).join(', ')} };
static const char *const NAME[${N}] = { ${blocks.map((b) => JSON.stringify(b.name)).join(', ')} };

int main() {
    printf("${NL}cpu/cold-branch${NL}");
    printf("A taken branch out of a streaming line: cold and warm counters, lowest..highest of four.${NL}");
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
fs.writeFileSync(path.join(__dirname, 'Makefile'), 'TARGET = cold-branch.elf\n\ninclude ../../common-test.mk\n');
console.log('wrote', N, 'blocks');
