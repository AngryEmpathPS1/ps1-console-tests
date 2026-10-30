'use strict';
// Writes hwtests/fix-shadow/main.cpp: node hwtests/fix-shadow/gen.js
const fs = require('fs'), path = require('path');
// the load at C's word 0 (and again at the tail's word 2 in T1): t6 = the slot (RAM), t5 = BFC00000h, t9 = 1F800000h
const LOADS = [
  ['N', 'no load (a nop)', 'nop', 'nop'],
  ['R', 'RAM lw', 'lw $t0, 0x40($t6)', 'lw $t1, 0x44($t6)'],
  ['B', 'BIOS ROM lbu', 'lbu $t0, 0($t5)', 'lbu $t1, 0($t5)'],
  ['J', 'JOY_STAT lbu', 'lbu $t0, 0x1044($t9)', 'lbu $t1, 0x1044($t9)'],
  ['I', 'I_STAT lhu', 'lhu $t0, 0x1070($t9)', 'lhu $t1, 0x1070($t9)'],
];
const EXITS = [
  ['B1', 'branch at C word 1', ['LD', 'BR', 'nop', 'nop']],
  ['B2', 'branch at C word 2', ['LD', 'nop', 'BR', 'nop']],
  ['S', 'fall into warm D', ['LD', 'nop', 'nop', 'nop']],
];
const TAILS = [
  ['T0', 'nops', (ld2) => ['nop', 'nop', 'nop', 'nop']],
  ['T1', 'nop, addiu, the load again', (ld2) => ['nop', 'addiu $v1, $v1, 1', ld2, 'nop']],
];
const CASES = [];
for (const [lk, ll, ld, ld2] of LOADS) for (const [ex, xl, words] of EXITS) for (const [tk, tl, tw] of TAILS)
  CASES.push([lk + '-' + ex + '-' + tk, `${ll}, ${xl}, then ${tl}`, ld, words, tw(ld2), ex === 'S']);
const NL = '\\n';
const q = (s) => `    "${s}${NL}"\n`;
let asm = q('.text') + q('.set push') + q('.set noreorder') + q('.set noat');
CASES.forEach(([, , ld, words, tail, fall], i) => {
  const b = 'fsB' + i;
  asm += q('.balign 8192') + q(`.global ${b}_trial`) + q(`${b}_trial:`);
  // a0 = evict?, a1 = spin turns; t9 = 1F800000h, t5 = BFC00000h, t6 = this slot; the count in v0 - the whole trial here
  asm += q('move $t7, $ra') + q('lui $t9, 0x1f80') + q('lui $t5, 0xbfc0') + q(`la $t6, ${b}_trial`) + q(`jal ${b}_entry`) + q('nop');
  asm += q(`beqz $a0, ${b}_sp`) + q('nop') + q(`jal ${b}_ev`) + q('nop');
  asm += q(`${b}_sp:`) + q(`bnez $a1, ${b}_sp`) + q('addiu $a1, $a1, -1');
  asm += q(`jal ${b}_entry`) + q('nop') + q('jr $t7') + q('nop');
  asm += q(`.org ${b}_trial + 0x80`) + q(`${b}_w16:`) + q('sh $zero, 0x1120($t9)') + q('jr $ra') + q('nop') + q('nop');
  asm += q(`${b}_r16:`) + q('lhu $t3, 0x1120($t9)') + q('jr $ra') + q('nop') + q('nop');
  asm += q(`.org ${b}_trial + 0x100`) + q(`${b}_entry:`);
  asm += q('move $t8, $ra') + q(`jal ${b}_w16`) + q('nop') + q('nop');
  asm += q(`jal ${b}_c`) + q('nop') + q(`jal ${b}_r16`) + q('nop') + q('andi $v0, $t3, 0xffff') + q('jr $t8') + q('nop');
  // C at +0x200 (the load at word 0, then the exit), D at +0x210 (the tail when C falls through), X at +0x300 (the branch's)
  asm += q(`.org ${b}_trial + 0x200`) + q(`${b}_c:`);
  for (const w of words) asm += q(w === 'LD' ? ld : w === 'BR' ? `beq $zero, $zero, ${b}_x` : w);
  asm += q(`${b}_d:`);
  if (fall) for (const w of tail) asm += q(w);
  asm += q('jr $ra') + q('nop') + q('nop') + q('nop');
  asm += q(`.org ${b}_trial + 0x300`) + q(`${b}_x:`);
  if (!fall) for (const w of tail) asm += q(w);
  asm += q('jr $ra') + q('nop') + q('nop') + q('nop');
  // the evictor 4 KB above C
  asm += q(`.org ${b}_trial + 0x1200`) + q(`${b}_ev:`) + q('jr $ra') + q('nop') + q('nop') + q('nop');
});
asm += q('.balign 8192') + q('nop') + q('.set pop');

