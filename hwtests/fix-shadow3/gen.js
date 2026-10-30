'use strict';
// Writes hwtests/fix-shadow3/main.cpp: node hwtests/fix-shadow3/gen.js
const fs = require('fs'), path = require('path');
// t9 = 1F800000h; v1 = the loops' count (2); the load at C's word 1 is a nop (N) or lbu JOY_STAT (J)
const LOADS = [['N', 'nop'], ['J', 'lbu $t0, 0x1044($t9)']];
// shape: [name, words, entry ('c' = jal to C word 0, 'p' = jal to P word 3: P cold, C behind it), C's words (LD = the
// load), what follows C (D: warm, the tail), loop count or 0]
const SHAPES = [
  ['A-S', 'C alone, falls into warm D', 'c', ['nop', 'LD', 'nop', 'nop'], 0],
  ['A-BX', 'C alone, branch at word 2 to a far warm line', 'c', ['nop', 'LD', 'beq $zero, $zero, X', 'nop'], 0],
  ['A-LC', 'C alone, a two-turn loop back to its own word 0', 'c', ['addiu $v1, $v1, -1', 'LD', 'bnez $v1, C', 'nop'], 2],
  ['P-S', 'P then C, falls into warm D', 'p', ['nop', 'LD', 'nop', 'nop'], 0],
  ['P-BX', 'P then C, branch at word 2 to a far warm line', 'p', ['nop', 'LD', 'beq $zero, $zero, X', 'nop'], 0],
  ['P-LP', 'P then C, a two-turn loop back to P word 3 (cold-bracket\'s shape)', 'p', ['addiu $v1, $v1, -1', 'LD', 'bnez $v1, P3', 'nop'], 2],
  ['P-LC', 'P then C, a two-turn loop back to C word 0', 'p', ['addiu $v1, $v1, -1', 'LD', 'bnez $v1, C', 'nop'], 2],
];
const CASES = [];
for (const [sn, sw, entry, cw, loop] of SHAPES) for (const [lk, ld] of LOADS)
  CASES.push({ name: lk + '-' + sn, words: `${lk === 'N' ? 'no load' : 'JOY_STAT lbu'} at C word 1, ${sw}`, entry, c: cw.map((w) => (w === 'LD' ? ld : w)), loop, t2: false });
// icache-tagmatch's shape: a Timer 2 read at a cold stub's word 0 opens the window (the count is END - START)
CASES.push({ name: 'T-J1', words: 'Timer 2 read at a cold stub\'s word 0 opening the window, jr ra at word 1', entry: 'c', c: ['lhu $t4, 0x1120($t9)', 'jr $ra', 'nop', 'nop'], loop: 0, t2: true });
CASES.push({ name: 'T-J3', words: 'Timer 2 read at a cold stub\'s word 0 opening the window, jr ra at word 2', entry: 'c', c: ['lhu $t4, 0x1120($t9)', 'nop', 'jr $ra', 'nop'], loop: 0, t2: true });
CASES.push({ name: 'T-S', words: 'Timer 2 read at a cold line\'s word 0 opening the window, falls into warm D', entry: 'c', c: ['lhu $t4, 0x1120($t9)', 'nop', 'nop', 'nop'], loop: 0, t2: true });
const NL = '\\n';
const q = (s) => `    "${s}${NL}"\n`;
let asm = q('.text') + q('.set push') + q('.set noreorder') + q('.set noat');
CASES.forEach((k, i) => {
  const b = 'fs3B' + i, lab = (w) => w.replace(/\bX\b/, `${b}_x`).replace(/\bP3\b/, `${b}_p3`).replace(/\bC$/, `${b}_c`);
  asm += q('.balign 8192') + q(`.global ${b}_trial`) + q(`${b}_trial:`);
  // a0 = evict?, a1 = spin turns; t9 = 1F800000h; the count in v0 - the whole trial here
  asm += q('move $t7, $ra') + q('lui $t9, 0x1f80') + q(`jal ${b}_entry`) + q('nop');
  asm += q(`beqz $a0, ${b}_sp`) + q('nop') + q(`jal ${b}_ev`) + q('nop');
  if (k.entry === 'p') asm += q(`jal ${b}_evp`) + q('nop');
  asm += q(`${b}_sp:`) + q(`bnez $a1, ${b}_sp`) + q('addiu $a1, $a1, -1');
  asm += q(`jal ${b}_entry`) + q('nop') + q('jr $t7') + q('nop');
  asm += q(`.org ${b}_trial + 0x80`) + q(`${b}_w16:`) + q('sh $zero, 0x1120($t9)') + q('jr $ra') + q('nop') + q('nop');
  asm += q(`${b}_r16:`) + q('lhu $t3, 0x1120($t9)') + q('jr $ra') + q('nop') + q('nop');
  asm += q(`.org ${b}_trial + 0x100`) + q(`${b}_entry:`);
  asm += q('move $t8, $ra') + q(`li $v1, ${k.loop || 2}`) + q(`jal ${b}_w16`) + q('nop') + q('nop');
  asm += q(`jal ${b}_${k.entry === 'p' ? 'p3' : 'c'}`) + q('nop') + q(`jal ${b}_r16`) + q('nop');
  asm += q(k.t2 ? 'subu $v0, $t3, $t4' : 'move $v0, $t3') + q('andi $v0, $v0, 0xffff') + q('jr $t8') + q('nop');
  // P at +0x1F0 (its word 3 the entry for 'p'), C at +0x200, D at +0x210 (warm: nops, jr ra), X at +0x300 (nops, jr ra)
  asm += q(`.org ${b}_trial + 0x1F0`) + q('nop') + q('nop') + q('nop') + q(`${b}_p3:`) + q('nop');
  asm += q(`${b}_c:`);
  for (const w of k.c) asm += q(lab(w));
  asm += q('nop') + q('nop') + q('nop') + q('nop') + q('jr $ra') + q('nop') + q('nop') + q('nop');
  asm += q(`.org ${b}_trial + 0x300`) + q(`${b}_x:`) + q('nop') + q('nop') + q('nop') + q('nop') + q('jr $ra') + q('nop') + q('nop') + q('nop');
  // the evictors 4 KB above C and P
  asm += q(`.org ${b}_trial + 0x11F0`) + q(`${b}_evp:`) + q('jr $ra') + q('nop') + q('nop') + q('nop');
  asm += q(`${b}_ev:`) + q('jr $ra') + q('nop') + q('nop') + q('nop');
});
asm += q('.balign 8192') + q('nop') + q('.set pop');

