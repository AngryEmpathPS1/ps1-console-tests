'use strict';
// Writes main.cpp beside it: chains of on-die loads in cold code - n = 1..8 I_STAT loads back to back from word w of the
// line, into one register or two alternately. node hwtests/io-chain/gen.js, then node hwtests/build.js io-chain.
const fs = require('fs'), path = require('path');
const rows = [];   // [name, unit as 16 tokens]
const unit = (toks) => { const t = toks.slice(); while (t.length < 16) t.push('n'); if (t.length !== 16) throw new Error('unit ' + toks.join(' ')); return t; };
const at = (w, first, d, second) => { const t = Array(w).fill('n'); t.push(first); for (let i = 1; i < d; i++) t.push('n'); t.push(second); return unit(t); };
const chain = (w, n, two) => { const t = Array(w).fill('n'); for (let k = 0; k < n; k++) t.push(two && (k & 1) ? 'I2' : 'I1'); return unit(t); };
for (const two of [false, true]) for (let w = 0; w < 4; w++) for (let n = 1; n <= 8; n++) rows.push([`${two ? 'IDIFF' : 'ISAME'} w${w} x${n}`, chain(w, n, two)]);
const ASM = { n: 'nop', L1: 'lw $t1, 0(%1)', L2: 'lw $t2, 4(%1)', I1: 'lw $t1, 0(%2)', I2: 'lw $t2, 0(%2)', S: 'sw $zero, 16(%1)', S2: 'sw $zero, 20(%1)', U1: 'addu $t3, $t1, $t1' };
const NL = '\\n';   // a C string's newline escape
let blocks = '';
rows.forEach(([name, toks], i) => {
  blocks += `BLOCK(b${i}, ${toks.map((t) => '"' + ASM[t] + NL + '"').join(' ')}) /* ${name} */\n`;
});
const src = `/*
 * cpu/io-chain - chains of on-die loads in code that misses the cache: n = 1..8 I_STAT loads back to back from word w of
 * the line, into one register or two alternately (2026-10-11 night)
 *
 * WHY. v0.377.75's stream (src/psx.js, "IN CODE THAT MISSES THE INSTRUCTION CACHE") fits 488 console rows to 0.98 cycles
 * RMS, and its worst misses are chains of I_STAT loads: hwtests/load-fill3's sixteen to a unit 71.59 on the 9001, 63.65
 * here, and every second word 39.67 against 36.31 - where load-fill5's pairs fit. The chain costs more than its pairs.
 * A grid of chain lengths at each word of the line shows where.
 *
 * WHAT IT DOES. As load-fill5: 128 units of 16 instructions, Timer 2, interrupts off, fastest..slowest of four passes,
 * each row a unit's cost above the nops block. ISAME: lw I_STAT into t1, n of them from word w of the unit's first line;
 * IDIFF: into t1 and t2 alternately. The rest of the unit nops.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: \`node hwtests/run.js io-chain\`.
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
    printf("${NL}cpu/io-chain${NL}");
    printf("Cold I_STAT chains: n loads back to back from word w of the line, one register (ISAME) or two (IDIFF).${NL}");
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
fs.writeFileSync(path.join(__dirname, 'Makefile'), 'TARGET = io-chain.elf\n\ninclude ../../common-test.mk\n');
console.log('wrote main.cpp,', rows.length, 'rows');