const decl = CASES.map((_, i) => `extern "C" uint32_t fsB${i}_trial(uint32_t evict, uint32_t spin);`).join('\n');
const tab = CASES.map((_, i) => `fsB${i}_trial`).join(', ');
const names = CASES.map(([n, w]) => `"${(n + '         ').slice(0, 9)} ${w}"`).join(',\n    ');
const caseList = CASES.map(([n, w]) => ` *   ${(n + '          ').slice(0, 10)}${w}`).join('\n');

const src = `/*
 * cpu/fix-shadow - a cold line with a load at its word 0, left for warm code: where the line's fixup cycle goes when it
 * falls inside the load's shadow (2026-10-12 small hours)
 *
 * WHY. hwtests/miss-grid and line-fill2 (SCPH-1001): every fill sequence costs one cycle more than this core charges,
 * once a sequence - a scratch core charges it at the sequence's first cache hit and reads both, and seq-miss2,
 * cold-window, shadow-miss and cold-branch, exactly. ps1-tests' cpu/access-time runs a loop whose second line holds the
 * load (word 0) and the branch back (word 1): its first turn fills that line, and the branch's target is the sequence's
 * first hit - and the load's third position, which a load's shadow runs inside the access. Charged as a stall there, the
 * fixup switches the shadow off (the BIOS rows +2 over this core, +4 over the consoles); run inside the access for free,
 * the BIOS and EXPANSION1 rows read the consoles' - and three on-die rows read 3-4 off. Which is it, by kind of load?
 *
 * WHAT IT DOES. miss-grid's harness (each case its own 8 KB slot, the whole trial in its code, only C cold in the timed
 * pass). C, entered by a jal: a load at word 0, then a taken branch at word 1 (B1) or word 2 (B2) to warm code, or a fall
 * into the warm next line (S); the code after it is four nops (T0) or a nop, an addiu and the same load again (T1: the
 * loop's own shape). The loads: none, RAM, BIOS ROM (8-bit), JOY_STAT and I_STAT. 16 trials over the refresh, cold
 * against warm:
${caseList}
 *
 * READ IT LIKE THIS. cold - warm: the N rows are miss-grid's J-B1, J-B2, J-S (the consoles' 7, 5, 5). A load row equal to
 * its N row: the fixup costs its cycle whatever the shadow. One less: it runs inside the access. More: it ends the
 * shadow (the core this was written on, as it stood: v0.377.79 + its scratch refits).
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: \`node hwtests/run.js fix-shadow\`. Generated by gen.js.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

${decl}
__asm__(
${asm});

typedef uint32_t (*Trial)(uint32_t, uint32_t);
static const Trial trials[] = { ${tab} };
static const char* const names[] = {
    ${names}
};
struct Span { uint32_t lo, hi; };
static Span run(Trial t, uint32_t evict) {
    Span s = { 0xFFFF, 0 };
    t(evict, 0);
    for (uint32_t k = 0; k < 16; k++) {
        const uint32_t v = t(evict, k * 8);
        if (v < s.lo) s.lo = v;
        if (v > s.hi) s.hi = v;
    }
    return s;
}

int main() {
    printf("\\ncpu/fix-shadow\\n");
    printf("A cold line with a load at word 0, left for warm code (Timer 2 cycles, fastest..slowest of 16; cold / warm).\\n");
    printf("There are no assertions - compare against your emulator.\\n\\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(0x1F801124u);
    write16(0x1F801124u, 0);
    for (unsigned i = 0; i < sizeof(trials) / sizeof(trials[0]); i++) {
        const Span c = run(trials[i], 1), w = run(trials[i], 0);
        printf("  %-64s cold %3u..%-3u  warm %3u..%-3u  miss %d\\n", names[i], (unsigned)c.lo, (unsigned)c.hi, (unsigned)w.lo, (unsigned)w.hi, (int)c.lo - (int)w.lo);
    }
    write16(0x1F801124u, m2);
    ExitCriticalSection();
    printf("\\nDone.\\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
`;
fs.writeFileSync(path.join(__dirname, 'main.cpp'), src);
console.log('wrote main.cpp');