const decl = CASES.map((_, i) => `extern "C" uint32_t fs3B${i}_trial(uint32_t evict, uint32_t spin);`).join('\n');
const tab = CASES.map((_, i) => `fs3B${i}_trial`).join(', ');
const names = CASES.map((k) => `"${(k.name + '       ').slice(0, 7)} ${k.words}"`).join(',\n    ');
const caseList = CASES.map((k) => ` *   ${(k.name + '        ').slice(0, 8)}${k.words}`).join('\n');

const src = `/*
 * cpu/fix-shadow3 - the load's cycles while its line streams, against what the fill sequence does next (2026-10-12 night)
 *
 * WHY. hwtests/fix-shadow and fix-shadow2 (SCPH-1001 x3): a load in a cold line that runs into warm code - by falling
 * through or by a branch to a far warm line - costs its own 2 cycles while its line streams, at any word. Owed and paid at
 * the sequence's first cache hit, this core reads both programs - and puts two others 2 over: cold-bracket's JOY_STAT
 * loop (a load at word 1 of the second line of a cold pair, the loop branching back into the first line, w0) and
 * icache-tagmatch's cold calls (a Timer 2 read at a cold stub's word 0 opening the window). Which of their differences
 * does it: the line being the second of a sequence, a loop back into a line of the sequence, the load being the window's
 * own opening read?
 *
 * WHAT IT DOES. fix-shadow's harness (each case its own 8 KB slot, the whole trial in its code; only C, and P for the P
 * shapes, cold in the timed pass). P +1F0h (entered at word 3), C +200h, D +210h (warm), X +300h (warm), evictors 4 KB
 * above. The loops run two turns (v1 = 2). The T rows count from the read inside C to the closing read. 16 trials over the
 * refresh, cold against warm:
${caseList}
 *
 * READ IT LIKE THIS. Each J row against its N row: 0 (+ the warm difference) where the load's cycles hide, 2 where they are
 * paid as in fix-shadow. The T rows against fix-shadow2's E rows (the closing read at a cold line's word 0: 5).
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: \`node hwtests/run.js fix-shadow3\`. Generated by gen.js.
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
    printf("\\ncpu/fix-shadow3\\n");
    printf("A load's cycles in a streaming line against what the sequence does next (Timer 2 cycles, fastest..slowest of 16; cold / warm).\\n");
    printf("There are no assertions - compare against your emulator.\\n\\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(0x1F801124u);
    write16(0x1F801124u, 0);
    for (unsigned i = 0; i < sizeof(trials) / sizeof(trials[0]); i++) {
        const Span c = run(trials[i], 1), w = run(trials[i], 0);
        printf("  %-74s cold %3u..%-3u  warm %3u..%-3u  miss %d\\n", names[i], (unsigned)c.lo, (unsigned)c.hi, (unsigned)w.lo, (unsigned)w.hi, (int)c.lo - (int)w.lo);
    }
    write16(0x1F801124u, m2);
    ExitCriticalSection();
    printf("\\nDone.\\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
`;
fs.writeFileSync(path.join(__dirname, 'main.cpp'), src);
console.log('wrote main.cpp');
