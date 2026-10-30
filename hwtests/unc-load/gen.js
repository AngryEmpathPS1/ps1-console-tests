'use strict';
// Writes main.cpp beside it: RAM loads (and their neighbours) in UNCACHED code - alone, in pairs at each distance, in
// runs, with a reader at each distance, beside I_STAT loads and stores.
// node hwtests/unc-load/gen.js, then node hwtests/build.js unc-load.
const fs = require('fs'), path = require('path');
const rows = [];   // [name, unit as 16 tokens]
const unit = (toks) => { const t = toks.slice(); while (t.length < 16) t.push('n'); if (t.length !== 16) throw new Error('unit ' + toks.join(' ')); return t; };
const at = (first, d, second) => { const t = [first]; for (let i = 1; i < d; i++) t.push('n'); t.push(second); return unit(t); };
rows.push(['L', unit(['L1'])]);
for (const d of [1, 2, 3, 4, 5, 6, 8]) rows.push([`L L+${d}`, at('L1', d, 'L2')]);
rows.push(['LL', unit(['L1', 'L2'])]);
rows.push(['4L', unit(['L1', 'L2', 'L1', 'L2'])]);
rows.push(['8L', unit(['L1', 'L2', 'L1', 'L2', 'L1', 'L2', 'L1', 'L2'])]);
rows.push(['16L', unit(Array(16).fill(0).map((_, i) => (i & 1 ? 'L2' : 'L1')))]);
for (const d of [1, 2, 3, 4]) rows.push([`L U+${d}`, at('L1', d, 'U1')]);
rows.push(['I', unit(['I1'])]);
for (const d of [1, 2, 3]) rows.push([`L I+${d}`, at('L1', d, 'I1')]);
for (const d of [1, 2, 3]) rows.push([`I L+${d}`, at('I1', d, 'L1')]);
rows.push(['S', unit(['S'])]);
for (const d of [1, 2, 3]) rows.push([`S L+${d}`, at('S', d, 'L1')]);
for (const d of [1, 2]) rows.push([`L S+${d}`, at('L1', d, 'S')]);
rows.push(['SS', unit(['S', 'S2'])]);
const ASM = { n: 'nop', L1: 'lw $t1, 0(%1)', L2: 'lw $t2, 4(%1)', I1: 'lw $t1, 0(%2)', S: 'sw $zero, 16(%1)', S2: 'sw $zero, 20(%1)', U1: 'addu $t3, $t1, $t1' };
const NL = '\\n';   // a C string's newline escape
let blocks = '';
rows.forEach(([name, toks], i) => {
  blocks += `BLOCK(b${i}, ${toks.map((t) => '"' + ASM[t] + NL + '"').join(' ')}) /* ${name} */\n`;
});
const src = `/*
 * cpu/unc-load - a RAM load in UNCACHED code: alone, in pairs and runs, with its reader, beside I_STAT loads and stores
 * (2026-10-12 night)
 *
 * WHY. Two measurements part. nocash's DIAG (MEM ACCESS TIMING, tests/nocash-diag/DIAG.A22 diag_mem_access_timing) times
 * sixteen \`lw\` from one RAM word back to back in uncached code and wants 0Ch cycles each - this core gives 12. hwtests/
 * load-fill's UNCACHED row times ONE \`lw\` among fifteen nops: 4.30 cycles over the nops on every one of four
 * consoles, where this core charges 6.09 (12 less a nop's 6). Both can hold - a lone load's bus access overlapping the
 * fetches around it where a run of loads cannot - but no row says how. This times the shapes between the two.
 *
 * WHAT IT DOES. load-fill's method run from KSEG1 (every instruction a RAM read): 128 units of 16 instructions, Timer 2
 * at the system clock, interrupts off, fastest..slowest of four passes, each row a unit's cost above the nops block run
 * the same way. Names: L a RAM load (t1 / t2, two words of a buffer), I an I_STAT load into t1, S a store (a word no load
 * reads), U a reader of t1 (\`addu t3, t1, t1\`); "+d" the second instruction d after the first; LL, 4L, 8L, 16L loads
 * back to back.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: \`node hwtests/run.js unc-load\`.
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
static Block uncached(Block f) { return (Block)(((uint32_t)f & 0x1FFFFFFFu) | 0xA0000000u); }

struct Span { uint32_t lo, hi; };
static Span time4(Block f) {
    Span s = { 0xFFFFFFFFu, 0 };
    for (int k = 0; k < 4; k++) { const uint32_t t = f((uint32_t)&ramWords[0], 0xBF801070u); if (t < s.lo) s.lo = t; if (t > s.hi) s.hi = t; }
    return s;
}

int main() {
    printf("${NL}cpu/unc-load${NL}");
    printf("RAM loads in uncached code (KSEG1): alone, in pairs at each distance, in runs, with a reader, beside I_STAT${NL}");
    printf("loads and stores. Timer 2, fastest..slowest of four. There are no assertions - compare against your emulator.${NL}${NL}");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    const Span base = time4(uncached(nops));
    printf("  %-14s %6u..%-6u${NL}", "NOPS", (unsigned)base.lo, (unsigned)base.hi);
    for (int i = 0; i < ${rows.length}; i++) {
        const Span s = time4(uncached(B[i]));
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
console.log(rows.length, 'rows');
