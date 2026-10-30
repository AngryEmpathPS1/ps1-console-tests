'use strict';
// Writes main.cpp beside it: load-fill4's cold blocks as grids - a load at each word of the line with its reader at each
// distance, I_STAT pairs at each word and distance, a load with a store behind it at each distance.
// node hwtests/load-fill5/gen.js, then node hwtests/build.js load-fill5.
const fs = require('fs'), path = require('path');
const rows = [];   // [name, unit as 16 tokens]
const unit = (toks) => { const t = toks.slice(); while (t.length < 16) t.push('n'); if (t.length !== 16) throw new Error('unit ' + toks.join(' ')); return t; };
const at = (w, first, d, second) => { const t = Array(w).fill('n'); t.push(first); for (let i = 1; i < d; i++) t.push('n'); t.push(second); return unit(t); };
for (let w = 0; w < 4; w++) for (const d of [2, 3, 4, 5, 6]) rows.push([`L${w} U+${d}`, at(w, 'L1', d, 'U1')]);
for (let w = 0; w < 4; w++) for (const d of [1, 2, 3]) rows.push([`I${w} I+${d}`, at(w, 'I1', d, 'I2')]);
for (let w = 0; w < 4; w++) for (const d of [2, 3, 4]) rows.push([`I${w} U+${d}`, at(w, 'I1', d, 'U1')]);
for (let w = 0; w < 4; w++) for (const d of [1, 2, 3]) rows.push([`L${w} S+${d}`, at(w, 'L1', d, 'S')]);
for (let w = 0; w < 4; w++) for (const d of [1, 2]) rows.push([`L${w} L+${d}`, at(w, 'L1', d, 'L2')]);
const ASM = { n: 'nop', L1: 'lw $t1, 0(%1)', L2: 'lw $t2, 4(%1)', I1: 'lw $t1, 0(%2)', I2: 'lw $t2, 0(%2)', S: 'sw $zero, 16(%1)', U1: 'addu $t3, $t1, $t1' };
const NL = '\\n';   // a C string's newline escape
let blocks = '';
rows.forEach(([name, toks], i) => {
  blocks += `BLOCK(b${i}, ${toks.map((t) => '"' + ASM[t] + NL + '"').join(' ')}) /* ${name} */\n`;
});
const src = `/*
 * cpu/load-fill5 - load-fill4's cold shapes as grids: where in its line a load sits against how far behind it its
 * reader, its second load or its store comes (2026-10-10)
 *
 * WHY. A model of code that misses the instruction cache (scratch/patch_v74.js; STATUS 2026-10-10 evening) reads load-fill
 * 1-4 to 1.27 cycles RMS, and what it misses comes in one-cycle families tied to the word a load sits at: a reader at
 * word 3 costs 7.13 behind a load at word 0 and 6.09 behind one at word 1 (load-fill4), the opposite of every bus rule
 * tried. A grid shows the shape where a few rows could not.
 *
 * WHAT IT DOES. As load-fill4: 128 units of 16 instructions (four lines, twice the cache), Timer 2, interrupts off,
 * fastest..slowest of four passes, each row a unit's cost above the nops block. Names: the first access at word w of the
 * unit's first line (L a RAM load into t1, I an I_STAT load into t1), then "U+d" an addu of t1 d instructions later, "I+d"
 * / "L+d" a second load into t2, "S+d" a store to a word no load reads.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: \`node hwtests/run.js load-fill5\`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE  0x1F801124u
uint32_t ramWords[32];
typedef uint32_t (*Block)(uint32_t, uint32_t);
#define HEAD ".set push${NL}.set noreorder${NL}.set noat${NL}lui $t9, 0x1f80${NL}.balign 64${NL}lhu $t8, 0x1120($t9)${NL}.rept 15${NL}nop${NL}.endr${NL}"
#define TAIL "lhu $t7, 0x1120($t9)${NL}nop${NL}subu %0, $t7, $t8${NL}.set pop${NL}"
#define BLOCK(NAME, ...) \\
static uint32_t __attribute__((noinline, aligned(64))) NAME(uint32_t p, uint32_t q) { \\
    uint32_t r; \\
    __asm__ volatile(HEAD ".rept 128${NL}" __VA_ARGS__ ".endr${NL}" TAIL : "=&r"(r) : "r"(p), "r"(q) : "t1", "t2", "t3", "t7", "t8", "t9", "memory"); \\
    return r & 0xFFFF; }

BLOCK(nops, ".rept 16${NL}nop${NL}.endr${NL}")
${blocks}
static const Block B[${rows.length}] = { ${rows.map((_, i) => 'b' + i).join(', ')} };
static const char *const NAME[${rows.length}] = { ${rows.map(([n]) => JSON.stringify(n)).join(', ')} };

struct Span { uint32_t lo, hi; };
static Span time4(Block f) {
    Span s = { 0xFFFFFFFFu, 0 };
    for (int k = 0; k < 4; k++) { const uint32_t t = f((uint32_t)&ramWords[0], 0xBF801070u); if (t < s.lo) s.lo = t; if (t > s.hi) s.hi = t; }
    return s;
}

int main() {
    printf("${NL}cpu/load-fill5${NL}");
    printf("Cold grids: the first access at word w of its line, the reader / second load / store d instructions behind.${NL}");
    printf("Timer 2, fastest..slowest of four. There are no assertions - compare against your emulator.${NL}${NL}");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    const Span base = time4(nops);
    for (int i = 0; i < ${rows.length}; i++) {
        const Span s = time4(B[i]);
        const int d = (int)s.lo - (int)base.lo;
        const int a = d < 0 ? -d : d, per = (a * 100 + 64) / 128;
        printf("  %-14s %6u..%-6u  above the nops %6d  ->  %s%d.%02d a unit${NL}", NAME[i], (unsigned)s.lo, (unsigned)s.hi, d, d < 0 ? "-" : "", per / 100, per % 100);
    }
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("${NL}Done.${NL}");
    for (;;) {}
}
`;
fs.writeFileSync(path.join(__dirname, 'main.cpp'), src);
fs.writeFileSync(path.join(__dirname, 'Makefile'), 'TARGET = load-fill5.elf\n\ninclude ../../common-test.mk\n');
console.log('wrote main.cpp,', rows.length, 'rows');
