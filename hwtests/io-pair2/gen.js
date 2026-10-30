'use strict';
// Writes main.cpp beside it: load-fill5's I_STAT pair shapes I3 I+3 and I0 I+2 (the second load at word 2 of a line,
// coupled three and two back) with each kind of instruction after the second load, a not-taken branch at the line's
// first word or not, and cold-bracket's own shape (a JOY_STAT byte read, then Timer 2's counter and mode).
// node hwtests/io-pair2/gen.js, then node hwtests/build.js io-pair2.
const fs = require('fs'), path = require('path');
const rows = [];   // [name, unit as 16 tokens]
const unit = (toks) => { const t = toks.slice(); while (t.length < 16) t.push('n'); if (t.length !== 16) throw new Error('unit ' + toks.join(' ')); return t; };
// I3 I+3: first load at line A word 3; second at line B word 2 (three on), X after it at B word 3; B word 0 a nop or a not-taken branch
const i3 = (first, second, x, br) => unit(['n', 'n', 'n', first, br ? 'B' : 'n', 'n', second, x]);
// I0 I+2: first at word 0, second at word 2, X at word 3
const i0 = (first, second, x) => unit([first, 'n', second, x]);
// I1 I+1: first at word 1, second at word 2, X at word 3
const i1 = (first, second, x) => unit(['n', first, second, x]);
for (const x of ['n', 'I3', 'R', 'S', 'A', 'M']) rows.push([`I3 I+3 ${x}`, i3('I1', 'I2', x, false)]);
for (const x of ['n', 'I3', 'M']) rows.push([`I3 I+3 br ${x}`, i3('I1', 'I2', x, true)]);
for (const x of ['n', 'M', 'I3']) rows.push([`J3 T+3 ${x}`, i3('J', 'T', x, false)]);
for (const x of ['n', 'M', 'I3']) rows.push([`J3 T+3 br ${x}`, i3('J', 'T', x, true)]);
for (const x of ['n', 'M']) rows.push([`I3 T+3 br ${x}`, i3('I1', 'T', x, true)]);
for (const x of ['n', 'M']) rows.push([`J3 I+3 br ${x}`, i3('J', 'I2', x, true)]);
for (const x of ['n', 'I3', 'R', 'S', 'A', 'M']) rows.push([`I0 I+2 ${x}`, i0('I1', 'I2', x)]);
for (const x of ['n', 'M']) rows.push([`J0 T+2 ${x}`, i0('J', 'T', x)]);
for (const x of ['n', 'I3', 'R', 'S', 'M']) rows.push([`I1 I+1 ${x}`, i1('I1', 'I2', x)]);
// %1 = a RAM word buffer, %2 = I_STAT (BF801070h): JOY_STAT is %2 - 2Ch, Timer 2's counter %2 + B0h, its mode %2 + B4h
const ASM = { n: 'nop', I1: 'lw $t1, 0(%2)', I2: 'lw $t2, 0(%2)', I3: 'lw $t3, 0(%2)', R: 'lw $t3, 8(%1)', S: 'sw $zero, 16(%1)',
  A: 'addu $t4, $t5, $t5', J: 'lbu $a0, -0x2c(%2)', T: 'lhu $t2, 0xb0(%2)', M: 'lhu $t3, 0xb4(%2)', B: 'bne $zero, $zero, 1f' };
const NL = '\\n';   // a C string's newline escape
let blocks = '';
rows.forEach(([name, toks], i) => {
  // a not-taken branch's label sits right after its delay slot, so the two paths are the same code
  const asm = []; toks.forEach((t, k) => { asm.push(ASM[t]); if (toks[k - 1] === 'B') asm.push('1:'); });
  blocks += `BLOCK(b${i}, ${asm.map((a) => '"' + a + NL + '"').join(' ')}) /* ${name} */\n`;
});
const src = `/*
 * cpu/io-pair2 - load-fill5's I_STAT pairs I3 I+3 and I0 I+2 with each kind of instruction after the second load, a
 * not-taken branch at the line's first word or not, and cold-bracket's own shape (2026-10-11)
 *
 * WHY. v0.377.77 (src/psx.js, "A LOAD BEHIND AN ON-DIE READ") charges a load behind an on-die read one cycle more at
 * word 2 or 3 of its line when the next line is not cached (CB_IPW): load-fill5's I1 I+1, I0 I+2 and I3 I+3 read 3 4 4
 * on the 9001, 2 3 3 without it. But cold-bracket's joy1 w2 - its counter read at word 2, three behind a JOY_STAT read
 * in the line before, a Timer 2 mode read after it and a not-taken bnez at the line's word 0 - reads 20 on the
 * consoles, 21 with that cycle. The shapes differ in what follows the second load, the branch, and which registers are
 * read. Each is varied here alone.
 *
 * WHAT IT DOES. As load-fill5: 128 units of 16 instructions (four lines, twice the cache, so every line misses), Timer
 * 2, interrupts off, fastest..slowest of four passes, each row a unit's cost above the nops block. Names: the first load
 * and its word (I an I_STAT word load, J a JOY_STAT byte load, T Timer 2's counter, halfword), then "+d" the second d
 * on, then "br" when the second load's line starts with a not-taken bne, then what follows the second load: n a nop, I3
 * another I_STAT load (a third register), R a RAM load, S a RAM store, A an addu, M Timer 2's mode (halfword).
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: \`node hwtests/run.js io-pair2\`.
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
    __asm__ volatile(HEAD ".rept 128${NL}" __VA_ARGS__ ".endr${NL}" TAIL : "=&r"(r) : "r"(p), "r"(q) : "t1", "t2", "t3", "t4", "t5", "t7", "t8", "t9", "a0", "memory"); \\
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
    printf("${NL}cpu/io-pair2${NL}");
    printf("I_STAT pairs at word 2 with what follows, a not-taken branch, and cold-bracket's JOY/Timer 2 shape.${NL}");
    printf("Timer 2, fastest..slowest of four. There are no assertions - compare against your emulator.${NL}${NL}");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    const Span base = time4(nops);
    for (int i = 0; i < ${rows.length}; i++) {
        const Span s = time4(B[i]);
        const int d = (int)s.lo - (int)base.lo;
        const int a = d < 0 ? -d : d, per = (a * 100 + 64) / 128;
        printf("  %-16s %6u..%-6u  above the nops %6d  ->  %s%d.%02d a unit${NL}", NAME[i], (unsigned)s.lo, (unsigned)s.hi, d, d < 0 ? "-" : "", per / 100, per % 100);
    }
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("${NL}Done.${NL}");
    for (;;) {}
}
`;
fs.writeFileSync(path.join(__dirname, 'main.cpp'), src);
console.log(rows.length, 'rows');
