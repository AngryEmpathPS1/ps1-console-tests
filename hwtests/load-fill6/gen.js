'use strict';
// Writes main.cpp beside it: load-fill5's grids for stores - a store at each word of the line with a second store, a RAM
// load, or an I_STAT load behind it at each distance, and an I_STAT load with a store behind it.
// node hwtests/load-fill6/gen.js, then node hwtests/build.js load-fill6.
const fs = require('fs'), path = require('path');
const rows = [];   // [name, unit as 16 tokens]
const unit = (toks) => { const t = toks.slice(); while (t.length < 16) t.push('n'); if (t.length !== 16) throw new Error('unit ' + toks.join(' ')); return t; };
const at = (w, first, d, second) => { const t = Array(w).fill('n'); t.push(first); for (let i = 1; i < d; i++) t.push('n'); t.push(second); return unit(t); };
for (let w = 0; w < 4; w++) for (const d of [1, 2, 3, 4]) rows.push([`S${w} S+${d}`, at(w, 'S', d, 'S2')]);
for (let w = 0; w < 4; w++) for (const d of [1, 2, 3, 4]) rows.push([`S${w} L+${d}`, at(w, 'S', d, 'L1')]);
for (let w = 0; w < 4; w++) for (const d of [1, 2, 3, 4]) rows.push([`S${w} I+${d}`, at(w, 'S', d, 'I1')]);
for (let w = 0; w < 4; w++) for (const d of [1, 2, 3]) rows.push([`I${w} S+${d}`, at(w, 'I1', d, 'S')]);
const ASM = { n: 'nop', L1: 'lw $t1, 0(%1)', L2: 'lw $t2, 4(%1)', I1: 'lw $t1, 0(%2)', I2: 'lw $t2, 0(%2)', S: 'sw $zero, 16(%1)', S2: 'sw $zero, 20(%1)', U1: 'addu $t3, $t1, $t1' };
const NL = '\\n';   // a C string's newline escape
let blocks = '';
rows.forEach(([name, toks], i) => {
  blocks += `BLOCK(b${i}, ${toks.map((t) => '"' + ASM[t] + NL + '"').join(' ')}) /* ${name} */\n`;
});
const src = `/*
 * cpu/load-fill6 - load-fill5's grids for stores: a store at each word of its line against what comes behind it - a second
 * store, a RAM load, an I_STAT load - and an I_STAT load with a store behind it (2026-10-10 night)
 *
 * WHY. The cold-stream model (scratch/coldbus/patch_v75.js; STATUS 2026-10-10 night) fits load-fill5's reader grid by one
 * rule, and misses stores: three in a line one burst (9.30) where its burst rule gives 11.8, S0 L1 and S3 L4 1.3 high,
 * an I_STAT load behind a store 1.3 high. A grid shows the shape.
 *
 * WHAT IT DOES. As load-fill5: 128 units of 16 instructions, Timer 2, interrupts off, fastest..slowest of four passes,
 * each row a unit's cost above the nops block. Names: S a store (words 4 and 5 of a buffer no load reads), L a RAM load
 * into t1, I an I_STAT load into t1, at word w of the unit's first line; "+d" the second access d instructions later.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: \`node hwtests/run.js load-fill6\`.
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
    printf("${NL}cpu/load-fill6${NL}");
    printf("Cold store grids: a store (or I_STAT load) at word w of its line, the second access d instructions behind.${NL}");
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
fs.writeFileSync(path.join(__dirname, 'Makefile'), 'TARGET = load-fill6.elf\n\ninclude ../../common-test.mk\n');
console.log('wrote main.cpp,', rows.length, 'rows');
