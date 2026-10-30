'use strict';
// Writes main.cpp beside it: a DRAM refresh met by WARM code's RAM reads (every instruction cached) at every phase from
// it - runs of RAM loads k nops apart (k = 0, 1, 2, 3, 4, 6, 8), cpu/access-time's own RAM loop, a load and its reader,
// and uncached nops (every fetch a RAM read). refresh-cold's twin: same harness, same calibration, no evictor.
// node hwtests/refresh-warm/gen.js, then node hwtests/build.js refresh-warm.
const fs = require('fs'), path = require('path');
const LW = 'lw $t5, 0($s3)';
const rep = (n, ...ins) => { const o = []; for (let i = 0; i < n; i++) o.push(...ins); return o; };
const nops = (k) => rep(k, 'nop');
// [name, what, instructions (the block's body; 'jr $ra' + 'nop' are added), uncached?]
const CASES = [
  ['K0', '24 x lw RAM, back to back', rep(24, LW)],
  ['K1', '24 x (lw RAM; 1 nop)', rep(24, LW, ...nops(1))],
  ['K2', '24 x (lw RAM; 2 nops)', rep(24, LW, ...nops(2))],
  ['K3', '24 x (lw RAM; 3 nops)', rep(24, LW, ...nops(3))],
  ['K4', '24 x (lw RAM; 4 nops)', rep(24, LW, ...nops(4))],
  ['K6', '18 x (lw RAM; 6 nops)', rep(18, LW, ...nops(6))],
  ['K8', '14 x (lw RAM; 8 nops)', rep(14, LW, ...nops(8))],
  ['AT', 'access-time\'s RAM loop, 24 turns (nop; addiu; lbu RAM; bnez; nop)', ['li $t6, 24', '1:', 'nop', 'addiu $t6, $t6, -1', 'lbu $t5, 0($s3)', 'bnez $t6, 1b', 'nop']],
  ['LU', '24 x (lw RAM; addu its value; 2 nops)', rep(24, LW, 'addu $t7, $t5, $t5', ...nops(2))],
  ['UNC', '32 nops, uncached (KSEG1)', nops(32), true],
];
const SLOTS = 7;
const PAGES = Math.ceil(CASES.length / SLOTS);
const NL = '\\n';
const q = (s) => `    "${s}${NL}"\n`;
function block(i, body) {
  const ins = [...body, 'jr $ra', 'nop'];
  if (ins.filter((x) => !/:$/.test(x)).length > 128) throw new Error(`case ${i} is ${ins.length} instructions, over its 512-byte slot`);
  let s = q(`.global rwB${i}`) + q(`rwB${i}:`);
  for (const x of ins) s += q(x);
  return s;
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
asm += q('.balign 512') + q('nop') + q('.set pop');

const decl = CASES.map((_, i) => `void rwB${i}(void);`).join(' ');
const tab = CASES.map(([, , , unc], i) => (unc ? `(Fn)((uint32_t)rwB${i} | 0xA0000000u)` : `rwB${i}`)).join(', ');
const names = CASES.map(([n, w]) => `"${(n + '   ').slice(0, 4)} ${w}"`).join(',\n    ');
const caseList = CASES.map(([n, w]) => ` *   ${(n + '    ').slice(0, 5)}${w}`).join('\n');

const src = `/*
 * cpu/refresh-warm - what a DRAM refresh costs WARM code's RAM reads, at every phase from it: loads k nops apart, the
 * access-time test's own RAM loop, a load and its reader, and uncached code (2026-10-11)
 *
 * WHY. hwtests/refresh-cold (the SCPH-5501 x3) settled the refresh against cold code's fills: due by a fill's request it
 * goes first, else after the burst's last data word, and the access it held starts a cycle after its 5. Against WARM RAM
 * reads only refresh-io's single read on an idle bus is measured (16..11 over six phases: the +1) - and a run of reads,
 * where each refresh may meet the bus busy with the read before, is what cpu/access-time's RAM loops are, and what any
 * loop locks to: a scratch core with the +1 (and the refresh in the last cycle of a busy read, which load-fill's uncached
 * nops ask for) reads access-time's rows at the same value at every refresh phase where the consoles' runs at free
 * phases differ, and its mean rows off by a refresh's 5-6 cycles. This measures the run directly.
 *
 * WHAT IT DOES. refresh-cold's harness: interrupts off, Timer 2 free-running at the system clock; the calibration
 * (\`lhu T2; jalr; lw RAM; jr; lhu T2\`, F = the first phase of the highest cost); then each case's block from one warm
 * slot (run once untimed, so every line is cached - no evictor), timed between two Timer 2 reads at every phase, 16
 * trials a target. Each block is shorter than 256 cycles, so at most one refresh falls in it.
 *   cases:
${caseList}
 * Each prints its commonest cost, then the cost by x = (F - the first Timer 2 read's phase) mod 256 - how many cycles
 * into the window the refresh comes due - sixteen to a line ('.' where no trial landed).
 *
 * READ IT LIKE THIS. A row is flat where the refresh falls outside the block. Inside, it repeats with the block's
 * period: each phase's cost is what a refresh costs a run of reads arriving there - 6 at the most on an idle bus
 * (refresh-io), less where it can start inside the read before (a busy bus). K0..K8 map that against the gap between
 * reads; AT is access-time's own loop; UNC is uncached code's fetches.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: \`node hwtests/run.js refresh-warm --frames=3000 --quiet=600\`.
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
}

__asm__(
${asm});

typedef void (*Fn)(void);
static const Fn BLK[NC] = { ${tab} };
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
    printf("\\ncpu/refresh-warm\\n");
    printf("What a DRAM refresh costs warm code's RAM reads at each phase from it (Timer 2 cycles).\\n");
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
        rcSweep(BLK[i], 0, hist, 16);
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
fs.writeFileSync(path.join(__dirname, 'Makefile'), 'TARGET = refresh-warm.elf\n\ninclude ../../common-test.mk\n');
console.log('wrote main.cpp: ' + CASES.length + ' cases, ' + PAGES + ' block pages');
