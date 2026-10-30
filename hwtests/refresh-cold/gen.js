'use strict';
// Writes main.cpp beside it: a DRAM refresh met by code that misses the instruction cache, at every phase from it - a
// 20-line block of nops, and the same block with a RAM load, a load and its reader, two loads, a store, a store and a
// load, two stores, an I_STAT read, a load two lines from a load, three loads, in its ninth line.
// node hwtests/refresh-cold/gen.js, then node hwtests/build.js refresh-cold.
const fs = require('fs'), path = require('path');
const LINES = 20, AT = 8;   // a block's lines; the line the cases change
// [name, what, [[line, word, asm], ...]]: s3 the RAM words (rcRam), t4 1F800000h; the loads go to t5..t7
const L = (w, off, reg = '$t5') => [AT, w, `lw ${reg}, ${off}($s3)`];
const CASES = [
  ['N', 'nops only', []],
  ['L1', 'lw RAM at word 1', [L(1, 0)]],
  ['L3', 'lw RAM at word 3', [L(3, 0)]],
  ['LU', 'lw RAM at word 1, its reader at word 2', [L(1, 0), [AT, 2, 'addu $t6, $t5, $t5']]],
  ['LL', 'lw RAM at words 1 and 2 (one page)', [L(1, 0), L(2, 4, '$t6')]],
  ['LX', 'lw RAM at word 1 and two lines on', [L(1, 0), [AT + 2, 1, 'lw $t6, 4($s3)']]],
  ['LLL', 'lw RAM at words 0, 1 and 2', [L(0, 0), L(1, 4, '$t6'), L(2, 8, '$t7')]],
  ['S1', 'sw RAM at word 1', [[AT, 1, 'sw $zero, 16($s3)']]],
  ['SS', 'sw RAM at words 1 and 2', [[AT, 1, 'sw $zero, 16($s3)'], [AT, 2, 'sw $zero, 20($s3)']]],
  ['SL', 'sw RAM at word 1, lw RAM at word 3', [[AT, 1, 'sw $zero, 16($s3)'], L(3, 0)]],
  ['I1', 'lw I_STAT at word 1', [[AT, 1, 'lw $t5, 0x1070($t4)']]],
  ['IU', 'lw I_STAT at word 1, its reader at word 2', [[AT, 1, 'lw $t5, 0x1070($t4)'], [AT, 2, 'addu $t6, $t5, $t5']]],
];
const SLOTS = 7;   // block slots a 4 KB page (512 bytes each); slot 7 of the first page is the warm code
const PAGES = Math.ceil(CASES.length / SLOTS);
const NL = '\\n';
const q = (s) => `    "${s}${NL}"\n`;
const nops = (n) => (n <= 0 ? '' : n === 1 ? q('nop') : q(`.rept ${n}`) + q('nop') + q('.endr'));

function block(i, edits) {
  const toks = new Array(LINES * 4).fill('nop');
  for (const [ln, w, a] of edits) toks[ln * 4 + w] = a;
  toks[(LINES - 1) * 4 + 2] = 'jr $ra';   // the last line: its word 2 returns, word 3 the delay slot
  let s = q(`.global rcB${i}`) + q(`rcB${i}:`), run = 0;
  for (const t of toks) { if (t === 'nop') { run++; continue; } s += nops(run) + q(t); run = 0; }
  return s + nops(run);
}

let asm = q('.text') + q('.set push') + q('.set noreorder') + q('.set noat');
for (let p = 0; p < PAGES; p++) {
  asm += q('.balign 4096');
  for (let k = 0; k < SLOTS; k++) {
    const i = p * SLOTS + k;
    asm += q('.balign 512');
    if (i < CASES.length) asm += block(i, CASES[i][2]);
    else asm += q('nop');
  }
  asm += q('.balign 512');
  if (p === 0) asm += fs.readFileSync(path.join(__dirname, 'warm.asm.txt'), 'utf8').split('\n').filter((l) => l.trim()).map((l) => q(l.replace(/"/g, '\\"'))).join('');
  else asm += q('nop');
}
// the evictors: slot k of this page runs 31 lines of nops and returns from its 32nd, taking every line of a block in
// slot k out of the cache (they share its 32 cache lines) and nothing else
asm += q('.balign 4096');
for (let k = 0; k < SLOTS; k++) asm += q('.balign 512') + q(`.global rcE${k}`) + q(`rcE${k}:`) + nops(124) + q('jr $ra') + q('nop') + nops(2);
asm += q('.balign 512') + q('nop') + q('.set pop');

const decl = CASES.map((_, i) => `void rcB${i}(void);`).join(' ');
const tab = CASES.map((_, i) => `rcB${i}`).join(', ');
const ev = CASES.map((_, i) => `rcE${i % SLOTS}`).join(', ');
const names = CASES.map(([n, w]) => `"${(n + '   ').slice(0, 4)} ${w}"`).join(',\n    ');
const caseList = CASES.map(([n, w]) => ` *   ${(n + '    ').slice(0, 5)}${w}`).join('\n');

const src = `/*
 * cpu/refresh-cold - what a DRAM refresh costs code that misses the instruction cache, at every phase from it: line
 * fills alone, and a RAM load, a store or an I_STAT read in the stream (2026-10-11)
 *
 * WHY. The stream (src/psx.js, "IN CODE THAT MISSES THE INSTRUCTION CACHE THE BUS IS A QUEUE", v0.377.75) leaves the
 * refresh out of its queue: one that comes due during the stream waits for the next line fill's grant (busGrant), which
 * charges it 5 cycles there whatever the bus was doing (CB_REF, the refresh inside the queue, is off: on it read worse).
 * v0.377.78's DRAM page mode (STATUS 2026-10-11 afternoon) moved cputest's access-time - two one-cycle rows, SCRATCHPAD
 * 8 and CACHECTRL 32, five cycles each - by shifting where a refresh meets its measure loop's first fills, and the refresh
 * rule there decides it. hwtests/line-fill reads fills 9.12-9.28 cycles apart a line (9 and the refreshes) - an average.
 * hwtests/refresh-io measures a refresh against warm code only. This measures it against cold code, phase by phase.
 *
 * WHAT IT DOES. Interrupts off; Timer 2 at the system clock, free-running (its count mod 256 keeps the 256-cycle refresh
 * grid's phase). CALIBRATION as refresh-io: \`lhu T2; jalr; lw RAM; jr; lhu T2\` (warm) at every phase; F = the first
 * phase of its highest cost (the RAM read waits for the refresh). Then each case: a block of ${LINES} cache lines, all
 * nops but what the case puts in its line ${AT} (the ninth) and the return in its last, each block in its own 512-byte
 * slot of a 4 KB page. A trial runs the evictor for the block's slot (32 lines of nops in another page sharing its cache
 * lines - nothing else leaves the cache), spins on Timer 2 to a target phase, runs 0..7 nops, reads Timer 2, calls the
 * block (every line a miss) and reads Timer 2 again when it returns - all of it from one warm slot, so the only bus
 * traffic in the timed window is the block's. 16 trials at each of the 256 targets.
 *   cases (line ${AT} of the block):
${caseList}
 * Each prints its commonest cost, then the cost (the commonest of the trials that landed there; '.' where none did) by
 * x = (F - the first Timer 2 read's phase) mod 256 - x is how many cycles into the window the refresh comes due, so
 * the rows read in the block's own order - sixteen to a line.
 *
 * READ IT LIKE THIS. Where the refresh falls outside the block (x past its cost) a row is flat. Inside, N gives what a
 * refresh costs a run of fills at each place against the fill's nine cycles (5 everywhere if it always waits for the
 * next fill and holds it; less where it fits the bus's idle cycles between one fill's burst and the next request). The
 * other rows minus N, near line ${AT}'s time (x about ${AT * 9}), say where the stream's read or store goes against it.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: \`node hwtests/run.js refresh-cold --frames=3000 --quiet=600\`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE 0x1F801124u
#define NC ${CASES.length}

extern "C" {
uint32_t rcRam[64] __attribute__((aligned(64)));
void rcSweep(void (*blk)(void), void (*ev)(void), uint8_t* hist, int reps);
void rcCalib(void);
${decl}
void rcE0(void); void rcE1(void); void rcE2(void); void rcE3(void); void rcE4(void); void rcE5(void); void rcE6(void);
}

__asm__(
${asm});

typedef void (*Fn)(void);
static const Fn BLK[NC] = { ${tab} };
static const Fn EV[NC] = { ${ev} };
static const char* const NAME[NC] = {
    ${names}
};
static uint8_t hist[256 * 512];     /* [phase][cost] trial counts */

static void clear() { for (int i = 0; i < 256 * 512; i++) hist[i] = 0; }
static int modeAt(int ph) {
    const uint8_t* h = &hist[(ph & 0xFF) * 512];
    int best = -1, bn = 0;
    for (int c = 0; c < 512; c++) if (h[c] > bn) { bn = h[c]; best = c; }
    return best;
}
static int modeAll() {
    static uint32_t tot[512];
    for (int c = 0; c < 512; c++) tot[c] = 0;
    for (int p = 0; p < 256; p++) for (int c = 0; c < 512; c++) tot[c] += hist[p * 512 + c];
    int best = 0;
    for (int c = 1; c < 512; c++) if (tot[c] > tot[best]) best = c;
    return best;
}

int main() {
    printf("\\ncpu/refresh-cold\\n");
    printf("What a DRAM refresh costs cold code (every line a miss) at each phase from it (Timer 2 cycles).\\n");
    printf("No assertions - compare against your emulator.\\n\\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);

    clear();
    rcSweep(rcCalib, 0, hist, 16);
    const int base = modeAll();
    int F = 0, hi = -1;
    for (int p = 0; p < 256; p++) { const int m = modeAt(p); if (m > hi) { hi = m; F = p; } }
    printf("CALIBRATION  lhu T2; jalr; lw RAM; jr; lhu T2: commonest %d, highest %d first at phase %d (F)\\n", base, hi, F);
    printf("  F-8 .. F+15:");
    for (int k = -8; k < 16; k++) { const int m = modeAt(F + k); if (m < 0) printf(" ."); else printf(" %d", m); }
    printf("\\n\\n");

    for (int i = 0; i < NC; i++) {
        clear();
        rcSweep(BLK[i], EV[i], hist, 16);
        printf("%s  commonest %d; cost by x = 0 .. 255:\\n", NAME[i], modeAll());
        for (int x = 0; x < 256; x++) {
            const int m = modeAt(F - x);
            if (m < 0) printf("    ."); else printf(" %4d", m);
            if (x % 16 == 15) printf("\\n");
        }
    }

    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\\nDone.\\n");
    for (;;) {}
}
`;
fs.writeFileSync(path.join(__dirname, 'main.cpp'), src);
fs.writeFileSync(path.join(__dirname, 'Makefile'), 'TARGET = refresh-cold.elf\n\ninclude ../../common-test.mk\n');
console.log('wrote main.cpp: ' + CASES.length + ' cases, ' + PAGES + ' block pages');
